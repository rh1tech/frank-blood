/*
 * frank-blood — a separate PSRAM arena for sound samples.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Blood's big sound samples (ambient loops of 100-260 KB) come and go with
 * every level and fragmented the general heap until a map no longer found a
 * contiguous block (a fatal error at a level change). Samples live here
 * instead: they can only fragment each other, and a sample that does not fit
 * is skipped (Resource::Lock returns NULL for samples). First fit over an
 * address-ordered free list, neighbours coalesced on free.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct blk {
    uint32_t size;      /* bytes including this header; multiple of 8 */
    struct blk *next;   /* free list only */
} blk_t;

#define HDR 8u

static uint8_t *arena;
static uint32_t arena_bytes;
static blk_t *free_list;

static void arena_init(void) {
    /* The arena comes out of the general heap once, at first use (after the
     * tile cache, before the game's own resources fragment it). */
    arena_bytes = 1536u * 1024u;
    arena = malloc(arena_bytes);
    if (!arena) { arena_bytes = 0; return; }
    free_list = (blk_t *)arena;
    free_list->size = arena_bytes;
    free_list->next = NULL;
}

void *frank_snd_alloc(uint32_t n) {
    if (!arena && !arena_bytes) arena_init();
    if (!arena) return NULL;
    const uint32_t need = (n + HDR + 7u) & ~7u;
    blk_t **pp = &free_list;
    for (blk_t *b = free_list; b; pp = &b->next, b = b->next) {
        if (b->size < need) continue;
        if (b->size - need >= 64u) {        /* split: keep the tail free */
            blk_t *rest = (blk_t *)((uint8_t *)b + need);
            rest->size = b->size - need;
            rest->next = b->next;
            *pp = rest;
            b->size = need;
        } else {
            *pp = b->next;
        }
        return (uint8_t *)b + HDR;
    }
    return NULL;
}

int frank_snd_owns(const void *p) {
    return arena && (const uint8_t *)p >= arena && (const uint8_t *)p < arena + arena_bytes;
}

void frank_snd_free(void *p) {
    if (!p) return;
    blk_t *b = (blk_t *)((uint8_t *)p - HDR);
    /* Insert in address order, then merge with the neighbours. */
    blk_t *prev = NULL, *cur = free_list;
    while (cur && cur < b) { prev = cur; cur = cur->next; }
    b->next = cur;
    if (prev) prev->next = b; else free_list = b;
    if (cur && (uint8_t *)b + b->size == (uint8_t *)cur) {
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b) {
        prev->size += b->size;
        prev->next = b->next;
    }
}

/* Largest free block and total free, for reports. */
void frank_snd_stats(uint32_t *largest, uint32_t *total) {
    uint32_t l = 0, t = 0;
    for (blk_t *b = free_list; b; b = b->next) {
        t += b->size;
        if (b->size > l) l = b->size;
    }
    *largest = l;
    *total = t;
}
