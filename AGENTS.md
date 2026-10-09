# AGENTS.md

Visona is a beat-synced stereo oscilloscope for music production and, long-term, a modular, cross-platform real-time audio-monitoring platform. It is written in C++ with JUCE. The macOS standalone app and the VST3, AU and CLAP plugins are the current focus; the Raspberry Pi appliance follows.

## Language rule: English only

**Everything committed to this repository must be in English.** This covers:

- Source code, identifiers, and comments
- UI strings
- Documentation
- Commit messages and branch names
- Pull request titles and descriptions
- Issues

The maintainer may talk to agents in Swedish. Translate to English before anything is committed, and use the English terms that already exist in the source code.

## Build & test

Requires CMake 3.22+ and Ninja; on macOS also Xcode. macOS builds target Apple Silicon (arm64) only, with macOS 14.0 as the deployment target. CMake downloads JUCE 9.0.2, Catch2 v3 and, for the CLAP plugins, clap-juce-extensions with `FetchContent`.

- `core/`: plain C++20 library with no JUCE dependency. Warnings are errors.
- `app/`: the JUCE app, "Visona": audio device, audio callback, settings and wiring. Built only when `VISONA_BUILD_APP` is ON (the default on macOS only); otherwise JUCE is not downloaded.
- `plugin/`: the JUCE plugins, built as VST3, AU and CLAP: Visona, a pass-through stereo effect with host transport and an optional sidechain, and Visona Sync, an instrument that writes a bar impulse for that sidechain. Built when `VISONA_BUILD_PLUGIN` is ON (included in the `macos`, `macos-plugin` and `xcode` presets).
- `ui/`: JUCE components, compiled into the app and plugin targets.
- `tests/`: Catch2 tests for `core/`. Keep JUCE out of `core/` and `tests/`. Multi-threaded stress tests are tagged `[stress]`.
- `site/`: the static website, visona.org, with the download page and the user manual. `python3 site/build.py --serve` previews it; `.github/workflows/site.yml` deploys it to GitHub Pages. See `docs/website.md`. When a change alters what the user sees or does, update the manual in `site/static/manual/index.html` too.

## Website and manual on every pull request

Before opening a pull request, check the website (`site/`) and the manual (`site/static/manual/index.html`) against the change:

- If the change makes anything they already say wrong or out of date (features, settings, controls, keyboard shortcuts, formats, supported platforms, system requirements, screenshots), update it in the same pull request.
- If the change adds a feature users may need explained, consider adding it to the manual and, if it is a notable feature, to the presentation page. If you decide not to, say why in the pull request description.

The audio callback must not allocate, lock, wait or log. Debug builds of the app count allocations made in it (`app/src/RealtimeAllocationCheck.h`), and the diagnostics overlay (press D) shows the count, which must stay at 0.

### macOS prerequisites

Before the first build on a Mac:

1. Install CMake 3.22 or later with `brew install cmake`. The command-line `macos` preset also needs `brew install ninja`.
2. Select the full Xcode, accept its license and finish its first-launch setup:

   ```sh
   sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
   sudo xcodebuild -license accept
   xcodebuild -runFirstLaunch
   ```

If CMake reports "No CMAKE_C_COMPILER could be found", Xcode is not set up yet. Fix it with step 2, then run `rm -rf build/xcode` (or `build/<preset>` for another preset) before running the preset again, because CMake caches the failed compiler check.

### Presets

Presets are in `CMakePresets.json`; build trees go to `build/<preset>/`.

```sh
# macOS: Xcode project with the app, plugins and tests
cmake --preset xcode && open build/xcode/Visona.xcodeproj

# macOS: command-line build of the app and plugins, then run the tests
cmake --preset macos && cmake --build --preset macos && ctest --preset macos

# macOS: plugins and tests only
cmake --preset macos-plugin && cmake --build --preset macos-plugin && ctest --preset macos-plugin

# Windows (x64 Native Tools prompt) or Linux: plugins and tests only
cmake --preset windows-plugin && cmake --build --preset windows-plugin && ctest --preset windows-plugin
cmake --preset linux-plugin && cmake --build --preset linux-plugin && ctest --preset linux-plugin

# Any platform: core and tests only, without JUCE (as in Linux CI)
cmake --preset core-gcc && cmake --build --preset core-gcc && ctest --preset core-gcc
```

`core-clang`, `core-sanitize` (Clang with ASan and UBSan) and `core-tsan` (Clang with TSan) work the same way. Format C++ with `clang-format`. Pull-request CI in `.github/workflows/ci.yml` runs the `core-gcc`, `core-clang`, `core-sanitize` and `core-tsan` presets. The macOS app, the macOS VST3, AU and CLAP plugins (Visona and Visona Sync, checked with `auval` and `clap-validator`), the Windows x64 and Linux x86_64 VST3 and CLAP plugins (checked with `pluginval` and `clap-validator`) and the Pi ARM64 app (with the `pi-ci` preset, a parallel build of `pi`) are built by `.github/workflows/release.yml` when a GitHub release is published. Run by hand, that workflow builds only the artifacts ticked, from the branch chosen, and publishes them to the `dev-builds` prerelease. `.github/workflows/pi-quick.yml`, run by hand, publishes the Pi app the same way but faster, with the `pi-quick` preset: app only, parallel, `ccache`, no tests. Nothing checks that the JUCE targets compile on macOS, Windows or Linux before then.
