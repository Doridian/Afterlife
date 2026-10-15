/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The File menu's save / load / delete dialogs. See ui/slotdlg.h.
 */
#include "ui/slotdlg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "game/shell.h"
#include "game/state.h"
#include "game/startup.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/controls.h"
#include "ui/news.h"
#include "ui/views.h"

/* the layout (0x66ae80..0x66aecc) */
static const GfxRect k_list_rect = {21, 232, 50, 189};
static const GfxRect k_title_rect = {23, 230, 22, 37};
static const GfxRect k_thumb_rect = {243, 346, 22, 99};
static const GfxRect k_year_rect = {243, 346, 105, 114};
static const GfxRect k_diff_rect = {243, 346, 115, 124};

#define BG 0x13
#define FG 0x1a
#define INFO_BG 0x65
#define BUTTON_COLORS 0x13131313u

/* texts */
#define TEXT_SAVE_GAME "Save a Game"                                                 /* 0x66c090 */
#define TEXT_SAVE_SCENARIO "Save a Scenario"                                         /* 0x66c09c */
#define TEXT_SAVE "Save"                                                             /* 0x66c0ac */
#define TEXT_CANCEL_SAVE "Cancel"                                                    /* 0x66c0b4 */
#define TEXT_OVERWRITE_BUTTONS "&Yes\0~&No\0"                                        /* 0x66c0bc */
#define TEXT_OVERWRITE "Overwrite existing save game?"                               /* 0x66c0c8 */
#define TEXT_OVERWRITE_TITLE "Save A Game"                                           /* 0x66c0e8 */
#define TEXT_YEAR "Year:  %lu"                                                       /* 0x66bff8 */
#define TEXT_EASY "Difficulty:  EASY"                                                /* 0x66c004 */
#define TEXT_MEDIUM "Difficulty:  MEDIUM"                                            /* 0x66c018 */
#define TEXT_HARD "Difficulty:  HARD"                                                /* 0x66c02c */
#define TEXT_LOAD_GAME "Load a Game"                                                 /* 0x66c214 */
#define TEXT_LOAD_SCENARIO "Load a Scenario"                                         /* 0x66c220 */
#define TEXT_LOAD "Load"                                                             /* 0x66c230 */
#define TEXT_CANCEL_LOAD "Cancel"                                                    /* 0x66c238 */
#define TEXT_DELETE_TITLE "Delete a Game"                                            /* 0x66c364 */
#define TEXT_DELETE "Delete"                                                         /* 0x66c374 */
#define TEXT_CANCEL_DELETE "Cancel"                                                  /* 0x66c37c */
#define TEXT_DELETE_BUTTONS "&Yes\0~&No\0"                                           /* 0x66c384 */
#define TEXT_DELETE_ASK "Delete game?"                                               /* 0x66c390 */
#define TEXT_DELETE_ASK_TITLE "Delete A Game"                                        /* 0x66c3a0 */
#define TEXT_HELP_CLOSE "This button CLOSES the window."                             /* 0x66c3b0 */
#define TEXT_HELP_LOAD_GAME "This button LOADS the selected savegame from disk."     /* 0x66c3d0 */
#define TEXT_HELP_LOAD_SCENARIO "This button LOADS the selected scenario from disk." /* 0x66c404 */
#define TEXT_HELP_SAVE "This button SAVES the selected savegame to disk."            /* 0x66c438 */
#define TEXT_HELP_DELETE "This button permanently DELETES the selected savegame."    /* 0x66c46c */
#define TEXT_HELP_TITLE                                                                                                \
    "This edit control allows you to type in a DESCRIPTION for the selected savegame." /* 0x66c4a4 */
#define TEXT_HELP_UNKNOWN "I have no idea what this button does."                      /* 0x66c4f8 */

#define KEY_ENTER 0x0d
#define KEY_ESC 0x1b
#define KEY_DELETE 0x2e
#define MSG_CLOSE 0x400

static struct {
    WmWindow *win, *list, *text; /* 0x66ae74 / 0x66ae78 / 0x66ae7c */
    int mode;                    /* save_mode_game 0x66aeed */
    bool no_help;                /* 0x66aee0 */
    bool choose;                 /* 0x66aedc: pick only (the title) */
    uint8_t *remap;              /* 0x66aee8: thumbnail colours (resource 0x14b6) */
    bool done;                   /* 0x66aed4 */
    int result;                  /* 0x66aed8 */
    int chosen;                  /* 0x65006c */
    uint32_t years[SLOTDLG_SLOTS];  /* 0x4f3df0: per list line */
    uint8_t levels[SLOTDLG_SLOTS];  /* 0x4f3d88 */
} sd;

