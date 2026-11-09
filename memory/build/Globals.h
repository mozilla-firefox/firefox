/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GLOBALS_H
#define GLOBALS_H

// This file contains compile-time constants that depend on sizes of structures
// or the page size.  Page size isn't always known at compile time so some
// values defined here may be determined at runtime.

#include "mozilla/Literals.h"

#include "Constants.h"
// Chunk.h is required for sizeof(arena_chunk_t), but it's inconvenient that
// Chunk.h can't access any constants.
#include "Chunk.h"
#include "Mutex.h"
#include "Utils.h"

// Define MALLOC_RUNTIME_CONFIG depending on MOZ_DEBUG. Overriding this as
// a build option allows us to build mozjemalloc/firefox without runtime asserts
// but with runtime configuration. Making some testing easier.

#ifdef MOZ_DEBUG
#  define MALLOC_RUNTIME_CONFIG
#endif

// Uncomment this to enable extra-vigilant assertions.  These assertions may run
// more expensive checks that are sometimes too slow for regular debug mode.
// #define MALLOC_DEBUG_VIGILANT

// When MALLOC_STATIC_PAGESIZE is defined, the page size is fixed at
// compile-time for better performance, as opposed to determined at
// runtime. Some platforms can have different page sizes at runtime
// depending on kernel configuration, so they are opted out by default.
// Debug builds are opted out too, for test coverage.
#ifndef MALLOC_RUNTIME_CONFIG
#  if !defined(XP_MACOSX) && !defined(ANDROID) && !defined(__ia64__) &&     \
      !defined(__sparc__) && !defined(__mips__) && !defined(__aarch64__) && \
      !defined(__powerpc__) && !defined(__loongarch__)
#    define MALLOC_STATIC_PAGESIZE 1
#  endif
#endif

