/*
 * Dumps every resource of a .000 bundle through the decompiled stack
 * (game resource layer at the bottom of compbun, Compression Layer on top),
 * the way the game's sound code reads resources. tests/compare_jiff.py checks
 * the output against tools/jiff.py. Needs the user's game files; none are in
 * the repo.
 *
 *   compbun_dump <data dir> <bundle> <out dir>
 *
 * Writes <out dir>/<index>_<id>.bin for each index entry, in index order.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compbun/cbres.h"
#include "game/resource.h"
#include "platform/platform.h"

static uint8_t *read_all(uint32_t id, long size, long chunk)
{
    char name[RC_NAME_LEN];
    rc_resource_name(id, name);
    CbHandle h = cbres_open(name, "rb");
    if (!h)
        return NULL;
    uint8_t *buf = malloc(size > 0 ? (size_t)size : 1);
    for (long done = 0; buf && done < size; done += chunk) {
        long n = size - done < chunk ? size - done : chunk;
        cbres_read(buf + done, 1, n, h);
    }
    cbres_close(h);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <data dir> <bundle> <out dir>\n", argv[0]);
        return 2;
    }
    plat_set_data_dir(argv[1]);
    /* Same bundle as local and CD, so every lookup hits it. */
    if (!rc_initialize(argv[2], argv[2]))
        return 1;
    rc_hook_compbun();
    compbun_init(1, 0);

    const RcIndex *index = rc_local_index();
    int failures = 0;
    for (uint32_t i = 0; i < index->count; i++) {
        uint32_t id = index->ids[i];
        long size = rc_resource_size(id);
        uint8_t *whole = read_all(id, size, size > 0 ? size : 1);
        uint8_t *pieces = read_all(id, size, 4097); /* sequential reads */
        if (!whole || !pieces || memcmp(whole, pieces, (size_t)size) != 0) {
            fprintf(stderr, "index %u id %u: read failed or chunked read differs\n", (unsigned)i, (unsigned)id);
            failures++;
        } else {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%04u_%u.bin", argv[3], (unsigned)i, (unsigned)id);
            FILE *out = fopen(path, "wb");
            if (!out || fwrite(whole, 1, (size_t)size, out) != (size_t)size) {
                fprintf(stderr, "%s: write failed\n", path);
                failures++;
            }
            if (out)
                fclose(out);
        }
        free(whole);
        free(pieces);
    }
    printf("%u resources dumped, %d failures\n", (unsigned)index->count, failures);
    compbun_shutdown();
    rc_shutdown();
    return failures != 0;
}
