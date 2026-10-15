/*
 * The map tile renderer (tile_draw, 0x4e6a6a, ~14 KB in the original). See
 * tile_draw.h, docs/formats/tile.md and docs/map.md.
 *
 * The original is one function with the strip decoder unrolled four times
 * (zoom 0 / zoomed out x effects / no effects, plus the 2x magnified path).
 * Here the setup (which sprite, where, which effects) is tile_draw() and the
 * three decoder loops are draw_rows_1x(), draw_rows_2x() and
 * draw_rows_zoomed(). Pixel effects are applied in the original's order by
 * fx_pixel(). Quirks of the original are kept and marked "quirk" (they change
 * pixels); its unchecked reads and writes are bounds-checked here.
 */
#include "gfx/tile_draw.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/palette.h"

/* --- state ---------------------------------------------------------------- */

static const TileSet *g_sets[TILE_MAX_SETS]; /* tile_set_entries, 0x57d478 */
static uint8_t g_noise[256];                 /* shade table 5, 0x64d410 */
static uint8_t g_hue_down[256];              /* 0x57d880 */
static uint8_t g_hue_up[256];                /* 0x57df08 */
static uint32_t g_fade_seed[50];             /* 0x57d980 */
static int g_fade_pos = 1;                   /* 0x57e008 */
static uint32_t g_rng = 1;

/* Smallest logical cell the focus rule draws (0x64e4f8 / 0x64e4fc, set by
 * map_view_draw). */
int32_t tile_draw_focus_min_x, tile_draw_focus_min_y;

enum {
    SLOT_TERRAIN = 1,  /* resource 2 */
    SLOT_STATIC = 4,   /* resource 5: CELL_TINT shows TV static */
    SLOT_OVERLAY = 10, /* resource 11: brackets and overlay sprites (0x57d4a0) */
    SLOT_HELL_ZONES = 14,
    SLOT_HEAVEN_ZONES = 15,
    SLOT_ZONE_BLOCKS = 28, /* resource 29 (0x57d4e8) */
    SLOT_METER = 30,
};

/* The game RNG (FUN_0042b088(lo, hi)) isn't ported; only the distribution
 * matters for these tables. */
static int rng_range(int lo, int hi)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return lo + (int)((g_rng >> 16) % (uint32_t)(hi - lo + 1));
}

void tile_draw_bind_set(int slot, const TileSet *ts)
{
    if (slot >= 0 && slot < TILE_MAX_SETS)
        g_sets[slot] = ts;
}

const TileSet *tile_draw_set(int slot) { return slot >= 0 && slot < TILE_MAX_SETS ? g_sets[slot] : NULL; }

/* 0x4e66b0 */
void tile_draw_init(uint32_t seed)
{
    g_rng = seed ? seed : 1;
    for (int i = 0; i < 256; i++)
        g_hue_down[i] = g_hue_up[i] = (uint8_t)i;
    /* Rotate 0x20..0xaf (144 colours) by +-10, wrapping inside the range. */
    for (int i = 0x20; i < 0xb0; i++) {
        int c = i + 10;
        if (c > 0xaf)
            c = i - 0x86;
        g_hue_up[i] = (uint8_t)c;
        c = i - 10;
        if (c > 0xaf)
            c = i - 0x9a;
        if (c < 0x20)
            c = 0xaf - (0x1f - c);
        g_hue_down[i] = (uint8_t)c;
    }
    for (int i = 0; i < 50; i++)
        g_fade_seed[i] = (uint32_t)rng_range(1, 0x7fff) << 16 | (uint32_t)rng_range(1, 0x7fff);
    g_fade_pos = 1;
}

