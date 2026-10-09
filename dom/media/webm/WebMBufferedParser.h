/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(WebMBufferedParser_h_)
#  define WebMBufferedParser_h_

#  include "MediaResource.h"
#  include "MediaResult.h"
#  include "mozilla/Mutex.h"
#  include "nsISupportsImpl.h"
#  include "nsTArray.h"

namespace mozilla {

// Stores a stream byte offset and the scaled timecode of the block at
// that offset.
struct WebMTimeDataOffset {
  WebMTimeDataOffset(int64_t aEndOffset, uint64_t aTimecode,
                     int64_t aInitOffset, int64_t aSyncOffset,
                     int64_t aClusterEndOffset)
      : mEndOffset(aEndOffset),
        mInitOffset(aInitOffset),
        mSyncOffset(aSyncOffset),
        mClusterEndOffset(aClusterEndOffset),
        mTimecode(aTimecode) {}

  bool operator==(int64_t aEndOffset) const { return mEndOffset == aEndOffset; }

  bool operator!=(int64_t aEndOffset) const { return mEndOffset != aEndOffset; }

  bool operator<(int64_t aEndOffset) const { return mEndOffset < aEndOffset; }

  int64_t mEndOffset;
  int64_t mInitOffset;
  int64_t mSyncOffset;
  int64_t mClusterEndOffset;
  // In nanoseconds
  uint64_t mTimecode;
};

// A simple WebM parser that produces data offset to timecode pairs as it
// consumes blocks.  A new parser is created for each distinct range of data
// received and begins parsing from the first WebM cluster within that
// range.  Old parsers are destroyed when their range merges with a later
// parser or an already parsed range.  The parser may start at any position
// within the stream.
struct WebMBufferedParser {
  explicit WebMBufferedParser(int64_t aOffset);

  uint32_t GetTimecodeScale() {
    MOZ_ASSERT(mGotTimecodeScale);
    return mTimecodeScale;
  }

  // Use this function when we would only feed media segment for the parser.
  void AppendMediaSegmentOnly() { mGotTimecodeScale = true; }

  // If this parser is not expected to parse a segment info, it must be told
  // the appropriate timecode scale to use from elsewhere.
  void SetTimecodeScale(uint32_t aTimecodeScale);

  // Steps the parser through aLength bytes of data.  Always consumes
  // aLength bytes.  Updates mCurrentOffset before returning.
  // Returns false if an error was encountered.
  MediaResult Append(const unsigned char* aBuffer, uint32_t aLength,
                     nsTArray<WebMTimeDataOffset>& aMapping);

  bool operator==(int64_t aOffset) const { return mCurrentOffset == aOffset; }

  bool operator<(int64_t aOffset) const { return mCurrentOffset < aOffset; }

  // Returns the start offset of the init (EBML) or media segment (Cluster)
  // following the aOffset position. If none were found, returns
  // mBlockEndOffset. This allows to determine the end of the interval containg
  // aOffset.
  int64_t EndSegmentOffset(int64_t aOffset);

  // Return the Cluster offset, return -1 if we can't find the Cluster.
  int64_t GetClusterOffset() const;

  // The offset at which this parser started parsing.  Used to merge
  // adjacent parsers, in which case the later parser adopts the earlier
  // parser's mStartOffset.
  int64_t mStartOffset;

  // Current offset within the stream.  Updated in chunks as Append() consumes
  // data.
  int64_t mCurrentOffset;

  // End of the first init segment; at or past where Tracks ends. Will only
  // be set if a Segment Information has been found.
  int64_t mInitEndOffset;

  // End offset of the last block parsed.
  // Will only be set if a complete block has been parsed.
  int64_t mBlockEndOffset;

 private:
  enum State {
    // Parser start state.  Expects to begin at a valid EBML element.  Move
    // to READ_VINT with mVIntRaw true, then return to READ_ELEMENT_SIZE.
    READ_ELEMENT_ID,

    // Store element ID read into mVInt into mElement.mID.  Move to
    // READ_VINT with mVIntRaw false, then return to PARSE_ELEMENT.
    READ_ELEMENT_SIZE,

    // Parser start state for parsers started at an arbitrary offset.  Scans
    // forward for the first cluster, then move to READ_ELEMENT_ID.
    FIND_CLUSTER_SYNC,

    // Simplistic core of the parser.  Does not pay attention to nesting of
    // elements.  Checks mElement for an element ID of interest, then moves
    // to the next state as determined by the element ID.
    PARSE_ELEMENT,

    // Read the first byte of a variable length integer.  The first byte
    // encodes both the variable integer's length and part of the value.
    // The value read so far is stored in mVInt.mValue and the length is
    // stored in mVInt.mLength.  The number of bytes left to read is stored
    // in mVIntLeft.
    READ_VINT,

    // Reads the remaining mVIntLeft bytes into mVInt.mValue.
    READ_VINT_REST,

    // mVInt holds the parsed timecode scale, store it in mTimecodeScale,
    // then return READ_ELEMENT_ID.
    READ_TIMECODESCALE,

    // mVInt holds the parsed cluster timecode, store it in
    // mClusterTimecode, then return to READ_ELEMENT_ID.
    READ_CLUSTER_TIMECODE,

    // mBlockTimecodeLength holds the remaining length of the block timecode
    // left to read.  Read each byte of the timecode into mBlockTimecode.
    // Once complete, calculate the scaled timecode from the cluster
    // timecode, block timecode, and timecode scale, and insert a
    // WebMTimeDataOffset entry into aMapping if one is not already present
    // for this offset.
    READ_BLOCK_TIMECODE,

