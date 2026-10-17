# Spectrum (FFT) analysis

The Spectrum view is shared by the standalone app and Visona effect plugin. The Scope
view retains its existing controls. A fixed analysis selector opens a compact desktop list
or a large in-window tile panel on touch/kiosk. More analysis views can be added to this
selector independently of future split-screen layouts. Only one view is displayed today.

## Controls

- **V** opens the view selector. **Alt+1 / Alt+2** select Scope / Spectrum while the editor
  has focus. Scope's existing **1–5** window shortcuts are unchanged.
- **Analysis settings** opens a scrollable in-window panel, also usable by touch.
- Channel modes are Stereo average, Left, Right, Mid `(L+R)/2`, and Side `(L-R)/2`.
  Stereo average combines the two channels' spectral powers; opposite-phase signals do
  not cancel. Mid and Side use amplitude sums/differences and can cancel.
- One shared Detector (Peak or RMS) and Smoothing control apply to both curves.
- Curve 1 is always visible, Fast (150 ms), Solid, in the waveform colour.
- Curve 2 is a white line with immediate attack and no hold. The shared Afterglow
  duration is the time to fall 12 dB: 0.5 s gives 24 dB/s, 1 s gives 12 dB/s,
  and 2 s gives 6 dB/s. Off hides it. For RMS the line uses each
  FFT frame's calibrated RMS amplitude before its history envelope, rather than
  a second power average. FFT window smoothing still applies.
- Global Gain replaces Top and moves the vertical viewport; labels and cursor
  values retain signal levels. Range and Slope remain independent. Slope is
  0–6 dB/octave around 1 kHz, default 3; default range is 90 dB.
- Per-curve visibility, response, style and colour selectors and Reset peaks
  have been removed. Older saved per-curve settings migrate to this fixed pair,
  retaining curve 1's detector and smoothing, channels, range and slope.
- The shared footer displays Gain/Auto in every view and Afterglow except Scope.
  Band uses Afterglow for marker hold time before the existing 12 dB/s release;
  Off hides its markers. Gain moves bars and markers with correctly labelled axes.
- Standalone Pause also pauses spectrum analysis. The spectrum otherwise follows incoming
  audio regardless of MIDI Stop or clock loss. It needs no musical clock.

The default is a fast Solid peak curve in the waveform colour and a slow white peak line,
both with 1/12-octave smoothing. The app saves the selected view and settings; the plugin
saves them per instance in its host state. Existing saved projects open in Scope.

## Measurement definition

The analyzer uses an 8,192-sample periodic Hann window and a 1,024-sample
hop. KISS FFT 131.1.0 (BSD-3-Clause, vendored unmodified in `third_party/kissfft`) supplies
the float FFT behind `SpectrumAnalyzer`; neither JUCE nor platform DSP APIs enter core.
FFT plans, rings, envelopes, scratch memory and triple-buffer snapshots are allocated
when the analyzer is constructed. Processing, stream resets and settings changes do
not allocate, lock or perform I/O. The audio callback is unchanged.

Magnitude calibration compensates Hann coherent gain: a bin-centred full-scale sine
has 0 dB peak before frequency smoothing and slope. RMS exponentially averages spectral
power and reads 3.0103 dB below Peak for a steady sine. Peak attacks immediately and its
amplitude decays exponentially with the selected time constant; Hold retains maxima
until reset. RMS uses the time constant for both rising and falling power.

Fractional-octave smoothing averages power over a symmetric logarithmic band. A narrow
tone therefore reads lower with wider smoothing. These are smoothed, tone-calibrated
FFT magnitudes, not integrated octave-band SPL, broadband RMS, LUFS or a noise-density
measurement. Raw display cells retain the maximum FFT bin where several bins fit one
logarithmic display cell; interpolation handles cells narrower than an FFT bin.

Snapshots contain 768 logarithmically spaced points from 20 Hz to the lower of 20 kHz
and Nyquist. Bin spacing is sample rate / 8,192: 5.86 Hz at 48 kHz and 23.44 Hz at
192 kHz. The first complete window takes 171 ms at 48 kHz (43 ms at 192 kHz); the hop
is 21.3 ms at 48 kHz. Windowing imposes temporal smearing as well as the chosen envelope.
Dropped input and stream changes restart the window and envelopes. Detector/smoothing
changes reset the affected envelope; response changes preserve it.
Invalid input samples are replaced with silence. Spectrum processing is disabled in Scope
and when the plugin editor closes. Scope analysis continues to maintain transport state.

Spectrum rendering caches the static grid and labels, blends full-resolution gradients in
contiguous rows (using ARM64 SIMD where available), and caches the faint tail below the
first curve. Curve detail, colours, glow strokes and FFT timing are unchanged. Only the
region occupied by the previous and current curves is restored and repainted; a visible
measurement cursor still requests a full repaint. Rendering pauses behind the opaque
Spectrum settings panel.