namespace mozilla {

// mozjemalloc has two values for page size.
//
// gPageSize:     A logical page size used for mozjemalloc's own structures.
// gRealPageSize  The actual page size used by the OS & Hardware.
//
// They can be different so that we can continue to use 4KB pages on systems
// with a larger page size. (WIP see Bug 1980047).
//
// For now they are the same on all platforms, since a lower logical page
// size creates a performance regression due to smaller runs and more
// frequent run allocation.
//
// gPageSize is always less than or equal to gRealPageSize.
//
#ifdef MALLOC_STATIC_PAGESIZE
// Platform specific page size conditions copied from js/public/HeapAPI.h
#  if defined(__powerpc64__)
static const size_t gRealPageSize = 64_KiB;
#  elif defined(__loongarch64)
static const size_t gRealPageSize = 16_KiB;
#  else
static const size_t gRealPageSize = 4_KiB;
#  endif
static const size_t gPageSize = gRealPageSize;
#else
// When MALLOC_OPTIONS contains one or several `P`s, gPageSize will be
// doubled for each `P`.  Likewise each 'p' will halve gPageSize.
extern size_t gRealPageSize;
extern size_t gPageSize;
#endif

// Return the smallest pagesize multiple that is >= s.
#define PAGE_CEILING(s) \
  (((s) + mozilla::gPageSizeMask) & ~mozilla::gPageSizeMask)
#define REAL_PAGE_CEILING(s) (((s) + gRealPageSizeMask) & ~gRealPageSizeMask)

// Return the largest pagesize multiple that is <= s.
#define REAL_PAGE_FLOOR(s) ((s) & ~gRealPageSizeMask)

#define PAGES_PER_REAL_PAGE_CEILING(s) \
  (((s) + gPagesPerRealPage - 1) & ~(gPagesPerRealPage - 1))

#ifdef MALLOC_STATIC_PAGESIZE
#  define GLOBAL(type, name, value) static const type name = value;
#  define GLOBAL_LOG2 LOG2
#  define GLOBAL_ASSERT_HELPER1(x) static_assert(x, #x)
#  define GLOBAL_ASSERT_HELPER2(x, y) static_assert(x, y)
#  define GLOBAL_ASSERT(...)                                          \
                                                                      \
    MOZ_PASTE_PREFIX_AND_ARG_COUNT(GLOBAL_ASSERT_HELPER, __VA_ARGS__) \
    (__VA_ARGS__)
#  define GLOBAL_CONSTEXPR constexpr
#  include "Globals.inc"
#  undef GLOBAL_CONSTEXPR
#  undef GLOBAL_ASSERT
#  undef GLOBAL_ASSERT_HELPER1
#  undef GLOBAL_ASSERT_HELPER2
#  undef GLOBAL_LOG2
#  undef GLOBAL
#else
// We declare the globals here and initialise them in DefineGlobals()
#  define GLOBAL(type, name, value) extern type name;
#  define GLOBAL_ASSERT(...)
#  include "Globals.inc"
#  undef GLOBAL_ASSERT
#  undef GLOBAL

void DefineGlobals();
#endif

// Max size class for bins.
#define gMaxBinClass \
  (gMaxSubPageClass ? gMaxSubPageClass : kMaxQuantumWideClass)

// Return the smallest chunk multiple that is >= s.
#define CHUNK_CEILING(s) (((s) + kChunkSizeMask) & ~kChunkSizeMask)

// Return the smallest cacheline multiple that is >= s.
#define CACHELINE_CEILING(s) (((s) + kCacheLineMask) & ~kCacheLineMask)

// Return the smallest quantum multiple that is >= a.
#define QUANTUM_CEILING(a) (((a) + (kQuantumMask)) & ~(kQuantumMask))
#define QUANTUM_WIDE_CEILING(a) \
  (((a) + (kQuantumWideMask)) & ~(kQuantumWideMask))

// Return the smallest sub page-size  that is >= a.
#define SUBPAGE_CEILING(a) (std::bit_ceil(a))

// Number of all the small-allocated classes
#define NUM_SMALL_CLASSES \
  (kNumQuantumClasses + kNumQuantumWideClasses + gNumSubPageClasses)

// Return the chunk address for allocation address a.
static inline arena_chunk_t* GetChunkForPtr(const void* aPtr) {
  return (arena_chunk_t*)(uintptr_t(aPtr) & ~kChunkSizeMask);
}

// Return the chunk offset of address a.
static inline size_t GetChunkOffsetForPtr(const void* aPtr) {
  return (size_t)(uintptr_t(aPtr) & kChunkSizeMask);
}

// Default maximum amount of dirty memory per arena. Converted to a page
// count in malloc_init_hard() once the page size is known.
#define DIRTY_MAX_DEFAULT_BYTES 1_MiB

enum PoisonType {
  NONE,
  SOME,
  ALL,
};

extern size_t opt_dirty_max;

#define OPT_JUNK_DEFAULT false
#define OPT_ZERO_DEFAULT false
#ifdef NIGHTLY_BUILD
#  define OPT_POISON_DEFAULT ALL
#else
#  define OPT_POISON_DEFAULT SOME
#endif
// Keep this larger than and ideally a multiple of kCacheLineSize;
#define OPT_POISON_SIZE_DEFAULT 256

#ifdef MALLOC_RUNTIME_CONFIG

extern bool opt_junk;
extern bool opt_zero;
extern PoisonType opt_poison;
extern size_t opt_poison_size;

#else

constexpr bool opt_junk = OPT_JUNK_DEFAULT;
constexpr bool opt_zero = OPT_ZERO_DEFAULT;
constexpr PoisonType opt_poison = OPT_POISON_DEFAULT;
constexpr size_t opt_poison_size = OPT_POISON_SIZE_DEFAULT;

static_assert(opt_poison_size >= kCacheLineSize);
static_assert((opt_poison_size % kCacheLineSize) == 0);

#endif

extern bool opt_randomize_small;

}  // namespace mozilla

#endif  // ! GLOBALS_H
