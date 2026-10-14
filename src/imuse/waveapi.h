/*
 * Port-only controls of the output driver (src/imuse/waveapi.c).
 */
#ifndef AFTERLIFE_IMUSE_WAVEAPI_H
#define AFTERLIFE_IMUSE_WAVEAPI_H

#include <stdint.h>

/* Receives every mixed block: count frames of interleaved signed 16-bit
 * stereo. Setting a sink before imuse_initialize replaces the audio device
 * by a virtual one that plays at the real rate, one timer period per
 * imuse_timer_handler call (for headless tests and recording). */
typedef void (*ImuseWaveSink)(const int16_t *frames, int count, void *user);
void imuse_wave_set_sink(ImuseWaveSink sink, void *user);

/* Output sample rate (22050). */
int imuse_wave_rate(void);

#endif
