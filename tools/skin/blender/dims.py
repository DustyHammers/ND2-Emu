# Real-world sizes of the panel and controls, in art px (2x art: 1 dp = 2 px).
#
# Scale: the panel top is 222 mm wide (the published width) and spans the whole canvas (1725 dp = 3450 px);
# the owner's top-down photo, fitted to the art through the 11 grey button centres (0.7 dp rms), confirms
# that the art covers the plate edge to edge. So 1 mm = 3450 / 222 = 15.54 px (7.77 dp).
#
# Plan sizes (W x H, radii) are measured on that photo after warping it into the art frame. Heights are
# estimates from the angled photos and are marked as such; change them here only.

MM = 3450.0 / 222.0          # art px per mm

CANVAS_W, CANVAS_H = 3450, 2070

PLATE = dict(
    thickness=1.2 * MM,      # sheet steel (estimate)
    corner_r=20.0,           # plan corner radius, measured on the art (window colour in the corners)
    edge_fillet=1.0 * MM,    # fold at the outer edge (estimate)
    hole_fillet=0.30 * MM,   # rolled paint edge into the holes; each hole gets 75-100 % of it (hash of its id)
    floor_z=-5.0 * MM,       # dark interior seen through the holes
)

BUTTON = dict(
    hole_w=248.0, hole_h=123.0, hole_r=16.0,   # the art's hole (all buttons but SHIFT, which is drawn 12 x 10 px smaller)
    cap_w=242.0, cap_h=118.0, cap_r=13.0,      # photo: cap fills the hole with about 3 px clearance
    pill_w=165.0, pill_h=70.0,                 # photo: the light top face is a stadium of about 10.6 x 4.5 mm
    top_z=4.0 * MM,          # cap top above the panel (estimate)
    shoulder=1.2 * MM,       # height of the rounded shoulder from the wall to the top face (estimate)
    crown=0.15 * MM,         # the top face's slight dome (estimate)
    draft=2.0,               # wall draft, px per side over the wall height
    bottom_z=-3.0 * MM,
    travel=1.2 * MM,         # pressed frame (estimate)
)

LED = dict(
    hole_r=24.0,             # photo: about 3 mm lens, matches the 22 dp rcss box
    lens_r=22.0,
    dome_z=0.25 * MM,        # dome apex just proud of the panel (estimate)
    body_bottom=-4.0 * MM,
)

DISPLAY = dict(
    x0=994.0, x1=1506.0, y0=509.0, y1=706.0, r=8.0,   # the art's window opening
    lens_z=-1.6 * MM,        # smoked lens just under the plate (estimate)
    board_z=-4.0 * MM,
)

MASTER = dict(
    r=95.0,                  # photo: knob about 12.2 mm across
    height=13.0 * MM,        # estimate
    flutes=30, flute_depth=2.5,
    top_round=8.0,
    pointer_r0=22.0, pointer_r1=86.0, pointer_w=11.0,
    sweep=240.0, frames=128, # frame 0 = 120 degrees left of up, frame 127 = 120 degrees right
)

ENCODER = dict(
    r_base=165.0,            # photo: about 21.2 mm across at the panel
    r_top=140.0,             # top of the faceted side (same ratio as render_encoder2.py: 0.845)
    chamfer=8.0,             # rounded chamfer from the side to the flat top
    height=15.0 * MM,        # estimate
    facets=18, groove=0.16, groove_depth=4.0,
    hole_r=60.0,
    frames=72,
)
