# Visona architecture

This document describes what Visona is, the principles it is built on, and the architecture of MVP 1.0. Milestones and steps are in [roadmap.md](roadmap.md). Decisions are referenced as D-NNN and recorded in [decisions.md](decisions.md).

## 1. Vision

Visona is a platform-independent real-time engine for musical audio analysis and visualization.

- **The scope is the first product.** It is a beat-synced stereo oscilloscope. It shows ¼, ½, 1, 2 or 4 bars in a sweep display, locked to musical time instead of milliseconds (D-020, D-034).
- **The monitoring platform is the architecture.** The scope is the first module. Spectrum, phase/vectorscope, correlation, levels, True Peak and loudness are meant to follow as widgets (D-028).
- **The long-term goal is a dedicated studio meter.** A Raspberry Pi with a small touchscreen should eventually be able to replace a hardware loudness and stereo meter.
- **Targets:**
  - MVP 1.0 is a macOS standalone app on Apple Silicon (D-033, D-044).
  - A VST3, AU and CLAP plugin for macOS, also built as VST3 and CLAP for Windows and Linux, follows the DAW's transport (D-096), with the Visona Sync companion instrument (D-097, D-098, D-099). A Linux ARM64 build runs on the Raspberry Pi.
  - The Raspberry Pi appliance, and Windows and Linux desktop apps, come later, all on the same analysis engine and UI.
- **More inputs than stereo.** Inputs are modeled as *sources*, each a group of 1..N channels. MVP 1.0 has exactly one stereo source, but the core never hardcodes two channels (D-049, D-052).
- **Free software.** Visona is licensed under AGPLv3 and is not a commercial product (D-042).

## 2. Principles

1. **Real-time and audio-safe.** The audio callback is a real-time thread (D-006).
   - It does not allocate, do file I/O, wait on the UI, hold long locks or log synchronously.
   - Audio is processed in blocks (D-007).
   - The audio thread produces data and the UI consumes it; a slow UI never affects audio (D-008).
   - Priorities, in order: audio reliability, correct synchronization, correct analysis, responsive controls, smooth rendering.
2. **Platform-agnostic core.** The core knows nothing about macOS, Raspberry Pi, CoreAudio, ALSA, a specific audio interface or the screen size (D-003).
   - Sample rate and channel count are never hardcoded (D-009, D-049).
   - Code is split by responsibility, not by platform (D-004).
3. **Analysis is separate from presentation.** Display gain, frequency coloring and similar settings are presentation only. They never change audio, analysis results or buffered data (D-024). State is kept in three kinds: analysis state, user configuration and platform/runtime state (D-005).
4. **Correct waveform first.** The waveform shape is always the full-band signed min/max. Frequency coloring is a visualization aid and never alters the shape (D-050, D-056).
5. **Touch-first and fully scalable UI.**
   - Large touch targets, and no required hover or right-click (D-025).
   - No assumptions about resolution or aspect ratio: rendering works from component bounds (D-026).
   - The visualization takes priority over chrome (D-027).
6. **Own the product, not every algorithm.**
   - Visona owns the architecture, transport model, scope behavior, UI, rendering and UX.
   - Well-tested libraries are reused for FFT, EBU R128, True Peak and tempo detection, always behind Visona's own interfaces (D-029).
7. **Implement what is required now; create seams for what is likely later.**
   - One general transport interface from the start, but only the transports that are needed get implemented (D-011, D-012).
   - A widget model from the start, but no dashboard editor before the scope works.
8. **Testable without hardware.** The core, transport and rendering are tested with synthetic signals and synthetic MIDI Clock (D-032).

## 3. MVP architecture

### 3.1 Layers

- **`core/`: plain C++20 with no JUCE dependency** (D-054). It contains:
  - ring buffer and triple buffer
  - source layout
  - `MidiClockTransport` and `ClockTimeMapper`
  - `BandSplitter` and `SweepAnalyzer`
  - snapshot types and bin-to-pixel reduction
  - the measurement ruler's readout (`Ruler`, D-094)
  - `AnalysisPipeline`, the analysis thread's work without the thread: ring in, snapshots out
  - for the plugins: `HostTransport` and `HostAnalysisPipeline`, the same work driven by the host playhead (D-096), and `BarImpulseScheduler` and `SidechainSyncDetector` for Visona Sync (D-097), which the app's `AnalysisPipeline` also uses on its sync input (D-109)

  Keeping JUCE out makes the core testable on Linux without a GUI, enforces the platform boundary, and lets the core be reused in plugins and on the Raspberry Pi.
