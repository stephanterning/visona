# Visona plugins

Visona is available as a **pass-through stereo effect** for use inside a DAW. Audio is copied to the output unchanged while the scope analyzes the incoming signal.

## Formats

| Format | Status | Identifier |
| --- | --- | --- |
| **VST3** | Available (macOS arm64) | Manufacturer `Ster`, plugin `Visn` — browser shows **Stephan Terning → Visona** |
| **AU** | Planned | Same four-character codes |
| **CLAP** | Planned | Same four-character codes |

Release builds are published as `Visona-vst3-macos-arm64.zip` on [GitHub Releases](https://github.com/stephanterning/visona/releases). Copy `Visona.vst3` into `~/Library/Audio/Plug-Ins/VST3/` and rescan in your DAW.

Alpha plugin builds are **unsigned** (ad-hoc signed in CI). Remove the quarantine flag if macOS blocks the bundle:

```sh
xattr -cr ~/Library/Audio/Plug-Ins/VST3/Visona.vst3
```

## Transport

The plugin follows the **host playhead**: tempo, position, play/stop and time signature come from the DAW. There is no MIDI Clock input and no FREE mode in the plugin editor.

The standalone app still uses MIDI Clock and manual FREE tempo.

## Editor

The plugin editor is a slimmed-down version of the standalone UI:

- Window, gain, waveform mode and zoom work as in the app
- Tempo is read-only (from the host)
- **Settings** opens appearance controls (waveform colour) only — no audio device or MIDI device panels
- Press **D** for the diagnostics overlay

Waveform mode and colour are saved **per plugin instance** in the DAW project. Global defaults live in `~/Library/Application Support/Visona/VisonaPluginDefaults.settings` and apply when an instance has no saved appearance.

## Build locally (macOS)

```sh
cmake --preset macos && cmake --build --preset macos
```

The VST3 bundle is at `build/macos/plugin/VisonaPlugin_artefacts/Release/VST3/Visona.vst3`.

Plugin-only build:

```sh
cmake --preset macos-plugin && cmake --build --preset macos-plugin
```

## Testing in Ableton Live

1. Insert **Visona** on a stereo track that receives audio.
2. Press Play — the sweep should restart at bar 1 and kicks should land on the grid.
3. Stop — the view freezes with **STOPPED**.
4. Move the playhead and press Continue — the scope should show the new position.
5. Try 120, 126 and 174 BPM.
