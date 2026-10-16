# Phase 2 post step (runs where the reference photo is; needs bpy, OpenEXR, numpy, Pillow, scipy).
#   python3 post_phase2.py RENDERS_DIR MAPS_DIR TARGETS_JSON OUT_DIR [--share 0.8] [--photo PHOTO_IN_ART2X]
# RENDERS_DIR holds the render artifacts (<light>_<crop>.exr + .json). TARGETS_JSON is photo_targets.py's output
# (numbers only). The photo is optional: without it the sheets show the target swatches instead of photo crops.
# For every light variant and view transform:
#   1. target colours: median sRGB of each ink's flat areas on the photo
#   2. measured light: median diffuse light D and glossy contribution G of each ink in the renders
#   3. the scene-linear colour L that the transform shows as the target, then the albedo a = (L - G) / D
#   4. each crop recombined with those albedos, denoised (OIDN, guided) and blended with the noisy image,
#      then shown through the transform
# and the sheets: calibration patches, lighting variants beside the photo, orange peel and lettering at 1:1.
import glob
import json
import os
import sys

import numpy as np
import OpenEXR
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy

import dims as D
from calib import flat_masks

import argparse

ap = argparse.ArgumentParser()
ap.add_argument('renders')
ap.add_argument('maps')
ap.add_argument('targets')
ap.add_argument('out')
ap.add_argument('--share', type=float, default=0.8)
ap.add_argument('--photo')
args = ap.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:])
RENDERS, MAPS, OUT, SHARE = args.renders, args.maps, args.out, args.share
os.makedirs(OUT, exist_ok=True)
INKS = ('red', 'black', 'tag', 'white')
TRANSFORMS = {'Standard': ('Standard', 'None'), 'AgX': ('AgX', 'None'), 'AgX HC': ('AgX', 'AgX - High Contrast')}
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


def font(size, bold=False):
    try:
        return ImageFont.truetype(FONT.replace('Sans', 'Sans-Bold') if bold else FONT, size)
    except OSError:
        return ImageFont.load_default()


# ---- inputs -------------------------------------------------------------------------------------------------

def read_exr(path):
    f = OpenEXR.File(path)
    ch, comp = {}, {}
    for part in f.parts:
        for k, c in part.channels.items():
            k = k.replace('ViewLayer.', '')
            base, _, suf = k.rpartition('.')
            if base and suf in ('X', 'Y', 'Z', 'R', 'G', 'B', 'A', 'V'):
                comp.setdefault(base, {})[suf] = np.asarray(c.pixels, np.float32)
            else:
                ch[k] = np.asarray(c.pixels, np.float32)
    for base, parts in comp.items():                       # single-channel parts: Material Index.X, Normal.X/Y/Z
        order = [s for s in ('X', 'Y', 'Z', 'R', 'G', 'B', 'A', 'V') if s in parts]
        ch[base] = parts[order[0]] if len(order) == 1 else np.stack([parts[s] for s in order], -1)
    return ch


def write_exr(path, rgba):
    hdr = {'compression': OpenEXR.ZIP_COMPRESSION, 'type': OpenEXR.scanlineimage}
    OpenEXR.File(hdr, {'RGBA': np.ascontiguousarray(rgba.astype(np.float32))}).write(path)


inks = np.asarray(Image.open(os.path.join(MAPS, 'inks.png'))).astype(np.float32) / 255
photo = np.asarray(Image.open(args.photo).convert('RGB')).astype(np.float32) if args.photo else None
H, W = inks.shape[:2]
flat = flat_masks(inks)
T = json.load(open(args.targets))
target = {ink: np.array(T['srgb_median'][ink], np.float32) for ink in INKS if ink in T['srgb_median']}


# ---- colour transforms through Blender's colour management --------------------------------------------------

bpy.ops.wm.read_factory_settings(use_empty=True)
SC = bpy.context.scene


def apply_transform(lin, name):
    """lin: (N, 3) scene-linear -> (N, 3) display sRGB 0..255 (float), through the named view transform."""
    vt, look = TRANSFORMS[name]
    SC.view_settings.view_transform, SC.view_settings.look = vt, look
    SC.view_settings.exposure, SC.view_settings.gamma = 0.0, 1.0
    n = len(lin)
    w = min(n, 4096)
    h = (n + w - 1) // w
    buf = np.zeros((h * w, 4), np.float32)
    buf[:n, :3], buf[:, 3] = lin, 1
    img = bpy.data.images.new('tf', w, h, float_buffer=True, alpha=True)
    img.pixels.foreach_set(buf.ravel())
    st = SC.render.image_settings
    st.file_format, st.color_mode, st.color_depth = 'PNG', 'RGBA', '16'
    path = os.path.join(OUT, '_tf.png')
    img.save_render(path, scene=SC)
    bpy.data.images.remove(img)
    r = bpy.data.images.load(path)
    r.colorspace_settings.name = 'Non-Color'
    a = np.empty(w * h * 4, np.float32)
    r.pixels.foreach_get(a)
    bpy.data.images.remove(r)
    os.remove(path)
    a = a.reshape(-1, 4)          # written and read back bottom-up alike, so the order matches buf
    return a[:n, :3] * 255


