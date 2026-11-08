/* PT SkyWalker541 - CPU version. See pt_shader.h.
 *
 * Copyright (c) 2026 SkyWalker541. MIT License, see LICENSE.
 *
 * Ported from PT_SkyWalker541.glsl v2.1.0. Credits carried over from the
 * shader:
 *   - LCD Dot mode is inspired by Themaister's dot shader (public domain).
 *   - The backing grain's hash is Dave Hoskins' "Hash without Sine"
 *     (https://www.shadertoy.com/view/4djSRW), MIT License,
 *     Copyright (c) 2014 David Hoskins.
 *
 * HOW THE WORK IS SPLIT
 *
 * The shader's arithmetic lives in pt_math.h, shared with the test reference.
 * This file decides when each piece of it runs:
 *
 *   on a new size     the backing grain, per screen pixel (it depends on
 *                     position alone), and the dot's distance from centre;
 *   on new settings   the rest of what depends on position: which game pixel
 *                     casts each screen pixel's drop shadow, and the grid and
 *                     bezel shadow per row and column;
 *   when the light    glare and sheen, per screen pixel;
 *   moves
 *   every frame       one pass over the game pixels (white detection,
 *                     transparency, darken, the shadow each one casts), then
 *                     one pass over the screen.
 *
 * THE SCREEN PASS
 *
 * The screen pass is written to be vectorized by the compiler (NEON on the
 * Brick, SSE on a desktop): each screen row is a run of plain loops over
 * arrays, with no table lookups and no branches per pixel. To get there:
 *
 *   - a game row's values (color, transparency, the drop shadow falling on
 *     it) are spread out to one entry per screen column once, then reused by
 *     every screen row that game row covers;
 *   - the backing color is computed, not looked up: it is a few multiplies
 *     and clamps of the shadowed grey;
 *   - every pixel takes the same arithmetic. A pixel that White mode leaves
 *     opaque simply has transparency 0, which blends to the pixel itself, so
 *     the shader's early exit is not needed as a branch.
 *
 * Colors are 8.8 fixed point (65280 is 1.0) and blend weights run 0..4096.
 * The backing can go well past white (Backing color strength above 1), so
 * nothing is clamped until the final output, as in the shader. Against the
 * float reference the result is within one level everywhere; sweep.sh holds
 * it to that.
 */
#include "pt_shader.h"
#include "pt_math.h"

#include <stdlib.h>
#include <string.h>

