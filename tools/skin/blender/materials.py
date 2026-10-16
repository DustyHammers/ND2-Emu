# Materials. The panel paint takes its inks from masks.py's maps (aligned 1:1 with the canvas), so the
# shader maps world x, y straight to the art. Ink colours live in RGB nodes named col_<ink> so a later step
# can set calibrated values without rebuilding anything.
import bpy

import dims as D

# linear albedo guesses (from the art's sRGB); calibration replaces them
INK_ALBEDO = {
    'red': (0.40, 0.010, 0.019),
    'black': (0.018, 0.018, 0.019),
    'tag': (0.58, 0.010, 0.021),
    'white': (0.74, 0.72, 0.72),
}
PAINT = dict(
    rough_red=0.45, rough_ink=0.55,        # base layer (mostly diffuse)
    spec_level=0.12,                        # base layer specular (the coat carries most of the sheen)
    # coat weight on the red body and on the printed inks: run 1 (1.0 everywhere) left the black print 48-130
    # against the photo's 38 at zero albedo, so the gloss is cut to what the calibration can reach
    coat_red=0.55, coat_ink=0.35, coat_rough_red=0.30, coat_rough_ink=0.45, coat_ior=1.5,
    peel_wavelength=0.6 * D.MM,             # orange peel cell size
    peel_distance=0.15,                     # bump distance (px): about 1.4 degrees of slope
    ink_distance=0.12,                      # printed relief (px)
    lip_distance=0.35,                      # build-up lip (px)
    smudge_size=30 * D.MM, smudge_amount=0.06,
    virtual_cam_height=1000 * D.MM,         # reflections as if seen from 1 m above the panel centre
)


def _n(nt, kind, name=None, loc=(0, 0), **inputs):
    n = nt.nodes.new(kind)
    if name:
        n.name = n.label = name
    n.location = loc
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def _vm(nt, op, a, b=None, loc=(0, 0), name=None):
    n = nt.nodes.new('ShaderNodeVectorMath')
    n.operation = op
    n.location = loc
    if name:
        n.name = n.label = name
    _link(nt, a, n.inputs[0])
    if b is not None:
        _link(nt, b, n.inputs[1])
    return n


def _m(nt, op, a, b=None, loc=(0, 0), name=None):
    n = nt.nodes.new('ShaderNodeMath')
    n.operation = op
    n.location = loc
    if name:
        n.name = n.label = name
    _link(nt, a, n.inputs[0])
    if b is not None:
        _link(nt, b, n.inputs[1])
    return n


def _link(nt, src, dst):
    """src: an output socket, a node (first output) or a constant."""
    if src is None:
        return
    if isinstance(src, bpy.types.Node):
        src = src.outputs[0]
    if isinstance(src, bpy.types.NodeSocket):
        nt.links.new(src, dst)
    else:
        dst.default_value = src


def _image(nt, path, loc, name):
    img = bpy.data.images.load(path, check_existing=True)
    img.colorspace_settings.name = 'Non-Color'
    img.alpha_mode = 'CHANNEL_PACKED'
    t = nt.nodes.new('ShaderNodeTexImage')
    t.image, t.interpolation, t.extension = img, 'Cubic', 'EXTEND'
    t.name = t.label = name
    t.location = loc
    return t


