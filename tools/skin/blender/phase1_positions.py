# Phase 1 check: the scene in flat grey, rendered at 1x, with every rcss rectangle drawn over it, plus a
# numeric check that each control's projected centre lands on its rcss centre.
#   python3 phase1_positions.py OUT_DIR [samples] [photo_in_art2x.png]
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

import dims as D
import scene

out = sys.argv[1]
samples = int(sys.argv[2]) if len(sys.argv) > 2 else 32
photo = sys.argv[3] if len(sys.argv) > 3 else None
os.makedirs(out, exist_ok=True)

rects, kinds = scene.build(frame_knob=64, frame_enc=0, pressed=('b_param5',))
sc = bpy.context.scene
grey = scene.material('flat_grey', (0.42, 0.42, 0.42), 0.7)
light_grey = scene.material('flat_grey_light', (0.62, 0.62, 0.62), 0.7)
dark = scene.material('flat_interior', (0.03, 0.03, 0.03), 0.9)
for ob in sc.objects:
    if ob.type != 'MESH':
        continue
    ob.data.materials.clear()
    if ob.name in ('Interior', 'DisplayBoard', 'DisplayLens'):
        ob.data.materials.append(dark)
    elif ob.name == 'MasterPointer':
        ob.data.materials.append(light_grey)
    else:
        ob.data.materials.append(grey)

# flat test lighting: soft key from the top left, dim grey world
w = bpy.data.worlds.new('World')
sc.world = w
w.use_nodes = True
w.node_tree.nodes['Background'].inputs['Color'].default_value = (0.25, 0.25, 0.25, 1)
w.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.6
ld = bpy.data.lights.new('Key', 'AREA')
ld.size = 3000
ld.energy = 3.5e8
key = bpy.data.objects.new('Key', ld)
sc.collection.objects.link(key)
key.location = (D.CANVAS_W / 2 - 4000, -D.CANVAS_H / 2 + 4000, 6000)
key.rotation_euler = (Vector((D.CANVAS_W / 2, -D.CANVAS_H / 2, 0)) - key.location).to_track_quat('-Z', 'Y').to_euler()

sc.render.engine = 'CYCLES'
sc.cycles.device = 'CPU'
sc.cycles.samples = samples
sc.cycles.use_denoising = True
sc.render.film_transparent = True
sc.view_settings.view_transform = 'Standard'
sc.render.resolution_percentage = int(os.environ.get('RES_PERCENT', '50'))   # 50 = 1x, 100 = 2x
sc.render.image_settings.file_format = 'PNG'
sc.render.image_settings.color_mode = 'RGBA'
sc.render.filepath = os.path.join(out, 'phase1_render_%dx.png' % (sc.render.resolution_percentage // 50))
bpy.ops.render.render(write_still=True)

# numeric check: projected bounding-box centre (plan) of each control vs the rcss centre, in dp
cam = sc.camera
res = []
names = {cid: cid for cid in rects}
for cid, (cx, cy, wd, ht) in rects.items():
    obn = {'master': 'MasterKnob', 'value': 'Encoder'}.get(cid, cid)
    ob = sc.objects.get(obn)
    if ob is None:
        continue
    ev = ob.evaluated_get(bpy.context.evaluated_depsgraph_get())
    vs = [ev.matrix_world @ v.co for v in ev.data.vertices]
    zmax = max(v.z for v in vs)
    top = [v for v in vs if v.z > zmax - 0.5 * (zmax - min(v.z for v in vs))]
    c = Vector((sum(v.x for v in top) / len(top), sum(v.y for v in top) / len(top), 0))
    p = world_to_camera_view(sc, cam, c)
    px, py = p.x * D.CANVAS_W, (1 - p.y) * D.CANVAS_H
    res.append((cid, round((px - cx) / 2, 3), round((py - cy) / 2, 3)))
worst = max(max(abs(a), abs(b)) for _, a, b in res)
json.dump({'controls': len(res), 'worst_dp': worst, 'offsets_dp': res}, open(os.path.join(out, 'phase1_centres.json'), 'w'), indent=1)
print('controls', len(res), 'worst centre offset dp', worst)
