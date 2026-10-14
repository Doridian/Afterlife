/*
 * compbun resource file manager (compbun:source:cbresfm.c, 0x4d51a0-0x4d5560).
 * See cbres.h for the layer model.
 */
#include "compbun/cbres.h"

#include <stddef.h>

#include "compbun/bndlemgr.h"
#include "compbun/cmprsmgr.h"
#include "compbun/codec.h"
#include "compbun/preload.h"
#include "platform/platform.h"

/* 0x6749c8 topLayer: entry point of every cbres_* call. */
static CbLayer *g_top_layer;
/* 0x54f364 currentLayer: the layer whose slot is executing. cbres_lower_*
 * temporarily step it down one layer, so nested lower calls keep walking
 * down the stack. */
static CbLayer *g_current_layer;

/* --- C File Layer: the CRT, through the platform layer ------------------ */

static PlatFile *to_file(CbHandle h)
{
    return (PlatFile *)h;
}

/* CRT fopen (0x4eff22). Data files are read-only, so the mode is ignored. */
static CbHandle cfile_open(const char *name, const char *mode)
{
    (void)mode;
    return (CbHandle)plat_file_open(name);
}

/* CRT fclose (0x4b89cd) */
static void cfile_close(CbHandle h)
{
    plat_file_close(to_file(h));
}

/* CRT fread (0x4effc9) */
static long cfile_read(void *buf, long size, long count, CbHandle h)
{
    if (size <= 0 || count <= 0)
        return 0;
    return (long)plat_file_read(to_file(h), buf, (size_t)size, (size_t)count);
}

/* 0x4d54d0. Calls fread directly rather than through the layer's read slot,
 * so it bypasses a replaced read slot (harmless in the original, where the
 * replacement handles are FILE pointers too). */
static long cfile_read_cb(void *buf, long size, long count, CbHandle h, CbReadCallback cb)
{
    long n = cfile_read(buf, size, count, h);
    cb(buf, size, count, n);
    return n;
}

/* CRT fseek (0x4eff35) */
static int cfile_seek(CbHandle h, long offset, int whence)
{
    return plat_file_seek(to_file(h), offset, whence);
}

/* CRT ftell (0x4f0255) */
static long cfile_tell(CbHandle h)
{
    return plat_file_tell(to_file(h));
}

/* CRT fgetpos (0x4f063c): ftell into *pos, 0 or -1. */
static int cfile_getpos(CbHandle h, long *pos)
{
    *pos = plat_file_tell(to_file(h));
    return *pos == -1 ? -1 : 0;
}

/* 0x6749d0 */
CbLayer cbres_c_file_layer = {
    "C File Layer", NULL,       cfile_open, cfile_close, cfile_read, cfile_read_cb,
    cfile_seek,     cfile_tell, cfile_getpos, NULL,
};

/* --- calls into the layer below the current one -------------------------- */

/* 0x4d5270 */
CbHandle cbres_lower_open(const char *name, const char *mode)
{
    CbLayer *saved = g_current_layer;
    g_current_layer = saved->next;
    CbHandle h = g_current_layer->open(name, mode);
    g_current_layer = saved;
    return h;
}

/* 0x4d52a0 */
void cbres_lower_close(CbHandle h)
{
    CbLayer *saved = g_current_layer;
    g_current_layer = saved->next;
    g_current_layer->close(h);
    g_current_layer = saved;
}

/* 0x4d52d0 */
long cbres_lower_read(void *buf, long size, long count, CbHandle h)
{
    CbLayer *saved = g_current_layer;
    g_current_layer = saved->next;
    long n = g_current_layer->read(buf, size, count, h);
    g_current_layer = saved;
    return n;
}

/* 0x4d5310 */
int cbres_lower_getpos(CbHandle h, long *pos)
{
    CbLayer *saved = g_current_layer;
    g_current_layer = saved->next;
    int r = g_current_layer->getpos(h, pos);
    g_current_layer = saved;
    return r;
}

/* 0x4d5340 */
long cbres_lower_tell(CbHandle h)
{
    CbLayer *saved = g_current_layer;
    g_current_layer = saved->next;
    long r = g_current_layer->tell(h);
    g_current_layer = saved;
    return r;
}

/* 0x4d5370 */
int cbres_lower_seek(CbHandle h, long offset, int whence)
{
    CbLayer *saved = g_current_layer;
    g_current_layer = saved->next;
    int r = g_current_layer->seek(h, offset, whence);
    g_current_layer = saved;
    return r;
}

/* --- entry points -------------------------------------------------------- */

/* 0x4d53b0 */
CbHandle cbres_open(const char *name, const char *mode)
{
    g_current_layer = g_top_layer;
    return g_top_layer->open(name, mode);
}

/* 0x4d53d0 */
void cbres_close(CbHandle h)
{
    g_current_layer = g_top_layer;
    g_top_layer->close(h);
}

/* 0x4d53f0 */
long cbres_read(void *buf, long size, long count, CbHandle h)
{
    g_current_layer = g_top_layer;
    return g_top_layer->read(buf, size, count, h);
}

/* 0x4d5420 */
long cbres_tell(CbHandle h)
{
    g_current_layer = g_top_layer;
    return g_top_layer->tell(h);
}

/* 0x4d5440 */
int cbres_seek(CbHandle h, long offset, int whence)
{
    g_current_layer = g_top_layer;
    return g_top_layer->seek(h, offset, whence);
}

/* 0x4d5470: push a layer on top and run its init. */
int cbres_push_layer(CbLayer *layer)
{
    layer->next = g_top_layer;
    g_top_layer = layer;
    g_current_layer = layer;
    if (layer->init)
        return layer->init();
    return 1;
}

/* 0x4d54a0. The original also registers the module ("compbun:source:cbresfm.c",
 * Apr 18 1996) with the VHUMOR module runtime; that runtime is out of scope. */
void cbres_init(void)
{
    cbres_c_file_layer.next = NULL;
    g_top_layer = &cbres_c_file_layer;
}

/* 0x4d5510. Walks the layer list and does nothing else: each layer's own
 * shutdown is called by the module runtime. */
void cbres_shutdown(void)
{
    for (CbLayer *l = g_top_layer; l; l = l->next) {
    }
}

/* 0x4d51a0. Afterlife calls this once, from its sound init (0x4b9510), as
 * compbun_init(1, 0): compression on, bundling off. */
void compbun_init(int compress, int bundle)
{
    cbres_init();
    if (bundle)
        cbres_push_layer(&bndl_layer);
    if (compress)
        cbres_push_layer(&cmprs_layer);
}

/* Port-only. In the original, VHUMOR's module runtime calls each module's
 * registered shutdown at exit (vh_module_register). Without that runtime the
 * port calls them here, most recently initialized first. */
void compbun_shutdown(void)
{
    for (CbLayer *l = g_top_layer; l; l = l->next) {
        if (l == &cmprs_layer) {
            preload_shutdown();
#ifdef COMPBUN_HAVE_CODECS
            codec_shutdown();
#endif
            cmprs_shutdown();
        } else if (l == &bndl_layer) {
            bndl_shutdown();
        }
    }
    cbres_shutdown();
    g_top_layer = g_current_layer = NULL;
}
