/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* EXE texts by address: see ui/exe_text.h. */
#include "ui/exe_text.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gfx/exe_res.h"

static const uint8_t *va_ptr(uint32_t va, uint32_t *avail)
{
    static bool tried;
    if (!tried)
        exe_res_open("ALIFE.EXE");
    tried = true;
    *avail = 0;
    return va ? exe_res_va(va, avail) : NULL;
}

const char *exe_str(uint32_t va)
{
    uint32_t avail;
    const uint8_t *p = va_ptr(va, &avail);
    if (!p || !memchr(p, 0, avail))
        return "";
    return (const char *)p;
}

uint8_t exe_u8(uint32_t va)
{
    uint32_t avail;
    const uint8_t *p = va_ptr(va, &avail);
    return p && avail >= 1 ? p[0] : 0;
}

uint32_t exe_u32(uint32_t va)
{
    uint32_t avail;
    const uint8_t *p = va_ptr(va, &avail);
    if (!p || avail < 4)
        return 0;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const char *exe_strp(uint32_t va) { return exe_str(exe_u32(va)); }

const char *exe_stri(uint32_t va, unsigned i) { return exe_strp(va + 4 * i); }

const char *exe_fmt(uint32_t va, const char *spec, const char *fallback)
{
    const char *f = exe_str(va);
    const char *s = spec;
    for (const char *p = f; *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (*p == '%')
            continue;
        while (*p && strchr("-+ #0123456789.", *p))
            p++;
        if (*s == 0 || *p != *s)
            return fallback;
        s++;
        if (!*p)
            break;
    }
    return *f && *s == 0 ? f : fallback;
}

const char *exe_buttons(uint32_t va, int n, char *buf, size_t size)
{
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        const char *t = exe_str(va);
        size_t l = strlen(t);
        if (l == 0 || o + l + 2 > size)
            break;
        memcpy(buf + o, t, l + 1);
        o += l + 1;
        va += (uint32_t)l + 1;
    }
    if (o + 1 > size)
        o = size - 1;
    buf[o] = 0;
    if (o == 0 && size > 1)
        buf[1] = 0;
    return buf;
}
