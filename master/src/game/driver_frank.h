/*
 * frank-blood — jfaudiolib PCM driver for the FRANK master (driver_frank.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DRIVER_FRANK_H
#define DRIVER_FRANK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int FrankDrv_GetError(void);
const char *FrankDrv_ErrorString(int ErrorNumber);
int FrankDrv_PCM_Init(int *mixrate, int *numchannels, int *samplebits, void *initdata);
void FrankDrv_PCM_Shutdown(void);
int FrankDrv_PCM_BeginPlayback(char *BufferStart, int BufferSize, int NumDivisions,
                               void (*CallBackFunc)(void));
void FrankDrv_PCM_StopPlayback(void);
void FrankDrv_PCM_Lock(void);
void FrankDrv_PCM_Unlock(void);

/* Core 1: mix into the I2S ring while it has room. True if it mixed. */
bool driver_frank_service(void);
/* Bench: replace the output with a sine for `ms` milliseconds. */
void driver_frank_tone(uint32_t hz, uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif
