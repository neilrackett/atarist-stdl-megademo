/*
 * STDL Megademo - part 1: intro (bars 0-7)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copper bars on Timer B behind a 3D starfield, typewriter text, the
 * chrome logo dropping on the boom at bar 4, and an LED spectrum
 * analyser driven by the YM registers the music has set.
 *
 * Palette: 0 copper (raster), 1-3 stars by depth, 4-11 logo chrome,
 * 15 text and analyser (raster: a rainbow down the text, an LED
 * gradient with gaps down the analyser - the segments are the
 * raster table, not drawing).
 *
 * Nothing is drawn twice. Each typed character goes onto each page
 * once (STDL_DrawChar); stars keep out of the 16-pixel groups the
 * text and the logo occupy, so the erase - which clears a dot's
 * whole group - can never touch them; the analyser overwrites its
 * own rows instead of clearing them first.
 */

#include <string.h>
#include "demo.h"
#include "dots.h"

#define NOINLINE __attribute__((noinline))
#define NSTARS 110
#define LOGO_X ((320 - LOGO_W) / 2)
#define LOGO_REST 22
#define AN_Y 170                /* analyser rows 170..199 */
#define AN_H 30
#define BAR(b) ((b) * SONG_FPB)

typedef struct { int16_t x, y, z; } Star;

static Star stars[NSTARS];
static uint16_t ssave[2][NSTARS];
static int sn[2];
static STDL_Surface *logo;
static int logo_y[2];           /* where each page has the logo, -999 none */
static uint32_t lastf;
static int16_t drop_y[64];
static uint8_t an_lvl[16];
static uint8_t an_drawn[2][16];     /* bar heights on each page */
static uint16_t an_thr[16];

typedef struct {
    int16_t from, to;           /* song frames */
    int16_t y;
    const char *text;
} Line;

static const Line lines[] = {
    { BAR(0) + 12, BAR(4) - 6, 64,  "THE YEAR IS 1989..." },
    { BAR(1),      BAR(4) - 6, 78,  "...OR IS IT?" },
    { BAR(2),      BAR(4) - 6, 104, "68000 * YM2149 * STDL" },
    { BAR(3),      BAR(4) - 6, 118, "WELCOME TO THE" },
    { BAR(5),      BAR(8),     118, "SIXTEEN COLOURS. EIGHT MHZ." },
    { BAR(6),      BAR(8),     132, "NO EXCUSES." },
    { BAR(7) - 48, BAR(8),     150, "TURN UP THE YM!" },
};
#define NLINES ((int)(sizeof(lines) / sizeof(lines[0])))
static uint8_t typed[2][NLINES];        /* chars on each page */

/* groups the stars keep out of, a bit per group of each row (the
 * text boxes and the logo), maintained as they come and go */
uint32_t fmask[AN_Y];
/* per depth: 16 * 112 * 256 / z in the high word (x and y are stored
 * times 16, so the projection is muls + swap), colour in the low */
uint32_t zinfo[1024];

static const uint16_t base_pal[16] = {
    0x000, 0x224, 0x669, 0xFFF,
    0x001, 0x113, 0x336, 0x77F, 0xFFF, 0x210, 0x531, 0xC72,
    0x000, 0x000, 0x000, 0xFFF
};

STDL_Surface *intro_logo(void)
{
    return logo;
}

static void star_new(Star *s, int far)
{
    s->x = (int16_t)(((int)(rnd() & 2047) - 1024) * 16);
    s->y = (int16_t)(((int)(rnd() & 2047) - 1024) * 16);
    s->z = far ? (int16_t)(900 + (rnd() & 127)) : (int16_t)(64 + (rnd() & 895));
}

void precalc_intro(void)
{
    int i, v, y;

    logo = STDL_SurfaceFromIndexed8(logo_img, LOGO_W, LOGO_H, LOGO_W, 0);
    for (i = 24; i < 1024; i++) {
        zinfo[i] = ((uint32_t)(16L * 112 * 256 / i) << 16) |
                   (uint32_t)(i > 640 ? 1 : i > 300 ? 2 : 3);
    }
    for (i = 0; i < NSTARS; i++) {
        star_new(&stars[i], 0);
    }
    /* the drop: fall, two bounces, rest */
    v = 0;
    y = -LOGO_H * 16;
    for (i = 0; i < 64; i++) {
        v += 7;
        y += v;
        if (y > LOGO_REST * 16) {
            y = LOGO_REST * 16;
            v = -v * 2 / 5;
        }
        drop_y[i] = (int16_t)(y >> 4);
    }
    drop_y[63] = LOGO_REST;
    /* analyser bins: period thresholds, log-spaced down from 2kHz */
    for (i = 0; i < 16; i++) {
        long f = 2000;
        int k;
        for (k = 0; k < i; k++) {
            f = f * 100 / 132;
        }
        an_thr[i] = (uint16_t)(125000L / (f ? f : 1));
    }
}

