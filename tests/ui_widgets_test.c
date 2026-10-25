/*
 * Behaviour test for the UI toolkit: menus, scrollbars, list box, text
 * field, text viewer, message box and text prompt (src/ui/). Needs the
 * user's game files; none are in the repo.
 *
 *   ui_widgets_test <data dir> <bundle> <out prefix> [--window]
 *
 * Everything is driven through the synthetic input path the game uses:
 * mouse events go into the mouse module (gfx_mouse_on_move/on_button) and
 * keyboard records into the window manager's key_poll hook, and wm_idle
 * turns them into window messages. Checks the resulting commands, state
 * and screen pixels, and writes <prefix>_menu.ppm, <prefix>_widgets.ppm and
 * <prefix>_msgbox.ppm. Headless unless --window is given.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/resource.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/controls.h"
#include "ui/menu.h"
#include "ui/region.h"
#include "ui/wm.h"

#define W 640
#define H 480

static int g_failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

/* ------------------------------------------------------------ helpers */

static uint8_t *read_resource(uint32_t id, long *len)
{
    PlatFile *f = rc_open_resource(id);
    if (!f)
        return NULL;
    *len = rc_resource_size(id);
    uint8_t *buf = *len > 0 ? malloc((size_t)*len) : NULL;
    if (buf)
        plat_file_read(f, buf, 1, (size_t)*len);
    plat_file_close(f);
    return buf;
}

static bool write_ppm(const char *prefix, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    const GfxSurface *scr = gfx_screen();
    uint8_t pal[256 * 4];
    gfx_palette_get(pal, 0, 256);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", scr->width, scr->height);
    for (int y = 0; y < scr->height; y++)
        for (int x = 0; x < scr->width; x++) {
            const uint8_t *c = pal + scr->pixels[(size_t)y * scr->stride + x] * 4;
            fputc(c[0], f);
            fputc(c[1], f);
            fputc(c[2], f);
        }
    return fclose(f) == 0;
}

static uint8_t spx(int x, int y)
{
    const GfxSurface *s = gfx_screen();
    return s->pixels[(size_t)y * s->stride + x];
}

/* screen pixel (x, y) shows window w's surface */
static bool shows(const WmWindow *w, int x, int y)
{
    int lx = x, ly = y;
    wm_point_to_local(w, &lx, &ly);
    if (!w->surface || lx < 0 || ly < 0 || lx >= w->surface->width || ly >= w->surface->height)
        return false;
    return spx(x, y) == w->surface->pixels[(size_t)ly * w->surface->stride + lx];
}

/* centre of a child's rectangle, in screen coordinates */
static void centre_of(const WmWindow *w, int *x, int *y)
{
    *x = (w->rect.left + w->rect.right) / 2;
    *y = (w->rect.top + w->rect.bottom) / 2;
    if (w->parent)
        wm_point_to_screen(w->parent, x, y);
}

/* the keyboard queue behind wm_hooks.key_poll */
typedef struct {
    int type, key, ch;
} Key;
static Key g_keys[64];
static int g_key_r, g_key_w;

static bool key_poll(int *type, int *key, int *ch)
{
    if (g_key_r == g_key_w)
        return false;
    *type = g_keys[g_key_r].type;
    *key = g_keys[g_key_r].key;
    *ch = g_keys[g_key_r].ch;
    g_key_r = (g_key_r + 1) % 64;
    return true;
}

static void push_key(int type, int key, int ch)
{
    g_keys[g_key_w] = (Key){type, key, ch};
    g_key_w = (g_key_w + 1) % 64;
}

static void key_tap(int vk, int ch)
{
    push_key(WM_KEY_DOWN, vk, ch);
    push_key(WM_KEY_UP, vk, ch);
}

static void type_text(const char *s)
{
    for (; *s; s++)
        push_key(WM_KEY_CHAR, 0, (unsigned char)*s);
}

static void pump(int n)
{
    for (int i = 0; i < n; i++) {
        wm_idle();
        ctl_idle();
    }
}

