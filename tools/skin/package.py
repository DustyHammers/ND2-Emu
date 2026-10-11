# Builds the default-skin package from the rendered layers, the sprite assets and controls.json.
import io, json, os, shutil, zipfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out')
PKG = os.path.join(HERE, 'out', 'ND2_default_skin')   # the package for GIMP work (not committed)
SKIN = 'nd2-default'
SK = os.path.join(PKG, SKIN)
shutil.rmtree(PKG, ignore_errors=True)
os.makedirs(SK)

# ---- background layers (bottom to top); 2x art on a 1725 x 1035 dp canvas
LAYERS = [('body', 'Red body'), ('panels', 'Black panel print'),
          ('display', 'Display window'), ('redprint', 'Red print: master scale, encoder ring'),
          ('tagboxes', 'Tag boxes (red and STORE)'), ('lines', 'Lines and brackets'), ('text', 'Lettering'),
          ('wordmark', 'Wordmark (placeholder)')]
LAYER_NAMES = {'body': 'Body', 'panels': 'Panels', 'display': 'Display', 'redprint': 'RedPrint',
               'tagboxes': 'TagBoxes', 'lines': 'Lines', 'text': 'Text', 'wordmark': 'Wordmark'}
def layer_file(i, l): return f'ND2Layer{i:02d}{LAYER_NAMES[l]}.png'
def layer_id(i, l): return f'layer_{i:02d}_{l}'
SPRITES = {n: n for n in ['ND2BlackButton.png', 'ND2GreyButton.png', 'ND2RedLed.png', 'ND2GreenLed.png', 'ND2MasterKnob.png',
                           'ND2Encoder.png']}
files = []
for i, (l, _) in enumerate(LAYERS, 1):
    fn = layer_file(i, l)
    shutil.copy(os.path.join(OUT, l + '.png'), os.path.join(SK, fn)); files.append(fn)

# ---- sprites
A = os.path.join(HERE, '..', '..', 'skins', 'default', 'nd2-default')   # the sprites live in the skin itself
for f, to in SPRITES.items():
    shutil.copy(os.path.join(A, f), os.path.join(SK, to))
SEGS = ['unlit', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'dp']
cell = Image.open(os.path.join(OUT, 'seg_unlit.png')).size                      # 176 x 212 px (2x of 88 x 106 dp)
seg = Image.new('RGBA', (cell[0] * len(SEGS), cell[1]), (0, 0, 0, 0))
for i, s in enumerate(SEGS): seg.alpha_composite(Image.open(os.path.join(OUT, f'seg_{s}.png')), (i * cell[0], 0))
seg.save(os.path.join(SK, 'ND2DisplaySegments.png'))
shutil.copy(os.path.join(OUT, 'trig.png'), os.path.join(SK, 'ND2TrigLamp.png'))     # 184 x 100 px (2x of 92 x 50 dp)

controls = json.load(open(os.path.join(OUT, 'controls.json')))

# ---- RCSS
def dp(v): return f'{round(v, 2):g}dp'
r = []
r.append('/* ND2 default skin. Canvas 1725 x 1035 dp; art at 2x (1 dp = 2 px). Generated from the layered source; */')
r.append('/* positions match the background layers, so move a control here and in the art together. */\n')
r.append('@spritesheet buttons_black { src: ND2BlackButton.png; resolution: 2x; btn_black_up: 0px 0px 244px 118px; btn_black_down: 0px 118px 244px 118px; }')
r.append('@spritesheet buttons_grey { src: ND2GreyButton.png; resolution: 2x; btn_grey_up: 0px 0px 244px 118px; btn_grey_down: 0px 118px 244px 118px; }')
r.append('@spritesheet leds_red { src: ND2RedLed.png; resolution: 1x; led_red_off: 0px 0px 31px 31px; led_red_on: 0px 31px 31px 31px; }')
r.append('@spritesheet leds_green { src: ND2GreenLed.png; resolution: 1x; led_green_off: 0px 0px 31px 31px; led_green_on: 0px 31px 31px 31px; }')
cw, ch = cell
r.append('@spritesheet segments { src: ND2DisplaySegments.png; resolution: 2x; ' +
         ' '.join(f'seg_{s}: {i * cw}px 0px {cw}px {ch}px;' for i, s in enumerate(SEGS)) + ' }')
