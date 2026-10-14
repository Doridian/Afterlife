/*
 * Keyboard ("Kb", 0x4d05e0-0x4d08d6) and the main window procedure
 * (main_wnd_proc 0x4cb7ad), which routes window messages to the keyboard,
 * the mouse (gfx/mouse.h), the display and the task table.
 *
 * The keyboard module is a 32-entry FIFO of {type, vk, ch} records. The
 * window procedure pushes WM_KEYDOWN / WM_SYSKEYDOWN (type 2), WM_KEYUP /
 * WM_SYSKEYUP (3) and WM_CHAR (1); the window manager's idle task
 * (0x4c178f) pops one per pump with kb_get_event and sends it to the focus
 * window as message 0x1e (char), 0x1f (key down) or 0x20 (key up), and
 * tracks VK_SHIFT. kb_set_handler stores a callback nobody calls.
 */
#ifndef AFTERLIFE_GAME_INPUT_H
#define AFTERLIFE_GAME_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"

#define KB_QUEUE_LEN 32

/* KbEvent.type */
enum {
    KB_EV_CHAR = 1, /* WM_CHAR: ch = the character, vk = 0 */
    KB_EV_DOWN = 2, /* WM_KEYDOWN: vk, ch = low byte of vk */
    KB_EV_UP = 3,   /* WM_KEYUP: vk, ch = low byte of vk */
};

/* The original packs these into 9 bytes (int, int, char). */
typedef struct {
    int32_t type;
    int32_t vk;
    uint8_t ch;
} KbEvent;

int kb_initialize(void); /* 0x4d05e0: empties the queue */
int kb_shutdown(void);   /* 0x4d05f7 */
int kb_suspend(void);    /* 0x4d0609 */
int kb_resume(void);     /* 0x4d061b */

/* 0x4d062d / 0x4d0653: the stored callback (wm_initialize installs one);
 * the keyboard module never calls it. */
typedef void (*KbHandler)(void);
KbHandler kb_set_handler(KbHandler fn);
void kb_restore_handler(KbHandler fn);

/* 0x4d0666: pops the oldest event, NULL if none. The record stays valid
 * until the next pop (a static copy, kb_event_out 0x548618). */
const KbEvent *kb_get_event(void);
int kb_pending(void); /* 0x4d067b: queued events */
/* 0x4d0690: dispatches window messages (no idle tasks) until a key event
 * is queued, pops it and returns its ch. Unused by the game. */
uint8_t kb_wait_key(void);

void kb_on_key_down(int vk); /* 0x4d06f1 */
void kb_on_key_up(int vk);   /* 0x4d070e */
void kb_on_char(uint8_t ch); /* 0x4d072b */

/* 0x4d07b0: drops the event when 32 are queued. */
void kb_queue_push(int type, int vk, uint8_t ch);
const KbEvent *kb_queue_pop(void); /* 0x4d0820 */
void kb_queue_reset(void);         /* 0x4d088e */
/* 0x4d08b7: injects an event (used to fake a keypress, 0x4acbee). */
void kb_post(int type, int vk, uint8_t ch);

/* 0x4cb7ad main_wnd_proc, for one platform event (one window message).
 * Returns false if it wasn't handled (DefWindowProc in the original). */
bool main_wnd_proc(const PlatEvent *ev);

/* Port: the main window was asked to close while there was no window
 * manager root (before wm_initialize, or in tests without it). Otherwise
 * WM_CLOSE is sent to the root window as message 0x406 (WM_MSG_CLOSE),
 * which forwards it to the top-level windows (the game's quit dialog). */
bool app_close_requested(void);

#endif
