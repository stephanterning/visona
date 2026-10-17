# Stereo analysis

Select **Stereo** in the analysis selector, or press **Alt+4**. The same panel is
used by the standalone app and plugins. **Polar Sample** is the default; the
**Polar Level** and **Lissajous** buttons select the other visualizations. The selected mode and display gain
are stored in standalone settings and per plugin instance.

## Reading the display

- Polar Sample folds the signed sample direction into an upper semicircle. Mono
  is vertical. A single channel lies on its corresponding diagonal. Opposite
  polarity reaches the horizontal edges. Radius is the larger absolute channel
  sample, following Signalizer's projection; it is not RMS energy or Polar Level.
- Lissajous uses `x=(R-L)/2`, `y=(L+R)/2`. Mono is vertical, opposite polarity is
  horizontal, and a quadrature sine makes a circle. A single channel is diagonal.
- Correlation measures `sum(L*R)/sqrt(sum(L*L)*sum(R*R))`, without mean subtraction.
  +1 means proportional signals with the same polarity; -1 means opposite polarity.
  Near zero can indicate decorrelation or a quadrature relationship. It is not
  proof of mono compatibility; listen to the mono sum too.
- Balance is equal-power balance: `(E_R-E_L)/(E_R+E_L)`, from -1 left to +1 right.
  It is neither stereo width nor an amplitude-linear pan position.

Display gain is common to both channels and changes only the drawing. It does not
affect the measurement, gate, audio, or history length. Points beyond the display
scale are omitted and **Display clipped** is shown; they are not forced onto the
boundary. Polar Level instead caps its displayed radii at the boundary and shows
the same warning; aggregation always precedes display gain and clipping. **Reset view** restores Polar Sample and 0 dB display gain. Colour follows
the waveform colour setting. The app's Pause holds the current snapshot and stops
analysis, as for the spectrum views.

## Validity and time windows

Correlation, balance and signal validity use a complete **200 ms rectangular window**. Each channel opens its
validity gate at **-90 dBFS RMS** and closes below **-96 dBFS RMS**. The 6 dB
hysteresis prevents switching repeatedly around a single threshold.

| State | Correlation | Balance / cloud or level profile |
| --- | --- | --- |
| Both channels measurable | Value | Available |
| One channel below threshold | `--`, channel status shown | Available |
| Both below threshold | `--`, No measurable signal | Hidden / `--` |
| Incomplete new window | `--`, measuring status | Hidden / `--` |
| Non-finite or unrepresentable input in the window | `--`, Invalid audio samples | Hidden / `--` |
| No new snapshot for 500 ms | `--`, Waiting for audio | Hidden / `--` |

After a gap, overlap, source change or reactivation, old history and gate states
are discarded. A complete fresh window is required. A deliberately paused display
retains its frozen values. The current implementation accepts sample rates from
8 to 384 kHz; other rates leave the analyzer unavailable. Input exceeding absolute
amplitude 1e6 is rejected to keep the float meter's intermediate products finite;
ordinary audio headroom above 0 dBFS is preserved.

Snapshots are published at 120 Hz and contain **every sample in the last 100 ms**.
The two sample modes build a sample-density image instead of skipping every Nth sample or
joining discontinuous polar points with lines. Brightness is normalized by sample
rate. The raster uses the plot's logical pixel dimensions. A cached opaque RGB
background includes the grid; sparse updates restore previous occupied pixels
and composite current ones without rescaling an alpha image at paint time.
Only the union of the old and new cloud bounds and the readout area is repainted.
Lissajous uses a 60 Hz timer; Polar Sample polls at 240 Hz and Polar Level at 120 Hz to provide
scheduling headroom above the 60 FPS target; a late message-thread callback need
not consume the entire margin. Only fetched snapshots trigger new renders.
Publishing at 120 Hz provides fresh data despite timer and
audio-block scheduling differences; every publication still uses the complete
200 ms measurement and 100 ms sample history.
The analysis retains a fixed-duration density window. Display-only afterglow extends the visible history without extending the measurement window.

