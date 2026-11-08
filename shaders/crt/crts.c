/* crt-lottes-fast on the CPU (ChaOS). See crts.h.
 *
 * Follows crt/shaders/crt-lottes-fast.glsl (libretro glsl-shaders), Timothy
 * Lottes' CRTS, public domain (Unlicense), step for step, in single-precision
 * float as a GPU would, with these differences of means, not of picture:
 *
 *   - the game's pixels are taken to linear light once per frame (FromSrgb at
 *     CRT_GAMMA), not per fetch;
 *   - the output's ToSrgb is a table of 4096 steps;
 *   - without curvature, the separable work is done per column and per row,
 *     not per pixel (see crts.h); the corner vignette, which the GLSL's
 *     always-on warp applies even then, is still per pixel;
 *   - with curvature, the scanline weights (a cosine of the distance between
 *     rows) and the horizontal Gaussian's four weights (over their sum) are
 *     tables over that distance, read with linear interpolation;
 *   - the tone curve's ratio * peak is the colour over (peak * y + z), one
 *     division, as it is algebraically.
 *
 * libretro's GLSL declares its sizes mediump, and a GPU that keeps mediump at
 * half precision (Mesa's llvmpipe does) works out InputSize / OutputSize to
 * three decimal places or so, which slides the scanlines a little further
 * down the screen at every row. That is a GPU's rounding, not the shader's
 * design, and is not copied: test/pt/othc.c compares with the GLSL at highp.
 *
 * Texels outside the frame are its edge's, as a GLES texture clamped to edge
 * gives them.
 */
#include "crts.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const crts_param crts_params[CRTS_NPARAMS] = {
	[CRTS_MASK]              = { "MASK",              1.0f,  0.0f,  3.0f,  1.0f  },
	[CRTS_MASK_INTENSITY]    = { "MASK_INTENSITY",    0.5f,  0.0f,  1.0f,  0.05f },
	[CRTS_SCANLINE_THINNESS] = { "SCANLINE_THINNESS", 0.5f,  0.0f,  1.0f,  0.1f  },
	[CRTS_SCAN_BLUR]         = { "SCAN_BLUR",         2.5f,  1.0f,  6.0f,  0.5f  },
	[CRTS_CURVATURE]         = { "CURVATURE",         0.02f, 0.0f,  0.25f, 0.01f },
	[CRTS_TRINITRON_CURVE]   = { "TRINITRON_CURVE",   0.0f,  0.0f,  1.0f,  1.0f  },
	[CRTS_CORNER]            = { "CORNER",            3.0f,  0.0f,  11.0f, 1.0f  },
	[CRTS_CRT_GAMMA]         = { "CRT_GAMMA",         2.4f,  0.0f,  51.0f, 0.1f  },
};

int crts_param_index(const char *name)
{
	int i;
	for (i = 0; i < CRTS_NPARAMS; i++)
		if (!strcmp(crts_params[i].name, name)) return i;
	return -1;
}

#define TO_SRGB_STEPS 4096
#define SCAN_N 1024               /* the scanline table's steps over a row */
#define HW_N   256                /* the Gaussian table's steps over a texel */
#define PADX   3                  /* edge texels repeated left and right */
#define PADY   1                  /* and above and below */

struct crts_ctx {
	float p[CRTS_NPARAMS];
	float lin[256];                 /* FromSrgb at CRT_GAMMA, per 8-bit level */
	uint8_t to_srgb[TO_SRGB_STEPS + 1];
	float tone_y, tone_z;           /* CrtsTone's ret.y and ret.z */
	float thin, blur, mask;         /* INPUT_THIN, INPUT_BLUR, INPUT_MASK */
	int   mtype;                    /* MASK, 0..3 */
	float warp_x, warp_y, corner;
	float scan[SCAN_N + 2][2];      /* scanA, scanB by the distance down */
	float hw[HW_N + 2][4];          /* the four weights over their sum */
	/* the frame, linear, with its edges repeated PADX and PADY deep */
	float *img;
	size_t img_cap;
	int w, h, s, ow, oh, stride;    /* stride: texels a padded row */
	/* per screen column and per screen row */
	float *cx, *cfy, *col_v, *col_w;
	int   *col_x0;
	float *ry, *rfx, *row_v, *row_a, *row_b;
	int   *row_y0;
	size_t col_cap, row_cap;
	int ready;
};

