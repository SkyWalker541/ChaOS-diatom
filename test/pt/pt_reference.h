/* PT SkyWalker541 - test reference. See pt_reference.c. */
#ifndef PT_REFERENCE_H
#define PT_REFERENCE_H

#include <stddef.h>
#include <stdint.h>
#include "../../shaders/pt/pt_shader.h"

/* One frame, the slow and literal way: RGB bytes, (w*s) x (h*s). */
void pt_reference_render(const pt_params *p, const uint32_t *src, int w, int h,
                         size_t pitch, int s, unsigned char *dst_rgb, unsigned frame);

#endif
