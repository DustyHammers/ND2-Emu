# Builds the photoreal skin scene in Blender (run with Blender's Python or the `bpy` module).
#   1 Blender unit = 1 art px (2x art). Art (x, y) -> Blender (x, -y). The panel surface is z = 0.
# Control positions come from the skin's rcss, so the scene always matches the plugin's layout.
import math
import os
import re
import zlib

import bmesh
import bpy

import dims as D
from layout import read_rcss_plain



# ---- layout -------------------------------------------------------------------------------------------------

def read_rcss():
    return read_rcss_plain()


def hash01(key):
    return (zlib.crc32(key.encode()) & 0xffff) / 0xffff


# ---- 2D outlines (lists of (x, y) in art px, counter-clockwise in Blender) -----------------------------------

def rounded_rect(cx, cy, w, h, r, k=12, m=6):
    """Fixed topology: 4 corner arcs of k points and m points on each straight side (same N for any size)."""
    r = min(r, w / 2, h / 2)
    a, b = w / 2 - r, h / 2 - r
    pts = []
    corners = [(a, b, 0), (-a, b, 90), (-a, -b, 180), (a, -b, 270)]
    for i, (ox, oy, start) in enumerate(corners):
        for j in range(k):
            t = math.radians(start + 90 * j / (k - 1))
            pts.append((ox + r * math.cos(t), oy + r * math.sin(t)))
        nx, ny, _ = corners[(i + 1) % 4]
        ex, ey = ox + r * math.cos(math.radians(start + 90)), oy + r * math.sin(math.radians(start + 90))
        fx, fy = nx + r * math.cos(math.radians(start + 90)), ny + r * math.sin(math.radians(start + 90))
        for j in range(1, m + 1):
            t = j / (m + 1)
            pts.append((ex + (fx - ex) * t, ey + (fy - ey) * t))
    return [(cx + x, cy + y) for x, y in pts]


def circle(cx, cy, r, n=64):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


# ---- mesh helpers -------------------------------------------------------------------------------------------

def new_object(name, bm, collection, smooth=True):
    me = bpy.data.meshes.new(name)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    collection.objects.link(ob)
    return ob


def prism(bm, outline, z0, z1):
    """Closed prism from an art-space outline (art y down -> Blender -y)."""
    bot = [bm.verts.new((x, -y, z0)) for x, y in outline]
    top = [bm.verts.new((x, -y, z1)) for x, y in outline]
    n = len(outline)
    bm.faces.new(top)
    bm.faces.new(list(reversed(bot)))
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))


