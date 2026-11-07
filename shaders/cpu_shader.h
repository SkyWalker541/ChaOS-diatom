/* The CPU shaders, one interface (ChaOS).
 *
 * ChaOS-diatom draws three shaders with the CPU, each for the consoles it
 * suits (the launcher shows each console only its own):
 *
 *   CS_PT    PT SkyWalker541 (shaders/pt)        Game Boy, Color, Advance
 *   CS_CRT   crt-lottes-fast (shaders/crt)       NES, SNES, Genesis, Master
 *                                                System, PC Engine
 *   CS_LCD   lcd3x (shaders/lcd)                 Game Gear, Neo Geo Pocket
 *
 * Each is drawn the same way - a frame begun once, then its screen rows in
 * bands from any number of threads, each with its own cs_band - so the
 * drawing code (port/shade.c) and the pause's preview (src/main.c) need not
 * know which is which. The values are diatom_shader's (diatom_port.h).
 */
#ifndef CPU_SHADER_H
#define CPU_SHADER_H

#include <stddef.h>
#include <stdint.h>

#include "pt_shader.h"     /* pt_format */

enum { CS_NONE, CS_PT, CS_CRT, CS_LCD, CS_KINDS };

typedef struct cs_ctx cs_ctx;
typedef struct cs_band cs_band;

/* A kind's name ("PT SkyWalker541"), its parameters' count, and each one's
 * GLSL name and default. NULL / 0 for CS_NONE or an unknown kind. */
const char *cs_name(int kind);
int         cs_nparams(int kind);
const char *cs_param_name(int kind, int i);
float       cs_param_def(int kind, int i);
int         cs_param_index(int kind, const char *name);

cs_ctx  *cs_create(int kind);
void     cs_destroy(cs_ctx *c);
int      cs_kind(const cs_ctx *c);
/* cs_nparams(kind) values */
void     cs_set_params(cs_ctx *c, const float *v);
/* src is 0x00RRGGBB, kept until the frame is drawn. 0 on success. */
int      cs_begin_frame(cs_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
                        int scale, unsigned frame);
cs_band *cs_band_create(void);
void     cs_band_destroy(cs_band *b);
/* Screen rows [y0, y1) into `rows`, which is where row y0 goes. */
int      cs_render_rows(const cs_ctx *c, cs_band *b, int y0, int y1,
                        uint32_t *rows, size_t pitch, const pt_format *fmt);

#endif
