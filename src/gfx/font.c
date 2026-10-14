/*
 * Bitmap fonts. See font.h.
 */
#include "gfx/font.h"

#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "gfx/draw.h"
#include "gfx/exe_res.h"

#define FONT_BUILTIN_VA 0x673a10u /* font_builtin */

typedef struct {
    uint32_t id;         /* 0x5485a8[] */
    const uint8_t *data; /* 0x5485d0[] */
    uint32_t size;       /* port-only, for bounds checks */
    bool owned;
} FontSlot;

static FontSlot g_slots[GFX_FONT_SLOTS];
static const uint8_t *g_cur;   /* 0x5485a4 */
static uint32_t g_cur_size;
static uint32_t g_cur_id;      /* 0x5485f8 */
static uint8_t g_ul_color;     /* 0x5485a0: colormap[1] when the text started */
static bool g_ul_pending;      /* 0x673a08: '&' seen */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Glyph of `ch` in font `f`, or NULL if it points outside the data. */
static const uint8_t *glyph_of(const uint8_t *f, uint32_t size, uint8_t ch)
{
    if (!f || size < 0x19 + 256 * 4)
        return NULL;
    uint32_t off = rd32(f + 0x19 + (uint32_t)ch * 4);
    if (off > size - 0x15 - 4)
        return NULL;
    return f + 0x15 + off;
}

static const uint8_t *glyph(uint8_t ch) { return glyph_of(g_cur, g_cur_size, ch); }

/* 0x4cdcbf rc_load_resource: the whole resource in a heap block. */
static uint8_t *load_resource(uint32_t id, uint32_t *size)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    long len = rc_resource_size(id);
    uint8_t *buf = len > 0 ? malloc((size_t)len) : NULL;
    if (buf && plat_file_read(f, buf, 1, (size_t)len) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *size = buf ? (uint32_t)len : 0;
    return buf;
}

static void slot_clear(FontSlot *s)
{
    if (s->owned)
        free((void *)s->data);
    *s = (FontSlot){0};
}

/* 0x4cf400 */
void gfx_font_init(void)
{
    for (unsigned i = 0; i < GFX_FONT_SLOTS; i++)
        slot_clear(&g_slots[i]);
    g_slots[0].id = GFX_FONT_BUILTIN;
    uint32_t avail = 0;
    if (exe_res_open("ALIFE.EXE"))
        g_slots[0].data = exe_res_va(FONT_BUILTIN_VA, &avail);
    g_slots[0].size = g_slots[0].data ? avail : 0;
    gfx_font_select(GFX_FONT_BUILTIN);
}

/* 0x4cf444 */
void gfx_font_shutdown(void)
{
    for (unsigned i = 0; i < GFX_FONT_SLOTS; i++)
        slot_clear(&g_slots[i]);
    g_cur = NULL;
    g_cur_size = 0;
    g_cur_id = 0;
}

static int find_free(void)
{
    for (int i = 0; i < GFX_FONT_SLOTS; i++)
        if (g_slots[i].id == 0)
            return i;
    return -1;
}

/* 0x4cf456 */
bool gfx_font_load(uint32_t id)
{
    int i = find_free();
    if (i < 0)
        return false;
    uint32_t size;
    uint8_t *data = load_resource(id, &size);
    if (!data)
        return false;
    g_slots[i] = (FontSlot){id, data, size, true};
    return true;
}

bool gfx_font_register(uint32_t id, const uint8_t *data, uint32_t size)
{
    int i = find_free();
    if (i < 0 || !data)
        return false;
    g_slots[i] = (FontSlot){id, data, size, false};
    return true;
}

/* 0x4d056e font_slot: loads the font when it isn't cached. (The original
 * recurses forever if that fails.) */
static int font_slot(uint32_t id)
{
    for (int i = 0; i < GFX_FONT_SLOTS; i++)
        if (g_slots[i].id == id)
            return i;
    if (!gfx_font_load(id))
        return -1;
    for (int i = 0; i < GFX_FONT_SLOTS; i++)
        if (g_slots[i].id == id)
            return i;
    return -1;
}

