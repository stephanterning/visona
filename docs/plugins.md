# Visona plugins

Visona is available as a **pass-through stereo effect** for use inside a DAW. Audio is copied to the output unchanged while the scope analyzes the incoming signal. **Visona Sync** is a companion instrument that feeds Visona's sidechain, so Visona can measure how late the audio arrives after plugins with latency (see [Sidechain sync](#sidechain-sync-visona-sync)).

## Formats

| Format | Status | Identifier |
| --- | --- | --- |
| **VST3 — Visona** | Available (macOS arm64) | Manufacturer `Ster`, plugin `Visn` — browser shows **Stephan Terning → Visona** |
| **VST3 — Visona Sync** | Available (macOS arm64) | Manufacturer `Ster`, plugin `Sync` — **Stephan Terning → Visona Sync** |
| **AU** | Planned | Same four-character codes |
| **CLAP** | Planned | Same four-character codes |

Release builds are published on [GitHub Releases](https://github.com/stephanterning/visona/releases) as `Visona-vst3-macos-arm64.zip` (`Visona.vst3`) and `Visona-sync-vst3-macos-arm64.zip` (`Visona Sync.vst3`). Copy the bundles into `~/Library/Audio/Plug-Ins/VST3/` and rescan in your DAW.

Alpha plugin builds are **unsigned** (ad-hoc signed in CI). Remove the quarantine flag after copying the bundles, then sign them ad hoc on your Mac. A DAW may refuse to load a bundle that is quarantined or whose signature no longer matches, for example after it was copied or rebuilt:

```sh
xattr -cr ~/Library/Audio/Plug-Ins/VST3/Visona.vst3
xattr -cr ~/Library/Audio/Plug-Ins/VST3/Visona\ Sync.vst3
codesign --force --sign - --deep ~/Library/Audio/Plug-Ins/VST3/Visona.vst3
codesign --force --sign - --deep ~/Library/Audio/Plug-Ins/VST3/Visona\ Sync.vst3
```

Do the same after installing a local build. Then restart the DAW or rescan its plugins.

## Transport

The plugin follows the **host playhead**: tempo, position, play/stop and time signature come from the DAW, as reported at the first frame of each audio block. There is no MIDI Clock input and no FREE mode in the plugin editor. The status bar shows **HOST RUN** while the DAW plays and **STOPPED** while it is stopped.

The standalone app still uses MIDI Clock and manual FREE tempo.

## Sidechain sync (Visona Sync)

Ableton Live reports the same playhead to every plugin on a track, including a plugin placed after plugins with latency, such as a lookahead limiter or a linear-phase EQ. The audio reaching Visona is then late by their latency, and the waveform lands that much after the grid. **Visona Sync** measures that latency.

Visona Sync is a host-synced instrument that writes a −6 dBFS impulse at the start of every bar. Live aligns a sidechain with the main input of the plugin it feeds, so the impulse reaches Visona exactly as late as the audio. Visona measures how far after its playhead's bar line the impulse arrives, and draws the audio that much earlier.

1. Add **Visona Sync** on an instrument track (no MIDI clip needed).
2. In **Visona**, choose the Sync track as the sidechain input (**Post FX**).
3. Press Play. From the first bar line the status bar shows **SC** and the measured latency, such as **SC +98.7 ms**. The diagnostics overlay (**D**) shows it in samples, next to the sidechain level. **SC ...** means the sidechain input is on but no impulse has arrived yet.
4. Visona keeps the latency it measured. When plugins with latency are added or removed before Visona, the new value takes over after two bars, and the sweep starts over.

The sidechain signal is not validated yet: any peak above −20 dBFS counts as a bar impulse, so route nothing but Visona Sync to it. If **Reduced Latency When Monitoring** is on in Live's Options menu, keep the Sync track unarmed, because Live skips delay compensation for monitored tracks.

## Editor

The plugin editor is a slimmed-down version of the standalone UI:

- Window, gain, waveform mode and zoom work as in the app
- The tempo comes from the host and is shown in the status bar; the BPM control and the full screen button are hidden
- **Settings** opens appearance controls (waveform colour) only — no audio device or MIDI device panels
- Press **D** for the diagnostics overlay, which includes a sidechain sync row

Waveform mode and colour are saved **per plugin instance** in the DAW project. Global defaults live in `~/Library/Application Support/Visona/VisonaPluginDefaults.settings` and apply when an instance has no saved appearance.

## Build locally (macOS)

```sh
cmake --preset macos && cmake --build --preset macos
```

The VST3 bundles are at:

- `build/macos/plugin/VisonaPlugin_artefacts/Release/VST3/Visona.vst3`
- `build/macos/plugin/VisonaSyncPlugin_artefacts/Release/VST3/Visona Sync.vst3`

Plugin-only build, with the bundles under `build/macos-plugin/` instead:

```sh
cmake --preset macos-plugin && cmake --build --preset macos-plugin
```

To install a local build, replace the old bundles, then clear the quarantine flag and sign them as described above:

```sh
rm -rf ~/Library/Audio/Plug-Ins/VST3/Visona.vst3 ~/Library/Audio/Plug-Ins/VST3/Visona\ Sync.vst3
cp -R build/macos-plugin/plugin/VisonaPlugin_artefacts/Release/VST3/Visona.vst3 \
      "build/macos-plugin/plugin/VisonaSyncPlugin_artefacts/Release/VST3/Visona Sync.vst3" \
      ~/Library/Audio/Plug-Ins/VST3/
```

## Testing in Ableton Live

1. Insert **Visona** on a stereo track that receives audio.
2. Press Play — the sweep should restart at bar 1 and kicks should land on the grid.
3. Stop — the view freezes with **STOPPED**.
4. Move the playhead and press Continue — the scope should show the new position.
5. Try 120, 126 and 174 BPM.
6. Put a plugin with latency before Visona and route Visona Sync to its sidechain. The status bar should show **SC** with that latency, and kicks should stay on the grid.

### If the scope stops or Live feels sluggish

Before removing or re-adding the plugin, note:

- Status line: **HOST RUN** or **STOPPED**, and **SC** with the measured latency if the sidechain is in use
- Diagnostics overlay (**D**): sample rate, block size, ring overruns, stream time, sidechain sync and analysis load
- Whether Live's transport is playing
- Whether audio still passes through when the UI stops updating
- Whether the Mac became sluggish before or after the waveform stopped

Quit Live fully and restart if needed. Do **not** delete `~/Library/Preferences/Ableton` — that folder holds Live's preferences and authorization. If a full Live reset is ever required, follow [Ableton's documented procedure](https://help.ableton.com/hc/en-us/articles/209070849) and back up first.
