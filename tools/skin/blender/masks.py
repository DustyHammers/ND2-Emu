# Paint and ink maps for the panel material, made from the skin's background art (2x, 3450 x 2070).
#   python3 masks.py OUT_DIR
# Writes (never committed; CI makes them on every run):
#   inks.png       RGBA, 8 bit, Non-Color: R = red body paint, G = black print, B = red tag print, A = white print.
#                  Edges are unmixed between the two inks that meet there; glyph cores are 100 % ink.
#   ink_height.png 16-bit grey: printed layers (black = 0.5, inks on top = 1.0), softened over about 1 px.
#   lip_height.png 16-bit grey: the paint build-up around each hole (per-hole amount and thicker side).
#   inks_preview.png  the four inks in false colour, for checking.
import json
import math
import os
import sys
import zlib

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage as nd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dims as D
from layout import SKIN, read_rcss_plain

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

art = Image.open(os.path.join(SKIN, 'ND2Background.jpg')).convert('RGB')
W, H = art.size
# the art carries a fine grain; a 5 px median removes it and keeps the glyph edges
p = np.asarray(art.filter(ImageFilter.MedianFilter(5))).astype(np.float32)

RED, BLACK, TAG, WHITE = (168, 26, 38), (24, 24, 24), (200, 26, 40), (226, 222, 222)
C = np.array([RED, BLACK, TAG, WHITE], np.float32)

# 1) which red is where: the body is the one big red region; red regions inside the black print are tags
redish = (p[..., 0] - np.maximum(p[..., 1], p[..., 2])) > 60
lab, n = nd.label(redish)
sizes = nd.sum(redish, lab, range(1, n + 1))
body = int(np.argmax(sizes)) + 1
is_body = lab == body
# a red region away from the body is printed tag ink if black print surrounds it; a red region enclosed by
# white lettering is a letter's counter and takes the red of the nearest body or tag region
dark = p.max(-1) < 70
tag_ids, counters = [], []
for i, sl in enumerate(nd.find_objects(lab), start=1):
    if i == body or sl is None:
        continue
    sl = tuple(slice(max(a.start - 4, 0), a.stop + 4) for a in sl)
    comp = nd.binary_fill_holes(lab[sl] == i)          # the outline only: lettering inside a tag doesn't count
    ring = nd.binary_dilation(comp, iterations=3) & ~comp & ~redish[sl]
    (tag_ids if ring.any() and dark[sl][ring].mean() >= 0.5 else counters).append(i)
is_tag = np.isin(lab, tag_ids)
if counters:
    is_counter = np.isin(lab, counters)
    near_body = nd.distance_transform_edt(~is_body) <= nd.distance_transform_edt(~is_tag)
    is_body |= is_counter & near_body
    is_tag |= is_counter & ~near_body
# each pixel takes the red of the nearest red region (body or tag), so anti-aliased edges unmix to the right red
dist_body = nd.distance_transform_edt(~is_body)
dist_tag = nd.distance_transform_edt(~is_tag)
red_here = np.where((dist_body <= dist_tag)[..., None], np.array(RED, np.float32), np.array(TAG, np.float32))

# 2) unmix each pixel as a blend of two inks: (red_here, black), (red_here, white) or (black, white)
def blend(a, b):
    ab = b - a
    t = np.clip(((p - a) * ab).sum(-1) / np.maximum((ab * ab).sum(-1), 1e-6), 0, 1)
    err = ((a + t[..., None] * ab - p) ** 2).sum(-1)
    return t, err

blk = np.broadcast_to(np.array(BLACK, np.float32), p.shape)
wht = np.broadcast_to(np.array(WHITE, np.float32), p.shape)
pairs = [(red_here, blk), (red_here, wht), (blk, wht)]
res = [blend(a, b) for a, b in pairs]
best = np.argmin(np.stack([e for _, e in res]), 0)
t = np.choose(best, [r[0] for r in res])

def core(x):                     # glyph cores to 100 %: the outer 8 % of each side of the blend goes to the ink
    return np.clip((x - 0.08) / 0.84, 0, 1)

