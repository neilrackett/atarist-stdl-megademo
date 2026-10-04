/*
 * STDL Megademo - part 2: plasma, and the borders go (bars 8-15)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An Atari ST tribute. Bars 8-11: a palette-cycled plasma boxed in
 * the middle of the screen, copper bars (Timer B) all round it and
 * through the side borders, a scroller underneath. Bar 12, on the
 * boom: the rasters stop, STDL opens the top and bottom borders
 * together - 273 lines - the plasma fills everything above a
 * scroller that now runs in the old lower border, and a ring of bobs
 * flies through where the borders used to be. For Level 16 and the
 * Union Demo, 1989.
 *
 * The plasma costs nothing per frame: it is drawn once (precalc,
 * 11 colours with ordered dithering) and all its motion is the
 * eleven palette entries rotating, pumped by the kick.
 *
 * Palette: 0 copper/black, 1-11 plasma wheel, 12-15 box bevel then
 * bob gold (the scroller in the border shares the gold).
 */

#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "dots.h"
#include "text.h"
#include "bobs.h"

#define BAR(b) ((b) * SONG_FPB)
#define PL_H 273                /* the plasma covers a full overscan */
#define BOX_X 32
#define BOX_Y 14
#define BOX_W 256
#define BOX_H 150
#define TOP 28                  /* rows the top border adds above    */
#define NBOBS 14

static STDL_Surface *plasma;    /* PL_H rows, indices 1..11          */
static Bob *bob;
static BobList bobl;
static Band band;
static int open;                /* borders open (phase B)            */
static uint32_t lastf;

static const char text_a[] =
    "      HOLD ON TO YOUR SEATS...   THIS PLASMA IS BOXED IN BY THE SAME "
    "BORDER EVERY ST OWNER STARED AT FOR YEARS...   BUT NOT FOR LONG...      ";
static const char text_b[] =
    "BORDERS? WHAT BORDERS?!   273 LINES OF OVERSCAN, OPENED BY STDL ...   "
    "RESPECT TO LEVEL 16 AND THE UNION DEMO, 1989 !!!   BOBS IN THE TOP "
    "BORDER, A SCROLLER IN THE BOTTOM ONE...          ";

/* the 40-row band in the old lower border: 4 clear rows, 32 of text
 * from bright gold down to dark, 4 clear */
static const uint8_t gold_rows[40] = {
    15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 14, 13,
    13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12
};
static int8_t down4[128];

/*
 * sqrt(n) * 16 for every n the two ring centres can produce, built by
 * walking the squares upward - no multiply, no divide, about a second
 * less loader than a bit-by-bit root per cell. Freed after use.
 */
#define SQ_MAX 34000
static uint16_t *sqrt_table(void)
{
    uint16_t *t = malloc(sizeof(uint16_t) * SQ_MAX);
    uint32_t k = 0, next = 1, n, n256 = 0;

    if (t == NULL) {
        return NULL;
    }
    for (n = 0; n < SQ_MAX; n++, n256 += 256) {
        while (next <= n256) {          /* next = (k + 1)^2 */
            next += 2 * k + 3;
            k++;
        }
        t[n] = (uint16_t)k;
    }
    return t;
}

/*
 * Pack one more pixel into a group being built: planes 0 and 1 ride
 * in the two halves of one long, 2 and 3 in another, so a pixel is a
 * shift, a mask (the low half's top bit must not climb into the high
 * half) and an OR from a table per pair.
 */
static uint32_t spread01[16], spread23[16];
#define PACK(a, b, c) do { \
        a = ((a << 1) & 0xFFFEFFFEu) | spread01[c]; \
        b = ((b << 1) & 0xFFFEFFFEu) | spread23[c]; \
    } while (0)