struct crts_band {
	float *ha, *hb;                 /* a game row filtered across, ow * 3 each */
	int    ya, yb;                  /* which game rows they are */
	size_t cap;
};

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static float from_srgb1(float c, float gamma)
{
	return c <= 0.04045f ? c * (1.0f / 12.92f)
	                     : powf(c * (1.0f / 1.055f) + (0.055f / 1.055f), gamma);
}

static float to_srgb1(float c)
{
	return c < 0.0031308f ? c * 12.92f : 1.055f * powf(c, 0.41666f) - 0.055f;
}

static void scan_w(float thin, float off, float *a, float *b)
{
	const float pi2 = 6.28318530717958f;
	*a = cosf(fminf(0.5f, off * thin) * pi2) * 0.5f + 0.5f;
	*b = cosf(fminf(0.5f, (-off) * thin + thin) * pi2) * 0.5f + 0.5f;
}

/* The four horizontal weights over their sum; off0 is the distance from
 * the first texel, 1 to 2 */
static void gauss_w(float blur, float off0, float *w)
{
	float t = 0.0f;
	int k;
	for (k = 0; k < 4; k++) {
		float o = off0 - (float)k;
		w[k] = exp2f(blur * o * o);
		t += w[k];
	}
	t = 1.0f / t;
	for (k = 0; k < 4; k++) w[k] *= t;
}

crts_ctx *crts_create(void)
{
	crts_ctx *c = calloc(1, sizeof *c);
	int i;

	if (!c) return NULL;
	for (i = 0; i < CRTS_NPARAMS; i++) c->p[i] = crts_params[i].def;
	crts_set_params(c, c->p);
	return c;
}

void crts_destroy(crts_ctx *c)
{
	if (!c) return;
	free(c->img);
	free(c->cx); free(c->cfy); free(c->col_v); free(c->col_w); free(c->col_x0);
	free(c->ry); free(c->rfx); free(c->row_v); free(c->row_a); free(c->row_b); free(c->row_y0);
	free(c);
}

void crts_set_params(crts_ctx *c, const float *v)
{
	float mask_t, mid_out, pmid = 0.18f;
	int i;

	for (i = 0; i < CRTS_NPARAMS; i++) c->p[i] = v[i];
	for (i = 0; i < 256; i++) c->lin[i] = from_srgb1((float)i / 255.0f, c->p[CRTS_CRT_GAMMA]);
	for (i = 0; i <= TO_SRGB_STEPS; i++) {
		float s = to_srgb1((float)i / TO_SRGB_STEPS) * 255.0f + 0.5f;
		c->to_srgb[i] = (uint8_t)clampf(s, 0.0f, 255.0f);
	}
	c->mtype = (int)floorf(c->p[CRTS_MASK] + 0.5f);
	c->thin  = 0.5f + 0.5f * c->p[CRTS_SCANLINE_THINNESS];
	c->blur  = -1.0f * c->p[CRTS_SCAN_BLUR];
	c->mask  = 1.0f - c->p[CRTS_MASK_INTENSITY];
	c->corner = 0.998f + 0.001f * c->p[CRTS_CORNER];

	/* CrtsTone(1.0, 0.0, thin, mask): contrast 1 and saturation 0 leave
	 * peak and ratio as they are, so only y and z are used */
	mask_t = c->mask;
	if (c->mtype == 0) mask_t = 1.0f;
	if (c->mtype == 1) mask_t = 0.5f + mask_t * 0.5f;
	mid_out = 0.18f / ((1.5f - c->thin) * (0.5f * mask_t + 0.5f));
	c->tone_y = ((-pmid) + mid_out) / ((1.0f - pmid) * mid_out);
	c->tone_z = ((-pmid) * mid_out + pmid) / (mid_out * (-pmid) + mid_out);

	c->warp_x = c->p[CRTS_CURVATURE];
	c->warp_y = (3.0f / 4.0f) * c->warp_x;
	c->warp_x *= (1.0f - c->p[CRTS_TRINITRON_CURVE]);

	/* one more entry past the end, so the interpolation can read i + 1 */
	for (i = 0; i <= SCAN_N + 1; i++)
		scan_w(c->thin, (float)i / SCAN_N, &c->scan[i][0], &c->scan[i][1]);
	for (i = 0; i <= HW_N + 1; i++)
		gauss_w(c->blur, 1.0f + (float)i / HW_N, c->hw[i]);
	c->w = c->h = c->s = 0;       /* the column and row tables again */
	c->ready = 0;
}