def inverse_transform(target_srgb, name):
    """Scene-linear colour shown as target_srgb by the transform (search on a log grid, then refine)."""
    g = np.concatenate([[0.0], np.logspace(-4.5, 0.8, 47)]).astype(np.float32)
    cand = np.stack(np.meshgrid(g, g, g, indexing='ij'), -1).reshape(-1, 3)
    shown = apply_transform(cand, name)
    best = cand[np.argmin(((shown - target_srgb) ** 2).sum(1))]
    for _ in range(2):
        steps = np.linspace(0.7, 1.3, 21)
        cand = np.stack(np.meshgrid(best[0] * steps + 1e-6, best[1] * steps + 1e-6, best[2] * steps + 1e-6, indexing='ij'), -1).reshape(-1, 3)
        shown = apply_transform(cand, name)
        i = np.argmin(((shown - target_srgb) ** 2).sum(1))
        best, err = cand[i], np.sqrt(((shown[i] - target_srgb) ** 2).sum())
    return best, float(err)


def to_display(rgb_lin, name):
    h, w = rgb_lin.shape[:2]
    return np.clip(apply_transform(rgb_lin.reshape(-1, 3), name).reshape(h, w, 3), 0, 255).astype(np.uint8)


# ---- denoising with OIDN through the compositor -------------------------------------------------------------

def denoise(rgba, albedo, normal):
    h, w = rgba.shape[:2]
    paths = []
    for nm, a in (('img', rgba), ('alb', np.dstack([albedo, np.ones((h, w))])), ('nor', np.dstack([normal, np.ones((h, w))]))):
        p = os.path.join(OUT, f'_dn_{nm}.exr')
        write_exr(p, a)
        paths.append(p)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    sc.collection.objects.link(cam)
    sc.camera = cam
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = 1
    sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = w, h, 100
    sc.render.film_transparent = True
    if hasattr(sc, 'compositing_node_group'):            # Blender 5.x
        nt = bpy.data.node_groups.new('post', 'CompositorNodeTree')
        nt.interface.new_socket('Image', in_out='OUTPUT', socket_type='NodeSocketColor')
        out = nt.nodes.new('NodeGroupOutput')
        sc.compositing_node_group = nt
        out_in = out.inputs[0]
    else:                                                # Blender 4.2
        sc.use_nodes = True
        nt = sc.node_tree
        nt.nodes.clear()
        out_in = nt.nodes.new('CompositorNodeComposite').inputs['Image']
    ims = []
    for p in paths:
        n = nt.nodes.new('CompositorNodeImage')
        n.image = bpy.data.images.load(p)
        if '_dn_img' not in p:
            n.image.colorspace_settings.name = 'Non-Color'
        ims.append(n)
    dn = nt.nodes.new('CompositorNodeDenoise')
    nt.links.new(ims[0].outputs['Image'], dn.inputs['Image'])
    nt.links.new(ims[1].outputs['Image'], dn.inputs['Albedo'])
    nt.links.new(ims[2].outputs['Image'], dn.inputs['Normal'])
    for nm in ('HDR',):
        if nm in dn.inputs:
            dn.inputs[nm].default_value = True
    nt.links.new(dn.outputs[0], out_in)
    st = sc.render.image_settings
    st.file_format, st.color_depth = 'OPEN_EXR', '32'
    res = os.path.join(OUT, '_dn_out.exr')
    sc.render.filepath = res
    bpy.ops.render.render(write_still=True)
    out = read_exr(res)
    key = [k for k in out if k.endswith('RGBA') or k in ('RGBA', 'Combined', 'Image')]
    arr = out[key[0]] if key else np.dstack([out[c] for c in ('R', 'G', 'B', 'A')])
    for p in paths + [res]:
        os.remove(p)
    global SC
    bpy.ops.wm.read_factory_settings(use_empty=True)
    SC = bpy.context.scene
    return arr


