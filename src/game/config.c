/*
 * The INI-reading parts of the init functions. See config.h.
 */
#include "game/config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/ini.h"

AlConfig g_config;

static void str_upper(char *s) /* _strupr */
{
    for (; *s; s++)
        *s = (char)toupper((unsigned char)*s);
}

void cfg_set_defaults(AlConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->heap_size = 0x400000;
    cfg->sound_enabled = true;
    cfg->music_stream[0] = 0x32000; /* 204800 */
    cfg->music_stream[1] = 0x25800; /* 153600 */
    cfg->music_stream[2] = 0x8000;  /* 32768 */
    cfg->voice_stream[0] = 0x10400; /* 66560 */
    cfg->voice_stream[1] = 0xc000;  /* 49152 */
    cfg->voice_stream[2] = 0x5800;  /* 22528 */
    cfg->mixer_channels = 8;
    cfg->resolution_max = true;
    cfg->display_mode = DI_MODE_FULLSCREEN;
}

/* 0x4c98bb (INI part) */
void cfg_read_heap(AlConfig *cfg)
{
    const char *v = ini_get_string(NULL, 0, "Heap size");
    if (v)
        cfg->heap_size = (uint32_t)atoi(v) << 10;
}

/* 0x4cd9b0 (INI part) */
int cfg_read_resource_paths(AlConfig *cfg)
{
    int err = 0;
    if (!ini_get_string(cfg->cd_path, sizeof(cfg->cd_path), "CD Path"))
        err = 1;
    if (!ini_get_string(cfg->resource_file, sizeof(cfg->resource_file), "Resource file") && !err)
        err = 2;
    return err;
}

/* 0x4ca1c0 (INI part) */
void cfg_read_display(AlConfig *cfg, int desktop_w, int desktop_h, int desktop_bpp,
                      bool *depth_mismatch)
{
    char *v;

    *depth_mismatch = false;
    cfg->has_color_depth = false;
    v = ini_get_string(NULL, 0, "Color depth");
    if (v) {
        str_upper(v); /* the original uppercases the value in place */
        if (strcmp(v, "DONT CARE") != 0) {
            cfg->has_color_depth = true;
            cfg->color_depth = atoi(v);
            /* desktop BITSPIXEL * PLANES compared with the setting */
            *depth_mismatch = desktop_bpp != cfg->color_depth;
        }
    }

    cfg->display_mode = DI_MODE_FULLSCREEN;
    v = ini_get_string(NULL, 0, "Force display settings");
    cfg->force_display = v && strcmp(v, "ON") == 0;
    if (cfg->force_display)
        cfg->display_mode |= DI_MODE_FORCE;

    /* The original keeps these unsigned, so a negative value clamps to the
     * desktop size below. sscanf leaves an unparsed field at the default. */
    int w = desktop_w, h = desktop_h;
    v = ini_get_string(NULL, 0, "Resolution");
    cfg->resolution_max = !v || strcmp(v, "MAX") == 0;
    if (!cfg->resolution_max)
        sscanf(v, "%dx%d", &w, &h);
    cfg->req_width = w;
    cfg->req_height = h;
    if (w < 0 || w > desktop_w)
        w = desktop_w;
    if (h < 0 || h > desktop_h)
        h = desktop_h;
    cfg->width = w;
    cfg->height = h;
    if (!cfg->force_display && (w < desktop_w || h < desktop_h))
        cfg->display_mode = DI_MODE_WINDOWED;

    /* No check: when the key is missing the buffer holds the INI's last line,
     * as in the original. */
    ini_get_string(cfg->window_name, sizeof(cfg->window_name), "Window name");
}

/* 0x4b9510 (INI part) */
void cfg_read_sound(AlConfig *cfg)
{
    char *v = ini_get_string(NULL, 0, "Sound");
    if (v) {
        str_upper(v);
        if (strcmp(v, "NONE") == 0) {
            cfg->sound_enabled = false;
            return; /* the original returns before reading the other keys */
        }
    }
    cfg->sound_enabled = true;
    v = ini_get_string(NULL, 0, "Music stream");
    if (v)
        sscanf(v, "%d, %d, %d", &cfg->music_stream[0], &cfg->music_stream[1], &cfg->music_stream[2]);
    v = ini_get_string(NULL, 0, "Voice stream");
    if (v)
        sscanf(v, "%d, %d, %d", &cfg->voice_stream[0], &cfg->voice_stream[1], &cfg->voice_stream[2]);
    cfg->mixer_channels = 8;
    v = ini_get_string(NULL, 0, "Mixer channels");
    if (v)
        cfg->mixer_channels = atoi(v);
}

/* 0x4805d8 cfg_error(0, NULL): an in-game "ERROR" box in the original. */
static void cfg_missing(const char *key)
{
    fprintf(stderr, "*** ERROR ***: Missing INI profile string:  \"%s\"\n", key);
}

/* 0x480375. The cache is filled on first use. Quirk kept: a miss leaves the
 * INI's last line in the cache, so the next call returns that. */
bool cfg_save_games_dir(char *out)
{
    static char cache[0x800]; /* 0x66aef8 */
    if (cache[0] == '\0' && !ini_get_string(cache, sizeof(cache), "Save games")) {
        cfg_missing("Save games");
        return false;
    }
    strcpy(out, cache);
    return true;
}

/* 0x4803dc */
bool cfg_cd_dir(char *out)
{
    static char cache[0x800]; /* 0x66b6f8 */
    if (cache[0] == '\0') {
        if (!ini_get_string(cache, sizeof(cache), "CD Path")) {
            cfg_missing("CD Path"); /* the original reports "Save games" here */
            return false;
        }
        /* _splitpath + _makepath(drive, dir): keep up to the last separator. */
        char *sep = NULL;
        for (char *p = cache; *p; p++)
            if (*p == '\\' || *p == '/' || (p == cache + 1 && *p == ':'))
                sep = p;
        if (sep)
            sep[1] = '\0';
        else
            cache[0] = '\0';
        size_t n = strlen(cache);
        if (n > 0 && cache[n - 1] == '\\')
            cache[n - 1] = '\0';
    }
    strcpy(out, cache);
    return true;
}

/* 0x4ccf6e (INI part) */
void cfg_savepath(char *out)
{
    out[0] = '\0';
    ini_get_string(out, 0x400, "Savepath"); /* unchecked, see ini_get_string */
    char *sp = strchr(out, ' ');
    if (sp)
        *sp = '\0';
    size_t n = strlen(out);
    if (n == 0 || out[n - 1] != '\\')
        strcat(out, "\\");
}

/* 0x42a6c3 (INI part) */
bool cfg_video_settings(int *flags)
{
    char buf[0x50];
    if (!ini_get_string(buf, sizeof(buf), "Video Settings"))
        return false;
    *flags = (unsigned char)atoi(buf); /* stored in a byte */
    return true;
}
