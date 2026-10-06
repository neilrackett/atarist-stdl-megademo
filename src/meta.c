/*
 * STDL Megademo - part 6: metaballs (bars 40-47)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The climax, with the acid line. Five blobs drift and merge into
 * chrome, and a word is shouted on every clap.
 *
 * The screen does the maths. Each blob is a 3-bit falloff (0-7)
 * stored as three bitplanes, and it is ADDED into the four screen
 * planes with a ripple-carry adder done sixteen pixels at a time:
 * XOR for the sum bit, AND/OR for the carry, and a carry out of the
 * top plane saturates the group to 15. Where blobs overlap the
 * values sum, and the palette turns density into a surface - dark
 * glow, a hot rim where the sum crosses the threshold, chrome
 * inside. No pixel is ever read on its own; a 48x48 blob is about a
 * hundred and fifty groups through twenty-odd register operations.
 *
 * Palette: 0 copper (raster), 1-2 glow, 3 rim, 4-15 surface.
 */

#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "dots.h"
#include "fast.h"
#include "text.h"

#define NOINLINE __attribute__((noinline))
#define BAR(b) ((b) * SONG_FPB)
#define BR 22                   /* blob radius                      */
#define BS 44                   /* blob size                        */
#define BG 4                    /* groups a shifted blob row spans  */
#define NBLOB 5
#define RX0 48                  /* the region blobs move in         */
#define RX1 272
#define RY0 10
#define RY1 190

/* [shift][row][group][b0 b1 b2] */
static uint16_t *blob;
/* per shift and row: first non-empty group << 8 | how many */
static uint16_t spans[16][BS];
static int16_t old_x[2][NBLOB], old_y[2][NBLOB];
static int oldn[2];
static int16_t word_y[2];       /* the shouted word's rows, -1 none */
static Band word_band;          /* the current word, 1bpp           */
static int word_k = -1;
static uint32_t lastf;
static uint16_t cop_grad[4][6];
static uint16_t pal_cache[16];
static int pal_hue = -1, pal_valid;

static const char *const words[] = {
    "STDL", "68000", "YM2149", "PLANAR", "NO", "BLITTER", "WAS",
    "HARMED", "IN", "THIS", "DEMO", "!!!", "ACID", "8 MHZ", "ATARI", "ST"
};

/*
 * Add a whole blob: BS rows, each its own run of groups (the first
 * and the count packed in a word per row, the corners of the circle
 * skipped), three blob words into the screen's four planes per
 * group with a saturating ripple-carry add. Data registers only (the
 * 68000's logic ops take no address register): d0-d3 the planes,
 * d4-d6 the blob, d7 the carry.
 */
void blob_draw(const uint16_t *src, uint16_t *dst, const uint16_t *spans);
__asm__(
"    .text\n"
"    .even\n"
"    .globl _blob_draw\n"
"_blob_draw:\n"
"    movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"    move.l 48(%sp),%a0\n"          /* blob rows (this shift)       */
"    move.l 52(%sp),%a1\n"          /* screen, blob's first group   */
"    move.l 56(%sp),%a3\n"          /* per row: first << 8 | count  */
"    lea    7040(%a1),%a4\n"        /* BS (44) rows down            */
"1:  move.w (%a3)+,%d6\n"
"    move.w %d6,%d5\n"
"    lsr.w  #8,%d5\n"               /* first group                  */
"    and.w  #255,%d6\n"             /* groups                       */
"    beq    7f\n"
"    move.w %d5,%d4\n"
"    add.w  %d4,%d4\n"
"    add.w  %d5,%d4\n"
"    add.w  %d4,%d4\n"              /* first * 6 bytes              */
"    lea    0(%a0,%d4.w),%a5\n"
"    lsl.w  #3,%d5\n"
"    lea    0(%a1,%d5.w),%a6\n"
"    lsl.w  #3,%d6\n"
"    lea    0(%a6,%d6.w),%a2\n"
"2:  movem.w (%a6),%d0-%d3\n"
"    move.w (%a5)+,%d4\n"
"    move.w (%a5)+,%d5\n"
"    move.w (%a5)+,%d6\n"
"    move.w %d0,%d7\n"              /* bit 0                        */
"    and.w  %d4,%d7\n"
"    eor.w  %d4,%d0\n"
"    move.w %d1,%d4\n"              /* bit 1                        */
"    and.w  %d5,%d4\n"
"    eor.w  %d5,%d1\n"
"    move.w %d1,%d5\n"
"    eor.w  %d7,%d1\n"
"    and.w  %d5,%d7\n"
"    or.w   %d4,%d7\n"
"    move.w %d2,%d4\n"              /* bit 2                        */
"    and.w  %d6,%d4\n"
"    eor.w  %d6,%d2\n"
"    move.w %d2,%d5\n"
"    eor.w  %d7,%d2\n"
"    and.w  %d5,%d7\n"
"    or.w   %d4,%d7\n"
"    move.w %d3,%d4\n"              /* bit 3, and the overflow      */
"    and.w  %d7,%d4\n"
"    eor.w  %d7,%d3\n"
"    or.w   %d4,%d0\n"              /* saturate to 15               */
"    or.w   %d4,%d1\n"
"    or.w   %d4,%d2\n"
"    or.w   %d4,%d3\n"
"    movem.w %d0-%d3,(%a6)\n"
"    addq.l #8,%a6\n"
"    cmp.l  %a6,%a2\n"
"    bne.s  2b\n"
"7:  lea    24(%a0),%a0\n"          /* BG * 3 words a blob row      */
"    lea    160(%a1),%a1\n"
"    cmp.l  %a1,%a4\n"
"    bne    1b\n"
"    movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"    rts\n"
);