WmWindow *slotdlg_window(void) { return sd.win; }
WmWindow *slotdlg_list(void) { return sd.list; }
WmWindow *slotdlg_text(void) { return sd.text; }

static intptr_t list_send(unsigned msg, intptr_t a, intptr_t b) { return sd.list ? wm_send(sd.list, sd.list, msg, a, b) : -1; }

/* ======================================================================== */
/* Slot files                                                                */
/* ======================================================================== */

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* save_open_slot 0x47fca7's file, for reading */
bool slotdlg_slot_file(int mode, unsigned slot, char *out, size_t n)
{
    shell_slot_path(mode, slot, out, n);
    if (file_exists(out))
        return true;
    char name[32], alt[1100];
    snprintf(name, sizeof name, mode == 2 ? "ascen%03u.asc" : "agame%03u.alf", slot);
    /* the CD (scenarios), or the data dir as the save folder, matched
     * case-insensitively */
    char dir[1100];
    snprintf(dir, sizeof dir, "%s", out);
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    if ((mode == 2 || strcmp(dir, plat_data_dir()) == 0) && plat_data_path(name, alt, sizeof alt) &&
        file_exists(alt)) {
        snprintf(out, n, "%s", alt);
        return true;
    }
    shell_slot_path(mode, slot, out, n);
    return false;
}

static int read_u8(FILE *f)
{
    int c = fgetc(f);
    return c == EOF ? 0 : c;
}

static uint32_t read_u32(FILE *f)
{
    uint8_t b[4] = {0};
    if (fread(b, 1, 4, f) != 4)
        return 0;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* the header: "ALSV", the version as 4 digits, the kind, the time
 * stamp, the year, the title, the difficulty (version > 7219), the
 * thumbnail's size (version > 7025) */
static bool read_header(FILE *f, SlotInfo *info, char magic[5])
{
    memset(info, 0, sizeof *info);
    char v[5] = {0};
    if (fread(magic, 1, 4, f) != 4 || fread(v, 1, 4, f) != 4)
        return false;
    magic[4] = '\0';
    info->version = atoi(v);
    read_u8(f);
    info->timestamp = read_u32(f);
    info->year = read_u32(f);
    int i;
    for (i = 0; i < 0xff; i++) {
        info->title[i] = (char)read_u8(f);
        if (!info->title[i])
            break;
    }
    info->title[i] = '\0';
    if (i == 0xff) /* (the original stops at 255 and goes on from there) */
        while (read_u8(f))
            ;
    info->difficulty = 1;
    if (info->version > 0x1c33) {
        int lo = read_u8(f);
        info->difficulty = (uint16_t)(lo | read_u8(f) << 8);
    }
    if (info->version > 0x1b71) {
        info->thumb_w = read_u32(f);
        info->thumb_h = read_u32(f);
    }
    return true;
}

bool slotdlg_read_info(const char *path, SlotInfo *info)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char magic[5];
    bool ok = read_header(f, info, magic);
    fclose(f);
    return ok;
}

/* 0x480a10: the list of the slots that exist; returns the line of the
 * newest one (-1 on failure) */
static int fill_list(int mode)
{
    memset(sd.years, 0, sizeof sd.years);
    memset(sd.levels, 0, sizeof sd.levels);
    int best = 0, n = 0;
    uint32_t newest = 0;
    for (unsigned slot = 0; slot < SLOTDLG_SLOTS; slot++) {
        char path[1100];
        if (!slotdlg_slot_file(mode, slot, path, sizeof path))
            continue;
        SlotInfo info;
        if (!slotdlg_read_info(path, &info))
            continue;
        if (newest <= info.timestamp) {
            newest = info.timestamp;
            best = (int)list_send(CTL_LIST_COUNT, 0, 0);
        }
        sd.years[n] = info.year;
        sd.levels[n] = (uint8_t)info.difficulty;
        n++;
        list_send(CTL_LIST_ADD, (intptr_t)info.title, (intptr_t)slot);
    }
    return best;
}

/* 0x480db8: the first slot with no file, -1 if all 100 are taken */
static int free_slot(void)
{
    for (unsigned slot = 0; slot < SLOTDLG_SLOTS; slot++) {
        char path[1100];
        if (!slotdlg_slot_file(1, slot, path, sizeof path))
            return (int)slot;
    }
    return -1;
}

