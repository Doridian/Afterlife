/*
 * Mouse and cursor. See mouse.h.
 */
#include "gfx/mouse.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/exe_res.h"

typedef struct {
    int x, y;
    unsigned buttons;
} MouseEvent;

#define GFX_MOUSE_QUEUE_LEN 32

static int g_x, g_y;              /* 0x522fb0 / 0x522fb4 */
static unsigned g_buttons;        /* 0x522fac */
static GfxRect g_clip;            /* 0x523138: left, right, top, bottom */
static GfxRect g_saved_clip;      /* 0x522f98 */
static GfxMouseHandler g_handler;       /* 0x522fa8 */
static MouseEvent g_queue[GFX_MOUSE_QUEUE_LEN]; /* 0x522fb8 */
static int g_head, g_tail;        /* 0x672548 (read) / 0x672544 (write) */
static PlatCursor *g_cursor;      /* 0x67254c */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 0x4b93fb: drops the event when the queue is full. */
static void queue_push(int x, int y, unsigned buttons)
{
    int next = g_tail + 1 >= GFX_MOUSE_QUEUE_LEN ? 0 : g_tail + 1;
    if (next == g_head)
        return;
    g_queue[g_tail] = (MouseEvent){x, y, buttons};
    g_tail = next;
}

/* 0x4b935a */
bool gfx_mouse_queue_pop(int *x, int *y, unsigned *buttons)
{
    if (g_head == g_tail)
        return false;
    const MouseEvent *e = &g_queue[g_head];
    if (x)
        *x = e->x;
    if (y)
        *y = e->y;
    if (buttons)
        *buttons = e->buttons;
    g_head = g_head + 1 >= GFX_MOUSE_QUEUE_LEN ? 0 : g_head + 1;
    return true;
}

/* 0x4b933b */
void gfx_mouse_queue_reset(void) { g_head = g_tail = 0; }

/* 0x4b8cd0. SetCursorPos takes screen coordinates: the center of the
 * desktop. */
void gfx_mouse_init(void)
{
    gfx_mouse_set_pos(gfx_screen_width() / 2, gfx_screen_height() / 2);
    GfxRect prev, clip = {0, 10000, 0, 10000};
    gfx_mouse_set_clip_rect(&prev, &clip);
}

/* 0x4b8d5e */
unsigned gfx_mouse_get_state(int *x, int *y)
{
    int qx, qy;
    unsigned b;
    if (!gfx_mouse_queue_pop(&qx, &qy, &b)) {
        qx = g_x;
        qy = g_y;
        b = g_buttons;
    }
    if (qx < g_clip.left)
        qx = g_clip.left;
    if (qy < g_clip.top)
        qy = g_clip.top;
    if (qx > g_clip.right - 1)
        qx = g_clip.right - 1;
    if (qy > g_clip.bottom - 1)
        qy = g_clip.bottom - 1;
    if (x)
        *x = qx;
    if (y)
        *y = qy;
    return b;
}

/* 0x4b8e2a */
unsigned gfx_mouse_get_buttons(void) { return g_buttons; }

/* 0x4b8e3f */
void gfx_mouse_set_clip_rect(GfxRect *prev, const GfxRect *clip)
{
    if (!clip) {
        if (prev)
            *prev = g_saved_clip;
        return;
    }
    GfxRect old = g_clip;
    g_saved_clip = g_clip;
    g_clip.left = clip->left < 1 ? 0 : clip->left;
    g_clip.top = clip->top < 1 ? 0 : clip->top;
    g_clip.right = clip->right <= gfx_screen_width() - 1 ? clip->right : gfx_screen_width() - 1;
    g_clip.bottom = clip->bottom <= gfx_screen_height() - 1 ? clip->bottom : gfx_screen_height() - 1;
    if (prev)
        *prev = old;
}

/* 0x4b8f1f */
void gfx_mouse_get_clip_rect(GfxRect *out)
{
    if (out)
        *out = g_clip;
}

