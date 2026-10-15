/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * The simulation scheduler: game/sim.h.
 */
#include "game/sim.h"

#include <string.h>

#include "game/badthings.h"
#include "game/build.h"
#include "game/economy.h"
#include "game/karma.h"
#include "game/newworld.h"
#include "game/planet.h"
#include "game/realm.h"
#include "game/souls.h"
#include "game/tables.h"
#include "game/timer.h"
#include "game/util.h"
#include "game/world.h"

SimRuntime g_sim;
SimHooks g_sim_hooks;

uint32_t sim_clock_ms(void) { return g_sim_hooks.clock ? g_sim_hooks.clock() : tm_get_time(); }

void sim_reset(void)
{
    memset(&g_sim, 0, sizeof g_sim);
    economy_reset();
    karma_reset(); /* the train dispatcher's cursor and whistle timer */
    badthings_reset(); /* the giant's footstep */
    if (!g_planet_hooks.souls)
        g_planet_hooks.souls = realm_souls_from_planet;
}

/* map_view_select_plane(&map_view, p): the simulation half here, the
 * view's through the hook */
static void select_plane(int plane)
{
    world_select_plane(plane);
    if (g_sim_hooks.select_plane)
        g_sim_hooks.select_plane(plane);
}

static void hook(void (*f)(void))
{
    if (f)
        f();
}

static int message(const char *title, const char *text, const char *buttons)
{
    return g_sim_hooks.message ? g_sim_hooks.message(title, text, buttons) : 1;
}

static void sound(uint32_t id, int prio)
{
    if (g_sim_hooks.sound)
        g_sim_hooks.sound(id, prio);
}

static void sound_unload(uint32_t id)
{
    if (g_sim_hooks.sound_unload)
        g_sim_hooks.sound_unload(id);
}

static void music_fade(int ms)
{
    if (g_sim_hooks.music_fade)
        g_sim_hooks.music_fade(ms);
}

static void news(unsigned id)
{
    if (g_sim_hooks.news)
        g_sim_hooks.news(id);
}

static void date(void) { hook(g_sim_hooks.statusbar_date); }

static void clear_ledger(PlaneStats *r)
{
    for (int i = 0; i < 16; i++) {
        r->unk_7a08[i].unk_00 = 0;
        for (int j = 0; j < 16; j++)
            r->unk_7a08[i].v[j] = 0;
    }
}

/* the cost per SOUL of the soul rate: mul_div_round(rate, 0x100, n) */
static uint32_t per_soul(uint32_t n) { return mul_div_round(g_state.unk_630430, 0x100, n); }

/* ---- The real-time timers of sim_update ---------------------------------------- */

/* Hell Freezes Over (Hell) / My Blue Heaven (Heaven): realm +0x80fa
 * spread target, +0x80f9 current, +0x80fb years before it recovers */
static void freeze_timer(int plane)
{
    PlaneStats *r = world_realm(plane);
    bool hell = plane == GS_PLANE_HELL;
    uint32_t *t = hell ? &g_sim.t_freeze : &g_sim.t_blues;
    int maxwait = hell ? 0x9c4 : 2000;
    if (r->unk_80fa != 0 && !g_sim.paused) {
        g_state.unk_5b2f5c = 0;
        if (r->unk_80f9 < r->unk_80fa) {
            uint32_t now = sim_clock_ms();
            if ((uint32_t)rand_range(500, maxwait) < now - *t) {
                *t = now;
                select_plane(plane);
                g_build.map_dirty = 1;
                if (!badthings_spread())
                    r->unk_80fa = 0;
            }
        } else {
            r->unk_80fa = 0;
        }
    }
    if (r->unk_80f9 != 0 && r->unk_80fa == 0 && r->unk_80fb == 0 && !g_sim.paused) {
        uint32_t now = sim_clock_ms();
        if ((uint32_t)rand_range(500, maxwait) < now - *t) {
            *t = now;
            select_plane(plane);
            g_build.map_dirty = 1;
            uint8_t *due = hell ? &g_sim.freeze_msg : &g_sim.blues_msg;
            if (!badthings_recover()) {
                music_fade(0xb4);
                if (*due) {
                    if (hell)
                        message("A Warm Fuzzy For You...", "Hell has thawed out!", "&Spiffy!");
                    else
                        message("Sun, sun, sun, here it comes...",
                                "The natural perkiness of Heaven has chased the Blues away!", "&Keen!");
                    *due = 0;
                }
                g_state.unk_5b2f61 &= hell ? 0xf7 : 0xfb;
                r->unk_80f9 = 0;
                sound_unload(hell ? 0x10007 : 0x1000b);
                sound_unload(hell ? 0x10008 : 0x1000c);
            } else {
                *due = 1;
            }
        }
    }
}

