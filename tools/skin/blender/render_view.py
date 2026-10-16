# Renders the whole unit with every control in place, for judging realism:
#   --view hero : a perspective three-quarter view from the front left, like the reference photos
#   --view top  : the skin's straight-down orthographic view (whole canvas)
#   python3 render_view.py --view hero --maps MAPS_DIR --hdr FILE --samples 96 --out FILE.png [--light C]
import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Matrix, Vector

import dims as D
import lighting
import materials
import scene
import unit

EXPOSURE_EV = 0.717          # from the calibration (lighting C, Standard view)


def hero_camera(sc, az=32.0, el=34.0, focal=70.0, fill=0.66, w=1600, h=1000):
    U = scene.up_vector()
    F = Vector((0, -U.z, U.y))                         # toward the viewer, horizontal in the world
    X = Vector((1, 0, 0))
    target = Vector((D.CANVAS_W / 2, -D.CANVAS_H / 2, 0)) - U * (scene.wall_depth(-D.CANVAS_H / 2) * 0.45)
    cam = bpy.data.cameras.new('Hero')
    cam.lens, cam.sensor_width, cam.sensor_fit = focal, 36.0, 'HORIZONTAL'
    cam.clip_start, cam.clip_end = 10, 200000
    hfov = 2 * math.atan(18.0 / focal)
    dist = (D.CANVAS_W * 1.05 / fill) / (2 * math.tan(hfov / 2))
    a, e = math.radians(az), math.radians(el)
    d = (F * math.cos(a) - X * math.sin(a)) * math.cos(e) + U * math.sin(e)
    pos = target + d * dist
    f = (target - pos).normalized()
    r = f.cross(U).normalized()
    u = r.cross(f)
    m = Matrix((r, u, -f)).transposed().to_4x4()
    m.translation = pos
    ob = bpy.data.objects.new('HeroCam', cam)
    sc.collection.objects.link(ob)
    ob.matrix_world = m
    sc.camera = ob
    sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = w, h, 100


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--view', default='hero', choices=('hero', 'top'))
    ap.add_argument('--light', default='C')
    ap.add_argument('--maps', required=True)
    ap.add_argument('--hdr')
    ap.add_argument('--samples', type=int, default=96)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    t0 = time.time()
    scene.build(frame_knob=64, frame_enc=0)
    sc = bpy.context.scene
    c_case = bpy.data.collections['Body']
    c_back = scene.collection('Back panel')
    emission = unit.add_all(c_case, c_back, os.path.abspath(a.maps))
    if a.view == 'hero':
        unit.backdrop(scene.collection('Studio'))
    mats = materials.assign_all(sc, os.path.abspath(a.maps), None, emission)
    lighting.setup(sc, a.light, a.hdr)
    sc.render.engine = 'CYCLES'
    cy = sc.cycles
    cy.device, cy.samples, cy.use_denoising = 'CPU', a.samples, True
    cy.use_adaptive_sampling, cy.adaptive_threshold = True, 0.02
    cy.max_bounces, cy.diffuse_bounces, cy.glossy_bounces, cy.transmission_bounces = 8, 3, 4, 8
    cy.sample_clamp_indirect = 10
    sc.view_settings.view_transform, sc.view_settings.look = 'Standard', 'None'
    sc.view_settings.exposure = EXPOSURE_EV
    st = sc.render.image_settings
    st.file_format, st.color_mode, st.color_depth = 'PNG', 'RGBA' if a.view == 'top' else 'RGB', '8'
    if a.view == 'hero':
        hero_camera(sc)
        sc.render.film_transparent = False
    else:
        sc.render.film_transparent = True
        sc.render.resolution_percentage = 50          # 1x: 1725 x 1035
    sc.render.filepath = a.out
    bpy.ops.render.render(write_still=True)
    print(f'{a.view}: {time.time() - t0:.0f} s')


if __name__ == '__main__':
    main()
