/*
 * iMUSE:groups.c (0x4da590..0x4da690): 16 group volumes. Group 0 is the
 * master volume, which scales the effective volume of the other groups.
 */
#include "imuse/imuse_internal.h"

static int g_group_vols[16];     /* 0x55ec98 "groupVols[]" */
static int g_group_eff_vols[16]; /* 0x55ecd8 "groupEffVols[]" */

/* 0x4da590 */
int groups_init(void)
{
    for (int i = 0; i < 16; i++) {
        g_group_eff_vols[i] = 127;
        g_group_vols[i] = 127;
    }
    return 0;
}

/* 0x4da5e0 */
int groups_shutdown(void)
{
    return 0;
}

/* 0x4da5f0, opcode 7. Returns the old volume; vol -1 just reads it. */
int groups_set_vol(int group, int vol)
{
    if ((unsigned)group > 15)
        return IMUSE_ERR_ARG;
    if (vol == -1)
        return g_group_vols[group];
    if ((unsigned)vol > 127)
        return IMUSE_ERR_ARG;
    int old = g_group_vols[group];
    if (group == 0) {
        g_group_vols[0] = vol;
        g_group_eff_vols[0] = vol;
        for (int i = 1; i < 16; i++)
            g_group_eff_vols[i] = ((g_group_vols[i] + 1) * vol) >> 7;
    } else {
        g_group_vols[group] = vol;
        g_group_eff_vols[group] = ((vol + 1) * g_group_vols[0]) >> 7;
    }
    wave_update_group_vols();
    return old;
}

/* 0x4da690 */
int groups_get_eff_vol(int group)
{
    if ((unsigned)group > 15)
        return IMUSE_ERR_ARG;
    return g_group_eff_vols[group];
}
