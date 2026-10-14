/*
 * iMUSE:fades.c (0x4da110..0x4da430): linear parameter fades, stepped at
 * 60 Hz. The value is applied with set_param only every 6th tick (10 Hz) and
 * only when it changed; a volume fade that ends at 0 stops the sound.
 */
#include "imuse/imuse_internal.h"

#define MAX_FADES 16

typedef struct {
    int status;
    int sound;
    int param;
    int current_val;
    int counter;          /* ticks left */
    int length;
    int slope;            /* whole steps per tick */
    int slope_mod;        /* remainder, spread with... */
    int mod_ovflo_counter; /* ...this Bresenham accumulator */
    int nudge;            /* +1 or -1 */
} Fade;

static Fade g_fades[MAX_FADES]; /* 0x55ea18 "fades[]" */
static int g_fades_on;          /* 0x55ea10 "fadesOn" */

/* 0x4da110 */
int fades_init(void)
{
    for (int i = 0; i < MAX_FADES; i++)
        g_fades[i].status = 0;
    g_fades_on = 0;
    return 0;
}

/* 0x4da160. Note that stop_all_sounds calls this and it clears nothing. */
int fades_shutdown(void)
{
    return 0;
}

/* 0x4da310: param -1 matches every parameter of the sound. */
void fades_clear(int sound, int param)
{
    for (int i = 0; i < MAX_FADES; i++) {
        Fade *f = &g_fades[i];
        if (f->status && f->sound == sound && (f->param == param || param == -1))
            f->status = 0;
    }
}

/* 0x4da1d0, opcode 14. ticks = 0 sets the value at once. */
int fades_fade_param(int sound, int param, int value, int ticks)
{
    if (sound == 0 || ticks < 0)
        return IMUSE_ERR_ARG;
    if (param != IMUSE_P_PRIORITY && param != IMUSE_P_VOLUME && param != IMUSE_P_PAN &&
        param != IMUSE_P_DETUNE && param != 0xf00 && ((param >> 8) & 0xff) != 0x11)
        return IMUSE_ERR_ARG;
    fades_clear(sound, param);
    if (ticks == 0) {
        if (param == IMUSE_P_VOLUME && value == 0)
            imuse_stop_sound(sound);
        else
            imuse_set_param(sound, param, value);
        return 0;
    }
    for (int i = 0; i < MAX_FADES; i++) {
        Fade *f = &g_fades[i];
        if (f->status)
            continue;
        f->sound = sound;
        f->param = param;
        f->current_val = imuse_get_param(sound, param);
        f->length = ticks;
        f->counter = ticks;
        int diff = value - f->current_val;
        f->slope = diff / ticks;
        f->nudge = diff < 0 ? -1 : 1;
        if (diff < 0)
            diff = -diff;
        f->slope_mod = diff % ticks;
        f->mod_ovflo_counter = 0;
        f->status = 1;
        g_fades_on = 1;
        return 0;
    }
    imuse_log("ERROR: fd unable to alloc fade...\n");
    return IMUSE_ERR_FULL;
}

/* 0x4da350: one 60 Hz step of every fade. */
void fades_tick(void)
{
    if (!g_fades_on)
        return;
    g_fades_on = 0;
    for (int i = 0; i < MAX_FADES; i++) {
        Fade *f = &g_fades[i];
        if (!f->status)
            continue;
        g_fades_on = 1;
        if (--f->counter == 0)
            f->status = 0;
        int val = f->current_val + f->slope;
        unsigned acc = (unsigned)f->slope_mod + (unsigned)f->mod_ovflo_counter;
        f->mod_ovflo_counter = (int)acc;
        if ((unsigned)f->length <= acc) {
            f->mod_ovflo_counter = (int)(acc - (unsigned)f->length);
            val += f->nudge;
        }
        if (f->current_val != val) {
            f->current_val = val;
            if (f->counter % 6 == 0) {
                if (f->param == IMUSE_P_VOLUME && val == 0)
                    imuse_stop_sound(f->sound);
                else
                    imuse_set_param(f->sound, f->param, val);
            }
        }
    }
}