t = core(t)
m_red = np.where(best == 0, 1 - t, np.where(best == 1, 1 - t, 0))
m_black = np.where(best == 0, t, np.where(best == 2, 1 - t, 0))
m_white = np.where(best == 1, t, np.where(best == 2, t, 0))
tag_frac = (red_here[..., 0] == TAG[0]).astype(np.float32)
m_body, m_tag = m_red * (1 - tag_frac), m_red * tag_frac

inks = np.stack([m_body, m_black, m_tag, m_white], -1)
# outside the art's rounded corners (window colour) the sheet is still red paint: it bends down into the walls
_r = D.PLATE['corner_r']
_yy, _xx = np.mgrid[0:H, 0:W]
_cx = np.clip(_xx, _r, W - 1 - _r)
_cy = np.clip(_yy, _r, H - 1 - _r)
_out = (np.hypot(_xx - _cx, _yy - _cy) > _r - 3)
inks[_out] = (1.0, 0.0, 0.0, 0.0)
# the art draws an edge shadow along its sides and bottom (up to about 18 px); in 3D the sheet's bends shade
# themselves, so the outer band is plain body paint (nothing is printed there)
_band = (_yy > H - 40) | (_xx < 20) | (_xx > W - 21) | (_yy < 20)
inks[_band] = (1.0, 0.0, 0.0, 0.0)
del _yy, _xx, _cx, _cy, _band

# 2a) the value encoder's print, redrawn with the circles at the owner's straight-on photo's sizes (the art
#     draws them too big): a black disc (r 272) with a red ring (r 197-209), the main panel's bottom edge notched
#     around it (r 301, red between). The straight edges (main panel, the red stripe, the LOWER PARAM band) are
#     read from the art itself, so they stay exactly where the art has them. Within r 340 only; the art's own
#     notch is inside that, so everything meets the untouched art at the boundary.
_rects0, _kinds0 = read_rcss_plain()
_ecx, _ecy = _rects0['value'][:2]
ENC = dict(ring_in=197.0, ring_out=209.0, disc=272.0, notch=301.0, region=340.0)


def _edges_left(x):
    """Rows (relative to the centre) where the art's column x changes ink: main panel / stripe / band / body."""
    col = p[int(_ecy) - 520:int(_ecy) + 420, int(x)]
    red = (col[:, 0] - np.maximum(col[:, 1], col[:, 2])) > 60
    ch = np.where(np.diff(red.astype(int)) != 0)[0] + 1 - 520
    return ch


_ch = _edges_left(_ecx - 450)      # red stripe starts, band starts, body starts
_y_main, _y_band_top, _y_band_bot = float(_ch[0]), float(_ch[1]), float(_ch[2])
_R = int(ENC['region']) + 2
_y0, _y1, _x0, _x1 = int(_ecy - _R), int(_ecy + _R) + 1, int(_ecx - _R), int(_ecx + _R) + 1
_ss = 4                                                            # supersampling for clean edges
_yy, _xx = np.mgrid[0:(_y1 - _y0) * _ss, 0:(_x1 - _x0) * _ss].astype(np.float32)
_dx = _x0 + (_xx + 0.5) / _ss - _ecx
_dy = _y0 + (_yy + 0.5) / _ss - _ecy
_r = np.hypot(_dx, _dy)
_black = (_dy < _y_main) & (_r > ENC['notch'])                       # main panel, notched around the disc
_black |= (_dy >= _y_band_top) & (_dy < _y_band_bot) & (_dx < 0)     # the LOWER PARAM band, running into the disc
_black |= _r < ENC['disc']                                           # the disc
_ring = (_r >= ENC['ring_in']) & (_r < ENC['ring_out'])
_black &= ~_ring
_blk = _black.reshape(_y1 - _y0, _ss, _x1 - _x0, _ss).mean((1, 3))
_inside = np.hypot(np.arange(_x0, _x1)[None, :] + 0.5 - _ecx, np.arange(_y0, _y1)[:, None] + 0.5 - _ecy) < ENC['region']
_loc = inks[_y0:_y1, _x0:_x1]
_loc[_inside] = np.stack([1 - _blk, _blk, np.zeros_like(_blk), np.zeros_like(_blk)], -1)[_inside]
print(f'encoder print redrawn: main panel edge {_y_main:+.0f}, band {_y_band_top:+.0f} to {_y_band_bot:+.0f} (rows from the centre)')

