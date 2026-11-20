---
name: libvpx/match-simd-assembly-to-c-reference-semantics
description: When modifying RTCD prototypes or DSP signatures, match C reference semantics across SIMD backends and update all internal static helpers
rubric: correctness
severity: blocking
---

**Rule.** Ensure architecture-specific SIMD and assembly implementations match
the C reference (_c) function's behavior, boundary handling, and argument
semantics exactly, without omitting reference logic. When refactoring RTCD
prototypes or DSP interfaces (such as convolve filtering signatures), update all
architecture backends (x86, ARM, PPC, MIPS, LoongArch) and their internal static
helper and inline functions to match the updated parameter types and call sites.

**Why.** SIMD and assembly routines serve as drop-in replacements for C
reference routines; semantic discrepancies cause codec divergence or
cross-platform bugs. Refactoring public dispatch signatures without updating
internal static helpers in secondary architecture files causes compilation
failures and signature mismatches on non-primary target platforms.

## Examples

Bad:

```cpp
// 1. C reference omits parameter logic used in SIMD:
void vpx_plane_add_noise_c(uint8_t *start, const int8_t *noise,
                           int blackclamp, int whiteclamp,
                           unsigned int width, unsigned int height, int pitch) {
  (void)whiteclamp;  // C reference omits clamp logic present in SIMD
}

// 2. Caller updated to single filter but internal static helper was missed:
convolve(src, src_stride, dst, dst_stride, filter, x0_q4, x_step_q4, y0_q4, y_step_q4, w, h);

static inline void convolve(const uint8_t *src, ptrdiff_t src_stride,
                            uint8_t *dst, ptrdiff_t dst_stride,
                            const InterpKernel *const x_filters, int x0_q4,
                            int x_step_q4, const InterpKernel *const y_filters,
                            int y0_q4, int y_step_q4, int w, int h);
```

Good:

```cpp
// 1. C reference and SIMD share identical behavior and argument semantics:
void vpx_plane_add_noise_c(uint8_t *start, const int8_t *noise,
                           int blackclamp, int whiteclamp,
                           unsigned int width, unsigned int height, int pitch) {
  const int bothclamp = blackclamp + whiteclamp;
}

// 2. Internal static helper updated to match refactored signature:
static VPX_FORCE_INLINE void convolve(const uint8_t *src, ptrdiff_t src_stride,
                                      uint8_t *dst, ptrdiff_t dst_stride,
                                      const InterpKernel *const filter,
                                      int x0_q4, int x_step_q4, int y0_q4,
                                      int y_step_q4, int w, int h);
```
