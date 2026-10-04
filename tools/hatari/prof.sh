#!/bin/bash
# STDL Megademo - Hatari CPU profile of one part
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/hatari/prof.sh NAME PART SECONDS [st|ste|megaste]
#
# Runs the unstripped build (symbols for the profiler), jumps to part
# PART (1-7), waits two seconds, profiles for SECONDS and prints the
# functions ranked by exclusive cycles a frame. The recipe, since two
# steps are not obvious: 'profile save' must come while profiling is
# still on ('profile off' frees the data first), and a call still
# running at the save is not in the call graph - profile a window the
# work finishes inside.
. "$(dirname "$0")/common.sh"
NAME=$1; PART=$2; SECS=${3:-4}; M=${4:-st}
cp build/megademo.elf dist/MEGADBG.TOS
key=$(printf '0x%02x' $((PART + 1)))
MACHINE=$M FF=off tmp/run.sh $NAME dist/MEGADBG.TOS 6 \
  "waitfor title;sleep 1;$(k 0x39);sleep 2;$(k $key);sleep 2;fifo hatari-debug symbols prg;sleep 0.5;fifo hatari-debug profile on;sleep $SECS;fifo hatari-debug profile save $PWD/tmp/out/$NAME.prof;sleep 1;$(k 0x01);sleep 3" \
  > /dev/null 2>&1
rm -f dist/MEGADBG.TOS
tr -d '\r' < tmp/out/$NAME.log | grep "fps"
FR=$(grep -E "^0x[0-9a-f]+: .*_frame$" tmp/out/$NAME.prof | head -1 | sed -E 's/.*= ([0-9]+) s.*/\1/')
python3 tools/hatari/profsum.py tmp/out/$NAME.prof ${TOP:-14} ${FR:-1}
