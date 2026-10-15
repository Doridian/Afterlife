/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Test of the tutorial (WP8): runs the real game headless with the user's
 * data (none is in the repo), starts a new game through the title, then
 *
 *   1. walks the op tables (dry run): every lesson ends, guards balance,
 *      every speech's subtitle is in ALIFE.EXE;
 *   2. runs every lesson for real through the Tutorial menu (lesson 9,
 *      which has no menu item, through tutorial_run): the lesson's save is
 *      loaded from the bundles, the prologue's scripted clicks set the
 *      view up, every op runs with the scripted input live (no early
 *      stop), each control / menu item the pointer glides to exists, and
 *      the lessons that build (1: zones, roads and a gate; 3: Karma
 *      stations and track; 7: a siphon; 8: ports) changed the map through
 *      the scripted clicks; at the end the player's game is back (the map
 *      and the money as before), the input is live again, the advisor
 *      window has its own procedure back and "All Done!" was shown;
 *   3. stops a lesson early with Esc (the real input path) and checks the
 *      player's game came back.
 *
 *   tutorial_test <data dir> <out dir> [lesson ...]
 *
 * Time runs on a virtual clock (20 ms per reading) for the glide, the
 * waits and the simulation; voices are cut at each akos_talk_wait. PNGs
 * of each lesson go to <out dir>.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/build.h"
#include "game/enhance.h"
#include "game/ini.h"
#include "game/shell.h"
#include "game/shell_int.h"
#include "game/sim.h"
#include "game/sound.h"
#include "game/startup.h"
#include "game/tutorial.h"
#include "game/world.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "ui/advisors.h"
#include "ui/controls.h"
#include "ui/graphview.h"
#include "ui/mapview.h"
#include "ui/menu.h"
#include "ui/microview.h"
#include "ui/slotdlg.h"
#include "ui/title.h"
#include "ui/tutorialwin.h"
#include "ui/wm.h"

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

/* ---- PNG (stored deflate) ---------------------------------------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t c = crc_update(0xffffffffu, hdr + 4, 4);
    c = crc_update(c, data, len) ^ 0xffffffffu;
    put32(hdr, c);
    fwrite(hdr, 1, 4, f);
}

static void write_png(const char *name, const GfxSurface *s)
{
    if (!g_out || !s)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        g_failures++;
        return;
    }
    uint8_t rgbx[1024], pal[768];
    gfx_palette_get(rgbx, 0, 256);
    for (int i = 0; i < 256; i++)
        pal[i * 3] = rgbx[i * 4], pal[i * 3 + 1] = rgbx[i * 4 + 1], pal[i * 3 + 2] = rgbx[i * 4 + 2];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    int w = s->width, h = s->height;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", pal, 768);
    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    uint8_t *raw = malloc(raw_len), *z = malloc(2 + raw_len + blocks * 5 + 4);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, s->pixels + (size_t)y * (size_t)s->stride, (size_t)w);
    }
    uint8_t *q = z;
    *q++ = 0x78, *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *q++ = off + n == raw_len;
        *q++ = (uint8_t)n, *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n, *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        q += n;
        off += n;
    }
    put32(q, b << 16 | a);
    q += 4;
    chunk(f, "IDAT", z, (size_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    fclose(f);
}

/* ---- synthetic input ----------------------------------------------------- */

static void push_key(SDL_Keycode key, SDL_Scancode sc)
{
    SDL_Event e;
    SDL_zero(e); /* not "= {0}": that zeroes only the union's first member (GCC 15+) */
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.scancode = sc;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
}

static void push_move(int x, int y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = (float)x;
    e.motion.y = (float)y;
    SDL_PushEvent(&e);
}

static void push_button(int x, int y, bool down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = down;
    e.button.x = (float)x;
    e.button.y = (float)y;
    SDL_PushEvent(&e);
}

static void click_window(WmWindow *w)
{
    GfxRect r = w->rect;
    wm_parent_to_screen(w, &r);
    int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    push_move(x, y);
    push_button(x, y, true);
    push_button(x, y, false);
}

/* ---- the virtual clock ---------------------------------------------------- */

static uint32_t g_clock = 1000;
static uint32_t virtual_clock(void) { return g_clock += 20; }

/* ---- the pump hook: the title and message boxes --------------------------- */

static int g_pumps;
static int g_msgboxes;
static int g_title_clicks;

