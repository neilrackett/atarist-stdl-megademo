/*
 * STDL Megademo - boot, loader, title and the part sequencer
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Seven parts over a 56-bar YM song. The song decides everything:
 * which part is on is a function of the frame the music is playing,
 * so a slow part drops frames and never drifts off the beat.
 *
 * Keys: SPACE pause, 1-7 or LEFT/RIGHT jump between parts, ESC quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include "demo.h"
#include "fast.h"

STDL_Surface *scr;
int page;
const STDL_MachineInfo *mach;
uint32_t stat_frames[SONG_PARTS], stat_vbls[SONG_PARTS];
uint32_t stat_hist[SONG_PARTS][4];
void (*loader_progress)(int done, int total);

int  core_init(void);
void core_quit(void);
void raster_commit(void);

static const Part *const parts[SONG_PARTS] = {
    &part_intro, &part_plasma, &part_boing, &part_dots,
    &part_glenz, &part_meta, &part_outro
};
static int cur = -1;

int pace = 1;
int mem_fail;
static uint32_t last_flip;
static int calm;

/*
 * The frame's own cost decides the pace: the VBLs that went by while
 * it was drawn (it starts just after a flip, so that is the whole
 * ones) plus the one the flip waits for. Two overruns in a row raise
 * it - one spike only costs its own frame - and eight frames that
 * would all have fitted bring it down a step.
 */
static int over, worst;

static void pace_update(int skip)
{
    int need = (int)(vbl_free - last_flip) + 1;

    if (skip) {
        over = calm = 0;
        return;
    }
    if (need > 4) {
        need = 4;
    }
    if (need > pace) {
        calm = 0;
        if (need > worst) {
            worst = need;
        }
        if (++over >= 2) {
            pace = worst;
            over = worst = 0;
        }
    } else if (need < pace) {
        over = worst = 0;
        if (++calm >= 8) {
            pace--;
            calm = 0;
        }
    } else {
        over = worst = calm = 0;
    }
}

void demo_flip(void)
{
    /* hold the frame until pace - 1 VBLs have passed since the last
     * flip; STDL_Flip then waits for one more and shows it */
    while ((int)(vbl_free - last_flip) < pace - 1) {
        STDL_WaitVBL();
    }
    raster_commit();
    STDL_Flip();
    last_flip = vbl_free;
    page ^= 1;
}

void demo_clear_both(uint8_t col)
{
    STDL_Rect r;
    int i;

    for (i = 0; i < 2; i++) {
        r.x = 0; r.y = 0;
        r.w = (uint16_t)scr->w; r.h = (uint16_t)scr->h;
        STDL_FillRect(scr, &r, col);
        demo_flip();
    }
}

static void cleanup(void)
{
    raster_off();
}

/* --------------------------------------------------------- loader */

static const uint16_t title_pal[16] = {
    0x000, 0x000, 0x000, 0x000,
    /* chrome, as in the intro: outline, sky, horizon, ground */
    0x001, 0x113, 0x336, 0x77F, 0xFFF, 0x210, 0x531, 0xC72,
    0x000, 0x000, 0x000, 0x0FFF
};

static int load_steps, load_total;
static uint32_t load_t[8];

static void draw_bar(int done, int total)
{
    STDL_Rect r;

    r.x = 96; r.y = 150; r.w = 128; r.h = 6;
    STDL_FillRect(scr, &r, 15);
    r.x = 97; r.y = 151; r.w = 126; r.h = 4;
    STDL_FillRect(scr, &r, 0);
    r.w = (uint16_t)(126 * done / total);
    if (r.w > 0) {
        STDL_FillRect(scr, &r, 14);
    }
}

/* the depacker look: random stripes in the border colour */
static void stripes(void)
{
    uint16_t *t = raster_buf();
    int n = raster_n(), i;

    for (i = 0; i < n; i++) {
        uint16_t c = (rnd() & 3) == 0 ? (uint16_t)(rnd() & 0x777) : 0;
        t[2 * i] = c;
        t[2 * i + 1] = 0x0FFF;
    }
}

static void progress(int done, int total)
{
    int k;

    (void)total;
    load_steps = done;
    if (done < 8) {
        load_t[done] = vbl_free;
    }
    for (k = 0; k < 2; k++) {
        draw_bar(done, load_total);
        stripes();
        demo_flip();
    }
}

