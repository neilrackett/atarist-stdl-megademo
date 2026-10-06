/*
 * STDL Megademo
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared by every part: the screen, the song clock, the staged
 * palette, the Timer B rasters, the bitplane text routines and the
 * small amount of maths the effects have in common.
 */

#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>
#include <stdl/stdl.h>
#include "gen/song.h"
#include "gen/assets.h"

/* ------------------------------------------------------------ screen */

extern STDL_Surface *scr;       /* the page being drawn (back buffer) */
extern int page;                /* 0/1: which of the two that is      */
extern const STDL_MachineInfo *mach;

void demo_flip(void);           /* STDL_Flip, rasters handed over too */
/* VBLs a frame lasts (1 = 50fps, 2 = 25fps, 3 = 16.7fps), set by the
 * main loop from what the frames actually cost: a part that cannot
 * hold 50 is steadier held to 25 than wobbling between the two, and
 * a faster machine is given the rate it can hold */
extern int pace;
void demo_clear_both(uint8_t col); /* fill both pages, flipping twice  */

/* rows of the current page; screen stride is always 160 */
static inline uint8_t *scr_row(int y)
{
    return scr->pixels + (uint16_t)y * 160u;
}

/* ------------------------------------------------------- song clock */

/*
 * The clock everything runs on: the song frame (0..SONG_FRAMES-1)
 * the YM is playing, read from the music itself (STDL_MusicFrame).
 * A part that renders at 25fps still sees the song move at 50Hz, so
 * it lands on the beat whatever it costs.
 */
uint32_t sync_frame(void);
int      sync_start(int part);      /* seek the song to part n, play  */
void     sync_pause(int on);
int      sync_paused(void);
void     sync_stop(void);
/* frames since the last row (within 4 bars) carrying any of `mask` */
int      sync_since(uint32_t f, uint16_t mask);
static inline uint16_t sync_flags(uint32_t f)
{
    return song_flags[(f / SONG_FPR) % SONG_ROWS];
}
/* the YM registers as the music last set them: volume 0-15 per voice
 * (16 = envelope) and period */
void ym_peek(uint8_t vol[3], uint16_t per[3], uint8_t *mixer);

/* decaying 0..256 envelope from a frame count */
static inline int env_decay(int since, int len)
{
    return since >= len ? 0 : 256 - (since << 8) / len;
}

/* VBL ticks of the song clock (paused with it, reset by sync_start)
 * and a free-running count for timing */
extern volatile uint32_t vbl_ticks, vbl_free;

/* -------------------------------------------------------------- palette */

/*
 * Colours are linear 0xRGB, four bits a channel, everywhere in the
 * program; the hardware's STE layout (each nibble rotated so a plain
 * ST reads the top three bits) is one table lookup on the way out -
 * pal_show does it, and raster tables are filled through HWC().
 * Mixing and the hue wheel are tables too: a raster table wants a
 * hundred colours a frame, and the obvious arithmetic is a library
 * multiply or divide per channel on a 68000.
 */
static inline uint16_t rgb4(int r, int g, int b)
{
    return (uint16_t)((r << 8) | (g << 4) | b);
}
extern uint16_t hwtab[4096];
#define HWC(c) (hwtab[(c) & 0xFFF])
uint16_t col_mix(uint16_t a, uint16_t b, int t);   /* t 0..256 */
uint16_t col_scale(uint16_t a, int t);             /* t 0..256 */
extern uint16_t huetab[16 * 256];
/* bright, saturated hue wheel: h 0..1023 (wraps), bright 0..15 */
static inline uint16_t col_hue(int h, int bright)
{
    return huetab[(bright << 8) | ((h >> 2) & 255)];
}
void colour_init(void);

/*
 * Palette writes are staged and land in the vertical blank, so a
 * colour never changes half way down the picture. pal_show applies
 * the global flash (towards white) and fade (towards black) on the
 * way.
 */