static WmWindow *find_button(int id) { return g_main_window ? ctl_find_by_id(g_main_window->children, id) : NULL; }

static void dialog_driver(void);

static void pump_hook(void)
{
    g_pumps++;
    WmWindow *box = ctl_msgbox_window();
    if (box && wm_is_visible(box)) {
        if (g_pumps % 20 == 0) {
            /* real input stops a lesson; answer the boxes inside a lesson
             * by key only where the input is live */
            WmWindow *b = ctl_msgbox_button(0);
            if (!tutorial_active() && b && wm_is_visible(b)) {
                click_window(b);
                g_msgboxes++;
            } else {
                push_key(SDLK_RETURN, SDL_SCANCODE_RETURN);
            }
        }
        return;
    }
    if (slotdlg_window() && wm_is_visible(slotdlg_window())) {
        dialog_driver();
        return;
    }
    if (g_game_in_progress)
        return;
    TitleStage st = title_stage();
    if ((st == TITLE_STAGE_INTRO || st == TITLE_STAGE_ANIM) && g_pumps % 50 == 0)
        push_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    if (g_pumps % 30 != 0)
        return;
    if (g_title_clicks == 0 && st == TITLE_STAGE_MENU && find_button(TITLE_BTN_NEW)) {
        click_window(find_button(TITLE_BTN_NEW));
        g_title_clicks = 1;
    } else if (g_title_clicks == 1 && st == TITLE_STAGE_DIFFICULTY && find_button(TITLE_BTN_EASY)) {
        click_window(find_button(TITLE_BTN_EASY));
        g_title_clicks = 2;
    }
}

static void steps(int n)
{
    for (int i = 0; i < n && g_game_running; i++)
        game_main_step();
}

/* ---- state fingerprints --------------------------------------------------- */

static uint32_t map_hash(void)
{
    uint32_t h = 2166136261u;
    for (int p = 1; p <= 3; p++)
        for (int i = 0; i < GS_MAP_CELLS; i++) {
            h ^= g_state.plane[p].cells[i];
            h *= 16777619u;
        }
    return h;
}

static int count_set(int plane, unsigned set)
{
    int n = 0;
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (g_state.plane[plane].cells[i] >> 27 == set)
            n++;
    return n;
}

typedef struct Counts {
    int zones_hell, zones_heaven, roads_hell, roads_heaven, gates_heaven, gates_hell;
    int karma_plane2, rails;
    unsigned siphon_cell, port_cells[2]; /* tile sets where lessons 7 / 8 build */
} Counts;

static Counts counts(void)
{
    Counts c;
    c.zones_hell = count_set(GS_PLANE_HELL, 14) + count_set(GS_PLANE_HELL, 15);
    c.zones_heaven = count_set(GS_PLANE_HEAVEN, 14) + count_set(GS_PLANE_HEAVEN, 15);
    c.roads_hell = count_set(GS_PLANE_HELL, 12);
    c.roads_heaven = count_set(GS_PLANE_HEAVEN, 12);
    c.gates_heaven = count_set(GS_PLANE_HEAVEN, 13);
    c.gates_hell = count_set(GS_PLANE_HELL, 13);
    c.karma_plane2 = 0;
    for (int i = 0; i < GS_MAP_CELLS; i++)
        if (g_state.plane[GS_PLANE_MIDDLE].cells[i] >> 27 != 0 && g_state.plane[GS_PLANE_MIDDLE].cells[i] >> 27 != 1)
            c.karma_plane2++;
    c.rails = count_set(GS_PLANE_MIDDLE, 16);
    c.siphon_cell = g_state.plane[GS_PLANE_HEAVEN].cells[0x31 * GS_MAP_W + 0x25] >> 27;
    c.port_cells[0] = g_state.plane[GS_PLANE_HELL].cells[0x4e * GS_MAP_W + 0x3d] >> 27;
    c.port_cells[1] = g_state.plane[GS_PLANE_HELL].cells[0x55 * GS_MAP_W + 0x37] >> 27;
    return c;
}

/* ---- the step hook --------------------------------------------------------- */

