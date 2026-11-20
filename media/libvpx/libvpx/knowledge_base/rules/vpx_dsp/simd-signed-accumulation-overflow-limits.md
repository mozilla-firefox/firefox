---
name: libvpx/vpx_dsp/simd-signed-accumulation-overflow-limits
description: When asserting against overflow in SIMD accumulation loops, compute limits using signed ranges (INT16_MAX / 255) and actual block dimension parameters
rubric: correctness
severity: should_fix
---

**Rule.** When accumulating signed pixel differences (ranging from -255 to +255
for 8-bit inputs) into fixed-width SIMD registers (such as int16x8_t or
__m128i), compute overflow limits against the signed range (e.g., INT16_MAX /
255 = 32767 / 255 ≈ 128 additions per lane) rather than unsigned bounds (65535 /
255 ≈ 256), and enforce maximum dimensions with assert(). In parameterized
kernels with variable block dimensions, calculate accumulator lane limits using
the actual w and h parameters and vector lane count (e.g., (w / 8) * h <= 128)
rather than hardcoding constants copied from fixed-size variants.

**Why.** Pixel differences are signed values; calculating thresholds against
unsigned maxima (65535) or hardcoding fixed dimensions in variable-sized kernels
allows 16-bit registers to overflow past 32767 into negative numbers, silently
corrupting variance and sum computations. Exact assertions based on signed lane
capacities and actual block dimensions prevent undetected overflow bugs across
varying block sizes.

## Examples

Bad:

```cpp
// Variant 1: Unsigned calculation ignores signed overflow (32767 / 255 ≈ 128).
static void variance_neon_w8(const uint8_t *a, int a_stride, const uint8_t *b,
                             int b_stride, int h, uint32_t *sse, int *sum) {
  int16x8_t sum_s16 = vdupq_n_s16(0);
  assert(h <= 256);  // Bad: 256 * 255 = 65280 overflows int16x8_t.
}

// Variant 2: Hardcoding fixed dimensions in parameterized kernel ignores w.
static void variance_neon_w16(const uint8_t *a, int a_stride, const uint8_t *b,
                              int b_stride, int w, int h, uint32_t *sse,
                              int *sum) {
  assert(4 * h < 2048);  // Bad: hardcoded constant ignores variable width w.
}
```

Good:

```cpp
// Variant 1: Calculate overflow threshold using signed limit (128 per lane).
static void variance_neon_w8(const uint8_t *a, int a_stride, const uint8_t *b,
                             int b_stride, int h, uint32_t *sse, int *sum) {
  int16x8_t sum_s16 = vdupq_n_s16(0);
  assert(h <= 128);  // 128 additions per lane <= 32767 / 255.
}

// Variant 2: Compute lane accumulation bounds from actual dimensions w and h.
static void variance_neon_w16(const uint8_t *a, int a_stride, const uint8_t *b,
                              int b_stride, int w, int h, uint32_t *sse,
                              int *sum) {
  assert((w / 8) * h <= 128);  // Accounts for 8-wide SIMD register and w, h.
}
```
