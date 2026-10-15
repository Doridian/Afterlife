/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The tutorial: the runner (0x425e3b), the script's prologue and epilogue
 * (0x41f611), the window restore (0x425ab0, 0x4259c6), the attract mode
 * (0x4ac69c) and the scripted input (0x4ac2de-0x4acbee). The lessons
 * themselves are op tables (tutorial_script.c). See game/tutorial.h.
 */
#include "game/tutorial.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game/build.h"
#include "game/input.h"
#include "game/mapobj.h"
#include "game/music.h"
#include "game/shell.h"
#include "game/shell_int.h"
#include "game/sim.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/state.h"
#include "game/timer.h"
#include "game/world.h"
#include "gfx/akos_talk.h"
#include "gfx/draw.h"
#include "gfx/mouse.h"
#include "ui/about.h"
#include "ui/advisors.h"
#include "ui/advisors_int.h"
#include "ui/controls.h"
#include "ui/ctrlpanel.h"
#include "ui/exe_text.h"
#include "ui/graphview.h"
#include "ui/macro.h"
#include "ui/mapview.h"
#include "ui/menu.h"
#include "ui/microview.h"
#include "ui/news.h"
#include "ui/planetview.h"
#include "ui/soulview.h"
#include "ui/statusbar.h"
#include "ui/tutorialwin.h"
#include "ui/views.h"
#include "ui/wm.h"

TutorialHooks tutorial_hooks;

/* The game's windows as game_main lists them (0x646390, 0x12 entries of
 * {flags, window, x, y}; the port has no debug window (0x5f7470) or
 * 0x638ea0 here). */
#define TUT_WINDOWS 15
enum { WF_VISIBLE = 1, WF_MINIMIZED = 2, WF_PLACED = 4 };

static struct {
    bool active;           /* 0x63869c: the scripted input runs */
    TutGlide glide;
    uint32_t last_step;    /* 0x66fce0 */
    bool temp_saved;       /* 0x65402c: slot 200 holds the player's game */
    bool microview_owned;  /* 0x654030 */
    int view_plane, view_x, view_y; /* 0x4f3084 / 0x4f3064 / 0x4f3080 */
    bool blocks_off[3];    /* 0x6392d4 / 0x639290 / 0x63931c: zone block toggles were up */
    struct {
        unsigned flags;
        WmWindow *w;
        int x, y;
    } win[TUT_WINDOWS];
    uint8_t saved_speed;   /* lesson 3's uVar1 */
} tut;

bool tutorial_active(void) { return tut.active; }
const TutGlide *tutorial_glide(void) { return &tut.glide; }

static uint32_t now_ms(void) { return tutorial_hooks.clock ? tutorial_hooks.clock() : tm_get_time(); }

/* the name table at the top of 0x41f611 */
static const uint32_t k_names[TUTORIAL_LESSONS] = {0x65a100, 0x65a11c, 0x65a134, 0x65a150, 0x65a170,
                                                   0x65a188, 0x65a1a4, 0x65a1c0, 0x65a1d8};
#define TEXT_TEMP_TITLE 0x65a1e8 /* "Temp Game From Hell" */
#define TEXT_ADVISOR_TITLE 0x65a1fc
#define TEXT_DONE_FMT 0x65eb54
#define TEXT_DONE_TITLE 0x65eb84
#define TEXT_DONE_BUTTONS 0x65eb7c

const char *tutorial_lesson_name(int n) { return n >= 1 && n <= TUTORIAL_LESSONS ? exe_str(k_names[n - 1]) : ""; }

const char *tutorial_op_name(int op)
{
    static const char *const k[TO_COUNT] = {
        "END",   "IF",  "ENDIF", "SAY",   "WAIT_TALK", "PUMP",    "FRAME",      "GLIDE_CTL",  "GLIDE_MENU",
        "GLIDE_CELL", "GLIDE_WIN", "SYNC", "CLICK", "DOWN", "UP", "RDOWN", "RUP", "SECONDS", "YEARS", "CENTER",
        "MOVE_WIN", "SPEED_PUSH", "SPEED_POP", "ANIM_CELL", "MAPVIEW_OUT", "CLEAR_TEXT", "MICROVIEW",
    };
    return op >= 0 && op < TO_COUNT ? k[op] : "?";
}

