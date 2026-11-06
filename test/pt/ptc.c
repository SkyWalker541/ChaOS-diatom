/* ptc: render a frame through the C port, with the same arguments as glref.
 *
 * usage: ptc in.ppm scale frame out.ppm [NAME=value ...] [--bench N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "ppm.h"
#include "../../shaders/pt/pt_shader.h"

/* --threads N: begin the frame once, then N threads each draw a band. */
typedef struct { pt_ctx *c; pt_band *b; int y0, y1; uint32_t *dst; size_t pitch; const pt_format *f; } job;
static void *band_main(void *a)
{
	job *j = a;
	pt_render_band(j->c, j->b, j->y0, j->y1, j->dst, j->pitch, j->f);
	return NULL;
}
static int render_threads(pt_ctx *c, int nt, pt_band **bands, const uint32_t *src, int w, int h,
                          int s, uint32_t *dst, size_t pitch, const pt_format *f, unsigned frame)
{
	pthread_t th[16]; job jobs[16]; int i, oh = h * s;
	if (pt_begin_frame(c, src, w, h, (size_t)w, s, frame)) return -1;
	for (i = 0; i < nt; i++) {
		jobs[i] = (job){ c, bands[i], oh * i / nt, oh * (i + 1) / nt, dst, pitch, f };
		pthread_create(&th[i], NULL, band_main, &jobs[i]);
	}
	for (i = 0; i < nt; i++) pthread_join(th[i], NULL);
	return 0;
}

int main(int argc, char **argv)
{
	img in, out;
	int scale, frame, i, bench = 0, nt = 0;
	pt_band *bands[16];
	pt_params p;
	pt_ctx *c;
	uint32_t *src, *dst;
	pt_format fmt = { 16, 8, 0, 0xff000000u };

	if (argc < 5) { fprintf(stderr, "usage: ptc in.ppm scale frame out.ppm [NAME=value ...] [--bench N]\n"); return 2; }
	if (!ppm_read(argv[1], &in)) return 1;
	scale = atoi(argv[2]); frame = atoi(argv[3]);
	pt_params_default(&p);
	for (i = 5; i < argc; i++) {
		char name[64];
		const char *eq;
		int k;
		if (!strcmp(argv[i], "--bench") && i + 1 < argc) { bench = atoi(argv[++i]); continue; }
		if (!strcmp(argv[i], "--threads") && i + 1 < argc) { nt = atoi(argv[++i]); continue; }
		eq = strchr(argv[i], '=');
		if (!eq || eq - argv[i] >= 64) continue;
		memcpy(name, argv[i], (size_t)(eq - argv[i])); name[eq - argv[i]] = 0;
		k = pt_param_index(name);
		if (k < 0) { fprintf(stderr, "unknown parameter %s\n", name); return 2; }
		p.v[k] = (float)atof(eq + 1);
	}

	src = malloc(sizeof *src * (size_t)in.w * in.h);
	for (i = 0; i < in.w * in.h; i++)
		src[i] = ((uint32_t)in.px[i*3] << 16) | ((uint32_t)in.px[i*3+1] << 8) | in.px[i*3+2];
	out.w = in.w * scale; out.h = in.h * scale;
	dst = malloc(sizeof *dst * (size_t)out.w * out.h);

	c = pt_create();
	pt_set_params(c, &p);
	for (i = 0; i < nt && i < 16; i++) bands[i] = pt_band_create();
	if (nt ? render_threads(c, nt, bands, src, in.w, in.h, scale, dst, (size_t)out.w, &fmt, (unsigned)frame)
	       : pt_render(c, src, in.w, in.h, (size_t)in.w, scale, dst, (size_t)out.w, &fmt, (unsigned)frame)) {
		fprintf(stderr, "render failed\n"); return 1;
	}
	if (bench) {
		struct timespec a, b;
		clock_gettime(CLOCK_MONOTONIC, &a);
		for (i = 0; i < bench; i++) {
			if (nt) render_threads(c, nt, bands, src, in.w, in.h, scale, dst, (size_t)out.w, &fmt, (unsigned)(frame + i));
			else pt_render(c, src, in.w, in.h, (size_t)in.w, scale, dst, (size_t)out.w, &fmt, (unsigned)(frame + i));
		}
		clock_gettime(CLOCK_MONOTONIC, &b);
		fprintf(stderr, "%.3f ms/frame\n",
		        ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / bench);
	}

	out.px = malloc((size_t)out.w * out.h * 3);
	for (i = 0; i < out.w * out.h; i++) {
		out.px[i*3] = (dst[i] >> 16) & 255; out.px[i*3+1] = (dst[i] >> 8) & 255; out.px[i*3+2] = dst[i] & 255;
	}
	if (!ppm_write(argv[4], &out)) return 1;
	pt_destroy(c);
	return 0;
}