/* tail of 0x408eb0 tile_load_all */
void tile_draw_build_tables(const uint8_t *rgbx)
{
    gfx_palette_build_shade_table(0, rgbx, 0x100, 0x100, 0x80, 0x20, 0xff, 0x78, 0x7d);
    gfx_palette_build_shade_table(1, rgbx, 0xf0, 0xf0, 0xf0, 10, 0xff, 0x26, 0x2f);
    gfx_palette_build_shade_table(2, rgbx, 0x100, 0x100, 0x100, 0x20, 0xff, 0x20, 0x27);
    gfx_palette_build_shade_table(3, rgbx, 0x80, 0x80, 500, 10, 0xff, 0x20, 0xaf);
    gfx_palette_build_shade_table(4, rgbx, 0x100, 0x100, 0x100, 0x20, 0xff, 0x7c, 0x7f);
    for (int i = 0; i < 256; i++)
        g_noise[i] = i < 0x20 ? (uint8_t)i : (uint8_t)rng_range(0xe3, 0xe6);
}

const uint8_t *tile_draw_table(unsigned n) { return n == 5 ? g_noise : gfx_palette_shade_table(n); }

uint8_t *tile_draw_hue_table(int dir) { return dir < 0 ? g_hue_down : g_hue_up; }

/* --- entry access ----------------------------------------------------------- */

/* tile_set_entries[slot][index - 1]: false if the slot is empty or the index
 * out of range (the original would read whatever is there). */