- **`app/` (JUCE):** audio device management, audio callback, MIDI input, the analysis thread, settings and wiring.
- **`plugin/` (JUCE):** the Visona effect (`PluginProcessor`, its analysis thread and `ScopeEditor`) and the Visona Sync instrument (`SyncProcessor`, `SyncEditor`). See 3.7.
- **`ui/` (JUCE):** `ScopeView`, `StatusBar`, `ControlBar`, `SettingsPanel` and `DiagnosticsOverlay`, shared by the app and the plugin.

### 3.2 Threads and data flow

```
Audio thread (JUCE callback)              MIDI thread (JUCE MidiInput)
  planar samples[ch][n]                     F8/FA/FB/FC/F2 + host time
  + BlockTiming{sampleIndex, hostTimeNs, n}
        │ SPSC ring (lock-free)                    │ SPSC ring (lock-free)
        └──────────────────┬───────────────────────┘
                           ▼
            Analysis thread (own thread, polls every ~2–5 ms)
              ClockTimeMapper: MIDI host time → sample index
              MidiClockTransport: ticks → musical position
              BandSplitter: per channel → low / mid / high (coloring only)
              SweepAnalyzer: signed min/max per bin, write head
                           │ triple buffer (lock-free, latest wins)
                           ▼
            UI thread (VBlankAttachment, ≤ 60 fps)
              ScopeView / StatusBar / ControlBar
```

- **Audio thread.** Copies input into the ring and pushes `BlockTiming`, and nothing else.
  - `sampleIndex` is a 64-bit running frame counter since the stream started. All musical time relates to it.
  - The channel count comes from the source layout.
  - The device is opened with all of its input channels, and the callback copies the input channel chosen for each source channel (D-063).
- **MIDI thread.** Filters real-time messages and Song Position Pointer (SPP), and pushes `MidiClockEvent{type, sppValue, hostTimeNs}`.
- **Analysis thread.** Not hard real-time, but allocation-free in steady state.
  - It runs about one tick behind audio, so position is *interpolated* between known ticks rather than extrapolated.
- **UI thread.** Reads the latest snapshot and renders it.
  - Window changes reach the analysis through an atomic or a small command queue.
  - Display gain is applied only when rendering.
- **Ring size.** About 1 s of audio. Overruns are counted and shown in diagnostics.

### 3.3 MIDI Clock transport

MIDI Clock is the only external clock source in MVP 1.0 (D-036). Without it, the sweep runs free at a tempo set by hand (D-090). The messages are:
- Clock `F8` (24 PPQN)
- Start `FA`, Continue `FB`, Stop `FC`
- SPP `F2`: a 14-bit value in MIDI beats, where one MIDI beat is a sixteenth note, or 6 ticks.

Position is counted in ticks, with `ticksPerBar = 24 × numerator × 4 / denominator`. That is 96 in 4/4, which is the MVP default (D-017).

| Event | Effect (D-045, D-059, D-090) |
|---|---|
| Startup | `FREE`: the sweep runs free in the chosen window at the free tempo |
| Start | Position = 0, `Running`. The first Clock after Start is the downbeat of bar 1. The sweep clears and restarts at x = 0 |
| Clock in `Running` | Position += 1 tick; the tick's sample time is recorded |
| Clock in `Stopped`/`FREE` | Updates the BPM estimate only (some DAWs send clock while stopped) |
| Stop | `Stopped`. Position is kept; the UI freezes the last frame and shows `STOPPED` |
| SPP | Position = SPP × 6 ticks. Accepted whenever not `Running` (D-073); ignored and counted in `Running` |
| Continue | `Running` from the current position. The write head jumps to that phase, and existing content becomes the previous pass. From `FREE`, the sweep starts over as on Start, but at the SPP position |
| > 0.5 s without Clock in `Running` | `ClockLost`: freeze and show `MIDI CLOCK LOST` |
| Clock returns in `ClockLost` | `Running` again, continuing the tick count. Position may be off until the next Start, or SPP + Continue |
| Click on `STOPPED` or `MIDI CLOCK LOST` | `FREE` at the last MIDI tempo; the sweep starts over at bar 1. The position is kept for Continue (D-090) |

