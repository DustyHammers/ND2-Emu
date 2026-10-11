# Sandblasted grain on the panel's button and MASTER LEVEL knob sprites, the value encoder's finish: shape and lighting
# stay as first drawn (skins/default/source/*_plain.png); the surface gets the encoder's fine grain (albedo speckle and a
# little relief lit from the top left). The sprites are taken to 2x first, so the grain is as fine as the encoder's.
#   texture_sprites.py              writes ND2BlackButton.png, ND2GreyButton.png and ND2MasterKnob.png into the skin
#   texture_sprites.py MODE IN OUT [AMP]    one sprite: MODE buttons (2 states stacked) or knob (128 frames, 240 deg)
import os
import sys
import numpy as np
from PIL import Image

AMP = 0.11       # albedo speckle (light surfaces need less: the grain is relative); relief shading = 8 x AMP

def noise(h, w, seed):
    g = np.random.default_rng(seed).standard_normal((h, w)).astype(np.float32)
    for _ in range(2):
        g = (g + np.roll(g, 1, 0) + np.roll(g, -1, 0) + np.roll(g, 1, 1) + np.roll(g, -1, 1)) / 5
    return g / g.std()

def apply(rgba, g, weight):
    rgb = (rgba[..., :3] / 255.0) ** 2.2
    relief = g - 0.5 * (np.roll(g, 1, 0) + np.roll(g, 1, 1))     # light from the top left
    f = 1 + weight * (AMP * g + 0.12 * 8 * AMP * relief)
    rgb = np.clip(rgb * f[..., None], 0, 1) ** (1 / 2.2)
    out = rgba.copy(); out[..., :3] = rgb * 255
    return out

def make(MODE, SRC, DST, amp):
    global AMP
    AMP = amp
    im = Image.open(SRC).convert('RGBA')
    big = np.asarray(im.resize((im.width * 2, im.height * 2), Image.LANCZOS)).astype(np.float32)
    a = big[..., 3] / 255.0
    if MODE == 'buttons':
        g = noise(*big.shape[:2], 21)
        w = np.clip((a - 0.6) / 0.4, 0, 1)
        out = apply(big, g, w)
    else:
        C = 240                                   # 2x cell
        G = noise(1024, 1024, 22)
        out = big.copy()
        yy, xx = np.mgrid[0:C, 0:C] - (C - 1) / 2
        for i in range(128):
            r0, c0 = divmod(i, 14)
            cell = big[r0 * C:(r0 + 1) * C, c0 * C:(c0 + 1) * C]
            phi = np.radians(-120 + 240 * i / 127)            # the grain turns with the knob
            u = xx * np.cos(-phi) - yy * np.sin(-phi); v = xx * np.sin(-phi) + yy * np.cos(-phi)
            g = G[(v + 512).astype(int) % 1024, (u + 512).astype(int) % 1024]
            lum = cell[..., :3].mean(axis=2) / 255.0
            w = np.clip((cell[..., 3] / 255.0 - 0.6) / 0.4, 0, 1) * np.clip((0.55 - lum) / 0.25, 0, 1)   # dark body only, not the pointer
            out[r0 * C:(r0 + 1) * C, c0 * C:(c0 + 1) * C] = apply(cell, g, w)
    Image.fromarray(np.clip(out, 0, 255).round().astype(np.uint8)).save(DST, optimize=True)
    print(MODE, big.shape[1], 'x', big.shape[0], '->', DST)


if __name__ == '__main__':
    if len(sys.argv) > 3:
        make(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 0.11)
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        src = os.path.join(here, '..', '..', 'skins', 'default', 'source')
        dst = os.path.join(here, '..', '..', 'skins', 'default', 'nd2-default')
        make('buttons', os.path.join(src, 'ND2BlackButton_plain.png'), os.path.join(dst, 'ND2BlackButton.png'), 0.12)
        make('buttons', os.path.join(src, 'ND2GreyButton_plain.png'), os.path.join(dst, 'ND2GreyButton.png'), 0.03)
        make('knob', os.path.join(src, 'ND2MasterKnob_plain.png'), os.path.join(dst, 'ND2MasterKnob.png'), 0.11)
