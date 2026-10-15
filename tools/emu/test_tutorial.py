#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Differential test of the tutorial (WP8, src/game/tutorial*.c) against
ALIFE.EXE under the emulator.

1. The lesson scripts. The original tutorial_script (0x41f611) runs for
   each lesson 1..9 with every function it calls hooked (logging the call
   and its arguments, doing nothing; the save/load succeed, the view is
   already zoomed out and unrotated, the zone block buttons pressed), the
   windows it reads being fake ones at distinctive positions. The logged
   call sequence of the lesson part is turned into the port's op terms
   (window pointers and positions back into window ids and offsets, the
   game speed noted at each akos_talk_wait) and compared with what the
   port's interpreter runs (tutorial_run with tutorial_hooks.dry, logging
   each op through tutorial_hooks.step). Once with the advisors talking
   (all guards taken) and once silenced (advisor_silenced = 1: the guarded
   speeches are skipped on both sides). Also checked: the prologue's calls
   in that state, and that only lesson 7 marks the Microview as its own.

2. The pointer glide. tut_glide_to (0x4ac880) and tut_glide_task (0x4ac72b)
   against tutorial_glide_start / tutorial_glide_step for random start
   points and targets: the same positions step by step, the same arrival.

    tools/emu/test_tutorial.py [--glides N]