/* ======================================================================== */
/* Drawing                                                                   */
/* ======================================================================== */

static void fill(const GfxRect *r, uint8_t color)
{
    gfx_fill_rect(sd.win->surface, r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1, color);
}

static void font_colors(uint32_t font, unsigned fg, unsigned bg)
{
    const unsigned c[2] = {fg, bg};
    gfx_font_select(font);
    gfx_font_set_colors(font, 1, 2, c);
}

/* 0x4806e9: one line of text, cut at the rectangle's right edge */
static void draw_line(const char *s, const GfxRect *r)
{
    fill(r, BG);
    int x = r->left;
    for (; *s && x <= r->right; s++) {
        gfx_font_draw_char(sd.win->surface, (uint8_t)*s, x, r->top, r);
        x += gfx_font_char_width((uint8_t)*s);
    }
}

/* 0x4807c5: the year and difficulty of the selected line, or (current)
 * of the game being played */
static void draw_info(bool current)
{
    if (!sd.win || !sd.list)
        return;
    unsigned i = (unsigned)list_send(CTL_LIST_GET_SEL, 0, 0);
    if (i > 99)
        i = 99;
    uint32_t year = current ? g_state.year : sd.years[i];
    unsigned level = current ? g_state.hdr.difficulty : sd.levels[i];
    font_colors(500, FG, BG);
    char buf[256];
    /* "Year: %lu" */
    const char *fmt = TEXT_YEAR, *pct = strchr(fmt, '%');
    if (pct && strncmp(pct, "%lu", 3) == 0 && !strchr(pct + 3, '%'))
        snprintf(buf, sizeof buf, "%.*s%u%s", (int)(pct - fmt), fmt, (unsigned)year, pct + 3);
    else
        snprintf(buf, sizeof buf, "%u", (unsigned)year);
    fill(&k_year_rect, INFO_BG);
    GfxRect r = k_year_rect;
    gfx_font_draw_text(sd.win->surface, r.left, r.top, &k_year_rect, &r, buf, GFX_TEXT_NOJUSTIFY);
    wm_redraw(sd.win, &k_year_rect);
    if (sd.mode == 1) {
        snprintf(buf, sizeof buf, "%s", level == 1 ? TEXT_EASY : level == 2 ? TEXT_MEDIUM : TEXT_HARD);
        fill(&k_diff_rect, INFO_BG);
        r = k_diff_rect;
        gfx_font_draw_text(sd.win->surface, r.left, r.top, &k_diff_rect, &r, buf, GFX_TEXT_NOJUSTIFY);
        wm_redraw(sd.win, &k_diff_rect);
    }
}

/* 0x47fff4: the slot's thumbnail */
static void draw_thumb(int slot)
{
    char path[1100];
    if (!sd.win || slot < 0 || !slotdlg_slot_file(sd.mode, (unsigned)slot, path, sizeof path))
        return;
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    SlotInfo info;
    char magic[5];
    if (len >= 0x100 && read_header(f, &info, magic) && strcmp(magic, "ALSV") == 0) {
        fill(&k_thumb_rect, BG);
        long pos = ftell(f);
        uint32_t w = info.thumb_w, h = info.thumb_h;
        int rw = k_thumb_rect.right - k_thumb_rect.left + 1, rh = k_thumb_rect.bottom - k_thumb_rect.top + 1;
        if (info.version > 0x1b71 && (uint64_t)w * h < (uint64_t)(len - pos) && w <= (uint32_t)rw &&
            h <= (uint32_t)rh) {
            GfxSurface *s = sd.win->surface;
            for (uint32_t y = 0; y < h; y++) {
                uint8_t *row = s->pixels + (size_t)(k_thumb_rect.top + (int)y) * (size_t)s->stride +
                               (size_t)k_thumb_rect.left;
                if (fread(row, 1, w, f) != w)
                    break;
                if (sd.remap)
                    for (uint32_t x = 0; x < w; x++)
                        row[x] = sd.remap[row[x]];
            }
        }
    }
    fclose(f);
}

