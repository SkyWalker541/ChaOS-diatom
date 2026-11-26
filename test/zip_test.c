/* ChaOS: diatom_zip_load on a .7z, as when a game starts from one, and on
 * a .zip made beside it, so the old way is seen to still hold. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diatom.h"

static int failures;
#define CHECK(c, ...) do { if (c) printf("ok: "); else { printf("FAIL: "); failures++; } printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void)
{
	static const char *const sevens[] = { "test/fixtures/lzma2.7z", "test/fixtures/bcj.7z", NULL };
	void *d = NULL, *z = NULL;
	size_t n = 0, zn = 0;
	char name[256];
	int k;

	CHECK(diatom_zip_is("Game.7z") && diatom_zip_is("Game.ZIP") && !diatom_zip_is("Game.gba"),
	      "a .7z and a .zip are both archives to open");
	for (k = 0; sevens[k]; k++) {
		CHECK(diatom_zip_load(sevens[k], &d, &n, name, sizeof name) && n == 8000 &&
		      !strcmp(name, "Metroid (USA).gba"), "%s: the game, %zu bytes, %s", sevens[k], n, name);
		if (k == 0 && d) {
			FILE *f = fopen("/tmp/zip_test.gba", "wb");

			if (f) { fwrite(d, 1, n, f); fclose(f); }
		}
		free(d); d = NULL;
	}
	if (system("cd /tmp && rm -f zip_test.zip && python3 -c \"import zipfile as z;"
	           "a=z.ZipFile('zip_test.zip','w',z.ZIP_DEFLATED);a.write('zip_test.gba','Metroid (USA).gba');a.close()\"") == 0) {
		CHECK(diatom_zip_load("/tmp/zip_test.zip", &z, &zn, name, sizeof name) && zn == 8000,
		      "a .zip still opens as before (%zu bytes)", zn);
		free(z);
	}
	/* A zip made with Zstandard (method 93), as some ROM sets are now: every
	 * game in them failed with "cannot extract" (2026-11-25) */
	{
		unsigned char want[8000];   /* what the fixture holds, made up, nothing copied */
		size_t wn;

		for (wn = 0; wn < sizeof want; wn++) want[wn] = (unsigned char)((wn * 37 + (wn >> 5)) & 0xff);
		CHECK(diatom_zip_load("test/fixtures/zstd.zip", &z, &zn, name, sizeof name) && zn == 8000 &&
		      !memcmp(z, want, 8000) && !strcmp(name, "Homebrew Game (World).sms"),
		      "a Zstandard zip opens, byte for byte (%zu bytes, %s)", zn, name);
		free(z); z = NULL;
	}
	/* and one damaged inside: refused by the zip's own check, not run */
	{
		FILE *f = fopen("test/fixtures/zstd.zip", "rb"), *g;
		unsigned char buf[4096];
		size_t bn = f ? fread(buf, 1, sizeof buf, f) : 0;

		if (f) fclose(f);
		if (bn > 70 && (g = fopen("/tmp/zip_test_bad.zip", "wb"))) {
			buf[70] ^= 0x5a;    /* a byte of the compressed game */
			fwrite(buf, 1, bn, g);
			fclose(g);
			CHECK(!diatom_zip_load("/tmp/zip_test_bad.zip", &z, &zn, name, sizeof name),
			      "a damaged Zstandard zip is refused");
			if (z) { free(z); z = NULL; }
		}
	}
	CHECK(!diatom_zip_load("test/fixtures/none.7z", &d, &n, name, sizeof name),
	      "a .7z that isn't there is refused");
	printf(failures ? "FAILED\n" : "all good\n");
	return failures ? 1 : 0;
}
