---
name: libvpx/vpx/append-public-api-enum-entries-for-abi-compatibility
description: When adding new enumerators to public API or configuration enums, append them to the end before sentinels to preserve ABI compatibility and numeric stability.
rubric: correctness
severity: blocking
---

**Rule.** When extending an enum with new values, modes, or features—including
public API enums in vpx/ headers (such as vp8e_enc_control_id or
vp9e_enc_control_id in vpx/vp8cx.h and vpx/vp8dx.h) and internal configuration
enums—append the new enumerators to the end of the list (immediately before any
_COUNT, _MAX, _LAST, or _TOTAL sentinel). Do not insert new enumerator members
into the middle or reorder existing members.

**Why.** In C/C++, enumerators without explicit values are assigned sequential
integer values. Inserting a new enumerator into the middle shifts the numeric
values of all subsequent enumerators, breaking binary compatibility (ABI) for
pre-compiled applications and external shared library consumers, and breaking
numeric stability for stored configurations, CLI arguments, and regression
tests.

## Examples

Bad:

```cpp
// In vpx/vp8cx.h: Inserting in the middle breaks public ABI
enum vp8e_enc_control_id {
  VP9E_SET_TILE_ROWS,
  VP9E_SET_TPL,  // Breaks ABI: shifts subsequent control IDs
  VP9E_SET_FRAME_PARALLEL_DECODING,
  VP9E_SET_AQ_MODE,
};

// In configuration enums: Inserting in middle shifts existing values
typedef enum {
  NO_AQ = 0,
  PSNR_AQ = 6,
  SUBTITLES_AQ = 7,  // Renumbers LOOKAHEAD_AQ from 7 to 8
  LOOKAHEAD_AQ = 8,
  AQ_MODE_COUNT
} AQ_MODE;
```

Good:

```cpp
// In vpx/vp8cx.h: Appending at the end preserves public ABI
enum vp8e_enc_control_id {
  VP9E_SET_TILE_ROWS,
  VP9E_SET_FRAME_PARALLEL_DECODING,
  VP9E_SET_AQ_MODE,
  VP9E_SET_TPL,  // Appended at end of enum
};

// In configuration enums: Appending before sentinel preserves numeric values
typedef enum {
  NO_AQ = 0,
  PSNR_AQ = 6,
  LOOKAHEAD_AQ = 7,
  SUBTITLES_AQ = 8,  // Appended before sentinel
  AQ_MODE_COUNT
} AQ_MODE;
```
