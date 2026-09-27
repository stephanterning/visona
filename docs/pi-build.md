# Building Visona on Raspberry Pi

Visona runs on **Raspberry Pi OS 64-bit** (Bookworm or later) as a native Linux ARM64 app using JUCE's ALSA backend. The target interface is a **RME Babyface Pro FS** in USB Class Compliant mode (no RME Linux drivers).

This document covers a developer build and manual smoke test. Kiosk mode, autostart, and fullscreen deployment are not part of this port yet.

## Prerequisites

Install build tools and JUCE's Linux dependencies:

```sh
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  libasound2-dev \
  libfreetype6-dev \
  libfontconfig1-dev \
  libx11-dev \
  libxext-dev \
  libxrandr-dev \
  libxinerama-dev \
  libxcursor-dev \
  libxcomposite-dev \
  libxi-dev \
  libgl1-mesa-dev
```

Optional, only if you want JUCE's JACK audio/MIDI backends in addition to ALSA:

```sh
sudo apt install -y libjack-jackd2-dev
```

Visona disables JUCE's curl and embedded web browser, so `libcurl` and WebKit/GTK development packages are **not** required.

CMake **3.22 or later** and **Ninja** are required. The `pi` preset uses **GCC** (`g++`) explicitly so the linker finds the matching `libstdc++` development files.

## Build

Clone the repository on the Pi, then configure and build with the `pi` preset:

```sh
git clone https://github.com/stephanterning/visona.git
cd visona
cmake --preset pi
cmake --build --preset pi
ctest --preset pi
```

The app binary is written to:

```text
build/pi/app/Visona_artefacts/Release/Visona
```

Build trees live under `build/<preset>/` as documented in [AGENTS.md](../AGENTS.md).

The same `pi` preset also works on other 64-bit Linux systems (for example an x86_64 desktop) when checking that the app compiles before copying sources to a Pi.

## Audio and MIDI setup

1. **ALSA** — Raspberry Pi OS includes ALSA. Plug in the Babyface Pro FS and confirm it appears:

   ```sh
   arecord -l
   ```

   In Class Compliant mode the device should enumerate as a standard USB audio interface.

2. **Permissions** — Add your user to the `audio` group so JUCE can open the ALSA device without root:

   ```sh
   sudo usermod -aG audio "$USER"
   ```

   Log out and back in (or reboot) for the group change to apply.

3. **Device selection in Visona** — On first run, open **Settings** and choose the Babyface under the ALSA input device list. Settings are stored in `~/.config/Visona/Visona.settings`.

4. **MIDI clock** — If you use an external MIDI clock source, select the corresponding ALSA MIDI input in Settings. MIDI timestamps use the same monotonic host clock as audio when the device does not supply block timestamps (see [decisions.md](decisions.md) D-065, D-078).

## Running the app

Visona is a desktop GUI application and needs a running X11 (or XWayland) session:

```sh
./build/pi/app/Visona_artefacts/Release/Visona
```

Press **D** to toggle the diagnostics overlay (block size, ring overruns, allocation count in Debug builds).

## Manual smoke test on Pi

1. Build and run the steps above on Raspberry Pi OS 64-bit with a display attached.
2. Connect the Babyface Pro FS in Class Compliant mode; confirm it appears in `arecord -l`.
3. Launch Visona; verify the window opens and the scope view repaints.
4. In Settings, select the Babyface ALSA input; confirm the status bar no longer reports a missing device.
5. Feed audio into the interface; confirm the waveform and level meters respond.
6. Optional: connect a MIDI clock source, select it in Settings, and confirm beat-synced sweep behaviour.

## Known limitations and blockers

| Topic | Status |
| --- | --- |
| **CI** | GitHub Actions still builds the app on macOS only. Linux jobs build `core/` and tests without JUCE. Pi builds are validated manually on hardware for now. |
| **Cross-compilation** | Not supported. Build natively on the Pi (or on another Linux machine with the same preset to check compilation only). |
| **Wayland-native** | JUCE on Linux targets X11. Running under XWayland on Pi desktop sessions is expected; a pure Wayland kiosk setup is future work. |
| **Device timestamps** | ALSA callbacks do not supply per-block host timestamps like CoreAudio. Visona falls back to `std::chrono::steady_clock` on Linux (see `app/src/HostTime.h`). MIDI/audio alignment may differ slightly from macOS until offset tuning is verified on Pi hardware. |
| **Hotplug / reconnect** | Same as macOS MVP: no hotplug or automatic reconnect (D-033). |
| **Kiosk / autostart** | Not implemented in this port. |

## Troubleshooting

- **`cannot find -lstdc++`** — Install `build-essential` and use the `pi` preset (GCC). If the default `c++` symlink points at Clang, ensure the matching `libstdc++-*-dev` package is installed or keep `CMAKE_CXX_COMPILER=g++` as the preset does.
- **`X11/extensions/XInput2.h: No such file or directory`** — Install `libxi-dev`.
- **No audio input / silence** — Check group membership (`audio`), cable and gain at the interface, and that the correct ALSA device is selected in Settings.
- **No display** — Run from a desktop session or set `DISPLAY` appropriately for your X server.