void slotdlg_shrink(GfxSurface *dst, const GfxRect *to, const GfxSurface *src, const GfxRect *from)
{
    int sw = from->right - from->left + 1, sh = from->bottom - from->top + 1;
    int dw = to->right - to->left + 1, dh = to->bottom - to->top + 1;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    size_t total = (size_t)src->stride * (size_t)src->height;
    size_t row = (size_t)from->top * (size_t)src->stride + (size_t)from->left;
    uint32_t fy = 0;
    for (int y = 0; y < dh; y++) {
        size_t p = row;
        uint32_t fx = 0;
        uint8_t *out = dst->pixels + (size_t)(to->top + y) * (size_t)dst->stride + (size_t)to->left;
        for (int left = dw - 1; left >= 0; left--) {
            if (p >= total)
                break;
            uint8_t c = src->pixels[p];
            /* (the original always takes the byte before, and the byte
             * after except at the last-but-one pixel) */
            uint8_t l = p > 0 ? src->pixels[p - 1] : c;
            uint8_t r = left == 1 || p + 1 >= total ? c : src->pixels[p + 1];
            *out++ = r == l ? l : r == c ? c : c == l ? l : c;
            fx += (uint32_t)(sw * 0x10000) / (uint32_t)dw;
            p += fx >> 16;
            fx &= 0xffff;
        }
        fy += (uint32_t)(sh * 0x10000) / (uint32_t)dh;
        row += (size_t)src->stride * (fy >> 16);
        fy &= 0xffff;
    }
}

/* 0x49513c: right-click help */
static void help(WmWindow *c)
{
    if (sd.no_help || !c || !sd.win)
        return;
    int id = (int)wm_get_data(c, 0);
    int x = ((c->rect.right - c->rect.left) >> 1) + sd.win->rect.left + c->rect.left;
    int y = ((c->rect.bottom - c->rect.top) >> 1) + sd.win->rect.top + c->rect.top;
    const char *t = TEXT_HELP_UNKNOWN;
    switch (id) {
    case SLOTDLG_ID_TITLE:
        t = TEXT_HELP_TITLE;
        break;
    case SLOTDLG_ID_LOAD:
        t = sd.mode == 1 ? TEXT_HELP_LOAD_GAME : TEXT_HELP_LOAD_SCENARIO;
        break;
    case SLOTDLG_ID_SAVE:
        t = TEXT_HELP_SAVE;
        break;
    case SLOTDLG_ID_CANCEL:
        t = TEXT_HELP_CLOSE;
        break;
    case SLOTDLG_ID_DELETE:
        t = TEXT_HELP_DELETE;
        break;
    default:
        break;
    }
    help_balloon_show(x, y, 0x78, t, true, true);
}

/* ======================================================================== */
/* The common window                                                         */
/* ======================================================================== */

static WmWindow *button(int x, int y, uint32_t id, uint32_t bmp_id, const char *label)
{
    GfxSurface *bmp = gfx_load_bmp_resource(bmp_id, NULL);
    if (!bmp)
        return NULL;
    GfxRect r = {x, x + bmp->width - 1, y, y + bmp->height - 1};
    WmWindow *b = ctl_button_create(sd.win, &r, id, CTL_STYLE_BITMAP, bmp);
    if (!b) {
        gfx_surface_destroy(bmp);
        return NULL;
    }
    if (label)
        ctl_button_set_label(b, label, 500, BUTTON_COLORS, true);
    return b;
}

/* 0x400: the dialog closes */
static void close_dialog(WmWindow *w)
{
    (void)w;
    wm_show(sd.win, false);
    sd.done = true;
    g_game_paused = 0;
    wm_activate(g_main_window);
}

static void cancel(WmWindow *w)
{
    sd.result = 0;
    sd.mode = 1;
    wm_post(w, w, MSG_CLOSE, 0, 0);
}