def loft(bm, rings, cap_top=True, cap_bottom=True):
    """rings: list of (outline, z) with the same point count, bottom to top. Top capped with a centre fan."""
    vs = [[bm.verts.new((x, -y, z)) for x, y in pts] for pts, z in rings]
    n = len(vs[0])
    for a, b in zip(vs, vs[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    if cap_top:
        pts, z = rings[-1]
        c = bm.verts.new((sum(p[0] for p in pts) / n, -sum(p[1] for p in pts) / n, z))
        for i in range(n):
            bm.faces.new((vs[-1][i], vs[-1][(i + 1) % n], c))
    if cap_bottom:
        bm.faces.new(list(reversed(vs[0])))
    return vs


def material(name, rgb, rough=0.6):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*rgb, 1)
    bsdf.inputs['Roughness'].default_value = rough
    return m


def collection(name, parent=None):
    c = bpy.data.collections.new(name)
    (parent or bpy.context.scene.collection).children.link(c)
    return c


# ---- parts --------------------------------------------------------------------------------------------------

def build_plate(rects, kinds, col):
    P, B, L = D.PLATE, D.BUTTON, D.LED
    bm = bmesh.new()
    prism(bm, rounded_rect(D.CANVAS_W / 2, D.CANVAS_H / 2, D.CANVAS_W, D.CANVAS_H, P['corner_r'], k=8, m=40),
          -P['thickness'], 0)
    plate = new_object('Plate', bm, col, smooth=False)

    cutters = bpy.data.collections.new('Cutters')
    bpy.context.scene.collection.children.link(cutters)
    holes = []                                       # (id, outline) for the fillet weights
    for cid, (cx, cy, w, h) in rects.items():
        kind = kinds.get(cid, ('', ''))[0]
        if kind == 'pbutton':
            out = rounded_rect(cx, cy, B['hole_w'], B['hole_h'], B['hole_r'], k=10, m=8)
        elif kind == 'led':
            out = circle(cx, cy, L['hole_r'], 48)
        elif kind == 'knob':
            out = circle(cx, cy, 40.0, 48)
        elif kind == 'encoder':
            out = circle(cx, cy, D.ENCODER['hole_r'], 48)
        else:
            continue
        holes.append((cid, cx, cy))
        b2 = bmesh.new()
        prism(b2, out, -P['thickness'] - 10, 10)
        new_object('cut_' + cid, b2, cutters, smooth=False)
    d = D.DISPLAY
    b2 = bmesh.new()
    prism(b2, rounded_rect((d['x0'] + d['x1']) / 2, (d['y0'] + d['y1']) / 2, d['x1'] - d['x0'], d['y1'] - d['y0'], d['r']),
          -P['thickness'] - 10, 10)
    new_object('cut_display', b2, cutters, smooth=False)
    holes.append(('display', (d['x0'] + d['x1']) / 2, (d['y0'] + d['y1']) / 2))

    mod = plate.modifiers.new('holes', 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.operand_type = 'COLLECTION'
    mod.collection = cutters
    mod.solver = 'EXACT'
    bpy.context.view_layer.objects.active = plate
    bpy.ops.object.modifier_apply(modifier='holes')
    for ob in list(cutters.objects):
        bpy.data.objects.remove(ob)
    bpy.data.collections.remove(cutters)

    # bevel weights on the top rim edges only (top face meets a wall): each hole 0.75..1.0 (hash of its id)
    # x hole_fillet; the outer edge gets edge_fillet
    me = plate.data
    wmax = max(P['hole_fillet'], P['edge_fillet'])
    bm = bmesh.new()
    bm.from_mesh(me)
    bw = bm.edges.layers.float.get('bevel_weight_edge') or bm.edges.layers.float.new('bevel_weight_edge')
    for e in bm.edges:
        v0, v1 = e.verts[0].co, e.verts[1].co
        if abs(v0.z) > 0.01 or abs(v1.z) > 0.01 or len(e.link_faces) != 2:
            continue
        if all(abs(f.normal.z) > 0.99 for f in e.link_faces):
            continue                                 # a split inside the flat top, not a rim
        mx, my = (v0.x + v1.x) / 2, -(v0.y + v1.y) / 2
        if min(mx, my, D.CANVAS_W - mx, D.CANVAS_H - my) < P['corner_r'] + 2:
            e[bw] = P['edge_fillet'] / wmax
            continue
        cid = min(holes, key=lambda h: (h[1] - mx) ** 2 + (h[2] - my) ** 2)[0]
        e[bw] = P['hole_fillet'] * (0.75 + 0.25 * hash01(cid)) / wmax
    bm.to_mesh(me)
    bm.free()
    bev = plate.modifiers.new('fillets', 'BEVEL')
    bev.limit_method = 'WEIGHT'
    bev.width = wmax
    bev.segments = 5
    bev.profile = 0.5
    bev.use_clamp_overlap = True
    bev.harden_normals = True                        # keeps the flat top flat next to the round fillets
    for p in me.polygons:
        p.use_smooth = True
    for o in bpy.context.selected_objects:
        o.select_set(False)
    plate.select_set(True)
    bpy.context.view_layer.objects.active = plate
    if bpy.app.version >= (4, 1, 0):
        bpy.ops.object.shade_auto_smooth(angle=math.radians(35))
    else:
        me.use_auto_smooth = True
        me.auto_smooth_angle = math.radians(35)
    return plate


def build_floor(col):
    bm = bmesh.new()
    prism(bm, rounded_rect(D.CANVAS_W / 2, D.CANVAS_H / 2, D.CANVAS_W - 20, D.CANVAS_H - 20, 10, k=4, m=2),
          D.PLATE['floor_z'] - 5, D.PLATE['floor_z'])
    return new_object('Interior', bm, col, smooth=False)


def button(name, cx, cy, col, pressed=False):
    B = D.BUTTON
    dz = -B['travel'] if pressed else 0.0
    k, m = 14, 10
    rings = []
    z_sh = B['top_z'] - B['shoulder']
    wall = [(B['bottom_z'], 0.0), (z_sh * 0.5, 0.5), (z_sh, 1.0)]
    for z, t in wall:
        inset = B['draft'] * t
        rings.append((rounded_rect(cx, cy, B['cap_w'] - 2 * inset, B['cap_h'] - 2 * inset, B['cap_r'], k, m), z + dz))
    w0, h0, r0 = B['cap_w'] - 2 * B['draft'], B['cap_h'] - 2 * B['draft'], B['cap_r']
    # shoulder profile (horizontal progress u toward the pill, rise v): a rounded top edge on the wall, a gently
    # convex slope, then a small round onto the flat top face, so the face reads as a distinct pill (photo)
    profile = [(0.01, 0.18), (0.04, 0.32), (0.09, 0.42), (0.16, 0.49), (0.30, 0.60), (0.50, 0.74), (0.70, 0.86),
               (0.86, 0.94), (0.93, 0.975), (0.97, 0.993), (1.0, 1.0)]
    for u, v in profile:
        w = w0 + (B['pill_w'] - w0) * u
        h = h0 + (B['pill_h'] - h0) * u
        r = r0 + (B['pill_h'] * 0.49 - r0) * u      # just under h/2: a true stadium has a zero-length side
        rings.append((rounded_rect(cx, cy, w, h, r, k, m), z_sh + B['shoulder'] * v + dz))
    for i in range(1, 6):                            # crowned top face
        s = 1 - i / 6
        pts = rounded_rect(cx, cy, B['pill_w'] * s + 1e-3, B['pill_h'] * s + 1e-3, B['pill_h'] * s * 0.49, k, m)
        rings.append((pts, B['top_z'] + B['crown'] * (1 - s * s) + dz))
    bm = bmesh.new()
    loft(bm, rings, cap_top=True, cap_bottom=True)
    return new_object(name, bm, col)


def led(name, cx, cy, col):
    L = D.LED
    bm = bmesh.new()
    rings = [(circle(cx, cy, L['lens_r'], 48), L['body_bottom']), (circle(cx, cy, L['lens_r'], 48), -D.PLATE['thickness'] * 0.6)]
    top0 = -D.PLATE['thickness'] * 0.6
    for i in range(1, 9):
        th = (math.pi / 2) * i / 9
        rings.append((circle(cx, cy, L['lens_r'] * math.cos(th), 48), top0 + (L['dome_z'] - top0) * math.sin(th)))
    loft(bm, rings)
    lens = new_object(name, bm, col)
    bm = bmesh.new()                                 # die and reflector cup (seen through the lens later)
    loft(bm, [(circle(cx, cy, 6, 24), -46), (circle(cx, cy, 15, 24), -30)], cap_top=False)
    prism(bm, rounded_rect(cx, cy, 6, 6, 0.5, k=2, m=1), -45, -41)
    new_object(name + '_die', bm, col, smooth=False)
    return lens


def display(col):
    d = D.DISPLAY
    cx, cy = (d['x0'] + d['x1']) / 2, (d['y0'] + d['y1']) / 2
    bm = bmesh.new()
    prism(bm, rounded_rect(cx, cy, d['x1'] - d['x0'] + 30, d['y1'] - d['y0'] + 30, d['r']), d['lens_z'] - 3, d['lens_z'])
    lens = new_object('DisplayLens', bm, col, smooth=False)
    bm = bmesh.new()
    prism(bm, rounded_rect(cx, cy, d['x1'] - d['x0'] + 40, d['y1'] - d['y0'] + 40, d['r']), d['board_z'] - 3, d['board_z'])
    new_object('DisplayBoard', bm, col, smooth=False)
    return lens


def master_knob(cx, cy, col, frame=64):
    K = D.MASTER
    n = 240
    def outline(r):
        return [(cx + (r - K['flute_depth'] * (0.5 + 0.5 * math.cos(K['flutes'] * 2 * math.pi * i / n))) * math.cos(2 * math.pi * i / n),
                 cy + (r - K['flute_depth'] * (0.5 + 0.5 * math.cos(K['flutes'] * 2 * math.pi * i / n))) * math.sin(2 * math.pi * i / n))
                for i in range(n)]
    H, tr = K['height'], K['top_round']
    rings = [(outline(K['r']), 0.0), (outline(K['r']), H - tr)]
    for i in range(1, 7):
        th = (math.pi / 2) * i / 6
        rings.append((outline(K['r'] - tr * (1 - math.cos(th))), H - tr + tr * math.sin(th)))
    for s in (0.85, 0.6, 0.3):                       # very slightly domed top
        rings.append(([(cx + (x - cx) * s, cy + (y - cy) * s) for x, y in outline(K['r'] - tr)], H + 1.5 * (1 - s * s)))
    bm = bmesh.new()
    loft(bm, rings)
    body = new_object('MasterKnob', bm, col)
    # pointer: a thin raised line on the top, rotated by the frame
    ang = math.radians(-K['sweep'] / 2 + K['sweep'] * frame / (K['frames'] - 1))
    ux, uy = math.sin(ang), -math.cos(ang)           # art space (y down); 0 = up
    px, py = -uy, ux
    r0, r1, hw = K['pointer_r0'], K['pointer_r1'], K['pointer_w'] / 2
    quad = [(cx + ux * r0 + px * hw, cy + uy * r0 + py * hw), (cx + ux * r1 + px * hw, cy + uy * r1 + py * hw),
            (cx + ux * r1 - px * hw, cy + uy * r1 - py * hw), (cx + ux * r0 - px * hw, cy + uy * r0 - py * hw)]
    if (quad[1][0] - quad[0][0]) * (quad[2][1] - quad[1][1]) - (quad[1][1] - quad[0][1]) * (quad[2][0] - quad[1][0]) > 0:
        quad.reverse()
    bm = bmesh.new()
    prism(bm, quad, H - 1, H + 2.2)
    pointer = new_object('MasterPointer', bm, col, smooth=False)
    return body, pointer


def encoder(cx, cy, col, frame=0):
    E = D.ENCODER
    F, n_per = E['facets'], 16
    phi = 2 * math.pi * frame / E['frames']
    def outline(r_corner, groove_depth):
        pts = []
        seg = 2 * math.pi / F
        for f in range(F):
            for j in range(n_per):
                t = (j / n_per - 0.5) * seg       # angle from the facet centre
                rr = r_corner * math.cos(seg / 2) / math.cos(t)
                g = abs(t) / (seg / 2)               # 1 at the facet corner
                if g > 1 - E['groove']:
                    q = (g - (1 - E['groove'])) / E['groove']
                    rr -= groove_depth * math.sin(q * math.pi / 2)
                a = f * seg + t + phi
                pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        return pts
    H, ch = E['height'], E['chamfer']
    rings = [(outline(E['r_base'], E['groove_depth']), 0.0)]
    for i in range(1, 6):
        t = i / 6
        rings.append((outline(E['r_base'] + (E['r_top'] - E['r_base']) * t, E['groove_depth']), (H - ch) * t))
    rings.append((outline(E['r_top'], E['groove_depth']), H - ch))
    for i in range(1, 5):                            # rounded chamfer; the grooves run out on it
        th = (math.pi / 2) * i / 4
        rings.append((outline(E['r_top'] - ch * (1 - math.cos(th)), E['groove_depth'] * (1 - i / 4)), H - ch + ch * math.sin(th)))
    bm = bmesh.new()
    loft(bm, rings)
    return new_object('Encoder', bm, col)


# ---- scene --------------------------------------------------------------------------------------------------

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def camera(res_scale=1.0):
    sc = bpy.context.scene
    cam = bpy.data.cameras.new('Ortho')
    cam.type = 'ORTHO'
    cam.ortho_scale = D.CANVAS_W
    cam.sensor_fit = 'HORIZONTAL'
    cam.clip_start, cam.clip_end = 1, 20000
    ob = bpy.data.objects.new('Camera', cam)
    sc.collection.objects.link(ob)
    ob.location = (D.CANVAS_W / 2, -D.CANVAS_H / 2, 8000)
    sc.camera = ob
    sc.render.resolution_x, sc.render.resolution_y = D.CANVAS_W, D.CANVAS_H
    sc.render.resolution_percentage = int(round(100 * res_scale))
    return ob


def build(frame_knob=64, frame_enc=0, pressed=()):
    reset()
    rects, kinds = read_rcss()
    root = bpy.context.scene.collection
    c_body = collection('Body')
    c_ctrl = collection('Controls')
    c_btn_b, c_btn_g = collection('Buttons black', c_ctrl), collection('Buttons grey', c_ctrl)
    c_led = collection('LEDs', c_ctrl)
    c_knob, c_enc = collection('Master knob', c_ctrl), collection('Encoder', c_ctrl)
    c_disp = collection('Display')
    build_plate(rects, kinds, c_body)
    build_floor(c_body)
    display(c_disp)
    for cid, (cx, cy, w, h) in rects.items():
        kind, cls = kinds.get(cid, ('', ''))
        if kind == 'pbutton':
            button(cid, cx, cy, c_btn_b if cls == 'black' else c_btn_g, cid in pressed)
        elif kind == 'led':
            led(cid, cx, cy, c_led)
        elif kind == 'knob':
            master_knob(cx, cy, c_knob, frame_knob)
        elif kind == 'encoder':
            encoder(cx, cy, c_enc, frame_enc)
    camera()
    return rects, kinds
