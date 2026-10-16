# Builds the photoreal skin scene in Blender (run with Blender's Python or the `bpy` module).
#   1 Blender unit = 1 art px (2x art). Art (x, y) -> Blender (x, -y). The panel surface is z = 0.
# Control positions come from the skin's rcss, so the scene always matches the plugin's layout.
import math
import os
import re
import zlib

import bmesh
import bpy
from mathutils import Matrix, Vector

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

def up_vector():
    """The world vertical in panel coordinates: the top slopes down toward the front (art y = canvas height)."""
    C = D.CASE
    s = (C['back_h'] - C['front_h']) / D.CANVAS_H
    c = math.sqrt(1 - s * s)
    return Vector((0.0, s, c))


def wall_depth(y_blender):
    """How far below the top edge (along the world vertical) the base is, at panel y (Blender, 0 .. -H)."""
    C = D.CASE
    return C['back_h'] + y_blender * up_vector().y


def case_shell(bm, inner=False):
    """The chassis envelope as a solid: the top outline swept down the world vertical to the base. inner=True
    gives the cavity (inset by the sheet thickness, from just under the top to below the base)."""
    P = D.PLATE
    t = P['thickness']
    U = up_vector()
    rb = D.CASE['corner_back_r']                     # back corners rounded like the red top's
    if inner:
        out = rounded_rect_fb(t, t, D.CANVAS_W - t, D.CANVAS_H - t, rb - t, 2.0, k=8, m=10)
        z0, extra = -t, 50.0
    else:
        out = rounded_rect_fb(0.0, 0.0, D.CANVAS_W, D.CANVAS_H, rb, 1.0, k=8, m=10)
        z0, extra = 0.0, 0.0
    top = [bm.verts.new((x, -y, z0)) for x, y in out]
    bot = [bm.verts.new(v.co - U * (wall_depth(v.co.y) + extra)) for v in top]
    n = len(top)
    bm.faces.new(top)
    bm.faces.new(list(reversed(bot)))
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))


def cover_section():
    """The red top's cross-section in (y, z) (Blender y: 0 at the back edge, -H at the front): outer path from
    the back edge over the top, the rolled front bend and down the short lip, with the inward normal at each
    point (the inner surface is the outer one moved by the sheet thickness)."""
    C, P = D.CASE, D.PLATE
    t, H = P['thickness'], D.CANVAS_H
    U = up_vector()
    s, c = U.y, U.z
    R = C['front_bend_r']
    t2 = Vector((-s, -c))                            # down the front face (the world vertical)
    w = Vector((c, -s))                              # inward along the base, toward the back
    alpha = math.acos(s)                             # how far the bend turns (90 degrees less the slope)
    d = R * math.tan(alpha / 2)
    pc = Vector((-H, 0.0))                           # the sharp corner the bend rounds off
    path = [(Vector((0.0, 0.0)), Vector((0.0, -1.0))), (Vector((-H + d, 0.0)), Vector((0.0, -1.0)))]
    cc = Vector((-H + d, -R))
    n = 14
    for i in range(1, n + 1):
        b = alpha * i / n
        v = Vector((-math.sin(b), math.cos(b)))
        path.append((cc + v * R, -v))
    lip_end = pc + t2 * C['lip_h']                   # the lip's bottom edge (on the front-face plane)
    path.append((lip_end, w))
    return path, t


