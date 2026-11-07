# Renames panel lettering in the flat art the 3D skin is made from (the newer parameter names), so masks.py and the
# renders pick them up. Old words are painted out with the panel colour around them and the new ones lettered in the
# art's own style (Liberation Sans Bold, its colours and places); a renamed tag gets a box of the new length.
# The 16 parameter names (both rows) are all relettered at one size: the largest at which every name, ATTACK/RATE
# the longest, stays clear of the next column's LED (owner: all the same size, bold, smaller as needed).
#   python3 relabel.py ART_IN.jpg ART_OUT.png
# The art is never committed; it is made at run time from the owner's art (git show ec1ba0b:...).
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT = '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf'
LABEL_PX, LABEL_LS = 39.3, 0.79          # 20 dp lettering at 2x, 0.02 em letter spacing (measured on the art)
TAG_PX = 36.6                            # 19 dp tag lettering at 2x
BOLDEN = 0.6                             # extra stroke per side (px at 2x)
DARKRED_RGB = (98, 9, 12)                # SCL PRE's dark red lettering
LABEL_RGB, TAG_TEXT_RGB = (222, 222, 222), (240, 223, 223)
PANEL_RGB, TAG_RGB = (32, 32, 31), (193, 28, 35)
TAG_PAD_L, TAG_PAD_R, TAG_TOP, TAG_BOTTOM_PAD = 13, 12, 6, 14

# the parameter names, left to right (upper row, lower row); None = unchanged text, relettered at the common size
PARX = [370, 510, 650, 790, 930, 1070, 1210, 1350]          # column x (dp): LED centre; the name starts 18 dp right
UPPER = ['FILTER', 'FILT ENV', 'ATTACK/RATE', 'DECAY', 'DISTORT', 'EQ FREQ', 'ECHO', 'LEVEL']
LOWER = ['SPECTRA', 'TIMB ENV', 'TIMB DEC', 'DECAY', 'PITCH', 'BEND', 'LEVEL', 'TONE \u25b6']
UPPER_TAGS = ['FILT TYPE', 'FILT RES', 'ATK MODE', 'DEC LO', 'TYPE', 'GAIN', 'AMOUNT', 'PAN']
LOWER_TAGS = ['WAVE', 'TIMBRE', 'PUNCH', 'DEC LO', 'SCL PRE', 'TIME', 'TYPE', None]   # TONE's column: no tag
TAG_ROWS = {'up': (846, 900), 'lo': (1020, 1074)}           # the red tag boxes (2x px, y)
ROW_CAP = {'up': (806, 846), 'lo': (980, 1016)}              # where the old names' letters are (2x px, y)
NOISE_LINE = (1026, 1070)                                    # the '<| NOISE' line under TONE (2x px, y)
LED_R = 23                                                   # LED hole radius (2x px)
GAP = 8                                                      # a name stops this far before the next LED hole
# (old ink box x0, y0, x1, y1 in 2x art px, new text, kind, anchor x) -- kind: centre, tag
EDITS = [
    ((955, 1672, 1272, 1699), 'ECHO BPM', 'centre', 1114),
    ((2747, 1452, 2841, 1479), 'TRIG/TAP', 'centre', 2796),
]


def text_image(text, px, ls, rgb, bold=BOLDEN):
    """The word on a transparent image, cropped to its letters from the cap line down. Drawn at 4x and reduced,
    with a stroke of `bold` px per side: the art's lettering is heavier than the plain font (6 px stems at 20 dp
    against the font's 5), and smaller lettering needs it to read as bold."""
    k = 4
    f = ImageFont.truetype(FONT, px * k)
    sym = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', px * k * 0.78)   # the arrows
    font_of = lambda ch: sym if ch in '\u25b6\u25c0' else f
    sw = int(round(bold * k))
    w = int(sum(font_of(ch).getlength(ch) for ch in text) + ls * k * len(text) + 40 * k)
    im = Image.new('L', (w, int(px * k * 1.6)), 0)
    d = ImageDraw.Draw(im)
    x = float(2 * sw)
    cap_top = f.getbbox('H')[1]
    for ch in text:
        g = font_of(ch)
        dy = 0 if g is f else cap_top - g.getbbox(ch)[1] + (f.getbbox('H')[3] - cap_top - (g.getbbox(ch)[3] - g.getbbox(ch)[1])) / 2
        d.text((x, dy + sw), ch, font=g, fill=255, stroke_width=sw, stroke_fill=255)
        x += g.getlength(ch) + ls * k
    box = im.getbbox()
    im = im.crop((box[0], max(0, cap_top + sw - sw), box[2], box[3]))
    im = im.resize((max(1, round(im.width / k)), max(1, round(im.height / k))), Image.LANCZOS)
    rgba = Image.new('RGBA', im.size, (*rgb, 0))
    rgba.putalpha(im)
    return rgba


def natural_width(text, px):
    return text_image(text, px, LABEL_LS * px / LABEL_PX, LABEL_RGB).width