The clock-loss timeout only applies in `Running`, so a DAW that stops sending clock on Stop does not trigger a false `MIDI CLOCK LOST`.

**Position per audio sample.** Between tick *k* at sample *s_k* and tick *k+1* at *s_{k+1}*, position = *k + (s − s_k) / (s_{k+1} − s_k)*.
- Audio is analyzed only up to the latest known tick. This costs about one tick of visual latency (≈ 21 ms at 120 BPM).
- On Stop or clock loss, the last interval is extrapolated by at most one tick and then held. On Stop, the extrapolation also ends at the Stop.
- The transport hands the analysis a timeline of spans, each saying how to treat its stretch of audio: free-running, musical (with the position), frozen, or pending until the next tick (D-075).
- Time in the transport is audio time: the clock-loss timeout runs on the audio's sample timeline.

**BPM.** `BPM = 60 × sampleRate / (24 × tick interval in samples)`.
- The estimate is the least-squares tempo of the last 25 clocks, or 24 intervals (one beat). A gap longer than the clock-loss timeout starts it over, and so does Start. It is displayed with 0.1 BPM resolution (D-074).
- There is no PLL in the MVP (D-035).

**MIDI time to audio sample time**

- MIDI events carry a host time from JUCE's timestamp. On macOS it is the CoreMIDI packet time, the driver's receive time, converted to JUCE's millisecond counter; Visona converts it back to host nanoseconds (D-077).
- Audio blocks carry a host time from `AudioIODeviceCallbackContext::hostTimeNs` when it is available, and otherwise a monotonic clock read at the start of the callback, in the same time base (D-065).
- `ClockTimeMapper` keeps a smoothed linear model of `sampleIndex ↔ hostTime`: a least-squares line through the blocks of the last 2 s (D-076). It absorbs callback jitter and drift between the audio clock and the host clock.
- A MIDI event's sample time is `mapper(t_midi) + latencyOffset`, since audio captured at a moment appears in the stream the input latency later than a MIDI event stamped at that moment (D-078). `latencyOffset` starts as the reported input latency, without one buffer when the device supplies input timestamps, plus an internal calibration constant with no UI. It is measured in step 8.
- With Visona Sync on the sync input (D-109), `AnalysisPipeline` measures the rest of the offset: how many frames after MIDI Clock's nearest bar line Visona Sync's bar impulse arrives, which covers the DAW's output latency, its MIDI Clock delay, the interface and the MIDI path. Every MIDI event is then placed that much later as well. The detector measures against the timeline without its own correction, so a correct offset keeps measuring the same. A new offset clears the musical sweep.

### 3.4 Sweep data model

- **Source.** A group of 1..N channels (D-052).
- **Window.** W ∈ {¼, ½, 1, 2, 4} bars, and phase φ = frac(positionInBars / W).
  - Windows start on multiples of W from bar 1, so a 2-bar window always starts on bar 1, 3, 5 …, and a ¼-bar window starts on every beat.
  - Nothing in the data model assumes W is between ¼ and 4 bars (D-058).
- **Bins.** A fixed B = 131,072 bins per window, independent of screen width (D-054, D-083). The renderer reduces bins to pixel columns.
  - Resizing and zooming therefore never touch the analysis, and tests stay deterministic.
  - At the deepest zoom, 1/32 of the window, that is still about one bin per physical pixel on a Retina display.
- **Cell.** `sweep[source][channel][bin] = {min, max}`, signed float (D-050), plus where the signal enters the bin, for STD (D-091), and the peak level of each band, for DJ (D-092).
  - `full` (broadband) always defines the waveform shape.
  - `low`, `mid` and `high` drive the frequency coloring only (D-056). A peak level per band per bin is enough for it.
