/*
 * Palette management. See palette.h.
 */
#include "gfx/palette.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/surface.h"
#include "platform/platform.h"

typedef struct {
    uint8_t r, g, b, flags;
} PalEntry;

/* 0x545c38: the original stores PALETTEENTRY-order bytes (B, G, R, flags in
 * memory, as an RGBQUAD) so it can hand +10 entries straight to
 * SetDIBColorTable. */
static PalEntry g_pal[256];

/* Squared-difference weights, 0x545838 / 0x5468c0 / 0x545430 (built in
 * di_initialize) and the identical copies 0x5475c8 / 0x5481a0 / 0x5471c8
 * (pal_init_distance_tables, 0x4cf22b). */
static int32_t g_dist_r[256], g_dist_g[256], g_dist_b[256];

static uint8_t g_shade[GFX_SHADE_TABLES][256]; /* 0x64cf10 */
static uint8_t g_highlight[256];               /* 0x535318 */

typedef struct {
    uint32_t first;  /* +0 */
    uint32_t count;  /* +4: 0 = unused */
    int32_t rate;    /* +8: ticks per step, sign = direction */
    uint32_t counter;/* +c */
    uint32_t enabled;/* +10 */
} CycleRange;

static CycleRange g_cycles[GFX_CYCLE_RANGES]; /* 0x5479c8 */
static int g_cycle_running = 1;               /* 0x673a00 */
static uint32_t g_cycle_last;                 /* 0x5471c0 */

/* The 20 static colors of the Windows 256-color system palette (an OS
 * constant, not game data). */
static const uint8_t k_static_lo[10][3] = {
    {0, 0, 0},     {128, 0, 0},   {0, 128, 0},   {128, 128, 0},   {0, 0, 128},
    {128, 0, 128}, {0, 128, 128}, {192, 192, 192}, {192, 220, 192}, {166, 202, 240},
};
static const uint8_t k_static_hi[10][3] = {
    {255, 251, 240}, {160, 160, 164}, {128, 128, 128}, {255, 0, 0},   {0, 255, 0},
    {255, 255, 0},   {0, 0, 255},     {255, 0, 255},   {0, 255, 255}, {255, 255, 255},
};

static int iabs(int v) { return v < 0 ? -v : v; }

/* Pushes shadow entries first..last to the screen (AnimatePalette +
 * RealizePalette in the original). */
static void push(unsigned first, unsigned last)
{
    PlatColor c[256];
    if (last < first || last > 255)
        return;
    for (unsigned i = first; i <= last; i++)
        c[i - first] = (PlatColor){g_pal[i].r, g_pal[i].g, g_pal[i].b};
    plat_video_set_palette(c, (int)first, (int)(last - first + 1));
    gfx_invalidate(NULL);
}

/* 0x4cb67a, 0x4cf22b and the tail of 0x4ca1c0 */
void gfx_palette_init(void)
{
    /* GetSystemPaletteEntries fills the statics; the game range starts as
     * an identity ramp (i, i, i), PC_RESERVED. */
    for (unsigned i = 0; i < 10; i++) {
        g_pal[i] = (PalEntry){k_static_lo[i][0], k_static_lo[i][1], k_static_lo[i][2], 0};
        g_pal[246 + i] = (PalEntry){k_static_hi[i][0], k_static_hi[i][1], k_static_hi[i][2], 0};
    }
    for (unsigned i = GFX_PAL_FIRST; i <= GFX_PAL_LAST; i++)
        g_pal[i] = (PalEntry){(uint8_t)i, (uint8_t)i, (uint8_t)i, GFX_PALF_RESERVED};
    for (int i = 0; i < 256; i++) {
        g_dist_r[i] = i * i * 30;
        g_dist_g[i] = i * i * 60;
        g_dist_b[i] = i * i * 20;
    }
    push(0, 255);
}

/* 0x4cacc7 */
void gfx_palette_set(const uint8_t *rgbx, unsigned first, unsigned count)
{
    if (count == 0)
        return;
    unsigned last = first + count - 1;
    if (first < GFX_PAL_FIRST)
        first = GFX_PAL_FIRST;
    if (last > GFX_PAL_LAST)
        last = GFX_PAL_LAST;
    if (rgbx) {
        for (unsigned i = first; i <= last; i++)
            g_pal[i] = (PalEntry){rgbx[i * 4], rgbx[i * 4 + 1], rgbx[i * 4 + 2], GFX_PALF_RESERVED};
    }
    push(first, last);
}

/* 0x4caece */
void gfx_palette_get(uint8_t *rgbx, unsigned first, unsigned count)
{
    if (!rgbx || count == 0)
        return;
    unsigned last = first + count - 1;
    if (last > 255)
        last = 255;
    for (unsigned i = first; i <= last; i++) {
        rgbx[i * 4] = g_pal[i].r;
        rgbx[i * 4 + 1] = g_pal[i].g;
        rgbx[i * 4 + 2] = g_pal[i].b;
    }
}

