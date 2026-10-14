/*
 * Startup, init order, task table and message pump. See startup.h and
 * docs/startup.md. Subsystems that belong to later steps are stubs here,
 * each labelled with its original address.
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
#include "game/resource.h"

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
/* Subsystems. Stubs belong to later steps (rendering, iMUSE, UI).           */
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

/* Stub for the video side: the original asks GDI for the desktop size
 * (GetClientRect(GetDesktopWindow())) and BITSPIXEL * PLANES. */
static void di_get_desktop_mode(int *w, int *h, int *bpp)
{
    *w = 640;
    *h = 480;
    *bpp = 8;
}

/* Stub (di_enum_display_modes 0x4cd58f): EnumDisplaySettings, up to 20
 * modes at the current depth. Only checked with "Force display settings". */
static bool di_mode_available(int w, int h)
{
    (void)w;
    (void)h;
    return true;
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
        exit(0);
    }
    /* di_save_system_palette (0x4cbedb) runs here. Then the low-memory
     * warnings: avail phys + page file < 10 MB ("Your machine is running low
     * on free memory"), else total phys < 7 MB. Not ported. */
    if (g_config.force_display && !di_mode_available(g_config.width, g_config.height))
        error_box("MAGIC ERROR DiInitialize", true,
                  "The resolution you have selected (%dx%d) is invalid\n", g_config.width,
                  g_config.height);

    /* di_create_identity_palette (0x4cb67a), di_clear_system_palette
     * (0x4cb54c), di_set_main_window_size (0x4cd73a: CreateWindowExA, framed
     * if smaller than the desktop, centred), two compatible DCs, the mouse
     * clip rect and the squares tables. The video layer owns all of that. */
    if (!plat_init(g_config.window_name, g_config.width, g_config.height))
        return -1;
    return 0;
}
static void di_shutdown(void) /* 0x4ca6ac */
{
    plat_shutdown();
}
static void di_suspend(void) {} /* 0x4ca713 */
static void di_resume(void) {}  /* 0x4ca725 */

/* --- Tm: timer --- */

/* 0x4d14e0: tm_start(16, 0x10000) -> timeSetEvent(16 ms, periodic). */
static int tm_initialize(void) { return 0; }
static void tm_shutdown(void) {} /* 0x4d150b */
static void tm_suspend(void) {}  /* 0x4d152c */
static void tm_resume(void) {}   /* 0x4d153e */

/* --- Ms: mouse --- */

/* 0x4b8cd0: centres the cursor and sets the clip rect. */
static int ms_initialize(void) { return 0; }
static void ms_shutdown(void) {} /* 0x4b8d28 */
static void ms_suspend(void) {}  /* 0x4b8d3a */
static void ms_resume(void) {}   /* 0x4b8d4c */

/* --- Kb: keyboard --- */

static int kb_initialize(void) { return 0; } /* 0x4d05e0: clears key state */
static void kb_shutdown(void) {}             /* 0x4d05f7 */
static void kb_suspend(void) {}              /* 0x4d0609 */
static void kb_resume(void) {}               /* 0x4d061b */

/* --- So: sound (HILEVEL "SoIntialize") --- */

static void vh_initialize(void) {}           /* 0x4d51d0: VHUMOR error, module and mem runtime */
static void imuse_initialize(void) {}        /* 0x4d3ea0: iMUSE with the waveOut driver */
static bool g_snd_initialized;              /* 0x6725ac */

/* 0x4b97e8. In the port it also takes compbun down, which the original
 * leaves to the VHUMOR module runtime. */
static void snd_shutdown(void)
{
    if (g_snd_initialized)
        compbun_shutdown();
    g_snd_initialized = false;
}
static void snd_suspend(void) {}             /* 0x4b9875 */
static void snd_resume(void) {}              /* 0x4b98a0 */
static void snd_idle(void) {}                /* 0x4b9d7c */

/* 0x4b9510 */
static int snd_initialize(void)
{
    cfg_read_sound(&g_config);
    if (!g_config.sound_enabled)
        return 0; /* "Sound = NONE": no buffers, no compbun, no iMUSE */
    /* Here the original he_malloc()s the music and voice stream buffers
     * (music_stream[0] and voice_stream[0] bytes, fatal if that fails). */
    rc_hook_compbun();
    vh_initialize();
    compbun_init(1, 0);
    /* iMUSE init data: main window, callbacks, mixer_channels voices. */
    imuse_initialize();
    sys_register_task(snd_initialize, snd_shutdown, snd_suspend, snd_resume, snd_idle);
    g_snd_initialized = true;
    return 0;
}

/* --- palette cycling --- */

static void pcyc_idle(void) {} /* 0x4cf108: rotates up to 100 palette ranges every 100 ms */

/* 0x4ceba0 */
static int pcyc_initialize(void)
{
    sys_register_task(pcyc_initialize, NULL, NULL, NULL, pcyc_idle);
    return 0;
}
static void pcyc_shutdown(void) {} /* 0x4cebe3 */

/* --- cursors --- */

static int cur_initialize(void) { return 0; } /* 0x4cf400: cursor table, default arrow */
static void cur_shutdown(void) {}             /* 0x4cf444 */

/* --- Wm: window manager --- */

/* 0x4c1580: registers its task (0x4c1722, 0x4c176b, 0x4c177d, 0x4c178f),
 * hooks the mouse, timer and paint callbacks, and creates the root window
 * (wm_create_window 0x4c1bce) covering the screen. */
static int wm_initialize(void) { return 0; }
static void wm_shutdown(void) {} /* 0x4c1722 */

/* --- controls --- */

/* 0x4ba100: registers (suspend 0x4ba14f, resume 0x4ba172, idle ctl_idle
 * 0x4bf68a: button blink, scroll auto-repeat). */
static int ctl_initialize(void) { return 0; }
static void ctl_shutdown(void) {} /* 0x4ba130 */

/* --- highlight palette --- */

/* 0x4c6bd0: builds a 256-entry remap to the nearest colour 20% brighter. */
static int pal_highlight_initialize(void) { return 0; }
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
        cur_initialize,           /* 0x4cf400 */
        wm_initialize,            /* 0x4c1580 */
        ctl_initialize,           /* 0x4ba100 */
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
    cur_shutdown();
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

static bool g_quit;
static void (*g_event_hook)(const PlatEvent *ev);

bool sys_quit_requested(void)
{
    return g_quit;
}

void sys_set_event_hook(void (*hook)(const PlatEvent *ev))
{
    g_event_hook = hook;
}

/* main_wnd_proc (0x4cb7ad), the part the platform layer reports. */
static void main_wnd_proc(const PlatEvent *ev)
{
    if (ev->type == PLAT_EV_QUIT)
        g_quit = true;
    if (g_event_hook)
        g_event_hook(ev);
}

/* 0x4c63f0 */
void sys_pump(void)
{
    PlatEvent ev;
    while (plat_poll_event(&ev))
        main_wnd_proc(&ev);
    for (int i = 0; i < SYS_MAX_TASKS; i++)
        if (g_tasks[i].idle)
            g_tasks[i].idle();
    /* The original also keeps a high-water mark of a tick counter
     * (0x6727e4 = max(0x6727e4, 0x6727e0)). */
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