const pt_param_info pt_param_table[PT_NPARAMS] = {
	[PT_GAME_SYSTEM]                  = { "PT_GAME_SYSTEM", "Game system (0=Manual, 1=GB, 2=GBC, 3=GBA SP, 4=GBA Orig)", 2.0f, 0.0f, 4.0f, 1.0f },
	[PT_MANUAL_WHITE_DETECTION_LEVEL] = { "PT_MANUAL_WHITE_DETECTION_LEVEL", "Manual white detection level", 0.85f, 0.0f, 1.0f, 0.01f },
	[PT_TRANSPARENT_PIXELS]           = { "PT_TRANSPARENT_PIXELS", "Transparent pixels (0=White, 1=Bright, 2=All)", 2.0f, 0.0f, 2.0f, 1.0f },
	[PT_TRANSPARENCY_AMOUNT]          = { "PT_TRANSPARENCY_AMOUNT", "Transparency amount", 0.10f, 0.0f, 1.0f, 0.01f },
	[PT_MINIMUM_WHITE_TRANSPARENCY]   = { "PT_MINIMUM_WHITE_TRANSPARENCY", "Minimum white transparency", 0.20f, 0.0f, 1.0f, 0.01f },
	[PT_BACKING_COLOR]                = { "PT_BACKING_COLOR", "Backing color (0=Off, 1=Pocket, 2=Grey, 3=White)", 1.0f, 0.0f, 3.0f, 1.0f },
	[PT_BACKING_COLOR_STRENGTH]       = { "PT_BACKING_COLOR_STRENGTH", "Backing color strength", 2.0f, 0.0f, 2.0f, 0.05f },
	[PT_GLARE_AND_SHEEN]              = { "PT_GLARE_AND_SHEEN", "Glare and sheen (0=Off, 1=On)", 0.0f, 0.0f, 1.0f, 1.0f },
	[PT_GLARE_STRENGTH]               = { "PT_GLARE_STRENGTH", "Glare strength", 0.15f, 0.0f, 1.0f, 0.01f },
	[PT_GLARE_SPREAD]                 = { "PT_GLARE_SPREAD", "Glare spread", 0.35f, 0.05f, 1.0f, 0.01f },
	[PT_SHEEN_STRENGTH]               = { "PT_SHEEN_STRENGTH", "Sheen strength", 0.12f, 0.0f, 0.5f, 0.01f },
	[PT_SHEEN_BAND_DENSITY]           = { "PT_SHEEN_BAND_DENSITY", "Sheen band density", 2.0f, 0.5f, 6.0f, 0.1f },
	[PT_SHEEN_HUE_TURN]               = { "PT_SHEEN_HUE_TURN", "Sheen hue turn (1.0 = one full color cycle)", 0.0f, 0.0f, 1.0f, 0.02f },
	[PT_SHEEN_RIPPLE]                 = { "PT_SHEEN_RIPPLE", "Sheen ripple (0=smooth bands)", 0.0f, 0.0f, 1.0f, 0.05f },
	[PT_SHEEN_RIPPLE_SIZE]            = { "PT_SHEEN_RIPPLE_SIZE", "Sheen ripple size", 0.6f, 0.1f, 3.0f, 0.05f },
	[PT_LIGHT_HOME_X]                 = { "PT_LIGHT_HOME_X", "Light home, left to right (0.5=middle)", 0.5f, -0.5f, 1.5f, 0.01f },
	[PT_LIGHT_HOME_Y]                 = { "PT_LIGHT_HOME_Y", "Light home, top to bottom (0.5=middle)", 0.45f, -0.5f, 1.5f, 0.01f },
	[PT_LIGHT_DRIFT_SPEED]            = { "PT_LIGHT_DRIFT_SPEED", "Light drift speed (0=stay at home)", 0.5f, 0.0f, 4.0f, 0.05f },
	[PT_DARKEN_PICTURE]               = { "PT_DARKEN_PICTURE", "Darken picture (0=Off)", 0.0f, 0.0f, 100.0f, 1.0f },
	[PT_PIXEL_EFFECT]                 = { "PT_PIXEL_EFFECT", "Pixel Effect (0=Off, 1=Grid, 2=LCD Dot)", 1.0f, 0.0f, 2.0f, 1.0f },
	[PT_GAP_COLOR]                    = { "PT_GAP_COLOR", "Gap color (0=Backing, 1=Black, 2=White)", 0.0f, 0.0f, 2.0f, 1.0f },
	[PT_GRID_LINE_WIDTH]              = { "PT_GRID_LINE_WIDTH", "Grid line width", 0.25f, 0.0f, 0.5f, 0.01f },
	[PT_GRID_LINE_OPACITY]            = { "PT_GRID_LINE_OPACITY", "Grid line opacity", 0.50f, 0.0f, 1.0f, 0.01f },
	[PT_DOT_SIZE]                     = { "PT_DOT_SIZE", "Dot size", 0.90f, 0.1f, 0.9f, 0.01f },
	[PT_DOT_EDGE_SOFTNESS]            = { "PT_DOT_EDGE_SOFTNESS", "Dot edge softness (0=hard, 1=soft)", 1.0f, 0.0f, 1.0f, 0.01f },
	[PT_DOT_GAP_OPACITY]              = { "PT_DOT_GAP_OPACITY", "Dot gap opacity", 0.50f, 0.0f, 1.0f, 0.01f },
	[PT_MIN_BRIGHTNESS_FOR_DOTS]      = { "PT_MIN_BRIGHTNESS_FOR_DOTS", "Min brightness for dots", 0.15f, 0.0f, 1.0f, 0.01f },
	[PT_DROP_SHADOW_OPACITY]          = { "PT_DROP_SHADOW_OPACITY", "Drop shadow opacity (0=Off)", 0.30f, 0.0f, 1.0f, 0.01f },
	[PT_SHADOW_DISTANCE]              = { "PT_SHADOW_DISTANCE", "Shadow distance", 1.0f, -10.0f, 10.0f, 0.5f },
	[PT_SHADOW_DIRECTION]             = { "PT_SHADOW_DIRECTION", "Shadow direction (0=Down-R, 1=Down-L, 2=Up-R, 3=Up-L)", 0.0f, 0.0f, 3.0f, 1.0f },
	[PT_BEZEL_EDGE_SHADOW]            = { "PT_BEZEL_EDGE_SHADOW", "Bezel edge shadow (0=Off)", 0.40f, 0.0f, 1.0f, 0.01f },
};

void pt_params_default(pt_params *p)
{
	int i;
	for (i = 0; i < PT_NPARAMS; i++) p->v[i] = pt_param_table[i].def;
}

int pt_param_index(const char *name)
{
	int i;
	for (i = 0; i < PT_NPARAMS; i++)
		if (!strcmp(pt_param_table[i].name, name)) return i;
	return -1;
}

/* ---- fixed point --------------------------------------------------------- */

#define ONE      65280               /* 1.0 as an 8.8 color */
#define W1       4096                /* 1.0 as a blend weight */
#define WSHIFT   12
#define Q15      32768               /* 1.0 as a multiplier */
#define Q14      16384               /* the bezel's 1.0: a color past white times
                                        it must still fit in 32 bits */

static inline int c88(float v)       /* a 0..1 color to 8.8, rounded */
{
	return (int)(clampf(v, 0.0f, 1.0f) * (float)ONE + 0.5f);
}
static inline int wq(float v)        /* a 0..1 weight to 0..W1, rounded */
{
	return (int)(clampf(v, 0.0f, 1.0f) * (float)W1 + 0.5f);
}
static inline int q15(float v)       /* a 0..1 multiplier to Q15, rounded */
{
	return (int)(clampf(v, 0.0f, 1.0f) * (float)Q15 + 0.5f);
}

/* a toward b by w (0..W1). a and b stay within about -0.6..2.4 in 8.8, which
 * keeps every product inside 32 bits. */
