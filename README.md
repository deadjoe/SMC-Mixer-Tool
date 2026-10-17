# SMC Mixer Tool

macOS app for the M-VAVE SMC-Mixer ([Wireless DAW Control Surface](https://www.m-vave.com/product?id=smc-mixer)). It reads the device identity and both stored profiles over USB-C or Bluetooth, edits the CC / User profile, and writes the changed records back.

![SMC Mixer Tool connected to an SMC-Mixer over USB-C, with knob 1 set to CC Rel](docs/screenshot.png)

## Download

[SMC Mixer Tool 1.0.3](https://github.com/deadjoe/SMC-Mixer-Tool/releases/tag/v1.0.3) is a universal macOS app for Apple silicon and Intel, on macOS 12 or later.

The app is not notarized. The first time you open it, Control-click SMC Mixer Tool and choose Open.

## Use

Connect the mixer with USB-C or Bluetooth and open the app. It reads a 6,282-byte settings image: the DAW profile, then the CC / User profile, 3,141 bytes each. Click a knob, fader, or button to see that control's stored settings. The panel only selects a control. It does not send performance MIDI.

CC / User and DAW on the inspector choose which stored profile is on screen. The mixer's live mode stays where it is. The M-VAVE manual switches it by holding Shift and pressing Left or Right.

In the CC / User view you can edit type, channel, CC, minimum, and maximum. Buttons have a mode, value 1, value 2, and LED. Knob types are CC Abs, Aftertouch, Note, and CC Rel. Midi Suite calls CC Rel "CW". Field names follow the type or mode: a CC Rel knob has CW and CCW values, and a Program button has Bank MSB, Bank LSB, and Program. A field the mixer does not send is marked not used. The messages each type sends are listed below. Revert restores the selected control. Write sends only the records that changed, reads those addresses back and checks them, then tells the mixer to save to flash so the edits survive a power cycle. If any step fails, nothing is saved. The edits stay on screen so Write can try again, and switching the mixer off and on discards whatever reached it. USB-C carries the payload as SysEx on the Private port. Bluetooth sends the same payload on the vendor channel. The DAW view is read-only.

The mixer sends each control message on its USB-C Master port, its Private port, and over Bluetooth at the same time. In a host or instrument, turn on only one of these inputs, or every message arrives two or three times.

Export saves the profile you are viewing as JSON. Copy puts that JSON on the clipboard. Channels in the file are numbered from 1.

The version next to the title is `1.0.3` plus the git commit used for the build. A dirty worktree adds `.dirty`. The version itself is set once, in `project()` in CMakeLists.txt.

The gear at the upper left opens diagnostics. It reports the USB-C Private and Master ports, the Bluetooth MIDI port, and which path carries settings. A Bluetooth connection also lists manufacturer, model, battery, signal, and services. Read snapshot loads the 6,282-byte image. Export .bin saves that image.

## MIDI each type sends

These were captured on the USB-C Master port with test values written to RAM only, and the original bytes were written back afterward. Value 1 and Value 2 were 17 and 34, so a fixed 0 or 127 could not pass for a stored value. `n` is the channel.

Knobs:

| Value | Type | Messages |
| --- | --- | --- |
| 0 | CC Abs | `Bn cc vv`, the value moving between Min and Max. Midi Suite calls it CC. |
| 1 | Aftertouch | `Dn vv`, channel pressure. No CC number is sent. |
| 2 | Note | Note On 104 plus the channel number on channel 1 while turning, velocity 127 then 0, and Pitch Bend `En 00 vv` on the knob's channel. This is a Mackie fader touch. No CC number is sent. |
| 3 | CC Rel | One `Bn cc vv` per step: Max clockwise, Min counter-clockwise. Midi Suite calls it CW. The DAW profile uses 1 and 65, Mackie's +1 and -1. |

Min and Max of a CC Rel knob are two values to send, not a range, so Min may be above Max.

Faders:

| Value | Type | Messages |
| --- | --- | --- |
| 0 | CC | `Bn cc vv` |
| 1 | Aftertouch | `Dn vv`, channel pressure. No CC number is sent. |
| 2 | DAW | Pitch Bend `En 00 vv`. The low byte is always 0. No CC number is sent. |

Buttons:

| Value | Mode | Messages |
| --- | --- | --- |
| 0 | CC Single | `Bn cc v1` on each press, nothing on release |
| 1 | CC Toggle | `Bn cc v2` and `Bn cc v1` on alternate presses |
| 2 | CC Push | `Bn cc v2` on press, `Bn cc v1` on release |
| 3 | Program | `Bn 00 cc`, `Bn 20 v1`, `Cn v2` on each press: Bank MSB, Bank LSB, Program |
| 4 | Custom | The stored message on each press, for example `F0 7F 7F 06 08 F7` |
| 5 | Note | `9n cc 7F` on press, `9n cc 00` on release. Value 1 and Value 2 are not sent. |

Not yet measured: what knob Speed does, whether Min and Max limit Note and Aftertouch knobs, and whether these types behave the same in DAW mode.

## Build

macOS 12 or later, CMake 3.22, and the Xcode command-line tools. CMake fetches JUCE 9.0.4, which keeps its own license. Each build signs the app bundle ad hoc, so macOS keeps one Bluetooth permission for it.

```
cmake -S . -B build
cmake --build build --target SMCMixerCoreTests
./build/SMCMixerCoreTests
cmake --build build --target SMCMixerGUI
open "build/SMCMixerGUI_artefacts/Debug/SMC Mixer Tool.app"
```

The release download is a universal Release build:

```
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-release --target SMCMixerGUI
```

The core tests cover the vendor frames, the snapshot layout, field limits, SysEx assembly, and the write sequence against a simulated mixer. They build with warnings as errors. GitHub Actions runs them and builds the app on macOS for every push and pull request.

## License

Source in this repository is under the Apache License 2.0. See [LICENSE](LICENSE).
