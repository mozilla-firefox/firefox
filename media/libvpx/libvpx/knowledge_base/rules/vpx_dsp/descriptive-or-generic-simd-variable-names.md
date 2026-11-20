---
name: libvpx/vpx_dsp/descriptive-or-generic-simd-variable-names
description: In SIMD and DSP code, use descriptive names for parameters, buffers, accumulators, constant vectors (avoiding assembly pw_* prefixes), shuffle masks, and _low/_high vector halves.
rubric: readability
severity: should_fix
---

**Rule.** In SIMD and DSP kernel implementations and helper functions:
1. **Parameters & Prototypes**: Name helper function parameters descriptively (such as `buf`, `src_ptr`, `ref_ptr`, `dst`, `stride`, `sum_squared`, or `sum`) with standard ordering (`sum_squared` before `sum`). Avoid single-letter pointer names (e.g., `uint8_t *a`) or cryptic abbreviations (e.g., `ps`/`pss`). For scalar multipliers or transform coefficients, use descriptive parameter names (`constant`, `coeff`, `cospi_*`) rather than ambiguous single letters like `c` that collide with sequential buffer identifiers (`a`, `b`, `c`).
2. **Constant Vectors & Shuffle Masks**: When declaring constant SIMD vectors (such as `__m128i`, `__m256i`, or NEON vectors using `_mm_set1_*`, `vdupq_n_*`) and shuffle/permutation/table-lookup masks (`_mm_setr_epi8`, `_mm_shuffle_epi8`, NEON `vtbl`), choose descriptive identifier names reflecting their values, purposes, or data transformations (such as `one`, `zero`, `round_const`, `round`, `zig_zag_*`, `transpose_*`, or `swap_*`). Do not carry over assembly-style type-prefixed labels (such as `pw_*`, `pd_*`, `pb_*`, or `pq_*` for packed word/dword/byte/qword) from `.asm` files into C source code, and avoid generic or opaque identifier names like `idx0`, `mask0`, or `shuf0`.
3. **Intermediate Buffers**: For multi-stage intermediate staging buffers, use clean numbered identifiers (`buffer` and `buffer2`) rather than ambiguous abbreviations like `sec_buffer` or `sec_buf`.
4. **Accumulators & Temporaries**: Name intermediate vectors and accumulator variables using accurately descriptive identifiers (such as `sum`, `sum0`, `out0`, `dc`, `row0_1`, `sum_abs_diff`, `dest`) or standard generic names (such as `a`, `b`, `t0`, `res`, `s`, `d`). Avoid misleading names (such as `abs` for running total accumulators) or pseudo-descriptive names that misrepresent vector layout.
5. **Vector Halves**: When decomposing or processing vector halves (e.g., with `vget_low_*` / `vget_high_*`), use explicit `_low` and `_high` suffixes (such as `sum_low`, `sum_high`, `xy_low`, `xy_high`) rather than shorthand abbreviations like `_l`/`_r` or `_lo`/`_hi`.

**Why.** Cryptic abbreviations (such as `ps`/`pss`, `sec_buf`, or `_l`/`_r`),
single-letter pointer names, assembly-style type prefixes (`pw_*`, `pd_*`),
generic mask names (`idx0`, `mask0`), misleading accumulator names (such as
`abs` for sums), and ambiguous multiplier names (like `c` colliding with buffer
identifiers) obscure vector contents, register packing, and sequential
arithmetic stages. They also increase the risk of argument transposition at call
sites and force readers to manually decode index vectors or assembly relics.
Using standard descriptive parameter names, standard accumulator ordering, clean
numbered buffer identifiers, descriptive constant/mask names, and explicit
`_low`/`_high` suffixes ensures clarity and consistency across DSP and SIMD
routines.

## Examples

Bad:

```cpp
// Bad: Assembly-style prefixes for broadcast constants and opaque mask names.
const __m128i pw_1 = _mm_set1_epi16(1);
const __m128i pd_32 = _mm_set1_epi32(32);
const __m128i idx0 =
    _mm_setr_epi8(0, 1, 2, 3, 8, 9, 14, 15, 10, 11, 4, 5, 6, 7, 12, 13);
zbin = _mm_sub_epi16(zbin, pw_1);

// Bad: Cryptic accumulator parameter names and single-letter pointer.
static INLINE void variance_inner_32(const uint8_t *a, const uint8_t *b,
                                     int32x4_t *ps, int32x4_t *pss);

// Bad: Ad-hoc buffer abbreviations and shorthand _l/_r suffixes.
int16_t sec_buffer[64];
int32x4_t sumsq_l = vmull_s16(vget_low_s16(x), vget_low_s16(y));
```

Good:

```cpp
// Good: Descriptive names reflecting constant values and permutation purposes.
const __m128i one = _mm_set1_epi16(1);
const __m128i round = _mm_set1_epi32(32);
const __m128i zig_zag_i16_0 =
    _mm_setr_epi8(0, 1, 2, 3, 8, 9, 14, 15, 10, 11, 4, 5, 6, 7, 12, 13);
zbin = _mm_sub_epi16(zbin, one);

// Good: Descriptive parameter names and sum_squared-before-sum ordering.
static INLINE void variance_inner_32(const uint8_t *src_ptr,
                                     const uint8_t *ref_ptr,
                                     int32x4_t *sum_squared, int32x4_t *sum);

// Good: Numbered buffer suffix and explicit _low/_high half suffixes.
int16_t buffer2[64];
int32x4_t sumsq_low = vmull_s16(vget_low_s16(x), vget_low_s16(y));
```