def align(ch, box):
    """Blender's render border can round to one row or column more than the box: pick the offset whose
    diffuse colour best matches the ink masks and trim every pass to the box."""
    x0, y0, x1, y1 = box
    bh, bw = y1 - y0, x1 - x0
    h, w = ch['Combined'].shape[:2]
    if (h, w) == (bh, bw):
        return ch
    def edges(a):                                # edges separate one-pixel offsets far better than values
        return np.abs(np.diff(a, axis=0))[:, :-1] + np.abs(np.diff(a, axis=1))[:-1]
    ref = edges(inks[y0:y1, x0:x1, 0])
    dc = ch['Diffuse Color'][..., 0]
    best, score = (0, 0), -1e9
    for dy in range(max(h - bh, 0) + 1):
        for dx in range(max(w - bw, 0) + 1):
            a = dc[dy:dy + bh, dx:dx + bw]
            if a.shape != (bh, bw):
                continue
            c = float(np.corrcoef(edges(a).ravel(), ref.ravel())[0, 1])
            if c > score:
                best, score = (dy, dx), c
    dy, dx = best
    print(f'align: render {w}x{h} for a {bw}x{bh} box, offset {dx},{dy} (match {score:.4f})')
    return {k: v[dy:dy + bh, dx:dx + bw] for k, v in ch.items()}


# ---- per light variant: calibrate and recombine -------------------------------------------------------------

exrs = sorted(glob.glob(os.path.join(RENDERS, '**', '*.exr'), recursive=True))
data = {}
for p in exrs:
    light, crop = os.path.basename(p)[:-4].split('_', 1)
    info = json.load(open(p[:-4] + '.json'))
    data.setdefault(light, {})[crop] = (p, info)

report = {'targets_srgb': {k: v.round(1).tolist() for k, v in target.items()},
          'denoised_share': SHARE, 'lights': {}}
results = {}             # (light, crop, transform) -> uint8 image
for light in sorted(data):
    stats = {ink: {'D': [], 'G': []} for ink in INKS}
    loaded = {}
    for crop, (p, info) in data[light].items():
        ch = align(read_exr(p), info['box'])
        x0, y0, x1, y1 = info['box']
        loaded[crop] = (ch, info)
        paint_px = np.abs(ch['Material Index'] - 1) < 0.5
        Dl = ch['Diffuse Direct'] + ch['Diffuse Indirect']
        Gl = ch['Glossy Color'] * (ch['Glossy Direct'] + ch['Glossy Indirect'])
        for ink in INKS:
            m = flat[ink][y0:y1, x0:x1] & paint_px
            if crop == 'flat':                       # the strips test 0.5x and 2x peel: use the 1x strip only
                xs = np.arange(x0, x1)
                m &= ((xs > 520) & (xs < 980))[None, :]
            if m.sum() > 50:
                stats[ink]['D'].append(Dl[m])
                stats[ink]['G'].append(Gl[m])
    rep = {}
    albedo = {}
    expo = {}
    WHITE_ALBEDO = 0.85                # a realistic white print; sets the exposure for each light and transform
    med = {ink: (np.median(np.concatenate(stats[ink]['D']), 0), np.median(np.concatenate(stats[ink]['G']), 0))
           for ink in INKS if stats[ink]['D']}
    for tname in TRANSFORMS:
        albedo[tname] = {}
        rep[tname] = {}
        need = {ink: inverse_transform(target[ink], tname) for ink in INKS if ink in target and ink in med}
        k = 1.0
        if 'white' in need:            # light x k: a = (L / k - G) / D; white's mean albedo = WHITE_ALBEDO
            Dw, Gw = med['white']
            k = float(need['white'][0].mean() / (WHITE_ALBEDO * Dw.mean() + Gw.mean()))
        expo[tname] = k
        rep[tname]['exposure_ev'] = round(float(np.log2(k)), 3)
        for ink, (L, err) in need.items():
            Dm, Gm = med[ink]
            a = (L / k - Gm) / np.maximum(Dm, 1e-6)
            clipped = bool((a < 0).any() or (a > 0.95).any())
            albedo[tname][ink] = np.clip(a, 0, 0.95)
            shown = apply_transform((albedo[tname][ink][None] * Dm + Gm) * k, tname)[0]
            rep[tname][ink] = dict(target=target[ink].round(1).tolist(), linear_needed=L.round(5).tolist(),
                                   diffuse_light=Dm.round(4).tolist(), gloss=Gm.round(5).tolist(),
                                   albedo=np.clip(a, 0, 0.95).round(4).tolist(), albedo_raw=a.round(4).tolist(),
                                   reachable=not clipped, shown=shown.round(1).tolist(), search_err=round(err, 2))
    report['lights'][light] = rep

    for crop, (ch, info) in loaded.items():
        x0, y0, x1, y1 = info['box']
        paint_px = (np.abs(ch['Material Index'] - 1) < 0.5)[..., None]
        Dl = ch['Diffuse Direct'] + ch['Diffuse Indirect']
        rest = ch['Glossy Color'] * (ch['Glossy Direct'] + ch['Glossy Indirect']) + \
            ch['Transmission Color'] * (ch['Transmission Direct'] + ch['Transmission Indirect']) + ch['Emission']
        comb = ch['Combined']
        check = np.abs(ch['Diffuse Color'] * Dl + rest - comb[..., :3]).mean()
        m = inks[y0:y1, x0:x1]
        for tname in TRANSFORMS:
            alb = sum(m[..., i:i + 1] * albedo[tname].get(ink, np.array(info['ink_albedo'][ink])) for i, ink in enumerate(INKS))
            dcol = np.where(paint_px, alb, ch['Diffuse Color'])
            rgb = dcol * Dl + rest
            rgba = np.dstack([rgb, comb[..., 3]])
            guide = ch['Denoising Albedo'] - ch['Diffuse Color'] + dcol
            dn = denoise(rgba, np.clip(guide, 0, 1), ch['Denoising Normal'])
            mix = (SHARE * dn[..., :3] + (1 - SHARE) * rgb) * expo[tname]
            a = comb[..., 3:4]
            win = np.array([0x2b, 0x2b, 0x2e], np.float32)
            disp = to_display(mix / np.maximum(a, 1e-4), tname).astype(np.float32)
            results[(light, crop, tname)] = (disp * a + win * (1 - a)).astype(np.uint8)
        report['lights'][light].setdefault('recombine_check_mean_abs', {})[crop] = round(float(check), 6)
        report['lights'][light].setdefault('render_s', {})[crop] = info['render_s']
        report['lights'][light].setdefault('s_per_mp_per_sample', {})[crop] = info['s_per_mp_per_sample']