/* ======================================================================== */
/* The glide (0x4ac880, 0x4ac72b)                                            */
/* ======================================================================== */

void tutorial_glide_start(TutGlide *g, int x, int y, int tx, int ty, int speed)
{
    g->x = x;
    g->y = y;
    g->target_x = tx;
    g->target_y = ty;
    g->dy = ty - y;
    g->dx = tx - x;
    g->step_x = g->dx < 0 ? -speed : speed;
    g->step_y = g->dy < 0 ? -speed : speed;
    g->active = true;
}

/* one axis: the step, or what is left if that is less */
static int axis_step(int step, int left)
{
    if (step < 0)
        return left <= step ? step : left;
    return step <= left ? step : left;
}

bool tutorial_glide_step(TutGlide *g)
{
    if (!g->active)
        return false;
    g->x += axis_step(g->step_x, g->target_x - g->x);
    g->y += axis_step(g->step_y, g->target_y - g->y);
    if (g->target_x == g->x && g->target_y == g->y) {
        g->end_x = g->target_x;
        g->end_y = g->target_y;
        g->active = false;
    }
    return true;
}

/* 0x4ac72b: the attract mode's task, on every pump */
static void glide_task(void)
{
    uint32_t now = now_ms();
    if (!(tut.last_step + TUTORIAL_GLIDE_MS < now))
        return;
    tut.last_step = now;
    if (!tut.glide.active)
        return;
    tutorial_glide_step(&tut.glide);
    gfx_mouse_set_pos(tut.glide.x, tut.glide.y);
    wm_mouse_event(1, tut.glide.x, tut.glide.y);
}

/* the task table's key for 0x4ac69c */
static int task_key(void) { return 0; }

/* 0x4ac880 */
static void glide_to(int x, int y)
{
    if (!tut.active)
        return;
    int cx, cy;
    gfx_mouse_get_state(&cx, &cy);
    tutorial_glide_start(&tut.glide, cx, cy, x, y, TUTORIAL_GLIDE_SPEED);
}

/* 0x4ac56f */
static void glide_wait(void)
{
    if (!tut.active)
        return;
    while (tut.glide.active)
        sys_pump();
}

/* the centre of window w (its rect is in its parent's coordinates) */
static void window_center(WmWindow *w, int *x, int *y)
{
    GfxRect r = w->rect;
    wm_local_to_screen(w->parent, &r);
    *x = ((r.right - r.left + 1) >> 1) + r.left;
    *y = ((r.bottom - r.top + 1) >> 1) + r.top;
}

static WmWindow *op_window(int which)
{
    switch (which) {
    case TW_CTRLPANEL:
        return g_ctrlpanel_window;
    case TW_GRAPHVIEW:
        return graphview_window();
    case TW_MAPVIEW:
        return mapview_window();
    case TW_MICROVIEW:
        return microview_window();
    case TW_ADVISOR:
        return advisor_window();
    case TW_MAIN:
        return g_main_window;
    default:
        return NULL;
    }
}

/* 0x4ac939: to control `id` of `w` */
static void glide_control(WmWindow *w, int id)
{
    if (!tut.active || !w)
        return;
    WmWindow *c = ctl_find_by_id(w, id);
    if (!c)
        return;
    int x, y;
    window_center(c, &x, &y);
    glide_to(x, y);
}

/* 0x4ac9dd: to menu item `id` */
static void glide_menu(int id)
{
    if (!tut.active)
        return;
    WmWindow *item = menu_find_item((uint32_t)id);
    if (!item)
        return;
    int x, y;
    window_center(item, &x, &y);
    glide_to(x, y);
}

/* the original's ((cell_h >> zoom) << magnify) style scaling */
static int zoomed(int v) { return (v >> g_map_view.zoom) << g_map_view.magnify; }

/* 0x4aca7d: to the middle of map cell (x, y) of `plane`, scrolling the
 * main view until it is in the client area. (Port: gives up scrolling
 * after a few tries; the original would loop.) */
