/*
 * iMUSE:wave:dispatch.c (0x4dee70..0x4e0950, "C:\iwin\WAVE\Dispatch.c"):
 * one dispatcher per track. It walks the sound's MAP (FRMT / REGN / TEXT /
 * JUMP / STOP events), feeds each region's PCM to the mixer, fires TEXT
 * markers at the triggers, follows JUMPs (with a short crossfade out of the
 * old position when the jump goes backwards), and for streamed sounds keeps
 * a list of "stream zones" that predict which file ranges the ring buffer
 * holds, so jumps can be prepared before the data is loaded.
 *
 * The MAP is copied into the dispatcher and converted to native byte order
 * (ConvertMap); its events are addressed as byte offsets into that copy:
 *
 *     +0 tag  +4 size  +8 offset (where in the file the event applies)
 *     FRMT +12 ?  +16 wordSize  +20 sampleRate  +24 channels
 *     REGN +12 length
 *     TEXT +12 NUL-terminated marker
 *     JUMP +12 destination  +16 hook ID  +20 fade length in ms
 *     STOP (no fields)
 */
#include <stdlib.h>
#include <string.h>

#include "imuse/imuse_internal.h"

#define LARGE_FADE_SIZE 350000
#define SMALL_FADE_SIZE 44100
#define SMALL_FADE_COUNT 4

static ImuseDispatch g_dispatches[IMUSE_MAX_TRACKS]; /* 0x55f288 "dispatches[]" */
static ImuseZone g_zones[IMUSE_MAX_ZONES];          /* 0x567520 "streamZones[]" */
static uint8_t *g_fade_mem;                         /* 0x567514 */
static uint8_t *g_large_fade_buf;                   /* 0x55f284 "largeFadeBufs" */
static uint8_t *g_small_fade_bufs;                  /* 0x55f270 "smallFadeBufs" */
static int g_large_fade_flag;                       /* 0x567510 */
static int g_small_fade_flags[SMALL_FADE_COUNT];    /* 0x5679d0 */
static int g_fade_started_flag;                     /* 0x55f280 */

#define ZLIST(p) ((ImuseNode **)(void *)(p))
#define ZNEXT(z) ((ImuseZone *)(z)->node.next)

/* Native int32 access into the converted map (TEXT strings can leave later
 * events unaligned). */
static int32_t mget(const uint8_t *map, int32_t off)
{
    int32_t v;
    memcpy(&v, map + off, 4);
    return v;
}

static void mset(uint8_t *map, int32_t off, int32_t v)
{
    memcpy(map + off, &v, 4);
}

/* 0x4dee70 */
int dispatch_init(ImuseInitData *init)
{
    (void)init;
    g_fade_mem = malloc(LARGE_FADE_SIZE + SMALL_FADE_COUNT * SMALL_FADE_SIZE);
    if (!g_fade_mem) {
        imuse_log("ERR in dispatch allocating buffers...\n");
        return IMUSE_ERR;
    }
    g_large_fade_buf = g_fade_mem;
    g_small_fade_bufs = g_fade_mem + LARGE_FADE_SIZE;
    g_large_fade_flag = 0;
    for (int i = 0; i < SMALL_FADE_COUNT; i++)
        g_small_fade_flags[i] = 0;
    for (int i = 0; i < IMUSE_MAX_ZONES; i++)
        g_zones[i].use_flag = 0;
    memset(g_dispatches, 0, sizeof(g_dispatches));
    return 0;
}

/* 0x4e0920 */
void dispatch_shutdown(void)
{
    free(g_fade_mem);
    g_fade_mem = NULL;
}

/* 0x4def20 */
ImuseDispatch *dispatch_get(int index)
{
    return &g_dispatches[index];
}

/* 0x4e06f0 */
static ImuseZone *alloc_zone(void)
{
    for (int i = 0; i < IMUSE_MAX_ZONES; i++) {
        ImuseZone *z = &g_zones[i];
        if (z->use_flag == 0) {
            z->node.prev = z->node.next = NULL;
            z->use_flag = 1;
            z->offset = 0;
            z->size = 0;
            z->fade_flag = 0;
            return z;
        }
    }
    imuse_log("ERR: out of streamZones...\n");
    return NULL;
}

/* Frees every zone on the list at *head. */
static void free_zones(ImuseZone **head)
{
    while (*head) {
        (*head)->use_flag = 0;
        imuse_list_remove(ZLIST(head), &(*head)->node);
    }
}

