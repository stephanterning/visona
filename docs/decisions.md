# Visona decision log

A lightweight log of decisions and open questions. The architecture is described in [architecture.md](architecture.md) and the plan in [roadmap.md](roadmap.md).

**Conventions**

- `D-NNN` is a decision and `Q-NNN` an open question. IDs are never reused.
- Each entry has a one-line statement, a status and a short rationale.
- A changed decision is not deleted. It is marked `Amended by`, `Refined by`, `Extended by` or `Superseded by`, pointing at the newer decision.
- Sources:
  - D-001–D-032 come from the original product specification.
  - D-033–D-053 come from three planning review rounds.
  - D-054–D-060 come from the approval of the MVP plan.
  - D-061 onward are made during implementation.
- Open question IDs keep their original numbers. Resolved questions are not listed; their answers are recorded as decisions.

---

## Decisions

### From the original specification

#### Architecture

- **D-001 — C++ and JUCE are the technology stack.** `Active`
  Optimized for real-time audio, MIDI, low overhead, native cross-platform GUI and VST3/AU. JUCE already abstracts the platforms' audio and MIDI APIs.
- **D-002 — No abstractions on top of JUCE for their own sake; the platform layer stays thin.** `Active`
  JUCE already keeps the platform layer small; extra layers cost without adding value.
- **D-003 — The core knows nothing about the platform, device or screen.** `Active`
  Nothing in the core may assume a Raspberry Pi, macOS, ALSA, CoreAudio, a specific audio interface or a screen size. The reference interface configuration is a deployment default. This is what lets one engine run on the Pi, the desktop and in plugins.
- **D-004 — Code is split by responsibility (core, ui, app, platform, plugin, tests), not by platform.** `Active`
  Responsibility boundaries are what protect the core. The concrete layout was approved in D-054.
- **D-005 — There are three separate kinds of state: analysis state, user configuration and platform/runtime state.** `Active`
  This keeps analysis, user choices and the environment apart, so that a device change cannot leak into the analysis, for example.

#### Real-time and audio

- **D-006 — The audio callback is treated as a real-time thread.** `Active`
  It does no continuous allocation, file I/O, waiting on the UI, long mutex locks or heavy synchronous logging. Audio reliability is priority one.
- **D-007 — Processing is block-based: `processAudio(AudioBlock, AudioTimingInfo)` and `processMidi(MidiEvent)`.** `Active`
  Efficient, and it matches how every host delivers audio.
- **D-008 — The audio thread produces analysis data and the UI thread consumes it; UI frame rate never affects audio.** `Active`
  30 fps with perfect audio beats 60 fps with dropouts. The priority order is audio reliability, then sync, analysis, controls and rendering.
- **D-009 — Sample rate is never hardcoded; all time calculations use the current sample rate.** `Active`
  96 kHz is the primary case, but 44.1, 48, 88.2 and 96 kHz must work, and higher rates if possible.
- **D-010 — Latency must be low, stable and predictable, but the scope need not be sample-accurate to what is heard.** `Active`
  The scope is a display, not an insert effect, so stability matters more than an absolute minimum.

#### Transport and clock

- **D-011 — The scope reads time through a common transport interface: tempo, running, beat/bar position, phase and confidence/sync state.** `Active`
  The scope should not need to know *why* the BPM is 126.3, and it must not be hardwired to MIDI Clock.
- **D-012 — The planned transport sources are Manual, AutoAudio, MidiClock and HostTransport; only the ones needed are implemented.** `Active`
  A seam for what is known to be coming, without building four transports up front.
- **D-013 — Clock order: Manual and MIDI in v1, and Auto BPM only once Manual and MIDI work well.** `Amended by D-036`
  MIDI is the most important automatic sync method; Auto is a harder, separate DSP problem. The MVP has MIDI Clock only, and Manual comes after it.
- **D-014 — Tempo and phase/position are separate concepts.** `Active`
  Knowing the BPM does not tell where the bar starts. Start/Continue and tick counting provide the alignment.
- **D-015 — MIDI Clock uses 24 PPQN; Clock, Start, Stop and Continue are handled; BPM is estimated from clock intervals and smoothed.** `Active`
  MIDI Clock has jitter and is not sample-accurate. The BPM display must not jump around, but tempo changes should be detected quickly.
- **D-016 — Start, Stop, Continue and clock-loss behavior is an explicit, configurable policy.** `Active`
  Behavior should never be a side effect of the implementation. The MVP defaults are in D-045.
