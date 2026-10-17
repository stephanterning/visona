# Third-party source provenance

This experiment is separate from the production build. The downloaded source trees
retain their original copyright headers and license files. `dependencies.json`
pins every archive with SHA-256; a moved release tag will fail verification.

| Source | Version/revision | Use | Upstream license |
| --- | --- | --- | --- |
| [LSP DSP Units](https://github.com/lsp-plugins/lsp-dsp-units/tree/8dd2a6905ca7ccefd2c3624560235680cb8761e2) | 1.0.38 source at `8dd2a69` | Unmodified `Correlometer`, `Panometer`, full upstream static-library build | LGPL-3.0-or-later |
| LSP common / DSP / LLTL / runtime libraries | 1.0.49 / 1.1.1 / 1.0.33 / 1.0.36 | Required by the upstream build, bundled into its archive | LGPL-3.0-or-later; retain each tree's notices |
| [Signalizer](https://github.com/jthorborg/signalizer/blob/e2043da622caf6dcf3ff472b8ac2b24a5ee02436/Source/Vectorscope/VectorscopeRendering.cpp) | `e2043da6` | Scalar polar projection adapted into `SignalizerPolar.h` | GPL-3.0-or-later in the source header |
| JUCE | Existing Visona JUCE 9.0.2 checkout | Optional offscreen graphics renderer | Existing Visona JUCE licensing applies |

`SignalizerPolar.h` derives from the scalar remainder path in
`VectorScope::drawPolarPlot`, originally copyright 2016 Janus Lynggaard Thorborg.
The original notice is retained and the GPL v3 text is included in
`licenses/Signalizer-GPL-3.0.txt`.

Changes to that path:

- Extracted a small scalar function returning a point.
- Removed CPL, SIMD, OpenGL, audio-buffer access, history and gain dependencies.
- Replaced `sincos` with `std::sin` / `std::cos`.
- Added early guards for silence and non-finite input.
- Defined the exact anti-phase seam explicitly. Opposite polar endpoints are
  equivalent fold boundaries; the chosen rule mirrors when L/R are swapped.

The peak-channel radius and angle calculation are retained elsewhere. The port is
not a byte-for-byte reproduction of Signalizer's complete rendering pipeline or
its SIMD approximations. The elementary Lissajous transform and JUCE drawing code
are Visona experiment code. No Ozone source or algorithm was copied or inferred as
an exact implementation from the screenshots.

LSP source files are not vendored into the repository. Their complete sources,
`COPYING`, and `COPYING.LESSER` are downloaded together by `prepare.py`. A production
integration must preserve these notices and provide the matching source/build
instructions with any distributed binaries. The full upstream library also pulls
in more code than these two meters require; review the actual production link and
its bundled notices before shipping.
