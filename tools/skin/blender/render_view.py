# Renders the whole unit with every control in place, for judging realism:
#   --view hero : a perspective three-quarter view from the front left, like the reference photos
#   --view top  : the skin's straight-down orthographic view (whole canvas)
#   --view persp: the skin canvas as a true perspective view from the front, like the owner's product photo
#                 (--elev degrees above the panel, --dist mm from the panel centre), fitted to the window
#   --view tilt : the skin canvas seen with the top tilted back (--tilt degrees from straight down), orthographic
#                 so every copy of a control looks the same, framed so the whole red top fits the plugin window
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


def tilt_camera(sc, tilt_deg=20.0, margin=0.05):
    """Orthographic camera tilted toward the front by tilt_deg, fitted to the whole red top (front face, sides,
    back edge) inside the canvas aspect, with a margin. Returns the projection used, for placing controls."""
    a = math.radians(tilt_deg)
    view = Vector((0.0, math.sin(a), -math.cos(a)))  # looking back and down
    right = Vector((1.0, 0.0, 0.0))
    up = (-view).cross(right).normalized()           # toward the back of the panel, so the front is at the bottom
    ob = bpy.data.objects['Plate']
    ev = ob.evaluated_get(bpy.context.evaluated_depsgraph_get())
    pts = [ob.matrix_world @ v.co for v in ev.data.vertices]
    xs = [p.dot(right) for p in pts]
    ys = [p.dot(up) for p in pts]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    aspect = D.CANVAS_W / D.CANVAS_H
    scale = max(w, h * aspect) * (1 + 2 * margin)
    cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    centre = right * cx + up * cy
    cam = bpy.data.cameras.new('Tilt')
    cam.type, cam.ortho_scale, cam.sensor_fit = 'ORTHO', scale, 'HORIZONTAL'
    cam.clip_start, cam.clip_end = 1, 100000
    o = bpy.data.objects.new('TiltCam', cam)
    sc.collection.objects.link(o)
    m = Matrix((right, up, -view)).transposed().to_4x4()
    m.translation = centre - view * 20000
    o.matrix_world = m
    sc.camera = o
    sc.render.resolution_x, sc.render.resolution_y = D.CANVAS_W, D.CANVAS_H
    k = D.CANVAS_W / scale                           # canvas px per panel px
    print(f'tilt {tilt_deg} deg: panel shown at {100 * k:.1f} % across, {100 * k * math.cos(a):.1f} % front to back')
    return dict(scale=scale, centre=(cx, cy), k=k)


