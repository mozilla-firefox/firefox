---
name: libvpx/pass-simd-vectors-by-pointer-for-abi-alignment
description: When defining SIMD helper functions, mark them static INLINE and pass excess or read-only vector parameters (__m128i, __m256i, __m512i) by const pointer for ABI alignment
rubric: correctness
severity: blocking
---

**Rule.** In SIMD helper functions and DSP kernels (especially on x86), mark
functions static INLINE (rather than plain static) and avoid passing excess or
large SIMD vector parameters (__m128, __m128i, __m128d, __m256i, __m512i) by
value across function boundaries. Limit by-value vector parameters to what
target calling conventions support in registers (at most 3 for 32-bit x86, 4 for
64-bit Windows). When a function requires multiple or excess vector inputs,
read-only constant vectors, or modifies vectors in-place, pass them by pointer
(e.g., const __m128i *, const __m128i *const, or __m128i *) or fixed-size array
(e.g., const __m128i in[4]).

**Why.** On 32-bit Windows x86 targets, the stack is only 4-byte aligned (not
16-byte, 32-byte, or 64-byte aligned), and MSVC calling conventions pass at most
three 128-bit vector arguments in registers (four on 64-bit Windows). Passing
vector types to non-inline functions or passing excess vector arguments by value
forces them onto the unaligned stack, triggering MSVC compiler error C2719
('formal parameter with requested alignment of 16 won\'t be aligned') or runtime
alignment faults (crashes on movdqa/vmovdqa). Additionally, passing 16-byte,
32-byte, or 64-byte vector values by value introduces unnecessary copy overhead
and increases register pressure. Marking helpers static INLINE and passing
vector arguments by const pointer satisfies ABI alignment requirements across
MSVC and GCC/Clang while avoiding stack copies.

## Examples

Bad:

```cpp
// Bad: Non-inline helper taking vector values can cause stack-spill alignment crashes
static void multiplication_and_add_2(const __m128i lo_0, const __m128i hi_0,
                                     const __m128i cst0, const __m128i cst1,
                                     __m128i *const res0, __m128i *const res1) {
  *res0 = idct_calc_wraplow_sse2(lo_0, hi_0, cst0);
  *res1 = idct_calc_wraplow_sse2(lo_0, hi_0, cst1);
}

// Bad: Passing excess or read-only vector constants by value triggers MSVC error C2719
static INLINE void average_16(__m128i *sum_0_u16, __m128i *sum_1_u16,
                              const __m128i mul_constants_0,
                              const __m128i mul_constants_1, const int strength,
                              const int rounding, const int weight) {
  // ...
}
```

Good:

```cpp
// Good: Mark helper static INLINE and pass <=3 vector arguments by value
static INLINE void multiplication_and_add_2(const __m128i lo_0, const __m128i hi_0,
                                            const __m128i cst0,
                                            __m128i *const res0, __m128i *const res1) {
  *res0 = idct_calc_wraplow_sse2(lo_0, hi_0, cst0);
}

// Good: Pass excess or read-only vector parameters via const pointer
static INLINE void average_16(__m128i *sum_0_u16, __m128i *sum_1_u16,
                              const __m128i *mul_constants_0,
                              const __m128i *mul_constants_1,
                              const int strength, const int rounding,
                              const int weight) {
  // ...
}
```
