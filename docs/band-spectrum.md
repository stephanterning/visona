# Spectrum (Band)

Spectrum (Band) is one of the analysis views, alongside Scope, Spectrum (FFT) and
Stereo (D-116, D-118). Select it in the desktop list or touch tiles, or with Alt+3.
App preferences and plugin instance state remember it, as view ID 2. Both spectra
have black backgrounds. The band bars follow the shared Waveform colour setting.

## Analysis

31 nominal third-octave centres span 20 Hz to 20 kHz. Exact centres use
`1000 * 10^((band - 17) / 10)`. Each channel runs through two cascaded unity-centre
biquad bandpasses per band; section bandwidth is adjusted for the cascade's half-power
edges. This is an approximately third-octave musical analyzer, not an IEC-certified
acoustic measurement filter bank. Bands whose upper edge exceeds Nyquist are omitted.

The instantaneous power is `(filteredLeft^2 + filteredRight^2) / 2`, so mono and
opposite-phase stereo do not cancel. The bar envelope attacks immediately and releases
with a 150 ms amplitude time constant. A centre-frequency sine is peak-calibrated;
this is neither integrated broadband RMS nor SPL. Peak markers retain instantaneous
peaks for the shared Afterglow time (0.5, 1 or 2 s; D-119), then release at 12 dB/s;
an equal or higher peak restarts the hold, and Afterglow Off hides the markers. A new
Afterglow time restarts the markers from the current bars. The visible span is fixed
at 60 dB and placed by the shared display gain, whose labels keep showing signal
levels; there is no tilt or smoothing control, and the FFT view's settings do not
affect the band view.

Filters execute on the existing analysis thread, not the audio callback. State and
triple-buffer snapshots are fixed-size, with no allocation, locks or I/O in process.
`SpectrumAnalyzer` owns the `BandAnalyzer` and forwards its input to it; the view
switch enables only the chosen analyzer. A gap, stream reset or reactivation clears
filter history. Snapshots are published at about 60 Hz; the UI targets 30 Hz and
waits longer when a frame takes longer to draw.

## Rendering

The static black background, labels and grid are cached at physical pixel resolution.
Bars and markers are drawn directly in paint, without an intermediate dynamic frame
image. A single bounding rectangle covering changed bars and markers is invalidated
on data updates. Keeping that region contiguous avoids expensive fragmented region
processing. Render time in diagnostics covers
background/cache preparation; Paint includes the bars, markers and background copy.
The FFT renderer continues to report its separate cached-curve render cost.

## Validation on Pi

`scripts/pi/band-response-probe.cpp` measures a 0.5-amplitude sine onset after silence
at 48 kHz on the Pi. The first publication at or above 90% of the input peak occurred at:

| Centre | Analysis response |
| --- | --- |
| 19.95 Hz | 216.7 ms |
| 63.10 Hz | 66.7 ms |
| 1 kHz | 16.7 ms |
| 7.94 kHz | 16.7 ms |

These are analysis publication times, excluding device, compositor and display latency.
Low-frequency selectivity still has a settling-time cost. Band is not a zero-latency
replacement for FFT and does not preserve FFT's narrow-tone resolution.

Tests cover tone calibration and adjacent-band rejection at 44.1/48/96 kHz,
third-octave edges, opposite-phase stereo and single-channel energy, onset, marker
hold/reset/decay, stream gaps, disabled processing, no allocations, audio block
independence and FFT/Band dispatch. UI checks include desktop/touch snapshots and
switching among all three views.

The measurements below were made on 2026-10-17, before the markers' hold followed
Afterglow and before the shared display controls (D-119).

With live input on the Raspberry Pi 4, diagnostics closed, a 25-second telemetry
run of the installed version averaged 29.77 FPS, 38.61% process CPU and 6.09 ms
paint time. The analysis thread used 9.18% CPU (included in process CPU). 100%
means one CPU core. Most active samples were around 40–47%; a brief quiet interval
lowered the average. These are workload-dependent measurements, not a guarantee.

The initial full-frame copy version measured 27.83 FPS and 86.94% CPU; direct
painting of the entire plot measured 26.20 FPS and 53.07% CPU. The final renderer
uses one bounding rectangle for changes. These sequential runs used live input,
so their CPU figures are not a controlled identical-signal benchmark. UI pixel
checks compare partial repaint output against a complete redraw over rising and
falling levels to detect stale bars or peak markers.
