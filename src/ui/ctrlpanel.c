/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The Control Panel and the Choose Tile Window (ui/ctrlpanel.h). */
#include "ui/ctrlpanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/tile.h"
#include "game/build.h"
#include "game/shell_int.h"
#include "game/tables.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/surface.h"
#include "gfx/tile_draw.h"
#include "ui/controls.h"
#include "ui/news.h"
#include "ui/statusbar.h"

static WmWindow *g_buttons[CTRL_BUTTONS]; /* 0x6462f0 */
static int g_prev_tool;                   /* 0x650030: the last tool that isn't the Soulview */
static bool g_panel_drag;                 /* 0x65007c */
static int g_drag_dx, g_drag_dy;          /* 0x4f3038 / 0x4f3018 */
static bool g_chooser_active;             /* 0x650078 */
static int g_chooser_w, g_chooser_h;      /* 0x6464d0 / 0x646544: its full size */
static PlatCursor *g_cursor_rocks;        /* 0x646538 (0x7c) */
static PlatCursor *g_cursor_83;           /* 0x64653c (0x83) */

/* The chooser's items (0x639120..: 0x639126 + 0x10 * i) */
typedef struct ChooserItem {
    uint8_t set, index, size; /* what build_tool_set_item gets */
    int32_t cost;             /* 0x639129 */
    uint32_t capacity;        /* 0x63912d */
    bool enabled;             /* 0x639131 */
    uint8_t zoom;             /* 0x639135: preview zoom */
} ChooserItem;

static ChooserItem g_items[10];
static int g_rows;        /* 0x639120: items per column */
static int g_cols;        /* 0x639121 */
static bool g_per_item;   /* 0x639122: each row is its own item (else a column is one) */
static int g_sel = -1;    /* 0x65ed94 */

WmWindow *shell_ctrl_button(int id) { return id > 0 && id < CTRL_BUTTONS ? g_buttons[id] : NULL; }
int tilechooser_selection(void) { return g_sel; }

static void set_pressed(int id, bool on)
{
    if (shell_ctrl_button(id))
        ctl_button_set_pressed(g_buttons[id], on, true);
}

void ctrlpanel_click(int id)
{
    if (g_ctrlpanel_window)
        wm_send(g_ctrlpanel_window, shell_ctrl_button(id), WM_MSG_CTL_CLICK, id, 1);
}

/* ======================================================================== */
/* Creation (part of game_main 0x401000)                                     */
/* ======================================================================== */

/* the button positions of game_main's loop: {x, y, style} */
static void button_place(int id, int *x, int *y, uint32_t *style)
{
    *style = CTL_STYLE_BITMAP | CTL_STYLE_STICKY;
    if (id < 0x15) {
        *x = ((id - 1) & 3) * 0x18 + 0x10;
        *y = ((id - 1) & ~3) * 6 + 0x3b;
        return;
    }
    switch (id) {
    case 0x15:
        *x = 0x54, *y = 0x110;
        break;
    case 0x16:
        *x = 0x28, *y = 0xf6;
        break;
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1a:
        *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
        *x = (id * 3 - 0x45) * 8 + 0x10, *y = 0x141;
        break;
    case 0x1b:
        *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
        *x = 0x10, *y = 0xf6;
        break;
    case 0x1c:
        *style = CTL_STYLE_BITMAP;
        *x = 0x4b, *y = 0xb6;
        break;
    case 0x1d:
        *style = CTL_STYLE_BITMAP;
        *x = 0x5b, *y = 0xce;
        break;
    case 0x1e:
        *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
        *x = 0x21, *y = 0xcb;
        break;
    case 0x1f:
        *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
        *x = 8, *y = 0x110;
        break;
    case 0x20:
        *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
        *x = 0x58, *y = 0xf6;
        break;
    case 0x21:
        *x = 0x1e, *y = 0xb7;
        break;
    case 0x22:
        *x = 0x1e, *y = 0xe2;
        break;
    case 0x23:
        *x = 0x38, *y = 200;
        break;
    case 0x24:
        *x = 0xd, *y = 200;
        break;
    default: /* 0x25 */
        *style = CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE;
        *x = 0x40, *y = 0xf6;
        break;
    }
}

