/* crt-lottes-fast, Timothy Lottes' CRTS ("PUBLIC DOMAIN CRT-STYLED SCALAR",
 * Unlicense), as libretro's glsl-shaders carries it in
 * crt/shaders/crt-lottes-fast.glsl, on the CPU (ChaOS).
 *
 * The same eight parameters as the GLSL, by its names, in its order. Drawn at a
 * whole-number scale as the PT shader is (pt_shader.h): pt_format says where
 * each channel goes, and a frame is begun once and drawn in bands, from any
 * number of threads at once, each with its own crts_band.
 *
 * Without curvature the filter is separable - the horizontal Gaussian and the
 * scanlines depend on the column and the row alone - so each band filters a
 * game row across once and every screen row is two of those mixed. With
 * curvature every pixel is filtered on its own, as the GLSL does it.
 */
#ifndef CRTS_H
#define CRTS_H

#include <stddef.h>
#include <stdint.h>

#include "pt_shader.h"     /* pt_format */

enum {
	CRTS_MASK, CRTS_MASK_INTENSITY, CRTS_SCANLINE_THINNESS, CRTS_SCAN_BLUR,
	CRTS_CURVATURE, CRTS_TRINITRON_CURVE, CRTS_CORNER, CRTS_CRT_GAMMA,
	CRTS_NPARAMS
};

typedef struct { const char *name; float def, min, max, step; } crts_param;
extern const crts_param crts_params[CRTS_NPARAMS];
int crts_param_index(const char *name);

typedef struct crts_ctx crts_ctx;
typedef struct crts_band crts_band;

crts_ctx  *crts_create(void);
void       crts_destroy(crts_ctx *c);
void       crts_set_params(crts_ctx *c, const float *v);   /* CRTS_NPARAMS */
crts_band *crts_band_create(void);
void       crts_band_destroy(crts_band *b);
/* src is 0x00RRGGBB, w x h, src_pitch pixels a row. 0 on success. */
int        crts_begin_frame(crts_ctx *c, const uint32_t *src, int w, int h,
                            size_t src_pitch, int scale);
/* The same, drawn to any ow x oh: the GLSL's sizes are the screen's own. */
int        crts_begin_frame_size(crts_ctx *c, const uint32_t *src, int w, int h,
                                 size_t src_pitch, int ow, int oh);
/* Screen rows [y0, y1) into `rows`, which is where row y0 goes. */
int        crts_render_rows(const crts_ctx *c, crts_band *b, int y0, int y1,
                            uint32_t *rows, size_t pitch, const pt_format *fmt);

#endif
