/*
 * STDL Megademo - the native inner loops
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Special cases the effects lean on every frame, where STDL's
 * general routines - which clip, keep masks, honour the plane budget
 * and dispatch on colour for every call - pay more in set-up than
 * the pixels cost. Each is a straight 68000 loop over memory the
 * demo owns outright.
 */

#include "demo.h"
#include "fast.h"

/* left and right edge masks: 0xFFFF >> s, then 0xFFFF << (15 - s) */
const uint16_t edge_masks[32] = {
    0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
    0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001,
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF
};

/*
 * Zero a box of whole groups: `groups` wide (multiple of 2 up to
 * 20), `rows` tall, from `p` with the screen's 160-byte stride.
 * Eight longs - two groups... four groups - at a time through movem.
 */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _clear_box\n"
"_clear_box:\n"
"    movem.l %d2-%d7/%a2-%a3,-(%sp)\n"
"    move.l 36(%sp),%a0\n"          /* top-left group              */
"    move.l 40(%sp),%d0\n"          /* groups                      */
"    move.l 44(%sp),%a3\n"          /* rows                        */
"    moveq  #0,%d1\n"
"    moveq  #0,%d2\n"
"    moveq  #0,%d3\n"
"    moveq  #0,%d4\n"
"    moveq  #0,%d5\n"
"    moveq  #0,%d6\n"
"    moveq  #0,%d7\n"
"    move.l %d7,%a2\n"
"    lsl.w  #3,%d0\n"               /* bytes a row                 */
"    move.w %d0,%a1\n"
"1:  lea    0(%a0,%a1.w),%a0\n"     /* clear right to left         */
"    move.w %a1,%d0\n"
"2:  cmp.w  #32,%d0\n"
"    blt.s  3f\n"
"    movem.l %d1-%d7/%a2,-(%a0)\n"
"    sub.w  #32,%d0\n"
"    bne.s  2b\n"
"    bra.s  4f\n"
"3:  move.l %d1,-(%a0)\n"
"    move.l %d1,-(%a0)\n"
"    move.l %d1,-(%a0)\n"
"    move.l %d1,-(%a0)\n"
"4:  lea    160(%a0),%a0\n"
"    subq.l #1,%a3\n"
"    move.l %a3,%d0\n"
"    bne.s  1b\n"
"    movem.l (%sp)+,%d2-%d7/%a2-%a3\n"
"    rts\n"
);

/*
 * The scroller band out to the screen: every group of every row,
 * each band word written into the planes its row's colour has and
 * zero into the rest (masks: two longs a row, planes 0:1 and 2:3).
 * About 74 cycles a group, against 143 for gcc's version.
 */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _band_rows\n"
"_band_rows:\n"
"    movem.l %d2-%d7/%a2,-(%sp)\n"
"    move.l 32(%sp),%a0\n"          /* band words, row 0 word 1    */
"    move.l 36(%sp),%a1\n"          /* screen row                  */
"    move.l 40(%sp),%d7\n"          /* rows                        */
"    move.l 44(%sp),%a2\n"          /* masks                       */
"    subq.w #1,%d7\n"
"    bmi.s  9f\n"
"1:  move.l (%a2)+,%d3\n"
"    move.l (%a2)+,%d4\n"
"    moveq  #19,%d6\n"
"2:  move.w (%a0)+,%d0\n"
"    move.w %d0,%d1\n"
"    swap   %d1\n"
"    move.w %d0,%d1\n"
"    move.l %d1,%d2\n"
"    and.l  %d3,%d1\n"
"    and.l  %d4,%d2\n"
"    move.l %d1,(%a1)+\n"
"    move.l %d2,(%a1)+\n"
"    dbra   %d6,2b\n"
"    addq.l #8,%a0\n"               /* the band's slack words      */
"    dbra   %d7,1b\n"
"9:  movem.l (%sp)+,%d2-%d7/%a2\n"
"    rts\n"
);

/*
 * Per-x tables for span fills: the byte offset of x's group in the
 * high word, and in the low word the mask from x to the right end
 * of the group (left edges) or from the left end to x (right edges).
 */
uint32_t span_l[320], span_r[320];

void fast_init(void)
{
    int x;

    for (x = 0; x < 320; x++) {
        span_l[x] = ((uint32_t)((x >> 4) * 8) << 16) | edge_masks[x & 15];
        span_r[x] = ((uint32_t)((x >> 4) * 8) << 16) | edge_masks[16 + (x & 15)];
    }
}

/*
 * A trapezoid XORed into one plane: rows rows from `row` (the plane's
 * byte in the first row's line), left and right edges stepped in
 * registers. *xl / *xr are 16.16 with the half-pixel rounding
 * already added; they come back stepped past the last row, so a
 * triangle's long edge carries straight on into its second half.
 * About 200 cycles a row plus 20 a middle group.
 */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _xor_trap\n"