bool ctrlpanel_create(void)
{
    GfxSurface *bmp = gfx_load_bmp_resource(100, NULL);
    if (!bmp)
        return false;
    GfxRect r = {8, bmp->width + 7, 0x28, bmp->height + 0x27};
    g_ctrlpanel_window = wm_create_window(g_main_window, &r, ctrlpanel_proc, WM_F_TOPMOST | WM_F_NOFOCUS, 0);
    if (!g_ctrlpanel_window) {
        gfx_surface_destroy(bmp);
        return false;
    }
    wm_set_title(g_ctrlpanel_window, "Control Panel");
    wm_move(g_ctrlpanel_window, g_main_window->client.left + 8, g_main_window->client.top + 8);
    gfx_blit(g_ctrlpanel_window->surface, 0, 0, bmp->width, bmp->height, bmp, 0, 0);
    gfx_surface_destroy(bmp);
    for (int id = 1; id < CTRL_BUTTONS; id++) {
        if (id < 0x17)
            g_shell_tool_cursors[id] = gfx_cursor_load((unsigned)(id + 0x65));
        int x, y;
        uint32_t style;
        button_place(id, &x, &y, &style);
        GfxSurface *b = gfx_load_bmp_resource((uint32_t)(id + 0x66), NULL);
        if (!b)
            continue;
        GfxRect br = {x, x + b->width / 2 - 1, y, y + b->height - 1};
        /* the bitmap holds both frames side by side; the original's
         * rectangle is the whole bitmap's width, which the button clips */
        br.right = x + b->width - 1;
        g_buttons[id] = ctl_button_create(g_ctrlpanel_window, &br, (uint32_t)id, style, b);
        if (!g_buttons[id])
            gfx_surface_destroy(b);
    }
    g_cursor_rocks = gfx_cursor_load(0x7c);
    g_cursor_83 = gfx_cursor_load(0x83);
    g_shell_busy_cursor = gfx_cursor_load(0x84);

    /* the Choose Tile Window: 5 x 2 copies of bitmap 800 */
    GfxSurface *tb = gfx_load_bmp_resource(800, NULL);
    if (!tb)
        return false;
    g_chooser_w = tb->width * 5;
    g_chooser_h = tb->height * 2;
    GfxRect cr = {0, g_chooser_w - 1, 0, g_chooser_h - 1};
    g_tilechooser_window = wm_create_window(g_ctrlpanel_window, &cr, tilechooser_proc, WM_F_CENTER | WM_F_NOFOCUS, 0);
    if (!g_tilechooser_window) {
        gfx_surface_destroy(tb);
        return false;
    }
    wm_set_title(g_tilechooser_window, "Choose Tile Window");
    for (int x = 0; x < 5; x++)
        for (int y = 0; y < 2; y++)
            gfx_blit(g_tilechooser_window->surface, x * tb->width, y * tb->height, tb->width, tb->height, tb, 0, 0);
    gfx_surface_destroy(tb);

    /* game_main after the windows: the Microview tool, rotation (1, 1),
     * zone details and the grid on */
    set_pressed(0x15, true);
    g_shell_cursor = g_shell_tool_cursors[0x15];
    wm_set_cursor(g_main_window, g_shell_cursor);
    set_pressed(0x21, true);
    for (int id = 0x17; id <= 0x1a; id++)
        set_pressed(id, true);
    g_shell_tool = 0x15;
    build_select_tool(g_shell_tool);
    return true;
}