#define LERP(a, b, w)  (((a) * (W1 - (w)) + (b) * (w) + W1 / 2) >> WSHIFT)

/* ---- the context ---------------------------------------------------------- */

/* One game pixel's share of the frame. */
typedef struct {
	uint16_t r, g, b;   /* 8.8, after Darken picture */
	uint16_t a;         /* weight toward the backing (0 when White mode keeps it opaque) */
	uint16_t sm;        /* the drop shadow it casts, as a grey multiplier, Q15 */
	float    fade;      /* LCD Dot's fade-in from black; 0 also stands for "leave alone" */
} pt_src;

struct pt_ctx {
	pt_params p;
	int dirty;

	int w, h, s, ow, oh;   /* s: the whole-number scale, or 0 if there is none */

	/* by position */
	uint16_t *grain;       /* ow*oh: the backing grey before shadow and tint, 8.8 */
	float    *dotrows;     /* s*ow: distance from the dot centre, per row of a game pixel
	                        * (only at a whole-number scale) */
	int      *xs, *ys;     /* ow, oh: the game column/row under each screen pixel's centre */
	int      *shx, *shy;   /* ow, oh: the game column/row casting the drop shadow */
	uint16_t *gridx, *gridy; /* ow, oh: grid weight per axis */
	uint16_t *bezx, *bezy; /* ow, oh: bezel shadow per axis, Q15 */
	uint16_t *bezcol;      /* ow: the bezel factor, Q14, on a row clear of the top and bottom edges */

	/* glare and sheen, by position. Each row records the light it was built
	 * for, so a band rebuilds only its own rows, and only when the light has
	 * moved. */
	int16_t  *glr, *glg, *glb;   /* sheen, 8.8 */
	uint16_t *glk;               /* glare amount, 0..W1 */
	float   (*rowlight)[3];      /* oh: the light each row was built for */
	float     light[3];          /* this frame's light */
	int       glare_ok;          /* rows are for the current settings */

	/* per frame */
	pt_src   *src;
	size_t    src_cap;
	int       ready;       /* pt_begin_frame succeeded for the current size */

	pt_band  *own;         /* the band pt_render uses */
};

/* One screen row's values, spread out to one entry per column. Each thread
 * drawing a band of rows needs its own. */
struct pt_band {
	int       cap;
	uint16_t *er, *eg, *eb, *ea, *esm, *em, *ebz;
	float    *efade;
};

pt_band *pt_band_create(void)
{
	return calloc(1, sizeof(pt_band));
}

void pt_band_destroy(pt_band *b)
{
	if (!b) return;
	free(b->er); free(b->eg); free(b->eb); free(b->ea);
	free(b->esm); free(b->em); free(b->ebz); free(b->efade);
	free(b);
}

static int band_fit(pt_band *b, int ow)
{
	size_t n = (size_t)ow;
	if (b->cap >= ow) return 0;
	free(b->er); free(b->eg); free(b->eb); free(b->ea);
	free(b->esm); free(b->em); free(b->ebz); free(b->efade);
	b->er = malloc(2 * n); b->eg = malloc(2 * n); b->eb = malloc(2 * n);
	b->ea = malloc(2 * n); b->esm = malloc(2 * n); b->em = malloc(2 * n);
	b->ebz = malloc(2 * n); b->efade = malloc(sizeof *b->efade * n);
	if (!b->er || !b->eg || !b->eb || !b->ea || !b->esm || !b->em || !b->ebz || !b->efade) {
		b->cap = 0;
		return -1;
	}
	b->cap = ow;
	return 0;
}

pt_ctx *pt_create(void)
{
	pt_ctx *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	pt_params_default(&c->p);
	c->dirty = 1;
	return c;
}

static void free_tables(pt_ctx *c)
{
	void **p[] = {
		(void **)&c->grain, (void **)&c->dotrows, (void **)&c->shx, (void **)&c->shy,
		(void **)&c->gridx, (void **)&c->gridy, (void **)&c->bezx, (void **)&c->bezy,
		(void **)&c->bezcol, (void **)&c->glr, (void **)&c->glg, (void **)&c->glb,
		(void **)&c->glk, (void **)&c->rowlight, (void **)&c->xs, (void **)&c->ys,
	};
	size_t i;
	for (i = 0; i < sizeof p / sizeof p[0]; i++) { free(*p[i]); *p[i] = NULL; }
	c->w = c->h = c->s = c->ow = c->oh = 0;
	c->glare_ok = 0;
}

void pt_destroy(pt_ctx *c)
{
	if (!c) return;
	free_tables(c);
	pt_band_destroy(c->own);
	free(c->src);
	free(c);
}

void pt_set_params(pt_ctx *c, const pt_params *p)
{
	if (memcmp(&c->p, p, sizeof *p)) {
		c->p = *p;
		c->dirty = 1;
		c->glare_ok = 0;
	}
}

/* ---- tables ---------------------------------------------------------------- */

