/*
 * imuse_test DATA_DIR OUT_DIR
 *
 * Plays Afterlife sounds through the game's sound module and iMUSE, headless
 * (the output driver's virtual device, 60 timer ticks per second), and
 * checks the mix:
 *
 *  1. An effect alone (snd_play, group 1). Its mix must equal the resource's
 *     PCM put through the mixer's arithmetic for one centred voice at full
 *     volume (computed here independently: 17 volume levels, equal-power pan
 *     table, soft-clip curve), sample for sample.
 *  2. A music track streamed from the bundle (snd_play(id, 4), bufID 1,
 *     group 4): the first seconds, alone, must equal its PCM the same way.
 *     Then an effect joins, then a voice (slot 199, bufID 2, group 2) with a
 *     trigger on its lip-sync markers; the music must duck to 5/8 while the
 *     voice plays and come back after.
 *  3. The looping sound (JUMP with hook 0), from memory and streamed: it
 *     must keep playing past its length and end once the hook is set to
 *     0x80, with no silent gap at the jump.
 *  4. Duration, silence, clipping and the 17x17 pan table against
 *     ALIFE.EXE's copy (0x6771a0) when the exe is in DATA_DIR.
 *
 * Writes OUT_DIR/imuse_sfx.wav and OUT_DIR/imuse_mix.wav (22050 Hz stereo).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/config.h"
#include "game/resource.h"
#include "game/sound.h"
#include "imuse/imuse.h"
#include "imuse/waveapi.h"
#include "platform/platform.h"

#define RATE 22050
#define SFX_ID 65573   /* 4.67 s, FRMT/REGN/STOP, 22050 Hz mono */
#define MUSIC_ID 65595 /* 3 min music, 22050 Hz mono */
#define VOICE_ID 65803 /* 5 s with 28 "MK nn" lip-sync markers */
#define LOOP_ID 65549  /* 1.48 s, the only sound with a JUMP (back, hook 0, 500 ms fade) */

int imuse_mixer_pan_level(int level, int pos); /* mixer.c, for this test */

static int16_t *g_out;
static size_t g_out_frames, g_out_cap;
static int g_failures;

static void sink(const int16_t *frames, int count, void *user)
{
    (void)user;
    if (g_out_frames + (size_t)count > g_out_cap) {
        g_out_cap = (g_out_cap + (size_t)count) * 2;
        g_out = realloc(g_out, g_out_cap * 2 * sizeof(int16_t));
    }
    memcpy(g_out + g_out_frames * 2, frames, (size_t)count * 2 * sizeof(int16_t));
    g_out_frames += (size_t)count;
}

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        g_failures++;
}

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void put16(FILE *f, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

static void write_wav(const char *path, const int16_t *s, size_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    uint32_t bytes = (uint32_t)(frames * 4);
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, RATE);
    put32(f, RATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    for (size_t i = 0; i < frames * 2; i++)
        put16(f, (uint16_t)s[i]);
    fclose(f);
    printf("wrote %s (%zu frames, %.2f s)\n", path, frames, (double)frames / RATE);
}

/* The PCM of an iMUS resource with a single region (FRMT/REGN/STOP). */
static uint8_t *load_pcm(uint32_t id, size_t *len)
{
    long size = rc_resource_size(id);
    PlatFile *f = rc_open_resource(id);
    if (!f || size <= 0)
        return NULL;
    uint8_t *buf = malloc((size_t)size);
    plat_file_read(f, buf, 1, (size_t)size);
    plat_file_close(f);
    uint32_t map = ((uint32_t)buf[12] << 24) | ((uint32_t)buf[13] << 16) | ((uint32_t)buf[14] << 8) | buf[15];
    size_t off = map + 0x18;
    *len = (size_t)size - off;
    uint8_t *pcm = malloc(*len);
    memcpy(pcm, buf + off, *len);
    free(buf);
    return pcm;
}

/* Expected output of one 8-bit sample: full volume (level 16), centre pan
 * (position 8: round(16 sin(pi/4)) = 11, i.e. volume 87), soft clip for 8
 * voices. */
static int expect(uint8_t s)
{
    int amp = (87 * 16 * (s - 128)) / 127;
    int n = 8;
    int j = amp >= 0 ? amp : -1 - amp;
    int64_t q = ((int64_t)j * n * 0xfffe) / (n * 0x7ff + j * (n - 1));
    int y = (int)((q + 1) / 2);
    return amp >= 0 ? y : -1 - y;
}

/* Compares out[start..] (both channels) against the PCM. Returns mismatches. */
static size_t compare(const int16_t *out, size_t out_frames, size_t start, const uint8_t *pcm, size_t n)
{
    size_t bad = 0;
    for (size_t i = 0; i < n && start + i < out_frames; i++) {
        int e = expect(pcm[i]);
        if (out[(start + i) * 2] != e || out[(start + i) * 2 + 1] != e) {
            if (bad < 5)
                printf("    frame %zu: got %d/%d, expected %d (src %u)\n", start + i, out[(start + i) * 2],
                       out[(start + i) * 2 + 1], e, pcm[i]);
            bad++;
        }
    }
    return bad;
}

static void stats(const char *what, const int16_t *s, size_t from, size_t to, double *rms_out)
{
    double sum = 0;
    int peak = 0;
    size_t clip = 0;
    for (size_t i = from * 2; i < to * 2; i++) {
        int v = s[i] < 0 ? -s[i] : s[i];
        if (v > peak)
            peak = v;
        if (v >= 32767)
            clip++;
        sum += (double)s[i] * s[i];
    }
    double rms = to > from ? sqrt(sum / (double)((to - from) * 2)) : 0;
    printf("    %-28s %6.2f-%6.2f s  rms %8.1f  peak %5d  clipped %zu\n", what, (double)from / RATE,
           (double)to / RATE, rms, peak, clip);
    if (rms_out)
        *rms_out = rms;
}

static void run_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        imuse_timer_handler();
        if (i % 6 == 0) /* snd_idle refills streams every 100 ms */
            imuse_process_streams();
    }
}

