# Signalizer polar projection

`core/include/visona/StereoProjection.h` contains a small adaptation of the scalar
polar path in [Signalizer's VectorscopeRendering.cpp](https://github.com/jthorborg/signalizer/blob/e2043da622caf6dcf3ff472b8ac2b24a5ee02436/Source/Vectorscope/VectorscopeRendering.cpp),
revision `e2043da622caf6dcf3ff472b8ac2b24a5ee02436`.

Copyright (C) 2016 Janus Lynggaard Thorborg (www.jthorborg.com).
The source is GPL-3.0-or-later; its copyright/license notice is retained in the
header and the GPL v3 text is in `COPYING`.

The adaptation removes CPL, OpenGL, SIMD, gain and history dependencies. It uses
`std::sin` and `std::cos`, guards silence/non-finite input, and explicitly chooses
the endpoint at exact anti-phase to avoid floating-point contraction changing the
fold seam. Away from the seam it retains the folded angle and peak-channel radius.
The independently tested Lissajous M/S transform is Visona code. No Ozone source
or Polar Level implementation is included.
