# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- `scripts/pi/install-visona.sh` on the Pi: download the latest release or a given tarball URL, unpack to `~/visona`, and run `install-kiosk.sh`.
- AU and CLAP builds of Visona (AU effect with sidechain) and Visona Sync (AU instrument), for macOS arm64. CLAP is built with clap-juce-extensions, since JUCE 9.0.2 has no CLAP support (D-098).
- Release workflow validates the AUs with `auval` and the CLAPs with `clap-validator`, and publishes `Visona-au-macos-arm64.zip`, `Visona-sync-au-macos-arm64.zip`, `Visona-clap-macos-arm64.zip` and `Visona-sync-clap-macos-arm64.zip`.

- VST3 and CLAP builds of both plugins for Windows x64 and Linux x86_64, validated with pluginval and `clap-validator` and published as `Visona-{vst3,clap}-windows-x64.zip`, `Visona-sync-{vst3,clap}-windows-x64.zip`, `Visona-{vst3,clap}-linux-x86_64.tar.gz` and `Visona-sync-{vst3,clap}-linux-x86_64.tar.gz` (D-099).
- `windows-plugin` and `linux-plugin` CMake presets.
- Running the Release workflow by hand builds only the artifacts ticked, from any branch, and publishes them to the Development builds prerelease, named after the branch and commit (D-104).
- The standalone app hides the mouse cursor after three seconds of inactivity in fullscreen and kiosk mode.
- The app opens the saved audio device and MIDI input again when they are plugged back in, on macOS and on the Pi, and also when they were missing at startup. While the audio device is missing, `NO AUDIO INPUT` says so and no other device is opened (D-102).
- Auto gain, off by default, turned on and off with the AUTO button next to the gain, which is lit while it is on. It zooms the view in 3 dB steps so the loudest peak fills the top 3 dB of the lane: out as soon as a peak goes past the edge, and in at a bar line after the peaks have stayed low for 10 s. Bars at or below −50 dBFS do not count, and setting the gain by hand turns it off (D-100, D-108).
- On a touchscreen, a finger held still on a control for half a second shows its tooltip, which goes away when the finger lifts or moves (D-108).

### Changed

- Visona Sync saves a small version tag as its state instead of nothing, so CLAP hosts can restore it.
- Display gain goes up to +18 dB instead of +36 dB.
- When the chosen audio device disappears on macOS, the app no longer switches to the default input (D-102).
- In kiosk mode the control bar is up to twice the size, for touchscreens, and the full-screen button is hidden. On smaller screens it is scaled up less, so it stays on one row (D-105, D-108).
- The control bar stays on one row: WINDOW and WAVE are select menus that open upwards, each control has its caption inside it, and the buttons show only their icons when their labels do not fit. It wraps only in desktop windows narrower than about 710 pixels (D-108).
- A two-finger pinch keeps what is between the fingers under them, so moving both fingers also moves the zoomed view, as on iOS (D-108).
- While the settings panel is open, the rest of the window is dimmed, and a click or tap outside the panel closes it (D-108).

### Fixed

- In kiosk mode on the Pi, the window stays fullscreen when the display is switched on after boot, switched off and on again, or changes resolution. It used to shrink into a corner with a title bar (D-103).
- On the Pi, an audio interface that is unplugged no longer leaves a frozen view that still looks as if it were running (D-102).
- On the Pi, a two-finger pinch on the touchscreen zooms. `install-kiosk.sh` turns off labwc's touch mouse emulation, which Raspberry Pi OS turns on and which hides multitouch from apps (D-105).
- On a touchscreen, hover tooltips no longer appear after a tap and stay on screen (D-108).
- On the Pi touchscreen, a tap on a choice in the settings panel picks it. The lists for the audio system, device, sample rate, buffer size, inputs and MIDI input now open inside the window, like WINDOW and WAVE, and scroll with a drag when they are long (D-108).

## [0.1.0-alpha.2] - 2026-09-28

### Added

- VST3 plugin for macOS arm64: pass-through stereo effect with host transport, slim editor and per-instance appearance state.
- `HostTransport` and `HostAnalysisPipeline` in `core/` for DAW playhead sync.
- Visona Sync, a VST3 instrument for macOS arm64 that writes a −6 dBFS impulse at every bar line of the host timeline.
- Sidechain input on the Visona plugin. With Visona Sync on it, Visona measures how late the audio arrives after plugins with latency, shows it in the status bar (such as `SC +98.7 ms`) and draws the audio on the grid.
- Release workflow publishes `Visona-vst3-macos-arm64.zip` and `Visona-sync-vst3-macos-arm64.zip`.

### Fixed

- The Pi kiosk scripts find the `Visona` binary in the unpacked release tarball, not only in a source build.

### Known limitations (alpha)

- Plugins are VST3 for macOS Apple Silicon only, ad-hoc signed. AU and CLAP are not included yet.
- The plugins are tested in Ableton Live only.
- The sidechain signal is not validated yet: route only Visona Sync to it.

## [0.1.0-alpha.1] - 2026-09-28

### Added

- macOS standalone app for Apple Silicon (macOS 14+).
- Beat-synced sweep scope with MIDI Clock, Song Position Pointer and `FREE` manual tempo.
- Waveform modes STD, PRECISE and DJ with frequency colouring in DJ mode.
- Horizontal zoom, measurement ruler, window sizes from ¼ to 4 bars, and display gain.
- Linux ARM64 build for Raspberry Pi OS with kiosk autostart scripts.
- GitHub Actions: Linux core tests on every pull request; macOS and Pi app builds on release or manual trigger.

### Known limitations (alpha)

- macOS Apple Silicon only; unsigned app bundle (ad-hoc signed in release builds).
- No hotplug or automatic reconnect for audio/MIDI devices.
- Raspberry Pi builds are provided as a binary tarball; build on the Pi only for development.
- Plugins (VST3/AU/CLAP) are not included yet.

[0.1.0-alpha.2]: https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.2
[0.1.0-alpha.1]: https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.1
