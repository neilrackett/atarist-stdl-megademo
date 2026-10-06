/*
 * STDL Megademo - part 5: glenz vectors and twisters (bars 32-39)
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A glenz object - a tetrakis hexahedron, 24 checkered triangles -
 * spinning in the middle and a twister either side, over a Timer B
 * sky, with a caption whose colour rolls down it on the same timer.
 *
 * The glass is planar arithmetic. Front faces are XORed into planes
 * 2 or 3 (two checker colours), back faces into planes 0 or 1, all
 * through STDL_XorHSpans onto a cleared box. A convex body's front
 * faces tile its silhouette exactly once and so do its back faces,
 * so every pixel ends up holding one front colour and one back
 * colour, and the palette entry for that pair is the blend: 16
 * colours give a see-through solid without ever reading a pixel.
 * The rasteriser keeps the top-left rule, so neighbouring triangles
 * share no pixel and the XOR never cancels along an edge.
 *
 * The twisters are pre-drawn slices - a cross-section of the bar at
 * 64 angles and 8 sways - and each row is a 24-byte copy.
 *
 * Palette: 0 sky (raster), 1/2 back faces, 4/8 front faces, 5/6/9/10
 * front over back, 11-14 twister shades; Timer B also drives colour 8
 * (white above the caption, the rolling gradient through it).
 */

#include <stdlib.h>
#include <string.h>
#include "demo.h"
#include "dots.h"
#include "text.h"
#include "fast.h"

#define NOINLINE __attribute__((noinline))
#define BAR(b) ((b) * SONG_FPB)
#define GL_H 168                /* rows above the scroller          */
#define GL_X0 48                /* the glenz box, group aligned     */
#define GL_X1 272
#define NV 14
#define NT 24
#define TW_ANG 64
#define TW_REP 192              /* angles stored: 64 repeated thrice */
#define TW_SWAY 8
#define TW_W 32                 /* 2 groups                         */

static int8_t tri[NT][4];       /* a, b, c, class                   */
static int16_t vx[NV], vy[NV], vz[NV];
static int16_t box_y0[2], box_y1[2];    /* rows the glenz used, per page */
static int16_t box_g0[2], box_g1[2];    /* and its groups (even-aligned) */
static uint32_t *twist;         /* [TW_ANG][TW_SWAY][2 groups * 2 longs] */
static int16_t recip[1024];
/* per phase, the byte offset of a slice relative to the base angle:
 * the wobble's angle and the sway's slot folded together */
static int16_t tw_off_l[1024], tw_off_r[1024];
static uint8_t captioned[2];
static uint32_t lastf;
static uint16_t sky[RAST_MAXN], scrl[RAST_MAXN];


/* ------------------------------------------------------------ build */

