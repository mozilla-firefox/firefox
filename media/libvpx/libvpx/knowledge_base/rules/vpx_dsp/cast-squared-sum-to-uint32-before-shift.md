---
name: libvpx/vpx_dsp/cast-squared-sum-to-uint32-before-shift
description: When computing variance terms from 32-bit accumulators, cast the squared-sum product to uint32_t before right-shifting rather than casting after or using uint64_t
rubric: correctness
severity: should_fix
---

**Rule.** When computing variance by subtracting a squared-sum term from a
32-bit sum-of-squares accumulator (such as sse or v->sum_square_error) where
intermediate products fit in 32 bits, cast the intermediate product
((int64_t)sum * sum) to uint32_t before right-shifting (e.g.,
((uint32_t)((int64_t)sum * sum) >> shift)). Avoid shifting as int64_t and
casting afterward, and do not cast the subtrahend to uint64_t.

**Why.** In 32-bit block variance calculations, the sum of squared errors
mathematically bounds the mean squared error term, meaning both minuend and
subtrahend fit within uint32_t. Casting the subtrahend to uint64_t unnecessarily
promotes the subtraction to 64-bit unsigned arithmetic. Furthermore, performing
the right-shift on a 64-bit integer ((int64_t)sum * sum >> shift) generates
multi-instruction 64-bit shift sequences on 32-bit target architectures. Casting
the product to uint32_t prior to right-shifting produces a single fast 32-bit
shift and prevents 64-bit promotion during subtraction.

## Examples

Bad:

```cpp
// Variant 1: Casting subtrahend to uint64_t unnecessarily promotes subtraction to 64-bit arithmetic.
v->variance =
    (int)(256 * (v->sum_square_error -
                 (uint64_t)(((int64_t)v->sum_error * v->sum_error) >>
                            v->log2_count)) >>
          v->log2_count);

// Variant 2: Shifting as int64_t before casting to uint32_t emits slow 64-bit shift instructions.
return *sse - (uint32_t)(((int64_t)sum * sum) >> shift);
```

Good:

```cpp
// Variant 1: Casting product to uint32_t before right-shift keeps 32-bit subtraction and fast shift.
v->variance =
    (int)(256 * (v->sum_square_error -
                 ((uint32_t)((int64_t)v->sum_error * v->sum_error) >>
                  v->log2_count)) >>
          v->log2_count);

// Variant 2: Casting product to uint32_t before right-shift generates a fast 32-bit shift instruction.
return *sse - ((uint32_t)((int64_t)sum * sum) >> shift);
```