static int build_geometry(pt_ctx *c, int w, int h, int ow, int oh)
{
	int s = ow % w == 0 && oh % h == 0 && ow / w == oh / h ? ow / w : 0, x, y;
	size_t n = (size_t)ow;

	free_tables(c);
	c->grain   = malloc(sizeof *c->grain * n * oh);
	c->dotrows = malloc(sizeof *c->dotrows * n * (s ? s : 1));
	c->xs = malloc(sizeof *c->xs * n);        c->ys = malloc(sizeof *c->ys * oh);
	c->shx = malloc(sizeof *c->shx * n);      c->shy = malloc(sizeof *c->shy * oh);
	c->gridx = malloc(sizeof *c->gridx * n);  c->gridy = malloc(sizeof *c->gridy * oh);
	c->bezx = malloc(sizeof *c->bezx * n);    c->bezy = malloc(sizeof *c->bezy * oh);
	c->bezcol = malloc(sizeof *c->bezcol * n);
	if (!c->grain || !c->dotrows || !c->xs || !c->ys || !c->shx || !c->shy || !c->gridx || !c->gridy ||
	    !c->bezx || !c->bezy || !c->bezcol) { free_tables(c); return -1; }
	c->w = w; c->h = h; c->s = s; c->ow = ow; c->oh = oh;

	for (y = 0; y < oh; y++) {
		float v = pt_tex0(y, oh);
		uint16_t *row = c->grain + (size_t)y * ow;
		for (x = 0; x < ow; x++)
			row[x] = (uint16_t)c88(pt_grain(pt_tex0(x, ow), v));
	}
	/* nearest: the game pixel under the screen pixel's centre, worked out
	 * as the shader's texture lookup is (and its drop shadow, pt_shadow_src) */
	for (x = 0; x < ow; x++) {
		int sx = (int)floorf(pt_tex0(x, ow) * (float)w);
		c->xs[x] = sx < 0 ? 0 : sx < w ? sx : w - 1;
	}
	for (y = 0; y < oh; y++) {
		int sy = (int)floorf(pt_tex0(y, oh) * (float)h);
		c->ys[y] = sy < 0 ? 0 : sy < h ? sy : h - 1;
	}
	/* The dot's distance repeats every game pixel at a whole-number scale
	 * (and the Grid and LCD Dot are drawn only at one). */
	for (y = 0; y < s; y++)
		for (x = 0; x < ow; x++)
			c->dotrows[(size_t)y * ow + x] = pt_dot_dist(x % s, y, ow, oh, w, h);
	c->dirty = 1;
	return 0;
}

static void build_settings(pt_ctx *c)
{
	const float *P = c->p.v;
	int ow = c->ow, oh = c->oh, x, y, bz;
	float dx, dy, sc;

	pt_shadow_dir(P, &dx, &dy);
	for (x = 0; x < ow; x++) c->shx[x] = pt_shadow_src(x, ow, c->w, dx, P[PT_SHADOW_DISTANCE]);
	for (y = 0; y < oh; y++) c->shy[y] = pt_shadow_src(y, oh, c->h, dy, P[PT_SHADOW_DISTANCE]);

	for (x = 0; x < ow; x++) c->gridx[x] = (uint16_t)wq(pt_grid_axis(x, ow, c->w, P));
	for (y = 0; y < oh; y++) c->gridy[y] = (uint16_t)wq(pt_grid_axis(y, oh, c->h, P));

	sc = pt_bezel_scale(P);
	for (x = 0; x < ow; x++) c->bezx[x] = (uint16_t)q15(pt_bezel_axis(x, ow, sc));
	for (y = 0; y < oh; y++) c->bezy[y] = (uint16_t)q15(pt_bezel_axis(y, oh, sc));
	/* mix(1 - B, 1, shade) == 1 - B + B * shade, in Q14 */
	bz = q15(P[PT_BEZEL_EDGE_SHADOW]);
	for (x = 0; x < ow; x++)
		c->bezcol[x] = (uint16_t)((Q15 - bz + ((bz * c->bezx[x] + Q15 / 2) >> 15) + 1) >> 1);
	c->dirty = 0;
}

/* Glare and sheen for one screen row: pt_glare_at from pt_math.h, written as
 * one loop with no calls and no branches so it vectorizes. A part the shader
 * skips (strength at or under 0.001) is computed with a strength of exactly
 * 0, which gives exactly the 0 the shader's skip gives. */
