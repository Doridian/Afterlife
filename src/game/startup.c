/*
 * Startup, init order, task table and message pump. See startup.h and
 * docs/startup.md. Subsystems that aren't ported yet are stubs here, each
 * labelled with its original address.
 */
#include "game/startup.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/config.h"
#include "game/ini.h"
#include "game/input.h"
#include "game/resource.h"
#include "game/sound.h"
#include "game/timer.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "ui/controls.h"
#include "ui/wm.h"

char g_arg_ini_path[0x100];
char g_arg_load_file[0x100];
int g_sys_state;

/* ======================================================================== */
/* Command line                                                              */
/* ======================================================================== */

static bool ext_is(const char *ext, const char *want)
{
    /* the original _strupr()s the extension and strcmp()s it */
    for (; *ext && *want; ext++, want++)
        if (toupper((unsigned char)*ext) != *want)
            return false;
    return *ext == '\0' && *want == '\0';
}

/* 0x4c6911 */
bool app_parse_args(int argc, char **argv, const char **dir_out)
{
    if (dir_out)
        *dir_out = NULL;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        /* The original treats every '/' or '-' argument as a switch; the
         * port only does that for '-' and "/?", so absolute paths work. */
        if (arg[0] == '-' || strcmp(arg, "/?") == 0) {
            if (arg[1] == '?')
                return false;
            continue;
        }
        const char *dot = strrchr(arg, '.');
        if (dot && ext_is(dot, ".INI")) {
            snprintf(g_arg_ini_path, sizeof(g_arg_ini_path), "%s", arg);
        } else if (dot && (ext_is(dot, ".ALF") || ext_is(dot, ".ASC"))) {
            snprintf(g_arg_load_file, sizeof(g_arg_load_file), "%s", arg);
        } else if (dir_out) {
            *dir_out = arg; /* port only */
        }
    }
    return true;
}

/* ======================================================================== */
/* Message boxes                                                             */
/* ======================================================================== */

/* 0x4d5570 */
void error_box(const char *title, bool fatal, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", title);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (fatal) {
        /* The original adds several lines of tech-support contact details,
         * hides the main window, shows the box, then shuts down. */
        fprintf(stderr, "A Fatal error has occurred while running Afterlife.\n");
        sys_shutdown();
        plat_shutdown();
        exit(1); /* exit(0) in the original */
    }
}

