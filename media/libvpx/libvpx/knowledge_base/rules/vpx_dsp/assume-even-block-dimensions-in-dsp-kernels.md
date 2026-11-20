---
name: libvpx/vpx_dsp/assume-even-block-dimensions-in-dsp-kernels
description: In DSP and SIMD block convolution and processing kernels, assume standard block dimension invariants without adding fallback branches or tail loops for odd heights or non-multiple-of-4 widths
rubric: design
severity: should_fix
---

**Rule.** In DSP and SIMD block convolution, prediction, filtering, copy, and
average routines (vpx_dsp), assume standard codec block dimension invariants
(even height and width multiple of 4). Do not implement dead fallback handling
or leftover byte handling (such as single-row tail loops, odd-height conditional
branches, or memcpy / scalar residue loops for non-multiple-of-4 widths), as
callers (decoder, encoder, and denoiser routines) only invoke these kernels on
valid partition block dimensions (typically 4, 8, 16, 32, or 64).

**Why.** Codec block dimensions in libvpx (VP8/VP9) are strictly
partition-aligned, guaranteeing even heights (multiples of 2 or 4) and widths
that are multiples of 4. Adding fallbacks for odd heights (e.g., when unrolling
loops by two rows) or arbitrary residual pixel widths introduces dead,
untestable code, redundant branch instructions, and unnecessary complexity in
performance-critical DSP routines.

## Examples

Bad:

```cpp
// Bad: Redundant fallback branch for odd height when unrolling rows
do {
  convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  h -= 2;
} while (h > 1);

if (h) {
  convolve_copy_row(src, dst, width);
}

// Bad: Handling arbitrary non-multiple-of-4 leftover width
while (width >= 4) {
  const __m128i p = _mm_loadl_epi64((const __m128i *)src);
  _mm_storel_epi64((__m128i *)dst, p);
  src += 4;
  dst += 4;
  width -= 4;
}
if (width) {
  memcpy(dst, src, width * sizeof(uint16_t));
}
```

Good:

```cpp
// Good: Assume even height invariant across block-processing routines
do {
  convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  h -= 2;
} while (h > 0);

// Good: Assert width multiple-of-4 invariant; omit dead fallback branch
assert(width % 4 == 0);
while (width >= 4) {
  const __m128i p = _mm_loadl_epi64((const __m128i *)src);
  _mm_storel_epi64((__m128i *)dst, p);
  src += 4;
  dst += 4;
  width -= 4;
}
```