/* 0x4e0730: one 350000-byte buffer and four of 44100. */
static uint8_t *alloc_fade_buf(int32_t *size)
{
    if (*size > LARGE_FADE_SIZE) {
        imuse_log("WARNING: requested fade too large (%lu)...\n", (unsigned long)*size);
        *size = LARGE_FADE_SIZE;
    }
    if (*size > SMALL_FADE_SIZE) {
        if (!g_large_fade_flag) {
            g_large_fade_flag = 1;
            return g_large_fade_buf;
        }
        imuse_log("ERR: couldn't allocate large fade buf...\n");
        *size = SMALL_FADE_SIZE;
    }
    for (int i = 0; i < SMALL_FADE_COUNT; i++) {
        if (!g_small_fade_flags[i]) {
            g_small_fade_flags[i] = 1;
            return g_small_fade_bufs + i * SMALL_FADE_SIZE;
        }
    }
    imuse_log("ERR: couldn't allocate small fade buf...\n");
    return NULL;
}

/* 0x4e0800 */
static void free_fade_buf(uint8_t *buf)
{
    if (buf == g_large_fade_buf) {
        if (!g_large_fade_flag)
            imuse_log("ERR: redundant large fade buf de-allocation...\n");
        g_large_fade_flag = 0;
        return;
    }
    for (int i = 0; i < SMALL_FADE_COUNT; i++) {
        if (buf == g_small_fade_bufs + i * SMALL_FADE_SIZE) {
            if (!g_small_fade_flags[i])
                imuse_log("ERR: redundant small fade buf de-allocation...\n");
            g_small_fade_flags[i] = 0;
            return;
        }
    }
    imuse_log("ERR: couldn't find fade buf to de-allocate...\n");
}

/* 0x4e08a0: bytes in ms milliseconds of the dispatcher's format. */
static uint32_t get_fade_size(const ImuseDispatch *d, int32_t ms)
{
    return ((((uint32_t)d->sample_rate * (uint32_t)ms) / 1000u) & ~1u) * (uint32_t)d->word_size *
           (uint32_t)d->channel_count >> 3;
}

/* 0x4e08d0: whole frames (a 12-bit frame pair is 3 bytes). */
static void validate_fade_size(const ImuseDispatch *d, int32_t *size)
{
    int32_t align = (d->word_size == 8 ? 1 : 3) * d->channel_count;
    if (align == 0) {
        imuse_log("ERR:ValidateFadeSize() tried mod by 0...\n");
        return;
    }
    *size -= *size % align;
}

/* Copies size bytes from the stream into a freshly allocated fade buffer
 * and sets up the fade state (shared by switch_stream and navigate). */
static int start_fade(ImuseDispatch *d, int32_t size, int sync_flag)
{
    if (d->fade_buf)
        free_fade_buf(d->fade_buf);
    d->fade_buf = alloc_fade_buf(&size);
    if (!d->fade_buf)
        return 0;
    d->fade_word_size = d->word_size;
    d->fade_sample_rate = d->sample_rate;
    d->fade_channel_count = d->channel_count;
    d->fade_offset = 0;
    d->fade_remaining = 0;
    d->fade_sync_flag = sync_flag;
    d->fade_sync_delta = 0;
    d->fade_vol = 0x7f0000;
    d->fade_slope = 0;
    while (d->fade_remaining < size) {
        int32_t n = size - d->fade_remaining;
        if (n > IMUSE_STREAM_MAX_READ)
            n = IMUSE_STREAM_MAX_READ;
        const uint8_t *src = streamer_read(d->stream, n);
        if (!src) /* port: the original copied from NULL */
            break;
        memcpy(d->fade_buf + d->fade_remaining, src, (size_t)n);
        d->fade_remaining += n;
    }
    return 1;
}

/* 0x4e0640: volume of the incoming audio during a crossfade (the inverse of
 * the fade buffer's), and sets the fade slope on first use. */
static int fade_in_vol(ImuseDispatch *d)
{
    if (d->fade_slope == 0) {
        uint32_t n = (uint32_t)d->fade_remaining;
        if (n < 2)
            n = 2;
        d->fade_slope = -(int32_t)(0x7f0000u / n);
    }
    return ((0x80 - (d->fade_vol >> 16)) * d->track->eff_vol) >> 7;
}

