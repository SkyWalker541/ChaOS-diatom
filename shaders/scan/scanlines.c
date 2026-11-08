/* Resolution-Independent Scanlines on the CPU (ChaOS). See scanlines.h.
 *
 * Follows res-independent-scanlines.glsl (libretro glsl-shaders), after
 * RiskyJumps, public domain:
 *
 *   scale = autoscale ? InputSize.y : imageSize
 *   angle = gl_FragCoord.y / OutputSize.y * omega * scale + phase,  omega = pi
 *   lines = abs(sin(angle) * amp) * (lines_white - lines_black) + lines_black
 *   color = texel * lines
 *
 * gl_FragCoord counts from the bottom of the output, so screen row Y from the
 * top is at oh - Y - 0.5. The factor per screen row is a table, made when the
 * size or the settings change, in 8.8 fixed point; each game pixel's three
 * channels are scaled once per screen row and repeated across its columns.
 */
#include "scanlines.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const scan_param scan_params[SCAN_NPARAMS] = {
	[SCAN_AMP]         = { "amp",         1.25f,   0.0f,   2.0f, 0.05f },
	[SCAN_PHASE]       = { "phase",       0.5f,    0.0f,   2.0f, 0.05f },
	[SCAN_LINES_BLACK] = { "lines_black", 0.0f,    0.0f,   1.0f, 0.05f },
	[SCAN_LINES_WHITE] = { "lines_white", 1.0f,    0.0f,   2.0f, 0.05f },
	[SCAN_IMAGE_SIZE]  = { "imageSize",   224.0f, 144.0f, 288.0f, 1.0f },
	[SCAN_AUTOSCALE]   = { "autoscale",   0.0f,    0.0f,   1.0f, 1.0f },
};

int scan_param_index(const char *name)
{
	int i;
	for (i = 0; i < SCAN_NPARAMS; i++)
		if (!strcmp(scan_params[i].name, name)) return i;
	return -1;
}

struct scan_ctx {
	float p[SCAN_NPARAMS];
	const uint32_t *src;
	size_t pitch;
	int w, h, ow, oh;
	int s;                  /* the whole-number scale, or 0 if there is none */
	uint32_t *yf;           /* per screen row, the factor times 65536 */
	int *ys, *xs;           /* per screen row and column, the game's */
	size_t ycap, xcap;
	int ready;
};

scan_ctx *scan_create(void)
{
	scan_ctx *c = calloc(1, sizeof *c);
	int i;
	if (!c) return NULL;
	for (i = 0; i < SCAN_NPARAMS; i++) c->p[i] = scan_params[i].def;
	return c;
}

void scan_destroy(scan_ctx *c)
{
	if (!c) return;
	free(c->yf); free(c->ys); free(c->xs);
	free(c);
}

void scan_set_params(scan_ctx *c, const float *v)
{
	memcpy(c->p, v, sizeof c->p);
	c->w = c->h = c->ow = c->oh = 0;      /* the table again at the next frame */
	c->ready = 0;
}

