# ND2

![ND2 with its default skin](skins/default/preview.png)

Low-level emulation of a classic six-channel drum synthesizer, running its original OS on an emulated
dsPIC33E. VST3, AU and standalone for macOS (Apple Silicon).

## Firmware

No firmware, OS images, ROMs or presets are included in this repository or its releases, and none will
be provided. Please don't ask for them. The emulator needs an OS update file that you obtain yourself;
it is intended for owners of the original hardware.

## Installing on macOS

The builds are not notarised by Apple, so macOS blocks them until the quarantine flag is removed:

1. Copy `ND2.vst3` to `~/Library/Audio/Plug-Ins/VST3/`, `ND2.component` to `~/Library/Audio/Plug-Ins/Components/`
   and `ND2.app` to `/Applications/`.
2. In Terminal, type `sudo xattr -r -d com.apple.quarantine ` (ending with a space), drag the copied file onto the
   Terminal window and press Return. Repeat for each of the three.
3. For the AU, run `killall -9 AudioComponentRegistrar`, then rescan plugins in your host.

On first start, choose the OS update file and then the factory bank files.

## Using the panel

- Buttons: click. **Option-click** holds a button down (for SHIFT or LOWER PARAM combinations); click it again or
  press Esc to release it.
- Encoder and MASTER LEVEL: drag up or down, or use the scroll wheel; hold Shift for fine steps.
- Right-click the panel for skins, zoom, the OS update file and the factory banks.

## Disclaimer

This project is not affiliated with, endorsed by or connected to the manufacturer of the original
hardware. All trademarks belong to their respective owners. The original OS and presets are the
copyright of their owners.

## License

Free and open source under the GNU Affero General Public License v3.0; see `LICENSE`.
