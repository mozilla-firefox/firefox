---
name: libvpx/vpx_dsp/unify-high-bitdepth-dsp-metric-functions
description: When supporting high bitdepth in DSP and metric functions, extend the existing function with bit_depth parameters instead of duplicating separate _hbd_ entry points or pipelines.
rubric: design
severity: should_fix
---

**Rule.** When adding high-bitdepth (10-bit or 12-bit) support to DSP and
quality metric functions (such as SSIM, PSNR, FastSSIM, or PSNR-HVS in vpx_dsp),
extend existing function signatures with bit_depth (or bd / in_bd) parameters
rather than creating separate duplicate functions with _hbd_ or _highbd_ names.
Unify the core algorithm pipeline into a single function that internally
branches on bit depth or operates on converted short buffer pointers (using
CONVERT_TO_SHORTPTR under #if CONFIG_VP9_HIGHBITDEPTH).

**Why.** Duplicating entire metric pipelines or DSP algorithms (such as DCT
transforms, variance calculations, masking, and error accumulation) causes code
bloat, proliferates duplicate public entry points, and creates synchronization
hazards where bug fixes or optimizations in the 8-bit path are missed in the
high-bitdepth path.

## Examples

Bad:

```cpp
// Creating duplicate entry points for high-bitdepth metrics
double vpx_calc_fastssim(const YV12_BUFFER_CONFIG *source,
                         const YV12_BUFFER_CONFIG *dest,
                         double *ssim_y, double *ssim_u, double *ssim_v);
double vpx_calc_hbd_fastssim(const YV12_BUFFER_CONFIG *source,
                             const YV12_BUFFER_CONFIG *dest,
                             double *ssim_y, double *ssim_u, double *ssim_v,
                             uint32_t bd, uint32_t in_bd);

// Duplicating entire DSP/metric algorithm pipelines
#if CONFIG_VP9_HIGHBITDEPTH
static double calc_hbd_psnrhvs(const uint16_t *src, int src_stride,
                               const uint16_t *dst, int dst_stride, int w, int h,
                               uint32_t bit_depth) {
  // Duplicated ~100 lines of DCT, variance, masking, and error computation for 10/12-bit
}
#endif
static double calc_psnrhvs(const uint8_t *src, int src_stride,
                           const uint8_t *dst, int dst_stride, int w, int h) {
  // Duplicated ~100 lines of identical algorithm logic for 8-bit
}
```

Good:

```cpp
// Extending the existing function with bit depth parameters
double vpx_calc_fastssim(const YV12_BUFFER_CONFIG *source,
                         const YV12_BUFFER_CONFIG *dest,
                         double *ssim_y, double *ssim_u, double *ssim_v,
                         uint32_t bd, uint32_t in_bd);

// Unifying algorithm computation with internal bit-depth branching / short pointers
static double calc_psnrhvs(const unsigned char *src, int src_stride,
                           const unsigned char *dst, int dst_stride, int w, int h,
                           uint32_t bit_depth, uint32_t shift) {
#if CONFIG_VP9_HIGHBITDEPTH
  const uint16_t *src16 = CONVERT_TO_SHORTPTR(src);
  // Shared DCT, variance, masking, and error accumulation logic
#endif
}
```