void precalc_glenz(void)
{
    static const int8_t sgn[2] = { -1, 1 };
    int f, i, k, a, s;
    int8_t px[NV], py[NV], pz[NV];
    int nt = 0;

    /* cube corners, then the six pyramid apexes */
    for (i = 0; i < 8; i++) {
        px[i] = (int8_t)(sgn[i & 1] * 34);
        py[i] = (int8_t)(sgn[(i >> 1) & 1] * 34);
        pz[i] = (int8_t)(sgn[(i >> 2) & 1] * 34);
    }
    for (i = 0; i < 6; i++) {
        int axis = i >> 1, v = (i & 1) ? 50 : -50;
        px[8 + i] = (int8_t)(axis == 0 ? v : 0);
        py[8 + i] = (int8_t)(axis == 1 ? v : 0);
        pz[8 + i] = (int8_t)(axis == 2 ? v : 0);
    }
    /* each cube face: four triangles round its apex. The class is the
     * direction of the cube edge a triangle stands on, which makes
     * every pair of neighbours differ - a checkerboard */
    for (f = 0; f < 6; f++) {
        int axis = f >> 1, side = f & 1;
        int ring[4], n = 0;
        int ax1 = (axis + 1) % 3, ax2 = (axis + 2) % 3;
        static const int order[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
        for (k = 0; k < 4; k++) {
            int c = side << axis;
            c |= order[k][0] << ax1;
            c |= order[k][1] << ax2;
            ring[n++] = c;
        }
        for (k = 0; k < 4; k++) {
            int b = ring[k], c = ring[(k + 1) & 3];
            int ap = 8 + f;
            /* edge b->c runs along ax1 when the ax2 bits agree */
            int along = ((b >> ax2) & 1) == ((c >> ax2) & 1) ? ax1 : ax2;
            int cls = along == (axis + 1) % 3 ? 0 : 1;
            /* wind it counter-clockwise seen from outside */
            int ux = px[b] - px[ap], uy = py[b] - py[ap], uz = pz[b] - pz[ap];
            int wx = px[c] - px[ap], wy = py[c] - py[ap], wz = pz[c] - pz[ap];
            int nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            if (nx * px[ap] + ny * py[ap] + nz * pz[ap] < 0) {
                int t = b; b = c; c = t;
            }
            tri[nt][0] = (int8_t)ap;
            tri[nt][1] = (int8_t)b;
            tri[nt][2] = (int8_t)c;
            tri[nt][3] = (int8_t)cls;
            nt++;
        }
    }
    for (i = 0; i < NV; i++) {
        vx[i] = px[i];
        vy[i] = py[i];
        vz[i] = pz[i];
    }
    for (i = 2; i < 1024; i++) {
        recip[i] = (int16_t)(65536L / i);
    }
    for (i = 0; i < 1024; i++) {
        int sway = 4 + mul14(SIN(i * 3 + 100), 3);
        tw_off_l[i] = (int16_t)((mul14(SIN(i), 40) * TW_SWAY + sway) * 16);
        tw_off_r[i] = (int16_t)((mul14(SIN(i), 48) * TW_SWAY + (7 - sway)) * 16);
    }
    recip[0] = recip[1] = 32767;

    /* twister slices: the bar's cross-section at each angle */
    twist = malloc(TW_REP * TW_SWAY * 4 * sizeof(uint32_t));
    if (twist == NULL) {
        mem_fail = 1;
    } else {
        for (a = 0; a < TW_ANG; a++) {
            for (s = 0; s < TW_SWAY; s++) {
                uint8_t row[TW_W];
                uint16_t pw[2][4];
                int cx = 16 + (s - 4) * 2, j, g;
                memset(row, 0, sizeof(row));
                for (k = 0; k < 4; k++) {
                    /* corners of a square, half-diagonal 21px */
                    int p0 = a * 4 + k * 256 + 128;      /* 64 angles = 90 deg */
                    int x0 = cx + mul14(COS(p0), 14);
                    int x1 = cx + mul14(COS(p0 + 256), 14);
                    int nz = SIN(p0 + 128);               /* face normal z */
                    if (nz > 0 && x0 != x1) {
                        int shade = nz >> 12;            /* 0..3 */
                        int lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
                        for (j = lo; j < hi; j++) {
                            if (j >= 0 && j < TW_W) {
                                row[j] = (uint8_t)(11 + (shade > 3 ? 3 : shade));
                            }
                        }
                        if (lo >= 0 && lo < TW_W) {
                            row[lo] = 11;                /* edge line */
                        }
                    }
                }
                memset(pw, 0, sizeof(pw));
                for (j = 0; j < TW_W; j++) {
                    uint8_t c = row[j];
                    uint16_t *q = pw[j >> 4];
                    q[0] = (uint16_t)((q[0] << 1) | (c & 1));
                    q[1] = (uint16_t)((q[1] << 1) | ((c >> 1) & 1));
                    q[2] = (uint16_t)((q[2] << 1) | ((c >> 2) & 1));
                    q[3] = (uint16_t)((q[3] << 1) | ((c >> 3) & 1));
                }
                for (g = 0; g < 2; g++) {
                    int rep;
                    for (rep = 0; rep < TW_REP; rep += TW_ANG) {
                        uint32_t *o = twist + (((a + rep) * TW_SWAY + s) * 4) + g * 2;
                        o[0] = ((uint32_t)pw[g][0] << 16) | pw[g][1];
                        o[1] = ((uint32_t)pw[g][2] << 16) | pw[g][3];
                    }
                }
            }
        }
    }
}

static void enter(void)
{
    int i;

    demo_clear_both(0);
    box_y0[0] = box_y0[1] = 0;
    box_y1[0] = box_y1[1] = 0;
    captioned[0] = captioned[1] = 0;
    lastf = 0;
    for (i = 0; i < RAST_MAXN; i++) {
        int y = i * 4;                  /* raster_on2(4, ...) below */
        /* deep blue at the top to violet at the horizon */
        sky[i] = y < GL_H ? HWC(col_mix(0x002, 0x427, y * 256 / GL_H)) : 0;
        /* colour 8: the glenz's own white above the band (it is
         * front-face-B-alone, the odd edge pixel), a gradient in it */
        scrl[i] = y < GL_H ? HWC(0xFFF)
                           : HWC(col_mix(0xFFF, 0xC4F, ((y - GL_H) & 31) * 8));
    }
    raster_on2(4, 8);
}

static void leave(void)
{
    raster_off();
}

/* ------------------------------------------------------- rasteriser */

/*
 * One triangle, XORed into one plane as two trapezoids. Vertices in
 * 1/16 pixel; a row is covered where its centre lies inside, and a
 * span runs from the first pixel centre at or right of the left
 * edge to the last one left of the right edge - the top-left rule,
 * so triangles sharing an edge never share a pixel and the XOR
 * never cancels along it. A shared edge is always stepped from its
 * top vertex with the same slope, so both sides agree exactly.
 */
/* (dx << 16) / dy: two chained divu.w in STDL_FixDiv - libgcc's
 * 32-bit divide costs four times as much and the glenz wants 72 a
 * frame. Forced inline: a call costs more than the divide */
static inline __attribute__((always_inline))
int32_t slope(int x0, int y0, int x1, int y1)
{
    return y1 > y0 ? STDL_FixDiv(x1 - x0, y1 - y0) : 0;
}

/* x (16.16, rounding folded in) where an edge crosses row r's
 * centre; the fraction is 0..15 sixteenths, so four adds, not a
 * library multiply */
static int32_t edge_at(int x0, int y0, int32_t s, int r)
{
    int frac = (r << 4) + 8 - y0;           /* 1/16 rows past y0 */
    int32_t s4 = s >> 4, x = ((int32_t)x0 << 12) + 32767;

    if (s4 == (int16_t)s4) {
        return x + mul16(s4, frac);         /* under 8 px a row */
    }
    if (frac & 1) x += s4;
    if (frac & 2) x += s4 << 1;
    if (frac & 4) x += s4 << 2;
    if (frac & 8) x += s4 << 3;
    return x;
}

/*
 * A back face at half vertical resolution: a "pair" v is rows 2v and
 * 2v+1, covered when the point between their centres - y = 2v+1 - is
 * inside, so every back triangle uses the same pairs and they still
 * tile the silhouette exactly once.
 */
static int32_t edge_at2(int x0, int y0, int32_t s, int v)
{
    int frac = (v << 5) + 16 - y0;          /* 1/16 rows past y0 */
    int32_t s4 = s >> 4, x = ((int32_t)x0 << 12) + 32767;

    if (s4 == (int16_t)s4) {
        return x + mul16(s4, frac);
    }
    while (frac >= 16) {
        x += s;
        frac -= 16;
    }
    if (frac & 1) x += s4;
    if (frac & 2) x += s4 << 1;
    if (frac & 4) x += s4 << 2;
    if (frac & 8) x += s4 << 3;
    return x;
}

static void raster_tri2(int ax, int ay, int bx, int by, int cx, int cy,
                        uint8_t *plane, int *ymin, int *ymax)
{
    int t, v0, v1, vb;
    int32_t sac, sab, sbc, xa, xb;

    if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; }
    if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    v0 = (ay + 15) >> 5;
    vb = (by + 15) >> 5;
    v1 = (cy + 15) >> 5;
    if (v1 <= v0 || v0 < 0 || v1 * 2 > GL_H) {
        return;
    }
    sac = slope(ax, ay, cx, cy);
    sab = slope(ax, ay, bx, by);
    sbc = slope(bx, by, cx, cy);
    xa = edge_at2(ax, ay, sac, v0);
    if (v0 * 2 < *ymin) *ymin = v0 * 2;
    if (v1 * 2 > *ymax) *ymax = v1 * 2;
    if (MUL16(bx - ax, cy - ay) - MUL16(cx - ax, by - ay) < 0) {
        if (vb > v0) {
            xb = edge_at2(ax, ay, sab, v0);
            xor_trap2(plane + row_off[v0 * 2], &xb, sab * 2, &xa, sac * 2, vb - v0);
        }
        if (v1 > vb) {
            xb = edge_at2(bx, by, sbc, vb);
            xor_trap2(plane + row_off[vb * 2], &xb, sbc * 2, &xa, sac * 2, v1 - vb);
        }
    } else {
        if (vb > v0) {
            xb = edge_at2(ax, ay, sab, v0);
            xor_trap2(plane + row_off[v0 * 2], &xa, sac * 2, &xb, sab * 2, vb - v0);
        }
        if (v1 > vb) {
            xb = edge_at2(bx, by, sbc, vb);
            xor_trap2(plane + row_off[vb * 2], &xa, sac * 2, &xb, sbc * 2, v1 - vb);
        }
    }
}

