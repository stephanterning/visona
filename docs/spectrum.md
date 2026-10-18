# Spectrum (FFT)

Spectrum (FFT) is one of the analysis views of the standalone app and the Visona effect plugin (D-116, D-117). Spectrum (Band) is described in [band-spectrum.md](band-spectrum.md) and Stereo in [stereo.md](stereo.md). Only one view is shown at a time; split-screen layouts are future work.

## Controls

- **V** opens the view selector: a compact list with a mouse, large tiles in kiosk mode or after a touch. **Alt+1** to **Alt+4** choose Scope, Spectrum (FFT), Spectrum (Band) and Stereo while the app or the plugin editor has focus. Scope's **1–5** window shortcuts work only in Scope.
- **Analysis settings**, in the control bar, opens a scrollable in-window panel that also works by touch. **Done** or **Esc** closes it. While it covers the plot, the curves are not redrawn.
  - **Channels:** Stereo average, Left, Right, Mid `(L+R)/2` or Side `(L-R)/2`. Stereo average combines the two channels' spectral powers, so opposite-phase signals do not cancel. Mid and Side are amplitude sums and differences and can cancel.
  - **Range:** 30, 60, 90 (the default) or 120 dB.
  - **Slope:** None, or 1 to 6 dB/octave around 1 kHz, 3 by default. It is presentation only.
  - **Detector:** Peak or RMS, and **Smoothing:** Raw, 1/24, 1/12 (the default), 1/6, 1/3 or 1 octave. Both apply to both curves.
- **Curve 1** is a Fast (150 ms) Solid fill in the waveform colour.
- **Curve 2** is a white line with immediate attack and no hold. **Afterglow**, in the control bar, sets the time it takes to fall 12 dB: 0.5 s gives 24 dB/s, 1 s 12 dB/s and 2 s 6 dB/s, and Off hides it. With RMS, the line follows each FFT frame's calibrated RMS amplitude, not a second, slower power average.
- **Gain** (shared by every view, D-119) moves the vertical viewport; the axis and the readout keep showing signal levels, so at +6 dB the top of the plot is −6 dBFS. **AUTO** follows the input's peaks in every view.
- A pointer or a finger on the plot shows the frequency and the level of both curves there, including the slope but not the display gain.
- The spectrum follows the incoming audio without MIDI Clock or host playback. The app's Pause holds it and stops its analysis.

The app saves the selected view (Spectrum (FFT) is view ID 1) and these settings; the plugin saves them per instance in its host state, and projects saved before the analysis views open in Scope. Older saved settings, with per-curve visibility, response, style and colour, are read and migrated: curve 1's detector and smoothing, the channels, range and slope are kept. The Afterglow choice is saved on its own, and the app also saves the shared gain.

## Measurement definition

The analyzer uses an 8,192-sample periodic Hann window and a 1,024-sample hop. KISS FFT 131.1.0 (BSD-3-Clause, vendored unmodified in `third_party/kissfft`) supplies the float FFT behind `SpectrumAnalyzer`; neither JUCE nor platform DSP APIs enter the core. FFT plans, rings, envelopes, scratch memory and triple-buffered snapshots are allocated when the analyzer is constructed. Processing, stream resets and settings changes do not allocate, lock or do I/O. The audio callback is unchanged.

Magnitude calibration compensates the Hann window's coherent gain: a bin-centred full-scale sine reads 0 dB peak before frequency smoothing and slope. RMS averages spectral power exponentially and reads 3.0103 dB below Peak for a steady sine. Peak attacks immediately and its amplitude decays exponentially with the time constant, 150 ms for curve 1; RMS uses the time constant for rising and falling power alike. The analyzer also has Medium (500 ms), Slow (1.5 s) and Hold responses, which the current presentation does not use.

Fractional-octave smoothing averages power over a symmetric logarithmic band, so a narrow tone reads lower with wider smoothing. Both curves share their frequency sampling, since they use the same smoothing; their envelopes stay independent. These are smoothed, tone-calibrated FFT magnitudes, not integrated octave-band SPL, broadband RMS, LUFS or a noise-density measurement. Where several FFT bins fall into one logarithmic display cell, Raw keeps the largest; cells narrower than a bin are interpolated.