"_xor_trap:\n"
"    movem.l %d2-%d7/%a2-%a5,-(%sp)\n"
"    move.l 44(%sp),%a3\n"          /* row (plane byte)            */
"    move.l 48(%sp),%a0\n"          /* &xl                         */
"    move.l 52(%sp),%d4\n"          /* dxl                         */
"    move.l 56(%sp),%a1\n"          /* &xr                         */
"    move.l 60(%sp),%d5\n"          /* dxr                         */
"    move.l 64(%sp),%d7\n"          /* rows                        */
"    move.l (%a0),%d0\n"
"    move.l (%a1),%d1\n"
"    lea    _span_l,%a4\n"
"    lea    _span_r,%a5\n"
"    subq.w #1,%d7\n"
"    bmi.s  9f\n"
"1:  move.l %d0,%d2\n"
"    swap   %d2\n"                  /* first pixel                 */
"    move.l %d1,%d3\n"
"    swap   %d3\n"                  /* one past the last           */
"    cmp.w  %d2,%d3\n"
"    ble.s  8f\n"
"    add.w  %d2,%d2\n"
"    add.w  %d2,%d2\n"
"    move.l 0(%a4,%d2.w),%d2\n"     /* offset:left mask            */
"    add.w  %d3,%d3\n"
"    add.w  %d3,%d3\n"
"    move.l -4(%a5,%d3.w),%d3\n"    /* last pixel: offset:right mask */
"    move.l %d2,%d6\n"
"    swap   %d6\n"
"    lea    0(%a3,%d6.w),%a2\n"
"    swap   %d3\n"
"    sub.w  %d6,%d3\n"              /* bytes between the groups    */
"    bne.s  2f\n"
"    swap   %d3\n"
"    and.w  %d3,%d2\n"              /* one group: both masks       */
"    eor.w  %d2,(%a2)\n"
"    bra.s  8f\n"
"2:  eor.w  %d2,(%a2)\n"
"    move.w %d3,%d6\n"
"    swap   %d3\n"
"    eor.w  %d3,0(%a2,%d6.w)\n"
"    lsr.w  #3,%d6\n"
"    subq.w #2,%d6\n"               /* middle groups - 1           */
"    bmi.s  8f\n"
"3:  addq.l #8,%a2\n"
"    not.w  (%a2)\n"
"    dbra   %d6,3b\n"
"8:  add.l  %d4,%d0\n"
"    add.l  %d5,%d1\n"
"    lea    160(%a3),%a3\n"
"    dbra   %d7,1b\n"
"9:  move.l %d0,(%a0)\n"
"    move.l %d1,(%a1)\n"
"    movem.l (%sp)+,%d2-%d7/%a2-%a5\n"
"    rts\n"
);

/* one plane of the band: 20 band words a row into every fourth word */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _band_rows1\n"
"_band_rows1:\n"
"    move.l 4(%sp),%a0\n"
"    move.l 8(%sp),%a1\n"
"    move.l 12(%sp),%d1\n"
"    subq.w #1,%d1\n"
"    bmi.s  9f\n"
"1:  move.w (%a0)+,(%a1)\n"
"    move.w (%a0)+,8(%a1)\n"
"    move.w (%a0)+,16(%a1)\n"
"    move.w (%a0)+,24(%a1)\n"
"    move.w (%a0)+,32(%a1)\n"
"    move.w (%a0)+,40(%a1)\n"
"    move.w (%a0)+,48(%a1)\n"
"    move.w (%a0)+,56(%a1)\n"
"    move.w (%a0)+,64(%a1)\n"
"    move.w (%a0)+,72(%a1)\n"
"    move.w (%a0)+,80(%a1)\n"
"    move.w (%a0)+,88(%a1)\n"
"    move.w (%a0)+,96(%a1)\n"
"    move.w (%a0)+,104(%a1)\n"
"    move.w (%a0)+,112(%a1)\n"
"    move.w (%a0)+,120(%a1)\n"
"    move.w (%a0)+,128(%a1)\n"
"    move.w (%a0)+,136(%a1)\n"
"    move.w (%a0)+,144(%a1)\n"
"    move.w (%a0)+,152(%a1)\n"
"    addq.l #8,%a0\n"
"    lea    160(%a1),%a1\n"
"    dbra   %d1,1b\n"
"9:  rts\n"
);

/*
 * Glyph rows into a scroller band (rows BAND_W words apart):
 * 16-pixel rows as one long OR at the shift, 32-pixel rows split
 * across three words.
 */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _glyph16_rows\n"
