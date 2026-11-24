/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_MultiWriterQueue_h_
#define mozilla_MultiWriterQueue_h_

#include <cstdint>
#include <utility>

#include "RollingNumber.h"
#include "mozilla/Atomics.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Mutex.h"
#include "prthread.h"

namespace mozilla {

// Default reader locking strategy, using a mutex to ensure that concurrent
// PopAll calls won't overlap.
class MOZ_CAPABILITY("mutex") MultiWriterQueueReaderLocking_Mutex {
 public:
  MultiWriterQueueReaderLocking_Mutex()
      : mMutex("MultiWriterQueueReaderLocking_Mutex") {}
  void Lock() MOZ_CAPABILITY_ACQUIRE(mMutex) { mMutex.Lock(); };
  void Unlock() MOZ_CAPABILITY_RELEASE(mMutex) { mMutex.Unlock(); };

 private:
  Mutex mMutex;
};

// Reader non-locking strategy, trusting that PopAll will never be called
// concurrently (e.g., by only calling it from a specific thread).
class MOZ_CAPABILITY("dummy lock") MultiWriterQueueReaderLocking_None {
 public:
#ifndef DEBUG
  void Lock() MOZ_CAPABILITY_ACQUIRE() {};
  void Unlock() MOZ_CAPABILITY_RELEASE() {};
#else
  // DEBUG-mode checks to catch concurrent misuses.
  void Lock() MOZ_CAPABILITY_ACQUIRE() {
    MOZ_ASSERT(mLocked.compareExchange(false, true));
  };
  void Unlock() MOZ_CAPABILITY_RELEASE() {
    MOZ_ASSERT(mLocked.compareExchange(true, false));
  };

 private:
  Atomic<bool> mLocked{false};
#endif
};

static constexpr uint32_t MultiWriterQueueDefaultBufferSize = 8192;

// Multi-writer, single-reader queue of elements of type `T`.
// Elements are bunched together in buffers of `BufferSize` elements.
//
// This queue is heavily optimized for pushing. In most cases pushes will only
// cost a couple of atomic reads and a few non-atomic reads. Worst cases:
// - Once per buffer, a push will allocate or reuse a buffer for later pushes;
// - During the above new-buffer push, other pushes will be blocked.
//
// By default, popping is protected by mutex; it may be disabled if popping is
// guaranteed never to be concurrent.
// In any case, popping will never negatively impact pushes.
// (However, *not* popping will add runtime costs, as unread buffers will not
// be freed, or made available to future pushes; Push functions provide
// feedback as to when popping would be most efficient.)
template <typename T, uint32_t BufferSize = MultiWriterQueueDefaultBufferSize,
          typename ReaderLocking = MultiWriterQueueReaderLocking_Mutex>
class MultiWriterQueue {
  static_assert(BufferSize > 0, "0-sized MultiWriterQueue buffer");

 public:
  // Constructor.
  // Allocates the initial buffer that will receive the first `BufferSize`
  // elements. Also allocates one reusable buffer, which will definitely be
  // needed after the first `BufferSize` elements have been pushed.
  // Ideally (if the reader can process each buffer quickly enough), there
  // won't be a need for more buffer allocations.
  MultiWriterQueue()
      : mBuffersCoverAtLeastUpTo(BufferSize - 1),
        mMostRecentBuffer(new Buffer{}),
        mReusableBuffers(new Buffer{}),
        mOldestBuffer(static_cast<Buffer*>(mMostRecentBuffer)),
        mLiveBuffersStats(1),
        mReusableBuffersStats(1),
        mAllocatedBuffersStats(2) {}

  ~MultiWriterQueue() {
    auto DestroyList = [](Buffer* aBuffer) {
      while (aBuffer) {
        Buffer* older = aBuffer->Older();
        delete aBuffer;
        aBuffer = older;
      }
    };
    DestroyList(mMostRecentBuffer);
    DestroyList(mReusableBuffers);
  }

