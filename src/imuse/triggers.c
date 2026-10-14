/*
 * iMUSE:triggers.c (0x4d9510..0x4d9da0): one-shot actions fired by TEXT
 * markers in a playing sound ("trigs[]", 8 slots) and actions run after a
 * number of 60 Hz ticks ("defers[]", 8 slots). An action is a command opcode
 * with ten arguments, a host function, or (opcode 0) the init data's
 * trigger callback.
 */
#include <string.h>

#include "imuse/imuse_internal.h"

#define MAX_TRIGS 8
#define MAX_DEFERS 8
#define MARKER_LEN 256

typedef struct {
    int sound; /* 0 = free */
    char marker[MARKER_LEN];
    int opcode;
    ImuseCallback fn; /* port: the original kept the function in opcode */
    intptr_t args[10];
    int clear_later;
} Trigger;

typedef struct {
    int counter; /* ticks left, 0 = free */
    int opcode;
    ImuseCallback fn;
    intptr_t args[10];
} Defer;

static Trigger g_trigs[MAX_TRIGS];   /* 0x55dde8 */
static Defer g_defers[MAX_DEFERS];   /* 0x55e790 */
static int g_defers_on;              /* 0x55dde0 */
static int g_mid_processing;         /* 0x55e788 */
static ImuseInitData *g_init;        /* 0x55e78c */
static char g_marker[MARKER_LEN];    /* 0x55e910 */

/* 0x4d9d60 (also opcode-10 helper 0x4d9580) */
static void triggers_clear_all(void)
{
    for (int i = 0; i < MAX_TRIGS; i++)
        g_trigs[i].sound = 0;
    for (int i = 0; i < MAX_DEFERS; i++)
        g_defers[i].counter = 0;
    g_defers_on = 0;
    g_mid_processing = 0;
}

/* 0x4d9510 */
int triggers_init(ImuseInitData *init)
{
    g_init = init;
    triggers_clear_all();
    return 0;
}

/* 0x4d9580 */
int triggers_shutdown(void)
{
    triggers_clear_all();
    return 0;
}

static void copy_args(intptr_t dst[10], const intptr_t *src)
{
    for (int i = 0; i < 10; i++)
        dst[i] = src ? src[i] : 0;
}

/* 0x4d9610, opcode 17 */
int triggers_set(int sound, const char *marker, int opcode, ImuseCallback fn, const intptr_t args[10])
{
    if (sound == 0)
        return IMUSE_ERR_ARG;
    if (!marker)
        marker = "";
    if (strlen(marker) > MARKER_LEN - 1) {
        imuse_log("ERR: attempting to set trig with oversize marker string...\n");
        return IMUSE_ERR_ARG;
    }
    for (int i = 0; i < MAX_TRIGS; i++) {
        Trigger *t = &g_trigs[i];
        if (t->sound != 0)
            continue;
        t->sound = sound;
        t->clear_later = 0;
        t->opcode = fn ? IMUSE_OP_CALLBACK : opcode;
        t->fn = fn;
        strcpy(t->marker, marker);
        copy_args(t->args, args);
        return 0;
    }
    imuse_log("ERR: tr unable to alloc trigger...\n");
    return IMUSE_ERR_FULL;
}

static int trig_matches(const Trigger *t, int sound, const char *marker, int opcode)
{
    if (t->sound == 0 || (sound != -1 && sound != t->sound))
        return 0;
    if (marker && strcmp(marker, t->marker) != 0)
        return 0;
    return opcode == -1 || t->opcode == opcode;
}

/* 0x4d96d0, opcode 18: number of matching triggers. */
int triggers_check(int sound, const char *marker, int opcode)
{
    int n = 0;
    for (int i = 0; i < MAX_TRIGS; i++)
        n += trig_matches(&g_trigs[i], sound, marker, opcode);
    return n;
}

/* 0x4d9750, opcode 19. While a marker is being processed the slots are only
 * flagged and freed afterwards. */
int triggers_clear(int sound, const char *marker, int opcode)
{
    for (int i = 0; i < MAX_TRIGS; i++) {
        Trigger *t = &g_trigs[i];
        if (!trig_matches(t, sound, marker, opcode))
            continue;
        if (g_mid_processing == 0)
            t->sound = 0;
        else
            t->clear_later = 1;
    }
    return 0;
}

