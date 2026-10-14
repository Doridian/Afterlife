/*
 * The AKOS costume engine. See akos.h and docs/formats/akos.md.
 *
 * Decompiled from ALIFE.EXE: costume slots 0x4d29b0-0x4d2c2c, the AKOS
 * module 0x4d5c35-0x4d7a01, and the codec 1 renderer 0x4dbee0-0x4de0cf.
 * The code is a cut-down build of LucasArts' SCUMM v7 AKOS engine, so
 * ScummVM's engines/scumm/akos.cpp (GPL) was used as a reference for names
 * and intent; the behaviour follows the ALIFE.EXE disassembly where they
 * differ.
 *
 * Differences from the original, all where it would read or write out of
 * bounds: every AKSQ / AKCH / AKCI read is checked, sequence walks are
 * bounded, and the renderer clips each pixel against the surface (the
 * original clips only one side horizontally and never checks y < 0). Cel
 * pixels are otherwise placed exactly as the original places them.
 */
#include "gfx/akos.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "platform/platform.h"

/* --- state ------------------------------------------------------------- */

typedef struct {
    uint8_t *data; /* owned resource */
    AkosFile file;
} Costume;

static Costume g_costumes[AKOS_MAX_COSTUMES]; /* 0x54eca0 (pointers) */
static AkosActor g_actors[AKOS_MAX_ACTORS];
static AkosQueued g_queue[AKOS_QUEUE_SIZE]; /* 0x54fff8 */
static int g_queue_len;                     /* 0x674c8c */

/* 0x54f7a8 / 0x54fbb0 / 0x54f390: i^2 * 30 / 60 / 20 (built by 0x4d5d26). */
static int32_t g_dist_r[256], g_dist_g[256], g_dist_b[256];

static uint32_t g_rand_seed = 1; /* 0x670e84, the game's shared RNG */

/* Per-draw globals of 0x4d76dc / 0x4d6713 / 0x4dd6bb. */
static uint8_t g_palette[256]; /* 0x64c310: AKPL entry -> colour for this draw */
static int g_mirror;           /* 0x64bac0: 1 = cels face right (x increases) */
static int g_move_x, g_move_y; /* 0x5500a4 / 0x5500ac */

/* 0x675df8: the SCUMM "big costume scale table". Entry i is i & 0xff with
 * its bits reversed, except that 255 and 511 hold 0xfe (767 holds 0xff). */
static uint8_t g_scale_table[768];

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static Costume *costume_of(int slot)
{
    return slot > 0 && slot < AKOS_MAX_COSTUMES && g_costumes[slot].data ? &g_costumes[slot] : NULL;
}

AkosActor *akos_actor(int actor) { return actor > 0 && actor < AKOS_MAX_ACTORS ? &g_actors[actor] : NULL; }

static const AkosFile *actor_costume(int actor)
{
    AkosActor *a = akos_actor(actor);
    return a ? akos_costume(a->costume) : NULL;
}

void akos_set_random_seed(uint32_t seed) { g_rand_seed = seed; }

/* 0x4b1202: MSVC rand(). */
static int game_rand(void)
{
    g_rand_seed = g_rand_seed * 0x343fdu + 0x269ec3u;
    return (int)((g_rand_seed >> 16) & 0x7fff);
}

/* --- costume slots (0x4d29b0-0x4d2c2c) ----------------------------------- */

/* 0x4d5d26 */
static void build_distance_tables(void)
{
    for (int i = 0; i < 256; i++) {
        g_dist_r[i] = i * i * 30;
        g_dist_g[i] = i * i * 60;
        g_dist_b[i] = i * i * 20;
    }
}

static void build_scale_table(void)
{
    for (int i = 0; i < 768; i++) {
        unsigned v = (unsigned)i & 0xff, r = 0;
        for (int b = 0; b < 8; b++)
            r |= ((v >> b) & 1u) << (7 - b);
        g_scale_table[i] = (uint8_t)r;
    }
    g_scale_table[255] = 0xfe;
    g_scale_table[511] = 0xfe;
}

/* 0x4d29b0 */
void akos_initialize(void)
{
    akos_shutdown();
    build_distance_tables();
    build_scale_table();
    memset(g_actors, 0, sizeof g_actors); /* .bss in the original */
    g_queue_len = 0;
}

void akos_shutdown(void)
{
    for (int i = 0; i < AKOS_MAX_COSTUMES; i++)
        akos_costume_free(i);
}