  // We need the index to be order-resistant to overflow, i.e., numbers before
  // an overflow should test smaller-than numbers after the overflow.
  // This is because we keep pushing elements with increasing Index, and this
  // Index is used to find the appropriate buffer based on a range; and this
  // need to work smoothly when crossing the overflow boundary.
  using Index = RollingNumber<uint32_t>;

  // Pushes indicate whether they have just reached the end of a buffer.
  using DidReachEndOfBuffer = bool;

  // Push new element and call aF on it.
  // Element may be in just-created state, or recycled after a PopAll call.
  // Atomically thread-safe; in the worst case some pushes may be blocked
  // while a new buffer is created/reused for them.
  // Returns whether that push reached the end of a buffer; useful if caller
  // wants to trigger processing regularly at the most efficient time.
  template <typename F>
  DidReachEndOfBuffer PushF(F&& aF) {
    // Atomically claim ownership of the next available element.
    const Index index{mNextElementToWrite++};
    // And now go and set that element.
    for (;;) {
      Index lastIndex{mBuffersCoverAtLeastUpTo};

      if (MOZ_UNLIKELY(index == lastIndex)) {
        // We have claimed the last element in the current head -> Allocate a
        // new head in advance of more pushes. Make it point at the current
        // most-recent buffer.
        // This whole process is effectively guarded:
        // - Later pushes will wait until mBuffersCoverAtLeastUpTo changes to
        //   one that can accept their claimed index.
        // - Readers will stop until the last element is marked as valid.
        Buffer* ourBuffer = mMostRecentBuffer;
        Buffer* newBuffer = NewBuffer(ourBuffer, index + 1);
        // Because we have claimed this very specific index, we should be the
        // only one touching the most-recent buffer pointer.
        MOZ_ASSERT(mMostRecentBuffer == ourBuffer);
        // Just pivot the most-recent buffer pointer to our new buffer.
        mMostRecentBuffer = newBuffer;
        // Because we have claimed this very specific index, we should be the
        // only one touching the buffer coverage watermark.
        MOZ_ASSERT(mBuffersCoverAtLeastUpTo == lastIndex.Value());
        // Update it to include the just-added most-recent buffer.
        mBuffersCoverAtLeastUpTo = index.Value() + BufferSize;
        // We know for sure that `ourBuffer` is the correct one for this index.
        ourBuffer->SetAndValidateElement(aF, index);
        // And indicate that we have reached the end of a buffer.
        return true;
      }

      if (MOZ_UNLIKELY(index > lastIndex)) {
        // We have claimed an element in a yet-unavailable buffer, wait for our
        // target buffer to be created (see above).
        while (Index(mBuffersCoverAtLeastUpTo) < index) {
          PR_Sleep(PR_INTERVAL_NO_WAIT);  // Yield
        }
        // Then loop to examine the new situation.
        continue;
      }

      // Here, we have claimed a number that is covered by current buffers.
      // These buffers cannot be destroyed, because our buffer is not filled
      // yet (we haven't written in it yet), therefore the reader thread will
      // have to stop there (or before) and won't destroy our buffer or more
      // recent ones.
      MOZ_ASSERT(index < lastIndex);
      Buffer* ourBuffer = mMostRecentBuffer;

      // In rare situations, another thread may have had the time to create a
      // new more-recent buffer, in which case we need to find our older buffer.
      while (MOZ_UNLIKELY(index < ourBuffer->Origin())) {
        // We assume that older buffers with still-invalid elements (e.g., the
        // one we have just claimed) cannot be destroyed.
        MOZ_ASSERT(ourBuffer->Older());
        ourBuffer = ourBuffer->Older();
      }

      // Now we can set&validate the claimed element, and indicate that we have
      // not reached the end of a buffer.
      ourBuffer->SetAndValidateElement(aF, index);
      return false;
    }
  }

  // Push new element and assign it a value.
  // Atomically thread-safe; in the worst case some pushes may be blocked
  // while a new buffer is created/reused for them.
  // Returns whether that push reached the end of a buffer; useful if caller
  // wants to trigger processing regularly at the most efficient time.
  DidReachEndOfBuffer Push(const T& aT) {
    return PushF([&aT](T& aElement, Index) { aElement = aT; });
  }

