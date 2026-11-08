/* Rewind (ChaOS): a fake core whose state is a frame counter spread over a
 * big buffer, run forward, saved, then stepped back: each step must restore
 * exactly the state saved REWIND_EVERY frames before the last. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rewind.h"

#define SZ 300000
static unsigned char st[SZ];
static unsigned frame;

static void paint(void) { memset(st, 0, SZ); memcpy(st + 1000, &frame, 4); st[SZ - 1] = (unsigned char)frame; for (int i = 0; i < 50; i++) st[5000 + i * 97] = (unsigned char)(frame * i); }
static void run(void) { frame++; paint(); }
static size_t size(void) { return SZ; }
static bool ser(void *p, size_t n) { memcpy(p, st, n); return true; }
static bool unser(const void *p, size_t n) { memcpy(st, p, n); memcpy(&frame, st + 1000, 4); return true; }

int main(void)
{
	diatom_core c;
	int fails = 0, i;
	unsigned want;

	memset(&c, 0, sizeof c);
	c.run = run; c.serialize_size = size; c.serialize = ser; c.unserialize = unser;
	rewind_set(true);
	paint();
	for (i = 0; i < 300; i++) { run(); rewind_after_frame(&c); }
	printf("frame %u, %u steps held\n", frame, rewind_depth());
	/* saves were at frames 3, 6, ... 300; the newest is 300 */
	for (want = 297; rewind_step(&c); want -= 3) {
		if (frame != want || st[SZ - 1] != (unsigned char)want) { printf("FAIL: at %u, wanted %u\n", frame, want); fails++; break; }
	}
	if (want + 3 != 3) { printf("FAIL: stopped at %u\n", want + 3); fails++; }
	/* and play on after rewinding, then back again */
	for (i = 0; i < 30; i++) { run(); rewind_after_frame(&c); }
	if (!rewind_step(&c) || frame != 30) { printf("FAIL: after playing on, at %u\n", frame); fails++; }
	rewind_reset();
	if (rewind_step(&c)) { printf("FAIL: a reset ring still stepped\n"); fails++; }
	printf(fails ? "FAILED\n" : "ok: rewind restores every saved state, newest first\n");
	return fails != 0;
}