def cover_mesh(bm):
    path, t = cover_section()
    W = D.CANVAS_W
    ring = []
    for p, nrm in path:
        o, i = p, p + nrm * t
        ring.append([bm.verts.new((0.0, o.x, o.y)), bm.verts.new((W, o.x, o.y)),
                     bm.verts.new((0.0, i.x, i.y)), bm.verts.new((W, i.x, i.y))])
    for a, b in zip(ring, ring[1:]):
        bm.faces.new((a[0], b[0], b[1], a[1]))       # outer surface
        bm.faces.new((a[3], b[3], b[2], a[2]))       # inner surface
        bm.faces.new((a[0], a[2], b[2], b[0]))       # cut edge at x = 0
        bm.faces.new((a[1], b[1], b[3], a[3]))       # cut edge at x = W
    for r in (ring[0], ring[-1]):                    # the back edge and the lip's bottom edge
        bm.faces.new((r[0], r[1], r[3], r[2]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def build_chassis(col):
    """The black sides, back and lower front: the swept case envelope, hollowed, minus everything outside the
    red top's inner surface down to the lip's bottom edge (the chassis tucks up behind the top and its lip)."""
    U = up_vector()
    bm = bmesh.new()
    case_shell(bm)
    ch = new_object('Chassis', bm, col, smooth=False)
    cav = bmesh.new()
    case_shell(cav, inner=True)
    cavity = new_object('chassis_cavity', cav, col, smooth=False)
    path, t = cover_section()
    inner = [p + nrm * t for p, nrm in path]
    out_last = path[-1][0]
    n_out = Vector((-U.z, U.y))                      # out of the front, horizontal in the world
    far = 6000.0
    poly = inner + [out_last + n_out * far, Vector((-D.CANVAS_H - far, far)), Vector((far, far)), Vector((far, -t))]
    env = bmesh.new()
    lo = [env.verts.new((-200.0, p.x, p.y)) for p in poly]
    hi = [env.verts.new((D.CANVAS_W + 200.0, p.x, p.y)) for p in poly]
    env.faces.new(lo)
    env.faces.new(list(reversed(hi)))
    for i in range(len(poly)):
        j = (i + 1) % len(poly)
        env.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(env, faces=env.faces[:])
    envelope = new_object('cover_envelope', env, col, smooth=False)
    for other in (cavity, envelope):
        m = ch.modifiers.new('cut', 'BOOLEAN')
        m.operation, m.object, m.solver = 'DIFFERENCE', other, 'EXACT'
        bpy.context.view_layer.objects.active = ch
        bpy.ops.object.modifier_apply(modifier=m.name)
        bpy.data.objects.remove(other)
    return ch


def rounded_rect_fb(x0, y0, x1, y1, r_back, r_front, k=16, m=40):
    """Art-space outline with different plan radii at the back corners (y0, top of the canvas) and the front."""
    pts = []
    corners = [(x1 - r_front, y1 - r_front, r_front, 0), (x0 + r_front, y1 - r_front, r_front, 90),
               (x0 + r_back, y0 + r_back, r_back, 180), (x1 - r_back, y0 + r_back, r_back, 270)]
    for i, (cx, cy, r, start) in enumerate(corners):
        for j in range(k):
            t = math.radians(start + 90 * j / (k - 1))
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
        nx, ny, nr, ns = corners[(i + 1) % 4]
        e = (cx + r * math.cos(math.radians(start + 90)), cy + r * math.sin(math.radians(start + 90)))
        f = (nx + nr * math.cos(math.radians(ns)), ny + nr * math.sin(math.radians(ns)))
        for j in range(1, m + 1):
            tt = j / (m + 1)
            pts.append((e[0] + (f[0] - e[0]) * tt, e[1] + (f[1] - e[1]) * tt))
    return pts


def _boolean(ob, other, op='DIFFERENCE'):
    m = ob.modifiers.new('b', 'BOOLEAN')
    m.operation, m.object, m.solver = op, other, 'EXACT'
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.data.objects.remove(other)


def build_plate(rects, kinds, col):
    P, B, L = D.PLATE, D.BUTTON, D.LED
    bm = bmesh.new()
    cover_mesh(bm)                                   # the top bending straight down into the front lip
    plate = new_object('Plate', bm, col, smooth=False)
    C = D.CASE
    keep = bmesh.new()                               # round the back corners in plan; the lip is untouched
    prism(keep, rounded_rect_fb(0.0, 0.0, D.CANVAS_W, D.CANVAS_H + 400.0, C['corner_back_r'], 1.0, k=16, m=40),
          -2000.0, 50.0)
    _boolean(plate, new_object('cover_keep', keep, col, smooth=False), 'INTERSECT')
    # round over the sheet's side and back edges now, on the clean sheet (before the holes), where the bevel
    # can clamp safely; the front bend is in the cross-section already
    bm = bmesh.new()
    bm.from_mesh(plate.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.05)          # the intersect leaves duplicate points
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges[:], dist=0.05)
    bm.normal_update()
    bw0 = bm.edges.layers.float.get('bevel_weight_edge') or bm.edges.layers.float.new('bevel_weight_edge')
    for e in bm.edges:
        if any(abs(v.co.z) > 0.01 for v in e.verts) or len(e.link_faces) != 2:
            continue
        if all(abs(f.normal.z) > 0.99 for f in e.link_faces):
            continue
        if -max(v.co.y for v in e.verts) > D.CANVAS_H - 300:      # leave the front bend alone
            continue
        e[bw0] = 1.0
    bm.to_mesh(plate.data)
    bm.free()
    roll = plate.modifiers.new('roll', 'BEVEL')
    roll.limit_method, roll.width, roll.segments, roll.profile = 'WEIGHT', C['edge_roll_r'], 6, 0.5
    roll.use_clamp_overlap = False                   # the clean sheet has nothing smaller than the roll to collide with
    bpy.context.view_layer.objects.active = plate
    bpy.ops.object.modifier_apply(modifier='roll')
    for e in plate.data.attributes['bevel_weight_edge'].data:
        e.value = 0.0

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
        holes.append((cid, cx, cy, P['led_fillet'] if kind == 'led' else P['hole_fillet']))
        b2 = bmesh.new()
        prism(b2, out, -P['thickness'] - 10, 10)
        new_object('cut_' + cid, b2, cutters, smooth=False)
    d = D.DISPLAY
    b2 = bmesh.new()
    prism(b2, rounded_rect((d['x0'] + d['x1']) / 2, (d['y0'] + d['y1']) / 2, d['x1'] - d['x0'], d['y1'] - d['y0'], d['r']),
          -P['thickness'] - 10, 10)
    new_object('cut_display', b2, cutters, smooth=False)
    holes.append(('display', (d['x0'] + d['x1']) / 2, (d['y0'] + d['y1']) / 2, P['hole_fillet']))

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

    # bevel weights on the top rim edges only (top face meets a wall): each hole 0.85..1.0 (hash of its id) of
    # its fillet (LED holes led_fillet, the others hole_fillet); the sheet's side and back edges get edge_roll_r.
    # The front bend is modelled in the cross-section, not bevelled
    me = plate.data
    wmax = P['hole_fillet']
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.normal_update()                               # face normals decide which top edges are rims
    bw = bm.edges.layers.float.get('bevel_weight_edge') or bm.edges.layers.float.new('bevel_weight_edge')
    for e in bm.edges:
        v0, v1 = e.verts[0].co, e.verts[1].co
        if abs(v0.z) > 0.01 or abs(v1.z) > 0.01 or len(e.link_faces) != 2:
            continue
        if all(abs(f.normal.z) > 0.99 for f in e.link_faces):
            continue                                 # a split inside the flat top, not a rim
        mx, my = (v0.x + v1.x) / 2, -(v0.y + v1.y) / 2
        if min(mx, my, D.CANVAS_W - mx) < D.CASE['corner_back_r'] + D.CASE['edge_roll_r'] + 8:
            continue                                 # the rolled side and back edges are done already
        cid, _, _, fil = min(holes, key=lambda h: (h[1] - mx) ** 2 + (h[2] - my) ** 2)
        e[bw] = fil * (0.85 + 0.15 * hash01(cid)) / wmax
    bm.to_mesh(me)
    bm.free()
    bev = plate.modifiers.new('fillets', 'BEVEL')
    bev.limit_method = 'WEIGHT'
    bev.width = wmax
    bev.segments = 8
    bev.profile = 0.5
    bev.use_clamp_overlap = False                    # the boolean leaves tiny triangles by each hole; clamping
                                                     # against them flattens every fillet to nothing
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
    # normals weighted by face area: the big flat top keeps exactly flat normals, so long triangles between a
    # hole and a rounded edge don't pick up the edge's tilt (it showed as streaks across the paint)
    wn = plate.modifiers.new('flat_top', 'WEIGHTED_NORMAL')
    wn.mode, wn.weight, wn.keep_sharp = 'FACE_AREA', 100, True
    return plate


def build_floor(col):
    bm = bmesh.new()
    prism(bm, rounded_rect(D.CANVAS_W / 2, D.CANVAS_H / 2, D.CANVAS_W - 120, D.CANVAS_H - 120, 10, k=4, m=2),
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
    # top profile (u = progress from the wall outline to the pill outline, v = share of the shoulder height),
    # after the close-up photo: a rounded top edge on the wall onto a flat ledge, then a raised pill
    profile = [(0.01, 0.16), (0.03, 0.28), (0.07, 0.38), (0.12, 0.44), (0.20, 0.47), (0.50, 0.50), (0.80, 0.52),
               (0.86, 0.56), (0.90, 0.68), (0.94, 0.84), (0.97, 0.95), (0.99, 0.99), (1.0, 1.0)]
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
    """Built around its own origin (moved to the knob's centre) so a frame is just a turn of the object; the
    pointer stripe is in the material, fixed to the knob (toward +y at rest)."""
    K = D.MASTER
    n = 14 * 24
    H, tr, dep = K['height'], K['top_round'], K['flute_depth']

    def outline(r, d):
        pts = []
        for i in range(n):
            a = 2 * math.pi * i / n
            u = (K['flutes'] * a / (2 * math.pi)) % 1.0              # 0..1 across one lobe
            lobe = max(0.0, 1.0 - (2 * u - 1) ** 2) ** 0.35           # rounded top, narrow notch at each end
            rr = r - d * (1.0 - lobe)
            pts.append((rr * math.sin(a), -rr * math.cos(a)))       # art space: a = 0 points up (12 o'clock)
        return pts

    rings = []
    for i in range(7):                               # tapered side
        t = i / 6
        z = (H - tr) * t
        rings.append((outline(K['r_base'] + (K['r_top'] - K['r_base']) * t, dep), z))
    for i in range(1, 9):                            # rounded top edge; the flutes run out on it
        th = (math.pi / 2) * i / 8
        rings.append((outline(K['r_top'] - tr * (1 - math.cos(th)), dep * (1 - i / 8)), H - tr + tr * math.sin(th)))
    for s_ in (0.8, 0.55, 0.3, 0.1):                 # flat top, very slightly domed
        rings.append(([(x * s_, y * s_) for x, y in outline(K['r_top'] - tr, 0.0)], H + 0.6 * (1 - s_ * s_)))
    bm = bmesh.new()
    loft(bm, rings)
    body = new_object('MasterKnob', bm, col)
    body.location = (cx, -cy, 0.0)
    ang = math.radians(-K['sweep'] / 2 + K['sweep'] * frame / (K['frames'] - 1))
    body.rotation_euler = (0.0, 0.0, -ang)           # clockwise seen from above
    return body, None


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
    build_chassis(c_body)
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