/* 0x4d5717 */
void warning_box(const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", title);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* choice_box (0x4d5862) with buttons OK|Cancel (9). Returns 1 for OK, 8 for
 * Cancel. Before sys_state is RUNNING it uses MessageBoxA, after that the
 * game's own dialog. No UI yet: always OK. */
static int choice_box_ok_cancel(const char *title, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", title);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(" [OK]\n", stderr);
    return 1;
}

/* ======================================================================== */
/* Task table                                                                */
/* ======================================================================== */

typedef struct {
    SysInitFn owner;
    SysTaskFn shutdown;
    SysTaskFn suspend;
    SysTaskFn resume;
    SysTaskFn idle;
} SysTask;

#define SYS_MAX_TASKS 10
static SysTask g_tasks[SYS_MAX_TASKS]; /* sys_tasks 0x5351c8 */

/* 0x4c6755 */
int sys_register_task(SysInitFn owner, SysTaskFn shutdown, SysTaskFn suspend,
                      SysTaskFn resume, SysTaskFn idle)
{
    for (int i = 0; i < SYS_MAX_TASKS; i++) {
        if (!g_tasks[i].owner) {
            g_tasks[i] = (SysTask){owner, shutdown, suspend, resume, idle};
            return 0;
        }
    }
    return -1;
}

/* 0x4c67f6 */
int sys_unregister_task(SysInitFn owner)
{
    for (int i = 0; i < SYS_MAX_TASKS; i++) {
        if (g_tasks[i].owner == owner) {
            memset(&g_tasks[i], 0, sizeof(g_tasks[i]));
            return 0;
        }
    }
    return -1;
}

/* ======================================================================== */
/* Subsystems                                                                */
/* ======================================================================== */

/* --- He: game heap (MAGIC "HeInitialize") --- */

/* 0x4c98bb. The original also records GlobalMemoryStatus (used by the
 * low-memory warnings in di_initialize), mallocs "Heap size" bytes as one
 * block and builds a 4096-node free list for he_malloc (0x4c9d8e). The port
 * allocates with malloc (see vh_mem.h), so only the setting is kept. */
static int he_initialize(void)
{
    cfg_read_heap(&g_config);
    return 0;
}
static void he_shutdown(void) {} /* 0x4c9a86: frees the heap block */
static void he_suspend(void) {}  /* 0x4c9afd */
static void he_resume(void) {}   /* 0x4c9b0f */

/* 0x4d1de0 / 0x4d1df2 / 0x4d1e07 / 0x4d1e19: empty in ALIFE.EXE. */
static int sys_null_initialize(void) { return 0; }
static void sys_null_shutdown(void) {}
static void sys_null_suspend(void) {}
static void sys_null_resume(void) {}

/* --- Rc: resources (HILEVEL "RcInitialize") --- */

/* 0x4cd9b0 */
static int rc_initialize_from_ini(void)
{
    int err = cfg_read_resource_paths(&g_config);
    if (err)
        error_box("HILEVEL ERROR RcInitialize", true,
                  "Cannot locate the resource file path in the INI file");
    /* rc_initialize reports a missing CD bundle ("Where oh where is the
     * Afterlife CD?", fatal in the original). The port carries on if a
     * local bundle was indexed. */
    if (!rc_initialize(g_config.resource_file, g_config.cd_path) && !rc_local_index()->count)
        error_box("Where oh where is the Afterlife CD?", true,
                  "Please insert the Afterlife CD in the drive.");
    return 0;
}

/* --- Di: display (MAGIC "DiInitialize") --- */

/* GetClientRect(GetDesktopWindow()) and GetDeviceCaps(BITSPIXEL * PLANES).
 * The port always draws 8-bit, so it reports the 256-color desktop the
 * game wants. An unknown desktop (headless) counts as 640x480, the
 * smallest resolution the game supports. */
static void di_get_desktop_mode(int *w, int *h, int *bpp)
{
    plat_video_desktop_size(w, h);
    if (*w <= 0 || *h <= 0) {
        *w = 640;
        *h = 480;
    }
    *bpp = 8;
}

/* di_enum_display_modes (0x4cd58f): EnumDisplaySettings, up to 20 modes at
 * the current depth; only checked with "Force display settings". The port
 * goes fullscreen at any size, so every mode up to the desktop is valid. */
static bool di_mode_available(int w, int h)
{
    return w > 0 && h > 0;
}

/* 0x4ca1c0 */
static int di_initialize(void)
{
    int desk_w, desk_h, desk_bpp;
    bool depth_mismatch;

    di_get_desktop_mode(&desk_w, &desk_h, &desk_bpp);
    /* The original reads "Color depth" before the other keys and asks right
     * away; cfg_read_display reads all four. */
    cfg_read_display(&g_config, desk_w, desk_h, desk_bpp, &depth_mismatch);
    if (depth_mismatch &&
        choice_box_ok_cancel("Afterlife Display Warning",
                             "Afterlife is designed to run using a color depth of %d colors.",
                             1 << (g_config.color_depth & 31)) == 8) {
        sys_shutdown();
        plat_shutdown();
        exit(0);
    }
    /* The video layer's parse of "Resolution" (MAX or WxH, clamped to the
     * desktop). Same rules as cfg_read_display, whose result it replaces
     * when the platform knows a different desktop size. */
    int w = g_config.width, h = g_config.height;
    char *res = ini_get_string(NULL, 0, "Resolution");
    if (gfx_parse_resolution(res, &w, &h) && w > 0 && h > 0) {
        g_config.width = w;
        g_config.height = h;
    }
    /* di_save_system_palette (0x4cbedb) runs here. Then the low-memory
     * warnings: avail phys + page file < 10 MB ("Your machine is running low
     * on free memory"), else total phys < 7 MB. Not ported. */
    if (g_config.force_display && !di_mode_available(g_config.width, g_config.height))
        error_box("MAGIC ERROR DiInitialize", true,
                  "The resolution you have selected (%dx%d) is invalid\n", g_config.width,
                  g_config.height);

    /* Identity palette (0x4cb67a), di_clear_system_palette (0x4cb54c), the
     * main window at the resolution (0x4cd73a: framed and centred if smaller
     * than the desktop, else a desktop-sized popup), two memory DCs, the
     * mouse clip and the color matching tables: gfx_init. */
    plat_video_set_title(g_config.window_name);
    if (!gfx_init(g_config.width, g_config.height, g_config.force_display)) {
        error_box("MAGIC ERROR SetMainWindowSize", true, "Unable to create main window");
        return -1;
    }
    return 0;
}
static void di_shutdown(void) /* 0x4ca6ac */
{
    /* The window itself goes with plat_shutdown at the end of main(), so
     * that sound (shut down after the display) still has its platform. */
    gfx_shutdown();
}
static void di_suspend(void) {} /* 0x4ca713 */
static void di_resume(void) {}  /* 0x4ca725 */

/* --- Tm: timer (game/timer.c) --- */

/* --- Ms: mouse (gfx/mouse.c) --- */

/* 0x4b8cd0: centres the pointer and clips it to the screen. */
static int ms_initialize(void)
{
    gfx_mouse_init();
    return 0;
}
static void ms_shutdown(void) {} /* 0x4b8d28 */
static void ms_suspend(void) {}  /* 0x4b8d3a */
static void ms_resume(void) {}   /* 0x4b8d4c */

/* --- Kb: keyboard (game/input.c) --- */

/* --- So: sound (HILEVEL "SoIntialize", game/sound.c) --- */

static void vh_initialize(void) {} /* 0x4d51d0: VHUMOR error, module and mem runtime */
static int snd_initialize(void);
static bool g_snd_initialized; /* 0x6725ac */

/* 0x4b97e8. In the port it also takes compbun down, which the original
 * leaves to the VHUMOR module runtime. */
static void snd_shutdown(void)
{
    if (!g_snd_initialized)
        return;
    sys_unregister_task(snd_initialize);
    snd_shutdown_imuse();
    compbun_shutdown();
    g_snd_initialized = false;
}
static void snd_suspend(void) { snd_suspend_imuse(); } /* 0x4b9875 */
static void snd_resume(void) { snd_resume_imuse(); }   /* 0x4b98a0 */
static void snd_idle(void) { snd_idle_imuse(); }       /* 0x4b9d7c */

/* 0x4b9510 */
static int snd_initialize(void)
{
    cfg_read_sound(&g_config);
    if (!g_config.sound_enabled)
        return 0; /* "Sound = NONE": no buffers, no compbun, no iMUSE */
    /* he_malloc of the music and voice stream buffers. The original checks
     * each allocation separately ("Unable to allocate music buffer" /
     * "voice buffer"). */
    if (snd_alloc_buffers(&g_config) != 0)
        error_box("HILEVEL ERROR SoIntialize", true, "Unable to allocate stream buffers (%d, %d bytes)",
                  (int)g_config.music_stream[0], (int)g_config.voice_stream[0]);
    rc_hook_compbun();
    vh_initialize();
    compbun_init(1, 0);
    /* iMUSE init data: main window, callbacks, mixer_channels voices. The
     * original ignores imuse_initialize's result. */
    if (snd_start_imuse(&g_config) != 0)
        warning_box("HILEVEL ERROR SoIntialize", "iMUSE failed to initialize; no sound");
    sys_register_task(snd_initialize, snd_shutdown, snd_suspend, snd_resume, snd_idle);
    g_snd_initialized = true;
    return 0;
}

/* --- palette cycling --- */

/* 0x4ceba0 */
static int pcyc_initialize(void)
{
    gfx_cycle_init();
    sys_register_task(pcyc_initialize, NULL, NULL, NULL, gfx_cycle_idle); /* idle 0x4cf108 */
    return 0;
}
static void pcyc_shutdown(void) {} /* 0x4cebe3 */

/* --- fonts --- */

/* 0x4cf400: font cache, slot 0 = the built-in font. */
static int font_initialize(void)
{
    gfx_font_init();
    return 0;
}
static void font_shutdown(void) { gfx_font_shutdown(); } /* 0x4cf444 */

/* --- Wm: window manager (ui/wm.c) --- */

/* kb_flush_one: the WM's keyboard source, the keyboard module's queue. */
static bool wm_key_poll(int *type, int *key, int *ch)
{
    const KbEvent *e = kb_get_event();
    if (!e)
        return false;
    *type = e->type;
    *key = e->vk;
    *ch = e->ch;
    return true;
}

static void wm_task_shutdown(void) { wm_shutdown(); } /* 0x4c1722 */
static void wm_task_suspend(void) { wm_suspend(); }   /* 0x4c176b */
static void wm_task_resume(void) { wm_resume(); }     /* 0x4c177d */

/* 0x4c1580. The original registers its task first and takes it out again
 * if the root window can't be created; ui/wm.c leaves that to the caller.
 * Its kb_set_handler(wm_key_handler) becomes the key_poll hook, and the
 * region builder's snd_idle calls the sound_idle hook. */
static int wm_init_step(void)
{
    wm_hooks.key_poll = wm_key_poll;
    wm_hooks.sound_idle = snd_idle_imuse;
    sys_register_task(wm_initialize, wm_task_shutdown, wm_task_suspend, wm_task_resume, wm_idle);
    if (wm_initialize() != 0) {
        sys_unregister_task(wm_initialize);
        return -1;
    }
    return 0;
}

/* --- controls (ui/controls.c) --- */

static void ctl_task_suspend(void) { ctl_suspend(); } /* 0x4ba14f */
static void ctl_task_resume(void) { ctl_resume(); }   /* 0x4ba172 */

/* 0x4ba100: only registers the task (no shutdown; idle ctl_idle 0x4bf68a:
 * button blink, scroll auto-repeat). The game's pause flag (0x5fa61c) is
 * hooked up with ctl_set_pause_flag once the game module exists. */
static int ctl_init_step(void)
{
    ctl_initialize();
    sys_register_task(ctl_initialize, NULL, ctl_task_suspend, ctl_task_resume, ctl_idle);
    return 0;
}

/* --- highlight palette --- */

/* 0x4c6bd0: builds a 256-entry remap to the nearest colour 20% brighter
 * (pal_build_highlight_remap 0x4c7d79). */
static int pal_highlight_initialize(void)
{
    gfx_palette_build_highlight();
    return 0;
}
static void pal_highlight_shutdown(void) {} /* 0x4c6be7 */

/* ======================================================================== */
/* Init / shutdown / pump                                                    */
/* ======================================================================== */

/* 0x4c6485 */
int sys_initialize(void)
{
    static const SysInitFn k_init[] = {
        he_initialize,            /* 0x4c98bb  "Heap size" */
        sys_null_initialize,      /* 0x4d1de0 */
        rc_initialize_from_ini,   /* 0x4cd9b0  "CD Path", "Resource file" */
        di_initialize,            /* 0x4ca1c0  display, main window */
        tm_initialize,            /* 0x4d14e0 */
        ms_initialize,            /* 0x4b8cd0 */
        kb_initialize,            /* 0x4d05e0 */
        snd_initialize,           /* 0x4b9510  compbun_init, iMUSE */
        pcyc_initialize,          /* 0x4ceba0 */
        font_initialize,          /* 0x4cf400 */
        wm_init_step,             /* 0x4c1580 */
        ctl_init_step,            /* 0x4ba100 */
        pal_highlight_initialize, /* 0x4c6bd0 */
    };
    memset(g_tasks, 0, sizeof(g_tasks));
    cfg_set_defaults(&g_config);
    for (size_t i = 0; i < sizeof(k_init) / sizeof(k_init[0]); i++)
        if (k_init[i]() != 0)
            return -1;
    g_sys_state = SYS_STATE_RUNNING;
    return 0;
}

/* 0x4c65ef. Not the exact reverse of init: the display goes down before
 * sound. */
void sys_shutdown(void)
{
    static bool done; /* sys_shutdown_done 0x672780 */
    if (done)
        return;
    g_sys_state = SYS_STATE_SHUTDOWN;
    pal_highlight_shutdown();
    ctl_shutdown();
    wm_shutdown();
    font_shutdown();
    pcyc_shutdown();
    di_shutdown();
    snd_shutdown();
    kb_shutdown();
    ms_shutdown();
    tm_shutdown();
    rc_shutdown(); /* 0x4cdf21 */
    sys_null_shutdown();
    he_shutdown();
    done = true;
}

bool sys_quit_requested(void)
{
    return app_close_requested();
}

bool sys_dispatch_message(void)
{
    PlatEvent ev;
    if (!plat_poll_event(&ev))
        return false;
    main_wnd_proc(&ev);
    return true;
}

/* 0x4c63f0 */
void sys_pump(void)
{
    tm_poll(); /* port: the timeSetEvent thread's due ticks */
    sys_dispatch_message();
    for (int i = 0; i < SYS_MAX_TASKS; i++)
        if (g_tasks[i].idle)
            g_tasks[i].idle();
    /* The original then updates the heap high-water mark (he_bytes_peak
     * 0x6727e4 = max(peak, he_bytes_used 0x6727e0)). The port has no heap. */
    gfx_present(); /* port: GDI drew to the window immediately */
    plat_yield();
}

/* 0x4c666a */
void sys_suspend(void)
{
    for (int i = 0; i < SYS_MAX_TASKS; i++)
        if (g_tasks[i].suspend)
            g_tasks[i].suspend();
    di_suspend();
    tm_suspend();
    ms_suspend();
    kb_suspend();
    sys_null_suspend();
    he_suspend();
}

/* 0x4c66e3 */
void sys_resume(void)
{
    he_resume();
    sys_null_resume();
    kb_resume();
    ms_resume();
    tm_resume();
    di_resume();
    for (int i = 0; i < SYS_MAX_TASKS; i++)
        if (g_tasks[i].resume)
            g_tasks[i].resume();
}
