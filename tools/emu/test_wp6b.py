#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the WP6b views' state-changing and RNG-drawing
functions (src/ui/microview.c, macro.c, soulview_text.c, src/game/
soultrack.c) against ALIFE.EXE under the emulator.

  - the Soulview's SOUL names (0x47f8fe) for thousands of seeds and its
    life stories (0x47ad32 + 0x47af6f, the text of 0x47adea) for every
    template, both realms, permanent and temporary trackers;
  - starting to follow SOULs (0x47e7cc: seed, belief, age; 4 RNG draws),
    the trackers' yearly pass (0x47d6fb) over several years with residents
    whose time is up, emptied buildings, finished and walking trackers;
  - the Macromanager: counting and cost (0x462164(0)) and implementing
    (0x462164(1)) for kind / lock / balance combinations, the focus slider
    (0x4635d5);
  - the Microview's actions: funding focus slider (0x46e3ef), acceptance
    sliders (0x46f056, 0x46f2a1), lock (0x4699d5), training on/off
    (0x46ef1f), loans (0x4694df), upgrade price and upgrade (0x4696e6,
    0x46f7d8: destroy and rebuild one size up), opening (0x46f613: 2 RNG
    draws).

For each save both sides load it, the port's state (with the tables a
running game has) is pushed into the original as tools/emu/test_souls.py
does, then after every call return values, the RNG and the whole state
must be identical. Drawing is stubbed in the original (the port has no
window); message boxes answer with a chosen button on both sides; map
objects (the walkers) run on both (tools/emu/mapobj_env.py).

    tools/emu/test_wp6b.py [save ...]
