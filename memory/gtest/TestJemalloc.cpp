/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Literals.h"
#include "mozilla/mozalloc.h"
#include "mozilla/Vector.h"
#include "mozilla/gtest/MozHelpers.h"
#include "mozmemory.h"
#include "nsCOMPtr.h"
#include "Utils.h"

#include "gtest/gtest.h"

#include <chrono>
#include <thread>

#ifdef MOZ_PHC
#  include "PHC.h"
#endif
#include "BaseAlloc.h"

#include <memory>

using namespace mozilla;

class AutoDisablePHCOnCurrentThread {
 public:
  AutoDisablePHCOnCurrentThread() {
#ifdef MOZ_PHC
    mozilla::phc::DisablePHCOnCurrentThread();
#endif
  }

  ~AutoDisablePHCOnCurrentThread() {
#ifdef MOZ_PHC
    mozilla::phc::ReenablePHCOnCurrentThread();
#endif
  }
};

static inline void TestOne(size_t size) {
  size_t req = size;
  size_t adv = malloc_good_size(req);
  char* p = (char*)malloc(req);
  size_t usable = moz_malloc_usable_size(p);
  // NB: Using EXPECT here so that we still free the memory on failure.
  EXPECT_EQ(adv, usable) << "malloc_good_size(" << req << ") --> " << adv
                         << "; "
                            "malloc_usable_size("
                         << req << ") --> " << usable;
  free(p);
}

static inline void TestThree(size_t size) {
  ASSERT_NO_FATAL_FAILURE(TestOne(size - 1));
  ASSERT_NO_FATAL_FAILURE(TestOne(size));
  ASSERT_NO_FATAL_FAILURE(TestOne(size + 1));
}

TEST(Jemalloc, UsableSizeInAdvance)
{
  /*
   * Test every size up to a certain point, then (N-1, N, N+1) triplets for a
   * various sizes beyond that.
   */

  for (size_t n = 0; n < 16_KiB; n++) ASSERT_NO_FATAL_FAILURE(TestOne(n));

  for (size_t n = 16_KiB; n < 1_MiB; n += 4_KiB)
    ASSERT_NO_FATAL_FAILURE(TestThree(n));

  for (size_t n = 1_MiB; n < 8_MiB; n += 128_KiB)
    ASSERT_NO_FATAL_FAILURE(TestThree(n));
}

static int gStaticVar;

bool InfoEq(jemalloc_ptr_info_t& aInfo, PtrInfoTag aTag, void* aAddr,
            size_t aSize, arena_id_t arenaId) {
  return aInfo.tag == aTag && aInfo.addr == aAddr && aInfo.size == aSize
#ifdef MOZ_DEBUG
         && aInfo.arenaId == arenaId
#endif
      ;
}

bool InfoEqFreedPage(jemalloc_ptr_info_t& aInfo, void* aAddr, size_t aPageSize,
                     arena_id_t arenaId) {
  size_t pageSizeMask = aPageSize - 1;

  return jemalloc_ptr_is_freed_page(&aInfo) &&
         aInfo.addr == (void*)(uintptr_t(aAddr) & ~pageSizeMask) &&
         aInfo.size == aPageSize
#ifdef MOZ_DEBUG
         && aInfo.arenaId == arenaId
#endif
      ;
}

TEST(Jemalloc, PtrInfo)
{
  arena_id_t arenaId = moz_create_arena();
  ASSERT_TRUE(arenaId != 0);

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);

  jemalloc_ptr_info_t info;
  Vector<char*> small, large, huge;

  // For small (less than half the page size) allocations, test every position
  // within many possible sizes.
  size_t small_max =
      stats.subpage_max ? stats.subpage_max : stats.quantum_wide_max;
  for (size_t n = 0; n <= small_max; n += 8) {
    auto p = (char*)moz_arena_malloc(arenaId, n);
    size_t usable = moz_malloc_size_of(p);
    ASSERT_TRUE(small.append(p));
    for (size_t j = 0; j < usable; j++) {
      jemalloc_ptr_info(&p[j], &info);
      ASSERT_TRUE(InfoEq(info, TagLiveAlloc, p, usable, arenaId));
    }
  }

  // Similar for large (small_max + 1 KiB .. 1MiB - 8KiB) allocations.
  for (size_t n = small_max + 1_KiB; n <= stats.large_max; n += 1_KiB) {
    auto p = (char*)moz_arena_malloc(arenaId, n);
    size_t usable = moz_malloc_size_of(p);
    ASSERT_TRUE(large.append(p));
    for (size_t j = 0; j < usable; j += 347) {
      jemalloc_ptr_info(&p[j], &info);
      ASSERT_TRUE(InfoEq(info, TagLiveAlloc, p, usable, arenaId));
    }
  }

  // Similar for huge (> 1MiB - 8KiB) allocations.
  for (size_t n = stats.chunksize; n <= 10_MiB; n += 512_KiB) {
    auto p = (char*)moz_arena_malloc(arenaId, n);
    size_t usable = moz_malloc_size_of(p);
    ASSERT_TRUE(huge.append(p));
    for (size_t j = 0; j < usable; j += 567) {
      jemalloc_ptr_info(&p[j], &info);
      ASSERT_TRUE(InfoEq(info, TagLiveAlloc, p, usable, arenaId));
    }
  }

  // The following loops check freed allocations. We step through the vectors
  // using prime-sized steps, which gives full coverage of the arrays while
  // avoiding deallocating in the same order we allocated.
  size_t len;

  // Free the small allocations and recheck them.
  int isFreedAlloc = 0, isFreedPage = 0;
  len = small.length();
  for (size_t i = 0, j = 0; i < len; i++, j = (j + 19) % len) {
    char* p = small[j];
    size_t usable = moz_malloc_size_of(p);
    free(p);
    for (size_t k = 0; k < usable; k++) {
      jemalloc_ptr_info(&p[k], &info);
      // There are two valid outcomes here.
      if (InfoEq(info, TagFreedAlloc, p, usable, arenaId)) {
        isFreedAlloc++;
      } else if (InfoEqFreedPage(info, &p[k], stats.page_size, arenaId)) {
        isFreedPage++;
      } else {
        ASSERT_TRUE(false);
      }
    }
  }
  // There should be both FreedAlloc and FreedPage results, but a lot more of
  // the former.
  ASSERT_TRUE(isFreedAlloc != 0);
  ASSERT_TRUE(isFreedPage != 0);
  ASSERT_TRUE(isFreedAlloc / isFreedPage > 8);

  // Free the large allocations and recheck them.
  len = large.length();
  for (size_t i = 0, j = 0; i < len; i++, j = (j + 31) % len) {
    char* p = large[j];
    size_t usable = moz_malloc_size_of(p);
    free(p);
    for (size_t k = 0; k < usable; k += 357) {
      jemalloc_ptr_info(&p[k], &info);
      ASSERT_TRUE(InfoEqFreedPage(info, &p[k], stats.page_size, arenaId));
    }
  }

  // Free the huge allocations and recheck them.
  len = huge.length();
  for (size_t i = 0, j = 0; i < len; i++, j = (j + 7) % len) {
    char* p = huge[j];
    size_t usable = moz_malloc_size_of(p);
    free(p);
    for (size_t k = 0; k < usable; k += 587) {
      jemalloc_ptr_info(&p[k], &info);
      ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));
    }
  }

  // Null ptr.
  jemalloc_ptr_info(nullptr, &info);
  ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));

  // Near-null ptr.
  jemalloc_ptr_info((void*)0x123, &info);
  ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));

  // Maximum address.
  jemalloc_ptr_info((void*)uintptr_t(-1), &info);
  ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));

  // Stack memory.
  int stackVar = 0;
  jemalloc_ptr_info(&stackVar, &info);
  ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));

  // Code memory.
  jemalloc_ptr_info((const void*)&jemalloc_ptr_info, &info);
  ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));

  // Static memory.
  jemalloc_ptr_info(&gStaticVar, &info);
  ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));

  // Chunk header.
  auto p = std::make_unique<int>();
  size_t chunksizeMask = stats.chunksize - 1;
  char* chunk = (char*)(uintptr_t(p.get()) & ~chunksizeMask);
  size_t chunkHeaderSize =
      stats.chunksize - stats.large_max - stats.real_page_size;
  for (size_t i = 0; i < chunkHeaderSize; i += 64) {
    jemalloc_ptr_info(&chunk[i], &info);
    ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));
  }

  // Run header.
  size_t page_sizeMask = stats.page_size - 1;
  char* run;
  // The run header could be at the page boundary of this or any of the
  // preceeding 3 pages.
  for (unsigned i = 0; i < 100; i++) {
    run = (char*)((uintptr_t(p.get()) & ~page_sizeMask) - stats.page_size * i);

    // The run header was not found before finding the chunk header.
    ASSERT_NE(run, (char*)((uintptr_t)chunk + chunkHeaderSize -
                           stats.real_page_size));

    jemalloc_ptr_info(run, &info);
    if (InfoEq(info, TagUnknown, nullptr, 0U, 0U)) {
      break;
    }

    // If the loop doesn't break because the run isn't found then the
    // assertion in the next loop will catch it.
  }

  for (size_t i = 0; i < 4 * sizeof(void*); i++) {
    jemalloc_ptr_info(&run[i], &info);
    ASSERT_TRUE(InfoEq(info, TagUnknown, nullptr, 0U, 0U));
  }

  // Entire chunk. It's impossible to check what is put into |info| for all of
  // these addresses; this is more about checking that we don't crash.
  for (size_t i = 0; i < stats.chunksize; i += 256) {
    jemalloc_ptr_info(&chunk[i], &info);
  }

  moz_dispose_arena(arenaId);
}

