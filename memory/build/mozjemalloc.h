/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozjemalloc_h
#define mozjemalloc_h

#include <errno.h>

#include "mozjemalloc_types.h"
#include "malloc_decls.h"
#include "mozilla/MacroArgs.h"

// Macro helpers

#define MACRO_CALL(a, b) a b
// Can't use macros recursively, so we need another one doing the same as above.
#define MACRO_CALL2(a, b) a b

#define ARGS_HELPER(name, ...)                                     \
  MACRO_CALL2(MOZ_PASTE_PREFIX_AND_ARG_COUNT(name, ##__VA_ARGS__), \
              (__VA_ARGS__))
#define TYPED_ARGS0()
#define TYPED_ARGS1(t1) t1 arg1
#define TYPED_ARGS2(t1, t2) TYPED_ARGS1(t1), t2 arg2
#define TYPED_ARGS3(t1, t2, t3) TYPED_ARGS2(t1, t2), t3 arg3
#define TYPED_ARGS4(t1, t2, t3, t4) TYPED_ARGS3(t1, t2, t3), t4 arg4

#define ARGS0()
#define ARGS1(t1) arg1
#define ARGS2(t1, t2) ARGS1(t1), arg2
#define ARGS3(t1, t2, t3) ARGS2(t1, t2), arg3
#define ARGS4(t1, t2, t3, t4) ARGS3(t1, t2, t3), arg4

#ifdef MOZ_MEMORY

size_t GetKernelPageSize();

// Implement the set of alignment functions in terms of memalign.
template <void* (*memalign)(size_t, size_t)>
struct AlignedAllocator {
  static inline int posix_memalign(void** aMemPtr, size_t aAlignment,
                                   size_t aSize) {
    void* result;

    // alignment must be a power of two and a multiple of sizeof(void*)
    if (((aAlignment - 1) & aAlignment) != 0 || aAlignment < sizeof(void*)) {
      return EINVAL;
    }

    // The 0-->1 size promotion is done in the memalign() call below
    result = memalign(aAlignment, aSize);

    if (!result) {
      return ENOMEM;
    }

    *aMemPtr = result;
    return 0;
  }

  static inline void* aligned_alloc(size_t aAlignment, size_t aSize) {
    if (aSize % aAlignment) {
      return nullptr;
    }
    return memalign(aAlignment, aSize);
  }

  static inline void* valloc(size_t aSize) {
    return memalign(GetKernelPageSize(), aSize);
  }
};

// These classes each implement the same interface.  Writing out the
// interface for each one rather than using inheritance makes things more
// explicit.
//
// Note: compilers are expected to be able to optimize out `this`.

// The MozJemalloc allocator
struct MozJemalloc {
#  define MALLOC_DECL(name, return_type, ...) \
    static inline return_type name(__VA_ARGS__);
#  include "malloc_decls.h"
};

#  ifdef MOZ_PHC
struct MozJemallocPHC : public MozJemalloc {
#    define MALLOC_DECL(name, return_type, ...) \
      static return_type name(__VA_ARGS__);
#    define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#    include "malloc_decls.h"

  static inline int posix_memalign(void** aMemPtr, size_t aAlignment,
                                   size_t aSize) {
    return AlignedAllocator<memalign>::posix_memalign(aMemPtr, aAlignment,
                                                      aSize);
  }

  static inline void* aligned_alloc(size_t aAlignment, size_t aSize) {
    return AlignedAllocator<memalign>::aligned_alloc(aAlignment, aSize);
  }

  static inline void* valloc(size_t aSize) {
    return AlignedAllocator<memalign>::valloc(aSize);
  }

  static size_t malloc_usable_size(usable_ptr_t);

  static void jemalloc_stats_internal(jemalloc_stats_t*, jemalloc_bin_stats_t*);

  static void jemalloc_stats_lite(jemalloc_stats_lite_t*);

  static void jemalloc_ptr_info(const void*, jemalloc_ptr_info_t*);

#    define MALLOC_DECL(name, return_type, ...) \
      static return_type name(__VA_ARGS__);
#    define MALLOC_FUNCS MALLOC_FUNCS_ARENA_ALLOC
#    include "malloc_decls.h"
};
#  endif

#  ifdef MOZ_REPLACE_MALLOC
// The replace-malloc allocator
struct ReplaceMalloc {
#    define MALLOC_DECL(name, return_type, ...) \
      static return_type name(__VA_ARGS__);
#    include "malloc_decls.h"
};
#  endif

#  ifdef MOZ_PHC
using CanonicalMalloc = MozJemallocPHC;
#  else
using CanonicalMalloc = MozJemalloc;
#  endif

#  ifdef MOZ_REPLACE_MALLOC
using DefaultMalloc = ReplaceMalloc;
#  else
using DefaultMalloc = CanonicalMalloc;
#  endif

// Poison - write "poison" to cells upon deallocation.
constexpr uint8_t kAllocPoison = 0xe5;

// Junk - write this junk value to freshly allocated cells.
constexpr uint8_t kAllocJunk = 0xe4;

#endif  // MOZ_MEMORY

// Dummy implementation of the moz_arena_* API, falling back to a given
// implementation of the base allocator.
template <typename T>
struct DummyArenaAllocator {
  static arena_id_t moz_create_arena_with_params(arena_params_t*) { return 0; }

  static void moz_dispose_arena(arena_id_t) {}

  static void moz_set_max_dirty_page_modifier(int32_t) {}

  static bool moz_enable_deferred_purge(bool aEnable) { return false; }

  static may_purge_now_result_t moz_may_purge_now(bool aPeekOnly,
                                                  uint32_t aReuseGraceMS,
                                                  purge_keep_going_t aKeepGoing,
                                                  void* aClosure) {
    return may_purge_now_result_t::Done;
  }

#define MALLOC_DECL(name, return_type, ...)                 \
  static return_type moz_arena_##name(                      \
      arena_id_t, ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    return T::name(ARGS_HELPER(ARGS, ##__VA_ARGS__));       \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"
};

#endif