/* Font `id` (0 = current). */
static const FontSlot *font_by_id(uint32_t id, FontSlot *tmp)
{
    if (id == 0) {
        *tmp = (FontSlot){g_cur_id, g_cur, g_cur_size, false};
        return g_cur ? tmp : NULL;
    }
    int i = font_slot(id);
    return i >= 0 && g_slots[i].data ? &g_slots[i] : NULL;
}

/* 0x4cf504 */
uint32_t gfx_font_select(uint32_t id)
{
    int i = font_slot(id);
    if (i < 0)
        return 0;
    uint32_t old = g_cur_id;
    g_cur_id = id;
    g_cur = g_slots[i].data;
    g_cur_size = g_slots[i].size;
    return old;
}

/* 0x4cf559 */
uint32_t gfx_font_current(void) { return g_cur_id; }

/* 0x4d0102 (varargs in the original). The colormap is written in place;
 * fonts live in writable heap blocks there. */
void gfx_font_set_colors(uint32_t id, unsigned first, unsigned count, const unsigned *colors)
{
    FontSlot tmp;
    if (count == 0 || first >= 15 || first + count >= 15)
        return;
    const FontSlot *f = font_by_id(id, &tmp);
    if (!f)
        return;
    uint8_t *map = (uint8_t *)f->data + 6;
    for (unsigned i = 0; i < count; i++)
        if (colors[i] < 0x100)
            map[first + i] = (uint8_t)colors[i];
}

/* 0x4d01d3 */
unsigned gfx_font_get_colormap(uint32_t id, unsigned n, uint8_t *out)
{
    FontSlot tmp;
    const FontSlot *f = out ? font_by_id(id, &tmp) : NULL;
    if (!f)
        return out ? 0 : n;
    unsigned limit = 1u << ((f->data[0x15] - 1) & 0x1f);
    if (limit > 16)
        limit = 16;
    for (unsigned i = 0; i < n && i < limit; i++)
        out[i] = f->data[6 + i];
    return limit;
}

/* 0x4d02a8 */
void gfx_font_set_colormap(uint32_t id, unsigned n, const uint8_t *in)
{
    FontSlot tmp;
    const FontSlot *f = in ? font_by_id(id, &tmp) : NULL;
    if (!f)
        return;
    unsigned limit = 1u << ((f->data[0x15] - 1) & 0x1f);
    if (limit > 16)
        limit = 16;
    for (unsigned i = 0; i < n && i < limit; i++)
        ((uint8_t *)f->data)[6 + i] = in[i];
}

/* 0x4d0361 */
int gfx_font_char_width(uint8_t ch)
{
    switch (ch) {
    case 0: case 4: case 7: case 0xd: case 0xe: case 0x12: case 0x15: case 0x16: case 0x17:
    case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        return 1;
    case 9:
        return 8;
    default: {
        const uint8_t *g = glyph(ch);
        return g ? g[0] : 0;
    }
    }
}

/* 0x4d041f */
int gfx_font_char_height(uint8_t ch)
{
    const uint8_t *g = glyph(ch);
    return g ? g[1] : 0;
}

/* 0x4d045e */
int gfx_font_text_width(const char *str)
{
    int w = 0;
    for (const uint8_t *p = (const uint8_t *)str; p && *p; p++) {
        const uint8_t *g = glyph(*p);
        if (*p == 9)
            w += 8;
        else if (*p != '&' && g)
            w += g[0] + (int8_t)g[2];
    }
    return w;
}

/* 0x4d050d */
int gfx_font_height(uint32_t id)
{
    FontSlot tmp;
    const FontSlot *f = font_by_id(id, &tmp);
    return f ? f->data[0x16] : 0;
}

/* 0x4cf56e. The original computes the bit offset of the first unclipped
 * row in a byte (bpp * rows * width & 0xff); the port doesn't truncate. As
 * in the original, the returned advance shrinks when the glyph is clipped
 * at the left or right. */