void precalc_meta(void)
{
    int s, r, x, b;

    blob = malloc(16 * BS * BG * 3 * sizeof(uint16_t));
    if (blob == NULL) {
        mem_fail = 1;
        return;
    }
    for (r = 0; r < BS; r++) {
        int dy = 2 * r - (BS - 1);              /* in half pixels  */
        uint16_t pl[3][4];
        memset(pl, 0, sizeof(pl));
        for (x = 0; x < BS; x++) {
            int dx = 2 * x - (BS - 1);
            uint32_t d2 = (uint32_t)(MUL16(dx, dx) + MUL16(dy, dy));
            uint32_t q, v;
            if (d2 >= 4 * BR * BR) {
                continue;
            }
            /* (1 - d^2/R^2)^2 in 0..256, times 7 with rounding */
            q = d2 << 8;
            __asm__("divu.w %1,%0" : "+d"(q) : "d"((uint16_t)(4 * BR * BR)));
            q = 256 - (q & 0xFFFF);
            v = (uint32_t)MUL16(q, q);
            v = ((v << 3) - v + 32768) >> 16;
            for (b = 0; b < 3; b++) {
                if (v & (1 << b)) {
                    pl[b][x >> 4] |= (uint16_t)(0x8000u >> (x & 15));
                }
            }
        }
        for (s = 0; s < 16; s++) {
            uint16_t *w = blob + (s * BS + r) * BG * 3;
            int g;
            for (g = 0; g < BG; g++) {
                for (b = 0; b < 3; b++) {
                    uint32_t win = ((uint32_t)(g ? pl[b][g - 1] : 0) << 16) |
                                   (g < 3 ? pl[b][g] : 0);
                    w[g * 3 + b] = (uint16_t)(win >> s);
                }
            }
        }
    }
    for (s = 0; s < 16; s++) {
        for (r = 0; r < BS; r++) {
            int g0 = -1, g1 = -1, g;
            for (g = 0; g < BG; g++) {
                const uint16_t *w = blob + ((s * BS + r) * BG + g) * 3;
                if (w[0] | w[1] | w[2]) {
                    if (g0 < 0) {
                        g0 = g;
                    }
                    g1 = g;
                }
            }
            spans[s][r] = (uint16_t)(g0 < 0 ? 0 : (g0 << 8) | (g1 - g0 + 1));
        }
    }
}

static void enter(void)
{
    int i;

    demo_clear_both(0);
    oldn[0] = oldn[1] = 0;
    word_y[0] = word_y[1] = -1;
    word_k = -1;
    pal_valid = 0;
    lastf = 0;
    for (i = 0; i < 4; i++) {
        int d;
        for (d = 0; d < 6; d++) {
            cop_grad[i][d] = HWC(col_scale(col_hue(i * 256 + 64, 9), 256 - d * 44));
        }
    }
    raster_on2(4, 0);
}

static void leave(void)
{
    raster_off();
}


