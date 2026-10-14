/*
 * compbun bundle manager, "Bundling layer" (compbun:source:bndlemgr.c).
 *
 * Serves 8.3-named files out of up to two open bundle files (fileSystem[]),
 * each with a sorted table of contents searched with bsearch. Afterlife never
 * uses it: it calls compbun_init(1, 0), so the layer is never pushed, and no
 * code in ALIFE.EXE fills fileSystem[] or sets bundleInitialized. The .000
 * resource bundles are read by the game's own resource code instead
 * (src/game/resource.c). See docs/compbun.md.
 */
#ifndef AFTERLIFE_COMPBUN_BNDLEMGR_H
#define AFTERLIFE_COMPBUN_BNDLEMGR_H

#include "compbun/cbres.h"

extern CbLayer bndl_layer;

int bndl_init(void);
void bndl_shutdown(void);
int bndl_find_entry(const char *name);

#endif