int gfx_font_draw_char(GfxSurface *s, uint8_t ch, int x, int y, const GfxRect *clip)
{
    s = gfx_target(s);
    if (!s->pixels)
        return 0;
    const uint8_t *g = glyph(ch);
    if (!g)
        return 0;
    unsigned bpp = g_cur[0x15];
    if (bpp == 0 || bpp > 8)
        return 0;
    int cl, ct, cw, ch_;
    if (clip) {
        cl = clip->left;
        ct = clip->top;
        cw = clip->right - cl + 1;
        ch_ = clip->bottom - ct + 1;
    } else {
        cl = 0;
        ct = 0;
        cw = s->stride;
        ch_ = s->height;
    }
    int gx = x + (int8_t)g[2], gy = y + (int8_t)g[3];
    int gw = g[0], gh = g[1], vis_w = gw;
    int ul_x0 = gx + 1, ul_x1 = gx + gw - 1, ul_y = gy + gh + 1;
    const uint8_t *bits = g + 4;
    uint32_t avail = (uint32_t)(g_cur + g_cur_size - bits);
    if (gx <= cl + cw && gy <= ct + ch_ && cl - gw < gx && ct - gh < gy) {
        uint32_t bit = 0;
        if (gy < ct) {
            bit = (uint32_t)(ct - gy) * (uint32_t)gw * bpp;
            gh -= ct - gy;
            gy = ct;
        }
        int lskip = 0, rskip = 0;
        if (gx < cl) {
            lskip = cl - gx;
            vis_w -= lskip;
            gx = cl;
        }
        if (ct + ch_ < gy + gh)
            gh = ct + ch_ - gy;
        if (cl + cw < gx + vis_w) {
            rskip = gx + vis_w - (cl + cw);
            vis_w -= rskip;
        }
        /* The clip rectangle may extend past the surface; stay inside. */
        unsigned mask = (1u << bpp) - 1;
        for (int row = 0; row < gh; row++, bit += (uint32_t)rskip * bpp) {
            int py = gy + row;
            bit += (uint32_t)lskip * bpp;
            for (int i = 0; i < vis_w; i++, bit += bpp) {
                if (bit / 8 >= avail)
                    break;
                unsigned v = bits[bit / 8] >> (8 - bpp - bit % 8) & mask;
                int px = gx + i;
                if (v && py >= 0 && py < s->height && px >= 0 && px < s->stride)
                    s->pixels[(size_t)py * s->stride + px] = g_cur[6 + v];
            }
        }
        if (gh > 0 && vis_w > 0)
            gfx_touch(s, gx, gx + vis_w - 1, gy, gy + gh - 1);
    }
    if (g_ul_pending) {
        gfx_line(s, ul_x0, ul_x1, ul_y, ul_y, g_ul_color);
        g_ul_pending = false;
    }
    return (int8_t)g[2] + vis_w;
}

