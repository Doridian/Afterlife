/*
 * iMUSE:files.c (0x4da860..0x4daa40): the host's sound access callbacks from
 * the init data, with the sound ID check in front.
 */
#include "imuse/imuse_internal.h"

static ImuseInitData *g_init; /* 0x55ed24 */

/* 0x4da860 */
int files_init(ImuseInitData *init)
{
    g_init = init;
    return init ? 0 : -1;
}

/* 0x4da8a0 */
int files_shutdown(void)
{
    return 0;
}

/* 0x4da950: 0 and the top 16 values are reserved. */
int files_valid_sound(int sound)
{
    return sound != 0 && (uint32_t)sound < 0xfffffff0u;
}

/* 0x4da8b0 */
const uint8_t *files_sound_addr(int sound)
{
    if (files_valid_sound(sound) && g_init && g_init->sound_addr)
        return g_init->sound_addr(sound);
    imuse_log("ERR: soundAddrFunc failure in files.c...\n");
    return NULL;
}

/* 0x4da8f0 */
const uint8_t *files_sound_map(int sound)
{
    if (files_valid_sound(sound) && g_init && g_init->sound_map)
        return g_init->sound_map(sound);
    return NULL;
}

/* 0x4da920: 2 if a track plays the sound, else -1. */
int files_check_sound(int sound)
{
    int s = 0;
    do {
        s = wave_get_next_sound(s);
        if (s == 0)
            return -1;
    } while (s != sound);
    return 2;
}

/* 0x4da970 */
int32_t files_seek(int sound, int32_t offset, int whence)
{
    if (files_valid_sound(sound) && g_init && g_init->seek)
        return g_init->seek(sound, offset, whence);
    imuse_log("ERR: seekFunc failure in files.c...\n");
    return 0;
}

/* 0x4da9c0 */
int32_t files_read(int sound, uint8_t *buf, int32_t size)
{
    if (files_valid_sound(sound) && g_init && g_init->read)
        return g_init->read(sound, buf, size);
    imuse_log("ERR: readFunc failure in files.c...\n");
    return 0;
}

/* 0x4daa10 */
const ImuseBufInfo *files_buf_info(int buf_id)
{
    if (buf_id != 0 && g_init && g_init->buf_info)
        return g_init->buf_info(buf_id);
    imuse_log("ERR: bufInfoFunc failure in files.c...\n");
    return NULL;
}
