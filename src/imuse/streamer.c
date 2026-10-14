/*
 * iMUSE:wave:streamer.c (0x4e0d90..0x4e17c0): up to three streams, each a
 * ring buffer the host provides (bufID -> ImuseBufInfo), filled from the
 * sound's file through the host's seek/read callbacks by
 * streamer_process_streams() and drained by the dispatcher.
 *
 * The ring is buf_size bytes; max_read + 4 bytes after it are spare, so a
 * read or view that wraps can be made contiguous by copying the wrapped part
 * from the start of the buffer to past the end. The same address range holds
 * the small list/string helpers the wave modules share.
 */
#include <string.h>

#include "imuse/imuse_internal.h"

static ImuseStream g_streams[IMUSE_MAX_STREAMS]; /* 0x5679e8 "streams[]" */
static ImuseStream *g_last_stream_loaded;        /* 0x5679e0 */
static int g_bail_flag;                          /* 0x567a78 */

/* 0x4e16b0 */
uint32_t imuse_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* 0x4e15e0: push to the front. The item must not be on a list. */
int imuse_list_add(ImuseNode **head, ImuseNode *item)
{
    if (!item || item->prev || item->next) {
        imuse_log("ERR: list arg err when adding...\n");
        return IMUSE_ERR_ARG;
    }
    item->next = *head;
    if (*head)
        (*head)->prev = item;
    item->prev = NULL;
    *head = item;
    return 0;
}

/* 0x4e1630. head may also point at an item's next field, which removes from
 * the rest of a list after that item. */
int imuse_list_remove(ImuseNode **head, ImuseNode *item)
{
    ImuseNode *p = *head;
    if (!item || !p) {
        imuse_log("ERR: list arg err when removing...\n");
        return IMUSE_ERR_ARG;
    }
    while (p && p != item)
        p = p->next;
    if (!p) {
        imuse_log("ERR: item not on list...\n");
        return IMUSE_ERR_NOT_READY;
    }
    if (item->next)
        item->next->prev = item->prev;
    if (!item->prev)
        *head = item->next;
    else
        item->prev->next = item->next;
    item->next = NULL;
    item->prev = NULL;
    return 0;
}

/* 0x4e0d90 */
int streamer_init(void)
{
    for (int i = 0; i < IMUSE_MAX_STREAMS; i++)
        g_streams[i].sound = 0;
    g_last_stream_loaded = NULL;
    return 0;
}

/* 0x4e0de0 */
ImuseStream *streamer_alloc(int sound, int buf_id, int32_t max_read)
{
    const ImuseBufInfo *info = files_buf_info(buf_id);
    if (!info) {
        imuse_log("ERR: streamer couldn't get buf info...\n");
        return NULL;
    }
    if ((info->size >> 2) <= max_read) {
        imuse_log("ERR: maxRead too big for buf...\n");
        return NULL;
    }
    for (int i = 0; i < IMUSE_MAX_STREAMS; i++) {
        if (g_streams[i].sound != 0 && g_streams[i].buf_id == buf_id) {
            imuse_log("ERR: stream bufID %lu already in use...\n", (unsigned long)buf_id);
            return NULL;
        }
    }
    for (int i = 0; i < IMUSE_MAX_STREAMS; i++) {
        ImuseStream *s = &g_streams[i];
        if (s->sound != 0)
            continue;
        s->sound = sound;
        s->cur_offset = 0;
        s->end_offset = files_seek(sound, 0, 2);
        s->buf_id = buf_id;
        s->buf = info->buf;
        s->buf_size = info->size - max_read - 4;
        s->load_size = info->load_size;
        s->critical_size = info->critical_size;
        s->max_read = max_read;
        s->load_index = 0;
        s->read_index = 0;
        s->paused = 0;
        return s;
    }
    imuse_log("ERR: no spare streams...\n");
    return NULL;
}

/* 0x4e0ef0 */
void streamer_close(ImuseStream *s)
{
    s->sound = 0;
    if (g_last_stream_loaded == s)
        g_last_stream_loaded = NULL;
}

/* 0x4e11d0: bytes loaded and not yet read. */
int32_t streamer_avail(const ImuseStream *s)
{
    int32_t n = s->load_index - s->read_index;
    if (n < 0)
        n += s->buf_size;
    return n;
}

/* 0x4e1300: one refill of up to load_size bytes from the file. At the end
 * of the file the stream pauses and four 0x7f bytes are appended (near
 * silence for 8-bit audio, the original's end marker). */
static int streamer_fetch_data(ImuseStream *s)
{
    if (s->end_offset == 0)
        s->end_offset = files_seek(s->sound, 0, 2);
    int32_t free_space = s->read_index - s->load_index;
    if (free_space < 1)
        free_space += s->buf_size;
    int32_t remaining = s->end_offset - s->cur_offset;
    int32_t n = s->load_size;
    if (free_space - 4 <= n)
        n = free_space - 4;
    if (remaining <= n)
        n = remaining;
    if (remaining < 1) {
        s->paused = 1;
        for (int i = 0; i < 4; i++) {
            s->buf[s->load_index] = 0x7f;
            if (++s->load_index >= s->buf_size) /* port: wrap */
                s->load_index = 0;
        }
    }
    while (n > 0) {
        int32_t chunk = s->buf_size - s->load_index;
        if (n <= chunk)
            chunk = n;
        int32_t pos = s->cur_offset;
        if (files_seek(s->sound, pos, 0) != pos) {
            imuse_log("ERR: Invalid seek in streamer...\n");
            s->paused = 1;
            return 0;
        }
        /* The original lets the mixer run while the disk is read and bails
         * out if anything moved the stream meanwhile. */
        g_bail_flag = 0;
        wave_unhalt();
        int32_t got = files_read(s->sound, s->buf + s->load_index, chunk);
        wave_halt();
        if (g_bail_flag)
            return 0;
        n -= got;
        s->cur_offset += got;
        g_last_stream_loaded = s;
        s->load_index += got;
        if (s->load_index >= s->buf_size)
            s->load_index -= s->buf_size;
        if (got != chunk) {
            imuse_log("ERR: unable to load correct amount of data (%ld, %ld)...\n", (long)chunk, (long)got);
            g_last_stream_loaded = NULL;
            return 0;
        }
    }
    return 0;
}