- **Pass metadata.** Each bin carries a `passId`, so the renderer can tell the new pass from the previous one ahead of the write head. The column at the head shows only the new pass.
- **Writing.** Each sample maps to bin *b = ⌊φ·B⌋*. When *b* changes, the new bin is reset and stamped with the current `passId`.
  - A bin holds the min and max of the signal drawn as straight lines between consecutive samples. It also reaches the values where the line crosses its edges, and a bin no sample falls in holds the piece of line through it (D-084).
  - Start and window changes clear the buffer.
  - Continue after an SPP relocate increments `passId` without clearing. The sweep recognizes it as a position that does not carry on within one tick of where it froze (D-079).
  - Frames missing because the ring dropped a block leave their bins empty in the current pass (D-067).
- **Free-running (`FREE`).** The position in ticks is (sampleIndex − s₀) / framesPerTick at the free tempo, from the frame s₀ where the free sweep started, and φ is as above (D-090). A new tempo or window starts it over.
- **Freeze (`Stopped`/`ClockLost`).** No bins are written, but the ring is still drained and status keeps updating.
- **Snapshot.** Contains the sweep buffer (whole or dirty range), write head bin, `passId`, transport state, BPM, sample rate, window and overrun counters. Display gain is not part of it.
  - Each triple-buffer slot remembers the sweep state it holds, and publishing copies only the bins that changed since then, so the consumer always reads a whole buffer.
- **`BandSplitter`.** An in-house fourth-order Linkwitz-Riley (LR4) crossover, with starting values around 200 Hz and 2.5 kHz (D-051).
  - The low band gets allpass compensation, so the bands sum flat.
  - Coefficients are computed from the current sample rate.
  - It feeds the coloring only; `full` is computed directly from the input.
  - It runs only in DJ mode (D-092).

### 3.5 Rendering

- **Modes** (D-091). The shape is the full-band signal in every mode (D-056).
  - *PRECISE:* for every pixel column, the waveform colour fills from the centre line out to the signed min and max.
  - *STD:* a thin line through the signal at each column edge, in the waveform colour. `sampleColumnEdges()` interpolates between the starts of neighbouring bins.
  - *DJ:* PRECISE, with each column coloured by its bands (D-092).
- **Frequency coloring** (D-092). A visualization aid that shows which frequencies make up the sound. Each column's band peaks are mixed as red (lows), green (mids) and blue (highs), weighted and squared so that the strongest band sets the hue, at full brightness. `reduceColumnBands()` reads each band later by its filter delay, within the same pass, and `djColour()` mixes the colour.
- **Waveform colour** (D-093). STD and PRECISE use one of eight presets, teal by default, chosen in the settings.
- **Lanes.** Stacked, with L on top (D-057). Each lane is a generic *channel view*, which in the MVP is one channel; Mid, Side or a single lane can become a setting later.
- **Vertical mapping.** *y = center − value × dbToGain(gainDb) × laneHalfHeight* (D-024).
  - At the lane edge the waveform is clipped with a *neutral* marker, so display overshoot is not mistaken for audio clipping.
- **Amplitude references.** A center line, plus faint lines where 0 dBFS and −6 dBFS land after display gain, and −12 dBFS and −18 dBFS from +4 dB and +10 dB of gain (D-088).
- **Write head and passes** (D-068).
  - The head is a thin line in an accent colour outside the waveform colours; there is no green preset.
  - A small erase gap follows the head.
  - The previous pass ahead of the head is drawn at full brightness, like the new one; the head line and gap are enough to read the sweep.
- **Grid.** Neutral gray, not blue.
  - Downbeats and bar lines are strongest and beat lines weaker. A finer note value follows how much is in view: beats only above 2 bars, eighths up to 2, sixteenths up to 1, thirty-seconds up to ½ and sixty-fourths from ¼ bar down (D-087). `gridDivisionFor()` in the core holds the rule.
  - Small bar numbers sit at the bottom edge; a window that starts between bars is labelled bar.beat (D-080). With a bar or less in view, beats are labelled bar.beat too, and the left edge names the beat the view starts in.
  - The bottom right corner names the finest note value and its length at the current tempo, such as `1/16 · 125 ms` (D-087).