static struct {
    int lesson;
    int ops_seen, ops_total;
    bool started, done, active_at_done;
    bool view_ok;            /* zoom 3, unrotated, blocks on at the first op */
    int missing_targets;     /* GLIDE_CTL / GLIDE_MENU targets not found */
    WmWindow *pending;       /* the last GLIDE_CTL's control */
    int pending_id, pending_index;
    uint32_t lesson_map;     /* the lesson's map at its first op */
    Counts start, end;
    uint8_t speed_end;
    int graph_mode_end, map_layer_end; /* the Graphview's mode, the Mapview's layer */
    int stop_at;             /* op index to press Esc at (-1 = never) */
    bool stop_sent;
    int shots;
} g_run;

static int count_ops(int n)
{
    const TutOp *o = tutorial_ops(n);
    int k = 0;
    while (o[k].op != TO_END)
        k++;
    return k;
}

static void step_hook(int lesson, int index, const TutOp *op)
{
    g_run.ops_seen++;
    if (tutorial_hooks.dry)
        return;
    if (index == 0) {
        g_run.started = true;
        g_run.lesson_map = map_hash();
        g_run.start = counts();
        WmWindow *b = shell_ctrl_button(0x17);
        g_run.view_ok = g_map_view.zoom == 3 && g_map_view.dir_x == 1 && g_map_view.dir_y == 1 && b &&
                        ctl_is_pressed(b);
        char name[64];
        snprintf(name, sizeof name, "tutorial_%d_start.png", lesson);
        write_png(name, gfx_screen());
    }
    /* a few screenshots in the middle of the lesson, with a subtitle up */
    if (op->op == TO_WAIT_TALK && g_run.shots < 3 && index > g_run.ops_total * (g_run.shots + 1) / 4) {
        char name[64];
        snprintf(name, sizeof name, "tutorial_%d_%d.png", lesson, ++g_run.shots);
        write_png(name, gfx_screen());
    }
    if (op->op == TO_WAIT_TALK) /* no need to listen */
        snd_slot_stop_stream(SND_VOICE_SLOT);
    /* the control the pointer glides to must be there when it clicks
     * (the window may only open on the pump after the previous click) */
    if (op->op == TO_GLIDE_CTL) {
        WmWindow *w = op->a == TW_CTRLPANEL   ? g_ctrlpanel_window
                      : op->a == TW_GRAPHVIEW ? graphview_window()
                      : op->a == TW_MAPVIEW   ? mapview_window()
                      : op->a == TW_MICROVIEW ? microview_window()
                                              : NULL;
        g_run.pending = w ? ctl_find_by_id(w, op->b) : NULL;
        g_run.pending_id = op->b;
        g_run.pending_index = index;
        if (!g_run.pending) {
            fprintf(stderr, "lesson %d op %d: control 0x%x of window %d not found\n", lesson, index, op->b, op->a);
            g_run.missing_targets++;
        }
    }
    if ((op->op == TO_CLICK || op->op == TO_DOWN) && g_run.pending) {
        if (!wm_is_visible(g_run.pending)) {
            fprintf(stderr, "lesson %d op %d: control 0x%x not visible at the click\n", lesson, g_run.pending_index,
                    g_run.pending_id);
            g_run.missing_targets++;
        }
        g_run.pending = NULL;
    }
    if (op->op == TO_GLIDE_MENU && !menu_find_item((uint32_t)op->a)) {
        fprintf(stderr, "lesson %d op %d: menu item 0x%x not found\n", lesson, index, op->a);
        g_run.missing_targets++;
    }
    if (g_run.stop_at >= 0 && index >= g_run.stop_at && !g_run.stop_sent) {
        push_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        g_run.stop_sent = true;
    }
}

static void done_hook(int lesson)
{
    if (tutorial_hooks.dry)
        return;
    g_run.done = true;
    g_run.active_at_done = tutorial_active();
    g_run.end = counts();
    g_run.speed_end = g_game_speed;
    g_run.graph_mode_end = graphview_mode();
    g_run.map_layer_end = mapview_state().layer;
    char name[64];
    snprintf(name, sizeof name, "tutorial_%d_end.png", lesson);
    write_png(name, gfx_screen());
}

/* ---- 1. the op tables --------------------------------------------------- */

