/* lcd3x, by Gigaherz (public domain), as libretro's glsl-shaders carries it in
 * handheld/shaders/lcd3x.glsl, on the CPU (ChaOS).
 *
 * Each game pixel, nearest, times a sine across the screen row for the LCD's
 * red, green and blue stripes and a sine down the column for its rows - which
 * at a whole-number scale depend on the screen pixel's place within its game
 * pixel alone. So a frame is the game's pixels times two small tables; drawn,
 * as the PT shader is (pt_shader.h), in bands from any number of threads.
 */
#ifndef LCD3X_H
#define LCD3X_H

#include <stddef.h>
#include <stdint.h>

#include "pt_shader.h"     /* pt_format */

enum { LCD3X_BRIGHTEN_SCANLINES, LCD3X_BRIGHTEN_LCD, LCD3X_NPARAMS };

typedef struct { const char *name; float def, min, max, step; } lcd3x_param;
extern const lcd3x_param lcd3x_params[LCD3X_NPARAMS];
int lcd3x_param_index(const char *name);

typedef struct lcd3x_ctx lcd3x_ctx;

lcd3x_ctx *lcd3x_create(void);
void       lcd3x_destroy(lcd3x_ctx *c);
void       lcd3x_set_params(lcd3x_ctx *c, const float *v);   /* LCD3X_NPARAMS */
/* src is 0x00RRGGBB, w x h, src_pitch pixels a row; kept, not copied, until
 * the frame is drawn. 0 on success. */
int        lcd3x_begin_frame(lcd3x_ctx *c, const uint32_t *src, int w, int h,
                             size_t src_pitch, int scale);
/* The same, drawn to any ow x oh: each screen pixel shows the game pixel
 * under its centre, the stripes running to the screen's own size. */
int        lcd3x_begin_frame_size(lcd3x_ctx *c, const uint32_t *src, int w, int h,
                                  size_t src_pitch, int ow, int oh);
/* Screen rows [y0, y1) into `rows`, which is where row y0 goes. */
int        lcd3x_render_rows(const lcd3x_ctx *c, int y0, int y1,
                             uint32_t *rows, size_t pitch, const pt_format *fmt);

#endif