- **Zoom** (D-085). Presentation only: the view is a part of the window, from an offset for a span, down to 1/32 of it, and may run past the end of the window into its start.
  - `SweepZoom` in the core holds it and implements zooming around a point and to a selection, and moving the view round past either end of the window (D-089); `ColumnMapping` maps the view's bins to columns, giving up to two column ranges for a range of bins.
  - Past the end of the window, bar numbers read on into the next window.
  - The head line shows only while the head is in view.
- **Color tokens.** One central palette holds band, grid, head, lane background, status and error colors. That keeps themes cheap later, without building a theme UI now.
- **Implementation** (D-054):
  - CPU rasterization into `juce::Image` tiles 64 physical pixels wide via `BitmapData`, at physical pixel resolution (HiDPI) (D-071).
  - Only columns that changed since the last frame are redrawn, and only their tiles are repainted, also across the start of a new pass and in a zoomed view. Resize, gain, zoom and window changes trigger a full redraw.
  - `VBlankAttachment`, capped at 60 fps on average whatever the display's refresh rate. Without a new snapshot, nothing is drawn. OpenGL only if measurements show it is needed.

### 3.6 UI layout

```
┌──────────────────────────────────────────────────────────────┐
│ 126.0 BPM   MIDI RUN   96 kHz   1 BAR   +12 dB               │  status
├──────────────────────────────────────────────────────────────┤
│ [       ▐████▌                                        ]  [×] │  zoom (only while zoomed)
├──────────────────────────────────────────────────────────────┤
│ L  ~~~~~~~~~ sweep ~~~~~~~~~│                                 │
├──────────────────────────────────────────────────────────────┤
│ R  ~~~~~~~~~ sweep ~~~~~~~~~│                                 │
├──────────────────────────────────────────────────────────────┤
│  WINDOW       BPM         GAIN               WAVE            │
│ [1 BAR ▴] [− 126.0 +] [− +12 dB +] [AUTO] [PRECISE ▴]    [⚙] │  controls
└──────────────────────────────────────────────────────────────┘
```

- **Status (top)** (D-046): BPM, MIDI state (`FREE`, `RUN`, `STOPPED`, `MIDI CLOCK LOST`), sample rate, window and gain.
  - The state reads `FREE`, `MIDI RUN`, `STOPPED` or `MIDI CLOCK LOST`, or `NO INPUT` in red when no audio input runs (D-080, D-090).
  - `STOPPED` and `MIDI CLOCK LOST` are buttons that switch to `FREE` (D-090).
  - With a Visona Sync input chosen, `SC +12.3 ms` follows the state once the offset is measured, and `SC ...` before (D-109).
  - Values are calm white or gray text with tabular digits.
  - Color is used for state only, and red is reserved for errors.
  - `MIDI CLOCK LOST` and `NO AUDIO INPUT` appear as a banner over the scope.
  - `STOPPED` shows a freeze indicator and slightly dims the scope.
  - While zoomed, the zoom comes last, such as `ZOOM 4.0× · 1.3–1.4` (D-085).
- **Zoom strip** (D-085): only while zoomed, between the status bar and the scope. It shows the whole window with the part in view and the head, and a × that resets the zoom. Dragging anywhere on it moves the view, round past either end of the window, and a click outside the part in view centres the view there (D-089).
- **Controls (bottom):** no knobs. Each control carries a small caption above its value: WINDOW, BPM, GAIN and WAVE (D-108).
  - WINDOW is a select menu, `¼ BAR` to `4 BARS`, whose list opens above it (D-108).
  - BPM is `[−] 120.0 [+]`, the free tempo from 40 to 300 BPM: the buttons step whole BPM, and drag or scroll fine-tunes it by 0.1. While MIDI Clock sets the tempo it shows that tempo, dimmed (D-090).
  - GAIN is `[−] +12 dB [+]`, from 0 to +18 dB in 1 dB steps. It can be changed by drag, scroll wheel and arrow keys, and double-click or double-tap resets it to 0 dB (D-100).
  - AUTO, right of GAIN, turns auto gain on and off. While it is on, the button is lit, the value is green and the gain moves in 3 dB steps: out as soon as a peak goes past the lane, in at bar lines once the peaks have stayed low for 10 s. A change by hand turns auto gain off (D-100, D-108).
  - WAVE is a select menu of `STD`, `PRECISE` and `DJ`, the drawing modes (D-091, D-108).
  - Secondary buttons: Diagnostics and Full screen, next to ⚙. They and ⚙ show only their icons when their labels do not fit (D-108).
  - Keyboard shortcuts: 1–5 for window, +/− (or ↑/↓) for gain, W for the waveform mode, F for fullscreen, D for diagnostics, and Esc to reset the zoom.