static int table(scan_ctx *c, int w, int h, int ow, int oh)
{
	const double pi = 3.141592654;
	double amp = c->p[SCAN_AMP], phase = c->p[SCAN_PHASE];
	double lb = c->p[SCAN_LINES_BLACK], lw = c->p[SCAN_LINES_WHITE];
	double scale = c->p[SCAN_AUTOSCALE] == 1.0f ? (double)h : (double)c->p[SCAN_IMAGE_SIZE];
	int Y, X;

	if ((size_t)oh > c->ycap) {
		free(c->yf); free(c->ys);
		c->yf = malloc((size_t)oh * sizeof *c->yf);
		c->ys = malloc((size_t)oh * sizeof *c->ys);
		c->ycap = c->yf && c->ys ? (size_t)oh : 0;
		if (!c->ycap) return -1;
	}
	if ((size_t)ow > c->xcap) {
		free(c->xs);
		c->xs = malloc((size_t)ow * sizeof *c->xs);
		c->xcap = c->xs ? (size_t)ow : 0;
		if (!c->xcap) return -1;
	}
	/* nearest: the game pixel under the screen pixel's centre */
	for (X = 0; X < ow; X++) {
		int sx = (int)(((X + 0.5) * w) / ow);
		c->xs[X] = sx < w ? sx : w - 1;
	}
	for (Y = 0; Y < oh; Y++) {
		int sy = (int)(((Y + 0.5) * h) / oh);
		c->ys[Y] = sy < h ? sy : h - 1;
	}
	for (Y = 0; Y < oh; Y++) {
		double fy = (oh - Y - 0.5) / oh;
		double lines = fabs(sin(fy * pi * scale + phase) * amp) * (lw - lb) + lb;
		if (lines < 0.0) lines = 0.0;
		if (lines > 4.0) lines = 4.0;     /* past 255 from 1 already */
		c->yf[Y] = (uint32_t)(lines * 65536.0 + 0.5);
	}
	return 0;
}

int scan_begin_frame_size(scan_ctx *c, const uint32_t *src, int w, int h,
                          size_t src_pitch, int ow, int oh)
{
	if (w <= 0 || h <= 0 || ow < 1 || oh < 1) { c->ready = 0; return -1; }
	if (w != c->w || h != c->h || ow != c->ow || oh != c->oh) {
		c->ready = 0;
		if (table(c, w, h, ow, oh)) { c->w = 0; return -1; }
		c->w = w; c->h = h; c->ow = ow; c->oh = oh;
		c->s = ow % w == 0 && oh % h == 0 && ow / w == oh / h ? ow / w : 0;
	}
	c->src = src; c->pitch = src_pitch;
	c->ready = 1;
	return 0;
}

int scan_begin_frame(scan_ctx *c, const uint32_t *src, int w, int h,
                     size_t src_pitch, int s)
{
	if (s < 1) { c->ready = 0; return -1; }
	return scan_begin_frame_size(c, src, w, h, src_pitch, w * s, h * s);
}

static inline uint32_t chan(uint32_t v, uint32_t f)
{
	uint32_t o = (v * f + 32768u) >> 16;
	return o > 255u ? 255u : o;
}

int scan_render_rows(const scan_ctx *c, int y0, int y1,
                     uint32_t *rows, size_t pitch, const pt_format *fm)
{
	int Y, x, k, s, oh, ow;
	if (!c || !c->ready) return -1;
	s = c->s; oh = c->oh; ow = c->ow;
	if (y0 < 0) { rows += (size_t)(-y0) * pitch; y0 = 0; }
	if (y1 > oh) y1 = oh;
	for (Y = y0; Y < y1; Y++) {
		const uint32_t *in = c->src + (size_t)c->ys[Y] * c->pitch;
		const uint32_t f = c->yf[Y];
		uint32_t *o = rows + (size_t)(Y - y0) * pitch;
		if (f == 0) {
			for (x = 0; x < ow; x++) o[x] = fm->opaque;
			continue;
		}
		if (!s) {           /* no whole-number scale: each column its own */
			for (x = 0; x < ow; x++) {
				uint32_t p = in[c->xs[x]];
				o[x] = (chan((p >> 16) & 0xff, f) << fm->r_shift) |
				       (chan((p >> 8) & 0xff, f) << fm->g_shift) |
				       (chan(p & 0xff, f) << fm->b_shift) | fm->opaque;
			}
			continue;
		}
		for (x = 0; x < c->w; x++) {
			uint32_t p = in[x];
			uint32_t q = (chan((p >> 16) & 0xff, f) << fm->r_shift) |
			             (chan((p >> 8) & 0xff, f) << fm->g_shift) |
			             (chan(p & 0xff, f) << fm->b_shift) | fm->opaque;
			for (k = 0; k < s; k++) *o++ = q;
		}
	}
	return 0;
}
