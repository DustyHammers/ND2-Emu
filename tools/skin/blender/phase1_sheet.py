# Phase 1 sheets: the render on the window colour with the rcss rectangles drawn over it, and crops of the
# 2x render next to the same crops of a reference photo warped into the art frame (if given).
#   python3 phase1_sheet.py RENDER_1X RENDER_2X OUT_DIR [photo_in_art2x.png]
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layout import read_rcss_plain

r1, r2, out = sys.argv[1:4]
photo = sys.argv[4] if len(sys.argv) > 4 else None
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
f = ImageFont.truetype(FONT, 11) if os.path.exists(FONT) else None
fb = ImageFont.truetype(FONT.replace('Sans', 'Sans-Bold'), 20) if os.path.exists(FONT) else None
rects, kinds = read_rcss_plain()
COL = {'pbutton': (0, 230, 255), 'led': (255, 230, 0), 'knob': (0, 255, 90), 'encoder': (0, 255, 90),
       'segdigit': (255, 0, 255), 'action': (255, 140, 0)}


def on_window(path):
    im = Image.open(path).convert('RGBA')
    bg = Image.new('RGBA', im.size, (0x2b, 0x2b, 0x2e, 255))
    bg.alpha_composite(im)
    return bg.convert('RGB')


def overlay(im, s):
    d = ImageDraw.Draw(im)
    for cid, (cx, cy, w, h) in rects.items():
        c = COL.get(kinds.get(cid, ('', ''))[0], (255, 255, 255))
        x0, y0, x1, y1 = (cx - w / 2) * s, (cy - h / 2) * s, (cx + w / 2) * s, (cy + h / 2) * s
        d.rectangle([x0, y0, x1, y1], outline=c)
        m = 4 * s * 2
        d.line([cx * s - m, cy * s, cx * s + m, cy * s], fill=c)
        d.line([cx * s, cy * s - m, cx * s, cy * s + m], fill=c)
    return im


# 1) whole panel at 1x with the rectangles
a = Image.open(r1)
alpha = a.getchannel('A')
bbox = alpha.point(lambda v: 255 if v > 128 else 0).getbbox()
im = overlay(on_window(r1), 0.5)
im.save(os.path.join(out, 'phase1_overlay_1x.png'))
print('opaque plate bbox at 1x:', bbox, 'canvas', a.size)

# 2) crops at 2x: render | photo, both with the rectangles
if photo:
    R = overlay(on_window(r2), 1.0)
    P = overlay(Image.open(photo).convert('RGB'), 1.0)
    crops = [('MASTER LEVEL knob', (100, 520, 520, 940)), ('Program button + display', (690, 470, 1560, 720)),
             ('Parameter buttons 1-2 + LEDs', (690, 790, 1260, 1230)), ('Value encoder', (1500, 1340, 2110, 1950)),
             ('Input LEDs 1-6', (2080, 490, 2920, 680)), ('SHIFT button', (360, 1490, 670, 1670))]
    rows = []
    for name, box in crops:
        c1, c2 = R.crop(box), P.crop(box)
        row = Image.new('RGB', (c1.width * 2 + 30, c1.height + 34), (24, 24, 26))
        row.paste(c1, (0, 34))
        row.paste(c2, (c1.width + 30, 34))
        ImageDraw.Draw(row).text((4, 6), name + '   (left: render, flat grey; right: your top-down photo)',
                                 fill=(235, 235, 235), font=fb)
        rows.append(row)
    W = max(r.width for r in rows)
    H = sum(r.height + 16 for r in rows)
    sheet = Image.new('RGB', (W, H), (24, 24, 26))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height + 16
    sheet.save(os.path.join(out, 'phase1_crops_vs_photo.png'))
