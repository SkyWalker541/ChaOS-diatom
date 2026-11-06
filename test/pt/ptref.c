/* ptref: render a frame through the test reference. Same arguments as ptc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppm.h"
#include "pt_reference.h"

int main(int argc, char **argv)
{
	img in, out;
	int scale, frame, i;
	pt_params p;
	uint32_t *src;

	if (argc < 5) { fprintf(stderr, "usage: ptref in.ppm scale frame out.ppm [NAME=value ...]\n"); return 2; }
	if (!ppm_read(argv[1], &in)) return 1;
	scale = atoi(argv[2]); frame = atoi(argv[3]);
	pt_params_default(&p);
	for (i = 5; i < argc; i++) {
		char name[64]; const char *eq = strchr(argv[i], '='); int k;
		if (!eq || eq - argv[i] >= 64) continue;
		memcpy(name, argv[i], (size_t)(eq - argv[i])); name[eq - argv[i]] = 0;
		if ((k = pt_param_index(name)) < 0) { fprintf(stderr, "unknown parameter %s\n", name); return 2; }
		p.v[k] = (float)atof(eq + 1);
	}
	src = malloc(sizeof *src * (size_t)in.w * in.h);
	for (i = 0; i < in.w * in.h; i++)
		src[i] = ((uint32_t)in.px[i*3] << 16) | ((uint32_t)in.px[i*3+1] << 8) | in.px[i*3+2];
	out.w = in.w * scale; out.h = in.h * scale;
	out.px = malloc((size_t)out.w * out.h * 3);
	pt_reference_render(&p, src, in.w, in.h, (size_t)in.w, scale, out.px, (unsigned)frame);
	return ppm_write(argv[4], &out) ? 0 : 1;
}
