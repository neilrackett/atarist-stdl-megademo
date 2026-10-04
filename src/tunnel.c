/*
 * STDL Megademo - part 4: the breakdown, in dots (bars 24-31)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bars 24-27: a dot tunnel - rings flying out of a curving tube,
 * twisting as they come. A ring's radius on screen picks one of 64
 * pre-made circles, so a dot is a table read and two adds, no
 * multiply. Bars 28-31: a 3D dot morph - sphere, torus, cube, the
 * word STDL, a double helix - that explodes on the snare roll.
 *
 * Dots live in planes 0-2 (colours 1-7, by depth) and the captions
 * in plane 3: the erase clears only the low three planes of each
 * dot's group, so the text is drawn once per page and never
 * touched again.
 *
 * Palette: 0 black, 1-7 dots far to near, 8-15 the caption white.
 */

#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "dots.h"

#define NOINLINE __attribute__((noinline))
#define BAR(b) ((b) * SONG_FPB)
#define NRING 12
#define NDOT 32                 /* dots per ring                    */
#define NLEV 64                 /* circle radii, 4px apart          */
#define NANG 64
#define NPTS 200                /* morph points                     */
#define NSHAPE 5
#define MAXDOTS (NRING * NDOT)

static int16_t (*circ)[NANG][2];        /* [NLEV][NANG] dx, dy      */
static int8_t (*shapes)[NPTS][3];       /* [NSHAPE][NPTS] xyz       */
static int16_t recip[1024];             /* 256 * 256 / z            */
static uint16_t save[2][MAXDOTS];
static int nsave[2];
static uint8_t caption[2];              /* which caption each page has */
static int16_t cur[NPTS][3];            /* the morph's current points  */
static int16_t epos[NPTS][3];           /* ... flying apart            */
static int8_t vel[NPTS][3];             /* explosion directions        */
static int exploded;
static int16_t rcp16[1024];             /* 1048576 / z                 */
static uint8_t zcol[1024];              /* dot colour by depth         */
static uint32_t lastf;
static int travel;                      /* tunnel distance flown    */
/* xinfo with two-pixel dots: the bit and its right neighbour */
static uint32_t xinfo2[320];

static const uint16_t base_pal[16] = {
    0x000, 0x339, 0x67D, 0x9AF, 0xDEF, 0xEFF, 0xFFF, 0xFFF,
    0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF
};

