# Exploration: the panel opened up with a rotary tool along the upper red band, showing the circuit board under it,
# with six bare potentiometers glued to the board and wired to it, their shafts standing up through the opening,
# and a small indicator LED. Added to the scene after the materials (render_view.py --bent).
# Everything is in art px (2x canvas, y down) and px heights (dims.MM px per mm), like scene.py.
import math
import os
import random

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from PIL import Image, ImageDraw, ImageFont

import dims as D
import materials
import scene

MM = D.MM
MOD = dict(
    slot=(470.0, 152.0, 2960.0, 330.0),    # the opening: x0, y0, x1, y1 (below the TRIG INPUTS line, above the black print)
    kerf=10.0,                             # cut-off wheel width (px): the overcuts past the corners
    chip=(0.6, 6.0),                       # paint chipped off along the cut: min, max width (px)
    chip_depth=1.1,                        # paint thickness (px)
    board_top=-6.0 * MM,                   # the circuit board's top surface
    board_t=1.6 * MM,
    board=(380.0, 40.0, 3050.0, 520.0),    # board area modelled (art px)
    pots_x=(700.0, 1110.0, 1520.0, 1930.0, 2340.0, 2750.0),
    pot_y=236.0,
    # each pot picks one of 8 bends (or off): turned smoothly from 7 o'clock (off) to 5 o'clock, 300 degrees
    pot_sweep=300.0,
    pot_angles=(-150.0, -150.0, 37.5, -150.0, 112.5, -75.0),  # demo positions (degrees, clockwise from up)
    led=(2905.0, 205.0),
    seed=7,
)


# ---- small mesh helpers ------------------------------------------------------------------------------------------

def box(bm, x0, y0, x1, y1, z0, z1):
    scene.prism(bm, [(x1, y1), (x0, y1), (x0, y0), (x1, y0)], z0, z1)


def obj(name, bm, col, mat, smooth=False):
    ob = scene.new_object(name, bm, col, smooth=smooth)
    ob.data.materials.append(mat)
    return ob


def cyl(bm, cx, cy, r, z0, z1, n=32):
    scene.prism(bm, scene.circle(cx, cy, r, n), z0, z1)


def wavy_edge(p0, p1, rnd, amp=1.4, step=18.0):
    """Points from p0 toward p1 (p1 excluded) with a small sideways wander, like a hand-held cut."""
    (x0, y0), (x1, y1) = p0, p1
    L = math.hypot(x1 - x0, y1 - y0)
    n = max(2, int(L / step))
    nx, ny = -(y1 - y0) / L, (x1 - x0) / L
    ph = [rnd.uniform(0, 2 * math.pi) for _ in range(3)]
    pts = []
    for i in range(n):
        t = i / n
        w = amp * (0.6 * math.sin(t * 7 + ph[0]) + 0.3 * math.sin(t * 23 + ph[1]) + 0.25 * math.sin(t * 61 + ph[2]))
        if i == 0:
            w = 0.0
        pts.append((x0 + (x1 - x0) * t + nx * w, y0 + (y1 - y0) * t + ny * w))
    return pts


def ragged_ring(x0, y0, x1, y1, rnd, wmin, wmax, step=6.0):
    """The rectangle grown outward by a width that wanders between wmin and wmax (chipped paint)."""
    pts = []
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    ph = [rnd.uniform(0, 2 * math.pi) for _ in range(4)]
    perim = 2 * ((x1 - x0) + (y1 - y0))
    s = 0.0
    for k in range(4):
        (ax, ay), (bx, by) = corners[k], corners[(k + 1) % 4]
        L = math.hypot(bx - ax, by - ay)
        nx, ny = (by - ay) / L, -(bx - ax) / L          # outward for a clockwise (screen) loop
        n = max(2, int(L / step))
        for i in range(n):
            t = i / n
            u = (s + t * L) / perim
            f = 0.5 + 0.5 * (0.5 * math.sin(u * 2 * math.pi * 9 + ph[0]) + 0.3 * math.sin(u * 2 * math.pi * 31 + ph[1])
                             + 0.2 * math.sin(u * 2 * math.pi * 87 + ph[2]))
            f = max(0.0, min(1.0, f)) ** 1.6
            if rnd.random() < 0.01:                     # an occasional bigger flake
                f = 2.2
            w = wmin + (wmax - wmin) * f
            pts.append((ax + (bx - ax) * t + nx * w, ay + (by - ay) * t + ny * w))
        s += L
    return pts