static void raster_tri(int ax, int ay, int bx, int by, int cx, int cy,
                       uint8_t *plane, int *ymin, int *ymax)
{
    int t, r0, r1, rb;
    int32_t sac, sab, sbc, xa, xb;

    if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; }
    if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    r0 = (ay + 7) >> 4;
    rb = (by + 7) >> 4;
    r1 = (cy + 7) >> 4;
    if (r1 <= r0 || r0 < 0 || r1 > GL_H) {
        return;                 /* the geometry keeps it on screen */
    }
    sac = slope(ax, ay, cx, cy);
    sab = slope(ax, ay, bx, by);
    sbc = slope(bx, by, cx, cy);
    xa = edge_at(ax, ay, sac, r0);
    if (r0 < *ymin) *ymin = r0;
    if (r1 > *ymax) *ymax = r1;
    /* b left of the long edge a->c, or right of it */
    if (MUL16(bx - ax, cy - ay) - MUL16(cx - ax, by - ay) < 0) {
        if (rb > r0) {
            xb = edge_at(ax, ay, sab, r0);
            xor_trap(plane + row_off[r0], &xb, sab, &xa, sac, rb - r0);
        }
        if (r1 > rb) {
            xb = edge_at(bx, by, sbc, rb);
            xor_trap(plane + row_off[rb], &xb, sbc, &xa, sac, r1 - rb);
        }
    } else {
        if (rb > r0) {
            xb = edge_at(ax, ay, sab, r0);
            xor_trap(plane + row_off[r0], &xa, sac, &xb, sab, rb - r0);
        }
        if (r1 > rb) {
            xb = edge_at(bx, by, sbc, rb);
            xor_trap(plane + row_off[rb], &xa, sac, &xb, sbc, r1 - rb);
        }
    }
}

