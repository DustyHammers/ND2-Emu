# Default skin sources

The default skin (`skins/default/nd2-default/`) is made from these files:

- `skin_source.html`: one page that draws every background layer of the panel; `?layer=<name>` shows one layer on
  a transparent canvas, `?seg=<segment>` one display segment.
- `build.js`: renders all layers at 2x and the display segments with Playwright (Chromium), into `out/`.
- `package.py`: assembles the skin folder (layers, sprites, RML, RCSS) and a layered `.ora` file for GIMP in
  `out/ND2_default_skin/`.
- `render_encoder2.py`: renders the endless value encoder sprite sheet (72 frames, 448 px).
- `README_skin.txt`: the notes that go into the package.

`out/` is never committed.
