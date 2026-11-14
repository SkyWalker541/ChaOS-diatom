/* SPDX-License-Identifier: MPL-2.0 */
/* Cheats (ChaOS). See cheats.h. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "libretro.h"

#define MAX_REGIONS 32
#define MAX_MEM     256

typedef struct {
	unsigned address, size, type, value, bitmask, repeat, add_address, add_value;
	bool     big_endian;
} mem_cheat;

static mem_cheat g_mem[MAX_MEM];
static int       g_nmem;
static int       g_ncodes;

/* The address space, as RetroArch's cheat search lays it out */
static struct { uint8_t *ptr; unsigned len; } g_map[MAX_REGIONS], g_reg[MAX_REGIONS];
static int      g_nmap, g_nreg;
static unsigned g_total;
static bool     g_regions_ready;

void diatom_cheats_note_map(const void *mmap_v)
{
	const struct retro_memory_map *m = mmap_v;
	unsigned i;

	g_nmap = 0;
	g_regions_ready = false;
	if (!m || !m->descriptors) return;
	for (i = 0; i < m->num_descriptors && g_nmap < MAX_REGIONS; i++) {
		const struct retro_memory_descriptor *d = &m->descriptors[i];
		/* The pointer as given, without the descriptor's offset, and its
		 * length: what RetroArch's cheat search reads, and therefore where
		 * the addresses in a .cht file point. */
		if ((d->flags & RETRO_MEMDESC_SYSTEM_RAM) && d->ptr && d->len) {
			g_map[g_nmap].ptr = d->ptr;
			g_map[g_nmap].len = (unsigned)d->len;
			g_nmap++;
		}
	}
}

static void regions(diatom_core *c)
{
	int i;

	g_regions_ready = true;
	g_nreg = 0;
	g_total = 0;
	for (i = 0; i < g_nmap; i++) {
		g_reg[g_nreg++] = g_map[i];
		g_total += g_map[i].len;
	}
	if (!g_nreg && c && c->get_memory_data && c->get_memory_size) {
		uint8_t *p = c->get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
		size_t   n = c->get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
		if (p && n) {
			g_reg[0].ptr = p;
			g_reg[0].len = (unsigned)n;
			g_nreg = 1;
			g_total = (unsigned)n;
		}
	}
}

/* Where `size` bytes from `address` are, if they lie wholly in one region */
static uint8_t *at(unsigned address, unsigned size)
{
	unsigned start = 0;
	int i;

	for (i = 0; i < g_nreg; i++) {
		if (address - start < g_reg[i].len) {
			if (size > g_reg[i].len - (address - start)) return NULL;
			return g_reg[i].ptr + (address - start);
		}
		start += g_reg[i].len;
	}
	return NULL;
}

static void layout(unsigned size, unsigned *bytes, unsigned *bits, unsigned *mask)
{
	switch (size) {
	case 0:  *bytes = 1; *bits = 1; *mask = 0x01; break;
	case 1:  *bytes = 1; *bits = 2; *mask = 0x03; break;
	case 2:  *bytes = 1; *bits = 4; *mask = 0x0F; break;
	case 4:  *bytes = 2; *bits = 8; *mask = 0xFFFF; break;
	case 5:  *bytes = 4; *bits = 8; *mask = 0xFFFFFFFFu; break;
	default: *bytes = 1; *bits = 8; *mask = 0xFF; break;
	}
}

static unsigned get(const uint8_t *p, unsigned bytes, bool be)
{
	switch (bytes) {
	case 2:  return be ? (unsigned)p[0] << 8 | p[1] : (unsigned)p[1] << 8 | p[0];
	case 4:  return be ? (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]
	                   : (unsigned)p[3] << 24 | (unsigned)p[2] << 16 | (unsigned)p[1] << 8 | p[0];
	default: return p[0];
	}
}