/* 0x4e0f10, opcode 27: refills one or two streams, the ones below their
 * critical size first, alternating otherwise. Afterlife calls it every
 * 100 ms from snd_idle. */
int streamer_process_streams(void)
{
    ImuseStream *s1 = NULL, *s2 = NULL;
    for (int i = 0; i < IMUSE_MAX_STREAMS; i++) {
        ImuseStream *s = &g_streams[i];
        if (s->sound == 0 || s->paused)
            continue;
        if (!s1)
            s1 = s;
        else if (!s2)
            s2 = s;
        else
            imuse_log("ERR: three streams in use...\n");
    }
    if (!s1)
        return 0;
    dispatch_predict_all_streams();
    if (!s2) {
        streamer_fetch_data(s1);
        return 0;
    }
    int crit1 = streamer_avail(s1) < s1->critical_size;
    int crit2 = streamer_avail(s2) < s2->critical_size;
    if (crit1 && !crit2) {
        streamer_fetch_data(s1);
    } else if (crit1) {
        if (g_last_stream_loaded == s1) {
            streamer_fetch_data(s1);
            streamer_fetch_data(s2);
        } else {
            streamer_fetch_data(s2);
            streamer_fetch_data(s1);
        }
    } else if (crit2) {
        streamer_fetch_data(s2);
    } else if (g_last_stream_loaded == s1) {
        streamer_fetch_data(s1);
    } else {
        streamer_fetch_data(s2);
    }
    return 0;
}

/* 0x4e1070: consumes size bytes, returning them contiguous. NULL if not
 * loaded yet. */
const uint8_t *streamer_read(ImuseStream *s, int32_t size)
{
    if (streamer_avail(s) < size || s->max_read < size)
        return NULL;
    int32_t ri = s->read_index;
    if (s->buf_size - ri < size)
        memcpy(s->buf + s->buf_size, s->buf, (size_t)(ri - s->buf_size + size + 4));
    const uint8_t *p = s->buf + ri;
    ri += size;
    if (ri >= s->buf_size)
        ri -= s->buf_size;
    s->read_index = ri;
    return p;
}

/* 0x4e10e0: like read, but only peeks at offset without consuming. */
const uint8_t *streamer_view(ImuseStream *s, int32_t offset, int32_t size)
{
    if (offset + size > streamer_avail(s) || size > s->max_read)
        return NULL;
    int32_t p = s->read_index + offset;
    if (p >= s->buf_size)
        p -= s->buf_size;
    if (s->buf_size - p < size)
        memcpy(s->buf + s->buf_size, s->buf, (size_t)(p - s->buf_size + size));
    return s->buf + p;
}

/* 0x4e1150: drops size loaded bytes. */
int streamer_skip(ImuseStream *s, int32_t size)
{
    g_bail_flag = 1;
    if (streamer_avail(s) < size)
        return IMUSE_ERR;
    s->read_index += size;
    if (s->read_index >= s->buf_size)
        s->read_index -= s->buf_size;
    return 0;
}

/* 0x4e1190: forgets everything loaded past offset (from the read index). */
int streamer_set_load_index(ImuseStream *s, int32_t offset)
{
    g_bail_flag = 1;
    if (streamer_avail(s) < offset)
        return IMUSE_ERR;
    s->load_index = s->read_index + offset;
    if (s->load_index >= s->buf_size)
        s->load_index -= s->buf_size;
    return 0;
}

/* 0x4e11e0: continue loading sound from offset. */
void streamer_restart(ImuseStream *s, int sound, int32_t offset)
{
    g_bail_flag = 1;
    s->sound = sound;
    s->cur_offset = offset;
    s->end_offset = 0;
    s->paused = 0;
    if (g_last_stream_loaded == s)
        g_last_stream_loaded = NULL;
}

/* 0x4e1220 */
void streamer_query(ImuseStream *s, int32_t *size, int32_t *crit, int32_t *free_space, int32_t *paused)
{
    dispatch_predict_all_streams();
    *size = s->buf_size;
    *crit = s->critical_size;
    *free_space = streamer_avail(s);
    *paused = s->paused;
}

/* 0x4e1260: the host pushes data itself. On overflow the oldest data is
 * dropped, rounded to whole 12-byte units. */
int streamer_feed(ImuseStream *s, const uint8_t *data, int32_t size, int paused)
{
    int32_t free_space = s->read_index - s->load_index;
    if (free_space < 1)
        free_space += s->buf_size;
    if (free_space - 4 < size) {
        int32_t over = size - (free_space - 4);
        imuse_log("ERR: FeedStream() buffer overflow...\n");
        streamer_skip(s, over - over % 12 + 12);
    }
    while (size > 0) {
        int32_t n = s->buf_size - s->load_index;
        if (size <= n)
            n = size;
        size -= n;
        memcpy(s->buf + s->load_index, data, (size_t)n);
        data += n;
        s->cur_offset += n;
        s->load_index += n;
        if (s->load_index >= s->buf_size)
            s->load_index -= s->buf_size;
    }
    s->paused = paused;
    return 0;
}
