/* ChaOS: PT's drifting light, built behind the picture shown and swapped in
 * whole every DRIFT_STEP (4) frames. Each frame drawn in bands, as the Brick
 * draws it, must be exactly the picture of one light: the one a fresh
 * renderer draws for the frame that light was taken at. So the light never
 * tears across the screen, and the picture is the shader's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pt_shader.h"

#define W 160
#define H 144
#define S 4

static void set(pt_params *p, const char *k, float v) { int i = pt_param_index(k); if (i >= 0) p->v[i] = v; }

int main(void)
{
	uint32_t *src = malloc(W * H * 4), *a = malloc(W * S * H * S * 4), *b = malloc(W * S * H * S * 4);
	pt_ctx *c = pt_create(), *fresh;
	pt_band *bd[3];
	pt_params p;
	pt_format fmt = { 16, 8, 0, 0xff000000u };
	int i, f, fails = 0, ripple;

	for (i = 0; i < W * H; i++) src[i] = (uint32_t)((i * 2654435761u) >> 8) & 0xf8f8f8;
	for (i = 0; i < 3; i++) bd[i] = pt_band_create();
	for (ripple = 0; ripple < 2; ripple++) {
		pt_params_default(&p);
		set(&p, "PT_GLARE_AND_SHEEN", 1); set(&p, "PT_LIGHT_DRIFT_SPEED", 3);
		set(&p, "PT_SHEEN_STRENGTH", 0.4f); set(&p, "PT_SHEEN_RIPPLE", ripple ? 0.6f : 0);
		set(&p, "PT_SHEEN_HUE_TURN", 0.3f);
		pt_set_params(c, &p);
		for (f = 100; f < 124; f++) {
			int shown = 100 + 4 * ((f - 100) / 4), oh = H * S, k;

			if (pt_begin_frame(c, src, W, H, W, S, (unsigned)f)) { puts("FAIL: begin"); return 1; }
			for (k = 0; k < 3; k++) pt_render_band(c, bd[k], oh * k / 3, oh * (k + 1) / 3, a, W * S, &fmt);
			if (pt_light_frame(c) != (unsigned)shown) {
				printf("FAIL: frame %d says its light is frame %u's, not %d's\n", f, pt_light_frame(c), shown);
				fails++;
			}
			fresh = pt_create();
			pt_set_params(fresh, &p);
			pt_render(fresh, src, W, H, W, S, b, W * S, &fmt, (unsigned)shown);
			pt_destroy(fresh);
			if (memcmp(a, b, (size_t)W * S * H * S * 4)) {
				printf("FAIL: frame %d (ripple %d) is not the picture of frame %d's light\n", f, ripple, shown);
				fails++;
			}
		}
		/* back to a fresh light for the next round */
		pt_destroy(c);
		c = pt_create();
	}
	printf(fails ? "pt drift: %d FAILED\n" : "pt drift: every frame one whole light, and the shader's\n", fails);
	return fails != 0;
}
