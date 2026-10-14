#include "bmp8.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int bmp8_blit(const uint8_t *data, size_t len, uint8_t *dst, int dst_w, int dst_h, PlatColor *palette, int *out_w,
              int *out_h)
{
    if (len < 54 || data[0] != 'B' || data[1] != 'M')
        return 0;
    uint32_t bits = rd32(data + 10), hdr = rd32(data + 14);
    int32_t w = (int32_t)rd32(data + 18), h = (int32_t)rd32(data + 22);
    if (rd16(data + 28) != 8 || rd32(data + 30) != 0 || w <= 0 || h == 0)
        return 0;
    int top_down = h < 0;
    if (top_down)
        h = -h;
    uint32_t colors = rd32(data + 46);
    if (colors == 0 || colors > 256)
        colors = 256;
    size_t stride = ((size_t)w + 3) & ~(size_t)3;
    if (14 + hdr + colors * 4 > len || bits + stride * (size_t)h > len)
        return 0;

    const uint8_t *pal = data + 14 + hdr;
    for (uint32_t i = 0; i < colors; i++)
        palette[i] = (PlatColor){pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4 + 0]};

    int cw = w < dst_w ? w : dst_w, ch = h < dst_h ? h : dst_h;
    for (int y = 0; y < ch; y++) {
        int src_row = top_down ? y : h - 1 - y;
        memcpy(dst + (size_t)y * dst_w, data + bits + (size_t)src_row * stride, (size_t)cw);
    }
    *out_w = w;
    *out_h = h;
    return 1;
}
