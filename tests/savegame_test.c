/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * savegame_test <save.ASC|.ALF> ...
 *
 * Loads each ALSV file (src/game/savegame.c), prints a summary and some
 * sanity checks, then saves it again in its own version and compares the
 * result with the original byte for byte.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/savegame.h"

static int g_failures;

#define CHECK(cond, ...)                          \
    do {                                          \
        if (!(cond)) {                            \
            printf("  FAIL: " __VA_ARGS__);       \
            printf("\n");                         \
            g_failures++;                         \
        }                                         \
    } while (0)

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

static void summarize(const char *path, GameState *g)
{
    const SaveHeader *h = &g->hdr;
    SaveTimestamp ts = savegame_decode_timestamp(h->timestamp);
    printf("%s: ALSV %d \"%s\"\n", path, h->version, h->title);
    printf("  saved %04d-%02d-%02d %02d:%02d:%02d, kind %u, thumbnail %ux%u\n", ts.year, ts.month, ts.day, ts.hour,
           ts.minute, ts.second, h->kind, (unsigned)h->thumb_w, (unsigned)h->thumb_h);
    printf("  year %" PRIu32 ", pennies %" PRId64 ", EMBOs %" PRId64 ", lost SOULs %" PRIu32 ", sim phase %u\n", g->year,
           gs_pennies(g), gs_embos(g), g->lost_souls, g->sim_phase);
    for (int pl = 1; pl <= 3; pl += 2) {
        const MapPlaneState *p = &g->plane[pl];
        int sets[32] = {0};
        for (int i = 0; i < GS_MAP_CELLS; i++)
            sets[p->cells[i] >> 27]++;
        int64_t extra = bignum_value(p->stats.souls);
        printf("  %s: population %" PRIu32 ", souls %" PRId64 ", cells by set:", pl == 1 ? "Hell  " : "Heaven",
               p->stats.population, extra);
        for (int i = 0; i < 32; i++)
            if (sets[i])
                printf(" %d:%d", i, sets[i]);
        printf("\n");
    }
    int objs = 0;
    for (int i = 0; i < GS_MAP_OBJECTS; i++)
        objs += g->objects[i].plane != 0;
    printf("  map objects in use %d, view dir %d,%d, bad things %" PRIu32 "\n", objs, g->view_dir_x, g->view_dir_y,
           g->bad_things);
    printf("  scenario: \"%s\" / button \"%s\"\n", g->scenario_title, g->scenario_button);
    printf("  description: \"%.200s\"\n", g->scenario_description);
    printf("  commands: \"%s\"\n", g->scenario_commands);
    printf("  planet map %" PRIu32 " bytes, lists %u/%u/%u\n", g->planet.map_size, g->planet.n_a, g->planet.n_b,
           g->planet.n_c);

    CHECK(h->year == g->year, "header year %u != body year %u", (unsigned)h->year, (unsigned)g->year);
    CHECK(g->year < 100000, "implausible year");
    CHECK(h->title[0] != 0, "empty title");
    CHECK(h->thumb_w == 104 && h->thumb_h == 78, "thumbnail not 104x78");
    CHECK(g->view_dir_x == 1 || g->view_dir_x == -1, "view dir x %d", g->view_dir_x);
    CHECK(g->view_dir_y == 1 || g->view_dir_y == -1, "view dir y %d", g->view_dir_y);
    CHECK(gs_pennies(g) > -1000000000LL && gs_pennies(g) < 1000000000000LL, "implausible pennies");
    CHECK(ts.month >= 1 && ts.month <= 12 && ts.day >= 1 && ts.hour < 24 && ts.minute < 60 && ts.second < 60,
          "bad timestamp");
    for (int pl = 1; pl <= 3; pl++)
        for (int i = 0; i < GS_MAP_CELLS; i++)
            if (g->plane[pl].cells[i] & 0x80u) {
                CHECK(0, "plane %d cell %d keeps CELL_HILITE", pl, i);
                break;
            }
    for (int i = 0; i < GS_MAP_OBJECTS; i++)
        if (g->objects[i].plane > 3 && g->objects[i].plane != 99) {
            CHECK(0, "map object %d on plane %u", i, g->objects[i].plane);
            break;
        }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <save.ASC> ...\n", argv[0]);
        return 2;
    }
    GameState *g = calloc(1, sizeof *g);
    if (!g)
        return 1;
    for (int a = 1; a < argc; a++) {
        size_t len;
        uint8_t *data = read_file(argv[a], &len);
        if (!data) {
            printf("%s: can't read\n", argv[a]);
            g_failures++;
            continue;
        }
        char err[160];
        if (!savegame_load(g, argv[a], err, sizeof err)) {
            printf("%s: load failed: %s\n", argv[a], err);
            g_failures++;
            free(data);
            continue;
        }
        summarize(argv[a], g);

        size_t out_len = 0;
        uint8_t *out = savegame_save_mem(g, &out_len);
        if (!out) {
            CHECK(0, "save failed");
        } else if (out_len != len || memcmp(out, data, len) != 0) {
            size_t i = 0;
            while (i < len && i < out_len && out[i] == data[i])
                i++;
            CHECK(0, "round trip differs at offset %zu (sizes %zu / %zu)", i, out_len, len);
        } else {
            printf("  round trip: %zu bytes identical\n", len);
        }

        /* the original's own format: save as 7300, load back */
        if (out) {
            GameState *g2 = calloc(1, sizeof *g2);
            g->hdr.version = ALSV_VERSION_CURRENT;
            size_t n2;
            uint8_t *b2 = savegame_save_mem(g, &n2);
            CHECK(b2 && savegame_load_mem(g2, b2, n2, err, sizeof err), "7300 re-load failed");
            if (b2)
                CHECK(g2->year == g->year && gs_pennies(g2) == gs_pennies(g) &&
                          memcmp(g2->plane[3].cells, g->plane[3].cells, sizeof g->plane[3].cells) == 0,
                      "7300 re-load differs");
            if (b2)
                printf("  as version %d: %zu bytes, reloads\n", ALSV_VERSION_CURRENT, n2);
            savegame_free(g2);
            free(g2);
            free(b2);
        }
        free(out);
        free(data);
        savegame_free(g);
    }
    free(g);
    if (g_failures)
        printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
