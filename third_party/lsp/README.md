# LSP stereo measurement subset

Visona builds the unmodified LSP `Correlometer`, `Panometer`, and `IStateDumper`
sources using `cmake/LspStereo.cmake`. SHA-256-verified source archives are fetched
by CMake, with no GNU Make, installed LSP library, runtime library, LLTL or JUCE
dependency:

| Project | Pin | License |
| --- | --- | --- |
| [lsp-dsp-units](https://github.com/lsp-plugins/lsp-dsp-units/tree/8dd2a6905ca7ccefd2c3624560235680cb8761e2) | `8dd2a6905ca7ccefd2c3624560235680cb8761e2` (1.0.38) | LGPL-3.0-or-later |
| [lsp-dsp-lib](https://github.com/lsp-plugins/lsp-dsp-lib/tree/1.1.1) | 1.1.1 | LGPL-3.0-or-later |
| [lsp-common-lib](https://github.com/lsp-plugins/lsp-common-lib/tree/1.0.49) | 1.0.49, headers only | LGPL-3.0-or-later |

Copyright belongs to the Linux Studio Plugins Project and the authors credited
in the downloaded files, including Vladimir Sadovnikov. Original notices and full
sources are retained in the fetched trees. `COPYING` and `COPYING.LESSER` accompany
this integration; the pinned URLs and checksums in CMake identify corresponding
source for reproducing a build.

`StereoDsp.cpp` is Visona's build glue, not a replacement DSP implementation. It
binds exactly six LSP function pointers to the upstream generic C++ implementations:
`copy`, `fill_zero`, `corr_init`, `corr_incr`, `h_sum`, and `sqr2`. The generic
implementation headers are included without editing them. The target is static,
position independent and has hidden visibility; its includes/definitions are
private dependencies of `visona_core`. No full LSP dispatch API is exported.

This avoids architecture-specific assembly and global runtime dispatch while
using the same measurement code on each platform. It also allows sanitizer
instrumentation of the complete measurement path. The binding depends on private
upstream headers: review it and rerun the oracle tests when updating pins. Do not
link a second/full LSP implementation into the same binary without reconciling
the symbols.

On MSVC, `cmake/LspMsvc.cmake` generates an overlay for the pinned common
`types.h`: it replaces the unused POSIX `unistd.h` include with a pointer-sized
signed `ssize_t` and detects Windows word size and endianness explicitly. The
fetched sources remain pristine; only this target and consumers of its headers
see the overlay. `StereoDsp.cpp` includes common types before the DSP declarations
so `ssize_t` is available there too. The meter algorithms and generic DSP sources
remain unmodified. Unused upstream GNU attribute and assembly macros are not
expanded by this subset; this is not general MSVC support for the full LSP API.

When changing pins, review the overlay and include order, build the Windows
plugins with MSVC, and run the core oracle and offline processor tests. PR CI
runs that Windows build in addition to the Linux core and sanitizer jobs.

`StereoAnalyzer` works around the pinned
Panometer's incomplete `clear()` by forcing a period change, and starts both
meters fresh for every published window. See [stereo analysis](../../docs/stereo.md)
for the measurements, validity policy and integration tests.