/* Birds of Paradise (bit 0, Heaven) / Bats out of Hell (bit 1, Hell):
 * they leave after 5 s without activity (realm +0x80fc). The timer follows
 * the clock while they are away. */
static void flock_timer(uint32_t now, int bitn)
{
    bool birds = bitn == 0;
    uint32_t *t = birds ? &g_sim.t_birds : &g_sim.t_bats;
    if ((g_state.unk_5b2f61 & (1u << bitn)) == 0 || g_sim.paused) {
        *t = now;
        return;
    }
    if (now - *t <= 5000)
        return;
    *t = now;
    select_plane(birds ? GS_PLANE_HEAVEN : GS_PLANE_HELL);
    PlaneStats *r = realm_cur();
    if (r->unk_80fc == 0) {
        g_build.map_dirty = 1;
        g_state.unk_5b2f61 &= (uint8_t)~(1u << bitn);
        if (birds)
            message("Coo coo ka chu...", "The Birds of Paradise have flown the coop!", "&Yay!");
        else
            message("It's over, bat-chum...", "The Bats Out Of Hell have fluttered away!", "&Hep!");
    } else {
        g_state.unk_5b2f5c = 0;
    }
    r->unk_80fc = 0;
}

/* ---- The phases ---------------------------------------------------------------------- */

/* the shared "is it time, and step" prologue of the timed phases */
static bool phase_due(uint32_t now, int d, uint32_t delay)
{
    if (now < (uint32_t)d * delay + g_sim.t_year)
        return false;
    if (++g_state.unk_5f7464 == 0)
        return false;
    g_state.unk_5f7464 = 0;
    return true;
}

static void phase_done(uint8_t next)
{
    g_state.sim_phase = next;
    date();
}

static void year_end(void)
{
    PlaneStats *he = world_realm(GS_PLANE_HEAVEN), *hl = world_realm(GS_PLANE_HELL);
    realm_worker_overflow(GS_PLANE_HEAVEN, GS_PLANE_HELL);
    realm_worker_overflow(GS_PLANE_HELL, GS_PLANE_HEAVEN);
    economy_score();
    hook(g_sim_hooks.statusbar);
    hook(g_sim_hooks.views_changed);
    g_sim.step_count = 0;
    for (int plane = GS_PLANE_HELL; plane <= GS_PLANE_HEAVEN; plane += 2) {
        select_plane(plane);
        realm_set29_year();
        realm_siphons_fill();
        karma_year();
        realm_ports_year();
    }
    hook(g_sim_hooks.graph_record);
    hook(g_sim_hooks.graph_summary);
    g_state.unk_5fa5f4 = 0;
    g_state.unk_5f7ed4 = 0;
    hook(g_sim_hooks.graphview_draw);
    hook(g_sim_hooks.mapview_refresh);
    g_state.unk_593250 = 0;
    economy_random_bad_things();
    if (bignum_less(g_state.pennies, 0)) {
        g_state.unk_593247++;
        if ((int8_t)g_state.unk_593247 > '2' && g_state.unk_5f4cd0[0][0] == 0)
            badthings_bankruptcy();
    } else {
        g_state.unk_593247 = 0;
    }
    if (hl->unk_80fb != 0)
        hl->unk_80fb--;
    if (he->unk_80fb != 0)
        he->unk_80fb--;
    karma_pending_scan();
    hook(g_sim_hooks.advisor_update);
    if (hl->unk_7ff0[RW_SURPLUS] > 100000 && he->unk_7ff0[RW_IMPORTED] + he->unk_7ff0[RW_TRAINED] < 10000 &&
        he->unk_7ff0[RW_SURPLUS] > 100000 && hl->unk_7ff0[RW_IMPORTED] + hl->unk_7ff0[RW_TRAINED] < 10000 &&
        (g_state.unk_63042c & 0x40000000) == 0 && g_state.unk_5f4cd0[0][0] == 0) {
        news(0x36d);
        g_state.unk_63042c |= 0x40000000;
    }
    if ((int32_t)g_state.unk_63048c >= 0)
        scenario_event_step();
    g_state.year++;
    g_state.sim_phase = 0;
    hook(g_sim_hooks.statusbar);
    g_sim.t_year = sim_clock_ms();
}

