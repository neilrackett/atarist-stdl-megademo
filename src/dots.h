/*
 * STDL Megademo - dot erasing and song hits
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DOTS_H
#define DOTS_H

#include <stdint.h>

/* clear the groups at the saved offsets: all planes, or 0-2 only */
void dots_erase(uint8_t *base, const uint16_t *save, int n);
void dots_erase3(uint8_t *base, const uint16_t *save, int n);

/* rows between two song frames carrying `mask` - a hit since last
 * frame, robust to frames being skipped */
int sync_hit(uint32_t f, uint32_t lastf, uint16_t mask);

#endif
