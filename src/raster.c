/*
 * STDL Megademo - Timer B rasters, through STDL
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Colour 0 (background, side borders) and optionally a second
 * register change every `step` displayed lines from a table filled
 * each frame - STDL_OpenRasters does the interrupt, the Timer C
 * prefix that keeps TOS's 200Hz tick from shaking the bars, the VBL
 * restart, and the clean-up on any exit, and refuses while a border
 * is open (the borders run on Timer B too). This file only keeps the
 * demo's own names for it and shows the table with the page flip.
 *
 * With a second register the table is two words an entry (colour 0,
 * then the other); with colour 0 alone it is one.
 */

#include "demo.h"

static int ntab = -1;
static uint8_t drawn;               /* raster_buf asked for since show */

int raster_on2(int s, int second)
{
    raster_off();
    ntab = STDL_OpenRasters(s < 1 ? 1 : s, 0, second ? second : -1);
    drawn = 0;
    return ntab;
}

int raster_on(int s)
{
    return raster_on2(s, 15);
}

void raster_off(void)
{
    if (ntab >= 0) {
        STDL_CloseRasters();
        ntab = -1;
    }
}

int raster_active(void)
{
    return ntab >= 0;
}

uint16_t *raster_buf(void)
{
    drawn = 1;
    return STDL_RasterTable();
}

int raster_n(void)
{
    return ntab;
}

/* hand the filled table over; from demo_flip, before the flip. Every
 * part rewrites every entry, so STDL need not copy the old table
 * forward (keep = 0) */
void raster_commit(void)
{
    if (ntab >= 0 && drawn) {
        STDL_ShowRasters(0);
        drawn = 0;
    }
}
