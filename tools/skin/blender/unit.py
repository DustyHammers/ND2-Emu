# The rest of the whole unit, around the top built by scene.py: base plate and feet, screws, the back-panel
# connectors, a lit display, and a studio backdrop for angled views. Panel coordinates (top surface z = 0);
# the world vertical is scene.up_vector().
import math
import os

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

import dims as D
import scene
from layout import SKIN

MM = D.MM


def _frame(origin, x_axis, normal):
    """Object matrix: local +Z along `normal` (out of a wall), local +X along `x_axis`."""
    z = normal.normalized()
    x = (x_axis - z * x_axis.dot(z)).normalized()
    y = z.cross(x)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = origin
    return m


def _cyl(bm, r, z0, z1, n=48, cap=True):
    rings = [([(r * math.cos(2 * math.pi * i / n), -r * math.sin(2 * math.pi * i / n)) for i in range(n)], z0),
             ([(r * math.cos(2 * math.pi * i / n), -r * math.sin(2 * math.pi * i / n)) for i in range(n)], z1)]
    scene.loft(bm, rings, cap_top=cap, cap_bottom=cap)


def _obj(name, bm, col, matrix, smooth=True):
    ob = scene.new_object(name, bm, col, smooth)
    ob.matrix_world = matrix
    return ob


def base_and_feet(col):
    U = scene.up_vector()
    C, P = D.CASE, D.PLATE
    t = P['thickness']
    bm = bmesh.new()
    out = scene.rounded_rect(D.CANVAS_W / 2, D.CANVAS_H / 2, D.CANVAS_W - 2 * t - 2, D.CANVAS_H - 2 * t - 2, 6, k=4, m=20)
    top = [bm.verts.new(Vector((x, -y, 0)) - U * scene.wall_depth(-y)) for x, y in out]
    bot = [bm.verts.new(v.co - U * C['base_t']) for v in top]
    bm.faces.new(list(reversed(top)))
    bm.faces.new(bot)
    for i in range(len(top)):
        j = (i + 1) % len(top)
        bm.faces.new((bot[i], bot[j], top[j], top[i]))
    base = scene.new_object('Base', bm, col, smooth=False)
    feet = []
    ins = C['foot_inset']
    for k, (fx, fy) in enumerate(((ins, ins), (D.CANVAS_W - ins, ins), (ins, D.CANVAS_H - ins), (D.CANVAS_W - ins, D.CANVAS_H - ins))):
        o = Vector((fx, -fy, 0)) - U * (scene.wall_depth(-fy) + C['base_t'])
        b2 = bmesh.new()
        _cyl(b2, C['foot_r'], -C['foot_h'], 0, 40)
        feet.append(_obj(f'Foot{k}', b2, col, _frame(o, Vector((1, 0, 0)), U)))
    return base, feet


def screw(name, origin, x_axis, normal, col):
    r = D.CASE['screw_r']
    bm = bmesh.new()
    n = 48
    rings = []
    for i in range(7):                       # pan head: a low dome
        th = (math.pi / 2) * i / 6
        rr = r * math.cos(th) if i else r
        rings.append(([(rr * math.cos(2 * math.pi * j / n), -rr * math.sin(2 * math.pi * j / n)) for j in range(n)],
                      0.9 * MM * math.sin(th) if i else 0.0))
    scene.loft(bm, rings)
    head = _obj(name, bm, col, _frame(origin, x_axis, normal))
    # cross recess
    bm = bmesh.new()
    w, l = 0.32 * MM, 1.3 * MM
    for a in (0, 90):
        q = [(-l, -w), (l, -w), (l, w), (-l, w)]
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        scene.prism(bm, [(x * ca - y * sa, x * sa + y * ca) for x, y in q], 0.45 * MM, 1.2 * MM)
    rec = _obj(name + '_recess', bm, col, _frame(origin, x_axis, normal), smooth=False)
    return head, rec


def screws(col):
    U = scene.up_vector()
    C = D.CASE
    out = []
    # on the face of the red lip, halfway down its flat part (outward normal toward the viewer)
    nf = Vector((0, -U.z, U.y))
    path, _ = scene.cover_section()
    d = C['front_bend_r'] * math.tan(math.acos(U.y) / 2)
    depth = (d + C['lip_h']) / 2
    for i, f in enumerate(C['lip_screw_x']):
        o = Vector((f * D.CANVAS_W, -D.CANVAS_H, 0)) - U * depth
        out += screw(f'ScrewFront{i}', o, Vector((1, 0, 0)), nf, col)
    for i, (x, sgn) in enumerate(((0.0, -1), (D.CANVAS_W, 1))):
        y = -0.55 * D.CANVAS_H
        o = Vector((x, y, 0)) - U * (scene.wall_depth(y) - 7.0 * MM)
        out += screw(f'ScrewSide{i}', o, Vector((0, 1, 0)), Vector((sgn, 0, 0)), col)
    return out


