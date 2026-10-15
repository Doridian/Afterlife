/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The advisors, Jasper and Aria (ALIFE.EXE 0x417230-0x41f610). Overview:
 * advisors.h. The speeches are in advisors_text.c.
 *
 * Window layout (local coordinates of the 0x352 background bitmap):
 *   close button   (0, 0), bitmap 0x259
 *   Jasper         head area (0xe, 0xe) 0x8d x 0x7e, actor at (0x50, 0x8b)
 *   Aria           head area (0xa1, 0xe) 0x8c x 0x7e, actor at (0xeb, 0x8b)
 *   issue buttons  (0xe, 0x91 + i * 0xe), bitmap 0x354, five of them
 *   subtitles      (0x6f..0x129, 0x95..0xd2), right of the buttons
 *
 * The issues: advisor_update has each realm collect its five most urgent
 * issue codes (lower = more urgent) into PlaneStats.unk_808b, then merges
 * the two lists into the five buttons, each with a Heaven code, a Hell
 * code or both (the same issue in both realms). The label is the issue's
 * name, prefixed with glyph 0x13 for Hell and suffixed with 0x14 for
 * Heaven. Pressing a button plays the matching speech.
 *
 * In the scenario editor (g_build.editor) the buttons instead browse the
 * speeches: button 1 is code `debug` in Heaven, 2 in Hell, 3 in both, and
 * buttons 4 and 5 step the code by -5 / +5.
 */
#include "ui/advisors.h"

#include <stdio.h>
#include <string.h>

#include "game/build.h"
#include "game/economy.h"
#include "game/planet.h"
#include "game/shell.h"
#include "game/sound.h"
#include "game/util.h"
#include "game/world.h"
#include "gfx/akos.h"
#include "gfx/akos_talk.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/surface.h"
#include "ui/advisors_int.h"
#include "ui/controls.h"
#include "ui/views.h"

#define FONT 500
#define BG 0x13 /* head and subtitle background */

AdvisorTalk g_adv_talk;

static struct {
    WmWindow *win;                     /* 0x638660 */
    WmWindow *buttons[1 + ADV_ISSUES]; /* 0x639330: [0] close (1000), [1..5] issues (1001..1005) */
    int jasper, aria;                  /* 0x639294 / 0x6392c4: costume slots */
    uint32_t heaven[ADV_ISSUES];       /* 0x639300: per button, ADV_NONE = none */
    uint32_t hell[ADV_ISSUES];         /* 0x6392e0 */
    uint8_t debug;                     /* 0x6392f4: the editor's speech browser, 5..0xdc */
    bool dragging;                     /* 0x654034 */
    int grab_x, grab_y;                /* 0x4f308c / 0x4f3088 */
} adv;

WmWindow *advisor_window(void) { return adv.win; }

unsigned advisor_issue(int i, bool hell)
{
    if (i < 0 || i >= ADV_ISSUES)
        return ADV_NONE;
    return hell ? adv.hell[i] : adv.heaven[i];
}

/* 0x426427 */
void advisor_silence(void)
{
    g_adv_talk.silenced = true;
    snd_slot_stop_stream(AKOS_TALK_VOICE_SLOT);
}

/* 0x426449 */
void advisor_unsilence(void) { g_adv_talk.silenced = false; }

static WmWindow *button(WmWindow *parent, int x, int y, uint32_t id, uint32_t bitmap)
{
    GfxSurface *bmp = gfx_load_bmp_resource(bitmap, NULL);
    if (!bmp)
        return NULL;
    GfxRect r = {x, x + bmp->width - 1, y, y + bmp->height - 1};
    WmWindow *b = ctl_button_create(parent, &r, id, CTL_STYLE_BITMAP, bmp);
    if (!b)
        gfx_surface_destroy(bmp);
    return b;
}

/* 0x417230. The original also loads bitmap 0x353 (an issue button) twice
 * and copies a piece of the background into one copy; neither is used
 * again, so the port skips them. */
