/* Which shader a game gets (ChaOS).
 *
 * Each console has the one shader that suits it (shaders/cpu_shader.h):
 *
 *         gb gbc gba             PT SkyWalker541
 *         nes sms md pce sfc     crt-lottes-fast
 *         gg ngp ngpc            lcd3x
 *
 * The launcher decides. Before each RUN it sends SETSHADER with a small file
 * of the settings for that game's console, written from its Shaders menu:
 *
 *         console = gbc          the console's tag, lower case
 *         enabled = 1            on, or 0 for off
 *         PT_PIXEL_EFFECT = 2    any of its shader's parameters, by GLSL name
 *
 * and sends it again whenever they change in the in-game menu, which applies
 * them at once (diatom_shading_set_file). An empty path means no shader. The
 * file's `console` is the shelf the game is on, which is a better answer than
 * the ROM's extension; without one, the game is placed as below.
 *
 * Without a SETSHADER (a launcher that knows nothing of shaders), pt.cfg
 * beside the diatom program is read instead, in the same form plus [gb] /
 * [gbc] / [gba] sections whose lines apply to that console only; or the file
 * DIATOM_PT_CFG names, for the desktop. Without either, the shader's
 * recommended settings apply (recommended()). Then only mGBA's games are
 * shaded, and which of the three a game is comes from its file's extension,
 * or for a zipped one from the width it draws at (240 is a GBA's).
 *
 * The other consoles are shaded only when the launcher names them.
 *
 * With PT on, the shader is the whole look, so mGBA's own colour
 * settings follow the shader README's "Emulator core settings" for the Game
 * system in use: colour correction, interframe blending and, for a Game Boy,
 * no colourization (its four plain greys). Set for that game only: options set
 * here lapse when it ends (options.c). Turning the shader off mid-game leaves
 * them as they are until the game ends.
 */
#include "shading.h"
#include "diatom.h"
#include "diatom_port.h"
#include "pt_shader.h"
#include "cpu_shader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

enum { CON_NONE, CON_GB, CON_GBC, CON_GBA,
       CON_NES, CON_SMS, CON_MD, CON_PCE, CON_SFC,
       CON_GG, CON_NGP, CON_NGPC, CON_COUNT };
static const char *const CON_NAME[CON_COUNT] = {
	"", "gb", "gbc", "gba", "nes", "sms", "md", "pce", "sfc", "gg", "ngp", "ngpc"
};
/* Each console's shader */
static const int CON_KIND[CON_COUNT] = {
	CS_NONE, CS_PT, CS_PT, CS_PT, CS_CRT, CS_CRT, CS_CRT, CS_CRT, CS_CRT,
	CS_LCD, CS_LCD, CS_LCD
};
#define MAXP 64

static int con_named(const char *s)
{
	int i;
	for (i = CON_GB; i < CON_COUNT; i++)
		if (!strcasecmp(s, CON_NAME[i])) return i;
	return CON_NONE;
}

/* The launcher's file, from SETSHADER: g_file_set says one came, g_file empty
 * says no shader. */
static char g_file[1024];
static int  g_file_set;

/* Which of the three a game is, or CON_NONE. */
static int console_of(const char *core, const char *rom, int base_w)
{
	const char *base, *dot;

	if (!core) return CON_NONE;
	base = strrchr(core, '/');
	base = base ? base + 1 : core;
	if (strncmp(base, "mgba", 4)) return CON_NONE;

	dot = rom ? strrchr(rom, '.') : NULL;
	if (dot) {
		if (!strcasecmp(dot, ".gb") || !strcasecmp(dot, ".dmg")) return CON_GB;
		if (!strcasecmp(dot, ".gbc") || !strcasecmp(dot, ".cgb")) return CON_GBC;
		if (!strcasecmp(dot, ".gba") || !strcasecmp(dot, ".agb")) return CON_GBA;
	}
	/* A zip, or no ROM: the size says GBA or not, and a Game Boy game drawn
	 * plainly in color is a Game Boy Color's. */
	return base_w == 240 ? CON_GBA : CON_GBC;
}

/* PT: the shader's README, "Recommended Settings": Game system and Backing
 * color per console. GBA is the original, unlit model, whose screen is the one
 * this shader recreates; pt.cfg turns it into an SP (3, and Backing color 3).
 *
 * crt-lottes-fast: its own defaults, but flat (CURVATURE 0): the picture sits
 * in a frame on the Brick, square-cornered, and a flat screen costs the CPU
 * half what a curved one does. The launcher's recommended settings are the
 * same (chaos src/look.c). lcd3x: its own defaults. */
