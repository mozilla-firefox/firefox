/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * SourceBuffer is a single producer, multiple consumer data structure used for
 * storing image source (compressed) data.
 */

#ifndef mozilla_image_sourcebuffer_h
#define mozilla_image_sourcebuffer_h

#include <algorithm>
#include <utility>

#include "mozilla/Maybe.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefCounted.h"
#include "mozilla/RefPtr.h"
#include "nsTArray.h"

class nsIInputStream;

namespace mozilla {
namespace image {

class SourceBuffer;

/**
 * IResumable is an interface for classes that can schedule themselves to resume
 * their work later. An implementation of IResumable generally should post a
 * runnable to some event target which continues the work of the task.
 */
struct IResumable {
  MOZ_DECLARE_REFCOUNTED_TYPENAME(IResumable)

  // Subclasses may or may not be XPCOM classes, so we just require that they
  // implement AddRef and Release.
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  virtual void Resume() = 0;

 protected:
  virtual ~IResumable() = default;
};

/**
 * SourceBufferIterator is a class that allows consumers of image source data to
 * read the contents of a SourceBuffer sequentially.
 *
 * Consumers can advance through the SourceBuffer by calling
 * AdvanceOrScheduleResume() repeatedly. After every advance, they should call
 * check the return value, which will tell them the iterator's new state.
 *
 * If WAITING is returned, AdvanceOrScheduleResume() has arranged
 * to call the consumer's Resume() method later, so the consumer should save its
 * state if needed and stop running.
 *
 * If the iterator's new state is READY, then the consumer can call Data() and
 * Length() to read new data from the SourceBuffer.
 *
 * Finally, in the COMPLETE state the consumer can call CompletionStatus() to
 * get the status passed to SourceBuffer::Complete().
 */
class SourceBufferIterator final {
 public:
  enum State {
    START,    // The iterator is at the beginning of the buffer.
    READY,    // The iterator is pointing to new data.
    WAITING,  // The iterator is blocked and the caller must yield.
    COMPLETE  // The iterator is pointing to the end of the buffer.
  };

  explicit SourceBufferIterator(SourceBuffer* aOwner, size_t aReadLimit)
      : mOwner(aOwner),
        mState(START),
        mChunkCount(0),
        mByteCount(0),
        mRemainderToRead(aReadLimit) {
    MOZ_ASSERT(aOwner);
    mData.mIterating.mChunk = 0;
    mData.mIterating.mData = nullptr;
    mData.mIterating.mOffset = 0;
    mData.mIterating.mAvailableLength = 0;
    mData.mIterating.mNextReadLength = 0;
  }

  SourceBufferIterator(SourceBufferIterator&& aOther)
      : mOwner(std::move(aOther.mOwner)),
        mState(aOther.mState),
        mData(aOther.mData),
        mChunkCount(aOther.mChunkCount),
        mByteCount(aOther.mByteCount),
        mRemainderToRead(aOther.mRemainderToRead) {}

  ~SourceBufferIterator();

  SourceBufferIterator& operator=(SourceBufferIterator&& aOther);

  SourceBufferIterator(const SourceBufferIterator&) = delete;
  SourceBufferIterator& operator=(const SourceBufferIterator&) = delete;

  /**
   * Returns true if there are no more than @aBytes remaining in the
   * SourceBuffer. If the SourceBuffer is not yet complete, returns false.
   */
  bool RemainingBytesIsNoMoreThan(size_t aBytes) const;

  /**
   * Advances the iterator through the SourceBuffer if possible. Advances no
   * more than @aRequestedBytes bytes. (Use SIZE_MAX to advance as much as
   * possible.)
   *
   * This is a wrapper around AdvanceOrScheduleResume() that makes it clearer at
   * the callsite when the no resuming is intended.
   *
   * @return State::READY if the iterator was successfully advanced.
   *         State::WAITING if the iterator could not be advanced because it's
   *           at the end of the underlying SourceBuffer, but the SourceBuffer
   *           may still receive additional data.
   *         State::COMPLETE if the iterator could not be advanced because it's
   *           at the end of the underlying SourceBuffer and the SourceBuffer is
   *           marked complete (i.e., it will never receive any additional
   *           data).
   */
  State Advance(size_t aRequestedBytes) {
    return AdvanceOrScheduleResume(aRequestedBytes, nullptr);
  }