bool advisor_create(void)
{
    if (adv.win)
        return true;
    WmWindow *main = views_main_window() ? views_main_window() : g_main_window;
    if (!main)
        main = wm_root();
    GfxSurface *bg = gfx_load_bmp_resource(0x352, NULL);
    if (!bg)
        return false;
    akos_talk_set_font(FONT);
    int w = bg->width, h = bg->height;
    GfxRect r;
    r.left = 0x10;
    r.right = w + 0xf;
    r.top = main->client.bottom - h - 0x18;
    r.bottom = r.top + h - 1;
    adv.win = wm_create_window(main, &r, advisor_proc, WM_F_MINIMIZABLE | WM_F_NOFOCUS, 0);
    if (!adv.win) {
        gfx_surface_destroy(bg);
        return false;
    }
    wm_set_title(adv.win, "Jasper and Aria");
    gfx_blit(adv.win->surface, 0, 0, w, h, bg, 0, 0);
    gfx_surface_destroy(bg);
    adv.buttons[0] = button(adv.win, 0, 0, ADVISOR_ID_CLOSE, 0x259);
    adv.win->close_button = adv.buttons[0];
    for (int i = 0; i < ADV_ISSUES; i++)
        adv.buttons[1 + i] = button(adv.win, 0xe, i * 0xe + 0x91, ADVISOR_ID_ISSUE + (uint32_t)i, 0x354);
    views_draw_title(adv.win);
    wm_redraw(adv.win, NULL);
    adv.jasper = akos_costume_load(0x7d3);
    adv.aria = akos_costume_load(0x7d4);
    adv.debug = 5;
    for (int i = 0; i < ADV_ISSUES; i++)
        adv.heaven[i] = adv.hell[i] = ADV_NONE;
    return true;
}

void advisor_destroy(void)
{
    if (!adv.win)
        return;
    advisor_silence();
    akos_talk_close(1);
    akos_talk_close(2);
    wm_show(adv.win, false);
    wm_destroy(adv.win);
    if (adv.aria > 0)
        akos_costume_free(adv.aria);
    if (adv.jasper > 0)
        akos_costume_free(adv.jasper);
    memset(&adv, 0, sizeof adv);
}

/* 0x417b94. Closing only stops the talking; the caller hides the window
 * (game_view_proc: wm_show(advisor_window, false), then advisor_show(0)),
 * and minimizing comes here too. */
void advisor_show(bool on)
{
    if (!on) {
        advisor_silence();
        akos_talk_close(1);
        akos_talk_close(2);
        return;
    }
    WmWindow *w = adv.win;
    if (!w)
        return;
    wm_set_title(w, "Jasper and Aria");
    wm_raise(w);
    views_draw_title(w);
    w->proc = advisor_proc;
    gfx_fill_rect(w->surface, 0xe, 0x91, 0x11a, 0x46, BG);
    gfx_fill_rect(w->surface, 0xe, 0xe, 0x8d, 0x7e, BG);
    gfx_fill_rect(w->surface, 0xa1, 0xe, 0x8c, 0x7e, BG);
    for (int i = 1; i <= ADV_ISSUES; i++) {
        WmWindow *b = adv.buttons[i];
        if (!b)
            continue;
        ctl_button_enable(b, true);
        ctl_button_show(b, true);
        ctl_button_set_pressed(b, true, true);
    }
    g_adv_talk.silenced = false;
    akos_talk_open(1, w, adv.jasper, 0xff, 0xff, 0xc, BG, 8);
    akos_talk_set_direction(1, 2);
    akos_talk_place(1, 0x50, 0x8b, w);
    akos_talk_set_anim(1, 1);
    akos_talk_open(2, w, adv.aria, 0xff, 0xff, 0xe, BG, 8);
    akos_talk_set_direction(2, 2);
    akos_talk_place(2, 0xeb, 0x8b, w);
    akos_talk_set_anim(2, 1);
    static const GfxRect text = {0x6f, 0x129, 0x95, 0xd2};
    g_adv_talk.text_rect[1] = text;
    g_adv_talk.text_rect[2] = text;
    advisor_update();
    wm_show(w, true);
    wm_redraw(w, NULL);
}

