/* PT SkyWalker541 - the shader's arithmetic, shared by the fast renderer and
 * the test reference so both build their tables from the same code.
 *
 * Copyright (c) 2026 SkyWalker541. MIT License, see LICENSE.
 *
 * Each function is the GLSL of PT_SkyWalker541.glsl v2.1.0 in single
 * precision, in the same order of operations.
 */
#ifndef PT_MATH_H
#define PT_MATH_H

#include <math.h>
#include "pt_shader.h"

/* ---- GLSL built-ins ----------------------------------------------------- */

static inline float fractf(float x)                   { return x - floorf(x); }
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float mixf(float a, float b, float t)   { return a * (1.0f - t) + b * t; }
static inline float smoothstepf(float e0, float e1, float x)
{
	float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

#define PT_LUMA_R 0.2126f
#define PT_LUMA_G 0.7152f
#define PT_LUMA_B 0.0722f

static inline float pt_bright(float r, float g, float b)
{
	return PT_LUMA_R * r + PT_LUMA_G * g + PT_LUMA_B * b;
}

/* TEX0 for a screen column or row: its centre, as a fraction of the picture.
 * With TextureSize == InputSize this is also orig_coord and gameUV. */
static inline float pt_tex0(int i, int n) { return ((float)i + 0.5f) / (float)n; }

/* ---- the backing material ------------------------------------------------ */

/* noiseHash: Dave Hoskins' "Hash without Sine"
 * (https://www.shadertoy.com/view/4djSRW), MIT License,
 * Copyright (c) 2014 David Hoskins. */
static inline float pt_noise_hash(float px, float py)
{
#ifdef PT_TEST_FLAT_GRAIN
	(void)px; (void)py;
	return 0.5f;
#else
	float x = fractf(px * 0.1031f), y = fractf(py * 0.1031f), z = fractf(px * 0.1031f);
	float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
	x += d; y += d; z += d;
	return fractf((x + y) * z);
#endif
}

/* proceduralBackground: the backing's grey at a point, before the drop
 * shadow and the tint. Always within 0.478 +- 0.0285. */
static inline float pt_grain(float u, float v)
{
	float grain  = pt_noise_hash(u * 128.0f, v * 128.0f) * 0.875f;
	float offset = (grain - 0.4375f) * 0.065f;
	return 0.478f + offset;
}

/* The backing color tint for PT_BACKING_COLOR (1, 2 or 3). */
static inline void pt_tint(float bc, float out[3])
{
	if      (bc < 1.5f) { out[0] = 0.651f; out[1] = 0.675f; out[2] = 0.518f; }
	else if (bc < 2.5f) { out[0] = out[1] = out[2] = 0.737f; }
	else                { out[0] = out[1] = out[2] = 1.0f; }
}

/* The backing after the drop shadow, tinted: computeBacking from "Palette
 * tint" on, for a grey g that already carries the shadow. */
static inline float pt_tinted(float g, float tint, float strength)
{
	return mixf(g, clampf(tint + (g * 2.0f - 1.0f), 0.0f, 1.0f), strength);
}

/* ---- per game pixel -------------------------------------------------------- */

static inline float pt_threshold(const float *P)
{
	float sys = P[PT_GAME_SYSTEM];
	if (sys < 0.5f) return P[PT_MANUAL_WHITE_DETECTION_LEVEL];
	if (sys < 1.5f) return 0.90f;
	if (sys < 2.5f) return 0.85f;
	if (sys < 3.5f) return 0.80f;
	return 0.75f;
}

static inline int pt_is_white(float r, float g, float b, float br, float th)
{
	float mn = fminf(r, fminf(g, b));
	return br > th && mn > th * 0.9f;
}

/* ---- per position ------------------------------------------------------------ */

/* The drop shadow's direction, as shadowDir in computeBacking. */
static inline void pt_shadow_dir(const float *P, float *dx, float *dy)
{
	float dir = P[PT_SHADOW_DIRECTION];
	if      (dir < 0.5f) { *dx = -1.0f; *dy =  1.0f; }   /* Down Right */
	else if (dir < 1.5f) { *dx =  1.0f; *dy =  1.0f; }   /* Down Left */
	else if (dir < 2.5f) { *dx = -1.0f; *dy = -1.0f; }   /* Up Right */
	else                 { *dx =  1.0f; *dy = -1.0f; }   /* Up Left */
}

/* Which game column (or row) casts the drop shadow onto screen column i:
 * nearest sampling one shadow-distance away, clamped to the edge. */
static inline int pt_shadow_src(int i, int on, int n, float dir, float dist)
{
	float u = pt_tex0(i, on) + dir * dist * (1.0f / (float)n);
	int k = (int)floorf(u * (float)n);
	return k < 0 ? 0 : (k >= n ? n - 1 : k);
}

/* The grid line mask along one axis, opacity folded in. The distance to the
 * nearest pixel border is the smaller of the two axes', and the mask falls as
 * that distance rises, so the 2D mask is the larger of the two axis masks. */
static inline float pt_grid_axis(int i, int on, int n, const float *P)
{
	float half = P[PT_GRID_LINE_WIDTH] * 0.5f;
	float c = fractf(pt_tex0(i, on) * (float)n);
	float e = fminf(c, 1.0f - c);
	return (1.0f - smoothstepf(half - 0.04f, half, e)) * P[PT_GRID_LINE_OPACITY];
}

/* Distance from the centre of a game pixel, for LCD Dot. */
static inline float pt_dot_dist(int x, int y, int ow, int oh, int w, int h)
{
	float du = fractf(pt_tex0(x, ow) * (float)w) - 0.5f;
	float dv = fractf(pt_tex0(y, oh) * (float)h) - 0.5f;
	return sqrtf(du * du + dv * dv);
}

static inline float pt_bezel_scale(const float *P)
{
	float sys = P[PT_GAME_SYSTEM];
	if (sys < 1.5f) return 22.0f;
	if (sys < 2.5f) return 35.0f;
	if (sys < 3.5f) return 70.0f;
	return 55.0f;
}

/* The bezel edge shadow along one axis: 1 except near the edges. */
static inline float pt_bezel_axis(int i, int on, float scale)
{
	return clampf((1.0f - fabsf(pt_tex0(i, on) * 2.0f - 1.0f)) * scale, 0.0f, 1.0f);
}

/* ---- glare and sheen ------------------------------------------------------- */

/* The light point, as the vertex shader works it out. */
static inline void pt_light(const float *P, unsigned frame, float light[3])
{
	light[0] = P[PT_LIGHT_HOME_X];
	light[1] = P[PT_LIGHT_HOME_Y];
	light[2] = P[PT_SHEEN_HUE_TURN];
	if (P[PT_GLARE_AND_SHEEN] > 0.5f && P[PT_LIGHT_DRIFT_SPEED] > 0.001f) {
		float t = (float)frame * P[PT_LIGHT_DRIFT_SPEED] * 0.01f;
		light[0] += 0.6f * sinf(t - 0.34f);
		light[1] += 0.4f * sinf(t * 0.73f - 0.52f);
		light[2] += t * 0.15f;
	}
}

/* At picture point (gx, gy): the sheen added to each channel, and how far
 * the result is then mixed toward white. applyGlareAndSheen is then
 *   clamp(mix(bg + sheen, 1, glare), 0, 1). */
static inline void pt_glare_at(const float *P, const float light[3], float aspect,
                               float gx, float gy, float sheen[3], float *glare)
{
	float dx = (gx - light[0]) * aspect, dy = (gy - light[1]) * 1.0f;
	float d2 = dx * dx + dy * dy;
	int k;

	sheen[0] = sheen[1] = sheen[2] = 0.0f;
	if (P[PT_SHEEN_STRENGTH] > 0.001f) {
		float ph = sqrtf(d2) * P[PT_SHEEN_BAND_DENSITY] + light[2];
		float hh[3], r[3], avg;
		if (P[PT_SHEEN_RIPPLE] > 0.001f) {
			float f = 3.0f / P[PT_SHEEN_RIPPLE_SIZE];
			float ux = gx * f, uy = gy * f;
			float t3[3];
			t3[0] = fabsf(fractf(ux * 1.00f + uy * 0.37f) * 2.0f - 1.0f);
			t3[1] = fabsf(fractf(uy * 1.13f - ux * 0.61f + 0.31f) * 2.0f - 1.0f);
			t3[2] = fabsf(fractf(ux * 0.43f - uy * 1.71f + 0.72f) * 2.0f - 1.0f);
			for (k = 0; k < 3; k++) t3[k] = t3[k] * t3[k] * (3.0f - 2.0f * t3[k]);
			ph += (t3[0] + t3[1] + t3[2] - 1.5f) * P[PT_SHEEN_RIPPLE] * 0.45f;
		}
		hh[0] = fractf(ph + 0.0f); hh[1] = fractf(ph + 0.3333f); hh[2] = fractf(ph + 0.6667f);
		for (k = 0; k < 3; k++) {
			float wv = fabsf(hh[k] * 2.0f - 1.0f);
			r[k] = wv * wv * (3.0f - 2.0f * wv);
		}
		avg = r[0] * 0.3333f + r[1] * 0.3333f + r[2] * 0.3333f;
		for (k = 0; k < 3; k++) sheen[k] = (r[k] - avg) * P[PT_SHEEN_STRENGTH];
	}
	*glare = 0.0f;
	if (P[PT_GLARE_STRENGTH] > 0.001f) {
		float sp = P[PT_GLARE_SPREAD];
		float q = d2 / (sp * sp);
		*glare = fminf(P[PT_GLARE_STRENGTH] / (1.0f + q * q), 0.85f);
	}
}

#endif