crts_band *crts_band_create(void)
{
	return calloc(1, sizeof(crts_band));
}

void crts_band_destroy(crts_band *b)
{
	if (!b) return;
	free(b->ha); free(b->hb);
	free(b);
}

static int cols(crts_ctx *c, size_t n)
{
	if (n <= c->col_cap) return 0;
	free(c->cx); free(c->cfy); free(c->col_v); free(c->col_w); free(c->col_x0);
	c->cx = malloc(n * sizeof(float));
	c->cfy = malloc(n * sizeof(float));
	c->col_v = malloc(n * sizeof(float));
	c->col_w = malloc(n * 4 * sizeof(float));
	c->col_x0 = malloc(n * sizeof(int));
	c->col_cap = c->cx && c->cfy && c->col_v && c->col_w && c->col_x0 ? n : 0;
	return c->col_cap ? 0 : -1;
}

static int rows(crts_ctx *c, size_t n)
{
	if (n <= c->row_cap) return 0;
	free(c->ry); free(c->rfx); free(c->row_v); free(c->row_a); free(c->row_b); free(c->row_y0);
	c->ry = malloc(n * sizeof(float));
	c->rfx = malloc(n * sizeof(float));
	c->row_v = malloc(n * sizeof(float));
	c->row_a = malloc(n * sizeof(float));
	c->row_b = malloc(n * sizeof(float));
	c->row_y0 = malloc(n * sizeof(int));
	c->row_cap = c->ry && c->rfx && c->row_v && c->row_a && c->row_b && c->row_y0 ? n : 0;
	return c->row_cap ? 0 : -1;
}

static int clampi(int x, int a, int b) { return x < a ? a : x > b ? b : x; }