  // Push new element and move-assign it a value.
  // Atomically thread-safe; in the worst case some pushes may be blocked
  // while a new buffer is created/reused for them.
  // Returns whether that push reached the end of a buffer; useful if caller
  // wants to trigger processing regularly at the most efficient time.
  DidReachEndOfBuffer Push(T&& aT) {
    return PushF([&aT](T& aElement, Index) { aElement = std::move(aT); });
  }

  // Pop all elements before the first invalid one, running aF on each of them
  // in FIFO order.
  // Thread-safety with other PopAll calls is controlled by the `Locking`
  // template argument.
  // Concurrent pushes are always allowed, because:
  // - PopAll won't read elements until valid,
  // - Pushes do not interfere with pop-related members -- except for
  //   mReusableBuffers, which is accessed atomically.
  template <typename F>
  void PopAll(F&& aF) {
    mReaderLocking.Lock();
    // Destroy every second fully-read buffer.
    // TODO: Research a better algorithm, probably based on stats.
    bool destroy = false;
    for (;;) {
      Buffer* b = mOldestBuffer;
      MOZ_ASSERT(!b->Older());
      // The next element to pop must be in that oldest buffer.
      MOZ_ASSERT(mNextElementToPop >= b->Origin());
      MOZ_ASSERT(mNextElementToPop < b->Origin() + BufferSize);

      // Start reading each element.
      if (!b->ReadAndInvalidateAll(aF, mNextElementToPop)) {
        // Found an invalid element, stop popping.
        mReaderLocking.Unlock();
        return;
      }

      // Reached the end of this oldest buffer
      MOZ_ASSERT(mNextElementToPop == b->Origin() + BufferSize);
      // Delete this oldest buffer.
      // Since the last element was valid, it must mean that there is a newer
      // buffer.
      MOZ_ASSERT(b->Newer());
      MOZ_ASSERT(mNextElementToPop == b->Newer()->Origin());
      StopUsing(b, destroy);
      destroy = !destroy;

      // We will loop and start reading the now-oldest buffer.
    }
  }

  // Size of all buffers (used, or recyclable), excluding external data.
  size_t ShallowSizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
    return mAllocatedBuffersStats.Count() * sizeof(Buffer);
  }

  struct CountAndWatermark {
    int mCount;
    int mWatermark;
  };

  CountAndWatermark LiveBuffersStats() const { return mLiveBuffersStats.Get(); }
  CountAndWatermark ReusableBuffersStats() const {
    return mReusableBuffersStats.Get();
  }
  CountAndWatermark AllocatedBuffersStats() const {
    return mAllocatedBuffersStats.Get();
  }

 private:
  // Structure containing the element to be stored, and a validity-marker.
  class BufferedElement {
   public:
    // Run aF on an invalid element, and mark it as valid.
    template <typename F>
    void SetAndValidate(F&& aF, Index aIndex) {
      MOZ_ASSERT(!mValid);
      aF(mT, aIndex);
      mValid = true;
    }

    // Run aF on a valid element and mark it as invalid, return true.
    // Return false if element was invalid.
    template <typename F>
    bool ReadAndInvalidate(F&& aF) {
      if (!mValid) {
        return false;
      }
      aF(mT);
      mValid = false;
      return true;
    }

   private:
    T mT;
    // mValid should be atomically changed to true *after* mT has been written,
    // so that the reader can only see valid data.
    // ReleaseAcquire, because when set to `true`, we want the just-written mT
    // to be visible to the thread reading this `true`; and when set to `false`,
    // we want the previous reads to have completed.
    Atomic<bool, ReleaseAcquire> mValid{false};
  };

  // Buffer contains a sequence of BufferedElements starting at a specific
  // index, and it points to the next-older buffer (if any).
  class Buffer {
   public:
    // Constructor of the very first buffer.
    Buffer() : mOlder(nullptr), mNewer(nullptr), mOrigin(0) {}

