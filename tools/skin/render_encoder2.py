# Endless value encoder, top-down, modelled on the hardware photos:
#  - flat, matte, sandblasted top disc (the grain turns with the knob)
#  - a small rounded chamfer at the top edge
#  - a slightly tapered faceted side (F flat facets, narrow grooves), seen from above as a thin dark ring
#    that picks up a little red from the panel around it
# Fixed light from the top left; only the knob turns. N frames = one full turn.
import os
import sys
import numpy as np
from PIL import Image

N = int(sys.argv[1]) if len(sys.argv) > 1 else 72
S = int(sys.argv[2]) if len(sys.argv) > 2 else 448
OUT = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'skins', 'default', 'nd2-default', 'ND2Encoder.png')
SS = 3
F = 18                 # side facets
RB = 0.86              # base radius (fraction of the half-frame); the rest is shadow room
RT = 0.845             # top disc radius, relative to the base
CH = 0.035             # chamfer width (relative)
GROOVE = 0.16          # groove width as a fraction of one facet
n = S * SS
yy, xx = (np.mgrid[0:n, 0:n] + 0.5) / n * 2 - 1
x, y = xx / RB, yy / RB
r = np.hypot(x, y)
th0 = np.arctan2(y, x)

light = np.array([-0.50, -0.65, 0.80]); light /= np.linalg.norm(light)
view = np.array([0.0, 0.0, 1.0])
hv = light + view; hv /= np.linalg.norm(hv)

# grain: smooth-ish noise in knob coordinates (sampled rotated per frame so it turns with the knob)
rng = np.random.default_rng(3)
G = 1024
g = rng.standard_normal((G, G)).astype(np.float32)
for _ in range(2):  # light blur: grain of about 1-2 px at 2x
    g = (g + np.roll(g, 1, 0) + np.roll(g, -1, 0) + np.roll(g, 1, 1) + np.roll(g, -1, 1)) / 5
g /= g.std()

def sample_grain(u, v):
    i = ((u * 0.5 + 0.5) * (G - 1)).clip(0, G - 1).astype(np.int32)
    j = ((v * 0.5 + 0.5) * (G - 1)).clip(0, G - 1).astype(np.int32)
    return g[j, i]

def frame(phi):
    th = th0 - phi                                   # knob-relative angle
    seg = 2 * np.pi / F
    k = np.floor(th / seg + 0.5)                     # facet index
    d = th - k * seg                                 # angle from the facet centre (-seg/2..seg/2)
    face_r = np.cos(seg / 2) / np.cos(d)             # flat facet outline (polygon), 1 at the corners
    outline = face_r
    inside = r <= outline
    nrm = np.zeros(r.shape + (3,))
    # top disc
    top = r <= RT - CH
    # knob-frame coordinates for the grain
    c, s_ = np.cos(-phi), np.sin(-phi)
    u = x * c - y * s_; v = x * s_ + y * c
    gr = sample_grain(u, v)
    gx = 0.10 * gr; gy = 0.10 * sample_grain(v, u)
    nt = np.dstack([gx, gy, np.ones_like(r)])
    # chamfer: normal tilts outward from 0 to ~55 deg
    t = np.clip((r - (RT - CH)) / CH, 0, 1)
    tilt = np.radians(55) * t
    cham = (r > RT - CH) & (r <= RT)
    nc = np.dstack([np.cos(th0) * np.sin(tilt), np.sin(th0) * np.sin(tilt), np.cos(tilt)])
    # facets: flat face normal points along the facet centre, tipped up by the taper
    ang = k * seg + phi                              # facet centre angle in the image frame
    elev = np.radians(32)                            # how much of each facet faces up (taper + perspective)
    gw = GROOVE * seg / 2
    edge = np.clip((np.abs(d) - (seg / 2 - gw)) / gw, 0, 1)    # 0 on the face, 1 at the groove bottom
    # inside a groove the normal swings towards the neighbouring facet direction
    swing = np.sign(d) * edge * seg * 2.2
    nf = np.dstack([np.cos(ang + swing) * np.cos(elev), np.sin(ang + swing) * np.cos(elev), np.full_like(r, np.sin(elev))])
    side = (r > RT) & inside
    nrm = np.where(top[..., None], nt, nrm)
    nrm = np.where(cham[..., None], nc, nrm)
    nrm = np.where(side[..., None], nf, nrm)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=2, keepdims=True), 1e-9)

    nd = np.clip(nrm @ light, 0, 1)
    ndh = nrm @ hv
    spec_matte = np.clip(ndh, 0, 1) ** 18            # broad, low sheen (sandblasted top)
    spec_side = np.clip(ndh, 0, 1) ** 60             # the facets are a bit glossier
    up_left = np.clip(-(nrm[..., 0] * 0.6 + nrm[..., 1] * 0.8), 0, 1)
    out_ward = np.clip(1 - nrm[..., 2], 0, 1)

    col = np.zeros(r.shape + (3,))
    top_base = np.array([0.020, 0.020, 0.021])
    side_base = np.array([0.008, 0.008, 0.009])
    tp = top | cham                                  # 2-D mask
    base = np.where(tp[..., None], top_base, side_base)
    col = base * (0.30 + 1.05 * nd[..., None])
    spec = np.where(tp, 0.060 * spec_matte, 0.090 * spec_side)
    col += spec[..., None] * np.array([0.9, 0.92, 1.0])
    col += (np.array([0.012, 0.012, 0.015]) * up_left[..., None] ** 2) * side[..., None]       # sky on the upper-left facets
    col *= (1 - 0.55 * (edge > 0.55) * side)[..., None] * 1.0                                   # groove bottoms
    # red pickup from the panel ring, on the lower/outer parts of the side
    col += (np.array([0.040, 0.004, 0.005]) * (out_ward * (1 - up_left))[..., None] ** 1.5) * side[..., None]
    # top-disc grain speckle (albedo)
    col *= np.where(top[..., None], 1 + 0.10 * gr[..., None], 1.0)
    # occlusion where the side meets the panel
    foot = np.clip((r - 0.93) / 0.07, 0, 1)
    col *= (1 - 0.45 * foot * side)[..., None]
    alpha = inside.astype(float)
    return col, alpha

rs = np.hypot(x - 0.05, y - 0.09)
shadow = np.clip(1.10 - rs, 0, 1) ** 1.5 * 0.78
frames = []
for i in range(N):
    col, al = frame(2 * np.pi * i / N)
    a = np.maximum(al, shadow * (1 - al))
    img = np.dstack([col * al[..., None], a]).reshape(S, SS, S, SS, 4).mean(axis=(1, 3))
    a2 = img[..., 3:4]
    rgb = np.where(a2 > 0, img[..., :3] / np.maximum(a2, 1e-6), 0)
    frames.append(np.dstack([np.clip(rgb, 0, 1) ** (1 / 2.2), a2[..., 0]]))
cols = 12 if N % 12 == 0 else min(N, 8)
rows = (N + cols - 1) // cols
sheet = np.zeros((rows * S, cols * S, 4))
for i, f in enumerate(frames):
    rr, cc = divmod(i, cols); sheet[rr * S:(rr + 1) * S, cc * S:(cc + 1) * S] = f
Image.fromarray((sheet * 255).round().astype(np.uint8)).save(OUT, optimize=True)
print('frames', N, 'size', S, 'grid', rows, 'x', cols, '->', OUT)
