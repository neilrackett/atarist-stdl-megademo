/*
 * STDL Megademo - part 7: Kefrens bars and the greetings (bars 48-55)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Kefrens bars, from the Amiga crew of that name: one row buffer,
 * and for every line down the screen a bar is drawn into it at a new
 * x and the buffer copied out, so each line shows every bar above it
 * stacked up - a wall of sine-walking pillars from one 16-pixel
 * sprite. Every other scanline is left black, which halves the
 * copying and lets the copper bars on Timer B show through the gaps
 * as a second layer. Then thanks, and a fade back to the top.
 *
 * Palette: 0 copper (raster), 1-7 bar gradient, 8 scroller (raster).
 */

#include <string.h>
#include "demo.h"
#include "dots.h"
#include "text.h"

#define BAR(b) ((b) * SONG_FPB)
#define KX 64                   /* kefrens columns 64..255          */
#define KG 12                   /* groups                           */
#define KROWS 78                /* drawn rows, every other line     */
#define BAND_Y 160

static uint16_t kbar[16][10];   /* per shift: 2 x (mask, p0..p3)    */
uint16_t kbuf[KG * 4 + 8];      /* the row buffer (+ slack)         */
static int16_t kx[KROWS];
static Band band;
static uint32_t lastf;
static uint16_t cop_tab[4][11], scroll_tab[RAST_MAXN];

static const char greets[] =
    "      GREETINGS TO THE CAREBEARS * THE LOST BOYS * DELTA FORCE * SYNC * "
    "OXYGENE * ST CONNEXION * AUTOMATION * POMPEY PIRATES * MEDWAY BOYS * "
    "D-BUG * LEVEL 16 * THE UNION * DHS * ULM * AND EVERYONE STILL CODING "
    "THE ST IN 2026...   STDL BY NEIL RACKETT...   ";

/*
 * rows rows: bar into the buffer at kx, buffer out to the screen,
 * two screen lines down. a6 = screen, a5 = bar variants, a4 = kx,
 * a3 = buffer, d7 = rows - 1.
 */
void kef_rows(uint8_t *dst, const uint16_t *bars, const int16_t *xs, int rows);
__asm__(
"    .text\n"
"    .even\n"
"    .globl _kef_rows\n"
"_kef_rows:\n"
"    movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"    move.l 48(%sp),%a6\n"
"    move.l 52(%sp),%a5\n"
"    move.l 56(%sp),%a4\n"
"    move.l 60(%sp),%d7\n"
"    lea    _kbuf,%a3\n"
"    subq.w #1,%d7\n"
"    bmi    9f\n"
"1:  move.w (%a4)+,%d0\n"           /* x within the columns          */
"    moveq  #15,%d1\n"
"    and.w  %d0,%d1\n"
"    add.w  %d1,%d1\n"              /* shift * 20 = (s*2)*10          */
"    move.w %d1,%d2\n"
"    lsl.w  #2,%d1\n"
"    add.w  %d2,%d1\n"
"    add.w  %d1,%d1\n"
"    lea    0(%a5,%d1.w),%a0\n"
"    lsr.w  #4,%d0\n"
"    lsl.w  #3,%d0\n"
"    lea    0(%a3,%d0.w),%a1\n"
"    moveq  #1,%d6\n"               /* two groups                    */
"2:  move.w (%a0)+,%d2\n"           /* mask: 1 keeps the buffer      */
"    move.w (%a1),%d3\n"
"    and.w  %d2,%d3\n"
"    or.w   (%a0)+,%d3\n"
"    move.w %d3,(%a1)+\n"
"    move.w (%a1),%d3\n"
"    and.w  %d2,%d3\n"
"    or.w   (%a0)+,%d3\n"
"    move.w %d3,(%a1)+\n"
"    move.w (%a1),%d3\n"
"    and.w  %d2,%d3\n"
"    or.w   (%a0)+,%d3\n"
"    move.w %d3,(%a1)+\n"
"    move.w (%a1),%d3\n"
"    and.w  %d2,%d3\n"
"    or.w   (%a0)+,%d3\n"
"    move.w %d3,(%a1)+\n"
"    dbra   %d6,2b\n"
"    move.l %a3,%a0\n"              /* 96 bytes out                  */
"    movem.l (%a0)+,%d0-%d6/%a1-%a2\n"
"    movem.l %d0-%d6/%a1-%a2,(%a6)\n"
"    movem.l (%a0)+,%d0-%d6/%a1-%a2\n"
"    movem.l %d0-%d6/%a1-%a2,36(%a6)\n"
"    movem.l (%a0)+,%d0-%d5\n"
"    movem.l %d0-%d5,72(%a6)\n"
"    lea    320(%a6),%a6\n"
"    dbra   %d7,1b\n"
"9:  movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"    rts\n"
);

