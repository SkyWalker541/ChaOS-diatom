/* PT SkyWalker541 - CPU version.
 *
 * Copyright (c) 2026 SkyWalker541. MIT License, see LICENSE.
 *
 * The pixel transparency shader by SkyWalker541 (PT_SkyWalker541.glsl,
 * v2.1.0), ported from GLSL to plain C so it runs on devices that draw
 * frames with the CPU. Same parameters, same names, same defaults and the
 * same picture, at whole-number scale factors.
 *
 * The work is split by what each part depends on, so the full-screen pass is
 * only lookups and blends:
 *   - screen position only (backing grain, glare and sheen, bezel shadow):
 *     built into tables when the size or a parameter changes;
 *   - position inside one game pixel (grid lines, dot shape): small tables;
 *   - the game pixel (white detection, transparency, drop shadow, darken):
 *     once per game pixel per frame.
 *
 * No allocation happens per frame once pt_render has seen a size.
 */
#ifndef PT_SHADER_H
#define PT_SHADER_H

#include <stddef.h>
#include <stdint.h>

/* The parameters, in the shader's menu order. Values are the shader's own
 * numbers: a saved RetroArch preset carries straight over. */
enum {
	PT_GAME_SYSTEM,
	PT_MANUAL_WHITE_DETECTION_LEVEL,
	PT_TRANSPARENT_PIXELS,
	PT_TRANSPARENCY_AMOUNT,
	PT_MINIMUM_WHITE_TRANSPARENCY,
	PT_BACKING_COLOR,
	PT_BACKING_COLOR_STRENGTH,
	PT_GLARE_AND_SHEEN,
	PT_GLARE_STRENGTH,
	PT_GLARE_SPREAD,
	PT_SHEEN_STRENGTH,
	PT_SHEEN_BAND_DENSITY,
	PT_SHEEN_HUE_TURN,
	PT_SHEEN_RIPPLE,
	PT_SHEEN_RIPPLE_SIZE,
	PT_LIGHT_HOME_X,
	PT_LIGHT_HOME_Y,
	PT_LIGHT_DRIFT_SPEED,
	PT_DARKEN_PICTURE,
	PT_PIXEL_EFFECT,
	PT_GAP_COLOR,
	PT_GRID_LINE_WIDTH,
	PT_GRID_LINE_OPACITY,
	PT_DOT_SIZE,
	PT_DOT_EDGE_SOFTNESS,
	PT_DOT_GAP_OPACITY,
	PT_MIN_BRIGHTNESS_FOR_DOTS,
	PT_DROP_SHADOW_OPACITY,
	PT_SHADOW_DISTANCE,
	PT_SHADOW_DIRECTION,
	PT_BEZEL_EDGE_SHADOW,
	PT_NPARAMS
};

typedef struct {
	const char *name;    /* the GLSL uniform's name, e.g. "PT_DOT_SIZE" */
	const char *label;   /* the shader's own menu label */
	float def, min, max, step;
} pt_param_info;

extern const pt_param_info pt_param_table[PT_NPARAMS];

typedef struct { float v[PT_NPARAMS]; } pt_params;

void pt_params_default(pt_params *p);
/* Index of a parameter by its GLSL name, or -1. */
int  pt_param_index(const char *name);

typedef struct pt_ctx pt_ctx;

pt_ctx *pt_create(void);
void    pt_destroy(pt_ctx *c);

/* New settings. Cheap to call; tables that depend on a changed value are
 * rebuilt on the next pt_render, the rest are kept. */
void    pt_set_params(pt_ctx *c, const pt_params *p);

/* Where each channel goes in an output pixel, and the bits always set (an
 * opaque alpha). A framebuffer's own layout, so nothing is repacked later. */
typedef struct { unsigned r_shift, g_shift, b_shift; uint32_t opaque; } pt_format;

typedef struct pt_band pt_band;

/* Draw one frame, all on the calling thread.
 *
 * src:   the game frame, 0x00RRGGBB per pixel, src_pitch pixels per row.
 * scale: the whole-number scale factor; the output is (w*scale) x (h*scale).
 * dst:   the top-left output pixel, dst_pitch pixels per row.
 * frame: a frame counter, as RetroArch's FrameCount; only the drifting light
 *        of Glare and sheen uses it.
 * Returns 0, or -1 if memory for a new size could not be had. */
int pt_render(pt_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
              int scale, uint32_t *dst, size_t dst_pitch, const pt_format *fmt,
              unsigned frame);

/* The same frame split across threads, in two steps.
 *
 * pt_begin_frame does the per-frame work that is not per screen row (the game
 * pixels, and the glare when the light moves), on one thread. Then any number
 * of threads may call pt_render_band at once, each with its own pt_band and
 * its own rows [y0, y1) of the output; dst is the top-left of the whole
 * output, as for pt_render. The bands only read the context, so nothing may
 * call pt_set_params or pt_begin_frame until they have all returned.
 *
 * A band is about one screen row of scratch; create one per thread and keep
 * it. Bands cut at any row give exactly the picture pt_render gives. */
pt_band *pt_band_create(void);
void     pt_band_destroy(pt_band *b);
int      pt_begin_frame(pt_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
                        int scale, unsigned frame);
int      pt_render_band(const pt_ctx *c, pt_band *b, int y0, int y1,
                        uint32_t *dst, size_t dst_pitch, const pt_format *fmt);

#endif