  /**
   * Advances the iterator through the SourceBuffer if possible. Advances no
   * more than @aRequestedBytes bytes. (Use SIZE_MAX to advance as much as
   * possible.) If advancing is not possible and @aConsumer is not null,
   * arranges to call the @aConsumer's Resume() method when more data is
   * available.
   *
   * @return State::READY if the iterator was successfully advanced.
   *         State::WAITING if the iterator could not be advanced because it's
   *           at the end of the underlying SourceBuffer, but the SourceBuffer
   *           may still receive additional data. @aConsumer's Resume() method
   *           will be called when additional data is available.
   *         State::COMPLETE if the iterator could not be advanced because it's
   *           at the end of the underlying SourceBuffer and the SourceBuffer is
   *           marked complete (i.e., it will never receive any additional
   *           data).
   */
  State AdvanceOrScheduleResume(size_t aRequestedBytes, IResumable* aConsumer);

  /**
   * Records that only @aConsumed bytes of the current chunk have been
   * processed. The iterator position advances by @aConsumed and
   * mNextReadLength is set to the remaining bytes, so that Data()/Length()
   * immediately reflect the unconsumed portion and IsReady() remains true.
   * Once all remaining bytes are consumed and mNextReadLength reaches zero,
   * the next AdvanceOrScheduleResume() will fetch the next chunk.
   */
  void MarkConsumed(size_t aConsumed);

  bool IsReady() const { return mState == READY; }

  /// If at the end, returns the status passed to SourceBuffer::Complete().
  nsresult CompletionStatus() const {
    MOZ_ASSERT(mState == COMPLETE,
               "Calling CompletionStatus() in the wrong state");
    return mState == COMPLETE ? mData.mAtEnd.mStatus : NS_OK;
  }

  /// If we're ready to read, returns a pointer to the new data.
  const char* Data() const {
    MOZ_ASSERT(mState == READY, "Calling Data() in the wrong state");
    return mState == READY ? mData.mIterating.mData + mData.mIterating.mOffset
                           : nullptr;
  }

  /// If we're ready to read, returns the length of the new data.
  size_t Length() const {
    MOZ_ASSERT(mState == READY, "Calling Length() in the wrong state");
    return mState == READY ? mData.mIterating.mNextReadLength : 0;
  }

  /// If we're ready to read, returns whether or not everything available thus
  /// far has been in the same contiguous buffer.
  bool IsContiguous() const {
    MOZ_ASSERT(mState == READY, "Calling IsContiguous() in the wrong state");
    return mState == READY ? mData.mIterating.mChunk == 0 : false;
  }

  /// @return a count of the chunks we've advanced through.
  uint32_t ChunkCount() const { return mChunkCount; }

  /// @return a count of the bytes in all chunks we've advanced through.
  size_t ByteCount() const { return mByteCount; }

  /// @return the source buffer which owns the iterator.
  SourceBuffer* Owner() const {
    MOZ_ASSERT(mOwner);
    return mOwner;
  }

  /// @return the current offset from the beginning of the buffer.
  size_t Position() const {
    return mByteCount - mData.mIterating.mAvailableLength;
  }

 private:
  friend class SourceBuffer;

  bool HasMore() const { return mState != COMPLETE; }

  State AdvanceFromLocalBuffer(size_t aRequestedBytes) {
    MOZ_ASSERT(mState == READY, "Advancing in the wrong state");
    MOZ_ASSERT(mData.mIterating.mAvailableLength > 0,
               "The local buffer shouldn't be empty");
    MOZ_ASSERT(mData.mIterating.mNextReadLength == 0,
               "Advancing without consuming previous data");

    mData.mIterating.mNextReadLength =
        std::min(mData.mIterating.mAvailableLength, aRequestedBytes);

    return READY;
  }