/* 0x4e0690: volume for the next bytes of the fade buffer, then steps the
 * fade down by slope per byte. */
static int fade_out_vol(ImuseDispatch *d, int32_t bytes)
{
    int32_t old = d->fade_vol;
    int32_t v = d->fade_slope * bytes + old;
    if (v < 0)
        v = 0;
    if (v > 0x7f0000)
        v = 0x7f0000;
    d->fade_vol = v;
    return (((old >> 16) + 1) * d->track->eff_vol) >> 7;
}

/* 0x4e17c0: hook 0 jumps unless the track's hook is 0x80 (which is then
 * reset); any other hook ID jumps once when the track's hook equals it.
 * Returns 0 to jump. */
static int check_hook(int *track_hook, int hook_id)
{
    if (hook_id == 0) {
        if (*track_hook == 0x80) {
            *track_hook = 0;
            return -1;
        }
        return 0;
    }
    if (*track_hook == hook_id) {
        *track_hook = 0;
        return 0;
    }
    return -1;
}

/* 0x4e00d0: copies a big-endian MAP block (tag + size + events) into dst
 * and converts its integers to native order. */
static int convert_map(const uint8_t *src, uint8_t *dst)
{
    if (imuse_read_be32(src) != TAG_MAP) {
        imuse_log("ERR: ConvertMap() got bogus map...\n");
        return IMUSE_ERR;
    }
    uint32_t size = imuse_read_be32(src + 4);
    uint32_t total = size + 8;
    if (total > IMUSE_MAP_SIZE) {
        imuse_log("ERR: MAP TOO BIG (%lu)...\n", (unsigned long)total);
        return IMUSE_ERR;
    }
    memcpy(dst, src, total);
    mset(dst, 0, (int32_t)imuse_read_be32(dst));
    mset(dst, 4, (int32_t)imuse_read_be32(dst + 4));
    uint32_t p = 8;
    while (p < total) {
        if (p + 8 > total) {
            imuse_log("ERR: ConvertMap() converted wrong amount...\n");
            return IMUSE_ERR;
        }
        uint32_t tag = imuse_read_be32(dst + p);
        uint32_t len = imuse_read_be32(dst + p + 4);
        mset(dst, (int32_t)p, (int32_t)tag);
        mset(dst, (int32_t)p + 4, (int32_t)len);
        p += 8;
        if (tag == TAG_TEXT) {
            /* offset, then the string; the walk skips past its NUL */
            mset(dst, (int32_t)p, (int32_t)imuse_read_be32(dst + p));
            p += 4;
            while (p < total && dst[p] != 0)
                p++;
            p++;
        } else {
            for (uint32_t i = 0; i < (len >> 2) && p + 4 <= total; i++, p += 4)
                mset(dst, (int32_t)p, (int32_t)imuse_read_be32(dst + p));
        }
    }
    if (p != total) {
        imuse_log("ERR: ConvertMap() converted wrong amount...\n");
        return IMUSE_ERR;
    }
    return 0;
}

static int32_t map_end(const uint8_t *map)
{
    return mget(map, 4) + 8;
}

/* 0x4e0220: the event at offset (prev < 0: the first one; else the one
 * after prev, which must also be at offset). -1 if none. */
static int32_t next_map_event(const uint8_t *map, int32_t offset, int32_t prev)
{
    int32_t end = map_end(map);
    if (prev < 0) {
        for (int32_t e = 8; e < end; e += mget(map, e + 4) + 8)
            if (mget(map, e + 8) == offset)
                return e;
        imuse_log("ERR: GetNextMapEvent() couldn't find event at offset %lu...\n", (unsigned long)offset);
        return -1;
    }
    int32_t e = prev + mget(map, prev + 4) + 8;
    if (e >= end) {
        imuse_log("ERR: GetNextMapEvent() map overrun...\n");
        return -1;
    }
    if (mget(map, e + 8) != offset) {
        imuse_log("ERR: GetNextMapEvent() no more events at offset %lu...\n", (unsigned long)offset);
        return -1;
    }
    return e;
}

/* 0x4e0360: the first JUMP whose hook offset lies inside the zone and whose
 * hook matches (checked against a copy of the track's hook). -1 if none. */
