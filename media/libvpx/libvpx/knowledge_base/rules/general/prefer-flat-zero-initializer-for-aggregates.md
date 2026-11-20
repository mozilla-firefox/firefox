---
name: libvpx/prefer-flat-zero-initializer-for-aggregates
description: When zero-initializing structs, arrays, or nested aggregates in C, use flat '= { 0 }' or subsystem zeroing macros instead of empty '= {}' or redundant nested braces
rubric: correctness
severity: blocking
---

**Rule.** When zero-initializing structs, arrays, or nested aggregate types in C
source and header files (`.c`, `.h`), use the flat `= { 0 }` initializer or
subsystem zeroing macros (such as `vp9_zero()`, `vp8_zero()`, `vpx_zero()`, or
`memset()`). Do not use empty initializer syntax `= {}` (which is nonstandard
prior to C23 and causes portability issues), and do not use redundant nested
brace initializers (such as `{ { 0 } }` or `{ { 0, 0 } }`). Standard flat `= { 0
}` is sufficient to zero-initialize all elements across any nesting depth.

**Why.** In standard C (C99/C11 6.7.8), empty aggregate initializers (`= {}`)
are invalid syntax that rely on nonstandard compiler extensions and cause
compilation failures on MSVC and strict C compilers. Additionally, redundant
nested braces (such as `{ { 0, 0 } }`) add visual clutter and become brittle
when array dimensions or struct layouts change. A single flat `= { 0 }` standard
initializer guarantees that the entire struct or multidimensional array is fully
zero-initialized across all nesting levels, while subsystem zeroing macros like
`vp9_zero()` / `vpx_zero()` provide clear, idiomatic struct zeroing.

## Examples

Bad:

```cpp
// Nonstandard empty initializer (invalid in C99/C11)
vpx_rc_gop_decision_t gop_decision = {};

// Redundant nested braces for multidimensional array or struct
struct buf_2d yv12_mb[4][MAX_MB_PLANE] = { { 0, 0 } };
int grid[4][4] = { { 0 } };
```

Good:

```cpp
// Standard flat zero initializer or subsystem zeroing macro
vpx_rc_gop_decision_t gop_decision = { 0 };
// Or: vp9_zero(gop_decision);

// Flat zero initializer for multidimensional array and structs
struct buf_2d yv12_mb[4][MAX_MB_PLANE] = { 0 };
int grid[4][4] = { 0 };
```