  State SetReady(uint32_t aChunk, const char* aData, size_t aOffset,
                 size_t aAvailableLength, size_t aRequestedBytes) {
    MOZ_ASSERT(mState != COMPLETE);
    mState = READY;

    // Prevent the iterator from reporting more data than it is allowed to read.
    if (aAvailableLength > mRemainderToRead) {
      aAvailableLength = mRemainderToRead;
    }

    // Update state.
    mData.mIterating.mChunk = aChunk;
    mData.mIterating.mData = aData;
    mData.mIterating.mOffset = aOffset;
    mData.mIterating.mAvailableLength = aAvailableLength;

    // Update metrics.
    mChunkCount++;
    mByteCount += aAvailableLength;

    // Attempt to advance by the requested number of bytes.
    return AdvanceFromLocalBuffer(aRequestedBytes);
  }

  State SetWaiting(bool aHasConsumer) {
    MOZ_ASSERT(mState != COMPLETE);
    // Without a consumer, we won't know when to wake up precisely. Caller
    // convention should mean that we don't try to advance unless we have
    // written new data, but that doesn't mean we got enough.
    MOZ_ASSERT(mState != WAITING || !aHasConsumer,
               "Did we get a spurious wakeup somehow?");
    return mState = WAITING;
  }

  State SetComplete(nsresult aStatus) {
    mData.mAtEnd.mStatus = aStatus;
    return mState = COMPLETE;
  }

  RefPtr<SourceBuffer> mOwner;

  State mState;

  /**
   * This union contains our iteration state if we're still iterating (for
   * states START, READY, and WAITING) and the status the SourceBuffer was
   * completed with if we're in state COMPLETE.
   */
  union {
    struct {
      uint32_t mChunk;    // Index of the chunk in SourceBuffer.
      const char* mData;  // Pointer to the start of the chunk.
      size_t mOffset;     // Current read position of the iterator relative to
                          // mData.
      size_t mAvailableLength;  // How many bytes remain unread in the chunk,
                                // relative to mOffset.
      size_t
          mNextReadLength;  // How many bytes the last iterator advance
                            // requested to be read, so that we know much
                            // to increase mOffset and reduce mAvailableLength
                            // by when the next advance is requested.
    } mIterating;  // Cached info of the chunk currently iterating over.
    struct {
      nsresult mStatus;  // Status code indicating if we read all the data.
    } mAtEnd;            // State info after iterator is complete.
  } mData;

  uint32_t mChunkCount;  // Count of chunks observed, including current chunk.
  size_t mByteCount;     // Count of readable bytes observed, including unread
                         // bytes from the current chunk.
  size_t mRemainderToRead;  // Count of bytes left to read if there is a maximum
                            // imposed by the caller. SIZE_MAX if unlimited.
};

/**
 * SourceBuffer is a parallel data structure used for storing image source
 * (compressed) data.
 *
 * SourceBuffer is a single producer, multiple consumer data structure. The
 * single producer calls Append() to append data to the buffer. In parallel,
 * multiple consumers can call Iterator(), which returns a SourceBufferIterator
 * that they can use to iterate through the buffer. The SourceBufferIterator
 * returns a series of pointers which remain stable for lifetime of the
 * SourceBuffer, and the data they point to is immutable, ensuring that the
 * producer never interferes with the consumers.
 *
 * In order to avoid blocking, SourceBuffer works with SourceBufferIterator to
 * keep a list of consumers which are waiting for new data, and to resume them
 * when the producer appends more. All consumers must implement the IResumable
 * interface to make this possible.
 */
class SourceBuffer final {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(image::SourceBuffer)
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(image::SourceBuffer)

  SourceBuffer();

  //////////////////////////////////////////////////////////////////////////////
  // Producer methods.
  //////////////////////////////////////////////////////////////////////////////

