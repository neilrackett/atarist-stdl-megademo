#!/usr/bin/env python3
# STDL Megademo - asset generator
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes src/gen/assets.c and src/gen/assets.h: the 8x8 demo font and
# its smoothed 16x16 and 32x32 enlargements (1bpp, for the bitplane
# scrollers and STDL_DrawText), the chrome logo and the Boing ball as
# chunky index bytes (turned planar at load time with
# STDL_SurfaceFromIndexed8), a shaded bob, and the sine table.
#
# Everything is drawn here from code: the font is designed in this
# file, nothing is sampled, so the whole demo is free to share.
# Pure Python, no packages, so it runs inside the build container too.

import math
import os
import sys

OUT = os.path.join(os.path.dirname(__file__), '..', 'src', 'gen')

# ---------------------------------------------------------------- font
# ASCII 32..90. Glyphs are 7 columns used of 8, rows 0-6 (row 7 is the
# descender/gap row). Two-pixel strokes: a chunky demo face that
# survives being scaled up.
GLYPHS = {
' ': [],
'!': ['..##....', '..##....', '..##....', '..##....', '..##....', '........', '..##....'],
'"': ['.##.##..', '.##.##..', '.#..#...'],
'#': ['.##.##..', '#######.', '.##.##..', '.##.##..', '#######.', '.##.##..'],
'$': ['..##....', '.#####..', '##......', '.####...', '....##..', '#####...', '..##....'],
'%': ['##...##.', '##..##..', '...##...', '..##....', '.##.....', '##..##..', '#...##..'],
'&': ['.###....', '##.##...', '.###....', '.###.##.', '##.###..', '##..##..', '.###.##.'],
"'": ['..##....', '..##....', '.##.....'],
'(': ['...##...', '..##....', '.##.....', '.##.....', '.##.....', '..##....', '...##...'],
')': ['.##.....', '..##....', '...##...', '...##...', '...##...', '..##....', '.##.....'],
'*': ['........', '.##.##..', '..###...', '#######.', '..###...', '.##.##..'],
'+': ['........', '..##....', '..##....', '######..', '..##....', '..##....'],
',': ['........', '........', '........', '........', '........', '..##....', '..##....', '.##.....'],
'-': ['........', '........', '........', '######..'],
'.': ['........', '........', '........', '........', '........', '..##....', '..##....'],
'/': ['.....##.', '....##..', '...##...', '..##....', '.##.....', '##......', '#.......'],
'0': ['.#####..', '##..###.', '##.####.', '####.##.', '###..##.', '##...##.', '.#####..'],
'1': ['..##....', '.###....', '..##....', '..##....', '..##....', '..##....', '.####...'],
'2': ['.#####..', '##...##.', '.....##.', '..####..', '.##.....', '##......', '#######.'],
'3': ['.#####..', '##...##.', '.....##.', '..####..', '.....##.', '##...##.', '.#####..'],
'4': ['...###..', '..####..', '.##.##..', '##..##..', '#######.', '....##..', '....##..'],
'5': ['#######.', '##......', '######..', '.....##.', '.....##.', '##...##.', '.#####..'],
'6': ['.#####..', '##......', '##......', '######..', '##...##.', '##...##.', '.#####..'],
'7': ['#######.', '.....##.', '....##..', '...##...', '..##....', '..##....', '..##....'],
'8': ['.#####..', '##...##.', '##...##.', '.#####..', '##...##.', '##...##.', '.#####..'],
'9': ['.#####..', '##...##.', '##...##.', '.######.', '.....##.', '.....##.', '.#####..'],
':': ['........', '..##....', '..##....', '........', '..##....', '..##....'],
';': ['........', '..##....', '..##....', '........', '..##....', '..##....', '.##.....'],
'<': ['....##..', '...##...', '..##....', '.##.....', '..##....', '...##...', '....##..'],
'=': ['........', '........', '######..', '........', '######..'],
'>': ['.##.....', '..##....', '...##...', '....##..', '...##...', '..##....', '.##.....'],
'?': ['.#####..', '##...##.', '....##..', '...##...', '...##...', '........', '...##...'],
'@': ['.#####..', '##...##.', '##.####.', '##.####.', '##.###..', '##......', '.#####..'],
'A': ['..###...', '.##.##..', '##...##.', '##...##.', '#######.', '##...##.', '##...##.'],
'B': ['######..', '##...##.', '##...##.', '######..', '##...##.', '##...##.', '######..'],
'C': ['.#####..', '##...##.', '##......', '##......', '##......', '##...##.', '.#####..'],
'D': ['#####...', '##..##..', '##...##.', '##...##.', '##...##.', '##..##..', '#####...'],
'E': ['#######.', '##......', '##......', '######..', '##......', '##......', '#######.'],
'F': ['#######.', '##......', '##......', '######..', '##......', '##......', '##......'],
'G': ['.#####..', '##...##.', '##......', '##.####.', '##...##.', '##...##.', '.######.'],
'H': ['##...##.', '##...##.', '##...##.', '#######.', '##...##.', '##...##.', '##...##.'],
'I': ['.####...', '..##....', '..##....', '..##....', '..##....', '..##....', '.####...'],
'J': ['....###.', '.....##.', '.....##.', '.....##.', '##...##.', '##...##.', '.#####..'],
'K': ['##...##.', '##..##..', '##.##...', '####....', '##.##...', '##..##..', '##...##.'],
'L': ['##......', '##......', '##......', '##......', '##......', '##......', '#######.'],
'M': ['##...##.', '###.###.', '#######.', '##.#.##.', '##...##.', '##...##.', '##...##.'],
'N': ['##...##.', '###..##.', '####.##.', '##.####.', '##..###.', '##...##.', '##...##.'],
'O': ['.#####..', '##...##.', '##...##.', '##...##.', '##...##.', '##...##.', '.#####..'],
'P': ['######..', '##...##.', '##...##.', '######..', '##......', '##......', '##......'],
'Q': ['.#####..', '##...##.', '##...##.', '##...##.', '##.#.##.', '##..##..', '.###.##.'],
'R': ['######..', '##...##.', '##...##.', '######..', '##.##...', '##..##..', '##...##.'],
'S': ['.#####..', '##...##.', '##......', '.#####..', '.....##.', '##...##.', '.#####..'],
'T': ['######..', '..##....', '..##....', '..##....', '..##....', '..##....', '..##....'],
'U': ['##...##.', '##...##.', '##...##.', '##...##.', '##...##.', '##...##.', '.#####..'],
'V': ['##...##.', '##...##.', '##...##.', '##...##.', '.##.##..', '..###...', '...#....'],
'W': ['##...##.', '##...##.', '##...##.', '##.#.##.', '#######.', '###.###.', '##...##.'],
'X': ['##...##.', '##...##.', '.##.##..', '..###...', '.##.##..', '##...##.', '##...##.'],
'Y': ['##..##..', '##..##..', '##..##..', '.####...', '..##....', '..##....', '..##....'],
'Z': ['#######.', '....##..', '...##...', '..##....', '.##.....', '##......', '#######.'],
}

