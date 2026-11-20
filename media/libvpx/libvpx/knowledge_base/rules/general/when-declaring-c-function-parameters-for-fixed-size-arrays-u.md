---
name: libvpx/when-declaring-c-function-parameters-for-fixed-size-arrays-u
description: When declaring C function parameters for fixed-size arrays or SIMD vector buffers, use pointer syntax with inline dimension comments like /*[N]*/
rubric: readability
severity: blocking
---

**Rule.** When declaring or defining C functions accepting fixed-size arrays,
lookup buffers, or SIMD vector arrays (e.g., int16x8_t *b, int32x4_t *out,
__m128i *in, __m256i *sums, or tran_low_t *a), declare parameters as pointers
(e.g., const __m128i *in or __m128i *const out) and annotate each parameter with
its expected element count using an inline comment directly after the parameter
name (e.g., const int16x8_t *b /*[8]*/, int16x8_t *out /*[16]*/, or __m128i
*const io /*io[8]*/). Avoid leaving pointer parameters unannotated, and avoid
using array bracket parameter syntax (e.g., const __m128i in[32]).

**Why.** In C, array bracket parameters decay to raw pointers and do not enforce
compile-time bounds checking, while bare pointer parameters omit expected buffer
capacities. Annotating pointer parameters with explicit inline dimension
comments (e.g., /*[N]*/ or /*name[N]*/) accurately reflects pointer semantics
while making buffer size contracts self-documenting for callers and reviewers,
preventing out-of-bounds memory accesses without causing compiler-specific
portability or alignment issues on SIMD types.

## Examples

Bad:

```cpp
// Bad: array bracket syntax misrepresents pointer passing semantics
static void idct32_full_8x32_quarter_1(const __m128i in[32], __m128i out[8]);

// Bad: bare pointer parameters omit expected buffer dimensions
static INLINE void store(tran_low_t *a, const int16x8_t *b) {
  // Callee expects 'b' to hold 8 elements, but signature is ambiguous
}

static INLINE void abs_extend_64bit_sse2(const __m128i in, __m128i *const out,
                                         __m128i *const sign);
```

Good:

```cpp
// Good: pointer syntax with explicit dimension comments
static void idct32_full_8x32_quarter_1(const __m128i *in /*in[32]*/,
                                       __m128i *out /*out[8]*/);

// Good: inline comments document expected array length and vector counts
static INLINE void store(tran_low_t *a, const int16x8_t *b /*[8]*/) {
  // ...
}

static INLINE void abs_extend_64bit_sse2(const __m128i in,
                                         __m128i *const out /*out[2]*/,
                                         __m128i *const sign /*sign[2]*/);
```
