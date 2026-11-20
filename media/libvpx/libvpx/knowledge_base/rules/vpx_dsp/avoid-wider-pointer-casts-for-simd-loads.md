---
name: libvpx/vpx_dsp/avoid-wider-pointer-casts-for-simd-loads
description: When loading unaligned or sub-block buffers in NEON routines, do not cast narrow pointers to wider types; use unaligned helpers, memcpy, or element-sized loads
rubric: correctness
severity: blocking
---

**Rule.** Do not cast narrow or unaligned element pointers (such as uint8_t* or
uint16_t*) to wider integer types (such as uint32_t* or int64_t*) for SIMD
vector loads (e.g., vld1_lane_u32 or vld1q_dup_s64) when alignment is not
guaranteed. For unaligned byte offsets, use unaligned load helpers (such as
load_unaligned_u8_* from mem_neon.h) or memcpy to avoid SIGBUS alignment faults
on ARMv7; for sub-block routines with narrow alignment guarantees, use
element-sized loads (such as vld1_s16) combined with vector concatenation
(vcombine_s16) or duplication (vdup_lane_s16).

**Why.** Casting unaligned or narrow-aligned buffers to wider typed pointers
invokes undefined behavior in C and generates instructions that trigger SIGBUS
alignment faults or performance penalties on strict-alignment CPU architectures
like ARMv7. Using dedicated unaligned load helpers, memcpy, or element-sized
vector operations safely populates SIMD registers without violating memory
alignment guarantees.

## Examples

Bad:

```cpp
// Bad: Casting unaligned uint8_t* byte offset to uint32_t* triggers ARMv7 SIGBUS
a1 = vreinterpret_u8_u32(
    vld1_lane_u32((const uint32_t *)(above + 1), zero, 0));

// Bad: Casting uint16_t* buffer with 2-byte alignment to int64_t* for 64-bit load
const int16x8_t above_s16 =
    vreinterpretq_s16_s64(vld1q_dup_s64((const int64_t *)above));
```

Good:

```cpp
// Good: Safe unaligned byte load via memcpy or unaligned load helpers
memcpy(&a1, above + 1, 4);

// Good: Safe element-sized load preserving 2-byte alignment guarantees
const int16x4_t above_s16d = vld1_s16((const int16_t *)above);
const int16x8_t above_s16 = vcombine_s16(above_s16d, above_s16d);
```
