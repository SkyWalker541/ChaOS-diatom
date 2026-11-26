/* Zip content, because that is how launchers ship ROM libraries.
 *
 * TortOS's entire library is zipped - one ROM per archive, No-Intro style -
 * and minarch handled that, so Diatom meeting its first real launcher failed
 * on the first game it was handed. The register's line about protodrive
 * testing "what the author expected" earned its keep in under a minute.
 *
 * Scope is deliberately the launcher case and nothing more: ONE interesting
 * entry per archive, chosen as the largest file, stored or deflated. Multi-
 * file archives (cue+bin) ship unzipped for the same reason they do on every
 * other firmware: the cue names its bin by path.
 *
 * A deflated entry goes through zlib; a stored one is copied as it is; a
 * Zstandard one (method 93) through zstd's own decoder (lib/zstd). Some ROM
 * sets are zipped with Zstandard now, and every game in them failed with
 * "cannot extract" (a tester's Master System, Game Gear, Neo Geo Pocket and
 * TurboGrafx-16 games, 2026-11-25).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "diatom.h"
#include "../lib/7z/sevenz.h"
#include "../lib/zstd/zstd.h"

#define ZIP_STORED  0
#define ZIP_DEFLATE 8
#define ZIP_ZSTD    93

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* ChaOS: a .7z is taken the same way - its largest file, read whole - by
 * 7-Zip's own C decoder (lib/7z, public domain). */
bool diatom_zip_is(const char *path)
{
	size_t n = path ? strlen(path) : 0;
	return (n > 4 && strcasecmp(path + n - 4, ".zip") == 0) || sevenz_is(path);
}

char diatom_zip_why[96];

