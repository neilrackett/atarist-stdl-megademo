#!/usr/bin/env python3
# STDL Megademo - rank functions in a saved Hatari CPU profile
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   profsum.py PROFILE [TOP] [FRAMES]
#
# Reads the call-graph lines ("0xADDR: caller = calls type
# incl-calls/instr/cycles excl-calls/instr/cycles, ..., _name") and
# prints calls, inclusive and exclusive cycles, and exclusive cycles
# per frame when FRAMES is given. A frame at 8MHz and 50Hz is 160000.
import re
import sys

tot = {}
for line in open(sys.argv[1]):
    if not line.startswith('0x') or ': ' not in line:
        continue
    name = line.rstrip().split(', ')[-1]
    if not name.startswith('_') and not name[0].isalpha():
        continue
    inc = exc = calls = 0
    for m in re.finditer(r'= (\d+) (\w)(?: (\d+)/(\d+)/(\d+) (\d+)/(\d+)/(\d+))?', line):
        calls += int(m.group(1))
        if m.group(5):
            inc += int(m.group(5))
            exc += int(m.group(8))
    t = tot.setdefault(name, [0, 0, 0])
    t[0] += calls
    t[1] += inc
    t[2] += exc
top = int(sys.argv[2]) if len(sys.argv) > 2 else 20
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1
print('%-26s %7s %11s %11s %9s' % ('function', 'calls', 'inclusive', 'exclusive', 'excl/frm'))
for n, (c, i, e) in sorted(tot.items(), key=lambda kv: -kv[1][2])[:top]:
    print('%-26s %7d %11d %11d %9d' % (n, c, i, e, e // frames))