json.dump(report, open(os.path.join(OUT, 'phase2_calibration.json'), 'w'), indent=1)
for (light, crop, tname), img in results.items():
    Image.fromarray(img).save(os.path.join(OUT, f'crop_{light}_{crop}_{tname.replace(" ", "")}.png'))


# ---- sheets -------------------------------------------------------------------------------------------------

def photo_crop(box):
    """The photo's crop, or (without a photo) a panel of the target swatches at the crop's size."""
    x0, y0, x1, y1 = box
    if photo is not None:
        return Image.fromarray(photo[y0:y1, x0:x1].astype(np.uint8))
    im = Image.new('RGB', (x1 - x0, y1 - y0), (24, 24, 26))
    d = ImageDraw.Draw(im)
    n = len(target)
    for i, (ink, c) in enumerate(target.items()):
        w = (x1 - x0) // n
        d.rectangle([i * w, 0, (i + 1) * w - 4, y1 - y0], fill=tuple(int(v) for v in c))
        d.text((i * w + 6, 6), ink, fill=(255, 255, 255) if ink != 'white' else (0, 0, 0), font=font(16))
    return im


PHOTO_LABEL = 'photo' if photo is not None else 'photo targets'


def label(img, text, size=22):
    c = Image.new('RGB', (img.width, img.height + size + 14), (24, 24, 26))
    c.paste(img, (0, size + 14))
    ImageDraw.Draw(c).text((6, 5), text, fill=(235, 235, 235), font=font(size, True))
    return c


def grid(rows, gap=14, bg=(24, 24, 26)):
    rows = [r for r in rows if r] or [[Image.new('RGB', (400, 60), bg)]]
    W_ = max(sum(i.width for i in r) + gap * (len(r) - 1) for r in rows)
    H_ = sum(max(i.height for i in r) for r in rows) + gap * (len(rows) - 1)
    s = Image.new('RGB', (W_, H_), bg)
    y = 0
    for r in rows:
        x = 0
        for i in r:
            s.paste(i, (x, y))
            x += i.width + gap
        y += max(i.height for i in r) + gap
    return s


def header(sheet, title, sub):
    c = Image.new('RGB', (sheet.width, sheet.height + 90), (24, 24, 26))
    c.paste(sheet, (0, 90))
    d = ImageDraw.Draw(c)
    d.text((10, 10), title, fill=(255, 255, 255), font=font(30, True))
    d.text((10, 52), sub, fill=(200, 200, 200), font=font(20))
    return c