static NOINLINE void blobs(uint32_t f, int kick)
{
    int t = (int)(f - BAR(40));
    int i;

    for (i = 0; i < oldn[page]; i++) {
        clear_box(scr->pixels + row_off[old_y[page][i]] + (old_x[page][i] >> 4) * 8,
                  BG, BS);
    }
    for (i = 0; i < NBLOB; i++) {
        /* close orbits, so they meet and merge */
        int x = 136 + mul14(SIN(t * (3 + i) + i * 205), 46);
        int y = 76 + mul14(COS(t * (2 + (i & 1)) + i * 330 + kick), 34);
        int s = x & 15;
        blob_draw(blob + (s * BS) * BG * 3,
                  (uint16_t *)(scr->pixels + row_off[y]) + (x >> 4) * 4,
                  spans[s]);
        old_x[page][i] = (int16_t)(x & ~15);
        old_y[page][i] = (int16_t)y;
    }
    oldn[page] = NBLOB;
}

static void frame(uint32_t f)
{
    uint16_t pal[16], *rt;
    int kick = env_decay(sync_since(f, S_KICK), 10);
    int clap = sync_since(f, S_CLAP);
    int t = (int)(f - BAR(40));
    int hue = t * 3, i, n;

    if (blob == NULL) {
        return;
    }
    if (sync_hit(f, lastf, S_CRASH | S_BOOM)) {
        fx_flash = 220;
    }
    /* the last shouted word on this page goes first */
    if (word_y[page] >= 0) {
        band_clear(scr, word_y[page], 32);
        word_y[page] = -1;
    }
    blobs(f, kick);
    if (clap < 18) {
        int k = (int)((f - (uint32_t)clap) / (SONG_FPB / 2)) % 16;
        int y = 84 - (18 - clap) / 3;
        /* drawn into a 1bpp band once per word, then ORed into plane
         * 3 alone each frame: any density plus 8 is in the white end
         * of the ramp */
        if (k != word_k) {
            const char *w = words[k];
            int n = text_len(w), x = 160 - n * 16, c;
            band_begin(&word_band, 32);
            for (c = 0; c < n; c++) {
                if (w[c] != ' ') {
                    band_glyph32(&word_band, x + c * 32, 0, w[c]);
                }
            }
            word_k = k;
        }
        band_or1(&word_band.buf[0][1], scr->pixels + row_off[y] + 6, 32);
        word_y[page] = (int16_t)y;
    }

    rt = raster_buf();
    n = raster_n();
    for (i = 0; i < n; i++) {
        rt[i] = 0;                  /* colour 0 alone: a word an entry */
    }
    {
        int k;
        for (k = 0; k < 4; k++) {
            int cy = 25 + mul14(SIN(t * (3 + k) + k * 256), 23), d;
            for (d = -5; d <= 5; d++) {
                int e = cy + d;
                if (e >= 0 && e < n) {
                    rt[e] = cop_grad[k][d < 0 ? -d : d];
                }
            }
        }
    }

    /* density is height: dark glow, a hot rim where it crosses 3,
     * then a lit surface climbing to white where blobs merge (the hue
     * creeps, so the ramp is rebuilt only when it has moved) */
    if ((hue >> 3) == pal_hue && kick == 0 && pal_valid) {
        pal_show(pal_cache);
        lastf = f;
        return;
    }
    pal[0] = 0x000;
    pal[1] = col_scale(col_hue(hue + 512, 15), 50);
    pal[2] = col_scale(col_hue(hue + 512, 15), 90);
    pal[3] = col_scale(col_hue(hue + 560, 15), 70);   /* dark edge */
    for (i = 4; i < 16; i++) {
        /* lit from the middle: brighter with height, white where
         * two blobs pile up */
        int lum = 70 + (i - 4) * 32;
        pal[i] = lum <= 256 ? col_scale(col_hue(hue, 15), lum)
                            : col_mix(col_hue(hue, 15), 0xFFF, lum - 256);
    }
    if (kick > 0) {
        for (i = 3; i < 16; i++) {
            pal[i] = col_mix(pal[i], 0xFFF, kick / 4);
        }
    }
    memcpy(pal_cache, pal, sizeof(pal));
    pal_hue = hue >> 3;
    pal_valid = kick == 0;
    pal_show(pal);
    lastf = f;
}

const Part part_meta = { "meta", enter, frame, leave };
