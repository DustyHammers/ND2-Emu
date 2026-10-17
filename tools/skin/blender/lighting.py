# Lighting variants for the skin (section 5 of the spec):
#  - a large soft key from the top left (strongest), hidden from glossy rays, plus a glossy-only twin of it at
#    `twin` of its strength (control sprites use twin = 1.0)
#  - a weak rim from the top right
#  - a studio HDR world (downloaded at run time, never committed); plain grey if it is missing
#  - an overhead softbox seen only in reflections, brightest toward the top left
# Light levels are set as irradiance on the panel at the canvas centre (E = 1 gives about 1/pi radiance off a
# white diffuse surface), so variants compare at the same exposure.
import math
import os

import bpy
from mathutils import Vector

import dims as D

CENTRE = Vector((D.CANVAS_W / 2, -D.CANVAS_H / 2, 0))

VARIANTS = {
    # H: the studio HDR environment alone (make_hdr.py): softboxes, strips and walls light and reflect in it
    'H': dict(hdr_only=True, exposure_ev=-1.35, world=1.0, world_rot=0),   # -0.35: the back strip's extra light
    # P: matched to the owner's front three-quarter product photo: a large, high soft key from the front left,
    # a broad fill from the right, short soft shadows, little sheen
    'P': dict(exposure_ev=0.0, key_az=125, key_el=62, key_size_mm=1400, key_dist_mm=1500, key_E=2.0, twin=0.2,
              rim_az=20, rim_el=45, rim_size_mm=1200, rim_E=0.55,
              world=0.55, world_rot=60, box_E=0.35, box_mm=(1200, 900), box_h_mm=1300, box_grad=0.3),
    # A: the spec's set-up
    'A': dict(key_az=135, key_el=40, key_size_mm=600, key_dist_mm=1500, key_E=2.4, twin=0.35,
              rim_az=45, rim_el=30, rim_size_mm=300, rim_E=0.30,
              world=0.35, world_rot=0, box_E=0.9, box_mm=(900, 600), box_h_mm=1200, box_grad=0.6),
    # B: higher, broader key and a stronger softbox: flatter, more even sheen
    'B': dict(key_az=135, key_el=60, key_size_mm=900, key_dist_mm=1500, key_E=2.2, twin=0.35,
              rim_az=45, rim_el=35, rim_size_mm=400, rim_E=0.20,
              world=0.45, world_rot=90, box_E=1.4, box_mm=(1200, 800), box_h_mm=1200, box_grad=0.4),
    # C: lower, smaller key and more rim: more relief, more contrast
    'C': dict(key_az=140, key_el=25, key_size_mm=400, key_dist_mm=1500, key_E=2.6, twin=0.35,
              rim_az=40, rim_el=25, rim_size_mm=300, rim_E=0.45,
              world=0.25, world_rot=180, box_E=0.6, box_mm=(900, 600), box_h_mm=1200, box_grad=0.8),
}

# area light power for irradiance E at distance d along its axis (calibrated on a test render)
_K = 0.29


def _dir(az, el):
    """Unit vector from the panel toward a light at azimuth az (0 = +x, 90 = top of the canvas) and elevation el."""
    a, e = math.radians(az), math.radians(el)
    return Vector((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))


def _area(sc, name, direction, dist, size, E, coll):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.shape = 'DISK'
    ld.size = size
    ld.energy = E * dist * dist / _K
    ob = bpy.data.objects.new(name, ld)
    coll.objects.link(ob)
    ob.location = CENTRE + direction * dist
    ob.rotation_euler = (CENTRE - ob.location).to_track_quat('-Z', 'Y').to_euler()
    return ob


def _vis(ob, camera=False, diffuse=True, glossy=True, transmission=True, scatter=True, shadow=True):
    ob.visible_camera, ob.visible_diffuse, ob.visible_glossy = camera, diffuse, glossy
    ob.visible_transmission, ob.visible_volume_scatter, ob.visible_shadow = transmission, scatter, shadow


