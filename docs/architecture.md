# Visona architecture

This document describes what Visona is, the principles it is built on, and the architecture of MVP 1.0. Milestones and PR steps are in [roadmap.md](roadmap.md). Decisions are referenced as D-NNN and recorded in [decisions.md](decisions.md).

## 1. Vision

Visona is a platform-independent real-time engine for musical audio analysis and visualization.

- **The scope is the first product.** It is a beat-synced stereo oscilloscope. It shows ¼, ½, 1, 2 or 4 bars in a sweep display, locked to musical time instead of milliseconds (D-020, D-034).
- **The monitoring platform is the architecture.** The scope is the first module. Spectrum, phase/vectorscope, correlation, levels, True Peak and loudness are meant to follow as widgets (D-028).
- **The long-term goal is a dedicated studio meter.** A Raspberry Pi with a small touchscreen should eventually be able to replace a hardware loudness and stereo meter.
- **Targets:**
  - MVP 1.0 is a macOS standalone app on Apple Silicon (D-033, D-044).
  - A Raspberry Pi appliance, Windows and Linux standalone apps, and VST3/AU plugins come later, all on the same analysis engine and UI.
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
  - `AnalysisPipeline`, the analysis thread's work without the thread: ring in, snapshots out

  Keeping JUCE out makes the core testable on Linux without a GUI, enforces the platform boundary, and lets the core be reused in plugins and on the Raspberry Pi.
- **`app/` (JUCE):** audio device management, audio callback, MIDI input, the analysis thread, settings and wiring.
- **`ui/` (JUCE):** `ScopeView`, `StatusBar`, `ControlBar`, `SettingsPanel` and `DiagnosticsOverlay`.

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

MIDI Clock is the only clock source in MVP 1.0 (D-036). The messages are:
- Clock `F8` (24 PPQN)
- Start `FA`, Continue `FB`, Stop `FC`
- SPP `F2`: a 14-bit value in MIDI beats, where one MIDI beat is a sixteenth note, or 6 ticks.

Position is counted in ticks, with `ticksPerBar = 24 × numerator × 4 / denominator`. That is 96 in 4/4, which is the MVP default (D-017).

| Event | Effect (D-045, D-059, D-060) |
|---|---|
| Startup | `Waiting`: free-running sweep with a fixed 2 s window |
| Start | Position = 0, `Running`. The first Clock after Start is the downbeat of bar 1. The sweep clears and restarts at x = 0 |
| Clock in `Running` | Position += 1 tick; the tick's sample time is recorded |
| Clock in `Stopped`/`Waiting` | Updates the BPM estimate only (some DAWs send clock while stopped) |
| Stop | `Stopped`. Position is kept; the UI freezes the last frame and shows `STOPPED` |
| SPP | Position = SPP × 6 ticks. Accepted in `Stopped`; ignored and logged in `Running` |
| Continue | `Running` from the current position. The write head jumps to that phase, and existing content becomes the previous pass |
| > 0.5 s without Clock in `Running` | `ClockLost`: freeze and show `MIDI CLOCK LOST` |
| Clock returns in `ClockLost` | `Running` again, continuing the tick count. Position may be off until the next Start, or SPP + Continue |

The clock-loss timeout only applies in `Running`, so a DAW that stops sending clock on Stop does not trigger a false `MIDI CLOCK LOST`.

**Position per audio sample.** Between tick *k* at sample *s_k* and tick *k+1* at *s_{k+1}*, position = *k + (s − s_k) / (s_{k+1} − s_k)*.
- Audio is analyzed only up to the latest known tick. This costs about one tick of visual latency (≈ 21 ms at 120 BPM).
- On Stop or clock loss, the last interval is extrapolated by at most one tick and then held.

**BPM.** `BPM = 60 × sampleRate / (24 × tick interval in samples)`.
- The estimate is a moving average over the last 24 intervals (one beat), reset on Start, and displayed with 0.1 BPM resolution.
- There is no PLL in the MVP (D-035).

**MIDI time to audio sample time**