static void put(uint8_t *p, unsigned bytes, unsigned bits, unsigned bitmask, unsigned v, bool be)
{
	switch (bytes) {
	case 2:
		if (be) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
		else    { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
		break;
	case 4:
		if (be) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
		else    { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
		break;
	default:
		if (bits < 8) {
			/* Only the bits the cheat names, each from the same bit of v */
			unsigned b;
			uint8_t  x = p[0];
			for (b = 0; b < 8; b++)
				if (bitmask >> b & 1)
					x = (uint8_t)((x & ~(1u << b)) | ((v >> b & 1u) << b));
			p[0] = x;
		} else {
			p[0] = (uint8_t)v;
		}
		break;
	}
}

void diatom_cheats_frame(diatom_core *c)
{
	bool run_next = true;
	int i;

	if (!g_nmem) return;
	if (!g_regions_ready) regions(c);
	if (!g_nreg || !g_total) return;

	for (i = 0; i < g_nmem; i++) {
		const mem_cheat *k = &g_mem[i];
		unsigned bytes, bits, mask, cur, v, r, idx = k->address, bm = k->bitmask;
		uint8_t *p;
		bool set = false;

		if (!run_next) { run_next = true; continue; }
		layout(k->size, &bytes, &bits, &mask);
		p = at(idx, bytes);
		if (!p) {
			/* A condition that cannot be read does not hold */
			if (k->type >= 4) run_next = false;
			continue;
		}
		cur = get(p, bytes, k->big_endian);
		v = k->value;
		switch (k->type) {
		case 1: set = true; break;
		case 2: set = true; v = cur + k->value; break;
		case 3: set = true; v = cur - k->value; break;
		case 4: run_next = cur == k->value; break;
		case 5: run_next = cur != k->value; break;
		case 6: run_next = k->value < cur;  break;
		case 7: run_next = k->value > cur;  break;
		default: break;
		}
		if (!set) continue;

		for (r = 1; r <= k->repeat; r++) {
			p = at(idx, bytes);
			if (!p) break;
			put(p, bytes, bits, bm, v, k->big_endian);
			v += k->add_value;
			if (mask) v %= mask;
			if (bits < 8) {
				unsigned s;
				for (s = 0; s < k->add_address; s++) {
					bm = (bm << bits) & 0xFF;
					if (!bm) { bm = mask; idx++; }
				}
			} else {
				idx += k->add_address * bytes;
			}
			idx %= g_total;
		}
	}
}

void diatom_cheats_game(diatom_core *c)
{
	(void)c;
	g_nmem = 0;
	g_ncodes = 0;
	g_regions_ready = false;
}

bool diatom_cheats_on(void)
{
	return g_nmem > 0 || g_ncodes > 0;
}

int diatom_cheats_load(diatom_core *c, const char *path, int *codes, int *mem)
{
	char  line[1200];
	FILE *f;
	void (*reset)(void) = c ? c->cheat_reset : NULL;
	void (*set)(unsigned, bool, const char *) = c ? c->cheat_set : NULL;

	g_nmem = 0;
	g_ncodes = 0;
	g_regions_ready = false;
	if (reset) reset();

	if (path && path[0] && (f = fopen(path, "r"))) {
		while (fgets(line, sizeof line, f)) {
			char *nl = strpbrk(line, "\r\n");
			if (nl) *nl = '\0';
			if (line[0] == 'C' && line[1] == '\t' && line[2]) {
				if (set) set((unsigned)g_ncodes, true, line + 2);
				g_ncodes++;
			} else if (line[0] == 'M' && line[1] == '\t' && g_nmem < MAX_MEM) {
				mem_cheat k = { 0 };
				unsigned be = 0;
				if (sscanf(line + 2, "%u %u %u %u %u %u %u %u %u",
				           &k.address, &k.size, &k.type, &k.value, &be,
				           &k.bitmask, &k.repeat, &k.add_address, &k.add_value) == 9) {
					k.big_endian = be != 0;
					g_mem[g_nmem++] = k;
				}
			}
		}
		fclose(f);
	}
	if (codes) *codes = g_ncodes;
	if (mem) *mem = g_nmem;
	fprintf(stderr, "diatom: cheats: %d code%s, %d memory\n",
	        g_ncodes, g_ncodes == 1 ? "" : "s", g_nmem);
	return g_ncodes + g_nmem;
}
