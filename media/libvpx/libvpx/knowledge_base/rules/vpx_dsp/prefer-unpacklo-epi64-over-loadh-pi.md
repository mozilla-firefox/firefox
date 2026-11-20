---
name: libvpx/vpx_dsp/prefer-unpacklo-epi64-over-loadh-pi
description: When combining two 64-bit loads into a 128-bit vector in SSE2, use _mm_unpacklo_epi64 instead of shift-or sequences or raw _mm_loadh_pi casts
rubric: safety
severity: should_fix
---

**Rule.** When loading two 64-bit values into a 128-bit __m128i vector in
SSE2/AVX2 code, use _mm_unpacklo_epi64 (or the loadh_epi64 helper) with
_mm_loadl_epi64. Avoid combining separate 64-bit loads with shift and bitwise OR
(_mm_slli_si128 and _mm_or_si128), and avoid raw _mm_loadh_pi with float/integer
vector casts (_mm_castsi128_ps / _mm_castps_si128).

**Why.** Combining 64-bit loads via shift and bitwise OR (_mm_slli_si128 /
_mm_or_si128) executes redundant instructions and increases register pressure.
Direct vector packing with pure integer intrinsics like _mm_unpacklo_epi64
produces minimal instruction sequences and avoids verbose type-punning casts
between float (__m128/__m64) and integer (__m128i) vectors.

## Examples

Bad:

```cpp
// Bad: Combining two 64-bit loads via shift and bitwise OR
const __m128i r_0 = _mm_loadl_epi64((const __m128i *)ref);
const __m128i r_1 = _mm_loadl_epi64((const __m128i *)(ref + ref_stride));
r = _mm_or_si128(r_0, _mm_slli_si128(r_1, 8));

// Bad: Combining via raw _mm_loadh_pi with float casts
p3p2 = _mm_loadl_epi64((const __m128i *)(s - 3 * p));
p3p2 = _mm_castps_si128(
    _mm_loadh_pi(_mm_castsi128_ps(p3p2), (const __m64 *)(s - 4 * p)));
```

Good:

```cpp
// Good: Combine two 64-bit loads cleanly with _mm_unpacklo_epi64
r = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)ref),
                       _mm_loadl_epi64((const __m128i *)(ref + ref_stride)));

// Good: Load both halves with pure integer intrinsics
p3p2 = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(s - 3 * p)),
                          _mm_loadl_epi64((const __m128i *)(s - 4 * p)));
```
