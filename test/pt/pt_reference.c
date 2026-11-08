/* PT SkyWalker541 - test reference. Never shipped.
 *
 * Copyright (c) 2026 SkyWalker541. MIT License, see ../../shaders/pt/LICENSE.
 *
 * The GLSL fragment shader's main(), once per screen pixel, in single
 * precision, with no tables and no shortcuts. Checked against the real GLSL by
 * sweep.sh, and the fast renderer is then checked against this, so a fast-path
 * shortcut that changes the picture shows up as a difference here.
 */
#include <stdint.h>
#include <stdlib.h>

#include "../../shaders/pt/pt_math.h"
#include "pt_reference.h"

typedef struct { float r, g, b; } rgb;

static rgb fetch(const uint32_t *src, int w, int h, size_t pitch, float u, float v)
{
	int x = (int)floorf(u * (float)w), y = (int)floorf(v * (float)h);
	uint32_t p;
	rgb c;
	x = x < 0 ? 0 : (x >= w ? w - 1 : x);
	y = y < 0 ? 0 : (y >= h ? h - 1 : y);
	p = src[(size_t)y * pitch + x];
	c.r = (float)((p >> 16) & 255) / 255.0f;
	c.g = (float)((p >> 8) & 255) / 255.0f;
	c.b = (float)(p & 255) / 255.0f;
	return c;
}

static rgb mix3(rgb a, rgb b, float t)
{
	rgb o = { mixf(a.r, b.r, t), mixf(a.g, b.g, t), mixf(a.b, b.b, t) };
	return o;
}

void pt_reference_render(const pt_params *pp, const uint32_t *src, int w, int h,
                         size_t pitch, int s, unsigned char *dst_rgb, unsigned frame)
{
	pt_reference_render_size(pp, src, w, h, pitch, w * s, h * s, dst_rgb, frame);
}

