/*
 * Behaviour test for the AKOS costume engine (src/formats/akos.c,
 * src/gfx/akos.c). Needs the user's game files; none are in the repo.
 *
 *   akos_test <data dir> <bundle> [out dir]
 *
 * For every AKOS resource in the bundle: parses it, decodes every cel,
 * walks every animation's AKCH record, and runs each animation through the
 * interpreter for a few steps while drawing it with the game palette.
 * With an out dir it writes PNGs:
 *   <id>_cels.png      every cel, decoded with the costume's own RGBS colours
 *   <id>_anims.png     the first frame of every animation (direction 2)
 *   <id>_aNNN_fK.png   frames of a few animations (the advisors' idle ones)
 *   advisors.png       Jasper and Aria as the advisor window draws them
 *   talker.png         the same through the talking-head module, with a subtitle
 * Also checks the interpreter on a synthetic costume. Exits non-zero on any
 * failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/akos.h"
#include "game/resource.h"
#include "gfx/akos.h"
#include "gfx/akos_talk.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/mouse.h"
#include "gfx/palette.h"
#include "gfx/surface.h"
#include "platform/platform.h"
#include "ui/wm.h"

static int g_failures;
static const char *g_out;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static uint8_t *load_resource(uint32_t id, size_t *len)
{
    long size = rc_resource_size(id);
    PlatFile *f = rc_open_resource(id);
    if (!f || size <= 0) {
        if (f)
            plat_file_close(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)size);
    if (buf && plat_file_read(f, buf, 1, (size_t)size) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    plat_file_close(f);
    *len = (size_t)size;
    return buf;
}

/* --- minimal PNG writer (stored deflate blocks) ------------------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    put32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    uint32_t c = crc_update(0xffffffffu, hdr + 4, 4);
    c = crc_update(c, data, len) ^ 0xffffffffu;
    put32(hdr, c);
    fwrite(hdr, 1, 4, f);
}

/* Indexed 8-bit PNG; pal is 256 x R,G,B. */
static void write_png(const char *name, const uint8_t *pix, int w, int h, int pitch, const uint8_t *pal)
{
    if (!g_out)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", g_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "can't write %s\n", path);
        g_failures++;
        return;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "PLTE", pal, 768);
    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    uint8_t *raw = malloc(raw_len), *z = malloc(2 + raw_len + blocks * 5 + 4);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (size_t)(w + 1)] = 0;
        memcpy(raw + (size_t)y * (size_t)(w + 1) + 1, pix + (size_t)y * (size_t)pitch, (size_t)w);
    }
    uint8_t *q = z;
    *q++ = 0x78, *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        *q++ = off + n == raw_len;
        *q++ = (uint8_t)n, *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n, *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521;
            b = (b + a) % 65521;
        }
        q += n;
        off += n;
    }
    put32(q, b << 16 | a);
    q += 4;
    chunk(f, "IDAT", z, (size_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    free(raw);
    free(z);
    fclose(f);
}

static uint8_t g_game_pal[768]; /* game palette as R,G,B for PNGs */
static uint8_t *g_rgbx;         /* palette resource 33 as R,G,B,x */
#define BG 255                  /* painted magenta in PNGs of game surfaces */

static void write_surface(const char *name, const GfxSurface *s)
{
    write_png(name, s->pixels, s->width, s->height, s->stride, g_game_pal);
}

/* --- per-costume checks -------------------------------------------------- */

/* Every cel decodes; a contact sheet of them in the costume's colours. */
static int check_cels(uint32_t id, const AkosFile *f)
{
    int bad = 0, sheet_w = 1024, x = 0, y = 0, row_h = 0;
    AkosCel *cels = calloc(f->num_cels ? f->num_cels : 1, sizeof *cels);
    int *pos = calloc(f->num_cels ? f->num_cels * 2u : 1, sizeof *pos);
    for (uint32_t i = 0; i < f->num_cels; i++) {
        if (akos_get_cel(f, i, &cels[i]) != AKOS_OK) {
            fprintf(stderr, "%u: cel %u: bad AKOF/AKCI entry\n", (unsigned)id, (unsigned)i);
            bad++;
            continue;
        }
        if (x + cels[i].width > sheet_w) {
            x = 0;
            y += row_h + 2;
            row_h = 0;
        }
        pos[i * 2] = x;
        pos[i * 2 + 1] = y;
        x += cels[i].width + 2;
        if (cels[i].height > row_h)
            row_h = cels[i].height;
    }
    int sheet_h = y + row_h;
    uint8_t *sheet = malloc((size_t)sheet_w * (size_t)(sheet_h ? sheet_h : 1));
    int tr = akos_transparent_index(f);
    memset(sheet, tr, (size_t)sheet_w * (size_t)(sheet_h ? sheet_h : 1));
    long pixels = 0;
    for (uint32_t i = 0; i < f->num_cels; i++) {
        const AkosCel *c = &cels[i];
        if (!c->data)
            continue;
        uint8_t *buf = malloc((size_t)c->width * c->height + 1);
        int r = akos_decode_cel(f, c, buf);
        if (r != AKOS_OK) {
            fprintf(stderr, "%u: cel %u (%ux%u): decode error %d\n", (unsigned)id, (unsigned)i, c->width, c->height, r);
            bad++;
        } else {
            for (int yy = 0; yy < c->height; yy++)
                memcpy(sheet + (size_t)(pos[i * 2 + 1] + yy) * sheet_w + pos[i * 2], buf + (size_t)yy * c->width,
                       c->width);
            pixels += (long)c->width * c->height;
        }
        free(buf);
    }
    uint8_t pal[768] = {0};
    for (uint32_t i = 0; i < 256 && i * 3 + 2 < f->rgbs.len; i++)
        memcpy(pal + i * 3, f->rgbs.data + i * 3, 3);
    pal[tr * 3] = 255, pal[tr * 3 + 1] = 0, pal[tr * 3 + 2] = 255;
    char name[64];
    snprintf(name, sizeof name, "%05u_cels.png", (unsigned)id);
    if (sheet_h)
        write_png(name, sheet, sheet_w, sheet_h, sheet_w, pal);
    printf("%u: %u cels decoded, %ld pixels, %d errors\n", (unsigned)id, (unsigned)f->num_cels, pixels, bad);
    free(sheet);
    free(cels);
    free(pos);
    return bad;
}

/* Animations: count the defined ones and render direction 2 of each. */
static int check_anims(uint32_t id, int slot, int cell_w, int cell_h, int px, int py)
{
    const AkosFile *f = akos_costume(slot);
    int anims = f->num_anims / 4, defined = 0, bad = 0, cols = 16;
    int rows = (anims + cols - 1) / cols;
    GfxSurface *sheet = gfx_surface_create(cell_w * cols, cell_h * (rows ? rows : 1));
    GfxSurface *cell = gfx_surface_create(cell_w, cell_h);
    akos_set_costume(1, slot);
    uint8_t rgbx[1024];
    gfx_palette_get(rgbx, 0, 256);
    akos_build_remap(1, rgbx, 0xff, 0xff, 0xff);
    for (int an = 0; an < anims; an++) {
        size_t avail;
        bool any = false;
        for (int d = 0; d < 4; d++) {
            const uint8_t *r = akos_anim_record(f, (unsigned)(an * 4 + d), &avail);
            if (!r)
                continue;
            any = true;
            /* the record must fit in AKCH, and its ranges in AKSQ */
            unsigned mask = (unsigned)(r[0] | r[1] << 8);
            size_t p = 2;
            for (int l = 0; l < 16; l++) {
                if (!(mask & (0x8000u >> l)))
                    continue;
                if (p >= avail) {
                    bad++;
                    break;
                }
                unsigned code = r[p++];
                if (code != 1 && code != 4 && code != 5) {
                    if (p + 4 > avail) {
                        bad++;
                        break;
                    }
                    unsigned start = (unsigned)(r[p] | r[p + 1] << 8), len = (unsigned)(r[p + 2] | r[p + 3] << 8);
                    if (start + len >= f->aksq.len) {
                        fprintf(stderr, "%u: anim %d dir %d limb %d: range %u+%u past AKSQ\n", (unsigned)id, an, d, l,
                                start, len);
                        bad++;
                    }
                    p += 4;
                }
            }
        }
        if (!any)
            continue;
        defined++;
        akos_reset(1);
        akos_set_anim(1, an, 2);
        gfx_fill_rect(cell, 0, 0, cell_w, cell_h, BG);
        akos_reset_bounds(1);
        akos_draw_actor(1, cell, px, py, 0xff, 0xff);
        gfx_blit(sheet, (an % cols) * cell_w, (an / cols) * cell_h, cell_w, cell_h, cell, 0, 0);
        for (int step = 0; step < 50; step++) /* exercise the interpreter */
            akos_increase_anims(1);
    }
    char name[64];
    snprintf(name, sizeof name, "%05u_anims.png", (unsigned)id);
    write_surface(name, sheet);
    printf("%u: %d of %d animations defined (x4 directions), codec %u, %u palette entries, %d bad records\n",
           (unsigned)id, defined, anims, f->codec, (unsigned)f->akpl.len, bad);
    gfx_surface_destroy(sheet);
    gfx_surface_destroy(cell);
    return bad;
}

/* A few frames of one animation, stepping the interpreter like the
 * talker does (one akos_increase_anims per frame). */
static void render_frames(uint32_t id, int slot, int anim, int frames, int w, int h, int px, int py)
{
    GfxSurface *s = gfx_surface_create(w * frames, h);
    GfxSurface *cell = gfx_surface_create(w, h);
    akos_set_costume(1, slot);
    akos_reset(1);
    akos_set_anim(1, anim, 2);
    int moved = 0;
    for (int i = 0; i < frames; i++) {
        gfx_fill_rect(cell, 0, 0, w, h, BG);
        akos_draw_actor(1, cell, px, py, 0xff, 0xff);
        gfx_blit(s, i * w, 0, w, h, cell, 0, 0);
        moved += akos_increase_anims(1);
    }
    char name[64];
    snprintf(name, sizeof name, "%05u_a%03d.png", (unsigned)id, anim);
    write_surface(name, s);
    printf("%u: anim %d, %d frames, moved on %d steps\n", (unsigned)id, anim, frames, moved);
    gfx_surface_destroy(s);
    gfx_surface_destroy(cell);
}

/* The advisor window (0x417230 / 0x417b94): bitmap 850 with the two face
 * boxes filled with colour 0x13, Jasper (2003) as actor 1 at (80, 139) and
 * Aria (2004) as actor 2 at (235, 139), both direction 2, animation 1. */
static void render_advisors(int jasper, int aria)
{
    GfxSurface *win = gfx_load_bmp_resource(850, NULL);
    if (!win) {
        fprintf(stderr, "no bitmap 850\n");
        g_failures++;
        return;
    }
    gfx_fill_rect(win, 0xe, 0x91, 0x11a, 0x46, 0x13);
    gfx_fill_rect(win, 0xe, 0xe, 0x8d, 0x7e, 0x13);
    gfx_fill_rect(win, 0xa1, 0xe, 0x8c, 0x7e, 0x13);
    uint8_t rgbx[1024];
    gfx_palette_get(rgbx, 0, 256);
    int slots[2] = {jasper, aria}, xs[2] = {0x50, 0xeb};
    for (int i = 0; i < 2; i++) {
        akos_set_costume(i + 1, slots[i]);
        akos_build_remap(i + 1, rgbx, 0xff, 0xff, 0xff);
        akos_reset(i + 1);
        akos_set_anim(i + 1, 1, 2);
        akos_reset_bounds(i + 1);
        akos_draw_actor(i + 1, win, xs[i], 0x8b, 0xff, 0xff);
        GfxRect b = akos_get_bounds(i + 1);
        printf("advisor %d: drawn in [%d..%d) x [%d..%d)\n", i + 1, b.left, b.right, b.top, b.bottom);
        CHECK(b.left >= 0xe && b.right <= 0xe + 0x8d + 0xa1 && b.top >= 0xe);
    }
    write_surface("advisors.png", win);
    gfx_surface_destroy(win);
}

/* --- synthetic costume: interpreter semantics ---------------------------- */

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static size_t add_chunk(uint8_t *buf, size_t pos, const char *tag, const uint8_t *data, size_t len)
{
    memcpy(buf + pos, tag, 4);
    be32(buf + pos + 4, (uint32_t)(len + 8));
    memcpy(buf + pos + 8, data, len);
    return pos + 8 + len;
}

static void check_synthetic(void)
{
    /* Animation 1 (index 4..7), limb 0: bytecode
     *   0: 00   cel 0 (the start position is skipped by the first step)
     *   1: c010 SetVar var3 = 5            (5 bytes)
     *   6: c020 cel 0 at (0, -2)           (8 bytes)
     *  14: c040 AddVar var3 += -1          (5 bytes)
     *  19: c071 JumpNE var3 != 0 -> 6      (7 bytes)
     *  26: 01   cel 1
     *  27: c080 StartAnim 2                (3 bytes)
     *  30: c030 Jump 26                    (4 bytes)
     * Limb 1: plain loop list (type 2) of cels 0, 1 at 34..35. */
    static const uint8_t aksq[] = {0x00, 0xc0, 0x10, 5,    0,    3,    0xc0, 0x20, 1,    0, 0, 0xfe,
                                   0xff, 0,    0xc0, 0x40, 0xff, 0xff, 3,    0xc0, 0x71, 6, 0, 3,
                                   0,    0,    0x01, 0xc0, 0x80, 2,    0xc0, 0x30, 26,   0, 0, 0x01};
    uint8_t akch[8 * 2 + 16] = {0};
    for (int d = 0; d < 4; d++)
        akch[(4 + d) * 2] = 16;                                               /* all four directions share one record */
    static const uint8_t rec[] = {0x00, 0xc0, 6, 0, 0, 0, 0, 2, 34, 0, 1, 0}; /* mask 0xc000 */
    memcpy(akch + 16, rec, sizeof rec);
    static const uint8_t akhd[12] = {1, 0, 0, 0, 8, 0, 0, 0, 1, 0, 16, 0};
    static const uint8_t akpl[16] = {0, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};
    uint8_t rgbs[48] = {0};
    static const uint8_t akof[12] = {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 4, 0};
    static const uint8_t akci[8] = {2, 0, 2, 0, 1, 0, 1, 0};
    /* cel 0: 2x2 of colour 1 (run 4); cel 1: 1x1 colour 2 */
    static const uint8_t akcd[2] = {0x14, 0x21};
    uint8_t *buf = calloc(1, 512);
    size_t p = 8;
    p = add_chunk(buf, p, "AKHD", akhd, sizeof akhd);
    p = add_chunk(buf, p, "AKPL", akpl, sizeof akpl);
    p = add_chunk(buf, p, "RGBS", rgbs, sizeof rgbs);
    p = add_chunk(buf, p, "AKSQ", aksq, sizeof aksq);
    p = add_chunk(buf, p, "AKCH", akch, sizeof akch);
    p = add_chunk(buf, p, "AKOF", akof, sizeof akof);
    p = add_chunk(buf, p, "AKCI", akci, sizeof akci);
    p = add_chunk(buf, p, "AKCD", akcd, sizeof akcd);
    memcpy(buf, "AKOS", 4);
    be32(buf + 4, (uint32_t)p);
    int slot = akos_costume_add(buf, p);
    CHECK(slot > 0);
    if (!slot)
        return;
    const int A = 3;
    akos_set_costume(A, slot);
    AkosActor *a = akos_actor(A);
    memset(a->remap, 0xff, sizeof a->remap); /* draw with the raw AKPL values */
    akos_set_anim(A, 1, 2);
    CHECK(a->active[0] == AKOS_LIMB_CODE && a->curpos[0] == 0);
    CHECK(a->active[1] == AKOS_LIMB_LOOP && a->curpos[1] == 34 && a->end[1] == 35);
    /* first step: skip cel 0, SetVar, stop on the c020 */
    CHECK(akos_increase_anims(A));
    CHECK(a->curpos[0] == 6 && akos_get_var(A, 3) == 5);
    CHECK(a->curpos[1] == 35);
    /* AddVar / JumpNE come back to the c020 until var3 reaches 0 */
    int steps = 0;
    while (a->curpos[0] == 6 && steps < 20) {
        akos_increase_anims(A);
        steps++;
    }
    CHECK(steps == 5 && a->curpos[0] == 26 && akos_get_var(A, 3) == 0);
    CHECK(a->curpos[1] == 34 || a->curpos[1] == 35);
    /* skip cel 1, StartAnim is queued, Jump back to 26 */
    int q0 = akos_queue_length();
    akos_increase_anims(A);
    CHECK(a->curpos[0] == 26);
    CHECK(akos_queue_length() == q0 + 1);
    CHECK(akos_queue_entry(q0) && akos_queue_entry(q0)->cmd == AKOS_QC_START_ANIM && akos_queue_entry(q0)->actor == A &&
          akos_queue_entry(q0)->a == 2);

    /* Drawing in direction 2 (mirrored: columns go right). Limb 0 shows
     * cel 1 (1x1, colour 2 -> AKPL 21), whose AKCI entry ends after
     * width/height, so its offsets are 0: at (3, 3). Limb 1 shows cel 0
     * (2x2, colour 1 -> AKPL 20); its 4-byte AKCI entry is followed by
     * cel 1's, so rel_x/rel_y read (1, 1): at (4..5, 4..5). */
    GfxSurface *s = gfx_surface_create(8, 8);
    akos_reset_bounds(A);
    a->curpos[1] = 34;
    akos_draw_actor(A, s, 3, 3, 0xff, 0xff);
    CHECK(s->pixels[3 * 8 + 3] == 21);
    CHECK(s->pixels[4 * 8 + 4] == 20 && s->pixels[5 * 8 + 5] == 20 && s->pixels[3 * 8 + 4] == 0);
    GfxRect b = akos_get_bounds(A);
    CHECK(b.left == 3 && b.top == 3 && b.right == 7 && b.bottom == 7);
    /* Direction 0 without the AKHD flag: unmirrored, columns go left from
     * x. Turning restarts both limbs. */
    memset(s->pixels, 0, 64);
    akos_set_direction(A, 0);
    CHECK(a->curpos[0] == 0 && a->curpos[1] == 34 && a->frame[0] == 4);
    a->curpos[0] = 6; /* c020: cel 0 at (0, -2) */
    a->active[1] = AKOS_LIMB_OFF;
    akos_draw_actor(A, s, 4, 4, 0xff, 0xff);
    CHECK(s->pixels[2 * 8 + 4] == 20 && s->pixels[3 * 8 + 3] == 20 && s->pixels[2 * 8 + 5] == 0);
    /* a stopped limb isn't drawn */
    memset(s->pixels, 0, 64);
    a->stopped = 1;
    akos_draw_actor(A, s, 4, 4, 0xff, 0xff);
    CHECK(s->pixels[2 * 8 + 4] == 0);
    gfx_surface_destroy(s);
    akos_process_queue();
    CHECK(akos_queue_length() == 0);
    akos_costume_free(slot);
}

/* --- the talking heads (gfx/akos_talk.c) --------------------------------- */

static int g_msg_marker = -1, g_msg_idle;

static intptr_t talk_proc(WmWindow *w, unsigned msg, intptr_t a, intptr_t b)
{
    if (msg == AKOS_TALK_MSG_MARKER && a == 1)
        g_msg_marker = (int)b;
    if (msg == AKOS_TALK_MSG_IDLE)
        g_msg_idle++;
    return wm_default_proc(w, msg, a, b);
}

/* Steps talker n now, whatever the time. */
static void talk_step(int n)
{
    AkosTalker *t = akos_talk(n);
    t->last_time = plat_ticks_ms() - t->period - 1;
    akos_talk_idle();
}

/* The advisor window as 0x417230 / 0x417b94 set it up, driven through the
 * talker module: idle steps, a subtitle, a lip-sync marker. */
static void check_talker(int jasper, int aria)
{
    if (!plat_init("akos_test", 640, 480) || !gfx_init(640, 480, false)) {
        fprintf(stderr, "no video\n");
        g_failures++;
        return;
    }
    gfx_mouse_init();
    gfx_font_init();
    gfx_palette_set(g_rgbx, GFX_PAL_FIRST, GFX_PAL_COUNT);
    CHECK(wm_initialize() == 0);
    GfxSurface *bmp = gfx_load_bmp_resource(850, NULL);
    CHECK(bmp != NULL);
    if (!bmp)
        return;
    GfxRect r = {16, 16 + bmp->width - 1, 16, 16 + bmp->height - 1};
    WmWindow *win = wm_create_window(NULL, &r, talk_proc, 0, 0);
    CHECK(win != NULL);
    if (!win)
        return;
    gfx_blit(win->surface, 0, 0, bmp->width, bmp->height, bmp, 0, 0);
    gfx_surface_destroy(bmp);
    gfx_fill_rect(win->surface, 0xe, 0x91, 0x11a, 0x46, 0x13);
    gfx_fill_rect(win->surface, 0xe, 0xe, 0x8d, 0x7e, 0x13);
    gfx_fill_rect(win->surface, 0xa1, 0xe, 0x8c, 0x7e, 0x13);

    akos_talk_initialize();
    CHECK(akos_talk_set_font(500) == 0);
    CHECK(akos_talk_open(4, win, jasper, 0xff, 0xff, 0xc, 0x13, 8) == -1); /* 0x46fc14's talker 4 */
    CHECK(akos_talk_open(1, win, jasper, 0xff, 0xff, 0xc, 0x13, 8) == 0);
    akos_talk_set_direction(1, 2);
    akos_talk_place(1, 0x50, 0x8b, win);
    akos_talk_set_anim(1, 1);
    CHECK(akos_talk_open(2, win, aria, 0xff, 0xff, 0xe, 0x13, 8) == 0);
    akos_talk_set_direction(2, 2);
    akos_talk_place(2, 0xeb, 0x8b, win);
    akos_talk_set_anim(2, 1);
    CHECK(akos_talk(1)->period == 125);

    /* the first step draws (drawn_anim was 0); a still head then sends
     * the idle message after 7 more steps */
    talk_step(1);
    GfxRect b = akos_get_bounds(1);
    CHECK(b.left == 14 && b.top == 22);
    for (int i = 0; i < 7; i++)
        talk_step(1);
    CHECK(g_msg_idle == 1);
    /* the window answers 6001 with an idle animation: Jasper's 0x29 moves */
    akos_talk_set_anim(1, 0x29);
    for (int i = 0; i < 3; i++)
        talk_step(1);
    /* a lip-sync marker is delivered to the window on the next idle */
    akos_talk(1)->marker = 12;
    akos_talk_idle();
    CHECK(g_msg_marker == 12 && akos_talk(1)->marker == -1);
    akos_talk_set_anim(1, 12 + 1);
    talk_step(1);
    talk_step(2);

    GfxRect text = {0x13, 0x104, 0x95, 0xd2};
    akos_talk_say(2, &text, "Hello. This is Aria, testing the subtitle area of the talking heads.", 0);
    CHECK(akos_talk(2)->text != NULL && akos_talk(2)->text_hold == 6);
    write_surface("talker.png", win->surface);
    akos_talk_clear_text(2);
    CHECK(akos_talk(2)->text == NULL);
    akos_talk_close(1);
    akos_talk_close(2);
    akos_talk_shutdown();
    wm_destroy(win);
    wm_shutdown();
    gfx_font_shutdown();
    gfx_shutdown();
    plat_shutdown();
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data dir> <bundle> [out dir]\n", argv[0]);
        return 2;
    }
    g_out = argc >= 4 ? argv[3] : NULL;
    crc_init();
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;

    size_t plen = 0;
    uint8_t *praw = load_resource(33, &plen);
    gfx_palette_init();
    uint8_t *rgbx = praw ? gfx_palette_load_resource(praw, plen) : NULL;
    if (!rgbx) {
        fprintf(stderr, "no palette resource 33\n");
        return 1;
    }
    g_rgbx = rgbx;
    gfx_palette_set(rgbx, GFX_PAL_FIRST, GFX_PAL_COUNT);
    gfx_palette_get(rgbx, 0, 256);
    for (int i = 0; i < 256; i++)
        memcpy(g_game_pal + i * 3, rgbx + i * 4, 3);
    g_game_pal[BG * 3] = 255, g_game_pal[BG * 3 + 1] = 0, g_game_pal[BG * 3 + 2] = 255;
    akos_initialize();

    check_synthetic();

    const RcIndex *ix = rc_cd_index();
    int costumes = 0, jasper = 0, aria = 0;
    for (uint32_t i = 0; i < ix->count; i++) {
        uint32_t id = ix->ids[i];
        size_t len = 0;
        if (rc_resource_size(id) < 8)
            continue;
        PlatFile *pf = rc_open_resource(id);
        char tag[4] = {0};
        if (pf) {
            plat_file_read(pf, tag, 1, 4);
            plat_file_close(pf);
        }
        if (memcmp(tag, "AKOS", 4) != 0)
            continue;
        uint8_t *data = load_resource(id, &len);
        AkosFile f;
        int r = data ? akos_open(&f, data, len) : AKOS_ERR_FORMAT;
        if (r != AKOS_OK) {
            fprintf(stderr, "%u: akos_open failed (%d)\n", (unsigned)id, r);
            g_failures++;
            free(data);
            continue;
        }
        costumes++;
        printf("%u: %zu bytes, AKHD version %u flags 0x%04x anims %u unk %u codec %u, AKPL %u, %u cels%s\n",
               (unsigned)id, len, f.version, f.flags, f.num_anims, f.unk6, f.codec, (unsigned)f.akpl.len,
               (unsigned)f.num_cels, f.akfo.data ? ", AKFO" : "");
        g_failures += check_cels(id, &f);
        int slot = akos_costume_add(data, len);
        CHECK(slot > 0);
        if (!slot)
            continue;
        /* cell size and actor position per costume (the advisors' cels sit
         * up and left of the actor position) */
        int cw = 320, ch = 320, px = 160, py = 240;
        if (id == 2002)
            cw = 48, ch = 64, px = 24, py = 56;
        else if (id == 2003 || id == 2004)
            cw = 160, ch = 140, px = 80, py = 130;
        g_failures += check_anims(id, slot, cw, ch, px, py);
        if (id == 2003) {
            jasper = slot;
            render_frames(id, slot, 0x29, 8, cw, ch, px, py);
            render_frames(id, slot, 0x2c, 8, cw, ch, px, py);
            render_frames(id, slot, 6, 4, cw, ch, px, py);
        } else if (id == 2004) {
            aria = slot;
            render_frames(id, slot, 0x2d, 8, cw, ch, px, py);
            render_frames(id, slot, 0x3d, 8, cw, ch, px, py);
        } else if (id == 2002) {
            render_frames(id, slot, 2, 12, cw, ch, px, py);
        }
    }
    if (jasper && aria) {
        render_advisors(jasper, aria);
        check_talker(jasper, aria);
    }
    CHECK(costumes > 0);
    akos_shutdown();
    free(rgbx);
    free(praw);
    rc_shutdown();
    printf("%d costumes, %s\n", costumes, g_failures ? "FAILED" : "OK");
    return g_failures ? 1 : 0;
}
