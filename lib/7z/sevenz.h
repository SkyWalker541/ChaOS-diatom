/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_SEVENZ_H
#define CHAOS_SEVENZ_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: .7z ROMs, read the way a .zip is - one game per archive, the largest
 * file in it being the game. Built on 7-Zip's own C decoder (lib/7z/src, Igor
 * Pavlov, public domain), compiled as one file (chaos_7z.c) with its LZMA
 * names prefixed so it sits beside libchdr's copy. */

/* Ends in .7z, any case */
bool sevenz_is(const char *path);

/* The largest file in the archive, read whole into *out (malloc'd, the
 * caller frees) with its length and its name inside the archive. False, with
 * nothing allocated, when the archive can't be read or holds no file, or when
 * that file is over max bytes (0 for no limit). */
bool sevenz_largest(const char *path, size_t max, unsigned char **out, size_t *len,
                    char *name, size_t name_n);

/* The CRC32 the archive records for its game, from its index alone - nothing
 * unpacked: the first file `allow` takes (given its name and ctx), or the
 * only file when allow takes none. False when there is no such file, or it
 * has no CRC recorded. */
bool sevenz_crc(const char *path, bool (*allow)(const char *name, void *ctx), void *ctx,
                unsigned *crc);

#endif
