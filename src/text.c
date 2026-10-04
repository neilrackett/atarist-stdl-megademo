/*
 * STDL Megademo - fonts and bitplane text
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The 8x8 font goes to STDL as an STDL_Font for STDL_DrawText. The
 * scrollers are native: a 1bpp band buffer collects the glyphs at any
 * pixel position (one long OR per glyph row), then one pass writes
 * the band out to the planes of a colour - so a 32-row scroller is a
 * few thousand stores a frame, whatever the text.
 */

#include <string.h>
#include "demo.h"
#include "text.h"
#include "fast.h"

STDL_Font font8x8;

void text_init(void)
{
    font8x8.cw = 8;
    font8x8.ch = 8;
    font8x8.first = FONT_FIRST;
    font8x8.last = FONT_LAST;
    font8x8.bytes_per_row = 1;
    font8x8.bits = (uint8_t *)font8;
}

static int glyph_index(int ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch -= 32;
    }
    if (ch < FONT_FIRST || ch > FONT_LAST) {
        return 0;
    }
    return ch - FONT_FIRST;
}

const uint16_t *glyph16(int ch)
{
    return font16 + glyph_index(ch) * 16;
}

const uint32_t *glyph32(int ch)
{
    return font32 + glyph_index(ch) * 32;
}

int text_len(const char *s)
{
    return (int)strlen(s);
}

/* ------------------------------------------------------------- band */

void band_begin(Band *b, int h)
{
    b->h = h > BAND_MAXH ? BAND_MAXH : h;
    memset(b->buf, 0, sizeof(b->buf[0]) * (size_t)b->h);
}

/* the glyph's rows as 32-bit windows ORed at word gx+1 (word 0 is
 * the slack for x in -15..-1, word 21 for the right edge) */
void band_glyph16(Band *b, int x, int y, int ch)
{
    const uint16_t *g;
    int r0, r1, sh, wi;

    if (x <= -16 || x >= 320) {
        return;
    }
    g = glyph16(ch);
    sh = 16 - (x & 15);
    wi = (x >> 4) + 1;
    r0 = y < 0 ? -y : 0;
    r1 = 16;
    if (y + r1 > b->h) {
        r1 = b->h - y;
    }
    if (r1 > r0) {
        glyph16_rows(g + r0, &b->buf[y + r0][wi], r1 - r0, sh);
    }
}

void band_glyph32(Band *b, int x, int y, int ch)
{
    const uint32_t *g;
    int r0, r1, sh, wi;

    if (x <= -32 || x >= 320) {
        return;
    }
    g = glyph32(ch);
    sh = x & 15;
    wi = (x >> 4) + 1;
    r0 = y < 0 ? -y : 0;
    r1 = 32;
    if (y + r1 > b->h) {
        r1 = b->h - y;
    }
    /* 32 bits shifted right by sh span three words; a glyph
     * entering on the left (wi -1) starts one word in, so step it
     * right a group and shift it 16 further by dropping its first
     * word - simpler: leave the slack word to catch it */
    if (wi < 0) {
        return;                 /* x below -16: off the left edge  */
    }
    if (r1 > r0) {
        glyph32_rows(g + r0, &b->buf[y + r0][wi], r1 - r0, sh);
    }
}

/* scroller: text laid out at `adv` px a char, scrolled `pos` px in */
void band_scroll(Band *b, const char *text, int len, int pos, int adv,
                 int big, const int8_t *yoff)
{
    int first = pos / adv;
    int x = -(pos - first * adv);
    int i = first % len;            /* one modulo, then wrap by hand */

    for (; x < 320; x += adv) {
        int ch = text[i];
        if (++i == len) {
            i = 0;
        }
        int y = yoff != NULL ? yoff[(x + 32) >> 2 & 127] : 0;
        if (ch == ' ') {
            continue;
        }
        if (big) {
            band_glyph32(b, x, y, ch);
        } else {
            band_glyph16(b, x, y, ch);
        }
    }
}

/*
 * Out to the screen: every plane of every group of the band's rows
 * is written (the colour's planes get the text, the others zero), so
 * the band needs no clearing first and owns its rows outright.
 */
void band_out(STDL_Surface *s, const Band *b, int y0, uint8_t col,
              const uint8_t *rowcol)
{
    uint32_t masks[BAND_MAXH * 2];
    int r, r0 = 0, r1 = b->h;

    if (y0 < 0) {
        r0 = -y0;
    }
    if (y0 + r1 > s->h) {
        r1 = s->h - y0;
    }
    if (r1 <= r0) {
        return;
    }
    for (r = r0; r < r1; r++) {
        uint8_t c = rowcol != NULL ? rowcol[r] : col;
        masks[2 * r] = ((c & 1) ? 0xFFFF0000u : 0) | ((c & 2) ? 0x0000FFFFu : 0);
        masks[2 * r + 1] = ((c & 4) ? 0xFFFF0000u : 0) | ((c & 8) ? 0x0000FFFFu : 0);
    }
    band_rows(&b->buf[r0][1], s->pixels + row_off[y0 + r0], r1 - r0,
              &masks[2 * r0]);
}

void band_out1(STDL_Surface *s, const Band *b, int y0, int plane)
{
    int r0 = 0, r1 = b->h;

    if (y0 < 0) {
        r0 = -y0;
    }
    if (y0 + r1 > s->h) {
        r1 = s->h - y0;
    }
    if (r1 > r0) {
        band_rows1(&b->buf[r0][1], s->pixels + row_off[y0 + r0] + plane * 2,
                   r1 - r0);
    }
}

/* ------------------------------------------------- direct to screen */

static void or_planes(uint16_t *grp, uint16_t v, uint8_t c)
{
    if (c & 1) grp[0] |= v;
    if (c & 2) grp[1] |= v;
    if (c & 4) grp[2] |= v;
    if (c & 8) grp[3] |= v;
}

void bp_glyph16(STDL_Surface *s, int x, int y, int ch, uint8_t col,
                const uint8_t *rowcol)
{
    const uint16_t *g = glyph16(ch);
    int r, sh, gx;

    if (x <= -16 || x >= 320) {
        return;
    }
    sh = 16 - (x & 15);
    gx = x >> 4;
    for (r = 0; r < 16; r++) {
        int yy = y + r;
        uint32_t v;
        uint16_t *row;
        uint8_t c;

        if (yy < 0 || yy >= s->h || g[r] == 0) {
            continue;
        }
        v = (uint32_t)g[r] << sh;
        c = rowcol != NULL ? rowcol[r] : col;
        row = (uint16_t *)(s->pixels + row_off[yy]);
        if (gx >= 0) {
            or_planes(row + gx * 4, (uint16_t)(v >> 16), c);
        }
        if (gx < 19 && (uint16_t)v) {
            or_planes(row + gx * 4 + 4, (uint16_t)v, c);
        }
    }
}

void bp_text16(STDL_Surface *s, int y, const char *t, uint8_t col,
               const uint8_t *rowcol)
{
    int n = (int)strlen(t);
    int x = 160 - n * 8;
    int i;

    for (i = 0; i < n; i++, x += 16) {
        if (t[i] != ' ') {
            bp_glyph16(s, x, y, t[i], col, rowcol);
        }
    }
}

void band_clear(STDL_Surface *s, int y, int h)
{
    STDL_Rect r;

    if (y < 0) {
        h += y;
        y = 0;
    }
    if (y + h > s->h) {
        h = s->h - y;
    }
    if (h <= 0) {
        return;
    }
    r.x = 0;
    r.y = (int16_t)y;
    r.w = 320;
    r.h = (uint16_t)h;
    STDL_FillRect(s, &r, 0);
}
