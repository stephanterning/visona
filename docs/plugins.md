# Visona plugins

Visona is available as a **pass-through stereo effect** for use inside a DAW. Audio is copied to the output unchanged while the scope analyzes the incoming signal. **Visona Sync** is a companion instrument that feeds Visona's sidechain, so Visona can measure how late the audio arrives after plugins with latency (see [Sidechain sync](#sidechain-sync-visona-sync)).

## Formats

Both plugins are built as VST3, AU and CLAP for macOS on Apple Silicon (macOS 14 or later). Every format uses manufacturer `Ster` and company name **Stephan Terning**, so hosts list them as **Stephan Terning → Visona** and **Stephan Terning → Visona Sync**.

| Plugin | VST3 | AU | CLAP |
| --- | --- | --- | --- |
| **Visona** (effect with sidechain) | Plugin code `Visn`, category Fx / Analyzer | Effect `aufx Visn Ster`, with the sidechain as a second input bus | ID `io.github.stephanterning.visona.plugin`, features audio-effect, analyzer |
| **Visona Sync** (instrument) | Plugin code `Sync`, category Instrument | Instrument `aumu Sync Ster`, with MIDI in | ID `io.github.stephanterning.visona.sync`, features instrument, utility |

Use one format per DAW; they behave the same. Ableton Live loads VST3 and AU but not CLAP. Reaper and Bitwig Studio load CLAP.

## Install

Release builds are published on [GitHub Releases](https://github.com/stephanterning/visona/releases), one zip per plugin and format:

| Zip | Bundle | Install folder |
| --- | --- | --- |
| `Visona-vst3-macos-arm64.zip` | `Visona.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| `Visona-sync-vst3-macos-arm64.zip` | `Visona Sync.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| `Visona-au-macos-arm64.zip` | `Visona.component` | `~/Library/Audio/Plug-Ins/Components/` |
| `Visona-sync-au-macos-arm64.zip` | `Visona Sync.component` | `~/Library/Audio/Plug-Ins/Components/` |
| `Visona-clap-macos-arm64.zip` | `Visona.clap` | `~/Library/Audio/Plug-Ins/CLAP/` |
| `Visona-sync-clap-macos-arm64.zip` | `Visona Sync.clap` | `~/Library/Audio/Plug-Ins/CLAP/` |

Unzip and copy each bundle into its folder; create the folder if it does not exist.

Alpha plugin builds are **unsigned** (ad-hoc signed in CI). Remove the quarantine flag after copying the bundles, then sign them ad hoc on your Mac. A DAW may refuse to load a bundle that is quarantined or whose signature no longer matches, for example after it was copied or rebuilt. For the formats you installed:

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

Do the same after installing a local build. Then restart the DAW or rescan its plugins.

### If a DAW does not show the AU

macOS keeps a cache of installed Audio Units. If a new or updated AU does not show up, or an old version keeps loading, refresh that cache, then restart the DAW:

```sh
killall -9 AudioComponentRegistrar
auval -a | grep Ster
```

`auval -a` lists every AU macOS knows of; both **Stephan Terning** plugins should be in the list. `auval -v aufx Visn Ster` and `auval -v aumu Sync Ster` validate them one at a time and should end with **AU VALIDATION SUCCEEDED**.

In Ableton Live, turn on **Use Audio Units** under **Settings → Plug-Ins**, then press **Rescan**. Rescanning is enough; do not delete any DAW preference folders to refresh plugins.

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

The bundles are under `build/macos/plugin/`, in a folder per format:

- `VisonaPlugin_artefacts/Release/VST3/Visona.vst3`, `AU/Visona.component` and `CLAP/Visona.clap`
- `VisonaSyncPlugin_artefacts/Release/VST3/Visona Sync.vst3`, `AU/Visona Sync.component` and `CLAP/Visona Sync.clap`

Plugin-only build, with the bundles under `build/macos-plugin/` instead:

```sh
cmake --preset macos-plugin && cmake --build --preset macos-plugin
```

The build also copies each bundle into its install folder under `~/Library/Audio/Plug-Ins/`, but it signs the build-tree copy only after that. To install a local build, replace the old bundles, then clear the quarantine flag and sign them as described in [Install](#install):

```sh
plugins=~/Library/Audio/Plug-Ins
build=build/macos-plugin/plugin
rm -rf "$plugins"/VST3/Visona*.vst3 "$plugins"/Components/Visona*.component "$plugins"/CLAP/Visona*.clap
mkdir -p "$plugins"/VST3 "$plugins"/Components "$plugins"/CLAP
cp -R "$build"/Visona*Plugin_artefacts/Release/VST3/*.vst3 "$plugins"/VST3/
cp -R "$build"/Visona*Plugin_artefacts/Release/AU/*.component "$plugins"/Components/
cp -R "$build"/Visona*Plugin_artefacts/Release/CLAP/*.clap "$plugins"/CLAP/
```

The CLAP builds use [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions), which CMake downloads, since JUCE 9.0.2 has no CLAP support of its own (D-098).

## Testing in Ableton Live

Live loads the VST3 and the AU; test one format at a time, since Live lists both.

1. Insert **Visona** on a stereo track that receives audio.
2. Press Play — the sweep should restart at bar 1 and kicks should land on the grid.
3. Stop — the view freezes with **STOPPED**.
4. Move the playhead and press Continue — the scope should show the new position.
5. Try 120, 126 and 174 BPM.
6. Put a plugin with latency before Visona and route Visona Sync to its sidechain. The status bar should show **SC** with that latency, and kicks should stay on the grid.

The AUs are under **Plug-Ins → Audio Units → Stephan Terning** in Live's browser.

## Testing CLAP in Reaper or Bitwig Studio

Ableton Live does not load CLAP plugins; use Reaper or Bitwig Studio.

- **Reaper:** open **Settings → Plug-ins → CLAP**, check that `~/Library/Audio/Plug-Ins/CLAP` is in the path list, and press **Re-scan**. Insert **CLAP: Visona (Stephan Terning)** on a track with audio. For the sidechain, set Visona's track to 4 channels and send the Visona Sync track to its channels 3/4. Reaper feeds channel 3 to Visona's sidechain input; check Visona's pin connector (the **2 in 2 out** button) if the status bar does not show **SC**.
- **Bitwig Studio:** CLAP plugins in `~/Library/Audio/Plug-Ins/CLAP` are found at startup; check **Settings → Locations → Plug-ins** if they are missing. Insert **Visona** from the browser on an audio track. Choose the Visona Sync track as the sidechain input in the device header.

Then go through steps 2–6 above. Visona Sync only writes impulses while the transport plays.

### If the scope stops or Live feels sluggish

Before removing or re-adding the plugin, note:

- Status line: **HOST RUN** or **STOPPED**, and **SC** with the measured latency if the sidechain is in use
- Diagnostics overlay (**D**): sample rate, block size, ring overruns, stream time, sidechain sync and analysis load
- Whether Live's transport is playing
- Whether audio still passes through when the UI stops updating
- Whether the Mac became sluggish before or after the waveform stopped

Quit Live fully and restart if needed. Do **not** delete `~/Library/Preferences/Ableton` — that folder holds Live's preferences and authorization. If a full Live reset is ever required, follow [Ableton's documented procedure](https://help.ableton.com/hc/en-us/articles/209070849) and back up first.