# ---- materials ---------------------------------------------------------------------------------------------------

def mats():
    M = {}
    steel = materials.simple('BareSteel', (0.62, 0.62, 0.63), 0.30, 0.5)
    steel.node_tree.nodes['Principled BSDF'].inputs['Metallic'].default_value = 1.0
    M['steel'] = steel
    nickel = materials.simple('Nickel', (0.70, 0.69, 0.66), 0.22, 0.5)
    nickel.node_tree.nodes['Principled BSDF'].inputs['Metallic'].default_value = 1.0
    M['nickel'] = nickel
    tin = materials.simple('Tin', (0.75, 0.75, 0.74), 0.28, 0.5)
    tin.node_tree.nodes['Principled BSDF'].inputs['Metallic'].default_value = 1.0
    M['tin'] = tin
    M['pot_body'] = materials.simple('PotBody', (0.02, 0.02, 0.022), 0.5, 0.5)
    M['chip'] = materials.simple('ChipEpoxy', (0.018, 0.018, 0.02), 0.45, 0.5)
    M['res'] = materials.simple('SmdResistor', (0.015, 0.015, 0.016), 0.55, 0.5)
    M['cap'] = materials.simple('SmdCap', (0.38, 0.28, 0.17), 0.5, 0.5)
    M['elko'] = materials.simple('ElkoSleeve', (0.02, 0.03, 0.10), 0.35, 0.5, coat=0.4, coat_rough=0.2)
    glue = materials.simple('HotGlue', (0.85, 0.84, 0.80), 0.25, 0.5, transmission=0.55)
    glue.node_tree.nodes['Principled BSDF'].inputs['Subsurface Weight'].default_value = 0.3
    M['glue'] = glue
    for name, rgb in (('red', (0.55, 0.02, 0.02)), ('yellow', (0.65, 0.48, 0.02)), ('green', (0.02, 0.28, 0.05)),
                      ('blue', (0.02, 0.08, 0.45)), ('white', (0.75, 0.75, 0.72)), ('black', (0.015, 0.015, 0.015)),
                      ('orange', (0.75, 0.18, 0.01))):
        M['wire_' + name] = materials.simple('Wire_' + name, rgb, 0.4, 0.5, coat=0.3, coat_rough=0.25)
    led = materials.simple('BentLed', (0.6, 0.05, 0.04), 0.1, 0.5, coat=0.8, coat_rough=0.05, transmission=0.3)
    b = led.node_tree.nodes['Principled BSDF']
    b.inputs['Emission Color'].default_value = (1.0, 0.03, 0.01, 1)
    b.inputs['Emission Strength'].default_value = 6.0
    M['led'] = led
    return M