/* 0x417e28: right click on a control. */
static void advisor_help(WmWindow *c)
{
    static const char *const k_issue[ADV_ISSUES] = {"MOST", "2nd MOST", "3rd MOST", "4th MOST", "5th MOST"};
    char buf[160];
    const char *s;
    uint32_t id = ctl_get_id(c);
    if (id == ADVISOR_ID_CLOSE) {
        s = "This button CLOSES the Helper Window.";
    } else if (id == 1) {
        s = wm_is_minimized(c->parent) ? "This button MAXIMIZES the window" : "This button MINIMIZES the window";
    } else if (id >= ADVISOR_ID_ISSUE && id < ADVISOR_ID_ISSUE + ADV_ISSUES) {
        snprintf(buf, sizeof buf,
                 "This button will prompt Aria and Jasper to advise you on the %s IMPORTANT problem facing your "
                 "Afterlife.",
                 k_issue[id - ADVISOR_ID_ISSUE]);
        s = buf;
    } else {
        s = "I have no idea what this button does.";
    }
    views_control_help(c, s);
}

/* 0x41801f: the buttons' labels. */
static void draw_labels(void)
{
    for (int i = 0; i < ADV_ISSUES; i++) {
        unsigned code = adv.heaven[i] == ADV_NONE ? adv.hell[i] : adv.heaven[i];
        char a[80], b[80];
        snprintf(a, sizeof a, adv.heaven[i] == ADV_NONE ? "%s" : "%s \x14", advisor_issue_label(code));
        snprintf(b, sizeof b, adv.hell[i] == ADV_NONE ? "%s" : "\x13 %s", a);
        if (adv.buttons[1 + i])
            ctl_button_set_label(adv.buttons[1 + i], b, FONT, 0x131a13, true);
    }
}

/* 0x41878c: merges Hell's and Heaven's lists (both ascending) into the
 * five buttons: the smaller code goes alone, equal codes share one. */
static void merge_issues(void)
{
    const uint32_t *hl = world_realm(GS_PLANE_HELL)->unk_808b;
    const uint32_t *he = world_realm(GS_PLANE_HEAVEN)->unk_808b;
    int ihl = 0, ihe = 0, out_hl = 0, out_he = 0;
    do {
        if (hl[ihl] < he[ihe]) {
            adv.hell[out_hl++] = hl[ihl++];
            adv.heaven[out_he++] = ADV_NONE;
        } else if (he[ihe] < hl[ihl]) {
            adv.heaven[out_he++] = he[ihe++];
            adv.hell[out_hl++] = ADV_NONE;
        } else {
            adv.heaven[out_he++] = he[ihe++];
            adv.hell[out_hl++] = hl[ihl++];
        }
        if (out_he >= ADV_ISSUES && out_hl < ADV_ISSUES)
            for (int i = out_hl; i < ADV_ISSUES; i++)
                adv.hell[i] = ADV_NONE;
        if (out_hl >= ADV_ISSUES && out_he < ADV_ISSUES)
            for (int i = out_he; i < ADV_ISSUES; i++)
                adv.heaven[i] = ADV_NONE;
    } while (out_hl < ADV_ISSUES && out_he < ADV_ISSUES);
}

/* 0x41894e: inserts `code` into the current realm's ascending list of
 * five. Quirk kept: a code already in the list returns only after the
 * entries above it were shifted up, which can duplicate the last of
 * them. */
static void add_issue(PlaneStats *r, uint32_t code)
{
    uint32_t *l = r->unk_808b;
    if (code > l[4])
        return;
    for (int i = 4;; i--) {
        if (i == 0) {
            l[0] = code;
            return;
        }
        if (l[i - 1] < code) {
            l[i] = code;
            return;
        }
        if (l[i - 1] == code)
            return;
        l[i] = l[i - 1];
    }
}