static int32_t find_jump(const uint8_t *map, const ImuseZone *z, int *hook)
{
    int32_t end = map_end(map);
    for (int32_t e = 8; e < end; e += mget(map, e + 4) + 8) {
        uint32_t at = (uint32_t)mget(map, e + 8);
        if ((uint32_t)mget(map, e) == TAG_JUMP && (uint32_t)z->offset < at &&
            at <= (uint32_t)(z->size + z->offset) && check_hook(hook, mget(map, e + 16)) == 0)
            return e;
    }
    return -1;
}

/* Byte position in the ring of the start of zone z (sum of earlier zones). */
static int32_t zone_start(const ImuseDispatch *d, const ImuseZone *z)
{
    int32_t pos = 0;
    for (const ImuseZone *p = d->zones; p != z; p = ZNEXT(p))
        pos += p->size;
    return pos;
}

/* 0x4e03e0: rearranges the stream so that after zone z's data up to the
 * jump's hook offset, loading continues at the jump destination. For a
 * backward jump the bytes after the hook (up to the jump's fade length) are
 * kept as a fade zone and crossfaded. force: do it even if the zone does
 * not yet reach past the fade. */
static void prepare_to_jump(ImuseDispatch *d, ImuseZone *z, int32_t ev, int force)
{
    const uint8_t *map = d->map;
    int32_t hook_at = mget(map, ev + 8);
    int32_t dest = mget(map, ev + 12);
    ImuseZone *nz = ZNEXT(z);
    if (z->offset + z->size == hook_at && nz) {
        if (!nz->fade_flag) {
            if (nz->offset == dest)
                return; /* already prepared */
        } else if (nz->offset == hook_at && ZNEXT(nz) && ZNEXT(nz)->offset == dest) {
            return;
        }
    }
    int32_t fade_size = (int32_t)get_fade_size(d, mget(map, ev + 20));
    if (!force && (uint32_t)((hook_at - z->offset) + fade_size) > (uint32_t)z->size)
        return;
    int32_t keep = (z->offset - hook_at) + z->size;
    if ((uint32_t)fade_size <= (uint32_t)keep)
        keep = fade_size;
    validate_fade_size(d, &keep);
    if ((uint32_t)hook_at < (uint32_t)dest)
        keep = 0; /* forward jumps don't fade */
    ImuseZone *fz = NULL;
    if (keep != 0 && !(fz = alloc_zone())) {
        imuse_log("ERR: PrepareToJump() couldn't alloc zone...\n");
        return;
    }
    ImuseZone *jz = alloc_zone();
    if (!jz) {
        imuse_log("ERR: PrepareToJump() couldn't alloc zone...\n");
        if (fz)
            fz->use_flag = 0;
        return;
    }
    z->size = hook_at - z->offset;
    streamer_set_load_index(d->stream, keep + z->size + zone_start(d, z));
    free_zones((ImuseZone **)(void *)&z->node.next);
    streamer_restart(d->stream, d->track->sound, dest);
    if (keep != 0) {
        z->node.next = &fz->node;
        fz->node.prev = &z->node;
        fz->node.next = NULL;
        fz->offset = hook_at;
        fz->size = keep;
        fz->fade_flag = 1;
        z = fz;
    }
    z->node.next = &jz->node;
    jz->node.prev = &z->node;
    jz->node.next = NULL;
    jz->offset = dest;
    jz->size = 0;
    jz->fade_flag = 0;
}

/* 0x4e05c0: no jump in zone z: drop the zones after it and keep loading
 * from where it ends. */
static void continue_after_zone(ImuseDispatch *d, ImuseZone *z)
{
    if (!ZNEXT(z))
        return;
    streamer_set_load_index(d->stream, z->size + zone_start(d, z));
    free_zones((ImuseZone **)(void *)&z->node.next);
    streamer_restart(d->stream, d->track->sound, z->offset + z->size);
}

/* 0x4e02b0: credits newly loaded bytes to the last zone, then checks each
 * zone for an upcoming jump. */
static void predict_stream(ImuseDispatch *d)
{
    if (!d->stream || !d->zones) {
        imuse_log("ERR: null streamID or zoneList in PredictStream()...\n");
        return;
    }
    int32_t sum = 0;
    ImuseZone *last = d->zones;
    for (ImuseZone *z = d->zones; z; z = ZNEXT(z)) {
        sum += z->size;
        last = z;
    }
    last->size += streamer_avail(d->stream) - sum;
    int hook = d->track->jump_hook;
    for (ImuseZone *z = d->zones; z; z = ZNEXT(z)) {
        if (z->fade_flag)
            continue;
        int32_t ev = find_jump(d->map, z, &hook);
        if (ev < 0)
            continue_after_zone(d, z);
        else
            prepare_to_jump(d, z, ev, 0);
    }
}

