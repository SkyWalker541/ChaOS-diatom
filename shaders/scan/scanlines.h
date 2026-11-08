/* Resolution-Independent Scanlines, after RiskyJumps' "Scanlines Sine
 * Absolute Value" (public domain), as libretro's glsl-shaders carries it in
 * scanlines/shaders/res-independent-scanlines.glsl, on the CPU (ChaOS).
 *
 * Each game pixel, nearest, times one factor per screen row: the absolute
 * value of a sine down the screen. With Automatic Scale on, the sine has the
 * game's own height, so each game row gets one dark line, which at a
 * whole-number scale falls in the same place in every row. The lightest of
 * the CPU shaders: a table and a multiply. Drawn, as the others are, in bands
 * from any number of threads.
 */
#ifndef SCANLINES_H
#define SCANLINES_H

#include <stddef.h>
#include <stdint.h>

#include "pt_shader.h"     /* pt_format */

enum { SCAN_AMP, SCAN_PHASE, SCAN_LINES_BLACK, SCAN_LINES_WHITE, SCAN_IMAGE_SIZE,
       SCAN_AUTOSCALE, SCAN_NPARAMS };

typedef struct { const char *name; float def, min, max, step; } scan_param;
extern const scan_param scan_params[SCAN_NPARAMS];
int scan_param_index(const char *name);

typedef struct scan_ctx scan_ctx;

scan_ctx *scan_create(void);
void      scan_destroy(scan_ctx *c);
void      scan_set_params(scan_ctx *c, const float *v);   /* SCAN_NPARAMS */
/* src is 0x00RRGGBB, w x h, src_pitch pixels a row; kept, not copied, until
 * the frame is drawn. 0 on success. */
int       scan_begin_frame(scan_ctx *c, const uint32_t *src, int w, int h,
                           size_t src_pitch, int scale);
/* The same, drawn to any ow x oh (each screen pixel the game pixel under its
 * centre; the sine down the screen's own height). */
int       scan_begin_frame_size(scan_ctx *c, const uint32_t *src, int w, int h,
                                size_t src_pitch, int ow, int oh);
/* Screen rows [y0, y1) into `rows`, which is where row y0 goes. */
int       scan_render_rows(const scan_ctx *c, int y0, int y1,
                           uint32_t *rows, size_t pitch, const pt_format *fmt);

#endif
