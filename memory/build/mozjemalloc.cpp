/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Portions of this file were originally under the following license:
//
// Copyright (C) 2006-2008 Jason Evans <jasone@FreeBSD.org>.
// All rights reserved.
// Copyright (C) 2007-2017 Mozilla Foundation.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice(s), this list of conditions and the following disclaimer as
//    the first lines of this file unmodified other than the possible
//    addition of one or more copyright notices.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice(s), this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// *****************************************************************************
//
// This allocator implementation is designed to provide scalable performance
// for multi-threaded programs on multi-processor systems.  The following
// features are included for this purpose:
//
//   + Multiple arenas are used if there are multiple CPUs, which reduces lock
//     contention and cache sloshing.
//
//   + Cache line sharing between arenas is avoided for internal data
//     structures.
//
//   + Memory is managed in chunks and runs (chunks can be split into runs),
//     rather than as individual pages.  This provides a constant-time
//     mechanism for associating allocations with particular arenas.
//
// Allocation requests are rounded up to the nearest size class, and no record
// of the original request size is maintained.  Allocations are broken into
// categories according to size class.  Assuming runtime defaults, the size
// classes in each category are as follows (for x86, x86_64 and Apple Silicon):
//
//   |=========================================================|
//   | Category | Subcategory    |     x86 |  x86_64 | Mac ARM |
//   |---------------------------+---------+---------+---------|
//   | Word size                 |  32 bit |  64 bit |  64 bit |
//   | Page size                 |    4 Kb |    4 Kb |   16 Kb |
//   |=========================================================|
//   | Small    | Quantum-spaced |      16 |      16 |      16 |
//   |          |                |      32 |      32 |      32 |
//   |          |                |      48 |      48 |      48 |
//   |          |                |     ... |     ... |     ... |
//   |          |                |     480 |     480 |     480 |
//   |          |                |     496 |     496 |     496 |
//   |          |----------------+---------|---------|---------|
//   |          | Quantum-wide-  |     512 |     512 |     512 |
//   |          | spaced         |     768 |     768 |     768 |
//   |          |                |     ... |     ... |     ... |
//   |          |                |    3584 |    3584 |    3584 |
//   |          |                |    3840 |    3840 |    3840 |
//   |          |----------------+---------|---------|---------|
//   |          | Sub-page       |       - |       - |    4096 |
//   |          |                |       - |       - |    8 kB |
//   |=========================================================|
//   | Large                     |    4 kB |    4 kB |       - |
//   |                           |    8 kB |    8 kB |       - |
//   |                           |   12 kB |   12 kB |       - |
//   |                           |   16 kB |   16 kB |   16 kB |
//   |                           |     ... |     ... |       - |
//   |                           |   32 kB |   32 kB |   32 kB |
//   |                           |     ... |     ... |     ... |
//   |                           | 1008 kB | 1008 kB | 1008 kB |
//   |                           | 1012 kB | 1012 kB |       - |
//   |                           | 1016 kB | 1016 kB |       - |
//   |                           | 1020 kB | 1020 kB |       - |
//   |=========================================================|
//   | Huge                      |    1 MB |    1 MB |    1 MB |
//   |                           |    2 MB |    2 MB |    2 MB |
//   |                           |    3 MB |    3 MB |    3 MB |
//   |                           |     ... |     ... |     ... |
//   |=========================================================|
//
// Legend:
//   n:    Size class exists for this platform.
//   -:    This size class doesn't exist for this platform.
//   ...:  Size classes follow a pattern here.
//
// A different mechanism is used for each category:
//
//   Small : Each size class is segregated into its own set of runs.  Each run
//           maintains a bitmap of which regions are free/allocated.
//
//   Large : Each allocation is backed by a dedicated run.  Metadata are stored
//           in the associated arena chunk header maps.
//
//   Huge : Each allocation is backed by a dedicated contiguous set of chunks.
//          Metadata are stored in a separate red-black tree.
//
// *****************************************************************************

#include "mozmemory_wrap.h"
#include "mozjemalloc.h"
#include "mozjemalloc_types.h"
#include "mozjemalloc_profiling.h"

#include <bit>
#include <cstring>
#include <cerrno>
#include <chrono>
#ifdef XP_WIN
#  include <io.h>
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif
#ifdef XP_DARWIN
#  include <libkern/OSAtomic.h>
#  include <mach/mach_init.h>
#  include <mach/vm_map.h>
#endif

#include "mozilla/Atomics.h"
#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DoublyLinkedList.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/Likely.h"
#include "mozilla/Literals.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/RandomNum.h"
#include "mozilla/RefPtr.h"
// Note: MozTaggedAnonymousMmap() could call an LD_PRELOADed mmap
// instead of the one defined here; use only MozTagAnonymousMemory().
#include "mozilla/TaggedAnonymousMemory.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/XorShift128PlusRNG.h"
#include "mozilla/fallible.h"
#include "RadixTree.h"
#include "Arena.h"
#include "BaseAlloc.h"
#include "Chunk.h"
#include "Constants.h"
#include "Extent.h"
#include "Globals.h"
#include "Mutex.h"
#include "PHC.h"
#include "RedBlackTree.h"
#include "Utils.h"
#include "Zero.h"

#if defined(XP_WIN)
#  include "mozmemory_stall.h"
#endif

using namespace mozilla;

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
// MallocProfilerCallbacks is refcounted so that one thread cannot destroy it
// while another thread accesses it.  This means that clearing this value or
// otherwise dropping a reference to it must not be done while holding an
// arena's lock.
constinit static RefPtr<MallocProfilerCallbacks> sCallbacks;
#endif

// ***************************************************************************
// MALLOC_DECOMMIT and MALLOC_DOUBLE_PURGE are mutually exclusive.
#if defined(MALLOC_DECOMMIT) && defined(MALLOC_DOUBLE_PURGE)
#  error MALLOC_DECOMMIT and MALLOC_DOUBLE_PURGE are mutually exclusive.
#endif

// Set to true once the allocator has been initialized.
#if defined(_MSC_VER) && !defined(__clang__)
// MSVC may create a static initializer for an Atomic<bool>, which may actually
// run after `malloc_init` has been called once, which triggers multiple
// initializations.
// We work around the problem by not using an Atomic<bool> at all. There is a
// theoretical problem with using `malloc_initialized` non-atomically, but
// practically, this is only true if `malloc_init` is never called before
// threads are created.
static bool malloc_initialized;
#else
// We can rely on Relaxed here because this variable is only ever set when
// holding gInitLock. A thread that still sees it false while another sets it
// true will enter the same lock, synchronize with the former and check the
// flag again under the lock.
static Atomic<bool, MemoryOrdering::Relaxed> malloc_initialized;
#endif

// This lock must be held while bootstrapping us.
constinit StaticMutex gInitLock MOZ_UNANNOTATED;

// We cannot instantiate
// Atomic<std::chrono::time_point<std::chrono::steady_clock>>
// so we explicitly force timestamps to be uint64_t in ns.
uint64_t GetTimestampNS() {
  // On most if not all systems we care about the conversion to ns is a no-op,
  // so we prefer to keep the precision here for performance, but let's be
  // explicit about it.
  return std::chrono::floor<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now())
      .time_since_epoch()
      .count();
}

namespace mozilla {

template <>
struct GetDoublyLinkedListElement<arena_t> {
  static DoublyLinkedListElement<arena_t>& Get(arena_t* aThis) {
    return aThis->mPurgeListElem;
  }
  static const DoublyLinkedListElement<arena_t>& Get(const arena_t* aThis) {
    return aThis->mPurgeListElem;
  }
};

}  // namespace mozilla

struct ArenaTreeTrait {
  static RedBlackTreeNode<arena_t>& GetTreeNode(arena_t* aThis) {
    return aThis->mLink;
  }

  static inline Order Compare(arena_t* aNode, arena_t* aOther) {
    MOZ_ASSERT(aNode);
    MOZ_ASSERT(aOther);
    return CompareInt(aNode->mId, aOther->mId);
  }

  using SearchKey = arena_id_t;

  static inline Order Compare(SearchKey aKey, arena_t* aOther) {
    MOZ_ASSERT(aOther);
    return CompareInt(aKey, aOther->mId);
  }
};

// Bookkeeping for all the arenas used by the allocator.
// Arenas are separated in two categories:
// - "private" arenas, used through the moz_arena_* API
// - all the other arenas: the default arena, and thread-local arenas,
//   used by the standard API.
class ArenaCollection {
 public:
  constexpr ArenaCollection() = default;

  bool Init() MOZ_REQUIRES(gInitLock) MOZ_EXCLUDES(mLock) {
    arena_params_t params;
    // The main arena allows more dirty pages than the default for other arenas.
    params.mMaxDirty = opt_dirty_max;
    params.mLabel = "Default";
    mDefaultArena =
        mLock.Init() ? CreateArena(/* aIsPrivate = */ false, &params) : nullptr;
    mPurgeListLock.Init();
    return bool(mDefaultArena);
  }

  // The requested arena must exist.
  inline arena_t* GetById(arena_id_t aArenaId, bool aIsPrivate)
      MOZ_EXCLUDES(mLock);

  arena_t* CreateArena(bool aIsPrivate, arena_params_t* aParams)
      MOZ_EXCLUDES(mLock);

  void DisposeArena(arena_t* aArena) MOZ_EXCLUDES(mLock) {
    // This will not call MayPurge but only unlink the element in case.
    // It returns true if we successfully removed the item from the list,
    // meaning we have exclusive access to it and can delete it.
    bool delete_now = RemoveFromOutstandingPurges(aArena);

    {
      MutexAutoLock lock(mLock);

      MOZ_RELEASE_ASSERT(mPrivateArenas.Search(aArena->mId),
                         "Arena not in tree");
      mPrivateArenas.Remove(aArena);
      mNumOperationsDisposedArenas += aArena->Operations();
    }
    {
      MutexAutoLock lock(aArena->mLock);
      if (!aArena->mIsPurgePending) {
        // If no purge was pending then we have exclusive access to the
        // arena and must delete it.
        delete_now = true;
      } else if (!delete_now) {
        // The remaining possibility, when we failed to remove the arena from
        // the list (because a purging thread alredy did so) then that thread
        // will be the last thread holding the arena and is now responsible for
        // deleting it.
        aArena->mMustDeleteAfterPurge = true;

        // Not that it's not possible to have checked the list of pending purges
        // BEFORE the arena was added to the list because that would mean that
        // an operation on the arena (free or realloc) was running concurrently
        // with deletion, which would be a memory error and the assertions in
        // the destructor help check for that.
      }
    }

    if (delete_now) {
      delete aArena;
    }
  }

  void SetDefaultMaxDirtyPageModifier(int32_t aModifier) {
    {
      MutexAutoLock lock(mLock);
      bool decreased = aModifier < mDefaultMaxDirtyPageModifier;
      mDefaultMaxDirtyPageModifier = aModifier;
      for (auto* arena : iter()) {
        // We can only update max-dirty for main-thread-only arenas from the
        // main thread.
        if (!arena->IsMainThreadOnly() || IsOnMainThreadWeak()) {
          arena->UpdateMaxDirty();
          if (decreased) {
            purge_action_t action;
            {
              MaybeMutexAutoLock arena_lock(arena->mLock);
              action = arena->ShouldStartPurge();
            }
            arena->MayDoOrQueuePurge(action, "SetDefaultMaxDirtyPageModifier");
          }
        }
      }
    }
  }

  int32_t DefaultMaxDirtyPageModifier() { return mDefaultMaxDirtyPageModifier; }

  using Tree = RedBlackTree<arena_t, ArenaTreeTrait>;

  class Iterator {
   public:
    explicit Iterator(Tree* aTree, Tree* aSecondTree,
                      Tree* aThirdTree = nullptr)
        : mFirstIterator(aTree),
          mSecondTree(aSecondTree),
          mThirdTree(aThirdTree) {}

    class Item {
     private:
      Iterator& mIter;
      arena_t* mArena;

     public:
      Item(Iterator& aIter, arena_t* aArena) : mIter(aIter), mArena(aArena) {}

      bool operator!=(const Item& aOther) const {
        return mArena != aOther.mArena;
      }

      arena_t* operator*() const { return mArena; }

      const Item& operator++() {
        mArena = mIter.Next();
        return *this;
      }
    };

    Item begin() {
      // If the first tree is empty calling Next() would access memory out of
      // bounds, so advance to the next non-empty tree (or last empty tree).
      MaybeNextTree();
      return Item(*this, mFirstIterator.Current());
    }

    Item end() { return Item(*this, nullptr); }

   private:
    Tree::Iterator mFirstIterator;
    Tree* mSecondTree;
    Tree* mThirdTree;

    void MaybeNextTree() {
      while (!mFirstIterator.NotDone() && mSecondTree) {
        mFirstIterator = mSecondTree->iter();
        mSecondTree = mThirdTree;
        mThirdTree = nullptr;
      }
    }

    arena_t* Next() {
      arena_t* arena = mFirstIterator.Next();
      if (arena) {
        return arena;
      }

      // Advance to the next tree if we can, if there's no next tree, or the
      // next tree is empty then Current() will return nullptr.
      MaybeNextTree();
      return mFirstIterator.Current();
    }

    friend Item;
  };

  Iterator iter() MOZ_REQUIRES(mLock) {
    return Iterator(&mArenas, &mPrivateArenas);
  }

  inline arena_t* GetDefault() { return mDefaultArena; }

  // Guards the collection of arenas. Must not be acquired while holding
  // a single arena's lock or mPurgeListLock.
  Mutex mLock MOZ_UNANNOTATED;

  // Guards only the list of outstanding purge requests. Can be acquired
  // while holding gArenas.mLock, but must not be acquired or held while
  // holding or acquiring a single arena's lock.
  Mutex mPurgeListLock;

  // We're running on the main thread which is set by a call to SetMainThread().
  bool IsOnMainThread() const {
    return mMainThreadId.isSome() &&
           ThreadIdEqual(mMainThreadId.value(), GetThreadId());
  }

  // We're running on the main thread or SetMainThread() has never been called.
  bool IsOnMainThreadWeak() const {
    return mMainThreadId.isNothing() || IsOnMainThread();
  }

  // After a fork set the new thread ID in the child.
  // This is done as the first thing after a fork, before mLock even re-inits.
  void ResetMainThread() MOZ_EXCLUDES(mLock) {
    // The post fork handler in the child can run from a MacOS worker thread,
    // so we can't set our main thread to it here.  Instead we have to clear it.
    mMainThreadId = Nothing();
  }

  void SetMainThread() MOZ_EXCLUDES(mLock) {
    MutexAutoLock lock(mLock);
    MOZ_ASSERT(mMainThreadId.isNothing());
    mMainThreadId = Some(GetThreadId());
  }

  // This requires the lock to get a consistent count across all the active
  // + disposed arenas.
  uint64_t OperationsDisposedArenas() MOZ_REQUIRES(mLock) {
    return mNumOperationsDisposedArenas;
  }

  // Enable or disable the lazy purge.
  //
  // Returns the former state of enablement.
  // This is a global setting for all arenas. Changing it may cause an
  // immediate purge for all arenas.
  bool SetDeferredPurge(bool aEnable) {
    MOZ_ASSERT(IsOnMainThreadWeak());

    // We must hold the arena collection lock while updating the status
    // globally AND on each arena.
    {
      MutexAutoLock lock(mLock);
      if (mIsDeferredPurgeEnabled == aEnable) {
        // There's nothing more to do.
        return aEnable;
      }

      mIsDeferredPurgeEnabled = aEnable;
      for (auto* arena : iter()) {
        MaybeMutexAutoLock lock(arena->mLock);
        arena->mIsDeferredPurgeEnabled = aEnable;
      }
    }

    MayPurgeAll(PurgeIfThreshold, __func__);

    return aEnable;
  }

  bool IsDeferredPurgeEnabled() MOZ_REQUIRES(mLock) {
    return mIsDeferredPurgeEnabled;
  }

  // Set aside a new purge request for aArena.
  void AddToOutstandingPurges(arena_t* aArena) MOZ_EXCLUDES(mPurgeListLock);

  // Remove an unhandled purge request for aArena.  Returns true if the arena
  // was in the list.
  bool RemoveFromOutstandingPurges(arena_t* aArena)
      MOZ_EXCLUDES(mPurgeListLock);

  // Execute all outstanding purge requests, if any.
  void MayPurgeAll(PurgeCondition aCond, const char* aCaller);

  // Purge some dirty memory, based on purge requests, returns true if there are
  // more to process.
  //
  // Returns a may_purge_now_result_t with the following meaning:
  // Done:       Purge has completed for all arenas.
  // NeedsMore:  There may be some arenas that needs to be purged now.
  // WantsLater: There is at least one arena that might want a purge later,
  //             according to aReuseGraceMS.
  //
  // Parameters:
  // aPeekOnly:     If true, check only if there is work to do without doing it.
  // aReuseGraceMS: The time to wait with purge after a page reuse happened
  //                for an arena.
  // aKeepGoing:    If this returns false purging will cease.
  //
  // This could exit for 3 different reasons.
  //  - There are no more requests (it returns false)
  //  - There are more requests but aKeepGoing() returned false. (returns true)
  //  - One arena is completely purged, (returns true).
  //
  may_purge_now_result_t MayPurgeSteps(bool aPeekOnly, uint32_t aReuseGraceMS,
                                       purge_keep_going_t aKeepGoing,
                                       void* aClosure);

 private:
  const static arena_id_t MAIN_THREAD_ARENA_BIT = 0x1;

  static bool ArenaIdIsMainThreadOnly(arena_id_t aArenaId) {
    return aArenaId & MAIN_THREAD_ARENA_BIT;
  }

  arena_t* mDefaultArena = nullptr;
  arena_id_t mLastPublicArenaId MOZ_GUARDED_BY(mLock) = 0;

  // Accessing mArenas and mPrivateArenas can only be done while holding mLock.
  Tree mArenas MOZ_GUARDED_BY(mLock);
  Tree mPrivateArenas MOZ_GUARDED_BY(mLock);

  // Arena ids are pseudo-obfuscated/deobfuscated based on these values randomly
  // initialized on first use.
  arena_id_t mArenaIdKey = 0;
  int8_t mArenaIdRotation = 0;

  // Set only rarely and then propagated on the same thread to all arenas via
  // UpdateMaxDirty(). But also read in ExtraCommitPages on arbitrary threads.
  // TODO: Could ExtraCommitPages use arena_t::mMaxDirty instead ?
  Atomic<int32_t> mDefaultMaxDirtyPageModifier;
  // This is never changed except for forking, and it does not need mLock.
  Maybe<ThreadId> mMainThreadId;

  // The number of operations that happened in arenas that have since been
  // destroyed.
  uint64_t mNumOperationsDisposedArenas = 0;

  // Linked list of outstanding purges. This list has no particular order.
  // It is ok for an arena to be in this list even if mIsPurgePending is false,
  // it will just cause an extra round of a (most likely no-op) purge.
  // It is not ok to not be in this list but have mIsPurgePending set to true,
  // as this would prevent any future purges for this arena (except for during
  // MayPurgeStep or Purge).
  DoublyLinkedList<arena_t> mOutstandingPurges MOZ_GUARDED_BY(mPurgeListLock);

  // Flag if we should defer purge to later.
  bool mIsDeferredPurgeEnabled MOZ_GUARDED_BY(mLock) = false;
};

constinit static ArenaCollection gArenas;

// Protects huge allocation-related data structures.
static Mutex huge_mtx;

// Tree of chunks that are stand-alone huge allocations.
static RedBlackTree<extent_node_t, ExtentTreeTrait> huge
    MOZ_GUARDED_BY(huge_mtx);

// Huge allocation statistics.
static size_t huge_allocated MOZ_GUARDED_BY(huge_mtx);
static size_t huge_mapped MOZ_GUARDED_BY(huge_mtx);
static uint64_t huge_operations MOZ_GUARDED_BY(huge_mtx);

// ******
// Arenas.

// The arena associated with the current thread (per
// jemalloc_thread_local_arena) On OSX, __thread/thread_local circles back
// calling malloc to allocate storage on first access on each thread, which
// leads to an infinite loop, but pthread-based TLS somehow doesn't have this
// problem.
#if !defined(XP_DARWIN)
static MOZ_THREAD_LOCAL(arena_t*) thread_arena;
#else
static detail::ThreadLocal<arena_t*, detail::ThreadLocalKeyStorage>
    thread_arena;
#endif

// ***************************************************************************
// Begin forward declarations.

static void huge_dalloc(void* aPtr, arena_t* aArena);
static bool malloc_init_hard();

#ifndef XP_WIN
#  ifdef XP_DARWIN
#    define FORK_HOOK extern "C"
#  else
#    define FORK_HOOK static
#  endif
FORK_HOOK void _malloc_prefork(void);
FORK_HOOK void _malloc_postfork_parent(void);
FORK_HOOK void _malloc_postfork_child(void);
#  ifdef XP_DARWIN
FORK_HOOK void _malloc_postfork(void);
#  endif
#endif

// End forward declarations.
// ***************************************************************************

// FreeBSD's pthreads implementation calls malloc(3), so the malloc
// implementation has to take pains to avoid infinite recursion during
// initialization.
// Returns whether the allocator was successfully initialized.
static inline bool malloc_init() {
  if (!malloc_initialized) {
    return malloc_init_hard();
  }
  return true;
}

#ifdef ANDROID
// Android's pthread.h does not declare pthread_atfork() until SDK 21.
extern "C" MOZ_EXPORT int pthread_atfork(void (*)(void), void (*)(void),
                                         void (*)(void));
#endif