boxes = {crop: info['box'] for light in data for crop, (p, info) in data[light].items()}
lights = sorted(data)

# lighting sheets, one per transform: rows = crops, columns = lights + photo (half size)
for tname in TRANSFORMS:
    rows = []
    for crop in ('master', 'params', 'display', 'encoder', 'top', 'wordmark'):
        if crop not in boxes:
            continue
        r = []
        for light in lights:
            if (light, crop, tname) in results:
                im = Image.fromarray(results[(light, crop, tname)])
                r.append(label(im.resize((im.width // 2, im.height // 2), Image.LANCZOS), f'{crop}  light {light}'))
        pc = photo_crop(boxes[crop])
        r.append(label(pc.resize((pc.width // 2, pc.height // 2), Image.LANCZOS), f'{crop}  {PHOTO_LABEL}'))
        rows.append(r)
    sh = header(grid(rows), f'Phase 2: lighting variants A / B / C, {tname} view transform',
                'Half size. Paint calibrated per light and transform to the photo (flat areas). Controls are sprites, '
                'so they are hidden here; their shadows stay.')
    sh.save(os.path.join(OUT, f'phase2_lighting_{tname.replace(" ", "")}.png'))

# calibration sheet: per light and transform, the photo's flat-area colour, the render's flat red and black
patch_rows = []
for light in lights:
    r = []
    for tname in TRANSFORMS:
        img = results.get((light, 'flat', tname))
        if img is None:
            continue
        x0, y0, _, _ = boxes['flat']
        pr = Image.fromarray(img[60:260, 540 - x0:960 - x0]) if img.shape[1] > 960 - x0 else Image.fromarray(img[:200, :420])
        cell = Image.new('RGB', (420, 330), (24, 24, 26))
        cell.paste(pr, (0, 0))
        d = ImageDraw.Draw(cell)
        rep = report['lights'][light][tname]
        y = 206
        for ink in ('red', 'black', 'tag', 'white'):
            if ink not in rep:
                continue
            e = rep[ink]
            d.rectangle([0, y, 40, y + 24], fill=tuple(int(v) for v in e['target']))
            d.rectangle([42, y, 82, y + 24], fill=tuple(int(np.clip(v, 0, 255)) for v in e['shown']))
            a = e['albedo']
            ok = '' if e['reachable'] else '  (out of reach)'
            d.text((90, y + 2), f'{ink}: albedo {a[0]:.3f} {a[1]:.3f} {a[2]:.3f}{ok}', fill=(225, 225, 225), font=font(15))
            y += 30
        r.append(label(cell, f'light {light}, {tname}', 18))
    patch_rows.append(r)
sh = header(grid(patch_rows), 'Phase 2: colour calibration (flat red body, 1x orange peel)',
            'Swatches: left = photo (median of flat areas), right = render with the solved albedo. '
            '"Out of reach": the target needs less gloss or more light.')
sh.save(os.path.join(OUT, 'phase2_calibration.png'))

# orange peel and lettering at 1:1 (light A unless missing, every transform's first)
la = 'A' if 'A' in lights else lights[0]
tf0 = 'AgX'
rows = []
if (la, 'flat', tf0) in results:
    img = Image.fromarray(results[(la, 'flat', tf0)])
    x0 = boxes['flat'][0]
    strips = [img.crop((60 - x0 + 200, 40, 60 - x0 + 600, 300)), img.crop((560 - x0, 40, 960 - x0, 300)),
              img.crop((1040 - x0, 40, 1440 - x0, 300))]
    rows.append([label(s, t, 18) for s, t in zip(strips, ('peel 0.5x', 'peel 1x', 'peel 2x'))] +
                [label(photo_crop((560, 1770, 960, 2030)), PHOTO_LABEL, 18)])
for light in lights:
    if (light, 'params', tf0) in results:
        x0, y0 = boxes['params'][:2]
        img = Image.fromarray(results[(light, 'params', tf0)]).crop((700 - x0, 790 - y0, 1300 - x0, 1080 - y0))
        rows.append([label(img, f'lettering 1:1, light {light}', 18)])
if photo is not None:
    rows.append([label(photo_crop((700, 790, 1300, 1080)), 'lettering 1:1, photo', 18)])
sh = header(grid(rows), f'Phase 2: orange peel strips and lettering at 1:1 ({tf0})',
            'Peel strength in strips on the flat red body (0.5x, 1x, 2x of 1.4 degrees slope); printed relief on the lettering.')
sh.save(os.path.join(OUT, 'phase2_peel_lettering.png'))
print('done:', len(results), 'crops')