static void mouse_move(int x, int y) { gfx_mouse_on_move(x, y); }
static void mouse_down(void) { gfx_mouse_on_button(GFX_MOUSE_LEFT, true); }
static void mouse_up(void) { gfx_mouse_on_button(GFX_MOUSE_LEFT, false); }

static void click(int x, int y)
{
    mouse_move(x, y);
    mouse_down();
    mouse_up();
}

/* ------------------------------------------------------------ windows */

static intptr_t g_last_command = -1;
static intptr_t g_list_sel = -1;
static int g_enter_count;
static intptr_t g_enter_id;

/* the main window: menu commands and its own scrollbars */
static intptr_t main_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MENU_COMMAND:
        g_last_command = a;
        return 0;
    case WM_MSG_VSCROLL:
    case WM_MSG_HSCROLL:
    case WM_MSG_VTRACK:
    case WM_MSG_HTRACK:
        /* w is the scrollbar */
        ctl_scroll_set_pos(w, (int)a);
        return 0;
    }
    return wm_default_proc(w, msg, a, b);
}

/* the panel holding the list, text field and viewer */
static intptr_t panel_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case CTL_LIST_MSG_SELECTED:
        g_list_sel = b;
        break;
    case CTL_TEXT_MSG_ENTER:
        g_enter_count++;
        g_enter_id = b;
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* ------------------------------------------------------------ modal loops */

enum { SCRIPT_NONE, SCRIPT_MSGBOX_CLICK2, SCRIPT_MSGBOX_TIMEOUT, SCRIPT_PROMPT };
static int g_script;
static int g_pump_calls;
static bool g_seen_dialog;

static void test_pump(void)
{
    g_pump_calls++;
    if (g_pump_calls > 3000) {
        fprintf(stderr, "FAIL: modal loop didn't end (script %d)\n", g_script);
        exit(1);
    }
    if (g_script == SCRIPT_MSGBOX_CLICK2 && g_pump_calls == 3) {
        WmWindow *dlg = ctl_msgbox_window();
        g_seen_dialog = dlg && wm_is_visible(dlg) && wm_is_visible(ctl_msgbox_button(1));
        if (g_seen_dialog) {
            int x, y;
            centre_of(ctl_msgbox_button(1), &x, &y);
            CHECK(wm_window_at(x, y) == ctl_msgbox_button(1));
            click(x, y);
        }
    } else if (g_script == SCRIPT_PROMPT && g_pump_calls == 3) {
        type_text("ab@c");
        push_key(WM_KEY_CHAR, 0, '\r');
    } else if (g_script == SCRIPT_MSGBOX_TIMEOUT) {
        plat_delay_ms(5);
    }
    wm_idle();
    ctl_idle();
}

/* ------------------------------------------------------------ main */

