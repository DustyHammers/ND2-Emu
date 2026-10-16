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
    edge_fillet=1.25 * MM,   # the bend where the top folds down into the sides; must stay under corner_r (20 px)
    # formed sheet metal: every punched hole has a rounded, painted edge; each hole gets 85-100 % of its radius
    # (hash of its id). Buttons and LEDs are kept just clear of the lettering around them.
    hole_fillet=0.50 * MM,   # button, knob, encoder and display holes
    led_fillet=0.45 * MM,    # LED holes
    floor_z=-5.0 * MM,       # dark interior seen through the holes
)

BUTTON = dict(
    hole_w=248.0, hole_h=123.0, hole_r=16.0,   # the art's hole (all buttons but SHIFT, which is drawn 12 x 10 px smaller)
    cap_w=242.0, cap_h=118.0, cap_r=13.0,      # photo: cap fills the hole with about 3 px clearance
    pill_w=165.0, pill_h=70.0,                 # photo: the light top face is a stadium of about 10.6 x 4.5 mm
    top_z=3.0 * MM,          # cap top above the panel (owner: lower than 5 mm)
    shoulder=1.3 * MM,       # from the wall's top edge to the top of the raised pill (estimate)
    crown=0.15 * MM,         # the top face's slight dome (estimate)
    draft=2.0,               # wall draft, px per side over the wall height
    bottom_z=-3.0 * MM,
    travel=1.2 * MM,         # pressed frame (estimate)
)

LED = dict(
    hole_r=23.0,             # photo: about 3 mm lens (22 px radius) in a hole just larger
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
    # photos: a tall black knob, slightly narrower at the top, with about 12 rounded lobes, a rounded top edge
    # onto a flat top, and a white pointer line that runs across the top and down the side to the panel
    r_base=104.0,            # about 13.4 mm across at the panel
    r_top=95.0,              # about 12.2 mm across at the top (the top-down photo)
    height=15.0 * MM,        # about as tall as it is wide (estimate from the angled photos)
    flutes=12, flute_depth=0.6 * MM,       # rounded convex lobes with narrow notches between them (photos)
    top_round=1.3 * MM,
    pointer_w=0.9 * MM, pointer_r0=0.22,   # stripe width; it starts at this share of the radius on the top
    sweep=240.0, frames=128, # frame 0 = 120 degrees left of up, frame 127 = 120 degrees right
)

ENCODER = dict(
    r_base=165.0,            # photo: about 21.2 mm across at the panel
    r_top=140.0,             # top of the faceted side (same ratio as render_encoder2.py: 0.845)
    chamfer=8.0,             # rounded chamfer from the side to the flat top
    height=18.0 * MM,        # close-up photo: nearly as tall as it is wide
    facets=18, groove=0.16, groove_depth=4.0,
    hole_r=60.0,
    frames=72,
)

# The whole unit: a formed sheet-metal shell (top, bends, front, back and wedge-shaped sides) on a black base
# plate with rubber feet. The skin is a straight-down view of the top, so everything is built in panel
# coordinates (top surface at z = 0) and the case's walls run along the world vertical, which is tilted
# relative to the panel because the top slopes down toward the front.
# Published size 222 x 43 x 140 mm (width x height x depth); back and front wall heights are estimates.
CASE = dict(
    # the red top is one formed sheet: the top and a short, well-rounded lip over the front edge, with two
    # screws on the lip's face. The sides, the back and the front below the lip are a separate black chassis.
    # it is a formed tray: the sheet rolls down all round, with a large radius over the front into the lip and a
    # smaller one over the sides and back into short flanges; the corners are rounded in plan
    front_bend_r=3.0 * MM,   # outer radius of the roll over the front edge (owner: less than 5 mm)
    side_bend_r=2.5 * MM,    # outer radius of the roll over the sides and the back
    flange_h=4.0 * MM,       # how far the sides and back reach down
    lip_h=10.0 * MM,         # from the top surface down to the lip's bottom edge
    corner_front_r=3.1 * MM, corner_back_r=4.0 * MM,   # plan radius of the corners (front: owner, tighter)
    lip_screw_x=(0.12, 0.88),
    back_h=40.0 * MM,        # from the back edge of the top down to the base (estimate; 43 mm with feet)
    front_h=12.0 * MM,       # from the front edge of the top down to the base: the red lip nearly covers it (photo)
    base_t=1.0 * MM,         # black base plate
    foot_r=6.0 * MM, foot_h=2.5 * MM, foot_inset=14.0 * MM,
    screw_r=2.6 * MM,        # black pan-head screws: two on the front, one on each side
    jack_h=30.0 * MM,        # back-panel connectors: centre height above the base (barrels just show over the back edge)
)
# back-panel connectors, x in art px (2x). CH1-6 sit under the art's CH labels; the others keep the original
# hardware's order and spacing, scaled to fit beside them (from the owner's top-down photo).
JACKS = [('jack', 120), ('jack', 220), ('jack', 409), ('din', 556), ('din', 797),
         ('jack', 1086), ('jack', 1340), ('jack', 1596), ('jack', 1850), ('jack', 2106), ('jack', 2360),
         ('jack', 2700), ('dc', 3050)]
