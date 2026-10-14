/*
 * Afterlife's resource file manager ("HILEVEL ... RcInitialize /
 * RcLoadResource / RcOpenResource", around 0x4cd9b0-0x4ce800, plus the small
 * file helpers at 0x4d202d-0x4d22d0). This is game code, not compbun: it
 * reads the JIFF .000 bundles (docs/formats/jiff.md) and is plugged into the
 * bottom of the compbun stack, so compbun "files" are resource IDs.
 *
 * Two bundles are indexed: the local "Resource file" from ALIFE.INI (e.g.
 * FULL.000 or MEDIUM.000 from a hard-disk install, or "None") and the CD's
 * "CD Path" (ALIFE.000). Lookups try the local index first, then the CD.
 */
#ifndef AFTERLIFE_GAME_RESOURCE_H
#define AFTERLIFE_GAME_RESOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"

typedef struct {
    uint32_t *ids;     /* RCID */
    uint32_t *offsets; /* OFST */
    uint32_t count;    /* HEAD */
} RcIndex;

/* Long enough for any decimal u32 plus NUL. */
#define RC_NAME_LEN 12

/* 0x4cd9b0, with the two INI values passed in. Returns false if the CD
 * bundle can't be indexed (the original then shows "Please insert the
 * Afterlife CD"); a missing local bundle is silently ignored. */
bool rc_initialize(const char *resource_file, const char *cd_path);
void rc_shutdown(void); /* port-only: frees the indexes */

const RcIndex *rc_local_index(void);
const RcIndex *rc_cd_index(void);

/* 0x4cda8e: a file positioned at the start of resource `id`'s data, or NULL. */
PlatFile *rc_open_resource(uint32_t id);
/* 0x4cdd61: data size of resource `id`, 0 if unknown. */
long rc_resource_size(uint32_t id);

/* compbun opens resources by name; the name is the decimal ID. (The original
 * passes the integer ID itself where compbun expects a char pointer.) */
void rc_resource_name(uint32_t id, char out[RC_NAME_LEN]);

/* What snd_initialize (0x4b9510) does before compbun_init(1, 0): point the
 * C File Layer's open/close/read/seek/tell slots at the resource functions. */
void rc_hook_compbun(void);

#endif