def board_material(tex_path):
    """Green solder mask over copper traces (slightly lighter, raised), tinned pads, white legend."""
    m = bpy.data.materials.new('PcbBoard')
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes['Principled BSDF']
    x0, y0, x1, y1 = MOD['board']
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    uv = materials._vm(nt, 'MULTIPLY_ADD', geo.outputs['Position'], (1 / (x1 - x0), 1 / (y1 - y0), 0))
    uv.inputs[2].default_value = (-x0 / (x1 - x0), 1 + y0 / (y1 - y0), 0)
    t = materials._image(nt, tex_path, (0, 0), 'pcb')
    t.interpolation = 'Linear'
    materials._link(nt, uv.outputs[0], t.inputs['Vector'])
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    materials._link(nt, t.outputs['Color'], sep.inputs[0])
    trace, pad, silk = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    mask = (0.020, 0.115, 0.045)
    over = (0.050, 0.200, 0.070)
    c1 = nt.nodes.new('ShaderNodeMix'); c1.data_type = 'RGBA'
    materials._link(nt, trace, c1.inputs['Factor']); c1.inputs['A'].default_value = (*mask, 1); c1.inputs['B'].default_value = (*over, 1)
    c2 = nt.nodes.new('ShaderNodeMix'); c2.data_type = 'RGBA'
    materials._link(nt, pad, c2.inputs['Factor']); materials._link(nt, c1.outputs['Result'], c2.inputs['A'])
    c2.inputs['B'].default_value = (0.72, 0.72, 0.70, 1)
    c3 = nt.nodes.new('ShaderNodeMix'); c3.data_type = 'RGBA'
    materials._link(nt, silk, c3.inputs['Factor']); materials._link(nt, c2.outputs['Result'], c3.inputs['A'])
    c3.inputs['B'].default_value = (0.80, 0.80, 0.76, 1)
    materials._link(nt, c3.outputs['Result'], b.inputs['Base Color'])
    materials._link(nt, pad, b.inputs['Metallic'])
    r = materials._m(nt, 'MULTIPLY_ADD', pad, -0.05)
    r.inputs[2].default_value = 0.33
    materials._link(nt, r, b.inputs['Roughness'])
    b.inputs['Coat Weight'].default_value = 0.5
    b.inputs['Coat Roughness'].default_value = 0.18
    h = materials._m(nt, 'ADD', materials._m(nt, 'MULTIPLY', trace, 0.6), materials._m(nt, 'MULTIPLY', pad, 0.4))
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Distance'].default_value = 0.6
    materials._link(nt, h, bump.inputs['Height'])
    materials._link(nt, bump.outputs['Normal'], b.inputs['Normal'])
    return m


# ---- the board's artwork (traces, pads, legend) -------------------------------------------------------------------