/* Training centre shortfall / surplus (codes 75, 85, 90). */
static void check_training(PlaneStats *r)
{
    uint32_t employed = r->unk_7ff0[RW_WORKFORCE], unfilled = r->unk_7ff0[RW_SHORTFALL];
    uint32_t tc = r->unk_804f[4];
    if (employed == 0) {
        if (unfilled > 10000)
            add_issue(r, 75);
    } else if (unfilled == 0) {
        if (employed > 1000 && tc != 0)
            add_issue(r, 90);
    } else {
        uint32_t pct = mul_div_clamp(employed, employed + unfilled, 100);
        if (pct < 91) {
            if (tc == 0) {
                if (pct < 50)
                    add_issue(r, 75);
            } else if (pct < 10 && tc - r->unk_8077[4] < tc / 20) {
                add_issue(r, 85);
            }
        } else if (tc != 0) {
            add_issue(r, 90);
        }
    }
}

/* The body of 0x418a39's realm loop for `plane` (selected). `total`
 * accumulates over both realms, as in the original. */
static void evaluate_realm(int plane, uint32_t *total)
{
    PlaneStats *r = realm_cur();
    for (int i = 0; i < ADV_ISSUES; i++)
        r->unk_808b[i] = ADV_NONE;

    int8_t debt = (int8_t)g_state.unk_593247; /* years in the red */
    if (debt != 0) {
        if (debt < 10)
            add_issue(r, 15);
        else if (debt < 45)
            add_issue(r, 10);
        else if (debt < 50)
            add_issue(r, 5);
    }
    if (r->unk_804b == 0 && bignum_less(g_state.pennies, 1000000))
        add_issue(r, 130);
    if (!bignum_ge_int(g_state.pennies, 1))
        add_issue(r, 135);
    else if (g_state.unk_5fa5f4 < g_state.unk_5f7ed4 &&
             bignum_less(g_state.pennies, (int32_t)((g_state.unk_5f7ed4 - g_state.unk_5fa5f4) * 20)))
        add_issue(r, 135);

    if (r->n_ports == 0) {
        add_issue(r, 20);
        return;
    }
    if (r->unk_804f[0] != 0 && r->unk_8077[0] * 100 / r->unk_804f[0] > 98)
        add_issue(r, 30);
    if (r->n_zones == 0)
        add_issue(r, 25);
    if (g_state.year > 1) {
        uint32_t lost = 0;
        for (int k = 0; k < 7; k++) {
            *total += r->unk_7fac[k];
            if (r->unk_809f[k] == 0)
                continue;
            lost += r->unk_809f[k];
            if (r->unk_811b == 0 && r->unk_7fac[k] != 0 &&
                mul_div_clamp(r->unk_7f74[k] + r->unk_7f3c[k] + r->unk_809f[k], r->unk_7fac[k], 100) < 99)
                add_issue(r, 80);
        }
        if (r->unk_809f[15] != 0) {
            lost += r->unk_809f[15];
            if (r->unk_811b == 0 && r->unk_804f[0] != 0 && mul_div_clamp(r->unk_8077[0], r->unk_804f[0], 100) < 99)
                add_issue(r, 80);
        }
        if (r->unk_809f[14] != 0) {
            lost += r->unk_809f[14];
            if (r->unk_811b == 0 && r->unk_804f[1] != 0 && mul_div_clamp(r->unk_8077[1], r->unk_804f[1], 100) < 99)
                add_issue(r, 80);
        }
        if (r->unk_8123 != 0)
            add_issue(r, 80);
        if (lost != 0 && r->population != 0) {
            uint32_t permille = mul_div_clamp(lost, r->population, 1000);
            add_issue(r, permille < 25 ? 125 : permille < 150 ? 100 : 35);
        }
    }
    if (r->population > 10000 && r->population < *total / 10)
        add_issue(r, 55);
    if (r->n_zones != 0) {
        if (r->unk_80e1 == 0)
            add_issue(r, r->unk_80e3 == 0 ? 110 : 115);
        else if ((int)r->n_zones - (int)r->unk_80e1 > 5)
            add_issue(r, r->unk_80e3 == 0 ? 120 : 115);
    }
    if (r->n_zones != 0 && r->unk_8107 > 10 && r->n_zones / 10 < r->unk_8107)
        add_issue(r, plane == GS_PLANE_HELL ? 170 : 171);
    if (r->n_zones != 0 && r->unk_8105 > 20)
        add_issue(r, r->n_zones / 5 < r->unk_8105 ? 160 : 165);
    if (r->n_zones != 0 && r->unk_8109 > 20 && r->n_zones / 20 < r->unk_8109)
        add_issue(r, plane == GS_PLANE_HEAVEN ? 175 : 180);
    if (r->unk_811b != 0)
        add_issue(r, 95);
    if (r->unk_7f34 != 0 && mul_div_clamp(r->unk_7f38, r->unk_7f34, 100) > 90)
        add_issue(r, 105);
    if (r->unk_809f[14] != 0) {
        if (r->unk_80e7 != 0)
            add_issue(r, 45);
        else if (r->unk_804f[1] == 0)
            add_issue(r, 40);
        else if (mul_div_clamp(r->unk_8077[1], r->unk_804f[1], 100) > 98)
            add_issue(r, 50);
    }
    check_training(r);
    if (g_state.planet_aaaa < 200)
        add_issue(r, 185);
    if (g_state.year > 1) {
        if (plane == GS_PLANE_HEAVEN) {
            if (g_planet.avg_not_opra < 200)
                add_issue(r, 210);
        } else if (g_planet.avg_not_ocra < 200) {
            add_issue(r, 215);
        }
    }
    if (r->unk_804f[2] == 0) {
        if (r->unk_7ff0[RW_IMPORTED] > 100000)
            add_issue(r, 60);
        else if (r->unk_7ff0[RW_IMPORTED] > 10000)
            add_issue(r, 65);
    } else if (r->unk_8063[2] + 10000u < r->unk_7ff0[RW_IMPORTED]) {
        add_issue(r, 70);
    }
    if (r->n_zones != 0) {
        uint32_t roads = r->unk_80eb / r->n_zones;
        if (plane == GS_PLANE_HELL) {
            if (roads < 24)
                add_issue(r, 140);
            else if (roads < 32)
                add_issue(r, 150);
        } else if (roads >= 97) {
            add_issue(r, 145);
        } else if (roads > 48) {
            add_issue(r, 155);
        }
    }
}