static GfxSurface *load(uint32_t id)
{
    GfxSurface *s = gfx_load_bmp_resource(id, NULL);
    if (!s)
        fprintf(stderr, "missing bitmap %#x\n", id);
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <out prefix> [--window]\n", argv[0]);
        return 2;
    }
    bool window = argc > 4 && strcmp(argv[4], "--window") == 0;
    if (!window) {
#ifdef _WIN32
        _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
#endif
    }
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;
    if (!plat_init("ui_widgets_test", W, H) || !gfx_init(W, H, false))
        return 1;
    gfx_mouse_init();
    gfx_font_init();
    gfx_font_load(500);
    gfx_font_load(501);

    long len = 0;
    uint8_t *raw = read_resource(33, &len);
    uint8_t *pal = raw ? gfx_palette_load_resource(raw, (size_t)len) : NULL;
    free(raw);
    CHECK(pal != NULL);
    if (pal)
        gfx_palette_set(pal, GFX_PAL_FIRST, GFX_PAL_COUNT);
    free(pal);
    gfx_palette_build_highlight();

    wm_hooks.key_poll = key_poll;
    CHECK(wm_initialize() == 0);
    ctl_initialize();
    ctl_hooks.pump = test_pump;
    int paused = 0;
    ctl_set_pause_flag(&paused);
    wm_set_input_live(true);

    /* the game's skins (game_main 0x401000) */
    GfxSurface *sb[8] = {load(0x4000), load(0x4001), load(0x4006), load(0x4004),
                         load(0x4002), load(0x4003), load(0x4006), load(0x4005)};
    ctl_scroll_set_vbitmaps(sb[0], sb[1], sb[2], sb[3]);
    ctl_scroll_set_hbitmaps(sb[4], sb[5], sb[6], sb[7]);
    GfxSurface *mk[9];
    for (int i = 0; i < 9; i++)
        mk[i] = load(0x4010 + (uint32_t)i);
    menu_set_skin(mk[0], mk[1], mk[2], mk[3], mk[4], mk[5], mk[6], mk[7], mk[8]);
    menu_set_font(501);

    /* the main window, like the game's (minus CLIPMOUSE) */
    GfxRect full = {0, W - 1, 0, H - 1};
    WmWindow *mainw = wm_create_toplevel(&full, main_proc, WM_F_MENUBAR | WM_F_VSCROLL | WM_F_HSCROLL);
    CHECK(mainw && mainw->menubar && mainw->vscroll && mainw->hscroll);
    if (!mainw || !mainw->menubar || !mainw->vscroll)
        return 1;
    GfxSurface *back = load(1210);
    if (back) {
        gfx_tile_fill(mainw->surface, 0, 0, W, H, back, 0, 0, back->width, back->height);
        gfx_surface_destroy(back);
    }
    /* the bar shrank the client area; the scrollbars too */
    CHECK(mainw->client.top == mainw->menubar->rect.bottom + 1);
    CHECK(mainw->client.right < W - 1 && mainw->client.bottom < H - 1);

    /* menus, built the way 0x409c17 does */
    WmWindow *bar = mainw->menubar;
    WmWindow *file = menu_add_item(bar, MENU_TEXT, 100, " &File ", 0, 501);
    WmWindow *glob = menu_add_item(bar, MENU_TEXT, 200, " &Global ", 0, 501);
    WmWindow *fm = menu_create(file, NULL, 0);
    WmWindow *gm = menu_create(glob, NULL, 0);
    WmWindow *it_new = menu_add_item(fm, MENU_TEXT, 1, "&New Game\tCtrl+N", 0, 501);
    WmWindow *it_load = menu_add_item(fm, MENU_TEXT, 2, "&Load Game", 0, 501);
    menu_add_item(fm, MENU_SEPARATOR, 0, NULL, 1, 501);
    WmWindow *it_quit = menu_add_item(fm, MENU_TEXT, 7, "&Quit", 0, 501);
    WmWindow *it_snd = menu_add_item(gm, MENU_TEXT | MENU_CHECKABLE, 21, "&Sound", 0, 501);
    menu_add_item(gm, MENU_TEXT, 22, "E&xit Menu", 0, 501);
    CHECK(file && glob && fm && gm && it_new && it_load && it_quit && it_snd);
    if (!it_quit || !it_snd)
        return 1;
    CHECK(menu_find_item(7) == it_quit);
    CHECK(menu_bar_of(it_quit) == bar);

    wm_show(mainw, true);
    wm_show_scrollbars(mainw, 3, true);
    menu_show(bar, true);
    menu_set_checked(it_snd, true);
    CHECK(menu_is_checked(it_snd));
    wm_activate(mainw);
    pump(2);
    CHECK(bar->surface != NULL && wm_is_visible(file));
    CHECK(shows(bar, 3, 3));

    /* --- menu by mouse: press on File, drag to Load Game, release ---- */
    int x, y;
    centre_of(file, &x, &y);
    CHECK(wm_window_at(x, y) == file);
    mouse_move(x, y);
    mouse_down();
    pump(2);
    CHECK(wm_is_visible(fm));
    CHECK(wm_open_menu() == fm);
    CHECK(menu_hot_item() == file);
    int lx, ly;
    centre_of(it_load, &lx, &ly);
    CHECK(wm_window_at(lx, ly) == it_load);
    /* the pop-up is on screen, below the bar */
    CHECK(shows(fm, lx, ly));
    CHECK(ly > bar->rect.bottom);
    uint8_t before = spx(lx, ly);
    mouse_move(lx, ly);
    pump(2);
    CHECK(menu_hot_item() == it_load);
    CHECK(wm_get_data(it_load, MENU_DATA_STYLE) & MENU_HILITE);
    /* highlighted: brightened through the highlight remap */
    CHECK(shows(fm, lx, ly));
    CHECK(spx(lx, ly) == gfx_palette_highlight()[before] || spx(lx, ly) != before);
    gfx_present();
    if (!write_ppm(argv[3], "menu"))
        g_failures++;
    mouse_up();
    pump(2);
    CHECK(g_last_command == 2);
    CHECK(!wm_is_visible(fm));
    CHECK(wm_open_menu() == NULL);
    CHECK(wm_focus() == mainw);
    CHECK(shows(mainw, lx, ly)); /* the pop-up's area shows the main window again */

    /* --- menu by keyboard: Alt, Down (opens File), Down, Down, Enter ---- */
    g_last_command = -1;
    key_tap(0x12, 0x12);
    pump(3);
    CHECK(menu_hot_item() == file);
    key_tap(0x28, 0x28);
    pump(3);
    CHECK(wm_is_visible(fm));
    CHECK(menu_hot_item() == it_new);
    key_tap(0x28, 0x28);
    key_tap(0x28, 0x28); /* skips the separator */
    pump(5);
    CHECK(menu_hot_item() == it_quit);
    key_tap(0x0d, 0x0d);
    pump(3);
    CHECK(g_last_command == 7);
    CHECK(!wm_is_visible(fm));
    CHECK(wm_focus() == mainw);

    /* --- accelerators: Alt, 'g' opens Global, 'x' chooses E&xit Menu --- */
    g_last_command = -1;
    key_tap(0x12, 0x12);
    pump(3);
    push_key(WM_KEY_CHAR, 0, 'g');
    pump(2);
    CHECK(wm_is_visible(gm));
    CHECK(menu_hot_item() == it_snd);
    push_key(WM_KEY_CHAR, 0, 'x');
    pump(2);
    CHECK(g_last_command == 22);
    CHECK(!wm_is_visible(gm));

    /* --- a click outside an open menu closes it, without a command ---- */
    g_last_command = -1;
    mouse_move(x, y);
    mouse_down();
    pump(2);
    CHECK(wm_is_visible(fm));
    mouse_move(400, 300);
    mouse_up();
    pump(3);
    CHECK(!wm_is_visible(fm));
    CHECK(g_last_command == -1);

    /* --- the main window's vertical scrollbar ---------------------------- */
    WmWindow *vs = mainw->vscroll, *back_a = NULL, *fwd_a = NULL, *thumb = NULL;
    ctl_scroll_parts(vs, &back_a, &fwd_a, &thumb);
    CHECK(back_a && fwd_a && thumb);
    ctl_scroll_set_range(vs, 0, 20);
    ctl_scroll_set_pos(vs, 0);
    int ty0 = thumb->rect.top;
    centre_of(fwd_a, &x, &y);
    CHECK(wm_window_at(x, y) == fwd_a);
    click(x, y);
    pump(4);
    CHECK(ctl_scroll_get_pos(vs) == 1);
    CHECK(thumb->rect.top > ty0);
    CHECK(shows(vs, x, y));
    /* track click near the bottom: the thumb jumps there */
    int tx = (vs->rect.left + vs->rect.right) / 2, tyy = fwd_a->rect.top - 3;
    wm_point_to_screen(vs, &tx, &tyy);
    tx = x;
    click(tx, tyy);
    pump(4);
    CHECK(ctl_scroll_get_pos(vs) >= 18);
    /* the back arrow */
    int p0 = ctl_scroll_get_pos(vs);
    centre_of(back_a, &x, &y);
    click(x, y);
    pump(4);
    CHECK(ctl_scroll_get_pos(vs) == p0 - 1);
    /* drag the thumb to the top */
    centre_of(thumb, &x, &y);
    mouse_move(x, y);
    mouse_down();
    pump(2);
    CHECK(wm_capture() == thumb);
    mouse_move(x, 0);
    pump(3);
    mouse_up();
    pump(3);
    CHECK(ctl_scroll_get_pos(vs) == 0);
    CHECK(wm_capture() == NULL);

    /* --- a panel with a list box, a text field and a text viewer ------- */
    GfxRect pr = {250, 609, 120, 439};
    WmWindow *panel = wm_create_window(NULL, &pr, panel_proc, 0, 0);
    CHECK(panel != NULL);
    if (!panel)
        return 1;
    uint8_t grey = gfx_palette_closest(60, 60, 70), white = gfx_palette_closest(250, 250, 250),
            dark = gfx_palette_closest(10, 10, 20);
    gfx_fill_rect(panel->surface, 0, 0, 360, 320, grey);
    wm_show(panel, true);
    wm_activate(panel);

    GfxRect lr = {10, 189, 10, 150};
    WmWindow *list = ctl_list_create(panel, &lr, 0, 1234, 77, 500, dark, white);
    CHECK(list != NULL);
    if (!list)
        return 1;
    CHECK(wm_send(list, list, CTL_LIST_USER, 0, 0) == 1234);
    char buf[256];
    for (int i = 0; i < 30; i++) {
        snprintf(buf, sizeof buf, "Line %d of the list", i);
        wm_send(list, list, CTL_LIST_ADD, (intptr_t)buf, i * 10);
    }
    CHECK(wm_send(list, list, CTL_LIST_COUNT, 0, 0) == 30);
    wm_send(list, list, CTL_LIST_SELECT, 0, 0);
    pump(2);
    int rows_top = (int)wm_send(list, list, CTL_LIST_GET_TOP, 0, 0);
    CHECK(rows_top == 0);
    /* click the third row */
    GfxRect rr;
    wm_send(list, list, CTL_LIST_ROW_RECT, (intptr_t)&rr, 2);
    x = (rr.left + rr.right) / 2;
    y = (rr.top + rr.bottom) / 2;
    wm_point_to_screen(list, &x, &y);
    click(x, y);
    pump(3);
    CHECK(wm_send(list, list, CTL_LIST_GET_SEL, 0, 0) == 2);
    CHECK(g_list_sel == 2);
    CHECK(wm_send(list, list, CTL_LIST_GET_DATA, -1, 0) == 20);
    CHECK(wm_send(list, list, CTL_LIST_GET_TEXT, -1, (intptr_t)buf) > 0 && strcmp(buf, "Line 2 of the list") == 0);
    /* the selected row is inverted: its background is the text color */
    {
        int bx = rr.right - 2, by = rr.top + 1;
        wm_point_to_screen(list, &bx, &by);
        CHECK(spx(bx, by) == white);
        int ox = rr.right - 2, oy = rr.top + 1 + (rr.bottom - rr.top + 1) * 2;
        wm_point_to_screen(list, &ox, &oy);
        CHECK(spx(ox, oy) == dark);
    }
    /* scroll it: the scrollbar's down arrow, repeatedly, moves the
     * selection and finally the top line */
    WmWindow *ltrack = list->children, *lfwd = NULL;
    ctl_scroll_parts(ltrack, NULL, &lfwd, NULL);
    CHECK(lfwd != NULL);
    if (lfwd) {
        centre_of(lfwd, &x, &y);
        CHECK(wm_window_at(x, y) == lfwd);
        for (int i = 0; i < 12; i++) {
            click(x, y);
            pump(4);
        }
    }
    int sel = (int)wm_send(list, list, CTL_LIST_GET_SEL, 0, 0);
    int top = (int)wm_send(list, list, CTL_LIST_GET_TOP, 0, 0);
    CHECK(sel == 14);
    CHECK(top > 0 && top <= sel);
    CHECK(ctl_scroll_get_pos(ltrack) == 14);
    CHECK(wm_send(list, list, CTL_LIST_DELETE, 0, 0) == 1);
    CHECK(wm_send(list, list, CTL_LIST_COUNT, 0, 0) == 29);

    /* the text field */
    GfxRect fr = {10, 189, 170, 188};
    WmWindow *field = ctl_text_create(panel, &fr, 55, dark, white, white, white, dark, 500, 0);
    CHECK(field != NULL);
    if (!field)
        return 1;
    wm_redraw(panel, NULL);
    centre_of(field, &x, &y);
    click(x, y);
    pump(3);
    CHECK(wm_focus() == field);
    type_text("Hello, wo@rld!");
    pump(20);
    CHECK(strcmp(ctl_text_get(field), "Hello, world!") == 0); /* '@' isn't accepted */
    push_key(WM_KEY_CHAR, 0, 8);
    pump(2);
    CHECK(strcmp(ctl_text_get(field), "Hello, world") == 0);
    push_key(WM_KEY_CHAR, 0, '\r');
    pump(2);
    CHECK(g_enter_count == 1 && g_enter_id == 55);
    /* the text is drawn in white on dark */
    {
        int whites = 0;
        for (int yy = field->rect.top; yy <= field->rect.bottom; yy++)
            for (int xx = field->rect.left; xx <= field->rect.right; xx++) {
                int sx = xx, sy = yy;
                wm_point_to_screen(panel, &sx, &sy);
                whites += spx(sx, sy) == white;
            }
        CHECK(whites > 40);
    }
    wm_send(field, field, CTL_TEXT_MSG_SET, (intptr_t)"Replaced", 0);
    CHECK(strcmp(ctl_text_get(field), "Replaced") == 0 && wm_get_data(field, CTL_TEXT_LEN) == 8);

    /* the text viewer */
    GfxRect vr = {200, 349, 10, 150};
    WmWindow *view = ctl_view_create(panel, &vr, 0, 9, 500, dark, white, white, 0x8000);
    CHECK(view != NULL && view->vscroll != NULL);
    if (view) {
        wm_send(view, view, CTL_VIEW_SET_TEXT,
                (intptr_t)"The quick brown fox jumps over the lazy dog. It keeps on jumping, line after line, "
                          "until the viewer has to wrap the words onto many lines and scroll.\n\nA second "
                          "paragraph follows, with more words to fill the remaining space and then some more "
                          "words so that the scrollbar has something to do.\n\nA third paragraph makes sure "
                          "that there are more lines than fit, by a good margin, so that scrolling by two lines "
                          "is possible at all.",
                0);
        wm_send(view, view, CTL_VIEW_REDRAW, 0, 0);
        CHECK(wm_get_data(view, 0x24) > 8);
        WmWindow *vfwd = NULL;
        ctl_scroll_parts(view->vscroll, NULL, &vfwd, NULL);
        if (vfwd) {
            centre_of(vfwd, &x, &y);
            CHECK(wm_window_at(x, y) == vfwd);
            click(x, y);
            pump(4);
            click(x, y);
            pump(4);
        }
        CHECK(wm_get_data(view, 0x20) == 2);
    }
    gfx_present();
    if (!write_ppm(argv[3], "widgets"))
        g_failures++;

    /* --- message box: modal, button 2 clicked from the pump ------------ */
    g_script = SCRIPT_MSGBOX_CLICK2;
    g_pump_calls = 0;
    WmWindow *prev_focus = wm_focus();
    int r = ctl_message_box(-1, -1, "Hey you!", "Save Game Before Loading?", "&Yes\0&No\0~Cancel\0", 0);
    CHECK(g_seen_dialog);
    CHECK(r == 2);
    CHECK(!wm_is_visible(ctl_msgbox_window()));
    CHECK(wm_focus() == prev_focus);
    CHECK(paused == 0);

    /* the same through the non-blocking half: Esc picks the '~' button */
    g_script = SCRIPT_NONE;
    CHECK(ctl_msgbox_open(-1, -1, "A poignant query...",
                          "Do You Really Want To Start a New Game? This text is long enough to wrap over a few "
                          "lines of the box.\n\nIt has a second paragraph.",
                          "&Yes\0&No\0~Cancel\0", 0));
    WmWindow *dlg = ctl_msgbox_window();
    CHECK(paused == 1);
    CHECK(wm_is_visible(dlg) && wm_focus() == dlg);
    /* centred, on screen */
    CHECK(dlg->rect.left == (W - (dlg->rect.right - dlg->rect.left + 1)) / 2);
    centre_of(ctl_msgbox_button(0), &x, &y);
    CHECK(shows(dlg, (dlg->rect.left + dlg->rect.right) / 2, dlg->rect.top + 30));
    CHECK(wm_is_visible(ctl_msgbox_button(2)) && !wm_is_visible(ctl_msgbox_button(3)));
    gfx_present();
    if (!write_ppm(argv[3], "msgbox"))
        g_failures++;
    key_tap(0x1b, 0x1b);
    pump(4);
    CHECK(ctl_msgbox_result() == 3);
    CHECK(ctl_msgbox_close() == 3);
    CHECK(!wm_is_visible(dlg));
    CHECK(shows(panel, 255, 125)); /* what was under it */

    /* a button's key: 'N' */
    CHECK(ctl_msgbox_open(100, 100, NULL, "Quit?", "&Yes\0&No\0", 0));
    CHECK(ctl_msgbox_window()->rect.left == 100);
    key_tap('N', 'N');
    pump(4);
    CHECK(ctl_msgbox_close() == 2);
    /* Tab moves the focus, Enter presses it */
    CHECK(ctl_msgbox_open(-1, -1, NULL, "Tab test", "One\0Two\0Three\0", 0));
    key_tap(9, 9);
    key_tap(9, 9);
    key_tap(0x0d, 0x0d);
    pump(8);
    CHECK(ctl_msgbox_close() == 3);
    /* a timed box closes by itself with 1 */
    g_script = SCRIPT_MSGBOX_TIMEOUT;
    g_pump_calls = 0;
    CHECK(ctl_message_box(-1, -1, NULL, "Bye.", "OK\0Wait\0", 30) == 1);

    /* --- the text prompt ------------------------------------------------- */
    g_script = SCRIPT_PROMPT;
    g_pump_calls = 0;
    const char *ans = ctl_prompt(-1, -1, "Name", "Enter a name:", "default");
    CHECK(ans && strcmp(ans, "abc") == 0);
    g_script = SCRIPT_NONE;

    if (window) {
        printf("close the window or press a key\n");
        wm_hooks.key_poll = NULL;
        for (;;) {
            PlatEvent ev;
            bool quit = false;
            while (plat_poll_event(&ev)) {
                if (ev.type == PLAT_EV_QUIT || ev.type == PLAT_EV_CLOSE || ev.type == PLAT_EV_KEY_DOWN)
                    quit = true;
                gfx_mouse_handle_event(&ev);
            }
            if (quit)
                break;
            wm_idle();
            ctl_idle();
            gfx_present();
            plat_delay_ms(10);
        }
    }

    ctl_shutdown();
    wm_shutdown();
    CHECK(rgn_pool_free_count() == RGN_POOL_SIZE);
    for (int i = 0; i < 8; i++)
        if (sb[i])
            gfx_surface_destroy(sb[i]);
    for (int i = 0; i < 9; i++)
        if (mk[i])
            gfx_surface_destroy(mk[i]);
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ui_widgets_test: all passed, wrote %s_{menu,widgets,msgbox}.ppm\n", argv[3]);
    return 0;
}