"""
import argparse
import ctypes
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bridge  # noqa: E402
import mapobj_env  # noqa: E402
from alife_emu import ORIG  # noqa: E402
from difftest import Harness, orig_path, SAVES  # noqa: E402

TABLES = [(0x6304a0, 8), (0x57e110, 6), (0x57e0b0, 6), (0x630200, 8), (0x5f7410, 6), (0x59cec0, 6), (0x630280, 4),
          (0x57e030, 3)]
SPECIAL_POS, OBJ_5F7408 = 0x5bb0f0, 0x5f7408
SELECT_PLANE = 0x42b102
MAP_VIEW = bridge.MAP_VIEW
PENNIES = 0x630438
YEAR = 0x630268
RECS, N_USED, N_WALK, SELECTED = 0x4f33f8, 0x664808, 0x66480c, 0x664810

# the original's functions
NAME, TEMPLATE, EXPAND = 0x47f8fe, 0x47ad32, 0x47af6f
AGE, START = 0x47d6fb, 0x47e7cc
MACRO_APPLY, MACRO_SLIDE = 0x462164, 0x4635d5
MV_FUND, MV_TRAIN, MV_OMNI, MV_LOCK, MV_ACTIVE, MV_LOAN = 0x46e3ef, 0x46f056, 0x46f2a1, 0x4699d5, 0x46ef1f, 0x4694df
MV_PRICE, MV_UPGRADE, MV_OPEN = 0x4696e6, 0x46f7d8, 0x46f613
MV_X, MV_Y, MV_PLANE, MV_SET, MV_INDEX, MV_COST = 0x638ed8, 0x638ed9, 0x638ef1, 0x6611f4, 0x638fd0, 0x6611f8
# port action ids (ui/microview.h)
ACT_FUND, ACT_TRAINING, ACT_OMNI, ACT_LOCK, ACT_ACTIVE, ACT_LOAN, ACT_UPGRADE = range(1, 8)

STUBS = {
    # drawing and windows
    0x4d08e0: 'gr_fill_rect', 0x4cf504: 'font_select', 0x4d0102: 'font_set_colors', 0x4cf9f0: 'font_draw_text',
    0x4c25c4: 'wm_redraw', 0x4ba7e9: 'ctl_button_set_pressed', 0x4ba74a: 'ctl_button_set_label',
    0x4ba26a: 'ctl_button_enable', 0x4ba2c1: 'ctl_button_show', 0x4c2248: 'wm_raise', 0x4c23da: 'wm_show',
    0x4c3f4d: 'wm_set_minimized', 0x4c63f0: 'sys_pump', 0x407282: 'game_frame_update', 0x4c2830: 'wm_send',
    0x4c2733: 'wm_update', 0x4c59a2: 'wm_is_visible',
    # the views' redraws
    0x47f259: 'soulview status', 0x47e6cc: 'soulview_refresh', 0x46e5c7: 'microview funding knob',
    0x46edce: 'microview acceptance knob', 0x468df0: 'microview loan panel', 0x46e6fb: 'microview training text',
    0x46ce40: 'microview redraw', 0x46f414: 'microview_refresh', 0x46313b: 'macro_refresh',
    0x46369f: 'macro knob', 0x408c4d: 'views_refresh_all',
    # sound, the rest of the UI the build code reaches
    0x4751af: 'snd_play', 0x475428: 'snd_preload', 0x4754a1: 'snd_unload', 0x475569: 'snd_stop (music)',
    0x403bfd: 'after message box', 0x4ebfbc: 'map_hilite_area', 0x4ebd8a: 'map_redraw_cells',
    0x47e229: 'Soulview trackers after rotating', 0x47e5bc: 'Soulview trackers reset', 0x4c8393: 'menu_set_checked',
    0x4c8355: 'menu_is_checked', 0x408ca4: 'ui_busy_cursor',
}
MSGBOX = 0x4bfc69


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


class Test:
    def __init__(self, verbose=False):
        # (the rebuilds stay stubbed: the port state, rebuilt tables included,
        # is pushed into the original after every load)
        self.h = h = Harness(run_rebuilds=False)
        L = self.L = h.port_.lib
        L.plat_set_data_dir.argtypes = [ctypes.c_char_p]
        L.shim_wp6b_init.argtypes = [ctypes.c_char_p]
        L.shim_wp6b_init(ORIG.encode())
        L.shim_wp6b_name.argtypes = [ctypes.c_uint32, ctypes.c_char_p]
        L.shim_wp6b_describe.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]
        L.shim_wp6b_describe.restype = ctypes.c_size_t
        L.shim_wp6b_rec_get.argtypes = [ctypes.c_int, ctypes.c_char_p]
        L.shim_wp6b_rec_set.argtypes = [ctypes.c_int, ctypes.c_char_p]
        L.shim_wp6b_macro.argtypes = [ctypes.c_uint32] + [ctypes.c_int] * 6 + [ctypes.POINTER(ctypes.c_uint32)]
        L.shim_wp6b_mv_cost.restype = ctypes.c_uint32
        L.shim_wp6b_pennies.argtypes = [ctypes.c_int32, ctypes.c_uint32]
        L.shim_wp6b_year.argtypes = [ctypes.c_uint32]
        L.shim_souls_tables.restype = ctypes.c_size_t
        L.shim_souls_tables.argtypes = [ctypes.c_void_p]
        L.realm_building_pop.restype = ctypes.c_uint32
        L.shim_cell_get.restype = ctypes.c_uint32
        self.answer = 1
        self.verbose = verbose
        self.checks = 0
        self.stub_all()
        mapobj_env.install(h)

    def stub_all(self):
        e = self.h.emu
        for a in STUBS:
            e.stub(a)
        e.hook(MSGBOX, lambda emu, args: self.answer)

    # ---- state -----------------------------------------------------------------
    def load(self, path):
        h, L = self.h, self.L
        h.load(path)
        L.shim_souls_setup()
        h.emu.restore(h.fresh)
        self.stub_all()
        bridge.orig_load(h.emu, h.port_.save_mem(h.version))
        self.stub_all()  # (the loader's stubs include the message box)
        bridge.write_port_raw(h.emu, h.port_)
        buf = ctypes.create_string_buffer(4096)
        n = L.shim_souls_tables(buf)
        raw, off = buf.raw[:n], 0
        for addr, recs in TABLES:
            h.emu.write(addr, raw[off:off + 13 * recs])
            off += 13 * recs
        h.emu.write(SPECIAL_POS, raw[off:off + 20])
        h.emu.w8(OBJ_5F7408, raw[off + 20])
        h.emu.w8(0x57e018, h.port('shim_wp6b_obj_57e018') if hasattr(L, 'shim_wp6b_obj_57e018') else 0)
        h.emu.rng_seed(h.port_.rng_state())
        self.check('load')

    def check(self, what):
        h = self.h
        self.checks += 1
        r1, r2 = h.emu.rng_state(), h.port_.rng_state()
        if r1 != r2:
            raise AssertionError('%s: RNG state original %08x, port %08x' % (what, r1, r2))
        # (plane_select_count is runtime only; difftest ignores its low byte)
        d = [x for x in h.diff() if not x.name.startswith('plane_select_count')]
        if d:
            raise AssertionError('%s: %d differences between the original and the port\n%s'
                                 % (what, len(d), '\n'.join('  ' + str(x) for x in d[:40])))
        if self.verbose:
            print('  ok', what)

    def select(self, plane):
        self.h.emu.call(SELECT_PLANE, MAP_VIEW, plane)
        self.L.world_select_plane(plane)

    def both_answer(self, b):
        self.answer = b
        self.L.shim_wp6b_answer(b)

    def set_pennies(self, v):
        hi, lo = (0, v) if v >= 0 else (-1, -v)
        self.h.emu.w32(PENNIES, hi)
        self.h.emu.w32(PENNIES + 4, lo)
        self.L.shim_wp6b_pennies(hi, lo)

    def set_year(self, y):
        self.h.emu.w32(YEAR, y)
        self.L.shim_wp6b_year(y)

    def rec_put(self, i, b):
        self.h.emu.write(RECS + i * 0x26, bytes(b))
        self.L.shim_wp6b_rec_set(i, bytes(b))

    def rec(self, i):
        return bytearray(self.h.emu.read(RECS + i * 0x26, 0x26))

    def counts(self, used, walking, sel=0):
        e = self.h.emu
        e.w8(N_USED, used)
        e.w8(N_WALK, walking)
        e.w8(SELECTED, sel)
        self.L.shim_wp6b_counts(used, walking, sel)

    def buildings(self, plane, sets, n, need_pop=True, skip=0):
        """(x, y) of n structure origins of the given tile sets on `plane`."""
        self.select(plane)
        out = []
        for i in range(skip, skip + 10000):
            j = i % 10000
            x, y = j % 100, j // 100
            c = self.L.shim_cell_get(x, y)
            if c >> 27 not in sets:
                continue
            if need_pop and c >> 27 in (14, 15) and self.L.realm_building_pop(x, y) == 0:
                continue
            out.append((x, y))
            if len(out) >= n:
                break
        return out

    # ---- the Soulview ------------------------------------------------------------
    def test_names(self, n=3000):
        e = self.h.emu
        buf = e.malloc(64)
        out = ctypes.create_string_buffer(64)
        rnd = random.Random(6)
        seeds = [0, 1, 0xffffffff, 0x20000000, 0x1fffffff, 0xaaaaaaab] + [rnd.getrandbits(32) for _ in range(n)]
        for s in seeds:
            self.h.orig(NAME, s, buf, track=False)
            a = e.cstr(buf, 64)
            self.L.shim_wp6b_name(s, out)
            b = out.value.decode('latin1')
            if a != b:
                raise AssertionError('name(%08x): original %r, port %r' % (s, a, b))
        print('  names: %d seeds identical (e.g. %r)' % (len(seeds), b))

    def test_stories(self, n=150):
        e = self.h.emu
        tmpl, out = e.malloc(4096), e.malloc(16384)
        pbuf = ctypes.create_string_buffer(16384)
        rnd = random.Random(7)
        kinds = set()
        total = 0
        for plane in (3, 1):
            homes = self.buildings(plane, (14, 15), 4)
            for k in range(n):
                seed = rnd.getrandbits(32)
                if k < 36:
                    seed = (seed & ~0x1f) | (k % 18)  # every template
                x, y = homes[k % len(homes)]
                b = bytearray(0x26)
                b[0] = 1
                b[2], b[3], b[4], b[5] = x, y, x, y
                b[8:12] = seed.to_bytes(4, 'little')
                b[0xc] = k & 1
                b[0x10] = plane
                self.rec_put(0, b)
                group = e.u32(0x66a7f0 + 4 * ((seed & 0x1f) % 18))
                self.h.orig(TEMPLATE, group, tmpl, seed, track=False)
                e.w8(0x66482c, 0)
                e.w32(0x664830, 1)
                e.w8(0x66a838, 0)
                end = self.h.orig(EXPAND, RECS, tmpl, out, track=False) & 0xffffffff
                e.w8(end, 0)
                a = e.cstr(out, 16384)
                self.L.shim_wp6b_describe(0, pbuf, 16384)
                p = pbuf.value.decode('latin1')
                if a != p:
                    i = next((j for j in range(min(len(a), len(p))) if a[j] != p[j]), min(len(a), len(p)))
                    raise AssertionError('story seed %08x plane %d temp %d differs at %d:\n  orig %r\n  port %r'
                                         % (seed, plane, k & 1, i, a[max(0, i - 40):i + 60],
                                            p[max(0, i - 40):i + 60]))
                kinds.add((seed & 0x1f) % 18)
                total += len(a)
        self.rec_put(0, bytearray(0x26))
        self.check('stories')
        print('  stories: %d (all %d templates), %d characters identical' % (2 * n, len(kinds), total))

    def start(self, i, plane, x, y, track=1):
        b = self.rec(i)
        b[1] = track
        self.rec_put(i, b)
        self.select(plane)
        self.h.orig(START, RECS + i * 0x26, MAP_VIEW, x, y)
        self.L.shim_wp6b_start(i, plane, x, y)

    def test_trackers(self, name):
        h = self.h
        self.counts(0, 0)
        for i in range(10):
            self.rec_put(i, bytearray(0x26))
        homes = self.buildings(3, (14, 15), 3, skip=1234) + self.buildings(1, (14, 15), 3, skip=4321)
        planes = [3, 3, 3, 1, 1, 1]
        for i, ((x, y), pl) in enumerate(zip(homes, planes)):
            self.start(i, pl, x, y, track=i & 1)
            used = h.emu.u8(N_USED)
            self.check('%s: start tracker %d at (%d,%d) plane %d' % (name, i, x, y, pl))
        # finished trackers (training, Omnibolge, lost) and SOULs headed for
        # a Karma station / Gate (walking, category 14 / 15)
        year = h.emu.u32(YEAR)
        for i, (st, cat, age) in zip(range(6, 10), ((6, 0, 6), (7, 0, 2), (4, 0, 9), (2, 14, 1))):
            b = bytearray(0x26)
            b[0], b[0x10], b[0x1b] = st, 3 if i & 1 else 1, cat
            b[8:12] = (0x1234567 * (i + 1) & 0xffffffff).to_bytes(4, 'little')
            b[0x1c:0x20] = (year - age).to_bytes(4, 'little')
            self.rec_put(i, b)
        used = sum(1 for i in range(10) if self.rec(i)[0])
        self.counts(used, sum(1 for i in range(10) if self.rec(i)[0] == 2))
        self.check('%s: trackers planted' % name)
        # residents whose time is up leave; one in an emptied building
        b = self.rec(1)
        b[0x13:0x17] = (1).to_bytes(4, 'little')
        self.rec_put(1, b)
        empty = self.buildings(1, (14, 15), 1, need_pop=False, skip=77)
        empty = [p for p in self.buildings(1, (14, 15), 400, need_pop=False) if self.L.realm_building_pop(*p) == 0]
        if empty:
            b = self.rec(4)
            b[4], b[5], b[6], b[7] = empty[0][0], empty[0][1], empty[0][0], empty[0][1]
            self.rec_put(4, b)
        self.select(1)
        for k, dy in enumerate((0, 1, 3, 10, 1, 1)):
            self.set_year(h.emu.u32(YEAR) + dy)
            h.orig(AGE)
            self.L.shim_wp6b_age()
            self.check('%s: trackers year %d (+%d)' % (name, k + 1, dy))
        states = [self.rec(i)[0] for i in range(10)]
        for i in range(10):  # the port's years-left figure agrees with the original's 0x47ee8f
            if self.rec(i)[0]:
                r1 = h.orig(0x47ee8f, RECS + i * 0x26, track=False) & 0xffff
                r2 = self.L.shim_wp6b_years_left(i) & 0xffff
                if r1 != r2:
                    raise AssertionError('%s: years left of tracker %d: original %d, port %d' % (name, i, r1, r2))
        print('  trackers: 6 started, %d ageing passes identical, states now %s' % (6, states))

    # ---- the Macromanager ------------------------------------------------------------
    def macro(self, kinds, focus, lock, unlock, auto, manual, commit):
        e = self.h.emu
        e.w32(0x639040, kinds)
        e.w8(0x639048, focus)
        for a, v in ((0x65fe94, lock), (0x65fe98, unlock), (0x65fe9c, auto), (0x65fea0, manual)):
            e.w32(a, v)
        r1 = self.h.orig(MACRO_APPLY, commit)
        c1 = e.u32(0x65fe90)
        cost = ctypes.c_uint32()
        r2 = self.L.shim_wp6b_macro(kinds, focus, lock, unlock, auto, manual, commit, ctypes.byref(cost))
        if r1 != r2 or c1 != cost.value:
            raise AssertionError('macro %04x: original %d structures / %d Pennies, port %d / %d'
                                 % (kinds, r1, c1, r2, cost.value))
        return r1, c1

    def test_macro(self, name):
        settings = [  # kinds, focus, lock, unlock, auto, manual
            (0x0001, 0x80, 0, 0, 0, 1), (0x00ff, 0x20, 1, 0, 0, 1), (0xff00, 0xe0, 0, 1, 0, 1),
            (0x8181, 0x80, 0, 0, 1, 0), (0xffff, 1, 1, 0, 1, 0), (0x7f7f, 0xff, 0, 0, 0, 0),
            (0x0a50, 0x40, 0, 1, 0, 0),
        ]
        self.set_pennies(50000000)
        out = []
        for st in settings:
            n, c = self.macro(*st, 0)
            self.check('%s: macro %s count' % (name, st))
            self.macro(*st, 1)
            self.check('%s: macro %s implemented' % (name, st))
            out.append((n, c))
        for x in range(0, 0x140, 5):
            self.h.orig(MACRO_SLIDE, 0, x)
            f1 = self.h.emu.u8(0x639048)
            f2 = self.L.shim_wp6b_macro_focus(x)
            if f1 != f2:
                raise AssertionError('macro slider x %d: original %d, port %d' % (x, f1, f2))
        print('  macro: %d settings counted and implemented identically %s, slider OK' % (len(settings), out))

    # ---- the Microview -----------------------------------------------------------------
    def mv_target(self, plane, x, y):
        e = self.h.emu
        if not e.u32(0x5c6670):  # a window record for the drawing code to read (+0x10 surface: none)
            e.w32(0x5c6670, e.malloc(0x200))
        self.select(plane)
        c = self.L.shim_cell_get(x, y)
        e.w8(MV_X, x)
        e.w8(MV_Y, y)
        e.w8(MV_PLANE, plane)
        e.w8(MV_SET, c >> 27)
        e.w8(MV_INDEX, (c >> 16) & 0x7f)
        for a in (MV_COST, 0x638ef0, 0x638ee4):  # the upgrade's price, target, size
            e.w32(a, 0) if a == MV_COST else e.w8(a, 0)
        self.L.shim_wp6b_mv_target(plane, x, y)

    def mv(self, what, addr, args, act, x=0):
        self.h.orig(addr, *args)
        self.L.shim_wp6b_mv_action(act, x)
        self.check(what)

    def test_microview(self, name):
        h, e = self.h, self.h.emu
        n = 0
        for plane in (3, 1):
            pn = 'Heaven' if plane == 3 else 'Hell'
            for (x, y) in self.buildings(plane, (14, 15), 2, skip=2500):
                self.mv_target(plane, x, y)
                for sx in (0, 0x4a, 0x60, 0x7f, 0xad, 0xc0):
                    self.mv('%s %s fund (%d,%d) x %d' % (name, pn, x, y, sx), MV_FUND, (0, MAP_VIEW, sx), ACT_FUND,
                            sx)
                for on in (1, 0, 1):
                    self.mv('%s %s lock %d' % (name, pn, on), MV_LOCK, (on,), ACT_LOCK, on)
                n += 1
            for (x, y) in self.buildings(plane, (8,), 2):
                self.mv_target(plane, x, y)
                for sx in (0, 0x40, 0x80, 0xd2, 0xff):
                    self.mv('%s %s training (%d,%d) x %d' % (name, pn, x, y, sx), MV_TRAIN, (sx,), ACT_TRAINING, sx)
                for on in (0, 1):
                    self.mv('%s %s training active %d' % (name, pn, on), MV_ACTIVE, (on,), ACT_ACTIVE, on)
                n += 1
            for (x, y) in self.buildings(plane, (5,), 2):
                self.mv_target(plane, x, y)
                for sx in (0, 0x50, 0xd2):
                    self.mv('%s %s omnibolge (%d,%d) x %d' % (name, pn, x, y, sx), MV_OMNI, (sx,), ACT_OMNI, sx)
                n += 1
            for (x, y) in self.buildings(plane, (19,), 2):
                self.mv_target(plane, x, y)
                self.set_pennies(10000000)
                for ans, what in ((2, 'declined'), (1, 'taken'), (1, 'repaid')):
                    self.both_answer(ans)
                    self.mv('%s %s loan (%d,%d) %s' % (name, pn, x, y, what), MV_LOAN, (), ACT_LOAN)
                self.both_answer(1)
                self.mv('%s %s loan again' % (name, pn), MV_LOAN, (), ACT_LOAN)
                self.set_pennies(5)
                self.mv('%s %s loan repay, too poor' % (name, pn), MV_LOAN, (), ACT_LOAN)
                n += 1
            # upgrades: price (0x4696e6), then upgrade (destroy + rebuild)
            for sets in ((7,), (8,), (9,), (13,), (17,), (19,), (30,)):
                for (x, y) in self.buildings(plane, sets, 1):
                    self.mv_target(plane, x, y)
                    self.select(plane)
                    h.orig(MV_PRICE)
                    c1, c2 = e.u32(MV_COST), self.L.shim_wp6b_mv_cost()
                    if c1 != c2:
                        raise AssertionError('%s upgrade price of set %d at (%d,%d): original %d, port %d'
                                             % (name, sets[0], x, y, c1, c2))
                    self.check('%s %s upgrade price set %d' % (name, pn, sets[0]))
                    if c1 == 0:
                        continue  # can't be upgraded: the button is hidden
                    self.set_pennies(100)
                    self.both_answer(1)
                    self.mv('%s %s upgrade set %d, too poor' % (name, pn, sets[0]), MV_UPGRADE, (), ACT_UPGRADE)
                    self.set_pennies(90000000)
                    self.both_answer(2)
                    self.mv('%s %s upgrade set %d, declined' % (name, pn, sets[0]), MV_UPGRADE, (), ACT_UPGRADE)
                    self.both_answer(1)
                    self.mv('%s %s upgrade set %d at (%d,%d) price %d' % (name, pn, sets[0], x, y, c1), MV_UPGRADE,
                            (), ACT_UPGRADE)
                    n += 1
        # opening draws two random numbers
        h.orig(MV_OPEN, 0, MAP_VIEW, 10, 10)
        self.L.shim_wp6b_mv_open(10, 10)
        self.check('%s microview open' % name)
        print('  microview: actions on %d structures identical' % n)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('saves', nargs='*')
    a = ap.parse_args()
    saves = a.saves or [orig_path(s) for s in SAVES]
    t = Test(a.verbose)
    failed = 0
    t.test_names()
    for s in saves:
        print(s)
        name = os.path.basename(s)
        try:
            t.load(s)
            t.test_stories()
            t.test_trackers(name)
            t.load(s)
            t.test_macro(name)
            t.load(s)
            t.test_microview(name)
        except AssertionError as ex:
            print('  FAIL:', ex)
            failed += 1
    lines, total = t.h.uncompared_writes()
    if total:
        print('globals the original wrote that are not compared (%d ranges):' % total)
        for ln in lines:
            print('  ' + ln)
    print('%d checks; ' % t.checks + ('all OK' if not failed else '%d save(s) differ' % failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