/* 0x418a39 */
void advisor_update(void)
{
    if (!adv.win || adv.win->proc != advisor_proc)
        return;
    for (int i = 0; i < ADV_ISSUES; i++)
        adv.heaven[i] = adv.hell[i] = ADV_NONE;
    if (!g_build.editor) {
        uint32_t total = 0;
        int before = views_select_plane(GS_PLANE_HELL);
        evaluate_realm(GS_PLANE_HELL, &total);
        views_select_plane(GS_PLANE_HEAVEN);
        evaluate_realm(GS_PLANE_HEAVEN, &total);
        if (before != GS_PLANE_HEAVEN)
            views_select_plane(before);
        merge_issues();
    } else {
        adv.heaven[0] = adv.debug;
        adv.hell[1] = adv.debug;
        adv.hell[2] = adv.debug;
        adv.heaven[2] = adv.debug;
    }
    draw_labels();
}

/* 0x4196ae */
void advisor_pick(int i)
{
    if (i < 0 || i >= ADV_ISSUES)
        return;
    snd_slot_stop_stream(AKOS_TALK_VOICE_SLOT);
    gfx_font_select(FONT);
    g_adv_talk.stamp++;
    if (adv.hell[i] == ADV_NONE) {
        if (adv.heaven[i] == ADV_NONE)
            advisor_say_quip(rand_range(1, 13));
        else
            advisor_say_heaven(adv.heaven[i]);
    } else if (adv.heaven[i] == ADV_NONE) {
        advisor_say_hell(adv.hell[i]);
    } else {
        advisor_say_both(adv.heaven[i]);
    }
}

