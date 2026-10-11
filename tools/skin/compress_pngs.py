# Shrinks the default skin's PNGs (run after build.js/package.py/texture_sprites.py/render_encoder2.py have written
# them into skins/default/nd2-default/). The grained animation sheets (value encoder, MASTER LEVEL knob) become
# 256-colour images with dithering, which cannot be told apart on the panel at half their size; every other PNG is
# recompressed losslessly. Needs: pip install pyoxipng imagequant
import os
import sys
import imagequant
import oxipng
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'skins', 'default', 'nd2-default')
PALETTE = {'ND2Encoder.png', 'ND2MasterKnob.png'}

total0 = total1 = 0
for f in sorted(os.listdir(SKIN)):
    if not f.lower().endswith('.png'):
        continue
    p = os.path.join(SKIN, f)
    before = os.path.getsize(p)
    if f in PALETTE and Image.open(p).mode != 'P':
        q = imagequant.quantize_pil_image(Image.open(p).convert('RGBA'), dithering_level=1.0, max_colors=256,
                                          min_quality=0, max_quality=100)
        q.save(p)
    oxipng.optimize(p, level=4, strip=oxipng.StripChunks.safe())
    after = os.path.getsize(p)
    total0 += before; total1 += after
    print(f'{f:28s} {before / 1e6:6.2f} -> {after / 1e6:6.2f} MB')
print(f'{"total":28s} {total0 / 1e6:6.2f} -> {total1 / 1e6:6.2f} MB')