static void copper_tables(void);
static uint8_t line_forbid[16];

static void forbid_rect(int y, int h, int x0, int x1, uint8_t on)
{
    int g0 = x0 >> 4, g1 = (x1 - 1) >> 4, g, r;
    uint32_t m = 0;

    for (g = g0; g <= g1; g++) {
        m |= 1UL << g;
    }
    for (r = y < 0 ? 0 : y; r < y + h && r < AN_Y; r++) {
        fmask[r] = on ? fmask[r] | m : fmask[r] & ~m;
    }
}

static void enter(void)
{
    demo_clear_both(0);
    logo_y[0] = logo_y[1] = -999;
    memset(typed, 0, sizeof(typed));
    sn[0] = sn[1] = 0;
    memset(an_lvl, 0, sizeof(an_lvl));
    memset(an_drawn, 0, sizeof(an_drawn));
    memset(fmask, 0, sizeof(fmask));
    memset(line_forbid, 0, sizeof(line_forbid));
    copper_tables();
    lastf = 0;
    raster_on(2);
}

static void leave(void)
{
    raster_off();
}

/*
 * Copper bars and the colour-15 gradients into the raster table.
 * Everything that can be is a table made at enter: the bars at four
 * kick levels and eight fade steps, the rainbow and the LED column.
 * Per frame the colour-0 column is cleared and each bar paints its
 * thirteen entries - a few thousand cycles, where testing every bar
 * against every line was sixty thousand.
 */
static uint16_t bargrad[8][4][3][7];    /* fade, kick, bar, distance */
static uint16_t rainbow[256];
static uint16_t ledcol[RAST_MAXN];

static void copper_tables(void)
{
    static const uint16_t hue[3] = { 0xF20, 0x2F4, 0x44F };
    static const uint16_t led[8] = {
        0xF00, 0xF40, 0xF80, 0xFC0, 0xEE0, 0x8E0, 0x4E0, 0x0E0
    };
    int fd, kl, k, d, i;

    for (fd = 0; fd < 8; fd++) {
        for (kl = 0; kl < 4; kl++) {
            for (k = 0; k < 3; k++) {
                for (d = 0; d < 7; d++) {
                    int lum = 256 - d * 36 + kl * 32;
                    uint16_t c = col_scale(hue[k], lum > 256 ? 256 : lum);
                    if (d == 0) {
                        c = col_mix(c, 0xFFF, 80 + kl * 32);
                    }
                    bargrad[fd][kl][k][d] = HWC(col_scale(c, (fd + 1) * 32));
                }
            }
        }
    }
    for (i = 0; i < 256; i++) {
        rainbow[i] = HWC(col_hue(i * 4, 15));
    }
    for (i = 0; i < RAST_MAXN; i++) {
        /* LED rows: lit two lines, dark two lines; green to red */
        int r = i - (AN_Y / 2);
        ledcol[i] = (r & 1) || r < 0 ? 0 : HWC(led[r >> 1 > 7 ? 7 : r >> 1]);
    }
}

static NOINLINE void coppers(uint32_t f, int kick)
{
    uint16_t *t = raster_buf();
    int n = raster_n();
    int fd = f < BAR(2) ? (int)(f / 24) : 7;
    int kl = kick >> 6;
    int i, k, d;
    uint16_t idx = (uint16_t)(f * 6 * 64);      /* rainbow, 8.8 */

    if (kl > 3) {
        kl = 3;
    }
    for (i = 0; i < AN_Y / 2 - 1; i++) {
        t[2 * i] = 0;
        t[2 * i + 1] = rainbow[(idx >> 8) & 255];
        idx = (uint16_t)(idx + 640);
    }
    for (; i < n; i++) {
        t[2 * i] = 0;
        t[2 * i + 1] = ledcol[i];
    }
    for (k = 0; k < 3; k++) {
        int cy = 50 + mul14(SIN((int)f * (5 + k) + k * 341), 40);
        const uint16_t *g = bargrad[fd][kl][k];
        for (d = -6; d <= 6; d++) {
            int e = cy + d;
            if (e >= 0 && e < n) {
                t[2 * e] = g[d < 0 ? -d : d];
            }
        }
    }
}