static bool get_entry(unsigned slot, unsigned index1, TileEntry *e)
{
    if (slot >= TILE_MAX_SETS || !g_sets[slot] || index1 == 0)
        return false;
    return tile_get_entry(g_sets[slot], (int)index1 - 1, e) == TILE_OK;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A strip's row data: the original's `entry + 0x1c + 4*strips +
 * offset[strip - 1]` (strip 1-based). */
typedef struct {
    const uint8_t *p;   /* current row's length byte */
    const uint8_t *end; /* end of the entry */
    const uint8_t *lut;
    size_t lut_len;
} Strip;

static bool strip_open(const TileEntry *e, unsigned strip1, Strip *s)
{
    if (e->type != 1 || strip1 == 0 || strip1 > e->strips)
        return false;
    size_t at = 0x1c + 4u * e->strips + rd32(e->base + 0x1c + 4 * (strip1 - 1));
    if (at >= e->avail)
        return false;
    s->p = e->base + at;
    s->end = e->base + e->avail;
    s->lut = e->lut;
    s->lut_len = e->lut_len;
    return true;
}

static uint8_t lut_at(const Strip *s, uint8_t i) { return i < s->lut_len ? s->lut[i] : 0; }

/* Steps to the next row (`p += *p + 1`). */
static void strip_next_row(Strip *s)
{
    if (s->p < s->end)
        s->p += (size_t)*s->p + 1;
    if (s->p > s->end)
        s->p = s->end;
}

/* Decodes `w` pixels of the current row (skip = 0, otherwise lut[byte]) into
 * buf and moves to the next row: the overlay decoder (into 0x57de48 /
 * 0x57dea8). */
static void strip_decode_row(Strip *s, uint8_t *buf, int w)
{
    memset(buf, 0, (size_t)w);
    if (s->p >= s->end)
        return;
    const uint8_t *p = s->p + 1, *row_end = s->p + 1 + *s->p;
    if (row_end > s->end)
        row_end = s->end;
    int x = 0;
    while (x < w && p < row_end) {
        int c = *p++, n = c & 0x3f, kind = c & 0xc0;
        for (int i = 0; i < n && x < w; i++, x++) {
            if (kind == 0x40)
                buf[x] = p + i < row_end ? lut_at(s, p[i]) : 0;
            else if (kind == 0x80)
                buf[x] = p < row_end ? lut_at(s, *p) : 0;
        }
        if (kind == 0x40)
            p += n;
        else if (kind == 0x80)
            p++;
        else if (kind == 0xc0)
            break; /* unused; would loop forever in the original */
    }
    strip_next_row(s);
}

/* Packet cursor within one row. `p` points at the next literal byte or at
 * the run's value; `n` is what's left of the current packet. */
typedef struct {
    const uint8_t *p, *row_end;
    int kind, n;
    bool done;
} Cursor;

static void cur_header(Cursor *c)
{
    if (c->p >= c->row_end) {
        c->done = true;
        c->kind = 0;
        c->n = 0;
        return;
    }
    int b = *c->p++;
    c->kind = b & 0xc0;
    c->n = b & 0x3f;
}

static void cur_start(Cursor *c, const Strip *s)
{
    c->done = false;
    if (s->p >= s->end) {
        c->p = c->row_end = s->end;
        cur_header(c);
        return;
    }
    c->p = s->p + 1;
    c->row_end = s->p + 1 + *s->p;
    if (c->row_end > s->end)
        c->row_end = s->end;
    cur_header(c);
}

/* Leaves the current packet (whatever is left of it) for the next one. */
static void cur_next(Cursor *c)
{
    if (c->kind == 0x40)
        c->p += c->n;
    else if (c->kind == 0x80)
        c->p++;
    cur_header(c);
}

static uint8_t cur_byte(const Cursor *c, int i) { return c->p + i < c->row_end ? c->p[i] : 0; }

/* --- per-pixel effects ------------------------------------------------------ */

typedef struct {
    GfxSurface *dst;
    int dir;              /* x step, +-1 */
    bool fx;              /* local_114: any effect (otherwise lut[] is written as is) */
    bool ov1, ov2;        /* local_c8, local_f0: overlay sprites */
    bool tint, shade0, shade4, noise; /* local_3c[1], [0], [4], [5] */
    bool meter;           /* local_c */
    uint8_t meter_lo, meter_hi;
    uint8_t key;
    const uint8_t *t0, *t1, *t4, *t5;
    uint8_t ov1_row[256], ov2_row[256];
    const uint8_t *ov1p, *ov2p;
    int fade;
    uint32_t fade_thr, seed;
    int clip_l, clip_r, clip_t, clip_b; /* inclusive, also inside the surface */
} Fx;

static uint8_t fx_pixel(Fx *f, uint8_t c)
{
    if (f->meter) {
        if (c < 0xb)
            c = c < f->meter_lo ? 0 : 0x30;
        if (c > 0xf5)
            c = c < f->meter_hi ? 0 : 0x48;
    }
    if (!f->ov1) {
        if (f->tint)
            c = f->t1[c];
    } else {
        uint8_t o = *f->ov1p++;
        c = o == f->key ? f->t1[c] : o;
    }
    if (f->ov2) {
        uint8_t o = *f->ov2p++;
        if (o != f->key)
            c = o;
    }
    if (f->shade0)
        c = f->t0[c];
    if (f->shade4)
        c = f->t4[c];
    if (f->noise)
        c = f->t5[c];
    return c;
}

/* The dithered fade: rotl(seed, 3) per opaque pixel (FUN_004f0905), drawn
 * when below fade * 0x19999999. */
static bool fade_pass(Fx *f)
{
    if (!f->fade)
        return true;
    f->seed = f->seed << 3 | f->seed >> 29;
    return f->seed < f->fade_thr;
}

/* The original writes wherever its pointer is; the port drops writes that
 * would leave the surface (only the 2x path can step one pixel past the
 * clip rectangle). */
static void put(Fx *f, int x, int y, uint8_t c)
{
    GfxSurface *s = f->dst;
    if (x >= 0 && y >= 0 && x < s->width && y < s->height)
        s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] = c;
}

static void fx_row_start(Fx *f, Strip *o1, Strip *o2, int src_w, int ov_off)
{
    if (src_w > 256)
        src_w = 256;
    if (ov_off >= src_w)
        ov_off = src_w - 1;
    if (ov_off < 0)
        ov_off = 0;
    if (f->ov1) {
        strip_decode_row(o1, f->ov1_row, src_w);
        f->ov1p = f->ov1_row + ov_off;
        if (*f->ov1p == 1) /* colour 1 in the overlay: none for this sprite */
            f->ov1 = false;
    }
    if (f->ov2) {
        strip_decode_row(o2, f->ov2_row, src_w);
        f->ov2p = f->ov2_row + ov_off;
        if (*f->ov2p == 1)
            f->ov2 = false;
    }
    if (f->fade) {
        if (++g_fade_pos > 49)
            g_fade_pos = 0;
        f->seed = g_fade_seed[g_fade_pos];
    }
}

