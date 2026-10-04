# STDL Megademo - shared set-up for the Hatari scripts
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drives STDL's own on-target driver (stdl/tests/hatari/run.sh) from a
# copy in tmp/, so its output lands in tmp/out/ rather than inside the
# submodule. TOS defaults to the EmuTOS image STDL's skill describes
# downloading; set TOS= for another (a real 1.04 is worth a run).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
mkdir -p tmp/out
cp -f stdl/tests/hatari/run.sh tmp/run.sh
TOS=${TOS:-$ROOT/tmp/tos/etos256uk.img}
if [ ! -f "$TOS" ]; then
    echo "no TOS image at $TOS - see 'Testing in Hatari' in" \
         "stdl/.claude/skills/stdl/SKILL.md, or set TOS=" >&2
    exit 2
fi
export TOS
# key press and release as one script fragment
k() { echo "keydown $1;sleep 0.2;keyup $1"; }
