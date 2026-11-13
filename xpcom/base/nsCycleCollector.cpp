/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// This file implements a garbage-cycle collector based on the paper
//
//   Concurrent Cycle Collection in Reference Counted Systems
//   Bacon & Rajan (2001), ECOOP 2001 / Springer LNCS vol 2072
//
// We are not using the concurrent or acyclic cases of that paper; so
// the green, red and orange colors are not used.
//
// The collector is based on tracking pointers of four colors:
//
// Black nodes are definitely live. If we ever determine a node is
// black, it's ok to forget about, drop from our records.
//
// White nodes are definitely garbage cycles. Once we finish with our
// scanning, we unlink all the white nodes and expect that by
// unlinking them they will self-destruct (since a garbage cycle is
// only keeping itself alive with internal links, by definition).
//
// Snow-white is an addition to the original algorithm. A snow-white node
// has reference count zero and is just waiting for deletion.
//
// Grey nodes are being scanned. Nodes that turn grey will turn
// either black if we determine that they're live, or white if we
// determine that they're a garbage cycle. After the main collection
// algorithm there should be no grey nodes.
//
// Purple nodes are *candidates* for being scanned. They are nodes we
// haven't begun scanning yet because they're not old enough, or we're
// still partway through the algorithm.
//
// XPCOM objects participating in garbage-cycle collection are obliged
// to inform us when they ought to turn purple; that is, when their
// refcount transitions from N+1 -> N, for nonzero N. Furthermore we
// require that *after* an XPCOM object has informed us of turning
// purple, they will tell us when they either transition back to being
// black (incremented refcount) or are ultimately deleted.

// Incremental cycle collection
//
// Beyond the simple state machine required to implement incremental
// collection, the CC needs to be able to compensate for things the browser
// is doing during the collection. There are two kinds of problems. For each
// of these, there are two cases to deal with: purple-buffered C++ objects
// and JS objects.

// The first problem is that an object in the CC's graph can become garbage.
// This is bad because the CC touches the objects in its graph at every
// stage of its operation.
//
// All cycle collected C++ objects that die during a cycle collection
// will end up actually getting deleted by the SnowWhiteKiller. Before
// the SWK deletes an object, it checks if an ICC is running, and if so,
// if the object is in the graph. If it is, the CC clears mPointer and
// mParticipant so it does not point to the raw object any more. Because
// objects could die any time the CC returns to the mutator, any time the CC
// accesses a PtrInfo it must perform a null check on mParticipant to
// ensure the object has not gone away.
//
// JS objects don't always run finalizers, so the CC can't remove them from
// the graph when they die. Fortunately, JS objects can only die during a GC,
// so if a GC is begun during an ICC, the browser synchronously finishes off
// the ICC, which clears the entire CC graph. If the GC and CC are scheduled
// properly, this should be rare.
//
// The second problem is that objects in the graph can be changed, say by
// being addrefed or released, or by having a field updated, after the object
// has been added to the graph. The problem is that ICC can miss a newly
// created reference to an object, and end up unlinking an object that is
// actually alive.
//
// The basic idea of the solution, from "An on-the-fly Reference Counting
// Garbage Collector for Java" by Levanoni and Petrank, is to notice if an
// object has had an additional reference to it created during the collection,
// and if so, don't collect it during the current collection. This avoids having
// to rerun the scan as in Bacon & Rajan 2001.
//
// For cycle collected C++ objects, we modify AddRef to place the object in
// the purple buffer, in addition to Release. Then, in the CC, we treat any
// objects in the purple buffer as being alive, after graph building has
// completed. Because they are in the purple buffer, they will be suspected
// in the next CC, so there's no danger of leaks. This is imprecise, because
// we will treat as live an object that has been Released but not AddRefed
// during graph building, but that's probably rare enough that the additional
// bookkeeping overhead is not worthwhile.
//
// For JS objects, the cycle collector is only looking at gray objects. If a
// gray object is touched during ICC, it will be made black by UnmarkGray.
// Thus, if a JS object has become black during the ICC, we treat it as live.
// Merged JS zones have to be handled specially: we scan all zone globals.
// If any are black, we treat the zone as being black.

// Safety
//
// An XPCOM object is either scan-safe or scan-unsafe, purple-safe or
// purple-unsafe.
//
// An nsISupports object is scan-safe if:
//
//  - It can be QI'ed to |nsXPCOMCycleCollectionParticipant|, though
//    this operation loses ISupports identity (like nsIClassInfo).
//  - Additionally, the operation |traverse| on the resulting
//    nsXPCOMCycleCollectionParticipant does not cause *any* refcount
//    adjustment to occur (no AddRef / Release calls).
//
// A non-nsISupports ("native") object is scan-safe by explicitly
// providing its nsCycleCollectionParticipant.
//
// An object is purple-safe if it satisfies the following properties:
//
//  - The object is scan-safe.
//
// When we receive a pointer |ptr| via
// |nsCycleCollector::suspect(ptr)|, we assume it is purple-safe. We
// can check the scan-safety, but have no way to ensure the
// purple-safety; objects must obey, or else the entire system falls
// apart. Don't involve an object in this scheme if you can't
// guarantee its purple-safety. The easiest way to ensure that an
// object is purple-safe is to use nsCycleCollectingAutoRefCnt.
//
// When we have a scannable set of purple nodes ready, we begin
// our walks. During the walks, the nodes we |traverse| should only
// feed us more scan-safe nodes, and should not adjust the refcounts
// of those nodes.
//
// We do not |AddRef| or |Release| any objects during scanning. We
// rely on the purple-safety of the roots that call |suspect| to
// hold, such that we will clear the pointer from the purple buffer
// entry to the object before it is destroyed. The pointers that are
// merely scan-safe we hold only for the duration of scanning, and
// there should be no objects released from the scan-safe set during
// the scan.
//
// We *do* call |Root| and |Unroot| on every white object, on
// either side of the calls to |Unlink|. This keeps the set of white
// objects alive during the unlinking.
//

#if !defined(__MINGW32__)
#  ifdef WIN32
#    include <crtdbg.h>
#    include <errno.h>
#  endif
#endif

#include "base/process_util.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/HashTable.h"
#include "mozilla/HoldDropJSObjects.h"
/* This must occur *after* base/process_util.h to avoid typedefs conflicts. */
#include <stdint.h>
#include <stdio.h>

#include <utility>

#include "js/friend/CycleCollector.h"
#include "js/SliceBudget.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoGlobalTimelineMarker.h"
#include "mozilla/Likely.h"
#include "mozilla/LinkedList.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/MruCache.h"
#include "mozilla/PoisonIOInterposer.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/SegmentedVector.h"
#include "mozilla/Telemetry.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/UniquePtr.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCycleCollector.h"
#include "nsDeque.h"
#include "nsDumpUtils.h"
#include "nsExceptionHandler.h"
#include "nsIConsoleService.h"
#include "nsICycleCollectorListener.h"
#include "nsIFile.h"
#include "nsIMemoryReporter.h"
#include "nsISerialEventTarget.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "prenv.h"
#include "xpcpublic.h"

using namespace mozilla;

struct NurseryPurpleBufferEntry {
  void* mPtr;
  nsCycleCollectionParticipant* mParticipant;
  nsCycleCollectingAutoRefCnt* mRefCnt;
};

#define NURSERY_PURPLE_BUFFER_SIZE 2048
bool gNurseryPurpleBufferEnabled = true;
NurseryPurpleBufferEntry gNurseryPurpleBufferEntry[NURSERY_PURPLE_BUFFER_SIZE];
uint32_t gNurseryPurpleBufferEntryCount = 0;

void ClearNurseryPurpleBuffer();

static void SuspectUsingNurseryPurpleBuffer(
    void* aPtr, nsCycleCollectionParticipant* aCp,
    nsCycleCollectingAutoRefCnt* aRefCnt) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  MOZ_ASSERT(gNurseryPurpleBufferEnabled);
  if (gNurseryPurpleBufferEntryCount == NURSERY_PURPLE_BUFFER_SIZE) {
    ClearNurseryPurpleBuffer();
  }

  gNurseryPurpleBufferEntry[gNurseryPurpleBufferEntryCount] = {aPtr, aCp,
                                                               aRefCnt};
  ++gNurseryPurpleBufferEntryCount;
}

// #define COLLECT_TIME_DEBUG

// Enable assertions that are useful for diagnosing errors in graph
// construction.
// #define DEBUG_CC_GRAPH

#define DEFAULT_SHUTDOWN_COLLECTIONS 5

// One to do the freeing, then another to detect there is no more work to do.
#define NORMAL_SHUTDOWN_COLLECTIONS 2

// Cycle collector environment variables
//
// MOZ_CC_LOG_ALL: If defined, always log cycle collector heaps.
//
// MOZ_CC_LOG_SHUTDOWN: If defined, log cycle collector heaps at shutdown.
//
// MOZ_CC_LOG_SHUTDOWN_SKIP: If set to a non-negative integer value n, then
// skip logging for the first n shutdown CCs. This implies MOZ_CC_LOG_SHUTDOWN.
// The first log or two are much larger than the rest, so it can be useful to
// reduce the total size of logs if you know already that the initial logs
// aren't interesting.
//
// MOZ_CC_LOG_THREAD: If set to "main", only automatically log main thread
// CCs. If set to "worker", only automatically log worker CCs. If set to "all",
// log either. The default value is "all". This must be used with either
// MOZ_CC_LOG_ALL or MOZ_CC_LOG_SHUTDOWN for it to do anything.
//
// MOZ_CC_LOG_PROCESS: If set to "main", only automatically log main process
// CCs. If set to "content", only automatically log tab CCs. If set to "all",
// log everything. The default value is "all". This must be used with either
// MOZ_CC_LOG_ALL or MOZ_CC_LOG_SHUTDOWN for it to do anything.
//
// MOZ_CC_ALL_TRACES: If set to "all", any cycle collector
// logging done will be WantAllTraces, which disables
// various cycle collector optimizations to give a fuller picture of
// the heap. If set to "shutdown", only shutdown logging will be WantAllTraces.
// The default is none.
//
// MOZ_CC_RUN_DURING_SHUTDOWN: In non-DEBUG or builds, if this is set,
// run cycle collections at shutdown.
//
// MOZ_CC_LOG_DIRECTORY: The directory in which logs are placed (such as
// logs from MOZ_CC_LOG_ALL and MOZ_CC_LOG_SHUTDOWN, or other uses
// of nsICycleCollectorListener)

// Various parameters of this collector can be tuned using environment
// variables.

struct nsCycleCollectorParams {
  bool mLogAll;
  bool mLogShutdown;
  bool mAllTracesAll;
  bool mAllTracesShutdown;
  bool mLogThisThread;
  int32_t mLogShutdownSkip = 0;

  nsCycleCollectorParams()
      : mLogAll(PR_GetEnv("MOZ_CC_LOG_ALL") != nullptr),
        mLogShutdown(PR_GetEnv("MOZ_CC_LOG_SHUTDOWN") != nullptr),
        mAllTracesAll(false),
        mAllTracesShutdown(false) {
    if (const char* lssEnv = PR_GetEnv("MOZ_CC_LOG_SHUTDOWN_SKIP")) {
      mLogShutdown = true;
      nsDependentCString lssString(lssEnv);
      nsresult rv;
      int32_t lss = lssString.ToInteger(&rv);
      if (NS_SUCCEEDED(rv) && lss >= 0) {
        mLogShutdownSkip = lss;
      }
    }

    const char* logThreadEnv = PR_GetEnv("MOZ_CC_LOG_THREAD");
    bool threadLogging = true;
    if (logThreadEnv && !!strcmp(logThreadEnv, "all")) {
      if (NS_IsMainThread()) {
        threadLogging = !strcmp(logThreadEnv, "main");
      } else {
        threadLogging = !strcmp(logThreadEnv, "worker");
      }
    }

    const char* logProcessEnv = PR_GetEnv("MOZ_CC_LOG_PROCESS");
    bool processLogging = true;
    if (logProcessEnv && !!strcmp(logProcessEnv, "all")) {
      switch (XRE_GetProcessType()) {
        case GeckoProcessType_Default:
          processLogging = !strcmp(logProcessEnv, "main");
          break;
        case GeckoProcessType_Content:
          processLogging = !strcmp(logProcessEnv, "content");
          break;
        default:
          processLogging = false;
          break;
      }
    }
    mLogThisThread = threadLogging && processLogging;

    const char* allTracesEnv = PR_GetEnv("MOZ_CC_ALL_TRACES");
    if (allTracesEnv) {
      if (!strcmp(allTracesEnv, "all")) {
        mAllTracesAll = true;
      } else if (!strcmp(allTracesEnv, "shutdown")) {
        mAllTracesShutdown = true;
      }
    }
  }

  // aShutdownCount is how many shutdown CCs we've started.
  // For non-shutdown CCs, we'll pass in 0.
  // For the first shutdown CC, we'll pass in 1.
  bool LogThisCC(int32_t aShutdownCount) {
    if (mLogAll) {
      return mLogThisThread;
    }
    if (aShutdownCount == 0 || !mLogShutdown) {
      return false;
    }
    if (aShutdownCount <= mLogShutdownSkip) {
      return false;
    }
    return mLogThisThread;
  }

  bool AllTracesThisCC(bool aIsShutdown) {
    return mAllTracesAll || (aIsShutdown && mAllTracesShutdown);
  }
};

#ifdef COLLECT_TIME_DEBUG
class TimeLog {
 public:
  TimeLog() : mLastCheckpoint(TimeStamp::Now()) {}

  void Checkpoint(const char* aEvent) {
    TimeStamp now = TimeStamp::Now();
    double dur = (now - mLastCheckpoint).ToMilliseconds();
    if (dur >= 0.5) {
      printf("cc: %s took %.1fms\n", aEvent, dur);
    }
    mLastCheckpoint = now;
  }

 private:
  TimeStamp mLastCheckpoint;
};
#else
class TimeLog {
 public:
  TimeLog() = default;
  void Checkpoint(const char* aEvent) {}
};
#endif

////////////////////////////////////////////////////////////////////////
// Base types
////////////////////////////////////////////////////////////////////////

class PtrInfo;

class EdgePool {
 public:
  // EdgePool allocates arrays of void*, primarily to hold PtrInfo*.
  // However, at the end of a block, the last two pointers are a null
  // and then a void** pointing to the next block.  This allows
  // EdgePool::Iterators to be a single word but still capable of crossing
  // block boundaries.

  EdgePool() {
    mSentinelAndBlocks[0].block = nullptr;
    mSentinelAndBlocks[1].block = nullptr;
  }

  ~EdgePool() {
    MOZ_ASSERT(!mSentinelAndBlocks[0].block && !mSentinelAndBlocks[1].block,
               "Didn't call Clear()?");
  }

  void Clear() {
    EdgeBlock* b = EdgeBlocks();
    while (b) {
      EdgeBlock* next = b->Next();
      delete b;
      b = next;
    }

    mSentinelAndBlocks[0].block = nullptr;
    mSentinelAndBlocks[1].block = nullptr;
  }

#ifdef DEBUG
  bool IsEmpty() {
    return !mSentinelAndBlocks[0].block && !mSentinelAndBlocks[1].block;
  }
#endif

 private:
  struct EdgeBlock;
  union PtrInfoOrBlock {
    // Use a union to avoid reinterpret_cast and the ensuing
    // potential aliasing bugs.
    PtrInfo* ptrInfo;
    EdgeBlock* block;
  };
  struct EdgeBlock {
    enum { EdgeBlockSize = 16 * 1024 };

    PtrInfoOrBlock mPointers[EdgeBlockSize];
    EdgeBlock() {
      mPointers[EdgeBlockSize - 2].block = nullptr;  // sentinel
      mPointers[EdgeBlockSize - 1].block = nullptr;  // next block pointer
    }
    EdgeBlock*& Next() { return mPointers[EdgeBlockSize - 1].block; }
    PtrInfoOrBlock* Start() { return &mPointers[0]; }
    PtrInfoOrBlock* End() { return &mPointers[EdgeBlockSize - 2]; }
  };

  // Store the null sentinel so that we can have valid iterators
  // before adding any edges and without adding any blocks.
  PtrInfoOrBlock mSentinelAndBlocks[2];

  EdgeBlock*& EdgeBlocks() { return mSentinelAndBlocks[1].block; }
  EdgeBlock* EdgeBlocks() const { return mSentinelAndBlocks[1].block; }

 public:
  class Iterator {
   public:
    Iterator() : mPointer(nullptr) {}
    explicit Iterator(PtrInfoOrBlock* aPointer) : mPointer(aPointer) {}
    Iterator(const Iterator& aOther) = default;

    Iterator& operator++() {
      if (!mPointer->ptrInfo) {
        // Null pointer is a sentinel for link to the next block.
        mPointer = (mPointer + 1)->block->mPointers;
      }
      ++mPointer;
      return *this;
    }

    PtrInfo* operator*() const {
      if (!mPointer->ptrInfo) {
        // Null pointer is a sentinel for link to the next block.
        return (mPointer + 1)->block->mPointers->ptrInfo;
      }
      return mPointer->ptrInfo;
    }
    bool operator==(const Iterator& aOther) const {
      return mPointer == aOther.mPointer;
    }
    bool operator!=(const Iterator& aOther) const {
      return mPointer != aOther.mPointer;
    }

#ifdef DEBUG_CC_GRAPH
    bool Initialized() const { return mPointer != nullptr; }
#endif

   private:
    PtrInfoOrBlock* mPointer;
  };

  class Builder;
  friend class Builder;
  class Builder {
   public:
    explicit Builder(EdgePool& aPool)
        : mCurrent(&aPool.mSentinelAndBlocks[0]),
          mBlockEnd(&aPool.mSentinelAndBlocks[0]),
          mNextBlockPtr(&aPool.EdgeBlocks()) {}

    Iterator Mark() { return Iterator(mCurrent); }

    void Add(PtrInfo* aEdge) {
      if (mCurrent == mBlockEnd) {
        EdgeBlock* b = new EdgeBlock();
        *mNextBlockPtr = b;
        mCurrent = b->Start();
        mBlockEnd = b->End();
        mNextBlockPtr = &b->Next();
      }
      (mCurrent++)->ptrInfo = aEdge;
    }

