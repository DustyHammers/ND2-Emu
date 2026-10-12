# Default skin sources

The default skin (`skins/default/nd2-default/`) is made from these files. Since 0.1.8 its background is one baked
picture, `ND2Background.jpg` (the owner's art: everything but the controls), kept in the skin folder itself.

- `skin_source.html`: the panel as it was drawn up to 0.1.7, in layers; still the source of the control positions
  (`controls.json`), the display segments (`?seg=<segment>`), the BANKS lettering and its lit outline
  (`?sprite=banks`). Its background layers are no longer used.
- `build.js`: renders the page at 2x with Playwright (Chromium), into `out/`.
- `package.py`: assembles the skin folder (background, sprites, RML, RCSS) and the README preview in
  `out/ND2_default_skin/`.
- `app_icon.html`: the standalone app's icon (rendered by `build.js` to `out/app-icon.png`; goes to `skins/default/`).
- `render_encoder2.py`: renders the endless value encoder sprite sheet (72 frames, 448 px).
- `texture_sprites.py`: the buttons and the MASTER LEVEL knob with the encoder's grain, from `skins/default/source/`.
- `compress_pngs.py`: run last; the encoder and knob sheets as 256-colour images, every other PNG recompressed
  losslessly (`pip install pyoxipng imagequant`).
- `README_skin.txt`: the notes that go into the package.

The skin is not compiled into the plugin: each bundle carries it in `Contents/Resources/nd2-default/`.

`out/` is never committed.