static NOINLINE void glenz(uint32_t f, int kick)
{
    static const uint8_t plane_off[4] = { 0, 2, 4, 6 };
    int t = (int)(f - BAR(32));
    int sa = SIN(t * 5), ca = COS(t * 5);
    int sb = SIN(t * 3 + 100), cb = COS(t * 3 + 100);
    int sc = SIN(t * 2), cc = COS(t * 2);
    int scale = 256 + kick / 16;
    int px[NV], py[NV];
    int ymin = GL_H, ymax = 0, i;

    /* clear what this page drew last time, rows and columns */
    if (box_y1[page] > box_y0[page]) {
        clear_box(scr->pixels + row_off[box_y0[page]] + box_g0[page] * 8,
                  box_g1[page] - box_g0[page], box_y1[page] - box_y0[page]);
    }
    for (i = 0; i < NV; i++) {
        int x = vx[i], y = vy[i], z = vz[i], x1, y1, z1, y2, z2, k;
        x1 = mul14(x, ca) - mul14(z, sa);
        z1 = mul14(x, sa) + mul14(z, ca);
        y1 = mul14(y, cb) - mul14(z1, sb);
        z2 = mul14(y, sb) + mul14(z1, cb);
        y2 = mul14(x1, sc) + mul14(y1, cc);
        x1 = mul14(x1, cc) - mul14(y1, sc);
        k = (int)(MUL16(recip[z2 + 300], scale) >> 8);
        /* to 1/16 pixel */
        px[i] = 2560 + (int)(MUL16(x1, k) >> 4);
        py[i] = 84 * 16 + (int)(MUL16(y2, k) >> 4);
    }
    for (i = 0; i < NT; i++) {
        int a = tri[i][0], b = tri[i][1], c = tri[i][2];
        int32_t area = MUL16(px[b] - px[a], py[c] - py[a]) -
                       MUL16(px[c] - px[a], py[b] - py[a]);
        /* screen y runs down, so counter-clockwise from outside
         * comes out with a negative area: front faces into planes
         * 2/3, back faces into 0/1 */
        int list = (area < 0 ? 2 : 0) + tri[i][3];
        if (area < 0) {
            raster_tri(px[a], py[a], px[b], py[b], px[c], py[c],
                       scr->pixels + plane_off[list], &ymin, &ymax);
        } else {
            raster_tri2(px[a], py[a], px[b], py[b], px[c], py[c],
                        scr->pixels + plane_off[list], &ymin, &ymax);
        }
    }
    box_y0[page] = (int16_t)ymin;
    box_y1[page] = (int16_t)ymax;
    {
        /* the columns the vertices span, widened to even groups for
         * clear_box (pixels are 1/16, so >> 8 is the group) */
        int minx = px[0], maxx = px[0], g0, g1;
        for (i = 1; i < NV; i++) {
            if (px[i] < minx) minx = px[i];
            if (px[i] > maxx) maxx = px[i];
        }
        g0 = (minx >> 8) & ~1;
        g1 = ((maxx >> 8) + 2) & ~1;
        if (g0 < GL_X0 / 16) g0 = GL_X0 / 16 & ~1;
        if (g1 > GL_X1 / 16) g1 = GL_X1 / 16;
        box_g0[page] = (int16_t)g0;
        box_g1[page] = (int16_t)g1;
    }
}