/* Keeps the overlay pointers inside their row buffers. */
static void fx_ov_skip(Fx *f, int n)
{
    if (f->ov1)
        f->ov1p += n;
    if (f->ov2)
        f->ov2p += n;
    const uint8_t *lim1 = f->ov1_row + 255, *lim2 = f->ov2_row + 255;
    if (f->ov1p > lim1)
        f->ov1p = lim1;
    if (f->ov2p > lim2)
        f->ov2p = lim2;
}

/* One opaque source pixel through the effects onto (x, y) (and a w x h
 * block for the 2x path). */
static void plot(Fx *f, const Strip *s, uint8_t raw, int x, int y, int bw, int bh)
{
    uint8_t c = lut_at(s, raw);
    if (f->fx) {
        if (f->ov1 && f->ov1p > f->ov1_row + 255)
            f->ov1p = f->ov1_row + 255;
        if (f->ov2 && f->ov2p > f->ov2_row + 255)
            f->ov2p = f->ov2_row + 255;
        c = fx_pixel(f, c);
        if (c == 0 || !fade_pass(f))
            return;
    }
    for (int dy = 0; dy < bh; dy++)
        for (int dx = 0; dx < bw; dx++)
            put(f, x + dx * f->dir, y + dy, c);
}

/* --- decoder loops ------------------------------------------------------------ */

typedef struct {
    int x0;       /* first destination x of a row (rightmost when mirrored) */
    int y0;       /* first destination row */
    int rows;     /* local_10c */
    int width;    /* local_144: destination pixels per row */
    int skip;     /* left/right clip in destination pixels (b4 or 20) */
    int ov_off;   /* where the overlay row starts (destination units) */
    int src_w;    /* local_50 */
    int zoom;
} Geo;

/* Zoom 0, 1:1. */
static void draw_rows_1x(Fx *f, Strip *s, Strip *o1, Strip *o2, const Geo *g)
{
    for (int r = 0; r < g->rows; r++) {
        fx_row_start(f, o1, o2, g->src_w, g->ov_off);
        Cursor c;
        cur_start(&c, s);
        /* skip the clipped-off source pixels */
        for (int k = g->skip; k > 0 && !c.done;) {
            int take = c.n < k ? c.n : k;
            if (c.kind == 0x40)
                c.p += take;
            c.n -= take;
            k -= take;
            if (k > 0)
                cur_next(&c);
        }
        int x = g->x0, y = g->y0 + r;
        for (int left = g->width; left > 0 && !c.done;) {
            int take = c.n < left ? c.n : left;
            if (c.kind == 0) {
                x += take * f->dir;
                if (f->fx)
                    fx_ov_skip(f, take);
            } else if (c.kind == 0x40 || c.kind == 0x80) {
                for (int i = 0; i < take; i++, x += f->dir)
                    plot(f, s, cur_byte(&c, c.kind == 0x40 ? i : 0), x, y, 1, 1);
            }
            left -= take;
            if (left < 1)
                break;
            if (c.kind == 0x40) {
                c.p += take;
                c.n -= take;
            } else {
                c.n -= take;
            }
            cur_next(&c);
        }
        strip_next_row(s);
    }
}

/* Zoom 0, magnified: every source pixel becomes 2x2, two destination rows
 * per source row. Quirk: a clip of odd width/height starts half a source
 * pixel off, and the last pixel/row can spill one past the clip. */