    // mVInt holds the parsed EBMLMaxIdLength, store it in mEBMLMaxIdLength,
    // then return to READ_ELEMENT_ID.
    READ_EBML_MAX_ID_LENGTH,

    // mVInt holds the parsed EBMLMaxSizeLength, store it in mEBMLMaxSizeLength,
    // then return to READ_ELEMENT_ID.
    READ_EBML_MAX_SIZE_LENGTH,

    // Will skip the current tracks element and set mInitEndOffset if an init
    // segment has been found.
    // SKIP_DATA may extend it past the end of the tracks element.
    CHECK_INIT_FOUND,

    // Skip mSkipBytes of data before resuming parse at mNextState.
    SKIP_DATA,
  };

  // Current state machine action.
  State mState;

  // Next state machine action.  SKIP_DATA and READ_VINT_REST advance to
  // mNextState when the current action completes.
  State mNextState;

  struct VInt {
    VInt() : mValue(0), mLength(0) {}
    uint64_t mValue;
    uint64_t mLength;
  };

  struct EBMLElement {
    uint64_t Length() { return mID.mLength + mSize.mLength; }
    VInt mID;
    VInt mSize;
  };

  EBMLElement mElement;

  VInt mVInt;

  bool mVIntRaw;

  // EBML start offset. This indicates the start of the last init segment
  // parsed. Will only be set if an EBML element has been found.
  int64_t mLastInitStartOffset;

  // EBML element size. This indicates the size of the body of the last init
  // segment parsed. Will only be set if an EBML element has been found.
  uint32_t mLastInitSize;

  // EBML max id length is the max number of bytes allowed for an element id
  // vint.
  uint8_t mEBMLMaxIdLength;

  // EBML max size length is the max number of bytes allowed for an element size
  // vint.
  uint8_t mEBMLMaxSizeLength;

  // Current match position within CLUSTER_SYNC_ID.  Used to find sync
  // within arbitrary data.
  uint32_t mClusterSyncPos;

  // Number of bytes of mVInt left to read.  mVInt is complete once this
  // reaches 0.
  uint32_t mVIntLeft;

  // Size of the block currently being parsed.  Any unused data within the
  // block is skipped once the block timecode has been parsed.
  uint64_t mBlockSize;

  // Cluster-level timecode.
  uint64_t mClusterTimecode;

  // Start offset of the cluster currently being parsed.  Used as the sync
  // point offset for the offset-to-time mapping as each block timecode is
  // been parsed. -1 if unknown.
  int64_t mClusterOffset;

  // End offset of the cluster currently being parsed. -1 if unknown.
  int64_t mClusterEndOffset;

  // Start offset of the block currently being parsed.  Used as the byte
  // offset for the offset-to-time mapping once the block timecode has been
  // parsed.
  int64_t mBlockOffset;

  // Block-level timecode.  This is summed with mClusterTimecode to produce
  // an absolute timecode for the offset-to-time mapping.
  int16_t mBlockTimecode;

  // Number of bytes of mBlockTimecode left to read.
  uint32_t mBlockTimecodeLength;

  // Count of bytes left to skip before resuming parse at mNextState.
  // Mostly used to skip block payload data after reading a block timecode.
  uint32_t mSkipBytes;

  // Timecode scale read from the segment info and used to scale absolute
  // timecodes.
  uint32_t mTimecodeScale;

  // True if we read the timecode scale from the segment info or have
  // confirmed that the default value is to be used.
  bool mGotTimecodeScale;

  // True if we've read the cluster time code.
  bool mGotClusterTimecode;
};

class WebMBufferedState final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebMBufferedState)

 public:
  WebMBufferedState() : mMutex("WebMBufferedState") {
    MOZ_COUNT_CTOR(WebMBufferedState);
  }

  void NotifyDataArrived(const unsigned char* aBuffer, uint32_t aLength,
                         int64_t aOffset);
  void Reset();
  void UpdateIndex(const MediaByteRangeSet& aRanges, MediaResource* aResource);
  bool CalculateBufferedForRange(int64_t aStartOffset, int64_t aEndOffset,
                                 uint64_t* aStartTime, uint64_t* aEndTime);

  // Returns true if mTimeMapping is not empty and sets aOffset to
  // the latest offset for which decoding can resume without data
  // dependencies to arrive at aTime. aTime will be clamped to the start
  // of mTimeMapping if it is earlier than the first element, and to the end
  // if later than the last
  bool GetOffsetForTime(uint64_t aTime, int64_t* aOffset);

  // Returns end offset of init segment or -1 if none found.
  int64_t GetInitEndOffset();

  // Returns start time
  bool GetStartTime(uint64_t* aTime);

  // Returns keyframe for time
  bool GetNextKeyframeTime(uint64_t aTime, uint64_t* aKeyframeTime);

 private:
  // Private destructor, to discourage deletion outside of Release():
  MOZ_COUNTED_DTOR(WebMBufferedState)

  // Synchronizes access to the mTimeMapping array.
  Mutex mMutex;

  // Sorted (by offset) map of data offsets to timecodes.  Populated
  // on the main thread as data is received and parsed by WebMBufferedParsers.
  nsTArray<WebMTimeDataOffset> mTimeMapping MOZ_GUARDED_BY(mMutex);

  // Sorted (by offset) live parser instances.  Main thread only.
  nsTArray<WebMBufferedParser> mRangeParsers;
};

}  // namespace mozilla

#endif
