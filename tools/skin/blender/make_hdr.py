# Builds a studio HDR environment (equirectangular, linear, 32-bit EXR) for the skin renders, so no download is
# needed: grey walls with a soft horizon, a dark floor, and light sources as soft-edged panels.
#   python3 make_hdr.py OUT.exr [width=2048]
# Directions use Blender's world convention: +x right, +y back (away from the viewer), +z up; the unit's front
# faces -y. The file is generated at run time and never committed.
import math
import sys

import numpy as np
import OpenEXR

OUT = sys.argv[1]
W = int(sys.argv[2]) if len(sys.argv) > 2 else 2048
H = W // 2

# pixel centres -> directions (Blender equirect: u = 0.5 - atan2(d.y, -d.x) / 2pi, v = elevation / pi + 0.5)
u = (np.arange(W) + 0.5) / W
v = 1.0 - (np.arange(H) + 0.5) / H
phi = (0.5 - u) * 2 * math.pi
th = (v - 0.5) * math.pi
PH, TH = np.meshgrid(phi, th)
dx, dy, dz = -np.cos(TH) * np.cos(PH), np.cos(TH) * np.sin(PH), np.sin(TH)
D = np.stack([dx, dy, dz], -1)


def unit(x, y, z):
    n = math.sqrt(x * x + y * y + z * z)
    return np.array([x / n, y / n, z / n])


def panel(centre, half_w_deg, half_h_deg, radiance, soft=0.25, rgb=(1.0, 1.0, 1.0), roll=0.0):
    """A rectangular light panel facing the origin, seen at `centre` (direction), angular half sizes in degrees,
    with a soft edge (share of the size) and a gentle hot centre, like a diffused softbox."""
    c = centre / np.linalg.norm(centre)
    up = np.array([0.0, 0.0, 1.0]) if abs(c[2]) < 0.95 else np.array([0.0, 1.0, 0.0])
    r = np.cross(up, c)
    r /= np.linalg.norm(r)
    t = np.cross(c, r)
    if roll:
        a = math.radians(roll)
        r, t = r * math.cos(a) + t * math.sin(a), -r * math.sin(a) + t * math.cos(a)
    front = D @ c
    with np.errstate(divide='ignore', invalid='ignore'):
        x = np.degrees(np.arctan2(D @ r, front))
        y = np.degrees(np.arctan2(D @ t, front))
    ex = np.clip((half_w_deg - np.abs(x)) / (half_w_deg * soft), 0, 1)
    ey = np.clip((half_h_deg - np.abs(y)) / (half_h_deg * soft), 0, 1)
    m = (ex * ey) ** 1.5 * (front > 0)
    hot = 1.0 + 0.25 * np.exp(-((x / half_w_deg) ** 2 + (y / half_h_deg) ** 2) * 2.0)
    return m[..., None] * hot[..., None] * radiance * np.array(rgb)


img = np.zeros((H, W, 3), np.float64)
# room: grey walls brightest just above the horizon, darker ceiling, dark floor; the horizon is a soft blend
el = np.degrees(TH)
wall_up = 0.10 + 0.06 * np.exp(-(el - 10) ** 2 / 400.0) - 0.04 * np.sin(np.radians(np.clip(el, 0, 90)))
floor = 0.03 + 0.012 * np.cos(np.radians(el)) ** 8
blend = 1.0 / (1.0 + np.exp(-el / 6.0))          # about 20 degrees wide
wall = floor + (wall_up - floor) * blend
img += wall[..., None] * np.array([1.0, 0.99, 0.97])
# lights (radiance in linear units): broad, soft-edged boxes only (thin strips streak across a glossy panel)
# (the first four were placed before the azimuth was corrected, so their y is mirrored: they stay where the
# approved renders had them)
img += panel(unit(-0.10, -0.35, 1.0), 30, 22, 6.0, soft=0.55)                         # overhead box, a bit forward
img += panel(unit(-0.75, 0.65, 0.65), 18, 24, 12.0, soft=0.50, rgb=(1.0, 0.98, 0.95))  # key, back left
img += panel(unit(0.85, -0.45, 0.45), 12, 22, 3.0, soft=0.60)                          # rim box, front right
img += panel(unit(0.80, 0.55, 0.30), 20, 20, 1.6, soft=0.60, rgb=(0.97, 0.98, 1.0))    # fill card, back right
# a long, low strip behind the unit: only surfaces that lean back toward it (the back bend) catch it, as the
# rolled back edge does in the owner's photo
img += panel(unit(0.0, 1.0, 0.62), 45, 4.0, 22.0, soft=0.45)
img = img.astype(np.float32)
OpenEXR.File({'compression': OpenEXR.ZIP_COMPRESSION, 'type': OpenEXR.scanlineimage},
             {'RGB': np.ascontiguousarray(img)}).write(OUT)
print(f'studio HDR {W}x{H}: max {img.max():.1f}, mean {img.mean():.3f}')
