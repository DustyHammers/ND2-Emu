# Packs the 3D renders into the default skin's files, plus a preview of every state for review:
#   python3 pack_skin.py RENDERS OUT [--test]
# RENDERS holds bg_N.png (the background in N horizontal bands, from the top) or bg.png, and the sprites.py
# images (btn_*, led_*, master_NNN, enc_NNN, each the control's box plus MARGIN px). OUT receives:
#   ND2Background.jpg, ND2BlackButton.png, ND2GreyButton.png, ND2RedLed.png, ND2GreenLed.png,
#   ND2MasterKnob.png, ND2Encoder.png, nd2-default.rcss (sprite sizes updated, layout untouched),
#   preview/ (the panel with controls in several states, and every control state enlarged), README.txt
# --test fills missing knob/encoder frames with the nearest rendered one (local checks only).
import glob
import hashlib
import math
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from layout import SKIN, read_rcss_plain

MARGIN = 8
PANEL_BG = (0x2b, 0x2b, 0x2e)          # body background-colour in the stylesheet
MASTER_FRAMES, ENC_FRAMES = 128, 72
MASTER_CELL, MASTER_COLS = 262, 14     # 131 dp at 2x
ENC_CELL, ENC_COLS = 448, 12           # 224 dp at 2x
LED_OFF, LED_ON = 44, 56               # 22 dp and the 28 dp lamp, at 2x
PRESS_DROP, PRESS_DARKEN = 2, 0.88     # pressed buttons: as a press looks from slightly in front of the panel
HOLE_R = 23.0                          # LED hole radius (dims.LED)

R, OUT = sys.argv[1], sys.argv[2]
TEST = '--test' in sys.argv
os.makedirs(os.path.join(OUT, 'preview'), exist_ok=True)
rects, kinds = read_rcss_plain()


def load(name):
    return Image.open(os.path.join(R, name)).convert('RGBA')


def inner(im, w, h):
    """The control's box out of a sprite render (MARGIN px around it)."""
    return im.crop((MARGIN, MARGIN, MARGIN + w, MARGIN + h))


def radial_alpha(im, r_full, r_zero):
    """Opaque inside r_full, fading to transparent at r_zero (from the centre)."""
    a = np.asarray(im).astype(np.float32)
    h, w = a.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    d = np.hypot(x + 0.5 - w / 2, y + 0.5 - h / 2)
    t = np.clip((r_zero - d) / (r_zero - r_full), 0, 1)
    t = t * t * (3 - 2 * t)
    a[..., 3] = a[..., 3] * t
    return Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), 'RGBA')


def frame(prefix, i, n):
    p = os.path.join(R, f'{prefix}_{i:03d}.png')
    if os.path.exists(p) or not TEST:
        return Image.open(p).convert('RGBA')
    have = sorted(int(os.path.basename(f)[len(prefix) + 1:-4]) for f in glob.glob(os.path.join(R, f'{prefix}_*.png')))
    return frame(prefix, min(have, key=lambda k: abs(k - i)), n)


def md5(p):
    return hashlib.md5(open(p, 'rb').read()).hexdigest()


# ---- background ---------------------------------------------------------------------------------------------
bands = sorted(glob.glob(os.path.join(R, 'bg_*.png')), key=lambda f: int(re.findall(r'\d+', os.path.basename(f))[0]))
if bands:
    parts = [Image.open(f).convert('RGBA') for f in bands]
    bg = Image.new('RGBA', (parts[0].width, sum(p.height for p in parts)))
    y = 0
    for p in parts:
        bg.paste(p, (0, y))
        y += p.height
else:
    bg = load('bg.png')
assert bg.size == (3450, 2070), bg.size
flat = Image.alpha_composite(Image.new('RGBA', bg.size, (*PANEL_BG, 255)), bg).convert('RGB')
flat.save(os.path.join(OUT, 'ND2Background.jpg'), quality=92, subsampling=0, optimize=True)

# ---- buttons: up and pressed, the whole 122 x 59 dp box (it lies inside the hole, so it is all cap and gap) ----
for cls in ('black', 'grey'):
    sheet = Image.new('RGBA', (244, 236))
    for k, st in enumerate(('up', 'down')):
        src = load(f'btn_{cls}_{st}.png')
        if st == 'up':
            im = inner(src, 244, 118)
        else:                    # pressed: the cap sits PRESS_DROP px lower (the gap above it opens) and darker
            im = src.crop((MARGIN, MARGIN - PRESS_DROP, MARGIN + 244, MARGIN - PRESS_DROP + 118))
        a = np.asarray(im).copy()
        if st == 'down':
            a[..., :3] = np.clip(a[..., :3].astype(np.float32) * PRESS_DARKEN, 0, 255).astype(np.uint8)
        a[..., 3] = 255
        sheet.paste(Image.fromarray(a, 'RGBA'), (0, 118 * k))
    sheet.save(os.path.join(OUT, f'ND2{cls.capitalize()}Button.png'))

