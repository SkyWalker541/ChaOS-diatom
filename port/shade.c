/* Shaders for ports that draw with the CPU (ChaOS). See shade.h.
 *
 * One frame, shaded:
 *
 *   1. the core's frame is converted once to 0x00RRGGBB, the form the shader
 *      reads (a GBA frame is 38,400 pixels, so this costs next to nothing);
 *   2. the shader begins the frame: the per-frame work over the game's pixels;
 *   3. the screen rows are split into bands, two drawn by helper threads and
 *      one by the calling thread, and the call returns when all three are done.
 *
 * Three bands because the Brick has four cores: one stays with whatever else
 * runs (the flip thread, sound), and the frame is drawn three times as fast as
 * on one. The bands give exactly the picture one thread would (test/pt checks
 * that), so the split is only a matter of speed.
 *
 * The call blocks the emulator for the time the shader takes, as the plain
 * scaler it replaces does. Every SHADE_LOG_EVERY frames the average time goes
 * to the log, which is how its cost on the device is read.
 */
#include "shade.h"
#include "cpu_shader.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADE_HELPERS    2
#define SHADE_LOG_EVERY  600     /* about ten seconds at 60 fps */

/* ---- the host's choice ---------------------------------------------------- */

static pthread_mutex_t g_set_mx = PTHREAD_MUTEX_INITIALIZER;
static int       g_set_new;          /* a choice not yet taken up */
static int       g_set_kind;         /* CS_*, CS_NONE for off */
static float     g_set_params[64];
static int       g_on;               /* as taken up by the drawing thread */
/* The speed check's: frames drawn since the settings were taken up, and
 * their time. Under g_set_mx. */
static uint64_t  g_cost_us;
static unsigned  g_cost_n;

_Static_assert((int)DIATOM_SHADER_PT == CS_PT && (int)DIATOM_SHADER_CRT == CS_CRT &&
               (int)DIATOM_SHADER_LCD == CS_LCD && (int)DIATOM_SHADER_SCAN == CS_SCAN,
               "diatom_shader and cpu_shader.h agree");

void shade_set(diatom_shader kind, const float *params, int nparams)
{
	char m[96];

	int n = cs_nparams((int)kind);

	pthread_mutex_lock(&g_set_mx);
	g_set_kind = n > 0 && n <= 64 && params && nparams == n ? (int)kind : CS_NONE;
	if (g_set_kind) memcpy(g_set_params, params, (size_t)n * sizeof *params);
	g_set_new = 1;
	pthread_mutex_unlock(&g_set_mx);

	snprintf(m, sizeof m, "shade: %s", g_set_kind ? cs_name(g_set_kind) : "off");
	diatom_port_log(DIATOM_LOG_INFO, m);
}

bool shade_active(void)
{
	bool on;
	pthread_mutex_lock(&g_set_mx);
	on = g_set_new ? g_set_kind != CS_NONE : g_on;
	pthread_mutex_unlock(&g_set_mx);
	return on;
}

/* ---- the frame, and the threads drawing it -------------------------------- */

static cs_ctx   *g_ctx;
static cs_band  *g_own;              /* the calling thread's band */
static uint32_t *g_buf;              /* the frame as 0x00RRGGBB */
static size_t    g_buf_cap;
static unsigned  g_frame;

typedef struct {
	pthread_t t;
	cs_band  *band;
	int       y0, y1;
	int       started;
} helper;

static helper          g_help[SHADE_HELPERS];
static int             g_nhelp;      /* helpers running */
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_go = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_done = PTHREAD_COND_INITIALIZER;
static unsigned        g_gen;        /* bumped once per frame */
static int             g_left;       /* helpers still drawing this frame */
static int             g_quit;
static uint32_t       *g_out;
static size_t          g_out_pitch;
static pt_format       g_fmt;
static void          (*g_hook)(void *ctx, int y0, int y1);
static void           *g_hook_ctx;

