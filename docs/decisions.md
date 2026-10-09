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

- **D-033 — MVP 1.0 is a macOS standalone app with the reference audio interface connected at startup; there is no hotplug or reconnect.** `Amended by D-102`
  Keeps the MVP small and on the primary development environment. The Raspberry Pi (a hardware spike, the appliance and the 7" touch UI, i.e. milestones 5–6) moves to after the MVP.
- **D-034 — The scope uses a sweep display.** `Active`
  - The bar grid is fixed.
  - A write head sweeps left to right and overwrites the previous pass.
  - x is the musical phase within the window.

  Bar positions stand still on screen. Sweep is the only mode in the MVP.
- **D-035 — Sync precision in the MVP: MIDI Clock is trusted directly (tick counting plus simple smoothing), with no PLL.** `Active`
  Enough to prove the concept. Acceptance criteria and jitter handling are revisited after the MVP as a dedicated focus area.
- **D-036 — MIDI Clock (Clock, Start, Stop, Continue) is the MVP's only clock source; Manual BPM moves to after the MVP.** `Amended by D-090`
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
- **D-043 — Development flow.** `Amended by D-095`
  - GitHub Actions builds the macOS app, plus the core and tests on Linux, on every PR.
  - Agents open draft PRs; the maintainer tests on a Mac and merges.

  The Linux job keeps the core free of platform dependencies (D-003) and is cheap. A self-hosted runner may be added later.
- **D-044 — The platform is macOS on Apple Silicon (arm64) only.** `Extended by D-099`
  One architecture and one real test environment are enough for a proof of concept. The reference environment is an Apple Silicon Mac with Xcode, and Ableton Live with MIDI Clock Type = Song, so that SPP is sent.
- **D-045 — MVP transport policies:** `Active`
  - Start: bar 1, and the sweep restarts.
  - Stop: freeze the last frame and show `STOPPED`.
  - Continue: resume from the tick count / SPP position.
  - Clock loss (about 0.5 s without ticks): freeze and show `MIDI CLOCK LOST`.
  - Before the first Start: a free-running sweep with a fixed time window.

  These are the explicit defaults required by D-016.
- **D-046 — MVP UI:** `Amended by D-100 and D-108`
  - Status at the top: BPM, MIDI state, sample rate, window and gain.
  - Interactive controls at the bottom: window ¼ ½ 1 2 4, and gain from 0 to +36 dB (+18 dB since D-100).
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
- **D-051 — There are three bands, with blue lows, orange mids and white highs, and crossovers around 200 Hz and 2.5 kHz as starting values.** `Amended by D-056 and D-092`
  The bands drive the frequency coloring only, and are tuned after testing with real music.
- **D-052 — Inputs are modeled as sources, each a group of 1..N channels. The MVP has exactly one stereo source.** `Active`
  The concrete model for D-049: the core iterates over sources and channels, never over a fixed L/R pair.
- **D-053 — The macOS build is arm64 only, with deployment target macOS 14.** `Active`
  Modern APIs, without requiring the newest macOS to run the app. Refines D-044.

### MVP plan approval

- **D-054 — The MVP plan is approved, including its proposals and technical recommendations.** `Amended by D-083`
  The plan is now the basis for the steps in [roadmap.md](roadmap.md). Approved:
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
- **D-056 — The waveform shape is always the full-band signed min/max. Frequency coloring is a visualization aid that must never alter the shape.** `Active` (the coloring method is D-092)
  - The colors are only a way to see which frequencies make up the sound; correct waveform rendering matters more.
  - Step 5 chooses the coloring method, either a blended color per column or bands drawn inside the full-band outline, based on which keeps the waveform correct and readable.
  - The mono/precise mode stays.

  Amends D-047 and D-051.
- **D-057 — L/R stacked is kept, with lanes modeled as generic channel views.** `Active`
  Stacked is the right way to see differences between channels. Mid, Side or a single lane can become a setting later without a rewrite. Confirms D-046.
- **D-058 — ¼ bar is kept as the shortest window.** `Active`
  The sweep buffer and bins do not assume ¼–4 bars, so ⅛ bar and 8 bars can be added as settings later. Confirms D-020.
- **D-059 — Clock recovery: when ticks return after a clock loss, the transport continues counting from the last position.** `Active`
  Simplest for the MVP. The position may be off until the next Start, or SPP + Continue.
- **D-060 — Before the first Start, the free-running sweep uses a fixed 2 s window.** `Superseded by D-090`
  Completes D-045.

### Implementation

- **D-061 — The macOS bundle ID is `io.github.stephanterning.visona` for now.** `Active`
  It matches the GitHub-hosted project and is used by the skeleton ([PR #4](https://github.com/stephanterning/visona/pull/4)). `se.stephanterning.visona` remains a possible later switch. Changing it later means users must re-grant the microphone permission, and saved settings move.
- **D-062 — When the audio ring is full, the audio thread drops the whole incoming block and counts an overrun.** `Active`
  The audio thread never waits, never splits a block and never overwrites audio the analysis thread has not read. `sampleIndex` keeps counting through dropped blocks, so the analysis sees a drop as a jump in `sampleIndex` instead of as shifted time. Overwriting the oldest audio instead would require the producer to move the consumer's read position, which a lock-free SPSC ring cannot do safely. Approved by the maintainer in the review of [PR #5](https://github.com/stephanterning/visona/pull/5).
- **D-063 — The audio device is opened with all of its input channels, and the audio callback copies the input channel chosen for each source channel into the ring.** `Active`
  - The settings panel has one choice per source channel (Left and Right), so any two inputs can form the pair, in either order. The reference interface's S/PDIF input, for example, is not on inputs 1 and 2.
  - A new choice takes effect at the next block without restarting the device, so `sampleIndex` and the overrun counters keep running.
  - Copying every input channel costs little, even on interfaces with many inputs.
- **D-064 — The device Visona opens on its first start is saved as the chosen device. If the saved device is missing at a later start, no other device is opened and `NO AUDIO INPUT` is shown.** `Amended by D-102`
  Visona restores the last device instead of following the system's default input, and never silently shows another device's audio after a start. A device that disappears while Visona runs is handled by JUCE, which opens the default input; hotplug is Q-018.
- **D-065 — Block host times use the audio device's time base. On macOS the fallback clock is `CLOCK_UPTIME_RAW` (`mach_absolute_time()`), not `std::chrono::steady_clock`.** `Active`
  CoreAudio host time stops while the Mac sleeps, but libc++'s `steady_clock` uses `CLOCK_MONOTONIC_RAW`, which keeps counting. `ClockTimeMapper` needs one time base, and JUCE's MIDI timestamps on macOS are also based on `mach_absolute_time()`.
- **D-066 — App Nap is disabled while Visona runs.** `Active`
  Visona must keep up while it is in the background, for example behind the DAW. App Nap throttles timers, which would starve the thread that drains the audio ring and cause overruns.
- **D-067 — Frames missing from the stream leave their bins empty in the current pass.** `Active`
  When the ring drops a block (D-062), `sampleIndex` jumps, and the sweep empties every bin the missing frames would have filled. The gap shows as a gap in the waveform, instead of the previous pass's data drawn as if it were new, or a flat line that looks like silence. A jump of a whole window or more empties every bin.
- **D-068 — The write head is a 1.5-pixel accent-green line followed by a 6-pixel erase gap, and the previous pass is not dimmed.** `Active`
  - Sizes are logical pixels. The accent is `#3fe08a`, which is outside the band palette and the status colours.
  - The erase gap is the "small erase gap ahead of the head" that the architecture proposed to try.
  - The plan dimmed the previous pass to 50–60 %. In the hardware check of [PR #7](https://github.com/stephanterning/visona/pull/7), the maintainer found the green line enough to read the sweep, so both passes are drawn at full brightness.
  - Bins still carry their pass, so the column at the head shows only the new pass, and dimming could return as a setting.
  - Since the start of a new pass leaves the old columns as they are, it redraws only the columns across the end of the window.
  - The colour and sizes are a token in `ui/Palette.h` and constants in `ui/ScopeView.cpp`.
- **D-069 — The chrome reflows in three steps with breakpoints at 760 and 480 logical pixels of width and 360 of height.** `Amended by D-108`
  - *Wide* (760 or wider): everything on one row with full labels.
  - *Narrow*: icon-only buttons; control groups wrap onto a second row if they do not fit.
  - *Compact* (narrower than 480 or lower than 360): the diagnostics and full-screen buttons move into the settings panel, and the bars get slimmer.
  - The status bar drops values from its end, down to the state, whenever they do not fit.

  Until step 7 adds the WINDOW control, the control bar fits on one row at every allowed window size, so the wrap step is in place but rarely seen.
- **D-070 — Step 3's debug readout becomes a diagnostics overlay, hidden by default.** `Active`
  - D, or the Diagnostics button, shows and hides it.
  - Besides the audio input values, it shows the analysis thread's load, the frame rate, render and paint times, and Visona's total CPU use, for the step 4 performance check and later measurements (D-040).
  - It is opaque, so updating it never repaints the scope behind it.
- **D-071 — The scope is rasterized into image tiles 64 physical pixels wide.** `Active`
  On macOS, JUCE 9 copies the whole `juce::Image` into a new `CFData` every time a changed image is drawn. One window-sized image would copy up to about 58 MB per frame in Retina full screen on a 5K display. With tiles, a frame only changes, and copies, the one or two tiles the write head passed. This implements the incremental rendering of D-054 without OpenGL.
- **D-072 — MIDI Clock sync (steps 6 and 7) comes before frequency coloring (step 5).** `Active`
  The maintainer's choice after step 4. Sync is the core of the product, and it does not depend on the band split. The steps keep their numbers.
- **D-073 — Song Position Pointer is accepted whenever the transport is not `Running`: in `Waiting`, `Stopped` and `ClockLost`.** `Active`
  The architecture only named `Stopped`. The position is not advancing in the other two either, so a pointer there can only mean a relocation. In `Running` it is still ignored, and counted for the log.
- **D-074 — The tempo is the least-squares fit of the last 25 clock times (24 intervals, one beat).** `Amended by D-110`
  - It uses the same one-beat window as the planned moving average, which only looks at the first and last clock of the window. The fit uses every clock in it, so under the same jitter the estimate varies about half as much.
  - A gap longer than the clock-loss timeout starts the estimate over, so the time between Stop and Continue, or during a clock loss, is not taken for a clock interval.
  - It is still reset on Start.
- **D-075 — The transport describes the audio's sample timeline as spans: free-running, musical, frozen and pending.** `Active`
  - *Free-running* is before the first Start.
  - *Musical* lies between two known ticks, or covers the at most one tick extrapolated on Stop or clock loss. The position in ticks runs linearly across it.
  - *Frozen* is written nowhere: after Stop, after clock loss, and after a Start before its first clock.
  - *Pending* follows the last tick while running; its audio waits for the next tick.
  - Each span records how many Starts came before it, so the sweep knows when to start over. A Continue from `Waiting` counts as one, since it leaves the free-running sweep.
  - The clock-loss timeout runs on the audio's timeline, so it is deterministic in tests.
  - On Stop, the extrapolation ends at the Stop if that comes first, since audio after it belongs to the stopped song.
- **D-076 — `ClockTimeMapper` fits a least-squares line through the audio blocks of the last 2 s.** `Active`
  - With one block, or with a fit more than 1 % off the nominal sample rate, it maps through the newest block at the nominal rate.
  - With ±100 µs of timestamp jitter at 96 kHz, it maps MIDI times to within 0.7 samples on average and 3 at most, over 300 test seeds.
- **D-077 — On macOS, JUCE stamps MIDI input with the CoreMIDI packet time, converted to its millisecond counter. Visona converts it back to host nanoseconds.** `Active`
  - This is the step 7 check of JUCE's MIDI timestamps, read from the JUCE 9.0.2 source (`juce_CoreMidi_mac.mm`, `juce_SystemStats_mac.mm`).
  - The packet time is the driver's receive time, not the moment the app sees the message.
  - JUCE anchors the conversion with a whole millisecond, so its timestamp can be up to about 1 ms early. The error is constant while the input stays open.
  - The counter is `mach_absolute_time()` in milliseconds, modulo 2^32. The MIDI callback undoes the wrap relative to `CLOCK_UPTIME_RAW`, the audio's time base (D-065).
  - The remaining error of up to 1 ms is left to the offset measurement in step 8. Thin CoreMIDI timestamping in `app/` stays the fallback if it matters.
- **D-078 — A MIDI event's sample time is `mapper(t_midi) + latencyOffset`, with the audio input latency as the offset.** `Amended by D-109`
  - Sound captured at time T carries a stream timestamp of T plus the input latency. A MIDI event stamped at T therefore belongs to the audio that appears later in the stream by that latency. The architecture had the sign the other way.
  - With CoreAudio's input timestamps, which mark the start of each buffer, the offset is the device latency plus the safety offset plus the stream latency: JUCE's input latency minus one buffer.
  - With the fallback clock, which is read after the buffer has filled, it is JUCE's full input latency.
  - The diagnostics overlay shows the offset in use. Step 8 measures the real one.
- **D-079 — After a freeze, a position that does not carry on from where the sweep froze is a relocation.** `Active`
  - Carrying on means resuming within one tick of the frozen position, which covers a Stop that came before its extrapolated tick ended.
  - On a relocation the head jumps to the new phase and starts a new pass without emptying anything, so the old content becomes the previous pass (architecture 3.4).
  - Start and window changes clear the sweep. Frames missing without a freeze, such as a dropped block, still empty their bins (D-067).
- **D-080 — The beat-synced UI.** `Amended by D-090`
  - The status bar reads `WAITING`, `MIDI RUN`, `STOPPED` or, in red, `MIDI CLOCK LOST`, next to the tempo such as `126.0 BPM`. The window reads `1 BAR` and so on, or `2 s` while the sweep runs free before the first Start.
  - Bar numbers are absolute, such as 17 after relocating there, and sit at the bottom edge. A window that starts between bars is labelled bar.beat.
  - The grid of each column is the grid of the pass it was drawn in, so columns ahead of the head keep the previous window's lines.
  - `STOPPED` dims the frozen view by 30 % and shows a pause mark. `MIDI CLOCK LOST` is a red banner over the scope, like `NO AUDIO INPUT`, which takes precedence.
  - Keys 1 to 5 choose the window.
- **D-081 — A new audio stream resets the transport to `Waiting`, and MIDI events wait for the stream's first block.** `Active` (`Waiting` is `FREE` since D-090)
  - The ticks' sample times belong to the old stream, so a device restart, for example a new sample rate in Settings, needs a new Start or Continue. That is rare, and simpler than carrying positions across streams.
  - Events that arrive before the first block wait in the queue; without any stream they are dropped.
  - The MIDI input is saved by JUCE identifier and by name. A saved input that is missing stays chosen, and the next start opens it if it is back.
- **D-082 — Horizontal zoom is part of the MVP, as its own step, 7b.** `Active`
  - The maintainer asked for it after step 7: zoom in on part of the window, such as the third beat of a 1-bar window, to study how the waveform moves, without choosing another window.
  - It is presentation only. The window, the analysis and the sweep stay as they are, and WINDOW still shows the chosen window.
  - Step 7b follows step 7, and the later steps keep their numbers.
- **D-083 — B = 131,072 bins per window.** `Active`
  - At the deepest zoom, 1/32 of the window, 4,096 bins are in view: about one per physical pixel of a 16-inch MacBook Pro's 3,456-pixel-wide Retina display in full screen. Fewer would blur the deepest zoom, and more could not be seen.
  - A stereo sweep buffer grows from 96 KiB to 3 MiB, and there are four: the analyzer's and three snapshots. Publishing still copies only the bins that changed.
  - Tests that compare bins with samples choose B = 4096 themselves.

  Amends D-054.
- **D-084 — Each bin holds the min and max of the signal drawn as straight lines between consecutive samples.** `Active`
  - A window can now have fewer frames than bins: a ¼-bar window at 120 BPM and 96 kHz has 48,000. Bins between samples would stay empty, and at deep zoom the waveform would fall apart into dots.
  - Where the line between two consecutive frames crosses a bin boundary, its value there goes into both bins, and a bin no sample falls in holds the piece of line through it. The waveform is one connected line at every zoom, as if the samples were joined by lines.
  - No line is drawn across a gap (D-067), a freeze or a relocation (D-079).
  - The bins still do not depend on how the audio is split into blocks.
- **D-085 — Zoom.** `Amended by D-087, D-089, D-090 and D-108`
  - Stepless, from the whole window down to 1/32 of it. A zoom within a zoom narrows the view further. There is no panning: zoom out and in again (amended by D-089).
  - Mouse: dragging across the scope zooms to the part selected, freely, without snapping; a drag under 8 pixels is a click and does nothing. The scroll wheel zooms around the pointer, up to zoom in. A trackpad pinch, and a two-finger pinch on a touchscreen, zoom around the point between the fingers.
  - Reset: Esc once the settings panel is closed, a double-click or double-tap on the scope, or the × of the overview strip. Choosing a window, and a switch between free-running and musical time, such as the first Start, reset it too. There are no zoom keys.
  - A view may run past the end of the window and carry on at its start, as the head does, so the downbeat can be seen from both sides. Zooming out near an edge gets there. Zooming out also turns the view back towards the whole window, which it reaches exactly at 1×.
  - While zoomed, an overview strip above the scope shows the whole window, the part in view and the head. The status bar shows the zoom, such as `ZOOM 4.0× · 1.3–1.4`: the magnification, then where the view starts and ends within the window, as bar.beat, bar.beat.sixteenth when less than a beat is in view, or seconds while the sweep runs free. The positions count from the window's start, so they stay the same from one window to the next.
  - The head line shows only while the head is in view. The grid adds thirty-seconds with a quarter of a bar in view and sixty-fourths with an eighth With a bar or less in view, beats are labelled bar.beat, and the left edge always names the beat the view starts in.
  - The zoom works while stopped and in the free-running sweep, lasts through Stop, Continue and every Start after the first, and is not saved across restarts.
- **D-086 — The roadmap's steps are called steps, not PRs: step 1 to step 8, and step 7b.** `Active`
  - A step is still one draft pull request, but GitHub numbers pull requests on its own, so "PR 7b" next to #12 was confusing. "PR" and "#" now always mean a GitHub pull request.
  - The docs were reworded to match. Commit messages and pull requests from before this call step N "PR N".
- **D-087 — The grid's finest note value follows how much is in view, and the scope names it.** `Active`
  - With more than 2 bars in view the grid marks beats only, up to 2 bars eighths, up to 1 bar sixteenths, up to ½ bar thirty-seconds, and with ¼ bar or less sixty-fourths. Each window gets its own value unzoomed, from 1/4 at 4 bars to 1/64 at ¼ bar, and zooming in refines it.
  - The maintainer asked for sixteenths in the 1-bar window, which before showed them only from ½ bar in view.
  - Eighths and sixteenths share one grey; thirty-seconds and sixty-fourths are fainter.
  - The bottom right corner of the scope names the finest value and its length at the current tempo, such as `1/16 · 125 ms`, in whole milliseconds from the tempo as the status bar shows it. Bar numbers that would run into it are left out.
  - The free-running sweep has no grid, so it shows nothing there.

  Amends D-085.
- **D-088 — −12 dBFS and −18 dBFS reference lines appear from +4 dB and +10 dB of display gain.** `Active`
  - With more gain, 0 dBFS and then −6 dBFS leave the lane, and the quieter lines take over as references. They look like the others and are labelled the same way.
  - There are no lines below −18 dBFS; the maintainer does not expect to need that much gain.
- **D-089 — A zoomed view can be moved, round past either end of the window.** `Active`
  - The maintainer asked for it after testing step 7b: to study the move from the last beat to the first, drag the view across the end of the window and see the end of one bar and the start of the next.
  - Dragging anywhere on the overview strip moves the view with the pointer or finger, not only dragging the highlighted part, which is only a few pixels wide at 32×. Past either end of the window the view carries on at the other end, so it can be moved round and round.
  - A click or tap on the strip outside the part in view centres the view there. A double-click still resets the zoom.
  - On the scope, scrolling sideways, such as with two fingers on a trackpad, or Shift with a mouse wheel, moves the view. Dragging on the scope still selects a part to zoom to.
  - Past the end of the window, bar numbers read on into the next window, such as 12.4 and then 13, and so does the status bar, such as `ZOOM 4.0× · 1.4–2.1`.

  Amends D-085.
- **D-090 — The free-running sweep is in bars at a tempo set by hand, and is called `FREE`. Clicking `STOPPED` or `MIDI CLOCK LOST` switches to it.** `Active`
  - The maintainer asked for it after testing step 7b: after Stop in Live, switch Visona to running free instead of looking at a frozen view.
  - `FREE` replaces `WAITING` and the 2 s window (D-060). The sweep then shows the chosen window, ¼ to 4 bars, at the free tempo, with the same grid and bar numbers as with MIDI Clock. It is not locked to any music: bar 1 is where the sweep started, and it starts over at bar 1 on a new tempo or window.
  - `STOPPED` and `MIDI CLOCK LOST` are drawn as buttons in the status bar. A click or tap switches to `FREE` at the tempo MIDI Clock last had, or the saved free tempo if none is known. The next Start or Continue follows MIDI Clock again; Continue resumes where the song stopped, as from `Waiting` before.
  - Manual BPM is back in the MVP, as the tempo of `FREE`: `BPM [−] 120.0 [+]` in the control bar, next to WINDOW. The buttons step whole BPM and repeat while held; dragging the value or scrolling over it fine-tunes it in 0.1 BPM steps. The range is 40–300 BPM, which answers Q-006 for the free tempo. While MIDI Clock sets the tempo, the control shows it dimmed and cannot be changed.
  - The free tempo is saved, and is 120 BPM on the first start. Visona starts in `FREE` at the saved tempo.
  - In the core, the transport's `runFree()` leaves `Stopped` or `ClockLost` for the free-running state, as a new start, and the pipeline gives the free-running sweep positions in ticks from the free tempo. The analyzer's frame-based window is no longer used by the app.
  - A Start from `FREE` keeps the zoom, since the window is the same.
  - `FREE` has the grid and the resolution label of D-087, with the free tempo's milliseconds.

  Amends D-036, D-045, D-080, D-085 and D-087, and supersedes D-060.
- **D-091 — The waveform has three drawing modes: STD, PRECISE and DJ, modelled on Oszillos Mega Scope.** `Amended by D-108`
  - The maintainer asked for them in step 5, after using Mega Scope, whose Precise mode he uses most.
  - *PRECISE* is the filled full-band signed min/max of every column, as before (D-050). Nothing is missed, and it is the default.
  - *STD* is a thin line through the signal, sampled at each column edge. With many samples per column it is cheaper to draw, since it reads two bins per column instead of every bin, but it can miss peaks. At deep zoom it is exact, since the bins then hold the lines between samples (D-084). For it, every bin also records where the signal enters it.
  - *DJ* is PRECISE coloured by frequency (D-092).
  - WAVE `[STD][PRECISE][DJ]` in the control bar chooses the mode, and W steps through them. The mode is saved.
  - The shape of every mode is the full-band signal (D-056). The mono/precise mode of the plan is PRECISE.
- **D-092 — DJ colouring mixes the three bands as red, green and blue per column, like Mega Scope.** `Active`
  - Mega Scope's manual describes its colouring as a three-band EQ whose outputs are used as RGB colours ([manual, section 17](https://schulz.audio/products/oszillos-mega-scope/manual/)). DJ mode's own details are not documented; its screenshots show saturated colours at full brightness.
  - Each bin holds the peak level of each band from the `BandSplitter` (LR4 at 200 Hz and 2.5 kHz with allpass compensation, from the closed [#9](https://github.com/stephanterning/visona/pull/9)). A column takes the peak of each band over its bins.
  - The colour is a mix of a warm red for the lows, a bright green for the mids and a light blue for the highs. Each band is weighted, 1, 1.3 and 1.8, since music has far more energy in the bass, and squared, so that the strongest band sets the hue: a kick is red, a kick with mids orange, a hi-hat blue. The mix is scaled to full brightness, so the colour shows the balance between the bands and never the level. The weights are starting values to tune against real music.
  - Each band is shifted back by its filter delay when it is read, at the band's reference frequency: 2.6 ms for the lows, 0.4 ms for the mids and 0.03 ms for the highs. Only bins of the same pass are used, so the head's column never takes the previous pass's colour.
  - The band filters run only in DJ mode; STD and PRECISE cost nothing extra. In a Debug build, DJ mode took the analysis thread from about 1 % to 2 % of a core. Bins written before DJ mode was chosen have no band levels and use the waveform colour.
  - The head stays green; the erase gap after it keeps it readable against the green of the mids.
  - Each bin grows by 16 bytes per channel: 4 MiB per stereo sweep buffer, 16 MiB for the analyzer's buffer and the three snapshots.

  Supersedes the two coloring methods of step 5 and the palette of D-051; the crossovers stay.
- **D-093 — STD and PRECISE use a waveform colour chosen in the settings from eight presets.** `Active`
  - Teal, like Mega Scope, is the default, and cyan, blue, violet, pink, amber, yellow and grey the others: calm but clear on black. Green is left out, since it is the head's colour.
  - The colour is a row of swatches in the settings panel, and is saved.
  - It only colours the waveform. A colour for the UI's own components may come later.
- **D-094 — A measurement ruler: drag with the right button to draw a rectangle, whose width is read against the time axis as shown.** `Active`
  - The maintainer asked for it, modelled on Oszillos Mega Scope's measure overlay. It is a ruler, not an analysis: the values never depend on the audio or the waveform drawn, and it may be drawn anywhere, across both lanes.
  - Dragging with the right button, a two-finger click on a trackpad or Control-click draws the rectangle; a left drag still zooms (D-085). On a touchscreen, which has no right button (D-025), a finger held still for half a second, within 8 pixels, and then dragged draws it. The rectangle and a readout beside the pointer exist only while the button or finger is held, and go away on release. Nothing is kept, and it does not snap.
  - The width is read against the time axis as shown, from the window, the zoom and the tempo of the sweep: the free tempo in `FREE`, MIDI Clock's estimate otherwise. The readout shows `ms`, `samples`, `frequency` (1 / the length), `note` and `length`. The values follow the tempo and the zoom live while the ruler is held.
  - Notes are named as in Ableton Live and Mega Scope, with middle C as C3 and A3 at 440 Hz, plus cents, such as `G#-2 +23 ct`. The length is in beats, with the note value down to sixty-fourths, or whole bars, that it is within 2 % of, such as `0.75 beats · 3/16`.
  - The readout has no levels for now. Mega Scope also shows start, end and delta dB from the rectangle's height; the maintainer asked to leave them out until further notice.
  - Precision is one pixel: at 120 BPM and 96 kHz on a scope about 1,500 logical pixels wide, about 1.3 ms at 1 bar unzoomed and 0.04 ms at 32×. With MIDI Clock the tempo estimate adds a small error, and a tempo change within the window is read at the current tempo.
  - The calculations are in the core (`Ruler.h`) and tested there; `ScopeView` draws the rectangle and the readout.
- **D-095 — Pull-request CI runs the Linux core jobs only; the macOS app, the VST3 plugins and the Pi binary are built by the Release workflow.** `Amended by D-104`
  - Made in the alpha preparation ([#22](https://github.com/stephanterning/visona/pull/22)) to save the private repository's macOS Actions minutes.
  - The Release workflow runs when a GitHub release is published, or on a manual trigger, which also works on a branch to check a macOS build before merging.
  - Until then, the maintainer's local Mac build is the check that the JUCE targets compile.

  Amends D-043.
- **D-096 — The VST3 plugin follows the host playhead, paired with the first frame of each block.** `Active`
  - Hosts report the playhead (position in quarter notes, tempo, time signature and whether it plays) for a block's first frame. The audio thread queues it with that frame's stream index; the analysis thread builds musical spans between consecutive playheads, and the audio after the newest one waits for the next, as after the latest tick with MIDI Clock (D-075).
  - A playhead more than one tick from where the previous one leads is a relocation, such as a loop or a jump. The sweep starts over, and the audio before it keeps going from the previous position.
  - A block without a playhead goes on from the last one at its tempo.
  - The plugin has no MIDI Clock input and no `FREE`, and its editor hides the BPM control and the full screen button.
  - Introduced in [#24](https://github.com/stephanterning/visona/pull/24). [#25](https://github.com/stephanterning/visona/pull/25) moved the pairing to the first frame, after the timeline had been one block early.
- **D-097 — Visona Sync measures, through the plugin's sidechain, how far the audio at the plugin lags the host playhead.** `Active`
  - Ableton Live reports the same playhead to every plugin on a track, so the audio after a plugin with latency arrives late by it ([Ableton's Delay Compensation FAQ](https://help.ableton.com/hc/en-us/articles/209072409-Delay-Compensation-FAQ)). Live aligns a sidechain with the main input of the plugin it feeds.
  - Visona Sync is a VST3 instrument that writes a one-frame −6 dBFS impulse at every bar line of the host playhead. The maintainer chose an audio impulse over a MIDI trigger, which Oszillos Mega Scope uses, because a sidechain is easier to route in Live. Communication between plugin instances was considered and rejected, since it cannot carry sample-accurate timing between tracks.
  - The Visona plugin measures the loudest sidechain sample of each block against the nearest bar line of the block's playhead. The first impulse locks the offset, and a different offset replaces it when two impulses in a row agree. The offset shifts where each playhead sits on the audio timeline (D-096), and a new offset restarts the sweep.
  - The status bar shows the offset, such as `SC +98.7 ms`, and `SC ...` while the sidechain is on but no impulse has arrived.
  - When the maintainer routes Visona Sync to the sidechain, it is used whatever the host's own delay compensation does; Visona does not try to tell whether the host already reports the right timing.
  - The signal is not validated yet: any peak above −20 dBFS counts. The maintainer asked to drop the validation for now, after a stricter check kept losing lock in Live.
- **D-098 — Both plugins are also built as AU, with JUCE's own client, and as CLAP, with clap-juce-extensions.** `Active`
  - JUCE 9.0.2 has no CLAP client. JUCE has announced native CLAP for JUCE 9, but it depends on AudioProcessor v2, which has not shipped. [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) supports JUCE 9 since its #178 and is used by shipping plugins such as Surge XT. It is fetched with `FetchContent`, pinned to a commit.
  - The wrapper maps the CLAP ID to JUCE's `AudioProcessor` without changing the processors, and state goes through `getStateInformation` and `setStateInformation` unchanged. When JUCE ships CLAP, moving to it should keep projects loading if its IDs and state match; that is to be checked then.
  - The CLAP IDs are the VST3 bundle IDs: `io.github.stephanterning.visona.plugin` and `io.github.stephanterning.visona.sync`. AU and CLAP keep manufacturer `Ster`, plugin codes `Visn` and `Sync`, and company name "Stephan Terning".
  - Visona is an AU effect (`aufx`), since it takes no MIDI, with its mono sidechain as an optional second input bus. Visona Sync is an AU instrument (`aumu`), which takes MIDI.
  - The Release workflow runs `auval` on both AUs and `clap-validator` on both CLAPs, and publishes one zip per plugin and format.
- **D-099 — The Release workflow also builds both plugins as VST3 and CLAP for Windows x64 and Linux x86_64.** `Active`
  - The maintainer asked for Windows and Linux plugins in every release and manual run. AU exists on macOS only.
  - The jobs use the new `windows-plugin` (MSVC, Ninja) and `linux-plugin` (GCC, Ninja) presets, run the core tests, validate the VST3s with [pluginval](https://github.com/Tracktion/pluginval) (strictness 5, without GUI tests) and the CLAPs with `clap-validator`, and publish one archive per plugin, format and platform: zips on Windows, tarballs on Linux.
  - Windows links the MSVC runtime statically, so no Visual C++ redistributable is needed. The Windows build does not copy the plugins into `Common Files`, which needs administrator rights.
  - Linux is built on Ubuntu 24.04, so the plugins need glibc 2.38 and GCC 13's libstdc++ or later. The binaries are unsigned; nothing on either platform has been tested in a DAW yet.
  - Extends D-044 for the plugins only; the standalone app stays on macOS and the Pi.
- **D-100 — Auto gain: an optional setting that zooms the display gain in 3 dB steps, out at once and in at bar lines.** `Amended by D-108`
  - The maintainer asked for it. It answers Q-014. It is off by default, and is set in the settings panel of the app and the Appearance panel of the plugin, per instance there, and saved.
  - Display gain now goes up to +18 dB, by hand as well as by auto gain. By hand it still moves in 1 dB steps.
  - Auto gain looks at the peak of each bar, the loudest sample over both channels:
    - As soon as the peak of the bar in progress lands above the top of the lane, the gain drops to the highest 3 dB step that fits it, so the peak is within the top 3 dB of the lane. The maintainer first asked for this at the next bar line, then for at once, since a bar is up to 6 s at 40 BPM. It costs one value in the snapshot and one comparison per frame.
    - At the end of each bar, once the gain has held for the hold time, it rises at once to the highest step that fits the loudest peak of the last hold time, if that is higher. The hold time is 10, 30 or 60 s, 30 s by default (fixed at 10 s since D-108).
    - Bars at or below −50 dBFS do not count when rising, so silence does not zoom in.
  - A loud peak keeps the gain down for a whole hold time after it, so the view does not "breathe" without a separate hysteresis.
  - A bar ends when the transport moves into the next one. While MIDI Clock or the host is stopped, no bar ends and the gain holds; in `FREE` the bars run on at the free tempo. A bar cut short by a new Start, or by a new tempo or window in `FREE`, is dropped for rising, but its peaks still zoom out while it plays.
  - `AUTO` replaces `GAIN` in the control bar, and the status bar shows `AUTO +9 dB`. Setting the gain by hand, with the buttons, drag, scroll, a double-click or the keys, turns auto gain off at the gain it had.
  - The analysis measures each bar's peak and hands the latest 16 bars, and the peak of the bar in progress, to the UI in the snapshot (`BarPeaks.h`); the decision is in `AutoGain.h` and runs on the message thread. Both are in the core and tested there. The plugin keeps its `AutoGain` in the processor, so that it carries on when the editor is closed and opened again.

  Amends D-046.
- **D-102 — The app follows the chosen audio device and MIDI input when they are unplugged and plugged in again.** `Active`
  - The maintainer asked for the Pi, and the Mac as well, to tolerate interfaces being plugged in and out.
  - While the chosen audio device is missing, no other device is opened. JUCE opens the default input in place of a device that disappears on macOS; Visona closes it again, so the view never shows another device's audio in its place. `NO AUDIO INPUT` says the device is not connected and will be opened when it is plugged in.
  - When the chosen device is listed again, it is opened with its saved setup, as at startup. A device that is listed but does not open is tried again every 2 s. This also applies to a saved device that is missing at startup.
  - A running device that has delivered no audio for 2 s is closed as lost and then opened again. JUCE's ALSA thread ends without telling anyone when its card is unplugged or fails, and the device still reports that it is playing, so the view would otherwise freeze.
  - JUCE lists ALSA devices only once. On Linux, Visona checks `/proc/asound/cards` twice a second, and when it changes it replaces JUCE's device types with fresh ones, which list the devices there are now. An open device keeps running if it is still there.
  - The chosen MIDI input is closed when it disappears and opened again when it returns. ALSA may give a replugged interface a new identifier, so it is found by identifier first and then by name, and the new identifier is saved.
  - A choice in the settings panel stops the waiting, so a device the user just failed to open is not retried behind the error the panel shows.

  Amends D-033 and D-064, and answers part of Q-018.
- **D-103 — In kiosk mode the window follows display changes and, on Linux, asks the window manager for real fullscreen.** `Active`
  - The maintainer found the window small and decorated in a corner when the display was switched on after the Pi had booted.
  - JUCE's `setFullScreen()` on Linux only sizes the window to the display. labwc treats such a window as fullscreen until the output changes, then gives it back its own size and a title bar. JUCE on Linux also reads the displays only at startup.
  - On Linux the kiosk window asks for `_NET_WM_STATE_FULLSCREEN`, so the compositor keeps it covering its output, and Visona polls the X screen size twice a second. When it changes, JUCE's displays are read again and the window is fitted to its display. Full screen chosen with F follows too, but does not ask the window manager.
  - On macOS JUCE notices display changes itself, and the kiosk window covers its display again.
  - Without a display at startup, the window is filled when one appears.
  - Checked with labwc 0.7 and XWayland on a headless output: starting with the output off, switching it on, and changing its mode.

  Answers part of Q-018.
- **D-104 — A manual run of the Release workflow builds only the artifacts ticked, and publishes them to a Development builds prerelease.** `Active`
  - The maintainer wants to test a branch, or a `develop` branch composed of several, without merging to `main`, and to build only what the test needs: macOS minutes are wasted on a change tested on the Pi. Nothing is built automatically on `develop` or any other branch.
  - "Run workflow" has one checkbox per artifact: the macOS app, the macOS VST3, AU and CLAP plugins, the Windows and Linux VST3 and CLAP plugins, and the Raspberry Pi app. A platform's job runs only if one of its boxes is ticked, builds only those CMake targets and the core tests, and runs only the checks for them (`auval` for AU, `clap-validator` for CLAP, `pluginval` for VST3).
  - The files go to one prerelease for all branches, `dev-builds` ("Development builds"), named after the artifact, the branch and the commit, such as `Visona-linux-arm64-pi-develop-abc1234.tar.gz`. A new build of the same artifact from the same branch replaces the earlier one. The run's summary links the files.
  - The repository is public, so the files download without logging in, for example with `curl -LO` on the Pi.
  - A published release still builds everything and attaches it to the release. The build files are no longer uploaded as Actions artifacts.
  - The checkboxes only appear when running from a branch that has this workflow, such as `main` or a branch made from it after this change.

  Amends D-095.

- **D-105 — Kiosk mode is the touch appliance: no full-screen button, a control bar at twice the size, and multitouch turned on in labwc.** `Amended by D-108`
  - The maintainer found the bottom controls too small to hit on a 10" Waveshare touchscreen, a pinch did not zoom, and the full-screen button did nothing useful in kiosk mode.
  - In kiosk mode the full-screen button is hidden in the control bar and in the settings panel. F still works from a keyboard.
  - In kiosk mode the control bar is laid out for half the window's width and height and drawn at twice the size, so its controls, text, padding and rows all double (up to twice since D-108). The bar picks its own step (narrow labels, wrapping rows, secondary buttons in settings) from the halved size. The status bar, the zoom strip and the settings panel keep their size.
  - Raspberry Pi OS sets `mouseEmulation="yes"` for touchscreens in labwc, which turns touches into mouse events: tap and drag work, but there is only ever one pointer, so a pinch cannot exist. JUCE 9 already reads XInput 2.2 touch events through XWayland, and `ScopeView` already zooms on a two-finger pinch (D-085). `install-kiosk.sh` sets `mouseEmulation="no"` in the user's `~/.config/labwc/rc.xml`, starting from the system copy if there is none, and keeps a backup. This also affects other desktop apps for that user, for example double-tap in the file manager, which is acceptable on an appliance.

- **D-106 — `install-visona.sh` installs or upgrades Visona on the Pi over SSH.** `Active`
  - The maintainer installs Development builds on `visona-pi.local` and wants one command instead of curl, tar and `install-kiosk.sh` by hand.
  - With no URL the script asks the GitHub releases API for the newest release (including pre-releases) that has `Visona-linux-arm64-pi.tar.gz`, skipping the `dev-builds` tag, because `/releases/latest/` ignores pre-releases. With a URL it installs that tarball (for example a Development builds asset). It stops a running Visona, unpacks to `~/visona` (overwriting the binary and `scripts/pi/`), runs `install-kiosk.sh` from the unpacked tree so kiosk and labwc touch settings match the build, and keeps `~/.config/Visona/`. Options pass through `--enable-autologin` and allow `--install-dir`. A one-liner can bootstrap the script from `main` on GitHub with `curl | bash`.
- **D-108 — Touch refinements: a one-row control bar with select menus and an AUTO button, a pinch that also pans, a tap outside the settings panel to close it, and tooltips for a held finger.** `Active`
  - The maintainer asked for them after using the kiosk build of D-105 on a 1920×1080 Waveshare touchscreen: the control bar wrapped onto two rows, auto gain was hard to see and was set in the settings, a pinch could not move the zoomed view, the settings panel only closed with Done, and tooltips appeared at odd times and stayed after a touch.
  - *One row.* Each control carries a small caption above its value, inside it: WINDOW, BPM, GAIN and WAVE. WINDOW and WAVE are select menus showing the current choice, such as `1 BAR` or `PRECISE`. A click or tap opens the list above the control, at the control bar's size, with the current choice ticked; the keys 1–5 and W still work. The Diagnostics, Full screen and Settings buttons show only their icons whenever their labels do not fit, at any step, and the gaps between the groups are 20, 10 and 8 logical pixels in the wide, narrow and compact steps. The groups wrap onto a second row only when even that does not fit, which a desktop window reaches below about 710 logical pixels of width. The plugin, without the BPM control and the full-screen button, fits on one row at its 640-pixel minimum.
  - *Kiosk scale.* The kiosk control bar is drawn at twice the size when it fits on one row, and otherwise at the largest scale that does, down to 1×, since the maintainer prefers slightly smaller controls to a second row. The step is still chosen from the size the bar is laid out at. In Xvfb it stayed at 2× at 1920×1080 and 1280×720, and was about 1.8× at 1024×600 and 1.4× at 800×480.
  - *AUTO.* A button right of the gain turns auto gain on and off: `GAIN [− +6 dB +] [AUTO]`. While it is on, the button is lit and the gain value drawn in the head's green (`palette::active`), and the caption stays GAIN. The status bar still shows `AUTO +9 dB`, and setting the gain by hand still turns auto gain off at the gain it had. Whether it is on is saved, in the app's settings and per plugin instance.
  - Auto gain is no longer in the app's settings panel or the plugin's Appearance panel, and its hold time is fixed at 10 s, the maintainer's choice; the 10, 30 and 60 s choice is gone, and a saved hold time is ignored.
  - *Pinch.* What was between the fingers stays between them: the pinch zooms around the point between the fingers where it started and moves the view as that point moves, as on iOS. Moving both fingers sideways pans the view, round past either end of the window (D-089). The zoom is horizontal only, so only sideways movement counts. Lifting one finger ends the pinch, and the other finger then does nothing until it is lifted too; there is no momentum. One finger still selects a part to zoom to. `SweepZoom::pinched` holds the rule in the core and is tested there.
  - *Settings.* While the app's settings panel is open, the rest of the window is dimmed, and a click or tap outside the panel closes it like Done. That click does nothing else: it does not reach the scope or the controls under it. The panel's choices open in the same in-window list as WINDOW and WAVE (`ui/ChoiceList`), not JUCE's PopupMenu, whose own window never sees a press from XInput touch events, so a tap on an item did nothing on the Pi. The list lies over the box with the current choice on it, as near as the window allows, and scrolls with a drag or the mouse wheel when it does not fit; a drag never picks an item. The plugin's Appearance panel is unchanged.
  - *Tooltips.* On a touchscreen a finger held still on a control for half a second, within 8 pixels, shows its tooltip, and lifting or moving the finger, or a second finger, hides it. A finger held on the scope draws the ruler (D-094) and shows no tooltip, and a held select menu opens its list instead. A mouse shows tooltips on hover as before. On the Pi a touch also moves the pointer, which left JUCE's hover tooltip showing, so after a touch hover tooltips stay off until the mouse moves more than 8 pixels from it at least half a second later. The texts are unchanged. `ui/Tooltips` replaces `juce::TooltipWindow` in the app and the plugin.

  Amends D-046, D-069, D-085, D-091, D-100 and D-105.
- **D-109 — The app measures the offset between MIDI Clock and the audio from Visona Sync, on an input chosen in the settings.** `Active`
  - The maintainer asked whether an impulse the ear cannot hear, such as Visona Sync's at 21 kHz, could ride on the DAW's master out for Visona to pick up. That was rejected. A one-frame impulse is broadband, so it is heard whatever its level. A tone burst near 21 kHz sits in the converters' anti-alias transition band at 44.1 kHz, is removed by lossy codecs, ends up in bounces, and could only be told from cymbals with a matched filter. Instead, Visona Sync's ordinary impulse goes out on an interface output of its own and comes back on an input of its own, so nothing reaches the master. Routing it there is up to the user.
  - The setting is the **Visona Sync** row of the settings panel: Off, the default, or one of the device's inputs. It is saved. An input the running device does not have counts as Off, but stays saved for when the device comes back (D-102).
  - The audio ring has one more channel for it, which is never drawn. While MIDI Clock runs, `AnalysisPipeline` measures it with `SidechainSyncDetector`, as the plugin does its sidechain (D-097): how many frames after MIDI Clock's nearest bar line the impulse arrives. Every MIDI event is then placed that much later, on top of the input latency of D-078. The measurement covers whatever lies between MIDI Clock and the audio: the DAW's output latency, Live's MIDI Clock Sync Delay, the interfaces and the MIDI path. The detector measures against the timeline without its own correction, so a correct offset keeps measuring the same, and the rule of D-097 holds: the first impulse locks, and a new offset takes over when two impulses in a row agree on it.
  - MIDI Clock still sets the tempo and the position. Nothing is measured in `FREE`, whose bars have nothing to do with the DAW's. The bar is 4/4, as everywhere in the app, so the DAW's bars must be too, and the offset must be less than half a bar.
  - A new offset clears the musical sweep, as in the plugin. MIDI Clock has no position to start over from, so the ticks after the change simply move, and the tempo estimate is off for about a beat.
  - The status bar shows `SC +12.3 ms` once locked and `SC ...` while waiting for the first impulse, as in the plugin. The diagnostics overlay has a Visona Sync row, in both the app and the plugin (it was "Sidechain sync" in the plugin), with the offset in frames, the impulse level and, while waiting, the input's level. Its MIDI offset row shows the offset in use, input latency plus the measurement, which can now be negative.

  Amends D-078: with Visona Sync routed, the measured offset is added to the input latency.
- **D-110 — The MIDI Clock tempo is fitted over up to four bars while it holds steady, and over the last beat while it changes.** `Active`
  - On the Pi the tempo flickered between values such as 129.9, 130.0 and 130.1 BPM. A one-beat fit under about 1 ms of USB MIDI timestamp jitter varies by roughly ±0.1–0.2 BPM at 130 BPM, which the 0.1 BPM display shows. The same fit over four bars (385 clocks) varies about 70 times less, around 0.003 BPM.
  - The window grows clock by clock up to four bars of the time signature. On every clock the last beat is fitted on its own as well. When the two slopes differ by more than six standard errors of the one-beat fit, or by 0.25 % of the tempo if that is more, the tempo has changed: the estimate is the last beat for one more beat, and then grows again from clocks after the change only. The jitter for the standard error comes from the second differences of the clock times, which a tempo change disturbs at one clock only, unlike the residuals of the fit.
  - So a step such as 120 to 126 BPM is followed within about a beat, as before, and a change too small to detect, such as 0.2 BPM, is taken up as the window slides, within four bars.
  - Visona Sync's bar impulse (D-109) is sample-accurate and would give an even better tempo, but only once a bar and only when it is routed. The four-bar fit is already far below the display's resolution, so the impulse still measures the offset only.

  Amends D-074 and D-015's smoothing.

---

## Open questions

- **Q-006 — What is the BPM min/max range?**
  The free tempo is 40–300 BPM (D-090). MIDI Clock's tempo is not limited. This matters less with the sweep model, since the sweep buffer is sized in bins rather than samples. The 0.5 s clock-loss timeout implies a floor of about 5 BPM.
- **Q-008 — Is a calibrated visual sync offset between MIDI and audio needed (e.g. ±20 ms)?**
  To be decided from the step 8 measurements. The architecture must not rule it out.
- **Q-009 — How is the time signature set in MIDI mode, given that MIDI Clock carries none?**
  Not needed for the MVP (4/4 default).
- **Q-012 — Which strategy for controls on small screens: auto-hide, overlay, a settings drawer or tap-to-show?**
  Relevant for the 7" Pi. Responsive chrome on the Mac is already part of the MVP.
- **Q-015 — What should the Pi display stack and rendering backend be (OS image, X11/Wayland/KMS-DRM, GPU backend, kiosk mode, touch input)?**
  After the MVP; part of the Pi hardware spike.
- **Q-016 — Should the Pi use ALSA directly or JACK for audio and MIDI?**
  After the MVP.
- **Q-017 — How is the JUCE app built for the Pi (arm64 Linux): natively, cross-compiled or in CI?**
  The core and tests already build on Linux in CI (D-043).
- **Q-018 — How should the app behave when the sample rate changes under it?**
  Missing, unplugged and returning audio and MIDI devices are answered by D-102, and display changes by D-103.
- **Q-020 — What form should a Pi diagnostic mode and logging take, and how is it reached without a keyboard?**
  After the MVP.
- **Q-021 — Which library should Auto BPM use (tempo, beat and ideally downbeat detection), with an acceptable license?**
  Evaluated in milestone 7.
- **Q-022 — Which FFT library: JUCE DSP or Signalsmith DSP?**
  Evaluated in milestone 9.
- **Q-023 — Which loudness library: libebur128 or an alternative, for LUFS, True Peak and LRA?**
  Evaluated in milestone 11.
- **Q-026 — In which order do the parts moved out of the MVP come back, and how do they fit the post-v1 order?**
  The candidates are the Pi work and sync precision; hotplug and reconnect came back with D-102. Manual BPM came back into the MVP (D-090).
