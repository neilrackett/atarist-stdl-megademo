#!/usr/bin/env python3
# STDL Megademo - contact sheet of a screenshot directory
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#   sheet.py DIR OUT.png [COLUMNS]   (needs Pillow)
import sys, glob
from PIL import Image, ImageDraw
d = sys.argv[1]; out = sys.argv[2]; cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
files = sorted(glob.glob(d + '/*.png'))
ims = [Image.open(f).convert('RGB') for f in files]
w, h = ims[0].size
s = 0.5
tw, th = int(w * s), int(h * s)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * tw, rows * th), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for i, im in enumerate(ims):
    x, y = (i % cols) * tw, (i // cols) * th
    sheet.paste(im.resize((tw, th)), (x, y))
    dr.text((x + 4, y + 2), str(i + 1), fill=(255, 255, 0))
sheet.save(out)
print(out, sheet.size)