/* 0x4df960 */
void dispatch_predict_all_streams(void)
{
    wave_halt();
    for (int i = 0; i < tracks_count(); i++) {
        ImuseDispatch *d = &g_dispatches[i];
        if (d->track && d->track->sound != 0 && d->stream && d->zones)
            predict_stream(d);
    }
    wave_unhalt();
}

/* 0x4dfcf0: loads and converts the MAP once. -3 = stream not loaded yet. */
static int get_map(ImuseDispatch *d)
{
    if (mget(d->map, 0) == (int32_t)TAG_MAP)
        return 0;
    if (d->current_offset != 0) {
        imuse_log("ERR: GetMap found offset but no map...\n");
        return IMUSE_ERR;
    }
    const uint8_t *m = files_sound_map(d->track->sound);
    if (m) {
        if (convert_map(m, d->map) != 0) {
            imuse_log("ERR: ConvertMap() failed...\n");
            return IMUSE_ERR;
        }
        if ((uint32_t)mget(d->map, 8) != TAG_FRMT) {
            imuse_log("ERR: expected 'FRMT' at start of map...\n");
            return IMUSE_ERR;
        }
        d->current_offset = mget(d->map, 16);
        if (d->stream) {
            if (streamer_avail(d->stream) != 0) {
                imuse_log("ERR: GetMap() expected empty stream buf...\n");
                return IMUSE_ERR;
            }
            if (d->zones) {
                imuse_log("ERR: GetMap() expected null streamZoneList...\n");
                return IMUSE_ERR;
            }
            if (!(d->zones = alloc_zone())) {
                imuse_log("ERR: GetMap() couldn't alloc zone...\n");
                return IMUSE_ERR;
            }
            d->zones->offset = d->current_offset;
            d->zones->size = 0;
            d->zones->fade_flag = 0;
            streamer_restart(d->stream, d->track->sound, d->current_offset);
        }
        return 0;
    }
    if (!d->stream) {
        const uint8_t *p = files_sound_addr(d->track->sound);
        if (!p) {
            imuse_log("ERR: GetMap() couldn't get sound addr...\n");
            return IMUSE_ERR;
        }
        if (imuse_read_be32(p) == TAG_IMUS && imuse_read_be32(p + 8) == TAG_MAP) {
            d->current_offset = (int32_t)imuse_read_be32(p + 12) + 0x18;
            if (convert_map(p + 8, d->map) != 0) {
                imuse_log("ERR: ConvertMap() failed...\n");
                return IMUSE_ERR;
            }
            if ((uint32_t)mget(d->map, 8) != TAG_FRMT) {
                imuse_log("ERR: expected 'FRMT' at start of map...\n");
                return IMUSE_ERR;
            }
            if (mget(d->map, 16) != d->current_offset) {
                imuse_log("ERR: GetMap() expected data to follow map...\n");
                return IMUSE_ERR;
            }
            return 0;
        }
        imuse_log("ERR: unrecognized file format in sound...\n");
        return IMUSE_ERR;
    }
    const uint8_t *hdr = streamer_view(d->stream, 0, 0x10);
    if (!hdr)
        return IMUSE_ERR_NOT_READY;
    if (imuse_read_be32(hdr) == TAG_IMUS && imuse_read_be32(hdr + 8) == TAG_MAP) {
        int32_t size = (int32_t)imuse_read_be32(hdr + 12) + 0x18;
        if (!streamer_view(d->stream, 0, size))
            return IMUSE_ERR_NOT_READY;
        const uint8_t *p = streamer_read(d->stream, size);
        if (!p) {
            imuse_log("ERR: stream read failed after view succeeded in GetMap()...\n");
            return IMUSE_ERR;
        }
        d->current_offset = size;
        if (convert_map(p + 8, d->map) != 0) {
            imuse_log("ERR: ConvertMap() failed...\n");
            return IMUSE_ERR;
        }
        if ((uint32_t)mget(d->map, 8) != TAG_FRMT) {
            imuse_log("ERR: expected 'FRMT' at start of map...\n");
            return IMUSE_ERR;
        }
        if (mget(d->map, 16) != d->current_offset) {
            imuse_log("ERR: GetMap() expected data to follow map...\n");
            return IMUSE_ERR;
        }
        if (d->zones) {
            imuse_log("ERR: GetMap() expected null streamZoneList...\n");
            return IMUSE_ERR;
        }
        if (!(d->zones = alloc_zone())) {
            imuse_log("ERR: GetMap() couldn't alloc zone...\n");
            return IMUSE_ERR;
        }
        d->zones->offset = d->current_offset;
        d->zones->size = streamer_avail(d->stream);
        d->zones->fade_flag = 0;
        return 0;
    }
    imuse_log("ERR: unrecognized file format in stream buf...\n");
    return IMUSE_ERR;
}

