/*
 * compbun resource file manager (compbun:source:cbresfm.c).
 *
 * compbun's file I/O is a stack of layers with a stdio-like interface. Each
 * layer handles a call itself or passes it to the layer below through the
 * cbres_lower_* helpers. Calls enter at the top of the stack (cbres_open,
 * cbres_read, ...). The bottom of the stack is always the "C File Layer".
 * Layers are pushed by compbun_init():
 *
 *     Compression Layer (cmprsmgr.c)   pushed when compress != 0
 *     Bundling layer    (bndlemgr.c)   pushed when bundle != 0
 *     C File Layer      (cbresfm.c)    always; slots point at the CRT
 *
 * A handle is whatever the layer that opened it returns; 0 means failure. The
 * original passed them around as 32-bit ints (FILE pointers at the bottom,
 * small indices higher up). Here they are intptr_t so a pointer still fits.
 */
#ifndef AFTERLIFE_COMPBUN_CBRES_H
#define AFTERLIFE_COMPBUN_CBRES_H

#include <stdint.h>

typedef intptr_t CbHandle;

/* Called by read_cb after the read with the same arguments plus the result. */
typedef void (*CbReadCallback)(void *buf, long size, long count, long result);

/* Layer descriptor. The field order matches the original tables
 * (cbres_c_file_layer 0x6749d0, bndl_layer 0x675c88, cmprs_layer 0x675bb0). */
typedef struct CbLayer {
    const char *name;
    int (*init)(void); /* called by cbres_push_layer, may be NULL */
    CbHandle (*open)(const char *name, const char *mode);
    void (*close)(CbHandle h);
    long (*read)(void *buf, long size, long count, CbHandle h);
    long (*read_cb)(void *buf, long size, long count, CbHandle h, CbReadCallback cb);
    int (*seek)(CbHandle h, long offset, int whence); /* whence 0 set, 1 cur, 2 end */
    long (*tell)(CbHandle h);
    int (*getpos)(CbHandle h, long *pos);
    struct CbLayer *next; /* layer below */
} CbLayer;

/* The bottom layer. Mutable on purpose: Afterlife's sound init (0x4b9510)
 * replaces its open/close/read/seek/tell slots with the game's resource
 * functions, so compbun opens resources by ID instead of files by name. */
extern CbLayer cbres_c_file_layer;

void compbun_init(int compress, int bundle);
void compbun_shutdown(void); /* port-only, see cbres.c */

void cbres_init(void);
void cbres_shutdown(void);
int cbres_push_layer(CbLayer *layer);

/* Entry points: start at the top layer. */
CbHandle cbres_open(const char *name, const char *mode);
void cbres_close(CbHandle h);
long cbres_read(void *buf, long size, long count, CbHandle h);
int cbres_seek(CbHandle h, long offset, int whence);
long cbres_tell(CbHandle h);

/* For layer implementations: call the same slot of the layer below the one
 * currently executing. */
CbHandle cbres_lower_open(const char *name, const char *mode);
void cbres_lower_close(CbHandle h);
long cbres_lower_read(void *buf, long size, long count, CbHandle h);
int cbres_lower_getpos(CbHandle h, long *pos);
long cbres_lower_tell(CbHandle h);
int cbres_lower_seek(CbHandle h, long offset, int whence);

#endif
