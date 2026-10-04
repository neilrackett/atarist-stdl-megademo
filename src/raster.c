/*
 * STDL Megademo - Timer B rasters (native)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The oldest trick on the machine: Timer B counts Display Enable
 * events - one per displayed line - and interrupts every `step`
 * lines, and the handler writes the next colour 0 and colour 15 from
 * a table. Colour 0 is also the border, so the bars run edge to edge
 * through the side borders; colour 15 gives text and the analyser a
 * gradient no 16-colour palette could.
 *
 * Two things keep the bars still:
 *  - the VBL callback (core.c) stops the timer, writes the first
 *    entry and restarts it before the picture begins, so line 0 of
 *    every frame starts from a known state;
 *  - TOS's 200Hz Timer C handler runs for over two scanlines at the
 *    CPU's level 6, which would hold the raster interrupt off and
 *    shake a bar by two lines four times a frame. A prefix on its
 *    vector lowers the mask to 5 first - STDL's overscan module does
 *    the same - so Timer B (MFP channel 8, above Timer C's 5) nests
 *    into it.
 *
 * Never on while an STDL border is open: those own Timer B.
 */

#include "demo.h"

#define MFP_IERA (*(volatile uint8_t *)0xfffffa07UL)
#define MFP_IMRA (*(volatile uint8_t *)0xfffffa13UL)
#define MFP_TBCR (*(volatile uint8_t *)0xfffffa1bUL)
#define MFP_TBDR (*(volatile uint8_t *)0xfffffa21UL)
#define VEC_TB   (*(volatile uint32_t *)0x120UL)
#define VEC_TC   (*(volatile uint32_t *)0x114UL)
#define COL0     (*(volatile uint16_t *)0xffff8240UL)
#define COL15    (*(volatile uint16_t *)0xffff825eUL)
#define COL8     (*(volatile uint16_t *)0xffff8250UL)

uint16_t *volatile rast_ptr;        /* read by the ISR                 */
uint32_t rast_oldtc;                /* TOS's Timer C handler           */

static uint16_t tab[2][RAST_MAXN * 2];
static volatile int8_t tab_show = -1; /* table the VBL arms, -1 = none */
static int tab_draw;
static uint8_t drawn;               /* raster_buf asked for since commit */
static volatile uint8_t on;
static uint8_t step, second;
static int ntab;
static uint32_t old_tb;
static uint8_t old_iera, old_imra, old_tbcr;

void rast_isr(void);
void rast_isr8(void);
void rast_isr0(void);
void rast_tc(void);

__asm__(
"    .text\n"
"    .even\n"
"    .globl _rast_isr\n"
"_rast_isr:\n"
"    move.l %a0,-(%sp)\n"
"    move.l _rast_ptr,%a0\n"
"    move.w (%a0)+,0xffff8240.w\n"
"    move.w (%a0)+,0xffff825e.w\n"
"    move.l %a0,_rast_ptr\n"
"    move.l (%sp)+,%a0\n"
"    move.b #0xFE,0xfffffa0f.w\n"   /* end of interrupt: Timer B */
"    rte\n"
/* the same, second colour 8: text in plane 3 alone */
"    .even\n"
"    .globl _rast_isr8\n"
"_rast_isr8:\n"
"    move.l %a0,-(%sp)\n"
"    move.l _rast_ptr,%a0\n"
"    move.w (%a0)+,0xffff8240.w\n"
"    move.w (%a0)+,0xffff8250.w\n"
"    move.l %a0,_rast_ptr\n"
"    move.l (%sp)+,%a0\n"
"    move.b #0xFE,0xfffffa0f.w\n"
"    rte\n"
/* colour 0 alone: the second word of each entry is skipped */
"    .even\n"
"    .globl _rast_isr0\n"
"_rast_isr0:\n"
"    move.l %a0,-(%sp)\n"
"    move.l _rast_ptr,%a0\n"
"    move.w (%a0),0xffff8240.w\n"
"    addq.l #4,%a0\n"
"    move.l %a0,_rast_ptr\n"
"    move.l (%sp)+,%a0\n"
"    move.b #0xFE,0xfffffa0f.w\n"
"    rte\n"
"    .even\n"
"    .globl _rast_tc\n"
"_rast_tc:\n"
"    move.w #0x2500,%sr\n"
"    move.l _rast_oldtc,-(%sp)\n"
"    rts\n"
);

static inline uint16_t irq_off(void)
{
    uint16_t sr;
    __asm__ volatile("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) :: "cc");
    return sr;
}

static inline void irq_restore(uint16_t sr)
{
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "cc");
}

/* from the VBL callback, before the picture starts */
void raster_vbl(void)
{
    const uint16_t *t;

    if (!on || tab_show < 0) {
        return;
    }
    t = tab[(int)tab_show];
    MFP_TBCR = 0;
    COL0 = t[0];
    if (second == 8) {
        COL8 = t[1];
    } else if (second == 15) {
        COL15 = t[1];
    }
    rast_ptr = (uint16_t *)t + 2;
    MFP_TBDR = step;
    MFP_TBCR = 8;                   /* event count: Display Enable     */
}

int raster_on(int s)
{
    return raster_on2(s, 15);
}

int raster_on2(int s, int sec)
{
    uint16_t sr;
    int i;

    if (on) {
        raster_off();
    }
    if (s < 1) {
        s = 1;
    }
    step = (uint8_t)s;
    ntab = 200 / s + 1;
    for (i = 0; i < 2; i++) {
        uint16_t *p = tab[i];
        int k;
        for (k = 0; k < RAST_MAXN * 2; k++) {
            p[k] = 0;
        }
    }
    tab_draw = 0;
    tab_show = 1;                   /* a black table until the first show */

    sr = irq_off();
    old_tb = VEC_TB;
    old_iera = MFP_IERA;
    old_imra = MFP_IMRA;
    old_tbcr = MFP_TBCR;
    MFP_TBCR = 0;
    second = (uint8_t)sec;
    VEC_TB = sec == 8 ? (uint32_t)rast_isr8 :
             sec == 15 ? (uint32_t)rast_isr : (uint32_t)rast_isr0;
    rast_ptr = tab[1] + 2;
    rast_oldtc = VEC_TC;
    VEC_TC = (uint32_t)rast_tc;
    MFP_IERA |= 1;
    MFP_IMRA |= 1;
    on = 1;
    irq_restore(sr);
    return ntab;
}

void raster_off(void)
{
    uint16_t sr;

    if (!on) {
        return;
    }
    sr = irq_off();
    on = 0;
    MFP_TBCR = 0;
    MFP_IERA = (uint8_t)((MFP_IERA & ~1) | (old_iera & 1));
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~1) | (old_imra & 1));
    VEC_TB = old_tb;
    /* only unhook Timer C if nobody hooked it after us */
    if (VEC_TC == (uint32_t)rast_tc) {
        VEC_TC = rast_oldtc;
    }
    MFP_TBCR = old_tbcr;
    irq_restore(sr);
}

int raster_active(void)
{
    return on;
}

uint16_t *raster_buf(void)
{
    drawn = 1;
    return tab[tab_draw];
}

int raster_n(void)
{
    return ntab;
}

/* hand the filled table to the VBL; from demo_flip, before the flip */
void raster_commit(void)
{
    if (!on || !drawn) {
        return;
    }
    drawn = 0;
    tab_show = (int8_t)tab_draw;
    tab_draw ^= 1;
}