static void glare_row(const pt_ctx *c, int Y)
{
	const float *P = c->p.v;
	const int ow = c->ow;
	const float aspect = (float)c->w / (float)c->h;
	const float L0 = c->light[0], L1 = c->light[1], L2 = c->light[2];
	const float gy = pt_tex0(Y, c->oh);
	const float sheen = P[PT_SHEEN_STRENGTH] > 0.001f ? P[PT_SHEEN_STRENGTH] : 0.0f;
	const float ripple = P[PT_SHEEN_RIPPLE] > 0.001f ? P[PT_SHEEN_RIPPLE] : 0.0f;
	const float rf = 3.0f / P[PT_SHEEN_RIPPLE_SIZE];
	const float dens = P[PT_SHEEN_BAND_DENSITY];
	const float gs = P[PT_GLARE_STRENGTH] > 0.001f ? P[PT_GLARE_STRENGTH] : 0.0f;
	const float sp2 = P[PT_GLARE_SPREAD] * P[PT_GLARE_SPREAD];
	int16_t *restrict gr = c->glr + (size_t)Y * ow;
	int16_t *restrict gg = c->glg + (size_t)Y * ow;
	int16_t *restrict gb = c->glb + (size_t)Y * ow;
	uint16_t *restrict gk = c->glk + (size_t)Y * ow;
	int X;

	for (X = 0; X < ow; X++) {
		float gx = ((float)X + 0.5f) / (float)ow;
		float dx = (gx - L0) * aspect, dy = (gy - L1) * 1.0f;
		float d2 = dx * dx + dy * dy;
		float ph = sqrtf(d2) * dens + L2;
		float ux = gx * rf, uy = gy * rf;
		float t0 = fabsf(fractf(ux * 1.00f + uy * 0.37f) * 2.0f - 1.0f);
		float t1 = fabsf(fractf(uy * 1.13f - ux * 0.61f + 0.31f) * 2.0f - 1.0f);
		float t2 = fabsf(fractf(ux * 0.43f - uy * 1.71f + 0.72f) * 2.0f - 1.0f);
		float h0, h1, h2, w0, w1, w2, r0, r1, r2, avg, q, gl, v;

		t0 = t0 * t0 * (3.0f - 2.0f * t0);
		t1 = t1 * t1 * (3.0f - 2.0f * t1);
		t2 = t2 * t2 * (3.0f - 2.0f * t2);
		ph += (t0 + t1 + t2 - 1.5f) * ripple * 0.45f;
		h0 = fractf(ph + 0.0f); h1 = fractf(ph + 0.3333f); h2 = fractf(ph + 0.6667f);
		w0 = fabsf(h0 * 2.0f - 1.0f); w1 = fabsf(h1 * 2.0f - 1.0f); w2 = fabsf(h2 * 2.0f - 1.0f);
		r0 = w0 * w0 * (3.0f - 2.0f * w0);
		r1 = w1 * w1 * (3.0f - 2.0f * w1);
		r2 = w2 * w2 * (3.0f - 2.0f * w2);
		avg = r0 * 0.3333f + r1 * 0.3333f + r2 * 0.3333f;

		q = d2 / sp2;
		gl = gs / (1.0f + q * q);
		gl = gl < 0.85f ? gl : 0.85f;

		/* rounded half away from zero; the sheen fits int16 in 8.8 */
		v = (r0 - avg) * sheen * (float)ONE; gr[X] = (int16_t)(v + (v < 0.0f ? -0.5f : 0.5f));
		v = (r1 - avg) * sheen * (float)ONE; gg[X] = (int16_t)(v + (v < 0.0f ? -0.5f : 0.5f));
		v = (r2 - avg) * sheen * (float)ONE; gb[X] = (int16_t)(v + (v < 0.0f ? -0.5f : 0.5f));
		gk[X] = (uint16_t)(gl * (float)W1 + 0.5f);
	}
}

static int glare_alloc(pt_ctx *c)
{
	size_t n = (size_t)c->ow * c->oh;
	int y;
	if (!c->glr) {
		c->glr = malloc(2 * n); c->glg = malloc(2 * n);
		c->glb = malloc(2 * n); c->glk = malloc(2 * n);
		c->rowlight = malloc(sizeof *c->rowlight * (size_t)c->oh);
		if (!c->glr || !c->glg || !c->glb || !c->glk || !c->rowlight) return -1;
		c->glare_ok = 0;
	}
	if (!c->glare_ok) {
		/* nothing matches NaN: every row is rebuilt */
		for (y = 0; y < c->oh; y++) c->rowlight[y][0] = c->rowlight[y][1] = c->rowlight[y][2] = NAN;
		c->glare_ok = 1;
	}
	return 0;
}

/* ---- per frame: the game pixels -------------------------------------------- */

static void prepare_source(pt_ctx *c, const uint32_t *src, size_t pitch)
{
	const float *P = c->p.v;
	float th = pt_threshold(P);
	float tp = P[PT_TRANSPARENT_PIXELS], amt = P[PT_TRANSPARENCY_AMOUNT];
	float dark = P[PT_DARKEN_PICTURE], shop = P[PT_DROP_SHADOW_OPACITY];
	float minb = fmaxf(P[PT_MIN_BRIGHTNESS_FOR_DOTS], 0.001f);
	int x, y;

	for (y = 0; y < c->h; y++)
		for (x = 0; x < c->w; x++) {
			uint32_t px = src[(size_t)y * pitch + x];
			float rr = (float)((px >> 16) & 255) / 255.0f;
			float rg = (float)((px >> 8) & 255) / 255.0f;
			float rb = (float)(px & 255) / 255.0f;
			float rawb = pt_bright(rr, rg, rb), pixb = rawb;
			float r = rr, g = rg, b = rb, alpha;
			int white = pt_is_white(rr, rg, rb, rawb, th);
			pt_src *o = &c->src[(size_t)y * c->w + x];

			if (dark > 0.5f) {
				float k = fmaxf(1.0f - dark * 0.01f * pixb, 0.0f);
				r *= k; g *= k; b *= k;
				pixb = pt_bright(r, g, b);
			}
			if (tp < 0.5f)      alpha = clampf(pixb / 3.0f + amt, 0.0f, 1.0f);
			else if (tp < 1.5f) alpha = clampf(amt * pixb * 2.665f, 0.0f, 1.0f);
			else                alpha = clampf(pixb / 3.0f + amt, 0.0f, 1.0f);
			if (white) alpha = fmaxf(alpha, P[PT_MINIMUM_WHITE_TRANSPARENCY]);

			o->r = (uint16_t)c88(r); o->g = (uint16_t)c88(g); o->b = (uint16_t)c88(b);
			o->a = (tp < 0.5f && !white) ? 0 : (uint16_t)wq(alpha);
			/* LCD Dot leaves raw brightness under 0.01 alone: a fade of 0 */
			o->fade = rawb < 0.01f ? 0.0f : smoothstepf(0.0f, minb, rawb);
			/* mix(bg, bg * 0.2, s) == bg * mix(1, 0.2, s) */
			o->sm = (uint16_t)q15(mixf(1.0f, 0.2f, white ? 0.0f : (1.0f - rawb) * shop));
		}
}