static void draw_rows_2x(Fx *f, Strip *s, Strip *o1, Strip *o2, const Geo *g)
{
    for (int r = 0; r < g->rows; r += 2) {
        fx_row_start(f, o1, o2, g->src_w, g->ov_off);
        Cursor c;
        cur_start(&c, s);
        for (int k = g->skip >> 1; k > 0 && !c.done;) {
            int take = c.n < k ? c.n : k;
            if (c.kind == 0x40)
                c.p += take;
            c.n -= take;
            k -= take;
            if (k > 0)
                cur_next(&c);
        }
        int x = g->x0, y = g->y0 + r;
        for (int left = g->width; left > 0 && !c.done;) {
            int take = (left + 1) >> 1;
            if (c.n < take)
                take = c.n;
            if (c.kind == 0) {
                x += 2 * take * f->dir;
                if (f->fx)
                    fx_ov_skip(f, take);
            } else if (c.kind == 0x40 || c.kind == 0x80) {
                for (int i = 0; i < take; i++, x += 2 * f->dir)
                    plot(f, s, cur_byte(&c, c.kind == 0x40 ? i : 0), x, y, 2, 2);
            }
            left -= 2 * take;
            if (left < 1)
                break;
            if (c.kind == 0x40)
                c.p += take;
            c.n -= take;
            cur_next(&c);
        }
        strip_next_row(s);
    }
}

/* Zoomed out by 2^zoom: every 2^zoom-th pixel of every 2^zoom-th row.
 * Sampling restarts per packet, which only differs from uniform sampling
 * after an empty packet. Quirks: the overlays still advance one row per
 * destination row and one pixel per drawn pixel (not per source pixel), so
 * they don't line up with the sprite when zoomed out. */
static void draw_rows_zoomed(Fx *f, Strip *s, Strip *o1, Strip *o2, const Geo *g)
{
    int step = 1 << g->zoom, rest = step - 1; /* local_4c */
    for (int r = 0; r < g->rows; r++) {
        fx_row_start(f, o1, o2, g->src_w, g->ov_off);
        Cursor c;
        cur_start(&c, s);
        int x = g->x0, y = g->y0 + r;
        int skip = g->skip << g->zoom; /* local_dc */
        for (int left = g->width; left > 0 && !c.done;) {
            for (int k = skip; k != 0 && !c.done;) {
                if (k < c.n) {
                    c.n -= k;
                    if (c.kind == 0x40)
                        c.p += k;
                    k = 0;
                } else {
                    k -= c.n;
                    cur_next(&c);
                }
            }
            if (c.done)
                break;
            int cnt = (rest + c.n) >> g->zoom; /* local_10 */
            int used = (cnt << g->zoom) - rest; /* local_140 */
            skip = rest;
            if (cnt == 0)
                continue; /* quirk: an empty packet costs another `rest` pixels */
            int n = cnt <= left ? cnt : left;
            left -= cnt;
            if (c.kind == 0) {
                x += n * f->dir;
            } else if (c.kind == 0x40) {
                for (int i = 0; i < n; i++, x += f->dir)
                    plot(f, s, cur_byte(&c, i * step), x, y, 1, 1);
                c.p += (n - 1) * step + 1;
            } else if (c.kind == 0x80) {
                uint8_t v = cur_byte(&c, 0);
                for (int i = 0; i < n; i++, x += f->dir)
                    plot(f, s, v, x, y, 1, 1);
            }
            c.n -= used;
        }
        for (int i = 0; i < step; i++)
            strip_next_row(s);
    }
}

/* --- tile_draw -------------------------------------------------------------- */

static int iabs(int v) { return v < 0 ? -v : v; }

/* Zone sets 14/15 shown as plain blocks from slot 28: one per footprint and
 * zone type (13 entries per type). */
static unsigned zone_block_index(unsigned size, unsigned index0)
{
    unsigned t = index0 / 13;
    if (t > 6)
        t = 7;
    unsigned i = size * 8 + t - 7;
    return i > 24 ? 25 : i;
}

