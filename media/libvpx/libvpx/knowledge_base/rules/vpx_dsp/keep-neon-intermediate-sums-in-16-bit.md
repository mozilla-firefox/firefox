---
name: libvpx/vpx_dsp/keep-neon-intermediate-sums-in-16-bit
description: Keep intermediate sums, linear combinations, and multiplications in 16-bit NEON vectors (vmulq_s16/vaddq_s16) instead of widening to 32-bit when values fit within 16-bit range
rubric: safety
severity: should_fix
---

**Rule.** In ARM NEON vector kernels (such as deblocking, filtering,
convolution, quantization, or scaling), keep intermediate sums, linear
combinations, and multiplications in 16-bit vector types (int16x4_t, int16x8_t,
or uint16x8_t) whenever the results and accumulated values fit within the 16-bit
range:
- Do not prematurely widen to dual 32-bit vectors (int32x4_t or uint32x4_t) with vmovl_s16/vmovl_u16 before performing additions, subtractions, or multiplications that safely fit in 16 bits.
- When multiplying 16-bit vector operands where the mathematical result fits within 16 bits (such as standard bitdepth quantization, dequantization, or scaling), use direct same-width multiplication (vmulq_s16/vmulq_u16 or vmul_s16/vmul_u16) instead of widening inputs with vmull_s16/vmull_u16 and narrowing the product back with vmovn_s32/vmovn_u32 and vcombine_s16.
Reserve 32-bit vector accumulators and widening multiplication (vmull_*) for operations (such as sum of squares, large matrix products, or high-bitdepth output) that genuinely require 32-bit precision.

**Why.** Operating directly on 16-bit vectors processes twice as many lanes per
instruction (8 lanes in a 128-bit q register or 4 lanes in a 64-bit d register),
eliminates unnecessary vector splitting, widening, and narrowing unpack
instructions (vget_low_*, vget_high_*, vmovl_*, vmull_*, vmovn_*, vcombine_*),
and reduces vector register pressure. Prematurely widening to dual 32-bit
vectors or widening and narrowing products doubles instruction count and
degrades SIMD throughput without numerical benefit.

## Examples

Bad:

```cpp
// Anti-pattern 1: Widening intermediate filter arithmetic to dual 32-bit vectors
int32x4_t sum_l = vmovl_s16(vget_low_s16(sum_tmp));
int32x4_t sum_r = vmovl_s16(vget_high_s16(sum_tmp));
sum_l = vmulq_n_s32(sum_l, 9);
sum_r = vmulq_n_s32(sum_r, 9);

// Anti-pattern 2: Unnecessary widening to 32-bit and narrowing back for 16-bit multiply
const int32x4_t prod_lo =
    vmull_s16(vget_low_s16(coeff), vget_low_s16(scale));
const int32x4_t prod_hi =
    vmull_s16(vget_high_s16(coeff), vget_high_s16(scale));
vst1q_s16(out_ptr, vcombine_s16(vmovn_s32(prod_lo), vmovn_s32(prod_hi)));
```

Good:

```cpp
// Solution 1: Keep intermediate filter calculation in a single 8-lane 16-bit vector
int16x8_t sum = vmulq_n_s16(sum_tmp, 9);
sum = vaddq_s16(sum, b);

// Solution 2: Direct 16-bit multiplication in a single SIMD instruction
vst1q_s16(out_ptr, vmulq_s16(coeff, scale));
```