Snapshots contain 768 logarithmically spaced points from 20 Hz to 20 kHz or Nyquist, whichever is lower. The bin spacing is the sample rate divided by 8,192: 5.86 Hz at 48 kHz and 23.44 Hz at 192 kHz. The first complete window takes 171 ms at 48 kHz (43 ms at 192 kHz), and the hop is 21.3 ms at 48 kHz. The window smears changes in time as well as the envelope does.

Dropped input and stream changes restart the window and the envelopes. A detector or smoothing change resets the envelope it affects, and a channel change both. Invalid input samples are replaced with silence. The FFT runs only while Spectrum (FFT) is shown, and the band filters only while Spectrum (Band) is; in the plugin, only while the editor is open. The scope's analysis keeps the transport and auto gain going meanwhile.

## Rendering

The renderer caches the static grid and labels at physical pixel resolution, blends the full-resolution gradient fill in contiguous rows (with ARM64 SIMD where available), and caches the faint tail below the first curve. Only the region the previous and current curves occupy is restored and repainted; a visible readout repaints the whole plot.

The refresh target is 30 Hz, or 25 Hz on ARM64 Linux. A slower frame stretches the interval to 1.5 times its render and paint time, so overdue timer callbacks do not queue up and the Pi's message thread keeps headroom for input, the status bar and window events. The diagnostics overlay shows Spectrum's rendered frames per second and its average and maximum render and paint times while it is selected; these count generated frames, not the display's refresh.

`visona_ui_checks --benchmark` renders a reproducible synthetic spectrum at 1920×1080 with the Line, Fill and Solid styles. The UI checks compare incremental rendering pixel by pixel with a full redraw after large level changes, with both curves and each style.

## Validation

Core tests cover tone frequency and level at 44.1, 48, 96 and 192 kHz, RMS and Peak calibration, phase-safe stereo averaging, Mid/Side cancellation, high-frequency peak retention, hold and reset, input gaps, invalid samples, block partitioning, settings validation, allocation checks, concurrent settings and snapshot use under stress, and app and plugin pipeline integration.

The optional UI checks render the views offscreen from synthetic audio, without opening audio devices or touching the app's preferences. They round-trip and migrate settings, exercise every control through its callback, switch views repeatedly, check the desktop and touch selectors, and write screenshots at 1280×720, 800×480 and 640×480. On macOS:

```sh
cmake --preset macos -DVISONA_COPY_PLUGINS=OFF -DVISONA_BUILD_UI_CHECKS=ON
cmake --build --preset macos
build/macos/ui/visona_ui_checks_artefacts/Release/visona_ui_checks /tmp/visona-ui-checks
ctest --preset macos
```

