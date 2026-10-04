#!/usr/bin/env python3
# STDL Megademo - the soundtrack
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A tiny YM2149 tracker in Python. It composes the song (the same
# arrangement as the three.js cracktro that inspired the demo, cut
# down to three square-wave voices), renders it to one register frame
# per 50Hz VBL and writes:
#
#   dist/MEGA0.STM .. MEGA6.STM   STDL_Music streams, one per part:
#                                 MEGAn starts at part n and wraps
#                                 round to it, so jumping to a part
#                                 is loading another file (STDL_Music
#                                 has no seek)
#   src/gen/song.c, song.h        the row-by-row sync table every
#                                 effect reads, so a flash lands on
#                                 the frame its drum does
#
# Voices: A = bass (the YM envelope as a sawtooth "buzzer"), acid
# and pads; B = lead, arpeggios and chip chords (the 50Hz Amiga
# arpeggio trick: one voice cycling a chord every frame); C = drums
# (tone sweeps and noise) and risers.
#
# 125 BPM so a row is exactly six VBLs - the visuals count frames.

import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(HERE, '..', 'dist')
GEN = os.path.join(HERE, '..', 'src', 'gen')

FPR = 6                 # frames per row (16th note)
RPB = 16                # rows per bar
FPB = FPR * RPB         # 96 frames, 1.92s a bar
BARS = 56
ROWS = BARS * RPB
FRAMES = ROWS * FPR     # 5376 frames, 107.5 seconds
PARTS = [0, 8, 16, 24, 32, 40, 48]   # intro plasma boing tunnel glenz meta outro

YM_CLOCK = 2000000


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12.0)


def tone_period(f):
    return max(1, min(4095, int(round(YM_CLOCK / 16.0 / f))))


def env_period(f):
    # sawtooth envelope: one cycle every 256 * EP master clocks
    return max(1, min(65535, int(round(YM_CLOCK / 256.0 / f))))


# ---------------------------------------------------------------- song
CHORDS = {
    'Am': dict(root=33, pad=[57, 60, 64], arp=[69, 72, 76, 81]),
    'F': dict(root=29, pad=[57, 60, 65], arp=[65, 69, 72, 77]),
    'C': dict(root=36, pad=[55, 60, 64], arp=[67, 72, 76, 79]),
    'G': dict(root=31, pad=[55, 59, 62], arp=[67, 71, 74, 79]),
    'E': dict(root=28, pad=[56, 59, 64], arp=[68, 71, 76, 80]),
}
P1 = ['Am', 'F', 'C', 'G', 'Am', 'F', 'C', 'G']
PROG = (P1 + P1 + P1 +
        ['F', 'G', 'Am', 'Am', 'F', 'G', 'E', 'E'] +
        P1 +
        ['Am', 'F', 'C', 'G', 'Am', 'F', 'C', 'E'] +
        P1)

MELODY = [
    [(0, 76, 3), (3, 76, 3), (6, 81, 2), (8, 79, 3), (11, 76, 3), (14, 74, 2)],
    [(0, 72, 3), (3, 72, 3), (6, 77, 2), (8, 76, 3), (11, 72, 3), (14, 69, 2)],
    [(0, 72, 3), (3, 76, 3), (6, 79, 2), (8, 84, 4), (12, 83, 2), (14, 79, 2)],
    [(0, 79, 3), (3, 74, 3), (6, 71, 2), (8, 74, 6), (14, 76, 2)],
    [(0, 76, 3), (3, 76, 3), (6, 81, 2), (8, 79, 3), (11, 76, 3), (14, 74, 2)],
    [(0, 72, 3), (3, 72, 3), (6, 77, 2), (8, 81, 3), (11, 79, 3), (14, 77, 2)],
    [(0, 76, 3), (3, 79, 3), (6, 84, 2), (8, 83, 3), (11, 79, 3), (14, 76, 2)],
    [(0, 79, 4), (4, 74, 4), (8, 71, 2), (10, 74, 2), (12, 79, 4)],
]
MELODY_E = [(0, 80, 4), (4, 76, 4), (8, 71, 2), (10, 76, 2), (12, 80, 4)]
ACID = [(0, 0, 1), (1, 12, 0), (2, 0, 0), (3, 0, 1), (4, 15, 0), (5, 0, 0), (6, 12, 1), (7, 0, 0),
        (8, 0, 0), (9, 7, 1), (10, 0, 0), (11, 12, 0), (12, 0, 1), (13, 3, 0), (14, 12, 0), (15, 10, 1)]