/* 0x4df9c0: at the end of a region, runs map events until the next REGN.
 * 0 = audio_remaining set; -1 = stop the sound; -3 = wait for data. */
static int navigate(ImuseDispatch *d)
{
    int r = get_map(d);
    if (r != 0)
        return r;
    if (d->audio_remaining != 0 || (d->stream && d->zones->offset != d->current_offset)) {
        imuse_log("ERR: navigation error in dispatch...\n");
        return IMUSE_ERR;
    }
    for (;;) {
        int32_t ev = -1;
        for (;;) {
            ev = next_map_event(d->map, d->current_offset, ev);
            if (ev < 0) {
                imuse_log("ERR: no more map events at offset %lx...\n", (unsigned long)d->current_offset);
                return IMUSE_ERR;
            }
            uint32_t tag = (uint32_t)mget(d->map, ev);
            if (tag == TAG_REGN) {
                if (d->current_offset == mget(d->map, ev + 8)) {
                    d->audio_remaining = mget(d->map, ev + 12);
                    return 0;
                }
                imuse_log("ERR: region offset %lu != currentOffset %lu...\n",
                          (unsigned long)mget(d->map, ev + 8), (unsigned long)d->current_offset);
                return IMUSE_ERR;
            }
            if (tag == TAG_STOP)
                return IMUSE_ERR;
            if (tag == TAG_TEXT) {
                triggers_process_marker(d->track->sound, (const char *)d->map + ev + 12);
                if (d->audio_remaining != 0)
                    return 0; /* a trigger switched the stream */
                continue;
            }
            if (tag == TAG_FRMT) {
                d->word_size = mget(d->map, ev + 16);
                d->sample_rate = mget(d->map, ev + 20);
                d->channel_count = mget(d->map, ev + 24);
                continue;
            }
            if (tag == TAG_JUMP) {
                if (check_hook(&d->track->jump_hook, mget(d->map, ev + 16)) != 0)
                    continue;
                break;
            }
            imuse_log("ERR: Unrecognized map event at offset %lx...\n", (unsigned long)d->current_offset);
            return IMUSE_ERR;
        }
        /* take the jump */
        d->current_offset = mget(d->map, ev + 12);
        if (d->stream) {
            if (d->zones->size != 0 || !ZNEXT(d->zones)) {
                imuse_log("ERR: failed to prepare for jump...\n");
                prepare_to_jump(d, d->zones, ev, 1);
            }
            ImuseZone *z = d->zones;
            z->use_flag = 0;
            imuse_list_remove(ZLIST(&d->zones), &z->node);
            if (d->zones && d->zones->fade_flag) {
                if (start_fade(d, d->zones->size, 0))
                    g_fade_started_flag = 1;
                z = d->zones;
                z->use_flag = 0;
                imuse_list_remove(ZLIST(&d->zones), &z->node);
            }
        }
    }
}

/* 0x4df080. Returns 0 also when a stream has no data yet (-3). */
int dispatch_start(ImuseTrack *t, int buf_id)
{
    ImuseDispatch *d = t->dispatch;
    d->current_offset = 0;
    d->audio_remaining = 0;
    d->map[0] = 0;
    d->fade_buf = NULL;
    if (buf_id == 0) {
        d->stream = NULL;
    } else {
        d->stream = streamer_alloc(t->sound, buf_id, IMUSE_STREAM_MAX_READ);
        if (!d->stream) {
            imuse_log("ERR: unable to alloc stream...\n");
            return IMUSE_ERR;
        }
        d->stream_buf_id = buf_id;
        d->zones = NULL;
        d->stream_err_flag = 0;
    }
    int r = navigate(d);
    if (r != 0 && r != IMUSE_ERR_NOT_READY) {
        imuse_log("ERR: problem starting sound in dispatch...\n");
        dispatch_release(t);
        return IMUSE_ERR;
    }
    return 0;
}

