/*
 * iMUSE:time.c (0x4da730..0x4da7f0): hands the host the periodic handler
 * (init->timer_handler) and remembers its period. The original's DOS
 * heritage shows in "TcStartInts: Who owns timer?": on Windows the host owns
 * the timer and only calls the handler.
 */
#include "imuse/imuse_internal.h"

static int32_t g_usec_per_int; /* 0x55ed20 */
static int g_in_handler;       /* 0x675a94 "intFlag" */

/* 0x4da7b0: non-reentrant entry to the command module's tick. */
void imuse_timer_handler(void)
{
    if (!g_in_handler) {
        g_in_handler = 1;
        commands_tick();
        g_in_handler = 0;
    }
}

/* 0x4da730 */
int time_init(ImuseInitData *init)
{
    if (init->usec_per_int == 0) {
        imuse_log("TcStartInts: Who owns timer?\n");
    } else {
        init->timer_handler = imuse_timer_handler;
        g_usec_per_int = init->usec_per_int;
    }
    g_in_handler = 0;
    return 0;
}

/* 0x4da790 (calls the empty module shutdown 0x4da7e0) */
int time_shutdown(void)
{
    return 0;
}

/* 0x4da7a0 */
int32_t time_usec_per_int(void)
{
    return g_usec_per_int;
}