// ***************************************************************************
// Begin Utility functions/macros.

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
namespace mozilla {

void jemalloc_set_profiler_callbacks(
    RefPtr<MallocProfilerCallbacks>&& aCallbacks) {
  sCallbacks = aCallbacks;
}

}  // namespace mozilla
#endif

// End Utility functions/macros.
// ***************************************************************************
// Begin arena.

static inline arena_t* thread_local_arena(bool enabled) {
  arena_t* arena;

  if (enabled) {
    // The arena will essentially be leaked if this function is
    // called with `false`, but it doesn't matter at the moment.
    // because in practice nothing actually calls this function
    // with `false`, except maybe at shutdown.
    arena_params_t params;
    params.mLabel = "Thread local";
    arena = gArenas.CreateArena(/* aIsPrivate = */ false, &params);
  } else {
    arena = gArenas.GetDefault();
  }
  thread_arena.set(arena);
  return arena;
}

inline void MozJemalloc::jemalloc_thread_local_arena(bool aEnabled) {
  if (malloc_init()) {
    thread_local_arena(aEnabled);
  }
}

// Choose an arena based on a per-thread value.
static inline arena_t* choose_arena(size_t size) {
  arena_t* ret = nullptr;

  // We can only use TLS if this is a PIC library, since for the static
  // library version, libc's malloc is used by TLS allocation, which
  // introduces a bootstrapping issue.

  if (size > kMaxQuantumClass) {
    // Force the default arena for larger allocations.
    ret = gArenas.GetDefault();
  } else {
    // Check TLS to see if our thread has requested a pinned arena.
    ret = thread_arena.get();
    // If ret is non-null, it must not be in the first page.
    MOZ_DIAGNOSTIC_ASSERT_IF(ret, (size_t)ret >= gPageSize);
    if (!ret) {
      // Nothing in TLS. Pin this thread to the default arena.
      ret = thread_local_arena(false);
    }
  }

  MOZ_DIAGNOSTIC_ASSERT(ret);
  return ret;
}

inline uint8_t arena_t::FindFreeBitInMask(uint32_t aMask, uint32_t& aRng) {
  if (mPRNG != nullptr) {
    if (aRng == UINT_MAX) {
      aRng = mPRNG->next() % 32;
    }
    uint8_t bitIndex;
    // RotateRight asserts when provided bad input.
    aMask = aRng ? RotateRight(aMask, aRng)
                 : aMask;  // Rotate the mask a random number of slots
    bitIndex = static_cast<uint8_t>(std::countr_zero(aMask));
    return (bitIndex + aRng) % 32;
  }
  return static_cast<uint8_t>(std::countr_zero(aMask));
}

inline void* arena_t::ArenaRunRegAlloc(arena_run_t* aRun, arena_bin_t* aBin) {
  void* ret;
  unsigned i, mask, bit, regind;
  uint32_t rndPos = UINT_MAX;

  MOZ_DIAGNOSTIC_ASSERT(aRun->mMagic == ARENA_RUN_MAGIC);
  MOZ_ASSERT(aRun->mRegionsMinElement < aBin->mRunNumRegionsMask);

  // Move the first check outside the loop, so that aRun->mRegionsMinElement can
  // be updated unconditionally, without the possibility of updating it
  // multiple times.
  i = aRun->mRegionsMinElement;
  mask = aRun->mRegionsMask[i];
  if (mask != 0) {
    bit = FindFreeBitInMask(mask, rndPos);

    regind = ((i << (LOG2(sizeof(int)) + 3)) + bit);
    MOZ_ASSERT(regind < aBin->mRunNumRegions);
    ret = (void*)(((uintptr_t)aRun) + aBin->mRunFirstRegionOffset +
                  (aBin->mSizeClass * regind));

    // Clear bit.
    mask ^= (1U << bit);
    aRun->mRegionsMask[i] = mask;

    return ret;
  }

  for (i++; i < aBin->mRunNumRegionsMask; i++) {
    mask = aRun->mRegionsMask[i];
    if (mask != 0) {
      bit = FindFreeBitInMask(mask, rndPos);

      regind = ((i << (LOG2(sizeof(int)) + 3)) + bit);
      MOZ_ASSERT(regind < aBin->mRunNumRegions);
      ret = (void*)(((uintptr_t)aRun) + aBin->mRunFirstRegionOffset +
                    (aBin->mSizeClass * regind));

      // Clear bit.
      mask ^= (1U << bit);
      aRun->mRegionsMask[i] = mask;

      // Make a note that nothing before this element
      // contains a free region.
      aRun->mRegionsMinElement = i;  // Low payoff: + (mask == 0);

      return ret;
    }
  }
  // Not reached.
  MOZ_DIAGNOSTIC_ASSERT(0);
  return nullptr;
}

static inline void arena_run_reg_dalloc(arena_run_t* run, arena_bin_t* bin,
                                        void* ptr, size_t size) {
  uint32_t diff, regind;
  unsigned elm, bit;

  MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);

  // Avoid doing division with a variable divisor if possible.  Using
  // actual division here can reduce allocator throughput by over 20%!
  diff =
      (uint32_t)((uintptr_t)ptr - (uintptr_t)run - bin->mRunFirstRegionOffset);

  MOZ_ASSERT(diff <=
             (static_cast<unsigned>(bin->mRunSizePages) << gPageSize2Pow));
  regind = diff / bin->mSizeDivisor;

  MOZ_DIAGNOSTIC_ASSERT(diff == regind * size);
  MOZ_DIAGNOSTIC_ASSERT(regind < bin->mRunNumRegions);

  elm = regind >> (LOG2(sizeof(int)) + 3);
  if (elm < run->mRegionsMinElement) {
    run->mRegionsMinElement = elm;
  }
  bit = regind - (elm << (LOG2(sizeof(int)) + 3));
  MOZ_RELEASE_ASSERT((run->mRegionsMask[elm] & (1U << bit)) == 0,
                     "Double-free?");
  run->mRegionsMask[elm] |= (1U << bit);
}

#ifndef MALLOC_DECOMMIT
void arena_t::TouchMadvisedPage(arena_chunk_t* aChunk, size_t page) {
  // It should be MADVISED because it's part of the same real page.
  MOZ_ASSERT(aChunk->mPageMap[page].bits & CHUNK_MAP_MADVISED);

  // But it must not have the other flags.
  MOZ_ASSERT((aChunk->mPageMap[page].bits &
              (CHUNK_MAP_FRESH | CHUNK_MAP_DECOMMITTED | CHUNK_MAP_DIRTY)) ==
             0);

  // Clear MADVISED and set DIRTY.  It's dirty since it may still contain
  // data from a previous use.
  aChunk->mPageMap[page].bits =
      (aChunk->mPageMap[page].bits & ~CHUNK_MAP_MADVISED) | CHUNK_MAP_DIRTY;

  // Although this increases the number of dirty pages in the chunk, we
  // don't add it to the purge list because these pages can't be purged.
  // DallocRun will add it later.
  aChunk->mNumDirty++;
  mNumDirty++;
  mStats.committed++;
  mNumMAdvised--;
}
#endif

bool arena_t::SplitAndAllocRun(arena_run_t* aRun, size_t aSize, bool aLarge,
                               bool aZero) {
  arena_chunk_t* chunk = GetChunkForPtr(aRun);
  size_t old_ndirty = chunk->mNumDirty;
  size_t run_ind =
      (unsigned)((uintptr_t(aRun) - uintptr_t(chunk)) >> gPageSize2Pow);
  size_t total_pages =
      (chunk->mPageMap[run_ind].bits & ~gPageSizeMask) >> gPageSize2Pow;
  size_t need_pages = (aSize >> gPageSize2Pow);
  MOZ_ASSERT(need_pages > 0);
  MOZ_ASSERT(need_pages <= total_pages);
  size_t rem_pages = total_pages - need_pages;

  MOZ_ASSERT((chunk->mPageMap[run_ind].bits & CHUNK_MAP_BUSY) == 0);

#ifdef MALLOC_DECOMMIT
  size_t i = 0;
  while (i < need_pages) {
    MOZ_ASSERT((chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_BUSY) == 0);

    // Commit decommitted pages if necessary.  If a decommitted
    // page is encountered, commit all needed adjacent decommitted
    // pages in one operation, in order to reduce system call
    // overhead.
    if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DECOMMITTED) {
      // The start of the decommitted area is on a real page boundary.
      MOZ_ASSERT((run_ind + i) % gPagesPerRealPage == 0);

      // Advance i+j to just past the index of the last page
      // to commit.  Clear CHUNK_MAP_DECOMMITTED along the way.
      size_t j;
      for (j = 0; i + j < need_pages && (chunk->mPageMap[run_ind + i + j].bits &
                                         CHUNK_MAP_DECOMMITTED);
           j++) {
        // DECOMMITTED, MADVISED and FRESH are mutually exclusive.
        MOZ_ASSERT((chunk->mPageMap[run_ind + i + j].bits &
                    (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED)) == 0);
      }

      // Consider committing more pages to amortise calls to VirtualAlloc.
      // This only makes sense at the edge of our run hence the if condition
      // here.
      if (i + j == need_pages) {
        size_t extra_commit = ExtraCommitPages(j, rem_pages);
        extra_commit =
            PAGES_PER_REAL_PAGE_CEILING(run_ind + i + j + extra_commit) -
            run_ind - i - j;
        for (; i + j < need_pages + extra_commit &&
               (chunk->mPageMap[run_ind + i + j].bits &
                CHUNK_MAP_MADVISED_OR_DECOMMITTED);
             j++) {
          MOZ_ASSERT((chunk->mPageMap[run_ind + i + j].bits &
                      (CHUNK_MAP_FRESH | CHUNK_MAP_MADVISED)) == 0);
        }
      }
      // The end of the decommitted area is on a real page boundary.
      MOZ_ASSERT((run_ind + i + j) % gPagesPerRealPage == 0);

      if (!pages_commit(
              (void*)(uintptr_t(chunk) + ((run_ind + i) << gPageSize2Pow)),
              j << gPageSize2Pow)) {
        return false;
      }

      // pages_commit zeroes pages, so mark them as such if it succeeded.
      // That's checked further below to avoid manually zeroing the pages.
      for (size_t k = 0; k < j; k++) {
        chunk->mPageMap[run_ind + i + k].bits =
            (chunk->mPageMap[run_ind + i + k].bits & ~CHUNK_MAP_DECOMMITTED) |
            CHUNK_MAP_ZEROED | CHUNK_MAP_FRESH;
      }

      mNumFresh += j;
      i += j;
    } else {
      i++;
    }
  }
#endif

  // Keep track of trailing unused pages for later use.
  if (rem_pages > 0) {
    chunk->mPageMap[run_ind + need_pages].bits =
        (rem_pages << gPageSize2Pow) |
        (chunk->mPageMap[run_ind + need_pages].bits & gPageSizeMask);
    chunk->mPageMap[run_ind + total_pages - 1].bits =
        (rem_pages << gPageSize2Pow) |
        (chunk->mPageMap[run_ind + total_pages - 1].bits & gPageSizeMask);
    mRunsAvail.Insert(&chunk->mPageMap[run_ind + need_pages]);
  }

  if (chunk->mDirtyRunHint == run_ind) {
    chunk->mDirtyRunHint = run_ind + need_pages;
  }

#ifndef MALLOC_DECOMMIT
  bool first_page_was_madvised =
      chunk->mPageMap[run_ind].bits & CHUNK_MAP_MADVISED;
  bool last_page_was_madvised =
      chunk->mPageMap[run_ind + need_pages - 1].bits & CHUNK_MAP_MADVISED;
#endif
  for (size_t i = 0; i < need_pages; i++) {
    // Zero if necessary.
    if (aZero) {
      if ((chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_ZEROED) == 0) {
        memset((void*)(uintptr_t(chunk) + ((run_ind + i) << gPageSize2Pow)), 0,
               gPageSize);
        // CHUNK_MAP_ZEROED is cleared below.
      }
    }

    // Update dirty page accounting.
    if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DIRTY) {
      chunk->mNumDirty--;
      mNumDirty--;
      // CHUNK_MAP_DIRTY is cleared below.
    } else if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_MADVISED) {
      mStats.committed++;
      mNumMAdvised--;
    } else if (chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_FRESH) {
      mStats.committed++;
      mNumFresh--;
    }

    // This bit has already been cleared
    MOZ_ASSERT(!(chunk->mPageMap[run_ind + i].bits & CHUNK_MAP_DECOMMITTED));

    // Initialize the chunk map.  This clears the dirty, zeroed and madvised
    // bits, decommitted is cleared above.
    if (aLarge) {
      chunk->mPageMap[run_ind + i].bits = CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
    } else {
      chunk->mPageMap[run_ind + i].bits = size_t(aRun) | CHUNK_MAP_ALLOCATED;
    }
  }

#ifndef MALLOC_DECOMMIT
  // Remove the MADVISED bit from leading and trailing pages that are part of
  // the same real pages that we've touched.  This may cross into other free
  // runs, including busy runs.  This is safe because real page boundaries are
  // not crossed by either this code or the purging code.
  if (first_page_was_madvised) {
    for (size_t i = run_ind - 1;
         (i & (gPagesPerRealPage - 1)) != (gPagesPerRealPage - 1); i--) {
      // This loop will never go beyond into the chunk header or touch the guard
      // page because the guard page is always aligned.
      MOZ_ASSERT(gChunkHeaderNumPages <= i);

      TouchMadvisedPage(chunk, i);
    }
  }

  if (last_page_was_madvised) {
    for (size_t i = run_ind + need_pages; (i & (gPagesPerRealPage - 1)) != 0;
         i++) {
      // This loop will never go beyond the end of the chunk or touch the guard
      // page because the guard page is always aligned.
      MOZ_ASSERT(i < gChunkNumPages - gPagesPerRealPage);

      TouchMadvisedPage(chunk, i);
    }
  }
#endif

  // Set the run size only in the first element for large runs.  This is
  // primarily a debugging aid, since the lack of size info for trailing
  // pages only matters if the application tries to operate on an
  // interior pointer.
  if (aLarge) {
    chunk->mPageMap[run_ind].bits |= aSize;
  }

  if (chunk->mNumDirty == 0 && old_ndirty > 0 && !chunk->mIsPurging &&
      mChunksDirty.ElementProbablyInList(chunk)) {
    mChunksDirty.remove(chunk);
  }
  return true;
}

void arena_t::InitChunk(arena_chunk_t* aChunk, size_t aMinCommittedPages) {
  new (aChunk) arena_chunk_t(this);

  mStats.mapped += kChunkSize;

  // Setup the chunk's pages in two phases.  First we mark which pages are
  // committed & decommitted and perform the decommit.  Then we update the map
  // to create the runs.

  // Clear the bits for the real header pages.
  size_t i;
  for (i = 0; i < gChunkHeaderNumPages - gPagesPerRealPage; i++) {
    aChunk->mPageMap[i].bits = 0;
  }
  mStats.committed += gChunkHeaderNumPages - gPagesPerRealPage;

  // Decommit the last header page (=leading page) as a guard.
  MOZ_ASSERT(i % gPagesPerRealPage == 0);
  pages_decommit((void*)(uintptr_t(aChunk) + (i << gPageSize2Pow)),
                 gRealPageSize);
  for (; i < gChunkHeaderNumPages; i++) {
    aChunk->mPageMap[i].bits = CHUNK_MAP_DECOMMITTED;
  }

  // If MALLOC_DECOMMIT is enabled then commit only the pages we're about to
  // use.  Otherwise commit all of them.
#ifdef MALLOC_DECOMMIT
  // The number of usable pages in the chunk, in other words, the total number
  // of pages in the chunk, minus the number of pages in the chunk header
  // (including the guard page at the beginning of the chunk), and the number of
  // pages for the guard page at the end of the chunk.
  size_t chunk_usable_pages =
      gChunkNumPages - gChunkHeaderNumPages - gPagesPerRealPage;
  size_t n_fresh_pages = PAGES_PER_REAL_PAGE_CEILING(
      aMinCommittedPages +
      ExtraCommitPages(aMinCommittedPages,
                       chunk_usable_pages - aMinCommittedPages));
#else
  size_t n_fresh_pages =
      gChunkNumPages - gPagesPerRealPage - gChunkHeaderNumPages;
#endif

  // The committed pages are marked as Fresh.  Our caller, SplitAndAllocRun will
  // update this when it uses them.
  for (size_t j = 0; j < n_fresh_pages; j++) {
    aChunk->mPageMap[i + j].bits = CHUNK_MAP_ZEROED | CHUNK_MAP_FRESH;
  }
  i += n_fresh_pages;
  mNumFresh += n_fresh_pages;

#ifndef MALLOC_DECOMMIT
  // If MALLOC_DECOMMIT isn't defined then all the pages are fresh and setup in
  // the loop above.
  MOZ_ASSERT(i == gChunkNumPages - gPagesPerRealPage);
#endif

  // If MALLOC_DECOMMIT is defined, then this will decommit the remainder of the
  // chunk plus the last page which is a guard page, if it is not defined it
  // will only decommit the guard page.
  MOZ_ASSERT(i % gPagesPerRealPage == 0);
  pages_decommit((void*)(uintptr_t(aChunk) + (i << gPageSize2Pow)),
                 (gChunkNumPages - i) << gPageSize2Pow);
  for (; i < gChunkNumPages; i++) {
    aChunk->mPageMap[i].bits = CHUNK_MAP_DECOMMITTED;
  }

  // aMinCommittedPages will create a valid run.
  MOZ_ASSERT(aMinCommittedPages > 0);
  MOZ_ASSERT(aMinCommittedPages <=
             gChunkNumPages - gChunkHeaderNumPages - gPagesPerRealPage);

  // Create the run.
  aChunk->mPageMap[gChunkHeaderNumPages].bits |= gMaxLargeClass;
  aChunk->mPageMap[gChunkNumPages - gPagesPerRealPage - 1].bits |=
      gMaxLargeClass;
}

void arena_t::RemoveChunk(arena_chunk_t* aChunk) {
  MOZ_ASSERT(aChunk->mArena == this);

  // The chunk cannot be in either the spare or dirty chunk lists.
  MOZ_ASSERT(!mSpares.ElementProbablyInList(aChunk));
  MOZ_ASSERT(!mChunksDirty.ElementProbablyInList(aChunk));

  // RemoveChunk is only called with the spare chunk or after purging, so
  // the chunk will never be purging.
  MOZ_ASSERT(!aChunk->mIsPurging);

  mNumDirty -= aChunk->mNumDirty;
  mStats.committed -= aChunk->mNumDirty;

  // Count the number of madvised/fresh pages and update the stats.
  size_t madvised = 0;
  size_t fresh = 0;
  for (size_t i = gChunkHeaderNumPages; i < gChunkNumPages - gPagesPerRealPage;
       i++) {
    MOZ_ASSERT((aChunk->mPageMap[i].bits & CHUNK_MAP_ALLOCATED) == 0);
    MOZ_ASSERT((aChunk->mPageMap[i].bits & CHUNK_MAP_BUSY) == 0);

    if (aChunk->mPageMap[i].bits & CHUNK_MAP_MADVISED) {
      madvised++;
    } else if (aChunk->mPageMap[i].bits & CHUNK_MAP_FRESH) {
      fresh++;
    }
  }

  mNumMAdvised -= madvised;
  mNumFresh -= fresh;

#ifdef MALLOC_DOUBLE_PURGE
  if (mChunksMAdvised.ElementProbablyInList(aChunk)) {
    mChunksMAdvised.remove(aChunk);
  }
#endif

  mStats.mapped -= kChunkSize;
  mStats.committed -= gChunkHeaderNumPages - gPagesPerRealPage;
}

void arena_t::DemoteChunkToSpare(arena_chunk_t* aChunk) {
  // Spare chunks can't exist on the dirty chunks list because they use the
  // same element field, so remove it from dirty chunks before adding it to
  // spare chunks.  If called after Purge then the chunk won't be on the
  // dirty chunks list, the caller must clear aChunk->mIsPurging after this
  // call.
  if (aChunk->mNumDirty && !aChunk->mIsPurging) {
    MOZ_ASSERT(mChunksDirty.ElementProbablyInList(aChunk));
    mChunksDirty.remove(aChunk);
  }
  MOZ_ASSERT(!mChunksDirty.ElementProbablyInList(aChunk));
  MOZ_ASSERT(!mSpares.ElementProbablyInList(aChunk));
  mSpares.pushFront(aChunk);
}

