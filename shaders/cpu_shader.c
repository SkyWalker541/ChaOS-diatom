/* The CPU shaders, one interface (ChaOS). See cpu_shader.h. */
#include "cpu_shader.h"
#include "crts.h"
#include "lcd3x.h"

#include <stdlib.h>
#include <string.h>

struct cs_ctx {
	int kind;
	pt_ctx    *pt;
	crts_ctx  *crt;
	lcd3x_ctx *lcd;
};

/* Each thread's own state, made for the kind it is first asked to draw */
struct cs_band {
	pt_band   *pt;
	crts_band *crt;
};

const char *cs_name(int kind)
{
	switch (kind) {
	case CS_PT:  return "PT SkyWalker541";
	case CS_CRT: return "crt-lottes-fast";
	case CS_LCD: return "lcd3x";
	}
	return NULL;
}

int cs_nparams(int kind)
{
	switch (kind) {
	case CS_PT:  return PT_NPARAMS;
	case CS_CRT: return CRTS_NPARAMS;
	case CS_LCD: return LCD3X_NPARAMS;
	}
	return 0;
}

const char *cs_param_name(int kind, int i)
{
	if (i < 0 || i >= cs_nparams(kind)) return NULL;
	switch (kind) {
	case CS_PT:  return pt_param_table[i].name;
	case CS_CRT: return crts_params[i].name;
	case CS_LCD: return lcd3x_params[i].name;
	}
	return NULL;
}

float cs_param_def(int kind, int i)
{
	if (i < 0 || i >= cs_nparams(kind)) return 0.0f;
	switch (kind) {
	case CS_PT:  return pt_param_table[i].def;
	case CS_CRT: return crts_params[i].def;
	case CS_LCD: return lcd3x_params[i].def;
	}
	return 0.0f;
}

int cs_param_index(int kind, const char *name)
{
	switch (kind) {
	case CS_PT:  return pt_param_index(name);
	case CS_CRT: return crts_param_index(name);
	case CS_LCD: return lcd3x_param_index(name);
	}
	return -1;
}

cs_ctx *cs_create(int kind)
{
	cs_ctx *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	c->kind = kind;
	switch (kind) {
	case CS_PT:  c->pt = pt_create();    if (c->pt) return c;  break;
	case CS_CRT: c->crt = crts_create(); if (c->crt) return c; break;
	case CS_LCD: c->lcd = lcd3x_create(); if (c->lcd) return c; break;
	}
	free(c);
	return NULL;
}

void cs_destroy(cs_ctx *c)
{
	if (!c) return;
	pt_destroy(c->pt);
	crts_destroy(c->crt);
	lcd3x_destroy(c->lcd);
	free(c);
}

int cs_kind(const cs_ctx *c) { return c ? c->kind : CS_NONE; }

void cs_set_params(cs_ctx *c, const float *v)
{
	switch (c->kind) {
	case CS_PT: {
		pt_params p;
		memcpy(p.v, v, sizeof p.v);
		pt_set_params(c->pt, &p);
		break;
	}
	case CS_CRT: crts_set_params(c->crt, v); break;
	case CS_LCD: lcd3x_set_params(c->lcd, v); break;
	}
}

int cs_begin_frame(cs_ctx *c, const uint32_t *src, int w, int h, size_t src_pitch,
                   int scale, unsigned frame)
{
	switch (c->kind) {
	case CS_PT:  return pt_begin_frame(c->pt, src, w, h, src_pitch, scale, frame);
	case CS_CRT: return crts_begin_frame(c->crt, src, w, h, src_pitch, scale);
	case CS_LCD: return lcd3x_begin_frame(c->lcd, src, w, h, src_pitch, scale);
	}
	return -1;
}

cs_band *cs_band_create(void)
{
	return calloc(1, sizeof(cs_band));
}

void cs_band_destroy(cs_band *b)
{
	if (!b) return;
	pt_band_destroy(b->pt);
	crts_band_destroy(b->crt);
	free(b);
}

int cs_render_rows(const cs_ctx *c, cs_band *b, int y0, int y1,
                   uint32_t *rows, size_t pitch, const pt_format *fmt)
{
	switch (c->kind) {
	case CS_PT:
		if (!b->pt && !(b->pt = pt_band_create())) return -1;
		return pt_render_rows(c->pt, b->pt, y0, y1, rows, pitch, fmt);
	case CS_CRT:
		if (!b->crt && !(b->crt = crts_band_create())) return -1;
		return crts_render_rows(c->crt, b->crt, y0, y1, rows, pitch, fmt);
	case CS_LCD:
		return lcd3x_render_rows(c->lcd, y0, y1, rows, pitch, fmt);
	}
	return -1;
}
