/* Rewind (ChaOS). See rewind.h. */
#include "rewind.h"

#include <pthread.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool     g_on;
static uint8_t *g_cur;          /* the newest state, whole */
static uint8_t *g_tmp;          /* the state being taken */
static size_t   g_size;         /* serialize_size when they were made */
static unsigned g_count;        /* frames since the last save */
static bool     g_have;         /* g_cur holds a state */

/* The ring: entries back to back, each [u32 length][coded bytes][u32 length]
 * so it can be walked from either end. head is where the next goes, tail the
 * oldest; used how much is in it. */
static uint8_t *g_ring;
static size_t   g_head, g_tail, g_used;
static unsigned g_entries;

static void rewind_idle(void);

void rewind_set(bool on)
{
	rewind_idle();
	g_on = on;
	if (!on) {
		free(g_ring); free(g_cur); free(g_tmp);
		g_ring = g_cur = g_tmp = NULL;
		g_size = 0;
	}
	rewind_reset();
	fprintf(stderr, "diatom: rewind %s\n", on ? "on" : "off");
}

bool rewind_on(void) { return g_on; }

void rewind_reset(void)
{
	rewind_idle();
	g_head = g_tail = g_used = 0;
	g_entries = 0;
	g_count = 0;
	g_have = false;
}

unsigned rewind_depth(void) { return g_entries; }

/* Ring access, wrapping */
static void ring_put(const void *src, size_t n)
{
	const uint8_t *s = src;
	while (n) {
		size_t k = REWIND_BUDGET - g_head;
		if (k > n) k = n;
		memcpy(g_ring + g_head, s, k);
		g_head = (g_head + k) % REWIND_BUDGET;
		s += k; n -= k;
	}
}

static void ring_get(size_t at, void *dst, size_t n)
{
	uint8_t *d = dst;
	while (n) {
		size_t k = REWIND_BUDGET - at;
		if (k > n) k = n;
		memcpy(d, g_ring + at, k);
		at = (at + k) % REWIND_BUDGET;
		d += k; n -= k;
	}
}

static void drop_oldest(void)
{
	uint32_t len;
	ring_get(g_tail, &len, 4);
	g_tail = (g_tail + 8 + len) % REWIND_BUDGET;
	g_used -= 8 + len;
	g_entries--;
}

/* XOR of two states, coded as pairs of [u16 zeros][u16 literals][literals],
 * into out (at most a little over n); returns its length */
static size_t encode(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out)
{
	size_t i = 0, o = 0;

	while (i < n) {
		size_t z = 0, l = 0, s;
		/* ChaOS: the unchanged stretch, eight bytes at a time while it lasts:
		 * most of a state is the same from one save to the next, and the
		 * byte loop over half a megabyte of mGBA's cost a frame its audio */
		for (;;) {
			uint64_t wa, wb;

			if (i + z + 8 > n || z + 8 > 65535) break;
			memcpy(&wa, a + i + z, 8);
			memcpy(&wb, b + i + z, 8);
			if (wa != wb) break;
			z += 8;
		}
		while (i + z < n && z < 65535 && a[i + z] == b[i + z]) z++;
		i += z;
		s = i;
		while (i + l < n && l < 65535 && a[i + l] != b[i + l]) l++;
		out[o++] = (uint8_t)z; out[o++] = (uint8_t)(z >> 8);
		out[o++] = (uint8_t)l; out[o++] = (uint8_t)(l >> 8);
		for (size_t k = 0; k < l; k++) out[o++] = a[s + k] ^ b[s + k];
		i += l;
	}
	return o;
}

static void decode_into(uint8_t *cur, size_t n, const uint8_t *in, size_t len)
{
	size_t i = 0, o = 0;

	while (o + 4 <= len && i <= n) {
		size_t z = in[o] | (size_t)in[o + 1] << 8;
		size_t l = in[o + 2] | (size_t)in[o + 3] << 8;
		o += 4;
		i += z;
		for (size_t k = 0; k < l && i < n && o < len; k++) cur[i++] ^= in[o++];
	}
}

static uint8_t *g_code;          /* scratch for one coded entry */