/*
 * Both twisters, a row at a time: a phase per twister steps down the
 * screen, its table gives the slice's offset from the frame's base
 * angle, and the slice's four longs are copied in. a0 = screen,
 * a1/a2 = left/right base slice, a3/a4 = offset tables.
 */
void twist_rows(uint32_t *dst, const uint32_t *bl, const uint32_t *br,
                const int16_t *offl, const int16_t *offr, int phl, int phr,
                int rows);
__asm__(
"    .text\n"
"    .even\n"
"    .globl _twist_rows\n"
"_twist_rows:\n"
"    movem.l %d2-%d7/%a2-%a5,-(%sp)\n"
"    move.l 44(%sp),%a0\n"
"    move.l 48(%sp),%a1\n"
"    move.l 52(%sp),%a2\n"
"    move.l 56(%sp),%a3\n"
"    move.l 60(%sp),%a4\n"
"    move.l 64(%sp),%d0\n"          /* left phase, *2 for words     */
"    add.w  %d0,%d0\n"
"    move.l 68(%sp),%d1\n"
"    add.w  %d1,%d1\n"
"    move.l 72(%sp),%d7\n"
"    move.w #2046,%d5\n"
"    subq.w #1,%d7\n"
"    bmi.s  9f\n"
"1:  and.w  %d5,%d0\n"
"    move.w 0(%a3,%d0.w),%d6\n"
"    lea    0(%a1,%d6.w),%a5\n"
"    move.l (%a5)+,(%a0)+\n"
"    move.l (%a5)+,(%a0)+\n"
"    move.l (%a5)+,(%a0)+\n"
"    move.l (%a5)+,(%a0)+\n"
"    and.w  %d5,%d1\n"
"    move.w 0(%a4,%d1.w),%d6\n"
"    lea    0(%a2,%d6.w),%a5\n"
"    move.l (%a5)+,128(%a0)\n"
"    move.l (%a5)+,132(%a0)\n"
"    move.l (%a5)+,136(%a0)\n"
"    move.l (%a5)+,140(%a0)\n"
"    lea    144(%a0),%a0\n"
"    addq.w #8,%d0\n"               /* left phase += 4              */
"    addq.w #6,%d1\n"               /* right phase += 3             */
"    dbra   %d7,1b\n"
"9:  movem.l (%sp)+,%d2-%d7/%a2-%a5\n"
"    rts\n"
);

