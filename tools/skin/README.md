# Default skin sources

The default skin (`skins/default/nd2-default/`) is made from these files:

- `skin_source.html`: one page that draws every background layer of the panel; `?layer=<name>` shows one layer on
  a transparent canvas, `?seg=<segment>` one display segment.
- `build.js`: renders all layers at 2x and the display segments with Playwright (Chromium), into `out/`.
- `package.py`: assembles the skin folder (layers, sprites, RML, RCSS) and a layered `.ora` file for GIMP in
  `out/ND2_default_skin/`.
- `app_icon.html`: the standalone app's icon (rendered by `build.js` to `out/app-icon.png`; goes to `skins/default/`).
- `render_encoder2.py`: renders the endless value encoder sprite sheet (72 frames, 448 px).
- `texture_sprites.py`: the buttons and the MASTER LEVEL knob with the encoder's grain, from `skins/default/source/`.
- `compress_pngs.py`: run last; the encoder and knob sheets as 256-colour images, every other PNG recompressed
  losslessly (`pip install pyoxipng imagequant`).
- `README_skin.txt`: the notes that go into the package.

The skin is not compiled into the plugin: each bundle carries it in `Contents/Resources/nd2-default/`.

`out/` is never committed.
