# Visona roadmap

The plan from MVP 1.0 to the long-term platform. The architecture is described in [architecture.md](architecture.md); decisions are referenced as D-NNN and recorded in [decisions.md](decisions.md).

**Status:**
- **MVP 1.0 proof of concept is achieved** (alpha). The maintainer verified the scenario on Apple Silicon Mac and on Raspberry Pi hardware with an RME Babyface Pro FS.
- Steps 1–7b and step 5 (waveform modes) are merged. Step 8 (formal logging and written measurements) is deferred polish, not a blocker for alpha.
- Raspberry Pi port and kiosk autostart landed early as a spike ([#20](https://github.com/stephanterning/visona/pull/20), [#21](https://github.com/stephanterning/visona/pull/21)); full milestone 6 (appliance hardening) remains after plugins.
- **Milestone 12 has started:** the VST3 plugin with host transport ([#24](https://github.com/stephanterning/visona/pull/24)) and Visona Sync for sidechain latency measurement in Ableton Live ([#25](https://github.com/stephanterning/visona/pull/25)) are merged, for 0.1.0-alpha.2 (D-096, D-097). AU and CLAP builds of both plugins follow (D-098), then UX polish and Pi appliance work (milestones 5–6).
- CI: Linux core tests on every pull request; the macOS app, the plugins and the Pi app are built on release or manual workflow trigger only (private repo Actions budget, D-095).

## 1. MVP 1.0

**Goal:** a proof of concept that verifies the concept holds and shows what works (D-040). It is a beat-synced sweep scope with a correct full-band waveform and frequency coloring as an aid, locked to MIDI Clock from Ableton Live, running on an Apple Silicon Mac.

**In scope**

- macOS standalone, Apple Silicon only, deployment target macOS 14 (D-044, D-053)
- One stereo source from the reference audio interface, connected at startup (D-033, D-052)
- MIDI Clock as the only external clock source, with Clock, Start, Stop, Continue and SPP (D-036, D-041)
- `FREE`: without MIDI Clock, or after Stop, the sweep runs free at a tempo set by hand, 40–300 BPM (D-090)
- Sweep display with windows of ¼, ½, 1, 2 and 4 bars (D-020, D-034)
- Horizontal zoom in on part of the window, down to 1/32 of it, without changing the window (D-082, D-085)
- Full-band waveform in three modes, STD, PRECISE and DJ, with DJ's frequency coloring as an aid and a chosen colour for the others (D-050, D-056, D-091–D-093)
- Display gain from 0 to +18 dB, by hand or with optional auto gain (D-024, D-046, D-098)
- Status bar, control bar, a settings panel with persistence, resizing and fullscreen (D-046)
- Transport policies per D-045, D-059 and D-090
- CI on every PR: core and tests on Linux (D-043, D-095). macOS and Pi app builds on release or manual trigger.

**Out of scope**

- `Auto` (audio-based tempo detection) (D-036, D-037). Manual BPM is in scope as the tempo of `FREE` (D-090).
- Raspberry Pi, the 7" touch UI and the appliance (D-033)
- Hotplug and reconnect (D-033). A device missing at startup only shows `NO AUDIO INPUT`.
- PLL and advanced jitter handling (D-035)
- A sync offset in the UI. There is an internal offset constant.
- Other display modes, multiple sources, and time signatures other than 4/4 in the UI. The model allows them (D-017, D-052).
- Spectrum, phase, loudness, plugins, Windows and Linux apps, Intel Macs, signing and notarization

**Definition of Done: the proof-of-concept scenario**

1. Visona starts on the Mac with the last audio device, input channel pair and MIDI input restored.
2. Before the first Start, incoming stereo shows in a free-running sweep at the saved free tempo.
3. On Play in Ableton Live, the sweep restarts at bar 1 and the bars stand still on screen.
4. Kicks on the beat land on the grid lines at 120, 126 and 174 BPM.
5. The window (¼–4 bars), gain (0–18 dB) and zoom can be changed live.
6. Stop freezes the view and shows `STOPPED`. Moving the playhead to bar 17 and pressing Continue lands correctly (SPP). Clicking `STOPPED` runs the sweep free at the last tempo.
7. Pulling the MIDI cable shows `MIDI CLOCK LOST` within about 0.5 s.
8. A long studio session runs without dropouts or ring overruns.
9. Measurements and lessons learned are documented (D-040).

## 2. Steps

Each step is a draft pull request, which has its own number on GitHub, such as #12 for step 7b (D-086). The maintainer tests it on an Apple Silicon Mac with the reference hardware, then merges it (D-043). The `docs/` PR that adds this roadmap comes first (D-048). The steps keep their numbers, but steps 6 and 7 are done before step 5 (D-072), and step 7b, added later, follows step 7 (D-082).

**Step 1 – Skeleton and CI** (milestone 0)
- Content: CMake, JUCE, an empty `core/` library, an app that opens an empty Visona window, Catch2 with one test, CI for both jobs, and `.clang-format`.
- Acceptance: CI is green on macOS and Linux, and the Linux job does not fetch JUCE.
- Hardware check: builds in Xcode, the app starts, and the window resizes and goes fullscreen.

**Step 2 – Core primitives**
- Content: SPSC ring, triple buffer, source layout, `BlockTiming` and `MidiClockEvent`. No UI.
- Acceptance: unit tests and TSan stress tests are green.
- Hardware check: none (review only).

**Step 3 – Audio input and settings** (milestone 1)
- Content:
  - Audio device management and a settings panel for device and input pair, with persistence.
  - The audio callback feeding the ring.
  - A debug readout: sample rate, block size, peak per channel and overruns.
  - Microphone permission, and `NO AUDIO INPUT` when the device is missing.
- Acceptance: CI is green, and the callback does not allocate (checked by review and debug instrumentation).
- Hardware check: S/PDIF input shows correct L/R levels (pan test), 96 kHz is shown, the choices survive a restart, and there are no overruns over 30 minutes.

**Step 4 – Free-running sweep scope** (milestone 2)
- Content:
  - The analysis thread, and `SweepAnalyzer` with the `full` band only, in a fixed time window.
  - Snapshots via the triple buffer.
  - `ScopeView` with stacked L/R lanes, the write head with the dimmed previous pass, amplitude references and a neutral clip marker.
  - The central color palette.
  - Gain, status bar, and a control bar with stepped responsive chrome.
- Acceptance: deterministic sweep tests are green, and rendering holds 60 fps without overruns, including with the settings panel open.
- Hardware check: the waveform looks right with real music; the write head and passes are readable; gain, resize and fullscreen work; CPU load is noted.

**Step 5 – Waveform modes and frequency coloring** (D-056, D-091–D-093)
- Content:
  - `BandSplitter` (LR4) with tests, and per-band peak levels per bin, split only in DJ mode.
  - Three drawing modes modelled on Oszillos Mega Scope: STD (a thin line), PRECISE (filled min/max) and DJ (PRECISE coloured by frequency, bands as red, green and blue), with WAVE in the control bar.
  - Eight waveform colours for STD and PRECISE in the settings. The mode and colour are saved.
- Acceptance:
  - Band split tests are green, and group delay is documented and compensated.
  - The outline is identical in PRECISE and DJ.
- Hardware check, with real music at both ¼ and 4 bars:
  - DJ: kicks read red to orange, mids green and hi-hats blue, and the colours line up with the transients. Tune the band weights if one band dominates.
  - STD looks like a thin line and costs less to render than PRECISE; note the render time of each mode in diagnostics.
  - Check that the head and grid colors do not clash with the coloring or the chosen colour.

**Step 6 – MIDI Clock transport in core**
- Content: state machine, tick counting, SPP, BPM estimate, clock-loss timeout and `ClockTimeMapper`. No UI.
- Acceptance: every transport, SPP and mapping test in the [test strategy](architecture.md#5-test-strategy) is green on Linux.
- Hardware check: none (review only).

**Step 7 – Beat-synced sweep** (milestone 4, plus window selection)
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

**Step 7b – Horizontal zoom** (D-082)
- Content:
  - B = 131,072 bins, and bins that hold the lines between samples, so the waveform stays one connected line at deep zoom (D-083, D-084).
  - Zoom in `ScopeView` down to 1/32 of the window: drag to select, scroll wheel, trackpad and touch pinch, and reset with Esc, a double-click or a new window (D-085).
  - A view that may run past the end of the window, a finer grid and beat labels when zoomed, the overview strip and the zoom in the status bar.
- Acceptance: zoom tests are green, including brute-force tests of the zoomed bin-to-column mapping, and rendering still holds 60 fps.
- Hardware check with real music, in Ableton Live and before the first Start:
  - Zoom in on the third beat of a 1-bar window by dragging, and further with the wheel and a trackpad pinch, down to 32×.
  - Zoom in around the downbeat and see both sides of it.
  - Esc, a double-click, the × and choosing a window reset the zoom; Stop and Continue keep it.
  - The waveform looks connected and sharp on the Retina display at every zoom, and CPU load is noted.

**Step 8 – Proof-of-concept hardening and measurements**
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
| Choose MANUAL or MIDI clock | Partly: `FREE` runs at a manual tempo until MIDI Clock starts (D-090) | 3, 4 |
| Set BPM manually | Yes, for `FREE` (D-090) | 3 |
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
| 3 | Manual musical sync | Show exactly N bars at a given BPM | MVP 1.0: window selection, and manual BPM for `FREE` (D-090) |
| 4 | MIDI Clock | Lock the scope to external MIDI Clock | MVP 1.0 |
| 5 | Touch UI | Use the product from a 7" touchscreen | After MVP |
| 6 | Raspberry Pi appliance | Mount the Pi permanently and use it as an instrument | After MVP |
| 7 | Auto BPM | Work musically without MIDI Clock (audio-based tempo detection) | After MVP |
| 8 | Modular dashboard | The scope becomes one module in a general monitoring platform | After MVP |
| 9 | Spectrum | Scope and spectrum side by side, with an adaptive layout | After MVP |
| 10 | Stereo/phase | Correlation, vectorscope and stereo balance | After MVP |
| 11 | Loudness | LUFS M/S/I, True Peak and LRA; the Pi starts replacing a hardware meter | After MVP |
| 12 | Plugin | VST3/AU with host transport, inside the DAW | After MVP; VST3 on macOS since 0.1.0-alpha.2, AU and CLAP next (D-098) |

Milestone 7 is a separate DSP track and must not delay earlier milestones.

## 5. After MVP 1.0 (alpha)

**Immediate order:**

```
Alpha release (v0.1.0)
  → VST3 / AU / CLAP plugins (milestone 12; VST3 and Visona Sync done, AU and CLAP built, Windows and Linux plugin builds, D-099)
  → UX polish and open issues from alpha use
  → Touch UI (milestone 5) and Pi appliance hardening (milestone 6)
  → Sync precision focus area (D-035)
  → Auto BPM (milestone 7)
  → Modular dashboard and further modules (milestones 8–11)
```

**Still open from the MVP defer list:**

- Sync precision as a dedicated focus area: acceptance criteria, jitter handling, possibly a PLL, and a sync offset (D-035)
- Hotplug and reconnect (D-033)
- Step 8 file logging and written measurement notes (D-040)

**Longer-term module order** (after plugins and v1 appliance):

```
v1 beat-synced scope (Mac + Pi appliance)
  → Auto BPM
  → Modular widget/layout engine
  → Spectrum
  → Phase / correlation
  → Peak / RMS / True Peak
  → LUFS / loudness
  → Dedicated hardware meter replacement
```

**The order follows real usage.** If the scope turns out to be extremely useful, improving the scope beats mechanically moving on to the next module.

## 6. Later, unscheduled

The architecture should allow these, but they are not planned yet:

- More inputs than stereo: several sources, each with 1..N channels (D-049, D-052)
- Display modes other than sweep, such as scroll, and channel views such as Mid, Side, or overlaid L/R (D-034, D-057)
- Windows shorter than ¼ bar or longer than 4 bars, such as ⅛ bar and 8 bars (D-058)
- Time signatures other than 4/4 (D-017)
- Presets and layouts
- Auto-hide or overlay controls and a settings drawer
- A diagnostic mode on the Pi
- A calibrated sync offset, after testing with real hardware
- Windows packaging, skins and themes, networking, remote control
