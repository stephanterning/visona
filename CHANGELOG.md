# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Analysis views in the standalone app and the Visona plugin, shown one at a time: Scope, Spectrum (FFT), Spectrum (Band) and Stereo. The view selector, first in the control bar, opens a list with a mouse and large tiles in kiosk mode or after a touch; V opens it and Alt+1 to Alt+4 choose a view. The view and its settings are saved in the app and per plugin instance, and projects saved before open in Scope. The views follow the incoming audio without MIDI Clock or host playback, and the app's Pause holds them (D-116).
- Spectrum (FFT): an 8,192-sample FFT with vendored KISS FFT 131.1.0 (BSD-3-Clause), a fast Solid curve in the waveform colour and a white history line, a shared Peak or RMS detector and octave smoothing, Stereo average, Left, Right, Mid and Side channels, adjustable range and slope, and a pointer and touch frequency readout (D-117).
- Spectrum (Band): 31 third-octave bands from a streaming filter bank, with peak markers (D-118).
- Stereo: Polar Sample, Polar Level with two profiles, and Lissajous over the last 100 ms, with correlation and balance from LSP's meters over 200 ms and per-channel signal validity. On Retina displays the plot is drawn at physical resolution (D-120).
- Afterglow, a select menu in the control bar outside Scope: Off, 0.5 s, 1 s or 2 s, for the FFT history line, the Band peak markers and the stereo views' persistence (D-119).
- `VISONA_BUILD_UI_CHECKS=ON` builds offline UI checks and screenshot renderers for the analysis views, and `VISONA_COPY_PLUGINS=OFF` keeps a local build from replacing the installed plugins.
- `scripts/pi/build-and-deploy-to-pi-over-network.sh` builds the Pi app in Docker on an Apple Silicon Mac and deploys it over SSH. With `--profile`, or `VISONA_PERF_LOG=1`, the app logs its frame rate and CPU use for `scripts/pi/measure-performance.py`, with the diagnostics overlay closed.

### Changed

- Display gain goes up to +64 dB, and auto gain up to +63 dB. The scope's reference lines continue in 3 dB steps without the former −18 dBFS limit (D-115).
- Gain and AUTO are shared by every analysis view. Auto gain follows the input's peaks in 100 ms windows instead of bars, so it also rises while the transport is stopped or lost (D-119).
- Outside Scope, the app's status bar names the view and reads `LIVE` while audio runs (D-116).
- The diagnostics overlay refreshes at 4 Hz in the app, so it costs less while measuring.


### Fixed

- Plugin preparation detaches the old audio stream before replacing it, including repeated host preparation and destruction without an explicit release.
- Visona Sync schedules bar impulses without heap allocation and retains impulses that round across a block boundary. Missing PPQ positions no longer produce repeated bar impulses; invalid host tempo and time signatures retain valid defaults.
- Plugin state clamps saved colour and window indices. Small host blocks retain enough timing slots, and time-signature changes update the scope's window length.
- Auto gain retains more than ten seconds of input peaks. If the UI misses the entire retained history, gain is preserved and waits for a new ten-second history before rising; current peaks can still lower it immediately. App settings writes are coalesced and flushed on shutdown.
- The app retries an input device that fails to start, stops or reports an error. Selecting the active view preserves its history, and plugin diagnostics no longer share timing counters between instances or consume modified D shortcuts.
- Visona Sync's editor reads atomic transport display values. Stereo diagnostics report the physical display scale and recent timing maxima; scope-only counters are hidden in other views. The spectrum stress test consumes the final snapshot even when its producer finishes first.


## [1.0.0-beta.1] - 2026-10-16

### Added

- The macOS app and the VST3, AU and CLAP plugins in releases are signed with a Developer ID and notarized by Apple, so they install without Gatekeeper warnings or `xattr` and ad-hoc signing by hand (D-114, first recorded as D-111).
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
- The macOS app has an icon, the website's waveform motif, drawn from one master in `assets/icon/` (D-112).
- The standalone app can measure how late the audio arrives after MIDI Clock from Visona Sync's bar impulse on an input of its own, chosen under **Visona Sync** in the settings, and moves MIDI Clock onto the audio by that much. The status bar shows the offset, such as `SC +12.3 ms` (D-109).
- A pause button first in the app's control bar holds the view and stops the analysis, to keep the screen still and save CPU. The status bar shows `PAUSED`, and `Resume` goes on where MIDI Clock is by then. Space or P presses it (D-110).