/* 0x4d2be0: first free slot in 1..31, 0 if none. */
static int costume_alloc(void)
{
    for (int i = 1; i < AKOS_MAX_COSTUMES; i++)
        if (!g_costumes[i].data)
            return i;
    return 0;
}

/* 0x4d2a29. The original stores the pointer without looking at it; the
 * port parses it here. */
int akos_costume_add(uint8_t *data, size_t len)
{
    int slot = costume_alloc();
    if (!slot || akos_open(&g_costumes[slot].file, data, len) < 0) {
        free(data);
        return 0;
    }
    g_costumes[slot].data = data;
    return slot;
}

/* 0x4d29ea: rc_load_resource (0x4cdcbf) + 0x4d2a29. */
int akos_costume_load(uint32_t res_id)
{
    long size = rc_resource_size(res_id);
    PlatFile *f = rc_open_resource(res_id);
    if (!f || size <= 0) {
        if (f)
            plat_file_close(f);
        return 0;
    }
    uint8_t *buf = malloc((size_t)size);
    if (buf && plat_file_read(f, buf, 1, (size_t)size) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    return buf ? akos_costume_add(buf, (size_t)size) : 0;
}

/* 0x4d2a6f. The original accepts slot 32 (one past the table). */
int akos_costume_free(int slot)
{
    if (slot <= 0 || slot >= AKOS_MAX_COSTUMES)
        return -1;
    free(g_costumes[slot].data);
    memset(&g_costumes[slot], 0, sizeof g_costumes[slot]);
    return 0;
}

/* 0x4d2c2c */
const AkosFile *akos_costume(int slot)
{
    Costume *c = costume_of(slot);
    return c ? &c->file : NULL;
}

void akos_set_costume(int actor, int slot)
{
    AkosActor *a = akos_actor(actor);
    if (a)
        a->costume = (uint16_t)slot;
}

/* --- palette (0x4d5c35-0x4d5ef7) ----------------------------------------- */

/* 0x4d5d9d */
int akos_closest_color(const uint8_t *rgbx, int r, int g, int b)
{
    r = r > 255 ? 255 : r < 0 ? 0 : r;
    g = g > 255 ? 255 : g < 0 ? 0 : g;
    b = b > 255 ? 255 : b < 0 ? 0 : b;
    int best = 11;
    int32_t best_d = INT32_MAX;
    for (int i = 11; i < 0xb0; i++) {
        const uint8_t *e = rgbx + i * 4;
        int dr = (r & 0xfc) - (e[0] & 0xfc), dg = (g & 0xfc) - (e[1] & 0xfc), db = (b & 0xfc) - (e[2] & 0xfc);
        if (dr == 0 && dg == 0 && db == 0)
            return i;
        int32_t d = g_dist_b[abs(db)] + g_dist_g[abs(dg)] + g_dist_r[abs(dr)];
        if (d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* The original's (x * f) / 256 rounding toward zero. */
static int scale_256(int x, int f)
{
    if (f == 0x100)
        return x;
    int p = x * f;
    return (p + (p < 0 ? 0xff : 0)) >> 8;
}

/* 0x4d5ef7. The last argument of the original (always 0) is unused. */
void akos_build_remap(int actor, const uint8_t *rgbx, int r, int g, int b)
{
    AkosActor *a = akos_actor(actor);
    const AkosFile *f = actor_costume(actor);
    if (!a || !f || !rgbx)
        return;
    uint32_t n = f->akpl.len > 256 ? 256 : f->akpl.len;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t entry = f->akpl.data[i];
        const uint8_t *c = f->rgbs.data + i * 3;
        if (a->shadow_mode == 0 || entry > 15)
            a->remap[i] = (uint8_t)akos_closest_color(rgbx, scale_256(c[0], r), scale_256(c[1], g), scale_256(c[2], b));
    }
}

/* 0x4d5c35: the colours of this draw. Remap entries 0xff fall back to the
 * AKPL value. (0x4d5c06, which would install them as a palette, is an empty
 * stub.) */
static void setup_palette(const AkosActor *a, const AkosFile *f)
{
    uint32_t n = f->akpl.len > 256 ? 256 : f->akpl.len;
    for (uint32_t i = 0; i < n; i++)
        g_palette[i] = a->remap[i] != 0xff ? a->remap[i] : f->akpl.data[i];
    if (f->akpl.len == 256 && a->remap[0] == 0xff)
        g_palette[0] = 0xff;
}

/* --- animation records (0x4d6080-0x4d6434) ------------------------------- */

/* 0x4d6360 */
void akos_reset(int actor)
{
    AkosActor *a = akos_actor(actor);
    if (!a)
        return;
    a->stopped = 0;
    for (int l = 0; l < AKOS_LIMBS; l++) {
        a->active[l] = AKOS_LIMB_OFF;
        a->curpos[l] = a->start[l] = a->end[l] = a->frame[l] = 0xffff;
    }
}

/* 0x4d6080: decodes AKCH record `index` for the limbs selected by
 * `usemask` (bit 15 = limb 0). Limbs the record defines but usemask
 * doesn't select are skipped over. */
static void start_anim(int actor, int index, uint32_t usemask)
{
    AkosActor *a = akos_actor(actor);
    const AkosFile *f = actor_costume(actor);
    if (!a || !f || index < 0)
        return;
    size_t avail;
    const uint8_t *r = akos_anim_record(f, (unsigned)index, &avail);
    if (!r)
        return;
    uint32_t mask = rd16(r);
    size_t p = 2;
    for (int l = 0; l < AKOS_LIMBS && (mask & 0xffff); l++, mask <<= 1, usemask <<= 1) {
        if (!(mask & 0x8000))
            continue;
        if (p >= avail)
            return;
        uint8_t code = r[p++];
        bool has_range = code != AKOS_LIMB_DISABLE && code != AKOS_LIMB_STOP && code != AKOS_LIMB_START;
        if (has_range && p + 4 > avail)
            return;
        if (usemask & 0x8000) {
            if (code == AKOS_LIMB_STOP) {
                a->stopped |= 1u << l;
            } else if (code == AKOS_LIMB_START) {
                a->stopped &= ~(1u << l);
            } else {
                uint32_t start = 0, len = 0;
                if (code == AKOS_LIMB_DISABLE) {
                    code = AKOS_LIMB_OFF;
                } else {
                    start = rd16(r + p);
                    len = rd16(r + p + 2);
                }
                a->active[l] = code;
                a->frame[l] = (uint32_t)index;
                a->end[l] = start + len;
                a->start[l] = a->curpos[l] = start;
            }
        }
        if (has_range)
            p += 4;
    }
}

/* 0x4d62a5 */
void akos_set_direction(int actor, int dir)
{
    AkosActor *a = akos_actor(actor);
    if (!a || a->direction == dir)
        return;
    uint32_t bit = 0x8000;
    for (int l = 0; l < AKOS_LIMBS; l++, bit >>= 1) {
        uint32_t fr = a->frame[l];
        if (fr != 0xffff && (fr & 3) != (uint32_t)dir)
            start_anim(actor, (int)((fr & 0xfc) | (uint32_t)dir), bit);
    }
    a->direction = dir;
}

/* 0x4d6434 */
void akos_set_anim(int actor, int anim, int dir)
{
    AkosActor *a = akos_actor(actor);
    if (!a)
        return;
    if (anim >= AKOS_ANIM_ALIAS_FIRST && anim < AKOS_ANIM_ALIAS_FIRST + 5)
        anim = a->alias[anim - AKOS_ANIM_ALIAS_FIRST];
    if (a->costume != 0) {
        a->needs_redraw = 1;
        a->counter = 0;
        if (anim == a->alias[0]) /* the init animation */
            akos_reset(actor);
        if (anim != AKOS_ANIM_TURN) {
            start_anim(actor, anim * 4 + dir, 0xffff);
            a->last_anim = (uint8_t)anim;
        }
        if (a->direction != dir)
            akos_set_direction(actor, dir);
    }
    a->direction = dir;
}

/* --- variables and the command queue (0x4d66ba, 0x4d75d2-0x4d764e) ------- */

int32_t akos_get_var(int actor, int var)
{
    AkosActor *a = akos_actor(actor);
    return a && var >= 0 && var < AKOS_VARS ? a->vars[var] : 0;
}

void akos_set_var(int actor, int var, int32_t value)
{
    AkosActor *a = akos_actor(actor);
    if (a && var >= 0 && var < AKOS_VARS)
        a->vars[var] = value;
}

void akos_clear_vars(int actor)
{
    for (int i = 0; i < AKOS_VARS; i++)
        akos_set_var(actor, i, 0);
}

void akos_queue_command(int cmd, int actor, int a, int b)
{
    if (g_queue_len >= AKOS_QUEUE_SIZE)
        return;
    g_queue[g_queue_len++] = (AkosQueued){cmd, actor, a, b};
}

void akos_process_queue(void)
{
    for (int i = 0; i < g_queue_len; i++) {
        const AkosQueued *q = &g_queue[i];
        if (q->cmd == AKOS_QC_START_ANIM) {
            AkosActor *t = akos_actor(q->actor);
            if (t)
                akos_set_anim(q->actor, q->a, t->direction);
        }
    }
    g_queue_len = 0;
}

int akos_queue_length(void) { return g_queue_len; }
const AkosQueued *akos_queue_entry(int i) { return i >= 0 && i < g_queue_len ? &g_queue[i] : NULL; }

/* --- the sequence interpreter (0x4d6580, 0x4d6b14, 0x4d78c6) ------------- */

/* AKSQ opcodes (ScummVM's names). */
enum {
    AKC_RETURN = 0xc001,
    AKC_SET_VAR = 0xc010,
    AKC_CMD_QUE3 = 0xc015,
    AKC_COMPLEX_CHAN = 0xc020,
    AKC_JUMP = 0xc030,
    AKC_JUMP_IF_SET = 0xc031,
    AKC_ADD_VAR = 0xc040,
    AKC_IGNORE = 0xc050,
    AKC_INC_VAR = 0xc060,
    AKC_CMD_QUE3_QUICK = 0xc061,
    AKC_JUMP_E = 0xc070, /* ..0xc075: E NE L LE G GE */
    AKC_START_ANIM = 0xc080,
    AKC_START_VAR_ANIM = 0xc081,
    AKC_RANDOM = 0xc082,
    AKC_SET_ACTOR_CLIP = 0xc083,
    AKC_START_ANIM_IN_ACTOR = 0xc084,
    AKC_SET_VAR_IN_ACTOR = 0xc085,
    AKC_HIDE_ACTOR = 0xc086,
    AKC_SET_DRAW_OFFS = 0xc087,
    AKC_JUMP_TABLE = 0xc088,
    AKC_SOUND_STUFF = 0xc089,
    AKC_FLIP = 0xc08a,
    AKC_CMD3 = 0xc08b,
    AKC_IGNORE3 = 0xc08c,
    AKC_IGNORE2 = 0xc08d,
    AKC_SKIP_E = 0xc090, /* ..0xc095 */
    AKC_CLEAR_FLAG = 0xc09f,
};

typedef struct {
    const uint8_t *p;
    uint32_t len;
} Seq;

static unsigned seq_byte(const Seq *s, uint32_t pos) { return pos < s->len ? s->p[pos] : 0; }
static int seq_word(const Seq *s, uint32_t pos) { return (int16_t)(seq_byte(s, pos) | seq_byte(s, pos + 1) << 8); }
static uint32_t seq_uword(const Seq *s, uint32_t pos)
{
    return (uint32_t)(seq_byte(s, pos) | seq_byte(s, pos + 1) << 8);
}

/* A code is one byte, or two (big-endian) when the first has bit 7 set. */
static unsigned seq_code(const Seq *s, uint32_t pos)
{
    unsigned b = seq_byte(s, pos);
    return b & 0x80 ? b << 8 | seq_byte(s, pos + 1) : b;
}

/* Length of the AKOS_LIMB_CODE instruction at pos (the type 6 case of
 * 0x4d6b14). Cels and unknown codes are 1 or 2 bytes. */
static uint32_t instr_len(const Seq *s, uint32_t pos, unsigned code)
{
    switch (code) {
    case AKC_RETURN:
    case AKC_INC_VAR:
    case AKC_CMD_QUE3_QUICK:
    case AKC_HIDE_ACTOR:
    case AKC_CLEAR_FLAG:
        return 2;
    case AKC_CMD_QUE3:
    case AKC_IGNORE:
    case AKC_START_ANIM:
    case AKC_START_VAR_ANIM:
    case AKC_SET_ACTOR_CLIP:
    case AKC_JUMP_TABLE:
    case AKC_IGNORE3:
    case AKC_IGNORE2:
        return 3;
    case AKC_JUMP:
    case AKC_START_ANIM_IN_ACTOR:
    case AKC_FLIP:
        return 4;
    case AKC_SET_VAR:
    case AKC_JUMP_IF_SET:
    case AKC_ADD_VAR:
    case AKC_SKIP_E:
    case AKC_SKIP_E + 1:
    case AKC_SKIP_E + 2:
    case AKC_SKIP_E + 3:
    case AKC_SKIP_E + 4:
    case AKC_SKIP_E + 5:
        return 5;
    case AKC_SET_VAR_IN_ACTOR:
    case AKC_SET_DRAW_OFFS:
    case AKC_CMD3:
        return 6;
    case AKC_JUMP_E:
    case AKC_JUMP_E + 1:
    case AKC_JUMP_E + 2:
    case AKC_JUMP_E + 3:
    case AKC_JUMP_E + 4:
    case AKC_JUMP_E + 5:
    case AKC_RANDOM:
        return 7;
    case AKC_SOUND_STUFF:
        return 8;
    case AKC_COMPLEX_CHAN: {
        uint32_t n = seq_byte(s, pos + 2), q = pos + 3;
        while (n--)
            q += seq_byte(s, q + 4) & 0x80 ? 6 : 5;
        return q - pos;
    }
    default:
        return code & 0x8000 ? 2 : 1;
    }
}

/* 0x4d6580: condition `op` (0..5 = E NE L LE G GE) of a against b. */
static bool akos_compare(int a, int b, int op)
{
    switch (op) {
    case 0:
        return a == b;
    case 1:
        return a != b;
    case 2:
        return a < b;
    case 3:
        return a <= b;
    case 4:
        return a > b;
    case 5:
        return a >= b;
    default:
        return false;
    }
}

/* Upper bound on instructions run per step (port-only: the original loops
 * forever on a sequence that never reaches a cel). */
#define STEP_LIMIT 4096

/* 0x4d6b14: advances limb `limb` by one step. akfo is the optional cel goto
 * table. Returns true if the limb's position changed. */
static bool increase_anim(int actor, int limb, const Seq *sq, const AkosChunk *akfo)
{
    AkosActor *a = &g_actors[actor];
    unsigned type = a->active[limb];
    uint32_t old = a->curpos[limb], end = a->end[limb];
    uint32_t pos = old;
    bool skipping = false;

    for (int guard = 0; guard < STEP_LIMIT; guard++) {
        /* Step past the current code according to the limb type. */
        unsigned code = seq_code(sq, pos);
        uint32_t step = code & 0x8000 ? 2 : 1;
        switch (type) {
        case AKOS_LIMB_LOOP:
            pos += step;
            if (pos > end)
                pos = a->start[limb];
            break;
        case AKOS_LIMB_ONCE:
            if (pos != end)
                pos += step;
            break;
        case AKOS_LIMB_CODE:
            pos += instr_len(sq, pos, code);
            break;
        default: /* AKOS_LIMB_FROZEN and unknown types stay put */
            break;
        }
        if (pos >= sq->len) /* port-only: ran off the sequence */
            break;
        code = seq_code(sq, pos);
        if (skipping && code != AKC_CLEAR_FLAG)
            continue;

        switch (code) {
        case AKC_SET_VAR:
            akos_set_var(actor, (int)seq_byte(sq, pos + 4), seq_word(sq, pos + 2));
            continue;
        case AKC_CMD_QUE3: {
            unsigned slot = seq_byte(sq, pos + 2);
            if (slot != 0 && slot < 8)
                akos_queue_command(AKOS_QC_SOUND, actor, a->sounds[slot], 0);
            continue;
        }
        case AKC_JUMP:
            pos = seq_uword(sq, pos + 2);
            goto done;
        case AKC_JUMP_IF_SET: {
            int v = (int)seq_byte(sq, pos + 4);
            if (akos_get_var(actor, v) != 0) {
                akos_set_var(actor, v, 0);
                pos = seq_uword(sq, pos + 2);
                goto done;
            }
            continue;
        }
        case AKC_ADD_VAR: {
            int v = (int)seq_byte(sq, pos + 4);
            akos_set_var(actor, v, akos_get_var(actor, v) + seq_word(sq, pos + 2));
            continue;
        }
        case AKC_IGNORE:
        case AKC_CMD3:
        case AKC_IGNORE3:
        case AKC_IGNORE2:
            continue;
        case AKC_INC_VAR:
            akos_set_var(actor, 0, akos_get_var(actor, 0) + 1);
            continue;
        case AKC_CMD_QUE3_QUICK:
            akos_queue_command(AKOS_QC_SOUND, actor, a->sounds[1], 0);
            continue;
        case AKC_JUMP_E:
        case AKC_JUMP_E + 1:
        case AKC_JUMP_E + 2:
        case AKC_JUMP_E + 3:
        case AKC_JUMP_E + 4:
        case AKC_JUMP_E + 5:
            if (akos_compare(akos_get_var(actor, (int)seq_byte(sq, pos + 4)), seq_word(sq, pos + 5),
                             (int)(code - AKC_JUMP_E))) {
                pos = seq_uword(sq, pos + 2);
                goto done;
            }
            continue;
        case AKC_START_ANIM:
            akos_queue_command(AKOS_QC_START_ANIM, actor, (int)seq_byte(sq, pos + 2), 0);
            continue;
        case AKC_START_VAR_ANIM:
            akos_queue_command(AKOS_QC_START_ANIM, actor, akos_get_var(actor, (int)seq_byte(sq, pos + 2)), 0);
            continue;
        case AKC_RANDOM: {
            /* The original passes the value through a word in some game
             * structure (0x647a18 + 0x44), truncating it to 16 bits. */
            int lo = seq_word(sq, pos + 2), hi = seq_word(sq, pos + 4);
            int range = hi + 1 - lo;
            int v = lo + (range != 0 ? game_rand() % range : 0);
            akos_set_var(actor, (int)seq_byte(sq, pos + 6), (int16_t)v);
            continue;
        }
        case AKC_SET_ACTOR_CLIP:
            akos_queue_command(AKOS_QC_CLIP, actor, (int)seq_byte(sq, pos + 2), 0);
            continue;
        case AKC_START_ANIM_IN_ACTOR:
            akos_queue_command(AKOS_QC_START_ANIM, akos_get_var(actor, (int)seq_byte(sq, pos + 2)),
                               akos_get_var(actor, (int)seq_byte(sq, pos + 3)), 0);
            continue;
        case AKC_SET_VAR_IN_ACTOR:
            akos_set_var(akos_get_var(actor, (int)seq_byte(sq, pos + 2)), (int)seq_byte(sq, pos + 3),
                         seq_word(sq, pos + 4));
            continue;
        case AKC_HIDE_ACTOR:
            akos_queue_command(AKOS_QC_HIDE, actor, 0, 0);
            continue;
        case AKC_SET_DRAW_OFFS:
            akos_queue_command(AKOS_QC_DRAW_OFFS, actor, seq_word(sq, pos + 2), seq_word(sq, pos + 4));
            continue;
        case AKC_JUMP_TABLE: {
            /* "AKOS:NO CEL GOTO OFFSET TABLE": the original reports it
             * through an empty stub and then reads through NULL. */
            int i = akos_get_var(actor, (int)seq_byte(sq, pos + 2)) - 1;
            if (akfo->data && i >= 0 && (uint32_t)i * 2 + 2 <= akfo->len)
                pos = rd16(akfo->data + i * 2);
            goto done;
        }
        case AKC_SOUND_STUFF: {
            unsigned slot = seq_byte(sq, pos + 2), sub = seq_byte(sq, pos + 4), val = seq_byte(sq, pos + 6);
            /* "Illegal Cyst Stub %d command %d" / "Invalid Cyst Stub actor
             * sound slot %d" go to empty stubs. */
            if (slot < 8 && sub >= 1 && sub <= 3)
                akos_queue_command(AKOS_QC_SOUND_VOL + (int)sub - 1, actor, a->sounds[slot], (int)val);
            continue;
        }
        case AKC_FLIP:
            a->flip = seq_word(sq, pos + 2);
            continue;
        case AKC_SKIP_E:
        case AKC_SKIP_E + 1:
        case AKC_SKIP_E + 2:
        case AKC_SKIP_E + 3:
        case AKC_SKIP_E + 4:
        case AKC_SKIP_E + 5:
            if (!akos_compare(akos_get_var(actor, (int)seq_byte(sq, pos + 4)), seq_word(sq, pos + 2),
                              (int)(code - AKC_SKIP_E)))
                skipping = true;
            continue;
        case AKC_CLEAR_FLAG:
            skipping = false;
            continue;
        default:
            /* AKC_RETURN, AKC_COMPLEX_CHAN, cels; other 0xc0xx codes are
             * "UNDEFINED uSweat TOKEN 0x%04x" (empty stub) and stop too. */
            goto done;
        }
    }
    pos = old; /* port-only: no cel reached, stay */
done:
    a->curpos[limb] = pos;
    return pos != old;
}

/* 0x4d78c6 */
bool akos_increase_anims(int actor)
{
    AkosActor *a = akos_actor(actor);
    const AkosFile *f = actor_costume(actor);
    if (!a)
        return false;
    if (++a->counter < a->speed)
        return false;
    a->counter = 0;
    if (!f)
        return false;
    Seq sq = {f->aksq.data, f->aksq.len};
    bool moved = false;
    for (int l = 0; l < AKOS_LIMBS; l++)
        if (a->active[l] != AKOS_LIMB_OFF)
            moved |= increase_anim(actor, l, &sq, &f->akfo);
    return moved;
}

/* --- codec 1 (0x4dd6bb and its loops 0x4dbee0-0x4dd47c) ----------------- */

typedef struct {
    const uint8_t *p, *end;
    int shift;
    unsigned mask;
    uint32_t run;
    unsigned color;
} RleReader;

static unsigned rle_next(RleReader *r)
{
    if (r->run == 0) {
        if (r->p >= r->end) {
            r->color = 0; /* port-only: out of data draws nothing */
            r->run = UINT32_MAX;
        } else {
            unsigned b = *r->p++;
            r->color = b >> r->shift;
            r->run = b & r->mask;
            if (r->run == 0)
                r->run = r->p < r->end ? *r->p++ : 0;
            if (r->run == 0)
                r->run = UINT32_MAX;
        }
    }
    r->run--;
    return r->color;
}

static void put_pixel(GfxSurface *s, int x, int y, unsigned color)
{
    if (color != 0 && x >= 0 && x < s->width && y >= 0 && y < s->height)
        s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] = g_palette[color];
}

static void extend_bounds(AkosActor *a, int left, int right, int top, int bottom)
{
    if (left < a->bounds.left)
        a->bounds.left = left;
    if (right > a->bounds.right)
        a->bounds.right = right;
    if (top < a->bounds.top)
        a->bounds.top = top;
    if (bottom > a->bounds.bottom)
        a->bounds.bottom = bottom;
}

static void track_rows(AkosActor *a, const GfxSurface *s, int top, int bottom)
{
    if (top < 0 || top > s->height)
        top = 0;
    if (bottom > s->height)
        bottom = s->height;
    if (top < a->top)
        a->top = top;
    if (bottom > a->bottom)
        a->bottom = bottom;
}

/* 0x4dd6bb, unscaled (0x4dc09c / 0x4dbf7c; the masked variants are
 * unreachable: 0x4d5c11 and 0x4d5c23 always return 0 and the mask selector
 * 0x64c410 is never set). Columns go right from x when mirrored, left from
 * x otherwise; ox is negated in the latter case. */
static void codec1_draw(AkosActor *a, GfxSurface *s, const AkosFile *f, const AkosCel *cel, int ox, int oy, int x,
                        int y, int sx, int sy)
{
    int w = cel->width, h = cel->height;
    RleReader r = {cel->data, cel->data + cel->avail, akos_rle_shift(f), 0, 0, 0};
    r.mask = (1u << r.shift) - 1;
    int dir = g_mirror ? 1 : -1;

    if (sx == 0xff && sy == 0xff) {
        if (!g_mirror)
            ox = -ox;
        x += ox;
        y += oy;
        int left = g_mirror ? x : x - w, right = g_mirror ? x + w : x;
        extend_bounds(a, left, right + 1, y, y + h + 1);
        if (y >= s->height || y + h <= 0 || left >= s->width || right <= 0)
            return;
        track_rows(a, s, y, y + h);
        for (int c = 0; c < w; c++) {
            int dx = x + dir * c;
            for (int row = 0; row < h; row++)
                put_pixel(s, dx, y + row, rle_next(&r));
        }
        return;
    }

    /* Scaled (0x4dc7d5 / 0x4dc639). Never used by Afterlife (the talker
     * always passes 0xff, 0xff). A source column or row is kept when its
     * scale table entry is below the scale; the offset is scaled the same
     * way. The original's column skipping at the clip edges is replaced by
     * per-pixel clipping. */
    int xstep = -1;
    if (ox < 0) {
        ox = -ox;
        xstep = 1;
    }
    int xi; /* scale table index of the first column */
    if (!g_mirror) {
        xi = ox + 0x180;
        for (int i = 0, t = xi; i < ox; i++)
            if (g_scale_table[t--] < sx)
                x += xstep;
    } else {
        xi = 0x180 - ox;
        for (int i = 0, t = xi; i < ox; i++)
            if (g_scale_table[t++] < sx)
                x -= xstep;
    }
    int span = x;
    for (int i = 0, t = xi; i < w; i++, t += dir)
        if (g_scale_table[t] < sx)
            span += dir;
    int left = g_mirror ? x : span, right = g_mirror ? span : x;

    int ystep = -1;
    if (oy < 0) {
        oy = -oy;
        ystep = 1;
    }
    int yi = 0x180 - oy;
    for (int i = 0, t = yi; i < oy; i++)
        if (g_scale_table[t++] < sy)
            y -= ystep;
    int bottom = y;
    for (int i = 0, t = yi; i < h; i++)
        if (g_scale_table[t++] < sy)
            bottom++;
    extend_bounds(a, left, right + 1, y, bottom + 1);
    if (y >= s->height || bottom <= 0 || left >= s->width || right <= 0)
        return;
    track_rows(a, s, y, bottom);

    int dx = x;
    for (int c = 0, t = xi; c < w; c++, t += dir) {
        int dy = y;
        for (int row = 0; row < h; row++) {
            unsigned color = rle_next(&r);
            if (g_scale_table[yi + row] < sy) {
                put_pixel(s, dx, dy, color);
                dy++;
            }
        }
        if (g_scale_table[t] < sx)
            dx += dir;
    }
}

/* --- drawing (0x4d6713, 0x4d76dc) ---------------------------------------- */

static void draw_cel(AkosActor *a, GfxSurface *s, const AkosFile *f, const AkosCel *cel, int ox, int oy, int x, int y,
                     int sx, int sy)
{
    switch (f->codec) {
    case AKOS_CODEC_RLE:
        codec1_draw(a, s, f, cel, ox, oy, x, y, sx, sy);
        break;
    case AKOS_CODEC_BOMP:
        /* 0x4d7a01 is an empty function: BOMP costumes draw nothing. */
        break;
    default:
        /* "a)/b) UNDEFINED compression type 0x%04x?" (empty stub) */
        break;
    }
}

/* 0x4d6713 */
static void draw_limb(int actor, int limb, GfxSurface *s, const AkosFile *f, int x, int y, int sx, int sy)
{
    AkosActor *a = &g_actors[actor];
    if (a->active[limb] == AKOS_LIMB_OFF || (a->stopped & (1u << limb)))
        return;
    Seq sq = {f->aksq.data, f->aksq.len};
    uint32_t pos = a->curpos[limb];
    unsigned code = seq_code(&sq, pos);
    AkosCel cel;
    if (code == AKC_RETURN)
        return;
    if (code == AKC_COMPLEX_CHAN) {
        /* count, then per cel: s16 x, s16 y, cel (1 or 2 bytes, & 0xfff).
         * Only width/height come from AKCI; the move accumulators are not
         * advanced. */
        unsigned n = seq_byte(&sq, pos + 2);
        uint32_t q = pos + 3;
        for (unsigned i = 0; i < n; i++) {
            unsigned b = seq_byte(&sq, q + 4), c = b;
            if (b & 0x80)
                c = (b << 8 | seq_byte(&sq, q + 5)) & 0xfff;
            int cx = seq_word(&sq, q), cy = seq_word(&sq, q + 2);
            q += b & 0x80 ? 6 : 5;
            if (akos_get_cel(f, c, &cel) == AKOS_OK)
                draw_cel(a, s, f, &cel, cx + g_move_x, cy + g_move_y, x, y, sx, sy);
        }
        return;
    }
    if (akos_get_cel(f, code & 0xfff, &cel) != AKOS_OK)
        return;
    int ox = cel.rel_x + g_move_x, oy = cel.rel_y + g_move_y;
    /* quirk: the original subtracts move_y from the x accumulator, not y */
    g_move_x += cel.move_x - cel.move_y;
    draw_cel(a, s, f, &cel, ox, oy, x, y, sx, sy);
}

/* 0x4d76dc */
void akos_draw_actor(int actor, GfxSurface *dst, int x, int y, int scale_x, int scale_y)
{
    AkosActor *a = akos_actor(actor);
    const AkosFile *f = actor_costume(actor);
    if (!a || !f)
        return;
    GfxSurface *s = gfx_target(dst);
    if (!s || !s->pixels)
        return;
    setup_palette(a, f);
    g_move_x = g_move_y = 0;
    a->top = INT_MAX;
    a->bottom = 0;
    g_mirror = (a->direction != 0 || (f->flags & AKOS_HDR_MIRROR)) ? 1 : 0;
    if (a->flip)
        g_mirror = !g_mirror;
    for (int l = 0; l < AKOS_LIMBS; l++)
        draw_limb(actor, l, s, f, x, y, scale_x, scale_y);
    /* Port-only: mark the drawn area dirty when drawing to the screen. */
    if (a->bounds.left < a->bounds.right && a->bounds.top < a->bounds.bottom)
        gfx_touch(s, a->bounds.left, a->bounds.right - 1, a->bounds.top, a->bounds.bottom - 1);
}

/* 0x4d3508 */
void akos_reset_bounds(int actor)
{
    AkosActor *a = akos_actor(actor);
    if (a)
        a->bounds = (GfxRect){0x7fff, 0, 0x7fff, 0};
}

/* 0x4d356b */
GfxRect akos_get_bounds(int actor)
{
    AkosActor *a = akos_actor(actor);
    return a ? a->bounds : (GfxRect){0, 0, 0, 0};
}