arena_run_t* arena_t::AllocRun(size_t aSize, bool aLarge, bool aZero) {
  MOZ_ASSERT(aSize <= gMaxLargeClass);
  MOZ_ASSERT((aSize & gPageSizeMask) == 0);

  // Search the arena's chunks for the best fit.
  arena_chunk_map_t* mapelm = mRunsAvail.SearchOrNext(aSize);
  if (mapelm) {
    arena_chunk_t* chunk = GetChunkForPtr(mapelm);
    size_t pageind = (uintptr_t(mapelm) - uintptr_t(chunk->mPageMap)) /
                     sizeof(arena_chunk_map_t);

    MOZ_ASSERT((chunk->mPageMap[pageind].bits & CHUNK_MAP_BUSY) == 0);
    arena_run_t* run =
        (arena_run_t*)(uintptr_t(chunk) + (pageind << gPageSize2Pow));
    mRunsAvail.Remove(mapelm);

    // Update page map.
    if (!SplitAndAllocRun(run, aSize, aLarge, aZero)) {
      mRunsAvail.Insert(mapelm);
      return nullptr;
    }
    return run;
  } else if (!mSpares.isEmpty()) {
    arena_chunk_t* chunk = mSpares.popFront();
    MOZ_ASSERT(!chunk->mIsPurging);

    arena_run_t* run = (arena_run_t*)(uintptr_t(chunk) +
                                      (gChunkHeaderNumPages << gPageSize2Pow));
    MOZ_ASSERT((chunk->mPageMap[gChunkHeaderNumPages].bits & CHUNK_MAP_BUSY) ==
               0);

    // If the chunk is dirty it must be in mChunksDirty for
    // SplitAndAllocRun() to work correctly, so this runs before
    // SplitAndAllocRun(), even if SplitAndAllocRun() removes the chunk drom
    // mChunksDirty anyway..
    if (chunk->mNumDirty) {
      MOZ_ASSERT(!mChunksDirty.ElementProbablyInList(chunk));
      mChunksDirty.pushFront(chunk);
    }

    if (!SplitAndAllocRun(run, aSize, aLarge, aZero)) {
      // On failure rather than put the chunk back in mSpares, release it
      // altogether.  Since we're obviously running low on memory anyway.
      RemoveChunk(chunk);
      arena_chunk_dealloc(mChunkAllocator, (void*)chunk, kChunkSize);
      return nullptr;
    }

    return run;
  } else {
    // No usable runs.  Create a new chunk from which to allocate
    // the run.
    arena_chunk_t* chunk = (arena_chunk_t*)arena_chunk_alloc(
        mChunkAllocator, kChunkSize, kChunkSize);
    if (!chunk) {
      return nullptr;
    }

    InitChunk(chunk, aSize >> gPageSize2Pow);
    arena_run_t* run = (arena_run_t*)(uintptr_t(chunk) +
                                      (gChunkHeaderNumPages << gPageSize2Pow));

    bool result = SplitAndAllocRun(run, aSize, aLarge, aZero);
    MOZ_RELEASE_ASSERT(result,
                       "SplitAndAllocRun() won't fail because InitChunk should "
                       "have left enough pages committed");
    return run;
  }
}

void arena_t::UpdateMaxDirty() {
  MaybeMutexAutoLock lock(mLock);
  int32_t modifier = gArenas.DefaultMaxDirtyPageModifier();
  if (modifier) {
    int32_t arenaOverride =
        modifier > 0 ? mMaxDirtyIncreaseOverride : mMaxDirtyDecreaseOverride;
    if (arenaOverride) {
      modifier = arenaOverride;
    }
  }

  mMaxDirty =
      modifier >= 0 ? mMaxDirtyBase << modifier : mMaxDirtyBase >> -modifier;
}

#ifdef MALLOC_DECOMMIT

size_t arena_t::ExtraCommitPages(size_t aReqPages, size_t aRemainingPages) {
  const int32_t modifier = gArenas.DefaultMaxDirtyPageModifier();
  if (modifier < 0) {
    return 0;
  }

  // The maximum size of the page cache
  const size_t max_page_cache = mMaxDirty;

  // The current size of the page cache, note that we use mNumFresh +
  // mNumMAdvised here but Purge() does not.
  const size_t page_cache = mNumDirty + mNumFresh + mNumMAdvised;

  if (page_cache > max_page_cache) {
    // We're already exceeding our dirty page count even though we're trying
    // to allocate.  This can happen due to fragmentation.  Don't commit
    // excess memory since we're probably here due to a larger allocation and
    // small amounts of memory are certainly available in the page cache.
    return 0;
  }
  if (modifier > 0) {
    // If modifier is > 0 then we want to keep all the pages we can, but don't
    // exceed the size of the page cache.  The subtraction cannot underflow
    // because of the condition above.
    return std::min(aRemainingPages, max_page_cache - page_cache);
  }

  // The rest is arbitrary and involves a some assumptions.  I've broken it down
  // into simple expressions to document them more clearly.

  // Assumption 1: a quarter of mMaxDirty is a sensible "minimum
  // target" for the dirty page cache.  Likewise 3 quarters is a sensible
  // "maximum target".  Note that for the maximum we avoid using the whole page
  // cache now so that a free that follows this allocation doesn't immeidatly
  // call Purge (churning memory).
  const size_t min = max_page_cache / 4;
  const size_t max = 3 * max_page_cache / 4;

  // Assumption 2: Committing 32 pages at a time is sufficient to amortise
  // VirtualAlloc costs.
  size_t amortisation_threshold = 32;

  // extra_pages is the number of additional pages needed to meet
  // amortisation_threshold.
  size_t extra_pages = aReqPages < amortisation_threshold
                           ? amortisation_threshold - aReqPages
                           : 0;

  // If committing extra_pages isn't enough to hit the minimum target then
  // increase it.
  if (page_cache + extra_pages < min) {
    extra_pages = min - page_cache;
  } else if (page_cache + extra_pages > max) {
    // If committing extra_pages would exceed our maximum target then it may
    // still be useful to allocate extra pages.  One of the reasons this can
    // happen could be fragmentation of the cache,

    // Therefore reduce the amortisation threshold so that we might allocate
    // some extra pages but avoid exceeding the dirty page cache.
    amortisation_threshold /= 2;
    extra_pages = std::min(aReqPages < amortisation_threshold
                               ? amortisation_threshold - aReqPages
                               : 0,
                           max_page_cache - page_cache);
  }

  // Cap extra_pages to aRemainingPages and adjust aRemainingPages.  We will
  // commit at least this many extra pages.
  extra_pages = std::min(extra_pages, aRemainingPages);

  // Finally if commiting a small number of additional pages now can prevent
  // a small commit later then try to commit a little more now, provided we
  // don't exceed max_page_cache.
  if ((aRemainingPages - extra_pages) < amortisation_threshold / 2 &&
      (page_cache + aRemainingPages) < max_page_cache) {
    return aRemainingPages;
  }

  return extra_pages;
}
#endif

ArenaPurgeResult arena_t::Purge(PurgeCondition aCond, PurgeStats& aStats,
                                purge_keep_going_t aKeepGoing, void* aClosure)
    MOZ_EXCLUDES(mLock) {
  mLock.Lock();

  if (mMustDeleteAfterPurge) {
    mIsPurgePending = false;
    mLock.Unlock();
    return Dying;
  }

  if (!ShouldContinuePurge(aCond)) {
    mIsPurgePending = false;
    mLock.Unlock();
    return ReachedThresholdOrBusy;
  }

  arena_chunk_t* chunk = PurgeGetSpareChunk(aStats);
  if (chunk) {
    // Release the memory outside of the lock.
    mLock.Unlock();
    arena_chunk_dealloc(mChunkAllocator, (void*)chunk, kChunkSize);
    aStats.system_calls++;
    return NotDone;
  }

  chunk = PurgeGetDirtyChunk(aCond, aStats);
  mLock.Unlock();
  if (chunk) {
    return PurgeDirtyPages(chunk, aCond, aStats, aKeepGoing, aClosure);
  }

  return ReachedThresholdOrBusy;
}

arena_chunk_t* arena_t::PurgeGetSpareChunk(PurgeStats& aStats)
    MOZ_REQUIRES(mLock) {
  if (mSpares.isEmpty()) {
    return nullptr;
  }

  // Start flushing our cache of spare chunks.
  arena_chunk_t* chunk = mSpares.popBack();

  // This is not possible. Not because another thread won't be purging
  // memory because that is possible (but rare).  But because it'd need
  // to start purging, then become empty before the purge finishes,
  // which cannot happen because busy runs won't be merged and it won't
  // be detected as empty until the end of the purge.
  MOZ_ASSERT(!chunk->mIsPurging);

  aStats.chunks++;
  aStats.pages_dirty += chunk->mNumDirty;
  aStats.pages_total += (kChunkSize >> gPageSize2Pow) - gPagesPerRealPage * 2;
  RemoveChunk(chunk);

  return chunk;
}

arena_chunk_t* arena_t::PurgeGetDirtyChunk(PurgeCondition aCond,
                                           PurgeStats& aStats)
    MOZ_REQUIRES(mLock) {
#ifdef MOZ_DEBUG
  size_t ndirty = 0;
  for (auto& chunk : mChunksDirty) {
    ndirty += chunk.mNumDirty;
  }

  // Spare chunks don't appear in mChunksDirty.
  for (auto& chunk : mSpares) {
    ndirty += chunk.mNumDirty;
  }

  // Not all dirty chunks are in mChunksDirty as some may not have enough
  // dirty pages for purging or might currently be being purged.
  MOZ_ASSERT(ndirty <= mNumDirty);
#endif

  // Take a single chunk and attempt to purge some of its dirty pages.  The
  // loop below will purge memory from the chunk until either:
  //  * The dirty page count for the arena hits its target,
  //  * Another thread attempts to delete this chunk, or
  //  * The chunk has no more dirty pages.
  // In any of these cases the loop will break and Purge() will return,
  // which means it may return before the arena meets its dirty page count
  // target, the return value is used by the caller to call Purge() again
  // where it will take the next chunk with dirty pages.
  if (mChunksDirty.isEmpty()) {
    // We have to clear the flag to preserve the invariant that if Purge()
    // returns anything other than NotDone then the flag is clear. If
    // there's more purging work to do in other chunks then either other
    // calls to Purge() (in other threads) will handle it or we rely on
    // ShouldStartPurge() returning true at some point in the future.
    mIsPurgePending = false;

    // There are chunks with dirty pages (because mNumDirty > 0 above) but
    // they're not in mChunksDirty, they might not have enough dirty pages.
    // Or maybe they're busy being purged by other threads.
    return nullptr;
  }

  arena_chunk_t* chunk = mChunksDirty.popFront();
  MOZ_ASSERT(chunk->mNumDirty > 0);
  MOZ_ASSERT(!chunk->IsEmpty());

  // Mark the chunk as busy so it won't be deleted and remove it from
  // mChunksDirty so we're the only thread purging it.
  MOZ_ASSERT(!chunk->mIsPurging);
  chunk->mIsPurging = true;
  aStats.chunks++;

  return chunk;
}

ArenaPurgeResult arena_t::PurgeDirtyPages(arena_chunk_t* aChunk,
                                          PurgeCondition aCond,
                                          PurgeStats& aStats,
                                          purge_keep_going_t aKeepGoing,
                                          void* aClosure) MOZ_EXCLUDES(mLock) {
  // True if we should continue purging memory from this arena.
  bool continue_purge_arena = true;

  // True if we should continue purging memory in this chunk.
  bool continue_purge_chunk = true;

  // True if at least one Purge operation has occured and therefore we need to
  // call FinishPurgingInChunk() before returning.
  bool purged_once = false;

  // False if aKeepGoing prevents us from finishing this chunk in one go.
  bool keep_going = true;

  while (continue_purge_chunk && continue_purge_arena && keep_going) {
    // This structure is used to communicate between the two PurgePhase
    // functions.
    PurgeInfo purge_info(*this, aChunk, aStats);

    {
      // Phase 1: Find pages that need purging.
      MaybeMutexAutoLock lock(purge_info.mArena.mLock);
      MOZ_ASSERT(aChunk->mIsPurging);

      if (purge_info.mArena.mMustDeleteAfterPurge) {
        aChunk->mIsPurging = false;
        purge_info.mArena.mIsPurgePending = false;
        return Dying;
      }

      continue_purge_chunk = purge_info.FindDirtyPages(purged_once);
      continue_purge_arena = purge_info.mArena.ShouldContinuePurge(aCond);

      // The code below will exit returning ReachedThresholdOrBusy if these are
      // both false, so clear mIsPurgePending while we still hold the
      // lock.
      if (!continue_purge_chunk && !continue_purge_arena) {
        purge_info.mArena.mIsPurgePending = false;
      }
    }
    if (!continue_purge_chunk) {
      // There's nothing else to do here, our caller may execute Purge() again
      // if continue_purge_arena is true.
      return continue_purge_arena ? NotDone : ReachedThresholdOrBusy;
    }
    // Note that even if continue_purge_arena is false, then the purge may still
    // continue (as long as continue_purge_chunk is true). It must because the
    // pages have already been marked in FindDirtyPages(), then it will exit
    // after phase 2.

#ifdef MALLOC_DECOMMIT
    pages_decommit(purge_info.DirtyPtr(), purge_info.DirtyLenBytes());
#else
#  ifdef XP_SOLARIS
    posix_madvise(purge_info.DirtyPtr(), purge_info.DirtyLenBytes(), MADV_FREE);
#  else
    madvise(purge_info.DirtyPtr(), purge_info.DirtyLenBytes(), MADV_FREE);
#  endif
#endif

    // Check budget outside any lock, after the madvise/decommit which is the
    // potentially expensive operation.
    keep_going = aKeepGoing ? aKeepGoing(aClosure) : true;

    bool arena_is_dying;
    {
      // Phase 2: Mark the pages with their final state (madvised or
      // decommitted) and fix up any other bookkeeping.
      MaybeMutexAutoLock lock(purge_info.mArena.mLock);
      MOZ_ASSERT(aChunk->mIsPurging);

      // We can't early exit if the arena is dying, we have to finish the purge
      // (which restores the state so the destructor will check it) and maybe
      // release the old spare arena.
      arena_is_dying = purge_info.mArena.mMustDeleteAfterPurge;

      continue_purge_chunk = purge_info.UpdatePagesAndCounts();
      continue_purge_arena = purge_info.mArena.ShouldContinuePurge(aCond);

      if (!continue_purge_chunk || !continue_purge_arena || !keep_going) {
        // We're going to stop (or pause) purging here so update the chunk's
        // bookkeeping to reflect what we did (so far).
        purge_info.FinishPurgingInChunk(true, continue_purge_chunk);
        // Only clear mIsPurgePending when truly done. Otherwise the arena
        // stays marked pending so it gets re-queued for the next purge pass.
        if (!continue_purge_arena) {
          purge_info.mArena.mIsPurgePending = false;
        }
      }
    }  // MaybeMutexAutoLock

    if (arena_is_dying) {
      return Dying;
    }
    purged_once = true;
  }

  return continue_purge_arena ? NotDone : ReachedThresholdOrBusy;
}

ArenaPurgeResult arena_t::PurgeLoop(PurgeCondition aCond, const char* aCaller,
                                    uint32_t aReuseGraceMS,
                                    purge_keep_going_t aKeepGoing,
                                    void* aClosure) {
  PurgeStats purge_stats(mId, mLabel, aCaller);

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
  // We hold our own reference to callbacks for the duration of PurgeLoop to
  // make sure it's not released during purging.
  RefPtr<MallocProfilerCallbacks> callbacks = sCallbacks;
  TimeStamp start;
  if (callbacks) {
    start = TimeStamp::Now();
  }
#endif

  uint64_t reuseGraceNS = (uint64_t)aReuseGraceMS * 1000 * 1000;
  uint64_t now;
  ArenaPurgeResult pr;
  do {
    pr = Purge(aCond, purge_stats, aKeepGoing, aClosure);
    now = aReuseGraceMS ? GetTimestampNS() : 0;
  } while (pr == NotDone &&
           (!aReuseGraceMS || (now - mLastPageReuseNS >= reuseGraceNS)) &&
           (!aKeepGoing || aKeepGoing(aClosure)));

#ifdef MOZJEMALLOC_PROFILING_CALLBACKS
  if (callbacks) {
    TimeStamp end = TimeStamp::Now();
    // We can't hold an arena lock while committing profiler markers.
    callbacks->OnPurge(start, end, purge_stats, pr);
  }
#endif

  return pr;
}

bool arena_t::PurgeInfo::FindDirtyPages(bool aPurgedOnce) {
  // It's possible that the previously dirty pages have now been
  // allocated or the chunk is dying.
  if (mChunk->mNumDirty == 0) {
    // Add the chunk to the mChunksMAdvised list if it's had at least one
    // madvise.
    FinishPurgingInChunk(aPurgedOnce, false);
    return false;
  }

  // This will locate a span of dirty pages within a single run (unallocated
  // runs never have unallocated neighbours).  The span of dirty pages may have
  // "holes" of clean never-allocated pages.  We don't know for sure the
  // trade-offs of purging those clean pages.  On one hand:
  //  * This reduces the number of system calls needed
  //  * This may cause less fragmentation in the kernel's structures, but not
  //    the CPU's page tables.
  //  * It's likely that the pages aren't committed by the OS anyway.
  // On the other hand:
  //  * Now accessing those pages will require either pages_commit() or a page
  //    fault to ensure they're available.
  do {
    if (!ScanForFirstDirtyPage()) {
      FinishPurgingInChunk(aPurgedOnce, false);
      return false;
    }
  } while (!ScanForLastDirtyPage());

  MOZ_ASSERT(mFreeRunInd >= gChunkHeaderNumPages);
  MOZ_ASSERT(mFreeRunInd <= mDirtyInd);
  MOZ_ASSERT(mFreeRunLen > 0);
  MOZ_ASSERT(mDirtyInd != 0);
  MOZ_ASSERT(mDirtyLen != 0);
  MOZ_ASSERT(mDirtyLen <= mFreeRunLen);
  MOZ_ASSERT(mDirtyInd + mDirtyLen <= mFreeRunInd + mFreeRunLen);
  MOZ_ASSERT(mDirtyInd % gPagesPerRealPage == 0);
  MOZ_ASSERT(mDirtyLen % gPagesPerRealPage == 0);

  // Count the number of dirty pages and clear their bits.
  mDirtyNPages = 0;
  for (size_t i = 0; i < mDirtyLen; i++) {
    size_t& bits = mChunk->mPageMap[mDirtyInd + i].bits;
    if (bits & CHUNK_MAP_DIRTY) {
      mDirtyNPages++;
      bits ^= CHUNK_MAP_DIRTY;
    }
  }

  MOZ_ASSERT(mDirtyNPages > 0);
  MOZ_ASSERT(mDirtyNPages <= mChunk->mNumDirty);
  MOZ_ASSERT(mDirtyNPages <= mDirtyLen);

  mChunk->mNumDirty -= mDirtyNPages;
  mArena.mNumDirty -= mDirtyNPages;

  // Mark the run as busy so that another thread freeing memory won't try to
  // coalesce it.
  mChunk->mPageMap[mFreeRunInd].bits |= CHUNK_MAP_BUSY;
  mChunk->mPageMap[FreeRunLastInd()].bits |= CHUNK_MAP_BUSY;

  // Before we unlock ensure that no other thread can allocate from these
  // pages.  Only chunks that are not spare, and therefore not-empty will be in
  // the mRunsAvail list.
  if (!mChunk->IsEmpty()) {
    mArena.mRunsAvail.Remove(&mChunk->mPageMap[mFreeRunInd]);
  }
  return true;
}

// Look for the first dirty page and the run it belongs to.
bool arena_t::PurgeInfo::ScanForFirstDirtyPage() {
  // Scan in two nested loops.  The outer loop iterates over runs, and the inner
  // loop iterates over pages within unallocated runs.
  size_t run_pages;
  for (size_t run_idx = mChunk->mDirtyRunHint;
       run_idx < gChunkNumPages - gPagesPerRealPage; run_idx += run_pages) {
    size_t run_bits = mChunk->mPageMap[run_idx].bits;
    // We must not find any busy pages because this chunk shouldn't be in
    // the dirty list.
    MOZ_ASSERT((run_bits & CHUNK_MAP_BUSY) == 0);

    // Determine the run's size, this is used in the loop iteration to move to
    // the next run.
    if (run_bits & CHUNK_MAP_LARGE || !(run_bits & CHUNK_MAP_ALLOCATED)) {
      size_t size = run_bits & ~gPageSizeMask;
      run_pages = size >> gPageSize2Pow;
    } else {
      arena_run_t* run =
          reinterpret_cast<arena_run_t*>(run_bits & ~gPageSizeMask);
      MOZ_DIAGNOSTIC_ASSERT(run == reinterpret_cast<arena_run_t*>(
                                       reinterpret_cast<uintptr_t>(mChunk) +
                                       (run_idx << gPageSize2Pow)));
      MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
      run_pages = run->mBin->mRunSizePages;
    }
    MOZ_ASSERT(run_pages > 0);
    MOZ_ASSERT(run_idx + run_pages <= gChunkNumPages);

    if (run_bits & CHUNK_MAP_ALLOCATED) {
      // Allocated runs won't contain dirty pages.
      continue;
    }

    mFreeRunInd = run_idx;
    mFreeRunLen = run_pages;
    mDirtyInd = 0;
    // Scan for dirty pages.
    for (size_t page_idx = run_idx; page_idx < run_idx + run_pages;
         page_idx++) {
      size_t& page_bits = mChunk->mPageMap[page_idx].bits;
      // We must not find any busy pages because this chunk shouldn't be in
      // the dirty list.
      MOZ_ASSERT((page_bits & CHUNK_MAP_BUSY) == 0);

      // gPagesPerRealPage is a power of two, use a bitmask to check if page_idx
      // is a multiple.
      if ((page_idx & (gPagesPerRealPage - 1)) == 0) {
        // A system call can be aligned here.
        mDirtyInd = page_idx;
      }

      if (page_bits & CHUNK_MAP_DIRTY) {
        MOZ_ASSERT((page_bits & CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED) == 0);
        MOZ_ASSERT(mChunk->mDirtyRunHint <= run_idx);
        mChunk->mDirtyRunHint = run_idx;

        if (mDirtyInd) {
          return true;
        }

        // This dirty page occurs before a page we can align on,
        // so it can't be purged.
        mPurgeStats.pages_unpurgable++;
      }
    }
  }

  return false;
}

