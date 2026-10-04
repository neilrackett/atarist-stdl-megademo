/*
 * STDL Megademo - native 16x16 bobs
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STDL_BlitSprite plus a dirty-rectangle restore is the general tool
 * and costs about 11000 cycles a 16x16 bob on a plain ST, most of it
 * the clip, the dispatch and the restore's own blit set-up. A swarm
 * wants the special case: sixteen pre-shifted copies of the bob, two
 * groups by sixteen rows of mask-and-OR in 68000, and a restore that
 * copies sixteen bytes a row back from the part's background - about
 * a third of the cost. The caller keeps the bobs on screen.
 */

#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "bobs.h"

/* [shift][row][group] mask, p0, p1, p2, p3 */
Bob *bob_make(const uint8_t *img, const uint8_t map[16])
{
    Bob *b = malloc(sizeof(Bob));
    int s, r, x, p;

    if (b == NULL) {
        return NULL;
    }
    for (s = 0; s < 16; s++) {
        for (r = 0; r < 16; r++) {
            uint16_t *w = b->data[s][r];
            w[0] = w[5] = 0xFFFF;
            for (p = 1; p < 5; p++) {
                w[p] = w[5 + p] = 0;
            }
            for (x = 0; x < 16; x++) {
                uint8_t v = img[r * 16 + x];
                int px = s + x, g = px >> 4;
                uint16_t bit = (uint16_t)(0x8000u >> (px & 15));
                uint8_t c;
                if (v == 0) {
                    continue;
                }
                c = map[v & 15];
                w[g * 5] &= (uint16_t)~bit;
                for (p = 0; p < 4; p++) {
                    if (c & (1 << p)) {
                        w[g * 5 + 1 + p] |= bit;
                    }
                }
            }
        }
    }
    return b;
}

__asm__(
"    .text\n"
"    .even\n"
"    .globl _bob_put\n"
"_bob_put:\n"                       /* (dst group, variant)        */
"    move.l 4(%sp),%a1\n"
"    move.l 8(%sp),%a0\n"
"    move.l %d2,-(%sp)\n"
"    moveq  #15,%d2\n"
"1:  move.w (%a0)+,%d0\n"           /* first group                 */
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    move.w (%a0)+,%d0\n"           /* second group                */
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    and.w  %d0,(%a1)\n"
"    move.w (%a0)+,%d1\n"
"    or.w   %d1,(%a1)+\n"
"    lea    144(%a1),%a1\n"
"    dbra   %d2,1b\n"
"    move.l (%sp)+,%d2\n"
"    rts\n"
"    .even\n"
"    .globl _bob_back\n"
"_bob_back:\n"                      /* (dst group, src group)      */
"    move.l 4(%sp),%a1\n"
"    move.l 8(%sp),%a0\n"
"    moveq  #15,%d0\n"
"1:  move.l (%a0)+,(%a1)+\n"
"    move.l (%a0)+,(%a1)+\n"
"    move.l (%a0)+,(%a1)+\n"
"    move.l (%a0)+,(%a1)+\n"
"    lea    144(%a0),%a0\n"
"    lea    144(%a1),%a1\n"
"    dbra   %d0,1b\n"
"    rts\n"
);

void bobs_reset(BobList *l)
{
    l->n[0] = l->n[1] = 0;
}

/* put the background back where this page's bobs were */
void bobs_restore(BobList *l, uint8_t *screen, const uint8_t *bg)
{
    int i;

    for (i = 0; i < l->n[page]; i++) {
        uint16_t off = l->off[page][i];
        bob_back(screen + off, bg + off);
    }
    l->n[page] = 0;
}

void bobs_draw(BobList *l, const Bob *b, uint8_t *screen, int x, int y)
{
    uint16_t off;

    if (x < 0) {
        x = 0;
    }
    if (x > 304) {
        x = 304;
    }
    off = (uint16_t)(row_off[y] + grp_off[x]);
    bob_put(screen + off, &b->data[x & 15][0][0]);
    if (l != NULL && l->n[page] < BOBS_MAX) {
        l->off[page][l->n[page]++] = off;
    }
}
