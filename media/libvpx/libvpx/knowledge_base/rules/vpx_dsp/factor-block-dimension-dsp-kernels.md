---
name: libvpx/vpx_dsp/factor-block-dimension-dsp-kernels
description: When implementing architecture-specific DSP kernels across multiple block dimensions, factor common loops into parameterized inline helpers and instantiate dimension variants in ascending order
rubric: readability
severity: should_fix
---

**Rule.** Factor repetitive architecture-specific DSP and SIMD kernel
implementations (such as variance, SAD, MSE, or subpel filtering) that differ
only by block dimensions into static inline helper functions parameterized by
dimension. Instantiate or declare individual block-size entry points as thin
wrappers or macro instantiations around the helper in consistent ascending order
(e.g., width then height: 8x4, 8x8, 8x16).

**Why.** Factoring dimension-parameterized kernels eliminates code duplication
across block sizes, reduces binary size, and ensures algorithms remain unified
in one place. Ordering macro instantiations and dimension entry points
monotonically makes declarations easy to scan and prevents omitted or duplicate
dimension variants.

## Examples

Bad:

```cpp
// Bad: Duplicating assembly routines across dimensions and unordered macro instantiations
uint32_t vpx_mse16x16_mmi(...) {
  __asm__ volatile ("li %[tmp0], 0x10 \n\t" VARIANCE_SSE_16 ...);
  return *sse;
}
uint32_t vpx_mse16x8_mmi(...) {
  __asm__ volatile ("li %[tmp0], 0x08 \n\t" VARIANCE_SSE_16 ...);
  return *sse;
}

// Unordered dimension instantiations:
SAD8(4);
SAD8(16);
SAD8(8);
```

Good:

```cpp
// Good: Parameterized helper and ascending macro instantiations
static inline uint32_t vpx_mse16x(..., uint32_t height) {
  __asm__ volatile (MMI_L(%[tmp0], %[height], 0x00) VARIANCE_SSE_16 ...);
  return *sse;
}
#define vpx_mse16xN(n) ...
vpx_mse16xN(8);
vpx_mse16xN(16);

// Ascending dimension instantiations (8x4, 8x8, 8x16):
SAD8(4);
SAD8(8);
SAD8(16);
```
