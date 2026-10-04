/*
 * STDL Megademo - part 3: the Boing ball (bars 16-23)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Amiga's mascot, done the ST way. The ball is a single sprite
 * drawn once at load in eight colours, each checker column split
 * into four sub-bands; spinning it is rotating which four of those
 * eight registers are red, so the spin costs nothing. It bounces on
 * every beat (the kick), crosses the screen, and turns round at the
 * walls.
 *
 * The shadow is one bitplane ORed in: the grey background is colour
 * 0 and the grid colour 1, and setting plane 1 under the shadow
 * turns them into 2 and 3 - dark grey and dark purple - which is
 * planar arithmetic doing a translucent shadow for free. Vector bobs
 * orbit the ball in 3D (drawn behind or in front by depth) and a
 * bouncing-letter scroller runs along the floor.
 *
 * Palette: 0 grey, 1 purple grid, 2/3 their shadows, 4-7 bobs,
 * 8-15 the ball's spinning checkers.
 */

#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "dots.h"
#include "text.h"
#include "bobs.h"

#define BAR(b) ((b) * SONG_FPB)
#define FLOOR_Y 108             /* ball top at rest: bottom on y 172 */
#define BAND_Y 172
#define BAND_H 28
#define NBOBS 6
#define SH_DX 12                /* shadow offset                    */
#define SH_DY 5

static STDL_Surface *bg;
static STDL_Sprite *ball;
static Bob *bob;
static BobList bobl;
static uint16_t *shadow;        /* 16 shifts x 80 rows x 6 words   */
static Band band;
static int8_t yoff[128];
static uint32_t lastf;

static const char text[] =
    "THE BOING BALL LIVES!!!   RESPECT TO THE AMIGA CREW OF 1984...   "
    "NOW BOUNCING ON EVERY KICK DRUM - ON AN ATARI ST, IN SIXTEEN "
    "COLOURS...   THE SPIN IS FREE: IT IS ALL PALETTE!      ";

static void grid(STDL_Surface *s)
{
    STDL_Rect r;
    int k;

    r.x = 0; r.y = 0; r.w = 320; r.h = 200;
    STDL_FillRect(s, &r, 0);
    for (k = 0; k <= 16; k++) {             /* back wall */
        STDL_VLine(s, 16 + 18 * k, 6, 150, 1);
    }
    for (k = 0; k <= 9; k++) {
        STDL_HLine(s, 16, 304, 6 + 16 * k, 1);
    }
    for (k = 0; k <= 16; k++) {             /* floor in perspective */
        int x0 = 16 + 18 * k;
        int x1 = 160 + (x0 - 160) * 4 / 3;
        STDL_Line(s, x0, 150, x1, 170, 1);
    }
    STDL_HLine(s, 16, 304, 153, 1);
    STDL_HLine(s, 12, 308, 157, 1);
    STDL_HLine(s, 8, 312, 162, 1);
    STDL_HLine(s, 3, 316, 168, 1);
}

void precalc_boing(void)
{
    STDL_Surface *bs;
    int flags = mach->has_blitter ? 0 : STDL_PRESHIFT;
    int i, r, sh;

    bg = STDL_CreateSurface(320, 200);
    if (bg != NULL) {
        grid(bg);
    } else {
        mem_fail = 1;
    }
    bs = STDL_SurfaceFromIndexed8(ball_img, BALL_SIZE, BALL_SIZE, BALL_SIZE, 0);
    if (bs != NULL) {
        ball = STDL_SpriteFromSurface(bs, BALL_SIZE, flags);
        STDL_FreeSurface(bs);
    }
    {
        uint8_t map[16];
        for (i = 0; i < 16; i++) {
            map[i] = (uint8_t)(3 + i);
        }
        bob = bob_make(bob_img, map);
    }
    /* the ball's silhouette, pre-shifted, for the one-plane shadow:
     * each row's mask once, then shifted a word at a time */
    shadow = malloc(16 * BALL_SIZE * 6 * sizeof(uint16_t));
    if (shadow != NULL) {
        for (r = 0; r < BALL_SIZE; r++) {
            uint16_t m[6];
            int x;
            memset(m, 0, sizeof(m));
            for (x = 0; x < BALL_SIZE; x++) {
                if (ball_img[r * BALL_SIZE + x]) {
                    m[x >> 4] |= (uint16_t)(0x8000u >> (x & 15));
                }
            }
            for (sh = 0; sh < 16; sh++) {
                uint16_t *w = shadow + (sh * BALL_SIZE + r) * 6;
                int k;
                for (k = 0; k < 6; k++) {
                    uint32_t win = ((uint32_t)(k ? m[k - 1] : 0) << 16) | m[k];
                    w[k] = (uint16_t)(win >> sh);
                }
            }
        }
    }
}

