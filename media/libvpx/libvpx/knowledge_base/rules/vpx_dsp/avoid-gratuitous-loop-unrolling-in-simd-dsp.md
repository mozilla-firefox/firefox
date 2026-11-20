---
name: libvpx/vpx_dsp/avoid-gratuitous-loop-unrolling-in-simd-dsp
description: When implementing SIMD DSP routines processing large blocks, use structured loops instead of manually unrolling iterations unless profiling demonstrates significant performance gains
rubric: safety
severity: should_fix
---

**Rule.** When implementing SIMD DSP functions processing large blocks (such as
16x16, 32x32, or 64x64 intra predictors, filters, or transforms), structure
repetitive operations in compact loops rather than manually unrolling all rows
or blocks. Do not introduce full manual unrolling unless benchmark profiling or
assembly inspection confirms a substantial performance improvement over
structured loops.

**Why.** Fully unrolling multi-row SIMD operations across large blocks causes
significant binary size bloat and instruction cache pressure without meaningful
performance gains on modern superscalar architectures, while making the routine
harder to maintain and review.

## Examples

Bad:

```cpp
// Anti-pattern: Manually unrolling all 32 rows across repeated blocks
dst += 31 * stride;
vst1q_u16(dst, row_0);
dst += 8;
vst1q_u16(dst, row_1);
dst -= stride + 8;
row_0 = vextq_u16(row_0, row_1, 1);
// ... repeated dozens of times for each remaining row ...
```

Good:

```cpp
// Preferred: Compact structured loop iterating over rows and blocks
dst += 31 * stride;
for (i = 0; i < 4; ++i) {
  for (j = 0; j < 8; ++j) {
    vst1q_u16(dst, row_0);
    dst += 8;
    vst1q_u16(dst, row_1);
    dst -= stride + 8;
    row_0 = vextq_u16(row_0, row_1, 1);
  }
  row_0 = next_row[i];
}
```