/* ---- per frame: the screen --------------------------------------------------- */

enum { FX_OFF, FX_GRID, FX_DOT };
enum { GAP_BACKING, GAP_BLACK, GAP_WHITE };

/* Spread one game row over a screen row. */
static void spread_source(const pt_ctx *c, pt_band *e, int sy)
{
	const pt_src *row = c->src + (size_t)sy * c->w;
	int X;
	for (X = 0; X < c->ow; X++) {
		const pt_src *p = &row[c->xs[X]];
		e->er[X] = p->r; e->eg[X] = p->g; e->eb[X] = p->b;
		e->ea[X] = p->a; e->efade[X] = p->fade;
	}
}

/* The drop shadow falling on one screen row: the multiplier cast by the game
 * pixel one shadow-distance away, per column. */
static void spread_shadow(const pt_ctx *c, pt_band *e, int shy, int on)
{
	int X;
	if (!on) {
		for (X = 0; X < c->ow; X++) e->esm[X] = Q15;
		return;
	}
	for (X = 0; X < c->ow; X++)
		e->esm[X] = c->src[(size_t)shy * c->w + c->shx[X]].sm;
}

static inline __attribute__((always_inline))
void render_rows(const pt_ctx *c, pt_band *e, int y0, int y1,
                 uint32_t *dst, size_t pitch, const pt_format *fmt,
                 const int FX, const int GAP, const int GLARE, const int BEZEL)
{
	const float *P = c->p.v;
	const int ow = c->ow, s = c->s;
	const int shadow = P[PT_DROP_SHADOW_OPACITY] > 0.001f;
	const int gapc = GAP == GAP_WHITE ? ONE : 0;
	const unsigned rs = fmt->r_shift, gs = fmt->g_shift, bs = fmt->b_shift;
	const uint32_t opq = fmt->opaque;
	int tr = 0, tg = 0, tb = 0, str = 0;
	const float dot_half = P[PT_DOT_SIZE] * 0.5f, dot_soft = P[PT_DOT_EDGE_SOFTNESS];
	const float dot_op = P[PT_DOT_GAP_OPACITY];
	const int bz = q15(P[PT_BEZEL_EDGE_SHADOW]);
	int Y, last_sy = -1, last_shy = -1;

	/* No backing color is a strength of 0: the backing stays the grey. */
	if (P[PT_BACKING_COLOR] > 0.5f) {
		float t[3];
		pt_tint(P[PT_BACKING_COLOR], t);
		tr = c88(t[0]); tg = c88(t[1]); tb = c88(t[2]);
		str = (int)lrintf(P[PT_BACKING_COLOR_STRENGTH] * (float)W1);
	}

	for (Y = y0; Y < y1; Y++) {
		const int sy = c->ys[Y];
		const uint16_t *restrict grain = c->grain + (size_t)Y * ow;
		const uint16_t *restrict er = e->er, *restrict eg = e->eg, *restrict eb = e->eb;
		const uint16_t *restrict ea = e->ea, *restrict esm = e->esm;
		const uint16_t *restrict em = e->em, *restrict ebz = e->ebz;
		const float *restrict efade = e->efade;
		const float *restrict edist = FX == FX_DOT ? c->dotrows + (size_t)(Y % s) * ow : NULL;
		const int16_t *restrict glr = GLARE ? c->glr + (size_t)Y * ow : NULL;
		const int16_t *restrict glg = GLARE ? c->glg + (size_t)Y * ow : NULL;
		const int16_t *restrict glb = GLARE ? c->glb + (size_t)Y * ow : NULL;
		const uint16_t *restrict glk = GLARE ? c->glk + (size_t)Y * ow : NULL;
		uint32_t *restrict out = dst + (size_t)(Y - y0) * pitch;   /* dst is row y0 */
		int X;

		if (sy != last_sy) { spread_source(c, e, sy); last_sy = sy; last_shy = -1; }
		if (c->shy[Y] != last_shy) { spread_shadow(c, e, c->shy[Y], shadow); last_shy = c->shy[Y]; }
		if (FX == FX_GRID) {
			const int gy = c->gridy[Y];
			for (X = 0; X < ow; X++) e->em[X] = c->gridx[X] > gy ? c->gridx[X] : gy;
		}
		if (BEZEL) {
			if (c->bezy[Y] < Q15) {
				const int by = c->bezy[Y];
				for (X = 0; X < ow; X++)
					e->ebz[X] = (uint16_t)((Q15 - bz + ((bz * ((c->bezx[X] * by + Q15 / 2) >> 15) + Q15 / 2) >> 15) + 1) >> 1);
			} else {
				memcpy(e->ebz, c->bezcol, sizeof *e->ebz * (size_t)ow);
			}
		}

		for (X = 0; X < ow; X++) {
			/* the backing: shadowed grey, then the tint (computeBacking) */
			int gp = (grain[X] * esm[X] + Q15 / 2) >> 15;
			int lr = tr + 2 * gp - ONE, lg = tg + 2 * gp - ONE, lb = tb + 2 * gp - ONE;
			int br, bgc, bb, a = ea[X], r, g, b;

			lr = lr < 0 ? 0 : (lr > ONE ? ONE : lr);
			lg = lg < 0 ? 0 : (lg > ONE ? ONE : lg);
			lb = lb < 0 ? 0 : (lb > ONE ? ONE : lb);
			br  = gp + (((lr - gp) * str + W1 / 2) >> WSHIFT);
			bgc = gp + (((lg - gp) * str + W1 / 2) >> WSHIFT);
			bb  = gp + (((lb - gp) * str + W1 / 2) >> WSHIFT);

			if (GLARE) {
				/* clamp(mix(bg + sheen, 1, glare), 0, 1) */
				int k = glk[X];
				br += glr[X]; bgc += glg[X]; bb += glb[X];
				br  += ((ONE - br)  * k + W1 / 2) >> WSHIFT;
				bgc += ((ONE - bgc) * k + W1 / 2) >> WSHIFT;
				bb  += ((ONE - bb)  * k + W1 / 2) >> WSHIFT;
				br  = br  < 0 ? 0 : (br  > ONE ? ONE : br);
				bgc = bgc < 0 ? 0 : (bgc > ONE ? ONE : bgc);
				bb  = bb  < 0 ? 0 : (bb  > ONE ? ONE : bb);
			}

			if (FX == FX_GRID && GAP == GAP_BACKING) {
				/* blending toward the backing twice, by a then by m, is one
				 * blend by 1 - (1 - a)(1 - m) */
				int m = em[X];
				int wgt = a + m - ((a * m + W1 / 2) >> WSHIFT);
				r = LERP(er[X], br, wgt); g = LERP(eg[X], bgc, wgt); b = LERP(eb[X], bb, wgt);
			} else {
				r = LERP(er[X], br, a); g = LERP(eg[X], bgc, a); b = LERP(eb[X], bb, a);
				if (FX == FX_GRID) {
					int m = em[X];
					r = LERP(r, gapc, m); g = LERP(g, gapc, m); b = LERP(b, gapc, m);
				}
			}

			if (FX == FX_DOT) {
				/* applyLCDDot: brightness sets the dot's radius */
				float luma = ((float)r * PT_LUMA_R + (float)g * PT_LUMA_G + (float)b * PT_LUMA_B) * (1.0f / (float)ONE);
				float radius = dot_half + 0.08f * luma;
				float soft = dot_soft * radius;
				float mask;
				soft = soft > 0.02f ? soft : 0.02f;          /* max(), kept inline so the loop vectorizes */
				mask = (radius - edist[X]) / soft;
				mask = mask < 0.0f ? 0.0f : (mask > 1.0f ? 1.0f : mask);
				int t = (int)((1.0f - mask) * dot_op * efade[X] * (float)W1 + 0.5f);
				if (GAP == GAP_BACKING) {
					r = LERP(r, br, t); g = LERP(g, bgc, t); b = LERP(b, bb, t);
				} else {
					r = LERP(r, gapc, t); g = LERP(g, gapc, t); b = LERP(b, gapc, t);
				}
			}

			if (BEZEL) {
				int f = ebz[X];
				r = (r * f + Q14 / 2) >> 14; g = (g * f + Q14 / 2) >> 14; b = (b * f + Q14 / 2) >> 14;
			}

			r = r < 0 ? 0 : (r > ONE ? ONE : r);
			g = g < 0 ? 0 : (g > ONE ? ONE : g);
			b = b < 0 ? 0 : (b > ONE ? ONE : b);
			out[X] = opq | ((uint32_t)((r + 128) >> 8) << rs)
			             | ((uint32_t)((g + 128) >> 8) << gs)
			             | ((uint32_t)((b + 128) >> 8) << bs);
		}
	}
}

