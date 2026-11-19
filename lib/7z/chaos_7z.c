/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: 7-Zip's C decoder as one unit, and sevenz_largest on top (sevenz.h).
 *
 * The decoder's own files are untouched copies from 7-Zip 26.06 (lib/7z/src,
 * public domain). libchdr carries an older LzmaDec of its own, so the names
 * that would clash are renamed here before it is included. */
#define _GNU_SOURCE
#define Z7_ST 1

#define LzmaDec_InitDicAndState chaos7z_LzmaDec_InitDicAndState
#define LzmaDec_Init            chaos7z_LzmaDec_Init
#define LzmaDec_DecodeToDic     chaos7z_LzmaDec_DecodeToDic
#define LzmaDec_DecodeToBuf     chaos7z_LzmaDec_DecodeToBuf
#define LzmaDec_FreeProbs       chaos7z_LzmaDec_FreeProbs
#define LzmaDec_Free            chaos7z_LzmaDec_Free
#define LzmaProps_Decode        chaos7z_LzmaProps_Decode
#define LzmaDec_AllocateProbs   chaos7z_LzmaDec_AllocateProbs
#define LzmaDec_Allocate        chaos7z_LzmaDec_Allocate
#define LzmaDecode              chaos7z_LzmaDecode

#include "src/7zAlloc.c"
#include "src/7zArcIn.c"
#include "src/7zBuf.c"
#include "src/7zCrc.c"
#include "src/7zCrcOpt.c"
#include "src/7zDec.c"
#include "src/7zFile.c"
#include "src/7zStream.c"
#include "src/Bcj2.c"
#include "src/Bra.c"
#include "src/Bra86.c"
#include "src/BraIA64.c"
#include "src/CpuArch.c"
#include "src/Delta.c"
#include "src/Lzma2Dec.c"
#include "src/LzmaDec.c"
#include "src/Ppmd7.c"
#include "src/Ppmd7Dec.c"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "sevenz.h"

bool sevenz_is(const char *path)
{
	size_t n = path ? strlen(path) : 0;

	return n > 3 && !strcasecmp(path + n - 3, ".7z");
}

/* A name inside the archive, UTF-16 to UTF-8 */
static void name_utf8(const UInt16 *w, char *out, size_t n)
{
	size_t o = 0;

	if (!n) return;
	for (; *w && o + 4 < n; w++) {
		unsigned c = *w;

		if (c >= 0xD800 && c < 0xDC00 && w[1] >= 0xDC00 && w[1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (w[1] - 0xDC00);
			w++;
		}
		if (c < 0x80) out[o++] = (char)c;
		else if (c < 0x800) { out[o++] = (char)(0xC0 | c >> 6); out[o++] = (char)(0x80 | (c & 63)); }
		else if (c < 0x10000) {
			out[o++] = (char)(0xE0 | c >> 12); out[o++] = (char)(0x80 | ((c >> 6) & 63));
			out[o++] = (char)(0x80 | (c & 63));
		} else {
			out[o++] = (char)(0xF0 | c >> 18); out[o++] = (char)(0x80 | ((c >> 12) & 63));
			out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63));
		}
	}
	out[o] = '\0';
}