static void run_phase(int speed)
{
    int d = speed * 5 - 5;
    uint32_t now = sim_clock_ms();
    PlaneStats *he = world_realm(GS_PLANE_HEAVEN), *hl = world_realm(GS_PLANE_HELL);
    switch (g_state.sim_phase) {
    case 0:
        g_state.unk_62659c = 0x7ff;
        phase_done(20);
        break;
    default:
        g_state.sim_phase = 70;
        break;
    case 10:
        if (!phase_due(now, d, 0xf0))
            break;
        if (!planet_sector_step()) {
            g_sim.step_count++;
        } else {
            g_sim.step_count = 0;
            phase_done(15);
        }
        break;
    case 15:
        if (!phase_due(now, d, 0x140))
            break;
        if (!planet_tech_step()) {
            g_sim.step_count++;
            break;
        }
        g_sim.step_count = 0;
        he->unk_810b = 0;
        hl->unk_810b = 0;
        souls_gate_arrived = 0;
        g_state.unk_57e067 = 0;
        g_state.unk_5fa61d = 0;
        g_state.planet_deaths_last = g_state.planet_deaths;
        g_state.planet_deaths = 0;
        g_state.sim_phase = 100;
        g_state.unk_626598 = 0;
        he->unk_8113 = 0;
        hl->unk_8113 = 0;
        he->unk_8123 = 0;
        hl->unk_8123 = 0;
        date();
        hook(g_sim_hooks.graphview_draw);
        hook(g_sim_hooks.soulview_age);
        break;
    case 20:
        if (!phase_due(now, d, 0x50))
            break;
        if (!planet_stats_step())
            break;
        g_sim.step_count = 0;
        select_plane(GS_PLANE_HEAVEN);
        realm_tile_costs_update(false);
        clear_ledger(realm_cur());
        select_plane(GS_PLANE_HELL);
        realm_tile_costs_update(false);
        clear_ledger(realm_cur());
        g_state.embos = g_state.embos_next;
        g_state.embos_next.hi = 0;
        g_state.embos_next.lo = 0;
        phase_done(30);
        g_state.unk_5bb108 = 0;
        if (g_state.year == 666)
            sound(0x10028, 2);
        g_world.select_count = 0;
        break;
    case 30:
        if (!phase_due(now, d, 0xa0))
            break;
        if (!planet_emit_step()) {
            g_sim.step_count++;
            break;
        }
        g_sim.step_count = 0;
        planet_year(0);
        for (int plane = GS_PLANE_HEAVEN; plane >= GS_PLANE_HELL; plane -= 2) {
            select_plane(plane);
            souls_gate_arrivals();
            souls_siphon_release();
        }
        he->unk_810f = he->unk_810b ? per_soul(he->unk_810b) : 0;
        hl->unk_810f = hl->unk_810b ? per_soul(hl->unk_810b) : 0;
        if (souls_gate_arrived != 0) {
            uint32_t v = per_soul(souls_gate_arrived);
            bignum_add(&g_state.pennies, v);
            g_state.unk_5fa5f4 += v;
        }
        if (g_state.unk_5bb108 != 0) {
            uint32_t v = per_soul(g_state.unk_5bb108);
            bignum_sub(&g_state.pennies, v);
            g_state.unk_5f7ed4 += v;
        }
        phase_done(10);
        if (g_state.embos_next.lo == 0 && g_state.embos_next.hi == 0 && g_state.year > 10 &&
            (g_state.unk_63042c & 0x80000000u) == 0) {
            g_state.unk_63042c |= 0x80000000u;
            news(0x369);
        }
        break;
    case 40:
        if (!phase_due(now, d, 0x1e0))
            break;
        select_plane(GS_PLANE_HEAVEN);
        g_sim.step_count++;
        if (souls_flow(0))
            phase_done(50);
        break;
    case 50: {
        if (!phase_due(now, d, 0x230))
            break;
        select_plane(GS_PLANE_HELL);
        if (!souls_flow(0)) {
            g_sim.step_count++;
            break;
        }
        g_sim.step_count = 0;
        g_state.lost_souls = g_state.unk_626598;
        phase_done(60);
        he->unk_8117 = he->unk_8113 ? per_soul(he->unk_8113) * 2 : 0;
        hl->unk_8117 = hl->unk_8113 ? per_soul(hl->unk_8113) * 2 : 0;
        if (g_state.unk_626598 != 0) {
            uint32_t v = per_soul(g_state.unk_626598);
            bignum_sub(&g_state.pennies, v * 2);
            g_state.unk_5f7ed4 += v * 2;
        }
        if (g_build.editor)
            bignum_add(&g_state.pennies, g_state.unk_5f7ed4);
        g_sim.groups_last = souls_groups_created;
        Bignum *t = &g_sim.souls_total;
        t->hi = 0, t->lo = 0;
        bignum_add(t, hl->population);
        bignum_add(t, he->population);
        bignum_add(t, he->souls.lo);
        bignum_add(t, hl->souls.lo);
        t->hi = (int32_t)((uint32_t)t->hi + (uint32_t)hl->souls.hi);
        t->hi = (int32_t)((uint32_t)t->hi + (uint32_t)he->souls.hi);
        souls_groups_created = 0;
        if (g_state.year == 666)
            sound(0x10028, 2);
        break;
    }
    case 60:
        if (!phase_due(now, d, 0x280))
            break;
        for (int plane = GS_PLANE_HEAVEN; plane >= GS_PLANE_HELL; plane -= 2) {
            select_plane(plane);
            realm_vibes_update();
            realm_happiness_update();
        }
        g_state.unk_630430 = sim_soul_rate();
        if (g_state.unk_630430 > 0x9ff)
            g_state.unk_630430 = 0xa00;
        if (g_state.unk_630430 < 0xe)
            g_state.unk_630430 = 0xd;
        g_sim.happiness_sum = 0;
        g_sim.milestone_shown = 0;
        karma_rail_check();
        phase_done(70);
        break;
    case 70:
        if (!phase_due(now, d, 0x2d0))
            break;
        select_plane(GS_PLANE_HEAVEN);
        g_sim.step_count++;
        if (economy_realm_year())
            phase_done(80);
        break;
    case 80:
        if (!phase_due(now, d, 800))
            break;
        select_plane(GS_PLANE_HELL);
        g_sim.step_count++;
        if (economy_realm_year())
            year_end();
        break;
    case 90:
        select_plane(GS_PLANE_HELL);
        if (!souls_zone_scan()) {
            g_sim.step_count++;
        } else {
            g_sim.step_count = 0;
            phase_done(40);
        }
        break;
    case 100:
        if (now < (uint32_t)d * 400 + g_sim.t_year)
            break;
        select_plane(GS_PLANE_HEAVEN);
        if (souls_zone_scan())
            phase_done(90);
        break;
    }
}

