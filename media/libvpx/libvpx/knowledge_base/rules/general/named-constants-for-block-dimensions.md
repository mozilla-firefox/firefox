---
name: libvpx/named-constants-for-block-dimensions
description: Use named constants, API enums, dimension macros, or counts instead of magic numbers, casts, or open-coded arithmetic in buffer sizing, bounds checks, configs, thresholds, and state comparisons
rubric: readability
severity: blocking
---

**Rule.** Do not hardcode raw numeric literals (magic numbers), integer casts,
or open-coded arithmetic expressions in array dimensions, buffer capacities,
struct table sizes, stride calculations, loop bounds, bounds checks, assertions,
codec configuration fields, conditional thresholds, array indexing, or
enum/state comparisons, and do not leave temporary debug toggles (such as '1 &&'
or '0 &&') in production code. Instead:
- Use defined named constants, dimension macros, or enums (such as REF_FRAMES, VPX_SS_MAX_LAYERS, MAX_PARTITIONS, BW, BH, DIST_STRIDE, or BACKGROUND_SEG_SKIP_ID) for fixed dimensions, buffer capacities, strides, and array indexing.
- Use predefined API constants and named enum values (such as VPX_DL_REALTIME, VPX_BITS_8, VPX_VBR, or VPX_CODEC_OK) when setting configuration fields (e.g., cfg.rc_end_usage, cfg.g_bit_depth), passing API arguments, or initializing arrays, avoiding raw literals or integer casts (C-style or static_cast).
- Compare bounds, capacity limits, and assertions against symbolic dimension macros or enum counts (such as MAX_PARTITIONS, VPX_MAX_LAYERS, TX_MODES, or MB_MODE_COUNT) rather than numeric literals (e.g., assert(tx_mode < TX_MODES)) or open-coded bitshifts and arithmetic (such as (1 << EIGHT_PARTITION) + 1).
- Derive normalization factors and pixel counts dynamically from block size parameters (e.g., bw * bh or width * height) rather than hardcoding fixed products or scaling denominators (such as 1024).
- Define descriptive constants (static const, const, #define, or enum) or parameterized function arguments for algorithmic cutoffs, thresholds, and coordinate scaling (e.g., y_low, skin_threshold, FRAMES_NO_SKIPPING_AFTER_KEY).
- Compare enumerated states, modes, or category flags against named enum constants or macros (e.g., kLowSadLowSumdiff) instead of raw integer values.

**Why.** Hardcoded numeric literals, open-coded arithmetic, raw integer values,
and integer casts obscure algorithmic intent and domain semantics across buffer
sizing, bounds checks, API configurations, normalization math, and state checks.
They tightly couple code to fixed values, introduce subtle off-by-one errors or
buffer overflows, break silently when enum definitions or buffer capacities are
modified or reordered, and make code fragile during parameter tuning. Using
symbolic dimension macros, API constants, and enum counts guarantees that
configurations and bounds remain synchronized with definitions and clearly
communicates valid domain ranges. Leftover debug toggles clutter control flow
and mask intended logic.

## Examples

Bad:

```cpp
// 1. Hardcoded array capacities and struct table bounds
struct vpx_svc_ref_frame_config {
  int update_reference[8][VPX_SS_MAX_LAYERS];
  int reference_slot[8];
};

// 2. Casting raw integers and magic numbers in API configs
cfg.rc_end_usage = (vpx_rc_mode)0;
cfg.g_bit_depth = static_cast<vpx_bit_depth>(8);
const vpx_enc_deadline_t frame_deadlines[] = { (vpx_enc_deadline_t)1 };

// 3. Open-coded bounds check arithmetic and hardcoded assertions
if (ctx->fragments.count >= (1 << EIGHT_PARTITION) + 1) {
  ctx->fragments.count = 0;
}
assert(tx_mode <= 4);
assert(mb_mode < 14);

// 4. Hardcoded strides, normalization denominators, raw enum comparisons, debug toggle
const int dist_stride = 34;
DECLARE_ALIGNED(16, uint16_t, y_dist[32 * 34]) = { 0 };
ref_stats[idx].mc_flow += (mc_flow * overlap_area) / 1024;
if (1 && x->content_state_sb > 1 && cpi->roi.skip[3] &&
    rc->frames_since_key > 20) {
  x->skip_low_source_sad = 1;
}
```

Good:

```cpp
// 1. Named macro/enum constants for struct tables and capacities
struct vpx_svc_ref_frame_config {
  int update_reference[REF_FRAMES][VPX_SS_MAX_LAYERS];
  int reference_slot[REF_FRAMES];
};

// 2. Predefined API constants and named enum values
cfg.rc_end_usage = VPX_VBR;
cfg.g_bit_depth = VPX_BITS_8;
const vpx_enc_deadline_t frame_deadlines[] = { VPX_DL_REALTIME };

// 3. Symbolic dimension constants and enum counts in bounds and assertions
if (ctx->fragments.count >= MAX_PARTITIONS) {
  ctx->fragments.count = 0;
}
assert(tx_mode < TX_MODES);
assert(mb_mode < MB_MODE_COUNT);

// 4. Synchronized dimension macros, derived pixel counts, named thresholds, no debug guard
DECLARE_ALIGNED(16, uint16_t, y_dist[BH * DIST_STRIDE]) = { 0 };
const int pix_num = bw * bh;
ref_stats[idx].mc_flow += (mc_flow * overlap_area) / pix_num;
#define BACKGROUND_SEG_SKIP_ID 3
#define FRAMES_NO_SKIPPING_AFTER_KEY 20
if (x->content_state_sb > kLowSadLowSumdiff &&
    cpi->roi.skip[BACKGROUND_SEG_SKIP_ID] &&
    rc->frames_since_key > FRAMES_NO_SKIPPING_AFTER_KEY) {
  x->skip_low_source_sad = 1;
}
```
