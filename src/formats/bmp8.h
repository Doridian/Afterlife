#ifndef AFTERLIFE_BMP8_H
#define AFTERLIFE_BMP8_H

#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

/* Decodes an uncompressed 8-bit Windows BMP (the only kind in the bundles)
 * into an indexed framebuffer, clipped to dst_w x dst_h. Fills palette[256].
 * Returns 0 on unsupported input. */
int bmp8_blit(const uint8_t *data, size_t len, uint8_t *dst, int dst_w, int dst_h, PlatColor *palette, int *out_w,
              int *out_h);

#endif