/* The talker messages both procedures share (6000, 6001, 0x1772). */
static void talker_message(unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == AKOS_TALK_MSG_MARKER) {
        /* m + 1 below 0x41 is a Jasper mouth (animation m + 1), above it
         * Aria's (m - 0x3f). The original also files the marker into two
         * never-read "mouth shape" globals (0x639270 / 0x63929c). */
        int anim = (int)b + 1;
        int n = anim < 0x41 ? 1 : 2;
        akos_talk_arm_lipsync(n);
        if (n == 2)
            anim = (int)b - 0x3f;
        if (anim > 0 && anim < 0x40)
            akos_talk_set_anim(n, anim);
    } else if (msg == AKOS_TALK_MSG_IDLE) {
        if (rand_range(0, 5) != 0)
            return;
        int anim, n = (int)a;
        if (n == 2) {
            anim = rand_range(0, 1) == 0 ? 0x2d : 0x3d;
        } else {
            n = 1;
            anim = rand_range(0, 1) == 0 ? 0x29 : 0x2c;
        }
        akos_talk_set_anim(n, anim);
    } else if (msg == ADVISOR_MSG_HUSH) {
        akos_talk_clear_marker(2);
        akos_talk_clear_marker(1);
        akos_talk_set_anim(1, 6);
        akos_talk_set_anim(2, 6);
    }
}

static void control_clicked(WmWindow *c)
{
    uint32_t id = ctl_get_id(c);
    switch (id) {
    case ADVISOR_ID_CLOSE:
        advisor_silence();
        /* wm_send(main_window, menu item 0x1b, 0x1006, 0x1b, 0): the menu
         * toggle hides the window and keeps the menu check and the
         * control panel button in sync. */
        shell_menu_command(MENU_HELPERS);
        break;
    case ADVISOR_ID_ISSUE:
    case ADVISOR_ID_ISSUE + 1:
    case ADVISOR_ID_ISSUE + 2:
        advisor_pick((int)(id - ADVISOR_ID_ISSUE));
        break;
    case ADVISOR_ID_ISSUE + 3:
        if (!g_build.editor) {
            advisor_pick((int)(id - ADVISOR_ID_ISSUE));
        } else {
            int v = adv.debug - 5;
            adv.debug = (uint8_t)(v < 6 ? 5 : v);
            advisor_update();
        }
        break;
    case ADVISOR_ID_ISSUE + 4:
        if (!g_build.editor) {
            advisor_pick((int)(id - ADVISOR_ID_ISSUE));
        } else {
            unsigned v = adv.debug + 5u;
            adv.debug = (uint8_t)(v > 0xdb ? 0xdc : v);
            advisor_update();
        }
        break;
    default:
        break;
    }
}

/* 0x41751b */
intptr_t advisor_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    switch (msg) {
    case WM_MSG_MINIMIZED:
        advisor_show(a == 0);
        break;
    case WM_MSG_MOUSEMOVE:
        if (adv.dragging) {
            GfxRect r = w->rect;
            rect_move_to(&r, (int)a - adv.grab_x, (int)b - adv.grab_y);
            wm_local_to_screen(w, &r);
            wm_set_rect(w, &r);
        }
        break;
    case WM_MSG_LBUTTONDOWN:
        wm_set_capture(w);
        wm_raise(w);
        adv.grab_x = (int)a;
        adv.grab_y = (int)b;
        adv.dragging = true;
        g_game_paused = 1;
        break;
    case WM_MSG_LBUTTONUP:
        wm_release_capture();
        adv.dragging = false;
        g_game_paused = 0;
        break;
    case WM_MSG_CTL_PRESS:
        wm_raise(w->parent);
        break;
    case WM_MSG_CTL_HOVER:
        if (wm_get_mode() != 0)
            wm_raise(w->parent);
        break;
    case WM_MSG_CTL_RDOWN:
        wm_raise(w->parent);
        advisor_help(w);
        break;
    case WM_MSG_CTL_CLICK:
        control_clicked(w);
        break;
    case AKOS_TALK_MSG_MARKER:
    case AKOS_TALK_MSG_IDLE:
    case ADVISOR_MSG_HUSH:
        talker_message(msg, a, b);
        break;
    default:
        break;
    }
    return wm_default_proc(w, msg, a, b);
}

/* 0x41f2c9 */
intptr_t advisor_tutorial_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == AKOS_TALK_MSG_MARKER || msg == AKOS_TALK_MSG_IDLE || msg == ADVISOR_MSG_HUSH)
        talker_message(msg, a, b);
    return wm_default_proc(w, msg, a, b);
}
