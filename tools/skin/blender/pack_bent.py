# Packs the bent mode's renders (bent_sprites.py) into the files the plugin draws over the normal skin:
#   python3 pack_bent.py RENDERS OUT
# OUT receives ND2BendPanel.png (the opened panel, drawn over the background only in bent mode), ND2BendPot.png
# (one pot shaft, 128 frames, shared by the six pots), ND2BendLed.png (the indicator, lit and unlit),
# README.txt (positions in dp and how each is used) and preview/ (normal and bent, for review only).
import glob
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import types

# kept in step with bent_sprites.py and bent_mod.py (which need Blender; this step runs without it)
BS = types.SimpleNamespace(POT_FRAMES=128, POT_BOX=60, LED_BOX=50)
MOD = dict(pots_x=(700.0, 1110.0, 1520.0, 1930.0, 2340.0, 2750.0), pot_y=236.0, led=(2905.0, 205.0), sweep=300.0)
R, OUT = sys.argv[1], sys.argv[2]
os.makedirs(os.path.join(OUT, 'preview'), exist_ok=True)
PANEL_BG = (0x2b, 0x2b, 0x2e)


def bands(prefix):
    fs = sorted(glob.glob(os.path.join(R, f'{prefix}_*.png')), key=lambda f: int(re.findall(r'\d+', os.path.basename(f))[-1]))
    ims = [Image.open(f).convert('RGBA') for f in fs]
    out = Image.new('RGBA', (ims[0].width, sum(i.height for i in ims)))
    y = 0
    for i in ims:
        out.paste(i, (0, y))
        y += i.height
    return out


def flat(im):
    return Image.alpha_composite(Image.new('RGBA', im.size, (*PANEL_BG, 255)), im)


def radial(im, r_full, r_zero):
    a = np.asarray(im).astype(np.float32)
    h, w = a.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    d = np.hypot(x + 0.5 - w / 2, y + 0.5 - h / 2)
    t = np.clip((r_zero - d) / (r_zero - r_full), 0, 1)
    a[..., 3] *= t * t * (3 - 2 * t)
    return Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), 'RGBA')


# ---- the opened panel: only where it differs from the normal background, feathered --------------------------
strip, base = flat(bands('strip')), flat(bands('base'))
s = np.asarray(strip).astype(np.float32)
b = np.asarray(base).astype(np.float32)
diff = np.abs(s[..., :3] - b[..., :3]).max(-1)
diff = nd.uniform_filter(diff, 5)                      # average out the render noise
mask = diff > 8.0                                      # the opening and the pots' nearby shading
lab, n = nd.label(mask)
sizes = nd.sum(mask, lab, range(1, n + 1))
mask = np.isin(lab, [i + 1 for i, z in enumerate(sizes) if z > 2000])   # no stray noise specks
mask = nd.binary_fill_holes(nd.binary_closing(mask, iterations=8))
# carry the faint shading around it too, fading out over about 20 px so the edge never shows
alpha = nd.gaussian_filter(nd.binary_dilation(mask, iterations=30).astype(np.float32), 8.0)
alpha = np.clip((alpha - 0.02) / 0.6, 0, 1)
ys, xs = np.where(alpha > 0.004)
x0, y0, x1, y1 = xs.min() & ~1, ys.min() & ~1, (xs.max() + 2) & ~1, (ys.max() + 2) & ~1   # even: whole dp
rgba = s.copy()
rgba[..., 3] = alpha * 255
panel = Image.fromarray(np.clip(rgba + 0.5, 0, 255).astype(np.uint8), 'RGBA').crop((x0, y0, x1, y1))
panel.save(os.path.join(OUT, 'ND2BendPanel.png'))