r.append('@spritesheet master { src: ND2MasterKnob.png; resolution: 2x;\n' +
         '\n'.join(f'  master_{f:03d}: {(f % 14) * 240}px {(f // 14) * 240}px 240px 240px;' for f in range(128)) + '\n}')
r.append('@spritesheet encoder { src: ND2Encoder.png; resolution: 2x;\n' +
         '\n'.join(f'  enc_{f:03d}: {(f % 12) * 448}px {(f // 12) * 448}px 448px 448px;' for f in range(72)) + '\n}\n')
r.append('body { width: 1725dp; height: 1035dp; background-color: #2b2b2e; }')
r.append('div.layer { position: absolute; left: 0dp; top: 0dp; width: 1725dp; height: 1035dp; }')
for i, (l, _) in enumerate(LAYERS, 1):
    r.append(f'#{layer_id(i, l)} {{ decorator: image({layer_file(i, l)}); }}')
r.append('\npbutton, led, knob, encoder, segdigit, trigin { display: block; position: absolute; }')
r.append('pbutton { width: 122dp; height: 59dp; }')
r.append('pbutton.black { decorator: image(btn_black_up); }  pbutton.black.pressed, pbutton.black.held { decorator: image(btn_black_down); }')
r.append('pbutton.grey { decorator: image(btn_grey_up); }    pbutton.grey.pressed, pbutton.grey.held { decorator: image(btn_grey_down); }')
r.append('/* LEDs: the lamp fades in over the unlit lens; the runtime sets the lamp opacity from the OS\'s LED brightness */')
r.append('led { width: 22dp; height: 22dp; }')
r.append('led lamp { display: block; position: absolute; left: -3dp; top: -3dp; width: 28dp; height: 28dp; opacity: 0; }')
r.append('led.red { decorator: image(led_red_off); }      led.red lamp { decorator: image(led_red_on); }')
r.append('led.green { decorator: image(led_green_off); }  led.green lamp { decorator: image(led_green_on); }')
r.append('/* trigger inputs: the outline around the CHn lettering is shown while audio comes in on that input */')
r.append('trigin { width: 92dp; height: 50dp; }')
r.append('trigin lamp { display: block; position: absolute; left: 0dp; top: 0dp; width: 92dp; height: 50dp; opacity: 0; decorator: image(ND2TrigLamp.png); }')
r.append('knob#master { frames: 128; spriteprefix: master_; }')
r.append('encoder#value { frames: 72; detents: 24; spriteprefix: enc_; }')
r.append('/* display digit: unlit segments as the background, one child per segment, shown with class lit */')
r.append('segdigit { width: 88dp; height: 106dp; decorator: image(seg_unlit); }')
r.append('segdigit seg { display: block; position: absolute; left: 0dp; top: 0dp; width: 88dp; height: 106dp; visibility: hidden; }')
r.append('segdigit seg.lit { visibility: visible; }')
r.append(' '.join(f'segdigit seg.{s} {{ decorator: image(seg_{s}); }}' for s in SEGS[1:]))
r.append('')
for c in controls:
    r.append(f'#{c["id"]} {{ left: {dp(c["x"])}; top: {dp(c["y"])}; width: {dp(c["w"])}; height: {dp(c["h"])}; }}')
open(os.path.join(SK, SKIN + '.rcss'), 'w').write('\n'.join(r) + '\n')

# ---- RML
m = ['<rml>', '<head>', '\t<title>ND2 default</title>', f'\t<link type="text/rcss" href="{SKIN}.rcss"/>', '</head>', '<body>']
m.append('\t<!-- background art, bottom to top (each a full-canvas PNG at 2x; edit them in GIMP) -->')
for i, (l, d) in enumerate(LAYERS, 1):
    m.append(f'\t<div class="layer" id="layer_{i:02d}_{l}"/>\t<!-- {d} -->')