/* Largest entry in the archive, extracted whole. */
bool diatom_zip_load(const char *path, void **out, size_t *out_len,
                     char *name, size_t name_n)
{
	snprintf(diatom_zip_why, sizeof diatom_zip_why, "its %s couldn't be read",
	         sevenz_is(path) ? ".7z" : "zip");
	if (sevenz_is(path)) {
		unsigned char *d = NULL;

		if (!sevenz_largest(path, 0, &d, out_len, name, name_n)) {
			fprintf(stderr, "diatom: 7z: couldn't read a game in %s\n", path);
			return false;
		}
		*out = d;
		diatom_zip_why[0] = '\0';
		return true;
	}
	FILE *f = fopen(path, "rb");
	unsigned char tail[66000 + 22], hdr[46], lh[30];
	long fsize, tail_off;
	size_t tail_len, i;
	uint32_t cd_off = 0, cd_n = 0;
	int found = 0;
	uint32_t best_usize = 0, best_csize = 0, best_lho = 0, best_crc = 0;
	uint16_t best_method = 0, odd_method = 0;
	char best_name[512] = "";

	*out = NULL; *out_len = 0;
	if (name && name_n) name[0] = '\0';
	if (!f) return false;

	/* End-of-central-directory: within the last 64K + comment slack. */
	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	tail_len = fsize < (long)sizeof tail ? (size_t)fsize : sizeof tail;
	tail_off = fsize - (long)tail_len;
	fseek(f, tail_off, SEEK_SET);
	if (fread(tail, 1, tail_len, f) != tail_len) { fclose(f); return false; }
	for (i = tail_len - 22 + 1; i-- > 0; ) {
		if (tail[i] == 'P' && tail[i+1] == 'K' && tail[i+2] == 5 && tail[i+3] == 6) {
			cd_n   = rd16(tail + i + 10);
			cd_off = rd32(tail + i + 16);
			found = 1;
			break;
		}
	}
	if (!found || !cd_n) { fclose(f); return false; }

	fseek(f, (long)cd_off, SEEK_SET);
	for (i = 0; i < cd_n; i++) {
		char en[512];
		uint16_t nlen, elen, clen, method;
		uint32_t csize, usize, lho, crc;

		if (fread(hdr, 1, 46, f) != 46 || rd32(hdr) != 0x02014b50) break;
		method = rd16(hdr + 10);
		crc    = rd32(hdr + 16);
		csize  = rd32(hdr + 20);
		usize  = rd32(hdr + 24);
		nlen   = rd16(hdr + 28);
		elen   = rd16(hdr + 30);
		clen   = rd16(hdr + 32);
		lho    = rd32(hdr + 42);

		if (nlen >= sizeof en) { fclose(f); return false; }
		if (fread(en, 1, nlen, f) != nlen) break;
		en[nlen] = '\0';
		fseek(f, elen + clen, SEEK_CUR);

		if (usize == 0 || en[nlen - 1] == '/') continue;        /* directory */
		if (method != ZIP_STORED && method != ZIP_DEFLATE && method != ZIP_ZSTD) {
			odd_method = method;                            /* said below, if nothing else is */
			continue;
		}
		if (usize > best_usize) {
			best_usize = usize; best_csize = csize; best_crc = crc;
			best_method = method; best_lho = lho;
			snprintf(best_name, sizeof best_name, "%s", en);
		}
	}
	if (!best_usize) {
		if (odd_method) {
			fprintf(stderr, "diatom: zip: %s is compressed a way Diatom can't read (method %u)\n",
			        path, odd_method);
			snprintf(diatom_zip_why, sizeof diatom_zip_why,
			         "its zip is packed a way ChaOS can't read yet (method %u)", odd_method);
		}
		fclose(f);
		return false;
	}

	/* The local header repeats the name/extra lengths; the data follows it. */
	fseek(f, (long)best_lho, SEEK_SET);
	if (fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50) { fclose(f); return false; }
	fseek(f, rd16(lh + 26) + rd16(lh + 28), SEEK_CUR);

	{
		unsigned char *comp = malloc(best_csize ? best_csize : 1);
		unsigned char *un   = malloc(best_usize);

		if (!comp || !un) { free(comp); free(un); fclose(f); return false; }
		if (fread(comp, 1, best_csize, f) != best_csize) {
			free(comp); free(un); fclose(f); return false;
		}
		fclose(f);

		if (best_method == ZIP_STORED) {
			memcpy(un, comp, best_usize);
		} else if (best_method == ZIP_ZSTD) {
			size_t got = ZSTD_decompress(un, best_usize, comp, best_csize);

			if (ZSTD_isError(got) || got != best_usize) {
				fprintf(stderr, "diatom: zip: zstd failed on %s: %s\n", best_name,
				        ZSTD_isError(got) ? ZSTD_getErrorName(got) : "short");
				free(comp); free(un); return false;
			}
		} else {
			z_stream z;
			int rc;

			memset(&z, 0, sizeof z);
			z.next_in   = comp;
			z.avail_in  = best_csize;
			z.next_out  = un;
			z.avail_out = best_usize;
			/* -15: raw deflate, no zlib wrapper - what zip stores. */
			if (inflateInit2(&z, -15) != Z_OK) {
				free(comp); free(un); return false;
			}
			rc = inflate(&z, Z_FINISH);
			inflateEnd(&z);
			if (rc != Z_STREAM_END || z.total_out != best_usize) {
				fprintf(stderr, "diatom: zip: inflate failed on %s\n", best_name);
				free(comp); free(un); return false;
			}
		}
		free(comp);
		/* The zip's own check, whichever way it was packed: a damaged
		 * Zstandard entry can decode to the right size and the wrong game */
		if ((uint32_t)crc32(0L, un, best_usize) != best_crc) {
			fprintf(stderr, "diatom: zip: %s is damaged (its check doesn't match)\n", best_name);
			snprintf(diatom_zip_why, sizeof diatom_zip_why, "its zip is damaged");
			free(un);
			return false;
		}
		*out = un;
		*out_len = best_usize;
		diatom_zip_why[0] = '\0';
		if (name && name_n) snprintf(name, name_n, "%s", best_name);
		return true;
	}
}
