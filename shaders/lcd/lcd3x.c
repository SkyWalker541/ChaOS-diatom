/* lcd3x on the CPU (ChaOS). See lcd3x.h.
 *
 * Follows handheld/shaders/lcd3x.glsl (libretro glsl-shaders), Gigaherz,
 * public domain: color = texel * yfactor * xfactors, in the game's own
 * (sRGB) values as the GLSL has them, with
 *
 *   angle    = texcoord * InputSize * 2 pi = (screen pixel + 0.5) / screen size * game size * 2 pi
 *   yfactor  = (brighten_scanlines + sin(angle.y)) / (brighten_scanlines + 1)
 *   xfactors = (brighten_lcd + sin(angle.x + offsets)) / (brighten_lcd + 1)
 *   offsets  = pi * (1/2, 1/2 - 2/3, 1/2 - 4/3)
 *
 * The factors are tables, per screen column and per screen row, made when
 * the size changes.
 */
#include "lcd3x.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const lcd3x_param lcd3x_params[LCD3X_NPARAMS] = {
	[LCD3X_BRIGHTEN_SCANLINES] = { "brighten_scanlines", 16.0f, 1.0f, 32.0f, 0.5f },
	[LCD3X_BRIGHTEN_LCD]       = { "brighten_lcd",        4.0f, 1.0f, 12.0f, 0.1f },
};

int lcd3x_param_index(const char *name)
{
	int i;
	for (i = 0; i < LCD3X_NPARAMS; i++)
		if (!strcmp(lcd3x_params[i].name, name)) return i;
	return -1;
}

struct lcd3x_ctx {
	float p[LCD3X_NPARAMS];
	const uint32_t *src;
	size_t pitch;
	int w, h, ow, oh;
	/* per screen column, the three xfactors and the game column it shows;
	 * per screen row, the yfactor and the game row */
	float *xf, *yf;
	int *xs, *ys;
	size_t xcap, ycap;
	int ready;
};

lcd3x_ctx *lcd3x_create(void)
{
	lcd3x_ctx *c = calloc(1, sizeof *c);
	int i;
	if (!c) return NULL;
	for (i = 0; i < LCD3X_NPARAMS; i++) c->p[i] = lcd3x_params[i].def;
	return c;
}

void lcd3x_destroy(lcd3x_ctx *c)
{
	if (!c) return;
	free(c->xf); free(c->yf); free(c->xs); free(c->ys);
	free(c);
}

void lcd3x_set_params(lcd3x_ctx *c, const float *v)
{
	memcpy(c->p, v, sizeof c->p);
	c->w = c->h = c->ow = c->oh = 0;      /* the tables again at the next frame */
	c->ready = 0;
}

/* The factors as the GLSL has them. Its angle is texcoord times 2 pi times the
 * game's size, hundreds of radians at the right of the screen, where a GPU's
 * interpolated texcoord drifts the stripes' phase (Mesa's llvmpipe by up to a
 * tenth of a radian); here it is worked out in double, so the stripes keep
 * their phase all the way across. */
static int tables(lcd3x_ctx *c, int w, int h, int ow, int oh)
{
	const double pi = 3.141592654;
	const double off[3] = { pi * (1.0 / 2.0), pi * (1.0 / 2.0 - 2.0 / 3.0),
	                        pi * (1.0 / 2.0 - 4.0 / 3.0) };
	double bs = c->p[LCD3X_BRIGHTEN_SCANLINES], bl = c->p[LCD3X_BRIGHTEN_LCD];
	double omx = pi * 2.0 * w, omy = pi * 2.0 * h;
	int X, Y, k;

	if ((size_t)ow > c->xcap) {
		free(c->xf); free(c->xs);
		c->xf = malloc((size_t)ow * 3 * sizeof *c->xf);
		c->xs = malloc((size_t)ow * sizeof *c->xs);
		c->xcap = c->xf && c->xs ? (size_t)ow : 0;
		if (!c->xcap) return -1;
	}
	if ((size_t)oh > c->ycap) {
		free(c->yf); free(c->ys);
		c->yf = malloc((size_t)oh * sizeof *c->yf);
		c->ys = malloc((size_t)oh * sizeof *c->ys);
		c->ycap = c->yf && c->ys ? (size_t)oh : 0;
		if (!c->ycap) return -1;
	}
	/* Nearest, as the GLSL's texture lookup: the game pixel under the
	 * screen pixel's centre. At a whole-number scale, X / scale. */
	for (X = 0; X < ow; X++) {
		double ax = ((X + 0.5) / ow) * omx;
		int sx = (int)(((X + 0.5) * w) / ow);
		c->xs[X] = sx < w ? sx : w - 1;
		for (k = 0; k < 3; k++)
			c->xf[X * 3 + k] = (float)((bl + sin(ax + off[k])) / (bl + 1.0));
	}
	for (Y = 0; Y < oh; Y++) {
		double ay = ((Y + 0.5) / oh) * omy;
		int sy = (int)(((Y + 0.5) * h) / oh);
		c->ys[Y] = sy < h ? sy : h - 1;
		c->yf[Y] = (float)((bs + sin(ay)) / (bs + 1.0));
	}
	return 0;
}

int lcd3x_begin_frame_size(lcd3x_ctx *c, const uint32_t *src, int w, int h,
                           size_t src_pitch, int ow, int oh)
{
	if (w <= 0 || h <= 0 || ow < 1 || oh < 1) { c->ready = 0; return -1; }
	if (w != c->w || h != c->h || ow != c->ow || oh != c->oh) {
		c->ready = 0;
		if (tables(c, w, h, ow, oh)) { c->w = 0; return -1; }
		c->w = w; c->h = h; c->ow = ow; c->oh = oh;
	}
	c->src = src; c->pitch = src_pitch;
	c->ready = 1;
	return 0;
}

int lcd3x_begin_frame(lcd3x_ctx *c, const uint32_t *src, int w, int h,
                      size_t src_pitch, int s)
{
	if (s < 1) { c->ready = 0; return -1; }
	return lcd3x_begin_frame_size(c, src, w, h, src_pitch, w * s, h * s);
}

static uint32_t chan(uint32_t v, float f)
{
	float o = (float)v * f + 0.5f;
	return o >= 255.0f ? 255u : o <= 0.0f ? 0u : (uint32_t)o;
}

int lcd3x_render_rows(const lcd3x_ctx *c, int y0, int y1,
                      uint32_t *rows, size_t pitch, const pt_format *fm)
{
	int Y, X, oh, ow;
	if (!c || !c->ready) return -1;
	oh = c->oh; ow = c->ow;
	if (y0 < 0) { rows += (size_t)(-y0) * pitch; y0 = 0; }
	if (y1 > oh) y1 = oh;
	for (Y = y0; Y < y1; Y++) {
		const uint32_t *in = c->src + (size_t)c->ys[Y] * c->pitch;
		const float yf = c->yf[Y], *xf = c->xf;
		const int *xs = c->xs;
		uint32_t *o = rows + (size_t)(Y - y0) * pitch;
		for (X = 0; X < ow; X++, xf += 3) {
			uint32_t p = in[xs[X]];
			o[X] = (chan((p >> 16) & 0xff, yf * xf[0]) << fm->r_shift) |
			       (chan((p >> 8) & 0xff, yf * xf[1]) << fm->g_shift) |
			       (chan(p & 0xff, yf * xf[2]) << fm->b_shift) | fm->opaque;
		}
	}
	return 0;
}
