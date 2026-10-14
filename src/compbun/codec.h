/*
 * Codec manager (compbun:source:codecmgr.c, "C:\COMPBUN\source\CODECMGR.C").
 *
 * A codec is a proc(cmd, dst, src, size) registered in codecList[15] under the
 * id it reports for CODEC_CMD_ID. The compression layer (cmprsmgr.c) calls
 * codec_decompress() once per block of a 'COMP' file; see docs/formats/comp.md
 * for the container and the algorithms.
 *
 * All codecs are LZSS (Okumura-style, 4 KiB window) plus an optional
 * byte-delta and 12-bit sample reordering stage, made for iMUSE audio.
 */
#ifndef AFTERLIFE_COMPBUN_CODEC_H
#define AFTERLIFE_COMPBUN_CODEC_H

#include <stddef.h>
#include <stdint.h>

/* Codec ids (index into codecList[]). The descriptive names are ours. */
enum {
    CODEC_RAW = 0x0,           /* stored, memcpy */
    CODEC_LZ = 0x1,            /* LZSS */
    CODEC_LZ_DELTA1 = 0x2,     /* LZSS, then 1st-order byte delta */
    CODEC_LZ_DELTA2 = 0x3,     /* LZSS, then 2nd-order byte delta */
    CODEC_LZ_12MONO_0 = 0x4,   /* 12-bit mono samples, block starts on a 3-byte frame */
    CODEC_LZ_12MONO_1 = 0x5,   /*   ... block starts 1 byte into a frame */
    CODEC_LZ_12MONO_2 = 0x6,   /*   ... block starts 2 bytes into a frame */
    CODEC_LZ_12STEREO_0 = 0xa, /* 12-bit stereo (sample planes split per channel), phase 0 */
    CODEC_LZ_12STEREO_1 = 0xb, /*   ... phase 1 */
    CODEC_LZ_12STEREO_2 = 0xc, /*   ... phase 2 */
    CODEC_LIST_SIZE = 15
};

/* Codec proc commands. */
enum {
    CODEC_CMD_INIT = 0,
    CODEC_CMD_SHUTDOWN = 1,
    CODEC_CMD_ID = 2,
    CODEC_CMD_COMPRESS = 3,   /* never issued by ALIFE.EXE */
    CODEC_CMD_DECOMPRESS = 4
};

/* Largest block a codec handles (codec_outputTempBuffer / codec_saveBuffer
 * are 0x2000 bytes) and the size the caller's output buffer must have (the
 * compression layer's decompressBuffer is 0x2200 bytes). */
#define CODEC_BLOCK_SIZE 0x2000
#define CODEC_DST_SIZE 0x2200

/*
 * cmd DECOMPRESS: decode `size` bytes at src into dst (CODEC_DST_SIZE bytes),
 *   return the decoded length, or -1 for corrupt input (the original has no
 *   checks).
 * cmd COMPRESS: encode `size` (<= CODEC_BLOCK_SIZE) bytes at src into dst
 *   (at least `size` bytes), return the compressed length; a result >= size
 *   means "doesn't fit, store it raw instead". src is left unchanged.
 * Other cmds: 0, the id, or -1 for an unknown cmd.
 */
typedef int (*codec_proc)(int cmd, void *dst, const void *src, int size);

void codec_init(void);              /* 0x4e1800 */
void codec_shutdown(void);          /* 0x4e1870 */
int codec_register(codec_proc proc); /* 0x4e1950: 0 on success, else the proc already there */

/* Port convenience: registers the ten built-in codecs in the order
 * cmprs_init (0x4daa60) does. */
void codec_register_builtin(void);

/* 0x4e1990: decompresses one block of `size` bytes from src into dst
 * (CODEC_DST_SIZE bytes) with codec `id` (codecList[id]). Returns the codec's
 * result (decoded length, -1 on corrupt input), 0 if no codec is registered
 * under that id. */
int codec_decompress(int id, void *dst, const void *src, int size);

/* Port addition (the original only reaches compressors through the proc):
 * codecList[id](CODEC_CMD_COMPRESS, ...), or -1 if id isn't registered. */
int codec_compress(int id, void *dst, const void *src, int size);

/* The built-in codec procs. */
int codec_raw_proc(int cmd, void *dst, const void *src, int size);          /* 0x4e3270 */
int codec_lz_proc(int cmd, void *dst, const void *src, int size);           /* 0x4e31c0 */
int codec_lz_delta1_proc(int cmd, void *dst, const void *src, int size);    /* 0x4e30a0 */
int codec_lz_delta2_proc(int cmd, void *dst, const void *src, int size);    /* 0x4e2f40 */
int codec_lz_12mono_0_proc(int cmd, void *dst, const void *src, int size);  /* 0x4e2bc0 */
int codec_lz_12mono_1_proc(int cmd, void *dst, const void *src, int size);  /* 0x4e2860 */
int codec_lz_12mono_2_proc(int cmd, void *dst, const void *src, int size);  /* 0x4e24e0 */
int codec_lz_12stereo_0_proc(int cmd, void *dst, const void *src, int size); /* 0x4e2120 */
int codec_lz_12stereo_1_proc(int cmd, void *dst, const void *src, int size); /* 0x4e1d80 */
int codec_lz_12stereo_2_proc(int cmd, void *dst, const void *src, int size); /* 0x4e19c0 */

/* The LZSS core (0x4e5ee0 / 0x4e5ea0). lz_decode returns the decoded length
 * or -1; lz_encode returns the encoded length, > src_len if it didn't fit in
 * src_len bytes (dst must hold src_len bytes). */
long codec_lz_decode(uint8_t *dst, size_t dst_cap, const uint8_t *src, size_t src_len);
long codec_lz_encode(uint8_t *dst, const uint8_t *src, size_t src_len);

#endif