/* Runs an action: opcode < 30 goes through the command function, a host
 * function gets the marker (NULL for defers), opcode 0 goes to the init
 * data's callback. */
static void run_action(int opcode, ImuseCallback fn, const char *marker, const intptr_t args[10])
{
    if (opcode == IMUSE_OP_CALLBACK && fn) {
        fn(marker, args);
    } else if (opcode != 0) {
        intptr_t a[14] = {0};
        for (int i = 0; i < 10; i++)
            a[i] = args[i];
        imuse_command(opcode, a);
    } else if (g_init && g_init->trigger_callback) {
        g_init->trigger_callback(marker, args);
    } else {
        imuse_log("ERR: null callback in InitData struct...\n");
    }
}

/* 0x4d9a40: one-shot, the slot is freed before the action runs. */
static void trigger_fire(Trigger *t, const char *marker)
{
    intptr_t args[10];
    copy_args(args, t->args);
    t->sound = 0;
    run_action(t->opcode, t->fn, marker, args);
}

/* 0x4d97e0: called by the dispatcher for each TEXT event. */
void triggers_process_marker(int sound, const char *marker)
{
    if (strlen(marker) >= MARKER_LEN) {
        imuse_log("ERR: TgProcessMarker() passed oversize marker string...\n");
        return;
    }
    strcpy(g_marker, marker);
    g_mid_processing++;
    for (int i = 0; i < MAX_TRIGS; i++) {
        Trigger *t = &g_trigs[i];
        if (t->sound != 0 && t->sound == sound && (t->marker[0] == '\0' || strcmp(g_marker, t->marker) == 0)) {
            /* The action may start sounds whose markers overwrite g_marker. */
            char saved[MARKER_LEN];
            strcpy(saved, g_marker);
            trigger_fire(t, g_marker);
            strcpy(g_marker, saved);
        }
    }
    if (--g_mid_processing == 0) {
        for (int i = 0; i < MAX_TRIGS; i++)
            if (g_trigs[i].clear_later)
                g_trigs[i].sound = 0;
    }
}

/* 0x4d98f0, opcode 20 */
int triggers_defer(int ticks, int opcode, ImuseCallback fn, const intptr_t args[10])
{
    if (ticks == 0)
        return IMUSE_ERR_ARG;
    for (int i = 0; i < MAX_DEFERS; i++) {
        Defer *d = &g_defers[i];
        if (d->counter != 0)
            continue;
        d->opcode = fn ? IMUSE_OP_CALLBACK : opcode;
        d->fn = fn;
        copy_args(d->args, args);
        d->counter = ticks;
        g_defers_on = 1;
        return 0;
    }
    imuse_log("ERR: tr unable to alloc deferred cmd...\n");
    return IMUSE_ERR_FULL;
}

/* 0x4d9970: 60 Hz. */
void triggers_defers_tick(void)
{
    if (!g_defers_on)
        return;
    g_defers_on = 0;
    for (int i = 0; i < MAX_DEFERS; i++) {
        Defer *d = &g_defers[i];
        if (d->counter == 0)
            continue;
        g_defers_on = 1;
        if (--d->counter == 0) {
            /* 0x4d9be0. The original passes args[0] where a trigger would
             * have the marker; the port passes NULL and all ten args. */
            run_action(d->opcode, d->fn, NULL, d->args);
        }
    }
}

/* 0x4d99c0: triggers and defers that will start (opcode 8) or switch to
 * (opcode 26) the sound. */
int triggers_count_pending(int sound)
{
    int n = 0;
    for (int i = 0; i < MAX_TRIGS; i++) {
        const Trigger *t = &g_trigs[i];
        if (t->sound && ((t->opcode == IMUSE_OP_START_SOUND && t->args[0] == sound) ||
                         (t->opcode == IMUSE_OP_SWITCH_STREAM && t->args[1] == sound)))
            n++;
    }
    for (int i = 0; i < MAX_DEFERS; i++) {
        const Defer *d = &g_defers[i];
        if (d->counter && ((d->opcode == IMUSE_OP_START_SOUND && d->args[0] == sound) ||
                           (d->opcode == IMUSE_OP_SWITCH_STREAM && d->args[1] == sound)))
            n++;
    }
    return n;
}
