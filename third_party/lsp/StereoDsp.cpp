// Bind the six DSP entry points used by LSP's Correlometer and Panometer to
// unmodified, portable upstream implementations. This is not the full LSP API.
// Constant initialization avoids process-global runtime dispatch and its locks.
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/stdlib/math.h>

#define PRIVATE_DSP_ARCH_GENERIC_IMPL
#include <private/dsp/arch/generic/copy.h>
#include <private/dsp/arch/generic/correlation.h>
#include <private/dsp/arch/generic/hmath/hsum.h>
#include <private/dsp/arch/generic/pmath/sqr.h>

namespace lsp::dsp
{
constinit decltype(copy) copy = generic::copy;
constinit decltype(fill_zero) fill_zero = generic::fill_zero;
constinit decltype(corr_init) corr_init = generic::corr_init;
constinit decltype(corr_incr) corr_incr = generic::corr_incr;
constinit decltype(h_sum) h_sum = generic::h_sum;
constinit decltype(sqr2) sqr2 = generic::sqr2;
} // namespace lsp::dsp