bool arena_t::PurgeInfo::ScanForLastDirtyPage() {
  mDirtyLen = 0;
  for (size_t i = FreeRunLastInd(); i >= mDirtyInd; i--) {
    size_t& bits = mChunk->mPageMap[i].bits;
    // We must not find any busy pages because this chunk shouldn't be in the
    // dirty list.
    MOZ_ASSERT(!(bits & CHUNK_MAP_BUSY));

    // gPagesPerRealPage is a power of two, use a bitmask to check if page_idx
    // is a multiple minus one.
    if ((i & (gPagesPerRealPage - 1)) == gPagesPerRealPage - 1) {
      // A system call can be aligned here.
      mDirtyLen = i - mDirtyInd + 1;
    }

    if (bits & CHUNK_MAP_DIRTY) {
      if (mDirtyLen) {
        return true;
      }

      // This dirty page occurs after a page we can align on,
      // so it can't be purged.
      mPurgeStats.pages_unpurgable++;
    }
  }

  // Advance the dirty page hint so that the next scan will make progress.
  mChunk->mDirtyRunHint = FreeRunLastInd() + 1;
  return false;
}

bool arena_t::PurgeInfo::UpdatePagesAndCounts() {
  size_t num_madvised = 0;
  size_t num_decommitted = 0;
  size_t num_fresh = 0;

  for (size_t i = 0; i < mDirtyLen; i++) {
    size_t& bits = mChunk->mPageMap[mDirtyInd + i].bits;

    // The page must not have the dirty bit set.
    MOZ_ASSERT((bits & CHUNK_MAP_DIRTY) == 0);

#ifdef MALLOC_DECOMMIT
    if (bits & CHUNK_MAP_DECOMMITTED) {
      num_decommitted++;
    }
#else
    if (bits & CHUNK_MAP_MADVISED) {
      num_madvised++;
    }
#endif
    else if (bits & CHUNK_MAP_FRESH) {
      num_fresh++;
    }

    // Clear these page status bits.
    bits &= ~CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED;

    // Set the free_operation bit.
#ifdef MALLOC_DECOMMIT
    bits |= CHUNK_MAP_DECOMMITTED;
#else
    bits |= CHUNK_MAP_MADVISED;
#endif
  }

  // Remove the CHUNK_MAP_BUSY marks from the run.
#ifdef MOZ_DEBUG
  MOZ_ASSERT(mChunk->mPageMap[mFreeRunInd].bits & CHUNK_MAP_BUSY);
  MOZ_ASSERT(mChunk->mPageMap[FreeRunLastInd()].bits & CHUNK_MAP_BUSY);
#endif
  mChunk->mPageMap[mFreeRunInd].bits &= ~CHUNK_MAP_BUSY;
  mChunk->mPageMap[FreeRunLastInd()].bits &= ~CHUNK_MAP_BUSY;

#ifndef MALLOC_DECOMMIT
  mArena.mNumMAdvised += mDirtyLen - num_madvised;
#endif

  mArena.mNumFresh -= num_fresh;
  mArena.mStats.committed -=
      mDirtyLen - num_madvised - num_decommitted - num_fresh;
  mPurgeStats.pages_dirty += mDirtyNPages;
  mPurgeStats.pages_total += mDirtyLen;
  mPurgeStats.system_calls++;

  // Note that this code can't update the dirty run hint.  There may be other
  // dirty pages within the same run.

  mFreeRunInd =
      mArena.TryCoalesce(mChunk, mFreeRunInd, mFreeRunLen, FreeRunLenBytes());

  if (!mChunk->IsEmpty()) {
    mArena.mRunsAvail.Insert(&mChunk->mPageMap[mFreeRunInd]);
  }

  return mChunk->mNumDirty != 0;
}

void arena_t::PurgeInfo::FinishPurgingInChunk(bool aAddToMAdvised,
                                              bool aAddToDirty) {
  // If there's no more purge activity for this chunk then finish up while
  // we still have the lock.
  MOZ_ASSERT(mChunk->mIsPurging);

  if (mChunk->IsEmpty()) {
    // This now-empty chunk will become the spare chunk and the spare
    // chunk will be returned for deletion.  Note that mChunk->mIsPurging is
    // still true for this call, that tells DemoteChunkToSpare not to remove
    // the chunk from mChunksDirty.
    mArena.DemoteChunkToSpare(mChunk);
  } else if (mChunk->mNumDirty != 0 && aAddToDirty) {
    // Put the semi-processed chunk on the front of the queue so that it is
    // the first chunk processed next time.
    mArena.mChunksDirty.pushFront(mChunk);
  }
  mChunk->mIsPurging = false;

#ifdef MALLOC_DOUBLE_PURGE
  if (aAddToMAdvised) {
    // The chunk might already be in the list, but this
    // makes sure it's at the front.
    if (mArena.mChunksMAdvised.ElementProbablyInList(mChunk)) {
      mArena.mChunksMAdvised.remove(mChunk);
    }
    mArena.mChunksMAdvised.pushFront(mChunk);
  }
#endif
}

// run_pages and size make each-other redundant. But we use them both and the
// caller computes both so this function requires both and will assert if they
// are inconsistent.
size_t arena_t::TryCoalesce(arena_chunk_t* aChunk, size_t run_ind,
                            size_t run_pages, size_t size) {
  // Copy in/out parameters to local variables so that we don't need '*'
  // operators throughout this code but also so that type checking is stricter
  // (references are too easily coerced).
  MOZ_ASSERT(size == run_pages << gPageSize2Pow);

  // Try to coalesce forward.
  if (run_ind + run_pages < gChunkNumPages - gPagesPerRealPage &&
      (aChunk->mPageMap[run_ind + run_pages].bits &
       (CHUNK_MAP_ALLOCATED | CHUNK_MAP_BUSY)) == 0) {
    size_t nrun_size =
        aChunk->mPageMap[run_ind + run_pages].bits & ~gPageSizeMask;

    // Remove successor from tree of available runs; the coalesced run is
    // inserted later.
    mRunsAvail.Remove(&aChunk->mPageMap[run_ind + run_pages]);

    size += nrun_size;
    run_pages = size >> gPageSize2Pow;

    MOZ_DIAGNOSTIC_ASSERT((aChunk->mPageMap[run_ind + run_pages - 1].bits &
                           ~gPageSizeMask) == nrun_size);
    aChunk->mPageMap[run_ind].bits =
        size | (aChunk->mPageMap[run_ind].bits & gPageSizeMask);
    aChunk->mPageMap[run_ind + run_pages - 1].bits =
        size | (aChunk->mPageMap[run_ind + run_pages - 1].bits & gPageSizeMask);
  }

  // Try to coalesce backward.
  if (run_ind > gChunkHeaderNumPages &&
      (aChunk->mPageMap[run_ind - 1].bits &
       (CHUNK_MAP_ALLOCATED | CHUNK_MAP_BUSY)) == 0) {
    size_t prun_size = aChunk->mPageMap[run_ind - 1].bits & ~gPageSizeMask;

    run_ind -= prun_size >> gPageSize2Pow;

    // Remove predecessor from tree of available runs; the coalesced run is
    // inserted later.
    mRunsAvail.Remove(&aChunk->mPageMap[run_ind]);

    size += prun_size;
    run_pages = size >> gPageSize2Pow;

    MOZ_DIAGNOSTIC_ASSERT((aChunk->mPageMap[run_ind].bits & ~gPageSizeMask) ==
                          prun_size);
    aChunk->mPageMap[run_ind].bits =
        size | (aChunk->mPageMap[run_ind].bits & gPageSizeMask);
    aChunk->mPageMap[run_ind + run_pages - 1].bits =
        size | (aChunk->mPageMap[run_ind + run_pages - 1].bits & gPageSizeMask);
  }

  // If the dirty run hint points within the run then the new greater run
  // is the run with the lowest index containing dirty pages.  So update the
  // hint.
  if ((aChunk->mDirtyRunHint > run_ind) &&
      (aChunk->mDirtyRunHint < run_ind + run_pages)) {
    aChunk->mDirtyRunHint = run_ind;
  }

  return run_ind;
}

void arena_t::DallocRun(arena_run_t* aRun, bool aDirty) {
  arena_chunk_t* chunk = GetChunkForPtr(aRun);
  size_t run_ind =
      (size_t)((uintptr_t(aRun) - uintptr_t(chunk)) >> gPageSize2Pow);
  MOZ_DIAGNOSTIC_ASSERT(run_ind >= gChunkHeaderNumPages);
  MOZ_RELEASE_ASSERT(run_ind < gChunkNumPages - 1);

  size_t size, run_pages;
  if ((chunk->mPageMap[run_ind].bits & CHUNK_MAP_LARGE) != 0) {
    size = chunk->mPageMap[run_ind].bits & ~gPageSizeMask;
    run_pages = (size >> gPageSize2Pow);
  } else {
    run_pages = aRun->mBin->mRunSizePages;
    size = run_pages << gPageSize2Pow;
  }

  // Mark pages as unallocated in the chunk map, at the same time clear all the
  // page bits and size information, set the dirty bit if the pages are now
  // dirty..
  for (size_t i = 0; i < run_pages; i++) {
    size_t& bits = chunk->mPageMap[run_ind + i].bits;

    // No bits other than ALLOCATED or LARGE may be set.
    MOZ_DIAGNOSTIC_ASSERT(
        (bits & gPageSizeMask & ~(CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED)) == 0);
    bits = aDirty ? CHUNK_MAP_DIRTY : 0;
  }

  if (aDirty) {
    // One of the reasons we check mIsPurging here is so that we don't add a
    // chunk that's currently in the middle of purging to the list, which could
    // start a concurrent purge.
    if (!chunk->mIsPurging &&
        (chunk->mNumDirty == 0 || !mChunksDirty.ElementProbablyInList(chunk))) {
      mChunksDirty.pushBack(chunk);
    }
    chunk->mNumDirty += run_pages;
    mNumDirty += run_pages;
  }

  chunk->mPageMap[run_ind].bits |= size;
  chunk->mPageMap[run_ind + run_pages - 1].bits |= size;

  run_ind = TryCoalesce(chunk, run_ind, run_pages, size);

  // Now that run_ind is finalised we can update the dirty run hint.
  if (aDirty && run_ind < chunk->mDirtyRunHint) {
    chunk->mDirtyRunHint = run_ind;
  }

  // Deallocate chunk if it is now completely unused.
  if (chunk->IsEmpty()) {
    if (!chunk->mIsPurging) {
      DemoteChunkToSpare(chunk);
    }
  } else {
    // Insert into tree of available runs, now that coalescing is complete.
    mRunsAvail.Insert(&chunk->mPageMap[run_ind]);
  }
}

void arena_t::TrimRunHead(arena_chunk_t* aChunk, arena_run_t* aRun,
                          size_t aOldSize, size_t aNewSize) {
  size_t pageind = (uintptr_t(aRun) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t head_npages = (aOldSize - aNewSize) >> gPageSize2Pow;

  MOZ_ASSERT(aOldSize > aNewSize);

  // Update the chunk map so that arena_t::RunDalloc() can treat the
  // leading run as separately allocated.
  aChunk->mPageMap[pageind].bits =
      (aOldSize - aNewSize) | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
  aChunk->mPageMap[pageind + head_npages].bits =
      aNewSize | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;

  DallocRun(aRun, false);
}

void arena_t::TrimRunTail(arena_chunk_t* aChunk, arena_run_t* aRun,
                          size_t aOldSize, size_t aNewSize, bool aDirty) {
  size_t pageind = (uintptr_t(aRun) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t npages = aNewSize >> gPageSize2Pow;

  MOZ_ASSERT(aOldSize > aNewSize);

  // Update the chunk map so that arena_t::RunDalloc() can treat the
  // trailing run as separately allocated.
  aChunk->mPageMap[pageind].bits =
      aNewSize | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
  aChunk->mPageMap[pageind + npages].bits =
      (aOldSize - aNewSize) | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;

  DallocRun((arena_run_t*)(uintptr_t(aRun) + aNewSize), aDirty);
}

arena_run_t* arena_t::GetNewEmptyBinRun(arena_bin_t* aBin) {
  arena_run_t* run;
  unsigned i, remainder;

  // Allocate a new run.
  run = AllocRun(static_cast<size_t>(aBin->mRunSizePages) << gPageSize2Pow,
                 false, false);
  if (!run) {
    return nullptr;
  }

  // Initialize run internals.
  run->mBin = aBin;

  for (i = 0; i < aBin->mRunNumRegionsMask - 1; i++) {
    run->mRegionsMask[i] = UINT_MAX;
  }
  remainder = aBin->mRunNumRegions & ((1U << (LOG2(sizeof(int)) + 3)) - 1);
  if (remainder == 0) {
    run->mRegionsMask[i] = UINT_MAX;
  } else {
    // The last element has spare bits that need to be unset.
    run->mRegionsMask[i] =
        (UINT_MAX >> ((1U << (LOG2(sizeof(int)) + 3)) - remainder));
  }

  run->mRegionsMinElement = 0;

  run->mNumFree = aBin->mRunNumRegions;
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  run->mMagic = ARENA_RUN_MAGIC;
#endif

  // Make sure we continue to use this run for subsequent allocations.
  new (&run->mRunListElem) DoublyLinkedListElement<arena_run_t>();
  aBin->mNonFullRuns.pushFront(run);

  aBin->mNumRuns++;
  return run;
}

arena_run_t* arena_t::GetNonFullBinRun(arena_bin_t* aBin) {
  auto mrf_head = aBin->mNonFullRuns.begin();
  if (mrf_head) {
    // Take the head and if we are going to fill it, remove it from our list.
    arena_run_t* run = &(*mrf_head);
    MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
    if (run->mNumFree == 1) {
      aBin->mNonFullRuns.remove(run);
    }
    return run;
  }
  return GetNewEmptyBinRun(aBin);
}

arena_bin_t::arena_bin_t(SizeClass aSizeClass) : mSizeClass(aSizeClass.Size()) {
  size_t try_run_size;
  unsigned try_nregs, try_mask_nelms, try_reg0_offset;
  // Size of the run header, excluding mRegionsMask.
  static const size_t kFixedHeaderSize = offsetof(arena_run_t, mRegionsMask);

  MOZ_ASSERT(aSizeClass.Size() <= gMaxBinClass);

  try_run_size = gMinimumRunSize;

  // Run size expansion loop.
  while (true) {
    try_nregs = ((try_run_size - kFixedHeaderSize) / mSizeClass) +
                1;  // Counter-act try_nregs-- in loop.

    // The do..while loop iteratively reduces the number of regions until
    // the run header and the regions no longer overlap.  A closed formula
    // would be quite messy, since there is an interdependency between the
    // header's mask length and the number of regions.
    do {
      try_nregs--;
      try_mask_nelms =
          (try_nregs >> (LOG2(sizeof(int)) + 3)) +
          ((try_nregs & ((1U << (LOG2(sizeof(int)) + 3)) - 1)) ? 1 : 0);
      try_reg0_offset = try_run_size - (try_nregs * mSizeClass);
    } while (kFixedHeaderSize + (sizeof(unsigned) * try_mask_nelms) >
             try_reg0_offset);

    // Try to keep the run overhead below kRunOverhead.
    if (Fraction(try_reg0_offset, try_run_size) <= kRunOverhead) {
      break;
    }

    // If the overhead is larger than the size class, it means the size class
    // is small and doesn't align very well with the header. It's desirable to
    // have smaller run sizes for them, so relax the overhead requirement.
    if (try_reg0_offset > mSizeClass) {
      if (Fraction(try_reg0_offset, try_run_size) <= kRunRelaxedOverhead) {
        break;
      }
    }

    // The run header includes one bit per region of the given size. For sizes
    // small enough, the number of regions is large enough that growing the run
    // size barely moves the needle for the overhead because of all those bits.
    // For example, for a size of 8 bytes, adding 4KiB to the run size adds
    // close to 512 bits to the header, which is 64 bytes.
    // With such overhead, there is no way to get to the wanted overhead above,
    // so we give up if the required size for mRegionsMask more than doubles the
    // size of the run header.
    if (try_mask_nelms * sizeof(unsigned) >= kFixedHeaderSize) {
      break;
    }

    // If next iteration is going to be larger than the largest possible large
    // size class, then we didn't find a setup where the overhead is small
    // enough, and we can't do better than the current settings, so just use
    // that.
    if (try_run_size + gPageSize > gMaxLargeClass) {
      break;
    }

    // Try more aggressive settings.
    try_run_size += gPageSize;
  }

  MOZ_ASSERT(kFixedHeaderSize + (sizeof(unsigned) * try_mask_nelms) <=
             try_reg0_offset);
  MOZ_ASSERT((try_mask_nelms << (LOG2(sizeof(int)) + 3)) >= try_nregs);

  // Our list management would break if mRunNumRegions == 1 and we should use
  // a large size class instead, anyways.
  MOZ_ASSERT(try_nregs > 1);

  // Copy final settings.
  MOZ_ASSERT((try_run_size >> gPageSize2Pow) <= UINT8_MAX);
  mRunSizePages = static_cast<uint8_t>(try_run_size >> gPageSize2Pow);
  mRunNumRegions = try_nregs;
  mRunNumRegionsMask = try_mask_nelms;
  mRunFirstRegionOffset = try_reg0_offset;
  mSizeDivisor = FastDivisor<uint16_t>(aSizeClass.Size(), try_run_size);
}

void arena_t::ResetSmallAllocRandomization() {
  if (MOZ_UNLIKELY(opt_randomize_small)) {
    MaybeMutexAutoLock lock(mLock);
    InitPRNG();
  }
  mRandomizeSmallAllocations = opt_randomize_small;
}

void arena_t::InitPRNG() {
  // Both another thread could race and the code backing RandomUint64
  // (arc4random for example) may allocate memory while here, so we must
  // ensure to start the mPRNG initialization only once and to not hold
  // the lock while initializing.
  mIsPRNGInitializing = true;
  {
    mLock.Unlock();
    mozilla::Maybe<uint64_t> prngState1 = mozilla::RandomUint64();
    mozilla::Maybe<uint64_t> prngState2 = mozilla::RandomUint64();
    mLock.Lock();

    mozilla::non_crypto::XorShift128PlusRNG prng(prngState1.valueOr(0),
                                                 prngState2.valueOr(0));
    if (mPRNG) {
      *mPRNG = prng;
    } else {
      void* backing =
          sBaseAlloc.alloc(sizeof(mozilla::non_crypto::XorShift128PlusRNG));
      mPRNG = new (backing)
          mozilla::non_crypto::XorShift128PlusRNG(std::move(prng));
    }
  }
  mIsPRNGInitializing = false;
}

void* arena_t::MallocSmall(size_t aSize, bool aZero) {
  void* ret;
  arena_bin_t* bin;
  arena_run_t* run;
  SizeClass sizeClass(aSize);
  aSize = sizeClass.Size();

  switch (sizeClass.Type()) {
    case SizeClass::Quantum:
      // Although we divide 2 things by kQuantum, the compiler will
      // reduce `kMinQuantumClass / kQuantum` to a single constant.
      bin = &mBins[(aSize / kQuantum) - (kMinQuantumClass / kQuantum)];
      break;
    case SizeClass::QuantumWide:
      bin = &mBins[kNumQuantumClasses + (aSize / kQuantumWide) -
                   (kMinQuantumWideClass / kQuantumWide)];
      break;
    case SizeClass::SubPage:
      bin = &mBins[kNumQuantumClasses + kNumQuantumWideClasses +
                   (FloorLog2(aSize) - LOG2(kMinSubPageClass))];
      break;
    default:
      MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unexpected size class type");
  }
  MOZ_DIAGNOSTIC_ASSERT(aSize == bin->mSizeClass);

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);

#ifdef MOZ_DEBUG
    bool isInitializingThread(false);
#endif

    if (MOZ_UNLIKELY(mRandomizeSmallAllocations && mPRNG == nullptr &&
                     !mIsPRNGInitializing)) {
#ifdef MOZ_DEBUG
      isInitializingThread = true;
#endif
      InitPRNG();
    }

    MOZ_ASSERT(!mRandomizeSmallAllocations || mPRNG ||
               (mIsPRNGInitializing && !isInitializingThread));

    num_dirty_before = mNumDirty;
    run = GetNonFullBinRun(bin);
    num_dirty_after = mNumDirty;
    if (MOZ_UNLIKELY(!run)) {
      return nullptr;
    }
    MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
    MOZ_DIAGNOSTIC_ASSERT(run->mNumFree > 0);
    ret = ArenaRunRegAlloc(run, bin);
    MOZ_DIAGNOSTIC_ASSERT(ret);
    run->mNumFree--;
    if (!ret) {
      return nullptr;
    }

    mStats.allocated_small += aSize;
    mStats.operations++;
  }
  if (num_dirty_after < num_dirty_before) {
    NotifyPageReuse();
  }
  if (!aZero) {
    ApplyZeroOrJunk(ret, aSize);
  } else {
    memset(ret, 0, aSize);
  }

  return ret;
}

void* arena_t::MallocLarge(size_t aSize, bool aZero) {
  void* ret;

  // Large allocation.
  aSize = PAGE_CEILING(aSize);

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);
    num_dirty_before = mNumDirty;
    ret = AllocRun(aSize, true, aZero);
    num_dirty_after = mNumDirty;
    if (!ret) {
      return nullptr;
    }
    mStats.allocated_large += aSize;
    mStats.operations++;
  }
  if (num_dirty_after < num_dirty_before) {
    NotifyPageReuse();
  }

  if (!aZero) {
    ApplyZeroOrJunk(ret, aSize);
  }

  return ret;
}

void* arena_t::Malloc(size_t aSize, bool aZero) {
  MOZ_DIAGNOSTIC_ASSERT(mMagic == ARENA_MAGIC);
  MOZ_ASSERT(aSize != 0);

  if (aSize <= gMaxBinClass) {
    return MallocSmall(aSize, aZero);
  }
  if (aSize <= gMaxLargeClass) {
    return MallocLarge(aSize, aZero);
  }
  return MallocHuge(aSize, aZero);
}