bool sevenz_largest(const char *path, size_t max, unsigned char **out, size_t *len,
                    char *name, size_t name_n)
{
	static const ISzAlloc alloc = { SzAlloc, SzFree };
	static const ISzAlloc temp = { SzAllocTemp, SzFreeTemp };
	CFileInStream file;
	CLookToRead2 look;
	CSzArEx db;
	UInt32 i, best = (UInt32)-1, block = 0xFFFFFFFF;
	UInt64 best_size = 0;
	Byte *buf = NULL;
	size_t buf_size = 0, offset = 0, got = 0;
	bool ok = false;

	*out = NULL;
	*len = 0;
	if (name && name_n) name[0] = '\0';
	if (InFile_Open(&file.file, path) != 0) return false;
	FileInStream_CreateVTable(&file);
	file.wres = 0;
	LookToRead2_CreateVTable(&look, False);
	look.buf = (Byte *)ISzAlloc_Alloc(&alloc, 1 << 18);
	if (!look.buf) { File_Close(&file.file); return false; }
	look.bufSize = 1 << 18;
	look.realStream = &file.vt;
	LookToRead2_INIT(&look)
	CrcGenerateTable();
	SzArEx_Init(&db);
	if (SzArEx_Open(&db, &look.vt, &alloc, &temp) == SZ_OK) {
		for (i = 0; i < db.NumFiles; i++) {
			UInt64 s;

			if (SzArEx_IsDir(&db, i)) continue;
			s = SzArEx_GetFileSize(&db, i);
			if (best == (UInt32)-1 || s > best_size) { best = i; best_size = s; }
		}
		if (best != (UInt32)-1 && (!max || best_size <= max) &&
		    SzArEx_Extract(&db, &look.vt, best, &block, &buf, &buf_size, &offset, &got,
		                   &alloc, &temp) == SZ_OK &&
		    (*out = malloc(got ? got : 1))) {
			memcpy(*out, buf + offset, got);
			*len = got;
			ok = true;
			if (name && name_n) {
				size_t wl = SzArEx_GetFileNameUtf16(&db, best, NULL);
				UInt16 *w = malloc(wl * sizeof *w);

				if (w) {
					SzArEx_GetFileNameUtf16(&db, best, w);
					name_utf8(w, name, name_n);
					free(w);
				}
			}
		}
		ISzAlloc_Free(&alloc, buf);
	}
	SzArEx_Free(&db, &alloc);
	ISzAlloc_Free(&alloc, look.buf);
	File_Close(&file.file);
	return ok;
}

bool sevenz_crc(const char *path, bool (*allow)(const char *name, void *ctx), void *ctx,
                unsigned *crc)
{
	static const ISzAlloc alloc = { SzAlloc, SzFree };
	static const ISzAlloc temp = { SzAllocTemp, SzFreeTemp };
	CFileInStream file;
	CLookToRead2 look;
	CSzArEx db;
	UInt32 i, only = 0, files = 0;
	bool found = false, only_has = false;

	if (InFile_Open(&file.file, path) != 0) return false;
	FileInStream_CreateVTable(&file);
	file.wres = 0;
	LookToRead2_CreateVTable(&look, False);
	look.buf = (Byte *)ISzAlloc_Alloc(&alloc, 1 << 16);
	if (!look.buf) { File_Close(&file.file); return false; }
	look.bufSize = 1 << 16;
	look.realStream = &file.vt;
	LookToRead2_INIT(&look)
	CrcGenerateTable();
	SzArEx_Init(&db);
	if (SzArEx_Open(&db, &look.vt, &alloc, &temp) == SZ_OK) {
		for (i = 0; i < db.NumFiles && !found; i++) {
			char name[512] = "";
			size_t wl;
			UInt16 *w;
			bool has = SzBitWithVals_Check(&db.CRCs, i);

			if (SzArEx_IsDir(&db, i)) continue;
			if (files++ == 0) { only = has ? db.CRCs.Vals[i] : 0; only_has = has; }
			wl = SzArEx_GetFileNameUtf16(&db, i, NULL);
			if ((w = malloc(wl * sizeof *w))) {
				SzArEx_GetFileNameUtf16(&db, i, w);
				name_utf8(w, name, sizeof name);
				free(w);
			}
			if (allow && allow(name, ctx) && has) {
				*crc = db.CRCs.Vals[i];
				found = true;
			}
		}
		if (!found && files == 1 && only_has) {
			*crc = only;
			found = true;
		}
	}
	SzArEx_Free(&db, &alloc);
	ISzAlloc_Free(&alloc, look.buf);
	File_Close(&file.file);
	return found;
}