m.append('\t<!-- controls -->')
for c in controls:
    t = c['type']
    if t == 'pbutton': m.append(f'\t<pbutton id="{c["id"]}" button="{c["button"]}" class="{c["kind"]}"/>')
    elif t == 'led': m.append(f'\t<led id="{c["id"]}" code="{c["code"]}" class="{c["color"]}"><lamp/></led>')
    elif t == 'knob': m.append(f'\t<knob id="{c["id"]}" control="master"/>')
    elif t == 'encoder': m.append(f'\t<encoder id="{c["id"]}"/>')
    elif t == 'trigin': m.append(f'\t<trigin id="{c["id"]}" pad="{c["pad"]}"><lamp/></trigin>')
    elif t == 'segdigit': m.append(f'\t<segdigit id="{c["id"]}" pos="{c["pos"]}">' + ''.join(f'<seg class="{s}"/>' for s in SEGS[1:]) + '</segdigit>')
m += ['</body>', '</rml>']
open(os.path.join(SK, SKIN + '.rml'), 'w').write('\n'.join(m) + '\n')

# ---- preview and the GIMP file (OpenRaster: GIMP opens it with every layer)
prev = Image.new('RGBA', Image.open(os.path.join(OUT, 'body.png')).size, (43, 43, 46, 255))
for l, _ in LAYERS: prev.alpha_composite(Image.open(os.path.join(OUT, l + '.png')))
prev.alpha_composite(Image.open(os.path.join(OUT, 'controls.png')))
prev.convert('RGB').save(os.path.join(PKG, 'preview.png'))
W, H = Image.open(os.path.join(OUT, 'body.png')).size
stack = [('REFERENCE controls (do not export)', os.path.join(OUT, 'controls.png'), 'controls.png')] + \
        [(f'{layer_file(i, l)[:-4]} - {d}', os.path.join(SK, layer_file(i, l)), layer_file(i, l)) for i, (l, d) in enumerate(LAYERS, 1)][::-1]
merged = Image.new('RGBA', (W, H), (0, 0, 0, 0))
for _, p, _ in stack[::-1]: merged.alpha_composite(Image.open(p))
def png_bytes(im):
    b = io.BytesIO(); im.save(b, 'PNG'); return b.getvalue()
ora = os.path.join(PKG, 'nd2-default_layers.ora')
with zipfile.ZipFile(ora, 'w') as z:
    z.writestr(zipfile.ZipInfo('mimetype'), 'image/openraster', compress_type=zipfile.ZIP_STORED)
    xml = [f'<?xml version="1.0" encoding="UTF-8"?>', f'<image version="0.0.5" w="{W}" h="{H}" xres="144" yres="144">', '<stack>']
    for name, p, fn in stack:
        z.write(p, 'data/' + fn, compress_type=zipfile.ZIP_DEFLATED)
        xml.append(f'<layer name="{name}" src="data/{fn}" x="0" y="0" opacity="1.0" visibility="visible" composite-op="svg:src-over"/>')
    xml += ['</stack>', '</image>']
    z.writestr('stack.xml', '\n'.join(xml), compress_type=zipfile.ZIP_DEFLATED)
    z.writestr('mergedimage.png', png_bytes(merged), compress_type=zipfile.ZIP_DEFLATED)
    th = merged.copy(); th.thumbnail((256, 256)); z.writestr('Thumbnails/thumbnail.png', png_bytes(th), compress_type=zipfile.ZIP_DEFLATED)
shutil.copy(os.path.join(HERE, 'README_skin.txt'), os.path.join(PKG, 'README.txt'))
gif = os.path.join(HERE, 'out', 'encoder_turning.gif')
if os.path.exists(gif): shutil.copy(gif, os.path.join(PKG, 'encoder_turning.gif'))
src = os.path.join(PKG, '_source'); os.makedirs(src, exist_ok=True)
for f in ['skin_source.html', 'app_icon.html', 'build.js', 'package.py', 'texture_sprites.py', 'README_skin.txt']: shutil.copy(os.path.join(HERE, f), src)
enc = os.path.join(HERE, 'render_encoder2.py')
if os.path.exists(enc): shutil.copy(enc, src)
print('controls', len(controls), 'layers', len(LAYERS))