/* 0x4df130 */
int dispatch_release(ImuseTrack *t)
{
    ImuseDispatch *d = t->dispatch;
    if (d->stream) {
        streamer_close(d->stream);
        free_zones(&d->zones);
        d->stream = NULL; /* port: don't leave a dangling stream */
    }
    if (d->fade_buf) {
        free_fade_buf(d->fade_buf);
        d->fade_buf = NULL; /* port: the original left it set */
    }
    return 0;
}

/* 0x4df190, opcode 26 */
int dispatch_switch_stream(int old_sound, int new_sound, int fade_ms, int sync, int keep_offset)
{
    if ((unsigned)fade_ms > 2000)
        fade_ms = 2000;
    ImuseDispatch *d = NULL;
    for (int i = 0; i < tracks_count(); i++) {
        ImuseDispatch *c = &g_dispatches[i];
        if (old_sound != 0 && c->track->sound == old_sound && c->stream) {
            d = c;
            break;
        }
    }
    if (!d) {
        imuse_log("ERR: DpSwitchStream() couldn't find sound...\n");
        return IMUSE_ERR;
    }
    if (d->zones) {
        if (d->word_size == 0) {
            imuse_log("ERR: DpSwitchStream() found streamZoneList but null wordSize...\n");
            return IMUSE_ERR;
        }
        int32_t size = (int32_t)get_fade_size(d, fade_ms);
        if ((uint32_t)d->zones->size < (uint32_t)size)
            size = d->zones->size;
        validate_fade_size(d, &size);
        if (!start_fade(d, size, keep_offset | sync))
            imuse_log("WARNING:DpSwitchStream() couldn't alloc fade buf...\n");
    }
    fades_clear(d->track->sound, -1);
    triggers_clear(d->track->sound, NULL, -1);
    d->track->sound = new_sound;
    streamer_skip(d->stream, streamer_avail(d->stream));
    if (keep_offset && d->zones) {
        /* continue at the same position in the new sound */
        streamer_restart(d->stream, new_sound, d->current_offset);
        free_zones((ImuseZone **)(void *)&d->zones->node.next);
        d->zones->size = 0;
        return 0;
    }
    streamer_restart(d->stream, new_sound, 0);
    free_zones(&d->zones);
    d->current_offset = 0;
    d->audio_remaining = 0;
    d->map[0] = 0;
    int r = navigate(d);
    if (r != 0 && r != IMUSE_ERR_NOT_READY) {
        imuse_log("ERR: problem switching stream in dispatch...\n");
        tracks_stop_track(d->track);
        return IMUSE_ERR;
    }
    return 0;
}

/* Mixes the next part of the fade buffer into the output at out_offset.
 * The original has two copies: at the top of a slice an incomplete last
 * frame frees the buffer at once (top = 1); after a jump inside the slice
 * it is only freed once empty. */
static void mix_fade(ImuseDispatch *d, int32_t frames, int32_t rate, int32_t out_offset, int top)
{
    uint32_t frame_bits = (uint32_t)(d->fade_word_size * d->fade_channel_count);
    uint32_t avail = ((uint32_t)d->fade_remaining << 3) / frame_bits;
    uint32_t in_frames = ((uint32_t)frames * (uint32_t)d->fade_sample_rate) / (uint32_t)rate;
    uint32_t out_frames = (uint32_t)frames;
    if (avail < in_frames) {
        out_frames = ((uint32_t)rate * avail) / (uint32_t)d->fade_sample_rate;
        in_frames = avail;
    }
    if (d->fade_word_size == 12 && d->fade_channel_count == 1)
        in_frames &= ~1u;
    if (in_frames == 0) {
        imuse_log("WARNING:fade ends with incomplete frame (or odd 12-bit mono frame)...\n");
    }
    if (in_frames == 0 && top) {
        /* fall through to free */
    } else {
        int32_t bytes = (int32_t)(frame_bits * in_frames >> 3);
        int vol = fade_out_vol(d, bytes);
        mixer_mix(d->fade_buf + d->fade_offset, (int32_t)in_frames, d->fade_word_size, d->fade_channel_count,
                  (int32_t)out_frames, out_offset, vol, d->track->pan);
        d->fade_offset += bytes;
        d->fade_remaining -= bytes;
        if (d->fade_remaining != 0)
            return;
    }
    free_fade_buf(d->fade_buf);
    d->fade_buf = NULL;
}