## Polar Level definition

The default **Directional level** profile uses linear weights between the 73
angular centres. With radius `r = max(abs(L), abs(R))`, weight `w`, and window
size `N` including silence, its fill is
`sqrt(sum(w*r*r) / max(sum(w), 0.01*N))` per direction. The 1% support floor is
an explicit sparse-direction regularizer, not a claim about iZotope's formula.
The outline is the largest radius with positive weight in that direction.
Endpoints do not wrap. Peak support can change abruptly when a sample crosses
a centre; the RMS weights vary continuously.

The renderer joins centre radii with bounded smoothstep interpolation, using
four segments per interval. This preserves centre values and never overshoots
adjacent values. Shared gain and temporal release remain display-only. Switching
profiles clears history. Settings append a profile ID (0 directional, 1 energy)
and accept older two- and three-field settings with the new default.

For example, a radius of 0.5 occupying 4% of the window in one direction gives
0.5 directional RMS versus 0.1 energy contribution. A single radius-1 sample
in 1,000 samples gives sqrt(0.1) directional fill and a peak of 1. This deliberately
attenuates rare directions instead of normalizing every isolated sample to full
height. These cases are covered by numerical tests.

The **Energy contribution** comparison profile retains the original definition
below, including its stepped drawing. Historical Pi results later in this document
apply to that original implementation, not to the new directional rendering.


Polar Level uses all samples in the published 100 ms history. Its direction is
identical to Polar Sample's upper-semicircle fold, evaluated in double precision
before quantization. There are 73 bin centres from -90 to +90 degrees, spaced by
2.5 degrees, with half-width endpoint bins. Nearest-centre assignment uses ties
towards the larger angle. Mono lies at 0 degrees; single channels lie at +/-45;
exact opposite-polarity samples retain Polar Sample's deterministic +/-90 seam.

For each sample, `r = max(abs(L), abs(R))`. For bin `b`, with `N` samples in the
**entire window**, including zeros:

- Filled radius: `sqrt(sum(r*r for samples in b) / N)`.
- Outline radius: `max(r for samples in b)`, or zero for an empty bin.

The squared filled radii sum to `mean(max(L*L, R*R))`. This conservation identity
is tested against an independent dot-product angular oracle. It is not total
stereo power, conditional RMS within a bin, loudness or an FFT-based pan spectrum.
The fixed bin width matters: energy distributed among more bins produces smaller
individual lobes. A rare transient has a large outline but a small filled lobe.
The raw calculation applies no peak hold, decay, interpolation across time or auto-normalization. The displayed fill and outline use the adjustable release described below.

`core/src/PolarLevel.cpp` is a stateless, allocation-free Visona aggregation over
the established sample-direction convention. It uses double-precision energy
accumulation and rejects a complete invalid window, rather than dropping samples
from its denominator. Gating, correlation and balance remain in the existing LSP
analyzer; no DSP or allocation is added to the audio callback. The UI respects the
same waiting/silence/one-channel/invalid/stale states and standalone pause behavior.
The native opaque image cache and damage repainting are shared with the other
modes. RMS fill and peak outline are labeled directly above the plot.

