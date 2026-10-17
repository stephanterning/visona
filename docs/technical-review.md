# Technical review and improvement plan

A review of the condition of the code on `main` at `8ad1301` (2026-10-17, after #51), with what should be fixed, what should be rewritten, general recommendations and a plan in phases. It complements [architecture.md](architecture.md), which describes the intended design, and [roadmap.md](roadmap.md), which orders the product work. Nothing here is decided yet; section 6 lists what needs the maintainer's call.

## How the review was done

- Every file in `core/`, `app/`, `plugin/`, `ui/`, `tests/`, the CMake files, the workflows, `scripts/`, `site/` and `third_party/` was read against AGENTS.md, the architecture and the decision log. Findings marked *checked* were confirmed by reading the code, by running it or by inspecting the 1.0.0-beta.1 release assets.
- **Built and run on Linux x86_64:** the core and its 265 tests with the `core-gcc`, `core-clang`, `core-sanitize` (ASan and UBSan) and `core-tsan` presets, all passing; a GCC 13 build of the app, both plugins (VST3 and CLAP) and `visona_ui_checks`, in about 8 minutes on 3 cores without a cache; the UI checks, headless without Xvfb, passing in 2.4 s; and `tests/scripts`, passing.
- **Static checks:** clang-format 18 against `.clang-format`, shellcheck and actionlint on the scripts and workflows, and `objdump`/`readelf` on the published binaries.
- **Not done:** nothing ran on macOS, Windows, a Raspberry Pi or in a DAW. Performance figures for the analyzers below come from an x86 VM and are only relative.

## 1. Summary

The architecture holds up. The core's lock-free primitives, the MIDI Clock transport, the clock mapping, the sweep, the scope renderer and the app's audio path are correct, carefully documented and well tested, and the audio callback of the app really is free of allocation, locks and logging. That is the foundation to keep.

The problems are concentrated in what was added fastest:

- **The plugins** have a use-after-free, an allocation on the audio thread, unvalidated host and project data, and a lost bar impulse in Visona Sync.
- **The analysis views** of #48 and #49 work, but they carry dead and legacy code, positional settings strings, no common view abstraction, and wiring that the app and the plugin each copy.
- **Duplication** between the app and plugin paths has begun to diverge: the two analysis pipelines, the two editors, the two analysis threads and the two playhead readers.
- **CI never compiles a JUCE target.** About 6,000 lines merged since 1.0.0-beta.1 have not been built for macOS, Windows or ARM64 by any workflow.
- **The release artefacts** do not match what the docs promised: the Pi binary needs Debian 13 (now documented), the archives ship no licence or third-party notices, and the binaries report `1.0.0` for a beta.

**The most important recommendations, in order:**

1. Fix the confirmed bugs of section 3.1 before the next release. They are small, local changes.
2. Add a Linux JUCE build with the UI checks, the Python tests, a site build and a format check to pull-request CI (phase 2). It was verified to work here.
3. Consolidate the analysis views before releasing them: delete the dead code, introduce an analysis-view interface, and give the settings a versioned format (phase 3).
4. Remove the app/plugin duplication: one analysis stage in the core, one editor workspace in `ui/`, one analysis thread, one playhead reader (phases 3–4).
5. Fix the release artefacts and the supply chain: licence notices, the version suffix, the Pi build environment, JUCE 9.0.4, checksummed validators (phase 5).
6. Make the Pi an appliance that recovers by itself: a supervised restart, a crash log and the deferred file log of step 8 (phase 6).

## 2. What is in good shape

- **Concurrency.** `TripleBuffer`, `SpscQueue` and `AudioRingBuffer` have correct memory ordering, cache-line separation and TSan stress tests. The threading model in the architecture matches the code.
- **The real-time paths.** The app's callback does one lock-free write and two relaxed stores. `PluginProcessor::processBlock` does not allocate or lock. `RealtimeAllocationCheck` has a self-test.
- **Transport and sweep.** `MidiClockTransport`, `ClockTimeMapper`, `SweepAnalyzer` and `SweepBuffer` are allocation-free after construction, give the same result whatever the block size, have no hardcoded channel count or sample rate, and are thoroughly tested.
- **Platform logic.** The D-102 device follower matches what JUCE 9.0.2 actually does; the host time bases are consistent on macOS and Linux (D-065, D-077); App Nap handling and the kiosk display following are deliberate and documented.
- **The older UI.** `ScopeView`, `ControlBar`'s layout, `ZoomOverview`, `StatusBar`, `GainControl`, `ChoiceList` and `Tooltips` are documented, use named constants, repaint carefully, and handle touch, with lifetime-safe callbacks.
- **Build and release.** Dependencies are pinned by URL and SHA-256, or by commit; core code builds with strict warnings as errors and under sanitizers on every PR; the release pipeline validates with `auval`, `pluginval` and `clap-validator`, notarizes and staples.
- **Formatting.** 176 of 182 C++ files match `.clang-format`.
- **Third-party code** is pinned, unmodified and kept behind Visona's interfaces, with provenance notes, as D-029 asks.
- **Documentation and decisions.** The decision log and the architecture are unusually complete for a project this size.

## 3. Findings

Severity: **high** can crash, corrupt or silently break a promise; **medium** is a real defect or a cost that will grow; **low** is hygiene. Paths and line numbers refer to `8ad1301`.

### 3.1 Bugs to fix now

| # | Severity | Where | What is wrong | Fix |
| --- | --- | --- | --- | --- |
| B1 | high, checked | `plugin/src/PluginProcessor.cpp:46-55` | `prepareToPlay` replaces `ring_`, freeing the old ring, before it detaches the analysis thread with `setStream`. A host that prepares twice without `releaseResources`, such as JUCE's AU wrapper when Logic toggles offline rendering, can have the analysis thread read freed memory. | Detach first (`analysis_.setStream(nullptr, 0.0)`, or call `releaseResources()`), then build the ring and writer, reset the offset, and attach last. |
| B2 | high, checked | `plugin/src/SyncProcessor.cpp:49-52`, `core/src/BarImpulseScheduler.cpp:65` | Visona Sync creates a `std::vector` in `processBlock` and `push_back`s into it, so every block with a bar line allocates and frees on the audio thread. This is the known open item. | Let `impulsesInBlock` write into a `std::span` and return a count, with a small `std::array` in the caller, and mark it `noexcept`. |
| B3 | high, checked | `.github/workflows/release.yml:408-442`, `pi-quick.yml` | The released Pi binary needs glibc 2.38 and `GLIBCXX_3.4.32`, so it does not run on Raspberry Pi OS Bookworm, which the docs promised. The docs now say Debian 13 (Trixie) or later. | Decide (section 6): build the Pi job in a `debian:bookworm` container, as `scripts/pi/docker/Dockerfile` already does, and assert the highest `GLIBC_` symbol in CI; or keep Trixie as the requirement. |
| B4 | medium, checked | `plugin/src/PluginState.cpp:113-118`, `ScopeEditor.cpp:143-147` | `waveformColour` and `window` are read from the project without clamping, and the colour then indexes an 8-element `std::array`. A corrupt project, or one saved by a later version with more colours, reads out of bounds. | Clamp both in `readFrom`, and add a `version` attribute to the instance state. |
| B5 | medium, checked | `core/include/visona/TimeSignature.h:19-27`, `PluginProcessor.cpp:165-167`, `SyncProcessor.cpp:76-78`, `BarImpulseScheduler.cpp:12-15` | Host time signatures reach integer division unvalidated. A denominator of 0 divides by zero on the audio thread (a crash on x86); a numerator of 0 makes the scheduler cast infinity to an integer. | Add `TimeSignature::isValid()` (numerator 1–64, denominator a power of two up to 32), apply it where the playhead is read, and keep the last valid value. |
| B6 | medium, checked | `core/src/BarImpulseScheduler.cpp:57-65` | When a bar line falls in the last half sample of a block, it rounds to the next block's first sample, which neither block accepts: about one bar in 1,024 at 512-frame blocks gets no impulse. | Decide the owning block from the rounded index, and test a bar line swept across a block boundary in 0.1-sample steps. |
| B7 | medium | `PluginProcessor.cpp:157-173`, `SyncProcessor.cpp:68-84` | A host that reports a tempo but no PPQ position gets a frozen PPQ marked valid: Visona Sync then writes an impulse in every block, or none, and the scope restarts its sweep every block. | Require a PPQ position for a valid playhead, or extrapolate the last one by the block length. |
| B8 | medium | `PluginProcessor.cpp:47-50` | The ring's timing slots are the capacity divided by the *maximum* block size. Hosts that send much smaller blocks run out of slots in about 15 ms and drop audio (D-062), shown as gaps. | Size the slots independently of the block size, as the app does (`capacity / 16`), with one shared helper. |
| B9 | medium | `core/src/HostAnalysisPipeline.cpp:102-108,244-258` | A time-signature change during playback keeps the old window length in ticks, so the published snapshot pairs the new time signature with the old window. | Restart the window and the peak meter when the time signature changes; add a test. |
| B10 | medium | `core/include/visona/BarPeaks.h:26`, `core/src/AutoGain.cpp:52-54` | Auto gain now reads 100 ms input windows from a ring sized for 16 bars, which holds 1.6 s. If the message thread stalls longer, a loud window is overwritten unseen and the gain can rise into clipping. | Raise the capacity to 64–128, or publish the maximum since the last read. |
| B11 | medium, checked | `plugin/src/ScopeEditor.cpp:458-459` | The plugin's diagnostics keep their elapsed time and busy counter in function-local `static`s shared by every instance, so two open editors report wrong analysis loads. | Make them members, as `MainComponent` does. |
| B12 | medium, checked | `app/src/Settings.cpp:35`, `MainComponent.cpp:386-413` | Settings save synchronously (`millisecondsBeforeSaving = 0`), and every gain step, including each auto-gain step from the vblank callback, rewrites and fsyncs the XML file on the message thread. On the Pi that is an SD-card write per step. | Save after a delay (about 1 s) and on shutdown, or persist only manual gain and the AUTO flag. |
| B13 | medium | `app/src/AudioEngine.cpp:165,436-467` | An open device that fails to start, or to reconfigure after an external sample-rate change, stays open and silent forever, and the banner says that no device is selected (Q-018). | Override `audioDeviceError`, treat an open but stopped chosen device like a stall (close and reopen), and say "The audio device stopped". |
| B14 | low, checked | `plugin/src/ScopeEditor.cpp:241-245` | The plugin's `D` shortcut ignores modifiers, so Cmd+D or Ctrl+D, Duplicate in most DAWs, toggles the overlay instead of reaching the host. | Require no modifiers, as the `V` handler does. |
| B15 | low | `SyncProcessor.cpp:45`, `SyncEditor.cpp:42` | `lastPlayhead_` is written on the audio thread and copied on the message thread without synchronisation: a data race. | Publish the few values the editor shows as relaxed atomics. |
| B16 | low | `ui/AnalysisSelector.cpp:175-180` | Picking the view that is already shown re-activates it, which clears the stereo afterglow and the statistics. | Ignore a selection equal to the current view. |
| B17 | low | `ui/StereoView.cpp:543-546`, `MainComponent.cpp:750-764` | The diagnostics show the stereo scale as 1 at 2×, lifetime maxima instead of the last second, and "0 Hz", "0 full redraws" and "0 tiles" rows that only apply to the scope. | One shared render-statistics type, with rows hidden when they do not apply. |
| B18 | low | `tests/SpectrumAnalyzerTests.cpp:288-319` | The stress test can fail if the producer finishes before the consumer thread first runs. | Read once more after the loop, or start the producer after a latch. |

### 3.2 What should be rewritten

**R1. An analysis-view interface and one shared editor workspace** (`ui/`, `app/src/MainComponent.cpp`, `plugin/src/ScopeEditor.cpp`).
D-028 promised widgets that declare a minimum size, a preferred aspect and a priority; nothing does. The views have different APIs (`setGainDb` against `setDisplayGainDb`, three statistics types, three frame-scheduling schemes), and the view identity is a bare `int` 0–3 in about 55 places across 7 files, with the view names kept in four. `MainComponent` (802 lines) and `ScopeEditor` (527 lines) share 134 identical lines in 16 blocks, and their near-copies have already diverged: the app shows `LIVE` and the plugin does not, auto gain is held and saved differently, and only the app guards the `D` key.
*Rewrite:* an `enum class AnalysisViewKind` and an `AnalysisView` interface implemented by all four views: kind, title and status label; `setActive`, `setDisplayGainDb`, `setAfterglow`, `setPaused`, `setWaveformColour`; statistics; saving and restoring state; an optional view action (Analysis settings or Reset view); `minimumSize()`, `preferredAspect()` and `priority()`. On top of it, a `ui/AnalysisWorkspace` component that owns the views, the status bar, the control bar, the zoom strip and the diagnostics, with a small host interface for what differs (transport text, pause, tempo, full screen, device information). `MainComponent` and `ScopeEditor` then keep only the device, settings and processor specifics. This is also the seam the dashboard of milestone 8 needs.

**R2. One analysis stage in the core** (`core/src/AnalysisPipeline.cpp`, `HostAnalysisPipeline.cpp`, `HostTransport.cpp`).
About 71 % of `HostAnalysisPipeline.cpp` is a copy of `AnalysisPipeline.cpp`, and the copies differ already: only the app skips the analyzers while paused, only the host caps the frames per poll, only the app has allocation tests. `HostTransport` copies `MidiClockTransport`'s span ring and adds stub members (`advanceTo`, `ignoredSppCount`) to look like it.
*Rewrite:* a shared stage that owns the sweep analyzer, the peak meters, the channel pointers and the spectrum and stereo analyzers, with `analyzeRegion()` and `fillSnapshot()`; a `TransportSpanRing` used by both transports; a C++20 `concept` for what a pipeline needs from a transport; and tests templated over both pipelines.

**R3. Delete the dead and legacy code around the analysis views.**
- `SpectrumView` keeps 16 choice boxes of which 5 are ever visible, and forces every curve setting on restore, which makes about 140 lines unreachable: the Fill style, its NEON row blend and caches, and most of the curve colours. Some UI checks loop over styles that are forced to Solid, so they test nothing.
- The core `SpectrumAnalyzer` still implements per-curve speeds, Hold, `afterglow = -1` "legacy independent envelopes" and `clearHold()`, which the UI cannot reach.
- `StereoView` keeps hidden gain and afterglow combo boxes as state storage, plus a `globalControls_` flag set as a side effect.
- The per-bar peaks (`BarPeakMeter::process`, `snapshot.barPeaks`) are computed and copied into every snapshot but read only by tests since D-119. `BarPeakMeter::processInput` reuses the bar machinery through a fake transport span measured in seconds.
- `SweepAnalyzer`'s frame-based mode is used only by tests, and the channel-count and allocation guarantees are tested in that mode rather than the musical one.

*Rewrite:* keep five settings and one envelope model in the spectrum, an `int afterglow_` in the stereo view, and an `InputPeakMeter` with its own window counter; port the frame-mode tests to the musical mode and delete the rest. Keep reading the old saved formats in one migration function.

**R4. Versioned, keyed settings, with codecs in the core.**
The spectrum's settings are 16 positional integers, 13 of them dead or derived, parsed all-or-nothing on the field count, with list positions as values; the stereo settings are 2–4 positional fields with migration by count. One added field silently drops every saved string, and older builds drop newer ones.
*Rewrite:* one keyed, versioned element per view (`<Spectrum version="2" channel="mid" rangeDb="90" .../>`), enum names rather than list indices, each attribute read independently with a default, and the mapping to and from key/value pairs in JUCE-free core code, so that round trips, migration and malformed input are Catch2 tests on Linux CI. The app settings get a `settingsVersion`, one block of keys and a backup of an unreadable file instead of silently starting over.

**R5. Small shared plugin pieces.** One `HostPlayheadReader` for both processors, with the validation of B5 and the PPQ rule of B7 (today two identical copies with dead members); one polling analysis thread for the app and the plugin (two near-copies with different timeouts and names); one ring-sizing helper.

**R6. A testable device follower.** `AudioEngine` (546 lines) holds device restore, the D-102 follower (polling, stall watchdog, waiting and retrying), MIDI reconnects, input routing, the stream and about ten forwarders. The follower's state machine is the most stateful code in the app and cannot be tested apart from JUCE. Extract it behind a narrow interface (`isListed`, `open`, `close`, `framesDelivered`) and test it with a fake.

### 3.3 Performance and design to review

**Performance on the Raspberry Pi**

- **P1. Hidden views do full work.** Gain, colour and size changes fan out to all four views, so with auto gain on in Scope every 3 dB step renders three invisible views. Record the value and render on activation instead (simple once R1 exists).
- **P2. The stereo analyzer recomputes the whole 200 ms window 120 times a second**, and the LSP meters zero buffers sized for 384 kHz on every publish, about 1.2 MB each time. Only the final correlation and balance are used, and they follow from the sums of `ll`, `rr` and `lr`, which the gate already computes in double precision. Keeping exact per-hop partial sums would remove almost all of that work, and possibly the LSP dependency (section 6).
- **P3. Spectrum (FFT) in stereo-average mode runs two 8,192-point complex FFTs on real input**, and about 1,500 loop-invariant `pow` calls per hop. Packing left and right into one complex FFT, or using `kiss_fftr`, halves the FFT cost; the tables can be computed when the settings change.
- **P4. The band filter bank** runs 124 double-precision biquad steps per frame, bands innermost, with two denormal branches per step. Looping frames inside bands, setting flush-to-zero once on the analysis thread, and decimating the low octaves would cut it several times.
- **P5. Polar Level is aggregated on the message thread**, with an `atan2` per history sample per snapshot. It belongs on the analysis thread, which already holds the history.
- **P6. The plugin analysis runs in every instance with the editor closed**, waking 333 times a second and analysing the sweep that nobody sees. Drain only, at a slower rate, while no editor is open.
- **P7. Message-thread allocation.** `StereoView::setDisplayGainDb` allocates a vector and copies a full-size image per 1 dB step; `SpectrumView` builds two 768-point paths and gradients per frame. Reuse preallocated members, as `StereoView`'s Polar Level paths already do.

On the VM, each of the stereo, spectrum and band analyzers cost about three times the whole sweep pipeline. Measure on the Pi for 30 seconds after each change (AGENTS.md).

**Design**

- **D1. The new analyzers take exactly `left` and `right`**, against the rule that sources have 1..N channels (D-049, D-052). A mono source is fed as left = right, so Side reads silent and correlation +1. Take `std::span<const float* const>` as `SweepAnalyzer` does, and give Stereo an explicit "not stereo" state.
- **D2. Ownership and thread contracts.** `BandAnalyzer` lives inside `SpectrumAnalyzer`, and the UI reaches the analyzers through mutable accessors on the pipeline; which methods are safe from which thread is documented only for the sweep. Make the band analyzer a sibling, expose narrow control and snapshot interfaces, and document the thread of every public method.
- **D3. Presentation fields in the core.** `SpectrumSettings` mixes analysis fields with style, colour, top, range and slope. Split it into analysis settings in the core and a style in `ui/`.
- **D4. Fixed pixel layouts (D-026).** `ControlBar` gives the view selector, Afterglow and the view button fixed widths of 190, 170 and 156 logical pixels at every step, so at the app's 320-pixel minimum the bar takes four rows in Spectrum (FFT) and Stereo, and in kiosk mode those widths lower the touch scale in every view. The analysis views lay out their titles, margins and readouts in fixed pixels, and below about 260 pixels of height the stereo readouts overlap its buttons. Derive the widths from the text, and the layouts from the bounds and the chrome step.
- **D5. Style drift.** The new files have 0–3 % blank lines (9–15 % in the older ones), no blank line between functions, one doc comment per header (13–23 in `SweepAnalyzer.h` and `MidiClockTransport.h`), inline magic numbers, raw `int` modes instead of enum classes, and colours outside the palette. clang-format does not catch any of it.
- **D6. Inconsistent status and pause feedback.** The app's `LIVE` state (now documented) has no plugin counterpart; the gain is left out of the status bar outside Scope although it affects those views; Scope and Stereo show that they are paused, the spectra do not.
- **D7. Host integration in the plugin.** Editor changes never call `updateHostDisplay`, so hosts do not mark the project as changed; a state load from undo or a preset does not reach an open editor, whose next action writes the stale state back; the editor size is not saved; the plugin accepts only a stereo main bus.
- **D8. Small inconsistencies.** The app does not save the sweep window, the plugin does. Global plugin defaults are read but never written (now documented). `IdleCursorHider` keeps calling `hideCursor()` every 3 s. The banner glues raw JUCE and ALSA errors to "Choose a device in Settings."

### 3.4 Build, CI and release

- **C1 (high). Pull-request CI compiles no JUCE code**, no MSVC and no ARM64. The release workflow is the first build of the app, the plugins and `ui/`. A Linux job building the app, both plugins and the UI checks took about 8 minutes on 3 cores here without a cache; with `ccache` it would be a few minutes.
- **C2 (medium). Tests that run nowhere:** `visona_ui_checks` (it passes headless on Linux), `tests/scripts/test_pi_deploy.py`, the Pi probes in `scripts/pi/*.cpp`, and a site build. Register them with CTest or CI.
- **C3 (medium). No format or lint check.** Six files are not formatted (`core/src/BarImpulseScheduler.cpp`, `tests/BarImpulseSchedulerTests.cpp`, `plugin/src/SyncProcessor.cpp`, `plugin/src/SyncEditor.cpp`, `app/src/IdleCursorHider.h`, and `app/src/MacWindow.mm`, which `.clang-format` cannot format because it has no Objective-C section). The clang-format version is not pinned.
- **C4 (medium). Warnings.** App, UI and plugin code builds only with JUCE's recommended warnings, never as errors. GCC 13 reports 7 warnings in Visona code (`-Wfloat-equal` in `ChromeButton`, `GainControl`, `TempoControl` and `StereoUiChecks`, `-Wrange-loop-construct` in `ChromeButton`, and a probable false-positive `-Warray-bounds` inlined from `ScopeView`), plus `-Woverloaded-virtual` because `PluginProcessor` hides the `processBlock(AudioBuffer<double>&)` overload.
- **C5 (high). JUCE is pinned to 9.0.2.** 9.0.4 (2026-10-12) adds protection against malformed `String` input, which project state and settings pass through, and 9.0.3 fixes Linux ALSA sample-rate and channel issues. clap-juce-extensions is three commits behind a fix for transport in split blocks. Nothing proposes updates (no Dependabot or Renovate).
- **C6 (high). Release security.** The validators (`pluginval`, `clap-validator`) are downloaded from mutable release assets and run without a checksum, on macOS after the Developer ID key has been imported into an unlocked keychain, and every job has a `contents: write` token. The p12 is imported without `-x`; `ilammy/msvc-dev-cmd` is referenced by a mutable tag; the notarization password is passed on the command line.
- **C7 (high). Release artefacts.** No archive contains `LICENSE` or third-party notices, although KISS FFT's BSD licence requires its notice with binaries and the AGPL its text. The binaries report `1.0.0` for `v1.0.0-beta.1` and alpha builds all reported `0.1.0`, so bug reports cannot tell builds apart. The Pi binary is not stripped and the tarball carries developer scripts. The Linux plugins export about 150 internal `visona::` symbols, and will export `kiss_fft_*`, inviting symbol clashes in hosts.
- **C8 (low). Duplication and structure.** The Pi build and packaging are written twice (Release and Pi quick build), packaging is hand-written in four shells with no `install()` rules or CPack, LTO is off for the x64 plugins for a reason that applies only to the Pi, and the release builds copy plugins into the runners' plugin folders.
- **C9 (low). Workflow behaviour.** CI's `cancel-in-progress` also cancels runs on `main`, the Pages deploy cancels deploys in progress, and the site is rebuilt after every Release run, including failed and development ones. `site/build.py` can publish a release whose uploads are incomplete, and deletes whatever `--out` points at.
- **C10 (medium). Pi scripts.** `install-visona.sh` runs from `main` through `curl | bash` without a `main` wrapper, stops Visona before downloading, and does not verify the tarball, although the releases API gives its SHA-256. `install-kiosk.sh` edits the system's `/etc/xdg/labwc/autostart` and has no uninstall. `scripts/macos/build-and-install.sh` checks out `main` and pulls unless told not to.

### 3.5 Tests

- **T1.** The allocation tests run the pipelines with the spectrum, band and stereo analyzers disabled, and `AllocationCounter` counts only C++ `new`, not the `malloc` that KISS FFT and LSP use. The analysis thread's allocation-free guarantee is therefore unverified for the new analyzers.
- **T2.** `HostAnalysisPipeline` has 8 cases, with no allocation, band-splitting, time-signature or queue-capacity tests; `BarImpulseScheduler` has 3, all with the bar line at offset 0; `TimeSignature` has none.
- **T3.** `AnalysisPipelineTests.cpp` (1,168 lines), `SweepAnalyzerTests.cpp` and `MidiClockTransportTests.cpp` should be split by topic, with shared feeding helpers in `tests/support/`. Label the `[stress]` cases for CTest and repeat them under TSan.
- **T4.** The UI checks reach into private members through `friend` declarations in production headers, and some drive hidden widgets or forced settings (R3). Moving the pure logic of the views (axis maths, settings codecs, band geometry, stereo display maths, control-bar placement, status text) into the core would test it on every PR.

### 3.6 Licensing and third-party code

- **L1 (high).** As C7: no licence text or notices ship with any binary. The binaries combine JUCE (AGPL-3.0-only or commercial), JUCE's bundled libraries (HarfBuzz, SheenBidi, libpng, the IJG JPEG library, zlib, LunaSVG/PlutoVG), the VST3 SDK and CLAP (MIT), KISS FFT (BSD-3-Clause), LSP (LGPL-3.0-or-later) and Signalizer-derived code (GPL-3.0-or-later). Because JUCE is AGPL-3.0-only, the shipped binaries are effectively AGPL-3.0-only even though Visona's own code is "-or-later". Generate a `THIRD_PARTY_NOTICES` file, ship it with `LICENSE` in every archive, point to it from the manual, and attach a complete source archive, including the fetched dependencies, to each release.
- **L2 (low).** `third_party/signalizer/COPYING` is a reflowed Latin-1 copy of the GPL, duplicated in `experiments/`; use the canonical text.
- **L3 (low).** The LSP binding includes private upstream headers; re-run the oracle tests on every pin change, or retire LSP (P2).
- **L4 (low).** `experiments/stereo-analysis` duplicates production code and builds only on macOS with GNU Make 4. Mark it as a frozen record of the commit it matched, or remove it once the oracle tests cover it.

### 3.7 Repository and process

- **H1.** 48 remote branches besides `main`: 25 are merged, about 20 more belong to squash-merged PRs, and 3 to closed PRs. "Automatically delete head branches" is off.
- **H2.** The CHANGELOG has no sections for the published 0.1.0-alpha.3 and alpha.4; `[Unreleased]` now covers #48 and #49 (this PR).
- **H3.** No `SECURITY.md`, `CONTRIBUTING.md`, PR template, Dependabot or Renovate configuration, or ruleset requiring CI on `main`. A security policy matters here, since the project ships Developer ID-signed binaries and a `curl | bash` installer.
- **H4.** The analysis docs had become development logs with contradicting sections, the site advertised unreleased views, and the screenshots in `docs/images/` still show the control bar from before 1.0.0-beta.1 (labelled buttons, segmented WINDOW and WAVE, no pause, AUTO or view selector). This PR fixes the text; the screenshots need retaking on a Mac with real audio.
- **H5.** The Pi appliance has no supervised restart, no crash handler and no file log: one crash leaves the touchscreen on an empty desktop until someone reboots, and nothing records why.

## 4. Plan

Each item is meant to be one pull request that can be merged on its own, in the order given within a phase (the working agreements forbid stacked PRs). Phases 1 and 2 can run in parallel.

### Phase 1: correctness, before the next release

1. **Plugin lifetime, state and shortcuts:** B1, B4, B11, B14 and B15. Acceptance: a test or a reasoned review for each; clamped values covered by a unit test of `PluginInstanceState` or its core equivalent.
2. **Visona Sync:** B2 and B6, with the allocation counter wrapped around both `processBlock`s in Debug plugin builds. Acceptance: a scheduler test sweeping the bar line across a block boundary; zero allocations counted in a Debug run.
3. **Host playhead validation:** B5, B7 and B9 through one `HostPlayheadReader` (R5) and `TimeSignature::isValid()`. Acceptance: core tests for 0/4, 4/0, 5/64 and a missing PPQ.
4. **Plugin ring sizing:** B8, sharing the app's sizing helper.
5. **Auto gain robustness and settings writes:** B10 and B12. Acceptance: no settings write per auto-gain step; a test that a late reader still sees the loudest window.
6. **App device recovery:** B13, and wrapping raw device errors in a sentence (D8).
7. **Formatting and a flaky test:** reformat the six files, add an Objective-C section to `.clang-format`, and fix B18.

### Phase 2: a safety net in CI

1. A pull-request job on `ubuntu-24.04` with `ccache`: configure `linux-plugin` with `-DVISONA_BUILD_APP=ON -DVISONA_BUILD_UI_CHECKS=ON -DVISONA_COPY_PLUGINS=OFF`, build the app, both plugins and `visona_ui_checks`, run the UI checks through CTest and upload their PNGs as an artifact.
2. Run `python3 -m unittest discover -s tests/scripts` and a site build from a committed releases fixture, with a link check.
3. A lint job: a pinned clang-format with `--dry-run --Werror`, shellcheck and actionlint.
4. Strict warnings for Visona's own app, UI and plugin sources, as errors in the CI job (C4).
5. `cancel-in-progress` only for pull requests; no cancelling of Pages deploys.
6. Optionally, a `windows-2025` job for the core and tests only, and a scheduled macOS build.

### Phase 3: consolidate the analysis views, then release them

1. **Remove dead and legacy code** (R3), keeping the old saved formats readable in one migration function. Fix the UI checks that test forced settings.
2. **Versioned settings codecs in the core** (R4), with Catch2 tests for round trips, migration and malformed input.
3. **The `AnalysisView` interface and the shared workspace** (R1), which also fixes P1, B16, B17 and D6, and decides the status text in one place.
4. **Layout from bounds** (D4): control-bar widths from the text, analysis-view layouts from the bounds and the chrome step, and the view's `minimumSize()`.
5. **Retake the screenshots** on a Mac with real audio, and add Spectrum and Stereo images to the site.
6. **Release 1.0.0-beta.2** with the analysis views.

### Phase 4: the core and the Pi's CPU

1. **One analysis stage** for both pipelines, a shared `TransportSpanRing` and a transport `concept` (R2); tests templated over both pipelines, including allocation tests with every analyzer enabled (T1, T2).
2. **Channel-agnostic analyzers** (D1) and a clear ownership and thread contract for them (D2, D3).
3. **Analyzer performance:** stereo partial sums (P2), one FFT for stereo average and precomputed tables (P3), the band filter loop order, flush-to-zero and decimation (P4), Polar Level on the analysis thread (P5), and the plugin analysis idling while no editor is open (P6). Measure each on the Pi for 30 seconds before and after.
4. **The device follower** as its own tested class (R6).

### Phase 5: release and supply chain

1. **Notices and licences:** `THIRD_PARTY_NOTICES`, `LICENSE` in every archive, a source archive with the fetched dependencies, and the AGPL-3.0-only combination stated in the README (L1).
2. **Versions:** a version suffix or build ID from the tag in `versionString()`, the diagnostics and the bundles; a release step that checks the tag against `PROJECT_VERSION`; the missing CHANGELOG sections (C7, H2).
3. **The Pi build environment** chosen in section 6, with a glibc assertion in CI, a stripped binary and an explicit file list for the tarball (B3, C7).
4. **Dependencies:** JUCE 9.0.4, an evaluated clap-juce-extensions bump, and Dependabot for actions plus Renovate for the `FetchContent` pins (C5).
5. **Release hardening:** checksummed validators, validation before the certificate import, `security import -x`, `persist-credentials: false`, read-only build jobs with a final upload job, an App Store Connect API key for notarization (C6).
6. **Hidden symbol visibility** for the core and the plugins, with a CI check of the exported symbols (C7).
7. **Install scripts:** a `main` wrapper, digest verification and download-before-stop in `install-visona.sh`; an uninstall and a user-level panel override in `install-kiosk.sh` (C10).

### Phase 6: appliance and longer term

1. **A self-healing Pi:** run the kiosk under a `systemd --user` unit with `Restart=on-failure`, install a crash handler that writes a backtrace, and add the rotating file log of step 8 (H5, Q-020).
2. **Plugin host integration** (D7): mark the project changed, reflect state loads in an open editor, save the editor size, and decide on mono buses.
3. **Repository hygiene:** delete merged branches and turn on automatic deletion, add `SECURITY.md`, a PR template mirroring AGENTS.md's checklist, and a ruleset requiring CI (H1, H3).
4. **The dashboard of milestone 8**, built on the `AnalysisView` interface of phase 3, rather than adding more single views first.

## 5. General recommendations

- **Let CI be the reference, not one Mac.** Most of the defects above would have been caught by compiling and running what already exists (the JUCE targets, the UI checks, the allocation counter in the plugins) on every pull request.
- **Do not add more analysis views until the interface of phase 3 exists.** Each view added before it multiplies the wiring in both editors.
- **Finish refactors by deleting.** The pattern behind R3 is old controls hidden instead of removed, and old settings fields kept positionally. Keep a single migration function per format and delete the rest.
- **Write the house style into AGENTS.md.** The older code follows clear conventions that the newer code does not: a doc comment on every public member, named constants with units, blank lines between functions, enum classes instead of integer modes, colours from `Palette.h`, settings codecs in the core. Agents follow what is written down.
- **Keep the docs as current behaviour first, history second.** Development measurements belong in a dated history section; contradicting paragraphs should be replaced, not appended to.
- **Give the Pi a CPU budget per view** (for example, the whole app within one core at 1920×1080 and 48 kHz), and check it with the 30-second measurement after every change to a view.
- **Treat the real-time rules as tests.** The allocation counter should cover every audio-thread entry point, in the app and in the plugins, and the core tests should run every analyzer under it.
- **Release in smaller steps.** Each published release should match its CHANGELOG section, and the site should only advertise what can be downloaded.

## 6. Decisions needed from the maintainer

1. **Which Raspberry Pi OS should the release support?** Building in a Bookworm container keeps Bookworm and Trixie; keeping the current runner means Trixie or later (what the docs now say).
2. **Release the analysis views before or after phase 3?** The plan proposes after, as 1.0.0-beta.2.
3. **Keep `LIVE`** in the app's status bar outside Scope (now documented), or show the transport state in every view, as the plugin does?
4. **Replace LSP** with exact partial sums in the stereo analyzer (P2), keeping the oracle tests, or keep LSP for provenance?
5. **Plugin global defaults:** add a "Make default" action, or remove the unused defaults file?
6. **Save the app's sweep window**, as the plugin does?
7. **Repository settings** that need an administrator: automatic branch deletion, the stale branches, a ruleset requiring CI, and private vulnerability reporting.
8. **Release-workflow changes** that touch the signing secrets (C6), which should be made and tested together with a manual run.