static void loader(void)
{
    static const uint16_t pal[16] = {
        0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3B3, 0xFFF
    };
    int i;

    pal_black();
    demo_clear_both(0);
    /*
     * The bottom border measures the machine the first time it opens:
     * a couple of frames with interrupts off, then a third of a second
     * trying the flick upward until the GLUE takes it, with the border
     * flickering. Do it now, with every colour black, and the plasma
     * part's border explosion later costs nothing.
     */
    if (STDL_OpenBottomBorder() != 0) {
        for (i = 0; i < 40; i++) {
            STDL_WaitVBL();
        }
        STDL_CloseBottomBorder();
    }
    scr = STDL_GetVideoSurface();
    demo_clear_both(0);

    pal_show(pal);
    for (i = 0; i < 2; i++) {
        STDL_DrawText(scr, &font8x8, 160 - 13 * 4, 100, "STDL MEGADEMO", 15);
        STDL_DrawText(scr, &font8x8, 160 - 12 * 4, 120, "DEPACKING...", 15);
        demo_flip();
    }
    raster_on(2);
    loader_progress = progress;
    load_total = 7;
    progress(0, load_total);
    precalc_intro();   progress(1, load_total);
    precalc_plasma();  progress(2, load_total);
    precalc_boing();   progress(3, load_total);
    precalc_dots();    progress(4, load_total);
    precalc_glenz();   progress(5, load_total);
    precalc_meta();    progress(6, load_total);
    precalc_outro();   progress(7, load_total);
    raster_off();
}

/* ---------------------------------------------------------- title */

STDL_Surface *intro_logo(void);    /* intro.c */

static int title(void)
{
    STDL_Surface *logo = intro_logo();
    STDL_Rect d;
    uint32_t t0 = vbl_ticks;
    int i, go = 0;

    pal_black();
    /* a marker for the test harness; the clear below hides it */
    fprintf(stderr, "MEGADEMO title\r\n");
    demo_clear_both(0);
    for (i = 0; i < 2; i++) {
        d.x = (int16_t)((320 - LOGO_W) / 2); d.y = 40;
        STDL_BlitSurface(logo, NULL, scr, &d);
        STDL_DrawText(scr, &font8x8, 160 - 21 * 4, 132,
                      "AN ATARI ST DEMO MADE", 14);
        STDL_DrawText(scr, &font8x8, 160 - 21 * 4, 142,
                      "WITH STDL, IN C + ASM", 14);
        demo_flip();
    }
    while (!go) {
        STDL_Event ev;
        uint32_t t = vbl_ticks - t0;
        uint16_t pal[16];
        STDL_Rect r;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT) {
                return 0;
            }
            if (ev.type == STDL_KEYDOWN) {
                if (ev.key.keysym.sym == STDLK_ESCAPE) {
                    return 0;
                }
                go = 1;
            }
            if (ev.type == STDL_JOYBUTTONDOWN) {
                go = 1;
            }
        }
        if (t > 600) {
            go = 1;              /* a demo starts by itself */
        }
        for (i = 0; i < 16; i++) {
            pal[i] = title_pal[i];
        }
        pal[14] = 0x666;
        /* fade in over half a second */
        fx_fade = t < 25 ? (int)(t * 256 / 25) : 256;
        pal_show(pal);
        /* blinking prompt, like an arcade's INSERT COIN */
        r.x = 100; r.y = 170; r.w = 120; r.h = 8;
        STDL_FillRect(scr, &r, 0);
        if ((t / 25) & 1) {
            STDL_DrawText(scr, &font8x8, 160 - 11 * 4, 170, "PRESS SPACE", 15);
        }
        demo_flip();
    }
    /* fade out */
    for (i = 16; i >= 0; i--) {
        fx_fade = i * 16;
        pal_show(title_pal);
        demo_flip();
    }
    fx_fade = 256;
    pal_black();
    return 1;
}

/* ----------------------------------------------------------- show */

static int part_of(uint32_t f)
{
    int bar = (int)(f / SONG_FPB), p = 0, i;

    for (i = 0; i < SONG_PARTS; i++) {
        if (bar >= song_part_bar[i]) {
            p = i;
        }
    }
    return p;
}

static void enter_part(int p)
{
    if (cur >= 0 && parts[cur]->leave != NULL) {
        parts[cur]->leave();
    }
    cur = p;
    pal_black();
    scr = STDL_GetVideoSurface();
    pace = 1;
    calm = 0;
    parts[cur]->enter();
    fx_flash = 256;
}

