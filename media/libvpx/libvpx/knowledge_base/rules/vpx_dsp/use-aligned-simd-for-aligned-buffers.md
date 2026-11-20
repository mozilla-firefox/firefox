---
name: libvpx/vpx_dsp/use-aligned-simd-for-aligned-buffers
description: In SIMD DSP routines, use aligned loads and stores with assertions for guaranteed-aligned buffers, and unaligned intrinsics for potentially unaligned inputs or sub-blocks
rubric: correctness
severity: blocking
---

**Rule.** In SIMD DSP implementations, match memory load and store intrinsics to
buffer alignment guarantees. Use aligned intrinsics (such as _mm_load_si128 /
_mm_store_si128 or _mm256_load_si256 / _mm256_store_si256) with explicit
alignment assertions (e.g., assert(((intptr_t)ptr & 0xf) == 0)) when buffers are
guaranteed to be 16- or 32-byte aligned. Conversely, use unaligned intrinsics
(such as _mm_loadu_si128 / _mm_storeu_si128 or _mm256_loadu_si256) when reading
or writing buffers that may not be aligned (such as 4x4 intra predictor inputs,
reference frame offsets, or offset pointers like above - 1).

**Why.** Using aligned SIMD load or store intrinsics on unaligned addresses
triggers hardware alignment exceptions (general protection faults / SIGSEGV).
For guaranteed-aligned buffers, using aligned operations with assertions
documents preconditions and maximizes SIMD memory throughput.

## Examples

Bad:

```cpp
// Hazard 1: Using aligned load on potentially unaligned 4x4 intra predictor inputs
const __m128i above_row = _mm_load_si128((const __m128i *)above);

// Hazard 2: Defaulting to unaligned ops on guaranteed-aligned prediction buffers without assertions
const __m128i p = _mm_loadu_si128((const __m128i *)(pred + x));
const __m128i r = _mm_loadu_si128((const __m128i *)(ref + x));
_mm_storeu_si128((__m128i *)(comp_pred + x), _mm_avg_epu8(p, r));
```

Good:

```cpp
// Case 1: Use unaligned load when input buffer alignment is not guaranteed
const __m128i above_row = _mm_loadu_si128((const __m128i *)above);

// Case 2: Use aligned ops with assertions for guaranteed 16-byte aligned buffers
assert(((intptr_t)comp_pred & 0xf) == 0);
assert(((intptr_t)pred & 0xf) == 0);
const __m128i p = _mm_load_si128((const __m128i *)(pred + x));
const __m128i r = _mm_loadu_si128((const __m128i *)(ref + x));
_mm_store_si128((__m128i *)(comp_pred + x), _mm_avg_epu8(p, r));
```
