/*
 * STDL Megademo - native 16x16 bobs
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BOBS_H
#define BOBS_H

#include <stdint.h>

typedef struct {
    uint16_t data[16][16][10];  /* shift, row, 2 x (mask, 4 planes) */
} Bob;

#define BOBS_MAX 24

/* where each page's bobs went, for the restore */
typedef struct {
    int n[2];
    uint16_t off[2][BOBS_MAX];
} BobList;

/* from a 16x16 chunky image: 0 transparent, else map[v] */
Bob *bob_make(const uint8_t *img, const uint8_t map[16]);
void bob_put(uint8_t *dst, const uint16_t *variant);
void bob_back(uint8_t *dst, const uint8_t *src);
void bobs_reset(BobList *l);
/* screen and background share a layout: stride 160, same rows */
void bobs_restore(BobList *l, uint8_t *screen, const uint8_t *bg);
/* x 0..304, y 0..h-16: the caller keeps them on screen */
void bobs_draw(BobList *l, const Bob *b, uint8_t *screen, int x, int y);

#endif