static void glide_cell(int plane, int x, int y)
{
    if (plane < 1 || plane > 3)
        return;
    for (int tries = 0;; tries++) {
        if (!tut.active)
            return;
        shell_select_plane(plane);
        shell_view_sync_origin();
        int cx, cy;
        map_view_cell_to_screen(&g_map_view, x, y, &cx, &cy);
        int sx = g_map_view.origin_x + cx;
        int sy = g_map_view.origin_y + cy + (zoomed(g_map_view.cell_h) >> 1);
        WmWindow *m = g_main_window;
        if (!m)
            return;
        if ((sx >= m->client.left && sx <= m->client.right && sy >= m->client.top && sy <= m->client.bottom) ||
            tries >= 4) {
            glide_to(sx + m->rect.left, sy + m->rect.top);
            return;
        }
        shell_center_on_cell(plane, x, y);
        shell_frame_update();
    }
}

/* the start of 0x4ac2de..0x4ac4f0: back to where the last glide ended if
 * the pointer was moved */
static bool at_glide_end(void)
{
    if (!tut.active)
        return false;
    int x, y;
    gfx_mouse_get_state(&x, &y);
    if (tut.glide.end_x != x || tut.glide.end_y != y) {
        glide_to(tut.glide.end_x, tut.glide.end_y);
        glide_wait();
    }
    return true;
}

static void mouse(int event) { wm_mouse_event(event, tut.glide.end_x, tut.glide.end_y); }

static void click(void) /* 0x4ac3dc */
{
    if (at_glide_end()) {
        mouse(2);
        mouse(5);
    }
}
static void press(void) /* 0x4ac471 */
{
    if (at_glide_end())
        mouse(2);
}
static void release(void) /* 0x4ac4f0 */
{
    if (at_glide_end())
        mouse(5);
}
static void rpress(void) /* 0x4ac2de */
{
    if (at_glide_end())
        mouse(3);
}
static void rrelease(void) /* 0x4ac35d */
{
    if (at_glide_end())
        mouse(6);
}

/* one turn of the game loop, as the waits run it */
static void run_game(void)
{
    sys_pump();
    shell_frame_update();
    g_sim.paused = (uint8_t)(g_game_paused != 0);
    sim_update(g_game_speed);
}

/* 0x4ac5a3: the game runs for n seconds. (Port: also stops when the
 * lesson was stopped, which the original only checks on entry.) */
static void wait_seconds(int n)
{
    if (!tut.active)
        return;
    uint32_t t0 = now_ms();
    do
        run_game();
    while (tut.active && now_ms() < t0 + (uint32_t)n * 1000u);
}

/* 0x4ac61f: the game runs for n years, if it isn't stopped. (Port: stops
 * with the lesson; the original went on running the player's reloaded
 * game for the rest of the years.) */
static void wait_years(int n)
{
    uint32_t y0 = g_state.year;
    if (g_game_speed == 0 || !tut.active)
        return;
    do
        run_game();
    while (tut.active && g_state.year < y0 + (uint32_t)n);
}

/* 0x4acbee (unused by the lessons): a key press and release */
void tutorial_post_key(uint8_t ch)
{
    if (!tut.active)
        return;
    kb_post(1, 0, ch);
    kb_post(3, 0, ch);
}

/* ======================================================================== */
/* The attract mode (0x4ac69c)                                               */
/* ======================================================================== */

static void attract_stop(bool on)
{
    if (!on) {
        wm_set_input_live(true);
        tut.active = false;
        tut.glide.active = false;
        sys_unregister_task(task_key);
        wm_flush_input();
        tutorial_run(0);
    } else {
        music_fade_out(4);
        wm_set_input_live(false);
        tut.active = true;
        wm_flush_input();
        sys_register_task(task_key, NULL, NULL, NULL, glide_task);
    }
}

void tutorial_attract_stop(void) { attract_stop(false); }

/* ======================================================================== */
/* Windows (0x4259c6, 0x425ab0 and the hiding part of 0x425e3b)               */
/* ======================================================================== */

static void fill_window_table(void)
{
    WmWindow *const w[TUT_WINDOWS] = {
        graphview_window(), microview_window(), g_tilechooser_window, soulview_window(), g_ctrlpanel_window,
        advisor_window(),   NULL,               mapview_window(),     news_window(),     planetview_window(),
        macro_window(),     tutorialwin_window(),               about_window(),       planetview_disaster_window(), NULL,
    };
    for (int i = 0; i < TUT_WINDOWS; i++)
        tut.win[i].w = w[i];
}