void shade_band_hook(void (*fn)(void *ctx, int y0, int y1), void *ctx)
{
	g_hook = fn;
	g_hook_ctx = ctx;
}

static void *helper_main(void *arg)
{
	helper *hp = arg;
	unsigned seen = 0;

	for (;;) {
		pthread_mutex_lock(&g_mx);
		while (g_gen == seen && !g_quit) pthread_cond_wait(&g_go, &g_mx);
		if (g_quit) { pthread_mutex_unlock(&g_mx); return NULL; }
		seen = g_gen;
		pthread_mutex_unlock(&g_mx);

		cs_render_rows(g_ctx, hp->band, hp->y0, hp->y1, g_out + (size_t)hp->y0 * g_out_pitch,
		               g_out_pitch, &g_fmt);
		if (g_hook) g_hook(g_hook_ctx, hp->y0, hp->y1);

		pthread_mutex_lock(&g_mx);
		if (--g_left == 0) pthread_cond_signal(&g_done);
		pthread_mutex_unlock(&g_mx);
	}
}

/* Helpers start on the first shaded frame. Any that will not start leave
 * their rows to the calling thread: slower, never wrong. */
static void start_helpers(void)
{
	static int tried;
	int i;

	if (tried) return;
	tried = 1;
	for (i = 0; i < SHADE_HELPERS; i++) {
		g_help[i].band = cs_band_create();
		if (!g_help[i].band || pthread_create(&g_help[i].t, NULL, helper_main, &g_help[i])) {
			cs_band_destroy(g_help[i].band);
			g_help[i].band = NULL;
			break;
		}
		g_help[i].started = 1;
		g_nhelp++;
	}
	if (g_nhelp < SHADE_HELPERS) {
		char m[80];
		snprintf(m, sizeof m, "shade: %d of %d helper threads started", g_nhelp, SHADE_HELPERS);
		diatom_port_log(DIATOM_LOG_WARN, m);
	}
}

