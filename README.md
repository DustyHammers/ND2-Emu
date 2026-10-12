# ND2

![ND2 with its default skin](skins/default/preview.png)

Low-level emulation of a classic six-channel drum synthesizer, running its original OS on an emulated
dsPIC33E. VST3, AU and standalone for macOS (Apple Silicon; Intel is included but untested).

## Download

### [Download ND2 for macOS: latest version](https://github.com/DustyHammers/ND2-Emu/releases/latest/download/ND2-macOS.zip)

VST3, AU and standalone app in one zip, for macOS 13 or newer. See [Installing on macOS](#installing-on-macos)
below. Earlier versions are on the [releases page](https://github.com/DustyHammers/ND2-Emu/releases).

## What's new

### 0.1.7

- Banks window: click **BANKS** at the top left of the panel (or right-click > Banks...). Load any bank file into
  one of the 8 banks with Load... or by dragging it from the list (or from the Finder) onto a bank, and save a bank
  as a file. Bank files are the original manager's `.nd2_bank` files, kept in `~/Documents/ND2/Library/`.
- Loading goes through the OS's own program dump messages, one program at a time, as the original manager does
  with the hardware. ND2 keeps running meanwhile.
- The 8 banks are saved with each project. "Use these banks for new instances" makes them the banks new instances
  start with.
- The factory banks are in `Library/Factory/`: copies of the factory bank files you load, or, if you loaded them
  with an earlier version, made from ND2's factory memory the first time the Banks window opens.

### 0.1.6

- Logic Pro, GarageBand and MainStage: ND2 stayed silent and showed a plain "ND2" window. Apple's hosts offer an
  instrument a mono or stereo input, which ND2's Trigger In refused (all six channels or none), so the host could
  not start it. There, as in Ableton Live, Trigger In now also takes one to six channels; every other host keeps
  six or none.
- A short diagnostic log, `~/Documents/ND2/ND2-log.txt` (also in Console.app, search "ND2:"): each step from the
  host loading ND2 to its panel appearing. It helps find out why Logic Pro shows a plain "ND2" window instead of
  the panel.

### 0.1.5

- Lower CPU use on Apple Silicon: the rest of the faster processor emulation, and a faster sample-rate converter.
  The sound is identical, sample for sample.
- The plugin is now universal (Apple Silicon and Intel). Logic Pro running under Rosetta showed only a plain
  "ND2" window instead of the panel; it now gets the panel.
- The panel opens faster: its images are decoded on all cores at once instead of one after another.
- Smaller download: the panel's images are stored once in each plugin instead of inside the program (where a
  universal build would hold them twice), and the two animation sheets are compressed better.

### 0.1.4

- Panel: the strip of jacks above the panel and the output and MIDI labels are gone; CH1 to CH6 are centred.
- The body has bevelled sides and a rounded bottom edge, and the buttons come up through small cutouts in the panel.
- The buttons and the MASTER LEVEL knob have the same sandblasted finish as the value encoder, drawn at twice the
  resolution. The wordmark reads "nd2 Emu".
- The standalone app has its own icon.

### 0.1.3

- Slightly lower CPU use from a faster emulation of the processor. The sound is identical, sample for sample.

### 0.1.2

- Ableton Live: the VST3 no longer crashes when loaded and the AU is accepted. In Live, Trigger In takes one to
  six channels; every other host, REAPER included, keeps the six-channel Trigger In.
- Every build is now tested in a plugin host test (VST3 and AU).

### 0.1.1

- Trigger In takes all six channels (pads 1 to 6) or none. Hosts that asked for a stereo input first got only
  pads 1 and 2.

### 0.1.0

- First public version: VST3, AU and standalone with the default skin. Main output, six stereo channel outputs,
  six trigger inputs, MIDI in and out. Each project saves the complete machine, including presets stored on the
  panel.

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
- **BANKS** (top left) opens the Banks window; click it again, press Esc or click beside the window to close it.
- Right-click the panel for the Banks window, skins, zoom, the OS update file and the factory banks.

## Files and folders

ND2 creates `~/Documents/ND2/` the first time it runs, with four folders:

```
~/Documents/ND2/
  os/        your firmware/OS
  skins/     your own skins: a folder <name> containing <name>.rml and its files; listed under right-click > Skin
  Library/   bank files (.nd2_bank) for the Banks window; saved banks go here ("Bank 5 (11 Oct).nd2_bank")
    Factory/   the factory banks (copies of your own factory bank files)
  cache/     made by ND2, safe to delete:
             factory-<id>.flash        the factory memory new instances start with (built from your bank files,
                                       or the banks chosen with "Use these banks for new instances")
             factory-<id>.banks        the names of those banks
  skin-log.txt   skin messages, only while right-click > Developer > Write skin log is on
  ND2-log.txt    a short log of ND2 starting and its panel opening (for problem reports; kept small)
```

Settings shared by all instances (chosen skin, window size, skin log on/off) are in
`~/Library/Application Support/ND2/ND2.settings`. Each project stores its own complete machine, including presets
saved on the panel, so projects don't depend on these folders except for the OS.

Deleting `os/` makes ND2 ask for your firmware/OS again; deleting `cache/` makes it ask for the factory bank files
again (projects keep their own presets).

## Disclaimer

This project is not affiliated with, endorsed by or connected to the manufacturer of the original
hardware. All trademarks belong to their respective owners. The original OS and presets are the
copyright of their owners.

## License

Free and open source under the GNU Affero General Public License v3.0; see `LICENSE`.
