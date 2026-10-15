#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the debug window (src/ui/debugwin.c) against
ALIFE.EXE under the emulator.

The original's button handler debugwin_proc (0x458200) gets the same
button messages as the port's (left click 0x14 and right release 0x15 on
the four arrows, unknown ids, the close box), from the same states: the
start state, every set, and long random runs. After each message the
browser state (0x65edd0 min, 0x65edd4 max, 0x65edd8 set, 0x65eddc index)
and build_editor_mode (0x630514) must be equal. The original's drawing is
stubbed and logged: each step must clear the preview box (gr_fill_rect
0x4d08e0 at 3, 0x14, 0x74 x 0x8b, colour 0) and draw tile_draw_preview
(0x4eb585) with the port's new set and index + 1 at zoom 0 into a view
clipped to that box, then wm_redraw (0x4c25c4) the window; the close box
must call wm_show(window, 0). The port runs without a window (no drawing),
so its drawing is checked against these logged calls through its state
and its constants (ui/debugwin.c), and on screen by tests/debugwin_test.

    tools/emu/test_debugwin.py [--runs N]
"""
import argparse
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from alife_emu import Emu  # noqa: E402
from port import Port  # noqa: E402

PROC = 0x458200
WIN_PTR = 0x639244
STATE = (0x65edd0, 0x65edd4, 0x65edd8, 0x65eddc)  # min, max, set, index
EDITOR = 0x630514
GR_FILL_RECT, TILE_DRAW_PREVIEW, WM_REDRAW = 0x4d08e0, 0x4eb585, 0x4c25c4
WM_SHOW, WM_DEFAULT_PROC = 0x4c23da, 0x4c3331
CTL_CLICK, CTL_RUP = 0x14, 0x15
IDS = (0x65, 0x66, 0x67, 0x68)
CLOSE = 0x69


def s8(v):
    v &= 0xff
    return v - 0x100 if v >= 0x80 else v


class Orig:
    def __init__(self):
        self.emu = e = Emu()
        self.win = e.malloc(0x100)
        self.surface = e.malloc(0x40)
        e.w32(self.win + 0x10, self.surface)
        e.w32(WIN_PTR, self.win)
        self.log = []

        def fill(emu, a):
            self.log.append(('fill', [a(i) for i in range(6)]))

        def preview(emu, a):
            v = a(1)
            view = dict(clip=(emu.i32(v + 0x18), emu.i32(v + 0x1c), emu.i32(v + 0x20), emu.i32(v + 0x24)),
                        zoom=emu.i32(v + 0x31), magnify=emu.i32(v + 0x35), dir_y=s8(emu.u8(v + 0x3a)))
            self.log.append(('preview', [a(0), a(4), a(5), s8(a(6)), a(7)], view))

        e.hook(GR_FILL_RECT, fill)
        e.hook(TILE_DRAW_PREVIEW, preview)
        e.stub(WM_REDRAW, log=self.log)
        e.stub(WM_SHOW, log=self.log)
        e.stub(WM_DEFAULT_PROC)

    def set_state(self, st):
        for a, v in zip(STATE, st):
            self.emu.w8(a, v)

    def state(self):
        return tuple(s8(self.emu.u8(a)) for a in STATE)

    def msg(self, m, id_):
        self.log.clear()
        self.emu.call(PROC, 0x1234, m, id_, 0)
        return list(self.log)


class PortSide:
    def __init__(self):
        import ctypes
        self.p = p = Port()
        c = ctypes
        self.get = p.fn('shim_debugwin_get', c.c_uint32, [])
        self.set = p.fn('shim_debugwin_set', None, [c.c_int] * 4)
        self.editor = p.fn('shim_debugwin_editor', c.c_int, [c.c_int])
        self.send = p.fn('shim_debugwin_msg', None, [c.c_int, c.c_int])

    def state(self):
        v = self.get()
        return tuple(s8(v >> (8 * i)) for i in range(4))


def check_draw(o, log, st, what):
    """the original's calls for one step against the port's state"""
    _, mx, set_, index = st
    kinds = [x[0] for x in log]
    exp = ['fill', 'preview', WM_REDRAW]
    if kinds != exp:
        raise AssertionError('%s: calls %r, expected %r' % (what, kinds, exp))
    fill = log[0][1]
    if fill != [o.surface, 3, 0x14, 0x74, 0x8b, 0]:
        raise AssertionError('%s: gr_fill_rect%r' % (what, tuple(fill)))
    args, view = log[1][1], log[1][2]
    if args != [o.win, set_, index + 1, 0, 0]:
        raise AssertionError('%s: tile_draw_preview(win, set %d, index %d, zoom %d, cell %#x), port set %d index %d'
                             % (what, args[1], args[2], args[3], args[4], set_, index + 1))
    if view != dict(clip=(3, 0x14, 0x74, 0x8b), zoom=0, magnify=0, dir_y=1):
        raise AssertionError('%s: preview view %r' % (what, view))
    if log[2][1][:2] != [o.win, 0]:
        raise AssertionError('%s: wm_redraw%r' % (what, tuple(log[2][1][:2])))


def run(o, q, seq, start, what):
    o.set_state(start)
    q.set(*start)
    n = 0
    for m, id_ in seq:
        o.emu.w8(EDITOR, 1)
        q.editor(1)
        log = o.msg(m, id_)
        q.send(m, id_)
        a, b = o.state(), q.state()
        tag = '%s step %d (msg %#x id %#x)' % (what, n, m, id_)
        if a != b:
            raise AssertionError('%s: original %r, port %r (min, max, set, index)' % (tag, a, b))
        ed_o, ed_p = o.emu.u8(EDITOR), q.editor(-1)
        if ed_o != ed_p:
            raise AssertionError('%s: editor original %d, port %d' % (tag, ed_o, ed_p))
        if id_ in IDS and m in (CTL_CLICK, CTL_RUP):
            check_draw(o, log, a, tag)
        elif id_ == CLOSE and m == CTL_CLICK:
            if [x[1][:2] for x in log] != [[o.win, 0]] or ed_o != 0:
                raise AssertionError('%s: close: calls %r, editor %d' % (tag, log, ed_o))
        elif log:
            raise AssertionError('%s: unexpected calls %r' % (tag, log))
        n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', type=int, default=200)
    args = ap.parse_args()
    o, q = Orig(), PortSide()
    total = 0
    msgs = [(m, i) for m in (CTL_CLICK, CTL_RUP) for i in IDS]
    # every message from the start state (the original's .data values)
    start = (0, 0x6f, 14, 1)
    if o.state() != start:
        raise AssertionError('original start state %r' % (o.state(),))
    for mi in msgs + [(CTL_CLICK, CLOSE), (CTL_RUP, CLOSE), (CTL_CLICK, 0x64), (CTL_CLICK, 0x6a), (0x0d, 0x65)]:
        total += run(o, q, [mi], start, 'start')
    # every set reached by +1 / -1 / +10 / -10, then the index pushed both ways
    for s in range(1, 31):
        for d in (CTL_CLICK, CTL_RUP):
            seq = [(d, 0x65), (CTL_RUP, 0x67), (CTL_RUP, 0x67), (CTL_CLICK, 0x68), (CTL_RUP, 0x68),
                   (CTL_RUP, 0x68), (CTL_CLICK, 0x67), (d, 0x66), (CTL_CLICK, 0x68)]
            total += run(o, q, seq, (0, 1, s, 0), 'set %d' % s)
    # random runs from random states (also out-of-range ones)
    rnd = random.Random(0x457d98)
    for r in range(args.runs):
        st = (rnd.randint(0, 0x11), rnd.randint(0, 0x6f), rnd.randint(1, 30), rnd.randint(-5, 0x70))
        seq = [(rnd.choice((CTL_CLICK, CTL_RUP)), rnd.choice(IDS)) for _ in range(rnd.randint(1, 60))]
        if rnd.random() < 0.1:
            seq.append((CTL_CLICK, CLOSE))
        total += run(o, q, seq, st, 'run %d' % r)
    print('debugwin: %d messages, state, editor flag and drawing calls match' % total)


if __name__ == '__main__':
    main()