static void check_tables(void)
{
    for (int n = 1; n <= TUTORIAL_LESSONS; n++) {
        const TutOp *o = tutorial_ops(n);
        CHECK(o != NULL);
        if (!o)
            continue;
        int depth = 0, says = 0, empty = 0;
        for (int i = 0; o[i].op != TO_END; i++) {
            CHECK(o[i].op > TO_END && o[i].op < TO_COUNT);
            if (o[i].op == TO_IF)
                depth++;
            else if (o[i].op == TO_ENDIF)
                depth--;
            CHECK(depth >= 0 && depth <= 1);
            if (o[i].op == TO_SAY) {
                says++;
                CHECK(o[i].a == 1 || o[i].a == 2);
                if (!o[i].text || !o[i].text[0])
                    empty++;
            }
        }
        CHECK(depth == 0);
        CHECK(says > 0);
        CHECK(empty == 0);
        printf("lesson %d \"%s\": %d ops, %d lines\n", n, tutorial_lesson_name(n), count_ops(n), says);
        CHECK(tutorial_lesson_name(n)[0] != 0);
    }
    CHECK(tutorial_ops(0) == NULL && tutorial_ops(TUTORIAL_LESSONS + 1) == NULL);
    /* the dry run sees every op when talking, and skips the guarded ones
     * when silenced */
    tutorial_hooks.dry = true;
    for (int n = 1; n <= TUTORIAL_LESSONS; n++) {
        g_run.ops_seen = 0;
        tutorial_hooks.dry_silent = false;
        tutorial_run(n);
        CHECK(g_run.ops_seen == count_ops(n));
        int talking = g_run.ops_seen;
        g_run.ops_seen = 0;
        tutorial_hooks.dry_silent = true;
        tutorial_run(n);
        CHECK(g_run.ops_seen < talking);
    }
    tutorial_hooks.dry = tutorial_hooks.dry_silent = false;
    /* the glide: 10 pixels per axis and step, exactly onto the target */
    TutGlide g = {0};
    tutorial_glide_start(&g, 5, 100, 38, 71, TUTORIAL_GLIDE_SPEED);
    int k = 0;
    while (g.active && k < 100) {
        tutorial_glide_step(&g);
        k++;
    }
    CHECK(k == 4 && g.x == 38 && g.y == 71 && g.end_x == 38 && g.end_y == 71);
}

/* ---- 2./3. a lesson -------------------------------------------------------- */

