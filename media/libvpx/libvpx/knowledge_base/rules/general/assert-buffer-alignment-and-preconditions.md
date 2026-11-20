---
name: libvpx/assert-buffer-alignment-and-preconditions
description: Assert buffer, pointer, and stride alignment preconditions by casting to uintptr_t or intptr_t with (N - 1) bitmasks or modulo rather than comments alone
rubric: correctness
severity: blocking
---

**Rule.** Enforce function preconditions, buffer alignment, and row stride
constraints using assert() in both C reference functions and SIMD
implementations (such as when casting byte pointers like uint8_t* to wider types
like uint32_t* or uint16_t* for multi-byte lane operations like vld1_lane_u32 or
vst1_lane_u32), rather than relying solely on comments. When asserting alignment
to an N-byte boundary (e.g., 2, 4, 16, or 32 bytes), cast pointers to uintptr_t
or intptr_t before bitwise masking with (N - 1) (e.g., ((uintptr_t)ptr & 0xf) ==
0 for 16-byte alignment) or using modulo % N / % sizeof(...) (e.g.,
assert(!((intptr_t)dest % sizeof(uint32_t))) and assert(!(dest_stride %
sizeof(uint32_t)))). Never apply bitwise operators directly to raw pointer types
or use incorrect masks like 0xff for 16-byte alignment.

**Why.** SIMD and DSP routines rely on strict memory and stride alignment for
vector loads, stores, and multi-byte lane accesses. Explanatory comments do not
catch contract violations at runtime, whereas assert() statements catch
misaligned buffers, strides, or invalid arguments immediately during testing and
debug builds. In C and C++, bitwise operators cannot be applied directly to raw
pointer types without casting to uintptr_t or intptr_t. Furthermore, an N-byte
alignment mask must be (N - 1) (e.g., 0xf for 16 bytes, 0x1f for 32 bytes; 0xff
erroneously checks for 256-byte alignment), and casting byte buffers for
multi-byte lane access without asserting pointer and stride alignment risks
undefined behavior or hardware alignment faults.

## Examples

Bad:

```cpp
/* comp_pred and pred must be 16 byte aligned. */
void vpx_comp_avg_pred_c(uint8_t *comp_pred, const uint8_t *pred, int width,
                         int height, const uint8_t *ref, int ref_stride) {
  // Reliance solely on comments leaves alignment contract unenforced.
}

/* Invalid: bitwise & cannot be applied directly to pointer types */
assert((comp_pred & 0xf) == 0);

/* Invalid: 0xff checks 256-byte alignment instead of 16-byte alignment */
assert(((intptr_t)pred & 0xff) == 0);

// Missing alignment assertions when casting byte pointers for multi-byte lane access:
void vpx_idct4x4_16_add_neon(const tran_low_t *input, uint8_t *dest,
                             int dest_stride) {
  uint32x2_t d01 = vdup_n_u32(0);
  d01 = vld1_lane_u32((const uint32_t *)dest, dest01_u32, 0);
  d01 = vld1_lane_u32((const uint32_t *)(dest + dest_stride), dest01_u32, 1);
}
```

Good:

```cpp
void vpx_comp_avg_pred_c(uint8_t *comp_pred, const uint8_t *pred, int width,
                         int height, const uint8_t *ref, int ref_stride) {
  /* Cast pointer to uintptr_t/intptr_t and mask with (16 - 1) = 0xf */
  assert(((uintptr_t)comp_pred & 0xf) == 0);
  assert(((intptr_t)pred & 0xf) == 0);
}

// Assert both pointer and stride alignment for multi-byte vector lane access:
void vpx_idct4x4_16_add_neon(const tran_low_t *input, uint8_t *dest,
                             int dest_stride) {
  assert(!((intptr_t)dest % sizeof(uint32_t)));
  assert(!(dest_stride % sizeof(uint32_t)));
  uint32x2_t d01 = vdup_n_u32(0);
  d01 = vld1_lane_u32((const uint32_t *)dest, dest01_u32, 0);
  d01 = vld1_lane_u32((const uint32_t *)(dest + dest_stride), dest01_u32, 1);
}
```