    // Constructor of later buffers.
    Buffer(Buffer* aOlder, Index aOrigin)
        : mOlder(aOlder), mNewer(nullptr), mOrigin(aOrigin) {
      MOZ_ASSERT(aOlder);
      aOlder->mNewer = this;
    }

    Buffer* Older() const { return mOlder; }
    void SetOlder(Buffer* aOlder) { mOlder = aOlder; }

    Buffer* Newer() const { return mNewer; }
    void SetNewer(Buffer* aNewer) { mNewer = aNewer; }

    Index Origin() const { return mOrigin; }
    void SetOrigin(Index aOrigin) { mOrigin = aOrigin; }

    // Run aF on a yet-invalid element.
    // Not thread-safe by itself, but nothing else should write this element,
    // and reader won't access it until after it becomes valid.
    template <typename F>
    void SetAndValidateElement(F&& aF, Index aIndex) {
      MOZ_ASSERT(aIndex >= Origin());
      MOZ_ASSERT(aIndex < Origin() + BufferSize);
      mElements[aIndex - Origin()].SetAndValidate(aF, aIndex);
    }

    using DidReadLastElement = bool;

    // Read all valid elements starting at aIndex, marking them invalid and
    // updating aIndex.
    // Returns true if we ended up reading the last element in this buffer.
    // Accessing the validity bit is thread-safe (as it's atomic), but once
    // an element is valid, the reading itself is not thread-safe and should be
    // guarded.
    template <typename F>
    DidReadLastElement ReadAndInvalidateAll(F&& aF, Index& aIndex) {
      MOZ_ASSERT(aIndex >= Origin());
      MOZ_ASSERT(aIndex < Origin() + BufferSize);
      for (; aIndex < Origin() + BufferSize; ++aIndex) {
        if (!mElements[aIndex - Origin()].ReadAndInvalidate(aF)) {
          // Found an invalid element, stop here. (aIndex will not be updated
          // past it, so we will start from here next time.)
          return false;
        }
      }
      return true;
    }

   private:
    Buffer* mOlder;
    Buffer* mNewer;
    Index mOrigin;
    BufferedElement mElements[BufferSize];
  };

  // Reuse a buffer, or create a new one.
  // All buffered elements will be invalid.
  Buffer* NewBuffer(Buffer* aOlder, Index aOrigin) {
    MOZ_ASSERT(aOlder);
    for (;;) {
      Buffer* head = mReusableBuffers;
      if (!head) {
        ++mAllocatedBuffersStats;
        ++mLiveBuffersStats;
        Buffer* buffer = new Buffer(aOlder, aOrigin);
        return buffer;
      }
      Buffer* older = head->Older();
      // Try to pivot the reusable-buffer pointer from the current head to the
      // next buffer in line.
      if (mReusableBuffers.compareExchange(head, older)) {
        // Success! The reusable-buffer pointer now points at the older buffer,
        // so we can recycle this ex-head.
        --mReusableBuffersStats;
        ++mLiveBuffersStats;
        head->SetOlder(aOlder);
        aOlder->SetNewer(head);
        // We will be the newest; newer-pointer should already be null.
        MOZ_ASSERT(!head->Newer());
        head->SetOrigin(aOrigin);
        return head;
      }
      // Failure, someone else must have touched the list, loop to try again.
    }
  }

  // Discard a fully-read buffer.
  // If aDestroy is true, delete it.
  // If aDestroy is false, move the buffer to a reusable-buffer stack.
  void StopUsing(Buffer* aBuffer, bool aDestroy) {
    --mLiveBuffersStats;

    // We should only stop using the oldest buffer.
    MOZ_ASSERT(!aBuffer->Older());
    // The newest buffer should not be modified here.
    MOZ_ASSERT(aBuffer->Newer());
    MOZ_ASSERT(aBuffer->Newer()->Older() == aBuffer);
    // Detach from the second-oldest buffer.
    aBuffer->Newer()->SetOlder(nullptr);
    // Make the second-oldest buffer the now-oldest buffer.
    mOldestBuffer = aBuffer->Newer();

    if (aDestroy) {
      --mAllocatedBuffersStats;
      delete aBuffer;
    } else {
      ++mReusableBuffersStats;
      // The recycling stack only uses mOlder; mNewer is not needed.
      aBuffer->SetNewer(nullptr);

      // Make the given buffer the new head of reusable buffers.
      for (;;) {
        Buffer* head = mReusableBuffers;
        aBuffer->SetOlder(head);
        if (mReusableBuffers.compareExchange(head, aBuffer)) {
          break;
        }
      }
    }
  }