void pal_show(const uint16_t pal[16]);
void pal_black(void);
extern int fx_flash;            /* 0..256, decays by itself           */
extern int fx_fade;             /* 0..256 brightness, 256 = normal    */

/* ------------------------------------------------------------- rasters */

/*
 * Timer B rasters (STDL_OpenRasters underneath): colour 0 (background,
 * side borders) and colour 15 change every `step` displayed lines from
 * a table the program fills each frame. Two words an entry: colour 0,
 * colour 15 (or 8; raster_on2 with 0 is colour 0 alone, one word an
 * entry). Never open with a border open: STDL refuses.
 */
#define RAST_MAXN 201
int       raster_on(int step);
/* the same with the second colour 8 instead of 15, or 0 for colour
 * 0 alone (the second word of each entry is then ignored) */
int       raster_on2(int step, int second);
void      raster_off(void);
int       raster_active(void);
uint16_t *raster_buf(void);     /* table to fill for the next frame   */
int       raster_n(void);       /* entries in it (200 / step + 1)     */

/* ---------------------------------------------------------- text */

extern STDL_Font font8x8;
void text_init(void);
const uint16_t *glyph16(int ch);
const uint32_t *glyph32(int ch);
int  text_len(const char *s);

/*
 * Bitplane text: a 1bpp glyph ORed into the planes of a colour, so
 * it lands on a band the caller cleared to 0 in exactly that colour.
 * rowcol (optional) gives a colour per glyph row - a gradient for
 * nothing. Clipped to the screen; x and y may be anywhere.
 */
void bp_glyph16(STDL_Surface *s, int x, int y, int ch, uint8_t col,
                const uint8_t *rowcol);
/* centred line of 16px text */
void bp_text16(STDL_Surface *s, int y, const char *t, uint8_t col,
               const uint8_t *rowcol);
/* zero full-width rows y..y+h-1 */
void band_clear(STDL_Surface *s, int y, int h);

/* -------------------------------------------------------------- maths */

#define SIN(a) ((int)sintab[(a) & 1023])
#define COS(a) ((int)sintab[((a) + 256) & 1023])
/* 16x16->32 multiply and the like from stdl_math.h: inline asm, since
 * gcc 4.6 calls __mulsi3 for (int16_t)a * (int16_t)b whenever an
 * operand comes from a loop counter or the widths differ */
#include <stdl/stdl_math.h>
#define MUL16(a, b) STDL_MulS16((a), (b))
#define mul16(a, b) STDL_MulS16((a), (b))
/* (a * b) >> 14 with a 16-bit multiply */
static inline int mul14(int a, int b)
{
    return (int)(STDL_MulS16(a, b) >> 14);
}
uint16_t rnd(void);

/* 1-plane dots: tables for the native plotter (dots.c) */
extern uint16_t row_off[280];   /* y * 160                          */
extern uint16_t grp_off[320];   /* (x >> 4) * 8                     */
extern uint16_t bit_of[320];    /* 0x8000 >> (x & 15)               */
extern uint32_t xinfo[320];     /* grp_off << 16 | bit_of            */
void tables_init(void);

/* ------------------------------------------------------------- parts */

typedef struct {
    const char *name;
    void (*enter)(void);
    void (*frame)(uint32_t f);
    void (*leave)(void);
} Part;

extern const Part part_intro, part_plasma, part_boing, part_dots,
                  part_glenz, part_meta, part_outro;

/* one-off precalculation, run behind the loader */
void precalc_plasma(void);
void precalc_boing(void);
void precalc_dots(void);
void precalc_glenz(void);
void precalc_meta(void);
void precalc_outro(void);
void precalc_intro(void);
extern void (*loader_progress)(int done, int total);

/* set by a precalc that could not get its memory */
extern int mem_fail;

/* frame counters for the debug log */
extern uint32_t stat_frames[SONG_PARTS], stat_vbls[SONG_PARTS];
extern uint32_t stat_hist[SONG_PARTS][4];  /* frames of 1, 2, 3, 4+ VBLs */

#endif /* DEMO_H */
