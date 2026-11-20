---
name: libvpx/vpx/document-codec-control-values-and-defaults
description: When documenting public codec controls in vpx headers, format Doxygen comments with supported codecs, parameter ranges, defaults, operational semantics, buffer sizes, and prerequisites
rubric: readability
severity: should_fix
---

**Rule.** When declaring or updating public codec control enum identifiers (such
as in vpx/vp8cx.h, vpx/vp8dx.h, or vpx/vp8.h) and VPX_CTRL_USE_TYPE bindings,
format Doxygen comments starting with '/*!\brief Codec control function to
<action>.' (or '/*!\brief Codec control to <action>.'). Document valid parameter
ranges or clamping bounds using dedicated '\note Valid range: <min>..<max>'
directives (avoiding bare parentheticals in '\brief'), default settings,
operational semantics (avoiding bare '0: off, 1: on' labels), and pass/mode
constraints (e.g., 1-pass RTC only). For controls requiring caller-allocated
buffers, specify required capacities or public constants (e.g.,
VPX_SS_MAX_LAYERS), stating concrete numeric values if referencing internal
defines (e.g., MAX_ARF_GOP_SIZE). Clearly document all prerequisite
configuration flags or callback function pointers (e.g., in vpx_rc_funcs_t),
ensuring configuration prerequisite notes ('\attention') reference accurate
struct members (e.g., rc_end_usage for #VPX_CQ). Explicitly specify 'Supported
in codecs: <codecs>' (e.g., VP8, VP9) before the identifier.

**Why.** Public codec controls are configured through vpx_codec_control()
without compile-time value validation or self-describing parameter types.
External API consumers rely directly on public header Doxygen comments to
understand valid option values, full range and clamping limits, default
behaviors, pass/mode constraints, buffer allocation sizes, supported codec
targets, and required initialization steps (such as registered callbacks or
bitflags). Omitting prerequisite callbacks/flags, using vague on/off labels,
omitting supported codecs, or citing internal macros without concrete numbers
leaves external applications unable to configure controls properly or allocate
required buffer sizes.

## Examples

Bad:

```cpp
// Missing parameter values, default setting, and supported codecs:
/*!\brief Codec control function to enable golden reference for SVC. */
VP9E_SET_SVC_GF_TEMPORAL_REF,

// Non-standard Doxygen format and bare on/off without operational semantics:
/** control function to enable row based multi-threading in the decoder.
 * Calling this control function with argument "1" will enable row
 * multi-threading. Any other value will not enable row multi-threading.
 */
VP9D_SET_ROW_MT,

// Embedding range parenthetically in \brief instead of using \note:
/*!\brief Codec control function to set sharpness at expense of PSNR. (0..7). */
VP8E_SET_SHARPNESS,

// Missing required array buffer capacity or citing internal define without concrete value:
/*!\brief Codec control to get TPL stats for current GOP.
 * Allocation of memory of size MAX_ARF_GOP_SIZE * sizeof(TplFrameStats)
 * should be done by applications. */
VP9E_GET_TPL_STATS,

// Stale or incorrect struct field in configuration prerequisite note:
/*!\brief Codec control to set max data rate for spatial layer.
 * \attention Requires vpx_codec_enc_cfg_t::g_usage set to #VPX_CQ. */
VP8E_SET_CQ_LEVEL,

// Missing prerequisite callback pointers or flags needed for external controls:
/*!\brief Codec control to use external RC to control TPL.
 *
 * Supported in codecs: VP9
 */
VP9E_ENABLE_EXTERNAL_RC_TPL,
```

Good:

```cpp
// Documented values, default setting, and supported codecs:
/*!\brief Codec control function to enable golden reference for SVC.
 * 0: Off, 1: Enabled (default)
 *
 * Supported in codecs: VP9
 */
VP9E_SET_SVC_GF_TEMPORAL_REF,

// Standard Doxygen format, detailed operational semantics, and supported codecs:
/*!\brief Codec control function to set row level multi-threading.
 * 0 : off, 1 : on
 *
 * Supported in codecs: VP9
 */
VP9D_SET_ROW_MT,

// Dedicated \note directive for numeric valid range:
/*!\brief Codec control to set sharpness at expense of PSNR.
 * \note Valid range: 0..7
 *
 * Supported in codecs: VP8, VP9
 */
VP8E_SET_SHARPNESS,

// Concrete value specified for internal macro or public constant used:
/*!\brief Codec control to get TPL stats for current GOP.
 * Allocation of memory of size MAX_ARF_GOP_SIZE (50) * sizeof(TplFrameStats)
 * should be done by applications (or use VPX_SS_MAX_LAYERS for layer arrays).
 *
 * Supported in codecs: VP9
 */
VP9E_GET_TPL_STATS,

// Accurate struct field referenced in prerequisite note:
/*!\brief Codec control to set max data rate for spatial layer.
 * \attention Requires vpx_codec_enc_cfg_t::rc_end_usage set to #VPX_CQ.
 *
 * Supported in codecs: VP8, VP9
 */
VP8E_SET_CQ_LEVEL,

// Fully documented prerequisite callback pointers and configuration bitmasks:
/*!\brief Codec control to use external RC to control TPL.
 * VPX_RC_QP must be set in rc_type and get_encodeframe_decision callback
 * in vpx_rc_funcs_t must be set before enabling this control.
 *
 * Supported in codecs: VP9
 */
VP9E_ENABLE_EXTERNAL_RC_TPL,
```
