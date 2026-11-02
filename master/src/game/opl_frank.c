/*
 * frank-blood — music: jfaudiolib's MIDI sequencer played on an emulated
 * OPL2 (emu8950) through Apogee's AL_MIDI voice allocator and General MIDI
 * timbre bank, as the DOS game did on an Adlib.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Runs on core 1 inside driver_frank_service(): after each mixed division
 * the same span of OPL output is rendered and added, and the sequencer's
 * service routine is called at its tick rate in between. The game starts,
 * stops and changes songs from core 0 under MIDI_Lock, a spinlock of its
 * own (the sequencer's service routine never takes it).
 */
#include "opl_frank.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "emu8950.h"
#include "al_midi.h"
#include "sndcards.h"

#define OPL_CLOCK 3579545u

static OPL *opl;
static uint32_t opl_latch;
static spin_lock_t *midi_lock;

static void (*service)(void);
static volatile bool playing;
static uint32_t samples_per_tick_q16 = 1u << 16;   /* 16.16 */
static uint32_t until_tick_q16;                     /* samples to the next tick */

/* Counters for the console's `snd`. */
volatile uint32_t opl_frank_ticks, opl_frank_writes, opl_frank_frames;

/* AL_MIDI writes an Adlib's index port, then its data port. */
void Adlib_Write(int port, int value) {
    if (!(port & 1)) opl_latch = (uint32_t)value & 0xffu;
    else if (opl) { OPL_writeReg(opl, opl_latch, (uint8_t)value); opl_frank_writes++; }
}

static void midi_poly_aftertouch(int channel, int key, int pressure) {
    (void)channel; (void)key; (void)pressure;
}

static void midi_channel_aftertouch(int channel, int pressure) {
    (void)channel; (void)pressure;
}

int FrankDrv_MIDI_Init(midifuncs *funcs, const char *params) {
    (void)params;
    if (!midi_lock) midi_lock = spin_lock_init(spin_lock_claim_unused(true));
    if (!opl) {
        opl = OPL_new(OPL_CLOCK, OPL_SAMPLE_RATE);
        if (!opl) return -1;
    }
    OPL_reset(opl);
    AL_Init(Adlib);
    memset(funcs, 0, sizeof(*funcs));
    funcs->NoteOff = AL_NoteOff;
    funcs->NoteOn = AL_NoteOn;
    funcs->PolyAftertouch = midi_poly_aftertouch;
    funcs->ControlChange = AL_ControlChange;
    funcs->ProgramChange = AL_ProgramChange;
    funcs->ChannelAftertouch = midi_channel_aftertouch;
    funcs->PitchBend = AL_SetPitchBend;
    return 0;
}

void FrankDrv_MIDI_Shutdown(void) {
    FrankDrv_MIDI_HaltPlayback();
    AL_Shutdown();
}

int FrankDrv_MIDI_StartPlayback(void (*fn)(void)) {
    FrankDrv_MIDI_Lock();
    service = fn;
    until_tick_q16 = samples_per_tick_q16;
    playing = true;
    FrankDrv_MIDI_Unlock();
    return 0;
}

void FrankDrv_MIDI_HaltPlayback(void) {
    FrankDrv_MIDI_Lock();
    playing = false;
    service = NULL;
    FrankDrv_MIDI_Unlock();
}

void FrankDrv_MIDI_SetTempo(int tempo, int division) {
    /* tempo is beats per minute, division ticks per beat. */
    const uint64_t ticks_per_min = (uint64_t)(tempo > 0 ? tempo : 120) * (uint64_t)(division > 0 ? division : 96);
    samples_per_tick_q16 = (uint32_t)(((uint64_t)OPL_SAMPLE_RATE * 60u << 16) / ticks_per_min);
    if (samples_per_tick_q16 < (1u << 16)) samples_per_tick_q16 = 1u << 16;
}

void FrankDrv_MIDI_Lock(void) {
    if (midi_lock) spin_lock_unsafe_blocking(midi_lock);
}

void FrankDrv_MIDI_Unlock(void) {
    if (midi_lock) spin_unlock_unsafe(midi_lock);
}

/* Core 1: add `frames` of music to interleaved 16-bit stereo, running the
 * sequencer at its tick times in between. */
void opl_frank_mix(int16_t *pcm, uint32_t frames) {
    if (!opl || !playing) return;
    static int16_t buf[256];
    FrankDrv_MIDI_Lock();
    while (frames && playing) {
        /* Samples until the next tick (at least one), at most a buffer. */
        uint32_t n = until_tick_q16 >> 16;
        if (!n) {
            if (service) service();
            opl_frank_ticks++;
            until_tick_q16 += samples_per_tick_q16;
            continue;
        }
        if (n > frames) n = frames;
        if (n > sizeof(buf) / sizeof(buf[0])) n = sizeof(buf) / sizeof(buf[0]);
        OPL_calc_buffer(opl, buf, n);
        for (uint32_t i = 0; i < n; i++) {
            int32_t s = buf[i];
            int32_t l = pcm[0] + s, r = pcm[1] + s;
            pcm[0] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            pcm[1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            pcm += 2;
        }
        frames -= n;
        opl_frank_frames += n;
        until_tick_q16 -= n << 16;
    }
    FrankDrv_MIDI_Unlock();
}