/* A cell of a big building with set 0 points back at the building's origin
 * cell: bits 16-18 = dx, 20-22 = dy (origin at x - dx, y + dy). Returns the
 * origin cell's value and moves *ox, *oy to it. */
static uint32_t follow_origin(const MapView *v, const uint32_t *cell, uint32_t cc, int *ox, int *oy)
{
    if (cc & CELL_SET_MASK)
        return cc;
    int dy = (int)(cc >> 20 & 7), dx = (int)(cc >> 16 & 7);
    *ox -= dx;
    *oy += dy;
    if (!v->cells)
        return cc;
    ptrdiff_t at = (cell - v->cells) + (ptrdiff_t)v->width * dy - dx;
    return at >= 0 && at < (ptrdiff_t)v->width * v->height ? v->cells[at] : cc;
}

/* 0x4e6a6a */
TileDrawResult tile_draw(GfxSurface *dst, MapView *v, int map_x, int map_y, int sx, int sy, const uint32_t *cell,
                         unsigned set, unsigned index, unsigned strip, int fade, int force)
{
    TileDrawResult res = {0, 0, 0, map_x, map_y};
    dst = gfx_target(dst);
    if (!dst || !v)
        return res;

    int zoom = v->zoom, mag = v->magnify;
    int dir = mapview_dir(v); /* sign of local_184 */
    int hw = ((v->cell_w >> 1) >> v->zoom) << v->magnify; /* local_f8 */
    int hh = ((v->cell_h >> 1) >> v->zoom) << v->magnify; /* local_148 */
    int org_x = -1, org_y = -1; /* local_8 / local_1c: the building's origin cell */
    uint32_t cc = 0;
    bool bracket = false, hidden = false;
    unsigned bracket_index = 0;
    TileEntry e;
    unsigned size;

    if (!cell) {
        if (!get_entry(set, index, &e))
            return res;
        size = e.size;
    } else {
        org_x = map_x;
        org_y = map_y;
        cc = *cell;
        if (v->plane != 2 && (cc & CELL_BRACKET)) {
            bracket = true;
            bracket_index = v->plane == 1 ? 0x25 : 0x2d;
        }
        if (!(cc & CELL_ALT)) {
            if (strip == 0) {
                strip = cell_word_strip(cc);
                if (strip != 0) {
                    cc = follow_origin(v, cell, cc, &org_x, &org_y);
                    index = cell_word_index(cc);
                    set = cell_word_set(cc);
                }
            } else {
                index = cell_word_index(cc);
                set = cell_word_set(cc);
                force = 1;
            }
        } else if (v->plane == 2) {
            set = 1, index = 2, strip = 1;
        } else {
            strip = cell_word_strip(cc);
            cc = follow_origin(v, cell, cc, &org_x, &org_y);
            unsigned i0 = (cc & CELL_INDEX_MASK) >> CELL_INDEX_SHIFT, s = cell_word_set(cc);
            if (s == SLOT_HELL_ZONES || s == SLOT_HEAVEN_ZONES) {
                TileEntry z;
                unsigned zsize = get_entry(s, i0 + 1, &z) ? z.size : 0;
                set = SLOT_ZONE_BLOCKS;
                index = zone_block_index(zsize, i0);
            } else {
                strip = 1, set = 15, index = 0x5c; /* one stand-in per cell */
            }
        }
        if (set == 1 && index == 1 && !(v->flags & MAPVIEW_F_GRID))
            index = 2;
        if ((cc & CELL_DEFER) && !force)
            hidden = true;
        if (!get_entry(set, index, &e))
            return res;
        size = e.size;
    }
    if (!(v->flags & MAPVIEW_F_ZONE_DETAIL) && (set == SLOT_HELL_ZONES || set == SLOT_HEAVEN_ZONES)) {
        set = SLOT_ZONE_BLOCKS;
        index = zone_block_index(size, index - 1);
        if (!get_entry(set, index, &e))
            return res;
    }

    res.set = (uint8_t)set;
    res.index = (uint8_t)index;
    res.strip = (uint8_t)strip;
    if (strip >= size * 2u)
        return res;
    if (strip == 0)
        return res;
    bool focus_ok = set < 32 && (v->set_mask & (1u << set)) && map_x >= tile_draw_focus_min_x &&
                    map_y >= tile_draw_focus_min_y;
    if (!(focus_ok || force || v->focus_x < 0))
        return res;

    if (bracket)
        tile_draw(dst, v, map_x, map_y, sx, sy, NULL, SLOT_OVERLAY, bracket_index + size * 2, strip, 0, force);

    /* Miniature mode: half size, raised, on a plinth from set 1. */
    int x = sx, y = sy;
    bool mini = false;
    unsigned plinth = 0;
    if (v->mini && set != 1 && set != 12 && set != 2 && set != 10 && set != 9 && set != 16 && set != 17 &&
        set != 18 &&
        ((set != SLOT_HELL_ZONES && set != SLOT_HEAVEN_ZONES) || index > 0x5c || (index - 1) % 13 != 0) &&
        (set < 20 || set > 29) && v->plane != 2) {
        mini = true;
        if (v->magnify == 0)
            zoom++;
        else
            mag = 0;
        plinth = v->plane == 1 ? 0x19 : 0x15;
        hw >>= 1;
        hh >>= 1;
        int k = dir < 1 ? (int)strip - (int)size : (int)size - (int)strip;
        x = sx + (k + 1) * hw;
        y = sy - (((int)size * 2 - iabs((int)size - (int)strip)) - zoom + v->magnify) * hh;
    }

    if (!hidden) {
        Fx f;
        memset(&f, 0, sizeof f);
        f.dst = dst;
        f.dir = dir < 1 ? -1 : 1;
        f.key = v->key;
        f.t0 = tile_draw_table(0);
        f.t1 = tile_draw_table(1);
        f.t4 = tile_draw_table(4);
        f.t5 = tile_draw_table(5);
        if (v->plane != 2) {
            if (cc & 1) {
                if (v->plane == 3)
                    f.shade4 = true;
                else
                    f.shade0 = true;
            }
            if (cc & 2)
                f.tint = true;
        }
        int src_w = size == strip ? v->cell_w : v->cell_w >> 1; /* local_50 */
        if (size != strip) {
            /* the half strips right of the centre take the cell's right half */
            if (dir < 1 ? strip < size : strip > size)
                x += hw;
        }
        Strip s, o1 = {0}, o2 = {0};
        bool ok = strip_open(&e, strip, &s);
        TileEntry oe;
        if ((cc & CELL_OVERLAY) && size < 5) {
            unsigned oi = set == SLOT_OVERLAY ? 37 : 26 + size;
            f.ov2 = get_entry(SLOT_OVERLAY, oi, &oe) && strip_open(&oe, strip, &o2);
        }
        if ((cc & CELL_TINT) && !(v->flags & MAPVIEW_F_NO_TINT_OVERLAY)) {
            if (set == SLOT_STATIC)
                f.noise = true;
            else if (size < 5)
                f.ov1 = get_entry(SLOT_OVERLAY, 30 + size, &oe) && strip_open(&oe, strip, &o1);
        }
        if (set == SLOT_METER && org_x > 0 && v->meter)
            f.meter = v->meter(v->meter_ctx, org_x, org_y, (int)index - 1, &f.meter_lo, &f.meter_hi);
        f.fade = fade;
        f.fade_thr = (uint32_t)fade * 0x19999999u;
        f.fx = f.ov1 || f.ov2 || f.shade0 || f.tint || f.shade4 || f.noise || fade != 0 || f.meter;

        if (ok) {
            y += (v->cell_h >> zoom) - ((e.anchor >> zoom) << mag) + iabs((int)strip - (int)size) * hh;
            int w = (src_w << mag) >> zoom, h = (e.height << mag) >> zoom;
            int cl = v->clip_x, cr = v->clip_x + v->clip_w - 1;
            int ct = v->clip_y, cb = v->clip_y + v->clip_h - 1;
            int x1 = x + w - 1, y1 = y + h - 1;
            if (x1 < cl || cr < x || y1 < ct || cb < y) {
                if (mini && size < 5)
                    tile_draw(dst, v, map_x, map_y, sx, sy, NULL, SLOT_TERRAIN, plinth + size, strip, 0, force);
                return res; /* quirk: skips the front half of the bracket */
            }
            int lclip = cl - x > 0 ? cl - x : 0, rclip = x1 - cr > 0 ? x1 - cr : 0;
            int tclip = ct - y > 0 ? ct - y : 0, bclip = y1 - cb > 0 ? y1 - cb : 0;
            Geo g;
            g.width = w - lclip - rclip;
            g.rows = h - tclip - bclip;
            g.x0 = x + lclip + (f.dir < 0 ? g.width - 1 : 0);
            g.y0 = y + tclip;
            g.skip = f.dir > 0 ? lclip : rclip;
            g.src_w = src_w;
            g.zoom = zoom;
            g.ov_off = zoom == 0 ? lclip >> mag : lclip;
            /* rows above the clip: (2^zoom / 2^mag) source rows per row */
            uint32_t acc = 0, inc = (0x10000u >> mag) << zoom;
            for (int t = 0; t < tclip; t++) {
                acc += inc;
                while (acc > 0xffff) {
                    acc -= 0x10000;
                    strip_next_row(&s);
                    if (f.ov1)
                        strip_next_row(&o1);
                    if (f.ov2)
                        strip_next_row(&o2);
                }
            }
            if (zoom != 0)
                draw_rows_zoomed(&f, &s, &o1, &o2, &g);
            else if (mag != 0)
                draw_rows_2x(&f, &s, &o1, &o2, &g);
            else
                draw_rows_1x(&f, &s, &o1, &o2, &g);
            gfx_touch(dst, x + lclip, x + lclip + g.width - 1, g.y0, g.y0 + g.rows - 1);
        }
    }
    if (bracket)
        tile_draw(dst, v, map_x, map_y, sx, sy, NULL, SLOT_OVERLAY, bracket_index + size * 2 + 1, strip, 0, force);
    if (mini && size < 5)
        tile_draw(dst, v, map_x, map_y, sx, sy, NULL, SLOT_TERRAIN, plinth + size, strip, 0, force);
    return res;
}

