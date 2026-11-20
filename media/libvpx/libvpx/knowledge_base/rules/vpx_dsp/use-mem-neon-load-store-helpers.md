---
name: libvpx/vpx_dsp/use-mem-neon-load-store-helpers
description: In ARM NEON SIMD code, use mem_neon.h helpers for strided memory access and use vld1_u8/vld1q_u8 directly for contiguous unaligned loads
rubric: design
severity: blocking
---

**Rule.** In ARM NEON implementations, use centralized memory helpers from
mem_neon.h (such as load_u8(), store_u8(), load_u8_8x4(), or
load_unaligned_u8()) for strided 2D vector blocks or sub-vector chunks rather
than manually chaining vld1_lane_* / vst1_lane_* with explicit vreinterpret_*
casts. For contiguous 8-byte (uint8x8_t) and 16-byte (uint8x16_t) unaligned
loads, use vld1_u8 and vld1q_u8 directly instead of creating temporary stack
buffers or redundant memcpy wrapper functions.

**Why.** Centralized memory helpers in mem_neon.h encapsulate strided lane
indexing and address arithmetic cleanly without verbose lane boilerplate. For
contiguous 8-byte and 16-byte loads, NEON vld1_u8 and vld1q_u8 intrinsics
natively support unaligned byte pointers without alignment penalties, making
intermediate stack buffers and memcpy wrappers redundant overhead.

## Examples

Bad:

```cpp
// 1. Manually chaining lane loads and casts for strided blocks:
dd01 = vld1_lane_u32((const uint32_t *)(src + 0 * stride), vreinterpret_u32_u8(dd01), 0);
dd01 = vld1_lane_u32((const uint32_t *)(src + 1 * stride), vreinterpret_u32_u8(dd01), 1);

// 2. Redundant stack buffer and memcpy wrapper for 8-byte unaligned load:
static INLINE uint8x8_t load_unaligned_u8_8x1(const uint8_t *buf) {
  uint8_t a[8];
  memcpy(a, buf, 8);
  return vld1_u8(a);
}
```

Good:

```cpp
// 1. Use mem_neon.h helper for strided block loading:
dd01 = load_u8(src, stride);

// 2. Use vld1_u8 / vld1q_u8 directly for contiguous unaligned loads:
a0 = vld1_u8(above + 0);
```