// Only handles large allocations that require more than page alignment.
void* arena_t::PallocLarge(size_t aAlignment, size_t aSize, size_t aAllocSize) {
  void* ret;
  size_t offset;
  arena_chunk_t* chunk;

  MOZ_ASSERT((aSize & gPageSizeMask) == 0);
  MOZ_ASSERT((aAlignment & gPageSizeMask) == 0);

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);
    num_dirty_before = mNumDirty;
    ret = AllocRun(aAllocSize, true, false);
    if (!ret) {
      return nullptr;
    }

    chunk = GetChunkForPtr(ret);

    offset = uintptr_t(ret) & (aAlignment - 1);
    MOZ_ASSERT((offset & gPageSizeMask) == 0);
    MOZ_ASSERT(offset < aAllocSize);
    if (offset == 0) {
      TrimRunTail(chunk, (arena_run_t*)ret, aAllocSize, aSize, false);
    } else {
      size_t leadsize, trailsize;

      leadsize = aAlignment - offset;
      if (leadsize > 0) {
        TrimRunHead(chunk, (arena_run_t*)ret, aAllocSize,
                    aAllocSize - leadsize);
        ret = (void*)(uintptr_t(ret) + leadsize);
      }

      trailsize = aAllocSize - leadsize - aSize;
      if (trailsize != 0) {
        // Trim trailing space.
        MOZ_ASSERT(trailsize < aAllocSize);
        TrimRunTail(chunk, (arena_run_t*)ret, aSize + trailsize, aSize, false);
      }
    }
    num_dirty_after = mNumDirty;

    mStats.allocated_large += aSize;
    mStats.operations++;
  }
  if (num_dirty_after < num_dirty_before) {
    NotifyPageReuse();
  }
  // Note that since Bug 1488780we don't attempt purge dirty memory on this code
  // path. In general there won't be dirty memory above the threshold after an
  // allocation, only after free.  The exception is if the dirty page threshold
  // has changed.

  ApplyZeroOrJunk(ret, aSize);
  return ret;
}

void* arena_t::Palloc(size_t aAlignment, size_t aSize) {
  void* ret;
  size_t ceil_size;

  // Round size up to the nearest multiple of alignment.
  //
  // This done, we can take advantage of the fact that for each small
  // size class, every object is aligned at the smallest power of two
  // that is non-zero in the base two representation of the size.  For
  // example:
  //
  //   Size |   Base 2 | Minimum alignment
  //   -----+----------+------------------
  //     96 |  1100000 |  32
  //    144 | 10100000 |  32
  //    192 | 11000000 |  64
  //
  // Depending on runtime settings, it is possible that arena_malloc()
  // will further round up to a power of two, but that never causes
  // correctness issues.
  ceil_size = ALIGNMENT_CEILING(aSize, aAlignment);

  // (ceil_size < aSize) protects against the combination of maximal
  // alignment and size greater than maximal alignment.
  if (ceil_size < aSize) {
    // size_t overflow.
    return nullptr;
  }

  if (ceil_size <= gPageSize ||
      (aAlignment <= gPageSize && ceil_size <= gMaxLargeClass)) {
    ret = Malloc(ceil_size, false);
  } else {
    size_t run_size;

    // We can't achieve sub-page alignment, so round up alignment
    // permanently; it makes later calculations simpler.
    aAlignment = PAGE_CEILING(aAlignment);
    ceil_size = PAGE_CEILING(aSize);

    // (ceil_size < aSize) protects against very large sizes within
    // pagesize of SIZE_T_MAX.
    //
    // (ceil_size + aAlignment < ceil_size) protects against the
    // combination of maximal alignment and ceil_size large enough
    // to cause overflow.  This is similar to the first overflow
    // check above, but it needs to be repeated due to the new
    // ceil_size value, which may now be *equal* to maximal
    // alignment, whereas before we only detected overflow if the
    // original size was *greater* than maximal alignment.
    if (ceil_size < aSize || ceil_size + aAlignment < ceil_size) {
      // size_t overflow.
      return nullptr;
    }

    // Calculate the size of the over-size run that arena_palloc()
    // would need to allocate in order to guarantee the alignment.
    if (ceil_size >= aAlignment) {
      run_size = ceil_size + aAlignment - gPageSize;
    } else {
      // It is possible that (aAlignment << 1) will cause
      // overflow, but it doesn't matter because we also
      // subtract pagesize, which in the case of overflow
      // leaves us with a very large run_size.  That causes
      // the first conditional below to fail, which means
      // that the bogus run_size value never gets used for
      // anything important.
      run_size = (aAlignment << 1) - gPageSize;
    }

    if (run_size <= gMaxLargeClass) {
      ret = PallocLarge(aAlignment, ceil_size, run_size);
    } else if (aAlignment <= kChunkSize) {
      ret = MallocHuge(ceil_size, false);
    } else {
      ret = PallocHuge(ceil_size, aAlignment, false);
    }
  }

  MOZ_ASSERT((uintptr_t(ret) & (aAlignment - 1)) == 0);
  return ret;
}

class AllocInfo {
 public:
  template <bool Validate = false>
  static inline AllocInfo Get(const void* aPtr) {
    // If the allocator is not initialized, the pointer can't belong to it.
    if (Validate && !malloc_initialized) {
      return AllocInfo();
    }

    auto chunk = GetChunkForPtr(aPtr);
    if (Validate) {
      if (!chunk || !gChunkRTree.Get(chunk)) {
        return AllocInfo();
      }
    }

    if (chunk != aPtr) {
      MOZ_DIAGNOSTIC_ASSERT(chunk->mArena->mMagic == ARENA_MAGIC);
      size_t pageind = (((uintptr_t)aPtr - (uintptr_t)chunk) >> gPageSize2Pow);
      return GetInChunk(aPtr, chunk, pageind);
    }

    // Huge allocation
    MutexAutoLock lock(huge_mtx);
    extent_node_t* node = huge.Search(chunk);
    if (Validate && !node) {
      return AllocInfo();
    }
    return AllocInfo(node->mSize, node);
  }

  // Get the allocation information for a pointer we know is within a chunk
  // (Small or large, not huge).
  static inline AllocInfo GetInChunk(const void* aPtr, arena_chunk_t* aChunk,
                                     size_t pageind) {
    size_t mapbits = aChunk->mPageMap[pageind].bits;
    MOZ_DIAGNOSTIC_ASSERT((mapbits & CHUNK_MAP_ALLOCATED) != 0);

    size_t size;
    if ((mapbits & CHUNK_MAP_LARGE) == 0) {
      arena_run_t* run = (arena_run_t*)(mapbits & ~gPageSizeMask);
      MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
      size = run->mBin->mSizeClass;
    } else {
      size = mapbits & ~gPageSizeMask;
      MOZ_DIAGNOSTIC_ASSERT(size != 0);
    }

    return AllocInfo(size, aChunk);
  }

  // Validate ptr before assuming that it points to an allocation.  Currently,
  // the following validation is performed:
  //
  // + Check that ptr is not nullptr.
  //
  // + Check that ptr lies within a mapped chunk.
  static inline AllocInfo GetValidated(const void* aPtr) {
    return Get<true>(aPtr);
  }

  AllocInfo() : mSize(0), mChunk(nullptr) {}

  explicit AllocInfo(size_t aSize, arena_chunk_t* aChunk)
      : mSize(aSize), mChunk(aChunk) {
    MOZ_ASSERT(mSize <= gMaxLargeClass);
  }

  explicit AllocInfo(size_t aSize, extent_node_t* aNode)
      : mSize(aSize), mNode(aNode) {
    MOZ_ASSERT(mSize > gMaxLargeClass);
  }

  size_t Size() { return mSize; }

  arena_t* Arena() {
    if (mSize <= gMaxLargeClass) {
      return mChunk->mArena;
    }

    // Best effort detection that we're not trying to access an already
    // disposed arena.  arena_t's destructor will clear mMagic and mId;
    // any other use of the same memory will usually set them to some other
    // value.
    MOZ_DIAGNOSTIC_ASSERT(mNode->mArena->mMagic == ARENA_MAGIC);
    MOZ_RELEASE_ASSERT(mNode->mArenaId == mNode->mArena->mId);
    return mNode->mArena;
  }

  bool IsValid() const { return !!mSize; }

 private:
  size_t mSize;
  union {
    // Pointer to the chunk associated with the allocation for small
    // and large allocations.
    arena_chunk_t* mChunk;

    // Pointer to the extent node for huge allocations.
    extent_node_t* mNode;
  };
};