  /**
   * If the producer knows how long the source data will be, it should call
   * ExpectLength, which enables SourceBuffer to preallocate its buffer.
   */
  nsresult ExpectLength(size_t aExpectedLength);

  /// Append the provided data to the buffer.
  nsresult Append(const char* aData, size_t aLength);

  /// Append the data available on the provided nsIInputStream to the buffer.
  nsresult AppendFromInputStream(nsIInputStream* aInputStream, uint32_t aCount);

  /// Take ownership of provided data and append it without copying.
  nsresult AdoptData(char* aData, size_t aLength,
                     void* (*aRealloc)(void*, size_t), void (*aFree)(void*));

  /**
   * Mark the buffer complete, with a status that will be available to
   * consumers. Further calls to Append() are forbidden after Complete().
   */
  void Complete(nsresult aStatus);

  /// Returns true if the buffer is complete.
  bool IsComplete();

  /// Memory reporting.
  size_t SizeOfIncludingThisWithComputedFallback(MallocSizeOf) const;

  //////////////////////////////////////////////////////////////////////////////
  // Consumer methods.
  //////////////////////////////////////////////////////////////////////////////

  /**
   * Returns an iterator to this SourceBuffer, which cannot read more than the
   * given length.
   */
  SourceBufferIterator Iterator(size_t aReadLength = SIZE_MAX);

  //////////////////////////////////////////////////////////////////////////////
  // Consumer methods.
  //////////////////////////////////////////////////////////////////////////////

  /**
   * The minimum chunk capacity we'll allocate, if we don't know the correct
   * capacity (which would happen because ExpectLength() wasn't called or gave
   * us the wrong value). This is only exposed for use by tests; if normal code
   * is using this, it's doing something wrong.
   */
  static const size_t MIN_CHUNK_CAPACITY = 4096;

  /**
   * The maximum chunk capacity we'll allocate. This was historically the
   * maximum we would preallocate based on the network size. We may adjust it
   * in the future based on the image_decode.chunks telemetry to ensure most
   * images remain in a single chunk.
   */
  static const size_t MAX_CHUNK_CAPACITY = 20 * 1024 * 1024;

 private:
  friend class SourceBufferIterator;

  ~SourceBuffer();

  //////////////////////////////////////////////////////////////////////////////
  // Chunk type and chunk-related methods.
  //////////////////////////////////////////////////////////////////////////////

  class Chunk final {
   public:
    explicit Chunk(size_t aCapacity) : mCapacity(aCapacity), mLength(0) {
      MOZ_ASSERT(aCapacity > 0, "Creating zero-capacity chunk");
      mData = static_cast<char*>(malloc(mCapacity));
    }

    /// Create an "adopted" chunk taking ownership of the provided data.
    Chunk(char* aData, size_t aLength, void* (*aRealloc)(void*, size_t),
          void (*aFree)(void*))
        : mCapacity(aLength),
          mLength(aLength),
          mData(aData),
          mRealloc(aRealloc),
          mFree(aFree) {}

    ~Chunk() { mFree(mData); }

    Chunk(Chunk&& aOther)
        : mCapacity(aOther.mCapacity),
          mLength(aOther.mLength),
          mData(aOther.mData),
          mRealloc(aOther.mRealloc),
          mFree(aOther.mFree) {
      aOther.mCapacity = aOther.mLength = 0;
      aOther.mData = nullptr;
    }

    Chunk& operator=(Chunk&& aOther) {
      mFree(mData);
      mCapacity = aOther.mCapacity;
      mLength = aOther.mLength;
      mData = aOther.mData;
      mRealloc = aOther.mRealloc;
      mFree = aOther.mFree;
      aOther.mCapacity = aOther.mLength = 0;
      aOther.mData = nullptr;
      return *this;
    }

    bool AllocationFailed() const { return !mData; }
    size_t Capacity() const { return mCapacity; }
    size_t Length() const { return mLength; }

