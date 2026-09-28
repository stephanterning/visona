# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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

[0.1.0-alpha.1]: https://github.com/stephanterning/visona/releases/tag/v0.1.0-alpha.1