int crts_begin_frame(crts_ctx *c, const uint32_t *src, int w, int h,
                     size_t src_pitch, int s)
{
	int x, y, X, Y, stride = w + 2 * PADX;
	size_t n = (size_t)stride * (h + 2 * PADY) * 3;

	c->ready = 0;
	if (w <= 0 || h <= 0 || s < 1) return -1;
	if (n > c->img_cap) {
		free(c->img);
		c->img = malloc(n * sizeof(float));
		c->img_cap = c->img ? n : 0;
		if (!c->img) return -1;
	}
	for (y = -PADY; y < h + PADY; y++) {
		const uint32_t *in = src + (size_t)clampi(y, 0, h - 1) * src_pitch;
		float *o = c->img + (size_t)(y + PADY) * stride * 3;
		for (x = -PADX; x < w + PADX; x++) {
			uint32_t p = in[clampi(x, 0, w - 1)];
			float *t = o + (x + PADX) * 3;
			t[0] = c->lin[(p >> 16) & 0xff];
			t[1] = c->lin[(p >> 8) & 0xff];
			t[2] = c->lin[p & 0xff];
		}
	}
	if (w != c->w || h != c->h || s != c->s) {
		int ow = w * s, oh = h * s;
		if (cols(c, (size_t)ow) || rows(c, (size_t)oh)) return -1;
		for (X = 0; X < ow; X++) {
			float px = ((float)X + 0.5f) * ((float)w / (float)ow);
			float x0 = floorf(px - 1.5f) + 0.5f;
			gauss_w(c->blur, px - x0, c->col_w + X * 4);
			c->col_x0[X] = clampi((int)(x0 - 0.5f), -PADX, w - 1);
			px = ((float)X + 0.5f) * (2.0f / (float)ow) - 1.0f;
			c->cx[X] = px;
			c->col_v[X] = 1.0f - clampf(px * px, 0.0f, 1.0f);
		}
		for (Y = 0; Y < oh; Y++) {
			float py = ((float)Y + 0.5f) * ((float)h / (float)oh);
			float y0 = floorf(py - 0.5f) + 0.5f;
			scan_w(c->thin, py - y0, &c->row_a[Y], &c->row_b[Y]);
			c->row_y0[Y] = clampi((int)(y0 - 0.5f), -PADY, h - 1);
			py = ((float)Y + 0.5f) * (2.0f / (float)oh) - 1.0f;
			c->ry[Y] = py;
			c->row_v[Y] = 1.0f - clampf(py * py, 0.0f, 1.0f);
		}
		c->w = w; c->h = h; c->s = s;
		c->ow = ow; c->oh = oh; c->stride = stride;
	}
	/* what the curvature makes of each column and row, by the parameters */
	for (X = 0; X < c->ow; X++) c->cfy[X] = 1.0f + (c->cx[X] * c->cx[X]) * c->warp_y;
	for (Y = 0; Y < c->oh; Y++) c->rfx[Y] = 1.0f + (c->ry[Y] * c->ry[Y]) * c->warp_x;
	c->ready = 1;
	return 0;
}

/* A texel, linear: x from -PADX to w + PADX - 1, y from -PADY to h + PADY - 1 */
static inline const float *texel(const crts_ctx *c, int x, int y)
{
	return c->img + ((size_t)(y + PADY) * c->stride + (x + PADX)) * 3;
}

/* CrtsMask for one screen row: its pattern repeats every 6 columns (3 for
 * the grilles), so six masks, as the GLSL works each out, at the pixel's
 * centre (X + 0.5, Y + 0.5) */
static void row_masks(const crts_ctx *c, int Y, float m[6][3])
{
	float dark = c->mask, x;
	int X;
	for (X = 0; X < 6; X++) {
		float *o = m[X];
		switch (c->mtype) {
		case 2:
			o[0] = o[1] = o[2] = dark;
			x = ((float)X + 0.5f) * (1.0f / 3.0f);
			x -= floorf(x);
			if (x < 1.0f / 3.0f) o[0] = 1.0f; else if (x < 2.0f / 3.0f) o[1] = 1.0f; else o[2] = 1.0f;
			break;
		case 1:
			o[0] = o[1] = o[2] = 1.0f;
			x = ((float)X + 0.5f) * (1.0f / 3.0f);
			x -= floorf(x);
			if (x < 1.0f / 3.0f) o[0] = dark; else if (x < 2.0f / 3.0f) o[1] = dark; else o[2] = dark;
			break;
		case 3:
			o[0] = o[1] = o[2] = dark;
			x = (((float)X + 0.5f) + ((float)Y + 0.5f) * 2.9999f) * (1.0f / 6.0f);
			x -= floorf(x);
			if (x < 1.0f / 3.0f) o[0] = 1.0f; else if (x < 2.0f / 3.0f) o[1] = 1.0f; else o[2] = 1.0f;
			break;
		default:
			o[0] = o[1] = o[2] = 1.0f;
		}
	}
}