void ctrlpanel_destroy(void)
{
    if (g_tilechooser_window) {
        wm_show(g_tilechooser_window, false);
        wm_destroy(g_tilechooser_window);
        g_tilechooser_window = NULL;
    }
    if (g_ctrlpanel_window) {
        wm_show(g_ctrlpanel_window, false);
        wm_destroy(g_ctrlpanel_window);
        g_ctrlpanel_window = NULL;
    }
    memset(g_buttons, 0, sizeof g_buttons);
    for (int i = 1; i < 0x17; i++) {
        gfx_cursor_destroy(g_shell_tool_cursors[i]);
        g_shell_tool_cursors[i] = NULL;
    }
    gfx_cursor_destroy(g_cursor_rocks);
    gfx_cursor_destroy(g_cursor_83);
    gfx_cursor_destroy(g_shell_busy_cursor);
    g_cursor_rocks = g_cursor_83 = g_shell_busy_cursor = NULL;
}

/* 0x402695 */
void ctrlpanel_place_windows(WmWindow *const *windows, int n)
{
    int x = g_ctrlpanel_window->rect.right + 5, y = g_ctrlpanel_window->rect.top;
    for (int i = 0; i < n; i++) {
        if (windows[i]) {
            wm_move(windows[i], x, y);
            wm_raise(windows[i]);
        }
        x += 0xd;
        y += 0xe;
    }
}

/* ======================================================================== */
/* Help                                                                       */
/* ======================================================================== */

static void button_help(WmWindow *b)
{
    static const char *const text[CTRL_BUTTONS] = {
        [1] = "This button will put you into ENVY/CONTENTMENT ZONING mode.(1)",
        [2] = "This button will put you into AVARICE/CHARITY ZONING mode.(2)",
        [3] = "This button will put you into GLUTTONY/TEMPERANCE ZONING mode.(3)",
        [4] = "This button will put you into SLOTH/DILIGENCE ZONING mode.(4)",
        [5] = "This button will put you into LUST/CHASTITY ZONING mode.(5)",
        [6] = "This button will put you into WRATH/PEACEFULNESS ZONING mode.(6)",
        [7] = "This button will put you into PRIDE/HUMILITY ZONING mode.(7)",
        [8] = "This button will put you into GENERIC SIN/VIRTUE ZONING mode.(0)",
        [9] = "This button will allow you to choose a GATE, if you can afford any.(G)",
        [10] = "This button will put you into ROAD ZONING mode.(R)",
        [11] = "This button will allow you to choose a KARMA STATION, if you can afford any.(K)",
        [12] = "This button will put you into KARMA TRACK ZONING mode.(T)",
        [13] = "This button will allow you to choose a TOPIA, if you can afford any.(C)",
        [14] = "This button will allow you to choose a TRAINING CENTER, if you can afford any.(N)",
        [15] = "This button will allow you to choose a PORT, if you can afford any.(B)",
        [16] = "This button lets you put one of our MISCELLANEOUS STRUCTURES down.(E)",
        [17] = "This button will allow you to choose a SPECIAL STRUCTURE, if any are available.(S)",
        [18] = "This button will put you into OMNIBOLGE/LOVE DOME ZONING mode.(O)",
        [19] = "This button will put you into LIMBO ZONING mode.(L)",
        [20] = "This button will put you into STRUCTURE DESTRUCTION mode.(X)",
        [21] = "This button will put you into MICROVIEW mode.(M)",
        [22] = "This button will put you into SOULVIEW mode.(I)",
        [23] = "This button toggles the visibility of the buildings in Hell.",
        [24] = "This button toggles the visibility of the buildings in Heaven.",
        [25] = "This button toggles the visibility of the Karma Realm.",
        [26] = "This button toggles the visibility of the playing grid.",
        [27] = "This button toggles the GRAPH VIEW.(D)",
        [28] = "This button MAGNIFIES the Main View.(+)",
        [29] = "This button SHRINKS the Main View.(-)",
        [30] = "This button toggles the PLANET VIEW.(P)",
        [31] = "This button can be used to summon and dismiss Jasper Wormsworth and Aria Goodhalo, your "
               "majordomos.(H)",
        [32] = "This button toggles the MAP VIEW.(F)",
        [33] = "This button will rotate your Afterlife to face north.",
        [34] = "This button will rotate your Afterlife to face south.",
        [35] = "This button will rotate your Afterlife to face east.",
        [36] = "This button will rotate your Afterlife to face west.",
        [37] = "This button toggles the MACROMANAGER.(U)",
    };
    unsigned id = ctl_get_id(b);
    const char *t = id < CTRL_BUTTONS && text[id] ? text[id] : "I don't know what this button does.";
    int x = ((b->rect.right - b->rect.left) >> 1) + b->rect.left + g_ctrlpanel_window->rect.left;
    int y = ((b->rect.bottom - b->rect.top) >> 1) + b->rect.top + g_ctrlpanel_window->rect.top;
    help_balloon_show(x, y, 0x78, t, true, true);
}