- **Zoom on the scope** (D-085): drag to zoom to the selection, scroll to zoom around the pointer, scroll sideways or Shift-scroll to move the view (D-089), and double-click or double-tap to reset. A pinch zooms around the point between the fingers and moves the view with them, so what was between them stays there (D-108).
- **Measurement ruler** (D-094): drag with the right button, a two-finger click or Control, or hold a finger still for half a second and drag, to draw a rectangle. While it is held, a readout shows its width as ms, samples, frequency, note and musical length, read against the time axis as shown. It never reads the audio, and it goes away on release.
- **Tooltips** (D-108): shown on hover with a mouse. On a touchscreen a finger held still on a control for half a second shows its tooltip, and lifting or moving the finger hides it.
- **Responsive chrome** (D-069, D-108). The layout reflows in steps:
  - The control bar stays on one row. The buttons drop their labels first, and the groups wrap onto a second row only when even that does not fit, which a desktop window reaches below about 710 logical pixels of width.
  - Narrower windows use smaller text and gaps.
  - Very small windows move secondary controls behind ⚙.
  - In kiosk mode the control bar is drawn at up to twice the size, and smaller when that is what keeps it on one row (D-105).
  - The status bar shrinks first, down to BPM and MIDI state.
  - Breakpoints are logical sizes of the component bounds.
- **Diagnostics overlay** (D-070): audio input, overruns, analysis load, frame rate, render time and CPU use, hidden by default.
- **Settings panel (⚙):** audio device, sample rate, buffer size, input channel pair, MIDI input and the waveform colour (D-093). The free tempo is saved too.
  - The input channel for Left and for Right is chosen separately (D-063).
  - **Visona Sync** chooses the input that Visona Sync's bar impulses come in on, or Off (D-109).
  - It is a separate overlay that never forces the scope to repaint.
  - While it is open, the rest of the window is dimmed, and a click or tap outside it closes it like Done, without reaching what is under it (D-108).
  - Its choices open in a list inside the window, like WINDOW and WAVE, so taps reach them on a touchscreen; long lists scroll (D-108).
  - Settings are persisted with JUCE `ApplicationProperties` under `~/Library/Application Support/Visona/`.
  - If the saved audio device is missing, at startup or after it is unplugged, `NO AUDIO INPUT` is shown and no other device is opened. It is opened again when it is plugged in, and so is the MIDI input (D-064, D-102).
- **Window:** freely resizable, with native fullscreen.

### 3.7 Plugin

The Visona plugin is a pass-through stereo effect, built as VST3, AU and CLAP (D-098), with the same sweep, snapshots and UI as the app. Only the transport and the wiring differ (D-096).

- **Audio thread (`processBlock`).** Copies the main input into the ring, as the app's callback does, and queues the host playhead with the stream index of the block's first frame, where hosts report it, in an SPSC queue. It never allocates, locks or waits.
- **Analysis thread.** `HostAnalysisPipeline` hands each playhead to `HostTransport`, which builds the same kind of span timeline as the MIDI Clock transport: musical spans between consecutive playheads, a pending span after the newest one, and frozen spans while the host is stopped. A relocation starts the sweep over.
- **Editor.** `ScopeEditor` shows the same status bar, scope and control bar without the BPM control or full screen, and a settings panel with the waveform colour only. The waveform mode and colour are saved per instance in the DAW project.
- **Sidechain sync** (D-097). The effect has an optional mono sidechain input. Visona Sync, a separate instrument plugin, writes a −6 dBFS impulse at every bar line of the host playhead. `SidechainSyncDetector` measures, on the audio thread, how many frames after the playhead's nearest bar line the impulse arrives. The pipeline then places each playhead that many frames later on the audio timeline, so audio that passed plugins with latency lands on the grid.

