---
name: libvpx/vpx/document-public-api-and-callbacks-with-doxygen
description: When declaring or updating public API functions or callback prototypes in vpx/ headers, document purpose, invocation context, return status codes, and accurate \param direction tags
rubric: readability
severity: should_fix
---

**Rule.** When declaring or modifying public API functions or callback function
pointer typedefs in public headers (vpx/*.h), document each declaration with a
Doxygen block (/*!\brief ... */). Explicitly describe the function's purpose or
callback invocation timing and context with \brief, annotate every parameter
(including newly added or modified parameters) with accurate direction tags
(\param[in] for inputs, \param[out] for buffers or pointers populated by the
function, or \param[in,out] if modified in place) and descriptions, and document
all expected return values and status codes using \return or \retval (e.g.,
#VPX_RC_OK on success, #VPX_RC_ERROR on failure, or 0 vs. negative error codes).

**Why.** Public interface headers in vpx/ define contracts implemented by
external applications and integrators, and generate developer-facing API
documentation. Omitting Doxygen blocks, leaving callback invocation lifecycles
undocumented, mislabeling output parameters as \param[in], omitting \param tags
when adding parameters, or omitting expected return and error status codes
leaves API consumers without clear contracts on data ownership, parameter
modification semantics, and error handling, leading to integration bugs or
undefined behavior.

## Examples

Bad:

```cpp
// Missing Doxygen block on callback prototype:
typedef int (*vpx_rc_create_model_cb_fn_t)(
    void *priv, const vpx_rc_config_t *ratectrl_config,
    vpx_rc_model_t *rate_ctrl_model_ptr);

// Output parameter populated by function incorrectly marked \param[in]:
/*!\brief Read VpxTplGopStats from file
 *
 * \param[in] tpl_file       A FILE pointer that's already been opened.
 * \param[in] tpl_gop_stats  VpxTplGopStats populated with GOP TPL stats.
 */
vpx_codec_err_t vpx_alloc_and_read_tpl_stats(FILE *tpl_file,
                                             VpxTplGopStats *tpl_gop_stats);

// Callback missing parameter descriptions, direction tags, and return status codes:
/*!\brief Send first pass stats to the external rate control model callback
 * prototype
 *
 * \param[in]  rate_ctrl_model    Rate control model
 * \param[in]  first_pass_stats   First pass stats
 */
typedef vpx_rc_status_t (*vpx_rc_send_firstpass_stats_cb_fn_t)(
    vpx_rc_model_t rate_ctrl_model,
    const vpx_rc_firstpass_stats_t *first_pass_stats);
```

Good:

```cpp
// Fully documented callback prototype with invocation context, directions, and \retval:
/*!\brief Create an external rate control model callback prototype
 *
 * This callback is invoked by the encoder to create an external rate control
 * model instance.
 *
 * \param[in]  priv                Callback's private data
 * \param[in]  ratectrl_config     Pointer to vpx_rc_config_t
 * \param[out] rate_ctrl_model_ptr Pointer to allocated vpx_rc_model_t handler
 * \retval #VPX_RC_OK    On success
 * \retval #VPX_RC_ERROR On failure
 */
typedef vpx_rc_status_t (*vpx_rc_create_model_cb_fn_t)(
    void *priv, const vpx_rc_config_t *ratectrl_config,
    vpx_rc_model_t *rate_ctrl_model_ptr);

// Correctly marked \param[out] for populated output parameter:
/*!\brief Read VpxTplGopStats from file
 *
 * \param[in]  tpl_file       A FILE pointer that's already been opened.
 * \param[out] tpl_gop_stats  VpxTplGopStats populated with GOP TPL stats.
 * \retval #VPX_CODEC_OK        On success
 * \retval #VPX_CODEC_MEM_ERROR On allocation failure
 */
vpx_codec_err_t vpx_alloc_and_read_tpl_stats(FILE *tpl_file,
                                             VpxTplGopStats *tpl_gop_stats);

// Documented callback prototype with return status codes:
/*!\brief Send first pass stats to the external rate control model callback
 * prototype
 *
 * \param[in]  rate_ctrl_model    Rate control model
 * \param[in]  first_pass_stats   First pass stats
 * \return Returns VPX_RC_OK on success, or VPX_RC_ERROR on failure.
 */
typedef vpx_rc_status_t (*vpx_rc_send_firstpass_stats_cb_fn_t)(
    vpx_rc_model_t rate_ctrl_model,
    const vpx_rc_firstpass_stats_t *first_pass_stats);
```