ARP_ORDER = [0, 1, 2, 3, 1, 2, 3, 0, 2, 3, 0, 1, 3, 2, 1, 0]


def within(bar, *ranges):
    return any(a <= bar <= z for a, z in ranges)


class Ev:
    __slots__ = ('row', 'inst', 'note', 'rows', 'vel', 'notes', 'acc')

    def __init__(self, row, inst, note=0, rows=1, vel=1.0, notes=None, acc=0):
        self.row, self.inst, self.note, self.rows = row, inst, note, rows
        self.vel, self.notes, self.acc = vel, notes, acc


def compose():
    ev = []

    def add(inst, bar, step, note=0, rows=1, vel=1.0, **kw):
        ev.append(Ev(bar * RPB + step, inst, note, rows, vel, **kw))

    for bar in range(BARS):
        name = PROG[bar]
        ch = CHORDS[name]
        phrase = bar % 8

        # ---- A: pads where there is no bass
        if within(bar, (0, 3)):
            add('pad', bar, 0, notes=ch['pad'], rows=16, vel=0.8 + 0.05 * bar)
        if within(bar, (4, 7)):
            notes = [n + 12 for n in ch['pad']]
            for s in (0, 3, 6, 8, 11, 14):
                add('stab', bar, s, notes=notes, rows=2, vel=0.9)
        if within(bar, (24, 31)):
            add('pad', bar, 0, notes=ch['pad'], rows=16, vel=1.0)
        if within(bar, (52, 55)):
            add('pad', bar, 0, notes=ch['pad'], rows=16, vel=1.0 - (bar - 52) * 0.18)

        # ---- A: offbeat rave bass on the envelope
        if within(bar, (8, 23), (32, 39), (48, 51)):
            r = ch['root'] + 12
            if bar < 12:
                pat = [(2, 0, 2), (6, 0, 2), (10, 0, 2), (14, 0, 2)]
            else:
                pat = [(2, 0, 2), (6, 0, 2), (7, 12, 1), (10, 0, 2), (14, 0, 1), (15, 12, 1)]
            for s, o, l in pat:
                if bar in (23,) and s >= 8:
                    continue
                add('bass', bar, s, r + o, l, 0.95 if not o else 0.75)

        # ---- A: acid in the climax
        if within(bar, (40, 47)):
            for s, o, a in ACID:
                if bar == 47 and s >= 8:
                    break
                add('acid', bar, s, ch['root'] + 12 + o, 1, 0.9, acc=a)

        # ---- B: lead
        if within(bar, (16, 23), (32, 47)):
            mel = MELODY_E if name == 'E' else MELODY[phrase]
            octave = 12 if within(bar, (44, 47)) else 0
            for s, n, l in mel:
                add('lead', bar, s, n + octave, l, 0.9)
        if within(bar, (48, 55)):
            mel = MELODY_E if name == 'E' else MELODY[phrase]
            fade = 1.0 if bar < 52 else 1.0 - (bar - 51) * 0.17
            for s, n, l in mel:
                add('chip', bar, s, n, l, 0.85 * fade)

        # ---- B: 16th arps where there is no lead
        if within(bar, (0, 15)):
            vel = min(1.0, 0.65 + bar * 0.12)
            for s in range(16):
                add('arp', bar, s, ch['arp'][ARP_ORDER[s] % 4], 1, vel)

        # ---- B: chip chords and slow chip melody in the breakdown
        if within(bar, (24, 25)):
            notes = [n + 12 for n in ch['pad']]
            for s in range(0, 16, 2):
                add('stab', bar, s, notes=notes, rows=2, vel=0.8)
        if within(bar, (26, 31)):
            mel = MELODY[(bar - 24) % 8]
            for s, n, l in mel:
                if s % 8 == 0:
                    add('chip', bar, s, n + 12, max(l, 6), 0.8)

        # ---- C: drums
        full_kick = within(bar, (8, 22), (28, 30), (32, 46), (48, 51))
        claps = within(bar, (12, 22), (32, 46), (48, 51)) and bar != 15
        busy_hats = within(bar, (8, 23), (28, 31), (32, 47), (48, 51))
        open_hats = within(bar, (16, 23), (32, 47), (48, 51))
        drums = {}
        if full_kick:
            for b in range(4):
                drums[b * 4] = ('kick', 1.0)
        if bar in (23, 47):
            drums[0] = ('kick', 1.0)
            drums[4] = ('kick', 1.0)
        if bar == 4:
            drums[0] = ('boom', 1.0)
        if claps:
            for s in (4, 12):
                drums[s] = ('kickclap' if full_kick else 'clap', 0.9)
        for s in range(16):
            if s in drums:
                continue
            if open_hats and s % 4 == 2:
                drums[s] = ('ohat', 0.7)
            elif busy_hats:
                drums[s] = ('hat', [0.8, 0.45, 0.9, 0.5][s % 4])
            elif within(bar, (4, 7), (52, 53)) and s % 2 == 0:
                drums[s] = ('hat', 0.7 if s % 4 == 2 else 0.45)
        # snare rolls into the drops
        if bar in (7, 23, 47):
            for s in range(8, 16):
                drums[s] = ('snare', 0.55 + (s - 8) * 0.06)
        if bar == 11:
            for s in range(12, 16):
                drums[s] = ('snare', 0.7 + (s - 12) * 0.1)
        if bar in (15, 39):
            for s in range(12, 16):
                drums[s] = ('snare', 0.7 + (s - 12) * 0.1)
        if bar == 30:
            for s in range(0, 16, 2):
                drums[s] = ('snare', 0.5 + s * 0.02)
        if bar == 31:
            for s in range(16):
                drums[s] = ('snare', 0.6 + s * 0.025)
        # crashes and booms on the drops (bar 12: the borders go)
        if bar in (8, 12, 16, 24, 32, 40, 48):
            drums[0] = ('boom' if bar in (12, 24, 32, 40) else 'crash', 1.0)
        for s, (inst, vel) in drums.items():
            add(inst, bar, s, 0, 1, vel)
        if bar == 6:
            add('riser', bar, 0, 0, 32, 1.0)
        if bar in (10, 15, 23, 39, 47):
            add('riser', bar, 0, 0, 16, 0.8)
        if bar == 29:
            add('riser', bar, 0, 0, 48, 1.0)

    ev.sort(key=lambda e: e.row)
    return ev


