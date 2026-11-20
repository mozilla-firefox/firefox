---
name: libvpx/vpx_dsp/pass-simd-vectors-by-value-or-const-pointer
description: When declaring SIMD helper functions, pass vector parameters by value or const pointer unless the caller requires modified vector outputs returned
rubric: readability
severity: should_fix
---

**Rule.** Pass SIMD vector arguments (such as ARM NEON int16x8_t or x86 __m128i)
by value (e.g., int16x8_t a or const int16x8_t a) or as const pointers/arrays
(const int16x8_t *const a) whenever the caller does not require modified vector
outputs returned. Do not pass vector arguments as mutable pointers (int16x8_t
*a) for read-only inputs or for vectors modified only locally within the callee
(e.g., inlined transposes before storing); reserve mutable vector pointers
strictly for returning modified vector outputs to callers.

**Why.** SIMD vector types fit directly in hardware vector registers and are
designed to be passed by value. Passing them by mutable pointer forces
unnecessary address-of operations at call sites, inhibits keeping values in
registers across inline helpers, and misleadingly signals to readers that the
caller's vector variables are being mutated.

## Examples

Bad:

```cpp
// Anti-pattern: passing vectors by mutable pointer when caller output is unneeded
static INLINE void add_and_store(int16x8_t *a0, int16x8_t *a1, uint8_t *dest) {
  const int16x8_t sum = vaddq_s16(*a0, *a1);
  vst1_u8(dest, vqmovun_s16(sum));
}

static INLINE void transpose_and_store(int16x8_t *a0, int16x8_t *a1,
                                       int16_t **out) {
  transpose_s16_8x8(a0, a1, ...);  // Caller does not need mutated a0/a1 back
  vst1q_s16(*out, *a0);
}
```

Good:

```cpp
// Idiomatic: pass by value (or const pointer) when caller does not need outputs
static INLINE void add_and_store(const int16x8_t a0, const int16x8_t a1,
                                 uint8_t *dest) {
  const int16x8_t sum = vaddq_s16(a0, a1);
  vst1_u8(dest, vqmovun_s16(sum));
}

static INLINE void transpose_and_store(int16x8_t a0, int16x8_t a1,
                                       int16_t **out) {
  transpose_s16_8x8(&a0, &a1, ...);  // Local transposition before storing
  vst1q_s16(*out, a0);
}
```