- **D-017 — 4/4 is the default, but the transport model represents the time signature (numerator/denominator) from the start.** `Active`
  Supporting 3/4 or 6/8 later should not require rewriting the scope engine.
- **D-018 — BPM has decimal resolution, e.g. 126.1.** `Active`
  Real tempos are not always integers.

#### Scope

- **D-019 — Scope v1 consists of:** `Active`
  - stereo input and a ring buffer
  - a musical time base and sync
  - a selectable bar length
  - display gain
  - efficient waveform rendering
  - a touch/mouse UI

  Spectrum, loudness and phase are not needed for the first working version.
- **D-020 — The selectable windows are ¼, ½, 1, 2 and 4 bars.** `Active` (confirmed by D-058)
  The core of the original need.
- **D-021 — The core treats L and R separately, regardless of presentation.** `Extended by D-049`
  Every channel is processed separately, and two channels are never hardcoded.
- **D-022 — Waveform history is buffered and sized dynamically from sample rate, lowest BPM, maximum window and a safety margin.** `Active`
  In the approved plan, history lives as bins in the sweep buffer, and the audio ring is a short transfer buffer.
- **D-023 — Samples are never drawn one by one; the renderer builds a representation for the current pixel width (min/max per bin).** `Refined by D-050`
  Four bars at 120 BPM and 96 kHz is 768,000 samples per channel. Min/max preserves transients better than decimation.
- **D-024 — Display (vertical) gain is a v1 requirement and presentation only.** `Active`
  It never affects audio, analysis, metering, buffered data or output. The same rule applies to every display setting.

#### UI

- **D-025 — The UI is touch-first.** `Active`
  Large touch targets, no required hover or right-click, and clear selected states. The desktop gets extras, never a worse experience.
- **D-026 — The UI is fully scalable and responsive; rendering works from component bounds, never hardcoded pixels.** `Active`
  One UI for 800×480, ultrawide, near-square windows and freely resizable plugin windows, independent of DPI. Fixed logical minimum sizes are fine for touch controls.
- **D-027 — The visualization comes before chrome.** `Active`
  The product is a display, not a settings app.
- **D-028 — Visualizations are widgets from the start and declare minimum size, preferred aspect and priority; there is no dashboard editor before the scope works.** `Active`
  A seam for the monitoring platform without building it prematurely.

#### Dependencies

- **D-029 — Own the product, not every algorithm.** `Active`
  FFT, EBU R128, True Peak and tempo/onset detection come from libraries behind Visona's own interfaces, so a library can be swapped out. Visona owns the architecture, transport, scope, UI, rendering, layout, buffering, bar sync, display gain and UX.
- **D-030 — No Electron or browser runtime.** `Active`
  A Raspberry Pi 4 is a constrained target, where the bottlenecks are CPU, rendering and real-time behavior.

#### Process

- **D-031 — CMake build; the Mac is the primary development environment, and working features are later deployed to the Pi.** `Amended by D-033`
  The fastest iteration and debugging happen on the Mac. The Pi comes after the MVP.
- **D-032 — The core, transport and rendering are tested without hardware, using synthetic signals and synthetic MIDI Clock.** `Active`
  Most of the work can then be developed without the audio interface or the Pi, and bar boundaries and the BPM estimator can be verified deterministically.

### Planning review, round 1

