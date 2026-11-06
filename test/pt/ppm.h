/* Minimal binary PPM (P6) reading and writing for the test tools. */
#ifndef PTTEST_PPM_H
#define PTTEST_PPM_H

#include <stdio.h>
#include <stdlib.h>

typedef struct { int w, h; unsigned char *px; } img;   /* RGB, rows top-down */

static inline int ppm_read(const char *path, img *im)
{
	FILE *f = fopen(path, "rb");
	int maxv;
	size_t n;
	if (!f) { perror(path); return 0; }
	if (fscanf(f, "P6 %d %d %d", &im->w, &im->h, &maxv) != 3 || maxv != 255) {
		fprintf(stderr, "%s: not an 8-bit P6\n", path); fclose(f); return 0;
	}
	fgetc(f);
	n = (size_t)im->w * im->h * 3;
	im->px = malloc(n);
	if (fread(im->px, 1, n, f) != n) { fprintf(stderr, "%s: short\n", path); fclose(f); return 0; }
	fclose(f);
	return 1;
}

static inline int ppm_write(const char *path, const img *im)
{
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return 0; }
	fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
	fwrite(im->px, 1, (size_t)im->w * im->h * 3, f);
	fclose(f);
	return 1;
}

#endif
