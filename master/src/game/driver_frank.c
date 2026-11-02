/*
 * frank-blood — jfaudiolib PCM driver for the FRANK master: MultiVoc mixes
 * 16-bit stereo on core 1 into the I2S ring (audio.c, TDA1387 DAC).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Core 1 runs the HDMI scanout in interrupts and nothing else, so its idle
 * loop does the mixing (driver_frank_service): whenever the ring has room for
 * a division, MultiVoc's callback mixes the next one. The game changes voices
 * from core 0 under PCM_Lock, a hardware spinlock shared with the mixer.
 */
#include "driver_frank.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "audio/audio.h"

/* opl_frank.c (built apart: its Apogee headers clash with jfaudiolib's) */
void opl_frank_mix(int16_t *pcm, uint32_t frames);
#define OPL_SAMPLE_RATE 49716

static int ErrorCode;
static int Initialised;
static int Playing;

static char *MixBuffer;
static int MixBufferSize;
static int MixBufferCount;
static int MixBufferCurrent;
static int MixBufferUsed;
static void (*MixCallBack)(void);
static int MixRate = 22050;

/* Not an interrupt-masking lock: core 1 holds it for a whole division's mix,
 * far longer than the HDMI scanout's interrupt may wait. Nothing touches
 * MultiVoc from an interrupt, so masking is not needed. */
static spin_lock_t *mix_lock;

int FrankDrv_GetError(void) { return ErrorCode; }

const char *FrankDrv_ErrorString(int ErrorNumber) {
    (void)ErrorNumber;
    return "FRANK I2S";
}

int FrankDrv_PCM_Init(int *mixrate, int *numchannels, int *samplebits, void *initdata) {
    (void)initdata;
    if (!mix_lock) mix_lock = spin_lock_init(spin_lock_claim_unused(true));
    /* Mix what the DAC plays: 16-bit stereo; the rate the game asks for. */
    *numchannels = 2;
    *samplebits = 16;
    /* Everything runs at the OPL's own rate, so music needs no resampler;
     * MultiVoc resamples the game's 11/22 kHz effects anyway. */
    *mixrate = OPL_SAMPLE_RATE;
    MixRate = *mixrate;
    Initialised = 1;
    return 0;
}

void FrankDrv_PCM_Shutdown(void) {
    FrankDrv_PCM_StopPlayback();
    Initialised = 0;
}

int FrankDrv_PCM_BeginPlayback(char *BufferStart, int BufferSize, int NumDivisions,
                               void (*CallBackFunc)(void)) {
    if (!Initialised) return -1;
    FrankDrv_PCM_StopPlayback();
    FrankDrv_PCM_Lock();
    MixBuffer = BufferStart;
    MixBufferSize = BufferSize;
    MixBufferCount = NumDivisions;
    MixBufferCurrent = 0;
    MixBufferUsed = 0;
    MixCallBack = CallBackFunc;
    /* Prime the first division, as the SDL driver does. */
    MixCallBack();
    FrankDrv_PCM_Unlock();
    audio_set_rate((uint32_t)MixRate);
    Playing = 1;
    return 0;
}

void FrankDrv_PCM_StopPlayback(void) {
    if (!Playing) return;
    FrankDrv_PCM_Lock();
    Playing = 0;
    FrankDrv_PCM_Unlock();
    audio_flush();
}

/* Bench: microseconds core 0 (the game) spent waiting for the mixer. */
volatile uint32_t frank_pcm_wait_us, frank_pcm_locks;

void FrankDrv_PCM_Lock(void) {
    if (!mix_lock) return;
    if (get_core_num() == 0) {
        frank_pcm_locks++;
        if (!spin_try_lock_unsafe(mix_lock)) {
            const uint32_t t0 = time_us_32();
            spin_lock_unsafe_blocking(mix_lock);
            frank_pcm_wait_us += time_us_32() - t0;
        }
        return;
    }
    spin_lock_unsafe_blocking(mix_lock);
}

void FrankDrv_PCM_Unlock(void) {
    if (!mix_lock) return;
    spin_unlock_unsafe(mix_lock);
}

/* Bench (console `mix`): bit 0 effects, bit 1 music. */
volatile uint32_t frank_mix_mask = 3;

/* Bench test tone (console `tone`): a sine added to the mix. */
static volatile uint32_t tone_step, tone_left;
static uint32_t tone_phase;

void driver_frank_tone(uint32_t hz, uint32_t ms) {
    tone_step = (uint32_t)(((uint64_t)hz << 32) / (uint32_t)MixRate);
    tone_left = (uint32_t)((uint64_t)MixRate * ms / 1000u);
}

static void add_tone(int16_t *pcm, uint32_t frames) {
    static int16_t sine[256];
    if (!sine[64])
        for (int i = 0; i < 256; i++) {
            /* integer sine by a quarter-wave parabola: good enough for a bench tone */
            int x = i & 127, y = x * (128 - x);              /* 0..4096 */
            sine[i] = (int16_t)((i < 128 ? 1 : -1) * y * 4); /* +-16384 */
        }
    for (uint32_t i = 0; i < frames && tone_left; i++, tone_left--) {
        const int16_t v = sine[tone_phase >> 24];
        tone_phase += tone_step;
        pcm[2 * i] = v;
        pcm[2 * i + 1] = v;
    }
}

/* Core 1's idle loop: keep the I2S ring topped up. Returns true if it mixed. */
bool driver_frank_service(void) {
    if (!Playing || !MixCallBack) return false;
    bool did = false;
    /* Whole divisions only, while the ring has room for one. */
    while (Playing && audio_space() >= (uint32_t)MixBufferSize) {
        FrankDrv_PCM_Lock();
        if (!Playing) { FrankDrv_PCM_Unlock(); break; }
        if (MixBufferUsed == MixBufferSize) {
            MixCallBack();
            MixBufferUsed = 0;
            if (++MixBufferCurrent >= MixBufferCount) MixBufferCurrent = 0;
            FrankDrv_PCM_Unlock();
            if (!(frank_mix_mask & 1))
                memset(MixBuffer + MixBufferCurrent * MixBufferSize, 0, (size_t)MixBufferSize);
            if (frank_mix_mask & 2)
                opl_frank_mix((int16_t *)(MixBuffer + MixBufferCurrent * MixBufferSize),
                              (uint32_t)MixBufferSize / 4u);
            if (tone_left)
                add_tone((int16_t *)(MixBuffer + MixBufferCurrent * MixBufferSize),
                         (uint32_t)MixBufferSize / 4u);
            FrankDrv_PCM_Lock();
        }
        const char *src = MixBuffer + MixBufferCurrent * MixBufferSize + MixBufferUsed;
        const int len = MixBufferSize - MixBufferUsed;
        FrankDrv_PCM_Unlock();
        MixBufferUsed += (int)audio_write(src, (uint32_t)len);
        did = true;
    }
    return did;
}