/* ======================================================================== */
/* The tile chooser                                                           */
/* ======================================================================== */

/* 0x455ed7: replaces colour `from` by `to` in a rectangle of the surface */
static void recolor(GfxSurface *s, int x, int y, int w, int h, uint8_t from, uint8_t to)
{
    if (!s)
        return;
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= s->height)
            continue;
        uint8_t *row = s->pixels + (size_t)j * (size_t)s->stride;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < s->width && row[i] == from)
                row[i] = to;
    }
}

/* 0x455f6e: selects the item under (x, y) of the chooser (frames it in
 * colour 0x1e) and makes it the tool's item. Returns the selection. */
static int chooser_select(int x, int y)
{
    WmWindow *w = g_tilechooser_window;
    int colw = g_chooser_w / 5, half = g_chooser_h >> 1;
    int col = x < 0 ? 0 : x / colw;
    if (col > g_cols - 1)
        col = g_cols - 1;
    int idx = g_rows * col, cur_row = 0, row = 0;
    if (g_per_item) {
        cur_row = g_rows < 2 || g_sel < 0 ? 0 : g_sel % g_rows;
        row = (int)(int8_t)(y / half);
        if (row > g_rows - 1)
            row = g_rows - 1;
        if (row < 1)
            row = 0;
        idx += row;
    }
    if (idx >= 0 && idx < 10 && g_sel != idx && g_items[idx].enabled) {
        if (!g_per_item) {
            if (g_sel != -1)
                recolor(w->surface, (g_sel / g_rows) * colw, 4, colw, g_rows * half - 0x10, 0x1e, 0x13);
            recolor(w->surface, (idx / g_rows) * colw, 4, colw, g_rows * half - 0x10, 0x13, 0x1e);
        } else {
            if (g_sel >= 0)
                recolor(w->surface, (g_sel / g_rows) * colw, cur_row * half + 4, colw, half - 0x10, 0x1e, 0x13);
            recolor(w->surface, (idx / g_rows) * colw, row * half + 4, colw, half - 0x10, 0x13, 0x1e);
        }
        wm_redraw(w, NULL);
        g_sel = idx;
        build_tool_set_item(g_items[idx].set, g_items[idx].index, g_items[idx].size);
        g_build.tool_cost = g_items[idx].cost;
    }
    g_build.cost_shown = g_build.tool_cost;
    g_build.status_dirty = 1;
    return g_sel;
}

static void item_spec(ChooserItem *it, unsigned set, unsigned index, const BuildingSpec *spec, uint8_t zoom)
{
    it->set = (uint8_t)set;
    it->index = (uint8_t)index;
    it->size = spec->size;
    it->zoom = zoom;
    if (!g_build.editor)
        it->cost = (int32_t)spec->cost;
    it->capacity = spec->capacity;
    it->enabled = bignum_ge_int(g_state.pennies, it->cost) != 0;
}