- **D-033 — MVP 1.0 is a macOS standalone app with the reference audio interface connected at startup; there is no hotplug or reconnect.** `Active`
  Keeps the MVP small and on the primary development environment. The Raspberry Pi (a hardware spike, the appliance and the 7" touch UI, i.e. milestones 5–6) moves to after the MVP.
- **D-034 — The scope uses a sweep display.** `Active`
  - The bar grid is fixed.
  - A write head sweeps left to right and overwrites the previous pass.
  - x is the musical phase within the window.

  Bar positions stand still on screen. Sweep is the only mode in the MVP.
- **D-035 — Sync precision in the MVP: MIDI Clock is trusted directly (tick counting plus simple smoothing), with no PLL.** `Active`
  Enough to prove the concept. Acceptance criteria and jitter handling are revisited after the MVP as a dedicated focus area.
- **D-036 — MIDI Clock (Clock, Start, Stop, Continue) is the MVP's only clock source; Manual BPM moves to after the MVP.** `Active`
  MIDI Clock is the most important sync method in the studio. Amends D-013. The ¼–4 bar window selection stays in the MVP.
- **D-037 — The terms `MidiClock` and `Auto` are kept as specified.** `Active`
  `MidiClock` means sync to MIDI Clock, and `Auto` means audio-based tempo detection. Reserving `Auto` for audio-based detection avoids confusion in code and docs.
- **D-038 — Song Position Pointer (SPP) is deferred until after the MVP.** `Superseded by D-041`
  Without SPP, bar alignment is only correct when the DAW sends Start.
- **D-039 — Everything committed to the repository is in English: code, comments, docs, commit messages and PRs.** `Active`
  The rule lives in `AGENTS.md`.

### Planning review, round 2

- **D-040 — The MVP is a proof of concept: verify that the concept holds and learn what works.** `Active`
  Something that can be tried in the studio beats polish. Measurements and lessons learned are an explicit deliverable.
- **D-041 — SPP is part of the MVP.** `Active`
  Ableton Live sends SPP (D-044), and without it Continue after a relocate lands in the wrong place, which would make the proof of concept misleading. Supersedes D-038.
- **D-042 — The license is AGPLv3, matching JUCE's open-source license.** `Active`
  Visona is not a commercial product; it is free for anyone with the same need. GPL/AGPL-compatible dependencies are acceptable.
- **D-043 — Development flow.** `Active`
  - GitHub Actions builds the macOS app, plus the core and tests on Linux, on every PR.
  - Agents open draft PRs; the maintainer tests on a Mac and merges.

  The Linux job keeps the core free of platform dependencies (D-003) and is cheap. A self-hosted runner may be added later.
- **D-044 — The platform is macOS on Apple Silicon (arm64) only.** `Active`
  One architecture and one real test environment are enough for a proof of concept. The reference environment is an Apple Silicon Mac with Xcode, and Ableton Live with MIDI Clock Type = Song, so that SPP is sent.
- **D-045 — MVP transport policies:** `Active`
  - Start: bar 1, and the sweep restarts.
  - Stop: freeze the last frame and show `STOPPED`.
  - Continue: resume from the tick count / SPP position.
  - Clock loss (about 0.5 s without ticks): freeze and show `MIDI CLOCK LOST`.
  - Before the first Start: a free-running sweep with a fixed time window.

  These are the explicit defaults required by D-016.
- **D-046 — MVP UI:** `Active`
  - Status at the top: BPM, MIDI state, sample rate, window and gain.
  - Interactive controls at the bottom: window ¼ ½ 1 2 4, and gain from 0 to +36 dB.
  - A settings panel for audio device, input channel pair and MIDI input, with persisted settings.
  - A resizable window with fullscreen.
  - L/R stacked, with L on top.

  The visualization sits in the middle, with chrome at the edges (D-027).
- **D-047 — The waveform is rendered CDJ-style, as a colored multi-band waveform.** `Amended by D-056`
  Band splitting and coloring are analysis and presentation only, and never touch audio.
- **D-048 — Once the MVP plan is approved, English versions of the architecture, roadmap and decision log move into the repository's `docs/`.** `Active`
  The repository becomes the source of truth for plan and architecture (in English, D-039).
- **D-049 — Visona will eventually handle more audio inputs than stereo. The MVP implements stereo only, but the core never hardcodes two channels.** `Active`
  This avoids a rewrite when more inputs arrive. Extends D-021, and the model is D-052.

### Planning review, round 3

- **D-050 — The waveform is drawn as signed min/max per pixel, not as a mirrored envelope.** `Active`
  Signed min/max preserves asymmetry, such as DC offset and asymmetric transients, which a mirrored envelope hides. Refines D-023.
- **D-051 — There are three bands, with blue lows, orange mids and white highs, and crossovers around 200 Hz and 2.5 kHz as starting values.** `Amended by D-056`
  The bands drive the frequency coloring only, and are tuned after testing with real music.
- **D-052 — Inputs are modeled as sources, each a group of 1..N channels. The MVP has exactly one stereo source.** `Active`
  The concrete model for D-049: the core iterates over sources and channels, never over a fixed L/R pair.
- **D-053 — The macOS build is arm64 only, with deployment target macOS 14.** `Active`
  Modern APIs, without requiring the newest macOS to run the app. Refines D-044.

### MVP plan approval

- **D-054 — The MVP plan is approved, including its proposals and technical recommendations.** `Active`
  The plan is now the basis for the PR steps in [roadmap.md](roadmap.md). Approved:
  - the core as plain C++20 without JUCE
  - Catch2 v3
  - B = 4096 bins per window
  - an in-house LR4 crossover
  - analysis on its own thread with tick interpolation
  - CPU rasterization into `juce::Image`
  - JUCE via `FetchContent`
  - `ApplicationProperties` for persistence
  - the repository layout
  - gain in 1 dB steps with double-click reset
- **D-055 — JUCE 9.0.2, with 8.0.15 as the fallback if the skeleton does not build cleanly.** `Active`
  JUCE 9.0.0 was released on 2026-07-21, and JUCE 8 has had no release since. The license is unchanged, AGPLv3 or commercial, so D-042 holds either way. Sources: [JUCE 9.0.0 release](https://github.com/juce-framework/JUCE/releases/tag/9.0.0), [JUCE releases](https://github.com/juce-framework/JUCE/releases), [LICENSE.md at 9.0.0](https://github.com/juce-framework/JUCE/blob/9.0.0/LICENSE.md).
- **D-056 — The waveform shape is always the full-band signed min/max. Frequency coloring is a visualization aid that must never alter the shape.** `Active`
  - The colors are only a way to see which frequencies make up the sound; correct waveform rendering matters more.
  - PR 5 chooses the coloring method, either a blended color per column or bands drawn inside the full-band outline, based on which keeps the waveform correct and readable.
  - The mono/precise mode stays.

  Amends D-047 and D-051.
- **D-057 — L/R stacked is kept, with lanes modeled as generic channel views.** `Active`
  Stacked is the right way to see differences between channels. Mid, Side or a single lane can become a setting later without a rewrite. Confirms D-046.
- **D-058 — ¼ bar is kept as the shortest window.** `Active`
  The sweep buffer and bins do not assume ¼–4 bars, so ⅛ bar and 8 bars can be added as settings later. Confirms D-020.
- **D-059 — Clock recovery: when ticks return after a clock loss, the transport continues counting from the last position.** `Active`
  Simplest for the MVP. The position may be off until the next Start, or SPP + Continue.
- **D-060 — Before the first Start, the free-running sweep uses a fixed 2 s window.** `Active`
  Completes D-045.

### Implementation

- **D-061 — The macOS bundle ID is `io.github.stephanterning.visona` for now.** `Active`
  It matches the GitHub-hosted project and is used by the skeleton ([PR #4](https://github.com/stephanterning/visona/pull/4)). `se.stephanterning.visona` remains a possible later switch. Changing it later means users must re-grant the microphone permission, and saved settings move.

---

## Open questions

- **Q-006 — What is the BPM min/max range?**
  This matters less with the sweep model, since the sweep buffer is sized in bins rather than samples. The 0.5 s clock-loss timeout implies a floor of about 5 BPM.
- **Q-008 — Is a calibrated visual sync offset between MIDI and audio needed (e.g. ±20 ms)?**
  To be decided from the PR 8 measurements. The architecture must not rule it out.
- **Q-009 — How is the time signature set in MIDI mode, given that MIDI Clock carries none?**
  Not needed for the MVP (4/4 default).
- **Q-012 — Which strategy for controls on small screens: auto-hide, overlay, a settings drawer or tap-to-show?**
  Relevant for the 7" Pi. Responsive chrome on the Mac is already part of the MVP.
- **Q-014 — What should auto vertical gain target (~80 % of the height?), and how slow must it be to avoid visible "breathing"?**
  A later feature; manual gain comes first.
- **Q-015 — What should the Pi display stack and rendering backend be (OS image, X11/Wayland/KMS-DRM, GPU backend, kiosk mode, touch input)?**
  After the MVP; part of the Pi hardware spike.
- **Q-016 — Should the Pi use ALSA directly or JACK for audio and MIDI?**
  After the MVP.
- **Q-017 — How is the JUCE app built for the Pi (arm64 Linux): natively, cross-compiled or in CI?**
  The core and tests already build on Linux in CI (D-043).
- **Q-018 — How should the app behave when the interface is missing, unplugged or returns, when MIDI is missing, when the sample rate changes, or when the display reconnects?**
  After the MVP. The MVP assumes the interface is connected at startup.
- **Q-020 — What form should a Pi diagnostic mode and logging take, and how is it reached without a keyboard?**
  After the MVP.
- **Q-021 — Which library should Auto BPM use (tempo, beat and ideally downbeat detection), with an acceptable license?**
  Evaluated in milestone 7.
- **Q-022 — Which FFT library: JUCE DSP or Signalsmith DSP?**
  Evaluated in milestone 9.
- **Q-023 — Which loudness library: libebur128 or an alternative, for LUFS, True Peak and LRA?**
  Evaluated in milestone 11.
- **Q-026 — In which order do the parts moved out of the MVP come back, and how do they fit the post-v1 order?**
  The candidates are Manual BPM, the Pi work, sync precision and hotplug/reconnect.
