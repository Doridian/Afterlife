/*
 * The AKOS costume engine (ALIFE.EXE 0x4d29b0-0x4d2c2c costume slots,
 * 0x4d5c35-0x4d7a01 animation and drawing, 0x4dbee0-0x4de0cf the codec 1
 * renderer). It is a trimmed copy of the SCUMM v7 AKOS code (Full Throttle,
 * The Dig): "actors" with 16 limbs each, every limb running a sequence from
 * the costume's AKSQ bytecode, drawn as RLE cels into an 8-bit surface.
 * Afterlife uses it only for the advisors' talking heads (gfx/akos_talk.c).
 *
 * See docs/formats/akos.md for the format, the bytecode and the state.
 */
#ifndef AFTERLIFE_GFX_AKOS_H
#define AFTERLIFE_GFX_AKOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "formats/akos.h"
#include "gfx/surface.h"

#define AKOS_MAX_COSTUMES 32 /* slots 1..31 (0x54eca0, slot 0 unused) */
#define AKOS_MAX_ACTORS 32   /* actors 1..31; actor 0 is never drawn */
#define AKOS_LIMBS 16
#define AKOS_VARS 16
#define AKOS_QUEUE_SIZE 10 /* 0x54fff8: room for 10 entries before 0x550098 */

/* Limb command types (the AKCH command byte, stored in active[]). */
enum {
    AKOS_LIMB_OFF = 0,
    AKOS_LIMB_DISABLE = 1, /* AKCH: switch the limb off */
    AKOS_LIMB_LOOP = 2,    /* plain cel list, loops from start after end */
    AKOS_LIMB_ONCE = 3,    /* plain cel list, stops on end */
    AKOS_LIMB_STOP = 4,    /* AKCH: set the limb's stopped bit (not stored) */
    AKOS_LIMB_START = 5,   /* AKCH: clear the stopped bit (not stored) */
    AKOS_LIMB_CODE = 6,    /* AKSQ bytecode */
    AKOS_LIMB_FROZEN = 7,  /* never advances */
};

/* Special animation numbers for akos_set_anim (0x4d6434). 0x38-0x3c are
 * replaced by the actor's alias table (never set by Afterlife, so they
 * become 0); AKOS_ANIM_TURN only changes the direction. Animation init_anim
 * (0) also resets the actor first. */
#define AKOS_ANIM_ALIAS_FIRST 0x38
#define AKOS_ANIM_TURN 0x3e

/* Queued commands (0x4d66ba). The queue is never processed by the game, see
 * akos_process_queue. */
enum {
    AKOS_QC_HIDE = 1,       /* 0xc086 */
    AKOS_QC_SOUND = 3,      /* 0xc015 / 0xc061: actor sound slot */
    AKOS_QC_START_ANIM = 4, /* 0xc080 / 0xc081 / 0xc084 */
    AKOS_QC_CLIP = 5,       /* 0xc083 */
    AKOS_QC_DRAW_OFFS = 6,  /* 0xc087 */
    AKOS_QC_SOUND_VOL = 7,  /* 0xc089 subcommands 1..3 */
    AKOS_QC_SOUND_PAN = 8,
    AKOS_QC_SOUND_PRIO = 9,
};

typedef struct {
    int32_t cmd, actor, a, b;
} AkosQueued;

/* Per-actor state. The original spreads it over parallel arrays indexed by
 * actor; the original address of each is noted. The first five fields are
 * the 0x114-byte animation record at 0x649980 + actor * 0x114. */
typedef struct {
    uint8_t active[AKOS_LIMBS];  /* +0x00 limb command type (AKOS_LIMB_*) */
    uint32_t stopped;            /* +0x10 bit n: limb n frozen, not drawn */
    uint32_t curpos[AKOS_LIMBS]; /* +0x14 AKSQ offsets */
    uint32_t start[AKOS_LIMBS];  /* +0x54 */
    uint32_t end[AKOS_LIMBS];    /* +0x94 */
    uint32_t frame[AKOS_LIMBS];  /* +0xd4 AKCH index that started the limb, 0xffff = none */
    uint16_t costume;            /* 0x64bad0 costume slot */
    int32_t direction;           /* 0x64bb10 0..3 */
    int32_t vars[AKOS_VARS];     /* 0x64bb90 animation variables */
    uint8_t sounds[8];           /* 0x64c420 sound slots for 0xc015/0xc061/0xc089 */
    int32_t flip;                /* 0x64cd10 set by 0xc08a: inverts the mirror flag */
    uint8_t remap[256];          /* 0x647a20 AKPL entry -> screen colour, 0xff = AKPL value */
    uint8_t shadow_mode;         /* 0x64cd90 never set: 1..3 select shadow renderers */
    uint8_t speed;               /* 0x6498a0 frames per step - 1 (always 0) */
    uint8_t counter;             /* 0x649860 */
    uint8_t last_anim;           /* 0x649880 */
    uint8_t alias[5];            /* 0x64baa0, 0x649840, 0x6498c0, 0x6498e0, 0x649820 */
    int32_t needs_redraw;        /* 0x649900 */
    int32_t top, bottom;         /* 0x64ba00 / 0x6479a0: drawn rows */
    GfxRect bounds;              /* the talker's rect at 0x54f170 (see akos_talk) */
} AkosActor;

