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
- **D-056 — The waveform shape is always the full-band signed min/max. Frequency coloring is a visualization aid that must never alter the shape.** `Active`
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
- **D-060 — Before the first Start, the free-running sweep uses a fixed 2 s window.** `Active`
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
- **D-064 — The device Visona opens on its first start is saved as the chosen device. If the saved device is missing at a later start, no other device is opened and `NO AUDIO INPUT` is shown.** `Active`
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
- **D-069 — The chrome reflows in three steps with breakpoints at 760 and 480 logical pixels of width and 360 of height.** `Active`
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
- **D-074 — The tempo is the least-squares fit of the last 25 clock times (24 intervals, one beat).** `Active`
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
- **D-078 — A MIDI event's sample time is `mapper(t_midi) + latencyOffset`, with the audio input latency as the offset.** `Active`
  - Sound captured at time T carries a stream timestamp of T plus the input latency. A MIDI event stamped at T therefore belongs to the audio that appears later in the stream by that latency. The architecture had the sign the other way.
  - With CoreAudio's input timestamps, which mark the start of each buffer, the offset is the device latency plus the safety offset plus the stream latency: JUCE's input latency minus one buffer.
  - With the fallback clock, which is read after the buffer has filled, it is JUCE's full input latency.
  - The diagnostics overlay shows the offset in use. Step 8 measures the real one.
- **D-079 — After a freeze, a position that does not carry on from where the sweep froze is a relocation.** `Active`
  - Carrying on means resuming within one tick of the frozen position, which covers a Stop that came before its extrapolated tick ended.
  - On a relocation the head jumps to the new phase and starts a new pass without emptying anything, so the old content becomes the previous pass (architecture 3.4).
  - Start and window changes clear the sweep. Frames missing without a freeze, such as a dropped block, still empty their bins (D-067).
- **D-080 — The beat-synced UI.** `Active`
  - The status bar reads `WAITING`, `MIDI RUN`, `STOPPED` or, in red, `MIDI CLOCK LOST`, next to the tempo such as `126.0 BPM`. The window reads `1 BAR` and so on, or `2 s` while the sweep runs free before the first Start.
  - Bar numbers are absolute, such as 17 after relocating there, and sit at the bottom edge. A window that starts between bars is labelled bar.beat.
  - The grid of each column is the grid of the pass it was drawn in, so columns ahead of the head keep the previous window's lines.
  - `STOPPED` dims the frozen view by 30 % and shows a pause mark. `MIDI CLOCK LOST` is a red banner over the scope, like `NO AUDIO INPUT`, which takes precedence.
  - Keys 1 to 5 choose the window.
- **D-081 — A new audio stream resets the transport to `Waiting`, and MIDI events wait for the stream's first block.** `Active`
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
- **D-085 — Zoom.** `Amended by D-087`
  - Stepless, from the whole window down to 1/32 of it. A zoom within a zoom narrows the view further. There is no panning: zoom out and in again.
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

---

## Open questions

- **Q-006 — What is the BPM min/max range?**
  This matters less with the sweep model, since the sweep buffer is sized in bins rather than samples. The 0.5 s clock-loss timeout implies a floor of about 5 BPM.
- **Q-008 — Is a calibrated visual sync offset between MIDI and audio needed (e.g. ±20 ms)?**
  To be decided from the step 8 measurements. The architecture must not rule it out.
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
