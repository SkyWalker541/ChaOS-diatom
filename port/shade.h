/* Shaders for ports that draw with the CPU (ChaOS).
 *
 * Shared by the Brick's port and the desktop port, so the part that runs on
 * the Brick can be run and checked on a desktop: taking the host's choice,
 * converting the core's frame, splitting the work across threads and timing
 * it. A port calls shade_frame where it would scale a frame; if it returns
 * false, the port draws the frame itself, as it always has.
 */
#ifndef DIATOM_SHADE_H
#define DIATOM_SHADE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diatom_port.h"

/* diatom_port_shader's work. Safe to call from any thread: the change is
 * picked up at the next shade_frame. */
void shade_set(diatom_shader kind, const float *params, int nparams);

/* Whether frames are to be shaded at all, as last set. */
bool shade_active(void);

/* Draw one frame shaded, at a whole-number scale, into `out` (the top-left
 * pixel of the picture's rect, `out_pitch` pixels per row) in the given
 * channel order. False, with nothing drawn, when no shader is set, the scale
 * is not whole, or memory ran out: the caller then draws the frame itself.
 *
 * r_shift, g_shift and b_shift are where each 8-bit channel goes in an
 * output pixel, and `opaque` the bits always set. */
bool shade_frame(const void *src, int w, int h, size_t pitch, diatom_pixfmt fmt,
                 int scale, uint32_t *out, size_t out_pitch,
                 unsigned r_shift, unsigned g_shift, unsigned b_shift, uint32_t opaque);

/* For the next shade_frame only: each thread, once it has drawn its band of
 * rows [y0, y1) (counted from the picture's top), calls fn(ctx, y0, y1) on
 * them, in parallel with the others. The Brick lays the overlay over its band
 * there and copies it to the page, so that work is split three ways too. */
void shade_band_hook(void (*fn)(void *ctx, int y0, int y1), void *ctx);

/* Stop the helper threads and free everything. */
void shade_shutdown(void);

#endif
