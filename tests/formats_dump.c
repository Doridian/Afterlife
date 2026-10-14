/*
 * Decodes every FLI/FLC and TILE resource of a .000 bundle through
 * src/formats/{flc,tile}.c, loading them with the game's resource layer.
 * Needs the user's game files; none are in the repo.
 *
 *   formats_dump <data dir> <bundle> [out dir]
 *
 * With an out dir it writes PNGs: <id>_fNNN.png for some animation frames
 * (the first, every 10th and the last) and <id>_tiles.png, a contact sheet
 * of each tile set (drawn with palette resource 33, magenta = transparent).
 * Exits non-zero if any resource fails to decode.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "formats/flc.h"
#include "formats/tile.h"
#include "game/resource.h"
#include "platform/platform.h"

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

/* Indexed 8-bit PNG. */
static int write_png(const char *path, const uint8_t *pix, int w, int h, int pitch, const PlatColor *pal)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    uint8_t plte[768];
    for (int i = 0; i < 256; i++)
        plte[i * 3] = pal[i].r, plte[i * 3 + 1] = pal[i].g, plte[i * 3 + 2] = pal[i].b;
    chunk(f, "PLTE", plte, 768);

    size_t raw_len = (size_t)(w + 1) * (size_t)h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len), *z = malloc(z_len);
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
    return fclose(f);
}

/* --- FLI/FLC ------------------------------------------------------------ */

static int dump_flc(uint32_t id, const uint8_t *data, size_t len, const char *out)
{
    FlcPlayer p;
    int r = flc_open(&p, data, len);
    if (r < 0) {
        fprintf(stderr, "%u: flc_open failed (%d)\n", (unsigned)id, r);
        return 1;
    }
    int w = p.hdr.width, h = p.hdr.height;
    uint8_t *fb = calloc((size_t)w * (size_t)h, 1);
    PlatColor pal[256] = {{0, 0, 0}};
    FlcTarget t = {fb, w, w, h, pal, 1000};
    int palettes = 0;
    /* frames + the ring frame */
    for (int i = 0; i <= p.hdr.frames; i++) {
        r = flc_next_frame(&p, &t);
        if (r < 0) {
            fprintf(stderr, "%u: frame %d failed (%d)\n", (unsigned)id, i, r);
            free(fb);
            return 1;
        }
        if (r & FLC_FRAME_PALETTE)
            palettes++;
        if (out && (i % 10 == 0 || i >= p.hdr.frames - 1)) {
            char path[4096];
            snprintf(path, sizeof path, "%s/%05u_f%03d.png", out, (unsigned)id, i);
            write_png(path, fb, w, h, w, pal);
        }
    }
    int frames = p.frame;
    /* Looping: FLC continues at frame 2, FLI rewinds to frame 1. */
    if (p.hdr.magic == FLC_MAGIC_FLC)
        flc_seek_ring_frame(&p);
    else
        flc_rewind(&p);
    r = flc_next_frame(&p, &t);
    printf("%u: %s %dx%d, %u frames + ring, %u ms/frame, %d palette chunks, loop %s\n", (unsigned)id,
           p.hdr.magic == FLC_MAGIC_FLC ? "FLC" : "FLI", w, h, (unsigned)p.hdr.frames, (unsigned)p.hdr.speed, palettes,
           r < 0 ? "FAILED" : "ok");
    free(fb);
    return r < 0 || frames != p.hdr.frames + 1;
}

/* --- TILE --------------------------------------------------------------- */