static void recommended(int con, float *p)
{
	int kind = CON_KIND[con], i;

	for (i = 0; i < cs_nparams(kind); i++) p[i] = cs_param_def(kind, i);
	switch (con) {
	case CON_GB:  p[PT_GAME_SYSTEM] = 1; p[PT_BACKING_COLOR] = 1; break;
	case CON_GBC: p[PT_GAME_SYSTEM] = 2; p[PT_BACKING_COLOR] = 1; break;
	case CON_GBA: p[PT_GAME_SYSTEM] = 4; p[PT_BACKING_COLOR] = 2; break;
	}
	if (kind == CS_CRT) p[cs_param_index(CS_CRT, "CURVATURE")] = 0.0f;
}

static char *trim(char *s)
{
	char *e;
	while (isspace((unsigned char)*s)) s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
	return s;
}

/* The settings' path: the launcher's file, DIATOM_PT_CFG, or pt.cfg beside
 * this program. */
static int cfg_path(char *out, size_t cap)
{
	const char *env = getenv("DIATOM_PT_CFG");
	char exe[512];
	ssize_t n;
	char *slash;

	if (g_file_set) {
		if (!g_file[0]) return 0;
		snprintf(out, cap, "%s", g_file);
		return 1;
	}
	if (env && *env) {
		snprintf(out, cap, "%s", env);
		return 1;
	}
	n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n <= 0) return 0;
	exe[n] = '\0';
	slash = strrchr(exe, '/');
	if (!slash) return 0;
	*slash = '\0';
	snprintf(out, cap, "%s/pt.cfg", exe);
	return 1;
}

/* Apply pt.cfg's lines for this console: the shared part, then its section.
 * Two passes, so a section wins wherever it sits in the file. */
static void read_cfg(int con, float *p, int *enabled)
{
	char path[1100], line[256];
	int pass;
	FILE *f;

	if (!cfg_path(path, sizeof path) || !(f = fopen(path, "r"))) return;
	for (pass = 0; pass < 2; pass++) {
		const char *sect = "";
		static char cur[16];

		rewind(f);
		cur[0] = '\0';
		while (fgets(line, sizeof line, f)) {
			char *s = trim(line), *eq, *key, *val;
			int k, mine;

			if (!*s || *s == '#' || *s == ';') continue;
			if (*s == '[') {
				char *end = strchr(s, ']');
				if (end) *end = '\0';
				snprintf(cur, sizeof cur, "%s", trim(s + 1));
				for (k = 0; cur[k]; k++) cur[k] = (char)tolower((unsigned char)cur[k]);
				sect = cur;
				continue;
			}
			mine = pass == 0 ? sect[0] == '\0' : !strcmp(sect, CON_NAME[con]);
			if (!mine || !(eq = strchr(s, '='))) continue;
			*eq = '\0';
			key = trim(s);
			val = trim(eq + 1);
			if (!strcasecmp(key, "console")) continue;   /* file_console */
			if (!strcasecmp(key, "enabled")) {
				*enabled = atoi(val) != 0;
				continue;
			}
			k = cs_param_index(CON_KIND[con], key);
			if (k < 0) {
				fprintf(stderr, "diatom: %s: %s has no setting called %s\n", path,
				        cs_name(CON_KIND[con]), key);
				continue;
			}
			p[k] = (float)atof(val);
		}
	}
	fclose(f);
	printf("diatom: shader settings read from %s\n", path);
}

/* The shader README's "Emulator core settings", as mGBA's options, for the
 * Game system the shader is set to (Manual takes the console's own):
 *
 *   Game system        Color correction   Interframe blending
 *   1 Game Boy         off                off                 and no colourization
 *   2 Game Boy Color   GBC                off
 *   3 GBA SP           GBA                Smart
 *   4 GBA Original     GBA                LCD ghosting (accurate)
 */
static void core_options(int con, const float *p)
{
	int sys = (int)(p[PT_GAME_SYSTEM] + 0.5f);
	const char *cc, *blend;

	if (CON_KIND[con] != CS_PT) return;     /* mGBA's alone */
	if (sys < 1 || sys > 4) sys = con == CON_GB ? 1 : con == CON_GBC ? 2 : 4;
	switch (sys) {
	case 1:  cc = "OFF"; blend = "OFF";          break;
	case 2:  cc = "GBC"; blend = "OFF";          break;
	case 3:  cc = "GBA"; blend = "mix_smart";    break;
	default: cc = "GBA"; blend = "lcd_ghosting"; break;
	}
	diatom_options_set("mgba_color_correction", cc);
	diatom_options_set("mgba_interframe_blending", blend);
	if (sys == 1) {
		diatom_options_set("mgba_gb_colors", "Grayscale");
		diatom_options_set("mgba_gb_colors_preset", "0");
	}
	printf("diatom: mGBA set for the shader: color correction %s, interframe blending %s%s\n",
	       cc, blend, sys == 1 ? ", no colourization" : "");
}

