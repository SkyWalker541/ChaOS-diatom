/* Rewind (ChaOS): hold MENU+L1 in a game to step it back.
 *
 * While it is on (SETREWIND on=1 from the launcher, per console, after RUN),
 * the game is saved every REWIND_EVERY frames into a ring in memory, each
 * entry the XOR of a state with the one before it, run-length coded - two
 * frames of a game differ in a few kilobytes of the hundreds a state is, so
 * the ring holds minutes. Held, the frame loop pops one entry a frame,
 * restores that state and runs one frame to show it, in silence, so it plays
 * backwards at REWIND_EVERY times speed. Let go, play carries on from there.
 *
 * RUN, a loaded state and a reset start the ring over: there is nothing
 * before them to go back to that the game agrees with. */
#ifndef DIATOM_REWIND_H
#define DIATOM_REWIND_H

#include <stdbool.h>
#include "diatom.h"

#define REWIND_EVERY   3                    /* frames between saves */
#define REWIND_BUDGET  (48u << 20)          /* bytes of ring */

void rewind_set(bool on);       /* SETREWIND */
bool rewind_on(void);
void rewind_reset(void);        /* RUN, LOAD, RESET */
/* After a frame of normal play: saved every REWIND_EVERY of them */
void rewind_after_frame(diatom_core *c);
/* One step back: true when there was one and it was restored */
bool rewind_step(diatom_core *c);
/* Steps held in the ring, for the log */
unsigned rewind_depth(void);

#endif
