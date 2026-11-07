/* Which shader a game gets (ChaOS). See shading.c. */
#ifndef DIATOM_SHADING_H
#define DIATOM_SHADING_H

/* Before a game loads: set the emulator options the shader needs, when the
 * ROM's extension says which console it is. */
void diatom_shading_prepare(const char *core, const char *rom);

/* Choose and send the shader for a game that has just started: its core,
 * its ROM (or NULL) and the width it draws at. */
void diatom_shading_start(const char *core, const char *rom, int base_w);

/* SETSHADER: the launcher's settings file for the next game, or the one
 * running, which takes them at once. Empty: no shader. */
void diatom_shading_set_file(const char *path);

/* The game has ended: forget its shader and the launcher's file, so a launcher
 * that sends none next time gets pt.cfg's. */
void diatom_shading_end(void);

/* Is the shader drawing the running game? Its PT_NPARAMS settings into
 * `params` when it is. */
int diatom_shading_current(float *params);

#endif