   private:
    // mBlockEnd points to space for null sentinel
    PtrInfoOrBlock* mCurrent;
    PtrInfoOrBlock* mBlockEnd;
    EdgeBlock** mNextBlockPtr;
  };

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t n = 0;
    EdgeBlock* b = EdgeBlocks();
    while (b) {
      n += aMallocSizeOf(b);
      b = b->Next();
    }
    return n;
  }
};

#ifdef DEBUG_CC_GRAPH
#  define CC_GRAPH_ASSERT(b) MOZ_ASSERT(b)
#else
#  define CC_GRAPH_ASSERT(b)
#endif

#define CC_TELEMETRY(_name, _value)                                            \
  do {                                                                         \
    if (NS_IsMainThread()) {                                                   \
      Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR##_name, _value);        \
    } else {                                                                   \
      Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR_WORKER##_name, _value); \
    }                                                                          \
  } while (0)

enum NodeColor { black, white, grey };

// This structure should be kept as small as possible; we may expect
// hundreds of thousands of them to be allocated and touched
// repeatedly during each cycle collection.
class PtrInfo final {
 public:
  // mParticipant knows a more concrete type.
  void* mPointer;
  nsCycleCollectionParticipant* mParticipant;
  uint32_t mColor : 2;
  uint32_t mInternalRefs : 30;
  uint32_t mRefCount;

 private:
  EdgePool::Iterator mFirstChild;

  static const uint32_t kInitialRefCount = UINT32_MAX - 1;

 public:
  PtrInfo(void* aPointer, nsCycleCollectionParticipant* aParticipant)
      : mPointer(aPointer),
        mParticipant(aParticipant),
        mColor(grey),
        mInternalRefs(0),
        mRefCount(kInitialRefCount),
        mFirstChild() {
    MOZ_ASSERT(aParticipant);

    // We initialize mRefCount to a large non-zero value so
    // that it doesn't look like a JS object to the cycle collector
    // in the case where the object dies before being traversed.
    MOZ_ASSERT(!IsGrayJS() && !IsBlackJS());
  }

  // Allow NodePool::NodeBlock's constructor to compile.
  PtrInfo()
      : mPointer{nullptr},
        mParticipant{nullptr},
        mColor{0},
        mInternalRefs{0},
        mRefCount{0} {
    MOZ_ASSERT_UNREACHABLE("should never be called");
  }

  bool IsGrayJS() const { return mRefCount == 0; }

  bool IsBlackJS() const { return mRefCount == UINT32_MAX; }

  bool WasTraversed() const { return mRefCount != kInitialRefCount; }

  EdgePool::Iterator FirstChild() const {
    CC_GRAPH_ASSERT(mFirstChild.Initialized());
    return mFirstChild;
  }

  // this PtrInfo must be part of a NodePool
  EdgePool::Iterator LastChild() const {
    CC_GRAPH_ASSERT((this + 1)->mFirstChild.Initialized());
    return (this + 1)->mFirstChild;
  }

  void SetFirstChild(EdgePool::Iterator aFirstChild) {
    CC_GRAPH_ASSERT(aFirstChild.Initialized());
    mFirstChild = aFirstChild;
  }

  // this PtrInfo must be part of a NodePool
  void SetLastChild(EdgePool::Iterator aLastChild) {
    CC_GRAPH_ASSERT(aLastChild.Initialized());
    (this + 1)->mFirstChild = aLastChild;
  }

  void AnnotatedReleaseAssert(bool aCondition, const char* aMessage);
};

void PtrInfo::AnnotatedReleaseAssert(bool aCondition, const char* aMessage) {
  if (aCondition) {
    return;
  }

  const char* piName = "Unknown";
  if (mParticipant) {
    piName = mParticipant->ClassName();
  }
  nsPrintfCString msg("%s, for class %s", aMessage, piName);
  NS_WARNING(msg.get());
  CrashReporter::AnnotateCrashReport(CrashReporter::Annotation::CycleCollector,
                                     msg);

  MOZ_CRASH();
}

/**
 * A structure designed to be used like a linked list of PtrInfo, except
 * it allocates many PtrInfos at a time.
 */
class NodePool {
 private:
  // The -2 allows us to use |NodeBlockSize + 1| for |mEntries|, and fit
  // |mNext|, all without causing slop.
  enum { NodeBlockSize = 4 * 1024 - 2 };

  struct NodeBlock {
    // We create and destroy NodeBlock using moz_xmalloc/free rather than new
    // and delete to avoid calling its constructor and destructor.
    NodeBlock() : mNext{nullptr} {
      MOZ_ASSERT_UNREACHABLE("should never be called");

      // Ensure NodeBlock is the right size (see the comment on NodeBlockSize
      // above).
      static_assert(
          sizeof(NodeBlock) == 81904 ||  // 32-bit; equals 19.996 x 4 KiB pages
              sizeof(NodeBlock) ==
                  131048,  // 64-bit; equals 31.994 x 4 KiB pages
          "ill-sized NodeBlock");
    }
    ~NodeBlock() { MOZ_ASSERT_UNREACHABLE("should never be called"); }

    NodeBlock* mNext;
    PtrInfo mEntries[NodeBlockSize + 1];  // +1 to store last child of last node
  };

 public:
  NodePool() : mBlocks(nullptr), mLast(nullptr) {}

  ~NodePool() { MOZ_ASSERT(!mBlocks, "Didn't call Clear()?"); }

  void Clear() {
    NodeBlock* b = mBlocks;
    while (b) {
      NodeBlock* n = b->mNext;
      free(b);
      b = n;
    }

    mBlocks = nullptr;
    mLast = nullptr;
  }

#ifdef DEBUG
  bool IsEmpty() { return !mBlocks && !mLast; }
#endif

  class Builder;
  friend class Builder;
  class Builder {
   public:
    explicit Builder(NodePool& aPool)
        : mNextBlock(&aPool.mBlocks), mNext(aPool.mLast), mBlockEnd(nullptr) {
      MOZ_ASSERT(!aPool.mBlocks && !aPool.mLast, "pool not empty");
    }
    PtrInfo* Add(void* aPointer, nsCycleCollectionParticipant* aParticipant) {
      if (mNext == mBlockEnd) {
        NodeBlock* block = static_cast<NodeBlock*>(malloc(sizeof(NodeBlock)));
        if (!block) {
          return nullptr;
        }

        *mNextBlock = block;
        mNext = block->mEntries;
        mBlockEnd = block->mEntries + NodeBlockSize;
        block->mNext = nullptr;
        mNextBlock = &block->mNext;
      }
      return new (mozilla::KnownNotNull, mNext++)
          PtrInfo(aPointer, aParticipant);
    }

   private:
    NodeBlock** mNextBlock;
    PtrInfo*& mNext;
    PtrInfo* mBlockEnd;
  };

  class Enumerator;
  friend class Enumerator;
  class Enumerator {
   public:
    explicit Enumerator(NodePool& aPool)
        : mFirstBlock(aPool.mBlocks),
          mCurBlock(nullptr),
          mNext(nullptr),
          mBlockEnd(nullptr),
          mLast(aPool.mLast) {}

    bool IsDone() const { return mNext == mLast; }

    bool AtBlockEnd() const { return mNext == mBlockEnd; }

    PtrInfo* GetNext() {
      MOZ_ASSERT(!IsDone(), "calling GetNext when done");
      if (mNext == mBlockEnd) {
        NodeBlock* nextBlock = mCurBlock ? mCurBlock->mNext : mFirstBlock;
        mNext = nextBlock->mEntries;
        mBlockEnd = mNext + NodeBlockSize;
        mCurBlock = nextBlock;
      }
      return mNext++;
    }

   private:
    // mFirstBlock is a reference to allow an Enumerator to be constructed
    // for an empty graph.
    NodeBlock*& mFirstBlock;
    NodeBlock* mCurBlock;
    // mNext is the next value we want to return, unless mNext == mBlockEnd
    // NB: mLast is a reference to allow enumerating while building!
    PtrInfo* mNext;
    PtrInfo* mBlockEnd;
    PtrInfo*& mLast;
  };

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
    // We don't measure the things pointed to by mEntries[] because those
    // pointers are non-owning.
    size_t n = 0;
    NodeBlock* b = mBlocks;
    while (b) {
      n += aMallocSizeOf(b);
      b = b->mNext;
    }
    return n;
  }

 private:
  NodeBlock* mBlocks;
  PtrInfo* mLast;
};

struct PtrToNodeHashPolicy {
  using Key = PtrInfo*;
  using Lookup = void*;

  static js::HashNumber hash(const Lookup& aLookup) {
    return mozilla::HashGeneric(aLookup);
  }

  static bool match(const Key& aKey, const Lookup& aLookup) {
    return aKey->mPointer == aLookup;
  }
};

struct WeakMapping {
  // map and key will be null if the corresponding objects are GC marked
  PtrInfo* mMap;
  PtrInfo* mKey;
  PtrInfo* mKeyDelegate;
  PtrInfo* mVal;
};

class CCGraphBuilder;

struct CCGraph {
  NodePool mNodes;
  EdgePool mEdges;
  nsTArray<WeakMapping> mWeakMaps;
  uint32_t mRootCount;

 private:
  friend CCGraphBuilder;

  mozilla::HashSet<PtrInfo*, PtrToNodeHashPolicy> mPtrInfoMap;

  bool mOutOfMemory;

  static const uint32_t kInitialMapLength = 16384;

 public:
  CCGraph()
      : mRootCount(0), mPtrInfoMap(kInitialMapLength), mOutOfMemory(false) {}

  ~CCGraph() = default;

  void Init() { MOZ_ASSERT(IsEmpty(), "Failed to call CCGraph::Clear"); }

  void Clear() {
    mNodes.Clear();
    mEdges.Clear();
    mWeakMaps.Clear();
    mRootCount = 0;
    mPtrInfoMap.clearAndCompact();
    mOutOfMemory = false;
  }

#ifdef DEBUG
  bool IsEmpty() {
    return mNodes.IsEmpty() && mEdges.IsEmpty() && mWeakMaps.IsEmpty() &&
           mRootCount == 0 && mPtrInfoMap.empty();
  }
#endif

  PtrInfo* FindNode(void* aPtr);
  void RemoveObjectFromMap(void* aObject);

  uint32_t MapCount() const { return mPtrInfoMap.count(); }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
    size_t n = 0;

    n += mNodes.SizeOfExcludingThis(aMallocSizeOf);
    n += mEdges.SizeOfExcludingThis(aMallocSizeOf);

    // We don't measure what the WeakMappings point to, because the
    // pointers are non-owning.
    n += mWeakMaps.ShallowSizeOfExcludingThis(aMallocSizeOf);

    n += mPtrInfoMap.shallowSizeOfExcludingThis(aMallocSizeOf);

    return n;
  }
};

PtrInfo* CCGraph::FindNode(void* aPtr) {
  auto p = mPtrInfoMap.lookup(aPtr);
  return p ? *p : nullptr;
}

void CCGraph::RemoveObjectFromMap(void* aObj) {
  auto p = mPtrInfoMap.lookup(aObj);
  if (p) {
    PtrInfo* pinfo = *p;
    pinfo->mPointer = nullptr;
    pinfo->mParticipant = nullptr;
    mPtrInfoMap.remove(p);
  }
}

static nsISupports* CanonicalizeXPCOMParticipant(nsISupports* aIn) {
  nsISupports* out = nullptr;
  aIn->QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                      reinterpret_cast<void**>(&out));
  return out;
}

struct nsPurpleBufferEntry {
  nsPurpleBufferEntry(void* aObject, nsCycleCollectingAutoRefCnt* aRefCnt,
                      nsCycleCollectionParticipant* aParticipant)
      : mObject(aObject), mRefCnt(aRefCnt), mParticipant(aParticipant) {}

  nsPurpleBufferEntry(nsPurpleBufferEntry&& aOther)
      : mObject(nullptr), mRefCnt(nullptr), mParticipant(nullptr) {
    Swap(aOther);
  }

  void Swap(nsPurpleBufferEntry& aOther) {
    std::swap(mObject, aOther.mObject);
    std::swap(mRefCnt, aOther.mRefCnt);
    std::swap(mParticipant, aOther.mParticipant);
  }

  void Clear() {
    mRefCnt->RemoveFromPurpleBuffer();
    mRefCnt = nullptr;
    mObject = nullptr;
    mParticipant = nullptr;
  }

  ~nsPurpleBufferEntry() {
    if (mRefCnt) {
      mRefCnt->RemoveFromPurpleBuffer();
    }
  }

  void* mObject;
  nsCycleCollectingAutoRefCnt* mRefCnt;
  nsCycleCollectionParticipant* mParticipant;  // nullptr for nsISupports
};

class nsCycleCollector;

struct nsPurpleBuffer {
 private:
  uint32_t mCount;

  // Try to match the size of a jemalloc bucket, to minimize slop bytes.
  // - On 32-bit platforms sizeof(nsPurpleBufferEntry) is 12, so mEntries'
  //   Segment is 16,372 bytes.
  // - On 64-bit platforms sizeof(nsPurpleBufferEntry) is 24, so mEntries'
  //   Segment is 32,760 bytes.
  static const uint32_t kEntriesPerSegment = 1365;
  static const size_t kSegmentSize =
      sizeof(nsPurpleBufferEntry) * kEntriesPerSegment;
  typedef SegmentedVector<nsPurpleBufferEntry, kSegmentSize,
                          InfallibleAllocPolicy>
      PurpleBufferVector;
  PurpleBufferVector mEntries;

 public:
  nsPurpleBuffer() : mCount(0) {
    static_assert(
        sizeof(PurpleBufferVector::Segment) == 16372 ||      // 32-bit
            sizeof(PurpleBufferVector::Segment) == 32760 ||  // 64-bit
            sizeof(PurpleBufferVector::Segment) == 32744,    // 64-bit Windows
        "ill-sized nsPurpleBuffer::mEntries");
  }

  ~nsPurpleBuffer() = default;

  // This method compacts mEntries.
  template <class PurpleVisitor>
  void VisitEntries(PurpleVisitor& aVisitor) {
    Maybe<AutoRestore<bool>> ar;
    if (NS_IsMainThread()) {
      ar.emplace(gNurseryPurpleBufferEnabled);
      gNurseryPurpleBufferEnabled = false;
      ClearNurseryPurpleBuffer();
    }

    if (mEntries.IsEmpty()) {
      return;
    }

    uint32_t oldLength = mEntries.Length();
    uint32_t keptLength = 0;
    auto revIter = mEntries.IterFromLast();
    auto iter = mEntries.Iter();
    // After iteration this points to the first empty entry.
    auto firstEmptyIter = mEntries.Iter();
    auto iterFromLastEntry = mEntries.IterFromLast();
    for (; !iter.Done(); iter.Next()) {
      nsPurpleBufferEntry& e = iter.Get();
      if (e.mObject) {
        if (!aVisitor.Visit(*this, &e)) {
          return;
        }
      }

      // Visit call above may have cleared the entry, or the entry was empty
      // already.
      if (!e.mObject) {
        // Try to find a non-empty entry from the end of the vector.
        for (; !revIter.Done(); revIter.Prev()) {
          nsPurpleBufferEntry& otherEntry = revIter.Get();
          if (&e == &otherEntry) {
            break;
          }
          if (otherEntry.mObject) {
            if (!aVisitor.Visit(*this, &otherEntry)) {
              return;
            }
            // Visit may have cleared otherEntry.
            if (otherEntry.mObject) {
              e.Swap(otherEntry);
              revIter.Prev();  // We've swapped this now empty entry.
              break;
            }
          }
        }
      }

      // Entry is non-empty even after the Visit call, ensure it is kept
      // in mEntries.
      if (e.mObject) {
        firstEmptyIter.Next();
        ++keptLength;
      }

      if (&e == &revIter.Get()) {
        break;
      }
    }

    // There were some empty entries.
    if (oldLength != keptLength) {
      // While visiting entries, some new ones were possibly added. This can
      // happen during CanSkip. Move all such new entries to be after other
      // entries. Note, we don't call Visit on newly added entries!
      if (&iterFromLastEntry.Get() != &mEntries.GetLast()) {
        iterFromLastEntry.Next();  // Now pointing to the first added entry.
        auto& iterForNewEntries = iterFromLastEntry;
        while (!iterForNewEntries.Done()) {
          MOZ_ASSERT(!firstEmptyIter.Done());
          MOZ_ASSERT(!firstEmptyIter.Get().mObject);
          firstEmptyIter.Get().Swap(iterForNewEntries.Get());
          firstEmptyIter.Next();
          iterForNewEntries.Next();
        }
      }

      mEntries.PopLastN(oldLength - keptLength);
    }
  }

  void FreeBlocks() {
    mCount = 0;
    mEntries.Clear();
  }

  void SelectPointers(CCGraphBuilder& aBuilder);

  // RemoveSkippable removes entries from the purple buffer synchronously
  // (1) if !aAsyncSnowWhiteFreeing and nsPurpleBufferEntry::mRefCnt is 0 or
  // (2) if nsXPCOMCycleCollectionParticipant::CanSkip() for the obj or
  // (3) if nsPurpleBufferEntry::mRefCnt->IsPurple() is false.
  // (4) If aRemoveChildlessNodes is true, then any nodes in the purple buffer
  //     that will have no children in the cycle collector graph will also be
  //     removed. CanSkip() may be run on these children.
  void RemoveSkippable(nsCycleCollector* aCollector, js::SliceBudget& aBudget,
                       bool aRemoveChildlessNodes, bool aAsyncSnowWhiteFreeing,
                       CC_ForgetSkippableCallback aCb);

  MOZ_ALWAYS_INLINE void Put(void* aObject, nsCycleCollectionParticipant* aCp,
                             nsCycleCollectingAutoRefCnt* aRefCnt) {
    nsPurpleBufferEntry entry(aObject, aRefCnt, aCp);
    Unused << mEntries.Append(std::move(entry));
    MOZ_ASSERT(!entry.mRefCnt, "Move didn't work!");
    ++mCount;
  }

  void Remove(nsPurpleBufferEntry* aEntry) {
    MOZ_ASSERT(mCount != 0, "must have entries");
    --mCount;
    aEntry->Clear();
  }

  uint32_t Count() const { return mCount; }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
    return mEntries.SizeOfExcludingThis(aMallocSizeOf);
  }
};

static bool AddPurpleRoot(CCGraphBuilder& aBuilder, void* aRoot,
                          nsCycleCollectionParticipant* aParti);

