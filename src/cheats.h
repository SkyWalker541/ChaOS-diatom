/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DIATOM_CHEATS_H
#define DIATOM_CHEATS_H

#include <stdbool.h>

#include "diatom.h"

/* Cheats (ChaOS).
 *
 * The launcher reads a game's .cht files, keeps which cheats are on, and
 * sends Diatom only the ones that are, as a small file of its own (SETCHEATS,
 * `path`; empty is none). Two kinds, as RetroArch's cheat files have them:
 *
 *   C <tab> code
 *       A code for the core to decode itself - Game Genie, Pro Action
 *       Replay, GameShark, CodeBreaker and the like - through
 *       retro_cheat_set. "A+B+C" is one cheat of several parts, as the
 *       cores expect it.
 *
 *   M <tab> address size type value big_endian bitmask repeat add_address add_value
 *       A write Diatom makes itself after every frame: RetroArch's "retro"
 *       handler. `address` is an offset into the concatenation of the
 *       regions the core marked as system RAM in its memory map, in the
 *       order it gave them, or into retro_get_memory_data(SYSTEM_RAM) when
 *       it marked none - the address space RetroArch's cheat search shows,
 *       and so the one these addresses were found in. `size` 0..5 is 1, 2, 4,
 *       8, 16 or 32 bits; `type` 1 sets, 2 adds, 3 subtracts, 4..7 run the
 *       next cheat only if the value is equal, not equal, less than or
 *       greater than `value`.
 *
 * Written from the behaviour of RetroArch's cheat manager, not its code. */

/* RETRO_ENVIRONMENT_SET_MEMORY_MAPS, seen by env.c: the regions marked as
 * system RAM, kept for memory cheats */
void diatom_cheats_note_map(const void *mmap);

/* A game began or ended: the regions and the list are forgotten */
void diatom_cheats_game(diatom_core *c);

/* Replace the cheats in force with those in `path` ("" for none). Returns how
 * many were taken; *codes and *mem say of which kind. */
int  diatom_cheats_load(diatom_core *c, const char *path, int *codes, int *mem);

/* After each retro_run: memory cheats, if there are any. Costs nothing without. */
void diatom_cheats_frame(diatom_core *c);

/* Any cheat in force */
bool diatom_cheats_on(void);

#endif