- MIDI events carry a host time from JUCE's timestamp. Whether that is the CoreMIDI packet timestamp or the arrival time is verified in PR 7.
- Audio blocks carry a host time from `AudioIODeviceCallbackContext::hostTimeNs` when it is available, and otherwise a monotonic clock read at the start of the callback, in the same time base (D-065).
- `ClockTimeMapper` keeps a smoothed linear model of `sampleIndex ↔ hostTime` over the last N blocks. It absorbs callback jitter and drift between the audio clock and the host clock.
- A MIDI event's sample time is `mapper(t_midi) − latencyOffset`. `latencyOffset` starts as the reported input latency plus an internal calibration constant with no UI. It is measured in PR 8.

### 3.4 Sweep data model

- **Source.** A group of 1..N channels (D-052).
- **Window.** W ∈ {¼, ½, 1, 2, 4} bars, and phase φ = frac(positionInBars / W).
  - Windows start on multiples of W from bar 1, so a 2-bar window always starts on bar 1, 3, 5 …, and a ¼-bar window starts on every beat.
  - Nothing in the data model assumes W is between ¼ and 4 bars (D-058).
- **Bins.** A fixed B = 4096 bins per window, independent of screen width (D-054). The renderer reduces bins to pixel columns.
  - Resizing therefore never touches the analysis, and tests stay deterministic.
- **Cell.** `sweep[source][channel][band][bin] = {min, max}`, signed float (D-050).
  - `full` (broadband) always defines the waveform shape.
  - `low`, `mid` and `high` drive the frequency coloring only (D-056). PR 5 decides whether per-band min/max is enough, or whether a per-bin energy value is also needed.
- **Pass metadata.** Each bin carries a `passId`, so the renderer can tell the new pass from the previous one ahead of the write head. The column at the head shows only the new pass.
- **Writing.** Each sample maps to bin *b = ⌊φ·B⌋*. When *b* changes, the new bin is reset and stamped with the current `passId`.
  - Start and window changes clear the buffer.
  - Continue after an SPP relocate increments `passId` without clearing.
  - Frames missing because the ring dropped a block leave their bins empty in the current pass (D-067).
- **Free-running (`Waiting`).** φ = frac(sampleIndex / (2 s × sampleRate)) (D-060).
- **Freeze (`Stopped`/`ClockLost`).** No bins are written, but the ring is still drained and status keeps updating.
- **Snapshot.** Contains the sweep buffer (whole or dirty range), write head bin, `passId`, transport state, BPM, sample rate, window and overrun counters. Display gain is not part of it.
  - Each triple-buffer slot remembers the sweep state it holds, and publishing copies only the bins that changed since then, so the consumer always reads a whole buffer.
- **`BandSplitter`.** An in-house fourth-order Linkwitz-Riley (LR4) crossover, with starting values around 200 Hz and 2.5 kHz (D-051).
  - The low band gets allpass compensation, so the bands sum flat.
  - Coefficients are computed from the current sample rate.
  - It feeds the coloring only; `full` is computed directly from the input.

### 3.5 Rendering

- **Shape.** For every pixel column, the span between the full-band min and max is filled. That area is the waveform in every mode (D-056).
- **Frequency coloring.** A visualization aid that shows which frequencies make up the sound. The starting palette is blue lows, orange mids and white highs (D-051). PR 5 picks one of two methods, based on which keeps the waveform correct and readable at ¼ and 4 bars:
  - *Blended color per column:* the full-band span is filled with one color, mixed from each band's share of that column.
  - *Bands inside the full-band outline:* the band envelopes are drawn clipped to the full-band outline and never extend beyond it.
- **Mono/precise mode.** Draws `full` in a neutral color only. It stays available as a mode and as a reference that coloring does not change the shape.
- **Lanes.** Stacked, with L on top (D-057). Each lane is a generic *channel view*, which in the MVP is one channel; Mid, Side or a single lane can become a setting later.
- **Vertical mapping.** *y = center − value × dbToGain(gainDb) × laneHalfHeight* (D-024).
  - At the lane edge the waveform is clipped with a *neutral* marker, so display overshoot is not mistaken for audio clipping.
- **Amplitude references.** A center line, plus faint lines where 0 dBFS and −6 dBFS land after display gain.
- **Write head and passes** (D-068).
  - The head is a thin line in an accent color outside the band palette. It is never white, blue, orange or red.
  - A small erase gap follows the head.
  - The previous pass ahead of the head is drawn at full brightness, like the new one; the head line and gap are enough to read the sweep.
