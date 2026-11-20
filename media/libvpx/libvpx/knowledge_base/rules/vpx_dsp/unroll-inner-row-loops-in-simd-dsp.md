---
name: libvpx/vpx_dsp/unroll-inner-row-loops-in-simd-dsp
description: When implementing performance-critical SIMD DSP kernel loops with dynamic bounds, unroll inner row loops by 2 or 4 rows per iteration with remainder handling
rubric: safety
severity: should_fix
---

**Rule.** In performance-critical SIMD DSP functions (such as convolution,
subpixel filtering, transform, or highbd kernels) where loop bounds like block
height or row count are runtime-variable, manually unroll inner row-processing
loops to process multi-row blocks (e.g., 2 or 4 rows per iteration) before
handling remainder rows. Avoid single-row or minimal 1-row loop iterations with
dynamic bounds when consecutive row operations can be interleaved to assist
compiler instruction scheduling.

**Why.** Compilers cannot statically determine trip counts for dynamic function
parameters (such as block height or row count) and often fail to auto-unroll
vector loops. Manually unrolling inner row loops to process 2- or 4-row blocks
reduces loop branching overhead, hides memory load latency, and improves
instruction-level parallelism across SIMD execution ports.

## Examples

Bad:

```cpp
// Anti-pattern 1: Processing only single rows per iteration with dynamic height
do {
  highbd_convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  h -= 1;
} while (h > 0);

// Anti-pattern 2: Processing minimal 2-row increments in hot convolution loops
while (y > 1) {
  CALC_CONVOLVE8_HORZ_ROW(src_ptr, output_ptr);
  CALC_CONVOLVE8_HORZ_ROW(src_ptr + src_stride, output_ptr + output_pitch);
  src_ptr += 2 * src_stride;
  output_ptr += 2 * output_pitch;
  y -= 2;
}
```

Good:

```cpp
// Variant 1: Unroll by 2 rows with dynamic remainder handling
do {
  highbd_convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  highbd_convolve_copy_row(src, dst, width);
  src += src_stride;
  dst += dst_stride;
  h -= 2;
} while (h > 1);
if (h) {
  highbd_convolve_copy_row(src, dst, width);
}

// Variant 2: Unroll by 4 rows with stepped remainder loops for convolution
while (y > 3) {
  CALC_CONVOLVE8_HORZ_ROW(src_ptr, output_ptr);
  CALC_CONVOLVE8_HORZ_ROW(src_ptr + src_stride, output_ptr + output_pitch);
  CALC_CONVOLVE8_HORZ_ROW(src_ptr + 2 * src_stride, output_ptr + 2 * output_pitch);
  CALC_CONVOLVE8_HORZ_ROW(src_ptr + 3 * src_stride, output_ptr + 3 * output_pitch);
  src_ptr += 4 * src_stride;
  output_ptr += 4 * output_pitch;
  y -= 4;
}
while (y > 1) {
  CALC_CONVOLVE8_HORZ_ROW(src_ptr, output_ptr);
  CALC_CONVOLVE8_HORZ_ROW(src_ptr + src_stride, output_ptr + output_pitch);
  src_ptr += 2 * src_stride;
  output_ptr += 2 * output_pitch;
  y -= 2;
}
```