# 2b) keep lettering and label windows clear of the LED holes and their rounded edges: a group (a line of
#     lettering, or a label window with its letters) that reaches into a hole's keep-out circle is narrowed
#     horizontally, anchored on its far side, until it ends just outside it. In the art, DYN FILTER runs into the
#     ATK/RATE LED and the ENV MODE window into the DECAY LED.
_rects, _kinds = read_rcss_plain()
fixes = []
printed = (inks[..., 2] > 0.3) | (inks[..., 3] > 0.3)
groups, _ = nd.label(nd.binary_dilation(printed, structure=np.ones((5, 29), bool)))
KEEP = D.LED['hole_r'] + D.PLATE['led_fillet'] + 1.5   # the hole, its rounded edge, a hair of clear print
for cid, (cx, cy, w, h) in _rects.items():
    if _kinds.get(cid, ('', ''))[0] != 'led':
        continue
    y0, y1, x0, x1 = int(cy - KEEP), int(cy + KEEP) + 1, int(cx - KEEP), int(cx + KEEP) + 1
    yy_, xx_ = np.mgrid[y0:y1, x0:x1]
    near = printed[y0:y1, x0:x1] & ((xx_ - cx) ** 2 + (yy_ - cy) ** 2 <= KEEP ** 2)
    for g in np.unique(groups[y0:y1, x0:x1][near]):
        gm = (groups == g) & printed
        ys, xs = np.where(gm)
        gy0, gy1, gx0, gx1 = ys.min() - 2, ys.max() + 3, xs.min() - 2, xs.max() + 3
        if (gx0 + gx1) / 2 < cx:            # the group is left of the hole: pull its right end in
            new_w = int(cx - KEEP) - gx0
            dst = (gx0, gx0 + new_w)
        else:                               # right of the hole: push its left end out
            new_w = gx1 - int(cx + KEEP)
            dst = (gx1 - new_w, gx1)
        if new_w >= gx1 - gx0:
            continue
        region = inks[gy0:gy1, gx0:gx1].copy()
        soft = nd.binary_dilation(gm[gy0:gy1, gx0:gx1], iterations=2).astype(np.float32)
        bg = np.zeros_like(region)
        bg[..., 1] = 1.0                    # black print around the lettering
        def squeeze(a):
            return np.stack([np.asarray(Image.fromarray(a[..., c]).resize((new_w, a.shape[0]), Image.LANCZOS))
                             for c in range(a.shape[-1])], -1) if a.ndim == 3 else \
                np.asarray(Image.fromarray(a).resize((new_w, a.shape[0]), Image.LANCZOS))
        g_ink = squeeze((region * soft[..., None]).astype(np.float32))
        g_m = np.clip(squeeze(soft), 0, 1)
        out = region * (1 - soft[..., None]) + bg * soft[..., None]
        a0 = dst[0] - gx0
        out[:, a0:a0 + new_w] = out[:, a0:a0 + new_w] * (1 - g_m[..., None]) + g_ink
        out = np.clip(out, 0, 1)
        inks[gy0:gy1, gx0:gx1] = out / np.maximum(out.sum(-1, keepdims=True), 1e-6)
        printed = (inks[..., 2] > 0.3) | (inks[..., 3] > 0.3)
        fixes.append(dict(led=cid, box=[int(gx0), int(gy0), int(gx1), int(gy1)], width_from=int(gx1 - gx0), width_to=int(new_w),
                          squeeze=round(new_w / (gx1 - gx0), 3)))
        print(f'label near {cid}: {gx1 - gx0} -> {new_w} px wide ({100 * new_w / (gx1 - gx0):.1f} %)')
