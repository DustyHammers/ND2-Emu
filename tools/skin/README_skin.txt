ND2 default skin
================

What's here
-----------
nd2-default/                The skin folder. It is also built into the plugin as the Default skin. A copy you
                            change goes in ~/Documents/ND2/skins/<new name>/ (rename the folder and its .rml and
                            .rcss files to the new name); it then appears in the right-click menu under Skin.
nd2-default_layers.ora      The background art as one layered file. GIMP opens it with every layer
                            (File > Open). Top layer "REFERENCE controls" shows where the buttons,
                            LEDs, knobs and display sit; hide it before exporting.
preview.png                 Everything together (2x).
encoder_turning.gif         The value encoder turning one full loop.
_source/                    The scripts that made all of this (layer source page, app icon page, encoder
                            renderer, button/knob grain, packager), so any part can be regenerated.

Canvas
------
1725 x 1035 dp = the body. All background art is drawn at 2x: 3450 x 2070 px, transparent outside what
each layer paints.

Background layers (bottom to top), one PNG each, all the same size so they stack exactly
-----------------------------------------------------------------------------------------
ND2Layer01Body.png       red body with its edge highlight and shadow
ND2Layer02Panels.png     black panel print (main panel, lower-param area, encoder circle, TRIG box)
ND2Layer03Display.png    display window (the digits are separate sprites on top)
ND2Layer04RedPrint.png   red print: MASTER LEVEL scale disc and ticks, ring around the encoder
ND2Layer05TagBoxes.png   tag boxes only (red boxes, white STORE box), no lettering
ND2Layer06Lines.png      lines and brackets (gaps already left for their titles)
ND2Layer07Text.png       all lettering, including the lettering of the tags (Liberation Sans Bold)
ND2Layer08Wordmark.png   wordmark, a plain "ND2 EMU" placeholder

Editing a layer in GIMP and putting it back
-------------------------------------------
1. Open nd2-default_layers.ora. Edit the layer you want.
2. To save a layer back: hide every other layer, then File > Export As..., and overwrite the matching
   ND2LayerNN...png in your skin folder (keep the full canvas size; GIMP exports only what's visible).
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
                        that turns with the knob. Shown in a 141 dp element.
ND2TrigLamp.png         the trigger input outline, 184 x 100 px (2x of 92 x 50 dp), green with its glow.
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
<segdigit pos="0..3"><seg class="a"/>...  one display digit; each seg child gets class lit when lit.
<trigin pad="1..6"><lamp/>  a trigger input: class lit while audio comes in on that input (held 0.3 s);
                        the lamp is shown then (opacity 1) and hidden otherwise (opacity 0).