static NOINLINE void analyser(void)
{
    uint8_t vol[3], mix;
    uint16_t per[3];
    int c, i;

    ym_peek(vol, per, &mix);
    for (i = 0; i < 16; i++) {
        an_lvl[i] = an_lvl[i] > 2 ? (uint8_t)(an_lvl[i] - 2) : 0;
    }
    for (c = 0; c < 3; c++) {
        int v = vol[c] & 0x10 ? 15 : vol[c];
        int h;
        if (v == 0) {
            continue;
        }
        h = v * 2;
        if (!(mix & (8 << c))) {
            /* noise: splash over the top bins */
            for (i = 11; i < 16; i++) {
                if (an_lvl[i] < h - (15 - i)) {
                    an_lvl[i] = (uint8_t)(h - (15 - i));
                }
            }
        }
        if (!(mix & (1 << c))) {
            int b = 0;
            /* an_thr rises with the bin: highest pitch first */
            while (b < 15 && per[c] > an_thr[b]) {
                b++;
            }
            b = 15 - b;          /* lowest frequency on the left */
            if (an_lvl[b] < h) an_lvl[b] = (uint8_t)h;
            if (b > 0 && an_lvl[b - 1] < h * 2 / 3) an_lvl[b - 1] = (uint8_t)(h * 2 / 3);
            if (b < 15 && an_lvl[b + 1] < h * 2 / 3) an_lvl[b + 1] = (uint8_t)(h * 2 / 3);
        }
    }
    /* one 12px bar per group 2..17, colour 15 (every plane); only
     * the rows whose state changed on this page are written */
    for (i = 0; i < 16; i++) {
        int h = an_lvl[i] > AN_H ? AN_H : an_lvl[i];
        int old = an_drawn[page][i], r;
        uint32_t *p, v;
        if (h == old) {
            continue;
        }
        if (h > old) {          /* light rows AN_H-h .. AN_H-old-1 */
            r = h - old;
            p = (uint32_t *)(scr->pixels + row_off[AN_Y + AN_H - h]) + (2 + i) * 2;
            v = 0xFFF0FFF0u;
        } else {                /* darken rows AN_H-old .. AN_H-h-1 */
            r = old - h;
            p = (uint32_t *)(scr->pixels + row_off[AN_Y + AN_H - old]) + (2 + i) * 2;
            v = 0;
        }
        for (; r > 0; r--) {
            p[0] = v;
            p[1] = v;
            p += 40;
        }
        an_drawn[page][i] = (uint8_t)h;
    }
}

static NOINLINE void logo_step(int ly)
{
    STDL_Rect r;

    if (ly == logo_y[page]) {
        return;
    }
    if (logo_y[page] > -LOGO_H) {
        r.x = LOGO_X; r.y = (int16_t)logo_y[page];
        r.w = LOGO_W; r.h = LOGO_H;
        STDL_FillRect(scr, &r, 0);
        forbid_rect(logo_y[page], LOGO_H, LOGO_X, LOGO_X + LOGO_W, 0);
    }
    if (ly > -LOGO_H) {
        r.x = LOGO_X; r.y = (int16_t)ly;
        STDL_BlitSurface(logo, NULL, scr, &r);
        forbid_rect(ly, LOGO_H, LOGO_X, LOGO_X + LOGO_W, 1);
    }
    logo_y[page] = ly;
}

/* the typewriter: each new character onto each page once */
static NOINLINE void text_step(uint32_t f)
{
    int i;

    for (i = 0; i < NLINES; i++) {
        const Line *l = &lines[i];
        int len = text_len(l->text);
        int x0 = 160 - len * 4;
        if ((int)f >= l->from && (int)f < l->to) {
            int chars = (int)(f - (uint32_t)l->from) / 3 + 1;
            int k;
            if (chars > len) {
                chars = len;
            }
            for (k = typed[page][i]; k < chars; k++) {
                STDL_DrawChar(scr, &font8x8, x0 + k * 8, l->y, l->text[k], 15);
            }
            if (!line_forbid[i]) {
                forbid_rect(l->y, 8, x0, x0 + len * 8, 1);
                line_forbid[i] = 1;
            }
            typed[page][i] = (uint8_t)chars;
        } else if (typed[page][i]) {
            STDL_Rect r;
            r.x = (int16_t)x0; r.y = l->y;
            r.w = (uint16_t)(len * 8); r.h = 8;
            STDL_FillRect(scr, &r, 0);
            typed[page][i] = 0;
        }
        if (line_forbid[i] && ((int)f < l->from || (int)f >= l->to)) {
            /* text_step runs before the stars on both pages, so the
             * other page clears its copy before plotting near it */
            forbid_rect(l->y, 8, x0, x0 + len * 8, 0);
            line_forbid[i] = 0;
        }
    }
}

/*
 * The star loop, in 68000: fly, project, clip, keep out of the text
 * and the logo, plot, and remember where for the erase. About 450
 * cycles a star against 720 for the best gcc 4.6 made of the C. A
 * star that leaves the screen gets z = 0 and the C pass after
 * respawns it (rnd lives there).
 */
