/*
 * Autodesk FLI/FLC player, decompiled from ALIFE.EXE (0x409dc0-0x40b1a1,
 * the flc_* symbols). See docs/formats/flc.md.
 *
 * The original reads a resource either frame by frame from the bundle
 * ("streamed", frames limited to 51000 bytes) or whole into memory. The port
 * always works on a caller-supplied in-memory copy of the resource; the
 * decoders write into a caller-supplied 8-bit buffer and a palette array, so
 * this doesn't depend on the video layer.
 */
#ifndef AFTERLIFE_FLC_H
#define AFTERLIFE_FLC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

#define FLC_MAGIC_FLI 0xAF11
#define FLC_MAGIC_FLC 0xAF12
#define FLC_FRAME_TYPE 0xF1FA
#define FLC_HEADER_SIZE 128
#define FLC_FRAME_HEADER_SIZE 16
/* Size of the original's static frame buffer (flc_frame_buf, 0x639b90):
 * the limit for one frame's chunk data when streaming from the bundle. */
#define FLC_STREAM_FRAME_MAX 51000

/* Chunk types (frame chunk header: u32 size, u16 type). */
enum {
    FLC_CHUNK_COLOR_256 = 4,
    FLC_CHUNK_DELTA_FLC = 7, /* "SS2" word-oriented delta */
    FLC_CHUNK_COLOR_64 = 11,
    FLC_CHUNK_DELTA_FLI = 12, /* "LC" byte-oriented delta */
    FLC_CHUNK_BLACK = 13,
    FLC_CHUNK_BYTE_RUN = 15, /* "BRUN" full-frame RLE */
    FLC_CHUNK_COPY = 16,     /* "FLI_COPY" raw frame */
};

/* Original error codes. */
enum {
    FLC_OK = 0,
    FLC_ERR_TOO_BIG = -2,    /* streamed frame larger than FLC_STREAM_FRAME_MAX */
    FLC_ERR_BAD_MAGIC = -3,  /* not 0xAF11/0xAF12 */
    FLC_ERR_BAD_FRAME = -4,  /* frame header type isn't 0xF1FA */
    FLC_ERR_NOT_FOUND = -5,  /* resource can't be opened */
    FLC_ERR_TRUNCATED = -6,  /* port-only: data runs past the end of the resource */
};

/* The 128-byte file header, fields the player uses. */
typedef struct {
    uint32_t size;
    uint16_t magic;
    uint16_t frames; /* not counting the ring frame */
    uint16_t width, height, depth, flags;
    uint32_t speed;   /* ms per frame after flc_open (FLI's 1/70 s is converted) */
    uint32_t oframe1; /* file offset of frame 1 (forced to 128 for FLI) */
    uint32_t oframe2; /* file offset of frame 2 (FLC only; used for looping) */
} FlcHeader;

/* What the original draws into (flc_target_init, 0x40b0c9): a pixel buffer,
 * its pitch and clip size. The palette is the port's addition: COLOR_*
 * chunks write the 8-bit RGB entries there. */
typedef struct {
    uint8_t *pixels;
    int pitch;
    int width, height;  /* clip rectangle is (0,0)-(width-1,height-1) */
    PlatColor *palette; /* 256 entries, may be NULL */
    int speed_scale;    /* permille applied to the frame delay; 1000 = normal */
} FlcTarget;

/* Player state, the original's 0xaa-byte struct minus the file handle. */
typedef struct {
    FlcHeader hdr;
    const uint8_t *data;
    size_t len;
    size_t pos;   /* +0x8c: offset of the next frame header */
    int frame;    /* +0x95: frames decoded since the last rewind */
    int x, y;     /* +0x9d/+0xa1: where frame (0,0) lands in the target */
    bool playing; /* +0xa9: cleared to stop flc_play */
} FlcPlayer;

/* flc_next_frame result flag: a COLOR chunk changed the palette. */
#define FLC_FRAME_PALETTE 1

/* 0x409eb9 / 0x409dc0: parse the header and position at frame 1. `data`
 * must stay valid while the player is in use. Returns FLC_OK or an error. */
int flc_open(FlcPlayer *p, const uint8_t *data, size_t len);
/* 0x40a030 / 0x40a047 */
void flc_close(FlcPlayer *p);
/* 0x40a145: back to frame 1. */
void flc_rewind(FlcPlayer *p);
/* 0x40a1cf: to frame 2 (FLC only), skipping frame 1 when looping through
 * the ring frame. */
void flc_seek_ring_frame(FlcPlayer *p);
/* 0x40b134: centre the animation in the target. */
void flc_center(FlcPlayer *p, const FlcTarget *t);
/* 0x40a376: decode the next frame into the target. Returns a negative error,
 * or FLC_OK possibly or'ed with FLC_FRAME_PALETTE. */
int flc_next_frame(FlcPlayer *p, const FlcTarget *t);
/* The wait flc_play does after each frame, in ms. */
uint32_t flc_frame_delay_ms(const FlcPlayer *p, const FlcTarget *t);

/* Called by flc_play after each frame with the dirty rectangle (the
 * original blits it to the window, 0x4c25c4). `result` is what
 * flc_next_frame returned. */
typedef void (*FlcPresentFn)(void *ctx, const FlcTarget *t, int x0, int y0, int x1, int y1, int result);
/* 0x40a27f: play hdr.frames frames with timing. Stops early on an error or
 * when the callback clears p->playing. Returns the last frame result. */
int flc_play(FlcPlayer *p, const FlcTarget *t, FlcPresentFn present, void *ctx);

/* 0x40a5ad: decode one frame's chunk data (after the 16-byte frame header).
 * Exposed for tests. */
int flc_decode_chunks(FlcPlayer *p, const uint8_t *chunks, size_t len, int nchunks, const FlcTarget *t);

#endif
