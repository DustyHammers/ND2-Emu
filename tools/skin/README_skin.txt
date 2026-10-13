ND2 default skin
================

What's here
-----------
nd2-default/                The skin folder. It is also built into the plugin as the Default skin. A copy you
                            change goes in ~/Documents/ND2/skins/<new name>/ (rename the folder and its .rml and
                            .rcss files to the new name); it then appears in the right-click menu under Skin.
preview.jpg                 Everything together (2x).
encoder_turning.gif         The value encoder turning one full loop.
_source/                    The scripts that made all of this (source page for the control positions, display
                            segments and BANKS sprites, app icon page, encoder renderer, button/knob grain,
                            packager), so any part can be regenerated.

Canvas
------
1725 x 1035 dp = the body. All art is drawn at 2x: 3450 x 2070 px.

Background (since 0.1.8)
------------------------
ND2Background.jpg        everything but the controls, baked into one picture: body, panels, display window,
                         button cutouts, MASTER LEVEL scale, encoder ring, tags, lines, lettering, wordmark.
                         3450 x 2070 px; JPEG (quality 95, no chroma subsampling) because the body's grain makes
                         a lossless file about 11 MB. Outside the rounded corners it is the window colour
                         (#2b2b2e), so it looks the same as a transparent picture. A PNG works too: change the
                         file name in the .rcss (div#background).
ND2BanksLabel.png        the BANKS lettering (top left), drawn by the BANKS element itself, so it is not in the
                         background picture.
The controls are sprites on top of it (below); their places are fixed (the .rcss, near the end, by id), so the
holes, rings and LED sockets in the background have to stay where they are.
Up to 0.1.7 the background was nine layers (ND2Layer01Body.png ... ND2Layer09Wordmark.png); they are in the
repository's history.

Editing the background and putting it back
------------------------------------------
1. Edit ND2Background.jpg (or your own full-size PNG of it) in GIMP; keep 3450 x 2070 px.
2. Export over ND2Background.jpg in your skin folder.
3. Moving or resizing a control? Change it in the .rcss too (positions are near the end, by id).
4. In the plugin: right-click > Reload skin (or F5) shows your changes.

Control sprites
---------------
ND2BlackButton.png      2 frames (up, pressed), 244 x 118 px each (2x of 122 x 59 dp). Program, Channel Select,
                        Row Select. Sandblasted grain like the value encoder (texture_sprites.py).
ND2GreyButton.png       same, grey. Parameter buttons, Shift, Lower Param, Trig.
ND2RedLed.png           2 frames (unlit, lit), 31 x 31 px each, shown as a 22 dp lamp. All LEDs except:
ND2GreenLed.png         same, green. The six TRIG / MG LEDs.
                        The lit frame fades in over the unlit one at the LED's brightness (the OS
                        dims some LEDs).
ND2MasterKnob.png       128 frames, 14 per row, 240 px cells (2x of 120 dp), 240 degrees; sandblasted grain
                        that turns with the knob. Shown in a 131 dp element.
ND2Encoder.png          value encoder: 72 frames = one full turn (5 degrees per frame), 12 per row,
                        448 px cells (2x of 224 dp). Seamless: frame 71 runs straight into frame 0.
                        Modelled on the hardware: flat sandblasted top, 18 flat side facets with fine
                        grooves, fixed light from the top left; only the knob turns.
ND2DisplaySegments.png  one display digit in 9 cells of 176 x 212 px (2x of 88 x 106 dp), slant and glow
                        included: unlit (all segments dark), then a, b, c, d, e, f, g, dp each lit on
                        its own. A digit = the unlit cell with the lit segments on top.

RML elements
------------
<pbutton button="N">    a panel button by index (0 Row Select, 1 Program, 2/3 Channel Select left/right,
                        8-11 and 16-19 parameter buttons 1-8, 24 Shift/Exit, 25 Trig, 26 Lower Param).
                        Class pressed while down (held with Alt-click; any click releases it).
<led code="N"><lamp/>   an LED by index; class lit while on; the lamp's opacity follows the brightness.
<knob control="master"> MASTER LEVEL: RCSS frames (128) and spriteprefix (master_ -> master_000 ...).
<encoder>               the value encoder: RCSS frames (72, one turn), detents (24 per turn) and
                        spriteprefix (enc_). Drag up/down or use the mouse wheel to turn it.
<segdigit pos="0..3"><seg class="a"/>...  one display digit; each seg child gets class lit when lit. A click on any
                        digit starts typed program entry (the plugin then shows the entry on the digits).
<action name="banks"><lamp/></action>  a plugin action, not a panel control: a click opens (or closes) the Banks
                        window; class lit while it is open (the default skin shows its lamp, ND2BanksLit.png,
                        a green outline around the BANKS lettering, only then).
