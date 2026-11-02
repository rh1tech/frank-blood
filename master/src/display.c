/*
 * frank-blood — the game's 320x200 8-bit screen on HDMI.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "display.h"

#include <string.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "video/hstx_video.h"

#define TOP ((240 - DISPLAY_H) / 2)

static uint8_t fb[2][DISPLAY_W * DISPLAY_H] __attribute__((aligned(4)));
/* Three RGB888 palettes: the scanout reads pals[pal_show]; the game writes
 * one that is neither shown nor pending and publishes it as pal_pending,
 * which the frame callback takes (no copying in the scanout's interrupt, and
 * the game never waits: it sets the palette nearly every frame for fades). */
static uint32_t pals[3][256];
static volatile int8_t pal_show = 0;
static volatile int8_t pal_pending = -1;
static volatile uint8_t front;      /* buffer on screen */
static volatile int8_t pending = -1;/* buffer to show from the next frame */

static void __not_in_flash_func(row_fn)(uint32_t row, uint32_t field, uint32_t *px, uint32_t w) {
    (void)field;
    if (row < TOP || row >= TOP + DISPLAY_H) {
        for (uint32_t x = 0; x < w; x++) px[x] = 0;
        return;
    }
    const uint8_t *src = fb[front] + (row - TOP) * DISPLAY_W;
    const uint32_t *p = pals[pal_show];
    for (uint32_t x = 0; x < DISPLAY_W; x++) {
        const uint32_t c = p[src[x]];
        px[0] = c;
        px[1] = c;
        px += 2;
    }
}

static void (*volatile vsync_hook)(void);

static void __not_in_flash_func(frame_fn)(uint32_t frame) {
    (void)frame;
    if (pending >= 0) {
        front = (uint8_t)pending;
        pending = -1;
    }
    const int8_t np = pal_pending;
    if (np >= 0) {
        pal_show = np;
        pal_pending = -1;
    }
    void (*hook)(void) = vsync_hook;
    if (hook) hook();
}

static void (*core1_init)(void);
static bool (*core1_idle)(void);

static void core1_main(void) {
    video_start();
    if (core1_init) core1_init();
    for (;;)
        if (!core1_idle || !core1_idle()) __wfi();
}

bool display_init(void (*init1)(void), bool (*idle1)(void)) {
    core1_init = init1;
    core1_idle = idle1;
    for (int i = 0; i < 256; i++) pals[0][i] = pals[1][i] = pals[2][i] = (uint32_t)(i * 0x010101u);
    memset(fb, 0, sizeof(fb));
    if (!video_init(VIDEO_640x480p60, row_fn, frame_fn))
        return false;
    multicore_launch_core1(core1_main);
    return true;
}

uint8_t *display_backbuffer(void) {
    /* While a swap is pending the back buffer is the pending one: drawing
     * into it again just updates the frame about to be shown. */
    return fb[pending >= 0 ? pending : front ^ 1];
}

void display_present(bool wait) {
    pending = (int8_t)(front ^ 1);
    if (wait)
        while (pending >= 0) tight_loop_contents();
}

uint8_t *display_front(void) {
    return fb[front];
}

void display_set_vsync_hook(void (*hook)(void)) {
    vsync_hook = hook;
}

uint8_t *display_flip(void) {
    pending = -1;
    front ^= 1;
    return fb[front ^ 1];
}

void display_set_palette(const uint8_t *rgb, int first, int count) {
    /* The newest palette (pending, else shown) is the base for a partial update. */
    const int8_t pend = pal_pending, show = pal_show;
    const int8_t base = pend >= 0 ? pend : show;
    int8_t w = 0;
    while (w == show || w == pend) w++;
    uint32_t *back = pals[w];
    if (first > 0 || count < 256) memcpy(back, pals[base], sizeof(pals[0]));
    for (int i = 0; i < count && first + i < 256; i++, rgb += 3)
        back[first + i] = (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2];
    __dmb();
    pal_pending = w;
}

uint32_t display_frames(void) {
    return video_frames;
}
