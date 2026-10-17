# SMC Mixer Tool

macOS app for the M-VAVE SMC-Mixer ([Wireless DAW Control Surface](https://www.m-vave.com/product?id=smc-mixer)). It reads the device identity and both stored profiles over USB-C or Bluetooth, edits the CC / User profile, and writes the changed records back.

![SMC Mixer Tool connected to an SMC-Mixer over Bluetooth](docs/screenshot.png)

## Download

[SMC Mixer Tool 1.0.2](https://github.com/deadjoe/SMC-Mixer-Tool/releases/tag/v1.0.2) is a universal macOS app for Apple silicon and Intel, on macOS 12 or later.

The app is not notarized. The first time you open it, Control-click SMC Mixer Tool and choose Open.

## Use

Connect the mixer with USB-C or Bluetooth and open the app. It reads a 6,282-byte settings image: the DAW profile, then the CC / User profile, 3,141 bytes each. Click a knob, fader, or button to see that control's stored settings. The panel only selects a control. It does not send performance MIDI.

CC / User and DAW on the inspector choose which stored profile is on screen. The mixer's live mode stays where it is. On the hardware, hold Shift and press Left or Right.

In the CC / User view you can edit type, channel, CC, minimum, and maximum. Buttons have a mode, value 1, value 2, and LED. Knob types are CC Abs, Aftertouch, Note, and CC Rel. Midi Suite calls CC Rel "CW". Field names follow the type or mode: a CC Rel knob has CW and CCW values, and a Program button has Bank MSB, Bank LSB, and Program. A field the mixer does not send is marked not used. PLAN.md lists the messages each type sends. Revert restores the selected control. Write sends only the records that changed, reads those addresses back and checks them, then tells the mixer to save to flash so the edits survive a power cycle. USB-C carries the payload as SysEx on the Private port. Bluetooth sends the same payload on the vendor channel. The DAW view is read-only.

Export saves the profile you are viewing as JSON. Copy puts that JSON on the clipboard. Channels in the file are numbered from 1.

The version next to the title is `1.0.2` plus the git commit used for the build. A dirty worktree adds `.dirty`. The version itself is set once, in `project()` in CMakeLists.txt.

The gear at the upper left opens diagnostics. It reports the USB-C Private and Master ports, the Bluetooth MIDI port, and which path carries settings. A Bluetooth connection also lists manufacturer, model, battery, signal, and services. Read snapshot loads the 6,282-byte image. Export .bin saves that image.

## Build

macOS 12 or later, CMake 3.22, and the Xcode command-line tools. CMake fetches JUCE 9.0.4, which keeps its own license. Each build signs the app bundle ad hoc, so macOS keeps one Bluetooth permission for it.

```
cmake -S . -B build
cmake --build build --target SMCMixerCoreTests
./build/SMCMixerCoreTests
cmake --build build --target SMCMixerGUI
open "build/SMCMixerGUI_artefacts/Debug/SMC Mixer Tool.app"
```

GitHub Actions runs the core tests and builds the app on macOS.

## License

Source in this repository is under the Apache License 2.0. See [LICENSE](LICENSE).