/* 0x4259c6: the view buttons and menu checks follow the windows */
static void sync_view_buttons(void)
{
    static const struct {
        WmWindow *(*win)(void);
        int button, menu;
    } k[] = {
        {planetview_window, 0x1e, MENU_PLANETVIEW},
        {mapview_window, 0x20, MENU_MAPVIEW},
        {macro_window, 0x25, MENU_MACRO},
        {graphview_window, 0x1b, MENU_GRAPHVIEW},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        WmWindow *w = k[i].win();
        bool on = w && wm_is_visible(w);
        if (shell_ctrl_button(k[i].button))
            ctl_button_set_pressed(shell_ctrl_button(k[i].button), on, true);
        shell_menu_set_checked((unsigned)k[i].menu, on);
    }
}

/* 0x425e3b: every visible window but the control panel is hidden, its
 * position or its minimized state remembered */
static void hide_windows(void)
{
    for (int i = 0; i < TUT_WINDOWS; i++) {
        WmWindow *w = tut.win[i].w;
        if (!w || w == g_ctrlpanel_window || !(w->flags & WM_F_VISIBLE))
            continue;
        tut.win[i].flags |= WF_VISIBLE;
        if (!wm_is_minimized(w)) {
            tut.win[i].flags |= WF_PLACED;
            tut.win[i].x = w->rect.left;
            tut.win[i].y = w->rect.top;
        } else {
            wm_set_minimized(w, false);
            tut.win[i].flags |= WF_MINIMIZED;
        }
        wm_show(w, false);
    }
}

/* 0x425ab0 */
static void restore_windows(void)
{
    WmWindow *adv = advisor_window();
    if (adv)
        wm_show(adv, false);
    if (shell_ctrl_button(0x1f))
        ctl_button_set_pressed(shell_ctrl_button(0x1f), false, true);
    shell_menu_set_checked(MENU_HELPERS, false);
    if (g_ctrlpanel_window)
        wm_show(g_ctrlpanel_window, true);
    shell_menu_set_checked(MENU_CTRLPANEL, true);
    for (int i = 0; i < 3; i++) {
        WmWindow *b = shell_ctrl_button(0x17 + i);
        if (tut.blocks_off[i] && b && ctl_is_pressed(b)) {
            ctl_button_set_pressed(b, false, true);
            g_game_paused = 1;
            build_zone_blocks(0x17 + i, true);
            g_shell_repaint = 1;
            g_game_paused = 0;
            sys_pump();
            shell_frame_update();
        }
    }
    for (int i = 0; i < TUT_WINDOWS; i++) {
        WmWindow *w = tut.win[i].w;
        if (!w)
            continue;
        if (w == adv || w == g_ctrlpanel_window) {
            tut.win[i].flags = 0;
            continue;
        }
        if (tut.microview_owned && w == microview_window()) {
            tut.win[i].flags = 0;
            tut.microview_owned = false;
        }
        if (!(tut.win[i].flags & WF_VISIBLE)) {
            wm_show(w, false);
            tut.win[i].flags = 0;
            continue;
        }
        tut.win[i].flags &= ~(unsigned)WF_VISIBLE;
        if (!(tut.win[i].flags & WF_MINIMIZED)) {
            tut.win[i].flags &= ~(unsigned)WF_PLACED;
            wm_move(w, tut.win[i].x, tut.win[i].y);
            wm_show(w, true);
        } else {
            wm_show(w, true);
            wm_send(w, w, WM_MSG_MINIMIZED, 1, 0);
            wm_set_minimized(w, true);
            tut.win[i].flags &= ~(unsigned)WF_MINIMIZED;
        }
    }
    tut.microview_owned = false;
    sync_view_buttons();
    if (g_main_window && g_main_window->menubar)
        menu_show(g_main_window->menubar, true);
    ctrlpanel_click(0x15);
}

/* ======================================================================== */
/* The lesson ops                                                            */
/* ======================================================================== */

static bool may_talk(uint32_t stamp) { return !g_adv_talk.silenced && g_adv_talk.stamp == stamp; }

