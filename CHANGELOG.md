# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Auto gain, off by default, in the app's settings panel and the plugin's Appearance panel. It zooms the view in 3 dB steps so the loudest peak fills the top 3 dB of the lane: out as soon as a peak goes past the edge, and in at a bar line after the peaks have stayed low for 10, 30 or 60 s (30 s by default). Bars at or below −50 dBFS do not count, and setting the gain by hand turns it off.

### Changed

- Display gain goes up to +18 dB instead of +36 dB.

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