void pt_reference_render_size(const pt_params *pp, const uint32_t *src, int w, int h,
                              size_t pitch, int ow, int oh, unsigned char *dst_rgb,
                              unsigned frame)
{
	const float *P = pp->v;
	int X, Y;
	float th = pt_threshold(P), light[3], aspect = (float)w / (float)h;

	pt_light(P, frame, light);

	for (Y = 0; Y < oh; Y++)
		for (X = 0; X < ow; X++) {
			float u = pt_tex0(X, ow), v = pt_tex0(Y, oh);
			rgb pixel = fetch(src, w, h, pitch, u, v);
			rgb raw = pixel, result, bg = { 0, 0, 0 };
			float pixb = pt_bright(pixel.r, pixel.g, pixel.b);
			float rawb = pt_bright(raw.r, raw.g, raw.b);
			int white, need_bg, opaque;
			float alpha;
			unsigned char *o = dst_rgb + ((size_t)Y * ow + X) * 3;

			if (P[PT_DARKEN_PICTURE] > 0.5f) {
				float k = fmaxf(1.0f - P[PT_DARKEN_PICTURE] * 0.01f * pt_bright(pixel.r, pixel.g, pixel.b), 0.0f);
				pixel.r *= k; pixel.g *= k; pixel.b *= k;
				pixb = pt_bright(pixel.r, pixel.g, pixel.b);
			}
			white = pt_is_white(raw.r, raw.g, raw.b, rawb, th);
			opaque = P[PT_TRANSPARENT_PIXELS] < 0.5f && !white;

			if (opaque)
				need_bg = P[PT_PIXEL_EFFECT] > 0.5f && P[PT_GAP_COLOR] < 0.5f &&
				          !(P[PT_PIXEL_EFFECT] > 1.5f && rawb < 0.01f);
			else
				need_bg = 1;

			if (need_bg) {
				/* computeBacking */
				float g = pt_grain(u, v);
				if (P[PT_DROP_SHADOW_OPACITY] > 0.001f) {
					float dx, dy;
					rgb sh;
					float shb;
					pt_shadow_dir(P, &dx, &dy);
					sh = fetch(src, w, h, pitch,
					           u + dx * P[PT_SHADOW_DISTANCE] * (1.0f / (float)w),
					           v + dy * P[PT_SHADOW_DISTANCE] * (1.0f / (float)h));
					shb = pt_bright(sh.r, sh.g, sh.b);
					if (!pt_is_white(sh.r, sh.g, sh.b, shb, th)) {
						float st = (1.0f - shb) * P[PT_DROP_SHADOW_OPACITY];
						g = mixf(g, g * 0.2f, st);
					}
				}
				bg.r = bg.g = bg.b = g;
				if (P[PT_BACKING_COLOR] > 0.5f) {
					float t[3];
					pt_tint(P[PT_BACKING_COLOR], t);
					bg.r = pt_tinted(g, t[0], P[PT_BACKING_COLOR_STRENGTH]);
					bg.g = pt_tinted(g, t[1], P[PT_BACKING_COLOR_STRENGTH]);
					bg.b = pt_tinted(g, t[2], P[PT_BACKING_COLOR_STRENGTH]);
				}
				if (P[PT_GLARE_AND_SHEEN] > 0.5f) {
					float sh[3], gl;
					pt_glare_at(P, light, aspect, u, v, sh, &gl);
					bg.r = clampf(mixf(bg.r + sh[0], 1.0f, gl), 0.0f, 1.0f);
					bg.g = clampf(mixf(bg.g + sh[1], 1.0f, gl), 0.0f, 1.0f);
					bg.b = clampf(mixf(bg.b + sh[2], 1.0f, gl), 0.0f, 1.0f);
				}
			}

			if (opaque) {
				result = pixel;
			} else {
				float amt = P[PT_TRANSPARENCY_AMOUNT];
				if (P[PT_TRANSPARENT_PIXELS] < 0.5f)      alpha = clampf(pixb / 3.0f + amt, 0.0f, 1.0f);
				else if (P[PT_TRANSPARENT_PIXELS] < 1.5f) alpha = clampf(amt * pixb * 2.665f, 0.0f, 1.0f);
				else                                      alpha = clampf(pixb / 3.0f + amt, 0.0f, 1.0f);
				if (white) alpha = fmaxf(alpha, P[PT_MINIMUM_WHITE_TRANSPARENCY]);
				result = mix3(pixel, bg, alpha);
			}

			/* applyPixelEffect */
			if (P[PT_PIXEL_EFFECT] > 0.5f) {
				rgb gap;
				if (P[PT_GAP_COLOR] < 0.5f)      gap = bg;
				else if (P[PT_GAP_COLOR] < 1.5f) { gap.r = gap.g = gap.b = 0.0f; }
				else                             { gap.r = gap.g = gap.b = 1.0f; }

				if (P[PT_PIXEL_EFFECT] < 1.5f) {
					float m = fmaxf(pt_grid_axis(X, ow, w, P), pt_grid_axis(Y, oh, h, P));
					result = mix3(result, gap, m);
				} else if (rawb >= 0.01f) {
					float fade = smoothstepf(0.0f, fmaxf(P[PT_MIN_BRIGHTNESS_FOR_DOTS], 0.001f), rawb);
					float luma = pt_bright(result.r, result.g, result.b);
					float radius = P[PT_DOT_SIZE] * 0.5f + mixf(0.0f, 0.08f, luma);
					float soft = fmaxf(P[PT_DOT_EDGE_SOFTNESS] * radius, 0.02f);
					float mask = clampf((radius - pt_dot_dist(X, Y, ow, oh, w, h)) / soft, 0.0f, 1.0f);
					rgb dot = mix3(result, gap, (1.0f - mask) * P[PT_DOT_GAP_OPACITY]);
					result = mix3(result, dot, fade);
				}
			}

			/* applyBezelShadow */
			if (P[PT_BEZEL_EDGE_SHADOW] >= 0.001f) {
				float sc = pt_bezel_scale(P);
				float sh = pt_bezel_axis(X, ow, sc) * pt_bezel_axis(Y, oh, sc);
				float f = mixf(1.0f - P[PT_BEZEL_EDGE_SHADOW], 1.0f, sh);
				result.r *= f; result.g *= f; result.b *= f;
			}

			o[0] = (unsigned char)(clampf(result.r, 0.0f, 1.0f) * 255.0f + 0.5f);
			o[1] = (unsigned char)(clampf(result.g, 0.0f, 1.0f) * 255.0f + 0.5f);
			o[2] = (unsigned char)(clampf(result.b, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
}