def board_texture(path, parts, rnd):
    x0, y0, x1, y1 = (int(v) for v in MOD['board'])
    W, H = x1 - x0, y1 - y0
    tr = Image.new('L', (W, H), 0)
    pd = Image.new('L', (W, H), 0)
    sk = Image.new('L', (W, H), 0)
    dt, dp, ds = ImageDraw.Draw(tr), ImageDraw.Draw(pd), ImageDraw.Draw(sk)
    try:
        font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 16)
    except OSError:
        font = ImageFont.load_default()
    # traces: Manhattan runs with 45-degree corners, some wide (supply), with vias at the ends
    for _ in range(70):
        x, y = rnd.uniform(0, W), rnd.uniform(0, H)
        w = rnd.choice((5, 5, 6, 6, 8, 14))
        pts = [(x, y)]
        for _ in range(rnd.randint(2, 5)):
            L = rnd.uniform(60, 420)
            d = rnd.choice(((1, 0), (-1, 0), (0, 1), (0, -1), (0.707, 0.707), (-0.707, 0.707)))
            x, y = x + d[0] * L, y + d[1] * L
            pts.append((x, y))
        dt.line(pts, fill=255, width=w, joint='curve')
        for (vx, vy) in (pts[0], pts[-1]):
            if rnd.random() < 0.6:
                dp.ellipse((vx - 9, vy - 9, vx + 9, vy + 9), fill=255)
                dp.ellipse((vx - 4, vy - 4, vx + 4, vy + 4), fill=0)
    # pads under the parts, and the legend around them
    for kind, cx, cy, w, h, ref in parts:
        X, Y = cx - x0, cy - y0
        if kind in ('res', 'cap'):
            for s in (-1, 1):
                dp.rectangle((X + s * w * 0.42 - w * 0.22, Y - h * 0.6, X + s * w * 0.42 + w * 0.22, Y + h * 0.6), fill=255)
            ds.rectangle((X - w * 0.75, Y - h * 1.0, X + w * 0.75, Y + h * 1.0), outline=255, width=2)
            ds.text((X - w * 0.7, Y - h * 1.0 - 19), ref, fill=255, font=font)
        elif kind == 'soic':
            n = 4 if w < 100 else 8
            for i in range(n):
                px = X - w / 2 + (i + 0.5) * w / n
                for s in (-1, 1):
                    dp.rectangle((px - 5, Y + s * (h / 2 + 4) - 9, px + 5, Y + s * (h / 2 + 4) + 9), fill=255)
            ds.rectangle((X - w / 2 - 6, Y - h / 2 - 18, X + w / 2 + 6, Y + h / 2 + 18), outline=255, width=2)
            ds.ellipse((X - w / 2 + 2, Y + h / 2 - 2 - 12, X - w / 2 + 12, Y + h / 2 - 2), fill=255)
            ds.text((X - w / 2, Y - h / 2 - 40), ref, fill=255, font=font)
        elif kind == 'qfp':
            n = 16
            for i in range(n):
                q = -w / 2 + (i + 0.5) * w / n
                for s in (-1, 1):
                    dp.rectangle((X + q - 3, Y + s * (w / 2 + 12) - 10, X + q + 3, Y + s * (w / 2 + 12) + 10), fill=255)
                    dp.rectangle((X + s * (w / 2 + 12) - 10, Y + q - 3, X + s * (w / 2 + 12) + 10, Y + q + 3), fill=255)
            ds.text((X - w / 2, Y - w / 2 - 44), ref, fill=255, font=font)
        elif kind == 'elko':
            ds.ellipse((X - w / 2 - 6, Y - w / 2 - 6, X + w / 2 + 6, Y + w / 2 + 6), outline=255, width=3)
            ds.text((X - w / 2, Y - w / 2 - 26), ref, fill=255, font=font)
    img = Image.merge('RGB', (tr, pd, sk))
    img.save(path)
    return path


# ---- parts -------------------------------------------------------------------------------------------------------

def plan_parts(rnd):
    """Board parts in the opening's view, kept clear of the pots' footprints."""
    sx0, sy0, sx1, sy1 = MOD['slot']
    pots = [(x, MOD['pot_y']) for x in MOD['pots_x']]
    parts = []

    def clear(x, y, r):
        return all(abs(x - px) > 95 + r or abs(y - py) > 100 + r for px, py in pots) and \
            abs(x - MOD['led'][0]) > 40 + r

    parts.append(('qfp', 1735.0, 300.0, 155.0, 155.0, 'U1'))          # the processor, half under the black print
    parts.append(('soic', 880.0, 300.0, 78.0, 60.0, 'U4'))
    parts.append(('soic', 2565.0, 190.0, 155.0, 60.0, 'U7'))
    parts.append(('elko', 1300.0, 185.0, 98.0, 98.0, 'C21'))
    parts.append(('elko', 2140.0, 290.0, 98.0, 98.0, 'C22'))
    k = 1
    tries = 0
    while k < 70 and tries < 4000:
        tries += 1
        x, y = rnd.uniform(sx0 - 40, sx1 + 40), rnd.uniform(sy0 - 30, sy1 + 40)
        rot = rnd.random() < 0.5
        kind = 'res' if rnd.random() < 0.6 else 'cap'
        w, h = (25.0, 13.0) if rnd.random() < 0.7 else (31.0, 20.0)
        if rot:
            w, h = h, w
        if not clear(x, y, 20):
            continue
        if any(abs(x - p[1]) < (w + p[3]) / 2 + 16 and abs(y - p[2]) < (h + p[4]) / 2 + 22 for p in parts):
            continue
        parts.append((kind, x, y, w, h, ('R' if kind == 'res' else 'C') + str(k + 10)))
        k += 1
    return parts


