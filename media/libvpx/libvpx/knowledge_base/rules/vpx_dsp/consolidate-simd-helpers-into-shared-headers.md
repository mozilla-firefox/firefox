---
name: libvpx/vpx_dsp/consolidate-simd-helpers-into-shared-headers
description: When implementing reusable SIMD helpers or transform constants, consolidate them into shared architecture headers instead of duplicating them across individual DSP files
rubric: design
severity: should_fix
---

**Rule.** Factor reusable architecture-specific SIMD helpers (such as reduction
utilities, transpositions, or unaligned memory loads) and shared vector
constants (such as DCT rounding factors, bit shifts, or cosine tables) into
shared architecture headers (e.g., sum_neon.h, mem_neon.h, mem_sse2.h,
txfm_common_<arch>.h) rather than defining duplicate static inline functions or
local static vector constants across individual source files. If consolidation
must be deferred during a targeted change, explicitly add an attributed TODO
comment documenting the shared header target.

**Why.** Duplicating SIMD helper routines and vector constants across multiple
DSP source files causes code bloat, creates synchronization hazards when
constants or intrinsics are updated, and impairs readability. Centralizing
common primitives and tables in shared architecture headers ensures uniform
optimizations and modular code across architecture ports.

## Examples

Bad:

```cpp
// Duplicating static helper functions across DSP source files:
// In vpx_dsp/arm/sad_neon.c
static INLINE uint32_t horizontal_add_16x8(const uint16x8_t vec_16x8) {
  const uint32x4_t a = vpaddlq_u16(vec_16x8);
  const uint64x2_t b = vpaddlq_u32(a);
  const uint32x2_t c = vadd_u32(vreinterpret_u32_u64(vget_low_u64(b)),
                                vreinterpret_u32_u64(vget_high_u64(b)));
  return vget_lane_u32(c, 0);
}

// Defining shared SIMD transform constants in individual source files:
// In vpx_dsp/ppc/fdct32x32_vsx.c
static const int32x4_t vec_dct_const_rounding = { 8192, 8192, 8192, 8192 };
static const uint32x4_t vec_dct_const_bits = { 14, 14, 14, 14 };
static const uint16x8_t vec_dct_scale_log2 = { 2, 2, 2, 2, 2, 2, 2, 2 };
```

Good:

```cpp
// Consolidate reusable helpers in shared headers (e.g., vpx_dsp/arm/sum_neon.h)
// or track deferred refactoring with an attributed TODO:
// TODO(author): combine with sum_neon.h / avg_neon.h version.

// Place shared transform constants in common headers (vpx_dsp/ppc/txfm_common_vsx.h):
// In vpx_dsp/ppc/txfm_common_vsx.h
static const int32x4_t vec_dct_const_rounding = { 8192, 8192, 8192, 8192 };
static const uint32x4_t vec_dct_const_bits = { 14, 14, 14, 14 };
static const uint16x8_t vec_dct_scale_log2 = { 2, 2, 2, 2, 2, 2, 2, 2 };

// In vpx_dsp/ppc/fdct32x32_vsx.c
#include "vpx_dsp/ppc/txfm_common_vsx.h"
```