"_glyph16_rows:\n"                  /* (glyph, dst, rows, shift)   */
"    move.l 4(%sp),%a0\n"
"    move.l 8(%sp),%a1\n"
"    move.l 12(%sp),%d1\n"
"    move.l %d2,-(%sp)\n"
"    move.l 20(%sp),%d2\n"
"    subq.w #1,%d1\n"
"    bmi.s  9f\n"
"1:  moveq  #0,%d0\n"
"    move.w (%a0)+,%d0\n"
"    beq.s  2f\n"
"    lsl.l  %d2,%d0\n"
"    or.l   %d0,(%a1)\n"
"2:  lea    48(%a1),%a1\n"
"    dbra   %d1,1b\n"
"9:  move.l (%sp)+,%d2\n"
"    rts\n"
"    .even\n"
"    .globl _glyph32_rows\n"
"_glyph32_rows:\n"                  /* (glyph, dst, rows, shift)   */
"    move.l 4(%sp),%a0\n"
"    move.l 8(%sp),%a1\n"
"    move.l 12(%sp),%d1\n"
"    movem.l %d2-%d4,-(%sp)\n"
"    move.l 28(%sp),%d2\n"          /* right shift 0..15           */
"    moveq  #16,%d4\n"
"    sub.w  %d2,%d4\n"              /* the spill's left shift      */
"    subq.w #1,%d1\n"
"    bmi.s  9f\n"
"1:  move.l (%a0)+,%d0\n"
"    beq.s  2f\n"
"    move.l %d0,%d3\n"
"    lsr.l  %d2,%d0\n"
"    or.l   %d0,(%a1)\n"
"    lsl.w  %d4,%d3\n"              /* 16 when unshifted: clears it */
"    or.w   %d3,4(%a1)\n"
"2:  lea    48(%a1),%a1\n"
"    dbra   %d1,1b\n"
"9:  movem.l (%sp)+,%d2-%d4\n"
"    rts\n"
);

/* OR one plane of band rows (20 words a row) onto the screen */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _band_or1\n"
"_band_or1:\n"
"    move.l %d2,-(%sp)\n"          /* d2 is the caller's          */
"    move.l 8(%sp),%a0\n"
"    move.l 12(%sp),%a1\n"
"    move.l 16(%sp),%d1\n"
"    subq.w #1,%d1\n"
"    bmi.s  9f\n"
"1:  moveq  #19,%d0\n"
"2:  move.w (%a0)+,%d2\n"
"    or.w   %d2,(%a1)\n"
"    addq.l #8,%a1\n"
"    dbra   %d0,2b\n"
"    addq.l #8,%a0\n"
"    dbra   %d1,1b\n"
"9:  move.l (%sp)+,%d2\n"
"    rts\n"
);

/*
 * xor_trap at half vertical resolution: each step covers a pair of
 * rows with one span (the caller steps the edges two rows at a time).
 * For the glenz's back faces, which are only ever seen through the
 * front ones: half the per-row overhead for a stair the glass hides.
 */
__asm__(
"    .text\n"
"    .even\n"
"    .globl _xor_trap2\n"
"_xor_trap2:\n"
"    movem.l %d2-%d7/%a2-%a5,-(%sp)\n"
"    move.l 44(%sp),%a3\n"          /* first row of the pair       */
"    move.l 48(%sp),%a0\n"
"    move.l 52(%sp),%d4\n"
"    move.l 56(%sp),%a1\n"
"    move.l 60(%sp),%d5\n"
"    move.l 64(%sp),%d7\n"          /* pairs                       */
"    move.l (%a0),%d0\n"
"    move.l (%a1),%d1\n"
"    lea    _span_l,%a4\n"
"    lea    _span_r,%a5\n"
"    subq.w #1,%d7\n"
"    bmi    9f\n"
"    move.l %a1,-(%sp)\n"
"    move.l %a0,-(%sp)\n"
"1:  move.l %d0,%d2\n"
"    swap   %d2\n"
"    move.l %d1,%d3\n"
"    swap   %d3\n"
"    cmp.w  %d2,%d3\n"
"    ble.s  8f\n"
"    add.w  %d2,%d2\n"
"    add.w  %d2,%d2\n"
"    move.l 0(%a4,%d2.w),%d2\n"
"    add.w  %d3,%d3\n"
"    add.w  %d3,%d3\n"
"    move.l -4(%a5,%d3.w),%d3\n"
"    move.l %d2,%d6\n"
"    swap   %d6\n"
"    lea    0(%a3,%d6.w),%a2\n"
"    swap   %d3\n"
"    sub.w  %d6,%d3\n"
"    bne.s  2f\n"
"    swap   %d3\n"
"    and.w  %d3,%d2\n"
"    eor.w  %d2,(%a2)\n"
"    eor.w  %d2,160(%a2)\n"
"    bra.s  8f\n"
"2:  eor.w  %d2,(%a2)\n"
"    eor.w  %d2,160(%a2)\n"
"    lea    0(%a2,%d3.w),%a1\n"     /* the right group             */
"    move.w %d3,%d6\n"
"    swap   %d3\n"
"    eor.w  %d3,(%a1)\n"
"    eor.w  %d3,160(%a1)\n"
"    lsr.w  #3,%d6\n"
"    subq.w #2,%d6\n"
"    bmi.s  8f\n"
"3:  addq.l #8,%a2\n"
"    not.w  (%a2)\n"
"    not.w  160(%a2)\n"
"    dbra   %d6,3b\n"
"8:  add.l  %d4,%d0\n"
"    add.l  %d5,%d1\n"
"    lea    320(%a3),%a3\n"
"    dbra   %d7,1b\n"
"    move.l (%sp)+,%a0\n"
"    move.l (%sp)+,%a1\n"
"    move.l %d0,(%a0)\n"
"    move.l %d1,(%a1)\n"
"9:  movem.l (%sp)+,%d2-%d7/%a2-%a5\n"
"    rts\n"
);