struct SelectPointersVisitor {
  explicit SelectPointersVisitor(CCGraphBuilder& aBuilder)
      : mBuilder(aBuilder) {}

  bool Visit(nsPurpleBuffer& aBuffer, nsPurpleBufferEntry* aEntry) {
    MOZ_ASSERT(aEntry->mObject, "Null object in purple buffer");
    MOZ_ASSERT(aEntry->mRefCnt->get() != 0,
               "SelectPointersVisitor: snow-white object in the purple buffer");
    if (!aEntry->mRefCnt->IsPurple() ||
        AddPurpleRoot(mBuilder, aEntry->mObject, aEntry->mParticipant)) {
      aBuffer.Remove(aEntry);
    }
    return true;
  }

 private:
  CCGraphBuilder& mBuilder;
};

void nsPurpleBuffer::SelectPointers(CCGraphBuilder& aBuilder) {
  SelectPointersVisitor visitor(aBuilder);
  VisitEntries(visitor);

  MOZ_ASSERT(mCount == 0, "AddPurpleRoot failed");
  if (mCount == 0) {
    FreeBlocks();
  }
}

enum ccPhase {
  IdlePhase,
  GraphBuildingPhase,
  ScanAndCollectWhitePhase,
  CleanupPhase
};

enum ccIsManual { CCIsNotManual = false, CCIsManual = true };

////////////////////////////////////////////////////////////////////////
// Top level structure for the cycle collector.
////////////////////////////////////////////////////////////////////////

using js::SliceBudget;

class JSPurpleBuffer;

class nsCycleCollector : public nsIMemoryReporter {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTER

 private:
  bool mActivelyCollecting;
  bool mFreeingSnowWhite;
  // mScanInProgress should be false when we're collecting white objects.
  bool mScanInProgress;
  CycleCollectorResults mResults;
  TimeStamp mCollectionStart;

  CycleCollectedJSRuntime* mCCJSRuntime;

  ccPhase mIncrementalPhase;
  int32_t mShutdownCount = 0;
  CCGraph mGraph;
  UniquePtr<CCGraphBuilder> mBuilder;
  RefPtr<nsCycleCollectorLogger> mLogger;

#ifdef DEBUG
  nsISerialEventTarget* mEventTarget;
#endif

  nsCycleCollectorParams mParams;

  uint32_t mWhiteNodeCount;

  CC_BeforeUnlinkCallback mBeforeUnlinkCB;
  CC_ForgetSkippableCallback mForgetSkippableCB;

  nsPurpleBuffer mPurpleBuf;

  uint32_t mUnmergedNeeded;
  uint32_t mMergedInARow;

  RefPtr<JSPurpleBuffer> mJSPurpleBuffer;

 private:
  virtual ~nsCycleCollector();

 public:
  nsCycleCollector();

  void SetCCJSRuntime(CycleCollectedJSRuntime* aCCRuntime);
  void ClearCCJSRuntime();

  void SetBeforeUnlinkCallback(CC_BeforeUnlinkCallback aBeforeUnlinkCB) {
    CheckThreadSafety();
    mBeforeUnlinkCB = aBeforeUnlinkCB;
  }

  void SetForgetSkippableCallback(
      CC_ForgetSkippableCallback aForgetSkippableCB) {
    CheckThreadSafety();
    mForgetSkippableCB = aForgetSkippableCB;
  }

  void Suspect(void* aPtr, nsCycleCollectionParticipant* aCp,
               nsCycleCollectingAutoRefCnt* aRefCnt);
  void SuspectNurseryEntries();
  uint32_t SuspectedCount();
  void ForgetSkippable(js::SliceBudget& aBudget, bool aRemoveChildlessNodes,
                       bool aAsyncSnowWhiteFreeing);
  bool FreeSnowWhite(bool aUntilNoSWInPurpleBuffer);
  bool FreeSnowWhiteWithBudget(js::SliceBudget& aBudget);

  // This method assumes its argument is already canonicalized.
  void RemoveObjectFromGraph(void* aPtr);

  void PrepareForGarbageCollection();
  void FinishAnyCurrentCollection(CCReason aReason);

  bool Collect(CCReason aReason, ccIsManual aIsManual, SliceBudget& aBudget,
               nsICycleCollectorListener* aManualListener,
               bool aPreferShorterSlices = false);
  MOZ_CAN_RUN_SCRIPT
  void Shutdown(bool aDoCollect);

  bool IsIdle() const { return mIncrementalPhase == IdlePhase; }

  void SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                           size_t* aObjectSize, size_t* aGraphSize,
                           size_t* aPurpleBufferSize) const;

  JSPurpleBuffer* GetJSPurpleBuffer();

  CycleCollectedJSRuntime* Runtime() { return mCCJSRuntime; }

 private:
  void CheckThreadSafety();
  MOZ_CAN_RUN_SCRIPT
  void ShutdownCollect();

  void FixGrayBits(bool aIsShutdown, TimeLog& aTimeLog);
  bool IsIncrementalGCInProgress();
  void FinishAnyIncrementalGCInProgress();
  bool ShouldMergeZones(ccIsManual aIsManual);

  void BeginCollection(CCReason aReason, ccIsManual aIsManual,
                       nsICycleCollectorListener* aManualListener);
  void MarkRoots(SliceBudget& aBudget);
  void ScanRoots(bool aFullySynchGraphBuild);
  void ScanIncrementalRoots();
  void ScanWhiteNodes(bool aFullySynchGraphBuild);
  void ScanBlackNodes();
  void ScanWeakMaps();

  // returns whether anything was collected
  bool CollectWhite();

  void ClearWhiteJSWeakRefTargets();

 public:
  bool IsGCThingWhiteInCCGraph(JS::GCCellPtr aPtr);

 private:
  void CleanupAfterCollection();
};

NS_IMPL_ISUPPORTS(nsCycleCollector, nsIMemoryReporter)

/**
 * GraphWalker is templatized over a Visitor class that must provide
 * the following two methods:
 *
 * bool ShouldVisitNode(PtrInfo const *pi);
 * void VisitNode(PtrInfo *pi);
 */
template <class Visitor>
class GraphWalker {
 private:
  Visitor mVisitor;

  void DoWalk(nsDeque<PtrInfo>& aQueue);

  void CheckedPush(nsDeque<PtrInfo>& aQueue, PtrInfo* aPi) {
    if (!aPi) {
      MOZ_CRASH();
    }
    if (!aQueue.Push(aPi, fallible)) {
      mVisitor.Failed();
    }
  }

 public:
  void Walk(PtrInfo* aPi);
  void WalkFromRoots(CCGraph& aGraph);
  // copy-constructing the visitor should be cheap, and less
  // indirection than using a reference
  explicit GraphWalker(const Visitor aVisitor) : mVisitor(aVisitor) {}
};

////////////////////////////////////////////////////////////////////////
// The static collector struct
////////////////////////////////////////////////////////////////////////

struct CollectorData {
  RefPtr<nsCycleCollector> mCollector;
  CycleCollectedJSContext* mContext;
};

static MOZ_THREAD_LOCAL(CollectorData*) sCollectorData;

////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////

static inline void ToParticipant(nsISupports* aPtr,
                                 nsXPCOMCycleCollectionParticipant** aCp) {
  // We use QI to move from an nsISupports to an
  // nsXPCOMCycleCollectionParticipant, which is a per-class singleton helper
  // object that implements traversal and unlinking logic for the nsISupports
  // in question.
  *aCp = nullptr;
  CallQueryInterface(aPtr, aCp);
}

static void ToParticipant(void* aParti, nsCycleCollectionParticipant** aCp) {
  // If the participant is null, this is an nsISupports participant,
  // so we must QI to get the real participant.

  if (!*aCp) {
    nsISupports* nsparti = static_cast<nsISupports*>(aParti);
    MOZ_ASSERT(CanonicalizeXPCOMParticipant(nsparti) == nsparti);
    nsXPCOMCycleCollectionParticipant* xcp;
    ToParticipant(nsparti, &xcp);
    *aCp = xcp;
  }
}

template <class Visitor>
MOZ_NEVER_INLINE void GraphWalker<Visitor>::Walk(PtrInfo* aPi) {
  nsDeque<PtrInfo> queue;
  CheckedPush(queue, aPi);
  DoWalk(queue);
}

template <class Visitor>
MOZ_NEVER_INLINE void GraphWalker<Visitor>::WalkFromRoots(CCGraph& aGraph) {
  nsDeque<PtrInfo> queue;
  NodePool::Enumerator etor(aGraph.mNodes);
  for (uint32_t i = 0; i < aGraph.mRootCount; ++i) {
    CheckedPush(queue, etor.GetNext());
  }
  DoWalk(queue);
}

template <class Visitor>
MOZ_NEVER_INLINE void GraphWalker<Visitor>::DoWalk(nsDeque<PtrInfo>& aQueue) {
  // Use a aQueue to match the breadth-first traversal used when we
  // built the graph, for hopefully-better locality.
  while (aQueue.GetSize() > 0) {
    PtrInfo* pi = aQueue.PopFront();

    if (pi->WasTraversed() && mVisitor.ShouldVisitNode(pi)) {
      mVisitor.VisitNode(pi);
      for (EdgePool::Iterator child = pi->FirstChild(),
                              child_end = pi->LastChild();
           child != child_end; ++child) {
        CheckedPush(aQueue, *child);
      }
    }
  }
}

struct CCGraphDescriber : public LinkedListElement<CCGraphDescriber> {
  CCGraphDescriber() : mAddress("0x"), mCnt(0), mType(eUnknown) {}

  enum Type {
    eRefCountedObject,
    eGCedObject,
    eGCMarkedObject,
    eEdge,
    eRoot,
    eGarbage,
    eUnknown
  };

  nsCString mAddress;
  nsCString mName;
  nsCString mCompartmentOrToAddress;
  uint32_t mCnt;
  Type mType;
};

class LogStringMessageAsync : public DiscardableRunnable {
 public:
  explicit LogStringMessageAsync(const nsAString& aMsg)
      : mozilla::DiscardableRunnable("LogStringMessageAsync"), mMsg(aMsg) {}

  NS_IMETHOD Run() override {
    nsCOMPtr<nsIConsoleService> cs =
        do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    if (cs) {
      cs->LogStringMessage(mMsg.get());
    }
    return NS_OK;
  }

 private:
  nsString mMsg;
};

class nsCycleCollectorLogSinkToFile final : public nsICycleCollectorLogSink {
 public:
  NS_DECL_ISUPPORTS

  nsCycleCollectorLogSinkToFile()
      : mProcessIdentifier(base::GetCurrentProcId()),
        mGCLog("gc-edges"),
        mCCLog("cc-edges") {}

  NS_IMETHOD GetFilenameIdentifier(nsAString& aIdentifier) override {
    aIdentifier = mFilenameIdentifier;
    return NS_OK;
  }

  NS_IMETHOD SetFilenameIdentifier(const nsAString& aIdentifier) override {
    mFilenameIdentifier = aIdentifier;
    return NS_OK;
  }

  NS_IMETHOD GetProcessIdentifier(int32_t* aIdentifier) override {
    *aIdentifier = mProcessIdentifier;
    return NS_OK;
  }

  NS_IMETHOD SetProcessIdentifier(int32_t aIdentifier) override {
    mProcessIdentifier = aIdentifier;
    return NS_OK;
  }

  NS_IMETHOD GetGcLog(nsIFile** aPath) override {
    NS_IF_ADDREF(*aPath = mGCLog.mFile);
    return NS_OK;
  }

  NS_IMETHOD GetCcLog(nsIFile** aPath) override {
    NS_IF_ADDREF(*aPath = mCCLog.mFile);
    return NS_OK;
  }

  NS_IMETHOD Open(FILE** aGCLog, FILE** aCCLog) override {
    nsresult rv;

    if (mGCLog.mStream || mCCLog.mStream) {
      return NS_ERROR_UNEXPECTED;
    }

    rv = OpenLog(&mGCLog);
    NS_ENSURE_SUCCESS(rv, rv);
    *aGCLog = mGCLog.mStream;

    rv = OpenLog(&mCCLog);
    NS_ENSURE_SUCCESS(rv, rv);
    *aCCLog = mCCLog.mStream;

    return NS_OK;
  }

  NS_IMETHOD CloseGCLog() override {
    if (!mGCLog.mStream) {
      return NS_ERROR_UNEXPECTED;
    }
    CloseLog(&mGCLog, u"Garbage"_ns);
    return NS_OK;
  }

  NS_IMETHOD CloseCCLog() override {
    if (!mCCLog.mStream) {
      return NS_ERROR_UNEXPECTED;
    }
    CloseLog(&mCCLog, u"Cycle"_ns);
    return NS_OK;
  }

 private:
  ~nsCycleCollectorLogSinkToFile() {
    if (mGCLog.mStream) {
      MozillaUnRegisterDebugFILE(mGCLog.mStream);
      fclose(mGCLog.mStream);
    }
    if (mCCLog.mStream) {
      MozillaUnRegisterDebugFILE(mCCLog.mStream);
      fclose(mCCLog.mStream);
    }
  }

  struct FileInfo {
    const char* const mPrefix;
    nsCOMPtr<nsIFile> mFile;
    FILE* mStream;

    explicit FileInfo(const char* aPrefix)
        : mPrefix(aPrefix), mStream(nullptr) {}
  };

  /**
   * Create a new file named something like aPrefix.$PID.$IDENTIFIER.log in
   * $MOZ_CC_LOG_DIRECTORY or in the system's temp directory.  No existing
   * file will be overwritten; if aPrefix.$PID.$IDENTIFIER.log exists, we'll
   * try a file named something like aPrefix.$PID.$IDENTIFIER-1.log, and so
   * on.
   */
  already_AddRefed<nsIFile> CreateTempFile(const char* aPrefix) {
    nsPrintfCString filename("%s.%d%s%s.log", aPrefix, mProcessIdentifier,
                             mFilenameIdentifier.IsEmpty() ? "" : ".",
                             NS_ConvertUTF16toUTF8(mFilenameIdentifier).get());

    // Get the log directory either from $MOZ_CC_LOG_DIRECTORY or from
    // the fallback directories in OpenTempFile.  We don't use an nsCOMPtr
    // here because OpenTempFile uses an in/out param and getter_AddRefs
    // wouldn't work.
    nsIFile* logFile = nullptr;
    if (char* env = PR_GetEnv("MOZ_CC_LOG_DIRECTORY")) {
      NS_NewNativeLocalFile(nsCString(env), /* followLinks = */ true, &logFile);
    }

    // On Android or B2G, this function will open a file named
    // aFilename under a memory-reporting-specific folder
    // (/data/local/tmp/memory-reports). Otherwise, it will open a
    // file named aFilename under "NS_OS_TEMP_DIR".
    nsresult rv =
        nsDumpUtils::OpenTempFile(filename, &logFile, "memory-reports"_ns);
    if (NS_FAILED(rv)) {
      NS_IF_RELEASE(logFile);
      return nullptr;
    }

    return dont_AddRef(logFile);
  }

  nsresult OpenLog(FileInfo* aLog) {
    // Initially create the log in a file starting with "incomplete-".
    // We'll move the file and strip off the "incomplete-" once the dump
    // completes.  (We do this because we don't want scripts which poll
    // the filesystem looking for GC/CC dumps to grab a file before we're
    // finished writing to it.)
    nsAutoCString incomplete;
    incomplete += "incomplete-";
    incomplete += aLog->mPrefix;
    MOZ_ASSERT(!aLog->mFile);
    aLog->mFile = CreateTempFile(incomplete.get());
    if (NS_WARN_IF(!aLog->mFile)) {
      return NS_ERROR_UNEXPECTED;
    }

    MOZ_ASSERT(!aLog->mStream);
    nsresult rv = aLog->mFile->OpenANSIFileDesc("w", &aLog->mStream);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return NS_ERROR_UNEXPECTED;
    }
    MozillaRegisterDebugFILE(aLog->mStream);
    return NS_OK;
  }

  nsresult CloseLog(FileInfo* aLog, const nsAString& aCollectorKind) {
    MOZ_ASSERT(aLog->mStream);
    MOZ_ASSERT(aLog->mFile);

    MozillaUnRegisterDebugFILE(aLog->mStream);
    fclose(aLog->mStream);
    aLog->mStream = nullptr;

    // Strip off "incomplete-".
    nsCOMPtr<nsIFile> logFileFinalDestination = CreateTempFile(aLog->mPrefix);
    if (NS_WARN_IF(!logFileFinalDestination)) {
      return NS_ERROR_UNEXPECTED;
    }

    nsAutoString logFileFinalDestinationName;
    logFileFinalDestination->GetLeafName(logFileFinalDestinationName);
    if (NS_WARN_IF(logFileFinalDestinationName.IsEmpty())) {
      return NS_ERROR_UNEXPECTED;
    }

    aLog->mFile->MoveTo(/* directory */ nullptr, logFileFinalDestinationName);

    // Save the file path.
    aLog->mFile = logFileFinalDestination;

    // Log to the error console.
    nsAutoString logPath;
    logFileFinalDestination->GetPath(logPath);
    nsAutoString msg =
        aCollectorKind + u" Collector log dumped to "_ns + logPath;

    // We don't want any JS to run between ScanRoots and CollectWhite calls,
    // and since ScanRoots calls this method, better to log the message
    // asynchronously.
    RefPtr<LogStringMessageAsync> log = new LogStringMessageAsync(msg);
    NS_DispatchToCurrentThread(log);
    return NS_OK;
  }

  int32_t mProcessIdentifier;
  nsString mFilenameIdentifier;
  FileInfo mGCLog;
  FileInfo mCCLog;
};

NS_IMPL_ISUPPORTS(nsCycleCollectorLogSinkToFile, nsICycleCollectorLogSink)

class nsCycleCollectorLogger final : public nsICycleCollectorListener {
  ~nsCycleCollectorLogger() { ClearDescribers(); }

 public:
  nsCycleCollectorLogger()
      : mLogSink(nsCycleCollector_createLogSink()),
        mWantAllTraces(false),
        mDisableLog(false),
        mWantAfterProcessing(false),
        mCCLog(nullptr) {}

  NS_DECL_ISUPPORTS

  void SetAllTraces() { mWantAllTraces = true; }

  bool IsAllTraces() { return mWantAllTraces; }