uint8_t gfx_palette_flags(unsigned index) { return index < 256 ? g_pal[index].flags : 0; }

/* 0x4cb433. Searches 10..244: the loop bound is < 0xf5, so entry 245 is
 * never matched. */
uint8_t gfx_palette_closest(uint8_t r, uint8_t g, uint8_t b)
{
    int32_t best_dist = 0x8000000;
    unsigned best = GFX_PAL_FIRST; /* uninitialized in the original */
    for (unsigned i = GFX_PAL_FIRST; i < 0xf5; i++) {
        const PalEntry *e = &g_pal[i];
        if (e->flags & GFX_PALF_CYCLING)
            continue;
        if (e->r == r && e->g == g && e->b == b)
            return (uint8_t)i;
        int32_t d = g_dist_r[iabs(r - e->r)] + g_dist_b[iabs(b - e->b)] + g_dist_g[iabs(g - e->g)];
        if (d < best_dist) {
            best = i;
            best_dist = d;
        }
    }
    return (uint8_t)best;
}

/* 0x4cc3d4. The original's dir < 0 branch starts at entry 0 instead of
 * `first` (it rotates 0..count-1 of the shadow palette, then pushes the
 * untouched first..first+count-1, so it visibly does nothing). The port
 * rotates the range it was asked to. */
void gfx_palette_rotate(unsigned first, unsigned count, int dir)
{
    if (count == 0 || first + count > 256)
        return;
    PalEntry *p = &g_pal[first];
    if (dir < 0) {
        PalEntry t = p[0];
        memmove(p, p + 1, (count - 1) * sizeof(*p));
        p[count - 1] = t;
    } else {
        PalEntry t = p[count - 1];
        memmove(p + 1, p, (count - 1) * sizeof(*p));
        p[0] = t;
    }
    /* di_set_palette(NULL, ...): re-push without touching the flags. */
    unsigned last = first + count - 1;
    if (first < GFX_PAL_FIRST)
        first = GFX_PAL_FIRST;
    if (last > GFX_PAL_LAST)
        last = GFX_PAL_LAST;
    push(first, last);
}

/* 0x4cec6d */
uint8_t *gfx_palette_from_rgb(const uint8_t *rgb)
{
    uint8_t *out = malloc(256 * 4);
    if (!out)
        return NULL;
    for (unsigned i = 0; i < 256; i++) {
        out[i * 4] = rgb[i * 3];
        out[i * 4 + 1] = rgb[i * 3 + 1];
        out[i * 4 + 2] = rgb[i * 3 + 2];
        out[i * 4 + 3] = 0;
    }
    return out;
}

/* 0x4ced05. Cycling record (8 bytes): +2 speed, +6 first, +7 last; the
 * other bytes are unused here. Speed s becomes a period of 1024 / (17 s)
 * ticks (at least 1); s = 0 disables the range. */
uint8_t *gfx_palette_load_resource(const uint8_t *data, size_t len)
{
    if (!data || len < 256 * 4 + 15 * 8)
        return NULL;
    uint8_t *out = malloc(256 * 4);
    if (!out)
        return NULL;
    for (unsigned i = 0; i < 256; i++) {
        out[i * 4] = data[i * 4 + 2];
        out[i * 4 + 1] = data[i * 4 + 1];
        out[i * 4 + 2] = data[i * 4];
        out[i * 4 + 3] = 0;
    }
    const uint8_t *rec = data + 256 * 4;
    for (unsigned c = 0; c < 15; c++, rec += 8) {
        CycleRange *cr = &g_cycles[c];
        uint32_t rate = (uint32_t)rec[2] * 17;
        if (rate != 0) {
            rate = 0x400 / rate;
            if (rate < 2)
                rate = 1;
        }
        cr->rate = (int32_t)rate;
        cr->first = rec[6];
        cr->enabled = 1;
        if (rate == 0) {
            cr->count = 0;
        } else {
            cr->count = (uint32_t)rec[7] - cr->first + 1;
            for (unsigned i = cr->first; i <= rec[7]; i++)
                out[i * 4 + 3] |= GFX_PALF_CYCLING;
        }
        cr->counter = (uint32_t)iabs(cr->rate);
    }
    return out;
}

/* --- shade tables -------------------------------------------------------- */