static void exec_op(const TutOp *o)
{
    WmWindow *m = g_main_window;
    switch (o->op) {
    case TO_SAY:
        if (o->a == 1 || o->a == 2)
            akos_talk_say(o->a, &g_adv_talk.text_rect[o->a], exe_str((uint32_t)o->b), (uint32_t)o->c);
        break;
    case TO_WAIT_TALK:
        akos_talk_wait(o->a);
        break;
    case TO_PUMP:
        sys_pump();
        break;
    case TO_FRAME:
        shell_frame_update();
        break;
    case TO_GLIDE_CTL:
        glide_control(op_window(o->a), o->b);
        break;
    case TO_GLIDE_MENU:
        glide_menu(o->a);
        break;
    case TO_GLIDE_CELL:
        glide_cell(o->a, o->b, o->c);
        break;
    case TO_GLIDE_WIN: {
        WmWindow *wx = op_window(o->a), *wy = op_window(o->c);
        if (tut.active && wx && wy)
            glide_to(wx->rect.left + o->b, wy->rect.top + o->d);
        break;
    }
    case TO_SYNC:
        glide_wait();
        break;
    case TO_CLICK:
        click();
        break;
    case TO_DOWN:
        press();
        break;
    case TO_UP:
        release();
        break;
    case TO_RDOWN:
        rpress();
        break;
    case TO_RUP:
        rrelease();
        break;
    case TO_SECONDS:
        wait_seconds(o->a);
        break;
    case TO_YEARS:
        wait_years(o->a);
        break;
    case TO_CENTER:
        shell_center_on_cell(o->a, o->b, o->c);
        break;
    case TO_MOVE_WIN: {
        WmWindow *w = op_window(o->a);
        if (w && m)
            wm_move(w, (o->b ? m->client.right : m->client.left) + o->c,
                    (o->d ? m->client.bottom : m->client.top) + o->e);
        break;
    }
    case TO_SPEED_PUSH:
        tut.saved_speed = g_game_speed;
        g_game_speed = (uint8_t)o->a;
        break;
    case TO_SPEED_POP:
        g_game_speed = tut.saved_speed;
        break;
    case TO_ANIM_CELL:
        mapobj_place(0x52, 0x60, 0, 3, 2, 2, 3, 5);
        break;
    case TO_MAPVIEW_OUT:
        mapview_zoom(0, 0, 0);
        break;
    case TO_CLEAR_TEXT:
        if (advisor_window())
            gfx_fill_rect(advisor_window()->surface, 0xe, 0x91, 0x120, 0x46, 0x13);
        break;
    case TO_MICROVIEW:
        tut.microview_owned = true;
        break;
    default:
        break;
    }
}

/* the switch of 0x41f611 */
static void run_ops(int n, uint32_t stamp)
{
    const TutOp *ops = tutorial_ops(n);
    if (!ops)
        return;
    for (int i = 0; ops[i].op != TO_END; i++) {
        const TutOp *o = &ops[i];
        if (tutorial_hooks.step)
            tutorial_hooks.step(n, i, o);
        if (o->op == TO_ENDIF)
            continue;
        if (o->op == TO_IF) {
            bool taken = tutorial_hooks.dry ? !tutorial_hooks.dry_silent : may_talk(stamp);
            if (!taken) { /* to the matching ENDIF */
                int depth = 1;
                while (depth > 0 && ops[i + 1].op != TO_END) {
                    i++;
                    if (ops[i].op == TO_IF)
                        depth++;
                    else if (ops[i].op == TO_ENDIF)
                        depth--;
                }
            }
            continue;
        }
        if (!tutorial_hooks.dry)
            exec_op(o);
    }
    if (tutorial_hooks.lesson_done)
        tutorial_hooks.lesson_done(n);
}

/* ======================================================================== */
/* The script (0x41f611) and the runner (0x425e3b)                           */
/* ======================================================================== */

/* glide to control panel button `id`, click it, one frame */
static void press_panel_button(int id)
{
    glide_control(g_ctrlpanel_window, id);
    glide_wait();
    click();
    sys_pump();
    shell_frame_update();
}

static bool view_set_up(void)
{
    return g_map_view.zoom >= 3 && g_map_view.dir_x == 1 && g_map_view.dir_y == 1;
}