## 4. Build and CI

- **CMake ≥ 3.22.** The `VISONA_BUILD_APP` option is ON on macOS and OFF in the Linux job, so JUCE is never fetched there. `VISONA_BUILD_PLUGIN` builds the VST3, AU and CLAP plugins, and fetches [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) for CLAP (D-098); the `macos`, `macos-plugin` and `xcode` presets turn it on.
- **JUCE 9.0.2**, pinned to the exact tag via `FetchContent`. 8.0.15 is the fallback if the skeleton does not build cleanly (D-055).
- **macOS:**
  - `CMAKE_OSX_ARCHITECTURES=arm64` and `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` (D-053).
  - Xcode generator locally, Ninja in CI.
  - Microphone permission (`MICROPHONE_PERMISSION_ENABLED` plus a usage text) is required, or audio input stays silent.
  - Ad-hoc signing is enough for the proof of concept.
- **GitHub Actions (D-043, D-095):**
  - `CI` on every pull request and on `main`: Linux core and tests (GCC, Clang, ASan/UBSan, TSan). JUCE is not fetched in these jobs.
  - `Release` on published GitHub releases: the macOS arm64 app bundle, the Visona and Visona Sync VST3, AU and CLAP bundles (ad-hoc signed, with `auval` and `clap-validator` runs), the Windows x64 and Linux x86_64 VST3 and CLAP plugins (checked with `pluginval` and `clap-validator`, D-099), and the Linux arm64 Pi binary tarball with the kiosk scripts.
  - `Release` run by hand on any branch builds only the artifacts ticked and publishes them to the `dev-builds` prerelease, named after the branch and commit (D-104).
  - macOS app builds locally with the `macos` or `xcode` preset while developing the GUI.
- **Test framework:** Catch2 v3 via `FetchContent`. Its BSL-1.0 license is AGPL-compatible (D-054).
- **Style:** `.clang-format`; warnings are errors in `core/`.
- **Repository layout:**

```
visona/
├── CMakeLists.txt
├── cmake/       JUCE and CLAP setup, warning flags
├── core/        C++20, no JUCE (include/visona/…, src/)
├── app/         JUCE app: main, audio/MIDI engine, settings
├── plugin/      JUCE plugins (VST3, AU, CLAP): Visona and Visona Sync
├── ui/          JUCE components, shared by app and plugin
├── tests/       Catch2, core only
├── scripts/pi/  Pi kiosk scripts
├── docs/        architecture, roadmap, decisions, plugin and Pi guides
├── .github/workflows/  ci.yml, release.yml
├── AGENTS.md
├── CHANGELOG.md
├── README.md
└── LICENSE      AGPLv3
```

## 5. Test strategy

Everything below runs in CI on Linux without hardware (D-032).

- **Signals.** Generators for sine, square, impulse, stereo phase-shifted sine and a click per beat.
- **Transport.**
  - Synthetic MIDI Clock at 120 BPM, 24 PPQN, 4/4 and 96 kHz: 96 ticks = 1 bar = 192,000 samples, and bar boundaries must land within ±1 sample.
  - Cases: perfect clock, jitter (random ±1–2 ms), and a tempo change from 120 to 126 BPM (settling time is logged; the MVP has no hard limit).
  - Also Start, Stop, Continue, clock loss at 0.5 s, and recovery.
  - A tempo ramp from 120 to 140 BPM over 4 bars must leave no holes and no double-written areas in the sweep.
- **SPP.**
  - SPP = 16 followed by Continue gives bar 2, beat 1; SPP = 5 lands mid-beat.
  - The Stop → SPP → Continue sequence that Ableton Live sends.
  - SPP while `Running` is ignored; the maximum value, 16383, is handled.
