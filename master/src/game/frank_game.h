/*
 * frank-blood — glue between the game (Build engine + Blood) and the board.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FRANK_GAME_H
#define FRANK_GAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the game (Blood's app_main); returns when it quits. */
int frank_game_main(int argc, const char **argv);

/* A Build scancode pressed (down=1) or released, from any input source. */
void frank_key_event(int code, int down);

/* Deterministic runs: the 120 Hz timer advances frank_fixed_ticks per shown
 * frame instead of by real time (0 = real time). */
extern int frank_fixed_ticks;
void frank_timer_advance(int n);

/* Board side, called by the game layer. */
void frank_frame_shown(const uint8_t *frame);   /* every shown frame */
void frank_service(void);                       /* console, scripted input */
void frank_demo_mark(int start, const char *level); /* demo benchmark */

/* SRAM for the renderer's hottest tables (the game's own allocations live in
 * PSRAM). Never freed; NULL when the pool is used up. */
void *frank_sram_alloc(uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif
