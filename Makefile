# STDL Megademo
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-compiles with m68k-atari-mint-gcc via atarist-toolkit-docker:
#   STCMD_NO_TTY=1 stcmd make
#
# dist/MEGADEMO.TOS is the whole demo, the music linked in: copy it
# to a floppy, a hard disk or a GEMDOS drive and run it.
#
# The generated sources (src/gen/: the art, the song and its sync
# table) come from the Python tools; they are rebuilt when a tool
# changes, inside the container or out of it (pure Python, no
# packages).

CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
STRIP   = $(CROSS)strip
PYTHON ?= python3

STDL     = stdl
STDL_LIB = $(STDL)/libstdl.a

CFLAGS  = -O2 -fomit-frame-pointer -std=gnu99 -Wall -Wextra \
          -Wno-unused-parameter -MMD -MP -I$(STDL)/include -Isrc

SRCS = src/main.c src/core.c src/raster.c src/text.c src/dots.c \
       src/fast.c src/bobs.c src/intro.c src/plasma.c src/boing.c src/tunnel.c src/glenz.c \
       src/meta.c src/outro.c src/gen/assets.c src/gen/song.c src/gen/stm.c
OBJS = $(SRCS:src/%.c=build/%.o)

TARGET = dist/MEGADEMO.TOS

# Pinned: a rule above `all` would otherwise become the default goal
.DEFAULT_GOAL := all

all: $(TARGET)

# STDL's own sources, so a library edit or a submodule bump rebuilds
# the archive (xpad comes through STDL's own submodule)
STDL_SRCS = $(wildcard $(STDL)/src/*.c) $(wildcard $(STDL)/include/stdl/*.h) \
            $(wildcard $(STDL)/lib/xpad/src/*.c)

$(STDL_LIB): $(STDL_SRCS)
	$(MAKE) -C $(STDL) libstdl.a

$(TARGET): $(OBJS) $(STDL_LIB) | dist
	$(CC) $(CFLAGS) -o build/megademo.elf $(OBJS) $(STDL_LIB)
	cp build/megademo.elf $@
	$(STRIP) $@

build/%.o: src/%.c | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# generated sources: every object may include them
$(OBJS): src/gen/assets.h src/gen/song.h src/gen/stm.h

src/gen/assets.c src/gen/assets.h: build/assets.stamp
build/assets.stamp: tools/mkassets.py | build
	$(PYTHON) tools/mkassets.py
	@touch $@

src/gen/song.c src/gen/song.h src/gen/stm.c src/gen/stm.h: build/song.stamp
build/song.stamp: tools/mksong.py | build
	$(PYTHON) tools/mksong.py
	@touch $@

build dist:
	mkdir -p $@

clean:
	rm -rf build $(TARGET)

-include $(OBJS:.o=.d)

.PHONY: all clean