- **Clock mapping.** Synthetic block timestamps with callback jitter and drift. The mapping error must average under 1 sample and stay within a fixed maximum.
- **Waveform modes.** Each bin's start is its first sample or where the line crosses into it; band levels show which band a tone is in and are empty while band splitting is off; the DJ colour of a single band is its own colour and never depends on the level; the column reductions shift bands within a pass and interpolate edges.
- **Band split.**
  - 50 Hz lands in low, 1 kHz in mid and 8 kHz in high, each by a clear dB margin.
  - At each crossover both bands are at ≈ −6 dB, and the bands sum flat within ±0.1 dB.
  - Group delay per band is measured and documented, from 44.1 to 192 kHz.
- **Shape invariance.** Band splitting does not change the bins' min/max or starts, so the outline is identical in PRECISE and DJ for the same input (D-056).
- **Sweep and rendering.**
  - A 1 kHz sine at 96 kHz, 120 BPM and 1 bar, with B = 4096, produces a deterministic buffer: every bin holds exactly the min and max of the lines through its samples (D-084). A bin is 46.875 samples, about half a cycle, so every three neighbouring bins reach min ≈ −A and max ≈ +A. A sine with at least one cycle per bin, such as 4 kHz, reaches them in every bin.
  - With more bins than frames, the lines between frames fill the bins between them, and at the default B the waveform is one connected line.
  - A click per beat peaks at 0, ¼, ½ and ¾ of the window (±1 bin).
  - Window changes, free-running and freeze on Stop.
  - Bin-to-pixel reduction, also for zoomed views that run past the end of the window, checked by brute force against which columns show which bins.
  - Zooming around a point keeps it in place, stops at 1/32 of the window, and zooming out arrives at the whole window. Moving a view wraps round the window's ends and keeps its span.
  - The grid's finest note value for each amount in view, its length at a tempo, and which amplitude references show at each display gain.
- **Channel-count independence.** The analyzer gives the same per-channel result with 1, 2 and 6 channels (D-049).
- **Concurrency.** Stress tests for the SPSC ring and triple buffer under TSan.
- **Performance** (informational). A benchmark of analysis cost per second of stereo audio at 96 kHz.

## 6. Known risks

- **MIDI-to-audio jitter and offset.** USB MIDI and callback timing add jitter, and the audio and MIDI paths have different latency. *Mitigation:*
  - Interpolate between known ticks and smooth the clock mapping.
  - Measure the offset in step 8.
  - Ableton Live's MIDI Clock Sync Delay can serve as a calibration knob.
  - Measure it with Visona Sync on the sync input (D-109).
  - A sync offset in the UI waits for measured data.
- **JUCE MIDI timestamps on macOS are CoreMIDI packet times, but only to within about 1 ms.** This was checked in step 7 (D-077); JUCE anchors its conversion with a whole millisecond. *Mitigation:* the offset measurement in step 8, and thin CoreMIDI timestamping in `app/` if it matters.
- **Crossover group delay shifts the coloring.** LR4 at 200 Hz delays the low band by ≈ 2.6 ms. That is ≈ 17 px in a ¼-bar window at 174 BPM and 2000 px width. The shape is unaffected because it is full-band. *Mitigation:* the renderer reads each band later by its delay at the band's reference frequency (D-092).
- **Rendering cost.** CPU rasterization at Retina fullscreen and 60 fps may be heavy. *Mitigation:* incremental updates, reduction to physical columns, measurement in step 4, and OpenGL as a fallback.
- **Toolchain versions.** CI runner images lag behind new macOS and Xcode releases, and JUCE 9's CoreAudio implementation is new code. *Mitigation:* pin the JUCE tag, the runner image and the Xcode version, and treat the maintainer's local build as the reference.
- **Microphone permission and Gatekeeper.** Without the permission, input is silent, and an unsigned `.app` from CI is quarantined. *Mitigation:* set the permission in CMake from step 3, and build locally or clear the quarantine on the artifact.
- **Ableton Live clock behavior.** It is unclear whether Live sends clock while stopped, and exactly when SPP arrives relative to Continue. *Mitigation:* the transport handles both cases; verify in step 7.
- **Analysis thread starvation.** *Mitigation:* a ring of about 1 s, overrun counters, and a raised thread priority if needed.
