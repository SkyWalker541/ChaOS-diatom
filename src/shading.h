/* Which shader a game gets (ChaOS). See shading.c. */
#ifndef DIATOM_SHADING_H
#define DIATOM_SHADING_H

/* Before a game loads: set the emulator options the shader needs, when the
 * ROM's extension says which console it is. */
void diatom_shading_prepare(const char *core, const char *rom);

/* Choose and send the shader for a game that has just started: its core,
 * its ROM (or NULL) and the width it draws at. */
void diatom_shading_start(const char *core, const char *rom, int base_w);

#endif