static void run_lesson(int n, int stop_at)
{
    memset(&g_run, 0, sizeof g_run);
    g_run.lesson = n;
    g_run.ops_total = count_ops(n);
    g_run.stop_at = stop_at;
    uint32_t map0 = map_hash();
    int64_t pennies0 = gs_pennies(&g_state);
    uint32_t year0 = g_state.year;
    int boxes0 = g_msgboxes, pumps0 = g_pumps;
    if (n <= 8)
        shell_menu_command((unsigned)(MENU_TUTORIAL_FIRST + n - 1));
    else
        tutorial_run(n);
    /* (the "All Done!" box was answered inside: it is modal) */
    printf("lesson %d: %d/%d ops, %d pumps, missing targets %d, speed %u%s\n", n, g_run.ops_seen,
           g_run.ops_total, g_pumps - pumps0, g_run.missing_targets, g_run.speed_end,
           stop_at >= 0 ? " (stopped)" : "");
    CHECK(g_run.started);
    CHECK(g_run.view_ok);
    CHECK(g_run.lesson_map != map0); /* the lesson's own game was loaded */
    if (stop_at < 0) {
        CHECK(g_run.done && g_run.active_at_done);
        CHECK(g_run.ops_seen == g_run.ops_total);
        CHECK(g_run.missing_targets == 0);
    } else {
        CHECK(g_run.stop_sent);
        CHECK(g_run.ops_seen < g_run.ops_total);
    }
    CHECK(!tutorial_active());
    CHECK(g_msgboxes > boxes0);                       /* "All Done!" */
    CHECK(map_hash() == map0); /* the player's game is back */
    CHECK(gs_pennies(&g_state) == pennies0);
    CHECK(g_state.year == year0);
    if (g_state.year != year0 || gs_pennies(&g_state) != pennies0)
        printf("  year %u -> %u, pennies %lld -> %lld\n", (unsigned)year0, (unsigned)g_state.year,
               (long long)pennies0, (long long)gs_pennies(&g_state));
    CHECK(advisor_window() && advisor_window()->proc == advisor_proc && !wm_is_visible(advisor_window()));
    CHECK(g_ctrlpanel_window && wm_is_visible(g_ctrlpanel_window));
    shell_menu_command(MENU_SPEED_PAUSE); /* the lesson left the tempo fast, as the original does */
    steps(10);
    char name[64];
    snprintf(name, sizeof name, "tutorial_%d_after.png", n);
    write_png(name, gfx_screen());
    if (stop_at >= 0)
        return;
    Counts a = g_run.start, b = g_run.end;
    switch (n) {
    case 1: /* Hell zoned and roads, a gate, roads and zones in Heaven, tempo fast */
        printf("  zones hell %d->%d heaven %d->%d, roads hell %d->%d heaven %d->%d, gates heaven %d->%d\n",
               a.zones_hell, b.zones_hell, a.zones_heaven, b.zones_heaven, a.roads_hell, b.roads_hell, a.roads_heaven,
               b.roads_heaven, a.gates_heaven, b.gates_heaven);
        CHECK(b.zones_hell > a.zones_hell);
        CHECK(b.roads_hell > a.roads_hell);
        CHECK(b.gates_heaven > a.gates_heaven);
        CHECK(b.zones_heaven > a.zones_heaven);
        CHECK(g_run.speed_end == 1);
        break;
    case 3: /* Karma: stations and track on the middle plane */
        printf("  karma plane cells %d->%d, rails %d->%d\n", a.karma_plane2, b.karma_plane2, a.rails, b.rails);
        CHECK(b.karma_plane2 > a.karma_plane2);
        break;
    case 4: /* the Labor Graph through the Graphview's mode button */
        CHECK(g_run.graph_mode_end == GRAPH_LABOR);
        break;
    case 5: /* the graphs, then each Mapview layer in turn: the last is Rocks */
        printf("  graph mode %d, map layer 0x%x\n", g_run.graph_mode_end, g_run.map_layer_end);
        CHECK(g_run.map_layer_end == MAPVIEW_ROCKS);
        break;
    case 7:
        printf("  Heaven (0x25, 0x31): set %u -> %u\n", a.siphon_cell, b.siphon_cell);
        CHECK(b.siphon_cell != a.siphon_cell);
        break;
    case 8:
        printf("  Hell (0x3d, 0x4e): set %u -> %u, (0x37, 0x55): %u -> %u\n", a.port_cells[0], b.port_cells[0],
               a.port_cells[1], b.port_cells[1]);
        CHECK(b.port_cells[0] != a.port_cells[0] && b.port_cells[1] != a.port_cells[1]);
        break;
    default:
        break;
    }
}

/* ---- 5. the slot dialogs --------------------------------------------------- */

/* what the pump hook does in the open dialog */
static struct {
    const char *title; /* save: the text to type, NULL = keep */
    int line;          /* the list line to click (-1 = keep the selection) */
    uint32_t button;   /* then the button to click */
    int stage;         /* 0 select, 1 click the button, 2 done */
    int wait;
    bool shot;
    const char *png;
} g_dlg;

static void plan(const char *title, int line, uint32_t button, const char *png)
{
    g_dlg.title = title;
    g_dlg.line = line;
    g_dlg.button = button;
    g_dlg.stage = 0;
    g_dlg.wait = 0;
    g_dlg.shot = false;
    g_dlg.png = png;
}

static void dialog_driver(void)
{
    if (++g_dlg.wait % 15 != 0)
        return;
    WmWindow *w = slotdlg_window(), *list = slotdlg_list();
    if (g_dlg.stage == 0) {
        if (g_dlg.line >= 0 && list) {
            GfxRect row = {0, 0, 0, 0};
            wm_send(list, list, CTL_LIST_ROW_RECT, (intptr_t)&row, g_dlg.line);
            int x = (row.left + row.right) / 2, y = (row.top + row.bottom) / 2;
            wm_point_to_screen(list, &x, &y);
            push_move(x, y);
            push_button(x, y, true);
            push_button(x, y, false);
        }
        g_dlg.stage = 1;
        return;
    }
    if (g_dlg.stage == 1) {
        /* (after the click on the line, which puts its title in the field) */
        if (g_dlg.title && slotdlg_text())
            wm_send(slotdlg_text(), slotdlg_text(), CTL_TEXT_MSG_SET, (intptr_t)g_dlg.title, 0);
        if (!g_dlg.shot && g_dlg.png) {
            write_png(g_dlg.png, gfx_screen());
            g_dlg.shot = true;
        }
        WmWindow *b = ctl_find_by_id(w->children, (intptr_t)g_dlg.button);
        if (b) {
            click_window(b);
            g_dlg.stage = 2;
        }
        return;
    }
    if (g_dlg.stage >= 2 && g_dlg.button == SLOTDLG_ID_DELETE) { /* the dialog stays open: close it */
        WmWindow *c = ctl_find_by_id(w->children, SLOTDLG_ID_CANCEL);
        if (c && g_dlg.stage++ > 3)
            click_window(c);
    }
}