static void enter(void)
{
    int k;

    lastf = 0;
    for (k = 0; k < 2; k++) {
        if (bg != NULL) {
            STDL_BlitSurface(bg, NULL, scr, NULL);
        }
        demo_flip();
    }
    if (bg != NULL) {
        STDL_DirtyInit(bg, 8);
    }
    bobs_reset(&bobl);
}

static void leave(void)
{
    STDL_DirtyQuit();
}

/* plane 1 ORed under the silhouette: 0->2, 1->3 */
static void draw_shadow(int x, int y)
{
    const uint16_t *src;
    int r, r0 = 0, r1 = BALL_SIZE, g0 = x >> 4, w;

    if (shadow == NULL) {
        return;
    }
    if (y + r1 > BAND_Y) {
        r1 = BAND_Y - y;
    }
    src = shadow + (x & 15) * BALL_SIZE * 6;
    w = g0 + 6 > 20 ? 20 - g0 : 6;
    for (r = r0; r < r1; r++) {
        uint16_t *d = (uint16_t *)(scr->pixels + row_off[y + r]) + g0 * 4 + 1;
        const uint16_t *s = src + r * 6;
        int g;
        for (g = 0; g < w; g++) {
            d[g * 4] |= s[g];
        }
    }
}

static void frame(uint32_t f)
{
    uint16_t pal[16];
    int t = (int)(f - BAR(16));
    int beat = (int)(f % 24);
    int bx, by, sx, dir, spin, i, kick, pass;
    STDL_Rect r;

    if (sync_hit(f, lastf, S_CRASH)) {
        fx_flash = 160;
    }
    kick = env_decay(sync_since(f, S_KICK), 8);
    if (bg == NULL || ball == NULL) {
        return;
    }
    STDL_DirtyRestore(scr);
    bobs_restore(&bobl, scr->pixels, bg->pixels);

    /* across and back: a triangle wave between the walls */
    sx = (t * 3 / 2) % 448;
    dir = sx < 224 ? 1 : -1;
    bx = 16 + (sx < 224 ? sx : 448 - sx);
    /* a bounce every beat, landing on the kick */
    by = FLOOR_Y - (int)((88L * 4 * beat * (24 - beat)) / (24 * 24));

    draw_shadow(bx + SH_DX, by + SH_DY);
    /* one restore for ball and shadow together: they overlap */
    r.x = (int16_t)bx; r.y = (int16_t)by;
    r.w = BALL_SIZE + SH_DX + 16; r.h = BALL_SIZE + SH_DY;
    STDL_DirtyPush(&r);

    /* bobs behind, the ball, bobs in front */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < NBOBS; i++) {
            int a = t * 6 + i * (1024 / NBOBS);
            int z = SIN(a), x, y;
            if ((z >= 0) != pass || bob == NULL) {
                continue;
            }
            x = bx + 24 + mul14(COS(a), 60);
            y = by + 24 + mul14(z, 12) + mul14(COS(a), 10);
            if (y >= 0 && y < BAND_Y - 16) {
                bobs_draw(&bobl, bob, scr->pixels, x, y);
            }
        }
        if (pass == 0) {
            STDL_BlitSprite(ball, 0, scr, bx, by);
        }
    }

    /* letters hop along the floor, a wave set off by every kick */
    {
        static const int8_t hop[24] = {
            12, 10, 8, 7, 5, 4, 3, 2, 1, 1, 0, 0,
            0, 0, 0, 1, 1, 2, 3, 4, 5, 7, 8, 10
        };
        int since = sync_since(f, S_KICK);
        for (i = 0; i < 128; i++) {
            int p = since * 2 - (i >> 1);
            yoff[i] = (int8_t)((unsigned)p < 24 ? hop[p] : 12);
        }
    }
    band_begin(&band, BAND_H);
    band_scroll(&band, text, (int)sizeof(text) - 1, t * 2, 16, 0, yoff);
    band_out1(scr, &band, BAND_Y, 0);   /* plane 0 alone: purple */

    /* the spin: which four of the eight ball colours are red */
    spin = (t >> 1) * dir;
    pal[0] = 0xAAA;
    pal[1] = 0xA0A;
    pal[2] = 0x666;
    pal[3] = 0x606;
    pal[4] = 0x024;
    pal[5] = 0x157;
    pal[6] = 0x39C;
    pal[7] = 0xBEF;
    for (i = 0; i < 8; i++) {
        pal[8 + i] = ((i - spin) & 7) < 4 ? 0xF00 : 0xFFF;
    }
    if (kick > 0) {
        pal[0] = col_mix(pal[0], 0xFFF, kick / 4);
    }
    pal_show(pal);
    lastf = f;
}

const Part part_boing = { "boing", enter, frame, leave };
