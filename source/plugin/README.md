# Plugin (to come)

VST3 / AU / Standalone wrapper around `nd2::Device` (see `source/core/nd2/device.h`):

- Host MIDI → `Device::midiIn` at the event's sample position; `Device::render` at the native
  38 085.9375 Hz, then a high-quality resampler to the host rate.
- Plugin state = the flash image (`Device::getFlash` / `setFlash`), so edits and loaded banks persist.
- First run: ask for the OS update file; optional import of `.nd2_bank` files via SysEx.
- macOS arm64, deployment target 13.0. Enabled with `-DND2_BUILD_PLUGIN=ON`.