"""
import argparse
import ctypes
import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from alife_emu import Emu  # noqa: E402
from port import Port  # noqa: E402

SYMS = {}
with open(os.path.join(HERE, '..', '..', 'symbols', 'ALIFE.EXE.txt')) as f:
    for line in f:
        m = re.match(r'(0x[0-9a-fA-F]+)\s+(func|data)\s+(\w+)', line)
        if m:
            SYMS[m.group(3)] = int(m.group(1), 16)
A = SYMS.__getitem__

# what tutorial_script calls (all hooked)
CALLEES = ['snd_talk_start', 'akos_talk_wait', 'sys_pump', 'game_frame_update', 'mainview_center_on_cell',
           'wm_move', 'mapview_zoom', 'map_anim_cell_add', 'gr_fill_rect', 'save_write_game', 'save_load_game',
           'statusbar_draw', 'menu_set_checked', 'ctl_button_set_pressed', 'wm_set_title', 'wnd_draw_title_std',
           'wm_show', 'ctl_is_pressed', 'attract_stop', 'ctl_message_box', 'sprintf', 'tutorial_restore_windows',
           'tut_right_down', 'tut_right_up', 'tut_click', 'tut_left_down', 'tut_left_up', 'tut_glide_wait',
           'tut_wait_seconds', 'tut_wait_years', 'tut_glide_to', 'tut_glide_control', 'tut_glide_menu',
           'tut_glide_cell']
RETURNS = {'save_write_game': 1, 'save_load_game': 1, 'ctl_is_pressed': 1}
SIMPLE = {'sys_pump': 'PUMP', 'game_frame_update': 'FRAME', 'tut_glide_wait': 'SYNC', 'tut_click': 'CLICK',
          'tut_left_down': 'DOWN', 'tut_left_up': 'UP', 'tut_right_down': 'RDOWN', 'tut_right_up': 'RUP'}

# the port's ops (game/tutorial.h)
OPS = ['END', 'IF', 'ENDIF', 'SAY', 'WAIT_TALK', 'PUMP', 'FRAME', 'GLIDE_CTL', 'GLIDE_MENU', 'GLIDE_CELL',
       'GLIDE_WIN', 'SYNC', 'CLICK', 'DOWN', 'UP', 'RDOWN', 'RUP', 'SECONDS', 'YEARS', 'CENTER', 'MOVE_WIN',
       'SPEED_PUSH', 'SPEED_POP', 'ANIM_CELL', 'MAPVIEW_OUT', 'CLEAR_TEXT', 'MICROVIEW']
TW = {'ctrlpanel_window': 0, 'graphview_window': 1, 'mapview_window': 2, 'microview_window': 3,
      'advisor_window': 4, 'main_window': 5}
# fake window positions: rect left (+0x14) / top (+0x1c), far apart
FAKE_POS = {name: (0x10000 * (i + 1), 0x10000 * (i + 1) + 0x8000) for i, name in enumerate(TW)}
CLIENT = (0x1000, 0x2000, 0x3000, 0x4000)  # main window client left, right, top, bottom
GAME_SPEED = A('game_speed')


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def nearest(value, choices):
    """(index, offset) of the choice value is closest to."""
    best = min(range(len(choices)), key=lambda i: abs(s32(value - choices[i])))
    return best, s32(value - choices[best])


def orig_trace(emu, lesson, silenced):
    emu.restore(emu.base_snap)
    log = []
    win_addr = {}
    for name in TW:
        w = emu.malloc(0x100)
        left, top = FAKE_POS[name]
        emu.w32(w + 0x14, left)
        emu.w32(w + 0x1c, top)
        emu.w32(w + 0x10, emu.malloc(0x10))  # a surface
        emu.w32(A(name), w)
        win_addr[w] = name
    m = emu.u32(A('main_window'))
    for i, v in enumerate(CLIENT):
        emu.w32(m + 0x24 + 4 * i, v)
    emu.w32(0x5bb151, 3)    # map_view zoom: all the way out
    emu.w32(0x5bb155, 0)    # not magnified
    emu.w8(0x5bb159, 1)     # unrotated
    emu.w8(0x5bb15a, 1)
    emu.w32(A('tut_active'), 1)
    emu.w32(A('advisor_silenced'), 1 if silenced else 0)
    emu.w32(A('advisor_talk_stamp'), 1234)
    emu.w32(A('tut_microview_owned'), 0)
    emu.w8(GAME_SPEED, 1)

    def hook(name):
        def f(e, args):
            a = [s32(args(i)) for i in range(9)]
            if name == 'akos_talk_wait':
                a.append(e.u8(GAME_SPEED))
            log.append((name, a))
            return RETURNS.get(name, 0)
        return f
    for name in CALLEES:
        emu.hook(A(name), hook(name))
    emu.call(A('tutorial_script'), lesson, insn_limit=50_000_000)

    # the prologue (everything set up already), then the lesson, then the epilogue
    names = [n for n, _ in log]
    expect = ['save_write_game', 'save_load_game', 'statusbar_draw', 'menu_set_checked', 'menu_set_checked',
              'ctl_button_set_pressed', 'wm_set_title', 'wnd_draw_title_std', 'wm_show', 'wm_show',
              'ctl_is_pressed', 'ctl_is_pressed', 'ctl_is_pressed', 'tut_glide_cell', 'sys_pump',
              'game_frame_update', 'sys_pump', 'game_frame_update']
    assert names[:len(expect)] == expect, names[:len(expect)]
    assert log[1][1][1] == lesson + 9999, log[1]   # the lesson's save: resource 9999 + n
    assert log[13][1][:3] == [2, 0x31, 0x32], log[13]
    end = names.index('attract_stop')
    assert names[end:] == ['attract_stop', 'tutorial_restore_windows', 'sprintf', 'ctl_message_box'], names[end:]
    ops = []
    for name, a in log[len(expect):end]:
        if name in SIMPLE:
            ops.append((SIMPLE[name],))
        elif name == 'snd_talk_start':
            assert a[4] == 0x13
            ops.append(('SAY', a[0], emu.cstr(a[2]), a[3]))
        elif name == 'akos_talk_wait':
            ops.append(('WAIT_TALK', a[0], a[9]))
        elif name == 'tut_glide_control':
            ops.append(('GLIDE_CTL', TW[win_addr[a[0] & 0xffffffff]], a[1]))
        elif name == 'tut_glide_menu':
            ops.append(('GLIDE_MENU', a[0]))
        elif name == 'tut_glide_cell':
            ops.append(('GLIDE_CELL', a[0], a[1], a[2]))
        elif name == 'tut_glide_to':
            names_ = list(TW)
            wx, dx = nearest(a[0], [FAKE_POS[n][0] for n in names_])
            wy, dy = nearest(a[1], [FAKE_POS[n][1] for n in names_])
            ops.append(('GLIDE_WIN', TW[names_[wx]], dx, TW[names_[wy]], dy))
        elif name == 'tut_wait_seconds':
            ops.append(('SECONDS', a[0]))
        elif name == 'tut_wait_years':
            ops.append(('YEARS', a[0]))
        elif name == 'mainview_center_on_cell':
            ops.append(('CENTER', a[0], a[1], a[2]))
        elif name == 'wm_move':
            ax, dx = nearest(a[1], CLIENT[0:2])
            ay, dy = nearest(a[2], CLIENT[2:4])
            ops.append(('MOVE_WIN', TW[win_addr[a[0] & 0xffffffff]], ax, dx, ay, dy))
        elif name == 'map_anim_cell_add':
            assert a[1:9] == [0x52, 0x60, 0, 3, 2, 2, 3, 5], a
            ops.append(('ANIM_CELL',))
        elif name == 'mapview_zoom':
            assert a[:3] == [0, 0, 0]
            ops.append(('MAPVIEW_OUT',))
        elif name == 'gr_fill_rect':
            assert a[1:6] == [0xe, 0x91, 0x120, 0x46, 0x13], a
            ops.append(('CLEAR_TEXT',))
        else:
            raise AssertionError('unexpected call in lesson %d: %s %s' % (lesson, name, a))
    return ops, emu.u32(A('tut_microview_owned')) != 0


class TutOp(ctypes.Structure):
    _fields_ = [('op', ctypes.c_uint8), ('a', ctypes.c_int32), ('b', ctypes.c_int32), ('c', ctypes.c_int32),
                ('d', ctypes.c_int32), ('e', ctypes.c_int32), ('text', ctypes.c_char_p)]


STEP = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int, ctypes.POINTER(TutOp))


class TutorialHooks(ctypes.Structure):
    _fields_ = [('clock', ctypes.c_void_p), ('step', STEP), ('lesson_done', ctypes.c_void_p),
                ('dry', ctypes.c_bool), ('dry_silent', ctypes.c_bool)]


def port_trace(lib, lesson, silenced):
    seen = []

    def step(n, i, op):
        o = op.contents
        seen.append((o.op, o.a, o.text.decode('latin1') if o.text else o.b, o.c, o.d, o.e))
    cb = STEP(step)
    hooks = TutorialHooks.in_dll(lib, 'tutorial_hooks')
    hooks.step = cb
    hooks.dry = True
    hooks.dry_silent = silenced
    lib.tutorial_run(lesson)
    hooks.step = STEP()
    hooks.dry = hooks.dry_silent = False
    ops = []
    speed, saved = 1, 1
    owned = False
    for op, a, b, c, d, e in seen:
        name = OPS[op]
        if name in ('IF', 'ENDIF'):
            continue
        if name == 'SPEED_PUSH':
            saved, speed = speed, a
        elif name == 'SPEED_POP':
            speed = saved
        elif name == 'MICROVIEW':
            owned = True
        elif name == 'SAY':
            ops.append(('SAY', a, b, c))
        elif name == 'WAIT_TALK':
            ops.append(('WAIT_TALK', a, speed))
        elif name in ('GLIDE_CTL',):
            ops.append((name, a, b))
        elif name in ('GLIDE_MENU', 'SECONDS', 'YEARS'):
            ops.append((name, a))
        elif name in ('GLIDE_CELL', 'CENTER'):
            ops.append((name, a, b, c))
        elif name == 'GLIDE_WIN':
            ops.append((name, a, b, c, d))
        elif name == 'MOVE_WIN':
            ops.append((name, a, b, c, d, e))
        else:
            ops.append((name,))
    return ops, owned


def check_scripts(emu, lib):
    bad = 0
    for lesson in range(1, 10):
        for silenced in (False, True):
            o, o_owned = orig_trace(emu, lesson, silenced)
            p, p_owned = port_trace(lib, lesson, silenced)
            ok = o == p and o_owned == p_owned
            if not ok:
                bad += 1
                k = next((i for i in range(min(len(o), len(p))) if o[i] != p[i]), min(len(o), len(p)))
                print('lesson %d%s: DIFFERENT at %d (orig %d ops, port %d): orig %s port %s' % (
                    lesson, ' silenced' if silenced else '', k, len(o), len(p), o[k:k + 3], p[k:k + 3]))
            else:
                print('lesson %d%s: %d calls identical%s' % (lesson, ' (silenced)' if silenced else '', len(o),
                                                            ', Microview owned' if o_owned else ''))
    return bad


class TutGlide(ctypes.Structure):
    _fields_ = [('active', ctypes.c_bool)] + [(n, ctypes.c_int) for n in (
        'target_x', 'target_y', 'step_x', 'step_y', 'x', 'y', 'dx', 'dy', 'end_x', 'end_y')]


def check_glides(emu, lib, count, seed=1):
    rnd = random.Random(seed)
    emu.restore(emu.base_snap)
    for name in CALLEES:  # (left by check_scripts)
        emu.unhook(A(name))
    mouse = [0, 0]
    clock = [1000]
    moves = []

    def get_state(e, args):
        e.w32(args(0), mouse[0])
        e.w32(args(1), mouse[1])
        return 0

    def set_pos(e, args):
        mouse[0], mouse[1] = s32(args(0)), s32(args(1))
        return 0

    def mouse_event(e, args):
        moves.append((s32(args(0)), s32(args(1)), s32(args(2))))
        return 0

    def ticks(e, args):
        clock[0] += 11
        return clock[0]
    emu.hook(A('ms_get_state'), get_state)
    emu.hook(A('ms_set_pos'), set_pos)
    emu.hook(A('wm_mouse_event'), mouse_event)
    emu.import_stub('timeGetTime', 0, ticks)
    emu.w32(A('tut_active'), 1)
    lib.tutorial_glide_start.argtypes = [ctypes.POINTER(TutGlide)] + [ctypes.c_int] * 5
    lib.tutorial_glide_step.argtypes = [ctypes.POINTER(TutGlide)]
    lib.tutorial_glide_step.restype = ctypes.c_bool
    bad = 0
    for k in range(count):
        mouse[:] = [rnd.randrange(-50, 1100), rnd.randrange(-50, 800)]
        if k % 5 == 0:
            tx, ty = mouse[0] + rnd.randrange(-3, 4), mouse[1] + rnd.randrange(-3, 4)
        else:
            tx, ty = rnd.randrange(0, 1024), rnd.randrange(0, 768)
        g = TutGlide()
        lib.tutorial_glide_start(ctypes.byref(g), mouse[0], mouse[1], tx, ty, 10)
        moves.clear()
        emu.call(A('tut_glide_to'), tx, ty)
        port_moves = []
        while g.active:
            lib.tutorial_glide_step(ctypes.byref(g))
            port_moves.append((1, g.x, g.y))
        n = 0
        while emu.u32(A('tut_glide_active')) and n < 1000:
            emu.call(A('tut_glide_task'))
            n += 1
        end = (s32(emu.u32(0x518f74)), s32(emu.u32(0x518f78)))
        if moves != port_moves or end != (g.end_x, g.end_y):
            bad += 1
            if bad < 5:
                print('glide %d: (%d,%d)->(%d,%d) differs: orig %s.. port %s..' % (
                    k, mouse[0], mouse[1], tx, ty, moves[:4], port_moves[:4]))
    print('glides: %d of %d identical' % (count - bad, count))
    return bad


def check_text_filter(emu, lib, count, seed=2):
    """tutorial_window_show_page's text filter (0x4abeaf) on random pages."""
    rnd = random.Random(seed)
    emu.restore(emu.base_snap)
    page = [b'']
    texts = []
    emu.hook(A('rc_open_resource'), lambda e, a: e._fopen('tutpage.txt', 'rb'))
    emu.hook(A('rc_resource_size'), lambda e, a: len(page[0]))
    emu.hook(0x4cdf0a, lambda e, a: e._fclose(a(0)))   # the file close
    for name in ('wm_redraw', 'wm_show', 'wm_set_minimized'):
        emu.hook(A(name), lambda e, a: 0)

    def send(e, a):
        if a(2) == 0x8000:
            texts.append(e.cstr(a(3), 1 << 16).encode('latin1'))
        return 0
    emu.hook(A('wm_send'), send)
    lib.tutorialwin_filter.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    lib.tutorialwin_filter.restype = ctypes.c_size_t
    alphabet = b'ab  .\r\n\t\\\\\\n0x'
    bad = 0
    for k in range(count):
        n = rnd.randrange(1, 200)
        data = bytes([ord('A')]) + bytes(rnd.choice(alphabet) for _ in range(n))
        while data.endswith(b'\\'):  # (the original reads past the end there)
            data = data[:-1]
        page[0] = data
        emu.add_file('tutpage.txt', data)
        texts.clear()
        emu.call(A('tutorial_window_show_page'), 0x579 + k % 10)
        out = ctypes.create_string_buffer(len(data) + 1)
        lib.tutorialwin_filter(data, len(data), out)
        if texts != [out.value]:
            bad += 1
            if bad < 5:
                print('text %d: %r -> orig %r port %r' % (k, data, texts, out.value))
    print('text pages: %d of %d identical' % (count - bad, count))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--glides', type=int, default=300)
    args = ap.parse_args()
    emu = Emu()
    emu.base_snap = emu.snapshot()
    lib = Port().lib
    bad = check_scripts(emu, lib)
    bad += check_glides(emu, lib, args.glides)
    bad += check_text_filter(emu, lib, args.glides)
    print('FAILED' if bad else 'ok')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