  // Index of the next element to write. Modified when an element index is
  // claimed for a push. If the last element of a buffer is claimed, that push
  // will be responsible for adding a new head buffer.
  // Relaxed, because there is no synchronization based on this variable, each
  // thread just needs to get a different value, and will then write different
  // things (which themselves have some atomic validation before they may be
  // read elsewhere, independent of this `mNextElementToWrite`.)
  Atomic<Index::ValueType, Relaxed> mNextElementToWrite{0};

  // Index that a live recent buffer reaches. If a push claims a lesser-or-
  // equal number, the corresponding buffer is guaranteed to still be alive:
  // - It will have been created before this index was updated,
  // - It will not be destroyed until all its values have been written,
  //   including the one that just claimed a position within it.
  // Also, the push that claims this exact number is responsible for adding the
  // next buffer and updating this value accordingly.
  // ReleaseAcquire, because when set to a certain value, the just-created
  // buffer covering the new range must be visible to readers.
  Atomic<Index::ValueType, ReleaseAcquire> mBuffersCoverAtLeastUpTo;

  // Pointer to the most recent buffer. Never null.
  // This is the most recent of a deque of yet-unread buffers.
  // Only modified when adding a new head buffer.
  // ReleaseAcquire, because when modified, the just-created new buffer must be
  // visible to readers.
  Atomic<Buffer*, ReleaseAcquire> mMostRecentBuffer;

  // Stack of reusable buffers.
  // ReleaseAcquire, because when modified, the just-added buffer must be
  // visible to readers.
  Atomic<Buffer*, ReleaseAcquire> mReusableBuffers;

  // Template-provided locking mechanism to protect PopAll()-only member
  // variables below.
  ReaderLocking mReaderLocking;

  // Pointer to the oldest buffer, which contains the new element to be popped.
  // Never null.
  Buffer* mOldestBuffer;

  // Index of the next element to be popped.
  Index mNextElementToPop{0};

  // Stats.
  class AtomicCountAndWatermark {
   public:
    explicit AtomicCountAndWatermark(int aCount)
        : mCount(aCount), mWatermark(aCount) {}

    int Count() const { return mCount.load(); }

    CountAndWatermark Get() const {
      return CountAndWatermark{mCount.load(), mWatermark.load()};
    }

    int operator++() {
      int count = int(++mCount);
      // Update watermark.
      for (;;) {
        int watermark = mWatermark.load();
        if (watermark >= count) {
          // printf("++[%p] -=> %d-%d\n", this, count, watermark);
          break;
        }
        if (mWatermark.compareExchange(watermark, count)) {
          // printf("++[%p] -x> %d-(was %d now %d)\n", this, count, watermark,
          // count);
          break;
        }
      }
      return count;
    }

    int operator--() {
      int count = int(--mCount);
      // printf("--[%p] -> %d\n", this, count);
      return count;
    }

   private:
    // Relaxed, as these are just gathering stats, so consistency is not
    // critical.
    Atomic<int, Relaxed> mCount;
    Atomic<int, Relaxed> mWatermark;
  };
  // All buffers in the mMostRecentBuffer deque.
  AtomicCountAndWatermark mLiveBuffersStats;
  // All buffers in the mReusableBuffers stack.
  AtomicCountAndWatermark mReusableBuffersStats;
  // All allocated buffers (sum of above).
  AtomicCountAndWatermark mAllocatedBuffersStats;
};

}  // namespace mozilla

#endif  // mozilla_MultiWriterQueue_h_