static int dump_tile(uint32_t id, const uint8_t *data, size_t len, const char *out, const PlatColor *pal)
{
    TileSet ts;
    int r = tile_set_open(&ts, data, len);
    if (r < 0) {
        fprintf(stderr, "%u: tile_set_open failed (%d)\n", (unsigned)id, r);
        return 1;
    }
    /* Lay the sprites out left to right in rows up to 1024 px wide. */
    enum { SHEET_W = 1024, GAP = 2 };
    int x = 0, y = 0, row_h = 0, failures = 0, named = 0;
    TileEntry *ents = calloc(ts.count ? ts.count : 1, sizeof *ents);
    int *pos = calloc(ts.count ? ts.count * 2u : 1, sizeof *pos);
    for (int i = 0; i < ts.count; i++) {
        r = tile_get_entry(&ts, i, &ents[i]);
        if (r < 0) {
            fprintf(stderr, "%u: entry %d: %d\n", (unsigned)id, i, r);
            failures++;
            continue;
        }
        if (tile_entry_text(&ents[i], 0))
            named++;
        int sw = tile_sprite_width(&ents[i]), sh = ents[i].height;
        if (x + sw > SHEET_W && x > 0) {
            x = 0;
            y += row_h + GAP;
            row_h = 0;
        }
        pos[i * 2] = x, pos[i * 2 + 1] = y;
        x += sw + GAP;
        if (sh > row_h)
            row_h = sh;
    }
    int sheet_h = y + row_h;
    uint8_t *sheet = malloc((size_t)SHEET_W * (size_t)(sheet_h ? sheet_h : 1));
    memset(sheet, 255, (size_t)SHEET_W * (size_t)(sheet_h ? sheet_h : 1));
    for (int i = 0; i < ts.count; i++) {
        if (!ents[i].base)
            continue;
        r = tile_draw_sprite(&ents[i], sheet, SHEET_W, SHEET_W, sheet_h, pos[i * 2], pos[i * 2 + 1]);
        if (r < 0) {
            fprintf(stderr, "%u: entry %d draw: %d\n", (unsigned)id, i, r);
            failures++;
        }
    }
    printf("%u: TILE %u entries (cell %ux%u), %d named, first: %s\n", (unsigned)id, (unsigned)ts.count,
           (unsigned)ts.cell_width, (unsigned)ts.cell_height, named,
           ts.count && ents[0].base && tile_entry_text(&ents[0], 0) ? tile_entry_text(&ents[0], 0) : "-");
    if (out && sheet_h > 0) {
        PlatColor p2[256];
        memcpy(p2, pal, sizeof p2);
        p2[255] = (PlatColor){255, 0, 255}; /* background */
        char path[4096];
        snprintf(path, sizeof path, "%s/%05u_tiles.png", out, (unsigned)id);
        write_png(path, sheet, SHEET_W, sheet_h, SHEET_W, p2);
    }
    free(sheet);
    free(ents);
    free(pos);
    return failures != 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s <data dir> <bundle> [out dir]\n", argv[0]);
        return 2;
    }
    const char *out = argc == 4 ? argv[3] : NULL;
    crc_init();
    plat_set_data_dir(argv[1]);
    if (!rc_initialize(argv[2], argv[2]))
        return 1;

    /* Game palette for the tile sheets: resource 33 holds 256 RGBX entries. */
    PlatColor pal[256];
    for (int i = 0; i < 256; i++)
        pal[i] = (PlatColor){(uint8_t)i, (uint8_t)i, (uint8_t)i};
    size_t plen = 0;
    uint8_t *praw = load_resource(33, &plen);
    if (praw && plen >= 1024)
        for (int i = 0; i < 256; i++)
            pal[i] = (PlatColor){praw[i * 4], praw[i * 4 + 1], praw[i * 4 + 2]};
    free(praw);

    const RcIndex *index = rc_local_index();
    int flcs = 0, tiles = 0, failures = 0;
    for (uint32_t i = 0; i < index->count; i++) {
        uint32_t id = index->ids[i];
        size_t len = 0;
        uint8_t *data = load_resource(id, &len);
        if (!data)
            continue;
        if (len >= 6 && data[5] == 0xaf && (data[4] == 0x11 || data[4] == 0x12)) {
            flcs++;
            failures += dump_flc(id, data, len, out);
        } else if (len >= 4 && memcmp(data, "TILE", 4) == 0) {
            tiles++;
            failures += dump_tile(id, data, len, out, pal);
        }
        free(data);
    }
    printf("%d FLI/FLC, %d TILE, %d failures\n", flcs, tiles, failures);
    rc_shutdown();
    return failures != 0;
}