/* 0x456402: fills the chooser for `tool`, draws it next to `button` and
 * shows it. False for tools without a chooser. */
static bool chooser_open(int tool, WmWindow *button)
{
    WmWindow *w = g_tilechooser_window;
    int colw = g_chooser_w / 5;
    memset(g_items, 0, sizeof g_items);
    for (int i = 0; i < 10; i++)
        g_items[i].cost = 1;
    switch (tool) {
    case 9: /* gates (set 13; Hell's variant is index + 2) */
        g_rows = 2, g_cols = 4, g_per_item = false;
        for (int i = 0; i < 8; i++)
            item_spec(&g_items[i], 0xd, (unsigned)i * 2, &g_tables.set13[i], 1);
        break;
    case 11: /* Karma stations */
        g_rows = 1, g_cols = 3, g_per_item = true;
        for (int i = 0; i < 3; i++)
            item_spec(&g_items[i], 0x11, (unsigned)i, &g_tables.set17[i], 1);
        break;
    case 13: /* topias (set 7) */
        g_rows = 2, g_cols = 4, g_per_item = false;
        for (int i = 0; i < 8; i++)
            item_spec(&g_items[i], 7, (unsigned)i, &g_tables.set7[i], 1);
        break;
    case 14: /* training centres (set 8) */
        g_rows = 2, g_cols = 3, g_per_item = false;
        for (int i = 0; i < 6; i++) {
            item_spec(&g_items[i], 8, (unsigned)i, &g_tables.set8[i], 1);
            g_items[i].cost = (int32_t)g_tables.set8[i].cost; /* no editor exception here */
            g_items[i].enabled = bignum_ge_int(g_state.pennies, g_items[i].cost) != 0;
        }
        break;
    case 15: /* ports (set 9) */
        g_rows = 2, g_cols = 3, g_per_item = false;
        for (int i = 0; i < 6; i++)
            item_spec(&g_items[i], 9, (unsigned)i, &g_tables.set9[i], 1);
        break;
    case 16: /* rocks (set 0x1d) and banks (set 0x13) */
        g_rows = 2, g_cols = 3, g_per_item = false;
        for (int i = 0; i < 2; i++) {
            ChooserItem *it = &g_items[i];
            it->set = 0x1d, it->index = (uint8_t)i, it->zoom = 0, it->size = 1;
            if (!g_build.editor)
                it->cost = 50000;
            it->capacity = 0;
            it->enabled = bignum_ge_int(g_state.pennies, it->cost) != 0;
        }
        for (int i = 2; i < 6; i++)
            item_spec(&g_items[i], 0x13, (unsigned)(i - 2), &g_tables.set19[i - 2], (uint8_t)((i - 2) / 2));
        break;
    case 17: /* special structures (set 4): allowed (0x63042c) and not built yet (0x638668) */
        g_rows = 2, g_cols = 5, g_per_item = true;
        for (int i = 0; i < 10; i++) {
            ChooserItem *it = &g_items[i];
            unsigned idx = (unsigned)(i & 1) * 5 + (unsigned)(i >> 1);
            it->set = 4, it->index = (uint8_t)idx, it->zoom = 1, it->size = 4, it->cost = 0, it->capacity = 0;
            it->enabled = (g_state.unk_63042c & (1u << idx)) && !(g_state.unk_638668 & (1u << idx));
            if (g_build.editor)
                it->enabled = true;
        }
        break;
    case 18: /* Omnibolges / Love Domes (set 5) */
        g_rows = 2, g_cols = 3, g_per_item = true;
        for (int i = 0; i < 6; i++) {
            item_spec(&g_items[i], 5, (unsigned)i, &g_tables.set5[i], 2);
            uint32_t need = (i & 1) == 0 ? 0x10000u : 0x20000u;
            g_items[i].enabled = (g_state.unk_63042c & need) && bignum_ge_int(g_state.pennies, g_items[i].cost);
            if (g_build.editor)
                g_items[i].enabled = true;
        }
        break;
    case 19: /* Limbo structures (set 30) */
        g_rows = 1, g_cols = 3, g_per_item = true;
        for (int i = 0; i < 3; i++)
            item_spec(&g_items[i], 0x1e, (unsigned)i, &g_tables.set30[i], i == 0 ? 1 : 2);
        break;
    default:
        return false;
    }
    wm_show(w, false);
    wm_set_capture(w);
    g_sel = -1;

    /* a scratch view for the previews, like the main one but unrotated in y */
    MapView v = g_map_view;
    v.origin_x = v.origin_y = 0;
    v.zoom = 0;
    v.magnify = 0;
    v.dir_y = 1;
    v.flags = MAPVIEW_F_NO_TINT_OVERLAY;
    v.mini = 0;
    v.focus_x = v.focus_y = -1;
    int rowh = g_chooser_h >> 1;
    unsigned fc[2] = {0x13, 0x2f};
    gfx_font_set_colors(500, 1, 2, fc);
    gfx_font_select(500);
    char buf[64], num[FMT_NUM_LEN];
    int k = 0;
    for (int c = 0; c < g_cols; c++) {
        for (int r = 0; r < g_rows; r++, k++) {
            ChooserItem *it = &g_items[k];
            v.clip_x = 2 + c * colw;
            v.clip_y = 6 + r * rowh;
            v.clip_w = colw - 5;
            v.clip_h = rowh - 0x3c;
            gfx_fill_rect(w->surface, v.clip_x, v.clip_y, v.clip_w, v.clip_h, it->enabled ? 0x13 : 0x28);
            if (it->enabled) {
                tile_draw_preview(w->surface, &v, it->set, (unsigned)it->index + 1, it->zoom, NULL);
            } else {
                uint32_t cell = (uint32_t)it->set << 27 | (uint32_t)it->index << 16 | CELL_TINT;
                tile_draw_preview(w->surface, &v, it->set, (unsigned)it->index + 1, it->zoom, &cell);
            }
            int tx = c * colw, ty = r * rowh;
            gfx_fill_rect(w->surface, tx + 3, ty + 0x7a, 0x72, 0x25, 99);
            GfxRect clip = {tx + 4, tx + colw - 5, ty + 0x7a, ty + 0x9f};
            if (!it->enabled) {
                if (it->set == 4 && (g_state.unk_638668 & (1u << it->index))) {
                    snprintf(buf, sizeof buf, "One Per Customer.");
                } else {
                    static const char *const no[5] = {"Nope.", "Not Yet", "Unavailable.", "Unbuildable.",
                                                      "Can't Build This."};
                    snprintf(buf, sizeof buf, "%s", no[rand_range(0, 4)]);
                }
                gfx_font_draw_text(w->surface, tx + 8, ty + 0x7a, &clip, &clip, buf,
                                   GFX_TEXT_CENTER | GFX_TEXT_NOJUSTIFY);
                if (it->set != 4) {
                    snprintf(buf, sizeof buf, "Cost: %s\b", fmt_thousands_k(num, (uint32_t)it->cost));
                    gfx_font_draw_text(w->surface, tx + 8, ty + 0x95, NULL, NULL, buf, 0);
                }
            } else {
                const TileSet *ts = tile_draw_set(it->set);
                TileEntry e;
                const char *name = ts && tile_get_entry(ts, it->index, &e) == TILE_OK ? tile_entry_text(&e, 0) : NULL;
                if (name)
                    gfx_font_draw_text(w->surface, tx + 6, ty + 0x7a, &clip, &clip, name,
                                       GFX_TEXT_CENTER | GFX_TEXT_NOJUSTIFY);
                switch (it->set) {
                case 5:
                case 7:
                case 8:
                case 9:
                case 0xd:
                case 0x11:
                case 0x1e:
                    snprintf(buf, sizeof buf, "Cost: %s\b", fmt_thousands_k(num, (uint32_t)it->cost));
                    gfx_font_draw_text(w->surface, tx + 8, ty + 0x8c, NULL, NULL, buf, 0);
                    snprintf(buf, sizeof buf, "Capacity: %s", fmt_thousands_k(num, it->capacity));
                    gfx_font_draw_text(w->surface, tx + 8, ty + 0x95, NULL, NULL, buf, 0);
                    break;
                case 0x13:
                    snprintf(buf, sizeof buf, "Cost: %s\b", fmt_thousands_k(num, (uint32_t)it->cost));
                    gfx_font_draw_text(w->surface, tx + 8, ty + 0x8c, NULL, NULL, buf, 0);
                    snprintf(buf, sizeof buf, "Loan: %s\b", fmt_thousands_k(num, it->capacity));
                    gfx_font_draw_text(w->surface, tx + 8, ty + 0x95, NULL, NULL, buf, 0);
                    break;
                case 0x1d:
                    gfx_font_draw_text(w->surface, tx + 8, ty + 0x8c, NULL, NULL, "Cost: 50,000\b", 0);
                    break;
                default:
                    break;
                }
            }
        }
    }
    int width = g_cols * colw, height = g_rows == 1 ? rowh : g_chooser_h;
    wm_raise(w);
    wm_resize(w, width, height);
    /* next to the button, kept inside the main window (screen coordinates,
     * then made relative to the control panel, its parent) */
    WmWindow *panel = g_ctrlpanel_window;
    int x = panel->rect.left + (button ? button->rect.left : 0) + g_main_window->rect.left;
    if (x > g_main_window->client.right - width)
        x = g_main_window->client.right - width;
    int y = panel->rect.top + (button ? button->rect.top : 0) + g_main_window->rect.top;
    if (y > g_main_window->client.bottom - height)
        y = g_main_window->client.bottom - height;
    x -= panel->rect.left;
    y -= panel->rect.top;
    GfxRect nr = {x, x + width, y, y + height};
    wm_set_rect(w, &nr);
    wm_show(w, true);
    wm_redraw(w, NULL);
    g_sel = -1;
    return true;
}

