/*
 * ALIFE.INI reader. See ini.h.
 */
#include "game/ini.h"

#include <stdio.h>
#include <string.h>

#include "platform/platform.h"

static char g_ini_path[0x100];             /* 0x64d710 */
static char g_ini_value_buf[INI_VALUE_LEN]; /* 0x546dc0 */

/* 0x4c6ad6 */
void ini_set_path(const char *arg_ini_path)
{
    if (arg_ini_path && arg_ini_path[0]) {
        snprintf(g_ini_path, sizeof(g_ini_path), "%s", arg_ini_path);
    } else {
        /* _splitpath(exe_path, NULL, NULL, ini_path, NULL); strcat(".ini").
         * The port has no meaningful executable name, so use the original's. */
        snprintf(g_ini_path, sizeof(g_ini_path), "%s.ini", "ALIFE");
    }
}

const char *ini_path(void)
{
    return g_ini_path;
}

/* 0x4ce2e3 */
char *ini_get_string(char *buf, int size, const char *key)
{
    if (!key)
        return NULL;
    /* file_open(ini_path, 1): binary mode, so lines end in "\r\n". */
    FILE *f = plat_fopen_data(g_ini_path);
    if (!f)
        return NULL;
    if (!buf) {
        buf = g_ini_value_buf;
        if (size == 0 || size > INI_VALUE_LEN)
            size = INI_VALUE_LEN;
    }
    if (size < 1) {
        fclose(f);
        return NULL;
    }

    size_t key_len = strlen(key);
    while (fgets(buf, size, f)) { /* file_gets 0x4d2292 */
        if (strncmp(buf, key, key_len) != 0)
            continue;
        fclose(f);
        char *p = strchr(buf, '=');
        if (!p)
            return NULL;
        do
            p++;
        while (*p == ' ');
        memmove(buf, p, strlen(p) + 1); /* the original strcpy()s in place */
        char *end = strchr(buf, '\r');
        if (!end)
            end = strchr(buf, '\n');
        if (end)
            *end = '\0';
        return buf;
    }
    fclose(f);
    return NULL;
}

/* ini_set_string (0x4ce465), which rewrites one key through a temp file,
 * has no callers in ALIFE.EXE and is not ported. */
