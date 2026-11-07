/* The CPU ports of crt-lottes-fast and lcd3x, for comparing with glref.
 *
 *   othc crt|lcd in.ppm scale out.ppm [NAME=value ...]
 *
 * Draws in three bands, as the device does, to test the bands' seams too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

#include "crts.h"
#include "lcd3x.h"
#include "ppm.h"

int main(int argc, char **argv)
{
	img in, out;
	int s, w, h, ow, oh, i, b;
	uint32_t *src, *dst;
	pt_format fmt = { 16, 8, 0, 0 };

	if (argc < 5) { fprintf(stderr, "usage: othc crt|lcd in.ppm scale out.ppm [NAME=value ...]\n"); return 2; }
	if (!ppm_read(argv[2], &in)) return 1;
	s = atoi(argv[3]); w = in.w; h = in.h; ow = w * s; oh = h * s;
	src = malloc((size_t)w * h * 4);
	dst = malloc((size_t)ow * oh * 4);
	for (i = 0; i < w * h; i++)
		src[i] = (uint32_t)in.px[i * 3] << 16 | (uint32_t)in.px[i * 3 + 1] << 8 | in.px[i * 3 + 2];

	if (!strcmp(argv[1], "crt")) {
		float v[CRTS_NPARAMS];
		crts_ctx *c = crts_create();
		for (i = 0; i < CRTS_NPARAMS; i++) v[i] = crts_params[i].def;
		for (i = 5; i < argc; i++) {
			char *e = strchr(argv[i], '=');
			int k;
			if (!e) continue;
			*e = 0;
			k = crts_param_index(argv[i]);
			if (k < 0) { fprintf(stderr, "no parameter %s\n", argv[i]); return 2; }
			v[k] = (float)atof(e + 1);
		}
		crts_set_params(c, v);
		if (getenv("BENCH")) {
			int n = atoi(getenv("BENCH")), r;
			crts_band *bd = crts_band_create();
			double t0 = now();
			for (r = 0; r < n; r++) {
				crts_begin_frame(c, src, w, h, w, s);
				crts_render_rows(c, bd, 0, oh, dst, ow, &fmt);
			}
			fprintf(stderr, "%.3f ms/frame, %.1f ns/pixel\n", (now() - t0) / n, (now() - t0) * 1e6 / n / ((double)ow * oh));
		}
		if (crts_begin_frame(c, src, w, h, w, s)) return 1;
		for (b = 0; b < 3; b++) {
			crts_band *bd = crts_band_create();
			int y0 = oh * b / 3, y1 = oh * (b + 1) / 3;
			crts_render_rows(c, bd, y0, y1, dst + (size_t)y0 * ow, ow, &fmt);
			crts_band_destroy(bd);
		}
		crts_destroy(c);
	} else {
		float v[LCD3X_NPARAMS];
		lcd3x_ctx *c = lcd3x_create();
		for (i = 0; i < LCD3X_NPARAMS; i++) v[i] = lcd3x_params[i].def;
		for (i = 5; i < argc; i++) {
			char *e = strchr(argv[i], '=');
			int k;
			if (!e) continue;
			*e = 0;
			k = lcd3x_param_index(argv[i]);
			if (k < 0) { fprintf(stderr, "no parameter %s\n", argv[i]); return 2; }
			v[k] = (float)atof(e + 1);
		}
		lcd3x_set_params(c, v);
		if (getenv("BENCH")) {
			int n = atoi(getenv("BENCH")), r;
			double t0 = now();
			for (r = 0; r < n; r++) {
				lcd3x_begin_frame(c, src, w, h, w, s);
				lcd3x_render_rows(c, 0, oh, dst, ow, &fmt);
			}
			fprintf(stderr, "%.3f ms/frame, %.1f ns/pixel\n", (now() - t0) / n, (now() - t0) * 1e6 / n / ((double)ow * oh));
		}
		if (lcd3x_begin_frame(c, src, w, h, w, s)) return 1;
		for (b = 0; b < 3; b++) {
			int y0 = oh * b / 3, y1 = oh * (b + 1) / 3;
			lcd3x_render_rows(c, y0, y1, dst + (size_t)y0 * ow, ow, &fmt);
		}
		lcd3x_destroy(c);
	}
	out.w = ow; out.h = oh; out.px = malloc((size_t)ow * oh * 3);
	for (i = 0; i < ow * oh; i++) {
		out.px[i * 3] = dst[i] >> 16; out.px[i * 3 + 1] = dst[i] >> 8; out.px[i * 3 + 2] = dst[i];
	}
	return !ppm_write(argv[4], &out);
}
