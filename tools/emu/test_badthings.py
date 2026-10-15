#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential tests of the Bad Things (WP4: src/game/badthings.c,
badthing_objects.c) against ALIFE.EXE under the emulator.

Setup as in test_sim.py (both sides load the save, the original's tables
come from its own game_init_defaults, the map object code runs on both
sides with mapobj_env), plus the same special-structure sites on both
sides. Then, per save:

  kinds     each Bad Thing forced with bad_thing_start 0x43dc3c (k = 0..7),
            started twice (the second must be refused), then stepped:
            the map object block of game_frame_update on planes 1-3 and,
            every 4 frames, sim_update(speed 0) with the clock 500 ms on
            (its Bad Thing timers: freeze spread/recover 0x442122/0x442287,
            the flocks' end), until it is over or --frames ran out
  rotated   Birds, Handbasket, Nose, Dice and Disco again in a rotated view
            (view_dir -1/-1 and 1/-1)
  random    economy_random_bad_things 0x43e4e5 with all kinds enabled
  bankrupt  the bankruptcy 0x444d23, then the walker 0x4442dd and the
            fires 0x443c9e every step
  cheat     SAMNMAX 0x444ea5 (set 6) and the build cheat's giant (set 3)
  off       with g_state.bad_things 0 nothing starts

After each step every saved global (the map objects, the loot tables, the
Bad Things bits), the grids, the current-plane globals and the RNG must be
identical, and so must the side effects: the original's snd_play,
snd_stop, snd_unload, snd_music_play, snd_fade_out_music, ctl_message_box
(text) and news_popup_show calls against the port's hooks, in order.

    tools/emu/test_badthings.py [--only NAME[,NAME]] [--frames N] [--keep-going] [save ...]
"""
import argparse
import ctypes
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from difftest import SAVES, orig_path  # noqa: E402
import bridge  # noqa: E402
from test_sim import SimPair, Mismatch, SIM_TIMERS  # noqa: E402
from alife_emu import ORIG, Emu  # noqa: E402

MAP_VIEW = bridge.MAP_VIEW
SELECT_PLANE = 0x42b102
BAD_THING_START, RANDOM_BAD, BANKRUPT, DOOM, FIRE, CHEAT = 0x43dc3c, 0x43e4e5, 0x444d23, 0x4442dd, 0x443c9e, 0x444ea5
SPAWN = 0x4ee402
SIM_UPDATE = 0x44876b
NAMES = ['birds', 'bats', 'blues', 'freeze', 'nose', 'handbasket', 'dice', 'disco']
SETS = {0: 0x14, 1: 0x15, 4: 0x19, 5: 0x18, 6: 0x1a, 7: 0x1b}

# side effects, logged on the original's side: addr -> (kind, args used)
LOGGED = {0x4751af: 1, 0x4750f0: 2, 0x4754a1: 3, 0x4754d7: 4, 0x4753f2: 5, 0x4bfc69: 6, 0x476582: 7}
OTHER_STUBS = {0x4cf504: 'font_select', 0x4d0102: 'font_set_colors', 0x4c2733: 'wm_update',
               0x4c2830: 'wm_send', 0x4c59a2: 'wm_is_visible', 0x475428: 'snd_preload'}
OBJ = 0x64e5f0


class Pair(SimPair):
    def __init__(self):
        super().__init__()
        L, c = self.L, ctypes
        L.shim_bt_setup.argtypes = [c.c_int, c.c_char_p]
        L.shim_bt_event.argtypes = [c.c_int, c.POINTER(c.c_uint32 * 2), c.c_char_p, c.c_int]
        L.shim_bt_objects.argtypes = [c.c_char_p]
        L.shim_bt_set.argtypes = [c.c_uint32, c.c_uint32]
        L.shim_bt_realm_set.argtypes = [c.c_int, c.c_int, c.c_int]
        L.shim_bt_special.argtypes = [c.c_int, c.c_int, c.c_int]
        L.bad_thing_start.argtypes = [c.c_int]
        L.badthings_spread.restype = c.c_bool
        L.badthings_recover.restype = c.c_bool
        L.mapobj_frame.argtypes = [c.c_int]
        L.sim_update.argtypes = [c.c_int]
        L.world_select_plane.argtypes = [c.c_int]
        L.mapobj_spawn.argtypes = [c.c_int] * 10 + [c.c_uint32, c.c_int, c.c_uint32]
        self.log = []

    def stub_ui(self):
        e = self.h.emu
        for a in OTHER_STUBS:
            e.stub(a)
        for a in LOGGED:
            e.stub(a, ret=1 if a == 0x4bfc69 else 0, log=self.log)

    def prepare(self, save, seed=12345):
        super().prepare(save, seed)
        e, L = self.h.emu, self.L
        self.stub_ui()
        for a in SIM_TIMERS:
            e.w32(a, 0)
        e.w8(0x5fa61c, 0)
        self.time = 1000000
        L.shim_sim_clock(self.time, 0)
        L.shim_bt_setup(1, ORIG.encode())
        # the special structures' sites (runtime, from the post-load rebuild)
        sp = e.read(0x5bb0f0, 20)
        for i in range(10):
            L.shim_bt_special(i, sp[2 * i], sp[2 * i + 1])
        self.set(0x593248, 1)
        del self.log[:]
        self.check('prepared')

    def set(self, addr, v, size=4):
        e = self.h.emu
        if size == 4:
            e.w32(addr, v & 0xffffffff)
        else:
            e.w8(addr, v & 0xff)
        self.L.shim_bt_set(addr, v & 0xffffffff)

    def view(self, vx, vy):
        e = self.h.emu
        self.set(0x5fa5f8, vx, 1)
        self.set(0x5bb118, vy, 1)
        e.w8(MAP_VIEW + 0x39, vx & 0xff)
        e.w8(MAP_VIEW + 0x3a, vy & 0xff)

    def realm_set(self, plane, off, v):
        base = 0x630520 if plane == 1 else 0x5b2f70
        self.h.emu.w8(base + off, v)
        self.L.shim_bt_realm_set(plane, off, v)

    def events(self):
        """(orig, port) side effect lists since the last call."""
        e, L = self.h.emu, self.L
        o = []
        for addr, args in self.log:
            k = LOGGED[addr]
            if k == 1:
                o.append((1, args[0], args[1]))
            elif k == 6:
                o.append((6, e.cstr(args[3]) if args[3] else ''))
            elif k == 5:
                o.append((5, args[0] & 0xffffffff))
            else:
                o.append((k, args[0]))
        del self.log[:]
        p = []
        out = (ctypes.c_uint32 * 2)()
        buf = ctypes.create_string_buffer(96)
        for i in range(L.shim_bt_events()):
            k = L.shim_bt_event(i, ctypes.byref(out), buf, 96)
            if k == 1:
                p.append((1, out[0], out[1]))
            elif k == 6:
                p.append((6, buf.value.decode('latin-1')))
            elif k == 8:
                continue  # helpers_off: the original's wm_is_visible is stubbed (0)
            else:
                p.append((k, out[0]))
        L.shim_bt_clear()
        o = [(6, t[1][:95]) if t[0] == 6 else t for t in o]
        return o, p

    def check(self, what, runtime=(), ignore=()):
        # the original's loader selects planes twice as often (difftest);
        # the counter's upper bytes too
        super().check(what, runtime, list(ignore) + ['plane_select_count+0x%x' % i for i in (1, 2, 3)])
        o, p = self.events()
        if o != p:
            n = next((i for i in range(min(len(o), len(p))) if o[i] != p[i]), min(len(o), len(p)))
            raise Mismatch('%s: side effects differ at #%d\n  orig %s\n  port %s'
                           % (what, n, o[n:n + 6], p[n:n + 6]))
        self.seen.extend(o)

    seen = []

    def quick(self, what):
        """The cheap part of check(): map objects, Bad Things bits, RNG,
        side effects (a full check costs 2 s)."""
        e = self.h.emu
        buf = ctypes.create_string_buffer(63 * 0x17)
        bits = self.L.shim_bt_objects(buf)
        o = e.read(OBJ, 63 * 0x17)
        if o != buf.raw or bits != e.u8(0x5b2f61) or e.u32(Emu.RAND_STATE) != self.h.port_.rng_state():
            self.check(what)  # names the differences
            raise Mismatch('%s: objects/bits/RNG differ (orig bits %#x port %#x)' % (what, e.u8(0x5b2f61), bits))
        oe, pe = self.events()
        if oe != pe:
            n = next((i for i in range(min(len(oe), len(pe))) if oe[i] != pe[i]), min(len(oe), len(pe)))
            raise Mismatch('%s: side effects differ at #%d\n  orig %s\n  port %s'
                           % (what, n, oe[n:n + 6], pe[n:n + 6]))
        self.seen.extend(oe)

    def select(self, plane):
        self.h.orig(SELECT_PLANE, MAP_VIEW, plane, track=False)
        self.L.world_select_plane(plane)

    def frame(self):
        h, w = self.h, self.h.emu.u32(0x5f7460)
        plane = h.emu.u8(MAP_VIEW + 0x47)
        for p in (1, 2, 3):
            h.orig(SELECT_PLANE, MAP_VIEW, p, track=False)
            h.orig(0x4ee088, w, MAP_VIEW, p, 1)
            h.orig(0x4eedc1, w, MAP_VIEW, p, 1)
        h.orig(SELECT_PLANE, MAP_VIEW, plane, track=False)
        self.L.mapobj_frame(1)

    def timers(self):
        """sim_update(speed 0): only the real-time timers run."""
        e = self.h.emu
        self.time += 500
        self.L.shim_sim_clock(self.time, 0)
        self.h.orig(SIM_UPDATE, e.u32(0x5f7460), e.u32(0x5c6674), e.u32(0x63865c), MAP_VIEW, 0)
        self.L.sim_update(0)

    def objects(self, sets):
        e = self.h.emu
        return sum(1 for i in range(1, 63) if e.u8(OBJ + i * 0x17) and e.u8(OBJ + i * 0x17 + 6) in sets)


FRAMES = [3000]


def run_out(s, k, what, frames=None):
    """Steps Bad Thing k until it is over; returns the frames it took."""
    frames = frames or FRAMES[0]
    e = s.h.emu
    bit = 1 << k
    sets = {SETS[k]} if k in SETS else set()
    for t in range(frames):
        s.frame()
        if t % 4 == 3:
            s.timers()
            for plane in (1, 3):
                base = 0x630520 if plane == 1 else 0x5b2f70
                # let the ice recede at once instead of after 25-75 years
                if e.u8(base + 0x80fa) == 0 and e.u8(base + 0x80f9) and e.u8(base + 0x80fb):
                    s.realm_set(plane, 0x80fb, 0)
        if t % 128 == 127:
            s.check('%s frame %d' % (what, t))
        else:
            s.quick('%s frame %d' % (what, t))
        if not (e.u8(0x5b2f61) & bit) and (not sets or s.objects(sets) == 0):
            s.check('%s over after %d frames' % (what, t))
            return t
    s.check('%s after %d frames' % (what, frames))
    return frames


def start(s, k, what):
    s.h.orig(BAD_THING_START, k)
    s.L.bad_thing_start(k)
    s.check('%s: bad_thing_start(%d)' % (what, k))


def t_kinds(s, save):
    out = []
    for k in range(8):
        s.prepare(save, seed=1000 + k)
        start(s, k, NAMES[k])
        on = bool(s.h.emu.u8(0x5b2f61) & (1 << k))
        start(s, k, NAMES[k] + ' again')
        n = run_out(s, k, NAMES[k]) if on else 0
        out.append('%s %s' % (NAMES[k], n if on else 'refused'))
    print('    (%s)' % ', '.join(out), end='')


def t_rotated(s, save):
    for vx, vy in ((-1, -1), (1, -1)):
        for k in (0, 5, 4, 6, 7):
            s.prepare(save, seed=77 + k)
            s.view(vx, vy)
            start(s, k, '%s view %d/%d' % (NAMES[k], vx, vy))
            run_out(s, k, '%s view %d/%d' % (NAMES[k], vx, vy), frames=min(FRAMES[0], 500))


def t_random(s, save):
    s.set(0x5f746c, 0xff)
    for i in range(6):
        s.set(0x5b2f5c, 0x7fffffff)
        s.h.orig(RANDOM_BAD)
        s.L.economy_random_bad_things()
        s.check('economy_random_bad_things #%d' % i)
        for t in range(60):
            s.frame()
            if t % 4 == 3:
                s.timers()
        s.check('random #%d stepped' % i)


def t_bankrupt(s, save):
    s.h.orig(BANKRUPT)
    s.L.badthings_bankruptcy()
    s.check('bankruptcy')
    for i in range(80):
        s.h.orig(DOOM)
        s.L.badthings_doom_step()
        s.h.orig(FIRE)
        s.L.badthings_fire_step()
        if i % 16 == 15:
            s.check('doom/fire step %d' % i)
        else:
            s.quick('doom/fire step %d' % i)
    s.check('bankruptcy done')


def t_cheat(s, save):
    s.h.orig(CHEAT)
    s.L.badthings_cheat()
    s.check('SAMNMAX')
    for t in range(400):
        s.frame()
        if t % 128 == 127:
            s.check('SAMNMAX frame %d' % t)
        else:
            s.quick('SAMNMAX frame %d' % t)
    s.prepare(save, seed=99)
    args = (40, 3, 3, 2, 0, 1, 4, 4, 1, 10, 0xffffffff, 0x5a, 1)
    o = s.h.orig(SPAWN, *args) & 0xff
    p = s.L.mapobj_spawn(*args) & 0xff
    if o != p:
        raise Mismatch('spawn: orig %d port %d' % (o, p))
    for t in range(600):
        s.frame()
        if t % 128 == 127:
            s.check('giant frame %d' % t)
        else:
            s.quick('giant frame %d' % t)


def t_off(s, save):
    s.set(0x593248, 0)
    for k in range(9):
        start(s, k, 'disabled %d' % k)
    if s.h.emu.u8(0x5b2f61):
        raise Mismatch('a Bad Thing started while disabled')


TESTS = [('kinds', t_kinds), ('rotated', t_rotated), ('random', t_random), ('bankrupt', t_bankrupt),
         ('cheat', t_cheat), ('off', t_off)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('saves', nargs='*')
    ap.add_argument('--only', default='')
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--keep-going', action='store_true')
    args = ap.parse_args()
    FRAMES[0] = args.frames
    saves = args.saves or [orig_path(n) for n in SAVES]
    only = set(filter(None, args.only.split(',')))
    s = Pair()
    fails = 0
    for save in saves:
        print(os.path.basename(save))
        for name, fn in TESTS:
            if only and name not in only:
                continue
            try:
                s.prepare(save)
                fn(s, save)
                print('  %-9s ok' % name)
            except (Mismatch, AssertionError) as e:
                fails += 1
                print('  %-9s MISMATCH\n    %s' % (name, str(e).replace('\n', '\n    ')))
                if not args.keep_going:
                    break
            except Exception:
                fails += 1
                print('  %-9s ERROR' % name)
                traceback.print_exc()
                if not args.keep_going:
                    break
        sys.stdout.flush()
    kinds = {}
    for ev in s.seen:
        kinds[ev[0]] = kinds.get(ev[0], 0) + 1
    print('side effects compared: %s' % ', '.join('%s %d' % (n, kinds.get(k, 0)) for k, n in
                                                  ((1, 'sounds'), (2, 'stops'), (3, 'unloads'), (4, 'music'),
                                                   (5, 'fades'), (6, 'messages'), (7, 'news'))))
    print('%d failures' % fails if fails else 'all ok')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