  NS_IMETHOD AllTraces(nsICycleCollectorListener** aListener) override {
    SetAllTraces();
    NS_ADDREF(*aListener = this);
    return NS_OK;
  }

  NS_IMETHOD GetWantAllTraces(bool* aAllTraces) override {
    *aAllTraces = mWantAllTraces;
    return NS_OK;
  }

  NS_IMETHOD GetDisableLog(bool* aDisableLog) override {
    *aDisableLog = mDisableLog;
    return NS_OK;
  }

  NS_IMETHOD SetDisableLog(bool aDisableLog) override {
    mDisableLog = aDisableLog;
    return NS_OK;
  }

  NS_IMETHOD GetWantAfterProcessing(bool* aWantAfterProcessing) override {
    *aWantAfterProcessing = mWantAfterProcessing;
    return NS_OK;
  }

  NS_IMETHOD SetWantAfterProcessing(bool aWantAfterProcessing) override {
    mWantAfterProcessing = aWantAfterProcessing;
    return NS_OK;
  }

  NS_IMETHOD GetLogSink(nsICycleCollectorLogSink** aLogSink) override {
    NS_ADDREF(*aLogSink = mLogSink);
    return NS_OK;
  }

  NS_IMETHOD SetLogSink(nsICycleCollectorLogSink* aLogSink) override {
    if (!aLogSink) {
      return NS_ERROR_INVALID_ARG;
    }
    mLogSink = aLogSink;
    return NS_OK;
  }

  nsresult Begin() {
    nsresult rv;

    mCurrentAddress.AssignLiteral("0x");
    ClearDescribers();
    if (mDisableLog) {
      return NS_OK;
    }

    FILE* gcLog;
    rv = mLogSink->Open(&gcLog, &mCCLog);
    NS_ENSURE_SUCCESS(rv, rv);
    // Dump the JS heap.
    CollectorData* data = sCollectorData.get();
    if (data && data->mContext) {
      data->mContext->Runtime()->DumpJSHeap(gcLog);
    }
    rv = mLogSink->CloseGCLog();
    NS_ENSURE_SUCCESS(rv, rv);

    fprintf(mCCLog, "# WantAllTraces=%s\n", mWantAllTraces ? "true" : "false");
    return NS_OK;
  }
  void NoteRefCountedObject(uint64_t aAddress, uint32_t aRefCount,
                            const char* aObjectDescription) {
    if (!mDisableLog) {
      fprintf(mCCLog, "%p [rc=%u] %s\n", (void*)aAddress, aRefCount,
              aObjectDescription);
    }
    if (mWantAfterProcessing) {
      CCGraphDescriber* d = new CCGraphDescriber();
      mDescribers.insertBack(d);
      mCurrentAddress.AssignLiteral("0x");
      mCurrentAddress.AppendInt(aAddress, 16);
      d->mType = CCGraphDescriber::eRefCountedObject;
      d->mAddress = mCurrentAddress;
      d->mCnt = aRefCount;
      d->mName.Append(aObjectDescription);
    }
  }
  void NoteGCedObject(uint64_t aAddress, bool aMarked,
                      const char* aObjectDescription,
                      uint64_t aCompartmentAddress) {
    if (!mDisableLog) {
      fprintf(mCCLog, "%p [gc%s] %s\n", (void*)aAddress,
              aMarked ? ".marked" : "", aObjectDescription);
    }
    if (mWantAfterProcessing) {
      CCGraphDescriber* d = new CCGraphDescriber();
      mDescribers.insertBack(d);
      mCurrentAddress.AssignLiteral("0x");
      mCurrentAddress.AppendInt(aAddress, 16);
      d->mType = aMarked ? CCGraphDescriber::eGCMarkedObject
                         : CCGraphDescriber::eGCedObject;
      d->mAddress = mCurrentAddress;
      d->mName.Append(aObjectDescription);
      if (aCompartmentAddress) {
        d->mCompartmentOrToAddress.AssignLiteral("0x");
        d->mCompartmentOrToAddress.AppendInt(aCompartmentAddress, 16);
      } else {
        d->mCompartmentOrToAddress.SetIsVoid(true);
      }
    }
  }
  void NoteEdge(uint64_t aToAddress, const char* aEdgeName) {
    if (!mDisableLog) {
      fprintf(mCCLog, "> %p %s\n", (void*)aToAddress, aEdgeName);
    }
    if (mWantAfterProcessing) {
      CCGraphDescriber* d = new CCGraphDescriber();
      mDescribers.insertBack(d);
      d->mType = CCGraphDescriber::eEdge;
      d->mAddress = mCurrentAddress;
      d->mCompartmentOrToAddress.AssignLiteral("0x");
      d->mCompartmentOrToAddress.AppendInt(aToAddress, 16);
      d->mName.Append(aEdgeName);
    }
  }
  void NoteWeakMapEntry(uint64_t aMap, uint64_t aKey, uint64_t aKeyDelegate,
                        uint64_t aValue) {
    if (!mDisableLog) {
      fprintf(mCCLog, "WeakMapEntry map=%p key=%p keyDelegate=%p value=%p\n",
              (void*)aMap, (void*)aKey, (void*)aKeyDelegate, (void*)aValue);
    }
    // We don't support after-processing for weak map entries.
  }
  void NoteIncrementalRoot(uint64_t aAddress) {
    if (!mDisableLog) {
      fprintf(mCCLog, "IncrementalRoot %p\n", (void*)aAddress);
    }
    // We don't support after-processing for incremental roots.
  }
  void BeginResults() {
    if (!mDisableLog) {
      fputs("==========\n", mCCLog);
    }
  }
  void DescribeRoot(uint64_t aAddress, uint32_t aKnownEdges) {
    if (!mDisableLog) {
      fprintf(mCCLog, "%p [known=%u]\n", (void*)aAddress, aKnownEdges);
    }
    if (mWantAfterProcessing) {
      CCGraphDescriber* d = new CCGraphDescriber();
      mDescribers.insertBack(d);
      d->mType = CCGraphDescriber::eRoot;
      d->mAddress.AppendInt(aAddress, 16);
      d->mCnt = aKnownEdges;
    }
  }
  void DescribeGarbage(uint64_t aAddress) {
    if (!mDisableLog) {
      fprintf(mCCLog, "%p [garbage]\n", (void*)aAddress);
    }
    if (mWantAfterProcessing) {
      CCGraphDescriber* d = new CCGraphDescriber();
      mDescribers.insertBack(d);
      d->mType = CCGraphDescriber::eGarbage;
      d->mAddress.AppendInt(aAddress, 16);
    }
  }
  void End() {
    if (!mDisableLog) {
      mCCLog = nullptr;
      Unused << NS_WARN_IF(NS_FAILED(mLogSink->CloseCCLog()));
    }
  }
  NS_IMETHOD ProcessNext(nsICycleCollectorHandler* aHandler,
                         bool* aCanContinue) override {
    if (NS_WARN_IF(!aHandler) || NS_WARN_IF(!mWantAfterProcessing)) {
      return NS_ERROR_UNEXPECTED;
    }
    CCGraphDescriber* d = mDescribers.popFirst();
    if (d) {
      switch (d->mType) {
        case CCGraphDescriber::eRefCountedObject:
          aHandler->NoteRefCountedObject(d->mAddress, d->mCnt, d->mName);
          break;
        case CCGraphDescriber::eGCedObject:
        case CCGraphDescriber::eGCMarkedObject:
          aHandler->NoteGCedObject(
              d->mAddress, d->mType == CCGraphDescriber::eGCMarkedObject,
              d->mName, d->mCompartmentOrToAddress);
          break;
        case CCGraphDescriber::eEdge:
          aHandler->NoteEdge(d->mAddress, d->mCompartmentOrToAddress, d->mName);
          break;
        case CCGraphDescriber::eRoot:
          aHandler->DescribeRoot(d->mAddress, d->mCnt);
          break;
        case CCGraphDescriber::eGarbage:
          aHandler->DescribeGarbage(d->mAddress);
          break;
        case CCGraphDescriber::eUnknown:
          MOZ_ASSERT_UNREACHABLE("CCGraphDescriber::eUnknown");
          break;
      }
      delete d;
    }
    if (!(*aCanContinue = !mDescribers.isEmpty())) {
      mCurrentAddress.AssignLiteral("0x");
    }
    return NS_OK;
  }
  NS_IMETHOD AsLogger(nsCycleCollectorLogger** aRetVal) override {
    RefPtr<nsCycleCollectorLogger> rval = this;
    rval.forget(aRetVal);
    return NS_OK;
  }

 private:
  void ClearDescribers() {
    CCGraphDescriber* d;
    while ((d = mDescribers.popFirst())) {
      delete d;
    }
  }

  nsCOMPtr<nsICycleCollectorLogSink> mLogSink;
  bool mWantAllTraces;
  bool mDisableLog;
  bool mWantAfterProcessing;
  nsCString mCurrentAddress;
  mozilla::LinkedList<CCGraphDescriber> mDescribers;
  FILE* mCCLog;
};

NS_IMPL_ISUPPORTS(nsCycleCollectorLogger, nsICycleCollectorListener)

already_AddRefed<nsICycleCollectorListener> nsCycleCollector_createLogger() {
  nsCOMPtr<nsICycleCollectorListener> logger = new nsCycleCollectorLogger();
  return logger.forget();
}

static bool GCThingIsGrayCCThing(JS::GCCellPtr thing) {
  return JS::IsCCTraceKind(thing.kind()) && JS::GCThingIsMarkedGrayInCC(thing);
}

static bool ValueIsGrayCCThing(const JS::Value& value) {
  return JS::IsCCTraceKind(value.traceKind()) &&
         JS::GCThingIsMarkedGray(value.toGCCellPtr());
}

////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |MarkRoots| routine.
////////////////////////////////////////////////////////////////////////

class CCGraphBuilder final : public nsCycleCollectionTraversalCallback,
                             public nsCycleCollectionNoteRootCallback {
 private:
  CCGraph& mGraph;
  CycleCollectorResults& mResults;
  NodePool::Builder mNodeBuilder;
  EdgePool::Builder mEdgeBuilder;
  MOZ_INIT_OUTSIDE_CTOR PtrInfo* mCurrPi;
  nsCycleCollectionParticipant* mJSParticipant;
  nsCycleCollectionParticipant* mJSZoneParticipant;
  nsCString mNextEdgeName;
  RefPtr<nsCycleCollectorLogger> mLogger;
  bool mMergeZones;
  UniquePtr<NodePool::Enumerator> mCurrNode;
  uint32_t mNoteChildCount;

  struct PtrInfoCache : public MruCache<void*, PtrInfo*, PtrInfoCache, 491> {
    static HashNumber Hash(const void* aKey) { return HashGeneric(aKey); }
    static bool Match(const void* aKey, const PtrInfo* aVal) {
      return aVal->mPointer == aKey;
    }
  };

  PtrInfoCache mGraphCache;

 public:
  CCGraphBuilder(CCGraph& aGraph, CycleCollectorResults& aResults,
                 CycleCollectedJSRuntime* aCCRuntime,
                 nsCycleCollectorLogger* aLogger, bool aMergeZones);
  virtual ~CCGraphBuilder();

  bool WantAllTraces() const {
    return nsCycleCollectionNoteRootCallback::WantAllTraces();
  }

  bool AddPurpleRoot(void* aRoot, nsCycleCollectionParticipant* aParti);

  // This is called when all roots have been added to the graph, to prepare for
  // BuildGraph().
  void DoneAddingRoots();

  // Do some work traversing nodes in the graph. Returns true if this graph
  // building is finished.
  bool BuildGraph(SliceBudget& aBudget);

  void RemoveCachedEntry(void* aPtr) { mGraphCache.Remove(aPtr); }

 private:
  PtrInfo* AddNode(void* aPtr, nsCycleCollectionParticipant* aParticipant);
  PtrInfo* AddWeakMapNode(JS::GCCellPtr aThing);
  PtrInfo* AddWeakMapNode(JSObject* aObject);

  void SetFirstChild() { mCurrPi->SetFirstChild(mEdgeBuilder.Mark()); }

  void SetLastChild() { mCurrPi->SetLastChild(mEdgeBuilder.Mark()); }

 public:
  // nsCycleCollectionNoteRootCallback methods.
  NS_IMETHOD_(void)
  NoteXPCOMRoot(nsISupports* aRoot,
                nsCycleCollectionParticipant* aParticipant) override;
  NS_IMETHOD_(void) NoteJSRoot(JSObject* aRoot) override;
  NS_IMETHOD_(void)
  NoteNativeRoot(void* aRoot,
                 nsCycleCollectionParticipant* aParticipant) override;
  NS_IMETHOD_(void)
  NoteWeakMapping(JSObject* aMap, JS::GCCellPtr aKey, JSObject* aKdelegate,
                  JS::GCCellPtr aVal) override;
  // This is used to create synthetic non-refcounted references to
  // nsXPCWrappedJS from their wrapped JS objects. No map is needed, because
  // the SubjectToFinalization list is like a known-black weak map, and
  // no delegate is needed because the keys are all unwrapped objects.
  NS_IMETHOD_(void)
  NoteWeakMapping(JSObject* aKey, nsISupports* aVal,
                  nsCycleCollectionParticipant* aValParticipant) override;

  // nsCycleCollectionTraversalCallback methods.
  NS_IMETHOD_(void)
  DescribeRefCountedNode(nsrefcnt aRefCount, const char* aObjName) override;
  NS_IMETHOD_(void)
  DescribeGCedNode(bool aIsMarked, const char* aObjName,
                   uint64_t aCompartmentAddress) override;

  NS_IMETHOD_(void) NoteXPCOMChild(nsISupports* aChild) override;
  NS_IMETHOD_(void) NoteJSChild(JS::GCCellPtr aThing) override;
  NS_IMETHOD_(void)
  NoteNativeChild(void* aChild,
                  nsCycleCollectionParticipant* aParticipant) override;
  NS_IMETHOD_(void) NoteNextEdgeName(const char* aName) override;

 private:
  NS_IMETHOD_(void)
  NoteRoot(void* aRoot, nsCycleCollectionParticipant* aParticipant) {
    MOZ_ASSERT(aRoot);
    MOZ_ASSERT(aParticipant);

    if (!aParticipant->CanSkipInCC(aRoot) || MOZ_UNLIKELY(WantAllTraces())) {
      AddNode(aRoot, aParticipant);
    }
  }

  NS_IMETHOD_(void)
  NoteChild(void* aChild, nsCycleCollectionParticipant* aCp,
            nsCString& aEdgeName) {
    PtrInfo* childPi = AddNode(aChild, aCp);
    if (!childPi) {
      return;
    }
    mEdgeBuilder.Add(childPi);
    if (mLogger) {
      mLogger->NoteEdge((uint64_t)aChild, aEdgeName.get());
    }
    ++childPi->mInternalRefs;
  }

  JS::Zone* MergeZone(JS::GCCellPtr aGcthing) {
    if (!mMergeZones) {
      return nullptr;
    }
    JS::Zone* zone = JS::GetTenuredGCThingZone(aGcthing);
    if (js::IsSystemZone(zone)) {
      return nullptr;
    }
    return zone;
  }
};

CCGraphBuilder::CCGraphBuilder(CCGraph& aGraph, CycleCollectorResults& aResults,
                               CycleCollectedJSRuntime* aCCRuntime,
                               nsCycleCollectorLogger* aLogger,
                               bool aMergeZones)
    : mGraph(aGraph),
      mResults(aResults),
      mNodeBuilder(aGraph.mNodes),
      mEdgeBuilder(aGraph.mEdges),
      mJSParticipant(nullptr),
      mJSZoneParticipant(nullptr),
      mLogger(aLogger),
      mMergeZones(aMergeZones),
      mNoteChildCount(0) {
  // 4096 is an allocation bucket size.
  static_assert(sizeof(CCGraphBuilder) <= 4096,
                "Don't create too large CCGraphBuilder objects");

  if (aCCRuntime) {
    mJSParticipant = aCCRuntime->GCThingParticipant();
    mJSZoneParticipant = aCCRuntime->ZoneParticipant();
  }

  if (mLogger) {
    mFlags |= nsCycleCollectionTraversalCallback::WANT_DEBUG_INFO;
    if (mLogger->IsAllTraces()) {
      mFlags |= nsCycleCollectionTraversalCallback::WANT_ALL_TRACES;
      mWantAllTraces = true;  // for nsCycleCollectionNoteRootCallback
    }
  }

  mMergeZones = mMergeZones && MOZ_LIKELY(!WantAllTraces());

  MOZ_ASSERT(nsCycleCollectionNoteRootCallback::WantAllTraces() ==
             nsCycleCollectionTraversalCallback::WantAllTraces());
}

CCGraphBuilder::~CCGraphBuilder() = default;

PtrInfo* CCGraphBuilder::AddNode(void* aPtr,
                                 nsCycleCollectionParticipant* aParticipant) {
  if (mGraph.mOutOfMemory) {
    return nullptr;
  }

  PtrInfoCache::Entry cached = mGraphCache.Lookup(aPtr);
  if (cached) {
#ifdef DEBUG
    if (cached.Data()->mParticipant != aParticipant) {
      auto* parti1 = cached.Data()->mParticipant;
      auto* parti2 = aParticipant;
      NS_WARNING(
          nsPrintfCString("cached participant: %s; AddNode participant: %s\n",
                          parti1 ? parti1->ClassName() : "null",
                          parti2 ? parti2->ClassName() : "null")
              .get());
    }
#endif
    MOZ_ASSERT(cached.Data()->mParticipant == aParticipant,
               "nsCycleCollectionParticipant shouldn't change!");
    return cached.Data();
  }

  PtrInfo* result;
  auto p = mGraph.mPtrInfoMap.lookupForAdd(aPtr);
  if (!p) {
    // New entry
    result = mNodeBuilder.Add(aPtr, aParticipant);
    if (!result) {
      return nullptr;
    }

    if (!mGraph.mPtrInfoMap.add(p, result)) {
      // `result` leaks here, but we can't free it because it's
      // pool-allocated within NodePool.
      mGraph.mOutOfMemory = true;
      MOZ_ASSERT(false, "OOM while building cycle collector graph");
      return nullptr;
    }

  } else {
    result = *p;
    MOZ_ASSERT(result->mParticipant == aParticipant,
               "nsCycleCollectionParticipant shouldn't change!");
  }

  cached.Set(result);

  return result;
}

