---
name: libvpx/vpx_dsp/use-transform-shift-constants-over-literals
description: When performing bit shifts and rounding in transform and inverse transform kernels, use defined subsystem constants like DCT_CONST_BITS rather than hardcoded integer literals
rubric: readability
severity: should_fix
---

**Rule.** In forward and inverse transform implementations (such as DCT, FDCT,
ADST, IDCT, and IADST routines across ARM, x86, MIPS, and other architectures),
do not hardcode numeric literals (such as 14) for bit shifts and rounding
parameters; use defined subsystem constants (such as DCT_CONST_BITS and
DCT_CONST_ROUNDING) instead.

**Why.** Using defined subsystem constants clarifies algorithm precision and
intent, prevents off-by-one or mismatched bit-shift bugs, and ensures
consistency across architecture-specific DSP implementations and future
precision adjustments.

## Examples

Bad:

```cpp
// Hardcoded shift literal obscures precision conventions and constants
*add_lo = vrshrq_n_s32(sum0, 14);
t32[0].val[0] = vrshrn_n_s64(t[0].val[0], 14);
```

Good:

```cpp
// Using defined transform constants clarifies precision and matches conventions
*add_lo = vrshrq_n_s32(sum0, DCT_CONST_BITS);
t32[0].val[0] = vrshrn_n_s64(t[0].val[0], DCT_CONST_BITS);
```