typedef void (*rows_fn)(const pt_ctx *, pt_band *, int, int, uint32_t *, size_t, const pt_format *);

#define ROWS(FX, GAP, GL, BZ) \
	static void rows_##FX##_##GAP##_##GL##_##BZ(const pt_ctx *c, pt_band *e, int y0, int y1, \
		uint32_t *d, size_t p, const pt_format *f) \
	{ render_rows(c, e, y0, y1, d, p, f, FX, GAP, GL, BZ); }
#define ROWS_BZ(FX, GAP, GL)  ROWS(FX, GAP, GL, 0) ROWS(FX, GAP, GL, 1)
#define ROWS_GL(FX, GAP)      ROWS_BZ(FX, GAP, 0) ROWS_BZ(FX, GAP, 1)
#define ROWS_GAP(FX)          ROWS_GL(FX, 0) ROWS_GL(FX, 1) ROWS_GL(FX, 2)
ROWS_GAP(0) ROWS_GAP(1) ROWS_GAP(2)

#define ENT(FX, GAP, GL, BZ)  rows_##FX##_##GAP##_##GL##_##BZ
#define ENT_BZ(FX, GAP, GL)   ENT(FX, GAP, GL, 0), ENT(FX, GAP, GL, 1)
#define ENT_GL(FX, GAP)       ENT_BZ(FX, GAP, 0), ENT_BZ(FX, GAP, 1)
#define ENT_GAP(FX)           ENT_GL(FX, 0), ENT_GL(FX, 1), ENT_GL(FX, 2)
/* Indexed ((fx * 3 + gap) * 2 + glare) * 2 + bezel. */
static const rows_fn rows_table[3 * 3 * 2 * 2] = { ENT_GAP(0), ENT_GAP(1), ENT_GAP(2) };