/* The console the launcher's file names: CON_NONE when there is no file, or
 * it names none (then the game is placed by console_of). -1 when the launcher
 * said no shader. */
static int file_console(void)
{
	char line[256];
	int con = CON_NONE;
	FILE *f;

	if (!g_file_set) return CON_NONE;
	if (!g_file[0]) return -1;
	if (!(f = fopen(g_file, "r"))) return CON_NONE;
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line), *eq = strchr(s, '=');
		if (!eq) continue;
		*eq = '\0';
		if (!strcasecmp(trim(s), "console")) { con = con_named(trim(eq + 1)); break; }
	}
	fclose(f);
	return con;
}

/* What the shader is for this game: the console, its settings, and whether it
 * is on. CON_NONE when the game is not one of the three. */
static int choose(const char *core, const char *rom, int base_w, float *p, int *enabled)
{
	int fc = file_console();
	int con = fc > 0 ? fc : fc < 0 ? CON_NONE : console_of(core, rom, base_w);

	*enabled = 0;
	if (con == CON_NONE) return CON_NONE;
	*enabled = 1;
	recommended(con, p);
	read_cfg(con, p, enabled);
	return con;
}

static int g_options_set;    /* this game's mGBA options have been set */
static int g_running;        /* between diatom_shading_start and _end */
static int g_on;             /* the shader is drawing this game */
static int g_cur_con;        /* the console this game was placed as */
static float g_cur[MAXP];    /* and the settings it is drawn with */
static char g_core[1024], g_rom[1024];
static int  g_base_w;

void diatom_shading_prepare(const char *core, const char *rom)
{
	const char *dot = rom ? strrchr(rom, '.') : NULL;
	int enabled, con;
	float p[MAXP];

	g_options_set = 0;
	g_running = 0;
	/* Before the game loads, only a ROM whose console is named, by the
	 * launcher or its extension, can be placed; otherwise it waits for its
	 * size (diatom_shading_start). */
	if (file_console() == CON_NONE && (!dot || !strcasecmp(dot, ".zip"))) return;
	con = choose(core, rom, 0, p, &enabled);
	if (con != CON_NONE && enabled) {
		core_options(con, p);
		g_options_set = 1;
	}
}

void diatom_shading_start(const char *core, const char *rom, int base_w)
{
	int enabled, con;
	float p[MAXP];

	snprintf(g_core, sizeof g_core, "%s", core ? core : "");
	snprintf(g_rom, sizeof g_rom, "%s", rom ? rom : "");
	g_base_w = base_w;
	g_running = 1;
	con = choose(core, rom, base_w, p, &enabled);
	g_cur_con = con;
	memcpy(g_cur, p, sizeof g_cur);
	g_on = con != CON_NONE && enabled;
	if (con == CON_NONE) {
		diatom_port_shader(DIATOM_SHADER_NONE, NULL, 0);
		return;
	}
	/* Options not yet set (a zip's, now its console is known, or a change):
	 * mGBA takes them on the next frame. */
	if (enabled && !g_options_set) {
		core_options(con, p);
		g_options_set = 1;
	}
	printf("diatom: shader for %s: %s\n", CON_NAME[con], enabled ? cs_name(CON_KIND[con]) : "off");
	diatom_port_shader(enabled ? (diatom_shader)CON_KIND[con] : DIATOM_SHADER_NONE, p,
	                   cs_nparams(CON_KIND[con]));
}

void diatom_shading_set_file(const char *path)
{
	snprintf(g_file, sizeof g_file, "%s", path ? path : "");
	g_file_set = 1;
	if (!g_running) return;
	/* Mid-game, from the in-game menu: the same choice again, at once. A
	 * change of Game system changes mGBA's settings with it. */
	g_options_set = 0;
	{
		int enabled;
		float p[MAXP];
		int con = choose(g_core[0] ? g_core : NULL, g_rom[0] ? g_rom : NULL, g_base_w,
		                 p, &enabled);
		if (con != CON_NONE && enabled) {
			int was = (int)(g_cur[PT_GAME_SYSTEM] + 0.5f);
			int now = (int)(p[PT_GAME_SYSTEM] + 0.5f);
			if (!g_on || con != g_cur_con || was != now) core_options(con, p);
			g_options_set = 1;
		}
	}
	diatom_shading_start(g_core[0] ? g_core : NULL, g_rom[0] ? g_rom : NULL, g_base_w);
}

void diatom_shading_end(void)
{
	g_running = 0;
	g_on = 0;
	g_file_set = 0;
	g_file[0] = '\0';
}

int diatom_shading_current(float *params)
{
	int kind;
	if (!g_running || !g_on) return 0;
	kind = CON_KIND[g_cur_con];
	if (params) memcpy(params, g_cur, (size_t)cs_nparams(kind) * sizeof *params);
	return kind;
}