/* 0x44876b */
void sim_update(int speed)
{
    g_sim.update_count++;
    int view_plane = g_world.plane;
    if (g_build.karma_dirty) {
        g_build.karma_dirty = 0;
        karma_clear();
        select_plane(GS_PLANE_MIDDLE);
        karma_rebuild();
        select_plane(view_plane);
    }
    uint32_t now = sim_clock_ms();
    if (now - g_sim.t_cd > 30000) {
        g_sim.t_cd = now;
        hook(g_sim_hooks.cd_spin_up);
    }
    if (speed != 0 && !g_sim.paused && (now = sim_clock_ms()) - g_sim.t_anim > 500) {
        g_sim.t_anim = now;
        select_plane(GS_PLANE_HELL);
        if (g_state.sim_phase > 30)
            realm_ports_animate();
        select_plane(GS_PLANE_HEAVEN);
        if (g_state.sim_phase > 30)
            realm_ports_animate();
        select_plane(GS_PLANE_MIDDLE);
        karma_trains_tick();
    }
    if (g_state.unk_5f4cd0[0][0] != 0 && !g_sim.paused && speed != 0 &&
        (now = sim_clock_ms()) - g_sim.t_badthings > 0xf0) {
        g_build.map_dirty = 1;
        g_sim.t_badthings = now;
        badthings_doom_step();
        badthings_fire_step();
    }
    if (g_sim.update_count > 0xfffffff0u)
        g_sim.update_count = 0;
    freeze_timer(GS_PLANE_HELL);
    freeze_timer(GS_PLANE_HEAVEN);
    now = sim_clock_ms();
    flock_timer(now, 0);
    flock_timer(now, 1);
    if (g_build.status_dirty) {
        g_build.status_dirty = 0;
        hook(g_sim_hooks.statusbar);
        hook(g_sim_hooks.statusbar_refresh);
    }
    if (speed == 0 || g_sim.paused)
        return;
    g_build.map_dirty = 1;
    run_phase(speed);
}