int pt_begin_frame(pt_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
                   int s, unsigned frame)
{
	if (s <= 0) { c->ready = 0; return -1; }
	return pt_begin_frame_size(c, src, w, h, src_pitch, w * s, h * s, frame);
}

int pt_begin_frame_size(pt_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
                        int ow, int oh, unsigned frame)
{
	const float *P = c->p.v;

	c->ready = 0;
	if (w <= 0 || h <= 0 || ow <= 0 || oh <= 0) return -1;
	if ((c->w != w || c->h != h || c->ow != ow || c->oh != oh) &&
	    build_geometry(c, w, h, ow, oh)) return -1;
	if (c->dirty) build_settings(c);

	if ((size_t)w * h > c->src_cap) {
		pt_src *n = realloc(c->src, sizeof *n * (size_t)w * h);
		if (!n) return -1;
		c->src = n; c->src_cap = (size_t)w * h;
	}
	prepare_source(c, src, src_pitch);

	if (P[PT_GLARE_AND_SHEEN] > 0.5f) {
		pt_light(P, frame, c->light);
		if (glare_alloc(c)) return -1;
	}
	c->ready = 1;
	return 0;
}

static int band_rows(const pt_ctx *c, pt_band *b, int y0, int y1,
                     uint32_t *dst, size_t dst_pitch, const pt_format *fmt, int whole);

int pt_render_band(const pt_ctx *c, pt_band *b, int y0, int y1,
                   uint32_t *dst, size_t dst_pitch, const pt_format *fmt)
{
	return band_rows(c, b, y0, y1, dst, dst_pitch, fmt, 1);
}

int pt_render_rows(const pt_ctx *c, pt_band *b, int y0, int y1,
                   uint32_t *rows, size_t rows_pitch, const pt_format *fmt)
{
	return band_rows(c, b, y0, y1, rows, rows_pitch, fmt, 0);
}

/* `whole`: dst is the top-left of the whole output; otherwise row y0. */
static int band_rows(const pt_ctx *c, pt_band *b, int y0, int y1,
                     uint32_t *dst, size_t dst_pitch, const pt_format *fmt, int whole)
{
	const float *P = c->p.v;
	int fx, gap, glare, bezel;

	if (!c->ready || band_fit(b, c->ow)) return -1;
	if (y0 < 0) {
		if (!whole) dst += (size_t)(-y0) * dst_pitch;
		y0 = 0;
	}
	if (y1 > c->oh) y1 = c->oh;
	if (y0 >= y1) return 0;
	if (whole) dst += (size_t)y0 * dst_pitch;

	fx    = P[PT_PIXEL_EFFECT] < 0.5f ? FX_OFF : (P[PT_PIXEL_EFFECT] < 1.5f ? FX_GRID : FX_DOT);
	/* the Grid and LCD Dot follow the game's pixels, which away from a
	 * whole-number scale are not all the same size: off there */
	if (!c->s) fx = FX_OFF;
	gap   = P[PT_GAP_COLOR] < 0.5f ? GAP_BACKING : (P[PT_GAP_COLOR] < 1.5f ? GAP_BLACK : GAP_WHITE);
	glare = P[PT_GLARE_AND_SHEEN] > 0.5f;
	bezel = P[PT_BEZEL_EDGE_SHADOW] >= 0.001f;

	if (glare) {
		/* this band's rows only, so bands never write the same row */
		int y;
		for (y = y0; y < y1; y++)
			if (memcmp(c->rowlight[y], c->light, sizeof c->light)) {
				glare_row(c, y);
				memcpy(c->rowlight[y], c->light, sizeof c->light);
			}
	}
	rows_table[((fx * 3 + gap) * 2 + glare) * 2 + bezel](c, b, y0, y1, dst, dst_pitch, fmt);
	return 0;
}

int pt_render(pt_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
              int s, uint32_t *dst, size_t dst_pitch, const pt_format *fmt,
              unsigned frame)
{
	if (!c->own && !(c->own = pt_band_create())) return -1;
	if (pt_begin_frame(c, src, w, h, src_pitch, s, frame)) return -1;
	return pt_render_band(c, c->own, 0, h * s, dst, dst_pitch, fmt);
}