static bool buffers(size_t size)
{
	if (size == g_size && g_ring) return true;
	free(g_cur); free(g_tmp); free(g_code);
	g_cur = malloc(size);
	g_tmp = malloc(size);
	/* The worst case: every byte a literal, a 4-byte header per 65535 */
	g_code = malloc(size + 4 * (size / 65535 + 2) + 16);
	if (!g_ring) g_ring = malloc(REWIND_BUDGET);
	if (!g_cur || !g_tmp || !g_code || !g_ring) {
		fprintf(stderr, "diatom: rewind: no memory for %zu-byte states\n", size);
		free(g_cur); free(g_tmp); free(g_code); free(g_ring);
		g_cur = g_tmp = g_code = g_ring = NULL;
		g_size = 0;
		g_on = false;
		return false;
	}
	g_size = size;
	rewind_reset();
	fprintf(stderr, "diatom: rewind: %zu-byte states, a save every %d frames\n",
	        size, REWIND_EVERY);
	return true;
}

/* ChaOS: the coding of a save into the ring, off the game's thread. Taking
 * the state (serialize) stays with the game, which owns the core; comparing it
 * with the last and filing the difference runs on another core of the CPU
 * while the next frames play. Everything here waits for it first (rewind_idle),
 * so the ring and the two states are only ever touched by one side. */
static pthread_t       g_worker;
static bool            g_worker_up, g_job;
static pthread_mutex_t g_wmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_wcv = PTHREAD_COND_INITIALIZER;

static void file_save(void)
{
	size_t len;
	uint32_t l32;

	/* The entry is what turns the new state back into the one before */
	len = encode(g_cur, g_tmp, g_size, g_code);
	if (len + 8 > REWIND_BUDGET / 2) return;         /* a state too unlike to keep */
	while (g_used + len + 8 > REWIND_BUDGET && g_entries) drop_oldest();
	l32 = (uint32_t)len;
	ring_put(&l32, 4);
	ring_put(g_code, len);
	ring_put(&l32, 4);
	g_used += len + 8;
	g_entries++;
	{ uint8_t *t = g_cur; g_cur = g_tmp; g_tmp = t; }
}

static void *worker(void *arg)
{
	pthread_mutex_lock(&g_wmx);
	for (;;) {
		while (!g_job) pthread_cond_wait(&g_wcv, &g_wmx);
		pthread_mutex_unlock(&g_wmx);
		file_save();
		pthread_mutex_lock(&g_wmx);
		g_job = false;
		pthread_cond_broadcast(&g_wcv);
	}
	return arg;
}

/* Until the save being filed is in the ring */
static void rewind_idle(void)
{
	if (!g_worker_up) return;
	pthread_mutex_lock(&g_wmx);
	while (g_job) pthread_cond_wait(&g_wcv, &g_wmx);
	pthread_mutex_unlock(&g_wmx);
}

void rewind_after_frame(diatom_core *c)
{
	size_t size;

	if (!g_on || !c || !c->serialize_size || !c->serialize) return;
	if (++g_count < REWIND_EVERY) return;
	/* The last save filed first. Three frames are fifty milliseconds and
	 * filing takes a few, so in play this never waits; it keeps every save
	 * exactly where rewind expects it. */
	rewind_idle();
	g_count = 0;
	size = c->serialize_size();
	if (!size || !buffers(size)) return;
	if (!c->serialize(g_tmp, size)) return;
	if (!g_have) {
		memcpy(g_cur, g_tmp, size);
		g_have = true;
		return;
	}
	if (!g_worker_up)
		g_worker_up = pthread_create(&g_worker, NULL, worker, NULL) == 0;
	if (!g_worker_up) { file_save(); return; }   /* no thread: as before */
	pthread_mutex_lock(&g_wmx);
	g_job = true;
	pthread_cond_signal(&g_wcv);
	pthread_mutex_unlock(&g_wmx);
}

bool rewind_step(diatom_core *c)
{
	uint32_t len;
	size_t end, start;

	rewind_idle();

	if (!g_on || !g_have || !g_entries || !c || !c->unserialize) return false;
	/* The newest entry ends at head */
	end = (g_head + REWIND_BUDGET - 4) % REWIND_BUDGET;
	ring_get(end, &len, 4);
	start = (g_head + REWIND_BUDGET - 8 - len) % REWIND_BUDGET;
	ring_get((start + 4) % REWIND_BUDGET, g_code, len);
	decode_into(g_cur, g_size, g_code, len);
	g_head = start;
	g_used -= 8 + len;
	g_entries--;
	g_count = 0;
	return c->unserialize(g_cur, g_size);
}
