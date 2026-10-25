# Flat-area masks shared by the photo targets and the post step: nearly pure ink, eroded, away from the
# controls (and their shadows) and the canvas edge. inks: H x W x 4 float (masks.py's inks.png / 255).
import numpy as np
from scipy import ndimage as nd

from layout import read_rcss_plain

INKS = ('red', 'black', 'tag', 'white')


def flat_masks(inks):
    H, W = inks.shape[:2]
    rects, kinds = read_rcss_plain()
    keep = np.ones((H, W), bool)
    keep[:40], keep[-40:], keep[:, :40], keep[:, -40:] = False, False, False, False
    yy, xx = np.ogrid[:H, :W]
    for cid, (cx, cy, w, h) in rects.items():
        k = kinds.get(cid, ('', ''))[0]
        if k in ('knob', 'encoder'):
            m = {'knob': 110, 'encoder': 240}[k]
            keep &= (xx - cx) ** 2 + (yy - cy) ** 2 > m * m
        else:
            m = 24
            keep[int(max(cy - h / 2 - m, 0)):int(cy + h / 2 + m), int(max(cx - w / 2 - m, 0)):int(cx + w / 2 + m)] = False
    flat = {}
    for i, ink in enumerate(INKS):
        er = {'red': 10, 'black': 10, 'tag': 2, 'white': 2}[ink]
        flat[ink] = nd.binary_erosion(inks[..., i] > 0.97, iterations=er) & keep
    return flat