/* 0x4df4a0: mixes frames output frames at rate of track t. */
void dispatch_process(ImuseTrack *t, int32_t frames, int32_t rate)
{
    ImuseDispatch *d = t->dispatch;
    if (d->stream && d->zones)
        predict_stream(d);
    if (d->fade_buf)
        mix_fade(d, frames, rate, 0, 1);

    int32_t out_pos = 0;
    for (;;) {
        if (d->audio_remaining == 0) {
            g_fade_started_flag = 0;
            int r = navigate(d);
            if (r != 0) {
                if (r == IMUSE_ERR)
                    tracks_stop_track(t);
                if (d->fade_buf && d->fade_sync_flag)
                    d->fade_sync_delta += frames;
                return;
            }
            if (g_fade_started_flag && d->fade_buf)
                mix_fade(d, frames, rate, out_pos, 0);
        }
        if (frames == 0)
            return;

        uint32_t frame_bits = (uint32_t)(d->word_size * d->channel_count);
        uint32_t avail = ((uint32_t)d->audio_remaining << 3) / frame_bits;
        uint32_t in_frames = ((uint32_t)frames * (uint32_t)d->sample_rate) / (uint32_t)rate;
        uint32_t out_frames = (uint32_t)frames;
        if (avail < in_frames) {
            out_frames = (avail * (uint32_t)rate) / (uint32_t)d->sample_rate;
            in_frames = avail;
        }
        if (d->word_size == 12 && d->channel_count == 1)
            in_frames &= ~1u;
        if (in_frames == 0) {
            imuse_log("ERR region ends with incomplete frame (or odd 12-bit mono frame)...\n");
            tracks_stop_track(t);
            return;
        }
        int32_t bytes = (int32_t)(frame_bits * in_frames >> 3);
        const uint8_t *src;
        if (!d->stream) {
            src = files_sound_addr(t->sound);
            if (!src) {
                imuse_log("ERR: dispatch got NULL file addr...\n");
                return;
            }
            src += d->current_offset;
        } else {
            src = streamer_read(d->stream, bytes);
            if (!src) {
                /* starving: wait for the streamer, unless it is at its end */
                d->stream_err_flag = 1;
                if (d->fade_buf && d->fade_sync_flag)
                    d->fade_sync_delta += frames;
                int32_t size, crit, free_space, paused;
                streamer_query(d->stream, &size, &crit, &free_space, &paused);
                if (!paused)
                    return;
                imuse_log("WARNING: stopping starving paused stream...\n");
                tracks_stop_track(t);
                return;
            }
            d->zones->offset += bytes;
            d->zones->size -= bytes;
            d->stream_err_flag = 0;
        }
        int vol;
        if (!d->fade_buf) {
            vol = t->eff_vol;
        } else {
            if (d->fade_sync_flag && d->fade_sync_delta != 0) {
                /* catch up with frames the fade played while we starved */
                uint32_t skip = (uint32_t)d->fade_sync_delta;
                if (out_frames <= skip)
                    skip = out_frames;
                d->fade_sync_delta -= (int32_t)skip;
                out_frames -= skip;
                in_frames = ((uint32_t)d->sample_rate * out_frames) / (uint32_t)rate;
                if (d->word_size == 12 && d->channel_count == 1)
                    in_frames &= ~1u;
                src = src + bytes - ((uint32_t)(d->channel_count * d->word_size) * in_frames >> 3);
            }
            vol = d->fade_buf ? fade_in_vol(d) : t->eff_vol;
        }
        if (t->mailbox)
            mixer_set_radio_chatter();
        mixer_mix(src, (int32_t)in_frames, d->word_size, d->channel_count, (int32_t)out_frames, out_pos, vol,
                  t->pan);
        mixer_clear_radio_chatter();
        d->current_offset += bytes;
        out_pos += (int32_t)out_frames;
        frames -= (int32_t)out_frames;
        d->audio_remaining -= bytes;
    }
}
