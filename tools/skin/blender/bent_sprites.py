# Renders the bent mode's images, with the background's scene, light and camera (controls left to the plugin):
#   python3 bent_sprites.py --what strip|base|pot|led [--frames A:B] --maps MAPS --hdr HDR --out DIR
#   strip: the panel opened up (board, pot bodies, wiring; the shafts and the LED are the plugin's), band A of B
#          of the rows REGION covers                                   -> DIR/strip_A.png
#   base:  the same rows without the opening, to find what the opening changes (incl. the shafts' shadows)
#                                                                       -> DIR/base_A.png
#   pot:   a pot's shaft, frames A..B-1 of POT_FRAMES, 7 o'clock (off) to 5 o'clock -> DIR/pot_NNN.png
#   led:   the indicator LED, lit and unlit                            -> DIR/led_on.png, DIR/led_off.png
# The renders are generated at run time and never committed.
import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy

import dims as D
import bent_mod
import sprites

REGION_ROWS = 720           # rows 0..719 of the 2x canvas: the opening and the shafts' shadows
POT_FRAMES = 128
POT_BOX = 60                # half-size of a shaft image, px (2x)
LED_BOX = 50
SAMPLE_POT = 2              # the pot whose shaft is rendered (all six share the images)


def band(sc, i, n, path):
    r = sc.render
    H = D.CANVAS_H
    y0, y1 = REGION_ROWS * i // n, REGION_ROWS * (i + 1) // n
    r.border_min_x, r.border_max_x = 0.0, 1.0
    r.border_min_y, r.border_max_y = 1 - y1 / H, 1 - y0 / H
    r.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--what', required=True, choices=('strip', 'base', 'pot', 'led'))
    ap.add_argument('--maps', required=True)
    ap.add_argument('--hdr', required=True)
    ap.add_argument('--samples', type=int, default=64)
    ap.add_argument('--frames', default='')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    t0 = time.time()
    sc = sprites.setup(a)
    sprites.hide_controls()
    if a.what == 'base':
        i, n = (int(v) for v in a.frames.split(':'))
        band(sc, i, n, os.path.join(a.out, f'base_{i}.png'))
        return
    bent_mod.add(a.out)
    shafts = [o for o in bpy.data.objects if o.name.startswith('Pot') and o.name.endswith(('_shaft', '_mark'))]
    led = bpy.data.objects['ModLed']
    if a.what == 'strip':
        for o in shafts + [led]:            # the plugin draws these; their shadows and the LED's glow stay
            o.visible_camera = False
        i, n = (int(v) for v in a.frames.split(':'))
        band(sc, i, n, os.path.join(a.out, f'strip_{i}.png'))
    elif a.what == 'pot':
        lo, hi = (int(v) for v in (a.frames or f'0:{POT_FRAMES}').split(':'))
        cx, cy = bent_mod.MOD['pots_x'][SAMPLE_POT], bent_mod.MOD['pot_y']
        mine = [o for o in shafts if o.name.startswith(f'Pot{SAMPLE_POT}_')]
        sprites.MARGIN = 0
        box = (round(cx) - POT_BOX, round(cy) - POT_BOX, round(cx) + POT_BOX, round(cy) + POT_BOX)
        sweep = bent_mod.MOD['pot_sweep']
        for f in range(lo, hi):
            ang = -sweep / 2 + sweep * f / (POT_FRAMES - 1)
            for o in mine:
                o.rotation_euler = (0, 0, math.radians(90 - ang))
            sprites.render_box(sc, box, os.path.join(a.out, f'pot_{f:03d}.png'))
    else:
        lx, ly = bent_mod.MOD['led']
        sprites.MARGIN = 0
        box = (round(lx) - LED_BOX, round(ly) - LED_BOX, round(lx) + LED_BOX, round(ly) + LED_BOX)
        sprites.render_box(sc, box, os.path.join(a.out, 'led_on.png'))
        b = led.active_material.node_tree.nodes['Principled BSDF']
        b.inputs['Emission Strength'].default_value = 0.0
        sprites.render_box(sc, box, os.path.join(a.out, 'led_off.png'))
    print(f'bent {a.what}: {time.time() - t0:.0f} s')


if __name__ == '__main__':
    main()
