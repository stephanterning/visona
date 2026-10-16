# Spectrum analysis

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
- Two independently visible curves offer Peak or RMS, Fast (150 ms), Medium (500 ms),
  Slow (1.5 s), and Peak Hold. RMS cannot use Hold; it falls back to Slow.
- Frequency smoothing is Raw, 1/24, 1/12, 1/6, 1/3, or 1 octave. This is separate from
  the response time and does not increase FFT resolution.
- Styles are Line, Fill and Glow, in Spectrum, White, Blue or Orange.
- Top, Range and Slope affect presentation only. Slope is 0–6 dB/octave, pivoting at
  1 kHz. The default is +3 dB/octave, 0 dB top and 90 dB range.
- Move the pointer, or touch and drag, to inspect frequency and both curve levels.
  Readouts include the selected slope, explicitly labelled when nonzero.
- Reset peaks clears the analysis history, including held peaks.
- Standalone Pause also pauses spectrum analysis. The spectrum otherwise follows incoming
  audio regardless of MIDI Stop or clock loss. It needs no musical clock.

The default is a fast, frequency-coloured Glow peak curve and a slow white peak line,
both with 1/12-octave smoothing. The app saves the selected view and settings; the plugin
saves them per instance in its host state. Existing saved projects open in Scope.

## Measurement definition

The first implementation uses a 16,384-sample periodic Hann window and a 1,024-sample
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
and Nyquist. Bin spacing is sample rate / 16,384: 2.93 Hz at 48 kHz and 11.72 Hz at
192 kHz. The first complete window takes 341 ms at 48 kHz (85 ms at 192 kHz); the hop
is 21.3 ms at 48 kHz. Windowing imposes temporal smearing as well as the chosen envelope.
Settings changes, dropped input and stream changes restart the window and envelopes.
Invalid input samples are replaced with silence. Spectrum processing is disabled in Scope
and when the plugin editor closes. Scope analysis continues to maintain transport state.

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