[iZotope's Ozone manual](https://downloads.izotope.com/docs/izotope-ozone-7-help.pdf)
describes a Polar Level amplitude-by-angle display with averaged samples and
history. It does not supply the full numerical algorithm. Visona's explicit RMS
contribution and same-window peak definitions are therefore not claimed to match
Ozone pixel-for-pixel or numerically.

Saved mode IDs remain backward compatible: 0 = Polar Sample, 1 = Lissajous,
2 = Polar Level. Display gain remains shared and Reset view still selects Polar
Sample at 0 dB.

## Implementation

`StereoAnalyzer` is plain C++20 and sits beside `SpectrumAnalyzer` in both
`AnalysisPipeline` and `HostAnalysisPipeline`. It receives the main L/R channels
on the analysis thread; the standalone sync input and plugin sidechain are not
stereo measurement inputs. The audio callback is unchanged. Only the selected
analysis is enabled, and scope/transport processing keeps its existing behavior.

Storage for the maximum sample rate, LSP meters and triple-buffered snapshots is
allocated at construction. Processing, reset, gate updates and publication do
not allocate, lock, log or wait. A slow UI can miss snapshots without causing the
analysis to skip audio samples. Its timer and rendering run on the message thread.

The LSP build is a small CMake target using the unmodified meter classes and generic
DSP implementations, rather than the entire upstream GNU Make dependency stack.
See [LSP provenance](../third_party/lsp/README.md) and
[Signalizer provenance](../third_party/signalizer/README.md).

Every published measurement starts the LSP meters from zero and feeds the full
window. This avoids accumulating float subtraction residue through a loud-to-quiet
or sound-to-silence transition. Panometer additionally needs a temporary period
change because its pinned `clear()` leaves running sums/window age behind. The
gate recomputes non-negative energies from the same actual samples in double
precision; it never interprets LSP's low-energy correlation zero as valid stereo.
These bounded window passes trade some analysis work for reliable low-level
behavior. No custom correlation/pan algorithm replaces LSP.

The physical ring order is irrelevant to the final energy/cross-product sums.
Only the final full-window outputs are published. The sample history is copied in
chronological order, including when the ring wraps. Switching projection or gain
never resets or recomputes the measurement.

## Verification

`StereoAnalyzerTests.cpp` covers 54 signal/sample-rate combinations, compared
against independent double-precision calculations. Rates are 8, 44.1, 48, 96, 192
and 384 kHz. Inputs include mono, inverted mono, each channel alone, quadrature
sine, deterministic independent noise, unequal-level mono, silence and weak mono.
Each is processed in one call and in changing blocks of 1–4096 frames. Tests verify
every retained history sample, both channel gates and hysteresis, loud-to-quiet
recovery, invalid input, gaps, reset, pause/resume, stopped host transport, no C++
allocations, projection geometry and concurrent snapshot consumption.

The existing optional `VISONA_BUILD_UI_CHECKS` target also runs `StereoUiChecks.cpp`:
settings persistence/malformed values, actual mode buttons, display gain isolation,
clipping, pause, silence, one-channel input, stale streams and desktop/touch images.
Run `visona_ui_checks /absolute/output/directory` to generate both stereo and
existing spectrum UI fixtures.

The original investigation remains in `experiments/stereo-analysis`. Its full
upstream build is a research reference; production builds use `cmake/LspStereo.cmake`.
Polar Level is now implemented as the angular aggregate defined below.

### Local validation, 2026-10-17

On macOS ARM64 with Apple Clang 21, all 255 core tests passed in Release,
ASan/UBSan and TSan configurations. The latter required fixing an existing
Darwin-specific recursion in the test allocation counter: a TSan malloc hook must
not lazily initialize C++ thread-local storage. The counter now uses a pthread key
on that configuration; its own per-thread allocation tests pass too.

The standalone app and VST3/AU/CLAP targets build, and the offline spectrum/stereo
UI checks pass. The actual stereo panel was inspected at 1280x720 and 800x480.
For the 384 kHz fixture in a 1920x1080 panel, density rendering measured about
0.56 ms for Polar Sample and 0.40 ms for Lissajous per update on this machine.
These are offscreen render timings, not end-to-end frame latency or Pi performance.
Windows and live DAW validation were not run locally. Linux ARM64 was subsequently
built and deployed to the Pi as described below.

### Raspberry Pi performance, 2026-10-17

Measured the deployed local build on `visona-pi.local`, a Raspberry Pi 4 Model B
Rev 1.5, in a 1920x1080 kiosk session with live 48 kHz input, a 512-sample audio
buffer, 0 dB display gain and diagnostics hidden. The existing input stream and
audio settings were preserved; these are sequential live measurements, not a
repeatable recorded-signal benchmark. Each mode was reactivated to reset its
cumulative display counters, then sampled for 30 seconds using
`scripts/pi/measure-performance.py` (14 telemetry records per mode).

| Mode | Mean reported FPS | Reported FPS range | Density render | Paint | Process CPU | Analysis CPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Polar Sample | 15.49 | 15.44–15.54 | 3.56 ms | 40.55 ms | 93.27% | 7.61% |
| Lissajous | 21.00 | 20.14–21.18 | 5.09 ms | 22.90 ms | 89.39% | 8.57% |

CPU percentages use one core as 100%; analysis CPU is included in process CPU.
Stereo FPS and render/paint timings are cumulative since view activation, so the
range is a range of reported cumulative averages, not individual frame times.
FPS counts generated density images, not physical display refreshes.

Neither mode meets 60 FPS. The current UI timer and analyzer publication rate are
both 30 Hz, imposing a ceiling even before rendering cost. Paint time alone also
exceeds the 16.67 ms budget in both modes. Image scaling/compositing in `paint()`
is a candidate for profiling; these measurements do not isolate individual draw
calls. Reaching 60 FPS requires both a higher update cadence and cheaper painting.
The Pi reported 60.3 degrees Celsius and `get_throttled=0x50000` during the run
(historical undervoltage/throttling flags, no current flags).
Screenshots confirmed both selected modes and a visible live signal. The app was
left in its original Lissajous mode after measurement.

### Optimized Pi deployment, 2026-10-17

The opaque native-size grid/density cache and sparse damage repainting above were
built and deployed to the same Pi. Both 30-second measurements used the same audio
device configuration, resolution and display gain as the baseline. The live input
continued playing; its samples and stereo spread were not held constant. These
results establish performance for this session, not a worst-case bound for every
signal, display size or sample rate.

| Mode | Mean reported FPS | Density render | Paint | Process CPU | Analysis CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| Polar Sample | 61.40 | 1.02 ms | 1.52 ms | 51.72% | 27.58% |
| Lissajous | 60.66 | 0.34 ms | 1.76 ms | 47.73% | 27.25% |

Each series contains 15 telemetry records. FPS now uses approximately one-second
windows instead of the earlier activation-long average. Polar Sample ranged from
60.51 to 62.21 FPS. Lissajous's first interval included view activation and the
required fresh measurement window, reporting 52.56 FPS; the following 14 intervals
ranged from 60.05 to 62.16 FPS (61.24 FPS mean). Thus both sustain the 60 FPS target
in the measured steady state, with an initial settling interval for Lissajous.
Rates slightly above 60 reflect the JUCE millisecond timer, not a display refresh
measurement or a guarantee of individual frame deadlines.

The LSP formulas, fresh-state measurement, RMS validity thresholds, 200 ms window,
100 ms history and full sample inclusion are unchanged. Only publication cadence
increased to 120 Hz so the 60 Hz UI does not miss updates due to block/timer phase.
The audio callback remains unchanged. All 255 core tests pass, including the
independent double-precision stereo oracles and allocation checks. Targeted stereo
ASan/UBSan and TSan checks pass. UI checks also verify total sample contributions,
repeat-render pixel equality and complete restoration of the grid after silence.

The running binary's SHA-256 matched the local build:
`4130b14df3b5fcfcb1169d72645d3199d604ba1290a71291d19253a284202d62`.
Final temperature was 57.4 degrees Celsius; throttling flags remained `0x50000`.
The app remains in Lissajous mode. Raw local logs are in
`build/pi-polar-opt-final.log` and `build/pi-lissajous-opt-final.log`.

### Polar Level deployment and validation, 2026-10-17

Polar Level is installed and selected on `visona-pi.local`. The final Linux ARM64
binary and running executable both have SHA-256
`9d979051f482fd341b89a4b8e825e00673660b69b5d65fff4a4cc29531660c24`.
The final 60-second live measurement used the Pi 4's existing 48 kHz / 512-sample
audio configuration, 1920x1080 kiosk display, 0 dB display gain and diagnostics
hidden. The input was the available live signal, not a controlled replay; visual
checks showed a narrow, predominantly mono signal, and levels varied during the
run. No audio-device settings were changed.

| Reported FPS | Range across 30 records | Render | Paint | Process CPU | Analysis CPU |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 61.30 | 60.12–62.25 | 1.30 ms | 1.51 ms | 55.62% | 28.87% |

Every reported approximately one-second FPS window exceeded 60. These are
generated-image rates, not display scanout or proof that every individual frame
met its deadline. Raw telemetry is in `build/pi-polar-level-final-performance.log`.
The final temperature was 56 degrees Celsius, with historical-only throttling
flags `0x50000`.

A separate native Pi UI test supplied 4,800 samples covering all 73 directions
with peak-channel radius 0.95. At 1920x1080, 60 offline render+paint iterations
averaged **13.46 ms** with the normal Visona app still running. The first version
took 17.88 ms. Direct row copying to restore the RGB cache, avoiding a redundant
background clear under the opaque plot, and bounded bevel joins brought that
test below the 16.67 ms frame budget. This is a stress timing of rendering and
painting into a software image, not an end-to-end compositor FPS measurement.
At the maximum supported 384 kHz, the separate mixed-tone UI fixture measured
5.07 ms per Polar Level render on the Pi; that is not a 384 kHz live-audio FPS
claim. Raw test output is in `build/polar-level-pi-ui-final.log`.

All **259 core tests** pass. The 14 targeted stereo/Polar Level checks pass under
ASan/UBSan and TSan. New tests cover an independent nearest-direction dot-product
oracle, energy conservation, channel swapping, common polarity/gain, mono,
single-channel and opposite-polarity directions, angular boundaries, sparse
transients, silence, invalid input, sample-rate independence and no allocations.
UI checks pass on macOS and the Pi, including saved settings, complete grid
restoration, no accumulated peak trail, clipping, pause, invalid/stale input,
maximum-rate history and desktop/touch screenshots. macOS standalone and plugin
targets build; the offline website build passes. Windows and live DAW validation
remain untested locally.

### Follow-up with the user's stereo loop

After the user started a stereo music loop, a new 60-second Polar Level run on
the same deployed binary and settings produced 30 telemetry records:

| Mean FPS | Range | Records below 60 | Render | Paint | Process CPU | Analysis CPU |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 61.03 | 58.80–61.97 | 4 / 30 | 2.19 ms | 2.16 ms | 60.41% | 24.97% |

The screenshot confirmed a substantially larger peak profile than the earlier
low-level signal. The view sustains approximately 60 FPS on this loop, but this
follow-up does **not** establish an uninterrupted 60 FPS minimum: four reported
windows dipped below the target. These are approximately one-second generated
frame averages, not individual frame timings. The earlier all-above-60 result
applies only to its earlier signal and run. No code or device settings changed
for this measurement. Raw data: `build/pi-polar-level-loop-performance.log`;
visual check: `build/pi-polar-level-loop.png`.

### Stereo-loop optimization and second deployment

Polar Level now reuses preallocated paths and precomputed bin-edge unit vectors.
The arithmetic, bin assignments, RMS/peak definitions, validity and complete
sample history are unchanged. Its message-thread timer schedules at 75 Hz rather
than nominal 60 Hz to leave dispatch headroom above the requested 60 FPS floor.
This is deliberately more rendering work, not a change to the counter: only
fresh snapshots trigger renders. A new UI check verifies that repeated timer
polls without fresh data do not increment the frame count. The two sample modes
retain their 60 Hz timers.

After building and deploying, a **120-second** measurement with the user's stereo
loop at 48 kHz, 512-sample blocks, 1920x1080 and 0 dB display gain produced:

| Mean FPS | Range across 59 records | Records below 60 | Render | Paint | Process CPU | Analysis CPU |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 70.22 | 65.76–74.38 | 0 / 59 | 2.28 ms | 3.35 ms | 76.41% | 28.79% |

The prior loop measurement was 61.03 FPS on average, with a 58.80 FPS minimum.
The additional dispatch margin removes below-60 records in this run, at the cost
of higher process CPU (previously 60.41% of one core). These are sampled generated
frame-rate windows, not physical display refresh or an absolute guarantee of
individual frame deadlines under arbitrary load. The live signal was visually
confirmed after deployment; raw data is in
`build/pi-polar-level-loop-opt-performance.log`.

All 259 core tests and the UI checks pass. The desktop, touch and full-angle
Polar Level fixture images are pixel-identical to the preceding implementation.
During that comparison, the screenshot helpers were fixed to truncate existing
PNG files instead of appending another PNG, ensuring reruns show fresh results.
No core/DSP or audio-callback code changed in this optimization. The macOS app
and plugins build, and the offline website build passes.

The installed executable, running process and local binary all match SHA-256
`569b56cd185ad3df026423b6b2416f3ab13e9a6636da20a57604368e78193ae9`.
The Pi reported 60.8 degrees Celsius and `get_throttled=0x50000` (historical flags
only). Polar Level remains selected on the Pi.


### Adjustable display persistence

Afterglow offers Off, 0.5 s, 1 s (default), and 2 s, saved per app/plugin instance.
The duration specifies exponential decay to 1%, using elapsed monotonic time,
not a fixed per-frame multiplier. Polar Sample retains sparse pixel brightness
with a maximum envelope of the current 100 ms density and decayed history.
Overlapping snapshots therefore do not repeatedly accumulate energy. Pixels
below one 8-bit brightness step are discarded. Every current sample still contributes.
Polar Level uses immediate attack and exponential release independently for each
RMS and peak bin. These envelopes are display state, not measured RMS or peak.
Lissajous is unchanged. Polar Sample polls at 240 Hz and Polar Level at 120 Hz for timer headroom; only fresh 120 Hz snapshots produce frames.
Silence invalidates live readouts immediately while display history fades;
waiting, invalid input and stale streams clear history. Mode, gain, persistence,
geometry and activation changes clear history. Pause freezes history.
Legacy two-field settings load with 1 s persistence.


The sample renderer keeps history in 32x32 pixel tiles, reuses expired slots,
and skips writes whose final 8-bit alpha is unchanged. Rendering remains at the
native plot resolution. Density values for common sample counts are cached from
the same exponential expression, with a direct fallback for larger counts.
Projection results are cached by sample position and exact left/right float bits:
overlapping snapshots reuse the unmodified Signalizer-derived projection, while
a changed pair always recomputes it. Gain and clipping remain outside this cache.
No audio samples are decimated or omitted by these optimizations.

UI regression checks compare the tiled history with an independent dense envelope
and its complete rendered pixels across decay, expiry and slot reuse. They also
check equal elapsed decay across different frame counts, immediate attack, unchanged
signal validity/correlation, legacy settings, persistence round trips, and bitwise
projection-cache equivalence across wraparound. All 259 core tests and the complete
stereo/spectrum UI checks pass. macOS standalone, AU, VST3 and CLAP targets build.

Polar numeric readouts repaint at 30 Hz; signal validity, missing-stream and
clipping transitions repaint immediately. Their values and 200 ms integration
are unchanged. This avoids copying a full-width readout strip on every image.


### Pi validation with two-second afterglow (2026-10-17)

Final deployed build, the user's confirmed stereo loop, existing 48 kHz / 512-frame
input, native 1920x1080 desktop, 0 dB display gain and maximum 2 s persistence.
Diagnostics overlay closed; opt-in SSH telemetry enabled. Mode/settings were
visually checked before measurement. Polar Sample used 240 Hz timer polling and
Polar Level 120 Hz; only fresh 120 Hz analyzer snapshots count as generated images.

| Mode | Duration / records | Mean FPS | FPS range | Records below 60 | Render | Paint | Process CPU |
|---|---|---|---|---|---|---|---|
| Polar Sample | 120 s / 59 | 75.03 | 60.71–82.56 | 0 | 7.12 ms | 4.56 ms | 134.70% |
| Polar Level | 60 s / 30 | 89.95 | 76.53–95.76 | 0 | 2.59 ms | 3.34 ms | 86.75% |

Both runs meet 60 FPS in every sampled reporting window. These are generated-image
rates in approximately one-second windows sampled about every two seconds, not
physical display scanout or a guarantee that every individual frame meets 16.67 ms.
CPU is expressed relative to one core: Sample uses about 1.35 cores on this four-core
Pi. Longer persistence therefore carries a substantial CPU cost. No sample
reduction, plot downscaling, measurement-window changes or DSP approximations were
used to reach these rates. The Pi remains on Polar Level with 2 s persistence.

Logs: `build/afterglow-sample-final-120s.log` and
`build/afterglow-level-final-60s.log`. Final local/installed/running SHA-256:
`28bb9237d65c6f882f7ea52eb5fc2da87d925fead93f9bd28d99c5f9a606e761`.

Future Pi performance checks use 30-second runs by default. The maintainer clarified after these measurements that 60 FPS is an optimization target, not a strict minimum. The longer historical measurements above are retained as recorded.


## Polar Level comparison findings and agent handoff (2026-10-17)

**Status: unresolved visualization design; further work is deferred to the
[roadmap](roadmap.md#stereo-follow-up-polar-level-visualization).** The maintainer
wants a useful, readable representation of stereo information, not merely a
numerically consistent plot or a cosmetic copy of iZotope.

### Evidence and observations

The maintainer supplied a simultaneous comparison using the same signal:
`/Users/stephan/Desktop/Screen Recording 2026-10-17 at 19.29.35.mov`
(about 40.5 seconds). This is a local reference, not a repository asset; ask for
it again if unavailable. Earlier separate recordings were made at 18.58.02 and
19.16.44 on the same date. The simultaneous recording is the stronger comparison.

Sampled frames throughout the simultaneous recording show:

- iZotope's fill frequently reaches near the outer arc, with narrow side rays.
- Visona's Directional level fill remains much closer to the origin while its
  peak outline extends considerably farther. Around 10 seconds the fill reaches
  roughly 40% of the plot radius, versus nearly the full radius in iZotope.
  This is a visual estimate, not a calibrated amplitude measurement.
- Visona has a more continuous, rounded shape; iZotope retains more distinct
  rays. Visona is set to Directional level, 0 dB display gain and 2 s afterglow.
- This recording does not by itself establish either tool's radial transfer
  function, time constants or exact numerical equivalence of measurement taps.

### What was tried and what remains uncertain

The original Energy contribution profile divides each direction's energy by
all samples in the window. That explains why spreading energy over directions
reduces its individual filled lobes. The newer Directional level profile uses
weighted per-direction RMS with a 1% support floor, linear angular assignment
and bounded smoothstep drawing; its exact definition is documented above.
Both profiles remain selectable for A/B comparison with shared gain/afterglow.

The new profile changes the presentation but does **not** resolve the difference
seen against iZotope. The earlier explanation that whole-window normalization
was the main cause was incomplete. RMS versus peak behavior, radial scaling,
sparse-direction attenuation and history semantics remain separate questions.
Do not treat an auto-gain, peak-based fill or logarithmic scale in iZotope as an
established fact: none has been demonstrated by these recordings.

[iZotope's documentation](https://downloads.izotope.com/docs/ozone8/imager/index.html)
describes amplitude rays representing sample averages, with history shrinking
toward the origin. It does not specify the complete aggregation, normalization
or transfer function. In particular, do not assume its fill and outline are
Visona's RMS and peak pair. Visona's aggregation is our implementation; LSP
correlation/balance integration does not validate that visual design for us.

### Suggested next investigation

1. Agree on the intended information: directional level, energy distribution,
   stereo spread, transient behavior, or a deliberate combination. Define what
   the fill, outline, angle and radius should mean before changing the drawing.
2. Feed both tools the same controlled signals at their actual measurement taps:
   mono sine at several known amplitudes, a panned sine, opposite polarity,
   quadrature, and short tone bursts separated by silence. Record gain, routing,
   processing, sample rate and visual settings. Use a pan sweep to inspect angle
   mapping and crossings; use level steps and bursts to separate attack/release
   from radial scaling. Follow with the same music loop.
3. Evaluate radial mapping, angular support/smoothing and history independently.
   Increasing gain or afterglow alone cannot establish the cause of the observed
   differences. Any normalization or sparse-direction rule must be explicit and
   tested, with measurement values kept separate from display processing.
4. Keep controlled-signal tests, silence/weak-signal validity, invalid-input
   handling and real-time safety. Validate readability with the maintainer.
   Re-measure on Pi after changes: 30 seconds by default, aiming for 60 FPS
   without sacrificing correctness.

At this handoff the new directional profile was built and launched as macOS
standalone; 261 core tests and the UI checks passed. These checks establish the
implemented definitions and regressions covered, not equivalence to iZotope or
final visual acceptance. Historical Pi measurements above predate this profile;
its Pi performance has not been established. This documentation request does not
include further DSP changes, deployment, or a new performance run.


### Lissajous afterglow

Lissajous now uses the same display-only brightness envelope and Afterglow
selector as Polar Sample: Off, 0.5 s, 1 s or 2 s to reach 1% brightness.
The signed M/S projection, sample contributions and measurements are unchanged.
The existing sparse pixel history handles both views; overlapping windows use
maximum brightness rather than repeated accumulation. Invalid or missing streams
clear history, silence allows fading, and Off removes history immediately.
UI checks cover elapsed-time decay, frame-rate independence, attack, Off and
pixel-by-pixel comparison with a dense reference for both sample views.
The earlier statement that Lissajous was unchanged describes the original
Polar Sample/Level afterglow implementation, before this extension.


### Physical-pixel rendering on high-density displays

Stereo previously cached its plot at logical component resolution and painted
it with `drawImageAt`. On a 2x Retina display the cached image was enlarged;
on a 1x Pi display each cached pixel already matched a physical pixel. This
provided a concrete source of softer-looking stereo plots on macOS, independent
of the analysis or the different panel characteristics.

The stereo cache now follows the paint context's physical scale (1x to 3x),
like the spectrum views. It is presented with the inverse transform and no
smoothing resampling. Sample dots occupy 2x2 physical pixels, so their footprint
is finer on Retina; grid and Polar Level strokes retain their logical thickness.
All samples still contribute, and projections, measurement values, gain and
validity are unchanged. Pixel-density brightness can differ because samples
are distributed over more pixels; it is a visualization, not a level reading.
Dirty regions are converted back to logical coordinates before repainting.
Moving between display scales rebuilds the cache and clears display history.

UI checks exercise all three views at 1x, 2x and 1.5x and back to 1x, checking
physical cache dimensions, unchanged measurements and sample contributions.
High-density caches use more memory and can cost more to render. This change
does not constitute a new Pi FPS measurement; the 1x rendering path retains its
previous dimensions.


### Shared display controls

Gain/Auto and Afterglow now live in the common bottom control bar. The stereo
header contains only its view/profile choices. Global Gain applies equally to
all projections, and Auto follows the shared transport-independent input-peak feed regardless
of the visible view. Reset view retains the shared controls. Gain changes
reproject retained sample-history pixels around the plot origin; Polar Level
history already uses signal-unit radii. Samples/history outside the plot can be
discarded. Quantized historical pixels can merge when shrinking the display;
current samples are always projected again from their original values.

The global values are stored independently of the legacy stereo settings string.
Old per-view gain/afterglow values no longer override the shared settings during
startup. Earlier sections describing controls above the stereo plot are historical.
