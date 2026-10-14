/*
 * AKOS costume resources: LucasArts' SCUMM v7-era animated costumes, as
 * ALIFE.EXE reads them (the AKOS module at 0x4d5c35-0x4d7a01 and the codec 1
 * renderer at 0x4dbee0-0x4de0cf). Afterlife uses them for the advisors
 * Jasper and Aria. See docs/formats/akos.md.
 *
 * This file is pure parsing: chunk lookup, header and cel table accessors,
 * and decoders that turn one cel into a plain bitmap (for tools and tests).
 * The animation interpreter and the renderer that draws into game surfaces
 * are in gfx/akos.c.
 *
 * Byte order is mixed, as in SCUMM: the chunk tags and lengths are
 * big-endian, everything inside the chunks is little-endian, and AKSQ
 * opcodes are read as big-endian 16-bit words.
 */
#ifndef AFTERLIFE_FORMATS_AKOS_H
#define AFTERLIFE_FORMATS_AKOS_H

#include <stddef.h>
#include <stdint.h>

enum {
    AKOS_OK = 0,
    AKOS_ERR_FORMAT = -1,    /* not an AKOS resource, or a required chunk is missing */
    AKOS_ERR_TRUNCATED = -2, /* a chunk or a cel's data runs past the end */
    AKOS_ERR_RANGE = -3,     /* cel or animation index out of range */
    AKOS_ERR_CODEC = -4,     /* unknown codec */
};

/* Cel codecs (AKHD +8). */
#define AKOS_CODEC_RLE 1  /* column-major RLE with 16/32/64 colours (0x4dd6bb) */
#define AKOS_CODEC_BOMP 5 /* row-major BOMP RLE; a no-op stub in ALIFE.EXE (0x4d7a01) */

/* AKHD +2 flag: draw unmirrored even in direction 0. */
#define AKOS_HDR_MIRROR 0x0001

typedef struct {
    const uint8_t *data; /* chunk body (after the 8-byte tag + length) */
    uint32_t len;        /* body length; 0 and data NULL when absent */
} AkosChunk;

typedef struct {
    const uint8_t *data; /* the whole resource, starting with "AKOS" */
    size_t len;
    /* AKHD, 12 bytes, little-endian */
    uint16_t version;   /* +0, 1 */
    uint16_t flags;     /* +2, bit 0 = AKOS_HDR_MIRROR */
    uint16_t num_anims; /* +4, AKCH entries: animation * 4 + direction */
    uint16_t unk6;      /* +6, unused by the game */
    uint16_t codec;     /* +8 */
    uint16_t unk10;     /* +10, unused (16 in every file) */
    AkosChunk akhd, akpl, rgbs, aksq, akch, akof, akci, akcd;
    AkosChunk akfo;    /* "cel goto" table for opcode 0xc088; absent in every Afterlife file */
    uint32_t num_cels; /* AKOF entries (6 bytes each) */
} AkosFile;

/* One cel: AKOF gives its data offset in AKCD and its AKCI offset. */
typedef struct {
    uint16_t width, height; /* AKCI +0, +2 */
    /* AKCI +4..+10, read by the single-cel draw path only. Most Afterlife
     * costumes have 4-byte AKCI entries (width, height), so these overlap
     * the next entry, as in the original (0 past the chunk end). */
    int16_t rel_x, rel_y, move_x, move_y;
    const uint8_t *data; /* AKCD data */
    size_t avail;        /* bytes from data to the end of AKCD */
} AkosCel;

/* Validates the chunk structure and the header. The data must stay valid
 * while the file is used. */
int akos_open(AkosFile *a, const uint8_t *data, size_t len);

/* Finds a chunk by its 4-character tag ("AKPL"), like res_find_block
 * (0x4dbc23) on the costume. */
int akos_find_chunk(const AkosFile *a, const char tag[4], AkosChunk *out);

/* AKOF/AKCI lookup for cel `cel` (0-based). */
int akos_get_cel(const AkosFile *a, unsigned cel, AkosCel *out);

/* Codec 1 colour depth from the palette size (0x4dd6bb): 32 entries -> 3
 * bits of run length, 64 -> 2, anything else -> 4 (16 colours). */
int akos_rle_shift(const AkosFile *a);

/* AKCH entry for animation index `index` (= animation * 4 + direction):
 * a pointer to its record (u16 limb mask, then per set bit a command byte
 * and, unless it is 1, 4 or 5, u16 start and u16 length into AKSQ), or NULL
 * if the entry is empty or out of range. *avail = bytes up to the chunk end. */
const uint8_t *akos_anim_record(const AkosFile *a, unsigned index, size_t *avail);

/* The colour index the codec uses as transparent: 0 for codec 1, 255 for
 * BOMP. Decoded pixels are indexes into AKPL / RGBS. */
int akos_transparent_index(const AkosFile *a);

/* Decodes a cel into out (width * height bytes, row-major, top-down).
 * Transparent pixels get akos_transparent_index(). Returns AKOS_OK or an
 * error if the data is short or the codec is unknown. Codec 5 decoding is
 * port-only: ALIFE.EXE never draws BOMP cels. */
int akos_decode_cel(const AkosFile *a, const AkosCel *cel, uint8_t *out);

#endif
