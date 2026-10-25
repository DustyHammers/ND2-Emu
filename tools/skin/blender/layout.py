# Control layout from the skin's rcss and rml (no Blender needed): id -> (cx, cy, w, h) in art px.
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = os.path.join(HERE, '..', '..', '..', 'skins', 'default', 'nd2-default')


def read_rcss_plain(path=os.path.join(SKIN, 'nd2-default.rcss')):
    """id -> (cx, cy, w, h) in art px, plus the element class from the rml."""
    css = open(path).read()
    rects = {}
    for m in re.finditer(r'#(\w+) \{ left: ([\d.]+)dp; top: ([\d.]+)dp; width: ([\d.]+)dp; height: ([\d.]+)dp', css):
        x, y, w, h = (float(v) * 2 for v in m.groups()[1:])
        rects[m.group(1)] = (x + w / 2, y + h / 2, w, h)
    rml = open(os.path.join(os.path.dirname(path), 'nd2-default.rml')).read()
    kinds = {}
    for m in re.finditer(r'<(pbutton|led|knob|encoder|segdigit|action) id="(\w+)"([^>]*)>', rml):
        c = re.search(r'class="(\w+)"', m.group(3))
        kinds[m.group(2)] = (m.group(1), c.group(1) if c else '')
    return rects, kinds