size_t sSizes[] = {1,      42,      79,      918,     1.4_KiB,
                   73_KiB, 129_KiB, 1.1_MiB, 2.6_MiB, 5.1_MiB};

TEST(Jemalloc, Arenas)
{
  arena_id_t arena = moz_create_arena();
  ASSERT_TRUE(arena != 0);
  void* ptr = moz_arena_malloc(arena, 42);
  ASSERT_TRUE(ptr != nullptr);
  ptr = moz_arena_realloc(arena, ptr, 64);
  ASSERT_TRUE(ptr != nullptr);
  moz_arena_free(arena, ptr);
  ptr = moz_arena_calloc(arena, 24, 2);
  // For convenience, free can be used to free arena pointers.
  free(ptr);
  moz_dispose_arena(arena);

  // Avoid death tests adding some unnecessary (long) delays.
  SAVE_GDB_SLEEP_LOCAL();

  // Can't use an arena after it's disposed.
  // ASSERT_DEATH_WRAP(moz_arena_malloc(arena, 80), "");

  // Arena id 0 can't be used to somehow get to the main arena.
  ASSERT_DEATH_WRAP(moz_arena_malloc(0, 80), "");

  arena = moz_create_arena();
  arena_id_t arena2 = moz_create_arena();
  // Ensure arena2 is used to prevent OSX errors:
  (void)arena2;

  // For convenience, realloc can also be used to reallocate arena pointers.
  // The result should be in the same arena. Test various size class
  // transitions.
  for (size_t from_size : sSizes) {
    SCOPED_TRACE(testing::Message() << "from_size = " << from_size);
    for (size_t to_size : sSizes) {
      SCOPED_TRACE(testing::Message() << "to_size = " << to_size);
      ptr = moz_arena_malloc(arena, from_size);
      ptr = realloc(ptr, to_size);
      // Freeing with the wrong arena should crash.
      ASSERT_DEATH_WRAP(moz_arena_free(arena2, ptr), "");
      // Likewise for moz_arena_realloc.
      ASSERT_DEATH_WRAP(moz_arena_realloc(arena2, ptr, from_size), "");
      // The following will crash if it's not in the right arena.
      moz_arena_free(arena, ptr);
    }
  }

  moz_dispose_arena(arena2);
  moz_dispose_arena(arena);

  RESTORE_GDB_SLEEP_LOCAL();
}

// Check that a buffer aPtr is entirely filled with a given character from
// aOffset to aSize. For faster comparison, the caller is required to fill a
// reference buffer with the wanted character, and give the size of that
// reference buffer.
static void bulk_compare(char* aPtr, size_t aOffset, size_t aSize,
                         char* aReference, size_t aReferenceSize) {
  for (size_t i = aOffset; i < aSize; i += aReferenceSize) {
    size_t length = std::min(aSize - i, aReferenceSize);
    if (memcmp(aPtr + i, aReference, length)) {
      // We got a mismatch, we now want to report more precisely where.
      for (size_t j = i; j < i + length; j++) {
        ASSERT_EQ(aPtr[j], *aReference);
      }
    }
  }
}

// A range iterator for size classes between two given values.
class SizeClassesBetween {
 public:
  SizeClassesBetween(size_t aStart, size_t aEnd) : mStart(aStart), mEnd(aEnd) {}

  class Iterator {
   public:
    explicit Iterator(size_t aValue) : mValue(malloc_good_size(aValue)) {}

    operator size_t() const { return mValue; }
    size_t operator*() const { return mValue; }
    Iterator& operator++() {
      mValue = malloc_good_size(mValue + 1);
      return *this;
    }

   private:
    size_t mValue;
  };

  Iterator begin() { return Iterator(mStart); }
  Iterator end() { return Iterator(mEnd); }

 private:
  size_t mStart, mEnd;
};

#define ALIGNMENT_CEILING(s, alignment) \
  (((s) + ((alignment) - 1)) & (~((alignment) - 1)))

#define ALIGNMENT_FLOOR(s, alignment) ((s) & (~((alignment) - 1)))

static bool IsSameRoundedHugeClass(size_t aSize1, size_t aSize2,
                                   jemalloc_stats_t& aStats) {
  return (aSize1 > aStats.large_max && aSize2 > aStats.large_max &&
          ALIGNMENT_CEILING(aSize1 + aStats.page_size, aStats.chunksize) ==
              ALIGNMENT_CEILING(aSize2 + aStats.page_size, aStats.chunksize));
}

static bool CanReallocInPlace(size_t aFromSize, size_t aToSize,
                              jemalloc_stats_t& aStats) {
  // PHC allocations must be disabled because PHC reallocs differently to
  // mozjemalloc.
#ifdef MOZ_PHC
  MOZ_RELEASE_ASSERT(!mozilla::phc::IsPHCEnabledOnCurrentThread());
#endif

  if (aFromSize == malloc_good_size(aToSize)) {
    // Same size class: in-place.
    return true;
  }
  if (aFromSize >= aStats.page_size && aFromSize <= aStats.large_max &&
      aToSize >= aStats.page_size && aToSize <= aStats.large_max) {
    // Any large class to any large class: in-place when there is space to.
    return true;
  }
  if (IsSameRoundedHugeClass(aFromSize, aToSize, aStats)) {
    // Huge sizes that round up to the same multiple of the chunk size:
    // in-place.
    return true;
  }
  return false;
}

