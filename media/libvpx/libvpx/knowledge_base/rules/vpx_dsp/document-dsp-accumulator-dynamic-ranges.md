---
name: libvpx/vpx_dsp/document-dsp-accumulator-dynamic-ranges
description: When implementing, porting, or refactoring DSP and SIMD kernels, document input bit depths, value ranges, and accumulator dynamic ranges to justify vector types and prevent integer overflow
rubric: readability
severity: should_fix
---

**Rule.** In DSP and SIMD kernel implementations (such as SATD, SAD, variance,
transform, and filtering routines), document and retain bit depths, value
ranges, and mathematical dynamic ranges for inputs, outputs, and intermediate
accumulators. Group these bounds annotations in comments at the function header
or directly above key accumulation stages to justify chosen vector types (e.g.,
int32x4_t vs. int64x2_t or tran_high_t) and demonstrate that multi-pass
arithmetic will not overflow across maximum block sizes.

**Why.** DSP and SIMD kernels frequently require wider intermediate accumulator
registers or multi-pass reductions to prevent overflow on extreme pixel or
coefficient values. Documenting explicit mathematical dynamic ranges and bit
depths clarifies why specific accumulator sizes are required, verifies
arithmetic safety across target architectures, simplifies review, and prevents
overflow regressions during refactoring or optimization.

## Examples

Bad:

```cpp
// Missing dynamic range documentation or omitting output/accumulator ranges:
int vpx_highbd_satd_neon(const tran_low_t *coeff, int length) {
  int64x2_t sum_s64[2] = { vdupq_n_s64(0), vdupq_n_s64(0) };
  // Accumulate absolute values into 64-bit vectors without documenting why 64-bit is required...
  return (int)horizontal_add_int64x2(vaddq_s64(sum_s64[0], sum_s64[1]));
}

// coeff: 16 bits, dynamic range [-32640, 32640].
// length: value range {16, 64, 256, 1024}.
int vpx_satd_neon(const tran_low_t *coeff, int length) {
  // Accumulation without documenting output/accumulator dynamic range bounds...
  ...
}
```

Good:

```cpp
// coeff: 32 bits, dynamic range [-2147483648, 2147483647].
// length: value range {16, 64, 256, 1024}.
// satd: 42 bits, dynamic range [-2147483648 * 1024, 2147483647 * 1024].
// Requires 64-bit accumulators to prevent overflow on 1024-element blocks.
int vpx_highbd_satd_neon(const tran_low_t *coeff, int length) {
  int64x2_t sum_s64[2] = { vdupq_n_s64(0), vdupq_n_s64(0) };
  // Accumulate absolute values into 64-bit vectors...
  return (int)horizontal_add_int64x2(vaddq_s64(sum_s64[0], sum_s64[1]));
}

// coeff: 16 bits, dynamic range [-32640, 32640].
// length: value range {16, 64, 256, 1024}.
// satd: 26 bits, dynamic range [-32640 * 1024, 32640 * 1024].
int vpx_satd_neon(const tran_low_t *coeff, int length) {
  ...
}
```