# ---- LEDs: the unlit lens in its 22 dp box; the lit lens as the 28 dp lamp, its glow fading out past the hole ----
for col in ('red', 'green'):
    off = radial_alpha(inner(load(f'led_{col}_off.png'), LED_OFF, LED_OFF), HOLE_R - 1.0, HOLE_R)
    on_full = load(f'led_{col}_on.png')
    c = (on_full.width - LED_ON) // 2
    on = radial_alpha(on_full.crop((c, c, c + LED_ON, c + LED_ON)), HOLE_R, LED_ON / 2)
    sheet = Image.new('RGBA', (LED_ON, LED_OFF + LED_ON))
    sheet.paste(off, (0, 0))
    sheet.paste(on, (0, LED_OFF))
    sheet.save(os.path.join(OUT, f'ND2{col.capitalize()}Led.png'))

# ---- MASTER LEVEL knob and value encoder: one cell per frame, feathered out to the panel around them ----------
def anim(prefix, n, cell, cols, r_full, r_zero, name):
    rows = math.ceil(n / cols)
    sheet = Image.new('RGBA', (cols * cell, rows * cell))
    for i in range(n):
        im = radial_alpha(inner(frame(prefix, i, n), cell, cell), r_full, r_zero)
        sheet.paste(im, ((i % cols) * cell, (i // cols) * cell))
    sheet.save(os.path.join(OUT, name))
    return sheet


master = anim('master', MASTER_FRAMES, MASTER_CELL, MASTER_COLS, 118, 130, 'ND2MasterKnob.png')
enc = anim('enc', ENC_FRAMES, ENC_CELL, ENC_COLS, 182, 222, 'ND2Encoder.png')

# ---- stylesheet: the new sprite sizes; positions and everything else unchanged -----------------------------
css = open(os.path.join(SKIN, 'nd2-default.rcss')).read()
css = re.sub(r'@spritesheet leds_red \{[^}]*\}',
             f'@spritesheet leds_red {{ src: ND2RedLed.png; resolution: 2x; led_red_off: 0px 0px {LED_OFF}px {LED_OFF}px; '
             f'led_red_on: 0px {LED_OFF}px {LED_ON}px {LED_ON}px; }}', css)
css = re.sub(r'@spritesheet leds_green \{[^}]*\}',
             f'@spritesheet leds_green {{ src: ND2GreenLed.png; resolution: 2x; led_green_off: 0px 0px {LED_OFF}px {LED_OFF}px; '
             f'led_green_on: 0px {LED_OFF}px {LED_ON}px {LED_ON}px; }}', css)
cells = ''.join(f'\n  master_{i:03d}: {(i % MASTER_COLS) * MASTER_CELL}px {(i // MASTER_COLS) * MASTER_CELL}px '
                f'{MASTER_CELL}px {MASTER_CELL}px;' for i in range(MASTER_FRAMES))
css = re.sub(r'@spritesheet master \{[^}]*\}', '@spritesheet master { src: ND2MasterKnob.png; resolution: 2x;' + cells + '\n}', css)
assert css.count('master_127:') == 1 and 'led_red_off: 0px 0px 44px' in css
open(os.path.join(OUT, 'nd2-default.rcss'), 'w').write(css)

# ---- preview: the panel as the plugin draws it, with controls in mixed states ------------------------------
def box(cid):
    cx, cy, w, h = rects[cid]
    return round(cx - w / 2), round(cy - h / 2)


def paste(canvas, im, xy, opacity=1.0):
    if opacity < 1.0:
        a = np.asarray(im).astype(np.float32)
        a[..., 3] *= opacity
        im = Image.fromarray(a.astype(np.uint8), 'RGBA')
    layer = Image.new('RGBA', canvas.size)
    layer.paste(im, xy)
    return Image.alpha_composite(canvas, layer)


btn = {c: Image.open(os.path.join(OUT, f'ND2{c.capitalize()}Button.png')) for c in ('black', 'grey')}
led = {c: Image.open(os.path.join(OUT, f'ND2{c.capitalize()}Led.png')) for c in ('red', 'green')}
green = {'l_6', 'l_14', 'l_15', 'l_23', 'l_22', 'l_30'}
pressed = {'b_program', 'b_param3', 'b_trig'}
lit = {'l_29': 1.0, 'l_6': 1.0, 'l_4': 1.0, 'l_14': 0.5, 'l_7': 1.0, 'l_16': 0.3}
seg = Image.open(os.path.join(SKIN, 'ND2DisplaySegments.png')).convert('RGBA')
segc = {n: seg.crop((i * 176, 0, (i + 1) * 176, 212)).resize((142, 172), Image.LANCZOS)
        for i, n in enumerate(('unlit', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'dp'))}


def panel(master_f, enc_f, all_states=False):
    cv = Image.alpha_composite(Image.new('RGBA', flat.size), flat.convert('RGBA'))
    for cid, (kind, cls) in kinds.items():
        if kind == 'pbutton':
            down = cid in pressed
            cv = paste(cv, btn[cls].crop((0, 118 * down, 244, 118 * down + 118)), box(cid))
        elif kind == 'led':
            c = 'green' if cid in green else 'red'
            x, y = box(cid)
            cv = paste(cv, led[c].crop((0, 0, LED_OFF, LED_OFF)), (x, y))
            o = lit.get(cid, 0.0)
            if o:
                cv = paste(cv, led[c].crop((0, LED_OFF, LED_ON, LED_OFF + LED_ON)), (x - 6, y - 6), o)
        elif kind == 'knob':
            cv = paste(cv, master.crop(((master_f % MASTER_COLS) * MASTER_CELL, (master_f // MASTER_COLS) * MASTER_CELL,
                                        (master_f % MASTER_COLS + 1) * MASTER_CELL, (master_f // MASTER_COLS + 1) * MASTER_CELL)), box(cid))
        elif kind == 'encoder':
            cv = paste(cv, enc.crop(((enc_f % ENC_COLS) * ENC_CELL, (enc_f // ENC_COLS) * ENC_CELL,
                                     (enc_f % ENC_COLS + 1) * ENC_CELL, (enc_f // ENC_COLS + 1) * ENC_CELL)), box(cid))
        elif kind == 'segdigit':
            pos = int(cid[1:])
            ch, dp = [('P', 0), ('1', 1), ('0', 0), ('1', 1)][pos]
            font = {'P': 'abefg', '1': 'bc', '0': 'abcdef'}
            for n in ['unlit'] + list(font[ch]) + (['dp'] if dp else []):
                cv = paste(cv, segc[n], box(cid))
    lab = Image.open(os.path.join(SKIN, 'ND2BanksLabel.png')).convert('RGBA')   # drawn by the plugin, not baked in
    cv = paste(cv, lab, box('a_banks'))
    return cv.convert('RGB')


prev = panel(100, 9)
prev.save(os.path.join(OUT, 'preview', 'panel preview.jpg'), quality=92, subsampling=0)

# every state, enlarged
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 26)
except OSError:
    font = ImageFont.load_default()
tiles = [('Black button up', btn['black'].crop((0, 0, 244, 118)), 2), ('Black pressed', btn['black'].crop((0, 118, 244, 236)), 2),
         ('Grey button up', btn['grey'].crop((0, 0, 244, 118)), 2), ('Grey pressed', btn['grey'].crop((0, 118, 244, 236)), 2)]
for c in ('red', 'green'):
    tiles += [(f'{c.capitalize()} LED off', led[c].crop((0, 0, LED_OFF, LED_OFF)), 6),
              (f'{c.capitalize()} LED lit', led[c].crop((0, LED_OFF, LED_ON, LED_OFF + LED_ON)), 6)]
for f in (0, 64, 127):
    tiles.append((f'MASTER {f}', master.crop(((f % MASTER_COLS) * MASTER_CELL, (f // MASTER_COLS) * MASTER_CELL,
                                             (f % MASTER_COLS + 1) * MASTER_CELL, (f // MASTER_COLS + 1) * MASTER_CELL)), 1.5))
for f in (0, 3, 6):
    tiles.append((f'Encoder {f}', enc.crop(((f % ENC_COLS) * ENC_CELL, (f // ENC_COLS) * ENC_CELL,
                                           (f % ENC_COLS + 1) * ENC_CELL, (f // ENC_COLS + 1) * ENC_CELL)), 0.9))
cellw, cellh, cols = 520, 460, 4
sheet = Image.new('RGB', (cols * cellw, math.ceil(len(tiles) / cols) * cellh), (245, 245, 245))
d = ImageDraw.Draw(sheet)
for k, (t, im, s) in enumerate(tiles):
    x, y = (k % cols) * cellw, (k // cols) * cellh
    im = im.resize((round(im.width * s), round(im.height * s)), Image.NEAREST if s >= 2 else Image.LANCZOS)
    under = Image.new('RGBA', im.size, (*PANEL_BG, 255))
    sheet.paste(Image.alpha_composite(under, im).convert('RGB'), (x + (cellw - im.width) // 2, y + 50 + (cellh - 60 - im.height) // 2))
    d.text((x + 16, y + 12), t, fill=(20, 20, 20), font=font)
sheet.save(os.path.join(OUT, 'preview', 'every control state.png'))

# ---- read-me ------------------------------------------------------------------------------------------------
names = ['ND2Background.jpg', 'ND2BlackButton.png', 'ND2GreyButton.png', 'ND2RedLed.png', 'ND2GreenLed.png',
         'ND2MasterKnob.png', 'ND2Encoder.png', 'nd2-default.rcss']
lines = ['ND2 default skin: files rendered from the 3D model', '',
         'Copy these files into skins/default/nd2-default/, replacing the ones there:', '']
for n in names:
    old = os.path.join(SKIN, n)
    lines.append(f'  {n:22s} new md5 {md5(os.path.join(OUT, n))}   replaces md5 {md5(old) if os.path.exists(old) else "-"}')
lines += ['', 'Unchanged: ND2DisplaySegments.png, ND2BanksLabel.png, ND2BanksLit.png, nd2-default.rml.',
          'In nd2-default.rcss only the LED and MASTER LEVEL sprite sizes change; every position is the same.',
          'The preview folder is for review only; it does not go into the skin.']
open(os.path.join(OUT, 'README.txt'), 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