TEST(Jemalloc, InPlace)
{
  // Disable PHC allocations for this test, because CanReallocInPlace() isn't
  // valid for PHC allocations.
  AutoDisablePHCOnCurrentThread disable;

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);

  // Using a separate arena, which is always emptied after an iteration, ensures
  // that in-place reallocation happens in all cases it can happen. This test is
  // intended for developers to notice they may have to adapt other tests if
  // they change the conditions for in-place reallocation.
  arena_id_t arena = moz_create_arena();

  for (size_t from_size : SizeClassesBetween(1, 2 * stats.chunksize)) {
    SCOPED_TRACE(testing::Message() << "from_size = " << from_size);
    for (size_t to_size : sSizes) {
      SCOPED_TRACE(testing::Message() << "to_size = " << to_size);
      char* ptr = (char*)moz_arena_malloc(arena, from_size);
      char* ptr2 = (char*)moz_arena_realloc(arena, ptr, to_size);
      if (CanReallocInPlace(from_size, to_size, stats)) {
        EXPECT_EQ(ptr, ptr2);
      } else {
        EXPECT_NE(ptr, ptr2);
      }
      moz_arena_free(arena, ptr2);
    }
  }

  moz_dispose_arena(arena);
}

// Bug 1474254: disable this test for windows ccov builds because it leads to
// timeout.
#if !defined(XP_WIN) || !defined(MOZ_CODE_COVERAGE)
TEST(Jemalloc, JunkPoison)
{
  // Disable PHC allocations for this test, because CanReallocInPlace() isn't
  // valid for PHC allocations, and the testing UAFs aren't valid.
  AutoDisablePHCOnCurrentThread disable;

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);

  // Avoid death tests adding some unnecessary (long) delays.
  SAVE_GDB_SLEEP_LOCAL();

  // Create buffers in a separate arena, for faster comparisons with
  // bulk_compare.
  arena_id_t buf_arena = moz_create_arena();
  char* junk_buf = (char*)moz_arena_malloc(buf_arena, stats.page_size);
  // Depending on its configuration, the allocator will either fill the
  // requested allocation with the junk byte (0xe4) or with zeroes, or do
  // nothing, in which case, since we're allocating in a fresh arena,
  // we'll be getting zeroes.
  char junk = stats.opt_junk ? '\xe4' : '\0';
  for (size_t i = 0; i < stats.page_size; i++) {
    ASSERT_EQ(junk_buf[i], junk);
  }

  char* poison_buf = (char*)moz_arena_malloc(buf_arena, stats.page_size);
  memset(poison_buf, 0xe5, stats.page_size);

  static const char fill = 0x42;
  char* fill_buf = (char*)moz_arena_malloc(buf_arena, stats.page_size);
  memset(fill_buf, fill, stats.page_size);

  arena_params_t params;
  // Allow as many dirty pages in the arena as possible, so that purge never
  // happens in it. Purge breaks some of the tests below randomly depending on
  // what other things happen on other threads.
  params.mMaxDirty = size_t(-1);
  arena_id_t arena = moz_create_arena_with_params(&params);

  // Mozjemalloc is configured to only poison the first four cache lines.
  const size_t poison_check_len = 256;

  // Allocating should junk the buffer, and freeing should poison the buffer.
  for (size_t size : sSizes) {
    if (size <= stats.large_max) {
      SCOPED_TRACE(testing::Message() << "size = " << size);
      char* buf = (char*)moz_arena_malloc(arena, size);
      size_t allocated = moz_malloc_usable_size(buf);
      if (stats.opt_junk || stats.opt_zero) {
        ASSERT_NO_FATAL_FAILURE(
            bulk_compare(buf, 0, allocated, junk_buf, stats.page_size));
      }
      moz_arena_free(arena, buf);
      // We purposefully do a use-after-free here, to check that the data was
      // poisoned.
      ASSERT_NO_FATAL_FAILURE(
          bulk_compare(buf, 0, std::min(allocated, poison_check_len),
                       poison_buf, stats.page_size));
    }
  }

  // Shrinking in the same size class should be in place and poison between the
  // new allocation size and the old one.
  size_t prev = 0;
  for (size_t size : SizeClassesBetween(1, 2 * stats.chunksize)) {
    SCOPED_TRACE(testing::Message() << "size = " << size);
    SCOPED_TRACE(testing::Message() << "prev = " << prev);
    char* ptr = (char*)moz_arena_malloc(arena, size);
    memset(ptr, fill, moz_malloc_usable_size(ptr));
    char* ptr2 = (char*)moz_arena_realloc(arena, ptr, prev + 1);
    ASSERT_EQ(ptr, ptr2);
    ASSERT_NO_FATAL_FAILURE(
        bulk_compare(ptr, 0, prev + 1, fill_buf, stats.page_size));
    ASSERT_NO_FATAL_FAILURE(bulk_compare(ptr, prev + 1,
                                         std::min(size, poison_check_len),
                                         poison_buf, stats.page_size));
    moz_arena_free(arena, ptr);
    prev = size;
  }

  // In-place realloc should junk the new bytes when growing and poison the old
  // bytes when shrinking.
  for (size_t from_size : SizeClassesBetween(1, 2 * stats.chunksize)) {
    SCOPED_TRACE(testing::Message() << "from_size = " << from_size);
    for (size_t to_size : sSizes) {
      SCOPED_TRACE(testing::Message() << "to_size = " << to_size);
      if (CanReallocInPlace(from_size, to_size, stats)) {
        char* ptr = (char*)moz_arena_malloc(arena, from_size);
        memset(ptr, fill, moz_malloc_usable_size(ptr));
        char* ptr2 = (char*)moz_arena_realloc(arena, ptr, to_size);
        ASSERT_EQ(ptr, ptr2);
        // Shrinking allocation
        if (from_size >= to_size) {
          ASSERT_NO_FATAL_FAILURE(
              bulk_compare(ptr, 0, to_size, fill_buf, stats.page_size));
          // Huge allocations have guards and will crash when accessing
          // beyond the valid range.
          if (to_size > stats.large_max) {
            size_t page_limit = ALIGNMENT_CEILING(to_size, stats.page_size);
            ASSERT_NO_FATAL_FAILURE(bulk_compare(
                ptr, to_size, std::min(page_limit, poison_check_len),
                poison_buf, stats.page_size));
            ASSERT_DEATH_WRAP(ptr[page_limit] = 0, "");
          } else {
            ASSERT_NO_FATAL_FAILURE(bulk_compare(
                ptr, to_size, std::min(from_size, poison_check_len), poison_buf,
                stats.page_size));
          }
        } else {
          // Enlarging allocation
          ASSERT_NO_FATAL_FAILURE(
              bulk_compare(ptr, 0, from_size, fill_buf, stats.page_size));
          if (stats.opt_junk || stats.opt_zero) {
            ASSERT_NO_FATAL_FAILURE(bulk_compare(ptr, from_size, to_size,
                                                 junk_buf, stats.page_size));
          }
          // Huge allocation, so should have a guard page following
          if (to_size > stats.large_max) {
            ASSERT_DEATH_WRAP(
                ptr[ALIGNMENT_CEILING(to_size, stats.page_size)] = 0, "");
          }
        }
        moz_arena_free(arena, ptr2);
      }
    }
  }

  // Growing to a different size class should poison the old allocation,
  // preserve the original bytes, and junk the new bytes in the new allocation.
  for (size_t from_size : SizeClassesBetween(1, 2 * stats.chunksize)) {
    SCOPED_TRACE(testing::Message() << "from_size = " << from_size);
    for (size_t to_size : sSizes) {
      if (from_size < to_size && malloc_good_size(to_size) != from_size &&
          !IsSameRoundedHugeClass(from_size, to_size, stats)) {
        SCOPED_TRACE(testing::Message() << "to_size = " << to_size);
        char* ptr = (char*)moz_arena_malloc(arena, from_size);
        memset(ptr, fill, moz_malloc_usable_size(ptr));
        // Avoid in-place realloc by allocating a buffer, expecting it to be
        // right after the buffer we just received. Buffers smaller than the
        // page size and exactly or larger than the size of the largest large
        // size class can't be reallocated in-place.
        char* avoid_inplace = nullptr;
        if (from_size >= stats.page_size && from_size < stats.large_max) {
          avoid_inplace = (char*)moz_arena_malloc(arena, stats.page_size);
          ASSERT_EQ(ptr + from_size, avoid_inplace);
        }
        char* ptr2 = (char*)moz_arena_realloc(arena, ptr, to_size);
        ASSERT_NE(ptr, ptr2);
        if (from_size <= stats.large_max) {
          ASSERT_NO_FATAL_FAILURE(
              bulk_compare(ptr, 0, std::min(from_size, poison_check_len),
                           poison_buf, stats.page_size));
        }
        ASSERT_NO_FATAL_FAILURE(
            bulk_compare(ptr2, 0, from_size, fill_buf, stats.page_size));
        if (stats.opt_junk || stats.opt_zero) {
          size_t rounded_to_size = malloc_good_size(to_size);
          ASSERT_NE(to_size, rounded_to_size);
          ASSERT_NO_FATAL_FAILURE(bulk_compare(ptr2, from_size, rounded_to_size,
                                               junk_buf, stats.page_size));
        }
        moz_arena_free(arena, ptr2);
        moz_arena_free(arena, avoid_inplace);
      }
    }
  }

  // Shrinking to a different size class should poison the old allocation,
  // preserve the original bytes, and junk the extra bytes in the new
  // allocation.
  for (size_t from_size : SizeClassesBetween(1, 2 * stats.chunksize)) {
    SCOPED_TRACE(testing::Message() << "from_size = " << from_size);
    for (size_t to_size : sSizes) {
      if (from_size > to_size &&
          !CanReallocInPlace(from_size, to_size, stats)) {
        SCOPED_TRACE(testing::Message() << "to_size = " << to_size);
        char* ptr = (char*)moz_arena_malloc(arena, from_size);
        memset(ptr, fill, from_size);
        char* ptr2 = (char*)moz_arena_realloc(arena, ptr, to_size);
        ASSERT_NE(ptr, ptr2);
        if (from_size <= stats.large_max) {
          ASSERT_NO_FATAL_FAILURE(
              bulk_compare(ptr, 0, std::min(from_size, poison_check_len),
                           poison_buf, stats.page_size));
        }
        ASSERT_NO_FATAL_FAILURE(
            bulk_compare(ptr2, 0, to_size, fill_buf, stats.page_size));
        if (stats.opt_junk || stats.opt_zero) {
          size_t rounded_to_size = malloc_good_size(to_size);
          ASSERT_NE(to_size, rounded_to_size);
          ASSERT_NO_FATAL_FAILURE(bulk_compare(ptr2, from_size, rounded_to_size,
                                               junk_buf, stats.page_size));
        }
        moz_arena_free(arena, ptr2);
      }
    }
  }

  moz_dispose_arena(arena);

  moz_arena_free(buf_arena, poison_buf);
  moz_arena_free(buf_arena, junk_buf);
  moz_arena_free(buf_arena, fill_buf);
  moz_dispose_arena(buf_arena);

  RESTORE_GDB_SLEEP_LOCAL();
}
#endif  // !defined(XP_WIN) || !defined(MOZ_CODE_COVERAGE)

