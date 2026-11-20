---
name: libvpx/vpx_dsp/combine-neon-multiply-and-accumulate-or-subtract
description: In ARM NEON SIMD code, use fused and widening accumulate or subtract intrinsics (vmla, vmls, vmlal, vmlsl, vabal, vpadal) instead of separate multiply/difference/widen and add/sub operations
rubric: safety
severity: should_fix
---

**Rule.** In ARM NEON SIMD implementations, use fused or widening accumulate and
subtract intrinsics directly instead of chaining separate arithmetic and
addition/subtraction operations:
- For multiply-accumulate or multiply-subtract (`acc + (x * y)` or `acc - (x * y)`), use `vmla_*`, `vmls_*`, `vmlaq_*`, or `vmlsq_*` for same-width operations, and `vmlal_*` (e.g., `vmlal_u16`, `vmlal_s16`, `vmlal_u8`, `vmlal_s8`, `vmlal_u32`, `vmlal_s32`, `vmlal_lane_*`) or `vmlsl_*` for widening operations, rather than separate `vmul_*`/`vmull_*` followed by `vadd_*`/`vsub_*` or `vmovl_*`.
- For accumulating absolute differences (`acc + |x - y|`), use widening accumulate `vabal_*` directly instead of separate `vabdl_*` followed by `vaddq_*`.
- For accumulating pairwise sums or saturating doubling products, use `vpadal_*`/`vpadalq_*` or `vqdmlal_*`/`vqdmlsl_*` directly.
Avoid executing standalone multiplication, widening unpack (`vmovl_*`), or difference operations followed by separate vector additions (`vaddq_*`) or subtractions (`vsubq_*`).

**Why.** Fused and widening accumulate and subtract intrinsics (such as `vmla`,
`vmls`, `vmlal`, `vmlsl`, `vabal`, and `vpadal`) combine arithmetic operations,
bit-width widening, and vector accumulation or subtraction into single hardware
instructions. Compilers cannot be relied upon to automatically fuse separate
widening/arithmetic instructions and vector additions. Using fused intrinsics
directly eliminates intermediate widening instructions (like `vmovl_*`),
minimizes vector register pressure, and significantly reduces instruction count
and pipeline latency in performance-critical DSP and codec loops.

## Examples

Bad:

```cpp
// Anti-pattern 1: Separate multiply, widen unpack, and vector add
pred_u16 = vmulq_u16(sum_u16, pred_u16);
pred_0_u32 = vmovl_u16(vget_low_u16(pred_u16));
accum_0_u32 = vaddq_u32(pred_0_u32, accum_0_u32);

// Anti-pattern 2: Separate multiply and subtract
const int32x4_t a = vmulq_s32(sum, sum);
const int32x4_t b = vmulq_s32(sumsq, fifteen);
const int32x4_t b_a = vsubq_s32(b, a);

// Anti-pattern 3: Separate widening absolute difference and add
const uint16x8_t diff0 = vabdl_u8(vget_low_u8(src), vget_low_u8(ref));
const uint16x8_t diff1 = vabdl_u8(vget_high_u8(src), vget_high_u8(ref));
const uint16x8_t sad = vaddq_u16(diff0, diff1);
```

Good:

```cpp
// Solution 1: Fused widening multiply-accumulate (vmlal)
accum_0_u32 =
    vmlal_u16(accum_0_u32, vget_low_u16(sum_u16), vget_low_u16(pred_u16));

// Solution 2: Combined multiply-subtract (vmlsl)
const int32x4_t b = vmulq_s32(sumsq, fifteen);
const int32x4_t b_a = vmlsl_s16(b, sum, sum);

// Solution 3: Direct widening absolute difference accumulate (vabal)
uint16x8_t sad = vabdl_u8(vget_low_u8(src), vget_low_u8(ref));
sad = vabal_u8(sad, vget_high_u8(src), vget_high_u8(ref));
```