bool CCGraphBuilder::AddPurpleRoot(void* aRoot,
                                   nsCycleCollectionParticipant* aParti) {
  ToParticipant(aRoot, &aParti);

  if (WantAllTraces() || !aParti->CanSkipInCC(aRoot)) {
    PtrInfo* pinfo = AddNode(aRoot, aParti);
    if (!pinfo) {
      return false;
    }
  }

  return true;
}

void CCGraphBuilder::DoneAddingRoots() {
  // We've finished adding roots, and everything in the graph is a root.
  mGraph.mRootCount = mGraph.MapCount();

  mCurrNode = MakeUnique<NodePool::Enumerator>(mGraph.mNodes);
}

MOZ_NEVER_INLINE bool CCGraphBuilder::BuildGraph(SliceBudget& aBudget) {
  MOZ_ASSERT(mCurrNode);

  while (!aBudget.isOverBudget() && !mCurrNode->IsDone()) {
    mNoteChildCount = 0;

    PtrInfo* pi = mCurrNode->GetNext();
    if (!pi) {
      MOZ_CRASH();
    }

    mCurrPi = pi;

    // We need to call SetFirstChild() even on deleted nodes, to set their
    // firstChild() that may be read by a prior non-deleted neighbor.
    SetFirstChild();

    if (pi->mParticipant) {
      nsresult rv = pi->mParticipant->TraverseNativeAndJS(pi->mPointer, *this);
      MOZ_RELEASE_ASSERT(!NS_FAILED(rv),
                         "Cycle collector Traverse method failed");
    }

    if (mCurrNode->AtBlockEnd()) {
      SetLastChild();
    }

    aBudget.step(mNoteChildCount + 1);
  }

  if (!mCurrNode->IsDone()) {
    return false;
  }

  if (mGraph.mRootCount > 0) {
    SetLastChild();
  }

  mCurrNode = nullptr;

  return true;
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteXPCOMRoot(nsISupports* aRoot,
                              nsCycleCollectionParticipant* aParticipant) {
  MOZ_ASSERT(aRoot == CanonicalizeXPCOMParticipant(aRoot));

#ifdef DEBUG
  nsXPCOMCycleCollectionParticipant* cp;
  ToParticipant(aRoot, &cp);
  MOZ_ASSERT(aParticipant == cp);
#endif

  NoteRoot(aRoot, aParticipant);
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteJSRoot(JSObject* aRoot) {
  if (JS::Zone* zone = MergeZone(JS::GCCellPtr(aRoot))) {
    NoteRoot(zone, mJSZoneParticipant);
  } else {
    NoteRoot(aRoot, mJSParticipant);
  }
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteNativeRoot(void* aRoot,
                               nsCycleCollectionParticipant* aParticipant) {
  NoteRoot(aRoot, aParticipant);
}

NS_IMETHODIMP_(void)
CCGraphBuilder::DescribeRefCountedNode(nsrefcnt aRefCount,
                                       const char* aObjName) {
  mCurrPi->AnnotatedReleaseAssert(aRefCount != 0,
                                  "CCed refcounted object has zero refcount");
  mCurrPi->AnnotatedReleaseAssert(
      aRefCount != UINT32_MAX,
      "CCed refcounted object has overflowing refcount");

  mResults.mVisitedRefCounted++;

  if (mLogger) {
    mLogger->NoteRefCountedObject((uint64_t)mCurrPi->mPointer, aRefCount,
                                  aObjName);
  }

  mCurrPi->mRefCount = aRefCount;
}

NS_IMETHODIMP_(void)
CCGraphBuilder::DescribeGCedNode(bool aIsMarked, const char* aObjName,
                                 uint64_t aCompartmentAddress) {
  uint32_t refCount = aIsMarked ? UINT32_MAX : 0;
  mResults.mVisitedGCed++;

  if (mLogger) {
    mLogger->NoteGCedObject((uint64_t)mCurrPi->mPointer, aIsMarked, aObjName,
                            aCompartmentAddress);
  }

  mCurrPi->mRefCount = refCount;
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteXPCOMChild(nsISupports* aChild) {
  nsCString edgeName;
  if (WantDebugInfo()) {
    edgeName.Assign(mNextEdgeName);
    mNextEdgeName.Truncate();
  }
  if (!aChild || !(aChild = CanonicalizeXPCOMParticipant(aChild))) {
    return;
  }

  ++mNoteChildCount;

  nsXPCOMCycleCollectionParticipant* cp;
  ToParticipant(aChild, &cp);
  if (cp && (!cp->CanSkipThis(aChild) || WantAllTraces())) {
    NoteChild(aChild, cp, edgeName);
  }
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteNativeChild(void* aChild,
                                nsCycleCollectionParticipant* aParticipant) {
  nsCString edgeName;
  if (WantDebugInfo()) {
    edgeName.Assign(mNextEdgeName);
    mNextEdgeName.Truncate();
  }
  if (!aChild) {
    return;
  }

  ++mNoteChildCount;

  MOZ_ASSERT(aParticipant, "Need a nsCycleCollectionParticipant!");
  if (!aParticipant->CanSkipThis(aChild) || WantAllTraces()) {
    NoteChild(aChild, aParticipant, edgeName);
  }
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteJSChild(JS::GCCellPtr aChild) {
  if (!aChild) {
    return;
  }

  ++mNoteChildCount;

  nsCString edgeName;
  if (MOZ_UNLIKELY(WantDebugInfo())) {
    edgeName.Assign(mNextEdgeName);
    mNextEdgeName.Truncate();
  }

  if (GCThingIsGrayCCThing(aChild) || MOZ_UNLIKELY(WantAllTraces())) {
    if (JS::Zone* zone = MergeZone(aChild)) {
      NoteChild(zone, mJSZoneParticipant, edgeName);
    } else {
      NoteChild(aChild.asCell(), mJSParticipant, edgeName);
    }
  }
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteNextEdgeName(const char* aName) {
  if (WantDebugInfo()) {
    mNextEdgeName = aName;
  }
}

PtrInfo* CCGraphBuilder::AddWeakMapNode(JS::GCCellPtr aNode) {
  MOZ_ASSERT(aNode, "Weak map node should be non-null.");

  if (!GCThingIsGrayCCThing(aNode) && !WantAllTraces()) {
    return nullptr;
  }

  if (JS::Zone* zone = MergeZone(aNode)) {
    return AddNode(zone, mJSZoneParticipant);
  }
  return AddNode(aNode.asCell(), mJSParticipant);
}

PtrInfo* CCGraphBuilder::AddWeakMapNode(JSObject* aObject) {
  return AddWeakMapNode(JS::GCCellPtr(aObject));
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteWeakMapping(JSObject* aMap, JS::GCCellPtr aKey,
                                JSObject* aKdelegate, JS::GCCellPtr aVal) {
  // Don't try to optimize away the entry here, as we've already attempted to
  // do that in TraceWeakMapping in nsXPConnect.
  WeakMapping* mapping = mGraph.mWeakMaps.AppendElement();
  mapping->mMap = aMap ? AddWeakMapNode(aMap) : nullptr;
  mapping->mKey = aKey ? AddWeakMapNode(aKey) : nullptr;
  mapping->mKeyDelegate =
      aKdelegate ? AddWeakMapNode(aKdelegate) : mapping->mKey;
  mapping->mVal = aVal ? AddWeakMapNode(aVal) : nullptr;

  if (mLogger) {
    mLogger->NoteWeakMapEntry((uint64_t)aMap, aKey ? aKey.unsafeAsInteger() : 0,
                              (uint64_t)aKdelegate,
                              aVal ? aVal.unsafeAsInteger() : 0);
  }
}

NS_IMETHODIMP_(void)
CCGraphBuilder::NoteWeakMapping(JSObject* aKey, nsISupports* aVal,
                                nsCycleCollectionParticipant* aValParticipant) {
  MOZ_ASSERT(aKey, "Don't call NoteWeakMapping with a null key");
  MOZ_ASSERT(aVal, "Don't call NoteWeakMapping with a null value");
  WeakMapping* mapping = mGraph.mWeakMaps.AppendElement();
  mapping->mMap = nullptr;
  mapping->mKey = AddWeakMapNode(aKey);
  mapping->mKeyDelegate = mapping->mKey;
  MOZ_ASSERT(js::UncheckedUnwrapWithoutExpose(aKey) == aKey);
  mapping->mVal = AddNode(aVal, aValParticipant);

  if (mLogger) {
    mLogger->NoteWeakMapEntry(0, (uint64_t)aKey, 0, (uint64_t)aVal);
  }
}

static bool AddPurpleRoot(CCGraphBuilder& aBuilder, void* aRoot,
                          nsCycleCollectionParticipant* aParti) {
  return aBuilder.AddPurpleRoot(aRoot, aParti);
}

// MayHaveChild() will be false after a Traverse if the object does
// not have any children the CC will visit.
class ChildFinder : public nsCycleCollectionTraversalCallback {
 public:
  ChildFinder() : mMayHaveChild(false) {}

  // The logic of the Note*Child functions must mirror that of their
  // respective functions in CCGraphBuilder.
  NS_IMETHOD_(void) NoteXPCOMChild(nsISupports* aChild) override;
  NS_IMETHOD_(void)
  NoteNativeChild(void* aChild, nsCycleCollectionParticipant* aHelper) override;
  NS_IMETHOD_(void) NoteJSChild(JS::GCCellPtr aThing) override;

  NS_IMETHOD_(void)
  NoteWeakMapping(JSObject* aKey, nsISupports* aVal,
                  nsCycleCollectionParticipant* aValParticipant) override {}

  NS_IMETHOD_(void)
  DescribeRefCountedNode(nsrefcnt aRefcount, const char* aObjname) override {}
  NS_IMETHOD_(void)
  DescribeGCedNode(bool aIsMarked, const char* aObjname,
                   uint64_t aCompartmentAddress) override {}
  NS_IMETHOD_(void) NoteNextEdgeName(const char* aName) override {}
  bool MayHaveChild() { return mMayHaveChild; }

 private:
  bool mMayHaveChild;
};

NS_IMETHODIMP_(void)
ChildFinder::NoteXPCOMChild(nsISupports* aChild) {
  if (!aChild || !(aChild = CanonicalizeXPCOMParticipant(aChild))) {
    return;
  }
  nsXPCOMCycleCollectionParticipant* cp;
  ToParticipant(aChild, &cp);
  if (cp && !cp->CanSkip(aChild, true)) {
    mMayHaveChild = true;
  }
}

NS_IMETHODIMP_(void)
ChildFinder::NoteNativeChild(void* aChild,
                             nsCycleCollectionParticipant* aHelper) {
  if (!aChild) {
    return;
  }
  MOZ_ASSERT(aHelper, "Native child must have a participant");
  if (!aHelper->CanSkip(aChild, true)) {
    mMayHaveChild = true;
  }
}

NS_IMETHODIMP_(void)
ChildFinder::NoteJSChild(JS::GCCellPtr aChild) {
  if (aChild && JS::GCThingIsMarkedGray(aChild)) {
    mMayHaveChild = true;
  }
}

static bool MayHaveChild(void* aObj, nsCycleCollectionParticipant* aCp) {
  ChildFinder cf;
  aCp->TraverseNativeAndJS(aObj, cf);
  return cf.MayHaveChild();
}

// JSPurpleBuffer keeps references to GCThings which might affect the
// next cycle collection. It is owned only by itself and during unlink its
// self reference is broken down and the object ends up killing itself.
// If GC happens before CC, references to GCthings and the self reference are
// removed.
class JSPurpleBuffer {
  ~JSPurpleBuffer() {
    MOZ_ASSERT(mValues.IsEmpty());
    MOZ_ASSERT(mObjects.IsEmpty());
  }

 public:
  explicit JSPurpleBuffer(RefPtr<JSPurpleBuffer>& aReferenceToThis)
      : mReferenceToThis(aReferenceToThis),
        mValues(kSegmentSize),
        mObjects(kSegmentSize) {
    mReferenceToThis = this;
    mozilla::HoldJSObjects(this);
  }

  void Destroy() {
    RefPtr<JSPurpleBuffer> referenceToThis;
    mReferenceToThis.swap(referenceToThis);
    mValues.Clear();
    mObjects.Clear();
    mozilla::DropJSObjects(this);
  }

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(JSPurpleBuffer)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(JSPurpleBuffer)

  RefPtr<JSPurpleBuffer>& mReferenceToThis;

  // These are raw pointers instead of Heap<T> because we only need Heap<T> for
  // pointers which may point into the nursery. The purple buffer never contains
  // pointers to the nursery because nursery gcthings can never be gray and only
  // gray things can be inserted into the purple buffer.
  static const size_t kSegmentSize = 512;
  SegmentedVector<JS::Value, kSegmentSize, InfallibleAllocPolicy> mValues;
  SegmentedVector<JSObject*, kSegmentSize, InfallibleAllocPolicy> mObjects;
};

NS_IMPL_CYCLE_COLLECTION_CLASS(JSPurpleBuffer)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(JSPurpleBuffer)
  tmp->Destroy();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(JSPurpleBuffer)
  CycleCollectionNoteChild(cb, tmp, "self");
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

#define NS_TRACE_SEGMENTED_ARRAY(_field, _type)                       \
  {                                                                   \
    for (auto iter = tmp->_field.Iter(); !iter.Done(); iter.Next()) { \
      js::gc::CallTraceCallbackOnNonHeap<_type, TraceCallbacks>(      \
          &iter.Get(), aCallbacks, #_field, aClosure);                \
    }                                                                 \
  }

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(JSPurpleBuffer)
  NS_TRACE_SEGMENTED_ARRAY(mValues, JS::Value)
  NS_TRACE_SEGMENTED_ARRAY(mObjects, JSObject*)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

class SnowWhiteKiller : public TraceCallbacks {
  struct SnowWhiteObject {
    void* mPointer;
    nsCycleCollectionParticipant* mParticipant;
    nsCycleCollectingAutoRefCnt* mRefCnt;
  };

  // Segments are 4 KiB on 32-bit and 8 KiB on 64-bit.
  static const size_t kSegmentSize = sizeof(void*) * 1024;
  typedef SegmentedVector<SnowWhiteObject, kSegmentSize, InfallibleAllocPolicy>
      ObjectsVector;

 public:
  SnowWhiteKiller(nsCycleCollector* aCollector, js::SliceBudget* aBudget)
      : mCollector(aCollector),
        mObjects(kSegmentSize),
        mBudget(aBudget),
        mSawSnowWhiteObjects(false) {
    MOZ_ASSERT(mCollector, "Calling SnowWhiteKiller after nsCC went away");
  }

  explicit SnowWhiteKiller(nsCycleCollector* aCollector)
      : SnowWhiteKiller(aCollector, nullptr) {}

  ~SnowWhiteKiller() {
    for (auto iter = mObjects.Iter(); !iter.Done(); iter.Next()) {
      SnowWhiteObject& o = iter.Get();
      MaybeKillObject(o);
    }
  }

 private:
  void MaybeKillObject(SnowWhiteObject& aObject) {
    if (!aObject.mRefCnt->get() && !aObject.mRefCnt->IsInPurpleBuffer()) {
      mCollector->RemoveObjectFromGraph(aObject.mPointer);
      aObject.mRefCnt->stabilizeForDeletion();
      {
        JS::AutoEnterCycleCollection autocc(mCollector->Runtime()->Runtime());
        aObject.mParticipant->Trace(aObject.mPointer, *this, nullptr);
      }
      aObject.mParticipant->DeleteCycleCollectable(aObject.mPointer);
    }
  }

 public:
  bool Visit(nsPurpleBuffer& aBuffer, nsPurpleBufferEntry* aEntry) {
    if (mBudget) {
      if (mBudget->isOverBudget()) {
        return false;
      }
      mBudget->step();
    }

    MOZ_ASSERT(aEntry->mObject, "Null object in purple buffer");
    if (!aEntry->mRefCnt->get()) {
      mSawSnowWhiteObjects = true;
      void* o = aEntry->mObject;
      nsCycleCollectionParticipant* cp = aEntry->mParticipant;
      ToParticipant(o, &cp);
      SnowWhiteObject swo = {o, cp, aEntry->mRefCnt};
      if (!mBudget) {
        mObjects.InfallibleAppend(swo);
      }
      aBuffer.Remove(aEntry);
      if (mBudget) {
        MaybeKillObject(swo);
      }
    }
    return true;
  }

  bool HasSnowWhiteObjects() const { return !mObjects.IsEmpty(); }

  bool SawSnowWhiteObjects() const { return mSawSnowWhiteObjects; }

  virtual void Trace(JS::Heap<JS::Value>* aValue, const char* aName,
                     void* aClosure) const override {
    const JS::Value& val = aValue->unbarrieredGet();
    if (val.isGCThing() && ValueIsGrayCCThing(val)) {
      MOZ_ASSERT(!js::gc::IsInsideNursery(val.toGCThing()));
      mCollector->GetJSPurpleBuffer()->mValues.InfallibleAppend(val);
    }
  }

  virtual void Trace(JS::Heap<jsid>* aId, const char* aName,
                     void* aClosure) const override {}

  void AppendJSObjectToPurpleBuffer(JSObject* obj) const {
    if (obj && JS::ObjectIsMarkedGray(obj)) {
      MOZ_ASSERT(JS::ObjectIsTenured(obj));
      mCollector->GetJSPurpleBuffer()->mObjects.InfallibleAppend(obj);
    }
  }

  virtual void Trace(JS::Heap<JSObject*>* aObject, const char* aName,
                     void* aClosure) const override {
    AppendJSObjectToPurpleBuffer(aObject->unbarrieredGet());
  }

  virtual void Trace(nsWrapperCache* aWrapperCache, const char* aName,
                     void* aClosure) const override {
    AppendJSObjectToPurpleBuffer(aWrapperCache->GetWrapperPreserveColor());
  }

  virtual void Trace(JS::TenuredHeap<JSObject*>* aObject, const char* aName,
                     void* aClosure) const override {
    AppendJSObjectToPurpleBuffer(aObject->unbarrieredGetPtr());
  }

  virtual void Trace(JS::Heap<JSString*>* aString, const char* aName,
                     void* aClosure) const override {}

  virtual void Trace(JS::Heap<JSScript*>* aScript, const char* aName,
                     void* aClosure) const override {}

  virtual void Trace(JS::Heap<JSFunction*>* aFunction, const char* aName,
                     void* aClosure) const override {}

 private:
  RefPtr<nsCycleCollector> mCollector;
  ObjectsVector mObjects;
  js::SliceBudget* mBudget;
  bool mSawSnowWhiteObjects;
};

class RemoveSkippableVisitor : public SnowWhiteKiller {
 public:
  RemoveSkippableVisitor(nsCycleCollector* aCollector, js::SliceBudget& aBudget,
                         bool aRemoveChildlessNodes,
                         bool aAsyncSnowWhiteFreeing,
                         CC_ForgetSkippableCallback aCb)
      : SnowWhiteKiller(aCollector),
        mBudget(aBudget),
        mRemoveChildlessNodes(aRemoveChildlessNodes),
        mAsyncSnowWhiteFreeing(aAsyncSnowWhiteFreeing),
        mDispatchedDeferredDeletion(false),
        mCallback(aCb) {}

  ~RemoveSkippableVisitor() {
    // Note, we must call the callback before SnowWhiteKiller calls
    // DeleteCycleCollectable!
    if (mCallback) {
      mCallback();
    }
    if (HasSnowWhiteObjects()) {
      // Effectively a continuation.
      nsCycleCollector_dispatchDeferredDeletion(true);
    }
  }

  bool Visit(nsPurpleBuffer& aBuffer, nsPurpleBufferEntry* aEntry) {
    if (mBudget.isOverBudget()) {
      return false;
    }

    // CanSkip calls can be a bit slow, so increase the likelihood that
    // isOverBudget actually checks whether we're over the time budget.
    mBudget.step(5);
    MOZ_ASSERT(aEntry->mObject, "null mObject in purple buffer");
    if (!aEntry->mRefCnt->get()) {
      if (!mAsyncSnowWhiteFreeing) {
        SnowWhiteKiller::Visit(aBuffer, aEntry);
      } else if (!mDispatchedDeferredDeletion) {
        mDispatchedDeferredDeletion = true;
        nsCycleCollector_dispatchDeferredDeletion(false);
      }
      return true;
    }
    void* o = aEntry->mObject;
    nsCycleCollectionParticipant* cp = aEntry->mParticipant;
    ToParticipant(o, &cp);
    if (aEntry->mRefCnt->IsPurple() && !cp->CanSkip(o, false) &&
        (!mRemoveChildlessNodes || MayHaveChild(o, cp))) {
      return true;
    }
    aBuffer.Remove(aEntry);
    return true;
  }

 private:
  js::SliceBudget& mBudget;
  bool mRemoveChildlessNodes;
  bool mAsyncSnowWhiteFreeing;
  bool mDispatchedDeferredDeletion;
  CC_ForgetSkippableCallback mCallback;
};

void nsPurpleBuffer::RemoveSkippable(nsCycleCollector* aCollector,
                                     js::SliceBudget& aBudget,
                                     bool aRemoveChildlessNodes,
                                     bool aAsyncSnowWhiteFreeing,
                                     CC_ForgetSkippableCallback aCb) {
  RemoveSkippableVisitor visitor(aCollector, aBudget, aRemoveChildlessNodes,
                                 aAsyncSnowWhiteFreeing, aCb);
  VisitEntries(visitor);
}

bool nsCycleCollector::FreeSnowWhite(bool aUntilNoSWInPurpleBuffer) {
  CheckThreadSafety();

  if (mFreeingSnowWhite) {
    return false;
  }

  AUTO_PROFILER_LABEL_CATEGORY_PAIR(GCCC_FreeSnowWhite);

  AutoRestore<bool> ar(mFreeingSnowWhite);
  mFreeingSnowWhite = true;

  bool hadSnowWhiteObjects = false;
  do {
    SnowWhiteKiller visitor(this);
    mPurpleBuf.VisitEntries(visitor);
    hadSnowWhiteObjects = hadSnowWhiteObjects || visitor.HasSnowWhiteObjects();
    if (!visitor.HasSnowWhiteObjects()) {
      break;
    }
  } while (aUntilNoSWInPurpleBuffer);
  return hadSnowWhiteObjects;
}

bool nsCycleCollector::FreeSnowWhiteWithBudget(js::SliceBudget& aBudget) {
  CheckThreadSafety();

  if (mFreeingSnowWhite) {
    return false;
  }

  AUTO_PROFILER_LABEL_CATEGORY_PAIR(GCCC_FreeSnowWhite);
  AutoRestore<bool> ar(mFreeingSnowWhite);
  mFreeingSnowWhite = true;

  SnowWhiteKiller visitor(this, &aBudget);
  mPurpleBuf.VisitEntries(visitor);
  return visitor.SawSnowWhiteObjects();
  ;
}

void nsCycleCollector::ForgetSkippable(js::SliceBudget& aBudget,
                                       bool aRemoveChildlessNodes,
                                       bool aAsyncSnowWhiteFreeing) {
  CheckThreadSafety();

  if (mFreeingSnowWhite) {
    return;
  }

  mozilla::Maybe<mozilla::AutoGlobalTimelineMarker> marker;
  if (NS_IsMainThread()) {
    marker.emplace("nsCycleCollector::ForgetSkippable",
                   MarkerStackRequest::NO_STACK);
  }

  // If we remove things from the purple buffer during graph building, we may
  // lose track of an object that was mutated during graph building.
  MOZ_ASSERT(IsIdle());

  if (mCCJSRuntime) {
    mCCJSRuntime->PrepareForForgetSkippable();
  }
  MOZ_ASSERT(
      !mScanInProgress,
      "Don't forget skippable or free snow-white while scan is in progress.");
  mPurpleBuf.RemoveSkippable(this, aBudget, aRemoveChildlessNodes,
                             aAsyncSnowWhiteFreeing, mForgetSkippableCB);
}

MOZ_NEVER_INLINE void nsCycleCollector::MarkRoots(SliceBudget& aBudget) {
  JS::AutoAssertNoGC nogc;
  TimeLog timeLog;
  AutoRestore<bool> ar(mScanInProgress);
  MOZ_RELEASE_ASSERT(!mScanInProgress);
  mScanInProgress = true;
  MOZ_ASSERT(mIncrementalPhase == GraphBuildingPhase);

  AUTO_PROFILER_LABEL_CATEGORY_PAIR(GCCC_BuildGraph);
  JS::AutoEnterCycleCollection autocc(Runtime()->Runtime());
  bool doneBuilding = mBuilder->BuildGraph(aBudget);

  if (!doneBuilding) {
    timeLog.Checkpoint("MarkRoots()");
    return;
  }

  mBuilder = nullptr;
  mIncrementalPhase = ScanAndCollectWhitePhase;
  timeLog.Checkpoint("MarkRoots()");
}

////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |ScanRoots| routine.
////////////////////////////////////////////////////////////////////////

struct ScanBlackVisitor {
  ScanBlackVisitor(uint32_t& aWhiteNodeCount, bool& aFailed)
      : mWhiteNodeCount(aWhiteNodeCount), mFailed(aFailed) {}

  bool ShouldVisitNode(PtrInfo const* aPi) { return aPi->mColor != black; }

  MOZ_NEVER_INLINE void VisitNode(PtrInfo* aPi) {
    if (aPi->mColor == white) {
      --mWhiteNodeCount;
    }
    aPi->mColor = black;
  }

  void Failed() { mFailed = true; }

 private:
  uint32_t& mWhiteNodeCount;
  bool& mFailed;
};

static void FloodBlackNode(uint32_t& aWhiteNodeCount, bool& aFailed,
                           PtrInfo* aPi) {
  GraphWalker<ScanBlackVisitor>(ScanBlackVisitor(aWhiteNodeCount, aFailed))
      .Walk(aPi);
  MOZ_ASSERT(aPi->mColor == black || !aPi->WasTraversed(),
             "FloodBlackNode should make aPi black");
}

// Iterate over the WeakMaps.  If we mark anything while iterating
// over the WeakMaps, we must iterate over all of the WeakMaps again.
void nsCycleCollector::ScanWeakMaps() {
  bool anyChanged;
  bool failed = false;
  do {
    anyChanged = false;
    for (uint32_t i = 0; i < mGraph.mWeakMaps.Length(); i++) {
      WeakMapping* wm = &mGraph.mWeakMaps[i];

      // If any of these are null, the original object was marked black.
      uint32_t mColor = wm->mMap ? wm->mMap->mColor : black;
      uint32_t kColor = wm->mKey ? wm->mKey->mColor : black;
      uint32_t kdColor = wm->mKeyDelegate ? wm->mKeyDelegate->mColor : black;
      uint32_t vColor = wm->mVal ? wm->mVal->mColor : black;

      MOZ_ASSERT(mColor != grey, "Uncolored weak map");
      MOZ_ASSERT(kColor != grey, "Uncolored weak map key");
      MOZ_ASSERT(kdColor != grey, "Uncolored weak map key delegate");
      MOZ_ASSERT(vColor != grey, "Uncolored weak map value");

      if (mColor == black && kColor != black && kdColor == black) {
        FloodBlackNode(mWhiteNodeCount, failed, wm->mKey);
        anyChanged = true;
      }

      if (mColor == black && kColor == black && vColor != black) {
        FloodBlackNode(mWhiteNodeCount, failed, wm->mVal);
        anyChanged = true;
      }
    }
  } while (anyChanged);

  if (failed) {
    MOZ_ASSERT(false, "Ran out of memory in ScanWeakMaps");
    CC_TELEMETRY(_OOM, true);
  }
}

// Flood black from any objects in the purple buffer that are in the CC graph.
class PurpleScanBlackVisitor {
 public:
  PurpleScanBlackVisitor(CCGraph& aGraph, nsCycleCollectorLogger* aLogger,
                         uint32_t& aCount, bool& aFailed)
      : mGraph(aGraph), mLogger(aLogger), mCount(aCount), mFailed(aFailed) {}

  bool Visit(nsPurpleBuffer& aBuffer, nsPurpleBufferEntry* aEntry) {
    MOZ_ASSERT(aEntry->mObject,
               "Entries with null mObject shouldn't be in the purple buffer.");
    MOZ_ASSERT(aEntry->mRefCnt->get() != 0,
               "Snow-white objects shouldn't be in the purple buffer.");

    void* obj = aEntry->mObject;

    MOZ_ASSERT(
        aEntry->mParticipant ||
            CanonicalizeXPCOMParticipant(static_cast<nsISupports*>(obj)) == obj,
        "Suspect nsISupports pointer must be canonical");

    PtrInfo* pi = mGraph.FindNode(obj);
    if (!pi) {
      return true;
    }
    MOZ_ASSERT(pi->mParticipant,
               "No dead objects should be in the purple buffer.");
    if (MOZ_UNLIKELY(mLogger)) {
      mLogger->NoteIncrementalRoot((uint64_t)pi->mPointer);
    }
    if (pi->mColor == black) {
      return true;
    }
    FloodBlackNode(mCount, mFailed, pi);
    return true;
  }

 private:
  CCGraph& mGraph;
  RefPtr<nsCycleCollectorLogger> mLogger;
  uint32_t& mCount;
  bool& mFailed;
};

// Objects that have been stored somewhere since the start of incremental graph
// building must be treated as live for this cycle collection, because we may
// not have accurate information about who holds references to them.
void nsCycleCollector::ScanIncrementalRoots() {
  TimeLog timeLog;

  // Reference counted objects:
  // We cleared the purple buffer at the start of the current ICC, so if a
  // refcounted object is purple, it may have been AddRef'd during the current
  // ICC. (It may also have only been released.) If that is the case, we cannot
  // be sure that the set of things pointing to the object in the CC graph
  // is accurate. Therefore, for safety, we treat any purple objects as being
  // live during the current CC. We don't remove anything from the purple
  // buffer here, so these objects will be suspected and freed in the next CC
  // if they are garbage.
  bool failed = false;
  PurpleScanBlackVisitor purpleScanBlackVisitor(mGraph, mLogger,
                                                mWhiteNodeCount, failed);
  mPurpleBuf.VisitEntries(purpleScanBlackVisitor);
  timeLog.Checkpoint("ScanIncrementalRoots::fix purple");

  bool hasJSRuntime = !!mCCJSRuntime;
  nsCycleCollectionParticipant* jsParticipant =
      hasJSRuntime ? mCCJSRuntime->GCThingParticipant() : nullptr;
  nsCycleCollectionParticipant* zoneParticipant =
      hasJSRuntime ? mCCJSRuntime->ZoneParticipant() : nullptr;
  bool hasLogger = !!mLogger;

  NodePool::Enumerator etor(mGraph.mNodes);
  while (!etor.IsDone()) {
    PtrInfo* pi = etor.GetNext();

    // As an optimization, if an object has already been determined to be live,
    // don't consider it further.  We can't do this if there is a listener,
    // because the listener wants to know the complete set of incremental roots.
    if (pi->mColor == black && MOZ_LIKELY(!hasLogger)) {
      continue;
    }

    // Garbage collected objects:
    // If a GCed object was added to the graph with a refcount of zero, and is
    // now marked black by the GC, it was probably gray before and was exposed
    // to active JS, so it may have been stored somewhere, so it needs to be
    // treated as live.
    if (pi->IsGrayJS() && MOZ_LIKELY(hasJSRuntime)) {
      // If the object is still marked gray by the GC, nothing could have gotten
      // hold of it, so it isn't an incremental root.
      if (pi->mParticipant == jsParticipant) {
        JS::GCCellPtr ptr(pi->mPointer, JS::GCThingTraceKind(pi->mPointer));
        if (GCThingIsGrayCCThing(ptr)) {
          continue;
        }
      } else if (pi->mParticipant == zoneParticipant) {
        JS::Zone* zone = static_cast<JS::Zone*>(pi->mPointer);
        if (js::ZoneGlobalsAreAllGray(zone)) {
          continue;
        }
      } else {
        MOZ_ASSERT(false, "Non-JS thing with 0 refcount? Treating as live.");
      }
    } else if (!pi->mParticipant && pi->WasTraversed()) {
      // Dead traversed refcounted objects:
      // If the object was traversed, it must have been alive at the start of
      // the CC, and thus had a positive refcount. It is dead now, so its
      // refcount must have decreased at some point during the CC. Therefore,
      // it would be in the purple buffer if it wasn't dead, so treat it as an
      // incremental root.
      //
      // This should not cause leaks because as the object died it should have
      // released anything it held onto, which will add them to the purple
      // buffer, which will cause them to be considered in the next CC.
    } else {
      continue;
    }

    // At this point, pi must be an incremental root.

    // If there's a listener, tell it about this root. We don't bother with the
    // optimization of skipping the Walk() if pi is black: it will just return
    // without doing anything and there's no need to make this case faster.
    if (MOZ_UNLIKELY(hasLogger) && pi->mPointer) {
      // Dead objects aren't logged. See bug 1031370.
      mLogger->NoteIncrementalRoot((uint64_t)pi->mPointer);
    }

    FloodBlackNode(mWhiteNodeCount, failed, pi);
  }

  timeLog.Checkpoint("ScanIncrementalRoots::fix nodes");

  if (failed) {
    NS_ASSERTION(false, "Ran out of memory in ScanIncrementalRoots");
    CC_TELEMETRY(_OOM, true);
  }
}

// Mark nodes white and make sure their refcounts are ok.
// No nodes are marked black during this pass to ensure that refcount
// checking is run on all nodes not marked black by ScanIncrementalRoots.
void nsCycleCollector::ScanWhiteNodes(bool aFullySynchGraphBuild) {
  NodePool::Enumerator nodeEnum(mGraph.mNodes);
  while (!nodeEnum.IsDone()) {
    PtrInfo* pi = nodeEnum.GetNext();
    if (pi->mColor == black) {
      // Incremental roots can be in a nonsensical state, so don't
      // check them. This will miss checking nodes that are merely
      // reachable from incremental roots.
      MOZ_ASSERT(!aFullySynchGraphBuild,
                 "In a synch CC, no nodes should be marked black early on.");
      continue;
    }
    MOZ_ASSERT(pi->mColor == grey);

    if (!pi->WasTraversed()) {
      // This node was deleted before it was traversed, so there's no reason
      // to look at it.
      MOZ_ASSERT(!pi->mParticipant,
                 "Live nodes should all have been traversed");
      continue;
    }

    if (pi->mInternalRefs == pi->mRefCount || pi->IsGrayJS()) {
      pi->mColor = white;
      ++mWhiteNodeCount;
      continue;
    }

    pi->AnnotatedReleaseAssert(
        pi->mInternalRefs <= pi->mRefCount,
        "More references to an object than its refcount");

    // This node will get marked black in the next pass.
  }
}

// Any remaining grey nodes that haven't already been deleted must be alive,
// so mark them and their children black. Any nodes that are black must have
// already had their children marked black, so there's no need to look at them
// again. This pass may turn some white nodes to black.
void nsCycleCollector::ScanBlackNodes() {
  bool failed = false;
  NodePool::Enumerator nodeEnum(mGraph.mNodes);
  while (!nodeEnum.IsDone()) {
    PtrInfo* pi = nodeEnum.GetNext();
    if (pi->mColor == grey && pi->WasTraversed()) {
      FloodBlackNode(mWhiteNodeCount, failed, pi);
    }
  }

  if (failed) {
    NS_ASSERTION(false, "Ran out of memory in ScanBlackNodes");
    CC_TELEMETRY(_OOM, true);
  }
}

void nsCycleCollector::ScanRoots(bool aFullySynchGraphBuild) {
  JS::AutoAssertNoGC nogc;
  AutoRestore<bool> ar(mScanInProgress);
  MOZ_RELEASE_ASSERT(!mScanInProgress);
  mScanInProgress = true;
  mWhiteNodeCount = 0;
  MOZ_ASSERT(mIncrementalPhase == ScanAndCollectWhitePhase);

  JS::AutoEnterCycleCollection autocc(Runtime()->Runtime());

  if (!aFullySynchGraphBuild) {
    ScanIncrementalRoots();
  }

  TimeLog timeLog;
  ScanWhiteNodes(aFullySynchGraphBuild);
  timeLog.Checkpoint("ScanRoots::ScanWhiteNodes");

  ScanBlackNodes();
  timeLog.Checkpoint("ScanRoots::ScanBlackNodes");

  // Scanning weak maps must be done last.
  ScanWeakMaps();
  timeLog.Checkpoint("ScanRoots::ScanWeakMaps");

  if (mLogger) {
    mLogger->BeginResults();

    NodePool::Enumerator etor(mGraph.mNodes);
    while (!etor.IsDone()) {
      PtrInfo* pi = etor.GetNext();
      if (!pi->WasTraversed()) {
        continue;
      }
      switch (pi->mColor) {
        case black:
          if (!pi->IsGrayJS() && !pi->IsBlackJS() &&
              pi->mInternalRefs != pi->mRefCount) {
            mLogger->DescribeRoot((uint64_t)pi->mPointer, pi->mInternalRefs);
          }
          break;
        case white:
          mLogger->DescribeGarbage((uint64_t)pi->mPointer);
          break;
        case grey:
          MOZ_ASSERT(false, "All traversed objects should be black or white");
          break;
      }
    }

    mLogger->End();
    mLogger = nullptr;
    timeLog.Checkpoint("ScanRoots::listener");
  }
}

////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |CollectWhite| routine, somewhat modified.
////////////////////////////////////////////////////////////////////////

bool nsCycleCollector::CollectWhite() {
  // Explanation of "somewhat modified": we have no way to collect the
  // set of whites "all at once", we have to ask each of them to drop
  // their outgoing links and assume this will cause the garbage cycle
  // to *mostly* self-destruct (except for the reference we continue
  // to hold).
  //
  // To do this "safely" we must make sure that the white nodes we're
  // operating on are stable for the duration of our operation. So we
  // make 3 sets of calls to language runtimes:
  //
  //   - Root(whites), which should pin the whites in memory.
  //   - Unlink(whites), which drops outgoing links on each white.
  //   - Unroot(whites), which returns the whites to normal GC.

  ClearWhiteJSWeakRefTargets();

  // Segments are 4 KiB on 32-bit and 8 KiB on 64-bit.
  static const size_t kSegmentSize = sizeof(void*) * 1024;
  SegmentedVector<PtrInfo*, kSegmentSize, InfallibleAllocPolicy> whiteNodes(
      kSegmentSize);
  TimeLog timeLog;

  MOZ_ASSERT(mIncrementalPhase == ScanAndCollectWhitePhase);

  uint32_t numWhiteNodes = 0;
  uint32_t numWhiteGCed = 0;
  uint32_t numWhiteJSZones = 0;

  {
    JS::AutoAssertNoGC nogc;
    bool hasJSRuntime = !!mCCJSRuntime;
    nsCycleCollectionParticipant* zoneParticipant =
        hasJSRuntime ? mCCJSRuntime->ZoneParticipant() : nullptr;

    NodePool::Enumerator etor(mGraph.mNodes);
    while (!etor.IsDone()) {
      PtrInfo* pinfo = etor.GetNext();
      if (pinfo->mColor == white && pinfo->mParticipant) {
        if (pinfo->IsGrayJS()) {
          MOZ_ASSERT(mCCJSRuntime);
          ++numWhiteGCed;
          JS::Zone* zone;
          if (MOZ_UNLIKELY(pinfo->mParticipant == zoneParticipant)) {
            ++numWhiteJSZones;
            zone = static_cast<JS::Zone*>(pinfo->mPointer);
          } else {
            JS::GCCellPtr ptr(pinfo->mPointer,
                              JS::GCThingTraceKind(pinfo->mPointer));
            zone = JS::GetTenuredGCThingZone(ptr);
          }
          mCCJSRuntime->AddZoneWaitingForGC(zone);
        } else {
          whiteNodes.InfallibleAppend(pinfo);
          pinfo->mParticipant->Root(pinfo->mPointer);
          ++numWhiteNodes;
        }
      }
    }
  }

  mResults.mFreedRefCounted += numWhiteNodes;
  mResults.mFreedGCed += numWhiteGCed;
  mResults.mFreedJSZones += numWhiteJSZones;

  timeLog.Checkpoint("CollectWhite::Root");

  if (mBeforeUnlinkCB) {
    mBeforeUnlinkCB();
    timeLog.Checkpoint("CollectWhite::BeforeUnlinkCB");
  }

  // Unlink() can trigger a GC, so do not touch any JS or anything
  // else not in whiteNodes after here.

  for (auto iter = whiteNodes.Iter(); !iter.Done(); iter.Next()) {
    PtrInfo* pinfo = iter.Get();
    MOZ_ASSERT(pinfo->mParticipant,
               "Unlink shouldn't see objects removed from graph.");
    pinfo->mParticipant->Unlink(pinfo->mPointer);
#ifdef DEBUG
    if (mCCJSRuntime) {
      mCCJSRuntime->AssertNoObjectsToTrace(pinfo->mPointer);
    }
#endif
  }
  timeLog.Checkpoint("CollectWhite::Unlink");

  JS::AutoAssertNoGC nogc;
  for (auto iter = whiteNodes.Iter(); !iter.Done(); iter.Next()) {
    PtrInfo* pinfo = iter.Get();
    MOZ_ASSERT(pinfo->mParticipant,
               "Unroot shouldn't see objects removed from graph.");
    pinfo->mParticipant->Unroot(pinfo->mPointer);
  }
  timeLog.Checkpoint("CollectWhite::Unroot");

  nsCycleCollector_dispatchDeferredDeletion(false, true);
  timeLog.Checkpoint("CollectWhite::dispatchDeferredDeletion");

  mIncrementalPhase = CleanupPhase;

  return numWhiteNodes > 0 || numWhiteGCed > 0 || numWhiteJSZones > 0;
}

static bool IsGCThingWhiteInCCGraph(JS::GCCellPtr aPtr, void* aData) {
  auto* cc = static_cast<nsCycleCollector*>(aData);
  return cc->IsGCThingWhiteInCCGraph(aPtr);
}

bool nsCycleCollector::IsGCThingWhiteInCCGraph(JS::GCCellPtr aPtr) {
  PtrInfo* pinfo = mGraph.FindNode(aPtr.asCell());
  if (!pinfo) {
    return false;
  }

  MOZ_ASSERT(pinfo->mParticipant);
  bool isWhite = pinfo->mColor == white;

  MOZ_ASSERT_IF(isWhite, pinfo->IsGrayJS());
  return isWhite;
}

void nsCycleCollector::ClearWhiteJSWeakRefTargets() {
  // Clear the targets of JS WeakRef objects whose target is part of a cycle
  // that we're about to unlink.
  JSRuntime* runtime = Runtime()->Runtime();
  JS::MaybeClearWeakRefTargets(runtime, &::IsGCThingWhiteInCCGraph, this);
}

////////////////////////
// Memory reporting
////////////////////////

MOZ_DEFINE_MALLOC_SIZE_OF(CycleCollectorMallocSizeOf)

NS_IMETHODIMP
nsCycleCollector::CollectReports(nsIHandleReportCallback* aHandleReport,
                                 nsISupports* aData, bool aAnonymize) {
  size_t objectSize, graphSize, purpleBufferSize;
  SizeOfIncludingThis(CycleCollectorMallocSizeOf, &objectSize, &graphSize,
                      &purpleBufferSize);

  if (objectSize > 0) {
    MOZ_COLLECT_REPORT("explicit/cycle-collector/collector-object", KIND_HEAP,
                       UNITS_BYTES, objectSize,
                       "Memory used for the cycle collector object itself.");
  }

  if (graphSize > 0) {
    MOZ_COLLECT_REPORT(
        "explicit/cycle-collector/graph", KIND_HEAP, UNITS_BYTES, graphSize,
        "Memory used for the cycle collector's graph. This should be zero when "
        "the collector is idle.");
  }

  if (purpleBufferSize > 0) {
    MOZ_COLLECT_REPORT("explicit/cycle-collector/purple-buffer", KIND_HEAP,
                       UNITS_BYTES, purpleBufferSize,
                       "Memory used for the cycle collector's purple buffer.");
  }

  return NS_OK;
};

////////////////////////////////////////////////////////////////////////
// Collector implementation
////////////////////////////////////////////////////////////////////////

nsCycleCollector::nsCycleCollector()
    : mActivelyCollecting(false),
      mFreeingSnowWhite(false),
      mScanInProgress(false),
      mCCJSRuntime(nullptr),
      mIncrementalPhase(IdlePhase),
#ifdef DEBUG
      mEventTarget(GetCurrentSerialEventTarget()),
#endif
      mWhiteNodeCount(0),
      mBeforeUnlinkCB(nullptr),
      mForgetSkippableCB(nullptr),
      mUnmergedNeeded(0),
      mMergedInARow(0) {
}

nsCycleCollector::~nsCycleCollector() {
  MOZ_ASSERT(!mJSPurpleBuffer, "Didn't call JSPurpleBuffer::Destroy?");

  UnregisterWeakMemoryReporter(this);
}

void nsCycleCollector::SetCCJSRuntime(CycleCollectedJSRuntime* aCCRuntime) {
  MOZ_RELEASE_ASSERT(
      !mCCJSRuntime,
      "Multiple registrations of CycleCollectedJSRuntime in cycle collector");
  mCCJSRuntime = aCCRuntime;

  if (!NS_IsMainThread()) {
    return;
  }

  // We can't register as a reporter in nsCycleCollector() because that runs
  // before the memory reporter manager is initialized.  So we do it here
  // instead.
  RegisterWeakMemoryReporter(this);
}

void nsCycleCollector::ClearCCJSRuntime() {
  MOZ_RELEASE_ASSERT(mCCJSRuntime,
                     "Clearing CycleCollectedJSRuntime in cycle collector "
                     "before a runtime was registered");
  mCCJSRuntime = nullptr;
}

#ifdef DEBUG
static bool HasParticipant(void* aPtr, nsCycleCollectionParticipant* aParti) {
  if (aParti) {
    return true;
  }

  nsXPCOMCycleCollectionParticipant* xcp;
  ToParticipant(static_cast<nsISupports*>(aPtr), &xcp);
  return xcp != nullptr;
}
#endif

MOZ_ALWAYS_INLINE void nsCycleCollector::Suspect(
    void* aPtr, nsCycleCollectionParticipant* aParti,
    nsCycleCollectingAutoRefCnt* aRefCnt) {
  CheckThreadSafety();

  // Don't call AddRef or Release of a CCed object in a Traverse() method.
  MOZ_ASSERT(!mScanInProgress,
             "Attempted to call Suspect() while a scan was in progress");

  if (MOZ_UNLIKELY(mScanInProgress)) {
    return;
  }

  MOZ_ASSERT(aPtr, "Don't suspect null pointers");

  MOZ_ASSERT(HasParticipant(aPtr, aParti),
             "Suspected nsISupports pointer must QI to "
             "nsXPCOMCycleCollectionParticipant");

  MOZ_ASSERT(aParti || CanonicalizeXPCOMParticipant(
                           static_cast<nsISupports*>(aPtr)) == aPtr,
             "Suspect nsISupports pointer must be canonical");

  mPurpleBuf.Put(aPtr, aParti, aRefCnt);
}

void nsCycleCollector::SuspectNurseryEntries() {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  while (gNurseryPurpleBufferEntryCount) {
    NurseryPurpleBufferEntry& entry =
        gNurseryPurpleBufferEntry[--gNurseryPurpleBufferEntryCount];
    mPurpleBuf.Put(entry.mPtr, entry.mParticipant, entry.mRefCnt);
  }
}

void nsCycleCollector::CheckThreadSafety() {
#ifdef DEBUG
  MOZ_ASSERT(mEventTarget->IsOnCurrentThread());
#endif
}

// The cycle collector uses the mark bitmap to discover what JS objects are
// reachable only from XPConnect roots that might participate in cycles. We ask
// the JS runtime whether we need to force a GC before this CC. It should only
// be true when UnmarkGray has run out of stack. We also force GCs on shutdown
// to collect cycles involving both DOM and JS, and in WantAllTraces CCs to
// prevent hijinks from ForgetSkippable and compartmental GCs.
void nsCycleCollector::FixGrayBits(bool aIsShutdown, TimeLog& aTimeLog) {
  CheckThreadSafety();

  if (!mCCJSRuntime) {
    return;
  }

  // If we're not forcing a GC anyways due to shutdown or an all traces CC,
  // check to see if we still need to do one to fix the gray bits.
  if (!(aIsShutdown || (mLogger && mLogger->IsAllTraces()))) {
    mCCJSRuntime->FixWeakMappingGrayBits();
    aTimeLog.Checkpoint("FixWeakMappingGrayBits");

    bool needGC = !mCCJSRuntime->AreGCGrayBitsValid();
    // Only do a telemetry ping for non-shutdown CCs.
    CC_TELEMETRY(_NEED_GC, needGC);
    if (!needGC) {
      return;
    }
  }

  mResults.mForcedGC = true;

  uint32_t count = 0;
  do {
    if (aIsShutdown) {
      mCCJSRuntime->GarbageCollect(JS::GCOptions::Shutdown,
                                   JS::GCReason::SHUTDOWN_CC);
    } else {
      mCCJSRuntime->GarbageCollect(JS::GCOptions::Normal,
                                   JS::GCReason::CC_FORCED);
    }

    mCCJSRuntime->FixWeakMappingGrayBits();

    // It's possible that FixWeakMappingGrayBits will hit OOM when unmarking
    // gray and we will have to go round again. The second time there should not
    // be any weak mappings to fix up so the loop body should run at most twice.
    MOZ_RELEASE_ASSERT(count < 2);
    count++;
  } while (!mCCJSRuntime->AreGCGrayBitsValid());

  aTimeLog.Checkpoint("FixGrayBits");
}

bool nsCycleCollector::IsIncrementalGCInProgress() {
  return mCCJSRuntime && JS::IsIncrementalGCInProgress(mCCJSRuntime->Runtime());
}

void nsCycleCollector::FinishAnyIncrementalGCInProgress() {
  if (IsIncrementalGCInProgress()) {
    NS_WARNING("Finishing incremental GC in progress during CC");
    JSContext* cx = CycleCollectedJSContext::Get()->Context();
    JS::PrepareForIncrementalGC(cx);
    JS::FinishIncrementalGC(cx, JS::GCReason::CC_FORCED);
  }
}

void nsCycleCollector::CleanupAfterCollection() {
  TimeLog timeLog;
  MOZ_ASSERT(mIncrementalPhase == CleanupPhase);
  MOZ_RELEASE_ASSERT(!mScanInProgress);
  mGraph.Clear();
  timeLog.Checkpoint("CleanupAfterCollection::mGraph.Clear()");

  uint32_t interval =
      (uint32_t)((TimeStamp::Now() - mCollectionStart).ToMilliseconds());
#ifdef COLLECT_TIME_DEBUG
  printf("cc: total cycle collector time was %ums in %u slices\n", interval,
         mResults.mNumSlices);
  printf(
      "cc: visited %u ref counted and %u GCed objects, freed %d ref counted "
      "and %d GCed objects",
      mResults.mVisitedRefCounted, mResults.mVisitedGCed,
      mResults.mFreedRefCounted, mResults.mFreedGCed);
  uint32_t numVisited = mResults.mVisitedRefCounted + mResults.mVisitedGCed;
  if (numVisited > 1000) {
    uint32_t numFreed = mResults.mFreedRefCounted + mResults.mFreedGCed;
    printf(" (%d%%)", 100 * numFreed / numVisited);
  }
  printf(".\ncc: \n");
#endif

  CC_TELEMETRY(, interval);
  CC_TELEMETRY(_VISITED_REF_COUNTED, mResults.mVisitedRefCounted);
  CC_TELEMETRY(_VISITED_GCED, mResults.mVisitedGCed);
  CC_TELEMETRY(_COLLECTED, mWhiteNodeCount);
  timeLog.Checkpoint("CleanupAfterCollection::telemetry");

  if (mCCJSRuntime) {
    mCCJSRuntime->FinalizeDeferredThings(
        mResults.mAnyManual ? CycleCollectedJSRuntime::FinalizeNow
                            : CycleCollectedJSRuntime::FinalizeIncrementally);
    mCCJSRuntime->EndCycleCollectionCallback(mResults);
    timeLog.Checkpoint("CleanupAfterCollection::EndCycleCollectionCallback()");
  }
  mIncrementalPhase = IdlePhase;
}

void nsCycleCollector::ShutdownCollect() {
  FinishAnyIncrementalGCInProgress();
  CycleCollectedJSContext* ccJSContext = CycleCollectedJSContext::Get();
  JS::ShutdownAsyncTasks(ccJSContext->Context());

  SliceBudget unlimitedBudget = SliceBudget::unlimited();
  uint32_t i;
  bool collectedAny = true;
  for (i = 0; i < DEFAULT_SHUTDOWN_COLLECTIONS && collectedAny; ++i) {
    collectedAny = Collect(CCReason::SHUTDOWN, ccIsManual::CCIsManual,
                           unlimitedBudget, nullptr);
    // Run any remaining tasks that may have been enqueued via RunInStableState
    // or DispatchToMicroTask. These can hold alive CCed objects, and we want to
    // clear them out before we run the CC again or finish shutting down.
    ccJSContext->PerformMicroTaskCheckPoint(true);
    ccJSContext->ProcessStableStateQueue();
  }
  NS_WARNING_ASSERTION(i < NORMAL_SHUTDOWN_COLLECTIONS, "Extra shutdown CC");
}

static void PrintPhase(const char* aPhase) {
#ifdef DEBUG_PHASES
  printf("cc: begin %s on %s\n", aPhase,
         NS_IsMainThread() ? "mainthread" : "worker");
#endif
}

bool nsCycleCollector::Collect(CCReason aReason, ccIsManual aIsManual,
                               SliceBudget& aBudget,
                               nsICycleCollectorListener* aManualListener,
                               bool aPreferShorterSlices) {
  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("Incremental CC", GCCC);

  CheckThreadSafety();

  // This can legitimately happen in a few cases. See bug 383651.
  if (mActivelyCollecting || mFreeingSnowWhite) {
    return false;
  }
  mActivelyCollecting = true;

  MOZ_ASSERT(!IsIncrementalGCInProgress());

  mozilla::Maybe<mozilla::AutoGlobalTimelineMarker> marker;
  if (NS_IsMainThread()) {
    marker.emplace("nsCycleCollector::Collect", MarkerStackRequest::NO_STACK);
  }

  bool startedIdle = IsIdle();
  bool collectedAny = false;

  // If the CC started idle, it will call BeginCollection, which
  // will do FreeSnowWhite, so it doesn't need to be done here.
  if (!startedIdle) {
    TimeLog timeLog;
    FreeSnowWhite(true);
    timeLog.Checkpoint("Collect::FreeSnowWhite");
  }

  if (aIsManual == ccIsManual::CCIsManual) {
    mResults.mAnyManual = true;
  }

  ++mResults.mNumSlices;

  bool continueSlice = aBudget.isUnlimited() || !aPreferShorterSlices;
  do {
    switch (mIncrementalPhase) {
      case IdlePhase:
        PrintPhase("BeginCollection");
        BeginCollection(aReason, aIsManual, aManualListener);
        break;
      case GraphBuildingPhase:
        PrintPhase("MarkRoots");
        MarkRoots(aBudget);

        // Only continue this slice if we're running synchronously or the
        // next phase will probably be short, to reduce the max pause for this
        // collection.
        // (There's no need to check if we've finished graph building, because
        // if we haven't, we've already exceeded our budget, and will finish
        // this slice anyways.)
        continueSlice = aBudget.isUnlimited() ||
                        (mResults.mNumSlices < 3 && !aPreferShorterSlices);
        break;
      case ScanAndCollectWhitePhase:
        // We do ScanRoots and CollectWhite in a single slice to ensure
        // that we won't unlink a live object if a weak reference is
        // promoted to a strong reference after ScanRoots has finished.
        // See bug 926533.
        {
          AUTO_PROFILER_LABEL_CATEGORY_PAIR(GCCC_ScanRoots);
          PrintPhase("ScanRoots");
          ScanRoots(startedIdle);
        }
        {
          AUTO_PROFILER_LABEL_CATEGORY_PAIR(GCCC_CollectWhite);
          PrintPhase("CollectWhite");
          collectedAny = CollectWhite();
        }
        break;
      case CleanupPhase:
        PrintPhase("CleanupAfterCollection");
        CleanupAfterCollection();
        continueSlice = false;
        break;
    }
    if (continueSlice) {
      aBudget.stepAndForceCheck();
      continueSlice = !aBudget.isOverBudget();
    }
  } while (continueSlice);

  // Clear mActivelyCollecting here to ensure that a recursive call to
  // Collect() does something.
  mActivelyCollecting = false;

  if (aIsManual && !startedIdle) {
    // We were in the middle of an incremental CC (using its own listener).
    // Somebody has forced a CC, so after having finished out the current CC,
    // run the CC again using the new listener.
    MOZ_ASSERT(IsIdle());
    if (Collect(aReason, ccIsManual::CCIsManual, aBudget, aManualListener)) {
      collectedAny = true;
    }
  }

  MOZ_ASSERT_IF(aIsManual == CCIsManual, IsIdle());

  return collectedAny;
}

// Any JS objects we have in the graph could die when we GC, but we
// don't want to abandon the current CC, because the graph contains
// information about purple roots. So we synchronously finish off
// the current CC.
void nsCycleCollector::PrepareForGarbageCollection() {
  if (IsIdle()) {
    MOZ_ASSERT(mGraph.IsEmpty(), "Non-empty graph when idle");
    MOZ_ASSERT(!mBuilder, "Non-null builder when idle");
    if (mJSPurpleBuffer) {
      mJSPurpleBuffer->Destroy();
    }
    return;
  }

  FinishAnyCurrentCollection(CCReason::GC_WAITING);
}

void nsCycleCollector::FinishAnyCurrentCollection(CCReason aReason) {
  if (IsIdle()) {
    return;
  }

  SliceBudget unlimitedBudget = SliceBudget::unlimited();
  PrintPhase("FinishAnyCurrentCollection");
  // Use CCIsNotManual because we only want to finish the CC in progress.
  Collect(aReason, ccIsManual::CCIsNotManual, unlimitedBudget, nullptr);

  // It is only okay for Collect() to have failed to finish the
  // current CC if we're reentering the CC at some point past
  // graph building. We need to be past the point where the CC will
  // look at JS objects so that it is safe to GC.
  MOZ_ASSERT(IsIdle() || (mActivelyCollecting &&
                          mIncrementalPhase != GraphBuildingPhase),
             "Reentered CC during graph building");
}

// Don't merge too many times in a row, and do at least a minimum
// number of unmerged CCs in a row.
static const uint32_t kMinConsecutiveUnmerged = 3;
static const uint32_t kMaxConsecutiveMerged = 3;

bool nsCycleCollector::ShouldMergeZones(ccIsManual aIsManual) {
  if (!mCCJSRuntime) {
    return false;
  }

  MOZ_ASSERT(mUnmergedNeeded <= kMinConsecutiveUnmerged);
  MOZ_ASSERT(mMergedInARow <= kMaxConsecutiveMerged);

  if (mMergedInARow == kMaxConsecutiveMerged) {
    MOZ_ASSERT(mUnmergedNeeded == 0);
    mUnmergedNeeded = kMinConsecutiveUnmerged;
  }

  if (mUnmergedNeeded > 0) {
    mUnmergedNeeded--;
    mMergedInARow = 0;
    return false;
  }

  if (aIsManual == CCIsNotManual && mCCJSRuntime->UsefulToMergeZones()) {
    mMergedInARow++;
    return true;
  } else {
    mMergedInARow = 0;
    return false;
  }
}

void nsCycleCollector::BeginCollection(
    CCReason aReason, ccIsManual aIsManual,
    nsICycleCollectorListener* aManualListener) {
  TimeLog timeLog;
  MOZ_ASSERT(IsIdle());
  MOZ_RELEASE_ASSERT(!mScanInProgress);

  mCollectionStart = TimeStamp::Now();

  if (mCCJSRuntime) {
    mCCJSRuntime->BeginCycleCollectionCallback(aReason);
    timeLog.Checkpoint("BeginCycleCollectionCallback()");
  }

  bool isShutdown = (aReason == CCReason::SHUTDOWN);
  if (isShutdown) {
    mShutdownCount += 1;
  }

  // Set up the listener for this CC.
  MOZ_ASSERT_IF(isShutdown, !aManualListener);
  MOZ_ASSERT(!mLogger, "Forgot to clear a previous listener?");

  if (aManualListener) {
    aManualListener->AsLogger(getter_AddRefs(mLogger));
  }

  aManualListener = nullptr;
  if (!mLogger && mParams.LogThisCC(mShutdownCount)) {
    mLogger = new nsCycleCollectorLogger();
    if (mParams.AllTracesThisCC(isShutdown)) {
      mLogger->SetAllTraces();
    }
  }

  // BeginCycleCollectionCallback() might have started an IGC, and we need
  // to finish it before we run FixGrayBits.
  FinishAnyIncrementalGCInProgress();
  timeLog.Checkpoint("Pre-FixGrayBits finish IGC");

  FixGrayBits(isShutdown, timeLog);
  if (mCCJSRuntime) {
    mCCJSRuntime->CheckGrayBits();
  }

  FreeSnowWhite(true);
  timeLog.Checkpoint("BeginCollection FreeSnowWhite");

  if (mLogger && NS_FAILED(mLogger->Begin())) {
    mLogger = nullptr;
  }

  // FreeSnowWhite could potentially have started an IGC, which we need
  // to finish before we look at any JS roots.
  FinishAnyIncrementalGCInProgress();
  timeLog.Checkpoint("Post-FreeSnowWhite finish IGC");

  // Set up the data structures for building the graph.
  JS::AutoAssertNoGC nogc;
  JS::AutoEnterCycleCollection autocc(mCCJSRuntime->Runtime());
  mGraph.Init();
  mResults.Init();
  mResults.mSuspectedAtCCStart = SuspectedCount();
  mResults.mAnyManual = aIsManual;
  bool mergeZones = ShouldMergeZones(aIsManual);
  mResults.mMergedZones = mergeZones;

  MOZ_ASSERT(!mBuilder, "Forgot to clear mBuilder");
  mBuilder = MakeUnique<CCGraphBuilder>(mGraph, mResults, mCCJSRuntime, mLogger,
                                        mergeZones);
  timeLog.Checkpoint("BeginCollection prepare graph builder");

  if (mCCJSRuntime) {
    mCCJSRuntime->TraverseRoots(*mBuilder);
    timeLog.Checkpoint("mJSContext->TraverseRoots()");
  }

  AutoRestore<bool> ar(mScanInProgress);
  MOZ_RELEASE_ASSERT(!mScanInProgress);
  mScanInProgress = true;
  mPurpleBuf.SelectPointers(*mBuilder);
  timeLog.Checkpoint("SelectPointers()");

  mBuilder->DoneAddingRoots();
  mIncrementalPhase = GraphBuildingPhase;
}

uint32_t nsCycleCollector::SuspectedCount() {
  CheckThreadSafety();
  if (NS_IsMainThread()) {
    return gNurseryPurpleBufferEntryCount + mPurpleBuf.Count();
  }

  return mPurpleBuf.Count();
}

void nsCycleCollector::Shutdown(bool aDoCollect) {
  CheckThreadSafety();

  if (NS_IsMainThread()) {
    gNurseryPurpleBufferEnabled = false;
  }

  // Always delete snow white objects.
  FreeSnowWhite(true);

  if (aDoCollect) {
    ShutdownCollect();
  }

  if (mJSPurpleBuffer) {
    mJSPurpleBuffer->Destroy();
  }
}

void nsCycleCollector::RemoveObjectFromGraph(void* aObj) {
  if (IsIdle()) {
    return;
  }

  mGraph.RemoveObjectFromMap(aObj);
  if (mBuilder) {
    mBuilder->RemoveCachedEntry(aObj);
  }
}

void nsCycleCollector::SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                                           size_t* aObjectSize,
                                           size_t* aGraphSize,
                                           size_t* aPurpleBufferSize) const {
  *aObjectSize = aMallocSizeOf(this);

  *aGraphSize = mGraph.SizeOfExcludingThis(aMallocSizeOf);

  *aPurpleBufferSize = mPurpleBuf.SizeOfExcludingThis(aMallocSizeOf);

  // These fields are deliberately not measured:
  // - mCCJSRuntime: because it's non-owning and measured by JS reporters.
  // - mParams: because it only contains scalars.
}

JSPurpleBuffer* nsCycleCollector::GetJSPurpleBuffer() {
  if (!mJSPurpleBuffer) {
    // The Release call here confuses the GC analysis.
    JS::AutoSuppressGCAnalysis nogc;
    // JSPurpleBuffer keeps itself alive, but we need to create it in such way
    // that it ends up in the normal purple buffer. That happens when
    // nsRefPtr goes out of the scope and calls Release.
    RefPtr<JSPurpleBuffer> pb = new JSPurpleBuffer(mJSPurpleBuffer);
  }
  return mJSPurpleBuffer;
}

////////////////////////////////////////////////////////////////////////
// Module public API (exported in nsCycleCollector.h)
// Just functions that redirect into the singleton, once it's built.
////////////////////////////////////////////////////////////////////////

void nsCycleCollector_registerJSContext(CycleCollectedJSContext* aCx) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);
  // But we shouldn't already have a context.
  MOZ_ASSERT(!data->mContext);

  data->mContext = aCx;
  data->mCollector->SetCCJSRuntime(aCx->Runtime());
}

void nsCycleCollector_forgetJSContext() {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  // And we shouldn't have already forgotten our context.
  MOZ_ASSERT(data->mContext);

  // But it may have shutdown already.
  if (data->mCollector) {
    data->mCollector->ClearCCJSRuntime();
    data->mContext = nullptr;
  } else {
    data->mContext = nullptr;
    delete data;
    sCollectorData.set(nullptr);
  }
}

/* static */
CycleCollectedJSContext* CycleCollectedJSContext::Get() {
  CollectorData* data = sCollectorData.get();
  if (data) {
    return data->mContext;
  }
  return nullptr;
}

MOZ_NEVER_INLINE static void SuspectAfterShutdown(
    void* aPtr, nsCycleCollectionParticipant* aCp,
    nsCycleCollectingAutoRefCnt* aRefCnt, bool* aShouldDelete) {
  if (aRefCnt->get() == 0) {
    if (!aShouldDelete) {
      // The CC is shut down, so we can't be in the middle of an ICC.
      ToParticipant(aPtr, &aCp);
      aRefCnt->stabilizeForDeletion();
      aCp->DeleteCycleCollectable(aPtr);
    } else {
      *aShouldDelete = true;
    }
  } else {
    // Make sure we'll get called again.
    aRefCnt->RemoveFromPurpleBuffer();
  }
}

void NS_CycleCollectorSuspect3(void* aPtr, nsCycleCollectionParticipant* aCp,
                               nsCycleCollectingAutoRefCnt* aRefCnt,
                               bool* aShouldDelete) {
  CollectorData* data = sCollectorData.get();

  // This assertion will happen if you AddRef or Release a cycle collected
  // object on a thread that does not have an active cycle collector.
  // This can happen in a few situations:
  // 1. We never cycle collect on this thread. (The cycle collector is only
  // run on the main thread and DOM worker threads.)
  // 2. The cycle collector hasn't been initialized on this thread yet.
  // 3. The cycle collector has already been shut down on this thread.
  MOZ_DIAGNOSTIC_ASSERT(
      data,
      "Cycle collected object used on a thread without a cycle collector.");

  if (MOZ_LIKELY(data->mCollector)) {
    data->mCollector->Suspect(aPtr, aCp, aRefCnt);
    return;
  }
  SuspectAfterShutdown(aPtr, aCp, aRefCnt, aShouldDelete);
}

void ClearNurseryPurpleBuffer() {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  CollectorData* data = sCollectorData.get();
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);
  data->mCollector->SuspectNurseryEntries();
}

void NS_CycleCollectorSuspectUsingNursery(void* aPtr,
                                          nsCycleCollectionParticipant* aCp,
                                          nsCycleCollectingAutoRefCnt* aRefCnt,
                                          bool* aShouldDelete) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  if (!gNurseryPurpleBufferEnabled) {
    NS_CycleCollectorSuspect3(aPtr, aCp, aRefCnt, aShouldDelete);
    return;
  }

  SuspectUsingNurseryPurpleBuffer(aPtr, aCp, aRefCnt);
}

uint32_t nsCycleCollector_suspectedCount() {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);

  if (!data->mCollector) {
    return 0;
  }

  return data->mCollector->SuspectedCount();
}

bool nsCycleCollector_init() {
#ifdef DEBUG
  static bool sInitialized;

  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
  MOZ_ASSERT(!sInitialized, "Called twice!?");
  sInitialized = true;
#endif

  return sCollectorData.init();
}

void nsCycleCollector_startup() {
  if (sCollectorData.get()) {
    MOZ_CRASH();
  }

  CollectorData* data = new CollectorData;
  data->mCollector = new nsCycleCollector();
  data->mContext = nullptr;

  sCollectorData.set(data);
}

void nsCycleCollector_setBeforeUnlinkCallback(CC_BeforeUnlinkCallback aCB) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);

  data->mCollector->SetBeforeUnlinkCallback(aCB);
}

void nsCycleCollector_setForgetSkippableCallback(
    CC_ForgetSkippableCallback aCB) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);

  data->mCollector->SetForgetSkippableCallback(aCB);
}