def paint(maps_dir, peel_strips=None):
    """peel_strips: None, or (x_a, x_b) in art px: peel 0.5x left of x_a, 1x up to x_b, 2x beyond (test only)."""
    P = PAINT
    m = bpy.data.materials.new('Paint')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = _n(nt, 'ShaderNodeOutputMaterial', loc=(1600, 0))
    bsdf = _n(nt, 'ShaderNodeBsdfPrincipled', 'paint_bsdf', (1300, 0))
    nt.links.new(bsdf.outputs[0], out.inputs['Surface'])

    geo = _n(nt, 'ShaderNodeNewGeometry', loc=(-1800, 0))
    uv = _vm(nt, 'MULTIPLY_ADD', geo.outputs['Position'], (1 / D.CANVAS_W, 1 / D.CANVAS_H, 0), (-1600, 200))
    uv.inputs[2].default_value = (0, 1, 0)
    inks = _image(nt, f'{maps_dir}/inks.png', (-1400, 300), 'inks')
    _link(nt, uv.outputs[0], inks.inputs['Vector'])
    sep = _n(nt, 'ShaderNodeSeparateColor', loc=(-1100, 300))
    _link(nt, inks.outputs['Color'], sep.inputs[0])
    masks = {'red': sep.outputs[0], 'black': sep.outputs[1], 'tag': sep.outputs[2], 'white': inks.outputs['Alpha']}

    # base colour = sum of ink albedo x mask
    acc = None
    for i, (ink, alb) in enumerate(INK_ALBEDO.items()):
        c = _n(nt, 'ShaderNodeRGB', 'col_' + ink, (-1100, -100 - 200 * i))
        c.outputs[0].default_value = (*alb, 1)
        s = _vm(nt, 'SCALE', c.outputs[0], None, (-850, -100 - 200 * i))
        _link(nt, masks[ink], s.inputs['Scale'])
        acc = s if acc is None else _vm(nt, 'ADD', acc.outputs[0], s.outputs[0], (-650, -100 - 200 * i))
    _link(nt, acc.outputs[0], bsdf.inputs['Base Color'])

    ink_frac = _m(nt, 'SUBTRACT', 1.0, masks['red'], (-850, 600))
    # handling smudges: low-frequency noise in the gloss
    smudge = _n(nt, 'ShaderNodeTexNoise', 'smudge', (-1400, -1000))
    _link(nt, geo.outputs['Position'], smudge.inputs['Vector'])
    smudge.inputs['Scale'].default_value = 1 / P['smudge_size']
    smudge.inputs['Detail'].default_value = 2
    sm = _m(nt, 'MULTIPLY', _m(nt, 'SUBTRACT', smudge.outputs['Fac'], 0.5, (-1150, -1000)), P['smudge_amount'] * 2, (-950, -1000))

    def mix_rough(a, b, add=None, loc=(0, 0)):
        r = _m(nt, 'MULTIPLY_ADD', ink_frac, b - a, loc)
        r.inputs[2].default_value = a
        return _m(nt, 'ADD', r, add, (loc[0] + 200, loc[1])) if add is not None else r

    _link(nt, mix_rough(P['rough_red'], P['rough_ink'], sm, (-600, 600)).outputs[0], bsdf.inputs['Roughness'])
    _link(nt, mix_rough(P['coat_rough_red'], P['coat_rough_ink'], sm, (-600, 800)).outputs[0], bsdf.inputs['Coat Roughness'])
    bsdf.inputs['Specular IOR Level'].default_value = P['spec_level']
    _link(nt, mix_rough(P['coat_red'], P['coat_ink'], None, (-600, 1000)).outputs[0], bsdf.inputs['Coat Weight'])
    bsdf.inputs['Coat IOR'].default_value = P['coat_ior']

    # heights: printed relief, hole lips, orange peel (with optional test strips)
    ink_h = _image(nt, f'{maps_dir}/ink_height.png', (-1400, -1400), 'ink_height')
    lip_h = _image(nt, f'{maps_dir}/lip_height.png', (-1400, -1700), 'lip_height')
    for t in (ink_h, lip_h):
        _link(nt, uv.outputs[0], t.inputs['Vector'])
    peel = _n(nt, 'ShaderNodeTexNoise', 'peel', (-1400, -2000))
    _link(nt, geo.outputs['Position'], peel.inputs['Vector'])
    peel.inputs['Scale'].default_value = 1 / P['peel_wavelength']
    peel.inputs['Detail'].default_value = 3
    peel.inputs['Roughness'].default_value = 0.55
    peel_d = _n(nt, 'ShaderNodeValue', 'peel_distance', (-1150, -2200))
    peel_d.outputs[0].default_value = P['peel_distance']
    peel_dist = peel_d.outputs[0]
    if peel_strips:
        sx = _n(nt, 'ShaderNodeSeparateXYZ', loc=(-1400, -2400))
        _link(nt, geo.outputs['Position'], sx.inputs[0])
        a = _m(nt, 'GREATER_THAN', sx.outputs[0], peel_strips[0], (-1150, -2400))
        b = _m(nt, 'GREATER_THAN', sx.outputs[0], peel_strips[1], (-1150, -2600))
        f = _m(nt, 'ADD', _m(nt, 'MULTIPLY', a, 0.5, (-950, -2400)), _m(nt, 'ADD', b, 0.5, (-950, -2600)), (-750, -2500))
        peel_dist = _m(nt, 'MULTIPLY', f, peel_d.outputs[0], (-550, -2400)).outputs[0]

    def bump_chain(normal_in, x):
        n = normal_in
        for k, (h, dist) in enumerate(((ink_h.outputs['Color'], P['ink_distance']), (lip_h.outputs['Color'], P['lip_distance']),
                                       (peel.outputs['Fac'], peel_dist))):
            b = nt.nodes.new('ShaderNodeBump')
            b.location = (x + 180 * k, -1600)
            b.inputs['Strength'].default_value = 1.0
            _link(nt, dist, b.inputs['Distance'])
            _link(nt, h, b.inputs['Height'])
            if n is not None:
                _link(nt, n, b.inputs['Normal'])
            n = b.outputs['Normal']
        return n

    _link(nt, bump_chain(None, 200), bsdf.inputs['Normal'])

    # coat: "virtual perspective" normal. With V from the surface to a camera 1 m above the canvas centre,
    # the perspective reflection direction is R = (-Vx, -Vy, Vz); the half vector of R and straight up tilts the
    # normal so the orthographic camera sees the reflection a perspective camera would. Applied as a tilt on
    # the geometric normal, so fillets and walls keep their own shape.
    cam = (D.CANVAS_W / 2, -D.CANVAS_H / 2, P['virtual_cam_height'])
    v = _vm(nt, 'NORMALIZE', _vm(nt, 'SUBTRACT', cam, geo.outputs['Position'], (-1600, -2900)), None, (-1400, -2900))
    r = _vm(nt, 'MULTIPLY', v, (-1, -1, 1), (-1200, -2900))
    h = _vm(nt, 'NORMALIZE', _vm(nt, 'ADD', r, (0, 0, 1), (-1000, -2900)), None, (-800, -2900))
    tilt = _vm(nt, 'SUBTRACT', h, (0, 0, 1), (-600, -2900))
    nv = _vm(nt, 'NORMALIZE', _vm(nt, 'ADD', geo.outputs['Normal'], tilt, (-400, -2900)), None, (-200, -2900), 'virtual_normal')
    _link(nt, bump_chain(nv.outputs[0], 300), bsdf.inputs['Coat Normal'])
    return m