static void script(int n)
{
    uint32_t stamp = g_adv_talk.stamp;
    WmWindow *adv = advisor_window();
    tut.microview_owned = false;
    char temp[1100];
    shell_slot_path(1, TUTORIAL_TEMP_SLOT, temp, sizeof temp);
    if (!shell_save_game_to(temp, exe_str(TEXT_TEMP_TITLE))) {
        attract_stop(false);
        return;
    }
    tut.temp_saved = true;
    if (!shell_load_game_resource((uint32_t)(TUTORIAL_SAVE_BASE + n))) {
        attract_stop(false);
        return;
    }
    if (g_state.pennies.hi < 0)
        g_state.pennies.hi = 0;
    statusbar_draw();
    shell_menu_set_checked(MENU_HELPERS, true);
    shell_menu_set_checked(MENU_CTRLPANEL, true);
    if (shell_ctrl_button(0x1f))
        ctl_button_set_pressed(shell_ctrl_button(0x1f), true, true);
    wm_set_title(adv, exe_str(TEXT_ADVISOR_TITLE));
    views_draw_title(adv);
    wm_show(adv, true);
    wm_show(g_ctrlpanel_window, true);
    /* the view: zone blocks on, Hell/Heaven/Karma; zoomed all the way out;
     * not rotated */
    for (int id = 0x17; id <= 0x19; id++)
        if (shell_ctrl_button(id) && !ctl_is_pressed(shell_ctrl_button(id)))
            press_panel_button(id);
    glide_cell(2, 0x31, 0x32);
    if (g_map_view.magnify != 0)
        press_panel_button(0x1d);
    if (g_map_view.zoom < 3)
        for (int z = g_map_view.zoom; z < 3; z++)
            press_panel_button(0x1d);
    if (g_map_view.dir_x != 1 || g_map_view.dir_y != 1)
        press_panel_button(0x21);
    do {
        sys_pump();
        shell_frame_update();
        if (!tut.active)
            goto done;
    } while (!view_set_up());
    sys_pump();
    shell_frame_update();
    if (tut.active)
        run_ops(n, stamp);
done:
    attract_stop(false);
    adv->proc = advisor_proc;
    restore_windows();
    char text[200];
    snprintf(text, sizeof text, exe_fmt(TEXT_DONE_FMT, "ds", "#%d: %s"), n, tutorial_lesson_name(n));
    char buttons[32];
    ctl_message_box(-1, -1, exe_str(TEXT_DONE_TITLE), text, exe_buttons(TEXT_DONE_BUTTONS, 1, buttons, sizeof buttons),
                    0);
}

void tutorial_run(int n)
{
    if (tutorial_hooks.dry) {
        run_ops(n, g_adv_talk.stamp);
        return;
    }
    if (n == 0) {
        g_adv_talk.silenced = true;
        snd_slot_stop_stream(SND_VOICE_SLOT);
        akos_talk_close(1);
        akos_talk_close(2);
        if (tut.temp_saved) {
            tut.temp_saved = false;
            char temp[1100];
            shell_slot_path(1, TUTORIAL_TEMP_SLOT, temp, sizeof temp);
            shell_load_game_from(temp);
        }
        shell_center_on_cell(tut.view_plane, tut.view_x, tut.view_y);
        return;
    }
    WmWindow *adv = advisor_window(), *m = g_main_window;
    if (n < 1 || n > TUTORIAL_LESSONS || !adv || !m || !g_ctrlpanel_window || tut.active)
        return;
    tut.view_plane = 1;
    tut.view_x = tut.view_y = 0;
    shell_view_cell_at(((m->client.right - m->client.left) >> 1) + m->client.left,
                       ((m->client.bottom - m->client.top) >> 1) + m->client.top, &tut.view_plane, &tut.view_x,
                       &tut.view_y);
    wm_raise(adv);
    if (wm_is_minimized(adv)) {
        if (!wm_is_visible(adv))
            wm_show(adv, true);
        wm_set_minimized(adv, false);
    }
    snd_slot_stop_stream(SND_VOICE_SLOT);
    tut.temp_saved = false;
    g_adv_talk.silenced = false;
    advisor_tutorial_setup();
    for (int i = 0; i < 3; i++)
        tut.blocks_off[i] = shell_ctrl_button(0x17 + i) && !ctl_is_pressed(shell_ctrl_button(0x17 + i));
    attract_stop(true);
    fill_window_table();
    hide_windows();
    sync_view_buttons();
    wm_move(adv, m->client.right - 0x140, m->client.top + 4);
    wm_move(g_ctrlpanel_window, m->client.left + 4, m->client.top + 4);
    adv->proc = advisor_tutorial_proc;
    script(n);
}