static void jump(int p)
{
    if (p < 0) {
        p = SONG_PARTS - 1;
    }
    if (p >= SONG_PARTS) {
        p = 0;
    }
    if (cur >= 0 && parts[cur]->leave != NULL) {
        parts[cur]->leave();
    }
    cur = -1;
    pal_black();
    sync_start(p);              /* disk: borders are closed by now */
    enter_part(p);
    last_flip = vbl_free;       /* the load is not a frame's cost   */
}

static uint32_t load_vbls;

static void show(void)
{
    uint32_t last;
    int running = 1;

    sync_start(0);
    enter_part(0);
    last = last_flip = vbl_free;
    while (running) {
        STDL_Event ev;
        uint32_t f, now;
        int p, entered;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT) {
                running = 0;
            } else if (ev.type == STDL_KEYDOWN) {
                int sym = ev.key.keysym.sym;
                if (sym == STDLK_ESCAPE) {
                    running = 0;
                } else if (sym == STDLK_SPACE) {
                    sync_pause(!sync_paused());
                } else if (sym >= STDLK_1 && sym <= STDLK_7) {
                    jump(sym - STDLK_1);
                } else if (sym == STDLK_RIGHT) {
                    jump(cur + 1);
                } else if (sym == STDLK_LEFT) {
                    jump(cur - 1);
                }
            }
        }
        f = sync_frame();
        p = part_of(f);
        entered = p != cur;
        if (entered) {
            enter_part(p);
            f = sync_frame();
            last_flip = vbl_free;
        }
        parts[cur]->frame(f);
        pace_update(entered);
        demo_flip();

        now = vbl_free;
        if (!sync_paused()) {
            int d = (int)(now - last);
            stat_frames[cur]++;
            stat_vbls[cur] += (uint32_t)d;
            stat_hist[cur][d < 1 ? 0 : d > 4 ? 3 : d - 1]++;
            fx_flash -= 20 * d;
            if (fx_flash < 0) {
                fx_flash = 0;
            }
        }
        last = now;
    }
    if (cur >= 0 && parts[cur]->leave != NULL) {
        parts[cur]->leave();
    }
}

int main(int argc, char *argv[])
{
    int i;

    (void)argc; (void)argv;
    fprintf(stderr, "MEGADEMO start\r\n");
    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_TIMER | STDL_INIT_JOYSTICK) < 0) {
        return 1;
    }
    STDL_EnableMouse(0);
    scr = STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF);
    if (scr == NULL) {
        STDL_Quit();
        return 1;
    }
    STDL_SetRefresh(50);
    mach = STDL_GetMachineInfo();
    atexit(cleanup);
    tables_init();
    colour_init();
    fast_init();
    text_init();
    if (core_init() < 0) {
        STDL_Quit();
        return 1;
    }
    load_vbls = vbl_free;
    loader();
    load_vbls = vbl_free - load_vbls;
    if (mem_fail) {
        raster_off();
        core_quit();
        STDL_Quit();
        fprintf(stderr, "STDL MEGADEMO needs 1MB of memory.\r\n");
        return 1;
    }
    if (title()) {
        show();
    }
    raster_off();
    sync_stop();
    core_quit();
    STDL_Quit();

    fprintf(stderr, "MEGADEMO loader %lu.%02lus\r\n",
            (unsigned long)(load_vbls / 50), (unsigned long)(load_vbls % 50 * 2));
    fprintf(stderr, "steps (vbls):");
    for (i = 1; i < 8; i++) {
        fprintf(stderr, " %lu", (unsigned long)(load_t[i] - load_t[i - 1]));
    }
    fprintf(stderr, "\r\n");
    fprintf(stderr, "MEGADEMO stats (frames/vbls, fps):\r\n");
    for (i = 0; i < SONG_PARTS; i++) {
        if (stat_vbls[i] != 0) {
            fprintf(stderr, " %-7s %5lu/%5lu %3lu.%lu fps  [%lu %lu %lu %lu]\r\n",
                    parts[i]->name,
                    (unsigned long)stat_frames[i], (unsigned long)stat_vbls[i],
                    (unsigned long)(stat_frames[i] * 50 / stat_vbls[i]),
                    (unsigned long)(stat_frames[i] * 500 / stat_vbls[i] % 10),
                    (unsigned long)stat_hist[i][0], (unsigned long)stat_hist[i][1],
                    (unsigned long)stat_hist[i][2], (unsigned long)stat_hist[i][3]);
        }
    }
    return 0;
}
