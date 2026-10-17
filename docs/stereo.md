# Stereo analysis

Stereo is one of the analysis views of the standalone app and the plugins (D-116, D-120). Select **Stereo** in the analysis selector, or press **Alt+4**; it is saved as view ID 3. **Polar Sample** is the default; the **Polar Level** and **Lissajous** buttons above the plot select the other visualizations, and Polar Level adds a profile menu. The shared **Gain**, **AUTO** and **Afterglow** controls are in the control bar (D-119), next to **Reset view**, which goes back to Polar Sample and the Directional level profile and leaves the shared controls as they are. The mode and the Polar Level profile are stored in the app's settings and per plugin instance. Colour follows the waveform colour setting. The app's Pause holds the current snapshot and stops the analysis, as for the spectrum views.

## Reading the display

- Polar Sample folds the signed sample direction into an upper semicircle. Mono
  is vertical. A single channel lies on its corresponding diagonal. Opposite
  polarity reaches the horizontal edges. Radius is the larger absolute channel
  sample, following Signalizer's projection; it is not RMS energy or Polar Level.
- Lissajous uses `x=(R-L)/2`, `y=(L+R)/2`. Mono is vertical, opposite polarity is
  horizontal, and a quadrature sine makes a circle. A single channel is diagonal.
- Polar Level shows level per direction over the same 100 ms: a filled RMS profile and a peak outline, defined [below](#polar-level-definition).
- Correlation measures `sum(L*R)/sqrt(sum(L*L)*sum(R*R))`, without mean subtraction.
  +1 means proportional signals with the same polarity; -1 means opposite polarity.
  Near zero can indicate decorrelation or a quadrature relationship. It is not
  proof of mono compatibility; listen to the mono sum too.
- Balance is equal-power balance: `(E_R-E_L)/(E_R+E_L)`, from -1 left to +1 right.
  It is neither stereo width nor an amplitude-linear pan position.

Display gain is common to both channels and changes only the drawing. It does not
affect the measurement, the gate, the audio or the history length. In the sample
views, points beyond the display scale are omitted and **Display clipped** is shown;
they are not forced onto the boundary. Polar Level instead caps its displayed radii
at the boundary and shows the same warning; aggregation always comes before display
gain and clipping. When the gain changes, the retained afterglow pixels are
reprojected around the plot's origin; history that ends up outside the plot is
discarded, and quantized pixels can merge when the display shrinks. Current samples
are always projected again from their original values.

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
retains its frozen values. The analyzer accepts sample rates from 8 to 384 kHz;
other rates leave it unavailable. Input exceeding absolute amplitude 1e6 is rejected
to keep the float meter's intermediate products finite; ordinary audio headroom
above 0 dBFS is preserved.

Snapshots are published at 120 Hz and contain **every sample in the last 100 ms**;
every publication uses the complete 200 ms measurement and 100 ms sample history.
The two sample modes build a sample-density image instead of skipping every Nth
sample or joining discontinuous polar points with lines. Brightness is normalized
by sample rate. Lissajous polls on a 60 Hz timer; Polar Sample polls at 240 Hz and
Polar Level at 120 Hz, to leave headroom above the 60 FPS target, since JUCE's
message-thread timer is not deadline-based. Only fresh snapshots trigger new
renders, and a slow UI can miss snapshots without the analysis skipping samples.

## Afterglow

Afterglow offers Off, 0.5 s, 1 s (the default) and 2 s, shared with the spectrum
views (D-119). The duration is the time for an exponential decay to 1 %, using
elapsed monotonic time, not a fixed per-frame multiplier, so it does not depend on
the frame rate.

- **Polar Sample and Lissajous** keep sparse pixel brightness, with a maximum envelope
  of the current 100 ms density and the decayed history, so overlapping snapshots do
  not accumulate energy repeatedly. Pixels below one 8-bit brightness step are
  discarded. Every current sample still contributes.
- **Polar Level** uses immediate attack and exponential release independently for
  each RMS and peak bin. These envelopes are display state, not measured RMS or peak.

Silence invalidates the live readouts at once while the display history fades.
Waiting, invalid input and stale streams clear the history, and so do a change of
mode, profile, afterglow, geometry or display scale, and activation; a gain change
reprojects it instead. Pause freezes it.

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
profiles clears history.

For example, a radius of 0.5 occupying 4% of the window in one direction gives
0.5 directional RMS versus 0.1 energy contribution. A single radius-1 sample
in 1,000 samples gives sqrt(0.1) directional fill and a peak of 1. This deliberately
attenuates rare directions instead of normalizing every isolated sample to full
height. These cases are covered by numerical tests.

The **Energy contribution** comparison profile keeps the original definition
below, including its stepped drawing.

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
The raw calculation applies no peak hold, decay, interpolation across time or
auto-normalization; the displayed fill and outline use the Afterglow release.

`core/src/PolarLevel.cpp` is a stateless, allocation-free Visona aggregation over
the established sample-direction convention. It uses double-precision energy
accumulation and rejects a complete invalid window, rather than dropping samples
from its denominator. Gating, correlation and balance remain in the LSP-based
analyzer. The UI respects the same waiting, silence, one-channel, invalid and stale
states and the app's pause. RMS fill and peak outline are labelled directly above
the plot.

[iZotope's Ozone manual](https://downloads.izotope.com/docs/izotope-ozone-7-help.pdf)
describes a Polar Level amplitude-by-angle display with averaged samples and
history. It does not supply the full numerical algorithm. Visona's explicit RMS
contribution and same-window peak definitions are therefore not claimed to match
Ozone pixel-for-pixel or numerically.

## Rendering

The plot is cached as an opaque RGB image at the paint context's physical scale
(1x to 3x), like the spectrum views, and presented with the inverse transform and
no smoothing resampling. The cached background includes the grid. Sparse updates
restore previously occupied pixels and composite the current ones; only the union
of the old and new cloud bounds is repainted. Sample dots occupy 2x2 physical
pixels, so they are finer on a Retina display than on the Pi's 1x display; grid and
Polar Level strokes keep their logical thickness. Moving between display scales
rebuilds the cache and clears the history.

The sample renderer keeps its history in 32x32-pixel tiles, reuses expired slots,
and skips writes whose final 8-bit alpha is unchanged. Density values for common
sample counts are cached from the same exponential expression, with a direct
fallback for larger counts. Projection results are cached by sample position and
the exact bits of the left and right floats, so overlapping snapshots reuse the
unmodified Signalizer-derived projection, while a changed pair is always
recomputed. Gain and clipping stay outside this cache. Polar Level reuses
preallocated paths and precomputed bin-edge unit vectors. No samples are decimated
or omitted by these optimizations.

The numeric readouts repaint at 30 Hz; changes of signal validity, a missing stream
and clipping repaint at once. Their values and 200 ms integration are unchanged.

## Settings

The stereo settings are a comma-separated string of the mode (0 Polar Sample,
1 Lissajous, 2 Polar Level), two legacy fields for the per-view gain and afterglow
of the first version, and the Polar Level profile (0 Directional level, 1 Energy
contribution). Older two- and three-field strings load with the default profile.
The shared gain and afterglow are stored separately, and the legacy fields no
longer override them.

## Implementation

`StereoAnalyzer` is plain C++20 and sits beside `SpectrumAnalyzer` in both
`AnalysisPipeline` and `HostAnalysisPipeline`. It receives the main L/R channels
on the analysis thread; the standalone sync input and plugin sidechain are not
stereo measurement inputs. The audio callback is unchanged. Only the selected
analysis is enabled, and scope/transport processing keeps its existing behavior.

Storage for the maximum sample rate, LSP meters and triple-buffered snapshots is
allocated at construction. Processing, reset, gate updates and publication do
not allocate, lock, log or wait. Its timer and rendering run on the message thread.

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

The original investigation is in `experiments/stereo-analysis`. Its full upstream
build is a research reference; production builds use `cmake/LspStereo.cmake`.

## Verification

`StereoAnalyzerTests.cpp` covers 54 signal/sample-rate combinations, compared
against independent double-precision calculations. Rates are 8, 44.1, 48, 96, 192
and 384 kHz. Inputs include mono, inverted mono, each channel alone, quadrature
sine, deterministic independent noise, unequal-level mono, silence and weak mono.
Each is processed in one call and in changing blocks of 1–4096 frames. Tests verify
every retained history sample, both channel gates and hysteresis, loud-to-quiet
recovery, invalid input, gaps, reset, pause/resume, stopped host transport, no C++
allocations, projection geometry and concurrent snapshot consumption.
`PolarLevelTests.cpp` covers an independent nearest-direction dot-product oracle,
energy conservation, channel swapping, common polarity and gain, mono, single-channel
and opposite-polarity directions, angular boundaries, sparse transients, silence,
invalid input, sample-rate independence and no allocations.

The optional `VISONA_BUILD_UI_CHECKS` target also runs `StereoUiChecks.cpp`:
settings persistence and malformed values, the mode buttons, display gain isolation,
clipping, pause, silence, one-channel input, stale streams, the afterglow history
against an independent dense envelope across decay, expiry and slot reuse, equal
decay at different frame rates, all three views at 1x, 2x and 1.5x display scale,
and desktop and touch images. Run `visona_ui_checks /absolute/output/directory` to
generate the stereo and spectrum UI fixtures ([spectrum.md](spectrum.md#validation)).

Making TSan work on macOS required a fix in the test allocation counter: a TSan
malloc hook must not lazily initialize C++ thread-local storage, so on that
configuration the counter uses a pthread key; its own per-thread allocation tests
pass too.

## Measurement history on the Pi

Measured on 2026-10-17 on `visona-pi.local`, a Raspberry Pi 4 Model B Rev 1.5, in a
1920x1080 kiosk session with live 48 kHz input, 512-sample audio buffers, 0 dB
display gain and diagnostics hidden, with `scripts/pi/measure-performance.py`.
These are sequential live measurements, not a repeatable recorded-signal benchmark,
and the signal was not held constant. FPS counts generated images in roughly
one-second windows, not physical display refreshes; CPU counts one core as 100 %
and includes the analysis thread. Raw logs were kept locally, outside the repository.

| Build | Mode | Duration | Mean FPS (range) | Render | Paint | Process CPU | Analysis CPU |
| --- | --- | --- | --- | --- | --- | --- | --- |
| First version: 30 Hz timer and publication | Polar Sample | 30 s | 15.49 | 3.56 ms | 40.55 ms | 93.27 % | 7.61 % |
| | Lissajous | 30 s | 21.00 | 5.09 ms | 22.90 ms | 89.39 % | 8.57 % |
| Opaque cache, sparse repaint, 120 Hz publication | Polar Sample | 30 s | 61.40 (60.51–62.21) | 1.02 ms | 1.52 ms | 51.72 % | 27.58 % |
| | Lissajous | 30 s | 60.66 | 0.34 ms | 1.76 ms | 47.73 % | 27.25 % |
| Polar Level, Energy contribution, narrow mono signal | Polar Level | 60 s | 61.30 (60.12–62.25) | 1.30 ms | 1.51 ms | 55.62 % | 28.87 % |
| Same, with the maintainer's stereo loop | Polar Level | 60 s | 61.03 (58.80–61.97) | 2.19 ms | 2.16 ms | 60.41 % | 24.97 % |
| Preallocated Level paths, faster Level timer | Polar Level | 120 s | 70.22 (65.76–74.38) | 2.28 ms | 3.35 ms | 76.41 % | 28.79 % |
| 2 s afterglow, stereo loop | Polar Sample | 120 s | 75.03 (60.71–82.56) | 7.12 ms | 4.56 ms | 134.70 % | – |
| | Polar Level | 60 s | 89.95 (76.53–95.76) | 2.59 ms | 3.34 ms | 86.75 % | – |

- In the first version, both the UI timer and the publication ran at 30 Hz, a ceiling
  before any rendering cost, and painting alone exceeded the 16.67 ms frame budget.
  Raising the publication to 120 Hz, an opaque native-size cache and sparse damage
  repainting brought both sample modes to 60 FPS.
- With the stereo loop, 4 of 30 windows dipped below 60 FPS; a faster Polar Level
  timer removed the dips at the cost of more CPU. Polar Level now polls at 120 Hz.
- Longer afterglow costs substantial CPU: Polar Sample with 2 s of afterglow used
  about 1.35 cores of the Pi's four.
- Offscreen, a Polar Level stress fixture of 4,800 samples covering all 73
  directions rendered and painted at 1920x1080 in 13.46 ms on the Pi, down from
  17.88 ms, with the normal app still running; a 384 kHz mixed-tone fixture rendered
  in 5.07 ms. On macOS, a 384 kHz fixture rendered in about 0.56 ms for Polar Sample
  and 0.40 ms for Lissajous. These are render timings, not end-to-end frame rates.
- Not measured on the Pi yet: the Directional level profile, Lissajous afterglow, the
  physical-pixel cache (the 1x path keeps its dimensions) and the shared display
  controls.

Future Pi performance checks run for 30 seconds by default, and 60 FPS is an
optimization target, not a strict minimum (AGENTS.md).

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

These checks establish the implemented definitions and the regressions they cover,
not equivalence to iZotope or final visual acceptance.