The same program runs the stereo checks of [stereo.md](stereo.md#verification). No reference capture, extra weighting standards or auditory masking are included. The view takes visual inspiration from TDR Prism; its measurement algorithms and time constants are defined above and are not claimed to match Prism.

## Measurement history

Measurements made while the view was developed, on 2026-10-16 and 17. Several describe controls that have since gone: per-curve visibility, response, style and colour menus, the Glow style and Reset peaks (D-117). CPU percentages count one core as 100 %, and include the analysis thread. The live measurements used whatever audio was playing, so they are not identical-input benchmarks; the renderer benchmark is the controlled comparison.

### Renderer benchmark on the Pi

On a Raspberry Pi 4 Model B with 1 GB RAM and Debian 13 desktop, 30 frames per style at 1920×1080 with identical synthetic input and the app suspended:

| Style | Render time |
| --- | --- |
| Line | 7.19 ms |
| Fill | 11.86 ms |
| Glow (since removed) | 17.99 ms |
| Solid | 9.77 ms |

Solid keeps all 768 points, anti-aliasing, the frequency grid and the second curve, with one flat path fill and one thin outline instead of per-pixel fades and glow strokes. It became the only style of curve 1.

### Live CPU and frame rate on the Pi

- The first version's UI thread used about 78 % of a core, and 62 % after the first optimizations, with 28 % in the analysis thread and about 93 % for the whole app.
- Opt-in telemetry (`VISONA_PERF_LOG=1`, [pi-build.md](pi-build.md#measure-fps-and-cpu-over-ssh)) then showed about 18 FPS at 76 % with a conservative timer. A shorter timer and no redundant background painting gave 24–25 FPS, but about 110 %.
- With Glow, about 25 FPS at 112 %; with Solid alone 29 FPS at 111 %. With shared frequency sampling and the 25 Hz budget on ARM64 Linux, 25 FPS at about 80–85 %.
- The diagnostics overlay now refreshes at 4 Hz in the app, to cost less while measuring.

### FFT window comparison on the Pi

8,192 samples was chosen as the window (D-117). The hop stayed at 1,024 samples, and the display rate, 1/12-octave smoothing and the Peak Fast response were held constant. Builds with each size were installed and measured on the Pi. The deterministic ARM64 probe in `scripts/pi/spectrum-window-probe.cpp` compares a 1 kHz onset after silence and the tone pairs 60+70 Hz and 60+80 Hz, with identical 48 kHz input for each build.

| FFT samples | Bin spacing | Window length | Onset to 50 % amplitude | Onset to 90 % amplitude | 60+80 Hz valley depth |
| --- | --- | --- | --- | --- | --- |
| 4,096 | 11.72 Hz | 85.3 ms | 42.7 ms | 64.0 ms | 0.5 dB |
| 8,192 | 5.86 Hz | 170.7 ms | 85.3 ms | 128.0 ms | 12.5 dB |
| 16,384 | 2.93 Hz | 341.3 ms | 149.3 ms | 234.7 ms | 34.1 dB |

Onset is measured in audio sample time at published analysis frames, relative to the steady-state amplitude; it leaves out UI scheduling, audio device latency and display scanout. Valley depth is relative to the weaker of the two peaks. 4,096 barely separates 60 and 80 Hz, and 8,192 separates them clearly. Neither separates 60 and 70 Hz; 16,384 does, with an 8.2 dB valley. That is a real loss of close bass detail, which fractional-octave smoothing cannot restore.

The probe writes CSV spectra. Compile it with `core/src/SpectrumAnalyzer.cpp` and `third_party/kissfft/kiss_fft.c`, with the core and KISS FFT include directories. To compare window sizes, put a temporary copy of `visona/SpectrumAnalyzer.h` with another `spectrumFftSize` first on the include path, and compile the probe and the analyzer against the same header. Keep installed-app measurements apart from probe runs.

With Diagnostics closed and the same Solid display settings, the installed app averaged, over 20–25 seconds of live audio:

| FFT samples | FPS | Process CPU | Analysis CPU |
| --- | --- | --- | --- |
| 4,096 | 24.93 | 71.34 % | 9.41 % |
| 8,192 | 24.91 | 73.56 % | 13.80 % |
| 16,384 | 24.88 | 85.06 % | 26.38 % |

8,192 keeps the frame rate, about halves the analysis CPU of 16,384 and lowers the process total by about 14 %, trading some close bass separation for a much quicker onset.

### Fixes found while verifying the curve settings

Changing either curve's settings used to clear both envelopes and refill the FFT window. Now a detector or smoothing change resets only the envelope it affects, and a response change keeps the envelope, including when entering Hold. Tests check both curves' Peak and RMS decay constants at 44.1, 48 and 96 kHz, RMS and Peak attack, Hold independence, reset and live response changes without an FFT refill.

### Local verification on macOS

On 2026-10-16 the macOS Release builds of the app and both plugins, in VST3, AU and CLAP, built with plugin copying turned off, and the core tests passed in Release and with ASan/UBSan. TSan on macOS could not enumerate the tests, because the test allocation counter recursively initialized thread-local storage; that was fixed with the stereo work ([stereo.md](stereo.md#verification)). Pi touch input, the Windows and Linux plugins and playback inside a DAW were not exercised.