/* 0x4eb585 */
void tile_draw_preview(GfxSurface *dst, MapView *v, unsigned set, unsigned index, int zoom, const uint32_t *cell)
{
    int8_t save_dx = v->dir_x, save_dy = v->dir_y;
    int32_t save_zoom = v->zoom;
    v->dir_x = v->dir_y = 1;
    v->zoom = zoom;
    int hw = (int8_t)((((v->cell_w >> 1) >> zoom) & 0xff) << v->magnify);
    int hh = (int8_t)(((v->cell_h >> 1) >> zoom) << v->magnify);
    TileEntry e;
    unsigned es = set, ei = index;
    if (cell) {
        es = cell_word_set(*cell);
        ei = cell_word_index(*cell);
    }
    if (get_entry(es, ei, &e)) {
        int size = e.size;
        int x = (v->clip_w >> 1) + v->clip_x - hw * size;
        int y = (v->clip_h >> 1) + v->clip_y - hh * (size + 1) + (((e.anchor >> 1) >> zoom) << v->magnify);
        for (int s = 1; s < size * 2; s++) {
            if (!cell)
                tile_draw(dst, v, 0, 0, x, y, NULL, set, index, (unsigned)s, 0, 1);
            else
                tile_draw(dst, v, 0, 0, x, y, cell, 0, 0, (unsigned)s, 0, 1);
            x += hw;
            if (s == size)
                hh = -hh;
            y += hh;
        }
    }
    v->zoom = save_zoom;
    v->dir_x = save_dx;
    v->dir_y = save_dy;
}