/* 0x402789 */
intptr_t tilechooser_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        chooser_select((int)a, (int)b);
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_set_capture(w);
        g_game_paused = 1;
        g_chooser_active = true;
        break;
    case WM_MSG_RBUTTONDOWN: /* cancel: back to the last plain tool */
        wm_release_capture();
        wm_show(w, false);
        set_pressed(g_shell_tool, false);
        if (g_prev_tool)
            wm_send(g_ctrlpanel_window, g_ctrlpanel_window, WM_MSG_CTL_CLICK, g_prev_tool, 0);
        g_game_paused = 0;
        g_chooser_active = false;
        break;
    case WM_MSG_LBUTTONUP:
        if (g_chooser_active) {
            int sel = chooser_select((int)a, (int)b);
            if (sel < 0) {
                if (g_prev_tool)
                    wm_send(g_ctrlpanel_window, g_ctrlpanel_window, WM_MSG_CTL_CLICK, g_prev_tool, 0);
            } else if (g_shell_tool == 0x10) {
                g_shell_cursor = sel < 2 ? g_cursor_83 : g_cursor_rocks;
            } else {
                g_shell_cursor = g_shell_tool_cursors[g_shell_tool];
            }
            wm_release_capture();
            wm_show(w, false);
            set_pressed(g_shell_tool, false);
            g_game_paused = 0;
            g_chooser_active = false;
            statusbar_draw();
        }
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ======================================================================== */
/* ctrlpanel_proc 0x402996                                                    */
/* ======================================================================== */