The refresh target is 30 Hz, or 25 Hz on ARM64 Linux when curve 1 uses Solid. Slower frames budget both curve rendering and painting to
leave headroom on the message thread for input, status updates and
window events instead of queuing overdue timer callbacks. Diagnostics reports Spectrum's
actual rendered frames per second and average/maximum render and paint costs when
Spectrum is selected.
These are generated frames, not a measurement of the display's physical refresh rate.

The offline UI executable accepts `--benchmark` for a reproducible 1920×1080 synthetic
spectrum test of Line, Fill and Solid. Incremental rendering is checked pixel-for-pixel
against a full redraw after large level changes, with both curves and each style.

## Validation

Core tests cover tone frequency/level at 44.1/48/96/192 kHz, RMS/Peak calibration,
phase-safe stereo averaging, Mid/Side cancellation, high-frequency peak retention,
hold/reset, input gaps, invalid samples, block partitioning, settings validation,
allocation checks, concurrent settings/snapshot stress and app/plugin pipeline integration. Existing scope tests remain active.

Optional offline UI checks generate desktop, 800×480 touch, and 640×480 screenshots
from synthetic audio without opening audio devices or changing application preferences:

```sh
cmake --preset macos -DVISONA_COPY_PLUGINS=OFF -DVISONA_BUILD_UI_CHECKS=ON
cmake --build --preset macos
build/macos/ui/visona_ui_checks_artefacts/Release/visona_ui_checks /tmp/visona-ui-checks
ctest --preset macos
```

No split-screen editor, reference capture, extra weighting standards or auditory masking
is included. The spectrum takes visual inspiration from TDR Prism; its measurement
algorithms and time constants are defined above, not claimed to match Prism.

### Local verification (2026-10-16)

- macOS Release: standalone app and both plugins in VST3, AU and CLAP formats build.
  Plugin copying was disabled; no installed plugins were replaced.
- 234 core tests pass in Release and with ASan/UBSan.
- Offline UI checks cover settings round trips, repeated view changes, desktop/touch
  selectors and layouts at 1280×720, 800×480 and 640×480, including Retina rendering.
- macOS TSan cannot enumerate tests: the existing allocation hook in
  `tests/support/AllocationCounter.cpp` recursively initializes thread-local storage
  through dyld/malloc and overflows the stack before test execution. Linux TSan remains
  a CI check; it has not been run locally.
- Physical Pi touch input, Windows/Linux binaries and playback inside a DAW have not
  been exercised on this Mac. Synthetic-signal and offscreen UI tests do not replace
  those hardware/host checks.

### Pi rendering validation (2026-10-17)

- Raspberry Pi 4 Model B, 1 GB RAM, Debian 13 desktop; no thermal throttling reported.
- Rendering retains full display resolution, both curves, fractional-octave detail and glow.
- The initial live UI thread used about 78% of one core; a five-second sample after the
  final deployment measured 62%, with 28% in the analysis thread and about 93% for the
  whole app. These are live observations, not a controlled audio benchmark.
- macOS standalone/VST3 and Linux ARM64 standalone builds pass. Offline UI checks pass,
  including pixel-exact incremental/full redraw comparisons after rising and falling
  levels with both curves, all styles, colour changes and an odd-width viewport.

Opt-in SSH telemetry (`VISONA_PERF_LOG=1`, documented in `pi-build.md`) subsequently
showed the conservative timer delivering about 18 FPS / 76% process CPU with the
overlay closed. A shorter timer interval and elimination of redundant background
painting improved throughput to approximately 24–25 FPS, but process CPU remained
around 110%. This improves motion, not total CPU efficiency; further optimization is
still needed. These are live audio observations, not identical-input benchmarks.
Diagnostics now refreshes at 4 Hz in the standalone app to reduce measurement overhead.


### Solid renderer and shared analysis (2026-10-17)

A deterministic 1920×1080 benchmark on the Pi 4 measured 7.19 ms for Line,
11.86 ms for Fill, 17.99 ms for Glow and 9.77 ms for Solid (30 frames per style,
identical synthetic input, main app suspended during the benchmark). Solid preserves
all 768 curve points, anti-aliasing, the frequency grid and the second trace. The
renderer uses one flat path fill and one thin outline instead of per-pixel fades
and additional glow strokes. The desktop default is now Solid; Glow has been removed.

The analyzer now shares frequency sampling when both traces use the same smoothing;
their Peak/RMS envelopes remain independent. FFT size, hop, calibration and response
times are unchanged. Tests compare the shared path with independently sampled traces
across every smoothing setting and both detectors.

Live measurements with Diagnostics closed showed approximately 25 FPS / 112% CPU
with Glow before these changes. Solid alone ran at 29 FPS / 111% CPU, spending its
savings on extra frames. With shared analysis and a 25 Hz Solid budget on ARM64 Linux,
CPU fell to approximately 80–85% while retaining 25 FPS. These live measurements use
incoming audio and are not identical-input CPU benchmarks; the isolated renderer
benchmark above provides the controlled style comparison. CPU percentages represent
one full core at 100%. Solid remains an explicit per-curve style selection, persisted
in settings, with shared Waveform colour. Glow has since been removed; saved Glow styles migrate to Solid.


