/*
 * STDL Megademo - the VBL service, the song clock and the palette
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One STDL_AddVBL callback does what must happen in the vertical
 * blank: count VBLs (for frame pacing, and the clock when there is
 * no music) and write the staged palette. The song clock is the
 * music's own position; the rasters are STDL's (raster.c).
 */

#include <stdio.h>
#include <string.h>
#include "demo.h"
#include "dots.h"
#include "gen/stm.h"

volatile uint32_t vbl_ticks, vbl_free;
static volatile uint8_t paused;

static STDL_Music *music;
static uint32_t base_frame;         /* song frame where this stream starts */

/* staged palette: written by the main line, uploaded in the VBL */
static uint16_t pal_stage[16];
static volatile uint8_t pal_dirty;

int fx_flash;
int fx_fade = 256;

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

/* The callback: no GEMDOS, no drawing, no allocation (stdl_vbl.h). */
static void demo_vbl(void)
{
    vbl_free++;
    if (!paused) {
        vbl_ticks++;
    }
    if (pal_dirty) {
        const uint32_t *s = (const uint32_t *)pal_stage;
        volatile uint32_t *d = (volatile uint32_t *)0xffff8240UL;
        int i;
        for (i = 0; i < 8; i++) {
            d[i] = s[i];
        }
        pal_dirty = 0;
    }
}

int core_init(void)
{
    return STDL_AddVBL(demo_vbl);
}

void core_quit(void)
{
    STDL_RemoveVBL(demo_vbl);
    if (music != NULL) {
        STDL_HaltMusic();
        STDL_FreeMusic(music);
        music = NULL;
    }
}

/* ------------------------------------------------------------ clock */

/* the music's own position (STDL_MusicFrame: the next frame it will
 * play, which is what the next flip shows); the VBL count only when
 * there is no music */
uint32_t sync_frame(void)
{
    int fr = music != NULL ? STDL_MusicFrame() : -1;

    if (fr >= 0) {
        return (uint32_t)fr % SONG_FRAMES;
    }
    return (base_frame + vbl_ticks) % SONG_FRAMES;
}

/*
 * One song, linked into the program and played where it lies
 * (STDL_MusicFromMemory); jumping to a part is a seek, so there is no
 * file to load and nothing to keep in step but the music itself.
 */
int sync_start(int part)
{
    uint16_t sr;

    base_frame = (uint32_t)song_part_bar[part] * SONG_FPB;
    if (music == NULL) {
        music = STDL_MusicFromMemory(song_stm, SONG_STM_BYTES);
        if (music != NULL) {
            STDL_PlayMusic(music, -1);
        }
    }
    sr = irq_off();
    vbl_ticks = 0;
    paused = 0;
    irq_restore(sr);
    if (music == NULL) {
        return -1;
    }
    STDL_ResumeMusic();
    return STDL_SeekMusic((int)base_frame);
}

void sync_stop(void)
{
    STDL_HaltMusic();
}

void sync_pause(int on)
{
    paused = (uint8_t)(on != 0);
    if (music != NULL) {
        if (on) {
            STDL_PauseMusic();
        } else {
            STDL_ResumeMusic();
        }
    }
}

int sync_paused(void)
{
    return paused;
}

int sync_since(uint32_t f, uint16_t mask)
{
    int row = (int)(f / SONG_FPR);
    int k;

    for (k = 0; k < 64; k++) {
        int r = row - k;
        if (r < 0) {
            r += SONG_ROWS;
        }
        if (song_flags[r] & mask) {
            return (int)(f % SONG_FPR) + k * SONG_FPR;
        }
    }
    return 9999;
}

/* the stream's registers as it last set them (STDL_GetMusicRegisters):
 * no reading the chip, so no masking interrupts round a select */
void ym_peek(uint8_t vol[3], uint16_t per[3], uint8_t *mixer)
{
    uint8_t r[14];
    int i;

    STDL_GetMusicRegisters(r);
    for (i = 0; i < 3; i++) {
        per[i] = (uint16_t)(r[2 * i] | ((r[2 * i + 1] & 15) << 8));
        vol[i] = r[8 + i] & 0x1F;
    }
    if (mixer != NULL) {
        *mixer = r[7];
    }
}

/* ---------------------------------------------------------- palette */

uint16_t hwtab[4096];
uint16_t huetab[16 * 256];
static uint8_t lerp[17 * 256];      /* [t/16][a][b] channel lerp */