/* The tone curve and ToSrgb, into a screen pixel */
static inline uint32_t finish(const crts_ctx *c, float r, float g, float b, const pt_format *f)
{
	float peak = fmaxf(1.0f / (256.0f * 65536.0f), fmaxf(r, fmaxf(g, b)));
	float k = (float)TO_SRGB_STEPS / (peak * c->tone_y + c->tone_z);
	int ir, ig, ib;

	ir = (int)(clampf(r * k, 0.0f, (float)TO_SRGB_STEPS) + 0.5f);
	ig = (int)(clampf(g * k, 0.0f, (float)TO_SRGB_STEPS) + 0.5f);
	ib = (int)(clampf(b * k, 0.0f, (float)TO_SRGB_STEPS) + 0.5f);
	return ((uint32_t)c->to_srgb[ir] << f->r_shift) | ((uint32_t)c->to_srgb[ig] << f->g_shift) |
	       ((uint32_t)c->to_srgb[ib] << f->b_shift) | f->opaque;
}

/* One game row filtered across every screen column, planar: R for every
 * column, then G, then B, so the rows below can take four columns a step */
static void filter_row(const crts_ctx *c, int gy, float *out)
{
	float *oR = out, *oG = out + c->ow, *oB = out + 2 * c->ow;
	int X;
	for (X = 0; X < c->ow; X++) {
		const float *w = c->col_w + X * 4, *t = texel(c, c->col_x0[X], gy);
		oR[X] = t[0] * w[0] + t[3] * w[1] + t[6] * w[2] + t[9]  * w[3];
		oG[X] = t[1] * w[0] + t[4] * w[1] + t[7] * w[2] + t[10] * w[3];
		oB[X] = t[2] * w[0] + t[5] * w[1] + t[8] * w[2] + t[11] * w[3];
	}
}

/* The corner's vignette at screen pixel X of a row: the GLSL's warp is always
 * on, and at no curvature it still darkens the outermost pixels */
static inline float vignette(const crts_ctx *c, int X, float rv)
{
	const float ih = (float)c->h, ihc = ih * c->corner;
	return clampf(ih - ihc * (1.0f - c->col_v[X] * rv), 0.0f, 1.0f);
}