TEST(Jemalloc, TrailingGuard)
{
  // Disable PHC allocations for this test, because even a single PHC
  // allocation occurring can throw it off.
  AutoDisablePHCOnCurrentThread disable;

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);

  // Avoid death tests adding some unnecessary (long) delays.
  SAVE_GDB_SLEEP_LOCAL();

  arena_id_t arena = moz_create_arena();
  ASSERT_TRUE(arena != 0);

  // Do enough large allocations to fill a chunk, and then one additional one,
  // and check that the guard page is still present after the one-but-last
  // allocation, i.e. that we didn't allocate the guard.
  Vector<void*> ptr_list;
  for (size_t cnt = 0; cnt < stats.large_max / stats.page_size; cnt++) {
    void* ptr = moz_arena_malloc(arena, stats.page_size);
    ASSERT_TRUE(ptr != nullptr);
    ASSERT_TRUE(ptr_list.append(ptr));
  }

  void* last_ptr_in_chunk = ptr_list[ptr_list.length() - 1];
  void* extra_ptr = moz_arena_malloc(arena, stats.page_size);
  void* guard_page = (void*)ALIGNMENT_CEILING(
      (uintptr_t)last_ptr_in_chunk + stats.page_size, stats.page_size);
  jemalloc_ptr_info_t info;
  jemalloc_ptr_info(guard_page, &info);
  ASSERT_TRUE(jemalloc_ptr_is_freed_page(&info));

  ASSERT_DEATH_WRAP(*(char*)guard_page = 0, "");

  for (void* ptr : ptr_list) {
    moz_arena_free(arena, ptr);
  }
  moz_arena_free(arena, extra_ptr);

  moz_dispose_arena(arena);

  RESTORE_GDB_SLEEP_LOCAL();
}

TEST(Jemalloc, LeadingGuard)
{
  // Disable PHC allocations for this test, because even a single PHC
  // allocation occurring can throw it off.
  AutoDisablePHCOnCurrentThread disable;

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);

  // Avoid death tests adding some unnecessary (long) delays.
  SAVE_GDB_SLEEP_LOCAL();

  arena_id_t arena = moz_create_arena();
  ASSERT_TRUE(arena != 0);

  // Do a simple normal allocation, but force all the allocation space
  // in the chunk to be used up. This allows us to check that we get
  // the safe area right in the logic that follows (all memory will be
  // committed and initialized), and it forces this pointer to the start
  // of the zone to sit at the very start of the usable chunk area.
  void* ptr = moz_arena_malloc(arena, stats.large_max);
  ASSERT_TRUE(ptr != nullptr);
  // If ptr is chunk-aligned, the above allocation went wrong.
  void* chunk_start = (void*)ALIGNMENT_FLOOR((uintptr_t)ptr, stats.chunksize);
  ASSERT_NE((uintptr_t)ptr, (uintptr_t)chunk_start);
  // If ptr is 1 page after the chunk start (so right after the header),
  // we must have missed adding the guard page.
  ASSERT_NE((uintptr_t)ptr, (uintptr_t)chunk_start + stats.page_size);
  // The actual start depends on the amount of metadata versus the page
  // size, so we can't check equality without pulling in too many
  // implementation details.

  // Guard page should be right before data area
  void* guard_page = (void*)(((uintptr_t)ptr) - sizeof(void*));
  jemalloc_ptr_info_t info;
  jemalloc_ptr_info(guard_page, &info);
  ASSERT_TRUE(info.tag == TagUnknown);
  ASSERT_DEATH_WRAP(*(char*)guard_page = 0, "");

  moz_arena_free(arena, ptr);
  moz_dispose_arena(arena);

  RESTORE_GDB_SLEEP_LOCAL();
}