# ---- one pot shaft, 128 frames: only the shaft's top (the bushing and body are in the panel image) ----------
cell = 2 * BS.POT_BOX
cols = 16
rows = math.ceil(BS.POT_FRAMES / cols)
pots = Image.new('RGBA', (cols * cell, rows * cell))
r_shaft = 3.0 * 3450 / 222                              # knurled shaft radius, px (2x)
for f in range(BS.POT_FRAMES):
    im = radial(Image.open(os.path.join(R, f'pot_{f:03d}.png')).convert('RGBA'), r_shaft + 0.5, r_shaft + 2.0)
    pots.paste(im, ((f % cols) * cell, (f // cols) * cell))
pots.save(os.path.join(OUT, 'ND2BendPot.png'))

# ---- the LED: lit and unlit, the lit one with its glow ----------------------------------------------------
lc = 2 * BS.LED_BOX
led = Image.new('RGBA', (lc, 2 * lc))
r_led = 2.5 * 3450 / 222
led.paste(radial(Image.open(os.path.join(R, 'led_on.png')).convert('RGBA'), r_led + 2, BS.LED_BOX - 2), (0, 0))
led.paste(radial(Image.open(os.path.join(R, 'led_off.png')).convert('RGBA'), r_led + 0.5, r_led + 2.0), (0, lc))
led.save(os.path.join(OUT, 'ND2BendLed.png'))

# ---- preview: the same rows normal and bent, pots at mixed positions, LED lit --------------------------------
def frame_img(f):
    return pots.crop(((f % cols) * cell, (f // cols) * cell, (f % cols + 1) * cell, (f // cols + 1) * cell))


bent = base.copy()
bent.alpha_composite(panel, (x0, y0))
for px, f in zip(MOD['pots_x'], (0, 0, 64, 16, 111, 0)):
    bent.alpha_composite(frame_img(f), (round(px) - BS.POT_BOX, round(MOD['pot_y']) - BS.POT_BOX))
lx, ly = MOD['led']
bent.alpha_composite(led.crop((0, 0, lc, lc)), (round(lx) - BS.LED_BOX, round(ly) - BS.LED_BOX))
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 30)
except OSError:
    font = ImageFont.load_default()
W = base.width // 2
sheet = Image.new('RGB', (W + 40, 2 * (base.height // 2) + 140), (245, 245, 245))
d = ImageDraw.Draw(sheet)
d.text((20, 10), 'Normal', fill=(20, 20, 20), font=font)
sheet.paste(base.convert('RGB').resize((W, base.height // 2), Image.LANCZOS), (20, 50))
d.text((20, 70 + base.height // 2), 'Bent mode (pots at mixed positions, LED lit)', fill=(20, 20, 20), font=font)
sheet.paste(bent.convert('RGB').resize((W, base.height // 2), Image.LANCZOS), (20, 110 + base.height // 2))
sheet.save(os.path.join(OUT, 'preview', 'normal and bent.png'))
cells = [frame_img(round(k * (BS.POT_FRAMES - 1) / 8)) for k in range(9)]
st = Image.new('RGB', (9 * (cell * 2 + 10) + 10, cell * 2 + 60), (245, 245, 245))
d = ImageDraw.Draw(st)
for k, c in enumerate(cells):
    x = 10 + k * (cell * 2 + 10)
    st.paste(flat(c).convert('RGB').resize((cell * 2, cell * 2), Image.LANCZOS), (x, 50))
    d.text((x, 10), 'Off' if k == 0 else f'Bend {k}', fill=(20, 20, 20), font=font)
st.save(os.path.join(OUT, 'preview', 'pot positions.png'))

# ---- read-me ------------------------------------------------------------------------------------------------
dp = lambda v: v / 2
L = ['ND2 bent mode: images rendered from the 3D model', '',
     'All positions are in dp on the 1725 x 1035 dp panel (the images are 2x: 1 dp = 2 px).', '',
     f'ND2BendPanel.png   the opened panel; draw it at left {dp(x0):g} dp, top {dp(y0):g} dp, '
     f'{dp(x1 - x0):g} x {dp(y1 - y0):g} dp, over the background and under the controls, only in bent mode.',
     '                   It holds the opening, the board, the pot bodies, the wiring, the shafts\' shadows and',
     '                   the LED\'s glow on the board; it fades out to the panel at its edges.', '',
     f'ND2BendPot.png     one pot shaft, {BS.POT_FRAMES} frames of {dp(cell):g} x {dp(cell):g} dp, {cols} per row, '
     'shared by all six pots.',
     '                   Frame 0 = 7 o\'clock (off), frame 127 = 5 o\'clock, 300 degrees, linear. Turn smoothly, like',
     '                   the MASTER LEVEL knob. Positions: off and bends 1-8 at frames '
     + ', '.join(str(round(k * (BS.POT_FRAMES - 1) / 8)) for k in range(9)) + ' (the nearest one is picked).',
     '                   Centres (dp): ' + ', '.join(f'({dp(x):g}, {dp(MOD["pot_y"]):g})' for x in MOD['pots_x'])
     + f'; each image is centred there (left = x - {dp(cell) / 2:g} dp, top = y - {dp(cell) / 2:g} dp).', '',
     f'ND2BendLed.png     the indicator LED, {dp(lc):g} x {dp(lc):g} dp: lit (top), unlit (bottom), centred at '
     f'({dp(lx):g}, {dp(ly):g}) dp.',
     '                   Always lit in bent mode; while any pot turns it flickers (the lit image\'s opacity jumping',
     '                   between about 0.35 and 1.0 a few dozen times a second, over the unlit image).', '',
     'preview/ is for review only and does not go into the skin.']
open(os.path.join(OUT, 'README.txt'), 'w').write('\n'.join(L) + '\n')
print('\n'.join(L))