### Changed

- The pause, Diagnostics, Full screen and Settings buttons show only their icons, at every width; their labels are in the tooltips (D-113).
- `STOPPED` and `MIDI CLOCK LOST` in the status bar are no longer buttons. While MIDI Clock is stopped or lost, the pause button reads `Run free` and does what clicking them did (D-110).
- The MIDI Clock tempo holds steady instead of flickering by 0.1 BPM: it is fitted over up to four bars of clocks while the tempo holds, and over the last beat while it changes (D-111).
- Visona Sync saves a small version tag as its state instead of nothing, so CLAP hosts can restore it.
- Display gain goes up to +18 dB instead of +36 dB.
- When the chosen audio device disappears on macOS, the app no longer switches to the default input (D-102).
- In kiosk mode the control bar is up to twice the size, for touchscreens, and the full-screen button is hidden. On smaller screens it is scaled up less, so it stays on one row (D-105, D-108).
- The control bar stays on one row: WINDOW and WAVE are select menus that open upwards, each control has its caption inside it, and the buttons show only their icons when their labels do not fit. It wraps only in desktop windows narrower than about 710 pixels (D-108).
- A two-finger pinch keeps what is between the fingers under them, so moving both fingers also moves the zoomed view, as on iOS (D-108).
- While the settings panel is open, the rest of the window is dimmed, and a click or tap outside the panel closes it (D-108).
- The plugin's diagnostics row for the sidechain is called Visona Sync, as in the app (D-109).

### Fixed

- In kiosk mode on the Pi, the window stays fullscreen when the display is switched on after boot, switched off and on again, or changes resolution. It used to shrink into a corner with a title bar (D-103).
- On the Pi, an audio interface that is unplugged no longer leaves a frozen view that still looks as if it were running (D-102).
- On the Pi, a two-finger pinch on the touchscreen zooms. `install-kiosk.sh` turns off labwc's touch mouse emulation, which Raspberry Pi OS turns on and which hides multitouch from apps (D-105).
- On a touchscreen, hover tooltips no longer appear after a tap and stay on screen (D-108).
- On the Pi touchscreen, a tap on a choice in the settings panel picks it. The lists for the audio system, device, sample rate, buffer size, inputs and MIDI input now open inside the window, like WINDOW and WAVE, and scroll with a drag when they are long (D-108).

## [0.1.0-alpha.4] - 2026-10-09

Released from `main` without a section of its own here; its changes are listed under 1.0.0-beta.1 above. It added the pause button, Visona Sync with the app, the steady MIDI Clock tempo, the in-window settings lists, the ruler readout above the pointer, the native dark title bar, the app icon and the visona.org website ([release notes](https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.4)).

## [0.1.0-alpha.3] - 2026-10-05

Released from `main` without a section of its own here; its changes are listed under 1.0.0-beta.1 above. It added AU, CLAP and the Windows and Linux plugins, auto gain, the hidden idle cursor, device and display following, `install-visona.sh`, Development builds from a manual Release run, the faster Pi builds and the kiosk touch refinements ([release notes](https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.3)).

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

[Unreleased]: https://github.com/stephanterning/visona/compare/v1.0.0-beta.1...HEAD
[1.0.0-beta.1]: https://github.com/stephanterning/visona/compare/v0.1.0-alpha.4...v1.0.0-beta.1
[0.1.0-alpha.4]: https://github.com/stephanterning/visona/compare/v0.1.0-alpha.3...v0.1.0-alpha.4
[0.1.0-alpha.3]: https://github.com/stephanterning/visona/compare/v0.1.0-alpha.2...v0.1.0-alpha.3
[0.1.0-alpha.2]: https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.2
[0.1.0-alpha.1]: https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.1
