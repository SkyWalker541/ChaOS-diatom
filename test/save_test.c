/* ChaOS: the game's battery save - kept from before the game, never written
 * over a copy changed outside the game, and written at once on request. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "diatom.h"

static unsigned char ram[1024];
static void *md(unsigned id) { (void)id; return ram; }
static size_t ms(unsigned id) { (void)id; return sizeof ram; }
void diatom_port_log(diatom_log_level l, const char *m) { (void)l; printf("  log: %s\n", m); }

/* what save.c's states reach for, not used here */
int diatom_env_disc_count(void) { return 0; }
int diatom_env_disc_index(void) { return 0; }
void diatom_cheevos_runtime_reset(void) {}

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } else printf("ok: %s\n", what); } while (0)

static int first_byte(const char *p)
{
	FILE *f = fopen(p, "rb"); int c;
	if (!f) return -1;
	c = fgetc(f); fclose(f); return c;
}

static void put(const char *p, int b)
{
	unsigned char buf[sizeof ram]; FILE *f = fopen(p, "wb");
	memset(buf, b, sizeof buf); fwrite(buf, 1, sizeof buf, f); fclose(f);
}

int main(void)
{
	char dir[] = "/tmp/savetestXXXXXX", srm[256], bak[300], out[300];
	diatom_core c;

	if (!mkdtemp(dir)) return 1;
	snprintf(srm, sizeof srm, "%s/Game.srm", dir);
	snprintf(bak, sizeof bak, "%s.bak", srm);
	snprintf(out, sizeof out, "%s.outside", srm);
	memset(&c, 0, sizeof c);
	c.get_memory_data = md; c.get_memory_size = ms;

	/* 1: a save there at the start is loaded, and kept as .bak on the first write */
	put(srm, 0x11);
	diatom_save_init(&c, dir, "/roms/Game.m3u");
	CHECK(ram[0] == 0x11, "save loaded into the game");
	ram[0] = 0x22;
	diatom_save_now(); usleep(200000);
	CHECK(first_byte(srm) == 0x22, "the change written at once on request");
	CHECK(first_byte(bak) == 0x11, "the copy from before the game kept as .bak");
	ram[0] = 0x33;
	diatom_save_now(); usleep(200000);
	CHECK(first_byte(bak) == 0x11, ".bak still the copy from before the game");

	/* 2: a copy dropped in while the game runs is kept, not lost */
	sleep(1);   /* past the mtime granularity */
	put(srm, 0x44);
	ram[0] = 0x55;
	diatom_save_now(); usleep(200000);
	CHECK(first_byte(out) == 0x44, "the copy changed outside kept as .outside");
	CHECK(first_byte(srm) == 0x55, "the game's own save written");
	diatom_save_shutdown();

	/* 3: no write without a change */
	unlink(bak);
	diatom_save_init(&c, dir, "/roms/Game.m3u");
	diatom_save_now(); usleep(200000);
	diatom_save_shutdown();
	CHECK(access(bak, F_OK) != 0, "nothing written, nothing kept, when the game saved nothing");

	/* 4: flush on exit backs up too */
	diatom_save_init(&c, dir, "/roms/Game.m3u");
	ram[0] = 0x66;
	diatom_save_shutdown();
	CHECK(first_byte(srm) == 0x66 && first_byte(bak) == 0x55, "exit flush writes, keeping the old one");

	/* 5: the second game of a session - Diatom stays resident between games,
	 * and its writer must start again for each */
	diatom_save_init(&c, dir, "/roms/Game.m3u");
	ram[0] = 0x77;
	diatom_save_tick(); diatom_save_now(); usleep(200000);
	CHECK(first_byte(srm) == 0x77, "a later game's save written while it runs");
	ram[0] = 0x78;
	usleep(1100000); diatom_save_tick(); usleep(200000);
	diatom_save_shutdown();
	CHECK(first_byte(srm) == 0x78, "and its last save there after it ends");

	printf(fails ? "save: %d FAILED\n" : "save: all passed\n", fails);
	return fails != 0;
}