TEST(Jemalloc, DisposeArena)
{
  jemalloc_stats_t stats;
  jemalloc_stats(&stats);

  // Avoid death tests adding some unnecessary (long) delays.
  SAVE_GDB_SLEEP_LOCAL();

  arena_id_t arena = moz_create_arena();
  void* ptr = moz_arena_malloc(arena, 42);
  // Disposing of the arena when it's not empty is a MOZ_CRASH-worthy error.
  ASSERT_DEATH_WRAP(moz_dispose_arena(arena), "");
  moz_arena_free(arena, ptr);
  moz_dispose_arena(arena);

  arena = moz_create_arena();
  ptr = moz_arena_malloc(arena, stats.page_size * 2);
  // Disposing of the arena when it's not empty is a MOZ_CRASH-worthy error.
  ASSERT_DEATH_WRAP(moz_dispose_arena(arena), "");
  moz_arena_free(arena, ptr);
  moz_dispose_arena(arena);

  arena = moz_create_arena();
  ptr = moz_arena_malloc(arena, stats.chunksize * 2);
#ifdef MOZ_DEBUG
  // On debug builds, we do the expensive check that arenas are empty.
  ASSERT_DEATH_WRAP(moz_dispose_arena(arena), "");
  moz_arena_free(arena, ptr);
  moz_dispose_arena(arena);
#else
  // Currently, the allocator can't trivially check whether the arena is empty
  // of huge allocations, so disposing of it works.
  moz_dispose_arena(arena);
  // But trying to free a pointer that belongs to it will MOZ_CRASH.
  ASSERT_DEATH_WRAP(free(ptr), "");
  // Likewise for realloc
  ASSERT_DEATH_WRAP(ptr = realloc(ptr, stats.chunksize * 3), "");
#endif

  // Using the arena after it's been disposed of is MOZ_CRASH-worthy.
  ASSERT_DEATH_WRAP(moz_arena_malloc(arena, 42), "");

  RESTORE_GDB_SLEEP_LOCAL();
}

// An arena that keeps reusing a dirty page never becomes eligible for a regular
// deferred purge. A target purge must release the pages it does not reuse once
// the maximum purge delay has passed.
TEST(Jemalloc, DeferredPurgeOfBusyArena)
{
  jemalloc_free_dirty_pages();

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  const size_t pageSize = stats.page_size;

  const size_t kMaxDirty = 64;
  // A target purge that keeps a single reused page stops at half the
  // allowance, where the regular purge stops.
  const size_t kStopPages = kMaxDirty / 2;
  arena_params_t params;
  params.mMaxDirty = kMaxDirty;
  arena_id_t arena = moz_create_arena_with_params(&params);
  bool wasDeferred = moz_enable_deferred_purge(true);

  // Dirty many more pages than the allowance, spread over several chunks.
  // Keep every kPinEvery-th block, so that no chunk becomes empty and gets
  // released right away as an excess spare chunk.
  const size_t kPagesPerBlock = 4;
  const size_t kBlocks = 16 * kMaxDirty / kPagesPerBlock;
  const size_t kPinEvery = 8;
  const size_t kDirtyPages = (kBlocks - kBlocks / kPinEvery) * kPagesPerBlock;
  const size_t kBlockSize = kPagesPerBlock * pageSize;
  ASSERT_GT(stats.chunksize / kBlockSize, kPinEvery);
  void* blocks[kBlocks];
  for (void*& block : blocks) {
    block = moz_arena_malloc(arena, kBlockSize);
  }
  // jemalloc_stats() counts the dirty pages of all arenas (in bytes), the
  // checks below leave some room for the other arenas to change: up to half of
  // the freed pages for other arenas purging, and three times kStopPages for
  // other arenas dirtying pages.
  jemalloc_stats(&stats);
  const size_t dirtyBefore = stats.pages_dirty;
  for (size_t i = 0; i < kBlocks; i++) {
    if (i % kPinEvery) {
      moz_arena_free(arena, blocks[i]);
    }
  }
  // Freeing does not purge while the purge is deferred.
  jemalloc_stats(&stats);
  ASSERT_GE(stats.pages_dirty - dirtyBefore, kDirtyPages / 2 * pageSize);

  // Reuse one dirty page for many maximum purge delays and let the purge run
  // in between like the main thread would. The minimum purge delay is never
  // reached, as if the arena reused pages more often than that, so only a
  // target purge can release pages.
  const uint32_t kNeverMS = 60000;
  const uint32_t kMaxDelayMS = 50;
  auto reuseAndPurge = [&](uint32_t aMaxPurgeDelayMS) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(10 * kMaxDelayMS);
    while (std::chrono::steady_clock::now() < deadline) {
      void* page = moz_arena_malloc(arena, pageSize);
      moz_arena_free(arena, page);
      moz_may_purge_now(false, kNeverMS, aMaxPurgeDelayMS, nullptr, nullptr);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };

  // Without a maximum purge delay nothing is purged.
  reuseAndPurge(0);
  jemalloc_stats(&stats);
  EXPECT_GE(stats.pages_dirty - dirtyBefore, kDirtyPages / 2 * pageSize);

  // With it, a target purge keeps only the reused page, so it stops at
  // kStopPages. The bound adds the room of three times kStopPages for other
  // arenas from above.
  reuseAndPurge(kMaxDelayMS);
  jemalloc_stats(&stats);
  EXPECT_LE(stats.pages_dirty, dirtyBefore + 4 * kStopPages * pageSize);

  for (size_t i = 0; i < kBlocks; i += kPinEvery) {
    moz_arena_free(arena, blocks[i]);
  }
  moz_enable_deferred_purge(wasDeferred);
  moz_dispose_arena(arena);
}

// A target purge only releases the dirty pages that were unused since the last
// purge. If the arena reused more pages than its allowance, the target purge
// stops above it without ending the purge. If the arena stops reusing pages
// right after, the regular purge must still release them.
TEST(Jemalloc, DeferredPurgeAfterTargetPurge)
{
  jemalloc_free_dirty_pages();

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  const size_t pageSize = stats.page_size;

  const size_t kMaxDirty = 64;
  // As above, the regular purge stops at half the allowance.
  const size_t kStopPages = kMaxDirty / 2;
  arena_params_t params;
  params.mMaxDirty = kMaxDirty;
  arena_id_t arena = moz_create_arena_with_params(&params);
  bool wasDeferred = moz_enable_deferred_purge(true);

  // As above, keep every kPinEvery-th block so that no chunk becomes empty.
  const size_t kPagesPerBlock = 4;
  const size_t kBlocks = 16 * kMaxDirty / kPagesPerBlock;
  const size_t kPinEvery = 8;
  const size_t kDirtyPages = (kBlocks - kBlocks / kPinEvery) * kPagesPerBlock;
  const size_t kBlockSize = kPagesPerBlock * pageSize;
  ASSERT_GT(stats.chunksize / kBlockSize, kPinEvery);
  // Dirty pages that the working set below does not all reuse, the target
  // purge releases those that stay unused.
  void* blocks[kBlocks];
  for (void*& block : blocks) {
    block = moz_arena_malloc(arena, kBlockSize);
  }
  // As above, jemalloc_stats() counts the dirty pages of all arenas.
  jemalloc_stats(&stats);
  const size_t dirtyBefore = stats.pages_dirty;
  for (size_t i = 0; i < kBlocks; i++) {
    if (i % kPinEvery) {
      moz_arena_free(arena, blocks[i]);
    }
  }

  // Reuse a working set far above the allowance until a purge releases pages.
  // How many pages stay unused between two purges depends on where the working
  // set lands, so the first target purges may release nothing. The minimum
  // purge delay is never reached, so this can only be a target purge.
  const uint32_t kNeverMS = 60000;
  const uint32_t kGraceMS = 100;
  // The working set fits into the freed blocks, so that it stays in the pinned
  // chunks.
  const size_t kWorkingSetPages = kDirtyPages / 2;
  void* workingSet[kWorkingSetPages];
  bool targetPurged = false;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(20 * kGraceMS);
  while (!targetPurged && std::chrono::steady_clock::now() < deadline) {
    for (void*& page : workingSet) {
      page = moz_arena_malloc(arena, pageSize);
    }
    for (void* page : workingSet) {
      moz_arena_free(arena, page);
    }
    jemalloc_stats(&stats);
    const size_t dirtyFreed = stats.pages_dirty;
    moz_may_purge_now(false, kNeverMS, kGraceMS, nullptr, nullptr);
    jemalloc_stats(&stats);
    targetPurged = stats.pages_dirty < dirtyFreed;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_TRUE(targetPurged);
  // It kept the reused working set. A quarter of it leaves room for the other
  // arenas and is still far above the allowance, so the purge did not end.
  static_assert(kWorkingSetPages / 4 > kMaxDirty);
  ASSERT_GT(stats.pages_dirty, dirtyBefore + kWorkingSetPages / 4 * pageSize);

  // Now the arena is quiet and the regular purge can run. Give it a few
  // minimum purge delays, other arenas may be queued, too.
  const int kQuietPurges = 5;
  for (int i = 0; i < kQuietPurges; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kGraceMS));
    moz_may_purge_now(false, kGraceMS, kGraceMS, nullptr, nullptr);
  }

  // As above, with room for the other arenas.
  jemalloc_stats(&stats);
  EXPECT_LE(stats.pages_dirty, dirtyBefore + 4 * kStopPages * pageSize);

  for (size_t i = 0; i < kBlocks; i += kPinEvery) {
    moz_arena_free(arena, blocks[i]);
  }
  moz_enable_deferred_purge(wasDeferred);
  moz_dispose_arena(arena);
}

