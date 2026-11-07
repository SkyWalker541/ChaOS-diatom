/* Which shader a game gets (ChaOS). See shading.c. */
#ifndef DIATOM_SHADING_H
#define DIATOM_SHADING_H

/* Choose and send the shader for a game that has just started: its core,
 * its ROM (or NULL) and the width it draws at. */
void diatom_shading_start(const char *core, const char *rom, int base_w);

#endif
