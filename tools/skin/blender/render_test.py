# Renders one test crop of the background (controls hidden from the camera but casting shadows) at 2x, with
# the light passes needed to recalibrate the paint afterwards, as a multilayer EXR.
#   python3 render_test.py --light A --crop params --maps MAPS_DIR [--hdr FILE] [--samples 512] --out OUT_DIR
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy

import dims as D
import lighting
import materials
import scene

# test crops in art px (2x): x0, y0, x1, y1
CROPS = {
    'master': (60, 380, 760, 1000),       # MASTER LEVEL scale, red/black edge, knob shadow
    'top': (0, 0, 1400, 300),             # top edge fold, corner, channel lettering
    'params': (690, 760, 1960, 1300),     # tags, lines, LED holes, button holes and lips
    'display': (690, 400, 2120, 760),     # display window, PROGRAM, CHANNEL SELECT, STORE
    'encoder': (1460, 1300, 2160, 2000),  # encoder area and its printed ring
    'wordmark': (2560, 1720, 3450, 2070), # wordmark, bottom-right corner
    'flat': (0, 1730, 1500, 2070),        # flat red body: calibration and the orange peel strips
}
PEEL_STRIPS = (500, 1000)                 # x in art px: 0.5x | 1x | 2x orange peel (flat crop only)


def background_mode(sc):
    """Controls are sprites: hide them from the camera, keep their shadows and reflections."""
    ctrl = bpy.data.collections['Controls']
    for c in [ctrl] + list(ctrl.children_recursive):
        for ob in c.objects:
            ob.visible_camera = False


def setup_render(sc, samples, crop):
    sc.render.engine = 'CYCLES'
    cy = sc.cycles
    cy.device = 'CPU'
    cy.samples = samples
    cy.use_adaptive_sampling = True
    cy.adaptive_threshold = 0.008
    cy.use_denoising = False
    cy.max_bounces, cy.diffuse_bounces, cy.glossy_bounces, cy.transmission_bounces = 8, 3, 4, 6
    cy.sample_clamp_indirect = 10
    sc.render.film_transparent = True
    sc.render.resolution_percentage = 100
    x0, y0, x1, y1 = crop
    sc.render.use_border, sc.render.use_crop_to_border = True, True
    sc.render.border_min_x, sc.render.border_max_x = x0 / D.CANVAS_W, x1 / D.CANVAS_W
    sc.render.border_min_y, sc.render.border_max_y = 1 - y1 / D.CANVAS_H, 1 - y0 / D.CANVAS_H
    vl = sc.view_layers[0]
    for p in ('diffuse_direct', 'diffuse_indirect', 'diffuse_color', 'glossy_direct', 'glossy_indirect', 'glossy_color',
              'transmission_direct', 'transmission_indirect', 'transmission_color', 'emit', 'environment',
              'material_index'):
        setattr(vl, 'use_pass_' + p, True)
    vl.cycles.denoising_store_passes = True
    st = sc.render.image_settings
    if hasattr(st, 'media_type'):            # Blender 5.x
        st.media_type = 'MULTI_LAYER_IMAGE'      # its only format is OPEN_EXR_MULTILAYER
        st.file_format = 'OPEN_EXR_MULTILAYER'
    else:                                     # Blender 4.2
        st.file_format = 'OPEN_EXR_MULTILAYER'
    st.color_depth = '32'
    st.exr_codec = 'ZIP'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--light', default='A')
    ap.add_argument('--crop', required=True)
    ap.add_argument('--maps', required=True)
    ap.add_argument('--hdr')
    ap.add_argument('--samples', type=int, default=512)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    t0 = time.time()
    scene.build(frame_knob=64, frame_enc=0)
    sc = bpy.context.scene
    mats = materials.assign_all(sc, os.path.abspath(a.maps), PEEL_STRIPS if a.crop == 'flat' else None)
    mats['paint'].pass_index = 1
    V = lighting.setup(sc, a.light, a.hdr)
    background_mode(sc)
    crop = CROPS[a.crop]
    setup_render(sc, a.samples, crop)
    name = f'{a.light}_{a.crop}'
    sc.render.filepath = os.path.join(a.out, name + '.exr')
    t1 = time.time()
    bpy.ops.render.render(write_still=True)
    t2 = time.time()
    mp = (crop[2] - crop[0]) * (crop[3] - crop[1]) / 1e6
    info = dict(light=a.light, crop=a.crop, box=crop, samples=a.samples, adaptive=sc.cycles.adaptive_threshold,
                hdr=bool(a.hdr and os.path.exists(a.hdr)), build_s=round(t1 - t0, 1), render_s=round(t2 - t1, 1),
                megapixels=round(mp, 3), s_per_mp_per_sample=round((t2 - t1) / mp / a.samples, 4),
                threads=sc.render.threads, blender=bpy.app.version_string, variant=V,
                ink_albedo=materials.INK_ALBEDO, paint=materials.PAINT)
    json.dump(info, open(os.path.join(a.out, name + '.json'), 'w'), indent=1)
    print('render', name, info['render_s'], 's')


if __name__ == '__main__':
    main()
