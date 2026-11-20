---
name: libvpx/scope-internal-macros-to-source-files
description: When defining internal helper macros, transform shortcuts, or platform implementation shims, scope them to the .c/.cc source file rather than header files
rubric: design
severity: blocking
---

**Rule.** Do not define internal implementation macros, platform-specific thread
or OS shims (e.g., THREADFN, THREAD_RETURN), or file-private
transform/arithmetic helper macros (e.g., PACK_STORE) in shared or interface
header files when callers do not require them. Keep internal helper definitions,
private macros, and implementation details scoped directly to the .c or .cc
source file where they are used (or a dedicated internal header if strictly
required across internal modules).

**Why.** Exposing internal implementation macros in shared headers pollutes the
preprocessor namespace across all including translation units, leaks private
abstractions and implementation details, and increases the risk of macro name
collisions across consumers.

## Examples

Bad:

```cpp
// In vpx_util/vpx_thread.h
#if defined(_WIN32) && !HAVE_PTHREAD_H
#define THREADFN unsigned int __stdcall
#define THREAD_RETURN(val) (unsigned int)((DWORD_PTR)val)
#else
#define THREADFN void *
#define THREAD_RETURN(val) val
#endif

// In vpx_dsp/ppc/inv_txfm_vsx.h
// Internal macro only used inside inv_txfm_vsx.c
#define PACK_STORE(v0, v1)                            \
  tmp16_0 = vec_add(vec_perm(d_u0, d_u1, mask1), v0); \
  tmp16_1 = vec_add(vec_perm(d_u2, d_u3, mask1), v1); \
  output_v = vec_packsu(tmp16_0, tmp16_1);            \
  vec_vsx_st(output_v, 0, tmp_dest);
```

Good:

```cpp
// In vpx_util/vpx_thread.c
#if defined(_WIN32) && !HAVE_PTHREAD_H
#define THREADFN unsigned int __stdcall
#define THREAD_EXIT_SUCCESS 0
#else
#define THREADFN void *
#define THREAD_EXIT_SUCCESS NULL
#endif

// In vpx_dsp/ppc/inv_txfm_vsx.c
// Scoped to the source file where the transform kernel is implemented
#define PACK_STORE(v0, v1)                            \
  tmp16_0 = vec_add(vec_perm(d_u0, d_u1, mask1), v0); \
  tmp16_1 = vec_add(vec_perm(d_u2, d_u3, mask1), v1); \
  output_v = vec_packsu(tmp16_0, tmp16_1);            \
  vec_vsx_st(output_v, 0, tmp_dest);
```
