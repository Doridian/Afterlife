/*
 * Reader for the .000 resource bundles. Clean reimplementation from the format
 * notes in docs/formats/jiff.md; the original lives in the "compbun" library
 * (Bndlemgr.c / Cmprsmgr.c / PRELOAD.C) and should eventually replace this
 * once decompiled.
 */
#ifndef AFTERLIFE_JIFF_H
#define AFTERLIFE_JIFF_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t id;
    uint32_t offset; /* absolute file offset of the payload */
    uint32_t length; /* payload length */
} JiffEntry;

typedef struct {
    FILE *f;
    uint32_t count;
    JiffEntry *entries;
} JiffBundle;

int jiff_open(JiffBundle *b, FILE *f);
void jiff_close(JiffBundle *b);
const JiffEntry *jiff_find(const JiffBundle *b, uint32_t id);
/* Returns a malloc'd buffer the caller frees, or NULL. */
uint8_t *jiff_load(const JiffBundle *b, const JiffEntry *e);

#endif