/* 0x4cf2a2 */
uint8_t gfx_palette_closest_in_range(const uint8_t *rgbx, int r, int g, int b, uint8_t lo, uint8_t hi)
{
    if (r > 0xff)
        r = 0xff;
    if (g > 0xff)
        g = 0xff;
    if (b > 0xff)
        b = 0xff;
    r &= 0xfc;
    g &= 0xfc;
    b &= 0xfc;
    int32_t best_dist = 0x8000000;
    unsigned best = lo; /* uninitialized in the original */
    for (unsigned i = lo; i <= hi; i++) {
        int er = rgbx[i * 4] & 0xfc, eg = rgbx[i * 4 + 1] & 0xfc, eb = rgbx[i * 4 + 2] & 0xfc;
        if (er == r && eg == g && eb == b)
            return (uint8_t)i;
        int32_t d = g_dist_b[iabs(b - eb)] + g_dist_r[iabs(r - er)] + g_dist_g[iabs(g - eg)];
        if (d < best_dist) {
            best = i;
            best_dist = d;
        }
    }
    return (uint8_t)best;
}

/* 0x4ceee9 */
void gfx_palette_build_shade_table(unsigned table, const uint8_t *rgbx, int r, int g, int b, uint8_t lo, uint8_t hi,
                                   uint8_t match_lo, uint8_t match_hi)
{
    if (table >= GFX_SHADE_TABLES)
        return;
    uint8_t *t = g_shade[table];
    for (unsigned i = 0; i < 256; i++)
        t[i] = (uint8_t)i;
    for (unsigned i = lo; i <= hi; i++) {
        const uint8_t *e = rgbx + i * 4;
        int sr, sg, sb;
        if (e[0] < 5 && e[1] < 5 && e[2] < 5) {
            sr = r >> 2;
            sg = g >> 2;
            sb = b >> 2;
        } else {
            sr = e[0] * r / 256;
            sg = e[1] * g / 256;
            sb = e[2] * b / 256;
        }
        t[i] = gfx_palette_closest_in_range(rgbx, sr, sg, sb, match_lo, match_hi);
    }
}

const uint8_t *gfx_palette_shade_table(unsigned table)
{
    return table < GFX_SHADE_TABLES ? g_shade[table] : NULL;
}

/* 0x4c7d79 */
void gfx_palette_build_highlight(void)
{
    uint8_t pal[256 * 4];
    gfx_palette_get(pal, 0, 256);
    for (unsigned i = 0; i < 256; i++) {
        for (unsigned c = 0; c < 3; c++) {
            unsigned v = pal[i * 4 + c] / 5u + pal[i * 4 + c];
            pal[i * 4 + c] = (uint8_t)(v > 0xfe ? 0xff : v);
        }
    }
    for (unsigned i = 0; i < 256; i++)
        g_highlight[i] = gfx_palette_closest(pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2]);
}

const uint8_t *gfx_palette_highlight(void) { return g_highlight; }

/* --- palette cycling ------------------------------------------------------ */

/* 0x4ceba0. Also registers pcyc_idle as a system task. */
void gfx_cycle_init(void)
{
    memset(g_cycles, 0, sizeof(g_cycles));
}

/* 0x4cec02. The original accepts slot 100 (bound < 0x65), one past the
 * table. */
bool gfx_cycle_set(unsigned slot, unsigned first, unsigned count, int rate)
{
    if (slot >= GFX_CYCLE_RANGES)
        return false;
    g_cycles[slot].first = first;
    g_cycles[slot].count = count;
    g_cycles[slot].rate = rate;
    g_cycles[slot].counter = (uint32_t)iabs(rate);
    return true;
}

/* 0x4cf042 */
void gfx_cycle_stop_all(void)
{
    g_cycle_running = 0;
    for (unsigned i = 0; i < GFX_CYCLE_RANGES; i++)
        g_cycles[i].enabled = 0;
}

/* 0x4cf089 */
void gfx_cycle_start_all(void)
{
    g_cycle_running = 1;
    for (unsigned i = 0; i < GFX_CYCLE_RANGES; i++)
        g_cycles[i].enabled = 1;
}

/* 0x4cf0d0 */
void gfx_cycle_disable(unsigned slot)
{
    if (slot < GFX_CYCLE_RANGES)
        g_cycles[slot].enabled = 0;
}

/* 0x4cf0ec */
void gfx_cycle_enable(unsigned slot)
{
    if (slot < GFX_CYCLE_RANGES)
        g_cycles[slot].enabled = 1;
}

/* 0x4cf108 */
void gfx_cycle_idle(void)
{
    uint32_t now = plat_ticks_ms();
    if (!(g_cycle_last + 100 < now))
        return;
    g_cycle_last = now;
    if (!g_cycle_running)
        return;
    for (unsigned i = 0; i < GFX_CYCLE_RANGES; i++) {
        CycleRange *c = &g_cycles[i];
        if (c->count == 0)
            continue;
        if (c->counter != 0) {
            c->counter--;
        } else if (c->enabled) {
            c->counter = (uint32_t)iabs(c->rate);
            gfx_palette_rotate(c->first, c->count, c->rate < 0 ? -1 : 1);
        }
    }
}