def persp_camera(sc, elev_deg=50.0, dist_mm=580.0, margin=0.045):
    """Perspective camera in front of the panel, elev_deg above its plane, aimed at the panel centre; the focal
    length and lens shift fit the whole red top into the canvas with a margin."""
    from bpy_extras.object_utils import world_to_camera_view
    e = math.radians(elev_deg)
    centre = Vector((D.CANVAS_W / 2, -D.CANVAS_H / 2, -40.0))
    pos = centre + Vector((0.0, -math.cos(e), math.sin(e))) * dist_mm * D.MM
    cam = bpy.data.cameras.new('Persp')
    cam.lens, cam.sensor_width, cam.sensor_fit = 50.0, 36.0, 'HORIZONTAL'
    cam.clip_start, cam.clip_end = 10, 200000
    o = bpy.data.objects.new('PerspCam', cam)
    sc.collection.objects.link(o)
    f = (centre - pos).normalized()
    r = f.cross(Vector((0, 0, 1))).normalized()
    u = r.cross(f)
    m = Matrix((r, u, -f)).transposed().to_4x4()
    m.translation = pos
    o.matrix_world = m
    sc.camera = o
    sc.render.resolution_x, sc.render.resolution_y = D.CANVAS_W, D.CANVAS_H
    ob = bpy.data.objects['Plate']
    ev = ob.evaluated_get(bpy.context.evaluated_depsgraph_get())
    pts = [ob.matrix_world @ v.co for v in ev.data.vertices]
    for _ in range(3):
        bpy.context.view_layer.update()
        nd = [world_to_camera_view(sc, o, p) for p in pts]
        xs, ys = [p.x for p in nd], [p.y for p in nd]
        wf, hf = max(xs) - min(xs), max(ys) - min(ys)
        cam.lens *= min((1 - 2 * margin) / wf, (1 - 2 * margin) / hf)
        bpy.context.view_layer.update()
        nd = [world_to_camera_view(sc, o, p) for p in pts]
        xs, ys = [p.x for p in nd], [p.y for p in nd]
        cam.shift_x += ((max(xs) + min(xs)) / 2 - 0.5)
        cam.shift_y += ((max(ys) + min(ys)) / 2 - 0.5) * D.CANVAS_H / D.CANVAS_W
    bpy.context.view_layer.update()
    nd = [world_to_camera_view(sc, o, p) for p in pts]
    back = [p for p, q in zip(nd, pts) if q.y > -5 and q.z > -1]
    front = [p for p, q in zip(nd, pts) if q.y < -D.CANVAS_H + 120 and q.z > -1]
    if back and front:
        bw = max(p.x for p in back) - min(p.x for p in back)
        fw = max(p.x for p in front) - min(p.x for p in front)
        print(f'persp: elev {elev_deg}, dist {dist_mm} mm, lens {cam.lens:.1f} mm; back edge {bw / fw:.3f} of the front')


def shadow_floor(sc):
    """A shadow catcher under the unit, so the plugin window colour shows around it with a soft contact shadow."""
    ob = unit.backdrop(scene.collection('Studio'))
    ob.is_shadow_catcher = True
    return ob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--view', default='hero', choices=('hero', 'top', 'tilt', 'persp'))
    ap.add_argument('--elev', type=float, default=50.0)
    ap.add_argument('--dist', type=float, default=640.0)
    ap.add_argument('--tilt', type=float, default=20.0)
    ap.add_argument('--res', type=int, default=50, help='percent of the 2x canvas (50 = 1x)')
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
    elif a.view == 'tilt' and os.environ.get('FLOOR_SHADOW'):
        shadow_floor(sc)
    mats = materials.assign_all(sc, os.path.abspath(a.maps), None, emission)
    V = lighting.setup(sc, a.light, a.hdr)
    sc.render.engine = 'CYCLES'
    cy = sc.cycles
    cy.device, cy.samples, cy.use_denoising = 'CPU', a.samples, True
    cy.use_adaptive_sampling, cy.adaptive_threshold = True, 0.02
    cy.max_bounces, cy.diffuse_bounces, cy.glossy_bounces, cy.transmission_bounces = 8, 3, 4, 8
    cy.sample_clamp_indirect = 10
    sc.view_settings.view_transform, sc.view_settings.look = 'Standard', 'None'
    sc.view_settings.exposure = V.get('exposure_ev', EXPOSURE_EV)
    st = sc.render.image_settings
    st.file_format, st.color_mode, st.color_depth = 'PNG', 'RGB' if a.view == 'hero' else 'RGBA', '8'
    if a.view == 'hero':
        hero_camera(sc)
        sc.render.film_transparent = False
    elif a.view == 'tilt':
        tilt_camera(sc, a.tilt)
        sc.render.film_transparent = True
        sc.render.resolution_percentage = a.res
    elif a.view == 'persp':
        persp_camera(sc, a.elev, a.dist)
        sc.render.film_transparent = True
        sc.render.resolution_percentage = a.res
    else:
        sc.render.film_transparent = True
        sc.render.resolution_percentage = 50          # 1x: 1725 x 1035
    sc.render.filepath = a.out
    bpy.ops.render.render(write_still=True)
    print(f'{a.view}: {time.time() - t0:.0f} s')


if __name__ == '__main__':
    main()
