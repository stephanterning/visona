# AGENTS.md

Visona is a beat-synced stereo oscilloscope for music production and, long-term, a modular, cross-platform real-time audio-monitoring platform. It is written in C++ with JUCE. The macOS standalone app comes first; a Raspberry Pi appliance and VST3/AU plugins come later.

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

Requires CMake 3.22+ and Ninja; on macOS also Xcode. macOS builds target Apple Silicon (arm64) only, with macOS 14.0 as the deployment target. CMake downloads JUCE 9.0.2 and Catch2 v3 with `FetchContent`.

- `core/`: plain C++20 library with no JUCE dependency. Warnings are errors.
- `app/`: the JUCE app, "Visona". Built only when `VISONA_BUILD_APP` is ON (the default on macOS only); otherwise JUCE is not downloaded.
- `tests/`: Catch2 tests for `core/`. Keep JUCE out of `core/` and `tests/`. Multi-threaded stress tests are tagged `[stress]`.

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
# macOS: Xcode project with the app and tests
cmake --preset xcode && open build/xcode/Visona.xcodeproj

# macOS: command-line build of the app, then run the tests
cmake --preset macos && cmake --build --preset macos && ctest --preset macos

# Any platform: core and tests only, without JUCE (as in Linux CI)
cmake --preset core-gcc && cmake --build --preset core-gcc && ctest --preset core-gcc
```

`core-clang`, `core-sanitize` (Clang with ASan and UBSan) and `core-tsan` (Clang with TSan) work the same way. Format C++ with `clang-format`. CI in `.github/workflows/ci.yml` runs the `macos`, `core-gcc`, `core-clang`, `core-sanitize` and `core-tsan` presets on every pull request and on `main`.