m_body, m_black, m_tag, m_white = (inks[..., i] for i in range(4))
Image.fromarray((inks * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'inks.png'))

# false-colour preview (red body = dark red, black = dark grey, tag = orange, white = white)
pv = (m_body[..., None] * [120, 20, 30] + m_black[..., None] * [40, 40, 44] + m_tag[..., None] * [255, 150, 0]
      + m_white[..., None] * [255, 255, 255])
Image.fromarray(np.clip(pv, 0, 255).astype(np.uint8)).resize((W // 2, H // 2), Image.LANCZOS).save(
    os.path.join(OUT, 'inks_preview.png'))

# 3) printed relief: black print one layer, tag and white print a second layer on top; softened ~1 px
# label windows are the red paint showing through the black print (no print there); white prints on top
ink_h = 0.5 * m_black + 1.0 * m_white
ink_h = nd.gaussian_filter(ink_h, 0.8)
Image.fromarray((np.clip(ink_h, 0, 1) * 65535 + 0.5).astype(np.uint16)).save(os.path.join(OUT, 'ink_height.png'))

# 4) paint build-up lip around each hole: a narrow raised ring just outside the edge, its amount and thicker side
#    from a hash of the hole's id (repeatable)
rects, kinds = read_rcss_plain()
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
lip = np.zeros((H, W), np.float32)
LIPW = 0.30 * D.MM


def rr_sdf(x, y, cx, cy, w, h, r):
    qx, qy = np.abs(x - cx) - (w / 2 - r), np.abs(y - cy) - (h / 2 - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


holes = []
for cid, (cx, cy, w, h) in rects.items():
    kind = kinds.get(cid, ('', ''))[0]
    if kind == 'pbutton':
        holes.append((cid, cx, cy, D.BUTTON['hole_w'], D.BUTTON['hole_h'], D.BUTTON['hole_r']))
    elif kind == 'led':
        r = D.LED['hole_r']
        holes.append((cid, cx, cy, 2 * r, 2 * r, r))
d = D.DISPLAY
holes.append(('display', (d['x0'] + d['x1']) / 2, (d['y0'] + d['y1']) / 2, d['x1'] - d['x0'], d['y1'] - d['y0'], d['r']))
for cid, cx, cy, w, h, r in holes:
    hsh = zlib.crc32(cid.encode())
    amount = 0.6 + 0.4 * ((hsh & 0xff) / 255)
    side = 2 * math.pi * (((hsh >> 8) & 0xff) / 255)
    m = 12 + LIPW * 4
    x0, x1 = int(max(cx - w / 2 - m, 0)), int(min(cx + w / 2 + m, W))
    y0, y1 = int(max(cy - h / 2 - m, 0)), int(min(cy + h / 2 + m, H))
    sx, sy = xx[y0:y1, x0:x1], yy[y0:y1, x0:x1]
    s = rr_sdf(sx, sy, cx, cy, w, h, r)
    ang = np.arctan2(sy - cy, sx - cx)
    thick = 0.65 + 0.35 * np.cos(ang - side)
    ring = np.where(s > 0, np.exp(-((s - 0.35 * LIPW) / (LIPW * thick)) ** 2), 0) * amount * thick
    lip[y0:y1, x0:x1] = np.maximum(lip[y0:y1, x0:x1], ring)
Image.fromarray((np.clip(lip, 0, 1) * 65535 + 0.5).astype(np.uint16)).save(os.path.join(OUT, 'lip_height.png'))

json.dump({'size': [W, H], 'inks_srgb_art': {'red': RED, 'black': BLACK, 'tag': TAG, 'white': WHITE},
           'label_fixes': fixes,
           'coverage': {k: float(v.mean()) for k, v in zip(('red', 'black', 'tag', 'white'), (m_body, m_black, m_tag, m_white))}},
          open(os.path.join(OUT, 'masks.json'), 'w'), indent=1)
print('masks written to', OUT)
