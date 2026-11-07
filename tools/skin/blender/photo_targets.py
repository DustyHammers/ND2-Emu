# Target colours for the paint calibration, measured on a reference photo already warped into the art frame
# (2x). Writes numbers only (median sRGB of each ink's flat areas), so the photo itself never leaves its folder.
#   python3 photo_targets.py MAPS_DIR PHOTO_IN_ART2X OUT_JSON
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calib import INKS, flat_masks

maps, photo, out = sys.argv[1:4]
inks = np.asarray(Image.open(os.path.join(maps, 'inks.png'))).astype(np.float32) / 255
ph = np.asarray(Image.open(photo).convert('RGB')).astype(np.float32)
flat = flat_masks(inks)
res = {'source': 'owner top-down reference photo, fitted to the art (0.7 dp rms)', 'srgb_median': {}, 'pixels': {},
       'srgb_p25': {}, 'srgb_p75': {}}
from scipy import ndimage as nd

# the lettering in the art is not pixel-for-pixel where the photo's is, so tag and white ink are picked by colour
# inside their (dilated) areas: tag = clearly red pixels, white = bright, unsaturated pixels (glyph cores: the
# brightest half, since the photo's strokes are only a few pixels wide)
mx, mn = ph.max(-1), ph.min(-1)
sel = dict(flat)
sel['tag'] = nd.binary_dilation(inks[..., 2] > 0.5, iterations=6) & (ph[..., 0] - np.maximum(ph[..., 1], ph[..., 2]) > 70)
white_area = nd.binary_dilation(inks[..., 3] > 0.5, iterations=6) & (mn > 120) & (mx - mn < 40)
lum = ph.mean(-1)
sel['white'] = white_area & (lum >= np.median(lum[white_area]))
for ink in INKS:
    px = ph[sel[ink]]
    res['pixels'][ink] = int(len(px))
    res['srgb_median'][ink] = np.median(px, 0).round(1).tolist()
    res['srgb_p25'][ink] = np.percentile(px, 25, 0).round(1).tolist()
    res['srgb_p75'][ink] = np.percentile(px, 75, 0).round(1).tolist()
json.dump(res, open(out, 'w'), indent=1)
print(json.dumps(res['srgb_median']), res['pixels'])
