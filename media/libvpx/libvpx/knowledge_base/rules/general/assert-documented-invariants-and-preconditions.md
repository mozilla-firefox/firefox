---
name: libvpx/assert-documented-invariants-and-preconditions
description: When code relies on preconditions, arithmetic bounds, or invariants, verify them with non-inverted assert() checks rather than passive comments or redundant defensive runtime checks
rubric: readability
severity: blocking
---

**Rule.** When internal functions, mathematical formulas, or rate-control
heuristics rely on invariants, parameter restrictions, unsupported input sizes
or operating modes (such as BLOCK_SIZE constraints), or implicit arithmetic
bounds, explicitly verify the preconditions using assert() (e.g., assert(bsize
>= BLOCK_8X8), assert(tokens >= 0)) rather than relying solely on passive
comments. When asserting preconditions or divisibility (such as with assert() or
static_assert()), ensure the predicate tests the valid positive invariant (e.g.,
assert(width % 4 == 0), assert((val & mask) == 0)) rather than its inverted
negation (e.g., assert(width % 4 != 0)). Additionally, when callers guarantee
preconditions or arithmetic calculations cannot produce invalid states, omit
redundant defensive runtime checks or error returns (such as if (ptr == NULL)
return VPX_CODEC_INVALID_PARAM; or if (tokens < 0 || ...)
vpx_internal_error(...)) and instead validate the invariant directly with debug
assertions (assert(ptr != NULL), assert(tokens >= 0)).

**Why.** Passive comments and implicit assumptions cannot catch contract
violations, unsupported parameter configurations, or arithmetic overflow bugs
during refactoring and execution. Conversely, redundant defensive runtime
conditionals and error checks for impossible states (such as checking tokens < 0
for non-negative calculations or null checks for guaranteed caller pointers)
clutter control flow with dead branches, add runtime overhead in release builds,
and mislead static analysis tools into reporting false-positive defect warnings.
Using assert() actively validates contracts and internal invariants at entry or
calculation sites in debug builds with zero release overhead, catching
violations before state corruption propagates downstream. Furthermore, assertion
conditions must test the valid positive invariant rather than the negation;
inverting assertion predicates causes debug builds to trigger false assertion
failures on valid inputs while failing to catch actual contract violations.

## Examples

Bad:

```cpp
// Variant 1: Passive comments instead of executable assertions for preconditions and unsupported modes
// vp9_pick_intra_mode does not currently support sub8x8 blocks.
void vp9_pick_intra_mode(VP9_COMP *cpi, MACROBLOCK *x, RD_COST *rd_cost,
                         BLOCK_SIZE bsize, ...) {
  // Note: width must be a multiple of 4.
  /* Safe to set mi_row = 0 since tile rows == 0 */
  mi_row = 0;
}

// Variant 2: Inverted assertion predicates testing negation instead of invariant
assert(width % 4 != 0);  // Fails on valid blocks divisible by 4
assert(((intptr_t)buf & 0xf) != 0);

// Variant 3: Redundant defensive runtime checks for caller-guaranteed invariants
vpx_codec_err_t vp9_extrc_get_encodeframe_decision(EXT_RATECTRL *ext_ratectrl, ...) {
  if (ext_ratectrl == NULL) {
    return VPX_CODEC_INVALID_PARAM;  // Caller already guarantees non-null
  }
  return VPX_CODEC_OK;
}
int64_t tokens = get_token_alloc(cm->mb_rows, cm->mb_cols);
if (tokens < 0 || (uint64_t)tokens > SIZE_MAX) {
  vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR, "Invalid token alloc");
}
```

Good:

```cpp
// Variant 1: Enforce preconditions, invariants, and unsupported modes with assert()
void vp9_pick_intra_mode(VP9_COMP *cpi, MACROBLOCK *x, RD_COST *rd_cost,
                         BLOCK_SIZE bsize, ...) {
  assert(bsize >= BLOCK_8X8);
  assert(width % 4 == 0);
  assert(cm->log2_tile_rows == 0);
  mi_row = 0;
}

// Variant 2: Assert positive invariant rather than negation
assert(width % 4 == 0);
assert(((intptr_t)buf & 0xf) == 0);

// Variant 3: Assert caller-guaranteed and arithmetic invariants without dead branches
vpx_codec_err_t vp9_extrc_get_encodeframe_decision(EXT_RATECTRL *ext_ratectrl, ...) {
  assert(ext_ratectrl != NULL);
  return VPX_CODEC_OK;
}
int64_t tokens = get_token_alloc(cm->mb_rows, cm->mb_cols);
assert(tokens >= 0);
if ((uint64_t)tokens > SIZE_MAX) {
  vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR, "Invalid token alloc");
}
```