def connectors(col):
    """Back panel: 1/4-inch jacks with hex nuts, MIDI DIN sockets, the DC socket."""
    U = scene.up_vector()
    C = D.CASE
    nb = Vector((0, U.z, -U.y))                 # back wall, outward
    objs = []
    for k, (kind, x) in enumerate(D.JACKS):
        o = Vector((x, 0, 0)) - U * (C['back_h'] - C['jack_h'])
        m = _frame(o, Vector((-1, 0, 0)), nb)
        bm = bmesh.new()
        if kind == 'jack':
            hexr = 7.0 * MM / math.cos(math.pi / 6)
            hexo = [(hexr * math.cos(math.pi / 3 * i + math.pi / 6), -hexr * math.sin(math.pi / 3 * i + math.pi / 6)) for i in range(6)]
            scene.prism(bm, hexo, 0.0, 2.2 * MM)              # nut
            _cyl(bm, 4.8 * MM, 2.2 * MM, 9.0 * MM)            # threaded bush (shows over the back edge)
            objs.append(_obj(f'JackNut{k}', bm, col, m, smooth=False))
            b2 = bmesh.new()
            _cyl(b2, 3.3 * MM, 2.0 * MM, 9.05 * MM)           # the dark hole
            objs.append(_obj(f'JackHole{k}', b2, col, m))
        elif kind == 'din':
            _cyl(bm, 8.0 * MM, 0.0, 0.6 * MM)                 # metal shell ring
            objs.append(_obj(f'DinShell{k}', bm, col, m))
            b2 = bmesh.new()
            _cyl(b2, 6.8 * MM, 0.0, 0.65 * MM)                # black insulator face
            objs.append(_obj(f'DinFace{k}', b2, col, m))
            b3 = bmesh.new()
            for j in range(5):                                  # five pin holes on a 7.5 mm circle (180 degrees)
                a = math.radians(180 + 45 * j)
                before = len(b3.verts)
                _cyl(b3, 0.75 * MM, 0.62 * MM, 0.7 * MM, 16)
                b3.verts.ensure_lookup_table()
                bmesh.ops.translate(b3, verts=b3.verts[before:], vec=Vector((3.75 * MM * math.cos(a), 3.75 * MM * math.sin(a), 0)))
            objs.append(_obj(f'DinPins{k}', b3, col, m))
        else:                                                   # DC socket
            _cyl(bm, 4.5 * MM, 0.0, 1.5 * MM)
            objs.append(_obj(f'DcBody{k}', bm, col, m))
            b2 = bmesh.new()
            _cyl(b2, 2.8 * MM, 1.4 * MM, 1.55 * MM)
            objs.append(_obj(f'DcHole{k}', b2, col, m))
    return objs


def display_emission(out_dir, text='P1.01'):
    """A canvas-size map of the lit display segments, from the skin's own segment sprites."""
    from PIL import Image
    seg = Image.open(os.path.join(SKIN, 'ND2DisplaySegments.png')).convert('RGBA')
    cells = {n: seg.crop((i * 176, 0, (i + 1) * 176, 212)).getchannel('A') for i, n in
             enumerate(('unlit', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'dp'))}
    font = {'P': 'abefg', '1': 'bc', '0': 'abcdef', '2': 'abdeg', '3': 'abcdg', ' ': ''}
    digits, dps = [], []
    for ch in text:
        if ch == '.':
            dps[-1] = True
        else:
            digits.append(ch)
            dps.append(False)
    lit = np.zeros((D.CANVAS_H, D.CANVAS_W), np.float32)
    unlit = np.zeros_like(lit)
    for i, (ch, dp) in enumerate(zip(digits[:4], dps[:4])):
        x0, y0 = int(992 + 116 * i), 496
        u = np.asarray(cells['unlit'], np.float32) / 255
        unlit[y0:y0 + 212, x0:x0 + 176] = np.maximum(unlit[y0:y0 + 212, x0:x0 + 176], u)
        names = list(font.get(ch, '')) + (['dp'] if dp else [])
        for sname in names:
            a = np.asarray(cells[sname], np.float32) / 255
            lit[y0:y0 + 212, x0:x0 + 176] = np.maximum(lit[y0:y0 + 212, x0:x0 + 176], a)
    img = np.stack([lit, unlit, np.zeros_like(lit)], -1)
    path = os.path.join(out_dir, 'display_emission.png')
    Image.fromarray((img * 255).astype(np.uint8)).save(path)
    return path


def backdrop(col):
    """A large matte floor and a curved sweep behind, for angled views only."""
    U = scene.up_vector()
    C = D.CASE
    floor_pt = Vector((D.CANVAS_W / 2, -D.CANVAS_H / 2, 0)) - U * (scene.wall_depth(-D.CANVAS_H / 2) + C['base_t'] + C['foot_h'])
    bm = bmesh.new()
    s = 60000.0
    q = [(-s, -s), (s, -s), (s, s), (-s, s)]
    for x, y in q:
        bm.verts.new((x, y, 0))
    bm.faces.new(bm.verts[:])
    ob = scene.new_object('Backdrop', bm, col, smooth=False)
    ob.matrix_world = _frame(floor_pt, Vector((1, 0, 0)), U)
    return ob


def add_all(col_case, col_back, maps_dir):
    base_and_feet(col_case)
    screws(col_case)
    connectors(col_back)
    return display_emission(maps_dir)
