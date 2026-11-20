---
name: libvpx/vpx/require-caller-allocation-or-library-deallocator
description: When defining public C APIs or controls that allocate dynamic data, provide companion library deallocators or caller-allocated buffers in the same change
rubric: correctness
severity: should_fix
---

**Rule.** When defining public C API functions or codec controls that allocate
memory or return dynamic data, require caller-allocated buffers or provide
matching library deallocation functions (such as `vpx_*_free()`). Do not expect
callers to deallocate library-allocated memory with standard `free()`, and
provide companion deallocation/cleanup functions in the same change rather than
deferring them to a TODO.

**Why.** Across language bindings (such as Python, Java, or Rust) and shared
library or C runtime boundaries (such as Windows DLLs), client applications
cannot safely invoke standard `free()` on memory allocated internally by libvpx.
Introducing public allocation functions without companion deallocators forces
callers to leak memory or rely on incompatible allocators.

## Examples

Bad:

```cpp
/* Bad: Expecting application to free internally allocated memory */
VP9E_GET_TPL_STATS,
VPX_CTRL_USE_TYPE(VP9E_GET_TPL_STATS, vpx_tpl_gop_stats_t **)

/* Bad: Introducing an allocation API while deferring deallocation to a TODO */
// TODO(author): Add companion free function in a future change.
vpx_codec_err_t vpx_alloc_and_read_tpl_stats(FILE *tpl_file,
                                             VpxTplGopStats *tpl_gop_stats);
```

Good:

```cpp
/* Good: Pass caller-allocated structure for the library to populate */
VPX_CTRL_USE_TYPE(VP9E_GET_ACTIVEMAP, vpx_active_map_t *)

/* Good: Provide companion allocation and deallocation APIs together */
vpx_codec_err_t vpx_alloc_and_read_tpl_stats(FILE *tpl_file,
                                             VpxTplGopStats *tpl_gop_stats);
void vpx_free_tpl_gop_stats(VpxTplGopStats *tpl_gop_stats);
```
