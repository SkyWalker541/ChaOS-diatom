/* SPDX-License-Identifier: MPL-2.0 */
/* Cheats (ChaOS): do codes reach the core, and do memory cheats write what
 * RetroArch's would? Needs no core and no ROM. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "libretro.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t sysram[64];
static uint8_t a_ram[16], b_ram[16];

static void *mem_data(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? sysram : NULL; }
static size_t mem_size(unsigned id) { return id == RETRO_MEMORY_SYSTEM_RAM ? sizeof sysram : 0; }

static int  resets, sets;
static char last_code[256];
static void c_reset(void) { resets++; sets = 0; }
static void c_set(unsigned i, bool on, const char *code)
{
	(void)i;
	if (on) { sets++; snprintf(last_code, sizeof last_code, "%s", code); }
}

static const char *file(const char *text)
{
	static const char *p = "build/cheats_test.txt";
	FILE *f = fopen(p, "w");
	fputs(text, f);
	fclose(f);
	return p;
}

int main(void)
{
	diatom_core c;
	int codes, mem;

	memset(&c, 0, sizeof c);
	c.get_memory_data = mem_data;
	c.get_memory_size = mem_size;
	c.cheat_reset = c_reset;
	c.cheat_set = c_set;

	/* Codes go to the core, reset first */
	diatom_cheats_game(&c);
	diatom_cheats_load(&c, file("C\t7E1E6B14\nC\t7E1E5B14+7E1E5914\n"), &codes, &mem);
	CHECK(resets == 1 && sets == 2 && codes == 2 && mem == 0, "codes reach the core");
	CHECK(!strcmp(last_code, "7E1E5B14+7E1E5914"), "a code of several parts arrives whole");
	CHECK(diatom_cheats_on(), "cheats are on");
	diatom_cheats_load(&c, "", &codes, &mem);
	CHECK(resets == 2 && sets == 0 && !diatom_cheats_on(), "an empty path clears them");

	/* Memory: set an 8-bit value, every frame */
	memset(sysram, 0, sizeof sysram);
	diatom_cheats_load(&c, file("M\t5 3 1 99 0 0 1 1 0\n"), &codes, &mem);
	CHECK(mem == 1, "a memory cheat is taken");
	diatom_cheats_frame(&c);
	CHECK(sysram[5] == 99, "8-bit set: %d", sysram[5]);
	sysram[5] = 3;
	diatom_cheats_frame(&c);
	CHECK(sysram[5] == 99, "it holds, frame after frame");

	/* 16-bit, both byte orders */
	memset(sysram, 0, sizeof sysram);
	diatom_cheats_load(&c, file("M\t10 4 1 4660 0 0 1 1 0\nM\t20 4 1 4660 1 0 1 1 0\n"), NULL, NULL);
	diatom_cheats_frame(&c);
	CHECK(sysram[10] == 0x34 && sysram[11] == 0x12, "16-bit little-endian");
	CHECK(sysram[20] == 0x12 && sysram[21] == 0x34, "16-bit big-endian");

	/* Add, and a condition that guards the next cheat */
	memset(sysram, 0, sizeof sysram);
	sysram[1] = 10;
	sysram[2] = 7;
	diatom_cheats_load(&c, file("M\t1 3 2 5 0 0 1 1 0\nM\t2 3 4 8 0 0 1 1 0\nM\t3 3 1 42 0 0 1 1 0\nM\t4 3 1 1 0 0 1 1 0\n"), NULL, NULL);
	diatom_cheats_frame(&c);
	CHECK(sysram[1] == 15, "increase: %d", sysram[1]);
	CHECK(sysram[3] == 0, "a condition that fails skips the next cheat only");
	CHECK(sysram[4] == 1, "...and the one after runs");
	sysram[2] = 8;
	diatom_cheats_frame(&c);
	CHECK(sysram[3] == 42, "a condition that holds lets it run");

	/* Bits: only the masked ones change */
	memset(sysram, 0, sizeof sysram);
	sysram[7] = 0xF0;
	diatom_cheats_load(&c, file("M\t7 0 1 1 0 1 1 1 0\n"), NULL, NULL);
	diatom_cheats_frame(&c);
	CHECK(sysram[7] == 0xF1, "1-bit set touches its bit only: %02x", sysram[7]);

	/* Repeat: a run of bytes, the value stepping */
	memset(sysram, 0, sizeof sysram);
	diatom_cheats_load(&c, file("M\t30 3 1 1 0 0 4 1 1\n"), NULL, NULL);
	diatom_cheats_frame(&c);
	CHECK(sysram[30] == 1 && sysram[31] == 2 && sysram[32] == 3 && sysram[33] == 4 && sysram[34] == 0,
	      "repeat: %d %d %d %d %d", sysram[30], sysram[31], sysram[32], sysram[33], sysram[34]);

	/* Out of range: nothing written, nothing crashes */
	diatom_cheats_load(&c, file("M\t200 3 1 1 0 0 1 1 0\nM\t63 4 1 1 0 0 1 1 0\n"), NULL, NULL);
	diatom_cheats_frame(&c);
	CHECK(sysram[63] == 0, "a value off the end of memory is not written");

	/* A memory map: regions marked system RAM, end to end, in order */
	{
		struct retro_memory_descriptor d[3];
		struct retro_memory_map m = { d, 3 };
		memset(d, 0, sizeof d);
		d[0].flags = RETRO_MEMDESC_SYSTEM_RAM; d[0].ptr = a_ram; d[0].len = sizeof a_ram;
		d[1].flags = 0;                        d[1].ptr = sysram; d[1].len = sizeof sysram;
		d[2].flags = RETRO_MEMDESC_SYSTEM_RAM; d[2].ptr = b_ram; d[2].len = sizeof b_ram;
		diatom_cheats_note_map(&m);
		diatom_cheats_load(&c, file("M\t3 3 1 7 0 0 1 1 0\nM\t18 3 1 9 0 0 1 1 0\n"), NULL, NULL);
		diatom_cheats_frame(&c);
		CHECK(a_ram[3] == 7, "first region");
		CHECK(b_ram[2] == 9, "second system RAM region follows the first, skipping the rest");
	}

	diatom_cheats_game(&c);
	CHECK(!diatom_cheats_on(), "a new game starts without cheats");

	if (fails) { printf("cheats: %d failed\n", fails); return 1; }
	printf("ok: cheats reach the core, and memory cheats write as RetroArch's do\n");
	return 0;
}