static int isqrt(int n)
{
    int r = 0, b = 1 << 14;

    while (b > n) {
        b >>= 2;
    }
    while (b) {
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

static void put(int s, int i, int x, int y, int z)
{
    shapes[s][i][0] = (int8_t)(x < -127 ? -127 : x > 127 ? 127 : x);
    shapes[s][i][1] = (int8_t)(y < -127 ? -127 : y > 127 ? 127 : y);
    shapes[s][i][2] = (int8_t)(z < -127 ? -127 : z > 127 ? 127 : z);
}

void precalc_dots(void)
{
    int l, a, i, k, n;

    circ = malloc(sizeof(*circ) * NLEV);
    shapes = malloc(sizeof(*shapes) * NSHAPE);
    if (circ == NULL || shapes == NULL) {
        mem_fail = 1;
        return;
    }
    for (l = 0; l < NLEV; l++) {
        int r = 6 + l * 4;
        for (a = 0; a < NANG; a++) {
            circ[l][a][0] = (int16_t)mul14(COS(a * 16), r);
            circ[l][a][1] = (int16_t)mul14(SIN(a * 16), r);
        }
    }
    for (i = 2; i < 1024; i++) {
        recip[i] = (int16_t)(65536L / i);
    }
    for (i = 64; i < 1024; i++) {
        int c = 7 - ((i - 200) >> 5);
        rcp16[i] = (int16_t)(1048576L / i);
        zcol[i] = (uint8_t)(c < 1 ? 1 : c > 7 ? 7 : c);
    }
    for (i = 0; i < 320; i++) {
        uint16_t b = bit_of[i];
        xinfo2[i] = ((uint32_t)grp_off[i] << 16) | (uint16_t)(b | (b >> 1));
    }
    recip[0] = recip[1] = 32767;

    /* 0: sphere - a golden-angle spiral, even all over */
    for (i = 0; i < NPTS; i++) {
        int y = 110 - (220 * i + 110) / NPTS;
        int r = isqrt(110 * 110 - y * y);
        int ang = i * 391;
        put(0, i, mul14(COS(ang), r), y, mul14(SIN(ang), r));
    }
    /* 1: torus, 20 around by 10 across */
    for (i = 0; i < NPTS; i++) {
        int u = (i % 20) * 1024 / 20, v = (i / 20) * 1024 / 10;
        int rr = 80 + mul14(COS(v), 34);
        put(1, i, mul14(COS(u), rr), mul14(SIN(v), 34), mul14(SIN(u), rr));
    }
    /* 2: cube, dots along the twelve edges */
    for (i = 0; i < NPTS; i++) {
        int e = i % 12, p = (i / 12) * 180 / (NPTS / 12) - 90;
        int s1 = (e & 1) ? 90 : -90, s2 = (e & 2) ? 90 : -90;
        switch (e >> 2) {
            case 0:  put(2, i, p, s1, s2); break;
            case 1:  put(2, i, s1, p, s2); break;
            default: put(2, i, s1, s2, p); break;
        }
    }
    /* 3: STDL from the font, two layers deep */
    n = 0;
    {
        static const char word[] = "STDL";
        int pass;
        for (pass = 0; pass < 3 && n < NPTS; pass++) {
            for (k = 0; k < 4 && n < NPTS; k++) {
                const uint8_t *g = font8 + (word[k] - FONT_FIRST) * 8;
                int y, x;
                for (y = 0; y < 7 && n < NPTS; y++) {
                    for (x = 0; x < 7 && n < NPTS; x++) {
                        if (g[y] & (0x80 >> x)) {
                            put(3, n++, (k * 8 + x - 15) * 7, (y - 3) * 9,
                                pass == 0 ? -10 : pass == 1 ? 10 : 0);
                        }
                    }
                }
            }
        }
        for (; n < NPTS; n++) {
            shapes[3][n][0] = shapes[3][n % 64][0];
            shapes[3][n][1] = shapes[3][n % 64][1];
            shapes[3][n][2] = shapes[3][n % 64][2];
        }
    }
    /* 4: double helix */
    for (i = 0; i < NPTS; i++) {
        int s = i & 1, j = i >> 1;
        int ang = j * 22 + s * 512;
        put(4, i, mul14(COS(ang), 50), j * 230 / (NPTS / 2) - 115,
            mul14(SIN(ang), 50));
    }
    /* tilt every shape once, so the frame only spins about y */
    for (k = 0; k < NSHAPE; k++) {
        int ct = COS(70), st = SIN(70);
        for (i = 0; i < NPTS; i++) {
            int y = shapes[k][i][1], z = shapes[k][i][2];
            put(k, i, shapes[k][i][0], mul14(y, ct) - mul14(z, st),
                mul14(y, st) + mul14(z, ct));
        }
    }
    for (i = 0; i < NPTS; i++) {
        vel[i][0] = (int8_t)((int)(rnd() & 15) - 8);
        vel[i][1] = (int8_t)((int)(rnd() & 15) - 8);
        vel[i][2] = (int8_t)((int)(rnd() & 15) - 8);
    }
}

static void enter(void)
{
    demo_clear_both(0);
    nsave[0] = nsave[1] = 0;
    caption[0] = caption[1] = 0;
    lastf = 0;
    travel = 0;
    exploded = 0;
}

/*
 * One ring of 32 dots, in 68000: the circle's offsets, the ring's
 * centre, a screen clip, one OR into the ring's plane, and the
 * offset saved for the erase. A ring is one plane - far, middle or
 * near - and where rings cross the planes add up in the palette.
 * Returns the save pointer moved on.
 */
uint16_t *ring_plot(uint8_t *plane, const int16_t *circle, int cx, int cy,
                    int ang, uint16_t *save, const uint32_t *xtab);
__asm__(
"    .text\n"
"    .even\n"
"    .globl _ring_plot\n"
"_ring_plot:\n"
"    movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"    move.l 48(%sp),%a6\n"          /* screen + plane offset       */
"    move.l 52(%sp),%a2\n"          /* circle: 64 x (dx, dy)       */
"    move.l 56(%sp),%d5\n"          /* cx                          */
"    move.l 60(%sp),%d6\n"          /* cy                          */
"    move.l 64(%sp),%d4\n"          /* first angle                 */
"    move.l 68(%sp),%a5\n"          /* save                        */
"    move.l 72(%sp),%a4\n"          /* x table                     */
"    lea    _row_off,%a3\n"
"    moveq  #63,%d3\n"
"    moveq  #31,%d7\n"
"1:  and.w  %d3,%d4\n"
"    move.w %d4,%d0\n"
"    add.w  %d0,%d0\n"
"    add.w  %d0,%d0\n"
"    move.l 0(%a2,%d0.w),%d1\n"     /* dx:dy                       */
"    move.w %d1,%d2\n"
"    add.w  %d6,%d2\n"              /* y                           */
"    cmp.w  #200,%d2\n"
"    bhs.s  8f\n"
"    swap   %d1\n"
"    add.w  %d5,%d1\n"              /* x                           */
"    cmp.w  #320,%d1\n"
"    bhs.s  8f\n"
"    add.w  %d2,%d2\n"
"    move.w 0(%a3,%d2.w),%d2\n"
"    add.w  %d1,%d1\n"
"    add.w  %d1,%d1\n"
"    move.l 0(%a4,%d1.w),%d1\n"     /* group offset : bits         */
"    move.l %d1,%d0\n"
"    swap   %d0\n"
"    add.w  %d0,%d2\n"
"    move.w %d2,(%a5)+\n"
"    or.w   %d1,0(%a6,%d2.w)\n"
"8:  addq.w #2,%d4\n"
"    dbra   %d7,1b\n"
"    move.l %a5,%d0\n"
"    movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"    rts\n"
);

static NOINLINE int tunnel(uint32_t f)
{
    uint8_t *base = scr->pixels;
    uint16_t *sp = save[page];
    int t = (int)(f - BAR(24));
    int cam = travel;
    int camx = mul14(SIN(cam / 4), 40), camy = mul14(SIN(cam / 6 + 200), 28);
    int k;

    for (k = NRING - 1; k >= 0; k--) {
        int zabs = (cam / 64 + k + 1) * 64;
        int z = zabs - cam;                  /* 1..NRING*64         */
        int rad, lev, cx, cy, c;
        if (z < 24) {
            continue;
        }
        rad = (int)((240L * 64) / z);        /* radius on screen    */
        lev = (rad - 6) >> 2;
        if (lev >= NLEV) {
            continue;
        }
        if (lev < 0) {
            lev = 0;
        }
        /* the tube's centre line, seen from where the camera is */
        cx = 160 + (int)(MUL16(mul14(SIN(zabs / 4), 40) - camx, recip[z]) >> 8);
        cy = 100 + (int)(MUL16(mul14(SIN(zabs / 6 + 200), 28) - camy, recip[z]) >> 8);
        /* plane by depth: 0 far, 1 middle, 2 near (two pixels wide) */
        c = z > NRING * 64 * 2 / 3 ? 0 : z > NRING * 64 / 3 ? 1 : 2;
        sp = ring_plot(base + c * 2, &circ[lev][0][0], cx, cy,
                       (zabs / 64) * 3 + t, sp, c == 2 ? xinfo2 : xinfo);
    }
    return (int)(sp - save[page]);
}

/*
 * The morph's points in 68000: spin about y (coefficients Q15, so
 * each product is muls, add and swap - the tilt was baked into the
 * shapes at load), project through a reciprocal table pre-scaled so
 * the divide is two more multiplies with no shift, clip, colour by
 * depth from a table, plot into planes 0-2 and save the offset.
 * coef = cy, sy.
 */
uint16_t *morph_plot(const int16_t *pts, int n, const int16_t *coef,
                     uint8_t *base, uint16_t *save);
__asm__(
"    .text\n"
"    .even\n"
"    .globl _morph_plot\n"
"_morph_plot:\n"
"    movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"    move.l 48(%sp),%a0\n"          /* points x,y,z                */
"    move.l 52(%sp),%d7\n"          /* count                       */
"    move.l 56(%sp),%a5\n"          /* cy, sy                      */
"    move.l 60(%sp),%a6\n"          /* screen                      */
"    move.l 64(%sp),%a1\n"          /* save                        */
"    lea    _rcp16,%a4\n"
"    lea    _row_off,%a3\n"
"    lea    _xinfo,%a2\n"
"    move.w (%a5),%d6\n"            /* cy                          */
"    move.w 2(%a5),%d5\n"           /* sy                          */
"    lea    _zcol,%a5\n"
"    subq.w #1,%d7\n"
"    bmi    9f\n"
"1:  move.w (%a0)+,%d0\n"           /* x                           */
"    move.w (%a0)+,%d4\n"           /* y                           */
"    move.w (%a0)+,%d2\n"           /* z                           */
"    move.w %d0,%d3\n"
"    muls   %d6,%d3\n"              /* x cy                        */
"    move.w %d2,%d1\n"
"    muls   %d5,%d1\n"              /* z sy                        */
"    sub.l  %d1,%d3\n"
"    add.l  %d3,%d3\n"
"    swap   %d3\n"                  /* x1                          */
"    muls   %d5,%d0\n"              /* x sy                        */
"    muls   %d6,%d2\n"              /* z cy                        */
"    add.l  %d0,%d2\n"
"    add.l  %d2,%d2\n"
"    swap   %d2\n"
"    add.w  #340,%d2\n"             /* depth, camera back          */
"    cmp.w  #64,%d2\n"
"    blt.s  8f\n"
"    moveq  #0,%d1\n"
"    move.b 0(%a5,%d2.w),%d1\n"     /* colour                      */
"    add.w  %d2,%d2\n"
"    move.w 0(%a4,%d2.w),%d2\n"     /* 1048576 / z                 */
"    lsl.w  #4,%d3\n"
"    muls   %d2,%d3\n"
"    swap   %d3\n"
"    add.w  #160,%d3\n"
"    cmp.w  #320,%d3\n"
"    bhs.s  8f\n"
"    lsl.w  #4,%d4\n"
"    muls   %d2,%d4\n"
"    swap   %d4\n"
"    add.w  #100,%d4\n"
"    cmp.w  #200,%d4\n"
"    bhs.s  8f\n"
"    add.w  %d4,%d4\n"
"    move.w 0(%a3,%d4.w),%d4\n"
"    add.w  %d3,%d3\n"
"    add.w  %d3,%d3\n"
"    move.l 0(%a2,%d3.w),%d3\n"
"    move.l %d3,%d0\n"
"    swap   %d0\n"
"    add.w  %d0,%d4\n"
"    move.w %d4,(%a1)+\n"
"    lea    0(%a6,%d4.w),%a3\n"
"    lsr.b  #1,%d1\n"
"    bcc.s  2f\n"
"    or.w   %d3,(%a3)\n"
"2:  lsr.b  #1,%d1\n"
"    bcc.s  3f\n"
"    or.w   %d3,2(%a3)\n"
"3:  lsr.b  #1,%d1\n"
"    bcc.s  4f\n"
"    or.w   %d3,4(%a3)\n"
"4:  lea    _row_off,%a3\n"
"8:  dbra   %d7,1b\n"
"9:  move.l %a1,%d0\n"
"    movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"    rts\n"
);

static NOINLINE int morph(uint32_t f, int kick)
{
    int t = (int)(f - BAR(28));
    int seg = t / 48, w = t % 48;
    int from = seg % NSHAPE, to = (seg + 1) % NSHAPE;
    int mix = w < 24 ? 0 : (w - 24) * 256 / 24;  /* hold, then move */
    int ay = t * 6;
    int16_t coef[2];
    const int16_t *pts = &cur[0][0];
    int i;

    (void)kick;
    if ((int)f < BAR(31)) {
        /* the shape between two of the five */
        for (i = 0; i < NPTS; i++) {
            const int8_t *a = shapes[from][i], *b = shapes[to][i];
            if (mix == 0) {
                cur[i][0] = a[0];
                cur[i][1] = a[1];
                cur[i][2] = a[2];
            } else {
                cur[i][0] = (int16_t)(a[0] + (int)(MUL16(b[0] - a[0], mix) >> 8));
                cur[i][1] = (int16_t)(a[1] + (int)(MUL16(b[1] - a[1], mix) >> 8));
                cur[i][2] = (int16_t)(a[2] + (int)(MUL16(b[2] - a[2], mix) >> 8));
            }
        }
        exploded = 0;
    } else {
        /* the snare roll: everything flies apart */
        if (!exploded) {
            memcpy(epos, cur, sizeof(epos));
            exploded = 1;
        }
        for (i = 0; i < NPTS; i++) {
            epos[i][0] = (int16_t)(epos[i][0] + vel[i][0]);
            epos[i][1] = (int16_t)(epos[i][1] + vel[i][1]);
            epos[i][2] = (int16_t)(epos[i][2] + vel[i][2]);
        }
        pts = &epos[0][0];
    }
    coef[0] = (int16_t)(COS(ay) * 2 > 32767 ? 32767 : COS(ay) * 2);
    coef[1] = (int16_t)(SIN(ay) * 2 > 32767 ? 32767 : SIN(ay) * 2);
    return (int)(morph_plot(pts, NPTS, coef, scr->pixels, save[page]) - save[page]);
}

static void frame(uint32_t f)
{
    uint16_t pal[16];
    int kick = env_decay(sync_since(f, S_KICK | S_BOOM), 10);
    int want, i;

    if (circ == NULL || shapes == NULL) {
        return;
    }
    if (sync_hit(f, lastf, S_CRASH | S_BOOM)) {
        fx_flash = 200;
    }
    dots_erase3(scr->pixels, save[page], nsave[page]);

    /* captions in plane 3, drawn once per page */
    want = f >= BAR(28) ? 2 : 1;
    if (caption[page] != want) {
        band_clear(scr, 8, 16);
        band_clear(scr, 176, 16);
        if (want == 1) {
            bp_text16(scr, 8, "INTO THE TUNNEL", 8, NULL);
        } else {
            bp_text16(scr, 8, "200 DOTS", 8, NULL);
            bp_text16(scr, 176, "ON A 68000", 8, NULL);
        }
        caption[page] = (uint8_t)want;
    }

    {
        int dt = (int)(f - lastf);
        if (dt < 0 || dt > 8 || lastf == 0) {
            dt = 1;
        }
        travel += dt * (8 + kick / 24);
    }
    nsave[page] = f < BAR(28) ? tunnel(f) : morph(f, kick);

    memcpy(pal, base_pal, sizeof(pal));
    for (i = 1; i < 8; i++) {
        pal[i] = col_mix(pal[i], 0xFFF, kick / 3);
    }
    /* the caption flares with the snare */
    want = env_decay(sync_since(f, S_SNARE | S_CLAP), 12);
    for (i = 8; i < 16; i++) {
        pal[i] = col_mix(0x68C, 0xFFF, want);
    }
    pal_show(pal);
    lastf = f;
}

const Part part_dots = { "dots", enter, frame, NULL };
