---
name: libvpx/vpx_dsp/use-tran-coef-t-for-transform-constants
description: In DSP transform routines, declare and cast multiplier constants as tran_coef_t, coefficient buffers as tran_low_t, and intermediates as tran_high_t rather than raw primitive integer types
rubric: correctness
severity: should_fix
---

**Rule.** In DSP transform kernels, 1D/2D DCT/ADST implementations, and
butterfly helper functions, declare and cast transform parameters, constants,
and buffers using dedicated transform typedefs (tran_coef_t, tran_low_t,
tran_high_t) defined in vpx_dsp_common.h rather than hardcoding primitive
integer types (such as int16_t, int32_t, or int64_t). Specifically, use
tran_coef_t (or const tran_coef_t) for fixed multiplier coefficients (such as
cospi_*_64 and sinpi angle constants), reserve tran_low_t for transform
input/output coefficient buffers, and use tran_high_t for intermediate
calculation buffers and accumulators. Avoid typing multiplier constants or
helper arguments as tran_high_t or raw primitive integers when passing or
shifting values into 32-bit registers, scalar expressions, or SIMD intrinsics.

**Why.** Libvpx defines tran_low_t (16-bit in standard builds, 32-bit under
CONFIG_VP9_HIGHBITDEPTH), tran_high_t (32-bit in standard builds, 64-bit under
CONFIG_VP9_HIGHBITDEPTH), and tran_coef_t (fixed 16-bit multiplier constants) in
vpx_dsp_common.h to abstract coefficient bit depths across build configurations.
Hardcoding primitive types (int16_t, int32_t, int64_t) or casting to them breaks
high-bitdepth build compatibility. Furthermore, typing multiplier constants or
parameters as tran_high_t widens them to 64-bit on high-bitdepth builds,
triggering -Wshorten-64-to-32 compiler warnings, requiring unnecessary explicit
casts (e.g., (int32_t)c), and risking truncation bugs when passing values to
32-bit SIMD intrinsics (such as vdupq_n_s32, vmull_n_s32, or vmlal_n_s32). Using
the appropriate transform typedef maintains signature consistency and type
correctness across all build configurations.

## Examples

Bad:

```cpp
// Bad: Hardcoding primitive types or using tran_high_t for multiplier constants
// causes truncation warnings and forces unnecessary casts in SIMD intrinsics.
void fdct4x4_stage(const int32_t *input, int32_t *output, const int32_t constant) {
  const int64x2_t sum_lo = vmlal_n_s32(a64_lo, b_lo, (int32_t)constant);
}

static INLINE void highbd_butterfly_one_coeff_s32(
    const int32x4_t a, const int32x4_t b, const tran_high_t c,
    int32x4_t *add, int32x4_t *sub) {
  const int64x2_t a64_lo = vmull_n_s32(a_lo, (int32_t)c);
  const int64x2_t sum_lo = vmlal_n_s32(a64_lo, b_lo, (tran_low_t)c);
}
```

Good:

```cpp
// Good: Using tran_low_t for coefficient buffers and tran_coef_t for multiplier constants.
void fdct4x4_stage(const tran_low_t *input, tran_low_t *output, const tran_coef_t constant) {
  const int64x2_t sum_lo = vmlal_n_s32(a64_lo, b_lo, (tran_low_t)constant);
}

static INLINE void highbd_butterfly_one_coeff_s32(
    const int32x4_t a, const int32x4_t b, const tran_coef_t c,
    int32x4_t *add, int32x4_t *sub) {
  const int64x2_t a64_lo = vmull_n_s32(a_lo, c);
  const int64x2_t sum_lo = vmlal_n_s32(a64_lo, b_lo, c);
}
```