void nsCycleCollector_forgetSkippable(js::SliceBudget& aBudget,
                                      bool aRemoveChildlessNodes,
                                      bool aAsyncSnowWhiteFreeing) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);

  TimeLog timeLog;
  data->mCollector->ForgetSkippable(aBudget, aRemoveChildlessNodes,
                                    aAsyncSnowWhiteFreeing);
  timeLog.Checkpoint("ForgetSkippable()");
}

void nsCycleCollector_dispatchDeferredDeletion(bool aContinuation,
                                               bool aPurge) {
  CycleCollectedJSRuntime* rt = CycleCollectedJSRuntime::Get();
  if (rt) {
    rt->DispatchDeferredDeletion(aContinuation, aPurge);
  }
}

bool nsCycleCollector_doDeferredDeletion() {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);
  MOZ_ASSERT(data->mContext);

  return data->mCollector->FreeSnowWhite(false);
}

bool nsCycleCollector_doDeferredDeletionWithBudget(js::SliceBudget& aBudget) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);
  MOZ_ASSERT(data->mContext);

  return data->mCollector->FreeSnowWhiteWithBudget(aBudget);
}

already_AddRefed<nsICycleCollectorLogSink> nsCycleCollector_createLogSink() {
  nsCOMPtr<nsICycleCollectorLogSink> sink = new nsCycleCollectorLogSinkToFile();
  return sink.forget();
}