/* The core's frame as 0x00RRGGBB, padding columns left out. */
static bool convert(const void *src, int w, int h, size_t pitch, diatom_pixfmt fmt)
{
	size_t n = (size_t)w * (size_t)h;
	int x, y;

	if (n > g_buf_cap) {
		uint32_t *b = realloc(g_buf, n * sizeof *b);
		if (!b) return false;
		g_buf = b;
		g_buf_cap = n;
	}
	for (y = 0; y < h; y++) {
		uint32_t *o = g_buf + (size_t)y * w;
		if (fmt == DIATOM_PIX_RGB565) {
			const uint16_t *in = (const uint16_t *)((const uint8_t *)src + (size_t)y * pitch);
			for (x = 0; x < w; x++) {
				uint32_t c = in[x];
				uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
				o[x] = (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
			}
		} else {
			const uint32_t *in = (const uint32_t *)((const uint8_t *)src + (size_t)y * pitch);
			for (x = 0; x < w; x++) o[x] = in[x] & 0x00ffffffu;
		}
	}
	return true;
}

static void log_timing(uint64_t us, int w, int h, int scale)
{
	static uint64_t total, worst;
	static unsigned count;
	char m[160];

	total += us;
	if (us > worst) worst = us;
	if (++count < SHADE_LOG_EVERY) return;
	snprintf(m, sizeof m,
	         "shade: %s %dx%d at %dx, %u frames: %.2f ms average, %.2f ms worst, %d threads",
	         cs_name(cs_kind(g_ctx)), w, h, scale, count, (double)total / count / 1000.0, (double)worst / 1000.0,
	         g_nhelp + 1);
	diatom_port_log(DIATOM_LOG_INFO, m);
	total = worst = 0;
	count = 0;
}

bool shade_frame(const void *src, int w, int h, size_t pitch, diatom_pixfmt fmt,
                 int scale, uint32_t *out, size_t out_pitch,
                 unsigned r_shift, unsigned g_shift, unsigned b_shift, uint32_t opaque)
{
	uint64_t t0;
	int oh, nb, i;

	pthread_mutex_lock(&g_set_mx);
	if (g_set_new) {
		g_on = g_set_kind != CS_NONE;
		if (g_on) {
			/* another shader: the old one's context goes (the helpers
			 * draw only inside shade_frame, so none is using it) */
			if (g_ctx && cs_kind(g_ctx) != g_set_kind) { cs_destroy(g_ctx); g_ctx = NULL; }
			if (!g_ctx) g_ctx = cs_create(g_set_kind);
			if (g_ctx) cs_set_params(g_ctx, g_set_params);
		}
		g_set_new = 0;
		g_cost_us = 0;
		g_cost_n = 0;
	}
	pthread_mutex_unlock(&g_set_mx);

	if (!g_on || !g_ctx || !src || w <= 0 || h <= 0 || scale < 1 ||
	    (!g_own && !(g_own = cs_band_create()))) {
		g_hook = NULL;
		return false;
	}
	start_helpers();

	t0 = diatom_port_now_us();
	if (!convert(src, w, h, pitch, fmt) ||
	    cs_begin_frame(g_ctx, g_buf, w, h, (size_t)w, scale, g_frame++)) {
		g_hook = NULL;
		return false;
	}

	/* Bands of equal height: the calling thread takes the first. */
	oh = h * scale;
	nb = g_nhelp + 1;
	g_out = out;
	g_out_pitch = out_pitch;
	g_fmt.r_shift = r_shift; g_fmt.g_shift = g_shift; g_fmt.b_shift = b_shift;
	g_fmt.opaque = opaque;
	if (g_nhelp) {
		pthread_mutex_lock(&g_mx);
		for (i = 0; i < g_nhelp; i++) {
			g_help[i].y0 = oh * (i + 1) / nb;
			g_help[i].y1 = oh * (i + 2) / nb;
		}
		g_left = g_nhelp;
		g_gen++;
		pthread_cond_broadcast(&g_go);
		pthread_mutex_unlock(&g_mx);
	}
	cs_render_rows(g_ctx, g_own, 0, oh / nb, out, out_pitch, &g_fmt);
	if (g_hook) g_hook(g_hook_ctx, 0, oh / nb);
	if (g_nhelp) {
		pthread_mutex_lock(&g_mx);
		while (g_left) pthread_cond_wait(&g_done, &g_mx);
		pthread_mutex_unlock(&g_mx);
	}
	g_hook = NULL;
	{
		uint64_t us = diatom_port_now_us() - t0;
		log_timing(us, w, h, scale);
		pthread_mutex_lock(&g_set_mx);
		if (!g_set_new && g_cost_n < 1000000u) { g_cost_us += us; g_cost_n++; }
		pthread_mutex_unlock(&g_set_mx);
	}
	return true;
}

void shade_cost(double *ms, unsigned *frames, int *threads)
{
	pthread_mutex_lock(&g_set_mx);
	*frames = g_set_new ? 0 : g_cost_n;
	*ms = *frames ? (double)g_cost_us / *frames / 1000.0 : 0.0;
	pthread_mutex_unlock(&g_set_mx);
	*threads = (g_nhelp ? g_nhelp : SHADE_HELPERS) + 1;
}

void shade_shutdown(void)
{
	int i;

	pthread_mutex_lock(&g_mx);
	g_quit = 1;
	pthread_cond_broadcast(&g_go);
	pthread_mutex_unlock(&g_mx);
	for (i = 0; i < SHADE_HELPERS; i++)
		if (g_help[i].started) {
			pthread_join(g_help[i].t, NULL);
			cs_band_destroy(g_help[i].band);
			g_help[i].started = 0;
		}
	g_nhelp = 0;
	cs_band_destroy(g_own); g_own = NULL;
	cs_destroy(g_ctx);      g_ctx = NULL;
	free(g_buf);            g_buf = NULL; g_buf_cap = 0;
}