/* 0x4b8f57 */
GfxMouseHandler gfx_mouse_set_handler(GfxMouseHandler fn)
{
    GfxMouseHandler old = g_handler;
    g_handler = fn;
    return old;
}

/* 0x4b8f90 */
void gfx_mouse_on_move(int x, int y)
{
    g_x = clampi(x, g_clip.left, g_clip.right);
    g_y = clampi(y, g_clip.top, g_clip.bottom);
}

/* 0x4b901d / 0x4b9049 / 0x4b9075 (down), 0x4b90a1 / 0x4b90cd / 0x4b90f9 (up) */
void gfx_mouse_on_button(unsigned button, bool down)
{
    if (down)
        g_buttons |= button;
    else
        g_buttons &= ~button;
    queue_push(g_x, g_y, g_buttons);
}

/* 0x4b92af: SetCursorPos(client origin + (x, y)). */
void gfx_mouse_set_pos(int x, int y)
{
    g_x = x;
    g_y = y;
    plat_mouse_warp(x, y);
}

/* 0x4b92ff: GetCursorPos, in screen coordinates. The port has no screen
 * coordinates; it reports the last framebuffer position. */
void gfx_mouse_get_screen_pos(int *x, int *y)
{
    if (x)
        *x = g_x;
    if (y)
        *y = g_y;
}