    char* Data() const {
      MOZ_ASSERT(mData, "Allocation failed but nobody checked for it");
      return mData;
    }

    void AddLength(size_t aAdditionalLength) {
      MOZ_ASSERT(mLength + aAdditionalLength <= mCapacity);
      mLength += aAdditionalLength;
    }

    bool SetCapacity(size_t aCapacity) {
      MOZ_ASSERT(mData, "Allocation failed but nobody checked for it");
      MOZ_ASSERT(aCapacity > 0, "zero sized resize");
      if (aCapacity == 0) {
        return false;
      }
      char* data = static_cast<char*>(mRealloc(mData, aCapacity));
      if (!data) {
        return false;
      }

      mData = data;
      mCapacity = aCapacity;
      return true;
    }

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

   private:
    size_t mCapacity;
    size_t mLength;
    char* mData;
    void* (*mRealloc)(void*, size_t) = realloc;
    void (*mFree)(void*) = free;
  };

  nsresult AppendChunk(Maybe<Chunk>&& aChunk) MOZ_REQUIRES(mMutex);
  Maybe<Chunk> CreateChunk(size_t aCapacity, size_t aExistingCapacity = 0,
                           bool aRoundUp = true);
  nsresult Compact() MOZ_REQUIRES(mMutex);
  static size_t RoundedUpCapacity(size_t aCapacity);
  size_t FibonacciCapacityWithMinimum(size_t aMinCapacity) MOZ_REQUIRES(mMutex);

  //////////////////////////////////////////////////////////////////////////////
  // Iterator / consumer methods.
  //////////////////////////////////////////////////////////////////////////////

  void AddWaitingConsumer(IResumable* aConsumer) MOZ_REQUIRES(mMutex);

  // Releasing the last reference to a waiting consumer can destroy an
  // image::Decoder, whose SourceBufferIterator member reacquires mMutex from
  // its destructor. ResumeWaitingConsumers and HandleError therefore hand our
  // references to aOutConsumers instead of dropping them; callers must declare
  // aOutConsumers outside the scope which holds mMutex.
  void ResumeWaitingConsumers(nsTArray<RefPtr<IResumable>>* aOutConsumers)
      MOZ_REQUIRES(mMutex);

  typedef SourceBufferIterator::State State;

  State AdvanceIteratorOrScheduleResume(SourceBufferIterator& aIterator,
                                        size_t aRequestedBytes,
                                        IResumable* aConsumer);
  bool RemainingBytesIsNoMoreThan(const SourceBufferIterator& aIterator,
                                  size_t aBytes) const;

  void OnIteratorRelease();

  //////////////////////////////////////////////////////////////////////////////
  // Helper methods.
  //////////////////////////////////////////////////////////////////////////////

  nsresult HandleError(nsresult aError,
                       nsTArray<RefPtr<IResumable>>* aOutConsumers)
      MOZ_REQUIRES(mMutex);
  bool IsEmpty() MOZ_REQUIRES(mMutex);
  bool IsLastChunk(uint32_t aChunk) MOZ_REQUIRES(mMutex);

  //////////////////////////////////////////////////////////////////////////////
  // Member variables.
  //////////////////////////////////////////////////////////////////////////////

  /// All private members are protected by mMutex.
  mutable Mutex mMutex;

  /// The data in this SourceBuffer, stored as a series of Chunks.
  AutoTArray<Chunk, 1> mChunks MOZ_GUARDED_BY(mMutex);

  /// Consumers which are waiting to be notified when new data is available.
  nsTArray<RefPtr<IResumable>> mWaitingConsumers MOZ_GUARDED_BY(mMutex);

  /// If present, marks this SourceBuffer complete with the given final status.
  Maybe<nsresult> mStatus MOZ_GUARDED_BY(mMutex);

  /// Count of active consumers.
  uint32_t mConsumerCount MOZ_GUARDED_BY(mMutex);

  /// True if compacting has been performed.
  bool mCompacted MOZ_GUARDED_BY(mMutex);
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_sourcebuffer_h
