# Visona

**Visona** is a beat-synced stereo oscilloscope for music production. It shows your audio as a musical sweep scope locked to MIDI Clock from your DAW, with manual tempo when no clock is present. As a VST3, AU or CLAP plugin it follows the DAW's transport directly.

![Visona scope in action](docs/images/visona-scope.png)

## Download

Pre-built binaries are published on **[GitHub Releases](https://github.com/stephanterning/visona/releases)**.

| Platform | Requirement | Package |
| --- | --- | --- |
| **macOS** | Apple Silicon, macOS 14 or later | `Visona-macos-arm64.zip` — unzip and open `Visona.app` |
| **macOS plugin, Visona** | Apple Silicon, macOS 14 or later | `Visona-vst3-macos-arm64.zip`, `Visona-au-macos-arm64.zip` or `Visona-clap-macos-arm64.zip` — see the install folders below ([plugin guide](docs/plugins.md)) |
| **macOS plugin, Visona Sync** | Apple Silicon, macOS 14 or later | `Visona-sync-vst3-macos-arm64.zip`, `Visona-sync-au-macos-arm64.zip` or `Visona-sync-clap-macos-arm64.zip` ([sidechain sync](docs/plugins.md#sidechain-sync-visona-sync)) |
| **Windows plugins** | Windows 10 or 11, x64 | `Visona-vst3-windows-x64.zip`, `Visona-clap-windows-x64.zip` and the `Visona-sync-…` zips ([install](docs/plugins.md#windows)) |
| **Linux plugins** | x86_64, Ubuntu 24.04, Debian 13, Fedora 39 or newer | `Visona-vst3-linux-x86_64.tar.gz`, `Visona-clap-linux-x86_64.tar.gz` and the `Visona-sync-…` tarballs ([install](docs/plugins.md#linux)) |
| **Raspberry Pi** | Raspberry Pi OS 64-bit (desktop), ARM64 | `Visona-linux-arm64-pi.tar.gz` — see [Pi setup](docs/pi-setup.md) and [kiosk mode](docs/pi-kiosk.md) |

Alpha builds are **unsigned**. On macOS, if Gatekeeper blocks the app, remove the quarantine flag:

```sh
xattr -cr /path/to/Visona.app
```

On macOS, copy each plugin bundle into the folder for its format (Windows and Linux: see the [plugin guide](docs/plugins.md#install)):

| Format | Bundles | Install folder |
| --- | --- | --- |
| VST3 | `Visona.vst3`, `Visona Sync.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| AU | `Visona.component`, `Visona Sync.component` | `~/Library/Audio/Plug-Ins/Components/` |
| CLAP | `Visona.clap`, `Visona Sync.clap` | `~/Library/Audio/Plug-Ins/CLAP/` |

Then remove the quarantine flag and sign the bundles ad hoc on your Mac. A DAW may refuse to load a bundle that is quarantined or whose signature no longer matches, for example after it was copied or rebuilt. For the formats you installed:

```sh
cd ~/Library/Audio/Plug-Ins
xattr -cr VST3/Visona.vst3 VST3/Visona\ Sync.vst3
xattr -cr Components/Visona.component Components/Visona\ Sync.component
xattr -cr CLAP/Visona.clap CLAP/Visona\ Sync.clap
codesign --force --sign - --deep VST3/Visona.vst3
codesign --force --sign - --deep VST3/Visona\ Sync.vst3
codesign --force --sign - --deep Components/Visona.component
codesign --force --sign - --deep Components/Visona\ Sync.component
codesign --force --sign - --deep CLAP/Visona.clap
codesign --force --sign - --deep CLAP/Visona\ Sync.clap
```

Then restart the DAW or rescan its plugins. If a new AU does not show up, refresh macOS's AU cache with `killall -9 AudioComponentRegistrar` and restart the DAW; see the [plugin guide](docs/plugins.md#if-a-daw-does-not-show-the-au).

The **plugin** (pass-through stereo effect, host transport) ships as VST3, AU and CLAP on macOS, and as VST3 and CLAP on Windows and Linux. **Visona Sync**, a companion instrument, puts a bar impulse on the plugin's sidechain so Visona can measure how late audio arrives after plugins with latency in Ableton Live, and draw it on the grid. Ableton Live loads the VST3 or AU; for CLAP, use a host such as Reaper or Bitwig Studio. See [docs/plugins.md](docs/plugins.md).

## What you get

- **Sweep scope** with bar grid, stacked L/R lanes and a bright write head
- **MIDI Clock** sync with Start, Stop, Continue and Song Position Pointer
- **FREE mode** — free-running sweep at a manual BPM when MIDI Clock is absent
- **Windows** from ¼ bar to 4 bars, **horizontal zoom** and a **measurement ruler**
- **Waveform modes** STD, PRECISE and DJ (frequency colouring in DJ mode)
- **Display gain** 0–18 dB, with optional **auto gain** in 3 dB steps, settings persistence, fullscreen

Works with professional USB audio interfaces. Development and testing use an **RME Babyface Pro FS**; on Linux and Raspberry Pi the interface must run in **USB Class Compliant mode**.

## Quick start (macOS)

1. Download the latest release and open `Visona.app`.
2. Connect your audio interface and a MIDI clock source (optional).
3. In **Settings**, choose the audio input device and MIDI port.
4. Press **D** to toggle the diagnostics overlay.

## Raspberry Pi appliance

See [docs/pi-setup.md](docs/pi-setup.md), [docs/pi-build.md](docs/pi-build.md) and [docs/pi-kiosk.md](docs/pi-kiosk.md). For normal use, download the release tarball rather than building on the Pi; it holds the binary and the kiosk scripts.

## License

AGPL-3.0-or-later. See [LICENSE](LICENSE).

## Developers

Build and test instructions are in [AGENTS.md](AGENTS.md). Architecture, roadmap and decisions live under [docs/](docs/).

### Development builds

To test a branch without merging it, or a `develop` branch you have put together from several, open **Actions → Release → Run workflow**, choose the branch and tick the artifacts you need. Only the jobs for those artifacts run. The files are published to the **[Development builds](https://github.com/stephanterning/visona/releases/tag/dev-builds)** prerelease, named after the artifact, the branch and the commit, and the run's summary links them. A new build of the same artifact from the same branch replaces the earlier one (D-104).

On the Pi, pass the tarball URL from the workflow summary to `install-visona.sh` (see [pi-kiosk.md](docs/pi-kiosk.md#install-or-upgrade-over-ssh)):

```sh
curl -fsSL https://raw.githubusercontent.com/stephanterning/visona/main/scripts/pi/install-visona.sh | bash -s -- \
  'https://github.com/stephanterning/visona/releases/download/dev-builds/Visona-linux-arm64-pi-develop-abc1234.tar.gz'
```