VOICE = {'pad': 'A', 'stab': 'A', 'bass': 'A', 'acid': 'A',
         'lead': 'B', 'chip': 'B', 'arp': 'B',
         'kick': 'C', 'kickclap': 'C', 'clap': 'C', 'snare': 'C', 'hat': 'C',
         'ohat': 'C', 'crash': 'C', 'boom': 'C', 'riser': 'C'}
# Stabs in the breakdown sit on B (the pad holds A)
DRUMS = {'kick', 'kickclap', 'clap', 'snare', 'hat', 'ohat', 'crash', 'boom'}


def vscale(v, vel):
    if v <= 0:
        return 0
    return max(0, min(15, int(round(v + 6 * math.log2(max(vel, 0.05))))))


# --------------------------------------------------------------- render
def render_voice(e, t, prev_note):
    """State of the voice playing event e, t frames after its start.
    Returns None when the event has finished."""
    dur = e.rows * FPR
    inst = e.inst
    s = {'tone': True, 'noise': False, 'np': 0, 'vol': 0,
         'per': 1, 'env': None, 'trig': False}

    if inst == 'bass':
        if t >= dur - 1:
            return None
        f = hz(e.note)
        s['per'] = tone_period(f) + 1          # a hair flat: it beats
        s['vol'] = 16                          # envelope-driven
        s['env'] = (env_period(f), 0x08)       # repeating falling saw
        s['trig'] = (t == 0)
        return s
    if inst == 'acid':
        if t >= dur:
            return None
        f = hz(e.note)
        if e.acc:
            s['per'] = tone_period(f)
            s['vol'] = 16
            s['env'] = (env_period(f), 0x0A)   # triangle: rounder
            s['trig'] = (t == 0)
        else:
            if prev_note and t < 2:            # slide from the last note
                a, b = tone_period(hz(prev_note)), tone_period(f)
                s['per'] = int(a + (b - a) * (t + 1) / 3)
            else:
                s['per'] = tone_period(f)
            s['vol'] = vscale(max(8, 13 - t), e.vel)
        return s
    if inst in ('pad', 'stab'):
        if t >= dur:
            return None
        n = e.notes[t % len(e.notes)]          # the 50Hz chip chord
        s['per'] = tone_period(hz(n))
        if inst == 'pad':
            att = min(1.0, (t + 1) / 20.0)
            rel = min(1.0, (dur - t) / 8.0)
            s['vol'] = vscale(int(round(10 * att * rel)), e.vel)
        else:
            s['vol'] = vscale(max(7, 13 - t // 2), e.vel)
        return s
    if inst == 'arp':
        if t >= dur:
            return None
        s['per'] = tone_period(hz(e.note))
        s['vol'] = vscale(max(5, 12 - t), e.vel)
        return s
    if inst in ('lead', 'chip'):
        if t >= dur:
            return None
        n = e.note
        if inst == 'chip' and t < 3:
            n += 12 if t & 1 else 0            # chirpy octave attack
        f = hz(n)
        if t >= 9:                             # delayed vibrato
            f *= 1 + 0.007 * math.sin((t - 9) * 2 * math.pi / 7)
        s['per'] = tone_period(f)
        v = [14, 13, 13, 12][min(t, 3)]
        if t >= dur - 2:
            v -= 3
        s['vol'] = vscale(v, e.vel)
        return s

    # ---- drums / noise on C
    if inst == 'kick':
        sweep = [190, 120, 88, 70, 58, 50, 45, 42]
        vols = [15, 15, 14, 12, 10, 8, 5, 2]
        if t >= len(sweep):
            return None
        s['per'] = tone_period(sweep[t])
        s['vol'] = vscale(vols[t], e.vel)
        if t == 0:
            s['noise'], s['np'] = True, 3
        return s
    if inst == 'kickclap':
        sweep = [190, 120, 88, 70, 58]
        vols = [15, 14, 13, 12, 11, 9, 8, 6, 4, 2]
        if t >= len(vols):
            return None
        if t < len(sweep):
            s['per'] = tone_period(sweep[t])
        else:
            s['tone'] = False
        s['noise'], s['np'] = True, 9
        s['vol'] = vscale(vols[t], e.vel)
        return s
    if inst == 'clap':
        vols = [14, 8, 13, 7, 12, 10, 8, 6, 4, 2]
        if t >= len(vols):
            return None
        s['tone'] = False
        s['noise'], s['np'] = True, 10
        s['vol'] = vscale(vols[t], e.vel)
        return s
    if inst == 'snare':
        sweep = [240, 205, 185, 170]
        vols = [15, 13, 12, 10, 8, 6, 4, 2]
        if t >= len(vols):
            return None
        if t < len(sweep):
            s['per'] = tone_period(sweep[t])
        else:
            s['tone'] = False
        s['noise'], s['np'] = True, 5
        s['vol'] = vscale(vols[t], e.vel)
        return s
    if inst == 'hat':
        vols = [11, 7, 3]
        if t >= len(vols):
            return None
        s['tone'] = False
        s['noise'], s['np'] = True, 1
        s['vol'] = vscale(vols[t], e.vel)
        return s
    if inst == 'ohat':
        if t >= 12:
            return None
        s['tone'] = False
        s['noise'], s['np'] = True, 1
        s['vol'] = vscale(12 - t, e.vel)
        return s
    if inst == 'crash':
        if t >= 48:
            return None
        s['tone'] = False
        s['noise'], s['np'] = True, 2 + t // 16
        s['vol'] = vscale(15 - t // 3, e.vel)
        return s
    if inst == 'boom':
        if t >= 40:
            return None
        f = 110 * (0.25 ** (t / 40.0))
        s['per'] = tone_period(max(31, f))
        s['vol'] = vscale(15 - t * 15 // 40, e.vel)
        if t < 6:
            s['noise'], s['np'] = True, 4 + t
        return s
    if inst == 'riser':
        if t >= dur:
            return None
        p = t / float(dur)
        s['tone'] = False
        s['noise'], s['np'] = True, int(round(31 - 30 * p))
        s['vol'] = vscale(int(round(4 + 10 * p)), e.vel)
        return s
    raise ValueError(inst)


def voice_of(e):
    v = VOICE[e.inst]
    if e.inst == 'stab' and PROG and e.row >= 24 * RPB and e.row < 26 * RPB:
        return 'B'
    return v


def render(events):
    by_voice = {'A': [], 'B': [], 'C': []}
    for e in events:
        by_voice[voice_of(e)].append(e)

    # per voice and row: index of the last event started by that row
    last = {}
    for v, evs in by_voice.items():
        idx, i = [], -1
        for row in range(ROWS):
            while i + 1 < len(evs) and evs[i + 1].row <= row:
                i += 1
            idx.append(i)
        last[v] = idx

    frames = []
    for f in range(FRAMES):
        row = f // FPR
        regs = [0] * 14
        mixer = 0x3F
        trig = False
        noise_p = None
        env = None
        for ci, v in enumerate('ABC'):
            evs = by_voice[v]
            state = None
            if v == 'C':
                # newest drum still sounding wins; a riser fills gaps
                best = None
                for k in range(last[v][row], -1, -1):
                    e = evs[k]
                    if row - e.row > 64:
                        break
                    st = render_voice(e, f - e.row * FPR, None)
                    if st is None:
                        continue
                    if e.inst == 'riser':
                        if best is None:
                            best = st
                        continue
                    best = st
                    break
                state = best
            else:
                prev_note = None
                i = last[v][row]
                if i >= 0:
                    e = evs[i]
                    if i > 0 and evs[i - 1].inst == 'acid' and evs[i - 1].row == e.row - 1:
                        prev_note = evs[i - 1].note
                    state = render_voice(e, f - e.row * FPR, prev_note)
            if state is None or state['vol'] == 0:
                continue
            per = state['per']
            regs[2 * ci] = per & 0xFF
            regs[2 * ci + 1] = (per >> 8) & 0x0F
            if state['tone']:
                mixer &= ~(1 << ci)
            if state['noise']:
                mixer &= ~(8 << ci)
                noise_p = state['np']
            regs[8 + ci] = state['vol']
            if state['env']:
                env = state['env']
                trig = trig or state['trig']
        regs[6] = (noise_p or 0) & 0x1F
        regs[7] = mixer
        if env:
            regs[11] = env[0] & 0xFF
            regs[12] = (env[0] >> 8) & 0xFF
            regs[13] = env[1]
        frames.append((regs, trig))
    return frames


def carry_env(frames):
    """Envelope registers keep their last value when nobody uses
    them, so the delta stream does not rewrite them for nothing."""
    last = [0, 0, 0x08]
    out = []
    for regs, trig in frames:
        regs = regs[:]
        if regs[8] != 16 and regs[9] != 16 and regs[10] != 16:
            regs[11], regs[12], regs[13] = last
        else:
            last = [regs[11], regs[12], regs[13]]
        out.append((regs, trig))
    return out


def write_stm(path, frames):
    body = bytearray()
    prev = None
    for i, (regs, trig) in enumerate(frames):
        mask = 0
        vals = []
        for r in range(14):
            if r == 13:
                if prev is None or trig:
                    mask |= 1 << 13
                    vals.append(regs[13])
                continue
            if prev is None or regs[r] != prev[r]:
                mask |= 1 << r
                vals.append(regs[r])
        body += struct.pack('>H', mask) + bytes(vals)
        prev = regs
    head = b'STM1' + struct.pack('>HHHH', 50, len(frames), 0, 0)
    with open(path, 'wb') as fh:
        fh.write(head + body)
    return len(head) + len(body)


FLAG_BITS = ['kick', 'snare', 'clap', 'hat', 'ohat', 'crash', 'boom', 'riser',
             'bass', 'lead', 'arp', 'chip', 'acid', 'pad', 'stab']


def write_sync(events):
    flags = [0] * ROWS
    lead = [0] * ROWS
    bass = [0] * ROWS
    for e in events:
        names = [e.inst]
        if e.inst == 'kickclap':
            names = ['kick', 'clap']
        for n in names:
            flags[e.row] |= 1 << FLAG_BITS.index(n)
        if e.inst in ('lead', 'chip'):
            for r in range(e.row, min(ROWS, e.row + e.rows)):
                lead[r] = e.note
        if e.inst in ('bass', 'acid'):
            for r in range(e.row, min(ROWS, e.row + e.rows)):
                bass[r] = e.note
        if e.inst == 'riser':
            for r in range(e.row, min(ROWS, e.row + e.rows)):
                flags[r] |= 1 << FLAG_BITS.index('riser')
    h = ['/* generated by tools/mksong.py - do not edit\n * Copyright (C) 2026 Neil Rackett\n * SPDX-License-Identifier: GPL-3.0-or-later */',
         '#ifndef GEN_SONG_H', '#define GEN_SONG_H', '#include <stdint.h>',
         '#define SONG_FPR %d' % FPR, '#define SONG_FPB %d' % FPB,
         '#define SONG_BARS %d' % BARS, '#define SONG_ROWS %d' % ROWS,
         '#define SONG_FRAMES %d' % FRAMES,
         '#define SONG_PARTS %d' % len(PARTS)]
    for i, n in enumerate(FLAG_BITS):
        h.append('#define S_%s 0x%04x' % (n.upper(), 1 << i))
    h += ['extern const uint8_t song_part_bar[SONG_PARTS];',
          'extern const uint16_t song_flags[SONG_ROWS];',
          'extern const uint8_t song_lead[SONG_ROWS];',
          'extern const uint8_t song_bass[SONG_ROWS];',
          '#endif', '']
    c = ['/* generated by tools/mksong.py - do not edit\n * Copyright (C) 2026 Neil Rackett\n * SPDX-License-Identifier: GPL-3.0-or-later */', '#include "song.h"',
         'const uint8_t song_part_bar[SONG_PARTS] = {%s};' % ','.join(map(str, PARTS))]

    def arr(typ, name, data):
        s = 'const %s %s[SONG_ROWS] = {\n' % (typ, name)
        for i in range(0, len(data), 16):
            s += '    ' + ','.join(str(v) for v in data[i:i + 16]) + ',\n'
        return s + '};'
    c += [arr('uint16_t', 'song_flags', flags), arr('uint8_t', 'song_lead', lead),
          arr('uint8_t', 'song_bass', bass), '']
    with open(os.path.join(GEN, 'song.h'), 'w') as fh:
        fh.write('\n'.join(h))
    with open(os.path.join(GEN, 'song.c'), 'w') as fh:
        fh.write('\n'.join(c))


def main():
    os.makedirs(DIST, exist_ok=True)
    os.makedirs(GEN, exist_ok=True)
    events = compose()
    frames = carry_env(render(events))
    sizes = []
    for i, bar in enumerate(PARTS):
        start = bar * FPB
        rot = frames[start:] + frames[:start]
        sizes.append(write_stm(os.path.join(DIST, 'MEGA%d.STM' % i), rot))
    write_sync(events)
    print('song: %d events, %d frames (%.1fs), STM %s bytes' %
          (len(events), FRAMES, FRAMES / 50.0, '/'.join(map(str, sizes))))
    if os.environ.get('WAV'):
        write_wav(os.environ['WAV'], frames)


# ------------------------------------------------- optional host preview
def write_wav(path, frames, rate=44100):
    """Crude YM render for listening on the host (WAV=out.wav)."""
    import wave
    out = bytearray()
    ph = [0.0, 0.0, 0.0]
    envph = 0.0
    lfsr = 1
    nacc = 0.0
    nbit = 1
    voltab = [0] + [10 ** ((v - 15) * 1.5 / 20) for v in range(1, 16)]
    spf = rate // 50
    regs = [0] * 14
    for fr, trig in frames:
        regs = fr
        if trig:
            envph = 0.0
        for _ in range(spf):
            mix = 0.0
            npd = max(1, regs[6]) * 16.0 / YM_CLOCK * 8
            nacc += 1.0 / rate
            while nacc >= npd / 8:
                nacc -= npd / 8
                bit = ((lfsr ^ (lfsr >> 3)) & 1)
                lfsr = (lfsr >> 1) | (bit << 16)
                nbit = lfsr & 1
            ep = max(1, regs[11] | (regs[12] << 8))
            envph += YM_CLOCK / 256.0 / ep / rate
            for c in range(3):
                per = max(1, regs[2 * c] | (regs[2 * c + 1] << 8))
                ph[c] = (ph[c] + YM_CLOCK / 16.0 / per / rate) % 1.0
                t_on = not (regs[7] >> c) & 1
                n_on = not (regs[7] >> (c + 3)) & 1
                sq = (ph[c] < 0.5) or not t_on
                nz = nbit or not n_on
                v = regs[8 + c]
                if v & 0x10:
                    shape = regs[13]
                    x = envph % 1.0
                    if shape in (0x08,):
                        a = 1 - x
                    elif shape in (0x0A, 0x0E):
                        a = 1 - abs(2 * x - 1) if shape == 0x0A else abs(2 * x - 1)
                    else:
                        a = x
                    amp = a
                else:
                    amp = voltab[v & 15]
                if sq and nz:
                    mix += amp
            out += struct.pack('<h', int(max(-1, min(1, mix / 3 * 2 - 0.3)) * 20000))
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(out))


if __name__ == '__main__':
    main()