- **Grid.** Neutral gray, not blue.
  - Downbeats and bar lines are strongest and beat lines weaker. Sixteenths show only at ¼ and ½ bar.
  - Small bar numbers sit at the lane edge.
- **Color tokens.** One central palette holds band, grid, head, lane background, status and error colors. That keeps themes cheap later, without building a theme UI now.
- **Implementation** (D-054):
  - CPU rasterization into `juce::Image` tiles 64 physical pixels wide via `BitmapData`, at physical pixel resolution (HiDPI) (D-071).
  - Only columns that changed since the last frame are redrawn, and only their tiles are repainted, also across the start of a new pass. Resize, gain and window changes trigger a full redraw.
  - `VBlankAttachment`, capped at 60 fps on average whatever the display's refresh rate. Without a new snapshot, nothing is drawn. OpenGL only if measurements show it is needed.

### 3.6 UI layout

```
┌──────────────────────────────────────────────────────────────┐
│ 126.0 BPM   MIDI RUN   96 kHz   1 BAR   +12 dB               │  status
├──────────────────────────────────────────────────────────────┤
│ L  ~~~~~~~~~ sweep ~~~~~~~~~│                                 │
├──────────────────────────────────────────────────────────────┤
│ R  ~~~~~~~~~ sweep ~~~~~~~~~│                                 │
├──────────────────────────────────────────────────────────────┤
│ WINDOW [¼][½][1][2][4]        GAIN [−] +12 dB [+]        [⚙] │  controls
└──────────────────────────────────────────────────────────────┘
```

- **Status (top)** (D-046): BPM, MIDI state (`WAITING`, `RUN`, `STOPPED`, `MIDI CLOCK LOST`), sample rate, window and gain.
  - Until MIDI Clock is wired in (PR 7), the state reads `FREE RUN`, or `NO INPUT` in red when no audio input runs.
  - Values are calm white or gray text with tabular digits.
  - Color is used for state only, and red is reserved for errors.
  - `MIDI CLOCK LOST` and `NO AUDIO INPUT` appear as a banner over the scope.
  - `STOPPED` shows a freeze indicator and slightly dims the scope.
- **Controls (bottom):** no knobs.
  - WINDOW is an always-visible segmented control.
  - GAIN is `[−] +12 dB [+]`, from 0 to +36 dB in 1 dB steps. It can be changed by drag, scroll wheel and arrow keys, and double-click or double-tap resets it to 0 dB.
  - Secondary buttons: Diagnostics and Full screen, next to ⚙.
  - Keyboard shortcuts: 1–5 for window, +/− (or ↑/↓) for gain, F for fullscreen, D for diagnostics.
- **Responsive chrome** (D-069). The layout reflows in steps:
  - Wide windows put everything on one row.
  - Narrow windows use two rows with abbreviated labels.
  - Very small windows move secondary controls behind ⚙.
  - The status bar shrinks first, down to BPM and MIDI state.
  - Breakpoints are logical sizes of the component bounds.
- **Diagnostics overlay** (D-070): audio input, overruns, analysis load, frame rate, render time and CPU use, hidden by default.
- **Settings panel (⚙):** audio device, sample rate, buffer size, input channel pair and MIDI input.
  - The input channel for Left and for Right is chosen separately (D-063).
  - It is a separate overlay that never forces the scope to repaint.
  - Settings are persisted with JUCE `ApplicationProperties` under `~/Library/Application Support/Visona/`.
  - If the saved audio device is missing at startup, `NO AUDIO INPUT` is shown and no other device is opened (D-064).
- **Window:** freely resizable, with native fullscreen.

## 4. Build and CI

- **CMake ≥ 3.22.** The `VISONA_BUILD_APP` option is ON on macOS and OFF in the Linux job, so JUCE is never fetched there.
- **JUCE 9.0.2**, pinned to the exact tag via `FetchContent`. 8.0.15 is the fallback if the skeleton does not build cleanly (D-055).
- **macOS:**
  - `CMAKE_OSX_ARCHITECTURES=arm64` and `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` (D-053).
  - Xcode generator locally, Ninja in CI.
  - Microphone permission (`MICROPHONE_PERMISSION_ENABLED` plus a usage text) is required, or audio input stays silent.
  - Ad-hoc signing is enough for the proof of concept.