static bool slot_title(unsigned slot, char *out, size_t n)
{
    char path[1100];
    SlotInfo info;
    if (!slotdlg_slot_file(1, slot, path, sizeof path) || !slotdlg_read_info(path, &info))
        return false;
    snprintf(out, n, "%s", info.title);
    return true;
}

static void run_command_steps(unsigned menu)
{
    g_build.map_dirty = 0;
    shell_menu_command(menu);
    steps(5);
}

static void check_slot_dialogs(void)
{
    char path[1100], t[256];
    for (unsigned i = 0; i < 4; i++) {
        shell_slot_path(1, i, path, sizeof path);
        remove(path);
    }
    uint32_t year0 = g_state.year;
    /* save into a new slot (the empty line at the end is selected) */
    plan("Tutorial Test One", -1, SLOTDLG_ID_SAVE, "slot_save.png");
    run_command_steps(MENU_SAVE);
    CHECK(slot_title(0, t, sizeof t) && strcmp(t, "Tutorial Test One") == 0);
    /* a second one, after a year */
    shell_menu_command(MENU_SPEED_FAST);
    for (int i = 0; i < 20000 && g_state.year == year0; i++)
        game_main_step();
    shell_menu_command(MENU_SPEED_PAUSE);
    uint32_t year1 = g_state.year;
    CHECK(year1 > year0);
    plan("Tutorial Test Two", -1, SLOTDLG_ID_SAVE, NULL);
    run_command_steps(MENU_SAVE);
    CHECK(slot_title(1, t, sizeof t) && strcmp(t, "Tutorial Test Two") == 0);
    /* overwrite slot 0 (line 0): "Overwrite existing save game?" Yes */
    plan("Tutorial Test Three", 0, SLOTDLG_ID_SAVE, NULL);
    int boxes = g_msgboxes;
    run_command_steps(MENU_SAVE);
    CHECK(g_msgboxes == boxes + 1);
    CHECK(slot_title(0, t, sizeof t) && strcmp(t, "Tutorial Test Three") == 0);
    CHECK(!slotdlg_window());
    /* load line 0 (slot 0, saved in year1): the year comes back */
    shell_menu_command(MENU_SPEED_FAST);
    for (int i = 0; i < 20000 && g_state.year == year1; i++)
        game_main_step();
    shell_menu_command(MENU_SPEED_PAUSE);
    CHECK(g_state.year > year1);
    plan(NULL, 0, SLOTDLG_ID_LOAD, "slot_load.png");
    run_command_steps(MENU_LOAD);
    CHECK(g_state.year == year1);
    /* the title's chooser: line 1 is slot 1 */
    int slot = -1;
    plan(NULL, 1, SLOTDLG_ID_LOAD, "slot_choose.png");
    CHECK(slotdlg_choose(1, &slot) && slot == 1);
    /* delete line 1 (slot 1): "Delete game?" Yes, then Cancel */
    plan(NULL, 1, SLOTDLG_ID_DELETE, "slot_delete.png");
    boxes = g_msgboxes;
    run_command_steps(MENU_DELETE);
    CHECK(g_msgboxes == boxes + 1);
    CHECK(!slot_title(1, t, sizeof t));
    CHECK(slot_title(0, t, sizeof t));
    CHECK(!slotdlg_window());
    /* a scenario: ascen001.asc from the CD (the data dir) */
    plan(NULL, 0, SLOTDLG_ID_LOAD, "slot_scenario.png");
    run_command_steps(MENU_LOAD_SCENARIO);
    steps(40);
    printf("slot dialogs: saved, overwrote, loaded (year %u), chose slot %d, deleted; scenario \"%s\"\n",
           (unsigned)year1, slot, g_state.scenario_title);
    CHECK(g_state.scenario_title[0] != 0);
    for (unsigned i = 0; i < 4; i++) {
        shell_slot_path(1, i, path, sizeof path);
        remove(path);
    }
}