// An arena that reused all its dirty pages since its last purge has none to
// release in a target purge. If the arena is within its allowance, the target
// purge also ends its purge, so the arena is no longer queued.
TEST(Jemalloc, TargetPurgeWithinAllowanceEndsPurge)
{
  jemalloc_free_dirty_pages();

  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  const size_t pageSize = stats.page_size;

  const size_t kMaxDirty = 64;
  arena_params_t params;
  params.mMaxDirty = kMaxDirty;
  arena_id_t arena = moz_create_arena_with_params(&params);
  bool wasDeferred = moz_enable_deferred_purge(true);

  // Keep the chunk in use, so that it does not become a spare chunk that a
  // purge releases as a whole.
  void* pin = moz_arena_malloc(arena, pageSize);

  // Queue the arena with more dirty pages than its allowance, then reuse them
  // until it is within its allowance again. Leave more than half of the
  // allowance dirty, where a regular purge would stop, so that only a target
  // purge releases nothing.
  const size_t kPagesPerBlock = 16;
  const size_t kBlocks = 3 * kMaxDirty / kPagesPerBlock;
  const size_t kKeptBlocks = 3 * kMaxDirty / 4 / kPagesPerBlock;
  const size_t kReused = kBlocks - kKeptBlocks;
  static_assert(kBlocks * kPagesPerBlock > kMaxDirty);
  static_assert(kKeptBlocks * kPagesPerBlock <= kMaxDirty);
  static_assert(kKeptBlocks * kPagesPerBlock > kMaxDirty / 2);
  const size_t kBlockSize = kPagesPerBlock * pageSize;
  void* blocks[kBlocks];
  for (void*& block : blocks) {
    block = moz_arena_malloc(arena, kBlockSize);
  }
  for (void* block : blocks) {
    moz_arena_free(arena, block);
  }
  for (size_t i = 0; i < kReused; i++) {
    blocks[i] = moz_arena_malloc(arena, kBlockSize);
  }

  // The minimum purge delay is never reached, so this can only be a target
  // purge. Purge until no arena is queued, other arenas may be queued, too.
  const uint32_t kNeverMS = 60000;
  const uint32_t kMaxDelayMS = 50;
  const int kMaxPurgeSteps = 16;
  std::this_thread::sleep_for(std::chrono::milliseconds(2 * kMaxDelayMS));
  jemalloc_stats(&stats);
  const size_t dirtyBefore = stats.pages_dirty;
  for (int i = 0; i < kMaxPurgeSteps; i++) {
    if (moz_may_purge_now(false, kNeverMS, kMaxDelayMS, nullptr, nullptr) !=
        may_purge_now_result_t::NeedsMore) {
      break;
    }
  }
  // Nothing was released. A regular purge would have released the dirty pages
  // above half of the allowance, the slack for other arenas is half of that.
  const size_t kSlackPages = (kKeptBlocks * kPagesPerBlock - kMaxDirty / 2) / 2;
  jemalloc_stats(&stats);
  EXPECT_GE(stats.pages_dirty + kSlackPages * pageSize, dirtyBefore);
  EXPECT_EQ(moz_may_purge_now(true, kNeverMS, kMaxDelayMS, nullptr, nullptr),
            may_purge_now_result_t::Done);

  for (size_t i = 0; i < kReused; i++) {
    moz_arena_free(arena, blocks[i]);
  }
  moz_arena_free(arena, pin);
  moz_enable_deferred_purge(wasDeferred);
  moz_dispose_arena(arena);
}

static void CheckPtr(void* ptr, size_t size) {
  EXPECT_TRUE(ptr);
  jemalloc_ptr_info_t info;
  jemalloc_ptr_info(ptr, &info);
  EXPECT_EQ(info.tag, TagLiveAlloc);
  EXPECT_EQ(info.size, malloc_good_size(size));
}

static void CheckStats(const char* operation, unsigned iteration,
                       jemalloc_stats_lite_t& baseline,
                       jemalloc_stats_lite_t& stats, size_t num_ops,
                       ptrdiff_t bytes_diff) {
  if ((baseline.allocated_bytes + bytes_diff != stats.allocated_bytes

       || baseline.num_operations + num_ops != stats.num_operations)) {
    // All the tests that check stats, perform some operation, then check stats
    // again can race with other threads.  But the test can't be made thread
    // safe without a sagnificant amount of work.  However this IS a problem
    // when stepping through the test using a debugger, since other threads are
    // likely to run while the current thread is paused.  Instead of neading a
    // debugger our printf here can help understand a failing test.
    fprintf(stderr, "Check stats failed after iteration %u operation %s\n",
            iteration, operation);

    EXPECT_EQ(baseline.allocated_bytes + bytes_diff, stats.allocated_bytes);
    EXPECT_EQ(baseline.num_operations + num_ops, stats.num_operations);
  }
}

struct DisposeWhilePurgingClosure {
  arena_id_t mArena;
  void** mPins;
  size_t mNumPins;
  bool mDisposed;
};

// Called by the purge between two steps, without any lock held.
static bool DisposeWhilePurging(void* aClosure) {
  auto* closure = static_cast<DisposeWhilePurgingClosure*>(aClosure);
  if (!closure->mDisposed) {
    for (size_t i = 0; i < closure->mNumPins; i++) {
      moz_arena_free(closure->mArena, closure->mPins[i]);
    }
    moz_dispose_arena(closure->mArena);
    closure->mDisposed = true;
  }
  return true;
}

