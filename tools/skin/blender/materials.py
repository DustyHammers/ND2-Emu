# Materials. The panel paint takes its inks from masks.py's maps (aligned 1:1 with the canvas), so the
# shader maps world x, y straight to the art. Ink colours live in RGB nodes named col_<ink> so a later step
# can set calibrated values without rebuilding anything.
import bpy

import dims as D

# linear albedo guesses (from the art's sRGB); calibration replaces them
INK_ALBEDO = {                      # calibrated to the owner's photo (lighting C, Standard view, +0.72 EV)
    'red': (0.319, 0.008, 0.015),
    'black': (0.009, 0.009, 0.010),
    'tag': (0.319, 0.008, 0.015),     # the body paint (label windows show it through the black print)
    'white': (0.873, 0.841, 0.838),
}
PAINT = dict(
    rough_red=0.45, rough_ink=0.55,        # base layer (mostly diffuse)
    spec_level=0.12,                        # base layer specular (the coat carries most of the sheen)
    # coat weight on the red body and on the printed inks: run 1 (1.0 everywhere) left the black print 48-130
    # against the photo's 38 at zero albedo, so the gloss is cut to what the calibration can reach
    # the reference photos show a satin finish with only a faint sheen
    # owner: a very fine orange peel with a slight shine that sparkles in places
    coat_red=0.38, coat_ink=0.28, coat_rough_red=0.26, coat_rough_ink=0.34, coat_ior=1.5,
    peel_wavelength=0.30 * D.MM,            # orange peel cell size (very fine)
    peel_distance=0.20,                     # bump distance (px): a visible fine stipple, like the photos
    mottle=0.035,                           # powder-coat mottle: +/- share of the paint colour
    # sparkle: a small share of the coat's grains sit tilted and catch the light as tiny glints
    sparkle_size=0.09 * D.MM,               # grain spacing
    sparkle_density=0.08,                   # share of grains that are tilted
    sparkle_dot=0.40,                       # glint size within its grain (share of the spacing)
    sparkle_tilt=0.75,                      # how far a tilted grain leans (about 35 degrees at most)
    sparkle_smooth=0.20,                    # tilted grains are this much glossier, so their glints are crisp
    virtual_perspective=False,              # only for an orthographic skin camera
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

    # label windows are the body's red paint showing through the black print: same paint as the body
    ink_frac = _m(nt, 'SUBTRACT', _m(nt, 'SUBTRACT', 1.0, masks['red'], (-1000, 600)), masks['tag'], (-850, 600))
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
    coat_n = bump_chain(nv.outputs[0] if P['virtual_perspective'] else None, 300)
    # sparkle: Voronoi grains; a random few, and only a dot in each, get a random tilt in the coat's normal
    vor = nt.nodes.new('ShaderNodeTexVoronoi')
    vor.name = vor.label = 'sparkle'
    vor.voronoi_dimensions = '3D'
    vor.location = (300, -2600)
    _link(nt, geo.outputs['Position'], vor.inputs['Vector'])
    vor.inputs['Scale'].default_value = 1.0 / P['sparkle_size']
    vor.inputs['Randomness'].default_value = 1.0
    rnd = _vm(nt, 'MULTIPLY_ADD', vor.outputs['Color'], (2.0, 2.0, 2.0), (520, -2600))
    rnd.inputs[2].default_value = (-1.0, -1.0, -1.0)
    sc_ = nt.nodes.new('ShaderNodeSeparateColor')
    sc_.location = (520, -2800)
    _link(nt, vor.outputs['Color'], sc_.inputs[0])
    pick = _m(nt, 'GREATER_THAN', sc_.outputs[0], 1.0 - P['sparkle_density'], (720, -2800))
    dot = _m(nt, 'LESS_THAN', vor.outputs['Distance'], P['sparkle_dot'], (720, -2950))
    amt = _m(nt, 'MULTIPLY', _m(nt, 'MULTIPLY', pick, dot, (900, -2850)), P['sparkle_tilt'], (1080, -2850))
    tilt = _vm(nt, 'SCALE', rnd.outputs[0], None, (1080, -2650))
    _link(nt, amt.outputs[0], tilt.inputs['Scale'])
    spark_n = _vm(nt, 'NORMALIZE', _vm(nt, 'ADD', coat_n, tilt.outputs[0], (1260, -2650)), None, (1420, -2650), 'sparkle_normal')
    _link(nt, spark_n.outputs[0], bsdf.inputs['Coat Normal'])
    # glossier grains: subtract from the coat roughness where a grain is tilted
    cr = bsdf.inputs['Coat Roughness'].links[0].from_socket
    grain = _m(nt, 'MULTIPLY', pick, dot, (900, -3050))
    _link(nt, _m(nt, 'MAXIMUM', _m(nt, 'MULTIPLY_ADD', grain, -P['sparkle_smooth'], (1080, -3050)), 0.03, (1260, -3050)).outputs[0],
          bsdf.inputs['Coat Roughness'])
    nt.nodes[-2].inputs[2].default_value = 0.0
    _link(nt, cr, nt.nodes[-2].inputs[2])
    # powder-coat mottle on the paint colour
    mot = nt.nodes.new('ShaderNodeTexNoise')
    mot.location = (-1400, -3300)
    _link(nt, geo.outputs['Position'], mot.inputs['Vector'])
    mot.inputs['Scale'].default_value = 1.0 / (0.5 * D.MM)
    mot.inputs['Detail'].default_value = 4
    base_in = bsdf.inputs['Base Color'].links[0].from_socket
    f = _m(nt, 'MULTIPLY_ADD', mot.outputs['Fac'], 2 * P['mottle'], (-1150, -3300))
    f.inputs[2].default_value = 1.0 - P['mottle']
    _link(nt, _vm(nt, 'SCALE', base_in, None, (-950, -3300)).outputs[0], bsdf.inputs['Base Color'])
    _link(nt, f.outputs[0], nt.nodes[-1].inputs['Scale'])
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


def emissive_board(name, emission_png, colour=(1.0, 0.012, 0.004), strength=12.0, unlit=0.004):
    """Black display board; the segment map (R = lit, G = unlit segments) lights it from below the lens."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (0.004, 0.003, 0.003, 1)
    b.inputs['Roughness'].default_value = 0.6
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    uv = _vm(nt, 'MULTIPLY_ADD', geo.outputs['Position'], (1 / D.CANVAS_W, 1 / D.CANVAS_H, 0))
    uv.inputs[2].default_value = (0, 1, 0)
    t = _image(nt, emission_png, (-400, 300), 'segments')
    _link(nt, uv.outputs[0], t.inputs['Vector'])
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    _link(nt, t.outputs['Color'], sep.inputs[0])
    lvl = _m(nt, 'MULTIPLY_ADD', sep.outputs[1], unlit)
    lvl.inputs[2].default_value = 0.0
    tot = _m(nt, 'ADD', sep.outputs[0], lvl)
    b.inputs['Emission Color'].default_value = (*colour, 1)
    _link(nt, _m(nt, 'MULTIPLY', tot, strength).outputs[0], b.inputs['Emission Strength'])
    return m


def knob_with_pointer(name):
    """Black satin plastic with the white pointer stripe: in object space the knob points along +y; the stripe is
    a band of constant width along that direction, across the top (outside pointer_r0) and down the side."""
    K = D.MASTER
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes.get('Principled BSDF')
    b.inputs['Roughness'].default_value = 0.45
    b.inputs['Specular IOR Level'].default_value = 0.5
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    _link(nt, tc.outputs['Object'], sep.inputs[0])
    across = _m(nt, 'ABSOLUTE', sep.outputs['X'])                  # distance from the pointer's centre line
    band = _m(nt, 'LESS_THAN', across, K['pointer_w'] / 2)
    ahead = _m(nt, 'GREATER_THAN', sep.outputs['Y'], K['pointer_r0'] * K['r_top'])
    stripe = _m(nt, 'MULTIPLY', band, ahead)
    mix = nt.nodes.new('ShaderNodeMix')
    mix.data_type = 'RGBA'
    _link(nt, stripe.outputs[0], mix.inputs['Factor'])
    mix.inputs['A'].default_value = (0.020, 0.020, 0.022, 1)
    mix.inputs['B'].default_value = (0.82, 0.82, 0.80, 1)
    _link(nt, mix.outputs['Result'], b.inputs['Base Color'])
    return m


def metal(name, rgb, rough):
    m = simple(name, rgb, rough, 0.5)
    m.node_tree.nodes.get('Principled BSDF').inputs['Metallic'].default_value = 1.0
    return m


def assign_all(scene, maps_dir, peel_strips=None, emission_png=None):
    lens = simple('DisplayLens', (0.55, 0.05, 0.05), 0.04, 0.5, transmission=1.0)
    mats = {
        'paint': paint(maps_dir, peel_strips),
        'interior': simple('Interior', (0.012, 0.012, 0.013), 0.8, 0.3),
        'lens': lens,
        'board': emissive_board('DisplayBoard', emission_png) if emission_png else simple('DisplayBoard', (0.006, 0.004, 0.004), 0.7, 0.2),
        'grey': simple('ButtonGrey', (0.42, 0.42, 0.44), 0.55, 0.5),
        'black_plastic': simple('BlackPlastic', (0.022, 0.022, 0.024), 0.5, 0.5),
        'led_red': simple('LedRed', (0.30, 0.06, 0.05), 0.12, 0.5, transmission=0.3),
        'led_green': simple('LedGreen', (0.10, 0.20, 0.08), 0.12, 0.5, transmission=0.3),
        'pointer': simple('Pointer', (0.80, 0.80, 0.80), 0.4, 0.5),
        'knob': knob_with_pointer('MasterKnob'),
        'base': simple('BasePlate', (0.02, 0.02, 0.022), 0.5, 0.4),
        'rubber': simple('Rubber', (0.012, 0.012, 0.012), 0.85, 0.3),
        'screw': metal('BlackOxide', (0.06, 0.06, 0.065), 0.35),
        'nickel': metal('Nickel', (0.62, 0.60, 0.58), 0.28),
        'hole': simple('HoleBlack', (0.004, 0.004, 0.004), 0.9, 0.1),
        'backdrop': simple('Backdrop', (0.16, 0.16, 0.17), 0.9, 0.2),
    }
    green = {'l_6', 'l_14', 'l_15', 'l_23', 'l_22', 'l_30'}
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
        elif n == 'MasterKnob':
            k = 'knob'
        elif n in ('Base', 'Chassis'):
            k = 'base'
        elif n.startswith('Foot'):
            k = 'rubber'
        elif n.startswith('Screw'):
            k = 'screw'
        elif n.startswith(('JackNut', 'DinShell')):
            k = 'nickel'
        elif n.startswith(('JackHole', 'DinPins', 'DcHole')):
            k = 'hole'
        elif n.startswith(('DinFace', 'DcBody')):
            k = 'black_plastic'
        elif n == 'Backdrop':
            k = 'backdrop'
        elif 'Buttons grey' in users:
            k = 'grey'
        elif 'LEDs' in users:
            k = 'interior' if n.endswith('_die') else ('led_green' if n in green else 'led_red')
        else:
            k = 'black_plastic'                  # black buttons, knob, encoder
        ob.data.materials.clear()
        ob.data.materials.append(mats[k])
    return mats
