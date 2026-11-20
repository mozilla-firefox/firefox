---
name: libvpx/vpx/bump-abi-version-on-public-header-changes
description: When modifying public headers in vpx/, increment *_ABI_VERSION macros for ABI-altering struct or type changes, but do not bump them for backward-compatible macro additions
rubric: readability
severity: should_fix
---

**Rule.** When modifying public interface headers in `vpx/` (or
`vpx/internal/`), increment the corresponding `*_ABI_VERSION` macro whenever
changes alter the ABI (such as adding, removing, or reordering struct fields,
modifying member types, reassigning enum values, or changing callback
signatures), and ensure component ABI macros are accumulated into top-level ABI
macros (`VPX_ENCODER_ABI_VERSION`, `VPX_DECODER_ABI_VERSION`, or
`VPX_CODEC_ABI_VERSION`). Conversely, do not bump `*_ABI_VERSION` macros for
backward-compatible API additions (such as adding new `#define` constants or
comments) that do not alter binary layout or data structures.

**Why.** libvpx verifies ABI version constants at initialization (such as
`vpx_codec_enc_init_ver()` and `vpx_codec_dec_init_ver()`) to prevent binary
incompatibility between client applications and compiled library binaries.
Omitting an ABI version bump when changing struct layouts or types leaves binary
mismatches undetected at runtime, while unnecessarily bumping ABI versions on
backward-compatible additions causes initialization to reject existing
precompiled clients.

## Examples

Bad:

```cpp
// 1. Adding a field to a public struct without bumping the ABI version macro:
struct vpx_codec_cx_pkt {
  // ...
  uint8_t spatial_layer_encoded[VPX_SS_MAX_LAYERS];
};
#define VPX_ENCODER_ABI_VERSION (8 + VPX_CODEC_ABI_VERSION)

// 2. Unnecessarily bumping ABI version for a backward-compatible #define constant:
#define VPX_DEFAULT_Q -1
#define VPX_EXT_RATECTRL_ABI_VERSION (5 + VPX_TPL_ABI_VERSION)
```

Good:

```cpp
// 1. Incrementing the ABI version macro when modifying a public struct:
struct vpx_codec_cx_pkt {
  // ...
  uint8_t spatial_layer_encoded[VPX_SS_MAX_LAYERS];
};
#define VPX_ENCODER_ABI_VERSION (9 + VPX_CODEC_ABI_VERSION)

// 2. Retaining the existing ABI version when adding a backward-compatible #define constant:
#define VPX_DEFAULT_Q -1
#define VPX_EXT_RATECTRL_ABI_VERSION (4 + VPX_TPL_ABI_VERSION)
```