def build_parts(col, M, parts, zb):
    for kind, cx, cy, w, h, ref in parts:
        bm = bmesh.new()
        if kind in ('res', 'cap'):
            th = 0.45 * MM if kind == 'res' else 0.8 * MM
            box(bm, cx - w * 0.30, cy - h / 2, cx + w * 0.30, cy + h / 2, zb, zb + th)
            obj('Smd_' + ref, bm, col, M[kind])
            for s in (-1, 1):
                b2 = bmesh.new()
                box(b2, cx + s * w * 0.4 - w * 0.1, cy - h / 2, cx + s * w * 0.4 + w * 0.1, cy + h / 2, zb, zb + th)
                obj('SmdEnd_' + ref + str(s), b2, col, M['tin'])
        elif kind == 'soic':
            box(bm, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, zb + 0.15 * MM, zb + 1.5 * MM)
            obj('Chip_' + ref, bm, col, M['chip'])
            n = 4 if w < 100 else 8
            b2 = bmesh.new()
            for i in range(n):
                px = cx - w / 2 + (i + 0.5) * w / n
                for s in (-1, 1):
                    box(b2, px - 3.5, cy + s * h / 2 - (6 if s < 0 else 0) - (0 if s < 0 else -0), px + 3.5,
                        cy + s * h / 2 + (0 if s < 0 else 6) + s * 10, zb, zb + 0.6 * MM)
            obj('ChipLegs_' + ref, b2, col, M['tin'])
        elif kind == 'qfp':
            box(bm, cx - w / 2, cy - w / 2, cx + w / 2, cy + w / 2, zb + 0.1 * MM, zb + 1.4 * MM)
            obj('Chip_' + ref, bm, col, M['chip'])
            b2 = bmesh.new()
            n = 16
            for i in range(n):
                q = -w / 2 + (i + 0.5) * w / n
                for s in (-1, 1):
                    box(b2, cx + q - 2.5, cy + s * (w / 2 + 12) - 11, cx + q + 2.5, cy + s * (w / 2 + 12) + 11, zb, zb + 0.5 * MM)
                    box(b2, cx + s * (w / 2 + 12) - 11, cy + q - 2.5, cx + s * (w / 2 + 12) + 11, cy + q + 2.5, zb, zb + 0.5 * MM)
            obj('ChipLegs_' + ref, b2, col, M['tin'])
        elif kind == 'elko':
            r = w / 2
            cyl(bm, cx, cy, r, zb, zb + 5.0 * MM, 40)
            obj('Elko_' + ref, bm, col, M['elko'])
            b2 = bmesh.new()
            cyl(b2, cx, cy, r - 3, zb + 5.0 * MM, zb + 5.0 * MM + 1.0, 40)
            obj('ElkoTop_' + ref, b2, col, M['nickel'])