/* 0x4cf9f0 on (str, len). */
static int draw_text(GfxSurface *s, int x, int y, const GfxRect *clip, GfxRect *area, const char *str, size_t len,
                     unsigned flags)
{
    if (!str || !s || !s->pixels || !g_cur)
        return 0;
    int cl, ct, cw, ch;
    if (clip) {
        cl = clip->left;
        ct = clip->top;
        cw = clip->right - cl + 1;
        ch = clip->bottom - ct + 1;
    } else {
        cl = 0;
        ct = 0;
        cw = s->stride;
        ch = s->height;
    }
    int al = cl, at = ct, aw = cw, ah = ch;
    if (area) {
        al = area->left;
        at = area->top;
        aw = area->right - al + 1;
        ah = area->bottom - at + 1;
    }
    if (al + aw < x || at + ah < y)
        return 0;
    const uint8_t *sp = glyph(' ');
    int space_w = sp ? sp[0] : 0;
    int total = 0;
    if (flags & GFX_TEXT_CENTER)
        x = (cw >> 1) + cl + 1;
    int start = x;
    g_ul_color = g_cur[7];

    const uint8_t *p = (const uint8_t *)str, *end = p + len;
    while (p < end && *p) {
        /* Measure one line: up to a newline, the clip edge or the end. */
        const uint8_t *q = p;
        int n = 0, cx = start, last_sp_n = 0, spaces = 0, last_sp_px = 0, lx = start;
        for (;;) {
            if (cx >= cl + cw || lx < cl || q >= end || *q == 0)
                break;
            if (*q == '\n' || *q == '\r') {
                last_sp_n = n + 1;
                if (q + 1 < end && (q[1] == '\n' || q[1] == '\r')) {
                    last_sp_n = n + 2;
                    spaces += 2;
                    q += 2;
                } else {
                    spaces += 1;
                    q += 1;
                }
                n = last_sp_n;
                break;
            }
            if (*q == ' ') {
                spaces++;
                last_sp_px = cx - start;
                last_sp_n = n;
            }
            if (*q == 9) {
                total += 9;
                if (flags & GFX_TEXT_CENTER) {
                    lx -= 4;
                    cx += 5;
                } else {
                    cx += 9;
                }
            } else if (*q != '&') {
                const uint8_t *g = glyph(*q);
                int adv = g ? (int8_t)g[2] + g[0] : 0;
                cx += adv;
                total += adv;
                if (flags & GFX_TEXT_CENTER) {
                    lx -= adv >> 1;
                    cx -= adv >> 1;
                }
            }
            n++;
            q++;
        }
        /* More text follows: break at the last space (or newline) and
         * spread the slack over the spaces. */
        int sp_adv = space_w;
        if (q < end && *q && last_sp_n != 0) {
            if (--spaces == 0)
                spaces = 1;
            sp_adv = space_w + (cw - last_sp_px) / spaces;
            n = last_sp_n;
        }
        const uint8_t *line_start = p;
        int dx = lx;
        while (n-- > 0 && p < end) {
            uint8_t c = *p++;
            switch (c) {
            case 9:
                dx += 8;
                break;
            case '\n':
                dx = start;
                break;
            case '\r':
                break;
            case '&':
                g_ul_pending = true;
                break;
            default:
                if (c == ' ' && !(flags & GFX_TEXT_NOJUSTIFY)) {
                    gfx_font_draw_char(s, ' ', dx, y, area);
                    dx += sp_adv;
                } else {
                    dx += gfx_font_draw_char(s, c, dx, y, area);
                }
                break;
            }
        }
        if (p < end && *p == ' ')
            p++;
        y += g_cur[0x16] + 1;
        /* The original loops forever when a line makes no progress (text
         * starting at or past the clip edge). */
        if (p == line_start)
            break;
    }
    if ((flags & GFX_TEXT_SET_BOTTOM) && area)
        area->bottom = y + 1;
    return total + 1;
}

/* 0x4cf9f0 */
int gfx_font_draw_text(GfxSurface *s, int x, int y, const GfxRect *clip, GfxRect *area, const char *str,
                       unsigned flags)
{
    return draw_text(gfx_target(s), x, y, clip, area, str, str ? strlen(str) : 0, flags);
}

/* 0x4cff0e. Each line is handed to font_draw_text on its own (the
 * original NUL-terminates it in place), so the justification there never
 * kicks in. */
void gfx_font_draw_wrapped(GfxSurface *s, const GfxRect *rect, const char *str, uint32_t id, uint8_t c1,
                           uint8_t c2)
{
    s = gfx_target(s);
    if (!rect || !str)
        return;
    uint32_t old = gfx_font_select(id);
    unsigned colors[2] = {c1, c2};
    gfx_font_set_colors(id, 1, 2, colors);
    int lh = gfx_font_height(id);
    int space_w = gfx_font_char_width(' ');
    unsigned maxw = (unsigned)(rect->right - rect->left + 1);
    int y = rect->top;
    const char *line = str;
    while (*line) {
        const char *p = line, *brk = line;
        unsigned acc = 0;
        while (acc <= maxw) {
            if (*p == '\n') {
                brk = p + 1;
                break;
            }
            if (*p == ' ') {
                brk = p;
                while (*p == ' ') {
                    acc += (unsigned)space_w;
                    p++;
                }
            } else if (*p == 0) {
                brk = p;
                break;
            } else {
                acc += (unsigned)gfx_font_char_width((uint8_t)*p);
                p++;
            }
        }
        /* A word wider than the rectangle: the original never advances. */
        if (brk == line)
            brk = p > line ? p - (p - line > 1) : line + 1;
        GfxRect r = *rect;
        draw_text(s, rect->left, y, &r, &r, line, (size_t)(brk - line), 0);
        y += lh;
        line = brk;
        while (*line == ' ')
            line++;
    }
    if (id != old)
        gfx_font_select(old);
}
