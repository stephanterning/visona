# Visona roadmap

The plan from MVP 1.0 to the long-term platform. The architecture is described in [architecture.md](architecture.md); decisions are referenced as D-NNN and recorded in [decisions.md](decisions.md).

**Status:**
- The MVP plan is approved (D-054).
- The license (AGPLv3) and `AGENTS.md` are in place.
- PR 1 (skeleton and CI) is merged ([#4](https://github.com/stephanterning/visona/pull/4)).
- PR 2 (core primitives) is merged ([#5](https://github.com/stephanterning/visona/pull/5)).
- PR 3 (audio input and settings) is merged ([#6](https://github.com/stephanterning/visona/pull/6)).
- PR 4 (free-running sweep scope) is merged ([#7](https://github.com/stephanterning/visona/pull/7)).
- MIDI Clock sync, PR 6 and PR 7, comes before frequency coloring, PR 5 (D-072).
- PR 6 (MIDI Clock transport in core) is in review ([#8](https://github.com/stephanterning/visona/pull/8)).

## 1. MVP 1.0

**Goal:** a proof of concept that verifies the concept holds and shows what works (D-040). It is a beat-synced sweep scope with a correct full-band waveform and frequency coloring as an aid, locked to MIDI Clock from Ableton Live, running on an Apple Silicon Mac.

**In scope**

- macOS standalone, Apple Silicon only, deployment target macOS 14 (D-044, D-053)
- One stereo source from the reference audio interface, connected at startup (D-033, D-052)
- MIDI Clock as the only clock source, with Clock, Start, Stop, Continue and SPP (D-036, D-041)
- Sweep display with windows of ¼, ½, 1, 2 and 4 bars (D-020, D-034)
- Full-band signed min/max waveform, with frequency coloring as an aid and a mono/precise mode (D-050, D-056)
- Display gain from 0 to +36 dB (D-024, D-046)
- Status bar, control bar, a settings panel with persistence, resizing and fullscreen (D-046)
- Transport policies per D-045, D-059 and D-060
- CI on every PR: the macOS app, plus core and tests on Linux (D-043)

**Out of scope**

- Manual BPM and `Auto` (audio-based tempo detection) (D-036, D-037)
- Raspberry Pi, the 7" touch UI and the appliance (D-033)
- Hotplug and reconnect (D-033). A device missing at startup only shows `NO AUDIO INPUT`.
- PLL and advanced jitter handling (D-035)
- A sync offset in the UI. There is an internal offset constant.
- Other display modes, multiple sources, and time signatures other than 4/4 in the UI. The model allows them (D-017, D-052).
- Spectrum, phase, loudness, plugins, Windows and Linux apps, Intel Macs, signing and notarization

**Definition of Done: the proof-of-concept scenario**

1. Visona starts on the Mac with the last audio device, input channel pair and MIDI input restored.
2. Before the first Start, incoming stereo shows in a free-running sweep.
3. On Play in Ableton Live, the sweep restarts at bar 1 and the bars stand still on screen.
4. Kicks on the beat land on the grid lines at 120, 126 and 174 BPM.
5. The window (¼–4 bars) and gain (0–36 dB) can be changed live.
6. Stop freezes the view and shows `STOPPED`. Moving the playhead to bar 17 and pressing Continue lands correctly (SPP).
7. Pulling the MIDI cable shows `MIDI CLOCK LOST` within about 0.5 s.
8. A long studio session runs without dropouts or ring overruns.
9. Measurements and lessons learned are documented (D-040).

## 2. PR steps

Each step is a draft PR. The maintainer tests it on an Apple Silicon Mac with the reference hardware, then merges it (D-043). The `docs/` PR that adds this roadmap comes first (D-048). The steps keep their numbers, but PR 6 and PR 7 are done before PR 5 (D-072).

**PR 1 – Skeleton and CI** (milestone 0)
- Content: CMake, JUCE, an empty `core/` library, an app that opens an empty Visona window, Catch2 with one test, CI for both jobs, and `.clang-format`.
- Acceptance: CI is green on macOS and Linux, and the Linux job does not fetch JUCE.
- Hardware check: builds in Xcode, the app starts, and the window resizes and goes fullscreen.

**PR 2 – Core primitives**
- Content: SPSC ring, triple buffer, source layout, `BlockTiming` and `MidiClockEvent`. No UI.
- Acceptance: unit tests and TSan stress tests are green.
- Hardware check: none (review only).

**PR 3 – Audio input and settings** (milestone 1)
- Content:
  - Audio device management and a settings panel for device and input pair, with persistence.
  - The audio callback feeding the ring.
  - A debug readout: sample rate, block size, peak per channel and overruns.
  - Microphone permission, and `NO AUDIO INPUT` when the device is missing.
- Acceptance: CI is green, and the callback does not allocate (checked by review and debug instrumentation).
- Hardware check: S/PDIF input shows correct L/R levels (pan test), 96 kHz is shown, the choices survive a restart, and there are no overruns over 30 minutes.

**PR 4 – Free-running sweep scope** (milestone 2)
- Content:
  - The analysis thread, and `SweepAnalyzer` with the `full` band only, in a fixed time window.
  - Snapshots via the triple buffer.
  - `ScopeView` with stacked L/R lanes, the write head with the dimmed previous pass, amplitude references and a neutral clip marker.
  - The central color palette.
  - Gain, status bar, and a control bar with stepped responsive chrome.
- Acceptance: deterministic sweep tests are green, and rendering holds 60 fps without overruns, including with the settings panel open.
- Hardware check: the waveform looks right with real music; the write head and passes are readable; gain, resize and fullscreen work; CPU load is noted.

**PR 5 – Frequency coloring** (D-056)
- Content:
  - `BandSplitter` (LR4) with tests, and per-band data.
  - Both coloring methods: a blended color per column, and bands inside the full-band outline.
  - A toggle between them and mono/precise mode.
- Acceptance:
  - Band split tests are green, and group delay is documented.
  - The outline is identical in every coloring mode and in mono/precise mode.
- Hardware check, with real music at both ¼ and 4 bars:
  - Pick the method that keeps the waveform most correct and readable.
  - Judge whether the colors reveal the frequency content, and whether the crossovers or starting palette need tuning.
  - Check that the head and grid colors do not clash with the coloring.
  - The chosen method is recorded as a decision.

**PR 6 – MIDI Clock transport in core**
- Content: state machine, tick counting, SPP, BPM estimate, clock-loss timeout and `ClockTimeMapper`. No UI.
- Acceptance: every transport, SPP and mapping test in the [test strategy](architecture.md#5-test-strategy) is green on Linux.
- Hardware check: none (review only).

**PR 7 – Beat-synced sweep** (milestone 4, plus window selection)
- Content:
  - MIDI input in settings (persisted), the MIDI ring, and the transport wired into the analysis.
  - φ from musical position, and WINDOW ¼–4.
  - The grid hierarchy with bar numbers.
  - `WAITING`, `STOPPED` with a freeze indicator, the `MIDI CLOCK LOST` banner, and the full status bar.
  - Verify what JUCE's MIDI timestamp is on macOS.
- Acceptance: a synthetic end-to-end test (a click per beat plus synthetic clock) lands on the grid within ±1 bin.
- Hardware check in Ableton Live with MIDI Clock Type = Song:
  - At 120, 126 and 174 BPM, kicks land on the grid.
  - Start puts bar 1 at the left, and Stop freezes.
  - Relocating to bar 17 and pressing Continue lands correctly.
  - Also a tempo change, window switching, and a pulled MIDI cable showing `MIDI CLOCK LOST`.

**PR 8 – Proof-of-concept hardening and measurements**
- Content:
  - A file log: device, sample rate, block size, BPM, tick jitter statistics and overruns.
  - A measurement of the visual offset between an audio click and the grid.
  - Performance tuning.
  - Notes on lessons learned (D-040).
- Acceptance: a session of at least 2 hours without dropouts or overruns, with offset and CPU load documented.
- Hardware check: run Visona through a real studio session and report what works and what does not.

## 3. v1: the first complete product

v1 is the Raspberry Pi appliance: power on, the app starts fullscreen on a 7" touchscreen, finds the audio interface, and is fully usable by touch. MVP 1.0 covers part of it:

| v1 criterion | MVP 1.0 | Milestone |
|---|---|---|
| Pi boots straight into the app, fullscreen on 7" | After MVP | 6 |
| Audio interface found automatically | Partly: connected at startup, last choice restored, no hotplug | 1 (6) |
| Stereo S/PDIF shown as a waveform | Yes | 1, 2 |
| Choose MANUAL or MIDI clock | MIDI only (D-036) | 3, 4 |
| Set BPM manually | After MVP | 3 |
| Follows incoming MIDI Clock | Yes | 4 |
| Choose ¼, ½, 1, 2, 4 bars | Yes | 3 (window part), 4 |
| Stable sync to the chosen period | Yes, with simple sync; Start and SPP + Continue give the correct alignment | 4 |
| Vertical (display) gain | Yes | 2 |
| Everything usable by touch, UI fills the screen | After MVP (fullscreen on Mac is included) | 5, 6 |
| Runs on a Mac in a freely resizable window | Yes | 0, 2 |
| Stable audio through long sessions | Yes on Mac; Pi later | all |

Stricter sync acceptance criteria are defined after the MVP as a dedicated focus area (D-035).

## 4. Milestones 0–12

| # | Milestone | Goal | Phase |
|---|---|---|---|
| 0 | Project skeleton | A minimal app (CMake, JUCE, core library, app target, basic tests) builds on macOS, with core and tests on Linux | MVP 1.0 |
| 1 | Audio input | Prove that correct stereo audio arrives continuously | MVP 1.0 |
| 2 | Free-running scope | A useful plain oscilloscope, isolated from transport problems | MVP 1.0 |
| 3 | Manual musical sync | Show exactly N bars at a given BPM | Partly: window selection in MVP, manual BPM after |
| 4 | MIDI Clock | Lock the scope to external MIDI Clock | MVP 1.0 |
| 5 | Touch UI | Use the product from a 7" touchscreen | After MVP |
| 6 | Raspberry Pi appliance | Mount the Pi permanently and use it as an instrument | After MVP |
| 7 | Auto BPM | Work musically without MIDI Clock (audio-based tempo detection) | After MVP |
| 8 | Modular dashboard | The scope becomes one module in a general monitoring platform | After MVP |
| 9 | Spectrum | Scope and spectrum side by side, with an adaptive layout | After MVP |
| 10 | Stereo/phase | Correlation, vectorscope and stereo balance | After MVP |
| 11 | Loudness | LUFS M/S/I, True Peak and LRA; the Pi starts replacing a hardware meter | After MVP |
| 12 | Plugin | VST3/AU with host transport, inside the DAW | After MVP |

Milestone 7 is a separate DSP track and must not delay earlier milestones.

## 5. After MVP 1.0

**Moved out of the MVP** (order still open):

- Manual BPM, the rest of milestone 3 (D-036)
- Sync precision as a dedicated focus area: acceptance criteria, jitter handling, possibly a PLL, and a sync offset (D-035)
- Raspberry Pi: a hardware spike, the touch UI (milestone 5) and the appliance with hotplug and reconnect (milestone 6) (D-033)

**Order after v1:**

```
v1 beat-synced scope
  → Auto BPM
  → Modular widget/layout engine
  → Spectrum
  → Phase / correlation
  → Peak / RMS / True Peak
  → LUFS / loudness
  → Dedicated hardware meter replacement
  → VST3 / AU
```

**The order follows real usage.** If the scope turns out to be extremely useful, improving the scope beats mechanically moving on to the next module.

## 6. Later, unscheduled

The architecture should allow these, but they are not planned yet:

- More inputs than stereo: several sources, each with 1..N channels (D-049, D-052)
- Display modes other than sweep, such as scroll, and channel views such as Mid, Side, or overlaid L/R (D-034, D-057)
- Windows shorter than ¼ bar or longer than 4 bars, such as ⅛ bar and 8 bars (D-058)
- Time signatures other than 4/4 (D-017)
- Auto vertical gain
- Presets and layouts
- Auto-hide or overlay controls and a settings drawer
- A diagnostic mode on the Pi
- A calibrated sync offset, after testing with real hardware
- Windows packaging, skins and themes, networking, remote control
