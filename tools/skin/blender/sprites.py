# Renders the skin's control images from the same scene, light and camera as the background (the straight-down
# view), each cut to its control's box in the layout, so they sit on the background without seams:
#   python3 sprites.py --maps MAPS --hdr HDR --out DIR --what bg|buttons|leds|master|encoder [--frames A:B]
#   bg:      the background without the controls (their shadows stay), band A of B (--frames A:B) -> DIR/bg_A.png
#   buttons: black and grey, up and pressed     -> DIR/btn_{black,grey}_{up,down}.png
#   leds:    red and green, unlit and lit       -> DIR/led_{red,green}_{off,on}.png
#   master:  MASTER LEVEL frames 0..127         -> DIR/master_NNN.png
#   encoder: value encoder frames 0..71         -> DIR/enc_NNN.png
# Each image is the control's box at 2x (layout dp x 2) plus MARGIN px on every side, for registration.
# The renders are generated at run time and never committed.
import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Matrix

import dims as D
import lighting
import materials
import scene
import unit

MARGIN = 8
SAMPLE_BUTTON = {'black': 'b_program', 'grey': 'b_param4'}     # copies used for each shared image
SAMPLE_LED = {'red': 'l_29', 'green': 'l_6'}
LIT = {'red': ((1.0, 0.012, 0.006), 8.0), 'green': ((0.10, 1.0, 0.12), 4.0)}   # emission colour, strength


def setup(a, pressed=()):
    scene.build(frame_knob=64, frame_enc=0, pressed=pressed)
    sc = bpy.context.scene
    unit.add_all(bpy.data.collections['Body'], scene.collection('Back panel'), os.path.abspath(a.maps))
    materials.PAINT['virtual_perspective'] = True
    materials.assign_all(sc, os.path.abspath(a.maps), None, None)
    V = lighting.setup(sc, 'H', a.hdr)
    sc.render.engine = 'CYCLES'
    cy = sc.cycles
    cy.device, cy.samples, cy.use_denoising = 'CPU', a.samples, True
    cy.denoiser, cy.denoising_input_passes, cy.denoising_prefilter = 'OPENIMAGEDENOISE', 'RGB_ALBEDO_NORMAL', 'NONE'
    cy.denoising_quality = 'HIGH'
    cy.use_adaptive_sampling, cy.adaptive_threshold = True, 0.02
    cy.max_bounces, cy.diffuse_bounces, cy.glossy_bounces, cy.transmission_bounces = 8, 3, 4, 8
    cy.sample_clamp_indirect = 10
    sc.view_settings.view_transform, sc.view_settings.look = 'Standard', 'None'
    sc.view_settings.exposure = V['exposure_ev']
    st = sc.render.image_settings
    st.file_format, st.color_mode, st.color_depth = 'PNG', 'RGBA', '8'
    sc.render.film_transparent = True
    sc.render.resolution_percentage = 100
    r = sc.render
    r.use_border, r.use_crop_to_border = True, True
    return sc


def hide_controls():
    """The background shows no controls: the plugin draws them. They stay in the scene, unseen by the camera, so
    their shadows and reflections still fall on the panel, and the holes show what is under them."""
    for ob in bpy.data.collections['Controls'].all_objects:
        ob.visible_camera = False


def box_px(cid):
    """The control's box in art px (2x): x0, y0, x1, y1."""
    rects, _ = scene.read_rcss()
    cx, cy, w, h = rects[cid]
    return round(cx - w / 2), round(cy - h / 2), round(cx + w / 2), round(cy + h / 2)


def render_box(sc, box, out):
    x0, y0, x1, y1 = box
    W, H = D.CANVAS_W, D.CANVAS_H
    r = sc.render
    r.border_min_x, r.border_max_x = (x0 - MARGIN) / W, (x1 + MARGIN) / W
    r.border_min_y, r.border_max_y = 1 - (y1 + MARGIN) / H, 1 - (y0 - MARGIN) / H
    r.filepath = out
    bpy.ops.render.render(write_still=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--what', required=True, choices=('bg', 'buttons', 'leds', 'master', 'encoder'))
    ap.add_argument('--maps', required=True)
    ap.add_argument('--hdr', required=True)
    ap.add_argument('--samples', type=int, default=64)
    ap.add_argument('--frames', default='')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    t0 = time.time()
    if a.what == 'bg':
        i, n = (int(v) for v in a.frames.split(':'))
        sc = setup(a)
        hide_controls()
        r = sc.render
        H = D.CANVAS_H
        y0, y1 = H * i // n, H * (i + 1) // n
        r.border_min_x, r.border_max_x = 0.0, 1.0
        r.border_min_y, r.border_max_y = 1 - y1 / H, 1 - y0 / H
        r.filepath = os.path.join(a.out, f'bg_{i}.png')
        bpy.ops.render.render(write_still=True)
    elif a.what == 'buttons':
        for state, pressed in (('up', ()), ('down', tuple(SAMPLE_BUTTON.values()))):
            sc = setup(a, pressed)
            for cls, cid in SAMPLE_BUTTON.items():
                render_box(sc, box_px(cid), os.path.join(a.out, f'btn_{cls}_{state}.png'))
    elif a.what == 'leds':
        sc = setup(a)
        for col, cid in SAMPLE_LED.items():
            render_box(sc, box_px(cid), os.path.join(a.out, f'led_{col}_off.png'))
        for col, cid in SAMPLE_LED.items():
            ob = bpy.data.objects[cid]
            m = ob.active_material.copy()
            b = m.node_tree.nodes.get('Principled BSDF')
            rgb, strength = LIT[col]
            b.inputs['Emission Color'].default_value = (*rgb, 1)
            b.inputs['Emission Strength'].default_value = strength
            ob.data.materials[0] = m
            render_box(sc, box_px(cid), os.path.join(a.out, f'led_{col}_on.png'))
    elif a.what == 'master':
        sc = setup(a)
        K = D.MASTER
        knob = bpy.data.objects['MasterKnob']
        lo, hi = (int(v) for v in (a.frames or f'0:{K["frames"]}').split(':'))
        box = box_px('master')
        for f in range(lo, hi):
            ang = math.radians(-K['sweep'] / 2 + K['sweep'] * f / (K['frames'] - 1))
            knob.rotation_euler = (0.0, 0.0, -ang)
            render_box(sc, box, os.path.join(a.out, f'master_{f:03d}.png'))
    else:
        sc = setup(a)
        E = D.ENCODER
        lo, hi = (int(v) for v in (a.frames or f'0:{E["frames"]}').split(':'))
        rects, kinds = scene.read_rcss()
        cid = next(k for k, v in kinds.items() if v[0] == 'encoder')
        cx, cy, _, _ = rects[cid]
        box = box_px(cid)
        # build once at frame 0 and turn the object about the encoder's centre, so the texture of its sandblasted
        # top turns with it (rebuilding the shape each frame left the grain standing still)
        obs = [bpy.data.objects[n] for n in ('Encoder', 'EncoderCap')]
        for ob in obs:
            ob.data.transform(Matrix.Translation((-cx, cy, 0.0)))
            ob.location = (cx, -cy, 0.0)
        for f in range(lo, hi):
            for ob in obs:
                ob.rotation_euler = (0.0, 0.0, -2 * math.pi * f / E['frames'])    # clockwise seen from above
            render_box(sc, box, os.path.join(a.out, f'enc_{f:03d}.png'))
    print(f'sprites {a.what}: {time.time() - t0:.0f} s')


if __name__ == '__main__':
    main()
