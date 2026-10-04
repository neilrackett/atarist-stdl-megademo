/*
 * STDL Megademo - dot erasing
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STDL_PointsC is the general answer - clipped, mask-aware, any
 * surface, any plane budget - and it measures around 450 cycles a
 * point on a plain ST. A starfield or a dot tunnel wants hundreds a
 * frame onto a black screen it owns, so each effect plots in its own
 * 68000 loop (intro.c, tunnel.c), where a colour only ever adds
 * planes, and saves every dot's offset. This is the erase: it clears
 * the whole 16-pixel group a dot sat in (two long stores), which only
 * ever wipes other dots of the same frame - all being erased too.
 */

#include "demo.h"
#include "dots.h"

void dots_erase(uint8_t *base, const uint16_t *save, int n)
{
    for (; n > 0; n--) {
        uint32_t *w = (uint32_t *)(base + *save++);
        w[0] = 0;
        w[1] = 0;
    }
}

/* plane-limited erase: clear only planes 0..2 (overlay text in 3) */
void dots_erase3(uint8_t *base, const uint16_t *save, int n)
{
    for (; n > 0; n--) {
        uint16_t *w = (uint16_t *)(base + *save++);
        *(uint32_t *)w = 0;
        w[2] = 0;
    }
}