/* ---- 4. the (unused) tutorial text window ---------------------------------- */

static void check_text_window(void)
{
    char out[64];
    static const uint8_t in1[] = "one\r\ntwo\\nthree \r\nfour\\\\x\\0gone";
    size_t n = tutorialwin_filter(in1, sizeof in1 - 1, out);
    CHECK(strcmp(out, "one two\nthree four\\x") == 0 && n == strlen(out));
    static const uint8_t in2[] = "\\n\r\nx\ty\\"; /* CR after a \n, a tab, a trailing backslash */
    tutorialwin_filter(in2, sizeof in2 - 1, out);
    CHECK(strcmp(out, "\nxy") == 0);
    CHECK(tutorialwin_create(g_main_window));
    WmWindow *w = tutorialwin_window();
    if (!w)
        return;
    CHECK(!wm_is_visible(w));
    CHECK(tutorialwin_show_page(TUTORIALWIN_FIRST_PAGE));
    steps(5);
    CHECK(wm_is_visible(w) && tutorialwin_page() == TUTORIALWIN_FIRST_PAGE);
    write_png("tutorialwin.png", gfx_screen());
    /* Prev wraps to the last page, Next back to the first */
    WmWindow *prev = ctl_find_by_id(w->children, 0x3e9), *next = ctl_find_by_id(w->children, 0x3ea);
    CHECK(prev && next);
    if (prev && next) {
        click_window(prev);
        steps(10);
        CHECK(tutorialwin_page() == TUTORIALWIN_LAST_PAGE);
        click_window(next);
        steps(10);
        CHECK(tutorialwin_page() == TUTORIALWIN_FIRST_PAGE);
        click_window(next);
        steps(10);
        CHECK(tutorialwin_page() == TUTORIALWIN_FIRST_PAGE + 1);
        write_png("tutorialwin_2.png", gfx_screen());
    }
    WmWindow *close = ctl_find_by_id(w->children, 1000);
    CHECK(close != NULL);
    if (close) {
        click_window(close);
        steps(10);
        CHECK(!wm_is_visible(w));
    }
    printf("tutorial window: pages 0x%x..0x%x\n", TUTORIALWIN_FIRST_PAGE, TUTORIALWIN_LAST_PAGE);
    tutorialwin_destroy();
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <out dir> [lesson ...]\n", argv[0]);
        return 2;
    }
    g_out = argv[2];
    enhance_init_env(); /* AFTERLIFE_ENHANCEMENTS=1: the enhanced behaviour */
    printf("enhancements %s\n", g_enhancements ? "on" : "off");
    setvbuf(stdout, NULL, _IONBF, 0);
    crc_init();
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    plat_set_data_dir(argv[1]);
    ini_set_path("ALIFE.ini");
    if (!plat_init(NULL, 1, 1) || sys_initialize() != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    sys_pump_hook = pump_hook;
    shell_save_dir_override = g_out;
    char *game_argv[] = {argv[0], argv[1], NULL};
    int r = game_main_init(2, game_argv);
    CHECK(r == 0);
    CHECK(g_title_clicks == 2);
    if (r != 0)
        return 1;
    steps(20);
    tutorial_hooks.clock = virtual_clock;
    g_sim_hooks.clock = virtual_clock;
    tutorial_hooks.step = step_hook;
    tutorial_hooks.lesson_done = done_hook;

    check_tables();

    /* the player's game: run it a little so it differs from a fresh one */
    shell_menu_command(MENU_SPEED_FAST);
    steps(200);
    shell_menu_command(MENU_SPEED_PAUSE);

    bool any = false;
    for (int i = 3; i < argc; i++) {
        int n = atoi(argv[i]);
        if (n < 0) {
            run_lesson(-n, 40);
        } else if (n >= 1 && n <= TUTORIAL_LESSONS) {
            run_lesson(n, -1);
        }
        any = true;
    }
    if (!any) {
        for (int n = 1; n <= TUTORIAL_LESSONS; n++)
            run_lesson(n, -1);
        run_lesson(2, 40); /* Esc in the middle */
    }
    check_text_window();
    check_slot_dialogs();
    CHECK(g_game_running);
    tutorial_hooks = (TutorialHooks){0};
    g_sim_hooks.clock = NULL;
    game_main_shutdown();
    sys_shutdown();
    plat_shutdown();
    if (g_failures) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