void colour_init(void)
{
    static const uint8_t rot[16] = {
        0x0, 0x8, 0x1, 0x9, 0x2, 0xA, 0x3, 0xB,
        0x4, 0xC, 0x5, 0xD, 0x6, 0xE, 0x7, 0xF
    };
    int i, t, a, b;

    for (i = 0; i < 4096; i++) {
        hwtab[i] = (uint16_t)((rot[i >> 8] << 8) | (rot[(i >> 4) & 15] << 4) |
                              rot[i & 15]);
    }
    for (t = 0; t <= 16; t++) {
        for (a = 0; a < 16; a++) {
            for (b = 0; b < 16; b++) {
                lerp[(t << 8) | (a << 4) | b] =
                    (uint8_t)(a + ((b - a) * t + (b > a ? 8 : -8)) / 16);
            }
        }
    }
    for (i = 0; i < 256; i++) {
        int s = (i % 43) * 15 / 42, r, g, bb, k;
        switch (i / 43) {
            case 0:  r = 15;     g = s;      bb = 0;      break;
            case 1:  r = 15 - s; g = 15;     bb = 0;      break;
            case 2:  r = 0;      g = 15;     bb = s;      break;
            case 3:  r = 0;      g = 15 - s; bb = 15;     break;
            case 4:  r = s;      g = 0;      bb = 15;     break;
            default: r = 15;     g = 0;      bb = 15 - s; break;
        }
        for (k = 0; k < 16; k++) {
            huetab[(k << 8) | i] = rgb4(r * k / 15, g * k / 15, bb * k / 15);
        }
    }
}

uint16_t col_mix(uint16_t a, uint16_t b, int t)
{
    const uint8_t *l;

    if (t <= 0) {
        return a;
    }
    if (t >= 256) {
        return b;
    }
    l = lerp + ((t >> 4) << 8);
    return (uint16_t)((l[((a >> 4) & 0xF0) | (b >> 8)] << 8) |
                      (l[(a & 0xF0) | ((b >> 4) & 15)] << 4) |
                       l[((a << 4) & 0xF0) | (b & 15)]);
}

uint16_t col_scale(uint16_t a, int t)
{
    return col_mix(0, a, t);
}

void pal_show(const uint16_t pal[16])
{
    uint16_t out[16];
    uint16_t sr;
    int i;

    for (i = 0; i < 16; i++) {
        uint16_t c = pal[i];
        if (fx_flash > 0) {
            c = col_mix(c, 0x0FFF, fx_flash);
        }
        if (fx_fade < 256) {
            c = col_scale(c, fx_fade);
        }
        out[i] = HWC(c);
    }
    sr = irq_off();
    memcpy(pal_stage, out, sizeof(out));
    pal_dirty = 1;
    irq_restore(sr);
}

void pal_black(void)
{
    static const uint16_t black[16];
    int f = fx_flash, d = fx_fade;

    fx_flash = 0;
    fx_fade = 256;
    pal_show(black);
    fx_flash = f;
    fx_fade = d;
}

/* ------------------------------------------------------------ misc */

uint16_t rnd(void)
{
    static uint16_t s = 0xACE1u;
    /* 16-bit xorshift: no multiply */
    s ^= (uint16_t)(s << 7);
    s ^= (uint16_t)(s >> 9);
    s ^= (uint16_t)(s << 8);
    return s;
}

uint16_t row_off[280];
uint16_t grp_off[320];
uint16_t bit_of[320];
uint32_t xinfo[320];

void tables_init(void)
{
    int i;

    for (i = 0; i < 280; i++) {
        row_off[i] = (uint16_t)(i * 160);
    }
    for (i = 0; i < 320; i++) {
        grp_off[i] = (uint16_t)((i >> 4) * 8);
        bit_of[i] = (uint16_t)(0x8000u >> (i & 15));
        xinfo[i] = ((uint32_t)grp_off[i] << 16) | bit_of[i];
    }
}

/* rows that began after lastf, up to and including f's */
int sync_hit(uint32_t f, uint32_t lastf, uint16_t mask)
{
    uint32_t r = f / SONG_FPR, r0 = lastf / SONG_FPR;

    if (f < lastf || r - r0 > 32) {
        return (f % SONG_FPR) < 2 && (song_flags[r % SONG_ROWS] & mask);
    }
    for (; r > r0; r--) {
        if (song_flags[r % SONG_ROWS] & mask) {
            return 1;
        }
    }
    return 0;
}
