/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The "Audio Settings" window (ALIFE.EXE 0x4756f4-0x4761a0). Overview:
 * audiodlg.h.
 *
 * Layout (local coordinates of bitmap 4000): the close box 0x259 at
 * (0, 0); the toggles (bitmap 0xfa1, normal | pressed) at x 0x17, y 0x28
 * + 0x26 n; the sliders (horizontal, 0x28..0xe6) 0x22..0x3a + 0x26 n with
 * their labels above them at (0x28, 0x18 + 0x26 n).
 */
#include "ui/audiodlg.h"

#include "game/music.h"
#include "game/shell.h"
#include "game/sound.h"
#include "gfx/font.h"
#include "gfx/surface.h"
#include "imuse/imuse.h"
#include "ui/controls.h"
#include "ui/views.h"

#define AUDIODLG_BMP 4000
#define CLOSE_BMP 0x259
#define TOGGLE_BMP 0xfa1
#define ROWS 4
#define ROW_STEP 0x26

/* iMUSE groups (docs/imuse.md) */
enum { GROUP_MASTER = 0, GROUP_SFX = 1, GROUP_VOICE = 2, GROUP_MUSIC_SLIDER = 3, GROUP_MUSIC = 4 };

static WmWindow *g_win;          /* 0x638ea0 */
static WmWindow *g_toggle[ROWS]; /* 0x4f3238, 0x4f33cc, 0x4f323c, 0x4f33d8 */
static WmWindow *g_slider[ROWS]; /* 0x4f3244, 0x4f3248, 0x4f324c, 0x4f3250 */
static int g_master_vol;         /* 0x4f33c8: volumes while switched off */
static int g_music_vol;          /* 0x4f3234 */
static int g_voice_vol;          /* 0x4f33d0 */
static int g_sfx_vol;            /* 0x4f33d4 */
static bool g_dragging;          /* 0x6643b8 */
static int g_grab_x;             /* 0x4f3230 */
static int g_grab_y;             /* 0x4f3240 */

static const char *const g_labels[ROWS] = {
    "Master Volume",   /* 0x6643cc */
    "Music Volume",    /* 0x6643dc */
    "Voice Volume",    /* 0x6643ec */
    "Sound FX Volume", /* 0x6643fc */
};

WmWindow *audiodlg_window(void) { return g_win; }

/* 0x4756f4 snd_audio_dialog_create. (It also clears 0x168 bytes at
 * 0x4f3260 that nothing reads.) */