FIRST, LAST = 32, 90


def glyph(ch):
    rows = GLYPHS.get(ch, [])
    g = [[0] * 8 for _ in range(8)]
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            g[y][x] = 1 if c == '#' else 0
    return g


def scale2x(img):
    """AdvMAME2x / EPX: doubles without the staircase."""
    h, w = len(img), len(img[0])
    out = [[0] * (w * 2) for _ in range(h * 2)]

    def p(x, y):
        if 0 <= x < w and 0 <= y < h:
            return img[y][x]
        return 0
    for y in range(h):
        for x in range(w):
            e = p(x, y)
            b, d, f, hh = p(x, y - 1), p(x - 1, y), p(x + 1, y), p(x, y + 1)
            e0 = d if (d == b and b != f and d != hh) else e
            e1 = f if (b == f and b != d and f != hh) else e
            e2 = d if (d == hh and d != b and hh != f) else e
            e3 = f if (hh == f and d != hh and b != f) else e
            out[2 * y][2 * x] = e0
            out[2 * y][2 * x + 1] = e1
            out[2 * y + 1][2 * x] = e2
            out[2 * y + 1][2 * x + 1] = e3
    return out


def scale3x(img):
    """AdvMAME3x."""
    h, w = len(img), len(img[0])
    out = [[0] * (w * 3) for _ in range(h * 3)]

    def p(x, y):
        if 0 <= x < w and 0 <= y < h:
            return img[y][x]
        return 0
    for y in range(h):
        for x in range(w):
            a, b, c = p(x - 1, y - 1), p(x, y - 1), p(x + 1, y - 1)
            d, e, f = p(x - 1, y), p(x, y), p(x + 1, y)
            g, hh, i = p(x - 1, y + 1), p(x, y + 1), p(x + 1, y + 1)
            if b != hh and d != f:
                e0 = d if d == b else e
                e1 = b if ((d == b and e != c) or (b == f and e != a)) else e
                e2 = f if b == f else e
                e3 = d if ((d == b and e != g) or (d == hh and e != a)) else e
                e4 = e
                e5 = f if ((b == f and e != i) or (hh == f and e != c)) else e
                e6 = d if d == hh else e
                e7 = hh if ((d == hh and e != i) or (hh == f and e != g)) else e
                e8 = f if hh == f else e
            else:
                e0 = e1 = e2 = e3 = e4 = e5 = e6 = e7 = e8 = e
            blk = [e0, e1, e2, e3, e4, e5, e6, e7, e8]
            for k in range(9):
                out[3 * y + k // 3][3 * x + k % 3] = blk[k]
    return out


def rows_to_int(img):
    w = len(img[0])
    res = []
    for r in img:
        v = 0
        for x in range(w):
            v = (v << 1) | r[x]
        res.append(v)
    return res


# ---------------------------------------------------------------- logo
def text_bitmap(text, scaler, cell):
    """Render text with a scaled glyph per char; cell = advance in px."""
    imgs = [scaler(glyph(c)) for c in text]
    h = len(imgs[0])
    w = cell * len(text)
    out = [[0] * w for _ in range(h)]
    for i, g in enumerate(imgs):
        for y in range(h):
            for x in range(len(g[0])):
                if g[y][x] and i * cell + x < w:
                    out[y][i * cell + x] = 1
    return out


def trim_cols(img):
    w = len(img[0])
    used = [x for x in range(w) if any(r[x] for r in img)]
    x0, x1 = used[0], used[-1]
    return [r[x0:x1 + 1] for r in img]


def logo():
    """'STDL' in 48px chrome over 'MEGADEMO' in 16px, one chunky
    image. Index 0 is transparent; 4 = outline, 5-8 sky gradient
    down to the white horizon, 9-11 ground gradient (see the intro
    palette)."""
    big = trim_cols(text_bitmap('STDL', lambda g: scale3x(scale2x(g)), 48))
    big = big[:42]          # 7 rows of 6 px; the descender row is empty
    small = trim_cols(text_bitmap('MEGADEMO', scale2x, 16))[:14]
    pad = 3
    bw, bh = len(big[0]), len(big)
    sw, sh = len(small[0]), len(small)
    gap = 6
    W = max(bw, sw) + 2 * pad
    W = (W + 15) & ~15
    H = pad + bh + gap + sh + pad + 2
    img = [[0] * W for _ in range(H)]
    bx = (W - bw) // 2
    sx = (W - sw) // 2
    by = pad
    sy = pad + bh + gap

    def chrome(y, h):
        t = y / (h - 1)
        if t < 0.52:
            # sky: dark blue at the top to near white at the horizon
            return 5 + min(3, int(t / 0.52 * 4))
        # ground: dark brown just under the horizon to gold
        return 9 + min(2, int((t - 0.52) / 0.48 * 3))

    solid = [[0] * W for _ in range(H)]
    for y in range(bh):
        for x in range(bw):
            if big[y][x]:
                img[by + y][bx + x] = chrome(y, bh)
                solid[by + y][bx + x] = 1
    for y in range(sh):
        for x in range(sw):
            if small[y][x]:
                img[sy + y][sx + x] = chrome(y, sh)
                solid[sy + y][sx + x] = 1
    # bevel: the top-left edge of every stroke catches the light
    for y in range(H):
        for x in range(W):
            if solid[y][x] and (y == 0 or not solid[y - 1][x] or
                                x == 0 or not solid[y][x - 1]):
                v = img[y][x]
                img[y][x] = 8 if v < 9 else 11
    # outline and a two-pixel drop shadow in the darkest colour
    out = [row[:] for row in img]
    for y in range(H):
        for x in range(W):
            if solid[y][x]:
                continue
            near = False
            for dy in (-1, 0, 1, 2):
                for dx in (-1, 0, 1, 2):
                    yy, xx = y - dy, x - dx
                    if 0 <= yy < H and 0 <= xx < W and solid[yy][xx]:
                        if dy in (-1, 0, 1) and dx in (-1, 0, 1) or (dy == 2 and dx >= 0) or (dx == 2 and dy >= 0):
                            near = True
            if near:
                out[y][x] = 4
    return W, H, out


# ---------------------------------------------------------------- ball
def boing_ball(size=80):
    """The Boing ball, tilted, as palette-rotation indices 8-15: each
    checker column is four sub-bands, so rotating which four of the
    eight colours are red spins the ball a quarter-square per step."""
    r = size / 2 - 0.5
    c = (size - 1) / 2
    tilt = math.radians(-18)
    ct, st = math.cos(tilt), math.sin(tilt)
    out = [[0] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            dx, dy = (x - c) / r, (y - c) / r
            d2 = dx * dx + dy * dy
            if d2 > 1.0:
                continue
            dz = math.sqrt(1 - d2)
            # tilt the axis in the picture plane
            rx = dx * ct - dy * st
            ry = dx * st + dy * ct
            lon = math.atan2(rx, dz)          # -pi/2..pi/2 on the front
            lat = math.asin(max(-1, min(1, ry)))
            sub = int(math.floor((lon + math.pi) / (2 * math.pi) * 16 * 4))
            band = int(math.floor((lat + math.pi / 2) / math.pi * 8))
            out[y][x] = 8 + ((sub + 4 * (band & 1)) & 7)
    return out


def bob(size=16):
    """Shaded sphere, levels 1 (dark) .. 4 (highlight); 0 transparent."""
    out = [[0] * size for _ in range(size)]
    c = (size - 1) / 2
    r = size / 2 - 0.2
    lx, ly, lz = -0.5, -0.6, 0.62
    for y in range(size):
        for x in range(size):
            dx, dy = (x - c) / r, (y - c) / r
            d2 = dx * dx + dy * dy
            if d2 > 1:
                continue
            dz = math.sqrt(1 - d2)
            lum = max(0.0, dx * lx + dy * ly + dz * lz)
            spec = lum ** 12
            v = 0.15 + 0.85 * lum + spec
            out[y][x] = 1 + min(3, int(v * 3.2))
    return out


# ---------------------------------------------------------------- write
def c_bytes(name, data, per_line=24, typ='uint8_t'):
    s = 'const %s %s[%d] = {\n' % (typ, name, len(data))
    for i in range(0, len(data), per_line):
        s += '    ' + ','.join(str(v) for v in data[i:i + per_line]) + ',\n'
    return s + '};\n'


def main():
    os.makedirs(OUT, exist_ok=True)
    chars = [chr(c) for c in range(FIRST, LAST + 1)]
    missing = [c for c in chars if c not in GLYPHS]
    if missing:
        print('note: blank glyphs for', ''.join(missing), file=sys.stderr)

    f8, f16, f32 = [], [], []
    for ch in chars:
        g = glyph(ch)
        f8 += rows_to_int(g)
        g2 = scale2x(g)
        f16 += rows_to_int(g2)
        f32 += rows_to_int(scale2x(g2))

    lw, lh, limg = logo()
    ball = boing_ball(64)
    bb = bob(16)
    sin = [int(round(math.sin(i * 2 * math.pi / 1024) * 16384)) for i in range(1024)]

    h = ['/* generated by tools/mkassets.py - do not edit\n * Copyright (C) 2026 Neil Rackett\n * SPDX-License-Identifier: GPL-3.0-or-later */',
         '#ifndef GEN_ASSETS_H', '#define GEN_ASSETS_H', '#include <stdint.h>',
         '#define FONT_FIRST %d' % FIRST, '#define FONT_LAST %d' % LAST,
         '#define FONT_COUNT %d' % len(chars),
         'extern const uint8_t font8[FONT_COUNT * 8];',
         'extern const uint16_t font16[FONT_COUNT * 16];',
         'extern const uint32_t font32[FONT_COUNT * 32];',
         '#define LOGO_W %d' % lw, '#define LOGO_H %d' % lh,
         'extern const uint8_t logo_img[LOGO_W * LOGO_H];',
         '#define BALL_SIZE 64',
         'extern const uint8_t ball_img[BALL_SIZE * BALL_SIZE];',
         '#define BOB_SIZE 16',
         'extern const uint8_t bob_img[BOB_SIZE * BOB_SIZE];',
         '/* sine, 1024 steps a turn, Q14 */',
         'extern const int16_t sintab[1024];',
         '#endif', '']
    c = ['/* generated by tools/mkassets.py - do not edit\n * Copyright (C) 2026 Neil Rackett\n * SPDX-License-Identifier: GPL-3.0-or-later */',
         '#include "assets.h"', '']
    c.append(c_bytes('font8', f8, 16))
    c.append(c_bytes('font16', ['0x%04x' % v for v in f16], 12, 'uint16_t'))
    c.append(c_bytes('font32', ['0x%08xu' % v for v in f32], 8, 'uint32_t'))
    c.append(c_bytes('logo_img', [v for r in limg for v in r], 32))
    c.append(c_bytes('ball_img', [v for r in ball for v in r], 40))
    c.append(c_bytes('bob_img', [v for r in bb for v in r], 16))
    c.append(c_bytes('sintab', sin, 16, 'int16_t'))
    with open(os.path.join(OUT, 'assets.h'), 'w') as fh:
        fh.write('\n'.join(h))
    with open(os.path.join(OUT, 'assets.c'), 'w') as fh:
        fh.write('\n'.join(c))
    print('assets: font %d glyphs, logo %dx%d, ball, bob, sine' % (len(chars), lw, lh))

    if '--preview' in sys.argv:
        for r in limg:
            print(''.join(' .:-=+*#%@XYZ'[v] if v < 13 else '?' for v in r))
        for r in ball[::4]:
            print(''.join(' 01234567abcdefgh'[v] for v in r[::2]))
        for r in bb:
            print(''.join(' .:o@'[v] for v in r))


if __name__ == '__main__':
    main()