static NOINLINE void twisters(uint32_t f)
{
    int t = (int)(f - BAR(32));
    const uint32_t *bl, *br;

    if (twist == NULL) {
        return;
    }
    /* base angle in the middle copy, so +-48 of wobble stays inside */
    bl = twist + ((TW_ANG + ((t * 3) & 63)) * TW_SWAY) * 4;
    br = twist + ((TW_ANG + ((-t * 2) & 63)) * TW_SWAY) * 4;
    twist_rows((uint32_t *)scr->pixels, bl, br, tw_off_l, tw_off_r,
               t * 5, 300 - t * 4, GL_H);
}

static void frame(uint32_t f)
{
    uint16_t pal[16], *rt;
    int kick = env_decay(sync_since(f, S_KICK | S_BOOM), 10);
    int t = (int)(f - BAR(32));
    int i, n;

    if (sync_hit(f, lastf, S_CRASH | S_BOOM)) {
        fx_flash = 200;
    }
    glenz(f, kick);
    twisters(f);

    /* the caption, once per page, in plane 3: its colour is Timer B's
     * colour 8, a gradient that rolls down through it every frame */
    if (!captioned[page]) {
        bp_text16(scr, GL_H + 8, "GLENZ VECTORS!", 8, NULL);
        captioned[page] = 1;
    }

    rt = raster_buf();
    n = raster_n();
    for (i = 0; i < n; i++) {
        rt[2 * i] = sky[i];
        rt[2 * i + 1] = i < GL_H / 4 ? scrl[0] : scrl[GL_H / 4 + ((i + t) & 7)];
    }

    pal[0] = 0x000;
    pal[1] = 0x135;             /* back A               */
    pal[2] = 0x314;             /* back B               */
    pal[3] = 0x000;
    pal[4] = 0x7DF;             /* front A, nothing behind (edges) */
    pal[5] = 0x59C;             /* front A over back A  */
    pal[6] = 0x77B;             /* front A over back B  */
    pal[7] = 0x000;
    pal[8] = 0xFFF;             /* front B alone        */
    pal[9] = 0xBCE;             /* front B over back A  */
    pal[10] = 0xDBD;            /* front B over back B  */
    pal[11] = 0x310;            /* twister shades       */
    pal[12] = 0x830;
    pal[13] = 0xC71;
    pal[14] = 0xFC6;
    pal[15] = 0xFFF;
    if (kick > 0) {
        for (i = 4; i <= 10; i++) {
            pal[i] = col_mix(pal[i], 0xFFF, kick / 4);
        }
    }
    pal_show(pal);
    lastf = f;
}

const Part part_glenz = { "glenz", enter, frame, leave };