static int g_marker_count;
static char g_first_marker[64];
static void on_marker(const char *marker, const intptr_t args[10])
{
    if (g_marker_count++ == 0)
        snprintf(g_first_marker, sizeof(g_first_marker), "%s (arg %ld)", marker ? marker : "(null)", (long)args[0]);
}

/* The pan table at 0x6771a0 in ALIFE.EXE, via the PE section headers. */
static void check_pan_table(void)
{
    FILE *f = plat_fopen_data("ALIFE.EXE");
    if (!f) {
        printf("skip: ALIFE.EXE not found, pan table not compared\n");
        return;
    }
    uint8_t hdr[4096];
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    uint32_t pe = hdr[0x3c] | hdr[0x3d] << 8;
    if (n < sizeof(hdr) || pe > 3000) {
        fclose(f);
        return;
    }
    int nsec = hdr[pe + 6] | hdr[pe + 7] << 8;
    int opt = hdr[pe + 20] | hdr[pe + 21] << 8;
    uint32_t va = 0x6771a0 - 0x400000;
    long file_off = -1;
    for (int i = 0; i < nsec; i++) {
        const uint8_t *sh = hdr + pe + 24 + opt + i * 40;
        uint32_t sva = sh[12] | sh[13] << 8 | sh[14] << 16 | (uint32_t)sh[15] << 24;
        uint32_t ssz = sh[16] | sh[17] << 8 | sh[18] << 16 | (uint32_t)sh[19] << 24;
        uint32_t sraw = sh[20] | sh[21] << 8 | sh[22] << 16 | (uint32_t)sh[23] << 24;
        if (va >= sva && va < sva + ssz)
            file_off = (long)(sraw + (va - sva));
    }
    uint8_t tab[17 * 17];
    int ok = file_off >= 0 && fseek(f, file_off, SEEK_SET) == 0 && fread(tab, 1, sizeof(tab), f) == sizeof(tab);
    fclose(f);
    int diff = 0;
    for (int v = 0; ok && v < 17; v++)
        for (int p = 0; p < 17; p++)
            diff += tab[v * 17 + p] != imuse_mixer_pan_level(v, p);
    check(ok && diff == 0, "pan table = round(v sin(p pi/32)) matches ALIFE.EXE 0x6771a0");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s DATA_DIR OUT_DIR\n", argv[0]);
        return 2;
    }
    plat_set_data_dir(argv[1]);
    if (!rc_initialize("FULL.000", "ALIFE.000")) {
        fprintf(stderr, "can't index ALIFE.000 in %s\n", argv[1]);
        return 1;
    }
    rc_hook_compbun();
    compbun_init(1, 0);
    if (getenv("IMUSE_DEBUG"))
        imuse_set_debug(1);

    AlConfig cfg;
    cfg_set_defaults(&cfg);
    imuse_wave_set_sink(sink, NULL);
    if (snd_alloc_buffers(&cfg) != 0 || snd_start_imuse(&cfg) != 0) {
        fprintf(stderr, "iMUSE init failed\n");
        return 1;
    }
    check(imuse_wave_rate() == RATE, "output rate 22050");
    check_pan_table();
    char path[1024];

    /* --- 1: one effect ---------------------------------------------------- */
    size_t sfx_len, music_len, voice_len;
    uint8_t *sfx = load_pcm(SFX_ID, &sfx_len);
    uint8_t *music = load_pcm(MUSIC_ID, &music_len);
    uint8_t *voice = load_pcm(VOICE_ID, &voice_len);
    if (!sfx || !music || !voice) {
        fprintf(stderr, "test resources missing\n");
        return 1;
    }
    int slot = snd_play(SFX_ID, 64);
    check(slot == SFX_ID - SND_RES_BASE, "snd_play returns the slot");
    check(imuse_get_param(slot, IMUSE_P_PLAYING) == 1, "effect is playing");
    check(imuse_get_param(slot, IMUSE_P_GROUP) == IMUSE_GROUP_SFX, "effect is in group 1");
    int playing_ticks = 0;
    while (imuse_get_param(slot, IMUSE_P_PLAYING) > 0 && playing_ticks < 60 * 10) {
        run_ticks(1);
        playing_ticks++;
    }
    run_ticks(30);
    double secs = playing_ticks / 60.0, len_s = (double)sfx_len / RATE;
    printf("    effect: %zu samples (%.2f s), playing for %d ticks (%.2f s)\n", sfx_len, len_s, playing_ticks, secs);
    /* The mixer runs up to 8 blocks (0.37 s) ahead of the device. */
    check(secs > len_s - 0.45 && secs < len_s + 0.1, "effect ends after its length in real time");
    size_t bad = compare(g_out, g_out_frames, 0, sfx, sfx_len);
    check(bad == 0, "effect mix equals its PCM through the mixer tables, sample for sample");
    check(g_out_frames >= sfx_len, "output covers the whole effect");
    size_t tail_nonzero = 0;
    for (size_t i = sfx_len * 2; i < g_out_frames * 2; i++)
        tail_nonzero += g_out[i] != 0;
    check(tail_nonzero == 0, "silence after the effect");
    stats("effect", g_out, 0, sfx_len, NULL);
    snprintf(path, sizeof(path), "%s/imuse_sfx.wav", argv[2]);
    write_wav(path, g_out, g_out_frames);

    /* --- 2: music, effect, voice ------------------------------------------ */
    g_out_frames = 0;
    int mslot = snd_play(MUSIC_ID, 4);
    check(mslot == MUSIC_ID - SND_RES_BASE, "music starts");
    check(imuse_get_param(mslot, IMUSE_P_STREAMING) == 1, "music is streamed");
    check(imuse_get_param(mslot, IMUSE_P_STREAM_BUF) == SND_BUF_MUSIC, "music uses stream buffer 1");
    check(imuse_get_param(mslot, IMUSE_P_GROUP) == IMUSE_GROUP_MUSIC, "music is in group 4");
    imuse_process_streams();
    run_ticks(60 * 3);
    size_t music_only = g_out_frames;
    size_t start = 0;
    while (start < g_out_frames && g_out[start * 2] == 0)
        start++;
    size_t src0 = 0;
    while (src0 < music_len && music[src0] == 128)
        src0++;
    printf("    music: first sound at output frame %zu, source sample %zu\n", start, src0);
    check(start == src0, "music starts with the first mixed block");
    bad = compare(g_out, music_only, 0, music, music_len);
    check(bad == 0, "streamed music (3 s) equals its PCM through the mixer tables");
    double rms_music;
    stats("music alone", g_out, RATE / 2, music_only, &rms_music);

    snd_play(SFX_ID, 64);
    run_ticks(60 * 2);
    size_t sfx_end = g_out_frames;

    intptr_t args[10] = {VOICE_ID};
    check(snd_slot_stream(VOICE_ID, SND_VOICE_SLOT, 127, SND_BUF_VOICE) == 0, "voice stream starts");
    check(imuse_set_trigger(SND_VOICE_SLOT, NULL, IMUSE_OP_CALLBACK, on_marker, args) == 0, "trigger set");
    check(imuse_check_trigger(SND_VOICE_SLOT, NULL, -1) == 1, "trigger pending");
    check(imuse_get_param(SND_VOICE_SLOT, IMUSE_P_GROUP) == IMUSE_GROUP_VOICE, "voice is in group 2");
    size_t voice_start = g_out_frames;
    int vticks = 0;
    while (imuse_get_param(SND_VOICE_SLOT, IMUSE_P_PLAYING) > 0 && vticks < 60 * 15) {
        run_ticks(1);
        vticks++;
        if (vticks == 60)
            printf("    music group volume 1 s into the voice: %d\n", imuse_set_group_vol(IMUSE_GROUP_MUSIC, -1));
    }
    size_t voice_end = g_out_frames;
    printf("    voice: %zu samples (%.2f s), playing for %.2f s; markers fired %d, first %s\n", voice_len,
           (double)voice_len / RATE, vticks / 60.0, g_marker_count, g_first_marker);
    check(g_marker_count == 1, "trigger fired once (triggers are one-shot)");
    check(strncmp(g_first_marker, "MK", 2) == 0, "trigger got the lip-sync marker text");
    check(imuse_set_group_vol(IMUSE_GROUP_MUSIC, -1) == (127 * 5) >> 3 || vticks < 10,
          "music group ducked to 5/8 at the end of the voice");
    run_ticks(60);
    check(imuse_set_group_vol(IMUSE_GROUP_MUSIC, -1) == 127, "music group back to 127 after the voice");
    run_ticks(60);
    snd_stop(MUSIC_ID);
    check(imuse_get_param(mslot, IMUSE_P_PLAYING) == 0, "snd_stop stops the music");
    run_ticks(30);

    double rms_duck;
    stats("music + effect", g_out, music_only, sfx_end, NULL);
    stats("music (ducked) + voice", g_out, voice_start + RATE, voice_end, &rms_duck);
    stats("music after the voice", g_out, voice_end + RATE / 2, voice_end + RATE, NULL);
    double total = 0;
    stats("whole mix", g_out, 0, g_out_frames, &total);
    size_t clipped = 0;
    for (size_t i = 0; i < g_out_frames * 2; i++)
        clipped += g_out[i] >= 32767 || g_out[i] <= -32768;
    check(clipped == 0, "no clipped samples");
    check(total > 200, "mix is not silent");
    double expect_s = 3 + 2 + vticks / 60.0 + 2 + 0.5;
    double got_s = (double)g_out_frames / RATE;
    check(fabs(got_s - expect_s) < 0.5, "mix duration matches the ticks run (plus up to 0.37 s queued)");
    snprintf(path, sizeof(path), "%s/imuse_mix.wav", argv[2]);
    write_wav(path, g_out, g_out_frames);

    /* --- 3: JUMP loops, from memory and streamed ---------------------------- */
    for (int streamed = 0; streamed < 2; streamed++) {
        g_out_frames = 0;
        int ls = LOOP_ID - SND_RES_BASE;
        if (streamed) {
            snd_slot_stream(LOOP_ID, ls, 64, SND_BUF_VOICE);
            imuse_process_streams();
        } else {
            snd_play(LOOP_ID, 64);
        }
        run_ticks(60 * 5);
        int still = imuse_get_param(ls, IMUSE_P_PLAYING) > 0;
        check(still, streamed ? "streamed loop still plays after 5 s (3x its length)"
                              : "loop still plays after 5 s (3x its length)");
        /* longest run of exact silence after the start: a jump must not gap */
        size_t run = 0, longest = 0;
        for (size_t i = RATE / 4; i < g_out_frames; i++) {
            run = g_out[i * 2] == 0 ? run + 1 : 0;
            if (run > longest)
                longest = run;
        }
        printf("    longest silent run while looping: %zu frames\n", longest);
        check(longest < RATE / 10, "no gap at the jumps");
        imuse_set_hook(ls, 0x80);
        int t = 0;
        while (imuse_get_param(ls, IMUSE_P_PLAYING) > 0 && t < 60 * 5) {
            run_ticks(1);
            t++;
        }
        printf("    ended %.2f s after hook 0x80\n", t / 60.0);
        check(t < 60 * 2, "hook 0x80 lets the loop run out");
        if (streamed)
            snd_slot_stop_stream(ls);
        run_ticks(30);
        snprintf(path, sizeof(path), "%s/imuse_loop%s.wav", argv[2], streamed ? "_streamed" : "");
        write_wav(path, g_out, g_out_frames);
    }

    snd_shutdown_imuse();
    compbun_shutdown();
    rc_shutdown();
    free(sfx);
    free(music);
    free(voice);
    free(g_out);
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