int stars_asm(Star *s, int count, int step, uint16_t *save, uint8_t *base);
__asm__(
"    .text\n"
"    .even\n"
"    .globl _stars_asm\n"
"_stars_asm:\n"
"    movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"    move.l 48(%sp),%a0\n"          /* stars                       */
"    move.l 52(%sp),%d7\n"          /* count                       */
"    move.l 56(%sp),%d6\n"          /* step                        */
"    move.l 60(%sp),%a3\n"          /* save                        */
"    move.l 64(%sp),%a4\n"          /* screen base                 */
"    lea    _fmask,%a1\n"
"    lea    _zinfo,%a2\n"
"    lea    _xinfo,%a5\n"
"    lea    _row_off,%a6\n"
"    move.l %a3,-(%sp)\n"
"    subq.w #1,%d7\n"
"    bmi    7f\n"
"1:  move.w 4(%a0),%d0\n"           /* z -= step                   */
"    sub.w  %d6,%d0\n"
"    cmp.w  #24,%d0\n"
"    blt.s  8f\n"
"    move.w %d0,4(%a0)\n"
"    add.w  %d0,%d0\n"
"    add.w  %d0,%d0\n"
"    move.l 0(%a2,%d0.w),%d1\n"     /* rz:colour                   */
"    move.l %d1,%d2\n"
"    swap   %d2\n"
"    move.w (%a0),%d3\n"
"    muls   %d2,%d3\n"
"    swap   %d3\n"
"    add.w  #160,%d3\n"
"    cmp.w  #320,%d3\n"
"    bhs.s  8f\n"
"    move.w 2(%a0),%d4\n"
"    muls   %d2,%d4\n"
"    swap   %d4\n"
"    add.w  #100,%d4\n"
"    cmp.w  #168,%d4\n"
"    bhs.s  8f\n"
"    add.w  %d4,%d4\n"              /* y*2                         */
"    move.w %d3,%d5\n"
"    lsr.w  #4,%d5\n"               /* group                       */
"    move.w %d4,%d2\n"
"    add.w  %d2,%d2\n"              /* y*4                         */
"    move.l 0(%a1,%d2.w),%d2\n"
"    btst   %d5,%d2\n"
"    bne.s  9f\n"                   /* text or logo here           */
"    move.w 0(%a6,%d4.w),%d4\n"     /* row offset                  */
"    add.w  %d3,%d3\n"
"    add.w  %d3,%d3\n"
"    move.l 0(%a5,%d3.w),%d3\n"     /* group offset:bit            */
"    move.l %d3,%d5\n"
"    swap   %d5\n"
"    add.w  %d5,%d4\n"
"    move.w %d4,(%a3)+\n"
"    btst   #0,%d1\n"
"    beq.s  2f\n"
"    or.w   %d3,0(%a4,%d4.w)\n"
"2:  btst   #1,%d1\n"
"    beq.s  9f\n"
"    or.w   %d3,2(%a4,%d4.w)\n"
"9:  addq.l #6,%a0\n"
"    dbra   %d7,1b\n"
"    bra.s  7f\n"
"8:  clr.w  4(%a0)\n"               /* respawn in C                */
"    addq.l #6,%a0\n"
"    dbra   %d7,1b\n"
"7:  move.l %a3,%d0\n"
"    sub.l  (%sp)+,%d0\n"
"    lsr.l  #1,%d0\n"
"    movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"    rts\n"
);

static NOINLINE void stars_step(int dt, int speed)
{
    Star *s;

    sn[page] = stars_asm(stars, NSTARS, speed * dt, ssave[page], scr->pixels);
    for (s = stars; s < stars + NSTARS; s++) {
        if (s->z == 0) {
            star_new(s, 1);
        }
    }
}

static void frame(uint32_t f)
{
    int dt = (int)(f - lastf);
    int kick, ly;
    uint16_t pal[16];

    if (dt < 0 || dt > 8 || lastf == 0) {
        dt = 1;
    }
    kick = env_decay(sync_since(f, S_KICK | S_BOOM), 12);
    if (sync_hit(f, lastf, S_BOOM | S_CRASH)) {
        fx_flash = 230;
    }

    dots_erase(scr->pixels, ssave[page], sn[page]);
    ly = -999;
    if (f >= BAR(4)) {
        int k = (int)(f - BAR(4));
        ly = k < 64 ? drop_y[k] : LOGO_REST;
    }
    logo_step(ly);
    text_step(f);
    stars_step(dt, f < BAR(4) ? 5 : 18 + kick / 16);
    analyser();
    coppers(f, kick);

    memcpy(pal, base_pal, sizeof(pal));
    if (f < BAR(1)) {
        int k = (int)(f * 256 / BAR(1));
        pal[1] = col_scale(pal[1], k);
        pal[2] = col_scale(pal[2], k);
        pal[3] = col_scale(pal[3], k);
    }
    pal_show(pal);
    lastf = f;
}

const Part part_intro = { "intro", enter, frame, leave };