bool nsCycleCollector_collect(CCReason aReason,
                              nsICycleCollectorListener* aManualListener) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);

  AUTO_PROFILER_LABEL("nsCycleCollector_collect", GCCC);

  SliceBudget unlimitedBudget = SliceBudget::unlimited();
  return data->mCollector->Collect(aReason, ccIsManual::CCIsManual,
                                   unlimitedBudget, aManualListener);
}

void nsCycleCollector_collectSlice(SliceBudget& budget, CCReason aReason,
                                   bool aPreferShorterSlices) {
  CollectorData* data = sCollectorData.get();

  // We should have started the cycle collector by now.
  MOZ_ASSERT(data);
  MOZ_ASSERT(data->mCollector);

  AUTO_PROFILER_LABEL("nsCycleCollector_collectSlice", GCCC);

  data->mCollector->Collect(aReason, ccIsManual::CCIsNotManual, budget, nullptr,
                            aPreferShorterSlices);
}

void nsCycleCollector_prepareForGarbageCollection() {
  CollectorData* data = sCollectorData.get();

  MOZ_ASSERT(data);

  if (!data->mCollector) {
    return;
  }

  data->mCollector->PrepareForGarbageCollection();
}

void nsCycleCollector_finishAnyCurrentCollection() {
  CollectorData* data = sCollectorData.get();

  MOZ_ASSERT(data);

  if (!data->mCollector) {
    return;
  }

  data->mCollector->FinishAnyCurrentCollection(CCReason::API);
}

void nsCycleCollector_shutdown(bool aDoCollect) {
  CollectorData* data = sCollectorData.get();

  if (data) {
    MOZ_ASSERT(data->mCollector);
    AUTO_PROFILER_LABEL("nsCycleCollector_shutdown", OTHER);

    {
      RefPtr<nsCycleCollector> collector = data->mCollector;
      collector->Shutdown(aDoCollect);
      data->mCollector = nullptr;
    }

    if (!data->mContext) {
      delete data;
      sCollectorData.set(nullptr);
    }
  }
}