/* One screen pixel, as the GLSL has it */
static inline uint32_t pixel(const crts_ctx *c, const crts_band *b, int X, float sa,
                             float sb, const float *mm, float vin, const pt_format *f)
{
	const int ow = c->ow;
	const float *A = b->ha, *B = b->hb;
	return finish(c, (A[X] * sa + B[X] * sb) * (mm[0] * vin),
	                 (A[ow + X] * sa + B[ow + X] * sb) * (mm[1] * vin),
	                 (A[2 * ow + X] * sa + B[2 * ow + X] * sb) * (mm[2] * vin), f);
}

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static void rows_flat(const crts_ctx *c, crts_band *b, int y0, int y1,
                      uint32_t *out, size_t pitch, const pt_format *f)
{
	float m[6][3];
	int Y, X;

	for (Y = y0; Y < y1; Y++) {
		int ga = c->row_y0[Y], gb = ga + 1 > c->h - 1 + PADY ? ga : ga + 1;
		float sa = c->row_a[Y], sb = c->row_b[Y], rv = c->row_v[Y];
		uint32_t *o = out + (size_t)(Y - y0) * pitch;
		int xl, xr;

		/* the two game rows this screen row mixes, filtered across once
		 * each and kept while the next screen rows want them */
		if (b->ya != ga) {
			if (b->yb == ga) { float *t = b->ha; b->ha = b->hb; b->hb = t; b->ya = ga; b->yb = -99; }
			else { filter_row(c, ga, b->ha); b->ya = ga; }
		}
		if (b->yb != gb) { filter_row(c, gb, b->hb); b->yb = gb; }
		if (Y == y0 || c->mtype == 3) row_masks(c, Y, m);

		/* The vignette is exactly 1 but for a few pixels at each end of a
		 * row (and all of a top or bottom one): those, one at a time as
		 * the GLSL has them; the run between, where it is 1 and drops out,
		 * four at a time (ChaOS: about three times as fast on the Brick's
		 * A53, measured as 15.5 ms a frame before) */
		for (xl = 0; xl < c->ow && vignette(c, xl, rv) < 1.0f; xl++)
			o[xl] = pixel(c, b, xl, sa, sb, m[xl % 6], vignette(c, xl, rv), f);
		for (xr = c->ow; xr > xl && vignette(c, xr - 1, rv) < 1.0f; xr--)
			o[xr - 1] = pixel(c, b, xr - 1, sa, sb, m[(xr - 1) % 6], vignette(c, xr - 1, rv), f);
		X = xl;
#if defined(__ARM_NEON)
		{
			/* the mask's pattern, six columns, laid out so any four in a
			 * row read straight: index X % 12 and the three after */
			float mr[16], mg[16], mb[16];
			const float32x4_t vsa = vdupq_n_f32(sa), vsb = vdupq_n_f32(sb);
			const float32x4_t ty = vdupq_n_f32(c->tone_y), tz = vdupq_n_f32(c->tone_z);
			const float32x4_t steps = vdupq_n_f32((float)TO_SRGB_STEPS);
			const float32x4_t zero = vdupq_n_f32(0.0f), half = vdupq_n_f32(0.5f);
			const float32x4_t eps = vdupq_n_f32(1.0f / (256.0f * 65536.0f));
			const int ow = c->ow;
			const float *AR = b->ha, *AG = b->ha + ow, *AB = b->ha + 2 * ow;
			const float *BR = b->hb, *BG = b->hb + ow, *BB = b->hb + 2 * ow;
			int k, mi;

			for (k = 0; k < 16; k++) {
				mr[k] = m[k % 6][0]; mg[k] = m[k % 6][1]; mb[k] = m[k % 6][2];
			}
			mi = X % 12;
			for (; X + 4 <= xr; X += 4) {
				float32x4_t r = vmulq_f32(vaddq_f32(vmulq_f32(vld1q_f32(AR + X), vsa),
				                                    vmulq_f32(vld1q_f32(BR + X), vsb)),
				                          vld1q_f32(mr + mi));
				float32x4_t g = vmulq_f32(vaddq_f32(vmulq_f32(vld1q_f32(AG + X), vsa),
				                                    vmulq_f32(vld1q_f32(BG + X), vsb)),
				                          vld1q_f32(mg + mi));
				float32x4_t bl = vmulq_f32(vaddq_f32(vmulq_f32(vld1q_f32(AB + X), vsa),
				                                     vmulq_f32(vld1q_f32(BB + X), vsb)),
				                           vld1q_f32(mb + mi));
				float32x4_t peak = vmaxq_f32(eps, vmaxq_f32(r, vmaxq_f32(g, bl)));
				float32x4_t kk = vdivq_f32(steps, vaddq_f32(vmulq_f32(peak, ty), tz));
				int32x4_t ir = vcvtq_s32_f32(vaddq_f32(vminq_f32(vmaxq_f32(vmulq_f32(r, kk), zero), steps), half));
				int32x4_t ig = vcvtq_s32_f32(vaddq_f32(vminq_f32(vmaxq_f32(vmulq_f32(g, kk), zero), steps), half));
				int32x4_t ib = vcvtq_s32_f32(vaddq_f32(vminq_f32(vmaxq_f32(vmulq_f32(bl, kk), zero), steps), half));
				int32_t R[4], G[4], Bv[4];

				vst1q_s32(R, ir); vst1q_s32(G, ig); vst1q_s32(Bv, ib);
				for (k = 0; k < 4; k++)
					o[X + k] = ((uint32_t)c->to_srgb[R[k]] << f->r_shift) |
					           ((uint32_t)c->to_srgb[G[k]] << f->g_shift) |
					           ((uint32_t)c->to_srgb[Bv[k]] << f->b_shift) | f->opaque;
				mi += 4;
				if (mi >= 12) mi -= 12;
			}
		}
#endif
		for (; X < xr; X++)
			o[X] = pixel(c, b, X, sa, sb, m[X % 6], 1.0f, f);
	}
}