int sim_run_phase(int speed, int max_calls)
{
    int from = g_state.sim_phase;
    for (int i = 0; i < max_calls && g_state.sim_phase == from; i++)
        run_phase(speed);
    return g_state.sim_phase;
}

/* 0x448150 */
uint32_t sim_soul_rate(void)
{
    const SoulRatePoint *t = g_tables.soul_rate;
    uint32_t c = 0;
    uint32_t total = g_sim.souls_total.lo;
    if (g_state.unk_593250 == 0)
        c = 0x1a;
    if (g_sim.souls_total.hi == 0) {
        if (t[5].souls <= total)
            c = 0xa00;
        if (total == 0)
            c = 0x1a;
    } else {
        c = 0xa00;
    }
    if (c != 0) {
        if (g_state.bad_things == 0)
            c >>= 1;
        return c;
    }
    int i = 0, j;
    do {
        j = i;
        i = j + 1;
    } while (i < 5 && t[i].souls < total);
    const SoulRatePoint *a = &t[j], *b = &t[i];
    uint32_t hi, lo;
    if (a->rate_hi < b->rate_hi)
        hi = a->rate_hi + mul_div_clamp(total - a->souls, b->souls - a->souls, b->rate_hi - a->rate_hi);
    else if (b->rate_hi < a->rate_hi)
        hi = b->rate_hi - mul_div_clamp(b->souls - total, b->souls - a->souls, a->rate_hi - b->rate_hi);
    else
        hi = b->rate_hi;
    if (a->rate_lo < b->rate_lo)
        lo = a->rate_lo + mul_div_clamp(total - a->souls, b->souls - a->souls, b->rate_lo - a->rate_lo);
    else if (b->rate_hi < a->rate_hi) /* sic: compares the other column, and adds */
        lo = a->rate_lo + mul_div_clamp(b->souls - total, b->souls - a->souls, a->rate_lo - b->rate_lo);
    else
        lo = b->rate_lo;
    if (lo == hi) {
        c = lo;
        if (g_state.bad_things == 0)
            c >>= 1;
        return c;
    }
    g_state.unk_638690 = g_sim.happiness_sum / g_state.unk_593250;
    uint32_t avg = g_state.unk_638690;
    uint32_t mid = ((hi - lo) >> 1) + lo;
    c = hi;
    if (avg < 0x80) {
        if (avg < 0x1d) {
            c = lo + mul_div_clamp(avg, 0x1c, mid - lo);
        } else {
            uint32_t u = avg > 0x7f ? 0x80 : avg;
            c = mid + mul_div_clamp(0x80 - u, 100, hi - mid);
        }
        if (g_state.bad_things == 0)
            c >>= 1;
    }
    return c;
}

/* 0x448594 */
void scenario_event_step(void)
{
    int32_t i = (int32_t)g_state.unk_63048c;
    if (g_state.unk_57e160 != 0) {
        g_state.unk_57e160--;
        return;
    }
    if (i < 0 || i + 1 >= (int32_t)sizeof g_state.scenario_commands) {
        g_state.unk_63048c = 0xffffffffu;
        return;
    }
    char ch = g_state.scenario_commands[i];
    char arg = g_state.scenario_commands[i + 1];
    g_state.unk_63048c = (uint32_t)(i + 1);
    switch (ch) {
    case 'D':
    case 'd':
        g_state.unk_63048c = (uint32_t)(i + 2);
        bad_thing_start(arg - '1');
        break;
    case 'F':
    case 'f':
        planet_flood();
        break;
    case 'P':
    case 'p':
        break;
    case 'W':
    case 'w':
        g_state.unk_57e160 = (uint32_t)(int32_t)(arg - '0');
        g_state.unk_63048c = (uint32_t)(i + 2);
        break;
    default:
        g_state.unk_63048c = 0xffffffffu;
        break;
    }
}
