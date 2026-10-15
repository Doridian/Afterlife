/*
 * Program startup, the subsystem init/shutdown sequence, the task table and
 * the message pump (the "MAGIC" layer around 0x4c62e0-0x4c6bf0 plus the
 * small init functions it calls). See docs/startup.md.
 *
 * Original flow (WinMain = win_main 0x4c62e0):
 *   app_already_running   FindWindow on the exe name; else register the
 *                         class and a hidden 1x1 window (0x4cd892)
 *   app_chdir_to_exe      cwd = the exe's folder
 *   app_split_cmdline     strtok(cmdline, "\t ") into argv
 *   app_parse_args        .INI / .ALF / .ASC arguments, /? exits
 *   ini_set_path
 *   sys_initialize        13 subsystem inits in order, stop at the first error
 *   game_main(argc, argv) (0x401000) the game, returns when the player quits
 *   PostQuitMessage, sys_shutdown
 */
#ifndef AFTERLIFE_GAME_STARTUP_H
#define AFTERLIFE_GAME_STARTUP_H

#include <stdbool.h>

#include "platform/platform.h"

/* --- command line (0x4c6911) --------------------------------------------- */

extern char g_arg_ini_path[0x100];   /* arg_ini_path 0x64e190: a *.INI argument */
extern char g_arg_load_file[0x100];  /* arg_load_file 0x64dc64: a *.ALF or *.ASC argument */

/* 0x4c6911: argv[0] is skipped. Returns false for "/?" or "-?" (the original
 * exits). Port addition: a `dir_out` argument that is not one of the above
 * and does not start with '/' or '-' is returned as the data directory
 * (which stands in for the folder the original chdir()s to). */
bool app_parse_args(int argc, char **argv, const char **dir_out);

/* --- task table (0x5351c8, 10 entries) ----------------------------------- */

typedef int (*SysInitFn)(void);
typedef void (*SysTaskFn)(void);

/* 0x4c6755: `owner` is the registering init function and is the key for
 * sys_unregister_task. shutdown is unused by the dispatchers (each
 * subsystem's shutdown is called explicitly by sys_shutdown). suspend and
 * resume run when the window loses / regains focus, idle on every pump.
 * Returns 0, or -1 if the table is full. */
int sys_register_task(SysInitFn owner, SysTaskFn shutdown, SysTaskFn suspend,
                      SysTaskFn resume, SysTaskFn idle);
int sys_unregister_task(SysInitFn owner); /* 0x4c67f6 */

/* 0x4c63f0: handles one window message (PeekMessage, then main_wnd_proc),
 * then runs every task's idle callback. The game calls this from its main
 * loop and from every modal loop. Port additions: the emulated 16 ms timer
 * tick runs first (tm_poll), and the screen is presented at the end (the
 * original drew straight to the window). */
void sys_pump(void);
/* Port-only: called at the start of every sys_pump (tests use it to feed
 * synthetic input into the modal loops). NULL by default. */
extern void (*sys_pump_hook)(void);
/* Port: the PeekMessage / TranslateMessage / DispatchMessage part of
 * sys_pump (also used by kb_wait_key). False if there was no event. */
bool sys_dispatch_message(void);
void sys_suspend(void); /* 0x4c666a: WM_ACTIVATE inactive / minimize */
void sys_resume(void);  /* 0x4c66e3: WM_ACTIVATE active */

/* Port: the main window's close button was pressed (WM_CLOSE) while no
 * window manager root existed. Once wm_initialize has run, WM_CLOSE goes
 * to the root window as message 0x406 instead (forwarded to every
 * top-level window, as in the original), and this stays false. */
bool sys_quit_requested(void);

/* --- init sequence -------------------------------------------------------- */

/* sys_state 0x64dc60 */
enum { SYS_STATE_NONE = 0, SYS_STATE_RUNNING = 1, SYS_STATE_SHUTDOWN = 2 };
extern int g_sys_state;

int sys_initialize(void);  /* 0x4c6485: 0 on success, -1 on failure */
void sys_shutdown(void);   /* 0x4c65ef: safe to call more than once */

/* --- message boxes (0x4d5570, 0x4d5717) ----------------------------------- */

/* error_box: with `fatal`, appends the tech-support text, shuts everything
 * down and exits. */
void error_box(const char *title, bool fatal, const char *fmt, ...);
void warning_box(const char *title, const char *fmt, ...);

#endif