/* the window, its picture and the close button; NULL on failure */
static bool open_window(WmProc proc, const char *title)
{
    GfxSurface *bg = gfx_load_bmp_resource(0x25d, NULL);
    if (!bg)
        return false;
    GfxRect r = {1, bg->width, 0x28, bg->height + 0x27};
    sd.done = false;
    sd.result = 1;
    sd.win = wm_create_window(g_main_window, &r, proc, WM_F_HITCHILDREN | WM_F_TOPMOST, 0);
    if (!sd.win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(sd.win, title);
    gfx_blit(sd.win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    list_send(CTL_LIST_SCROLL, 0, 0);
    return true;
}

/* the modal part every dialog ends with */
static int run_dialog(void)
{
    views_draw_title(sd.win);
    wm_center(sd.win);
    wm_set_capture(sd.win);
    wm_redraw(sd.win, NULL);
    wm_raise(sd.win);
    wm_show(sd.win, true);
    wm_activate(sd.win);
    shell_busy_cursor(false);
    while (!sd.done)
        sys_pump();
    wm_release_capture();
    return sd.result;
}

static void destroy_window(void)
{
    if (sd.win) {
        wm_show(sd.win, false);
        wm_destroy(sd.win);
    }
    sd.win = sd.list = sd.text = NULL;
    free(sd.remap);
    sd.remap = NULL;
}

/* on failure while building: 0x400 and back */
static int fail(void)
{
    if (sd.win)
        close_dialog(sd.win);
    destroy_window();
    shell_busy_cursor(false);
    return 0;
}

/* ======================================================================== */
/* Save (0x480ea4, 0x48137d)                                                 */
/* ======================================================================== */

static int save_to(int slot, const char *title)
{
    char path[1100];
    shell_slot_path(sd.mode, (unsigned)slot, path, sizeof path);
    return shell_save_game_to(path, title ? title : "") ? 1 : 0;
}

/* 0x48137d's 0x8000: save to the selected slot (asking first) or a new one */
static int save_selected(WmWindow *w, WmWindow *field)
{
    int slot = (int)list_send(CTL_LIST_GET_DATA, -1, 0);
    const char *title = field ? ctl_text_get(field) : "";
    int r = 1;
    if (slot == -1) {
        slot = free_slot();
        if (slot == -1) {
            r = 0;
            sd.mode = 1;
        } else {
            r = save_to(slot, title);
        }
        wm_post(w, w, MSG_CLOSE, 0, 0);
    } else {
        GfxRect row = {0, 0, 0, 0};
        list_send(CTL_LIST_ROW_RECT, (intptr_t)&row, -1);
        int x = row.left, y = row.top;
        wm_point_to_screen(sd.list, &x, &y);
        if (ctl_message_box(x, y, TEXT_OVERWRITE_TITLE, TEXT_OVERWRITE, TEXT_OVERWRITE_BUTTONS, 0) == 1) {
            slot = (int)list_send(CTL_LIST_GET_DATA, -1, 0);
            r = save_to(slot, sd.text ? ctl_text_get(sd.text) : title);
            wm_post(w, w, MSG_CLOSE, 0, 0);
        } else {
            wm_set_capture(w);
        }
    }
    sd.result = r;
    return r;
}

static void show_selected_title(void)
{
    char text[256];
    if (list_send(CTL_LIST_GET_TEXT, -1, (intptr_t)text) == -1 || !text[0])
        wm_send(sd.text, sd.text, CTL_TEXT_MSG_SET, 0, 0);
    else
        wm_send(sd.text, sd.text, CTL_TEXT_MSG_SET, (intptr_t)text, 0);
}

static intptr_t save_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_LBUTTONDOWN:
        wm_raise(w);
        break;
    case WM_MSG_CREATE: {
        g_game_paused = 1;
        sd.list = ctl_list_create(w, &k_list_rect, CTL_LIST_ALLOW_END, SLOTDLG_ID_LIST, SLOTDLG_ID_LIST + 1, 500, BG,
                                  FG);
        GfxRect r = {k_title_rect.left, k_title_rect.right, k_title_rect.top,
                     k_title_rect.top + gfx_font_height(0x1f5) + 1};
        sd.text = ctl_text_create(w, &r, SLOTDLG_ID_TITLE, BG, FG, BG, FG, BG, 0x1f5,
                                  CTL_TEXT_RCLICK | CTL_TEXT_FOCUS);
        if (!sd.text || !sd.list)
            return 0;
        if (fill_list(1) == -1)
            return 0;
        list_send(CTL_LIST_SELECT, list_send(CTL_LIST_COUNT, 0, 0), 0); /* the empty line: a new slot */
        show_selected_title();
        break;
    }
    case WM_MSG_CTL_CLICK: {
        WmWindow *p = w->parent;
        wm_raise(p);
        uint32_t id = ctl_get_id(w);
        if (id == SLOTDLG_ID_SAVE)
            wm_send(p, p, CTL_TEXT_MSG_ENTER, (intptr_t)sd.text, 0);
        else if (id == SLOTDLG_ID_CANCEL)
            cancel(p);
        break;
    }
    case WM_MSG_CTL_RDOWN:
        help(w);
        break;
    case MSG_CLOSE:
        close_dialog(w);
        break;
    case WM_MSG_CHAR:
        if (a != KEY_ENTER && sd.text)
            wm_send(sd.text, sd.text, WM_MSG_CHAR, a, b);
        break;
    case WM_MSG_KEYUP:
        if (a == KEY_ENTER)
            wm_send(w, w, CTL_TEXT_MSG_ENTER, (intptr_t)sd.text, 0);
        else if (a == KEY_ESC)
            cancel(w);
        else if (sd.list)
            wm_send(sd.list, sd.list, WM_MSG_KEYUP, a, b);
        break;
    case CTL_TEXT_MSG_ENTER:
        return save_selected(w, (WmWindow *)a);
    case CTL_LIST_MSG_SELECTED:
        show_selected_title();
        draw_info(true);
        break;
    case CTL_LIST_MSG_DBLCLK:
        wm_send(w, w, CTL_TEXT_MSG_ENTER, (intptr_t)sd.text, 0);
        break;
    case CTL_TEXT_MSG_RCLICK:
        help((WmWindow *)a);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

int slotdlg_save(int mode)
{
    if (sd.win)
        return 1;
    shell_busy_cursor(true);
    sd.mode = mode;
    sd.no_help = false;
    sd.choose = false;
    if (!open_window(save_proc, mode == 1 ? TEXT_SAVE_GAME : TEXT_SAVE_SCENARIO))
        return fail();
    if (sd.text)
        wm_send(sd.text, sd.text, CTL_TEXT_MSG_DRAW, 0, 0);
    GfxSurface *close = gfx_load_bmp_resource(0x259, NULL);
    if (!close)
        return fail();
    GfxRect r = {0, close->width - 1, 0, close->height - 1};
    sd.win->close_button = ctl_button_create(sd.win, &r, SLOTDLG_ID_CANCEL, CTL_STYLE_BITMAP, close);
    if (!button(0x10b, 0x8b, SLOTDLG_ID_SAVE, 0x227, TEXT_SAVE) ||
        !button(0x10b, 0xa7, SLOTDLG_ID_CANCEL, 0x227, TEXT_CANCEL_SAVE))
        return fail();
    views_draw_title(sd.win);
    fill(&k_thumb_rect, BG);
    if (g_main_window && g_main_window->surface)
        slotdlg_shrink(sd.win->surface, &k_thumb_rect, g_main_window->surface, &g_main_window->client);
    draw_info(true);
    int r2 = run_dialog();
    destroy_window();
    return r2;
}

/* ======================================================================== */
/* Load (0x489e90, 0x494112, 0x48a3dc)                                       */
/* ======================================================================== */

/* 0x48a3dc's 0x8064 (and 0x494a93's): the line's title, picture and info */
static void show_slot(WmWindow *w, WmWindow *list, intptr_t index, bool clear_empty)
{
    char text[256];
    if (!sd.win) /* (while the list is filled during the window's creation) */
        return;
    font_colors(0x1f5, FG, BG);
    intptr_t n = wm_send(list, list, CTL_LIST_GET_TEXT, index, (intptr_t)text);
    if (n == -1) {
        if (!clear_empty)
            return;
        fill(&k_title_rect, BG);
    } else {
        draw_line(text, &k_title_rect);
    }
    wm_redraw(w, &k_title_rect);
    int slot = (int)wm_send(list, list, CTL_LIST_GET_DATA, -1, 0);
    if (slot != -1 || !clear_empty)
        draw_thumb(slot);
    wm_redraw(w, &k_thumb_rect);
    draw_info(false);
}

static void load_selected(WmWindow *w)
{
    int slot = (int)list_send(CTL_LIST_GET_DATA, -1, 0);
    if (slot == -1) {
        sd.result = 0;
        return;
    }
    if (!sd.choose) {
        wm_show(w, false);
        char path[1100];
        slotdlg_slot_file(sd.mode, (unsigned)slot, path, sizeof path);
        sd.result = shell_load_game_from(path) ? 1 : 0;
    } else {
        sd.chosen = slot;
    }
    sd.choose = false;
    wm_post(w, w, MSG_CLOSE, 0, 0);
}

static intptr_t load_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_LBUTTONDOWN:
        wm_raise(w);
        break;
    case WM_MSG_CREATE:
        g_game_paused = 1;
        sd.list = ctl_list_create(w, &k_list_rect, 0, SLOTDLG_ID_LIST, SLOTDLG_ID_LIST + 1, 500, BG, FG);
        if (!sd.list)
            return 0;
        break;
    case WM_MSG_CTL_CLICK: {
        WmWindow *p = w->parent;
        wm_raise(p);
        uint32_t id = ctl_get_id(w);
        if (id == SLOTDLG_ID_LOAD)
            wm_send(p, p, WM_MSG_KEYUP, KEY_ENTER, 0);
        else if (id == SLOTDLG_ID_CANCEL)
            cancel(p);
        break;
    }
    case WM_MSG_CTL_RDOWN:
        help(w);
        break;
    case MSG_CLOSE:
        close_dialog(w);
        break;
    case WM_MSG_KEYUP:
        if (a == KEY_ENTER)
            load_selected(w);
        else if (a == KEY_ESC)
            cancel(w);
        else if (sd.list)
            wm_send(sd.list, sd.list, WM_MSG_KEYUP, a, b);
        break;
    case CTL_LIST_MSG_SELECTED:
        show_slot(w, (WmWindow *)a, b, false);
        break;
    case CTL_LIST_MSG_DBLCLK:
        wm_send(w, w, WM_MSG_KEYUP, KEY_ENTER, 0);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

static int open_load(int mode, bool choose)
{
    sd.mode = mode;
    sd.choose = false;
    /* (0x494112 has its own copies of the texts) */
    if (!open_window(load_proc, choose ? (mode == 1 ? "Load a Game" /* 0x66c338 */ : "Load a Scenario" /* 0x66c344 */)
                                : mode == 1 ? TEXT_LOAD_GAME
                                            : TEXT_LOAD_SCENARIO))
        return fail();
    int best = fill_list(mode);
    list_send(CTL_LIST_SELECT, mode == 1 ? best : 0, 0);
    sd.choose = choose;
    list_send(CTL_LIST_SCROLL, 0, 0);
    GfxSurface *close = gfx_load_bmp_resource(0x259, NULL);
    if (!close)
        return fail();
    GfxRect r = {0, close->width - 1, 0, close->height - 1};
    sd.win->close_button = ctl_button_create(sd.win, &r, SLOTDLG_ID_CANCEL, CTL_STYLE_BITMAP, close);
    if (!button(0x10b, 0x8b, SLOTDLG_ID_LOAD, 0x227, choose ? "Load" /* 0x66c354 */ : TEXT_LOAD) ||
        !button(0x10b, 0xa7, SLOTDLG_ID_CANCEL, 0x227, choose ? "Cancel" /* 0x66c35c */ : TEXT_CANCEL_LOAD))
        return fail();
    views_draw_title(sd.win);
    wm_send(sd.win, sd.win, CTL_LIST_MSG_SELECTED, (intptr_t)sd.list, -1);
    int res = run_dialog();
    sd.choose = false;
    destroy_window();
    return res;
}

int slotdlg_load(int mode)
{
    if (sd.win)
        return 1;
    shell_busy_cursor(true);
    sd.no_help = !g_game_in_progress;
    return open_load(mode, false);
}

bool slotdlg_choose(int mode, int *slot)
{
    if (sd.win)
        return false;
    sd.no_help = true;
    /* the thumbnails' colours for the title's palette (247 bytes: the
     * original reads past the end for colours above that; the port keeps
     * those) */
    long len = rc_resource_size(0x14b6);
    PlatFile *f = len > 0 ? rc_open_resource(0x14b6) : NULL;
    if (!f)
        return false;
    sd.remap = malloc(256);
    if (len > 256)
        len = 256;
    bool ok = sd.remap && plat_file_read(f, sd.remap, 1, (size_t)len) == (size_t)len;
    plat_file_close(f);
    if (!ok) {
        free(sd.remap);
        sd.remap = NULL;
        return false;
    }
    for (long i = len; i < 256; i++)
        sd.remap[i] = (uint8_t)i;
    sd.chosen = -1;
    int r = open_load(mode, true);
    if (slot)
        *slot = sd.chosen;
    return r != 0 && sd.chosen != -1;
}

/* ======================================================================== */
/* Delete (0x4946bd, 0x494a93)                                               */
/* ======================================================================== */

static void delete_selected(WmWindow *w)
{
    GfxRect row = {0, 0, 0, 0};
    list_send(CTL_LIST_ROW_RECT, (intptr_t)&row, -1);
    int x = row.left, y = row.top;
    wm_point_to_screen(sd.list, &x, &y);
    if (list_send(CTL_LIST_COUNT, 0, 0) == 0)
        return;
    if (ctl_message_box(x, y, TEXT_DELETE_ASK_TITLE, TEXT_DELETE_ASK, TEXT_DELETE_BUTTONS, 0) == 1) {
        int slot = (int)list_send(CTL_LIST_GET_DATA, -1, 0);
        wm_activate(w);
        unsigned count = (unsigned)list_send(CTL_LIST_COUNT, 0, 0);
        unsigned sel = (unsigned)list_send(CTL_LIST_GET_SEL, 0, 0);
        if (list_send(CTL_LIST_DELETE, -1, 0) != 0) {
            sd.result = 0;
            if (count > 99)
                count = 100;
            if (sel > 0x62)
                sel = 99;
            unsigned i = sel;
            for (; i + 1 < count; i++) {
                sd.years[i] = sd.years[i + 1];
                sd.levels[i] = sd.levels[i + 1];
            }
            if (i < SLOTDLG_SLOTS) {
                sd.years[i] = 0;
                sd.levels[i] = 0;
            }
            draw_info(false);
            char path[1100];
            slotdlg_slot_file(1, (unsigned)slot, path, sizeof path);
            sd.result = remove(path) == 0 ? 1 : 0;
        }
    }
    wm_set_capture(sd.win);
}

static intptr_t delete_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_LBUTTONDOWN:
        wm_raise(w);
        break;
    case WM_MSG_CREATE:
        g_game_paused = 1;
        sd.list = ctl_list_create(w, &k_list_rect, 0, SLOTDLG_ID_LIST, SLOTDLG_ID_LIST + 1, 500, BG, FG);
        if (!sd.list)
            return 0;
        list_send(CTL_LIST_SELECT, fill_list(1), 0);
        break;
    case WM_MSG_CTL_CLICK: {
        WmWindow *p = w->parent;
        wm_raise(p);
        uint32_t id = ctl_get_id(w);
        if (id == SLOTDLG_ID_CANCEL) {
            sd.result = 0;
            wm_send(p, p, MSG_CLOSE, 0, 0);
        } else if (id == SLOTDLG_ID_DELETE) {
            wm_send(p, p, WM_MSG_KEYUP, KEY_DELETE, 0);
        }
        break;
    }
    case WM_MSG_CTL_RDOWN:
        help(w);
        break;
    case MSG_CLOSE:
        close_dialog(w);
        break;
    case WM_MSG_KEYUP:
        if (a == KEY_ENTER) {
            wm_send(w, w, WM_MSG_KEYUP, KEY_DELETE, 0);
        } else if (a == KEY_ESC) {
            sd.result = 0;
            wm_post(w, w, MSG_CLOSE, 0, 0);
        } else if (a == KEY_DELETE) {
            delete_selected(w);
        } else if (sd.list) {
            wm_send(sd.list, sd.list, WM_MSG_KEYUP, a, b);
        }
        break;
    case CTL_LIST_MSG_SELECTED:
        show_slot(w, (WmWindow *)a, b, true);
        break;
    case CTL_LIST_MSG_DBLCLK:
        wm_send(w, w, WM_MSG_KEYUP, KEY_DELETE, 0);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

int slotdlg_delete(void)
{
    if (sd.win)
        return 1;
    shell_busy_cursor(true);
    sd.mode = 1;
    sd.no_help = false;
    sd.choose = false;
    if (!open_window(delete_proc, TEXT_DELETE_TITLE))
        return fail();
    GfxSurface *close = gfx_load_bmp_resource(0x259, NULL);
    if (!close)
        return fail();
    GfxRect r = {0, close->width - 1, 0, close->height - 1};
    sd.win->close_button = ctl_button_create(sd.win, &r, SLOTDLG_ID_CANCEL, CTL_STYLE_BITMAP, close);
    if (!button(0x10b, 0x8b, SLOTDLG_ID_DELETE, 0x227, TEXT_DELETE) ||
        !button(0x10b, 0xa7, SLOTDLG_ID_CANCEL, 0x227, TEXT_CANCEL_DELETE))
        return fail();
    views_draw_title(sd.win);
    wm_send(sd.win, sd.win, CTL_LIST_MSG_SELECTED, (intptr_t)sd.list, -1);
    int res = run_dialog();
    destroy_window();
    return res;
}