void precalc_outro(void)
{
    static const uint8_t grad[16] = { 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 6, 5, 4, 3, 2, 1 };
    int s, x, p;

    memset(kbar, 0, sizeof(kbar));
    for (s = 0; s < 16; s++) {
        uint16_t *v = kbar[s];
        v[0] = v[5] = 0xFFFF;               /* keep everything ...  */
        for (x = 0; x < 16; x++) {
            int px = s + x, g = px >> 4;
            uint16_t bit = (uint16_t)(0x8000u >> (px & 15));
            v[g * 5] &= (uint16_t)~bit;     /* ... but the bar      */
            for (p = 0; p < 4; p++) {
                if (grad[x] & (1 << p)) {
                    v[g * 5 + 1 + p] |= bit;
                }
            }
        }
    }
}

static void enter(void)
{
    int k, d, i;

    demo_clear_both(0);
    lastf = 0;
    for (k = 0; k < 4; k++) {
        for (d = 0; d <= 10; d++) {
            cop_tab[k][d] = HWC(col_scale(col_hue(k * 256 + 128, 10),
                                          256 - d * 22));
        }
    }
    for (i = 0; i < RAST_MAXN; i++) {
        scroll_tab[i] = HWC(col_mix(0xFE4, 0xF42, ((i * 4 - BAND_Y) & 31) * 8));
    }
    raster_on2(4, 8);
}

static void leave(void)
{
    raster_off();
    fx_fade = 256;
}

static void frame(uint32_t f)
{
    uint16_t pal[16], *rt;
    int t = (int)(f - BAR(48));
    int kick = env_decay(sync_since(f, S_KICK), 10);
    int i, k, n, hue = t * 2;

    if (sync_hit(f, lastf, S_CRASH)) {
        fx_flash = 180;
    }
    /* the pillars: each row's bar walks on two sines */
    for (i = 0; i < KROWS; i++) {
        /* two swings or so down the screen: the pillars */
        kx[i] = (int16_t)(88 + mul14(SIN(i * 26 + t * 7), 52) +
                          mul14(SIN(i * 15 - t * 5 + 200), 34));
    }
    memset(kbuf, 0, sizeof(kbuf));
    kef_rows(scr->pixels + KX / 16 * 8, &kbar[0][0], kx, KROWS);

    /* the band: greetings, then thanks */
    band_begin(&band, 40);
    if (f < BAR(52)) {
        static int8_t down4[128];
        if (down4[0] == 0) {
            memset(down4, 4, sizeof(down4));
        }
        band_scroll(&band, greets, (int)sizeof(greets) - 1, t * 4, 32, 1, down4);
    } else {
        const char *msg = f < BAR(54) ? "THANKS FOR WATCHING" : "...AND AGAIN FROM THE TOP!";
        int len = text_len(msg), x = 160 - len * 8;
        for (i = 0; i < len; i++) {
            if (msg[i] != ' ') {
                band_glyph16(&band, x + i * 16, 12, msg[i]);
            }
        }
    }
    band_out1(scr, &band, BAND_Y, 3);   /* plane 3: colour 8 */

    /* coppers behind the pillars, a sunset down the scroller */
    rt = raster_buf();
    n = raster_n();
    for (i = 0; i < n; i++) {
        rt[2 * i] = 0;
        rt[2 * i + 1] = scroll_tab[i];
    }
    for (k = 0; k < 4; k++) {
        int cy = 20 + mul14(SIN(t * (2 + k) + k * 256), 19), d;
        for (d = -5; d <= 5; d++) {
            int e = cy + d;
            if (e >= 0 && e < BAND_Y / 4) {
                rt[2 * e] = cop_tab[k][(d < 0 ? -d : d) * 2];
            }
        }
    }

    pal[0] = 0x000;
    for (i = 1; i <= 7; i++) {
        uint16_t c = col_scale(col_hue(hue, 15), 60 + i * 28);
        pal[i] = i == 7 ? col_mix(c, 0xFFF, 140) : c;
        if (kick > 0) {
            pal[i] = col_mix(pal[i], 0xFFF, kick / 5);
        }
    }
    for (i = 8; i < 16; i++) {
        pal[i] = 0xFFF;
    }
    /* the last bar fades everything out, ready to loop */
    fx_fade = f >= BAR(55) ? 256 - (int)(f - BAR(55)) * 256 / SONG_FPB : 256;
    if (fx_fade < 0) {
        fx_fade = 0;
    }
    pal_show(pal);
    lastf = f;
}

const Part part_outro = { "outro", enter, frame, leave };
