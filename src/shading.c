/* Which shader a game gets (ChaOS).
 *
 * For now, until the launcher has its Shaders menu and says so itself:
 *
 *   - Game Boy, Game Boy Color and Game Boy Advance get PT SkyWalker541,
 *     everything else is drawn plainly. Those three are the games mGBA runs,
 *     and which of them a game is comes from its file's extension, or for a
 *     zipped one from the width it draws at (240 is a GBA's).
 *   - Each console starts from the shader's recommended settings for it, from
 *     its README: Game system and Backing color per console, the rest the
 *     shader's own defaults.
 *   - pt.cfg, beside the diatom program (on the card, TortOS/pt.cfg), changes
 *     any of that. It is read at the start of every game, so an edit takes
 *     effect from the next one. Format:
 *
 *         # a comment
 *         enabled = 1            on for every console, or 0 for off
 *         PT_PIXEL_EFFECT = 2    any parameter, by its GLSL name
 *
 *         [gba]                  what follows is for one console only:
 *         enabled = 0            gb, gbc or gba
 *         PT_GAME_SYSTEM = 3
 *
 *     Lines before any [section] apply to every console; a console's own
 *     section wins over them. An unknown name is logged and skipped.
 *   - With the shader on, the shader is the whole look, so mGBA's own colour
 *     settings follow the shader README's "Emulator core settings" for the
 *     Game system in use: colour correction, interframe blending and, for a
 *     Game Boy, no colourization (its four plain greys). With the shader off,
 *     the launcher's settings stand. Set for that game only: options set here
 *     lapse when it ends (options.c).
 *
 * DIATOM_PT_CFG names another file instead, for the desktop.
 */
#include "shading.h"
#include "diatom.h"
#include "diatom_port.h"
#include "pt_shader.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

enum { CON_NONE, CON_GB, CON_GBC, CON_GBA };
static const char *const CON_NAME[] = { "", "gb", "gbc", "gba" };

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

/* The shader's README, "Recommended Settings": Game system and Backing color
 * per console. GBA is the original, unlit model, whose screen is the one this
 * shader recreates; pt.cfg turns it into an SP (3, and Backing color 3). */
static void recommended(int con, pt_params *p)
{
	pt_params_default(p);
	switch (con) {
	case CON_GB:  p->v[PT_GAME_SYSTEM] = 1; p->v[PT_BACKING_COLOR] = 1; break;
	case CON_GBC: p->v[PT_GAME_SYSTEM] = 2; p->v[PT_BACKING_COLOR] = 1; break;
	case CON_GBA: p->v[PT_GAME_SYSTEM] = 4; p->v[PT_BACKING_COLOR] = 2; break;
	}
}

static char *trim(char *s)
{
	char *e;
	while (isspace((unsigned char)*s)) s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
	return s;
}

/* pt.cfg's path: DIATOM_PT_CFG, or beside this program. */
static int cfg_path(char *out, size_t cap)
{
	const char *env = getenv("DIATOM_PT_CFG");
	char exe[512];
	ssize_t n;
	char *slash;

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
static void read_cfg(int con, pt_params *p, int *enabled)
{
	char path[600], line[256];
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
			if (!strcasecmp(key, "enabled")) {
				*enabled = atoi(val) != 0;
				continue;
			}
			k = pt_param_index(key);
			if (k < 0) {
				fprintf(stderr, "diatom: pt.cfg: no setting called %s\n", key);
				continue;
			}
			p->v[k] = (float)atof(val);
		}
	}
	fclose(f);
	printf("diatom: pt.cfg read from %s\n", path);
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
static void core_options(int con, const pt_params *p)
{
	int sys = (int)(p->v[PT_GAME_SYSTEM] + 0.5f);
	const char *cc, *blend;

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

/* What the shader is for this game: the console, its settings, and whether it
 * is on. CON_NONE when the game is not one of the three. */
static int choose(const char *core, const char *rom, int base_w, pt_params *p, int *enabled)
{
	int con = console_of(core, rom, base_w);

	*enabled = 0;
	if (con == CON_NONE) return CON_NONE;
	*enabled = 1;
	recommended(con, p);
	read_cfg(con, p, enabled);
	return con;
}

static int g_options_set;    /* this game's mGBA options were set before it loaded */

void diatom_shading_prepare(const char *core, const char *rom)
{
	const char *dot = rom ? strrchr(rom, '.') : NULL;
	int enabled;
	pt_params p;

	g_options_set = 0;
	/* Before the game loads, only a ROM whose extension names its console
	 * can be placed; a zip waits for its size (diatom_shading_start). */
	if (!dot || !strcasecmp(dot, ".zip")) return;
	if (choose(core, rom, 0, &p, &enabled) != CON_NONE && enabled) {
		core_options(console_of(core, rom, 0), &p);
		g_options_set = 1;
	}
}

void diatom_shading_start(const char *core, const char *rom, int base_w)
{
	int enabled, con;
	pt_params p;

	con = choose(core, rom, base_w, &p, &enabled);
	if (con == CON_NONE) {
		diatom_port_shader(DIATOM_SHADER_NONE, NULL, 0);
		return;
	}
	/* A zip's options, now its console is known: mGBA takes them on the next
	 * frame. */
	if (enabled && !g_options_set) core_options(con, &p);
	printf("diatom: shader for %s: %s\n", CON_NAME[con], enabled ? "PT SkyWalker541" : "off");
	diatom_port_shader(enabled ? DIATOM_SHADER_PT : DIATOM_SHADER_NONE, p.v, PT_NPARAMS);
}