### Curve-settings verification (2026-10-17)

Fixed a shared-history reset: changing either trace previously cleared both envelopes
and refilled the FFT window. Detector/smoothing changes now reset only the affected
trace. Response changes preserve its current envelope, including entering Hold.
Channel changes and Reset peaks clear both envelopes; stream discontinuities still
clear the FFT ring. RMS response menus omit the unsupported Peak Hold option.

Verification exercises every dropdown option through the UI callback, checks persisted
values and publication of the matching analysis key, and checks restoration. Signal
tests verify both traces' Peak/RMS decay constants at 44.1, 48 and 96 kHz, RMS attack,
Peak attack, Hold independence, reset and live response changes without an FFT refill.
Existing tests cover calibrated levels, channel selection, smoothing reuse, raw peaks,
stream gaps, thread safety and allocation-free processing. The settings panel deliberately
suspends covered rendering, so visual comparisons should be made after closing it.


### FFT window comparison on Pi (2026-10-17)

Selected 8,192 samples as the new default. The hop remains 1,024 samples, and display
rate, 1/12-octave smoothing and Peak Fast response were held constant. Both 4,096 and
8,192 variants were installed and measured on the Pi. The deterministic ARM64 probe
in `scripts/pi/spectrum-window-probe.cpp` compares a 1 kHz onset after silence and
60+70 / 60+80 Hz pairs, using identical 48 kHz input for each build.

| FFT samples | Bin spacing | Window length | Onset to 50% amplitude | Onset to 90% amplitude | 60+80 Hz valley depth |
| --- | --- | --- | --- | --- | --- |
| 4,096 | 11.72 Hz | 85.3 ms | 42.7 ms | 64.0 ms | 0.5 dB |
| 8,192 | 5.86 Hz | 170.7 ms | 85.3 ms | 128.0 ms | 12.5 dB |
| 16,384 | 2.93 Hz | 341.3 ms | 149.3 ms | 234.7 ms | 34.1 dB |

Onset is measured in audio sample time at published analysis frames, relative to the
steady-state amplitude. It excludes UI scheduling, audio-device latency and display
scanout. Valley depth is relative to the weaker of the two local peaks. The 4,096
window barely separates the 60+80 Hz pair; 8,192 separates it clearly. Neither shorter
window separates the 60+70 Hz pair in this test; 16,384 does (8.2 dB valley). This is a
real loss of close bass detail, not something fractional-octave smoothing can restore.

The probe writes CSV spectra. Compile it with `core/src/SpectrumAnalyzer.cpp` and
`third_party/kissfft/kiss_fft.c`, with the core and KISS FFT include directories. For
an isolated comparison, use a temporary copy of `visona/SpectrumAnalyzer.h` with each
window constant and place that include directory first; compile the probe and analyzer
against the same header. Keep the installed app measurements separate from probe runs.

With Diagnostics closed and the same Solid display settings, live app averages were:

| FFT samples | FPS | Process CPU | Analysis CPU (included in process) |
| --- | --- | --- | --- |
| 4,096 | 24.93 | 71.34% | 9.41% |
| 8,192 | 24.91 | 73.56% | 13.80% |
| 16,384 | 24.88 | 85.06% | 26.38% |

These are 20–25 second live-audio windows, not identical-input CPU benchmarks; 100%
represents one full CPU core. The initial 16,384 run had Diagnostics open and was
excluded. The selected 8,192 setting retains frame rate, roughly halves analysis CPU
and reduces overall CPU by about 14% relative to the matched 16,384 run. It trades some
close bass separation for a substantially quicker onset. 240 core tests and the UI
checks passed with 8,192, including detector calibration and response-time checks.


Spectrum (FFT) is retained as view ID 1. Spectrum (Band) uses stable view ID 2;
Scope remains ID 0. Existing saved view selections are preserved. Both spectrum views
now have black backgrounds. See `band-spectrum.md` for the streaming filter bank.


## Global controls migration

The global Afterglow index is persisted separately in app settings and plugin
state (Off, 0.5 s, 1 s, 2 s; default 1 s). The standalone also remembers shared
display gain; plugins retain their existing gain/Auto state. Legacy stereo gain
and persistence no longer override these global controls. Existing spectrum
analysis settings remain readable, but fixed presentation and shared detection
are enforced by the view. Legacy core envelope modes remain for compatibility
and regression tests; the simplified UI uses the global history envelope.

Gain/Auto uses transport-independent 100 ms input peak windows, which the hidden Scope
view continues consuming without rendering. The existing 10 s hold policy and
3 dB steps remain; the detector runs even when musical transport is stopped. Switching analysis views does not reset
Auto or change its input. No allocation or drawing was added to the audio callback.
The 150 ms filled response and global FFT history release are separate behaviors.
Historical performance and curve-setting notes above describe earlier controls.