def build_pot(col, M, i, cx, cy, zb, angle_deg):
    """A 9 mm board-mount potentiometer: black base, steel cover, threaded bushing and a knurled split shaft."""
    parts = []
    bm = bmesh.new()
    box(bm, cx - 74, cy - 78, cx + 74, cy + 78, zb + 0.6 * MM, zb + 4.6 * MM)
    parts.append(obj(f'Pot{i}_base', bm, col, M['pot_body']))
    bm = bmesh.new()
    box(bm, cx - 70, cy - 74, cx + 70, cy + 74, zb + 4.6 * MM, zb + 6.4 * MM)
    parts.append(obj(f'Pot{i}_cover', bm, col, M['nickel']))
    bm = bmesh.new()
    cyl(bm, cx, cy, 3.5 * MM, zb + 6.4 * MM, zb + 11.4 * MM, 40)
    parts.append(obj(f'Pot{i}_bushing', bm, col, M['nickel'], smooth=True))
    # knurled shaft: 18 teeth, split along one axis for the top 5 mm (the slit shows where it points)
    r_o, r_i, teeth = 3.0 * MM, 2.78 * MM, 18
    star = []
    for k in range(teeth * 2):
        a = math.pi * k / teeth
        r = r_o if k % 2 == 0 else r_i
        star.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    bm = bmesh.new()
    scene.prism(bm, star, zb + 11.4 * MM, zb + 23.4 * MM)
    shaft = obj(f'Pot{i}_shaft', bm, col, M['nickel'])
    cut = bmesh.new()                          # the split runs along the pointer
    box(cut, cx - r_o - 5, cy - 0.45 * MM, cx + r_o + 5, cy + 0.45 * MM, zb + 18.4 * MM, zb + 24 * MM)
    cutter = scene.new_object(f'Pot{i}_slitcut', cut, col, smooth=False)
    m = shaft.modifiers.new('slit', 'BOOLEAN')
    m.operation, m.object, m.solver = 'DIFFERENCE', cutter, 'EXACT'
    bpy.context.view_layer.objects.active = shaft
    bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.data.objects.remove(cutter)
    # turn the shaft about its axis: the split points along the pot's position
    shaft.data.transform(Matrix.Translation((-cx, cy, 0)))
    shaft.location = (cx, -cy, 0)
    shaft.rotation_euler = (0, 0, math.radians(90 - angle_deg))
    # three solder lugs on the right-hand side, bent down to the board
    bm = bmesh.new()
    for k in (-1, 0, 1):
        box(bm, cx + 74, cy + k * 39 - 8, cx + 100, cy + k * 39 + 8, zb, zb + 2.2 * MM)
    parts.append(obj(f'Pot{i}_lugs', bm, col, M['tin']))
    # hot glue holding it to the board
    for k, (gx, gy) in enumerate(((cx - 80, cy - 20), (cx - 30, cy + 80))):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=20, v_segments=10, radius=1.0)
        for v in bm.verts:
            v.co = Vector((gx + v.co.x * 28, -(gy + v.co.y * 40), zb + max(v.co.z, -0.2) * 2.6 * MM))
        parts.append(obj(f'Pot{i}_glue{k}', bm, col, M['glue'], smooth=True))
    return [(cx + 100, cy + k * 39) for k in (-1, 0, 1)]     # the lugs' ends: where the wires start


def wire(col, M, name, pts, colour, radius=0.30 * MM):
    """A wire through the given (x, y, z) points (art px, px heights), as a round tube."""
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = radius
    cu.bevel_resolution = 3
    cu.use_fill_caps = True
    sp = cu.splines.new('BEZIER')
    sp.bezier_points.add(len(pts) - 1)
    for bp, (x, y, z) in zip(sp.bezier_points, pts):
        bp.co = (x, -y, z)
        bp.handle_left_type = bp.handle_right_type = 'AUTO'
    ob = bpy.data.objects.new(name, cu)
    col.objects.link(ob)
    ob.data.materials.append(M['wire_' + colour])
    return ob


def solder(col, M, name, x, y, z, r=0.75 * MM):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=8, radius=1.0)
    for v in bm.verts:
        v.co = Vector((x + v.co.x * r, -(y + v.co.y * r), z + max(v.co.z, 0.0) * r * 0.7))
    return obj(name, bm, col, M['tin'], smooth=True)


# ---- the whole mod -------------------------------------------------------------------------------------------------