def setup(sc, variant='A', hdr=None, twin=None):
    V = dict(VARIANTS[variant])
    if twin is not None:
        V['twin'] = twin
    coll = bpy.data.collections.new('Lights')
    sc.collection.children.link(coll)
    mm = D.MM
    if V.get('hdr_only'):
        return _world(sc, V, hdr)

    key_dir = _dir(V['key_az'], V['key_el'])
    key = _area(sc, 'Key', key_dir, V['key_dist_mm'] * mm, V['key_size_mm'] * mm, V['key_E'], coll)
    _vis(key, glossy=False)
    twin_ob = _area(sc, 'KeyGlossTwin', key_dir, V['key_dist_mm'] * mm, V['key_size_mm'] * mm, V['key_E'] * V['twin'], coll)
    _vis(twin_ob, diffuse=False, transmission=False, scatter=False, shadow=True)
    rim = _area(sc, 'Rim', _dir(V['rim_az'], V['rim_el']), V['key_dist_mm'] * mm, V['rim_size_mm'] * mm, V['rim_E'], coll)
    _vis(rim)

    # overhead softbox, glossy rays only; emission brightest at its top-left corner
    bw, bh = V['box_mm'][0] * mm, V['box_mm'][1] * mm
    me = bpy.data.meshes.new('Softbox')
    me.from_pydata([(-bw / 2, -bh / 2, 0), (bw / 2, -bh / 2, 0), (bw / 2, bh / 2, 0), (-bw / 2, bh / 2, 0)], [], [(3, 2, 1, 0)])
    box = bpy.data.objects.new('Softbox', me)
    coll.objects.link(box)
    box.location = CENTRE + Vector((-0.1 * bw, 0.1 * bh, V['box_h_mm'] * mm))
    m = bpy.data.materials.new('SoftboxEmit')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    o = nt.nodes.new('ShaderNodeOutputMaterial')
    em = nt.nodes.new('ShaderNodeEmission')
    tc = nt.nodes.new('ShaderNodeTexCoord')
    grad = nt.nodes.new('ShaderNodeVectorMath')
    grad.operation = 'DOT_PRODUCT'
    nt.links.new(tc.outputs['Object'], grad.inputs[0])
    grad.inputs[1].default_value = (-1 / bw, 1 / bh, 0)          # +1 at the top-left corner, -1 opposite
    mr = nt.nodes.new('ShaderNodeMath')
    mr.operation = 'MULTIPLY_ADD'
    nt.links.new(grad.outputs['Value'], mr.inputs[0])
    mr.inputs[1].default_value = V['box_grad'] * V['box_E']
    mr.inputs[2].default_value = V['box_E']
    nt.links.new(mr.outputs[0], em.inputs['Strength'])
    nt.links.new(em.outputs[0], o.inputs['Surface'])
    box.data.materials.append(m)
    _vis(box, camera=False, diffuse=False, glossy=True, transmission=False, scatter=False, shadow=False)

    return _world(sc, V, hdr)


def _world(sc, V, hdr):
    w = bpy.data.worlds.new('World')
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes['Background']
    bg.inputs['Strength'].default_value = V['world']
    if hdr and os.path.exists(hdr):
        env = nt.nodes.new('ShaderNodeTexEnvironment')
        env.image = bpy.data.images.load(hdr)
        mp = nt.nodes.new('ShaderNodeMapping')
        tc = nt.nodes.new('ShaderNodeTexCoord')
        mp.inputs['Rotation'].default_value = (0, 0, math.radians(V['world_rot']))
        nt.links.new(tc.outputs['Generated'], mp.inputs['Vector'])
        nt.links.new(mp.outputs[0], env.inputs['Vector'])
        nt.links.new(env.outputs['Color'], bg.inputs['Color'])
    else:
        bg.inputs['Color'].default_value = (0.2, 0.2, 0.2, 1)
        print('lighting: no HDR found, using a plain grey world')
    return V
