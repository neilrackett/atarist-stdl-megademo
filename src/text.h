/*
 * STDL Megademo - 1bpp band buffer for the scrollers
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>
#include <stdl/stdl.h>

#define BAND_MAXH 48
#define BAND_W    24            /* 20 screen words + slack both sides */

typedef struct {
    int h;
    uint16_t buf[BAND_MAXH][BAND_W];
} Band;

void band_begin(Band *b, int h);
void band_glyph16(Band *b, int x, int y, int ch);
void band_glyph32(Band *b, int x, int y, int ch);
/* a scroller line: char i of `text` (wrapping at len) sits at
 * i * adv - pos; yoff (optional, 128 entries, one per 4px of x)
 * lifts each char for DYCP waves */
void band_scroll(Band *b, const char *text, int len, int pos, int adv,
                 int big, const int8_t *yoff);
void band_out(STDL_Surface *s, const Band *b, int y0, uint8_t col,
              const uint8_t *rowcol);
/* only one plane: the band's other planes must already be zero (clear
 * them once per page), then text is colour 1 << plane - a quarter of
 * the writes */
void band_out1(STDL_Surface *s, const Band *b, int y0, int plane);

#endif
