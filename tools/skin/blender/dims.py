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
    hole_w=248.0, hole_h=123.0, hole_r=6.0,    # the art's hole size; nearly square corners (straight-on photo)
    cap_w=242.0, cap_h=118.0, cap_r=5.0,       # cap fills the hole with about 3 px clearance; nearly square corners
    pill_w=176.0, pill_h=78.0,                 # straight-on photo: the raised pill is about 11.5 x 5.4 mm
    top_z=3.0 * MM,          # cap top above the panel (owner: lower than 5 mm)
    shoulder=1.3 * MM,       # from the wall's top edge to the top of the raised pill (estimate)
    dish=0.22 * MM,          # the pill's top is a shallow finger groove: concave across, running along it (owner)
    draft=2.0,               # wall draft, px per side over the wall height
    bottom_z=-3.0 * MM,
    travel=1.2 * MM,         # pressed frame (estimate)
)

LED = dict(
    hole_r=23.0,             # photo: about 3 mm lens (22 px radius) in a hole just larger
    lens_r=22.0,
    dome_z=0.05 * MM,        # dome apex about level with the panel: it sits down in its hole (straight-on photo)
    body_bottom=-4.0 * MM,
)

DISPLAY = dict(
    x0=994.0, x1=1506.0, y0=509.0, y1=706.0, r=8.0,   # the art's window opening
    lens_z=-1.6 * MM,        # smoked lens just under the plate (estimate)
    board_z=-4.0 * MM,
    # four digit packages behind the red filter, on the skin's digit pitch (the plugin draws the segments)
    digit_x0=992.0, digit_pitch=116.0, divider_w=8.0,
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
    r_base=168.0,            # straight-on photo: about 21.6 mm across at the panel (a black gap to the ring)
    r_top=141.0,             # straight-on photo: the flat top (inside the chamfer) about 17 mm across
    chamfer=7.0,             # flat 45-degree bevel from the side to the flat top (photo)
    height=18.0 * MM,        # close-up photo: nearly as tall as it is wide
    # the side: 10 rounded segments between narrow flat-bottomed notches, each notch about 10 degrees wide
    # (owner's straight-on photo: notches about 7 degrees wide)
    facets=10, groove=0.22, groove_depth=9.0, phase_deg=3.0,
    # a thin round cap sits on the flat top, just inside its edge (owner: the two lines at the top)
    cap_inset=4.0, cap_h=0.35 * MM, cap_edge=2.6,
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
    # the top bends straight down along the front into the lip (the lip's ends are cut off at the sides); the
    # side and back edges of the sheet are rounded over, and the back corners are rounded in plan
    front_bend_r=3.0 * MM,   # outer radius of the bend into the front lip (owner: less than 5 mm)
    lip_h=10.0 * MM,         # from the top surface down to the lip's bottom edge
    edge_roll_r=1.0 * MM,    # the sheet's side and back edges, rounded over (under the 1.2 mm thickness)
    corner_back_r=4.0 * MM,  # plan radius of the back corners
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