/* the grid (view flag 0x80) on all three planes */
static void set_grid(bool on)
{
    for (int p = 1; p <= 3; p++) {
        if (on)
            g_build.view_flags[p] |= 0x80;
        else
            g_build.view_flags[p] &= 0x7f;
    }
}

static void pick_tool(int id)
{
    if (id >= 0x17 || id == g_shell_tool)
        return;
    set_pressed(id, true);
    if (g_shell_tool)
        set_pressed(g_shell_tool, false);
    g_shell_tool = id;
    if (id != 0x16)
        g_prev_tool = id;
    g_shell_cursor = g_shell_tool_cursors[id];
    build_select_tool(id);
    statusbar_draw();
}

intptr_t ctrlpanel_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE: /* dragging the panel by its background */
        if (g_panel_drag) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_drag_dx, (int)b - g_drag_dy);
            wm_local_to_screen(w, &r);
            if (w->parent)
                wm_screen_to_local(w->parent, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        g_game_paused = 1;
        g_panel_drag = true;
        wm_set_capture(w);
        g_drag_dx = (int)a;
        g_drag_dy = (int)b;
        break;
    case WM_MSG_RBUTTONDOWN:
        if (a > 0x5e && a < 0x6f && b > 6 && b < 0x17)
            help_balloon_show(w->rect.left + (int)a, w->rect.top + (int)b, 0x78,
                              "This little diagram shows the relative WEIGHTS given to each of the sins and "
                              "virtues.  The taller the bar, the more likely a SOUL is to be permanently weighed "
                              "down/uplifted by that sin/virtue.",
                              true, true);
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        g_panel_drag = false;
        g_game_paused = 0;
        break;
    case WM_MSG_CTL_PRESS: /* the toggles report their state as they change */
        if (a > 0x16 && a < 0x1a) {
            g_game_paused = 1;
            build_zone_blocks((int)a, b == 0);
            g_shell_repaint = 1;
            g_game_paused = 0;
        } else if (a == 0x1a) {
            g_game_paused = 1;
            set_grid(b != 0);
            g_shell_repaint = 1;
            g_game_paused = 0;
        }
        break;
    case WM_MSG_CTL_CLICK: {
        int id = (int)a;
        switch (id) {
        case 9:
        case 0xb:
        case 0xd:
        case 0xe:
        case 0xf:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            if (g_shell_tool)
                shell_cancel_drag();
            set_pressed(id, true);
            if (g_shell_tool && g_shell_tool != id)
                set_pressed(g_shell_tool, false);
            if (!wm_is_visible(g_ctrlpanel_window))
                shell_menu_command(MENU_CTRLPANEL);
            g_shell_tool = id;
            build_select_tool(id);
            if (chooser_open(id, shell_ctrl_button(id)))
                chooser_select(0, 1); /* the original passes (id, 1): the first item */
            statusbar_draw();
            break;
        case 0x16:
            if (shell_hooks.soulview_show)
                shell_hooks.soulview_show(true);
            /* fall through */
        default:
            if (g_shell_tool)
                shell_cancel_drag();
            pick_tool(id);
            break;
        case 0x1b:
            shell_menu_command(MENU_GRAPHVIEW);
            break;
        case 0x1c:
            shell_cancel_drag();
            shell_view_zoom_in();
            break;
        case 0x1d:
            shell_cancel_drag();
            shell_view_zoom_out();
            break;
        case 0x1e:
            shell_menu_command(MENU_PLANETVIEW);
            break;
        case 0x1f:
            shell_menu_command(MENU_HELPERS);
            break;
        case 0x20:
            shell_menu_command(MENU_MAPVIEW);
            break;
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
            shell_view_rotate_to(id);
            break;
        case 0x25:
            shell_menu_command(MENU_MACRO);
            break;
        }
        break;
    }
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() == 0)
            break;
        /* fall through */
    case WM_MSG_CTL_RDOWN:
        if (w != g_ctrlpanel_window)
            button_help(w);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