/* --- cursors -------------------------------------------------------------- */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Decodes the image of an RT_CURSOR or RT_ICON resource to w * h RGBA
 * (malloc'd): BITMAPINFOHEADER with doubled height, color table, XOR bitmap,
 * 1-bit AND mask, rows bottom-up. Screen-inverting pixels (AND 1, XOR
 * non-black) come out opaque black. */
static uint8_t *decode_dib(const uint8_t *bih, uint32_t len, int *out_w, int *out_h)
{
    const uint8_t *d = bih;
    if (len < 40)
        return NULL;
    int32_t w = (int32_t)rd32(bih + 4), h = (int32_t)rd32(bih + 8) / 2;
    unsigned bpp = rd16(bih + 14);
    uint32_t ncol = rd32(bih + 32);
    if (w <= 0 || h <= 0 || w > 256 || h > 256 || (bpp != 1 && bpp != 4 && bpp != 8))
        return NULL;
    if (ncol == 0)
        ncol = 1u << bpp;
    uint32_t xor_stride = ((uint32_t)w * bpp + 31) / 32 * 4, and_stride = ((uint32_t)w + 31) / 32 * 4;
    const uint8_t *pal = bih + rd32(bih);
    const uint8_t *xorb = pal + ncol * 4, *andb = xorb + xor_stride * (uint32_t)h;
    if ((size_t)(andb + and_stride * (uint32_t)h - d) > len)
        return NULL;
    uint8_t *rgba = calloc((size_t)w * (size_t)h, 4);
    if (!rgba)
        return NULL;
    for (int y = 0; y < h; y++) {
        const uint8_t *xr = xorb + (uint32_t)(h - 1 - y) * xor_stride;
        const uint8_t *ar = andb + (uint32_t)(h - 1 - y) * and_stride;
        for (int x = 0; x < w; x++) {
            unsigned idx;
            if (bpp == 1)
                idx = xr[x >> 3] >> (7 - (x & 7)) & 1;
            else if (bpp == 4)
                idx = xr[x >> 1] >> (x & 1 ? 0 : 4) & 15;
            else
                idx = xr[x];
            int and_bit = ar[x >> 3] >> (7 - (x & 7)) & 1;
            uint8_t *o = rgba + ((size_t)y * w + x) * 4;
            const uint8_t *c = idx < ncol ? pal + idx * 4 : pal;
            bool black = c[0] == 0 && c[1] == 0 && c[2] == 0;
            if (and_bit && black)
                continue; /* transparent */
            if (and_bit) {
                o[3] = 255; /* inverted: drawn black */
            } else {
                o[0] = c[2];
                o[1] = c[1];
                o[2] = c[0];
                o[3] = 255;
            }
        }
    }
    *out_w = w;
    *out_h = h;
    return rgba;
}

/* An RT_CURSOR resource: hotspot (2 x u16), then the image. */
static PlatCursor *decode_cursor(const uint8_t *d, uint32_t len)
{
    int w, h;
    uint8_t *rgba = len >= 4 ? decode_dib(d + 4, len - 4, &w, &h) : NULL;
    if (!rgba)
        return NULL;
    PlatCursor *pc = plat_cursor_create(rgba, w, h, rd16(d), rd16(d + 2));
    free(rgba);
    return pc;
}

/* 0x4b9125: LoadCursorA(hInstance, MAKEINTRESOURCE(id)). */
PlatCursor *gfx_cursor_load(unsigned id)
{
    uint32_t len;
    if (!exe_res_open("ALIFE.EXE"))
        return NULL;
    const uint8_t *grp = exe_res_find(EXE_RT_GROUP_CURSOR, id, &len);
    /* NEWHEADER (6 bytes) + first 14-byte entry; its last u16 is the
     * RT_CURSOR id. */
    if (!grp || len < 6 + 14 || rd16(grp + 4) == 0)
        return NULL;
    const uint8_t *cur = exe_res_find(EXE_RT_CURSOR, rd16(grp + 6 + 12), &len);
    return cur ? decode_cursor(cur, len) : NULL;
}

/* Port-only: the original's window class takes LoadIconA(hInstance, id)
 * (app_register_class, 0x4cd892); the port gives that icon to the window
 * (the browser tab's favicon on the web). Group icon `id`'s first image. */
void gfx_window_icon_load(unsigned id)
{
    uint32_t len;
    if (!exe_res_open("ALIFE.EXE"))
        return;
    const uint8_t *grp = exe_res_find(EXE_RT_GROUP_ICON, id, &len);
    /* NEWHEADER (6 bytes) + first 14-byte entry; its last u16 is the
     * RT_ICON id. */
    if (!grp || len < 6 + 14 || rd16(grp + 4) == 0)
        return;
    const uint8_t *ico = exe_res_find(EXE_RT_ICON, rd16(grp + 6 + 12), &len);
    int w, h;
    uint8_t *rgba = ico ? decode_dib(ico, len, &w, &h) : NULL;
    if (rgba)
        plat_window_set_icon(rgba, w, h);
    free(rgba);
}

/* 0x4b9164 */
void gfx_cursor_destroy(PlatCursor *c)
{
    if (c == g_cursor)
        g_cursor = NULL;
    plat_cursor_destroy(c);
}

/* 0x4b9179 */
bool gfx_cursor_show(bool show) { return plat_cursor_show(show); }

/* 0x4b9274 */
PlatCursor *gfx_cursor_get(void) { return g_cursor; }

/* 0x4b928a */
void gfx_cursor_set(PlatCursor *c)
{
    g_cursor = c;
    plat_cursor_set(c);
}

/* main_wnd_proc 0x4cb7ad: WM_MOUSEMOVE (0x200) -> ms_on_move; button
 * down/up (0x201-0x208) -> SetCapture/ReleaseCapture + ms_on_button;
 * WM_SETCURSOR (0x20) re-applies the current cursor. */
bool gfx_mouse_handle_event(const PlatEvent *ev)
{
    static const unsigned k_buttons[] = {0, GFX_MOUSE_LEFT, GFX_MOUSE_MIDDLE, GFX_MOUSE_RIGHT};
    switch (ev->type) {
    case PLAT_EV_MOUSE_MOVE:
        gfx_mouse_on_move(ev->x, ev->y);
        return true;
    case PLAT_EV_MOUSE_DOWN:
    case PLAT_EV_MOUSE_UP:
        if (ev->button >= 1 && ev->button <= 3) {
            /* Windows reports the position with the click; SDL may not
             * have sent a motion event for it yet. */
            gfx_mouse_on_move(ev->x, ev->y);
            gfx_mouse_on_button(k_buttons[ev->button], ev->type == PLAT_EV_MOUSE_DOWN);
        }
        return true;
    default:
        return false;
    }
}