// An arena disposed of while a purge is working on one of its chunks is
// deleted by that purge. The chunk must still be released.
TEST(Jemalloc, DisposeArenaWhilePurging)
{
  jemalloc_stats_t stats;
  jemalloc_stats(&stats);
  const size_t pageSize = stats.page_size;

  // Enough steps for the purge of any arenas that are queued.
  const int kMaxPurgeSteps = 100;

  // Finish any pending purge, so that the purge below works on our arena. If
  // another arena was still queued, the callback could dispose of our arena
  // while that one is purged, and this test would not reach the case it is
  // about. Disabling deferred purge runs all pending purges right away.
  bool wasDeferred = moz_enable_deferred_purge(true);
  moz_enable_deferred_purge(false);
  moz_enable_deferred_purge(true);

  // Free runs of dirty pages, separated by pins so that they stay separate
  // runs in the same chunk. The purge then releases one run per step. When the
  // arena is disposed of after the first step, the purge must still want to
  // continue: the chunk has dirty pages left, and the arena is above the
  // target of half its allowance. That is the case where the purge has to
  // finish the chunk before it returns.
  const size_t kMaxDirty = 8;
  const size_t kRuns = 8;
  const size_t kRunPages = 4;
  static_assert(kRuns * kRunPages > kMaxDirty, "freeing must queue the arena");
  static_assert((kRuns - 1) * kRunPages > kMaxDirty / 2,
                "the first step must not reach the target");
  ASSERT_LT(kRuns * (kRunPages + 1) * pageSize, stats.chunksize);

  arena_params_t params;
  params.mMaxDirty = kMaxDirty;
  arena_id_t arena = moz_create_arena_with_params(&params);

  void* runs[kRuns];
  void* pins[kRuns];
  for (size_t i = 0; i < kRuns; i++) {
    runs[i] = moz_arena_malloc(arena, kRunPages * pageSize);
    pins[i] = moz_arena_malloc(arena, pageSize);
  }
  void* inChunk = runs[0];
  for (void* run : runs) {
    moz_arena_free(arena, run);
  }

  // After the first step, empty the arena and dispose of it.
  DisposeWhilePurgingClosure closure{arena, pins, kRuns, false};
  for (int i = 0; i < kMaxPurgeSteps; i++) {
    // Purge the arena immediately, no target purge.
    if (moz_may_purge_now(false, 0, 0, DisposeWhilePurging, &closure) !=
        may_purge_now_result_t::NeedsMore) {
      break;
    }
  }
  moz_enable_deferred_purge(wasDeferred);
  ASSERT_TRUE(closure.mDisposed);

  // The chunk is no longer known to the allocator. If it was leaked,
  // jemalloc_ptr_info() finds it with its deleted arena: with diagnostic
  // asserts it crashes on the arena's magic number, without them it reports
  // the chunk's pages rather than TagUnknown, so the check below fails.
  jemalloc_ptr_info_t info;
  jemalloc_ptr_info(inChunk, &info);
  EXPECT_EQ(info.tag, TagUnknown);
}

TEST(Jemalloc, StatsLite)
{
  // Disable PHC allocations for this test, because even a single PHC
  // allocation occurring can throw it off.
  AutoDisablePHCOnCurrentThread disable;

  // Use this data to make an allocation, resize it twice, then free it.  Some
  // The data uses a few size classes and does a combination of in-place and
  // moving reallocations.
  struct {
    // The initial allocation size.
    size_t initial;
    // The first reallocation size and number of operations of the reallocation.
    size_t next;
    size_t next_ops;
    // The final reallocation size and number of operations of the reallocation.
    size_t last;
    size_t last_ops;
  } TestData[] = {
      /* clang-format off */
      { 16,      15,     0, 256,     2},
      {128_KiB,  64_KiB, 1,  68_KiB, 1},
      {  4_MiB,  16_MiB, 2,   3_MiB, 2},
      { 16_KiB, 512,     2,  32_MiB, 2},
      /* clang-format on */
  };

  arena_id_t my_arena = moz_create_arena();

  unsigned i = 0;
  for (auto data : TestData) {
    // Assert that the API returns /something/ a bit sensible.
    jemalloc_stats_lite_t baseline;
    jemalloc_stats_lite(&baseline);

    // Allocate an object.
    void* ptr = moz_arena_malloc(my_arena, data.initial);
    CheckPtr(ptr, data.initial);

    jemalloc_stats_lite_t stats1;
    jemalloc_stats_lite(&stats1);
    CheckStats("malloc()", i, baseline, stats1, 1,
               malloc_good_size(data.initial));

    // realloc the item in-place.
    ptr = moz_arena_realloc(my_arena, ptr, data.next);
    CheckPtr(ptr, data.next);

    jemalloc_stats_lite_t stats2;
    jemalloc_stats_lite(&stats2);
    CheckStats("realloc() 1", i, stats1, stats2, data.next_ops,
               malloc_good_size(data.next) - malloc_good_size(data.initial));

    // realloc so it has to move to a different location
    ptr = moz_arena_realloc(my_arena, ptr, data.last);
    CheckPtr(ptr, data.last);

    jemalloc_stats_lite_t stats3;
    jemalloc_stats_lite(&stats3);
    CheckStats("realloc() 2", i, stats2, stats3, data.last_ops,
               malloc_good_size(data.last) - malloc_good_size(data.next));

    moz_arena_free(my_arena, ptr);
    jemalloc_stats_lite_t stats4;
    jemalloc_stats_lite(&stats4);
    CheckStats("free()", i, stats3, stats4, 1, -malloc_good_size(data.last));

    i++;
  }

  moz_dispose_arena(my_arena);
}

void TestBaseAlloc(size_t size, unsigned& hits) {
  SCOPED_TRACE(testing::Message() << "testing size " << size);
  void* ptr1 = sBaseAlloc.alloc(size);
  EXPECT_TRUE(ptr1);
  EXPECT_TRUE(size <= sBaseAlloc.usable_size(ptr1));

  void* ptr2 = sBaseAlloc.alloc(size);
  EXPECT_TRUE(ptr2);
  EXPECT_TRUE(size <= sBaseAlloc.usable_size(ptr2));

  EXPECT_NE(ptr1, ptr2);

  // Freeing the memory and allocating more of the same size should reuse the
  // same address.
  sBaseAlloc.free(ptr1);
  void* ptr3 = sBaseAlloc.alloc(size);
  EXPECT_TRUE(size <= sBaseAlloc.usable_size(ptr3));
  if (ptr1 == ptr3) {
    hits++;
  }

  sBaseAlloc.free(ptr2);
  sBaseAlloc.free(ptr3);
}

TEST(Jemalloc, EnableDeferredPurge)
{
  bool wasDeferred = moz_enable_deferred_purge(true);
  EXPECT_TRUE(moz_enable_deferred_purge(false));
  EXPECT_FALSE(moz_enable_deferred_purge(false));
  EXPECT_FALSE(moz_enable_deferred_purge(true));
  EXPECT_TRUE(moz_enable_deferred_purge(true));
  moz_enable_deferred_purge(wasDeferred);
}

TEST(Jemalloc, BaseAlloc)
{
  // Used to calculate the free list hit rate, see the end of the test.
  unsigned tries = 0;
  unsigned hits = 0;

  // Try varying sizes to hit each size class.  The base allocator's size
  // classes depend on cache line and C++ structure sizes, they'll be at least
  // 16 from each-other.
  for (size_t size = 8; size < 1024; size += 8) {
    tries++;
    TestBaseAlloc(size, hits);
  }
  for (size_t size = 1024; size < 8192; size += 128) {
    tries++;
    TestBaseAlloc(size, hits);
  }
  for (size_t size = 8192; size < 8_MiB; size += 1024) {
    tries++;
    TestBaseAlloc(size, hits);
  }

  void* ptr1 = sBaseAlloc.calloc(100, 7);
  EXPECT_TRUE(ptr1);
  EXPECT_TRUE(100 * 7 <= sBaseAlloc.usable_size(ptr1));
  sBaseAlloc.free(ptr1);

  // Free list hit rate is at least 90% (its typically 99%)
  EXPECT_TRUE(float(hits) / float(tries) >= 0.9f);
}