static void rows_warp(const crts_ctx *c, int y0, int y1, uint32_t *out, size_t pitch,
                      const pt_format *f)
{
	const float hx = 0.5f * (float)c->w, hy = 0.5f * (float)c->h;
	const float ih = (float)c->h, ihc = ih * c->corner;
	float m[6][3];
	int Y, X, k;

	for (Y = y0; Y < y1; Y++) {
		uint32_t *o = out + (size_t)(Y - y0) * pitch;
		const float py = c->ry[Y], fx = c->rfx[Y];
		if (Y == y0 || c->mtype == 3) row_masks(c, Y, m);
		for (X = 0; X < c->ow; X++) {
			float qx = c->cx[X] * fx, qy = py * c->cfy[X];
			float vin = 1.0f - (1.0f - clampf(qx * qx, 0.0f, 1.0f)) * (1.0f - clampf(qy * qy, 0.0f, 1.0f));
			float sx, sy, off, fr, t, sa, sb, w[4], r = 0, g = 0, b = 0;
			const float *mm = m[X % 6], *s0, *s1, *h0, *h1;
			int ix, iy, i;

			vin = clampf(ih - ihc * vin, 0.0f, 1.0f);
			if (vin <= 0.0f) {      /* past the corner: black */
				o[X] = finish(c, 0.0f, 0.0f, 0.0f, f);
				continue;
			}
			/* the first scanline's centre is iy + 0.5, the first of the
			 * four texels' ix + 0.5 */
			sy = qy * hy + hy - 0.5f;
			sx = qx * hx + hx - 1.5f;
			iy = (int)(sy + 4096.0f) - 4096;
			ix = (int)(sx + 4096.0f) - 4096;
			off = sy - (float)iy;
			fr = sx - (float)ix;
			/* the scanline weights, then the Gaussian's, from the tables */
			t = off * SCAN_N; i = (int)t; t -= (float)i;
			s0 = c->scan[i]; s1 = c->scan[i + 1];
			sa = (s0[0] + (s1[0] - s0[0]) * t) * vin;
			sb = (s0[1] + (s1[1] - s0[1]) * t) * vin;
			t = fr * HW_N; i = (int)t; t -= (float)i;
			h0 = c->hw[i]; h1 = c->hw[i + 1];
			for (k = 0; k < 4; k++) w[k] = h0[k] + (h1[k] - h0[k]) * t;
			ix = clampi(ix, -PADX, c->w - 1);
			iy = clampi(iy, -PADY, c->h - 1);
			{
				const float *A = texel(c, ix, iy);
				const float *B = iy + 1 > c->h - 1 + PADY ? A : A + (size_t)c->stride * 3;
				for (k = 0; k < 4; k++) {
					r += (A[k * 3 + 0] * sa + B[k * 3 + 0] * sb) * w[k];
					g += (A[k * 3 + 1] * sa + B[k * 3 + 1] * sb) * w[k];
					b += (A[k * 3 + 2] * sa + B[k * 3 + 2] * sb) * w[k];
				}
			}
			o[X] = finish(c, r * mm[0], g * mm[1], b * mm[2], f);
		}
	}
}

int crts_render_rows(const crts_ctx *c, crts_band *b, int y0, int y1,
                     uint32_t *out, size_t pitch, const pt_format *fmt)
{
	if (!c || !c->ready || !b) return -1;
	if (y0 < 0) { out += (size_t)(-y0) * pitch; y0 = 0; }
	if (y1 > c->oh) y1 = c->oh;
	if (y0 >= y1) return 0;
	if (c->warp_x > 0.0f || c->warp_y > 0.0f) {
		rows_warp(c, y0, y1, out, pitch, fmt);
		return 0;
	}
	if ((size_t)c->ow * 3 > b->cap) {
		float *a = realloc(b->ha, (size_t)c->ow * 3 * sizeof *a), *bb;
		if (!a) return -1;
		b->ha = a;
		bb = realloc(b->hb, (size_t)c->ow * 3 * sizeof *bb);
		if (!bb) return -1;
		b->hb = bb;
		b->cap = (size_t)c->ow * 3;
	}
	b->ya = b->yb = -99;          /* a new frame's rows */
	rows_flat(c, b, y0, y1, out, pitch, fmt);
	return 0;
}
