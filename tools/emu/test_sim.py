#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential tests of the WP1 simulation (src/game/sim.c, economy.c,
realm.c, tables.c) against ALIFE.EXE under the emulator.

For each save (default: orig/ASCEN00[1-4].ASC) both sides load it (the
original through its own loader with the post-load rebuilds), the
original's constant tables are filled by its own game_init_defaults
(0x45546c, with game_reset_world stubbed) and compared with tables.c, the
tile type tables and the soul-rate curve are copied from the port, and both
RNGs get the same state. Then each test calls the original function and the
port's on the same state and compares every saved global, the grids, the
current-plane globals and the RNG (difftest.Harness.check), plus the
runtime globals the test names.

    tools/emu/test_sim.py [--only NAME[,NAME]] [--keep-going] [save ...]

UI calls (message boxes, sounds, news, views) are stubbed in the
original; the port's hooks are unset. The map object code runs on both
sides (WP3; tools/emu/mapobj_env.py gives the original the tile entries
and a main window without a surface, so its redraws draw nothing). Calls into other packages run as they
are on both sides (the original's code against the port's WP1b/WP2/WP5).
"""
import argparse
import ctypes
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from difftest import Harness, SAVES, orig_path  # noqa: E402
import bridge  # noqa: E402
import mapobj_env  # noqa: E402
from names import TILE_INFO  # noqa: E402

SELECT_PLANE = 0x42b102
MAP_VIEW = bridge.MAP_VIEW
GAME_INIT_DEFAULTS, GAME_RESET_WORLD, SND_PRELOAD_LOCKED = 0x45546c, 0x4535db, 0x475467

# The original's BuildingSpec tables, in shim_sim_tables(0) order.
SPEC_TABLES = [(0x6304a0, 8), (0x57e110, 6), (0x57e0b0, 6), (0x630200, 8), (0x5f7410, 6), (0x59cec0, 6),
               (0x630280, 4), (0x57e030, 3)]
LEVELS = [(0x639250, 26), (0x6390f0, 42)]
SOUL_RATE = 0x6391d0

UI_STUBS = {
    0x46f414: 'microview_refresh', 0x4c59a2: 'wm_is_visible',
    0x46313b: 'macro_refresh', 0x4bfc69: 'ctl_message_box', 0x4751af: 'snd_play', 0x476582: 'news_popup_show',
    0x4753f2: 'snd_fade_out_music', 0x4754a1: 'snd_unload', 0x447243: 'statusbar_draw',
    0x446904: 'statusbar_draw_date', 0x446dc5: 'statusbar 0x446dc5', 0x4c2830: 'wm_send',
    0x43dc3c: 'bad_thing_start (WP4, a stub in the port)',
}

# Runtime globals (not saved) compared after the calls that use them.
RUNTIME = {0x63911c: 'happiness_sum', 0x6391c8: 'milestone_shown', 0x6390e8: 'souls_total.hi',
           0x6390ec: 'souls_total.lo'}


class Mismatch(Exception):
    pass


class SimPair:
    def __init__(self):
        self.h = h = Harness(run_rebuilds=True)
        self.L = L = h.port_.lib
        c = ctypes
        L.shim_sim_setup.argtypes = [c.c_uint32, c.c_uint32]
        L.shim_sim_clock.argtypes = [c.c_uint32, c.c_uint32]
        L.shim_sim_get.argtypes = [c.c_uint32]
        L.shim_sim_get.restype = c.c_uint32
        L.shim_sim_set.argtypes = [c.c_uint32, c.c_uint32]
        L.shim_sim_tables.argtypes = [c.c_int, c.c_void_p]
        L.world_select_plane.argtypes = [c.c_int]
        L.realm_souls_from_planet.argtypes = [c.c_int, c.c_int, c.c_int, c.c_uint32, c.c_uint32]
        L.realm_worker_overflow.argtypes = [c.c_int, c.c_int]
        L.economy_realm_year.restype = c.c_bool
        L.sim_soul_rate.restype = c.c_uint32
        for a in UI_STUBS:
            h.emu.stub(a)
        from alife_emu import ORIG
        if not L.shim_planet_init(ORIG.encode()):
            raise SystemExit('port: planet_tables_load failed (ALIFE.EXE in %s?)' % ORIG)
        self.time = 1000000
        h.emu.import_stub('timeGetTime', 0, lambda e, a: self.time)
        mapobj_env.install(h)

    def tables(self, which):
        n = self.L.shim_sim_tables(which, None)
        buf = ctypes.create_string_buffer(n)
        self.L.shim_sim_tables(which, buf)
        return buf.raw

    def prepare(self, save, seed=12345):
        h, e = self.h, self.h.emu
        h.load(save)
        e.stub(GAME_RESET_WORLD)
        e.stub(SND_PRELOAD_LOCKED)
        h.orig(GAME_INIT_DEFAULTS, track=False)
        for a in UI_STUBS:
            e.stub(a)
        self.L.shim_sim_setup(self.time, 0)
        specs = self.tables(0)
        off = 0
        for addr, n in SPEC_TABLES:
            if e.read(addr, 13 * n) != specs[off:off + 13 * n]:
                raise Mismatch('tables.c: BuildingSpec table %#x differs from game_init_defaults' % addr)
            off += 13 * n
        lv = self.tables(1)
        if e.read(LEVELS[0][0], 26) + e.read(LEVELS[1][0], 42) != lv:
            raise Mismatch('tables.c: level ranges differ from game_init_defaults')
        e.write(SOUL_RATE, self.tables(2))  # game_reset_world's part
        # Start from identical grids and tile tables: the post-load
        # rebuilds (WP2) aren't what this tests.
        bridge.write_port_raw(e, h.port_)
        for a in RUNTIME:
            e.w32(a, 0)
        e.w32(0x630510, 0)
        e.w32(0x57e01c, 0)
        e.w8(0x65ede8, 0)  # the realm year's column
        e.w8(0x630514, 0)  # editor mode off
        h.seed(seed)
        self.check('after load')

    def select(self, plane):
        self.h.orig(SELECT_PLANE, MAP_VIEW, plane, track=False)
        self.L.world_select_plane(plane)

    def check(self, what, runtime=(), ignore=()):
        self.h.check(what=what, ignore=list(ignore) + ['plane_select_count'])
        bad = []
        for a in runtime:
            o, p = self.h.emu.u32(a), self.L.shim_sim_get(a)
            if a in (0x6391c8,):
                o &= 0xff
            if o != p:
                bad.append('%s @%#x: orig %d port %d' % (RUNTIME.get(a, '?'), a, o, p))
        if bad:
            raise Mismatch('%s: runtime globals differ\n  %s' % (what, '\n  '.join(bad)))


# ---- the tests ---------------------------------------------------------------------

def t_vibes(s):
    for plane in (3, 1):
        s.select(plane)
        s.h.orig(0x43a81d)
        s.L.realm_vibes_update()
        s.check('realm_vibes_update plane %d' % plane)


def t_happiness(s):
    for plane in (3, 1):
        s.select(plane)
        s.h.orig(0x43a81d)
        s.L.realm_vibes_update()
        s.h.orig(0x43b86f)
        s.L.realm_happiness_update()
        s.check('realm_happiness_update plane %d' % plane, runtime=[0x63911c])


def t_set29(s):
    for plane in (1, 3):
        s.select(plane)
        s.h.orig(0x43188a)
        s.L.realm_set29_year()
        s.check('realm_set29_year plane %d' % plane)
        s.h.orig(0x42dd50)
        s.L.realm_siphons_fill()
        s.check('realm_siphons_fill plane %d' % plane)
        s.h.orig(0x446829)
        s.L.realm_ports_year()
        s.check('realm_ports_year plane %d' % plane)


def t_ports_animate(s):
    for i in range(20):
        for plane in (1, 3):
            s.select(plane)
            s.h.orig(0x447e8f)
            s.L.realm_ports_animate()
    s.check('realm_ports_animate x20')


def t_age(s):
    for plane in (3, 1):
        s.select(plane)
        s.h.orig(0x445416)
        s.L.realm_age_departures()
        s.check('realm_age_departures plane %d' % plane)


def t_planet_souls(s):
    import random
    r = random.Random(5)
    for i in range(64):
        sx, sy, t = r.randrange(64), r.randrange(32), r.randrange(16)
        count, size = r.choice([1, 5, 100, 12345]), r.choice([1, 2, 7])
        s.h.orig(0x445785, 0, sx, sy, t, count, size)
        s.L.realm_souls_from_planet(sx, sy, t, count, size)
    s.check('realm_souls_from_planet x64')


def t_overflow(s):
    for a, b in ((3, 1), (1, 3)):
        s.h.orig(0x446242, a, b)
        s.L.realm_worker_overflow(a, b)
        s.check('realm_worker_overflow(%d, %d)' % (a, b))


def t_score(s):
    s.h.orig(0x43eb7c)
    s.L.economy_score()
    s.check('economy_score')


def t_soul_rate(s):
    for plane in (3, 1):
        s.select(plane)
        s.h.orig(0x43a81d)
        s.L.realm_vibes_update()
        s.h.orig(0x43b86f)
        s.L.realm_happiness_update()
    for total in (0, 1, 3000000, 80000000, 600000000, 2000000000):
        for a, v in ((0x6390e8, 0), (0x6390ec, total)):
            s.h.emu.w32(a, v)
            s.L.shim_sim_set(a, v)
        o = s.h.orig(0x448150) & 0xffffffff
        p = s.L.sim_soul_rate()
        if o != p:
            raise Mismatch('sim_soul_rate(total %d): orig %#x port %#x' % (total, o, p))
        s.check('sim_soul_rate(total %d)' % total, runtime=list(RUNTIME))


def realm_year_stack_byte(emu, via_sim_update=False):
    """Address of 0x43ed55's stack byte local_12c when emu.call runs it
    directly (two arguments) or through sim_update (five; its frame is
    0x3c bytes down to the call): the original reads the byte before
    writing it (a stale value feeds the variety score, realm +0x811f); the
    port starts it at 0."""
    import alife_emu
    nargs = 5 if via_sim_update else 2
    sp = (alife_emu.STACK + alife_emu.STACK_SIZE - 0x1000 - 4 * (nargs + 1)) & ~0xf
    return sp - (0x3c if via_sim_update else 0) - 0x12c


def t_realm_year(s):
    for plane in (3, 1):
        s.select(plane)
        for k in range(200):
            s.h.emu.w8(realm_year_stack_byte(s.h.emu), 0)
            o = s.h.orig(0x43ed55, s.h.emu.u32(0x5f7460), MAP_VIEW)
            p = s.L.economy_realm_year()
            if bool(o) != p:
                raise Mismatch('realm year plane %d call %d: orig %d port %d' % (plane, k, o, p))
            if o:
                break
        s.check('economy_realm_year plane %d' % plane, runtime=[0x6391c8])


def t_realm_year_chunked(s):
    """The incremental pass: both clocks advance 16 ms per read, so each
    call does a few columns and resumes."""
    e = s.h.emu
    ticks = [s.time]

    def tick(em, a):
        ticks[0] += 16
        return ticks[0]
    e.import_stub('timeGetTime', 0, tick)
    try:
        for plane in (3, 1):
            s.select(plane)
            for k in range(400):
                e.w8(realm_year_stack_byte(e), 0)
                s.L.shim_sim_clock(ticks[0], 16)
                o = s.h.orig(0x43ed55, e.u32(0x5f7460), MAP_VIEW)
                p = s.L.economy_realm_year()
                if bool(o) != p:
                    raise Mismatch('chunked realm year plane %d call %d: orig %d port %d' % (plane, k, o, p))
                if o:
                    break
            s.check('chunked economy_realm_year plane %d (%d calls)' % (plane, k + 1), runtime=[0x6391c8])
    finally:
        e.import_stub('timeGetTime', 0, lambda em, a: s.time)
        s.L.shim_sim_clock(s.time, 0)


def t_years(s):
    """Phases 60-80 and the year-end passes, three times over."""
    for year in range(3):
        for plane in (3, 1):
            s.select(plane)
            s.h.orig(0x43a81d)
            s.L.realm_vibes_update()
            s.h.orig(0x43b86f)
            s.L.realm_happiness_update()
        for plane in (3, 1):
            s.select(plane)
            s.h.emu.w8(realm_year_stack_byte(s.h.emu), 0)
            o = s.h.orig(0x43ed55, s.h.emu.u32(0x5f7460), MAP_VIEW)
            p = s.L.economy_realm_year()
            if not o or not p:
                raise Mismatch('realm year did not finish in one call')
        for a, b in ((3, 1), (1, 3)):
            s.h.orig(0x446242, a, b)
            s.L.realm_worker_overflow(a, b)
        s.h.orig(0x43eb7c)
        s.L.economy_score()
        for plane in (1, 3):
            s.select(plane)
            for addr, fn in ((0x43188a, 'realm_set29_year'), (0x42dd50, 'realm_siphons_fill'),
                             (0x446829, 'realm_ports_year')):
                s.h.orig(addr)
                getattr(s.L, fn)()
        s.h.orig(0x43e4e5)
        s.L.economy_random_bad_things()
        for a in (0x63911c, 0x6391c8):
            s.h.emu.w32(a, 0)
            s.L.shim_sim_set(a, 0)
        s.check('year %d' % (year + 1), runtime=list(RUNTIME))


# What sim_update reaches that belongs to unported packages (stubs or
# no-ops in the port) or the UI: stubbed in the original for t_sim_update.
SIM_UPDATE_STUBS = {
    0x4159ba: 'graphview_draw', 0x4640c9: 'mapview_refresh', 0x40d34b: 'graph_history_record',
    0x4154ab: 'graph summary', 0x418a39: 'advisor_update', 0x4486bd: 'cd_spin_up',
    0x47d6fb: 'Soulview trackers age (a hook in the port)',
    0x4442dd: 'badthings_step', 0x443c9e: 'badthings_step2', 0x442122: 'badthings_spread',
    0x442287: 'badthings_recover', 0x444d23: 'badthings_bankruptcy',
}
SIM_TIMERS = [0x65ee64, 0x65ee68, 0x65ee6c, 0x65ee70, 0x65ee74, 0x65ee78, 0x65ee7c, 0x65ee80, 0x65ee84,
              0x65ee88, 0x4f3120, 0x4f3144]


def t_sim_update(s, years=2):
    """Whole years through sim_update (speed 1, stopped clock): the
    scheduler with everything it calls (the port's WP1b/WP2/WP5 against
    the original's code), compared at every phase change."""
    e = s.h.emu
    for a in SIM_UPDATE_STUBS:
        e.stub(a)
    for a in SIM_TIMERS:
        e.w32(a, 0)
    e.w8(0x5fa61c, 0)  # game_paused
    args = (e.u32(0x5f7460), e.u32(0x5c6674), e.u32(0x63865c), MAP_VIEW, 1)
    year0 = e.u32(0x630268)
    calls = 0
    ignore = ['summary', 'block_4f33f8']
    while e.u32(0x630268) < year0 + years and calls < 20000:
        before = e.u8(0x5bb104)
        e.w8(realm_year_stack_byte(e, True), 0)
        s.h.orig(0x44876b, *args)
        s.L.sim_update(1)
        calls += 1
        after = e.u8(0x5bb104)
        if after != before:
            s.check('sim_update phase %d -> %d (year %d, call %d)' % (before, after, e.u32(0x630268), calls),
                    ignore=ignore)
    if e.u32(0x630268) < year0 + years:
        raise Mismatch('sim_update: year stuck in phase %d' % e.u8(0x5bb104))
    s.check('sim_update after %d years' % years, runtime=list(RUNTIME), ignore=ignore)


TESTS = [('vibes', t_vibes), ('happiness', t_happiness), ('set29', t_set29), ('ports', t_ports_animate),
         ('age', t_age), ('planet_souls', t_planet_souls), ('overflow', t_overflow), ('score', t_score),
         ('soul_rate', t_soul_rate), ('realm_year', t_realm_year), ('chunked', t_realm_year_chunked),
         ('years', t_years), ('sim_update', t_sim_update)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('saves', nargs='*')
    ap.add_argument('--only', default='')
    ap.add_argument('--keep-going', action='store_true')
    args = ap.parse_args()
    saves = args.saves or [orig_path(n) for n in SAVES]
    only = set(filter(None, args.only.split(',')))
    s = SimPair()
    fails = 0
    for save in saves:
        print(os.path.basename(save))
        for name, fn in TESTS:
            if only and name not in only:
                continue
            try:
                s.prepare(save)
                fn(s)
                print('  %-14s ok' % name)
            except (Mismatch, AssertionError) as e:
                fails += 1
                print('  %-14s MISMATCH\n    %s' % (name, str(e).replace('\n', '\n    ')))
                if not args.keep_going:
                    break
            except Exception:
                fails += 1
                print('  %-14s ERROR' % name)
                traceback.print_exc()
                if not args.keep_going:
                    break
    print('%d failures' % fails if fails else 'all ok')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