void precalc_plasma(void)
{
    /* 4x4 ordered dither: sixteen levels between two colours */
    static const uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    uint16_t *sq = sqrt_table();
    uint32_t *pat01, *pat23;
    uint8_t mod11[48];
    int cx, cy, i;

    memset(down4, 4, sizeof(down4));
    for (i = 0; i < 48; i++) {
        mod11[i] = (uint8_t)(i % 11 + 1);
    }
    for (i = 0; i < 16; i++) {
        spread01[i] = ((uint32_t)(i & 1) << 16) | ((i >> 1) & 1);
        spread23[i] = ((uint32_t)((i >> 2) & 1) << 16) | ((i >> 3) & 1);
    }
    /* every cell row there can be: the four dithered pixels of
     * level u (stored halved, 0..220) on Bayer row r, packed as
     * nibbles - planes 0:1 in one long, 2:3 in another */
    pat01 = malloc(4 * 221 * sizeof(uint32_t));
    pat23 = malloc(4 * 221 * sizeof(uint32_t));
    if (pat01 != NULL && pat23 != NULL) {
        int r, uu;
        for (r = 0; r < 4; r++) {
            for (uu = 0; uu < 221; uu++) {
                uint32_t a = 0, b = 0;
                int px;
                for (px = 0; px < 4; px++) {
                    PACK(a, b, mod11[(uu * 2 + bayer[r][px]) >> 4]);
                }
                pat01[r * 221 + uu] = a;
                pat23[r * 221 + uu] = b;
            }
        }
    }
    plasma = STDL_CreateSurface(320, PL_H);
    if (plasma == NULL || sq == NULL || pat01 == NULL || pat23 == NULL) {
        mem_fail = 1;
    } else {
        /* 4x4 cells: 80 across, 69 down; cell units are 2px of the
         * old field, so the waves keep their shape */
        for (cy = 0; cy < (PL_H + 3) / 4; cy++) {
            int fy = cy * 2;
            int dy = fy - 70, dy2 = fy - 120;
            int32_t ddy = MUL16(dy, dy), ddy2 = MUL16(dy2, dy2);
            int sy = SIN(MUL16(fy, 11) + 200);
            uint8_t u[80];
            int r;
            for (cx = 0; cx < 80; cx++) {
                int x = cx * 2;
                int dx = x - 80, dx2 = x - 20;
                int d = sq[MUL16(dx, dx) + ddy];
                int d2 = sq[MUL16(dx2, dx2) + ddy2];
                int32_t v = SIN(MUL16(x, 9)) + sy +
                            SIN(MUL16(x + fy, 5)) + SIN(MUL16(d, 22) >> 4) +
                            SIN((MUL16(d2, 13) >> 4) + 300);
                /* five waves, +-81920 -> 0..440: two and a half turns
                 * of the 11-colour wheel in 1/16 steps (stored /2) */
                u[cx] = (uint8_t)(MUL16((uint16_t)((v + 81920) >> 4) >> 1, 11) >> 8);
            }
            for (r = 0; r < 4 && cy * 4 + r < PL_H; r++) {
                uint32_t *row = (uint32_t *)(plasma->pixels + row_off[cy * 4 + r]);
                const uint32_t *p01 = pat01 + r * 221, *p23 = pat23 + r * 221;
                for (cx = 0; cx < 80; cx += 4) {
                    uint32_t a = 0, b = 0;
                    int k;
                    for (k = 0; k < 4; k++) {
                        /* a whole 4-pixel cell: four bits a plane */
                        a = ((a << 4) & 0xFFF0FFF0u) | p01[u[cx + k]];
                        b = ((b << 4) & 0xFFF0FFF0u) | p23[u[cx + k]];
                    }
                    row[0] = a;
                    row[1] = b;
                    row += 2;
                }
            }
        }
    }
    free(sq);
    free(pat01);
    free(pat23);

    /* bobs in colours 12..15 */
    {
        uint8_t map[16];
        for (i = 0; i < 16; i++) {
            map[i] = (uint8_t)(11 + i);
        }
        bob = bob_make(bob_img, map);
    }
}

static void copper_tables(void);

static void box_bevel(void)
{
    STDL_Rect r;

    r.x = BOX_X - 3; r.y = BOX_Y - 3; r.w = BOX_W + 6; r.h = BOX_H + 6;
    STDL_FillRect(scr, &r, 12);
    r.x = BOX_X - 3; r.y = BOX_Y - 3; r.w = BOX_W + 3; r.h = BOX_H + 3;
    STDL_FillRect(scr, &r, 14);
    r.x = BOX_X - 1; r.y = BOX_Y - 1; r.w = BOX_W + 2; r.h = BOX_H + 2;
    STDL_FillRect(scr, &r, 13);
}

static void enter(void)
{
    STDL_Rect s, d;
    int k;

    open = 0;
    lastf = 0;
    for (k = 0; k < 2; k++) {
        STDL_FillRect(scr, NULL, 0);
        box_bevel();
        s.x = BOX_X; s.y = BOX_Y + TOP; s.w = BOX_W; s.h = BOX_H;
        d.x = BOX_X; d.y = BOX_Y;
        STDL_BlitSurface(plasma, &s, scr, &d);
        demo_flip();
    }
    copper_tables();
    raster_on(2);
}

static void leave(void)
{
    if (open) {
        STDL_CloseBottomBorder();
        STDL_CloseTopBorder();
        scr = STDL_GetVideoSurface();
        open = 0;
    }
    raster_off();
}

/* the explosion: rasters off, borders open, plasma everywhere */
static void blow_borders(void)
{
    STDL_Rect s, d;
    int k, h, ph;
    static const uint16_t white[16] = {
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF,
        0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
    };

    raster_off();
    pal_show(white);
    STDL_OpenTopBorder();
    STDL_OpenBottomBorder();
    scr = STDL_GetVideoSurface();
    h = scr->h;
    ph = h - 40;
    /* if the top border stayed shut, the box rows sit 28 higher */
    for (k = 0; k < 2; k++) {
        s.x = 0; s.y = (int16_t)(h >= 228 && h != 245 ? 0 : TOP);
        s.w = 320; s.h = (uint16_t)ph;
        d.x = 0; d.y = 0;
        STDL_BlitSurface(plasma, &s, scr, &d);
        band_clear(scr, ph, 40);
        demo_flip();
    }
    bobs_reset(&bobl);
    open = 1;
    fx_flash = 256;
}

