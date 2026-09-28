# Visona plugins

Visona is available as a **pass-through stereo effect** for use inside a DAW. Audio is copied to the output unchanged while the scope analyzes the incoming signal.

## Formats

| Format | Status | Identifier |
| --- | --- | --- |
| **VST3 — Visona** | Available (macOS arm64) | Manufacturer `Ster`, plugin `Visn` — browser shows **Stephan Terning → Visona** |
| **VST3 — Visona Sync** | Available (macOS arm64) | Manufacturer `Ster`, plugin `Sync` — **Stephan Terning → Visona Sync** |
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

## Sidechain sync (Visona Sync)

**Visona Sync** is a host-synced instrument that outputs a −6 dBFS impulse at the start of each bar. Route its audio to the **sidechain input** on **Visona** so the scope can align audio to the grid when track latency compensation is incomplete.

1. Add **Visona Sync** on an instrument track (no MIDI clip needed).
2. Enable **Visona** sidechain input in Live and route the Sync track to it.
3. Press Play — after a few bars the status bar shows **SC SYNC** when bar impulses are recognised.
4. If the sidechain signal does not look like Visona Sync, Visona shows **SC ?** and ignores it.

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

The VST3 bundles are at:

- `build/macos/plugin/VisonaPlugin_artefacts/Release/VST3/Visona.vst3`
- `build/macos/plugin/VisonaSyncPlugin_artefacts/Release/VST3/Visona Sync.vst3`

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

### If the scope stops or Live feels sluggish

Before removing or re-adding the plugin, note:

- Status line: **HOST RUN** or **STOPPED**
- Diagnostics overlay (**D**): sample rate, ring overruns, analysis load
- Whether Live's transport is playing
- Whether audio still passes through when the UI stops updating
- Whether the Mac became sluggish before or after the waveform stopped

Quit Live fully and restart if needed. Do **not** delete `~/Library/Preferences/Ableton` — that folder holds Live's preferences and authorization. If a full Live reset is ever required, follow [Ableton's documented procedure](https://help.ableton.com/hc/en-us/articles/209070849) and back up first.
