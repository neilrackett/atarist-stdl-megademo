/*
 * STDL Megademo - the native inner loops (fast.c)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FAST_H
#define FAST_H

#include <stdint.h>

extern const uint16_t edge_masks[32];

/* zero `groups` (even) whole groups by `rows` rows from p */
void clear_box(uint8_t *p, int groups, int rows);
/* scroller band rows to the screen, masks two longs a row */
void band_rows(const uint16_t *src, uint8_t *dst, int rows,
               const uint32_t *masks);

/* one plane of band rows out: 20 words a row into every 4th word */
void band_rows1(const uint16_t *src, uint8_t *dst, int rows);
/* OR one plane of band rows onto the screen (dst is the plane's word
 * in the first row) */
void band_or1(const uint16_t *src, uint8_t *dst, int rows);
/* glyph rows into a band, rows 48 bytes apart */
void glyph16_rows(const uint16_t *g, uint16_t *dst, int rows, int shift);
void glyph32_rows(const uint32_t *g, uint16_t *dst, int rows, int shift);
/* per-x span tables and their builder */
extern uint32_t span_l[320], span_r[320];
void fast_init(void);
/* XOR a trapezoid into one plane, edges 16.16 stepped per row */
void xor_trap(uint8_t *row, int32_t *xl, int32_t dxl, int32_t *xr,
              int32_t dxr, int rows);

/* the same over pairs of rows: one span for each two */
void xor_trap2(uint8_t *row, int32_t *xl, int32_t dxl, int32_t *xr,
               int32_t dxr, int pairs);

#endif