inline void MozJemalloc::jemalloc_ptr_info(const void* aPtr,
                                           jemalloc_ptr_info_t* aInfo) {
  arena_chunk_t* chunk = GetChunkForPtr(aPtr);

  // Is the pointer null, or within one chunk's size of null?
  // Alternatively, if the allocator is not initialized yet, the pointer
  // can't be known.
  if (!chunk || !malloc_initialized) {
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  // Look for huge allocations before looking for |chunk| in gChunkRTree.
  // This is necessary because |chunk| won't be in gChunkRTree if it's
  // the second or subsequent chunk in a huge allocation.
  extent_node_t* node;
  {
    MutexAutoLock lock(huge_mtx);
    node =
        reinterpret_cast<RedBlackTree<extent_node_t, ExtentTreeBoundsTrait>*>(
            &huge)
            ->Search(const_cast<void*>(aPtr));
    if (node) {
      *aInfo = {TagLiveAlloc, node->mAddr, node->mSize, node->mArena->mId};
      return;
    }
  }

  // It's not a huge allocation. Check if we have a known chunk.
  if (!gChunkRTree.Get(chunk)) {
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(chunk->mArena->mMagic == ARENA_MAGIC);

  // Get the page number within the chunk.
  size_t pageind = (((uintptr_t)aPtr - (uintptr_t)chunk) >> gPageSize2Pow);
  if (pageind < gChunkHeaderNumPages) {
    // Within the chunk header.
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  size_t mapbits = chunk->mPageMap[pageind].bits;

  if (!(mapbits & CHUNK_MAP_ALLOCATED)) {
    void* pageaddr = (void*)(uintptr_t(aPtr) & ~gPageSizeMask);
    *aInfo = {TagFreedPage, pageaddr, gPageSize, chunk->mArena->mId};
    return;
  }

  if (mapbits & CHUNK_MAP_LARGE) {
    // It's a large allocation. Only the first page of a large
    // allocation contains its size, so if the address is not in
    // the first page, scan back to find the allocation size.
    size_t size;
    while (true) {
      size = mapbits & ~gPageSizeMask;
      if (size != 0) {
        break;
      }

      // The following two return paths shouldn't occur in
      // practice unless there is heap corruption.
      pageind--;
      MOZ_DIAGNOSTIC_ASSERT(pageind >= gChunkHeaderNumPages);
      if (pageind < gChunkHeaderNumPages) {
        *aInfo = {TagUnknown, nullptr, 0, 0};
        return;
      }

      mapbits = chunk->mPageMap[pageind].bits;
      MOZ_DIAGNOSTIC_ASSERT(mapbits & CHUNK_MAP_LARGE);
      if (!(mapbits & CHUNK_MAP_LARGE)) {
        *aInfo = {TagUnknown, nullptr, 0, 0};
        return;
      }
    }

    void* addr = ((char*)chunk) + (pageind << gPageSize2Pow);
    *aInfo = {TagLiveAlloc, addr, size, chunk->mArena->mId};
    return;
  }

  // It must be a small allocation.
  auto run = (arena_run_t*)(mapbits & ~gPageSizeMask);
  MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);

  // The allocation size is stored in the run metadata.
  size_t size = run->mBin->mSizeClass;

  // Address of the first possible pointer in the run after its headers.
  uintptr_t reg0_addr = (uintptr_t)run + run->mBin->mRunFirstRegionOffset;
  if (aPtr < (void*)reg0_addr) {
    // In the run header.
    *aInfo = {TagUnknown, nullptr, 0, 0};
    return;
  }

  // Position in the run.
  unsigned regind = ((uintptr_t)aPtr - reg0_addr) / size;

  // Pointer to the allocation's base address.
  void* addr = (void*)(reg0_addr + regind * size);

  // Check if the allocation has been freed.
  unsigned elm = regind >> (LOG2(sizeof(int)) + 3);
  unsigned bit = regind - (elm << (LOG2(sizeof(int)) + 3));
  PtrInfoTag tag =
      ((run->mRegionsMask[elm] & (1U << bit))) ? TagFreedAlloc : TagLiveAlloc;

  *aInfo = {tag, addr, size, chunk->mArena->mId};
}

namespace Debug {
// Helper for debuggers. We don't want it to be inlined and optimized out.
MOZ_NEVER_INLINE jemalloc_ptr_info_t* jemalloc_ptr_info(const void* aPtr) {
  static jemalloc_ptr_info_t info;
  MozJemalloc::jemalloc_ptr_info(aPtr, &info);
  return &info;
}
}  // namespace Debug

void arena_t::DallocSmall(arena_chunk_t* aChunk, void* aPtr,
                          arena_chunk_map_t* aMapElm) {
  arena_run_t* run;
  arena_bin_t* bin;
  size_t size;

  run = (arena_run_t*)(aMapElm->bits & ~gPageSizeMask);
  MOZ_DIAGNOSTIC_ASSERT(run->mMagic == ARENA_RUN_MAGIC);
  bin = run->mBin;
  size = bin->mSizeClass;
  MOZ_DIAGNOSTIC_ASSERT(uintptr_t(aPtr) >=
                        uintptr_t(run) + bin->mRunFirstRegionOffset);

  arena_run_reg_dalloc(run, bin, aPtr, size);
  run->mNumFree++;

  if (run->mNumFree == bin->mRunNumRegions) {
    // This run is entirely freed, remove it from our bin.
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
    run->mMagic = 0;
#endif
    MOZ_ASSERT(bin->mNonFullRuns.ElementProbablyInList(run));
    bin->mNonFullRuns.remove(run);
    DallocRun(run, true);
    bin->mNumRuns--;
  } else if (run->mNumFree == 1) {
    // This is first slot we freed from this run, start tracking.
    MOZ_ASSERT(!bin->mNonFullRuns.ElementProbablyInList(run));
    bin->mNonFullRuns.pushFront(run);
  }
  // else we just keep the run in mNonFullRuns where it is.
  // Note that we could move it to the head of the list here to get a strict
  // "most-recently-freed" order, but some of our benchmarks seem to be more
  // sensible to the increased overhead that this brings than to the order
  // supposedly slightly better for keeping CPU caches warm if we do.
  // In general we cannot foresee the future, so any order we choose might
  // perform different for different use cases and needs to be balanced with
  // the book-keeping overhead via measurements.

  mStats.allocated_small -= size;
  mStats.operations++;
}

void arena_t::DallocLarge(arena_chunk_t* aChunk, void* aPtr) {
  MOZ_DIAGNOSTIC_ASSERT((uintptr_t(aPtr) & gPageSizeMask) == 0);
  size_t pageind = (uintptr_t(aPtr) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t size = aChunk->mPageMap[pageind].bits & ~gPageSizeMask;

  mStats.allocated_large -= size;
  mStats.operations++;

  DallocRun((arena_run_t*)aPtr, true);
}

static inline void arena_dalloc(void* aPtr, size_t aOffset, arena_t* aArena) {
  MOZ_ASSERT(aPtr);
  MOZ_ASSERT(aOffset != 0);
  MOZ_ASSERT(GetChunkOffsetForPtr(aPtr) == aOffset);

  auto chunk = (arena_chunk_t*)((uintptr_t)aPtr - aOffset);
  auto arena = chunk->mArena;
  MOZ_ASSERT(arena);
  MOZ_DIAGNOSTIC_ASSERT(arena->mMagic == ARENA_MAGIC);
  MOZ_RELEASE_ASSERT(!aArena || arena == aArena);

  size_t pageind = aOffset >> gPageSize2Pow;
  if (opt_poison) {
    AllocInfo info = AllocInfo::GetInChunk(aPtr, chunk, pageind);
    MOZ_ASSERT(info.IsValid());
    MaybePoison(aPtr, info.Size());
  }

  purge_action_t purge_action;
  {
    MOZ_DIAGNOSTIC_ASSERT(arena->mLock.SafeOnThisThread());
    MaybeMutexAutoLock lock(arena->mLock);
    arena_chunk_map_t* mapelm = &chunk->mPageMap[pageind];
    MOZ_RELEASE_ASSERT(
        (mapelm->bits &
         (CHUNK_MAP_FRESH_MADVISED_OR_DECOMMITTED | CHUNK_MAP_ZEROED)) == 0,
        "Freeing in a page with bad bits.");
    MOZ_RELEASE_ASSERT((mapelm->bits & CHUNK_MAP_ALLOCATED) != 0,
                       "Double-free?");
    if ((mapelm->bits & CHUNK_MAP_LARGE) == 0) {
      // Small allocation.
      arena->DallocSmall(chunk, aPtr, mapelm);
    } else {
      // Large allocation.
      arena->DallocLarge(chunk, aPtr);
    }

    purge_action = arena->ShouldStartPurge();
  }

  arena->MayDoOrQueuePurge(purge_action, "arena_dalloc");
}

static inline void idalloc(void* ptr, arena_t* aArena) {
  size_t offset;

  MOZ_ASSERT(ptr);

  offset = GetChunkOffsetForPtr(ptr);
  if (offset != 0) {
    arena_dalloc(ptr, offset, aArena);
  } else {
    huge_dalloc(ptr, aArena);
  }
}

inline purge_action_t arena_t::ShouldStartPurge() {
  if (mNumDirty > mMaxDirty) {
    if (!mIsDeferredPurgeEnabled) {
      return purge_action_t::PurgeNow;
    }
    if (mIsPurgePending) {
      return purge_action_t::None;
    }
    mIsPurgePending = true;
    return purge_action_t::Queue;
  }
  return purge_action_t::None;
}

inline void arena_t::MayDoOrQueuePurge(purge_action_t aAction,
                                       const char* aCaller) {
  switch (aAction) {
    case purge_action_t::Queue:
      // Note that this thread committed earlier by setting
      // mIsPurgePending to add us to the list. There is a low
      // chance that in the meantime another thread ran Purge() and cleared
      // the flag, but that is fine, we'll adjust our bookkeeping when calling
      // ShouldStartPurge() or Purge() next time.
      gArenas.AddToOutstandingPurges(this);
      break;
    case purge_action_t::PurgeNow: {
      ArenaPurgeResult pr = PurgeLoop(PurgeIfThreshold, aCaller);
      // Arenas cannot die here because the caller is still using the arena, if
      // they did it'd be a use-after-free: the arena is destroyed but then used
      // afterwards.
      MOZ_RELEASE_ASSERT(pr != ArenaPurgeResult::Dying);
      break;
    }
    case purge_action_t::None:
      // do nothing.
      break;
  }
}

inline void arena_t::NotifyPageReuse() {
  // Note that there is a chance here for a race between threads calling
  // GetTimeStampNS in a different order than writing it to the Atomic,
  // resulting in mLastPageReuseNS going potentially backwards.
  // Our use case is not sensitive to small deviations, the worse that can
  // happen is a slightly earlier purge.
  mLastPageReuseNS = GetTimestampNS();
}

void arena_t::RallocShrinkLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                                size_t aOldSize) {
  MOZ_ASSERT(aSize < aOldSize);

  // Shrink the run, and make trailing pages available for other
  // allocations.
  purge_action_t purge_action;
  {
    MaybeMutexAutoLock lock(mLock);
    TrimRunTail(aChunk, (arena_run_t*)aPtr, aOldSize, aSize, true);
    mStats.allocated_large -= aOldSize - aSize;
    mStats.operations++;

    purge_action = ShouldStartPurge();
  }
  MayDoOrQueuePurge(purge_action, "RallocShrinkLarge");
}

// Returns whether reallocation was successful.
bool arena_t::RallocGrowLarge(arena_chunk_t* aChunk, void* aPtr, size_t aSize,
                              size_t aOldSize) {
  size_t pageind = (uintptr_t(aPtr) - uintptr_t(aChunk)) >> gPageSize2Pow;
  size_t npages = aOldSize >> gPageSize2Pow;

  size_t num_dirty_before, num_dirty_after;
  {
    MaybeMutexAutoLock lock(mLock);
    MOZ_DIAGNOSTIC_ASSERT(aOldSize ==
                          (aChunk->mPageMap[pageind].bits & ~gPageSizeMask));

    // Try to extend the run.
    MOZ_ASSERT(aSize > aOldSize);
    if (pageind + npages < gChunkNumPages - 1 &&
        (aChunk->mPageMap[pageind + npages].bits &
         (CHUNK_MAP_ALLOCATED | CHUNK_MAP_BUSY)) == 0 &&
        (aChunk->mPageMap[pageind + npages].bits & ~gPageSizeMask) >=
            aSize - aOldSize) {
      num_dirty_before = mNumDirty;
      // The next run is available and sufficiently large.  Split the
      // following run, then merge the first part with the existing
      // allocation.
      mRunsAvail.Remove(&aChunk->mPageMap[pageind + npages]);
      if (!SplitAndAllocRun(
              (arena_run_t*)(uintptr_t(aChunk) +
                             ((pageind + npages) << gPageSize2Pow)),
              aSize - aOldSize, true, false)) {
        mRunsAvail.Insert(&aChunk->mPageMap[pageind + npages]);
        return false;
      }

      aChunk->mPageMap[pageind].bits =
          aSize | CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;
      aChunk->mPageMap[pageind + npages].bits =
          CHUNK_MAP_LARGE | CHUNK_MAP_ALLOCATED;

      mStats.allocated_large += aSize - aOldSize;
      mStats.operations++;
      num_dirty_after = mNumDirty;
    } else {
      return false;
    }
  }
  if (num_dirty_after < num_dirty_before) {
    NotifyPageReuse();
  }
  return true;
}

#ifdef XP_DARWIN
#  define VM_COPY_MIN kChunkSize
static inline void pages_copy(void* dest, const void* src, size_t n) {
  MOZ_ASSERT((void*)((uintptr_t)dest & ~gPageSizeMask) == dest);
  MOZ_ASSERT(n >= VM_COPY_MIN);
  MOZ_ASSERT((void*)((uintptr_t)src & ~gPageSizeMask) == src);

  kern_return_t r = vm_copy(mach_task_self(), (vm_address_t)src, (vm_size_t)n,
                            (vm_address_t)dest);
  if (r != KERN_SUCCESS) {
    MOZ_CRASH("vm_copy() failed");
  }
}
#endif

void* arena_t::RallocSmallOrLarge(void* aPtr, size_t aSize, size_t aOldSize) {
  void* ret;
  size_t copysize;
  SizeClass sizeClass(aSize);

  // Try to avoid moving the allocation.
  if (aOldSize <= gMaxLargeClass && sizeClass.Size() == aOldSize) {
    if (aSize < aOldSize) {
      MaybePoison((void*)(uintptr_t(aPtr) + aSize), aOldSize - aSize);
    }
    return aPtr;
  }
  if (sizeClass.Type() == SizeClass::Large && aOldSize > gMaxBinClass &&
      aOldSize <= gMaxLargeClass) {
    arena_chunk_t* chunk = GetChunkForPtr(aPtr);
    if (sizeClass.Size() < aOldSize) {
      // Fill before shrinking in order to avoid a race.
      MaybePoison((void*)((uintptr_t)aPtr + aSize), aOldSize - aSize);
      RallocShrinkLarge(chunk, aPtr, sizeClass.Size(), aOldSize);
      return aPtr;
    }
    if (RallocGrowLarge(chunk, aPtr, sizeClass.Size(), aOldSize)) {
      ApplyZeroOrJunk((void*)((uintptr_t)aPtr + aOldSize), aSize - aOldSize);
      return aPtr;
    }
  }

  // If we get here, then aSize and aOldSize are different enough that we
  // need to move the object or the run can't be expanded because the memory
  // after it is allocated or busy.  In that case, fall back to allocating new
  // space and copying. Allow non-private arenas to switch arenas.
  ret = (mIsPrivate ? this : choose_arena(aSize))->Malloc(aSize, false);
  if (!ret) {
    return nullptr;
  }

  // Junk/zero-filling were already done by arena_t::Malloc().
  copysize = (aSize < aOldSize) ? aSize : aOldSize;
#ifdef VM_COPY_MIN
  if (copysize >= VM_COPY_MIN) {
    pages_copy(ret, aPtr, copysize);
  } else
#endif
  {
    memcpy(ret, aPtr, copysize);
  }
  idalloc(aPtr, this);
  return ret;
}

void* arena_t::Ralloc(void* aPtr, size_t aSize, size_t aOldSize) {
  MOZ_DIAGNOSTIC_ASSERT(mMagic == ARENA_MAGIC);
  MOZ_ASSERT(aPtr);
  MOZ_ASSERT(aSize != 0);

  return (aSize <= gMaxLargeClass) ? RallocSmallOrLarge(aPtr, aSize, aOldSize)
                                   : RallocHuge(aPtr, aSize, aOldSize);
}

void* arena_t::operator new(size_t aCount, const fallible_t&) noexcept {
  MOZ_ASSERT(aCount == sizeof(arena_t));
  // Ignore aCount, instead allocate axtra space for the trailing array of
  // bins.
  return sBaseAlloc.alloc(sizeof(arena_t) +
                          (sizeof(arena_bin_t) * NUM_SMALL_CLASSES));
}

arena_t::arena_t(arena_params_t* aParams, bool aIsPrivate)
    : mRandomizeSmallAllocations(opt_randomize_small),
      mIsPrivate(aIsPrivate),
      // The default maximum amount of dirty pages allowed on arenas is a
      // fraction of opt_dirty_max.
      mMaxDirtyBase((aParams && aParams->mMaxDirty) ? aParams->mMaxDirty
                                                    : (opt_dirty_max / 8)),
      mLastPageReuseNS(GetTimestampNS()),
      mChunkAllocator(&gSystemChunkAllocator) {
  MaybeMutex::DoLock doLock = MaybeMutex::MUST_LOCK;
  if (aParams) {
    uint32_t randFlags = aParams->mFlags & ARENA_FLAG_RANDOMIZE_SMALL_MASK;
    switch (randFlags) {
      case ARENA_FLAG_RANDOMIZE_SMALL_ENABLED:
        mRandomizeSmallAllocations = true;
        break;
      case ARENA_FLAG_RANDOMIZE_SMALL_DISABLED:
        mRandomizeSmallAllocations = false;
        break;
      case ARENA_FLAG_RANDOMIZE_SMALL_DEFAULT:
      default:
        break;
    }

    uint32_t threadFlags = aParams->mFlags & ARENA_FLAG_THREAD_MASK;
    if (threadFlags == ARENA_FLAG_THREAD_MAIN_THREAD_ONLY) {
      // At the moment we require that any ARENA_FLAG_THREAD_MAIN_THREAD_ONLY
      // arenas are created and therefore always accessed by the main thread.
      // This is for two reasons:
      //  * it allows jemalloc_stats to read their statistics (we also require
      //    that jemalloc_stats is only used on the main thread).
      //  * Only main-thread or threadsafe arenas can be guanteed to be in a
      //    consistent state after a fork() from the main thread.  If fork()
      //    occurs off-thread then the new child process cannot use these arenas
      //    (new children should usually exec() or exit() since other data may
      //    also be inconsistent).
      MOZ_ASSERT(gArenas.IsOnMainThread());
      MOZ_ASSERT(aIsPrivate);
      doLock = MaybeMutex::AVOID_LOCK_UNSAFE;
    }

    mMaxDirtyIncreaseOverride = aParams->mMaxDirtyIncreaseOverride;
    mMaxDirtyDecreaseOverride = aParams->mMaxDirtyDecreaseOverride;

    if (aParams->mLabel) {
      // The string may be truncated so always place a null-byte in the last
      // position.
      strncpy(mLabel, aParams->mLabel, LABEL_MAX_CAPACITY - 1);
      mLabel[LABEL_MAX_CAPACITY - 1] = 0;

      // If the string was truncated, then replace its end with "..."
      if (strlen(aParams->mLabel) >= LABEL_MAX_CAPACITY) {
        for (int i = 0; i < 3; i++) {
          mLabel[LABEL_MAX_CAPACITY - 2 - i] = '.';
        }
      }
    }

    if (aParams->mChunkAllocator) {
      MOZ_ASSERT(aIsPrivate);
      mChunkAllocator = aParams->mChunkAllocator;
    }
  }

  MOZ_RELEASE_ASSERT(mLock.Init(doLock));

  UpdateMaxDirty();

  // Initialize bins.
  SizeClass sizeClass(1);

  unsigned i;
  for (i = 0;; i++) {
    new (&mBins[i]) arena_bin_t(sizeClass);

    // SizeClass doesn't want sizes larger than gMaxBinClass for now.
    if (sizeClass.Size() == gMaxBinClass) {
      break;
    }
    sizeClass = sizeClass.Next();
  }
  MOZ_ASSERT(i == NUM_SMALL_CLASSES - 1);
}

arena_t::~arena_t() {
  size_t i;
  MaybeMutexAutoLock lock(mLock);

  MOZ_RELEASE_ASSERT(!mLink.Left() && !mLink.Right(),
                     "Arena is still registered");
  MOZ_RELEASE_ASSERT(!mStats.allocated_small && !mStats.allocated_large,
                     "Arena is not empty");
  while (!mSpares.isEmpty()) {
    arena_chunk_t* spare = mSpares.popFront();
    arena_chunk_dealloc(mChunkAllocator, spare, kChunkSize);
  }
  for (i = 0; i < NUM_SMALL_CLASSES; i++) {
    MOZ_RELEASE_ASSERT(mBins[i].mNonFullRuns.isEmpty(), "Bin is not empty");
  }
#ifdef MOZ_DEBUG
  {
    MutexAutoLock lock(huge_mtx);
    // This is an expensive check, so we only do it on debug builds.
    for (auto node : huge.iter()) {
      MOZ_RELEASE_ASSERT(node->mArenaId != mId, "Arena has huge allocations");
    }
  }
#endif
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mMagic = 0;
#endif
  mId = 0;
}

arena_t* ArenaCollection::CreateArena(bool aIsPrivate,
                                      arena_params_t* aParams) {
  // Allocate the memory for the arena before taking any locks, since it
  // will use the base allocator locks and could run a system call.
  arena_t* ret = new (fallible) arena_t(aParams, aIsPrivate);
  if (!ret) {
    // Only reached if there is an OOM error.

    // OOM here is quite inconvenient to propagate, since dealing with it
    // would require a check for failure in the fast path.  Instead, punt
    // by using the first arena.
    // In practice, this is an extremely unlikely failure.
    _malloc_message(_getprogname(), ": (malloc) Error initializing arena\n");

    return mDefaultArena;
  }

  MutexAutoLock lock(mLock);

  // Updating the arena's mIsDeferredPurgeEnabled needs to happen in the
  // same critical section as enrolling the arena in the collection, which
  // is why it's set here and not by arena_t's constructor.
  {
    // The arena lock here isn't necessary because nothing else has a
    // pointer to the arena yet, but the alternative is
    // MOZ_PUSH_IGNORE_THREAD_SAFETY.
    MaybeMutexAutoLock arena_lock(ret->mLock);
    ret->mIsDeferredPurgeEnabled = mIsDeferredPurgeEnabled;
  }

  // For public arenas, it's fine to just use incrementing arena id
  if (!aIsPrivate) {
    ret->mId = mLastPublicArenaId++;
    mArenas.Insert(ret);
    return ret;
  }

  // For private arenas, slightly obfuscate the id by XORing a key generated
  // once, and rotate the bits by an amount also generated once.
  if (mArenaIdKey == 0) {
    mozilla::Maybe<uint64_t> maybeRandom = mozilla::RandomUint64();
    MOZ_RELEASE_ASSERT(maybeRandom.isSome());
    mArenaIdKey = maybeRandom.value();
    maybeRandom = mozilla::RandomUint64();
    MOZ_RELEASE_ASSERT(maybeRandom.isSome());
    mArenaIdRotation = maybeRandom.value() & (sizeof(void*) * 8 - 1);
  }
  arena_id_t id = reinterpret_cast<arena_id_t>(ret) ^ mArenaIdKey;
  ret->mId =
      (id >> mArenaIdRotation) | (id << (sizeof(void*) * 8 - mArenaIdRotation));
  mPrivateArenas.Insert(ret);
  return ret;
}

// End arena.
// ***************************************************************************
// Begin general internal functions.

// Initialize huge allocation data.
static void huge_init() MOZ_REQUIRES(gInitLock) {
  huge_mtx.Init();
  MOZ_PUSH_IGNORE_THREAD_SAFETY
  huge_allocated = 0;
  huge_mapped = 0;
  huge_operations = 0;
  MOZ_POP_THREAD_SAFETY
}

void* arena_t::MallocHuge(size_t aSize, bool aZero) {
  return PallocHuge(aSize, kChunkSize, aZero);
}

void* arena_t::PallocHuge(size_t aSize, size_t aAlignment, bool aZero) {
  void* ret;
  size_t csize;
  size_t psize;
  extent_node_t* node;

  // We're going to configure guard pages in the region between the
  // page-aligned size and the chunk-aligned size, so if those are the same
  // then we need to force that region into existence.
  csize = CHUNK_CEILING(aSize + gRealPageSize);
  if (csize < aSize) {
    // size is large enough to cause size_t wrap-around.
    return nullptr;
  }

  // Allocate an extent node with which to track the chunk.
  node = new (fallible) extent_node_t();
  if (!node) {
    return nullptr;
  }

  // Allocate one or more contiguous chunks for this request.
  ret = arena_chunk_alloc(mChunkAllocator, csize, aAlignment);
  if (!ret) {
    delete node;
    return nullptr;
  }
  psize = REAL_PAGE_CEILING(aSize);
  MOZ_ASSERT(psize < csize);
#ifdef MOZ_DEBUG
  if (aZero) {
    chunk_assert_zero(ret, psize);
  }
#endif

  // Insert node into huge.
  node->mAddr = ret;
  node->mSize = psize;
  node->mArena = this;
  node->mArenaId = mId;

  {
    MutexAutoLock lock(huge_mtx);
    huge.Insert(node);

    // Although we allocated space for csize bytes, we indicate that we've
    // allocated only psize bytes.
    //
    // If DECOMMIT is defined, this is a reasonable thing to do, since
    // we'll explicitly decommit the bytes in excess of psize.
    //
    // If DECOMMIT is not defined, then we're relying on the OS to be lazy
    // about how it allocates physical pages to mappings.  If we never
    // touch the pages in excess of psize, the OS won't allocate a physical
    // page, and we won't use more than psize bytes of physical memory.
    //
    // A correct program will only touch memory in excess of how much it
    // requested if it first calls malloc_usable_size and finds out how
    // much space it has to play with.  But because we set node->mSize =
    // psize above, malloc_usable_size will return psize, not csize, and
    // the program will (hopefully) never touch bytes in excess of psize.
    // Thus those bytes won't take up space in physical memory, and we can
    // reasonably claim we never "allocated" them in the first place.
    huge_allocated += psize;
    huge_mapped += csize;
    huge_operations++;
  }

  pages_decommit((void*)((uintptr_t)ret + psize), csize - psize);

  if (!aZero) {
    ApplyZeroOrJunk(ret, psize);
  }

  return ret;
}

void* arena_t::RallocHuge(void* aPtr, size_t aSize, size_t aOldSize) {
  void* ret;
  size_t copysize;

  // Avoid moving the allocation if the size class would not change.
  if (aOldSize > gMaxLargeClass &&
      CHUNK_CEILING(aSize + gRealPageSize) ==
          CHUNK_CEILING(aOldSize + gRealPageSize)) {
    size_t psize = REAL_PAGE_CEILING(aSize);
    if (aSize < aOldSize) {
      MaybePoison((void*)((uintptr_t)aPtr + aSize), aOldSize - aSize);
    }
    if (psize < aOldSize) {
      pages_decommit((void*)((uintptr_t)aPtr + psize), aOldSize - psize);

      // Update recorded size.
      MutexAutoLock lock(huge_mtx);
      extent_node_t* node = huge.Search(aPtr);
      MOZ_ASSERT(node);
      MOZ_ASSERT(node->mSize == aOldSize);
      MOZ_RELEASE_ASSERT(node->mArena == this);
      huge_allocated -= aOldSize - psize;
      huge_operations++;
      // No need to change huge_mapped, because we didn't (un)map anything.
      node->mSize = psize;
    } else if (psize > aOldSize) {
      if (!pages_commit((void*)((uintptr_t)aPtr + aOldSize),
                        psize - aOldSize)) {
        return nullptr;
      }

      // We need to update the recorded size if the size increased,
      // so malloc_usable_size doesn't return a value smaller than
      // what was requested via realloc().
      MutexAutoLock lock(huge_mtx);
      extent_node_t* node = huge.Search(aPtr);
      MOZ_ASSERT(node);
      MOZ_ASSERT(node->mSize == aOldSize);
      MOZ_RELEASE_ASSERT(node->mArena == this);
      huge_allocated += psize - aOldSize;
      huge_operations++;
      // No need to change huge_mapped, because we didn't
      // (un)map anything.
      node->mSize = psize;
    }

    if (aSize > aOldSize) {
      ApplyZeroOrJunk((void*)((uintptr_t)aPtr + aOldSize), aSize - aOldSize);
    }
    return aPtr;
  }

  // If we get here, then aSize and aOldSize are different enough that we
  // need to use a different size class.  In that case, fall back to allocating
  // new space and copying. Allow non-private arenas to switch arenas.
  ret = (mIsPrivate ? this : choose_arena(aSize))->MallocHuge(aSize, false);
  if (!ret) {
    return nullptr;
  }

  copysize = (aSize < aOldSize) ? aSize : aOldSize;
#ifdef VM_COPY_MIN
  if (copysize >= VM_COPY_MIN) {
    pages_copy(ret, aPtr, copysize);
  } else
#endif
  {
    memcpy(ret, aPtr, copysize);
  }
  idalloc(aPtr, this);
  return ret;
}

static void huge_dalloc(void* aPtr, arena_t* aArena) {
  extent_node_t* node;
  size_t mapped = 0;
  {
    MutexAutoLock lock(huge_mtx);

    // Extract from tree of huge allocations.
    node = huge.Search(aPtr);
    MOZ_RELEASE_ASSERT(node, "Double-free?");
    MOZ_ASSERT(node->mAddr == aPtr);
    MOZ_RELEASE_ASSERT(!aArena || node->mArena == aArena);
    // See AllocInfo::Arena.
    MOZ_RELEASE_ASSERT(node->mArenaId == node->mArena->mId);
    huge.Remove(node);

    mapped = CHUNK_CEILING(node->mSize + gRealPageSize);
    huge_allocated -= node->mSize;
    huge_mapped -= mapped;
    huge_operations++;
  }

  // Unmap chunk.
  arena_chunk_dealloc(node->mArena->mChunkAllocator, node->mAddr, mapped);

  delete node;
}

// Returns whether the allocator was successfully initialized.
static bool malloc_init_hard() {
  unsigned i;
  const char* opts;

  AutoLock<StaticMutex> lock(gInitLock);

  if (malloc_initialized) {
    // Another thread initialized the allocator before this one
    // acquired gInitLock.
    return true;
  }

  if (!thread_arena.init()) {
    return true;
  }

  // Get page size and number of CPUs
  const size_t page_size = GetKernelPageSize();
  // We assume that the page size is a power of 2.
  MOZ_ASSERT(std::has_single_bit(page_size));
#ifdef MALLOC_STATIC_PAGESIZE
  if (gRealPageSize % page_size) {
    _malloc_message(
        _getprogname(),
        "Compile-time page size does not divide the runtime one.\n");
    MOZ_CRASH();
  }
#else
  gRealPageSize = page_size;
  gPageSize = page_size;
#endif

  opt_dirty_max = DIRTY_MAX_DEFAULT_BYTES / gPageSize;

  // Get runtime configuration.
  if ((opts = getenv("MALLOC_OPTIONS"))) {
    for (i = 0; opts[i] != '\0'; i++) {
      // All options are single letters, some take a *prefix* numeric argument.

      // Parse the argument.
      unsigned prefix_arg = 0;
      while (opts[i] >= '0' && opts[i] <= '9') {
        prefix_arg *= 10;
        prefix_arg += opts[i] - '0';
        i++;
      }

      switch (opts[i]) {
        case 'f':
          opt_dirty_max >>= prefix_arg ? prefix_arg : 1;
          break;
        case 'F':
          prefix_arg = prefix_arg ? prefix_arg : 1;
          if (opt_dirty_max == 0) {
            opt_dirty_max = 1;
            prefix_arg--;
          }
          opt_dirty_max <<= prefix_arg;
          if (opt_dirty_max == 0) {
            // If the shift above overflowed all the bits then clamp the result
            // instead.  If we started with the default then this will always
            // be a power of two so choose the maximum power of two that fits
            // in a size_t.
            opt_dirty_max = size_t(1) << (sizeof(size_t) * CHAR_BIT - 1);
          }
          break;
#ifdef MALLOC_RUNTIME_CONFIG
        case 'j':
          opt_junk = false;
          break;
        case 'J':
          opt_junk = true;
          break;
        case 'q':
          // The argument selects how much poisoning to do.
          opt_poison = NONE;
          break;
        case 'Q':
          if (opts[i + 1] == 'Q') {
            // Maximum poisoning.
            i++;
            opt_poison = ALL;
          } else {
            opt_poison = SOME;
            opt_poison_size = kCacheLineSize * prefix_arg;
          }
          break;
        case 'z':
          opt_zero = false;
          break;
        case 'Z':
          opt_zero = true;
          break;
#  ifndef MALLOC_STATIC_PAGESIZE
        case 'P':
          MOZ_ASSERT(gPageSize >= 1_KiB);
          MOZ_ASSERT(gPageSize <= 64_KiB);
          prefix_arg = prefix_arg ? prefix_arg : 1;
          gPageSize <<= prefix_arg;
          // We know that if the shift causes gPageSize to be zero then it's
          // because it shifted all the bits off.  We didn't start with zero.
          // Therefore if gPageSize is out of bounds we set it to 64KiB.
          if (gPageSize < 1_KiB || gPageSize > 64_KiB) {
            gPageSize = 64_KiB;
          }
          // We also limit gPageSize to be no larger than gRealPageSize, there's
          // no reason to support this.
          if (gPageSize > gRealPageSize) {
            gPageSize = gRealPageSize;
          }
          break;
        case 'p':
          MOZ_ASSERT(gPageSize >= 1_KiB);
          MOZ_ASSERT(gPageSize <= 64_KiB);
          prefix_arg = prefix_arg ? prefix_arg : 1;
          gPageSize >>= prefix_arg;
          if (gPageSize < 1_KiB) {
            gPageSize = 1_KiB;
          }
          break;
#  endif
#endif
        case 'r':
          opt_randomize_small = false;
          break;
        case 'R':
          opt_randomize_small = true;
          break;
        default: {
          char cbuf[2];

          cbuf[0] = opts[i];
          cbuf[1] = '\0';
          _malloc_message(_getprogname(),
                          ": (malloc) Unsupported character "
                          "in malloc options: '",
                          cbuf, "'\n");
        }
      }
    }
  }

  MOZ_ASSERT(gPageSize <= gRealPageSize);
#ifndef MALLOC_STATIC_PAGESIZE
  DefineGlobals();
#endif

#ifndef XP_WIN
  gCache.Init();
#endif

  huge_init();
  sBaseAlloc.Init();

  // Initialize arenas collection here.
  if (!gArenas.Init()) {
    return false;
  }

  // Assign the default arena to the initial thread.
  thread_arena.set(gArenas.GetDefault());

  if (!gChunkRTree.Init()) {
    return false;
  }

  malloc_initialized = true;

  // Dummy call so that the function is not removed by dead-code elimination
  Debug::jemalloc_ptr_info(nullptr);

#if !defined(XP_WIN) && !defined(XP_DARWIN)
  // Prevent potential deadlock on malloc locks after fork.
  pthread_atfork(_malloc_prefork, _malloc_postfork_parent,
                 _malloc_postfork_child);
#endif

#ifdef MOZ_PHC
  // PHC must be initialised after mozjemalloc.
  phc_init();
#endif

  return true;
}

// End general internal functions.
// ***************************************************************************
// Begin malloc(3)-compatible functions.

// The BaseAllocator class is a helper class that implements the base allocator
// functions (malloc, calloc, realloc, free, memalign) for a given arena,
// or an appropriately chosen arena (per choose_arena()) when none is given.
struct BaseAllocator {
#define MALLOC_DECL(name, return_type, ...) \
  inline return_type name(__VA_ARGS__);

#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"

  explicit BaseAllocator(arena_t* aArena) : mArena(aArena) {}

 private:
  arena_t* mArena;
};

#define MALLOC_DECL(name, return_type, ...)                  \
  inline return_type MozJemalloc::name(                      \
      ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) {              \
    BaseAllocator allocator(nullptr);                        \
    return allocator.name(ARGS_HELPER(ARGS, ##__VA_ARGS__)); \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"

inline void* BaseAllocator::malloc(size_t aSize) {
  void* ret;
  arena_t* arena;

  if (!malloc_init()) {
    ret = nullptr;
    goto RETURN;
  }

  if (aSize == 0) {
    aSize = 1;
  }
  // If mArena is non-null, it must not be in the first page.
  MOZ_DIAGNOSTIC_ASSERT_IF(mArena, (size_t)mArena >= gPageSize);
  arena = mArena ? mArena : choose_arena(aSize);
  ret = arena->Malloc(aSize, /* aZero = */ false);

RETURN:
  if (!ret) {
    errno = ENOMEM;
  }

  return ret;
}

inline void* BaseAllocator::memalign(size_t aAlignment, size_t aSize) {
  MOZ_ASSERT(((aAlignment - 1) & aAlignment) == 0);

  if (!malloc_init()) {
    return nullptr;
  }

  if (aSize == 0) {
    aSize = 1;
  }

  aAlignment = aAlignment < sizeof(void*) ? sizeof(void*) : aAlignment;
  arena_t* arena = mArena ? mArena : choose_arena(aSize);
  return arena->Palloc(aAlignment, aSize);
}

inline void* BaseAllocator::calloc(size_t aNum, size_t aSize) {
  void* ret;

  if (malloc_init()) {
    CheckedInt<size_t> checkedSize = CheckedInt<size_t>(aNum) * aSize;
    if (checkedSize.isValid()) {
      size_t allocSize = checkedSize.value();
      if (allocSize == 0) {
        allocSize = 1;
      }
      arena_t* arena = mArena ? mArena : choose_arena(allocSize);
      ret = arena->Malloc(allocSize, /* aZero = */ true);
    } else {
      ret = nullptr;
    }
  } else {
    ret = nullptr;
  }

  if (!ret) {
    errno = ENOMEM;
  }

  return ret;
}

inline void* BaseAllocator::realloc(void* aPtr, size_t aSize) {
  void* ret;

  if (aSize == 0) {
    aSize = 1;
  }

  if (aPtr) {
    MOZ_RELEASE_ASSERT(malloc_initialized);

    auto info = AllocInfo::Get(aPtr);
    auto arena = info.Arena();
    MOZ_RELEASE_ASSERT(!mArena || arena == mArena);
    ret = arena->Ralloc(aPtr, aSize, info.Size());
  } else {
    if (!malloc_init()) {
      ret = nullptr;
    } else {
      arena_t* arena = mArena ? mArena : choose_arena(aSize);
      ret = arena->Malloc(aSize, /* aZero = */ false);
    }
  }

  if (!ret) {
    errno = ENOMEM;
  }
  return ret;
}

inline void BaseAllocator::free(void* aPtr) {
  size_t offset;

  // A version of idalloc that checks for nullptr pointer.
  offset = GetChunkOffsetForPtr(aPtr);
  if (offset != 0) {
    MOZ_RELEASE_ASSERT(malloc_initialized);
    arena_dalloc(aPtr, offset, mArena);
  } else if (aPtr) {
    MOZ_RELEASE_ASSERT(malloc_initialized);
    huge_dalloc(aPtr, mArena);
  }
}

inline int MozJemalloc::posix_memalign(void** aMemPtr, size_t aAlignment,
                                       size_t aSize) {
  return AlignedAllocator<memalign>::posix_memalign(aMemPtr, aAlignment, aSize);
}

inline void* MozJemalloc::aligned_alloc(size_t aAlignment, size_t aSize) {
  return AlignedAllocator<memalign>::aligned_alloc(aAlignment, aSize);
}

inline void* MozJemalloc::valloc(size_t aSize) {
  return AlignedAllocator<memalign>::valloc(aSize);
}

// End malloc(3)-compatible functions.
// ***************************************************************************
// Begin non-standard functions.

// This was added by Mozilla for use by SQLite.
inline size_t MozJemalloc::malloc_good_size(size_t aSize) {
  if (aSize == 0) {
    aSize = SizeClass(1).Size();
  } else if (aSize <= gMaxLargeClass) {
    // Small or large
    aSize = SizeClass(aSize).Size();
  } else {
    // Huge.  We use PAGE_CEILING to get psize, instead of using
    // CHUNK_CEILING to get csize.  This ensures that this
    // malloc_usable_size(malloc(n)) always matches
    // malloc_good_size(n).
    aSize = REAL_PAGE_CEILING(aSize);
  }
  return aSize;
}

inline size_t MozJemalloc::malloc_usable_size(usable_ptr_t aPtr) {
  return AllocInfo::GetValidated(aPtr).Size();
}

inline void MozJemalloc::jemalloc_stats_internal(
    jemalloc_stats_t* aStats, jemalloc_bin_stats_t* aBinStats) {
  size_t non_arena_mapped, chunk_header_size;

  if (!aStats) {
    return;
  }
  if (!malloc_init()) {
    memset(aStats, 0, sizeof(*aStats));
    return;
  }
  if (aBinStats) {
    memset(aBinStats, 0, sizeof(jemalloc_bin_stats_t) * NUM_SMALL_CLASSES);
  }

  // Gather runtime settings.
  aStats->opt_junk = opt_junk;
  aStats->opt_randomize_small = opt_randomize_small;
  aStats->opt_zero = opt_zero;
  aStats->quantum = kQuantum;
  aStats->quantum_max = kMaxQuantumClass;
  aStats->quantum_wide = kQuantumWide;
  aStats->quantum_wide_max = kMaxQuantumWideClass;
  aStats->subpage_max = gMaxSubPageClass;
  aStats->large_max = gMaxLargeClass;
  aStats->chunksize = kChunkSize;
  aStats->page_size = gPageSize;
  aStats->real_page_size = gRealPageSize;
  aStats->dirty_max = opt_dirty_max;
  aStats->arena_run_header = offsetof(arena_run_t, mRegionsMask);

  // Gather current memory usage statistics.
  aStats->narenas = 0;
  aStats->mapped = 0;
  aStats->allocated = 0;
  aStats->waste = 0;
  aStats->pages_dirty = 0;
  aStats->pages_fresh = 0;
  aStats->pages_madvised = 0;
  aStats->bookkeeping = 0;
  aStats->bin_unused = 0;

  non_arena_mapped = 0;

  // Get huge mapped/allocated.
  {
    MutexAutoLock lock(huge_mtx);
    non_arena_mapped += huge_mapped;
    aStats->allocated += huge_allocated;
    aStats->num_operations += huge_operations;
    MOZ_ASSERT(huge_mapped >= huge_allocated);
  }

  // Get base mapped/allocated.
  auto base_stats = sBaseAlloc.GetStats();
  non_arena_mapped += base_stats.mMapped;
  aStats->bookkeeping += base_stats.mCommitted;

  gArenas.mLock.Lock();

  // Stats can only read complete information if its run on the main thread.
  MOZ_ASSERT(gArenas.IsOnMainThreadWeak());

  // Iterate over arenas.
  for (auto arena : gArenas.iter()) {
    // Cannot safely read stats for this arena and therefore stats would be
    // incomplete.
    MOZ_ASSERT(arena->mLock.SafeOnThisThread());

    size_t arena_mapped, arena_allocated, arena_committed, arena_dirty,
        arena_fresh, arena_madvised, j, arena_unused, arena_headers;

    arena_headers = 0;
    arena_unused = 0;

    {
      MaybeMutexAutoLock lock(arena->mLock);

      arena_mapped = arena->mStats.mapped;

      // "committed" counts dirty and allocated memory.
      arena_committed = arena->mStats.committed << gPageSize2Pow;

      arena_allocated =
          arena->mStats.allocated_small + arena->mStats.allocated_large;

      arena_dirty = arena->mNumDirty << gPageSize2Pow;
      arena_fresh = arena->mNumFresh << gPageSize2Pow;
      arena_madvised = arena->mNumMAdvised << gPageSize2Pow;

      aStats->num_operations += arena->mStats.operations;

      for (j = 0; j < NUM_SMALL_CLASSES; j++) {
        arena_bin_t* bin = &arena->mBins[j];
        size_t bin_unused = 0;
        size_t num_non_full_runs = 0;

        for (arena_run_t& run : bin->mNonFullRuns) {
          MOZ_DIAGNOSTIC_ASSERT(run.mMagic == ARENA_RUN_MAGIC);
          MOZ_RELEASE_ASSERT(run.mNumFree > 0 &&
                             run.mNumFree < bin->mRunNumRegions);
          MOZ_RELEASE_ASSERT(run.mBin == bin);
          MOZ_RELEASE_ASSERT(bin->mNonFullRuns.ElementIsLinkedWell(&run));
          arena_chunk_t* chunk = GetChunkForPtr(&run);
          MOZ_RELEASE_ASSERT(chunk->mArena == arena);
          bin_unused += run.mNumFree * bin->mSizeClass;
          num_non_full_runs++;
        }

        arena_unused += bin_unused;
        arena_headers += bin->mNumRuns * bin->mRunFirstRegionOffset;
        if (aBinStats) {
          aBinStats[j].size = bin->mSizeClass;
          aBinStats[j].num_non_full_runs += num_non_full_runs;
          aBinStats[j].num_runs += bin->mNumRuns;
          aBinStats[j].bytes_unused += bin_unused;
          size_t bytes_per_run = static_cast<size_t>(bin->mRunSizePages)
                                 << gPageSize2Pow;
          aBinStats[j].bytes_total +=
              bin->mNumRuns * (bytes_per_run - bin->mRunFirstRegionOffset);
          aBinStats[j].bytes_per_run = bytes_per_run;
          aBinStats[j].regions_per_run = bin->mRunNumRegions;
        }
      }
    }

    MOZ_ASSERT(arena_mapped >= arena_committed);
    MOZ_ASSERT(arena_committed >= arena_allocated + arena_dirty);

    aStats->mapped += arena_mapped;
    aStats->allocated += arena_allocated;
    aStats->pages_dirty += arena_dirty;
    aStats->pages_fresh += arena_fresh;
    aStats->pages_madvised += arena_madvised;
    // "waste" is committed memory that is neither dirty nor
    // allocated.  If you change this definition please update
    // memory/replace/logalloc/replay/Replay.cpp's jemalloc_stats calculation of
    // committed.
    MOZ_ASSERT(arena_committed >=
               (arena_allocated + arena_dirty + arena_unused + arena_headers));
    aStats->waste += arena_committed - arena_allocated - arena_dirty -
                     arena_unused - arena_headers;
    aStats->bin_unused += arena_unused;
    aStats->bookkeeping += arena_headers;
    aStats->narenas++;
  }
  gArenas.mLock.Unlock();

  // Account for arena chunk headers in bookkeeping rather than waste.
  chunk_header_size =
      ((aStats->mapped / aStats->chunksize) * (gChunkHeaderNumPages - 1))
      << gPageSize2Pow;

  aStats->mapped += non_arena_mapped;
  aStats->bookkeeping += chunk_header_size;
  aStats->waste -= chunk_header_size;

  MOZ_ASSERT(aStats->mapped >= aStats->allocated + aStats->waste +
                                   aStats->pages_dirty + aStats->bookkeeping);
}

inline void MozJemalloc::jemalloc_stats_lite(jemalloc_stats_lite_t* aStats) {
  if (!aStats) {
    return;
  }
  if (!malloc_init()) {
    memset(aStats, 0, sizeof(*aStats));
    return;
  }

  aStats->allocated_bytes = 0;
  aStats->num_operations = 0;

  // Get huge mapped/allocated.
  {
    MutexAutoLock lock(huge_mtx);
    aStats->allocated_bytes += huge_allocated;
    aStats->num_operations += huge_operations;
    MOZ_ASSERT(huge_mapped >= huge_allocated);
  }

  {
    MutexAutoLock lock(gArenas.mLock);
    for (auto arena : gArenas.iter()) {
      // We don't need to lock the arena to access these fields.
      aStats->allocated_bytes += arena->AllocatedBytes();
      aStats->num_operations += arena->Operations();
    }
    aStats->num_operations += gArenas.OperationsDisposedArenas();
  }
}

inline size_t MozJemalloc::jemalloc_stats_num_bins() {
  return NUM_SMALL_CLASSES;
}

inline void MozJemalloc::jemalloc_set_main_thread() {
  MOZ_ASSERT(malloc_initialized);
  gArenas.SetMainThread();
}

#ifdef MALLOC_DOUBLE_PURGE

// Explicitly remove all of this chunk's MADV_FREE'd pages from memory.
static size_t hard_purge_chunk(arena_chunk_t* aChunk) {
  size_t total_npages = 0;
  // See similar logic in arena_t::Purge().
  for (size_t i = gChunkHeaderNumPages; i < gChunkNumPages; i++) {
    // Find all adjacent pages with CHUNK_MAP_MADVISED set.
    size_t npages;
    for (npages = 0; aChunk->mPageMap[i + npages].bits & CHUNK_MAP_MADVISED &&
                     i + npages < gChunkNumPages;
         npages++) {
      // Turn off the page's CHUNK_MAP_MADVISED bit and turn on its
      // CHUNK_MAP_FRESH bit.
      MOZ_DIAGNOSTIC_ASSERT(!(aChunk->mPageMap[i + npages].bits &
                              (CHUNK_MAP_FRESH | CHUNK_MAP_DECOMMITTED)));
      aChunk->mPageMap[i + npages].bits ^=
          (CHUNK_MAP_MADVISED | CHUNK_MAP_FRESH);
    }

    // We could use mincore to find out which pages are actually
    // present, but it's not clear that's better.
    if (npages > 0) {
      // i and npages should be aligned because they needed to be for the
      // purge code that set CHUNK_MAP_MADVISED.
      MOZ_ASSERT((i % gPagesPerRealPage) == 0);
      MOZ_ASSERT((npages % gPagesPerRealPage) == 0);
      pages_decommit(((char*)aChunk) + (i << gPageSize2Pow),
                     npages << gPageSize2Pow);
      (void)pages_commit(((char*)aChunk) + (i << gPageSize2Pow),
                         npages << gPageSize2Pow);
    }
    total_npages += npages;
    i += npages;
  }

  return total_npages;
}

// Explicitly remove all of this arena's MADV_FREE'd pages from memory.
void arena_t::HardPurge() {
  MaybeMutexAutoLock lock(mLock);

  while (!mChunksMAdvised.isEmpty()) {
    arena_chunk_t* chunk = mChunksMAdvised.popFront();
    size_t npages = hard_purge_chunk(chunk);
    mNumMAdvised -= npages;
    mNumFresh += npages;
  }
}

inline void MozJemalloc::jemalloc_purge_freed_pages() {
  if (malloc_initialized) {
    MutexAutoLock lock(gArenas.mLock);
    MOZ_ASSERT(gArenas.IsOnMainThreadWeak());
    for (auto arena : gArenas.iter()) {
      arena->HardPurge();
    }
  }
}

#else  // !defined MALLOC_DOUBLE_PURGE

inline void MozJemalloc::jemalloc_purge_freed_pages() {
  // Do nothing.
}

#endif  // defined MALLOC_DOUBLE_PURGE

inline void MozJemalloc::jemalloc_free_dirty_pages(void) {
  if (malloc_initialized) {
    gArenas.MayPurgeAll(PurgeUnconditional, __func__);
  }
}

inline void MozJemalloc::jemalloc_free_excess_dirty_pages(void) {
  if (malloc_initialized) {
    gArenas.MayPurgeAll(PurgeIfThreshold, __func__);
  }
}

inline arena_t* ArenaCollection::GetById(arena_id_t aArenaId, bool aIsPrivate) {
  if (!malloc_initialized) {
    return nullptr;
  }

  // This function is never called with aIsPrivate = false, let's make sure it
  // doesn't silently change while we're making that assumption below because
  // we can't resolve non-private arenas this way.
  MOZ_RELEASE_ASSERT(aIsPrivate);
  // This function is not expected to be called before at least one private
  // arena was created.
  // coverity[missing_lock]
  MOZ_RELEASE_ASSERT(mArenaIdKey);
  arena_id_t id = (aArenaId << mArenaIdRotation) |
                  (aArenaId >> (sizeof(void*) * 8 - mArenaIdRotation));
  arena_t* result = reinterpret_cast<arena_t*>(id ^ mArenaIdKey);
  MOZ_RELEASE_ASSERT(result);
  MOZ_DIAGNOSTIC_ASSERT(result->mMagic == ARENA_MAGIC);
  MOZ_RELEASE_ASSERT(result->mId == aArenaId);
  return result;
}

inline arena_id_t MozJemalloc::moz_create_arena_with_params(
    arena_params_t* aParams) {
  if (malloc_init()) {
    arena_t* arena = gArenas.CreateArena(/* IsPrivate = */ true, aParams);
    return arena->mId;
  }
  return 0;
}

inline void MozJemalloc::moz_dispose_arena(arena_id_t aArenaId) {
  arena_t* arena = gArenas.GetById(aArenaId, /* IsPrivate = */ true);
  gArenas.DisposeArena(arena);
}

inline void MozJemalloc::moz_set_max_dirty_page_modifier(int32_t aModifier) {
  if (malloc_init()) {
    gArenas.SetDefaultMaxDirtyPageModifier(aModifier);
  }
}

inline void MozJemalloc::jemalloc_reset_small_alloc_randomization(
    bool aRandomizeSmall) {
  // When this process got forked by ForkServer then it inherited the existing
  // state of mozjemalloc. Specifically, parsing of MALLOC_OPTIONS has already
  // been done but it may not reflect anymore the current set of options after
  // the fork().
  //
  // Similar behavior is also present on Android where it is also required to
  // perform this step.
  //
  // Content process will have randomization on small malloc disabled via the
  // MALLOC_OPTIONS environment variable set by parent process, missing this
  // will lead to serious performance regressions because CPU prefetch will
  // break, cf bug 1912262.  However on forkserver-forked Content processes, the
  // environment is not yet reset when the postfork child handler is being
  // called.
  //
  // This API is here to allow those Content processes (spawned by ForkServer or
  // Android service) to notify jemalloc to turn off the randomization on small
  // allocations and perform the required reinitialization of already existing
  // arena's PRNG.  It is important to make sure that the PRNG state is properly
  // re-initialized otherwise child processes would share all the same state.

  {
    AutoLock<StaticMutex> lock(gInitLock);
    opt_randomize_small = aRandomizeSmall;
  }

  MutexAutoLock lock(gArenas.mLock);
  for (auto* arena : gArenas.iter()) {
    // We can only initialize the PRNG for main-thread-only arenas from the main
    // thread.
    if (!arena->IsMainThreadOnly() || gArenas.IsOnMainThreadWeak()) {
      arena->ResetSmallAllocRandomization();
    }
  }
}

inline bool MozJemalloc::moz_enable_deferred_purge(bool aEnabled) {
  return gArenas.SetDeferredPurge(aEnabled);
}

inline may_purge_now_result_t MozJemalloc::moz_may_purge_now(
    bool aPeekOnly, uint32_t aReuseGraceMS, purge_keep_going_t aKeepGoing,
    void* aClosure) {
  return gArenas.MayPurgeSteps(aPeekOnly, aReuseGraceMS, aKeepGoing, aClosure);
}

inline void ArenaCollection::AddToOutstandingPurges(arena_t* aArena) {
  MOZ_ASSERT(aArena);

  // We cannot trust the caller to know whether the element was already added
  // from another thread given we have our own lock.
  MutexAutoLock lock(mPurgeListLock);
  if (!mOutstandingPurges.ElementProbablyInList(aArena)) {
    mOutstandingPurges.pushBack(aArena);
  }
}

inline bool ArenaCollection::RemoveFromOutstandingPurges(arena_t* aArena) {
  MOZ_ASSERT(aArena);

  // We cannot trust the caller to know whether the element was already removed
  // from another thread given we have our own lock.
  MutexAutoLock lock(mPurgeListLock);
  if (mOutstandingPurges.ElementProbablyInList(aArena)) {
    mOutstandingPurges.remove(aArena);
    return true;
  }
  return false;
}

may_purge_now_result_t ArenaCollection::MayPurgeSteps(
    bool aPeekOnly, uint32_t aReuseGraceMS, purge_keep_going_t aKeepGoing,
    void* aClosure) {
  // This only works on the main thread because it may process main-thread-only
  // arenas.
  MOZ_ASSERT(IsOnMainThreadWeak());

  uint64_t now = GetTimestampNS();
  uint64_t reuseGraceNS = (uint64_t)aReuseGraceMS * 1000 * 1000;
  arena_t* found = nullptr;
  {
    MutexAutoLock lock(mPurgeListLock);
    if (mOutstandingPurges.isEmpty()) {
      return may_purge_now_result_t::Done;
    }
    for (arena_t& arena : mOutstandingPurges) {
      if (now - arena.mLastPageReuseNS >= reuseGraceNS) {
        found = &arena;
        break;
      }
    }

    if (!found) {
      return may_purge_now_result_t::WantsLater;
    }
    if (aPeekOnly) {
      return may_purge_now_result_t::NeedsMore;
    }

    // We need to avoid the invalid state where mIsPurgePending is set
    // but the arena is not in the list or about to be added. So remove the
    // arena from the list before calling Purge().
    mOutstandingPurges.remove(found);
  }

  ArenaPurgeResult pr = found->PurgeLoop(PurgeIfThreshold, __func__,
                                         aReuseGraceMS, aKeepGoing, aClosure);

  if (pr == ArenaPurgeResult::NotDone) {
    // If there's more work to do we re-insert the arena into the purge queue.
    // If the arena was busy we don't since the other thread that's purging it
    // will finish that work.

    // Note that after the above Purge() and taking the lock below there's a
    // chance another thread may be purging the arena and clear
    // mIsPurgePending.  Resulting in the state of being in the list
    // with that flag clear.  That's okay since the next time a purge occurs
    // (and one will because it's in the list) it'll clear the flag and the
    // state will be consistent again.
    MutexAutoLock lock(mPurgeListLock);
    if (!mOutstandingPurges.ElementProbablyInList(found)) {
      // Given we want to continue to purge this arena, push it to the front
      // to increase the probability to find it fast.
      mOutstandingPurges.pushFront(found);
    }
  } else if (pr == ArenaPurgeResult::Dying) {
    delete found;
  }

  // Even if there is no other arena that needs work, let the caller just call
  // us again and we will do the above checks then and return their result.
  // Note that in the current surrounding setting this may (rarely) cause a
  // new slice of the lazy purge task if it has to yield.
  return may_purge_now_result_t::NeedsMore;
}

void ArenaCollection::MayPurgeAll(PurgeCondition aCond, const char* aCaller) {
  MutexAutoLock lock(mLock);
  for (auto* arena : iter()) {
    // Arenas that are not IsMainThreadOnly can be purged from any thread.
    // So we do what we can even if called from another thread.
    if (!arena->IsMainThreadOnly() || IsOnMainThreadWeak()) {
      RemoveFromOutstandingPurges(arena);
      ArenaPurgeResult pr = arena->PurgeLoop(aCond, aCaller);

      // No arena can die here because we're holding the arena collection lock.
      // Arenas are removed from the collection before setting their
      // mMustDeleteAfterPurge flag.
      MOZ_RELEASE_ASSERT(pr != ArenaPurgeResult::Dying);
    }
  }
}

#define MALLOC_DECL(name, return_type, ...)                          \
  inline return_type MozJemalloc::moz_arena_##name(                  \
      arena_id_t aArenaId, ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) { \
    BaseAllocator allocator(                                         \
        gArenas.GetById(aArenaId, /* IsPrivate = */ true));          \
    return allocator.name(ARGS_HELPER(ARGS, ##__VA_ARGS__));         \
  }
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC_BASE
#include "malloc_decls.h"

// End non-standard functions.
// ***************************************************************************
#ifndef XP_WIN
// Begin library-private functions, used by threading libraries for protection
// of malloc during fork().  These functions are only called if the program is
// running in threaded mode, so there is no need to check whether the program
// is threaded here.
//
// Note that the only way to keep the main-thread-only arenas in a consistent
// state for the child is if fork is called from the main thread only.  Or the
// child must not use them, eg it should call exec().  We attempt to prevent the
// child for accessing these arenas by refusing to re-initialise them.
//
// This is only accessed in the fork handlers while gArenas.mLock is held.
static pthread_t gForkingThread;

#  ifdef XP_DARWIN
// This is only accessed in the fork handlers while gArenas.mLock is held.
static pid_t gForkingProcess;
#  endif

FORK_HOOK
void _malloc_prefork(void) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // Acquire all mutexes in a safe order.
  gArenas.mLock.Lock();
  gForkingThread = pthread_self();
#  ifdef XP_DARWIN
  gForkingProcess = getpid();
#  endif

  for (auto arena : gArenas.iter()) {
    if (arena->mLock.LockIsEnabled()) {
      arena->mLock.Lock();
    }
  }

  gArenas.mPurgeListLock.Lock();

  sBaseAlloc.mMutex.Lock();

  huge_mtx.Lock();
}

FORK_HOOK
void _malloc_postfork_parent(void) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // Release all mutexes, now that fork() has completed.
  huge_mtx.Unlock();

  sBaseAlloc.mMutex.Unlock();

  gArenas.mPurgeListLock.Unlock();

  for (auto arena : gArenas.iter()) {
    if (arena->mLock.LockIsEnabled()) {
      arena->mLock.Unlock();
    }
  }

  gArenas.mLock.Unlock();
}

FORK_HOOK
void _malloc_postfork_child(void) {
  // Do this before iterating over the arenas.
  gArenas.ResetMainThread();

  // Reinitialize all mutexes, now that fork() has completed.
  huge_mtx.Init();

  sBaseAlloc.mMutex.Init();

  gArenas.mPurgeListLock.Init();

  MOZ_PUSH_IGNORE_THREAD_SAFETY
  for (auto arena : gArenas.iter()) {
    arena->mLock.Reinit(gForkingThread);
  }
  MOZ_POP_THREAD_SAFETY

  gArenas.mLock.Init();
}

#  ifdef XP_DARWIN
FORK_HOOK
void _malloc_postfork(void) {
  // On MacOS we need to check if this is running in the parent or child
  // process.
  bool is_in_parent = getpid() == gForkingProcess;
  gForkingProcess = 0;
  if (is_in_parent) {
    _malloc_postfork_parent();
  } else {
    _malloc_postfork_child();
  }
}
#  endif  // XP_DARWIN
#endif    // ! XP_WIN

// End library-private functions.
// ***************************************************************************
#ifdef MOZ_REPLACE_MALLOC
// Windows doesn't come with weak imports as they are possible with
// LD_PRELOAD or DYLD_INSERT_LIBRARIES on Linux/OSX. On this platform,
// the replacement functions are defined as variable pointers to the
// function resolved with GetProcAddress() instead of weak definitions
// of functions. On Android, the same needs to happen as well, because
// the Android linker doesn't handle weak linking with non LD_PRELOADed
// libraries, but LD_PRELOADing is not very convenient on Android, with
// the zygote.
#  ifdef XP_DARWIN
#    define MOZ_REPLACE_WEAK __attribute__((weak_import))
#  elif defined(XP_WIN) || defined(ANDROID)
#    define MOZ_DYNAMIC_REPLACE_INIT
#    define replace_init replace_init_decl
#  elif defined(__GNUC__)
#    define MOZ_REPLACE_WEAK __attribute__((weak))
#  endif

#  include "replace_malloc.h"

#  define MALLOC_DECL(name, return_type, ...) CanonicalMalloc::name,

// The default malloc table, i.e. plain allocations. It never changes. It's
// used by init(), and not used after that.
static const malloc_table_t gDefaultMallocTable = {
#  include "malloc_decls.h"
};

// The malloc table installed by init(). It never changes from that point
// onward. It will be the same as gDefaultMallocTable if no replace-malloc tool
// is enabled at startup.
static malloc_table_t gOriginalMallocTable = {
#  include "malloc_decls.h"
};

// The malloc table installed by jemalloc_replace_dynamic(). (Read the
// comments above that function for more details.)
static malloc_table_t gDynamicMallocTable = {
#  include "malloc_decls.h"
};

// This briefly points to gDefaultMallocTable at startup. After that, it points
// to either gOriginalMallocTable or gDynamicMallocTable. It's atomic to avoid
// races when switching between tables.
static Atomic<malloc_table_t const*, mozilla::MemoryOrdering::Relaxed>
    gMallocTablePtr;

#  ifdef MOZ_DYNAMIC_REPLACE_INIT
#    undef replace_init
typedef decltype(replace_init_decl) replace_init_impl_t;
static replace_init_impl_t* replace_init = nullptr;
#  endif

#  ifdef XP_WIN
typedef HMODULE replace_malloc_handle_t;

static replace_malloc_handle_t replace_malloc_handle() {
  wchar_t replace_malloc_lib[1024];
  if (GetEnvironmentVariableW(L"MOZ_REPLACE_MALLOC_LIB", replace_malloc_lib,
                              std::size(replace_malloc_lib)) > 0) {
    return LoadLibraryW(replace_malloc_lib);
  }
  return nullptr;
}

#    define REPLACE_MALLOC_GET_INIT_FUNC(handle) \
      (replace_init_impl_t*)GetProcAddress(handle, "replace_init")

#  elif defined(ANDROID)
#    include <dlfcn.h>

typedef void* replace_malloc_handle_t;

static replace_malloc_handle_t replace_malloc_handle() {
  const char* replace_malloc_lib = getenv("MOZ_REPLACE_MALLOC_LIB");
  if (replace_malloc_lib && *replace_malloc_lib) {
    return dlopen(replace_malloc_lib, RTLD_LAZY);
  }
  return nullptr;
}

#    define REPLACE_MALLOC_GET_INIT_FUNC(handle) \
      (replace_init_impl_t*)dlsym(handle, "replace_init")

#  endif

static void replace_malloc_init_funcs(malloc_table_t*);

#  ifdef MOZ_REPLACE_MALLOC_STATIC
extern "C" void logalloc_init(malloc_table_t*, ReplaceMallocBridge**);

extern "C" void dmd_init(malloc_table_t*, ReplaceMallocBridge**);
#  endif

void phc_init(malloc_table_t*, ReplaceMallocBridge**);

bool Equals(const malloc_table_t& aTable1, const malloc_table_t& aTable2) {
  return memcmp(&aTable1, &aTable2, sizeof(malloc_table_t)) == 0;
}

// Below is the malloc implementation overriding jemalloc and calling the
// replacement functions if they exist.
static ReplaceMallocBridge* gReplaceMallocBridge = nullptr;
static void init() {
  malloc_table_t tempTable = gDefaultMallocTable;

#  ifdef MOZ_DYNAMIC_REPLACE_INIT
  replace_malloc_handle_t handle = replace_malloc_handle();
  if (handle) {
    replace_init = REPLACE_MALLOC_GET_INIT_FUNC(handle);
  }
#  endif

  // Set this *before* calling replace_init, otherwise if replace_init calls
  // malloc() we'll get an infinite loop.
  gMallocTablePtr = &gDefaultMallocTable;

  // Pass in the default allocator table so replace functions can copy and use
  // it for their allocations. The replace_init() function should modify the
  // table if it wants to be active, otherwise leave it unmodified.
  if (replace_init) {
    replace_init(&tempTable, &gReplaceMallocBridge);
  }
#  ifdef MOZ_REPLACE_MALLOC_STATIC
  if (Equals(tempTable, gDefaultMallocTable)) {
    logalloc_init(&tempTable, &gReplaceMallocBridge);
  }
#    ifdef MOZ_DMD
  if (Equals(tempTable, gDefaultMallocTable)) {
    dmd_init(&tempTable, &gReplaceMallocBridge);
  }
#    endif
#  endif
  if (!Equals(tempTable, gDefaultMallocTable)) {
    replace_malloc_init_funcs(&tempTable);
  }
  gOriginalMallocTable = tempTable;
  gMallocTablePtr = &gOriginalMallocTable;
}

// WARNING WARNING WARNING: this function should be used with extreme care. It
// is not as general-purpose as it looks. It is currently used by
// tools/profiler/core/memory_hooks.cpp for counting allocations and probably
// should not be used for any other purpose.
//
// This function allows the original malloc table to be temporarily replaced by
// a different malloc table. Or, if the argument is nullptr, it switches back to
// the original malloc table.
//
// Limitations:
//
// - It is not threadsafe. If multiple threads pass it the same
//   `replace_init_func` at the same time, there will be data races writing to
//   the malloc_table_t within that function.
//
// - Only one replacement can be installed. No nesting is allowed.
//
// - The new malloc table must be able to free allocations made by the original
//   malloc table, and upon removal the original malloc table must be able to
//   free allocations made by the new malloc table. This means the new malloc
//   table can only do simple things like recording extra information, while
//   delegating actual allocation/free operations to the original malloc table.
//
MOZ_JEMALLOC_API void jemalloc_replace_dynamic(
    jemalloc_init_func replace_init_func) {
  if (replace_init_func) {
    malloc_table_t tempTable = gOriginalMallocTable;
    (*replace_init_func)(&tempTable, &gReplaceMallocBridge);
    if (!Equals(tempTable, gOriginalMallocTable)) {
      replace_malloc_init_funcs(&tempTable);

      // Temporarily switch back to the original malloc table. In the
      // (supported) non-nested case, this is a no-op. But just in case this is
      // a (unsupported) nested call, it makes the overwriting of
      // gDynamicMallocTable less racy, because ongoing calls to malloc() and
      // friends won't go through gDynamicMallocTable.
      gMallocTablePtr = &gOriginalMallocTable;

      gDynamicMallocTable = tempTable;
      gMallocTablePtr = &gDynamicMallocTable;
      // We assume that dynamic replaces don't occur close enough for a
      // thread to still have old copies of the table pointer when the 2nd
      // replace occurs.
    }
  } else {
    // Switch back to the original malloc table.
    gMallocTablePtr = &gOriginalMallocTable;
  }
}

#  define MALLOC_DECL(name, return_type, ...)                           \
    inline return_type ReplaceMalloc::name(                             \
        ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) {                       \
      if (MOZ_UNLIKELY(!gMallocTablePtr)) {                             \
        init();                                                         \
      }                                                                 \
      return (*gMallocTablePtr).name(ARGS_HELPER(ARGS, ##__VA_ARGS__)); \
    }
#  include "malloc_decls.h"

MOZ_JEMALLOC_API struct ReplaceMallocBridge* get_bridge(void) {
  if (MOZ_UNLIKELY(!gMallocTablePtr)) {
    init();
  }
  return gReplaceMallocBridge;
}

// posix_memalign, aligned_alloc, memalign and valloc all implement some kind
// of aligned memory allocation. For convenience, a replace-malloc library can
// skip defining replace_posix_memalign, replace_aligned_alloc and
// replace_valloc, and default implementations will be automatically derived
// from replace_memalign.
static void replace_malloc_init_funcs(malloc_table_t* table) {
  if (table->posix_memalign == CanonicalMalloc::posix_memalign &&
      table->memalign != CanonicalMalloc::memalign) {
    table->posix_memalign =
        AlignedAllocator<ReplaceMalloc::memalign>::posix_memalign;
  }
  if (table->aligned_alloc == CanonicalMalloc::aligned_alloc &&
      table->memalign != CanonicalMalloc::memalign) {
    table->aligned_alloc =
        AlignedAllocator<ReplaceMalloc::memalign>::aligned_alloc;
  }
  if (table->valloc == CanonicalMalloc::valloc &&
      table->memalign != CanonicalMalloc::memalign) {
    table->valloc = AlignedAllocator<ReplaceMalloc::memalign>::valloc;
  }
  if (table->moz_create_arena_with_params ==
          CanonicalMalloc::moz_create_arena_with_params &&
      table->malloc != CanonicalMalloc::malloc) {
#  define MALLOC_DECL(name, ...) \
    table->name = DummyArenaAllocator<ReplaceMalloc>::name;
#  define MALLOC_FUNCS MALLOC_FUNCS_ARENA_BASE
#  include "malloc_decls.h"
  }
  if (table->moz_arena_malloc == CanonicalMalloc::moz_arena_malloc &&
      table->malloc != CanonicalMalloc::malloc) {
#  define MALLOC_DECL(name, ...) \
    table->name = DummyArenaAllocator<ReplaceMalloc>::name;
#  define MALLOC_FUNCS MALLOC_FUNCS_ARENA_ALLOC
#  include "malloc_decls.h"
  }
}

#endif  // MOZ_REPLACE_MALLOC
// ***************************************************************************
// Definition of all the _impl functions
// GENERIC_MALLOC_DECL2_MINGW is only used for the MinGW build, and aliases
// the malloc funcs (e.g. malloc) to the je_ versions. It does not generate
// aliases for the other functions (jemalloc and arena functions).
//
// We do need aliases for the other mozglue.def-redirected functions though,
// these are done at the bottom of mozmemory_wrap.cpp
#define GENERIC_MALLOC_DECL2_MINGW(name, name_impl, return_type, ...) \
  return_type name(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__))            \
      __attribute__((alias(MOZ_STRINGIFY(name_impl))));

#define GENERIC_MALLOC_DECL2(attributes, name, name_impl, return_type, ...)  \
  return_type name_impl(ARGS_HELPER(TYPED_ARGS, ##__VA_ARGS__)) attributes { \
    return DefaultMalloc::name(ARGS_HELPER(ARGS, ##__VA_ARGS__));            \
  }

#ifndef __MINGW32__
#  define GENERIC_MALLOC_DECL(attributes, name, return_type, ...)    \
    GENERIC_MALLOC_DECL2(attributes, name, name##_impl, return_type, \
                         ##__VA_ARGS__)
#else
#  define GENERIC_MALLOC_DECL(attributes, name, return_type, ...)    \
    GENERIC_MALLOC_DECL2(attributes, name, name##_impl, return_type, \
                         ##__VA_ARGS__)                              \
    GENERIC_MALLOC_DECL2_MINGW(name, name##_impl, return_type, ##__VA_ARGS__)
#endif

#define NOTHROW_MALLOC_DECL(...) \
  MOZ_MEMORY_API MACRO_CALL(GENERIC_MALLOC_DECL, (noexcept(true), __VA_ARGS__))
#define MALLOC_DECL(...) \
  MOZ_MEMORY_API MACRO_CALL(GENERIC_MALLOC_DECL, (, __VA_ARGS__))
#define MALLOC_FUNCS MALLOC_FUNCS_MALLOC
#include "malloc_decls.h"

#undef GENERIC_MALLOC_DECL
#define GENERIC_MALLOC_DECL(attributes, name, return_type, ...) \
  GENERIC_MALLOC_DECL2(attributes, name, name, return_type, ##__VA_ARGS__)

#define MALLOC_DECL(...) \
  MOZ_JEMALLOC_API MACRO_CALL(GENERIC_MALLOC_DECL, (, __VA_ARGS__))
#define MALLOC_FUNCS (MALLOC_FUNCS_JEMALLOC | MALLOC_FUNCS_ARENA)
#include "malloc_decls.h"
// ***************************************************************************

#ifdef HAVE_DLFCN_H
#  include <dlfcn.h>
#endif

#if defined(__GLIBC__) && !defined(__UCLIBC__)
// glibc provides the RTLD_DEEPBIND flag for dlopen which can make it possible
// to inconsistently reference libc's malloc(3)-compatible functions
// (bug 493541).
//
// These definitions interpose hooks in glibc.  The functions are actually
// passed an extra argument for the caller return address, which will be
// ignored.

extern "C" {
MOZ_EXPORT void (*__free_hook)(void*) = free_impl;
MOZ_EXPORT void* (*__malloc_hook)(size_t) = malloc_impl;
MOZ_EXPORT void* (*__realloc_hook)(void*, size_t) = realloc_impl;
MOZ_EXPORT void* (*__memalign_hook)(size_t, size_t) = memalign_impl;
}

#elif defined(RTLD_DEEPBIND)
// XXX On systems that support RTLD_GROUP or DF_1_GROUP, do their
// implementations permit similar inconsistencies?  Should STV_SINGLETON
// visibility be used for interposition where available?
#  error \
      "Interposing malloc is unsafe on this system without libc malloc hooks."
#endif

#ifdef XP_WIN
MOZ_EXPORT void* _recalloc(void* aPtr, size_t aCount, size_t aSize) {
  size_t oldsize = aPtr ? AllocInfo::Get(aPtr).Size() : 0;
  CheckedInt<size_t> checkedSize = CheckedInt<size_t>(aCount) * aSize;

  if (!checkedSize.isValid()) {
    return nullptr;
  }

  size_t newsize = checkedSize.value();

  // In order for all trailing bytes to be zeroed, the caller needs to
  // use calloc(), followed by recalloc().  However, the current calloc()
  // implementation only zeros the bytes requested, so if recalloc() is
  // to work 100% correctly, calloc() will need to change to zero
  // trailing bytes.
  aPtr = DefaultMalloc::realloc(aPtr, newsize);
  if (aPtr && oldsize < newsize) {
    memset((void*)((uintptr_t)aPtr + oldsize), 0, newsize - oldsize);
  }

  return aPtr;
}

// This impl of _expand doesn't ever actually expand or shrink blocks: it
// simply replies that you may continue using a shrunk block.
MOZ_EXPORT void* _expand(void* aPtr, size_t newsize) {
  if (AllocInfo::Get(aPtr).Size() >= newsize) {
    return aPtr;
  }

  return nullptr;
}

MOZ_EXPORT size_t _msize(void* aPtr) {
  return DefaultMalloc::malloc_usable_size(aPtr);
}
#endif

#ifdef MOZ_PHC
// Compile PHC and mozjemalloc together so that PHC can inline mozjemalloc.
#  include "PHC.cpp"
#endif