/* 0x4d29b0: clears the costume slots and builds the colour distance tables.
 * Frees anything loaded before (port-only). */
void akos_initialize(void);
/* Port-only: frees the loaded costumes. */
void akos_shutdown(void);

/* 0x4d29ea: loads resource `res_id` into a free costume slot. Returns the
 * slot (1..31), or 0 if the resource is missing, isn't a valid AKOS or no
 * slot is free. */
int akos_costume_load(uint32_t res_id);
/* 0x4d2a29: takes ownership of a malloc'd AKOS resource; returns the slot
 * or 0 (and frees it) on failure. */
int akos_costume_add(uint8_t *data, size_t len);
/* 0x4d2a6f: frees slot `slot`; -1 if out of range. */
int akos_costume_free(int slot);
/* 0x4d2c2c: the parsed costume in `slot`, or NULL. */
const AkosFile *akos_costume(int slot);

/* Port-only: the state of actor n (1..31), or NULL. */
AkosActor *akos_actor(int actor);
/* Port-only: binds a costume slot to an actor (the talker does this by
 * writing 0x64bad0 directly). */
void akos_set_costume(int actor, int slot);

/* 0x4d6360 (wrapper 0x4d2ad1): every limb off, stopped mask clear. */
void akos_reset(int actor);
/* 0x4d6434 (wrapper 0x4d2b01): starts animation `anim` facing `dir` (0..3)
 * on all limbs it defines, after resolving the aliases above. */
void akos_set_anim(int actor, int anim, int dir);
/* 0x4d62a5: restarts every limb's animation for direction `dir`. */
void akos_set_direction(int actor, int dir);

/* 0x4d78c6 (wrapper 0x4d2bb0): runs one step of every limb's sequence.
 * Returns true if any limb moved to a new position (the talker uses false
 * to detect that the head has come to rest). */
bool akos_increase_anims(int actor);

/* 0x4d76dc (wrapper 0x4d2b39): draws the actor's limbs at (x, y) into
 * `dst` (NULL = screen). scale_x / scale_y are 0..255; both 0xff means
 * unscaled. Extends the actor's bounds rectangle by every cel's box. */
void akos_draw_actor(int actor, GfxSurface *dst, int x, int y, int scale_x, int scale_y);

/* Bounds of what akos_draw_actor drew since the last reset (inclusive
 * left/top, exclusive right/bottom, as the original keeps them). */
void akos_reset_bounds(int actor);  /* 0x4d3508 */
GfxRect akos_get_bounds(int actor); /* 0x4d356b */

/* 0x4d5ef7: computes the actor's colour remap: each AKPL entry's RGBS
 * colour, scaled by r/g/b (256 = unchanged; the talker passes 255), matched
 * against `rgbx` (256 x R,G,B,x, e.g. from gfx_palette_get) over entries
 * 11..175. Entries 0..15 are left alone when the actor has a shadow mode. */
void akos_build_remap(int actor, const uint8_t *rgbx, int r, int g, int b);
/* 0x4d5d9d: closest entry in 11..175 of `rgbx` by 30/60/20-weighted
 * squared distance on 6-bit components; an exact 6-bit match wins at once. */
int akos_closest_color(const uint8_t *rgbx, int r, int g, int b);

/* 0x4d75d2 / 0x4d75f2 / 0x4d7610 (the last has no callers). */
int32_t akos_get_var(int actor, int var);
void akos_set_var(int actor, int var, int32_t value);
void akos_clear_vars(int actor);

/* 0x4d66ba: appends to the command queue (port: dropped when full; the
 * original has no bound). */
void akos_queue_command(int cmd, int actor, int a, int b);
/* 0x4d764e (no callers in ALIFE.EXE): runs the queued AKOS_QC_START_ANIM
 * commands with the target actor's current direction and empties the
 * queue. Every other command is ignored. */
void akos_process_queue(void);
/* Port-only: the queue, for tests. */
int akos_queue_length(void);
const AkosQueued *akos_queue_entry(int i);

/* Port-only: seed of the game RNG (0x670e84) used by opcode 0xc082. */
void akos_set_random_seed(uint32_t seed);

#endif