def simple(name, rgb, rough=0.5, spec=0.5, coat=0.0, coat_rough=0.1, transmission=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (*rgb, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular IOR Level'].default_value = spec
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Coat Roughness'].default_value = coat_rough
    b.inputs['Transmission Weight'].default_value = transmission
    return m


def assign_all(scene, maps_dir, peel_strips=None):
    mats = {
        'paint': paint(maps_dir, peel_strips),
        'interior': simple('Interior', (0.012, 0.012, 0.013), 0.8, 0.3),
        'lens': simple('DisplayLens', (0.05, 0.004, 0.004), 0.03, 0.5, transmission=0.0),
        'board': simple('DisplayBoard', (0.006, 0.004, 0.004), 0.7, 0.2),
        'grey': simple('ButtonGrey', (0.50, 0.50, 0.52), 0.45, 0.5),
        'black_plastic': simple('BlackPlastic', (0.025, 0.025, 0.027), 0.45, 0.5),
        'led': simple('LedLens', (0.35, 0.33, 0.30), 0.15, 0.5),
        'pointer': simple('Pointer', (0.80, 0.80, 0.80), 0.4, 0.5),
    }
    for ob in scene.objects:
        if ob.type != 'MESH':
            continue
        n = ob.name
        users = {c.name for c in ob.users_collection}
        if n == 'Plate':
            k = 'paint'
        elif n == 'Interior':
            k = 'interior'
        elif n == 'DisplayLens':
            k = 'lens'
        elif n == 'DisplayBoard':
            k = 'board'
        elif n == 'MasterPointer':
            k = 'pointer'
        elif 'Buttons grey' in users:
            k = 'grey'
        elif 'LEDs' in users:
            k = 'interior' if n.endswith('_die') else 'led'
        else:
            k = 'black_plastic'                  # black buttons, knob, encoder
        ob.data.materials.clear()
        ob.data.materials.append(mats[k])
    return mats
