/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * tools/emu wrappers for WP4 (game/badthings.h): fresh runtime state and an
 * event log of the side effects (sounds, music, message boxes, news) the
 * Bad Things, the map objects and the structures they destroy produce, to
 * compare with the original's calls.
 */
#include <stdint.h>
#include <string.h>

#include "game/badthings.h"
#include "game/build.h"
#include "game/karma.h"
#include "game/mapobj.h"
#include "game/sim.h"
#include "game/world.h"
#include "platform/platform.h"

#if defined(_WIN32)
#define SHIM_API __declspec(dllexport)
#else
#define SHIM_API __attribute__((visibility("default")))
#endif

typedef struct BtEvent {
    int32_t kind; /* 1 snd_play, 2 snd_stop, 3 snd_unload, 4 music, 5 fade, 6 message, 7 news, 8 helpers */
    uint32_t a, b;
    char text[96];
} BtEvent;

#define BT_MAX_EVENTS 4096
static BtEvent s_ev[BT_MAX_EVENTS];
static int s_nev;

static void ev(int kind, uint32_t a, uint32_t b, const char *text)
{
    if (s_nev >= BT_MAX_EVENTS)
        return;
    BtEvent *e = &s_ev[s_nev++];
    e->kind = kind, e->a = a, e->b = b;
    e->text[0] = '\0';
    if (text) {
        strncpy(e->text, text, sizeof e->text - 1);
        e->text[sizeof e->text - 1] = '\0';
    }
}
static void h_sound(uint32_t id, int prio) { ev(1, id, (uint32_t)prio, NULL); }
static void h_stop(uint32_t id) { ev(2, id, 0, NULL); }
static void h_unload(uint32_t id) { ev(3, id, 0, NULL); }
static void h_music(uint32_t id) { ev(4, id, 0, NULL); }
static void h_fade(int ms) { ev(5, (uint32_t)ms, 0, NULL); }
static int h_message(const char *title, const char *text, const char *buttons)
{
    (void)title, (void)buttons;
    ev(6, 0, 0, text);
    return 1;
}
static void h_news(unsigned id) { ev(7, id, 0, NULL); }
static void h_helpers(void) { ev(8, 0, 0, NULL); }

/* Fresh WP4 runtime state; with `log`, every sound hook of the packages a
 * Bad Thing reaches (WP4, WP3, WP2, WP1) records into the event log, and
 * `data_dir` (optional) becomes the platform data dir. */
SHIM_API void shim_bt_setup(int log, const char *data_dir)
{
    badthings_reset();
    memset(&g_badthings_hooks, 0, sizeof g_badthings_hooks);
    s_nev = 0;
    if (!log)
        return;
    if (data_dir)
        plat_set_data_dir(data_dir);
    g_badthings_hooks.sound = h_sound;
    g_badthings_hooks.sound_stop = h_stop;
    g_badthings_hooks.sound_unload = h_unload;
    g_badthings_hooks.music_play = h_music;
    g_badthings_hooks.music_fade = h_fade;
    g_badthings_hooks.message = h_message;
    g_badthings_hooks.news = h_news;
    g_badthings_hooks.helpers_off = h_helpers;
    g_mapobj_hooks.sound = h_sound;
    g_build_hooks.sound = h_sound;
    g_karma_hooks.sound = h_sound;
    g_sim_hooks.sound = h_sound;
    g_sim_hooks.sound_unload = h_unload;
    g_sim_hooks.music_fade = h_fade;
    g_sim_hooks.message = h_message;
    g_sim_hooks.news = h_news;
}

SHIM_API int shim_bt_events(void) { return s_nev; }
SHIM_API void shim_bt_clear(void) { s_nev = 0; }
/* event i: kind (0 = none), a and b in out[0..1], the text in text */
SHIM_API int shim_bt_event(int i, uint32_t out[2], char *text, int size)
{
    if (i < 0 || i >= s_nev)
        return 0;
    out[0] = s_ev[i].a, out[1] = s_ev[i].b;
    if (text && size > 0) {
        strncpy(text, s_ev[i].text, (size_t)size - 1);
        text[size - 1] = '\0';
    }
    return s_ev[i].kind;
}

SHIM_API void shim_bt_set(uint32_t addr, uint32_t v)
{
    switch (addr) {
    case 0x593248: g_state.bad_things = v; break;
    case 0x5f746c: g_state.unk_5f746c = v; break;
    case 0x5b2f5c: g_state.unk_5b2f5c = v; break;
    case 0x5fa5f8: g_state.view_dir_x = (int8_t)v; break;
    case 0x5bb118: g_state.view_dir_y = (int8_t)v; break;
    case 0x5b2f61: g_state.unk_5b2f61 = (uint8_t)v; break;
    case 0x638668: g_state.unk_638668 = v; break;
    default: break;
    }
}

/* realm field (plane 1 / 3, offset 0x80f9..0x80fc) */
SHIM_API void shim_bt_realm_set(int plane, int off, int v)
{
    PlaneStats *r = world_realm(plane);
    switch (off) {
    case 0x80f9: r->unk_80f9 = (uint8_t)v; break;
    case 0x80fa: r->unk_80fa = (uint8_t)v; break;
    case 0x80fb: r->unk_80fb = (uint8_t)v; break;
    case 0x80fc: r->unk_80fc = (uint8_t)v; break;
    default: break;
    }
}

SHIM_API void shim_bt_special(int i, int x, int y)
{
    if (i >= 0 && i < 10)
        g_build.special_pos[i][0] = (uint8_t)x, g_build.special_pos[i][1] = (uint8_t)y;
}

/* g_state.objects in the original's 0x17-byte layout (63 records), and the
 * Bad Things bits, for the per-frame quick comparison */
SHIM_API int shim_bt_objects(uint8_t *out)
{
    for (int i = 0; i < GS_MAP_OBJECTS; i++) {
        const MapObjectRec *o = &g_state.objects[i];
        uint8_t *p = out + i * 0x17;
        p[0] = o->plane, p[1] = (uint8_t)o->x, p[2] = (uint8_t)o->y, p[3] = (uint8_t)o->dx, p[4] = (uint8_t)o->dy;
        p[5] = (uint8_t)o->size, p[6] = o->set, p[7] = o->frame, p[8] = o->first_frame, p[9] = o->delay;
        p[10] = o->delay_reset;
        for (int k = 0; k < 4; k++)
            p[11 + k] = (uint8_t)(o->set_mask >> (8 * k)), p[19 + k] = (uint8_t)(o->flags >> (8 * k));
        p[15] = o->fade, p[16] = o->unk_10, p[17] = o->unk_11, p[18] = o->priority;
    }
    return g_state.unk_5b2f61;
}