def add(tmp_dir):
    rnd = random.Random(MOD['seed'])
    col = scene.collection('Bent mod')
    M = mats()
    plate = bpy.data.objects['Plate']
    sx0, sy0, sx1, sy1 = MOD['slot']
    k = MOD['kerf']
    # 1) the paint chipped off along the cut: a paint-deep ragged ring, bare steel at its bottom
    bm = bmesh.new()
    scene.prism(bm, ragged_ring(sx0, sy0, sx1, sy1, rnd, *MOD['chip']), -MOD['chip_depth'], 20.0)
    chip_cut = obj('ModChipCut', bm, col, M['steel'])
    # 2) the opening, with the cut-off wheel's overcuts past each corner
    outline = (wavy_edge((sx0, sy0), (sx1, sy0), rnd) + wavy_edge((sx1, sy0), (sx1, sy1), rnd)
               + wavy_edge((sx1, sy1), (sx0, sy1), rnd) + wavy_edge((sx0, sy1), (sx0, sy0), rnd))
    bm = bmesh.new()
    scene.prism(bm, list(reversed(outline)), -D.PLATE['thickness'] - 20, 30.0)
    slot_cut = obj('ModSlotCut', bm, col, M['steel'])
    for j, (cx, cy, dx, dy) in enumerate(((sx0, sy0, -1, -1), (sx1, sy0, 1, -1), (sx1, sy1, 1, 1), (sx0, sy1, -1, 1))):
        Lh = rnd.uniform(12, 34)
        Lv = rnd.uniform(6, 16) if dy < 0 else rnd.uniform(4, 9)      # stop short of the black print below
        for xa, xb, ya, yb in ((*sorted((cx - dx * 6, cx + dx * Lh)), cy - k / 2, cy + k / 2),
                               (cx - k / 2, cx + k / 2, *sorted((cy - dy * 6, cy + dy * Lv)))):
            b2 = bmesh.new()
            box(b2, xa, ya, xb, yb, -D.PLATE['thickness'] - 20, 30.0)
            slit = scene.new_object(f'ModSlit{j}', b2, col, smooth=False)
            m = slot_cut.modifiers.new('u', 'BOOLEAN')
            m.operation, m.object, m.solver = 'UNION', slit, 'EXACT'
            bpy.context.view_layer.objects.active = slot_cut
            bpy.ops.object.modifier_apply(modifier=m.name)
            bpy.data.objects.remove(slit)
    for cutter in (chip_cut, slot_cut):
        cutter.hide_render = True
        m = plate.modifiers.new('mod_' + cutter.name, 'BOOLEAN')
        m.operation, m.object, m.solver = 'DIFFERENCE', cutter, 'EXACT'
        m.material_mode = 'TRANSFER'
    # the dark interior floor under the opening goes, so the board shows
    floor = bpy.data.objects['Interior']
    bm = bmesh.new()
    x0, y0, x1, y1 = MOD['board']
    box(bm, x0, y0, x1, y1, -400.0, 5.0)
    fcut = scene.new_object('ModFloorCut', bm, col, smooth=False)
    fcut.hide_render = True
    m = floor.modifiers.new('mod_floor', 'BOOLEAN')
    m.operation, m.object, m.solver = 'DIFFERENCE', fcut, 'EXACT'
    # 3) the circuit board and its parts
    zb = MOD['board_top']
    parts = plan_parts(rnd)
    tex = board_texture(os.path.join(tmp_dir, 'pcb.png'), parts, rnd)
    bm = bmesh.new()
    box(bm, x0, y0, x1, y1, zb - MOD['board_t'], zb)
    obj('ModBoard', bm, col, board_material(tex))
    build_parts(col, M, parts, zb)
    # 4) the pots, their wires to the board, and the indicator LED
    colours = ('red', 'yellow', 'green', 'blue', 'white', 'orange')
    qfp = next(p for p in parts if p[0] == 'qfp')
    soics = [p for p in parts if p[0] == 'soic']
    elkos = [p for p in parts if p[0] == 'elko']
    lugs_all = []
    for i, (px, ang) in enumerate(zip(MOD['pots_x'], MOD['pot_angles'])):
        lugs_all.append(build_pot(col, M, i, px, MOD['pot_y'], zb, ang))
    # where each pot's wiper wire lands: chip legs and pads in view (the bend points are inside the chip)
    targets = []
    u4, u7 = sorted(soics, key=lambda p: p[1])
    targets.append((u4[1] + 6, u4[2] - u4[4] / 2 - 10))                       # pot 0 -> U4
    targets.append((elkos[0][1] + 20, elkos[0][2] + 40))                       # pot 1 -> near C21
    for j, i in enumerate((2, 3)):                                              # pots 2, 3 -> the processor's legs
        q = -qfp[3] / 2 + (3 + 9 * j + 0.5) * qfp[3] / 16
        targets.append((qfp[1] + q, qfp[2] - qfp[3] / 2 - 12))
    targets.append((u7[1] - 40, u7[2] + u7[4] / 2 + 10))                       # pot 4 -> U7
    targets.append((u7[1] + 50, u7[2] + u7[4] / 2 + 10))                       # pot 5 -> U7
    for i, (lugs, (tx, ty)) in enumerate(zip(lugs_all, targets)):
        if i == 5:
            continue                                    # the last pot's wiper wire crossed its own pot: none
        lx, ly = lugs[1]
        up = rnd.uniform(0.5, 3.5) * MM                 # a loose loop that rises above the panel now and then
        mid = ((lx + tx) / 2 + rnd.uniform(-30, 30), min(ly, ty) - rnd.uniform(20, 60))
        wire(col, M, f'Wire{i}', [(lx - 4, ly, zb + 2.0 * MM), (lx + 40, ly - 10, zb + 4.5 * MM),
                                  (mid[0], mid[1], up), (tx, ty, zb + 0.6 * MM)], colours[i])
        solder(col, M, f'Solder{i}a', lx - 2, ly, zb + 2.0 * MM, 0.6 * MM)
        solder(col, M, f'Solder{i}b', tx, ty, zb + 0.45 * MM, 0.55 * MM)
    # a shared ground wire hopping along the pots' bottom lugs, then off under the panel
    pts = []
    for i, lugs in enumerate(lugs_all):
        gx, gy = lugs[2]
        pts += [(gx - 2, gy, zb + 2.0 * MM), (gx + 150, gy + 22, zb + 2.6 * MM)]
        solder(col, M, f'SolderG{i}', gx - 2, gy, zb + 2.0 * MM, 0.55 * MM)
    pts += [(MOD['slot'][2] - 40, MOD['slot'][3] + 80, zb + 0.6 * MM)]
    wire(col, M, 'WireGround', pts, 'black', 0.26 * MM)
    # supply to the pots' top lugs: a short red daisy chain
    pts = []
    for i, lugs in enumerate(lugs_all):
        gx, gy = lugs[0]
        pts += [(gx - 2, gy, zb + 2.0 * MM), (gx + 160, gy - 18, zb + 3.0 * MM)]
        solder(col, M, f'SolderV{i}', gx - 2, gy, zb + 2.0 * MM, 0.55 * MM)
    pts += [(MOD['slot'][0] + 60, MOD['slot'][1] - 60, zb + 0.6 * MM)]
    wire(col, M, 'WireSupply', pts[:-1], 'red', 0.26 * MM)
    lx, ly = MOD['led']
    bm = bmesh.new()
    rings = [(scene.circle(lx, ly, 2.5 * MM, 32), zb + 1.0 * MM), (scene.circle(lx, ly, 2.5 * MM, 32), zb + 6.0 * MM)]
    for j in range(1, 7):
        th = (math.pi / 2) * j / 6
        rings.append((scene.circle(lx, ly, 2.5 * MM * math.cos(th), 32), zb + 6.0 * MM + 2.5 * MM * math.sin(th)))
    scene.loft(bm, rings)
    obj('ModLed', bm, col, M['led'], smooth=True)
    wire(col, M, 'WireLed', [(lx + 20, ly + 40, zb + 1.0 * MM), (lx + 60, ly + 120, zb + 2.5 * MM),
                             (lx - 120, MOD['slot'][3] + 60, zb + 0.5 * MM)], 'red', 0.26 * MM)
    return col
