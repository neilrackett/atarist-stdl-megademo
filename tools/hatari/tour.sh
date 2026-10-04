#!/bin/bash
# STDL Megademo - visit every part (keys 1-7), two shots each, and
# print the frame statistics the demo writes on exit
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/hatari/tour.sh [NAME] [st|ste|megaste]
#
# Real speed (FF=off): screenshots and the fps figures are honest.
# Shots land in tmp/out/shots_NAME/, a contact sheet in
# tmp/out/NAME_sheet.png (needs Pillow).
. "$(dirname "$0")/common.sh"
NAME=${1:-tour}; M=${2:-st}
S="waitfor title;sleep 1;$(k 0x39);sleep 3;shot;sleep 6;shot"
S="$S;$(k 0x03);sleep 3;shot;sleep 6;shot;sleep 2;shot"
for key in 0x04 0x05; do S="$S;$(k $key);sleep 3;shot;sleep 4;shot"; done
S="$S;sleep 5;shot"
for key in 0x06 0x07 0x08; do S="$S;$(k $key);sleep 3;shot;sleep 4;shot"; done
S="$S;$(k 0x01);sleep 4"
MACHINE=$M FF=off tmp/run.sh $NAME dist/MEGADEMO.TOS 6 "$S" 2>&1 | tr -d '\r' \
    | sed -n '/console output/,/screenshots/p' | grep -v "^===\|^$"
python3 tools/hatari/sheet.py tmp/out/shots_$NAME tmp/out/${NAME}_sheet.png 4 2>/dev/null