static void wheel(uint16_t *pal, uint32_t f, int kick)
{
    int i;

    for (i = 0; i < 11; i++) {
        int h = i * 93 + (int)f * 9;
        int b = 9 + mul14(SIN(i * 186 + (int)f * 3), 6);
        uint16_t c = col_hue(h, b);
        /* a white crest travels round the wheel */
        if (((i * 93 + (int)f * 12) & 1023) < 120) {
            c = col_mix(c, 0xFFF, 160);
        }
        if (kick > 0) {
            c = col_mix(c, 0xFFF, kick / 3);
        }
        pal[1 + i] = c;
    }
}

/* copper bars: gradients at four kick levels made at enter, painted
 * into a cleared column each frame */
static uint16_t cop_g[4][4][6], chrome[8];

static void copper_tables(void)
{
    int k, kl, d;

    for (k = 0; k < 4; k++) {
        for (kl = 0; kl < 4; kl++) {
            for (d = 0; d < 6; d++) {
                int lum = 256 - d * 40 + kl * 28;
                cop_g[k][kl][d] = HWC(col_scale(col_hue(k * 256 + 40, 15),
                                                lum > 256 ? 256 : lum));
            }
        }
    }
    for (k = 0; k < 8; k++) {
        chrome[k] = HWC(col_mix(0x4AF, 0xFFF, k * 36));
    }
}

static void coppers(uint32_t f, int kick)
{
    uint16_t *t = raster_buf();
    int n = raster_n(), i, k, d, kl = kick >> 6;

    if (kl > 3) {
        kl = 3;
    }
    for (i = 0; i < n; i++) {
        t[2 * i] = 0;
        t[2 * i + 1] = chrome[(i - 86) & 7];
    }
    for (k = 0; k < 4; k++) {
        int cy = 50 + mul14(SIN((int)f * 4 + k * 256 + mul14(SIN((int)f * 2), 200)), 44);
        const uint16_t *g = cop_g[k][kl];
        for (d = -5; d <= 5; d++) {
            int e = cy + d;
            if (e >= 0 && e < n) {
                t[2 * e] = g[d < 0 ? -d : d];
            }
        }
    }
}

static void frame(uint32_t f)
{
    uint16_t pal[16];
    int kick = env_decay(sync_since(f, S_KICK | S_BOOM), 10);
    int h, ph, i;

    if (!open && f >= BAR(12)) {
        blow_borders();
    }
    if (sync_hit(f, lastf, S_CRASH | S_BOOM) && f < BAR(12) - 2) {
        fx_flash = 200;
    }
    memset(pal, 0, sizeof(pal));
    wheel(pal, f, kick);

    if (!open) {
        /* phase A: the box, coppers, scroller under the box */
        band_begin(&band, 16);
        band_scroll(&band, text_a, (int)sizeof(text_a) - 1,
                    (int)(f - BAR(8)) * 2, 16, 0, NULL);
        band_out(scr, &band, 172, 15, NULL);
        coppers(f, kick);
        pal[12] = 0x333;
        pal[13] = 0x777;
        pal[14] = 0xBBB;
    } else {
        /* phase B: bobs through the borders, scroller in the border */
        int t = (int)(f - BAR(12));
        h = scr->h;
        ph = h - 40;
        bobs_restore(&bobl, scr->pixels, plasma->pixels);
        /* the bobs keep above the band, so only its 32 text rows are
         * drawn; the four clear rows either side stay clear */
        band_begin(&band, 32);
        band_scroll(&band, text_b, (int)sizeof(text_b) - 1, t * 4, 32, 1,
                    NULL);
        band_out(scr, &band, ph + 4, 0, gold_rows + 4);
        for (i = 0; i < NBOBS; i++) {
            int a = t * 5 + i * (1024 / NBOBS);
            int x = 152 + mul14(SIN(a * 2 + mul14(SIN(t * 2), 128)), 150);
            /* through the top border, never into the scroller's */
            int y = (ph - 16) / 2 + mul14(COS(a * 3 + t), (ph - 16) / 2);
            if (bob == NULL) {
                break;
            }
            bobs_draw(&bobl, bob, scr->pixels, x, y);
        }
        pal[12] = 0x420;
        pal[13] = 0x940;
        pal[14] = 0xDA2;
        pal[15] = 0xFF8;
    }
    pal_show(pal);
    lastf = f;
}

const Part part_plasma = { "plasma", enter, frame, leave };
