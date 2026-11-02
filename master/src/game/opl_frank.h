/*
 * frank-blood — music on an emulated OPL2 (opl_frank.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef OPL_FRANK_H
#define OPL_FRANK_H

#include <stdint.h>

#include "midifuncs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The OPL's own rate (3.579545 MHz / 72): the whole mix runs at it, so the
 * emulator needs no rate converter. */
#define OPL_SAMPLE_RATE 49716u

int FrankDrv_MIDI_Init(midifuncs *funcs, const char *params);
void FrankDrv_MIDI_Shutdown(void);
int FrankDrv_MIDI_StartPlayback(void (*service)(void));
void FrankDrv_MIDI_HaltPlayback(void);
void FrankDrv_MIDI_SetTempo(int tempo, int division);
void FrankDrv_MIDI_Lock(void);
void FrankDrv_MIDI_Unlock(void);

/* Core 1: add `frames` frames of music to interleaved 16-bit stereo. */
void opl_frank_mix(int16_t *pcm, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif
