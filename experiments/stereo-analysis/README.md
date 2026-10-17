# Stereo analysis library integration experiment

The subsequent production integration is described in [docs/stereo.md](../../docs/stereo.md).
This report records the original isolated experiment; its full upstream build is
retained as a reference, while production uses a smaller CMake target.

Completed on 2026-10-17 on Apple Silicon with Apple Clang 21, JUCE 9.0.2 and a
macOS 14.0 deployment target. This directory is an independently built experiment;
the app, plugins, core library and production CMake files are unchanged.

## Decision

**Proceed with LSP as a measurement candidate and the small Signalizer polar port
as a visualization candidate. Neither is a complete, ready-to-ship stereo panel.**

The integration demonstrated real upstream code reuse, found a reproducible LSP
reset problem, and exposed silence handling that must be addressed before the raw
values reach a production UI. Keep Lissajous and Polar Sample as complementary
views. Do not present this implementation as Ozone Polar Level.

| Part | What was integrated | Result |
| --- | --- | --- |
| Correlation | Unmodified LSP `Correlometer` | Matches an independent oracle on the tested active signals; requires validity handling around silence/very low levels |
| L/R balance | Unmodified LSP `Panometer`, both laws | Matches the oracle; adapter works around incomplete `clear()` state reset |
| Polar Sample | Adapted Signalizer scalar projection | Matches independent Cartesian geometry; exact anti-phase seam made deterministic |
| Lissajous | Standard linear M/S transform | Inverse/channel/scale properties checked; rendered with JUCE |
| UI handoff | Existing Visona `TripleBuffer` | Slow-consumer simulation retains the latest measurement without skipping analysis samples |
| Polar Level | Source investigation only | No verified ready-made Ozone-equivalent implementation found in the inspected Signalizer renderer |

## Reproduce

Prerequisites: CMake, Ninja, Clang, Python with `tarfile` extraction filters
(Python 3.12+), and **GNU Make 4+**. The macOS system Make 3.81 produces an incomplete
LSP configuration with missing architecture/dependency information. It is not
suitable. `prepare.py` rejects it before building. GNU Make 4.4.1 was built locally
for this experiment; no system software was installed.

Run from the repository root:

```sh
python3 experiments/stereo-analysis/prepare.py --make gmake
cmake -S experiments/stereo-analysis -B build/stereo-spike/probe -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLSP_ROOT="$PWD/build/stereo-spike/deps/lsp-dsp-units-8dd2a6905ca7ccefd2c3624560235680cb8761e2"
cmake --build build/stereo-spike/probe
ctest --test-dir build/stereo-spike/probe --output-on-failure
build/stereo-spike/probe/stereo_probe
```

Use `--make /absolute/path/to/make` for a locally built GNU Make, and optionally
`--archive-cache /path/to/cache` to reuse downloaded archives. `prepare.py` verifies
archive hashes, extracts sources under `build/`, cleans the generated LSP build,
and uses the upstream build system. It never runs `make install`. The manifest
contains LSP's four required library dependencies and the Signalizer source.
The build recipe currently supports macOS only.

For the optional **actual JUCE PNG renderer**, use the existing JUCE checkout
created by Visona's normal macOS configuration:

```sh
cmake -S experiments/stereo-analysis -B build/stereo-spike/probe \
  -DJUCE_SOURCE_DIR="$PWD/build/macos/_deps/juce-src"
cmake --build build/stereo-spike/probe
build/stereo-spike/probe/stereo_render "$PWD/build/stereo-spike/stereo-views.png"
```

The image contains mono, anti-mono, each channel alone, quadrature sine and
independent noise. It uses the same samples, fixed gain, no channel-independent
normalization, a 200 ms measurement window, and a 100 ms display history.
Points are intentionally not joined: the discrete points on a coherent 1 kHz
sine at 48 kHz are actual sample positions, not dropped frames. The renderer is
an offscreen integration fixture, not the proposed final panel design.

For sanitizers, build LSP itself with its upstream ASan feature in a separate tree:

```sh
python3 experiments/stereo-analysis/prepare.py --make gmake \
  --build-dir build/stereo-spike-asan --asan
cmake -S experiments/stereo-analysis -B build/stereo-spike-asan/probe -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DSPIKE_SANITIZERS=ON \
  -DLSP_ROOT="$PWD/build/stereo-spike-asan/deps/lsp-dsp-units-8dd2a6905ca7ccefd2c3624560235680cb8761e2"
cmake --build build/stereo-spike-asan/probe
ctest --test-dir build/stereo-spike-asan/probe --output-on-failure
```

ASan instruments upstream C++ and the probe; UBSan instruments the probe/adapters.
Handwritten DSP assembly is not instrumented. This is not a claim that every
upstream implementation has passed every sanitizer.

## Measurements and tests

The oracle computes independent double-precision rolling L/R energies and cross
products. It compares every output sample, including startup, over more than
three complete measurement windows. It also models LSP's documented-in-source
low-energy threshold, rather than treating a thresholded zero as valid correlation.

- Nine fixtures: mono, inverted mono, left only, right only, 90-degree sine,
  deterministic independent noise, unequal-level mono, silence, and quiet mono.
- Four sample rates: 44.1, 48, 96 and 192 kHz; both Panometer laws: 72 configurations.
- Each is processed as one long call and again with changing blocks of
  1, 7, 64, 127, 256, 511, 1024 and 4096 frames.
- Additional cases cover invalid zero windows, reset at periods 1/2/9600,
  signal-to-silence transitions, a 201 by 201 projection grid, L/R swaps,
  Lissajous inversion, non-finite display input, and delayed snapshot consumption.
- Release and ASan/UBSan probe runs passed. The JUCE renderer compiled and its PNG
  was visually inspected. The production test suite was not rerun because no
  production source or build configuration changed.

Observed maximum absolute errors in the regular fixture suite:

| Comparison | Maximum error |
| --- | ---: |
| LSP correlation vs double oracle | 2.14025e-7 |
| LSP pan vs double oracle | 2.96138e-6 |
| One-call vs changing-block output | 0 on this ARM build |

These are observations for the pinned build, not guarantees on other architectures.
The test tolerance is 2e-4. Processing/reset/publication scopes counted zero C++
allocations using Visona's existing allocation instrumentation. The counter does
not intercept direct `malloc`. Inspection of these meter methods found allocation
in initialization, not in their processing paths. No audio callback was involved.

## Important findings

### Correlation has a specific definition

LSP computes `sum(L*R) / sqrt(sum(L*L) * sum(R*R))` over a rectangular window.
It does not subtract each channel's mean. A future DC blocker would change the
measurement and needs an explicit decision; do not silently label this Pearson
correlation with mean subtraction.

The underlying DSP returns zero when the product of the two window energy sums is
below `1e-18`. Silence, a missing channel, and extremely quiet mono can therefore
all produce zero. Zero must not automatically mean decorrelated stereo. A UI
validity flag should distinguish unavailable measurements from actual correlation
near zero. The renderer labels the known single-channel fixtures as undefined;
that fixture-specific label is not a production gate implementation.

### LSP Panometer reset needs an adapter workaround

At the pinned version, `Panometer::clear()` zeroes its buffers and moves its head
but does not reset the running sums or window age. Reproduction:

1. Initialize a 9600-sample window.
2. Process 103 samples with right amplitude half of left.
3. Call `clear()` and process silence.
4. The raw equal-power pan remains **0.2**, instead of its silence default **0.5**.

`LspStereo::reset()` clears both meters, temporarily changes the Panometer period,
then restores it. That forces its sums/window age to be reset without allocation.
The regression test checks both the original behavior and the workaround.
Upstream sources remain unmodified. No upstream issue or message has been sent.

### Silence after active audio needs validity handling too

This is separate from explicit reset. In the signal-to-silence fixture, after all
nonzero samples have left the mathematical window, floating-point accumulation
residue can leave raw correlation at **1** and raw pan at **0.2**. The values return
to their silence defaults after the next periodic window rebase. The probe
records this behavior and verifies eventual settling; it does **not** assert
oracle agreement during this invalid silent interval.

Before production use, define a level-based validity gate with hysteresis and
startup/full-window status. Do not derive validity from the raw correlation value
alone. Keep invalid values out of numerical displays and warnings. The experiment
deliberately exposes the raw output so this limitation remains visible.

### “Balance” needs a named law

With LSP's default equal-power law, `pan = E_R / (E_L + E_R)`; it runs from left 0
to right 1. With its linear law, RMS amplitudes replace energies. At right amplitude
half of left, these give **0.2** and **1/3**, respectively. Either can be mapped to
`[-1, +1]` using `2*pan-1`, but they must not share an unexplained label or be
confused with stereo width or correlation.

### Signalizer Polar Sample is not just a rotated Lissajous view

The reused projection folds the direction into the upper semicircle and sets
radius to `max(abs(L), abs(R))`. The Lissajous fixture uses
`x=(R-L)/2, y=(L+R)/2`. The polar conversion therefore changes radial geometry;
it is not an energy-preserving coordinate rotation.

This is visible on the quadrature sine: Lissajous is a circle, while the polar
radius varies with channel peaks. Exact anti-phase lies on the fold seam. In the
unadapted scalar formula, fused floating-point operations can affect the endpoint
chosen at that seam. The adaptation explicitly selects endpoints there, and tests
channel mirroring. Near the seam a discontinuity is inherent to folding; do not
connect unrelated polar points with a continuous line across it.

Lissajous retains more directly interpretable signed channel geometry. Polar
Sample is a useful compact, familiar overview. Both should share measurements,
time history and common display gain; neither replaces correlation or mono
listening. A simple projection port avoids importing Signalizer's complete CPL,
OpenGL, parameter, audio-history and threading infrastructure.

### Polar Level remains a separate design task

The inspected Signalizer renderer draws polar samples as `GL_POINTS` or
`GL_LINE_STRIP`; its `fillPath` flag does not implement an Ozone-style angular level
histogram. No exact public Ozone calculation was verified by this experiment.

An eventual angular level view must explicitly choose between peak, conditional
RMS per occupied bin, and energy per bin divided by the total sample count. Those
are different measurements and produce different widths. Signed sample averaging
can cancel audio; occupancy-only histograms discard amplitude. Temporal smoothing,
angular smoothing, peak hold and gain must be specified and tested independently.
Do not add an attractive filled contour with an undocumented interpretation.

## Production integration path

1. Add an optional `StereoAnalyzer` beside `SpectrumAnalyzer`, fed from both
   `AnalysisPipeline` and `HostAnalysisPipeline` on the analysis thread. Reuse the
   existing audio ring; exclude the sync input. Call LSP dispatch initialization
   once before analysis starts and allocate meter storage at stream setup.
2. Include sample rate, window length, frame position, validity, and discontinuity
   information in the snapshot. Reset on missing frames or stream changes; UI
   frame drops must not skip measurement samples. The experiment's handoff check
   is single-threaded; concurrent integration still needs existing stress-test
   patterns and TSan coverage.
3. Implement and validate the level gate and reset workaround before exposing
   correlation/pan. Specify treatment of non-finite audio and denormals. Test
   impulses, delays, real music, and gain ramps in addition to these fixtures.
4. Keep projection math JUCE-free. Render snapshots on the UI thread with a
   shared aspect ratio and shared gain; benchmark real-time persistence/point
   density separately. This PNG fixture does not validate animated frame pacing.
5. Evaluate a smaller LSP source target if the full dependency stack is undesirable.
   This experiment intentionally built the unmodified upstream library first;
   it does not yet demonstrate a minimal CMake-only dependency integration.
6. Validate Windows/Linux/Pi and the plugin formats before adopting this globally.
   Update the manual when the actual user-facing stereo feature is introduced.

No website or manual change is needed for this experiment: it adds no user-visible
feature or changed control. See [THIRD_PARTY.md](THIRD_PARTY.md) for source and
license provenance.