def main():
    art = Image.open(sys.argv[1]).convert('RGB')
    out = sys.argv[2]
    d = ImageDraw.Draw(art)
    px_all = np.asarray(art).astype(np.float32)

    def around(x0, y0, x1, y1, dark=True, m=10):
        """The panel colour just around a box (its median, lettering left out)."""
        ring = np.concatenate([px_all[y0 - m:y0 - 5, x0:x1].reshape(-1, 3), px_all[y1 + 5:y1 + m, x0:x1].reshape(-1, 3)])
        ring = ring[ring.max(-1) < 120]
        return tuple(int(v) for v in np.median(ring, 0)) if len(ring) else PANEL_RGB

    def ink_box(x0, x1, y0, y1):
        sub = px_all[y0:y1, x0:x1]
        ys, xs = np.where(sub.min(-1) > 150)
        return (int(xs.min()) + x0, int(ys.min()) + y0, int(xs.max()) + x0, int(ys.max()) + y0)

    # 1) the parameter names: one size for all, the largest that keeps every name clear of the next LED
    names = []
    for row, words in (('up', UPPER), ('lo', LOWER)):
        for i, word in enumerate(words):
            left = (PARX[i] + 18) * 2
            limit = (PARX[i + 1] * 2 - LED_R - GAP) if i < 7 else 3000
            y0, y1 = ROW_CAP[row]
            search = ((PARX[i + 1] + 18) * 2 - 10) if i < 7 else 3000      # the old name may run past the LED
            names.append((row, i, word, left, limit, ink_box(left - 6, search, y0, y1)))
    size = min([LABEL_PX] + [LABEL_PX * (lim - left) / natural_width(w, LABEL_PX) for _, _, w, left, lim, _ in names])
    # the red tags under them: the same lettering size, boxes scaled with it, centred where the old boxes were
    for row, words in (('up', UPPER_TAGS), ('lo', LOWER_TAGS)):
        for i, word in enumerate(words):
            if word is None:
                continue
            ty0, ty1 = TAG_ROWS[row]
            x_left = (PARX[i] + 16) * 2
            sub = px_all[ty0:ty1, x_left - 4:(PARX[i + 1] + 16) * 2 - 8 if i < 7 else 3000]
            ys, xs = np.where((sub[..., 0] > 150) & (sub[..., 1] < 90))
            bx0, by0, bx1, by1 = x_left - 4 + xs.min(), ty0 + ys.min(), x_left - 4 + xs.max(), ty0 + ys.max()
            d.rectangle((bx0 - 3, by0 - 3, bx1 + 3, by1 + 3), fill=around(bx0, by0, bx1, by1))
            k = size / TAG_PX
            colour = DARKRED_RGB if word == 'SCL PRE' else TAG_TEXT_RGB
            t = text_image(word, size, 0.0, colour)
            h = round((by1 - by0 + 1) * k)
            top = round((by0 + by1) / 2 - h / 2)
            d.rounded_rectangle((bx0, top, bx0 + round(TAG_PAD_L * k) + t.width + round(TAG_PAD_R * k), top + h - 1),
                                radius=max(2, round(4 * k)), fill=TAG_RGB)
            art.paste(t, (bx0 + round(TAG_PAD_L * k), top + round((h - t.height) / 2)), t)
    for row, i, word, left, limit, (x0, y0, x1, y1) in names:
        d.rectangle((x0 - 4, y0 - 4, x1 + 4, y1 + 4), fill=around(x0, y0, x1, y1))
        t = text_image(word, size, LABEL_LS * size / LABEL_PX, LABEL_RGB)
        cap_h = round(size * 0.729 + 2 * BOLDEN)                            # cap height (Q's tail hangs below)
        cap = ROW_CAP[row][0] + 2 + (27 - cap_h) // 2                       # centred on the old cap height
        art.paste(t, (left, cap), t)
    # the second line under TONE: '<| NOISE', at the same size
    x0, y0, x1, y1 = ink_box(2720, 3000, *NOISE_LINE)
    d.rectangle((x0 - 4, y0 - 4, x1 + 4, y1 + 4), fill=around(x0, y0, x1, y1))
    t = text_image('\u25c0 NOISE', size, LABEL_LS * size / LABEL_PX, LABEL_RGB)
    art.paste(t, ((PARX[7] + 18) * 2, NOISE_LINE[0] + 4 + (27 - t.height) // 2), t)
    # 2) the other renamed words (their neighbours' size) and tags
    for (x0, y0, x1, y1), new, kind, ax in EDITS:
        if kind == 'tag':
            d.rectangle((x0 - 3, y0 - 3, x1 + 3, y1 + 3), fill=around(x0, y0, x1, y1))
            t = text_image(new, TAG_PX, 0.0, TAG_TEXT_RGB)
            d.rounded_rectangle((ax, y0, ax + TAG_PAD_L + t.width + TAG_PAD_R, y1), radius=4, fill=TAG_RGB)
            art.paste(t, (ax + TAG_PAD_L, y0 + TAG_TOP), t)
            continue
        d.rectangle((x0 - 4, y0 - 4, x1 + 4, y1 + 4), fill=around(x0, y0, x1, y1))
        t = text_image(new, LABEL_PX, LABEL_LS, LABEL_RGB)
        art.paste(t, (round(ax - t.width / 2), y0), t)
    art.save(out)
    print(f'parameter names at {size / 2:.1f} dp (was {LABEL_PX / 2:.1f}); renamed:',
          ', '.join(['FILT ENV', 'ATTACK/RATE', 'ECHO', 'TIMB ENV', 'TIMB DEC', 'ATK MODE', 'DEC LO'] + [e[1] for e in EDITS]),
          '(the parameter names and their red tags all at that size)')


if __name__ == '__main__':
    main()