bool audiodlg_create(WmWindow *main)
{
    if (g_win)
        return true;
    imuse_set_group_vol(GROUP_MASTER, 0x7f);
    imuse_set_group_vol(GROUP_MUSIC, 0x7f);
    imuse_set_group_vol(GROUP_VOICE, 0x7f);
    imuse_set_group_vol(GROUP_SFX, 0x7f);
    GfxSurface *bg = gfx_load_bmp_resource(AUDIODLG_BMP, NULL);
    if (!bg)
        return false;
    /* the original creates it without a surface and hands it the bitmap */
    GfxRect r = {0, bg->width - 1, 0, bg->height - 1};
    g_win = wm_create_window(main, &r, audiodlg_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (g_win)
        gfx_blit(g_win->surface, 0, 0, bg->width, bg->height, bg, 0, 0);
    gfx_surface_destroy(bg);
    if (!g_win)
        return false;

    GfxSurface *cb = gfx_load_bmp_resource(CLOSE_BMP, NULL);
    if (cb) {
        GfxRect br = {0, cb->width - 1, 0, cb->height - 1};
        g_win->close_button = ctl_button_create(g_win, &br, AUDIODLG_CLOSE, CTL_STYLE_BITMAP, cb);
        if (!g_win->close_button)
            gfx_surface_destroy(cb);
    }
    /* the original hands all four toggles the same bitmap; here each owns
     * a copy */
    for (int i = 0; i < ROWS; i++) {
        GfxSurface *s = gfx_load_bmp_resource(TOGGLE_BMP, NULL);
        if (!s)
            continue;
        int y = 0x28 + ROW_STEP * i;
        GfxRect tr = {0x17, s->width + 0x16, y, s->height + y - 1};
        g_toggle[i] = ctl_button_create(g_win, &tr, (uint32_t)(AUDIODLG_MASTER + i),
                                        CTL_STYLE_BITMAP | CTL_STYLE_TOGGLE, s);
        if (g_toggle[i])
            ctl_button_set_pressed(g_toggle[i], true, true);
        else
            gfx_surface_destroy(s);
    }
    wm_set_title(g_win, "Audio Settings"); /* 0x6643bc */
    wm_center(g_win);
    for (int i = 0; i < ROWS; i++) {
        GfxRect sr = {0x28, 0xe6, 0x22 + ROW_STEP * i, 0x3a + ROW_STEP * i};
        g_slider[i] = ctl_scroll_create(g_win, &sr, (uint32_t)(AUDIODLG_MASTER_VOL + i), CTL_SCROLL_SHOW);
        gfx_font_draw_text(g_win->surface, 0x28, 0x18 + ROW_STEP * i, NULL, NULL, g_labels[i], GFX_TEXT_NOJUSTIFY);
        ctl_scroll_set_range(g_slider[i], 0, 0x7f);
        ctl_scroll_set_pos(g_slider[i], 0x7f);
    }
    views_draw_title(g_win);
    return true;
}

void audiodlg_destroy(void)
{
    if (g_win) {
        if (wm_capture() == g_win)
            wm_release_capture();
        wm_destroy(g_win);
    }
    g_win = NULL;
    for (int i = 0; i < ROWS; i++)
        g_toggle[i] = g_slider[i] = NULL;
    g_dragging = false;
}

/* game_view_proc 0x405430 (menu item 7) */
void audiodlg_show(bool on)
{
    if (!g_win)
        return;
    if (on)
        wm_raise(g_win);
    wm_show(g_win, on);
}

static void show_sliders(bool on)
{
    for (int i = 0; i < ROWS; i++)
        if (g_slider[i])
            wm_show(g_slider[i], on);
}

/* the toggles (w = the button, a = its id, b = now pressed) */
static void toggle(unsigned id, bool on)
{
    switch (id) {
    case AUDIODLG_MASTER:
        if (!on) {
            g_master_vol = imuse_set_group_vol(GROUP_MASTER, -1);
            imuse_set_group_vol(GROUP_MASTER, 0);
        } else {
            imuse_pause();
            imuse_resume();
            imuse_set_group_vol(GROUP_MASTER, g_master_vol);
        }
        break;
    case AUDIODLG_MUSIC:
        if (!on) {
            g_music_vol = imuse_set_group_vol(GROUP_MUSIC, -1);
            imuse_set_group_vol(GROUP_MUSIC_SLIDER, 0);
            imuse_set_group_vol(GROUP_MUSIC, 0);
            music_off();
        } else {
            imuse_set_group_vol(GROUP_MUSIC_SLIDER, g_music_vol);
            imuse_set_group_vol(GROUP_MUSIC, g_music_vol);
            music_on();
        }
        break;
    case AUDIODLG_VOICE: /* imuse_get_voice_vol 0x4d4130 / imuse_set_voice_vol 0x4d4110 */
        if (!on) {
            g_voice_vol = imuse_set_group_vol(GROUP_VOICE, -1);
            imuse_set_group_vol(GROUP_VOICE, 0);
        } else {
            imuse_set_group_vol(GROUP_VOICE, g_voice_vol);
        }
        break;
    case AUDIODLG_SFX:
        if (!on) {
            g_sfx_vol = imuse_set_group_vol(GROUP_SFX, -1);
            imuse_set_group_vol(GROUP_SFX, 0);
            snd_sfx_off();
        } else {
            imuse_set_group_vol(GROUP_SFX, g_sfx_vol);
            snd_sfx_on();
        }
        break;
    default:
        break;
    }
}

/* a slider moved (w = its track, a = the position): only while its
 * toggle is on */
static void slide(WmWindow *w, int pos)
{
    for (int i = 0; i < ROWS; i++) {
        if (w != g_slider[i])
            continue;
        if (!g_toggle[i] || !ctl_is_pressed(g_toggle[i]))
            return;
        ctl_scroll_set_pos(w, pos);
        switch (i) {
        case 0:
            imuse_set_group_vol(GROUP_MASTER, pos);
            break;
        case 1:
            imuse_set_group_vol(GROUP_MUSIC_SLIDER, pos);
            imuse_set_group_vol(GROUP_MUSIC, pos);
            break;
        case 2:
            imuse_set_group_vol(GROUP_VOICE, pos);
            break;
        default:
            imuse_set_group_vol(GROUP_SFX, pos);
            break;
        }
        return;
    }
}

/* 0x475bd9 snd_audio_dialog_proc */
intptr_t audiodlg_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MOUSEMOVE:
        if (g_dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - g_grab_x, (int)b - g_grab_y);
            /* local -> parent (the original converts to the screen, the
             * same for a child of the full-screen main window) */
            r.left += w->rect.left, r.right += w->rect.left;
            r.top += w->rect.top, r.bottom += w->rect.top;
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        g_dragging = true;
        g_game_paused = 1;
        wm_set_capture(w);
        g_grab_x = (int)a;
        g_grab_y = (int)b;
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        g_dragging = false;
        g_game_paused = 0;
        break;
    case WM_MSG_CTL_PRESS:
        switch (a) {
        case 1: /* the minimize button */
            show_sliders(false);
            break;
        case AUDIODLG_CLOSE:
            shell_menu_set_checked(MENU_AUDIO, false);
            wm_show(g_win, false);
            break;
        default:
            toggle((unsigned)a, b != 0);
            break;
        }
        /* (the original falls through into the next case, which does
         * nothing for a control) */
        break;
    case WM_MSG_MINIMIZED: /* the scrollbars have surfaces of their own */
        if (w == g_win)
            show_sliders(a == 0);
        break;
    case WM_MSG_HTRACK:
    case WM_MSG_HSCROLL:
        slide(w, (int)a);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}