TEST(Jemalloc, BaseAllocMergeSplit)
{
  // Test merging and splitting together, start with a large allocation then
  // free it, ensuring it lands in the free lists.
  void* ptr1 = sBaseAlloc.alloc(4096);
  size_t ptr1_size = sBaseAlloc.usable_size(ptr1);
  ASSERT_TRUE(4096 <= ptr1_size);
  sBaseAlloc.free(ptr1);

  // For small enough sizes we know the allocator will return memory in LIFO
  // order so naively it would appear that we don't need loops to find recently
  // inserted cells. However the cell we just freed is 4096 bytes and it'll be
  // on that free list. The loop here needs to exhaust the smaller free lists
  // before finding and splitting the 4096 cell.  The smaller cells will be
  // leaked.
  BaseAlloc::Stats stats_before = sBaseAlloc.GetStats();
  void* ptr2;
  size_t ptr2_size;
  while (true) {
    ptr2 = sBaseAlloc.alloc(2048);
    ptr2_size = sBaseAlloc.usable_size(ptr2);
    ASSERT_TRUE(2048 <= ptr2_size);
    if (ptr2 == ptr1) {
      ASSERT_TRUE(ptr1 == ptr2);
      // To prove that it did indeed split the cell the new size must be less
      // than the old size.
      ASSERT_TRUE(ptr2_size < ptr1_size);
      break;
    }

    BaseAlloc::Stats stats = sBaseAlloc.GetStats();
    ASSERT_EQ(stats.mCommitted, stats_before.mCommitted);
    if (stats.mCommitted != stats_before.mCommitted) {
      return;
    }
  }

  // We should also be able to allocate the other half of the split cell.  Again
  // a loop is used because anything in free lists between 1024 bytes and 2048
  // bytes (where the previous loop started) must be exhausted first.
  void* ptr3;
  while (true) {
    ptr3 = sBaseAlloc.alloc(1024);
    size_t ptr3_size = sBaseAlloc.usable_size(ptr3);
    ASSERT_TRUE(1024 <= ptr2_size);
    if ((uintptr_t(ptr1) < uintptr_t(ptr3)) &&
        (uintptr_t(ptr3) < uintptr_t(ptr1) + 4096)) {
      ASSERT_TRUE(ptr3_size <= ptr1_size + ptr2_size);
      break;
    }

    BaseAlloc::Stats stats = sBaseAlloc.GetStats();
    ASSERT_EQ(stats.mCommitted, stats_before.mCommitted);
    if (stats.mCommitted != stats_before.mCommitted) {
      return;
    }
  }

  // To test merging we should now be able to free ptr2 and ptr3 and allocate a
  // new cell the size of ptr1 in the same place.
  sBaseAlloc.free(ptr2);
  sBaseAlloc.free(ptr3);
  // This also uses a loop because the cell we're looking for may have been
  // merged with more cells than we planned placing it in the oversize tree and
  // we must exhaust the 4096 list first.  Although this won't happen on
  // the cell's left without a different free() occuring during the test, so we
  // don't check for that case.
  while (true) {
    ptr1 = sBaseAlloc.alloc(4096);
    if (ptr1 == ptr2) {
      ASSERT_TRUE(4096 <= sBaseAlloc.usable_size(ptr1));
      break;
    }

    BaseAlloc::Stats stats = sBaseAlloc.GetStats();
    ASSERT_EQ(stats.mCommitted, stats_before.mCommitted);
    if (stats.mCommitted != stats_before.mCommitted) {
      return;
    }
  }

  sBaseAlloc.free(ptr1);
}

TEST(Jemalloc, BaseAllocRealloc)
{
  void* ptr1 = sBaseAlloc.alloc(4096);
  size_t ptr1_size = sBaseAlloc.usable_size(ptr1);
  ASSERT_TRUE(4096 <= ptr1_size);

  // Test realloc using the same allocation, first by shrinking.  Since we're
  // doing in-place reallocs here ptr1 and ptr2 are interchangeable.
  void* ptr2 = sBaseAlloc.realloc(ptr1, 2048);
  size_t ptr2_size = sBaseAlloc.usable_size(ptr2);
  ASSERT_TRUE(2048 <= ptr2_size);
  ASSERT_TRUE(4096 > ptr2_size);
  // all shrinking reallocs are in-place.
  ASSERT_EQ(ptr1, ptr2);

  // We should now be able to allocate the other half of the split cell.
  BaseAlloc::Stats stats_before = sBaseAlloc.GetStats();
  void* ptr3;
  while (true) {
    ptr3 = sBaseAlloc.alloc(1024);
    size_t ptr3_size = sBaseAlloc.usable_size(ptr3);
    ASSERT_TRUE(1024 <= ptr3_size);
    if ((uintptr_t(ptr1) < uintptr_t(ptr3)) &&
        (uintptr_t(ptr3) < uintptr_t(ptr1) + 4096)) {
      ASSERT_TRUE(ptr3_size < ptr1_size);
      break;
    }

    BaseAlloc::Stats stats = sBaseAlloc.GetStats();
    ASSERT_EQ(stats.mCommitted, stats_before.mCommitted);
  }
  sBaseAlloc.free(ptr3);

  // And now that it's free we should be able to realloc in place.
  ptr1 = sBaseAlloc.realloc(ptr2, 4096);
  ptr1_size = sBaseAlloc.usable_size(ptr1);
  ASSERT_EQ(ptr1, ptr2);
  ASSERT_TRUE(4096 <= ptr1_size);

  // Shrink it again (in-place).
  ptr2 = sBaseAlloc.realloc(ptr1, 2048);
  ptr2_size = sBaseAlloc.usable_size(ptr2);
  ASSERT_TRUE(2048 <= ptr2_size);
  ASSERT_TRUE(4096 > ptr2_size);
  ASSERT_EQ(ptr1, ptr2);

  // Allocate the neighbour cell once more, but don't free it.
  while (true) {
    ptr3 = sBaseAlloc.alloc(1024);
    size_t ptr3_size = sBaseAlloc.usable_size(ptr3);
    ASSERT_TRUE(1024 <= ptr3_size);
    if ((uintptr_t(ptr1) < uintptr_t(ptr3)) &&
        (uintptr_t(ptr3) < uintptr_t(ptr1) + 4096)) {
      ASSERT_TRUE(ptr3_size < ptr1_size);
      break;
    }

    BaseAlloc::Stats stats = sBaseAlloc.GetStats();
    ASSERT_EQ(stats.mCommitted, stats_before.mCommitted);
  }

  // Instead call realloc forcing it to move the cell.
  *(uintptr_t*)(ptr2) = 0xCAFE1234;
  ptr1 = sBaseAlloc.realloc(ptr2, 4096);
  ASSERT_NE(ptr1, ptr2);
  ASSERT_TRUE(4096 <= sBaseAlloc.usable_size(ptr1));
  ASSERT_EQ(*(uintptr_t*)(ptr1), 0xCAFE1234);

  // The origional cell should now be available for a 2048 byte allocation.
  void* ptr4 = sBaseAlloc.alloc(2048);
  ASSERT_EQ(ptr4, ptr2);
  ASSERT_TRUE(2048 <= sBaseAlloc.usable_size(ptr4));
  ASSERT_TRUE(4096 > sBaseAlloc.usable_size(ptr4));

  sBaseAlloc.free(ptr1);
  sBaseAlloc.free(ptr3);
  sBaseAlloc.free(ptr4);
}