- **GitHub Actions on every PR (D-043):**
  - `macos` (arm64 runner, pinned image and Xcode version): builds the app, runs the core tests and uploads the `.app` as an artifact.
  - `linux` (Ubuntu, GCC and Clang): builds the core and tests, plus a sanitizer job (ASan/UBSan, and TSan for the ring and triple buffer).
- **Test framework:** Catch2 v3 via `FetchContent`. Its BSL-1.0 license is AGPL-compatible (D-054).
- **Style:** `.clang-format`; warnings are errors in `core/`.
- **Repository layout:**

```
visona/
├── CMakeLists.txt
├── core/        C++20, no JUCE (include/visona/…, src/)
├── app/         JUCE app: main, audio/MIDI engine, settings
├── ui/          JUCE components
├── tests/       Catch2, core only
├── docs/        architecture, roadmap, decisions
├── .github/workflows/ci.yml
├── AGENTS.md
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
- **Band split.**
  - 50 Hz lands in low, 1 kHz in mid and 8 kHz in high, each by a clear dB margin.
  - At each crossover both bands are at ≈ −6 dB, and the bands sum flat within ±0.1 dB.
  - Group delay per band is measured and documented, from 44.1 to 192 kHz.
- **Shape invariance.** The rendered outline is identical in every coloring mode and in mono/precise mode for the same input (D-056).
- **Sweep and rendering.**
  - A 1 kHz sine at 96 kHz, 120 BPM and 1 bar produces a deterministic buffer: every bin holds exactly the min and max of its samples. A bin is 46.875 samples, about half a cycle, so every three neighbouring bins reach min ≈ −A and max ≈ +A. A sine with at least one cycle per bin, such as 4 kHz, reaches them in every bin.
  - A click per beat peaks at 0, ¼, ½ and ¾ of the window (±1 bin).
  - Window changes, free-running and freeze on Stop.
  - Bin-to-pixel reduction.
- **Channel-count independence.** The analyzer gives the same per-channel result with 1, 2 and 6 channels (D-049).
- **Concurrency.** Stress tests for the SPSC ring and triple buffer under TSan.
- **Performance** (informational). A benchmark of analysis cost per second of stereo audio at 96 kHz.

## 6. Known risks

- **MIDI-to-audio jitter and offset.** USB MIDI and callback timing add jitter, and the audio and MIDI paths have different latency. *Mitigation:*
  - Interpolate between known ticks and smooth the clock mapping.
  - Measure the offset in PR 8.
  - Ableton Live's MIDI Clock Sync Delay can serve as a calibration knob.
  - A sync offset in the UI waits for measured data.
- **JUCE MIDI timestamp semantics on macOS are unverified.** Arrival-time stamps would add jitter. *Mitigation:* verify in PR 7, and add thin CoreMIDI timestamping in `app/` if needed.
- **Crossover group delay shifts the coloring.** LR4 at 200 Hz delays the low band by ≈ 2 ms. That is ≈ 13 px in a ¼-bar window at 174 BPM and 2000 px width. The shape is unaffected because it is full-band. *Mitigation:* measure in PR 5 and compensate with a constant delay in the band data if needed.
- **Rendering cost.** CPU rasterization at Retina fullscreen and 60 fps may be heavy. *Mitigation:* incremental updates, reduction to physical columns, measurement in PR 4, and OpenGL as a fallback.
- **Toolchain versions.** CI runner images lag behind new macOS and Xcode releases, and JUCE 9's CoreAudio implementation is new code. *Mitigation:* pin the JUCE tag, the runner image and the Xcode version, and treat the maintainer's local build as the reference.
- **Microphone permission and Gatekeeper.** Without the permission, input is silent, and an unsigned `.app` from CI is quarantined. *Mitigation:* set the permission in CMake from PR 3, and build locally or clear the quarantine on the artifact.
- **Ableton Live clock behavior.** It is unclear whether Live sends clock while stopped, and exactly when SPP arrives relative to Continue. *Mitigation:* the transport handles both cases; verify in PR 7.
- **Analysis thread starvation.** *Mitigation:* a ring of about 1 s, overrun counters, and a raised thread priority if needed.
