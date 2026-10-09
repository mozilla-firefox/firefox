/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebMBufferedParser.h"

#include <algorithm>

#include "MediaDataDemuxer.h"
#include "mozilla/CheckedInt.h"
#include "nsThreadUtils.h"

#define WEBM_DEBUG(arg, ...)                                                \
  MOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Debug,                   \
              "WebMBufferedParser({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)

namespace mozilla {

static uint32_t VIntLength(unsigned char aFirstByte, uint32_t* aMask) {
  uint32_t count = 1;
  uint32_t mask = 1 << 7;
  while (count < 8) {
    if ((aFirstByte & mask) != 0) {
      break;
    }
    mask >>= 1;
    count += 1;
  }
  if (aMask) {
    *aMask = mask;
  }
  NS_ASSERTION(count >= 1 && count <= 8, "Insane VInt length.");
  return count;
}

constexpr uint8_t EBML_MAX_ID_LENGTH_DEFAULT = 4;
constexpr uint8_t EBML_MAX_SIZE_LENGTH_DEFAULT = 8;

WebMBufferedParser::WebMBufferedParser(int64_t aOffset)
    : mStartOffset(aOffset),
      mCurrentOffset(aOffset),
      mInitEndOffset(-1),
      mBlockEndOffset(-1),
      mState(READ_ELEMENT_ID),
      mNextState(READ_ELEMENT_ID),
      mVIntRaw(false),
      mLastInitStartOffset(-1),
      mLastInitSize(0),
      mEBMLMaxIdLength(EBML_MAX_ID_LENGTH_DEFAULT),
      mEBMLMaxSizeLength(EBML_MAX_SIZE_LENGTH_DEFAULT),
      mClusterSyncPos(0),
      mVIntLeft(0),
      mBlockSize(0),
      mClusterTimecode(0),
      mClusterOffset(-1),
      mClusterEndOffset(-1),
      mBlockOffset(0),
      mBlockTimecode(0),
      mBlockTimecodeLength(0),
      mSkipBytes(0),
      mTimecodeScale(1000000),
      mGotTimecodeScale(false),
      mGotClusterTimecode(false) {
  if (mStartOffset != 0) {
    mState = FIND_CLUSTER_SYNC;
  }
}

void WebMBufferedParser::SetTimecodeScale(uint32_t aTimecodeScale) {
  mTimecodeScale = aTimecodeScale;
  WEBM_DEBUG("{}", mTimecodeScale);
  mGotTimecodeScale = true;
}

MediaResult WebMBufferedParser::Append(const unsigned char* aBuffer,
                                       uint32_t aLength,
                                       nsTArray<WebMTimeDataOffset>& aMapping) {
  static const uint32_t EBML_ID = 0x1a45dfa3;
  static const uint32_t SEGMENT_ID = 0x18538067;
  static const uint32_t SEGINFO_ID = 0x1549a966;
  static const uint32_t TRACKS_ID = 0x1654AE6B;
  static const uint32_t CLUSTER_ID = 0x1f43b675;
  static const uint32_t TIMECODESCALE_ID = 0x2ad7b1;
  static const unsigned char TIMECODE_ID = 0xe7;
  static const unsigned char BLOCKGROUP_ID = 0xa0;
  static const unsigned char BLOCK_ID = 0xa1;
  static const unsigned char SIMPLEBLOCK_ID = 0xa3;
  static const uint16_t EBML_MAX_ID_LENGTH_ID = 0x42f2;
  static const uint16_t EBML_MAX_SIZE_LENGTH_ID = 0x42f3;
  static const uint32_t BLOCK_TIMECODE_LENGTH = 2;

  static const unsigned char CLUSTER_SYNC_ID[] = {0x1f, 0x43, 0xb6, 0x75};

  const unsigned char* p = aBuffer;

  // Parse each byte in aBuffer one-by-one, producing timecodes and updating
  // aMapping as we go.  Parser pauses at end of stream (which may be at any
  // point within the parse) and resumes parsing the next time Append is
  // called with new data.
  while (p < aBuffer + aLength) {
    switch (mState) {
      case READ_ELEMENT_ID:
        mVIntRaw = true;
        mState = READ_VINT;
        mNextState = READ_ELEMENT_SIZE;
        break;
      case READ_ELEMENT_SIZE:
        if (mVInt.mLength > mEBMLMaxIdLength) {
          nsPrintfCString detail("Invalid element id of length %" PRIu64,
                                 mVInt.mLength);
          WEBM_DEBUG("{}", detail.get());
          return MediaResult(NS_ERROR_FAILURE, detail);
        }
        mVIntRaw = false;
        mElement.mID = mVInt;
        mState = READ_VINT;
        mNextState = PARSE_ELEMENT;
        break;
      case FIND_CLUSTER_SYNC:
        if (*p++ == CLUSTER_SYNC_ID[mClusterSyncPos]) {
          mClusterSyncPos += 1;
        } else {
          mClusterSyncPos = 0;
        }
        if (mClusterSyncPos == sizeof(CLUSTER_SYNC_ID)) {
          mVInt.mValue = CLUSTER_ID;
          mVInt.mLength = sizeof(CLUSTER_SYNC_ID);
          mState = READ_ELEMENT_SIZE;
        }
        break;
      case PARSE_ELEMENT:
        if (mVInt.mLength > mEBMLMaxSizeLength) {
          nsPrintfCString detail("Invalid element size of length %" PRIu64,
                                 mVInt.mLength);
          WEBM_DEBUG("{}", detail.get());
          return MediaResult(NS_ERROR_FAILURE, detail);
        }
        mElement.mSize = mVInt;
        switch (mElement.mID.mValue) {
          case SEGMENT_ID:
            mState = READ_ELEMENT_ID;
            break;
          case SEGINFO_ID:
            mGotTimecodeScale = true;
            mState = READ_ELEMENT_ID;
            break;
          case TIMECODE_ID:
            mVInt = VInt();
            mVIntLeft = mElement.mSize.mValue;
            mState = READ_VINT_REST;
            mNextState = READ_CLUSTER_TIMECODE;
            break;
          case TIMECODESCALE_ID:
            mVInt = VInt();
            mVIntLeft = mElement.mSize.mValue;
            mState = READ_VINT_REST;
            mNextState = READ_TIMECODESCALE;
            break;
          case CLUSTER_ID:
            mClusterOffset = mCurrentOffset + (p - aBuffer) -
                             (mElement.mID.mLength + mElement.mSize.mLength);
            // Handle "unknown" length;
            if (mElement.mSize.mValue + 1 !=
                uint64_t(1) << (mElement.mSize.mLength * 7)) {
              mClusterEndOffset = mClusterOffset + mElement.mID.mLength +
                                  mElement.mSize.mLength +
                                  mElement.mSize.mValue;
            } else {
              mClusterEndOffset = -1;
            }
            mGotClusterTimecode = false;
            mState = READ_ELEMENT_ID;
            break;
          case BLOCKGROUP_ID:
            mState = READ_ELEMENT_ID;
            break;
          case SIMPLEBLOCK_ID:
            /* FALLTHROUGH */
          case BLOCK_ID:
            if (!mGotClusterTimecode) {
              WEBM_DEBUG(
                  "The Timecode element must appear before any Block or "
                  "SimpleBlock elements in a Cluster");
              return MediaResult(
                  NS_ERROR_FAILURE,
                  "The Timecode element must appear before any Block or "
                  "SimpleBlock elements in a Cluster");
            }
            mBlockSize = mElement.mSize.mValue;
            mBlockTimecode = 0;
            mBlockTimecodeLength = BLOCK_TIMECODE_LENGTH;
            mBlockOffset = mCurrentOffset + (p - aBuffer) -
                           (mElement.mID.mLength + mElement.mSize.mLength);
            mState = READ_VINT;
            mNextState = READ_BLOCK_TIMECODE;
            break;
          case TRACKS_ID:
            mSkipBytes = mElement.mSize.mValue;
            mState = CHECK_INIT_FOUND;
            break;
          case EBML_MAX_ID_LENGTH_ID:
          case EBML_MAX_SIZE_LENGTH_ID:
            if (int64_t currentOffset = mCurrentOffset + (p - aBuffer);
                currentOffset < mLastInitStartOffset ||
                currentOffset >= mLastInitStartOffset + mLastInitSize) {
              nsPrintfCString str("Unexpected %s outside init segment",
                                  mElement.mID.mValue == EBML_MAX_ID_LENGTH_ID
                                      ? "EBMLMaxIdLength"
                                      : "EBMLMaxSizeLength");
              WEBM_DEBUG("{}", str.get());
              return MediaResult(NS_ERROR_FAILURE, str);
            }
            if (mElement.mSize.mValue > 8) {
              // https://www.rfc-editor.org/rfc/rfc8794.html (EBML):
              //   An Unsigned Integer Element MUST declare a length from zero
              //   to eight octets.
              nsPrintfCString str("Bad length of %s size",
                                  mElement.mID.mValue == EBML_MAX_ID_LENGTH_ID
                                      ? "EBMLMaxIdLength"
                                      : "EBMLMaxSizeLength");
              WEBM_DEBUG("{}", str.get());
              return MediaResult(NS_ERROR_FAILURE, str);
            }
            mVInt = VInt();
            mVIntLeft = mElement.mSize.mValue;
            mState = READ_VINT_REST;
            mNextState = mElement.mID.mValue == EBML_MAX_ID_LENGTH_ID
                             ? READ_EBML_MAX_ID_LENGTH
                             : READ_EBML_MAX_SIZE_LENGTH;
            break;
          case EBML_ID:
            mLastInitStartOffset =
                mCurrentOffset + (p - aBuffer) -
                (mElement.mID.mLength + mElement.mSize.mLength);
            mLastInitSize = mElement.mSize.mValue;
            mEBMLMaxIdLength = EBML_MAX_ID_LENGTH_DEFAULT;
            mEBMLMaxSizeLength = EBML_MAX_SIZE_LENGTH_DEFAULT;
            mState = READ_ELEMENT_ID;
            break;
          default:
            mSkipBytes = mElement.mSize.mValue;
            mState = SKIP_DATA;
            mNextState = READ_ELEMENT_ID;
            break;
        }
        break;
      case READ_VINT: {
        unsigned char c = *p++;
        uint32_t mask;
        mVInt.mLength = VIntLength(c, &mask);
        mVIntLeft = mVInt.mLength - 1;
        mVInt.mValue = mVIntRaw ? c : c & ~mask;
        mState = READ_VINT_REST;
        break;
      }
      case READ_VINT_REST:
        if (mVIntLeft) {
          mVInt.mValue <<= 8;
          mVInt.mValue |= *p++;
          mVIntLeft -= 1;
        } else {
          mState = mNextState;
        }
        break;
      case READ_TIMECODESCALE:
        if (!mGotTimecodeScale) {
          WEBM_DEBUG("Should get the SegmentInfo first");
          return MediaResult(NS_ERROR_FAILURE,
                             "TimecodeScale appeared before SegmentInfo");
        }
        mTimecodeScale = mVInt.mValue;
        WEBM_DEBUG("READ_TIMECODESCALE {}", mTimecodeScale);
        mState = READ_ELEMENT_ID;
        break;
      case READ_CLUSTER_TIMECODE:
        mClusterTimecode = mVInt.mValue;
        mGotClusterTimecode = true;
        mState = READ_ELEMENT_ID;
        break;
      case READ_BLOCK_TIMECODE:
        if (mBlockTimecodeLength) {
          mBlockTimecode <<= 8;
          mBlockTimecode |= *p++;
          mBlockTimecodeLength -= 1;
        } else {
          // It's possible we've parsed this data before, so avoid inserting
          // duplicate WebMTimeDataOffset entries.
          {
            int64_t endOffset = mBlockOffset + mBlockSize +
                                mElement.mID.mLength + mElement.mSize.mLength;
            uint32_t idx = aMapping.IndexOfFirstElementGt(endOffset);
            if (idx == 0 || aMapping[idx - 1] != endOffset) {
              // Don't insert invalid negative timecodes.
              if (mBlockTimecode >= 0 ||
                  mClusterTimecode >= uint16_t(abs(mBlockTimecode))) {
                if (!mGotTimecodeScale) {
                  WEBM_DEBUG("Should get the TimecodeScale first");
                  return MediaResult(NS_ERROR_FAILURE,
                                     "Timecode appeared before SegmentInfo");
                }
                CheckedInt<uint64_t> checkedTimecode =
                    CheckedInt<uint64_t>(mClusterTimecode + mBlockTimecode) *
                    mTimecodeScale;
                if (!checkedTimecode.isValid() ||
                    checkedTimecode.value() >
                        static_cast<uint64_t>(INT64_MAX)) {
                  WEBM_DEBUG("Timecode overflow: {} in Cluster at {} ignored",
                             checkedTimecode.isValid() ? checkedTimecode.value()
                                                       : UINT64_MAX,
                             mClusterOffset);
                } else {
                  uint64_t absTimecode = checkedTimecode.value();
                  if ((idx == 0 ||
                       aMapping[idx - 1].mTimecode <= absTimecode) &&
                      (idx == aMapping.Length() ||
                       aMapping[idx].mTimecode >= absTimecode)) {
                    WebMTimeDataOffset entry(endOffset, absTimecode,
                                             mLastInitStartOffset,
                                             mClusterOffset, mClusterEndOffset);
                    aMapping.InsertElementAt(idx, entry);
                  } else {
                    WEBM_DEBUG(
                        "Out of order timecode {} in Cluster at {} ignored",
                        absTimecode, mClusterOffset);
                  }
                }
              }
            }
          }

          // Skip rest of block header and the block's payload.
          mBlockSize -= mVInt.mLength;
          mBlockSize -= BLOCK_TIMECODE_LENGTH;
          mSkipBytes = uint32_t(mBlockSize);
          mState = SKIP_DATA;
          mNextState = READ_ELEMENT_ID;
        }
        break;
      case READ_EBML_MAX_ID_LENGTH:
        if (mElement.mSize.mLength == 0) {
          // https://www.rfc-editor.org/rfc/rfc8794.html (EBML):
          //   If an Empty Element has a default value declared, then the EBML
          //   Reader MUST interpret the value of the Empty Element as the
          //   default value.
          mVInt.mValue = EBML_MAX_ID_LENGTH_DEFAULT;
        }
        if (mVInt.mValue < 4 || mVInt.mValue > 5) {
          // https://www.ietf.org/archive/id/draft-ietf-cellar-matroska-13.html
          // (Matroska):
          //   The EBMLMaxIDLength of the EBML Header MUST be "4".
          //
          // Also Matroska:
          //   Element IDs are encoded using the VINT mechanism described in
          //   Section 4 of [RFC8794] and can be between one and five octets
          //   long. Five-octet-long Element IDs are possible only if declared
          //   in the EBML header.
          nsPrintfCString detail("Invalid EMBLMaxIdLength %" PRIu64,
                                 mVInt.mValue);
          WEBM_DEBUG("{}", detail.get());
          return MediaResult(NS_ERROR_FAILURE, detail);
        }
        mEBMLMaxIdLength = mVInt.mValue;
        mState = READ_ELEMENT_ID;
        break;
      case READ_EBML_MAX_SIZE_LENGTH:
        if (mElement.mSize.mLength == 0) {
          // https://www.rfc-editor.org/rfc/rfc8794.html (EBML):
          //   If an Empty Element has a default value declared, then the EBML
          //   Reader MUST interpret the value of the Empty Element as the
          //   default value.
          mVInt.mValue = EBML_MAX_SIZE_LENGTH_DEFAULT;
        }
        if (mVInt.mValue < 1 || mVInt.mValue > 8) {
          // https://www.ietf.org/archive/id/draft-ietf-cellar-matroska-13.html
          // (Matroska):
          //   The EBMLMaxSizeLength of the EBML Header MUST be between "1" and
          //   "8" inclusive.
          nsPrintfCString detail("Invalid EMBLMaxSizeLength %" PRIu64,
                                 mVInt.mValue);
          WEBM_DEBUG("{}", detail.get());
          return MediaResult(NS_ERROR_FAILURE, detail);
        }
        mEBMLMaxSizeLength = mVInt.mValue;
        mState = READ_ELEMENT_ID;
        break;
      case SKIP_DATA:
        if (mSkipBytes) {
          uint32_t left = aLength - (p - aBuffer);
          left = std::min(left, mSkipBytes);
          p += left;
          mSkipBytes -= left;
        }
        if (!mSkipBytes) {
          mBlockEndOffset = mCurrentOffset + (p - aBuffer);
          // Per the WebM byte-stream spec the init segment runs up to (but
          // not including) the first Cluster, so any non-Cluster top-level
          // element (Cues, Tags, Chapters, SeekHead, Void, ...) that
          // appears between Tracks and the first Cluster is part of the
          // init segment. Extend mInitEndOffset over this just-skipped
          // element if we're past Tracks (mInitEndOffset >= 0) but haven't
          // yet seen a Cluster (mClusterOffset < 0).
          // A buffer can hold several init segments back to back, so only
          // extend while still inside the first one
          // (mLastInitStartOffset < mInitEndOffset).
          if (mInitEndOffset >= 0 && mClusterOffset < 0 &&
              mLastInitStartOffset < mInitEndOffset) {
            mInitEndOffset = mBlockEndOffset;
          }
          mState = mNextState;
        }
        break;
      case CHECK_INIT_FOUND:
        if (mSkipBytes) {
          uint32_t left = aLength - (p - aBuffer);
          left = std::min(left, mSkipBytes);
          p += left;
          mSkipBytes -= left;
        }
        if (!mSkipBytes) {
          if (mInitEndOffset < 0) {
            mInitEndOffset = mCurrentOffset + (p - aBuffer);
            mBlockEndOffset = mCurrentOffset + (p - aBuffer);
          }
          mState = READ_ELEMENT_ID;
        }
        break;
    }
  }

  NS_ASSERTION(p == aBuffer + aLength, "Must have parsed to end of data.");
  mCurrentOffset += aLength;

  return NS_OK;
}

int64_t WebMBufferedParser::EndSegmentOffset(int64_t aOffset) {
  if (mLastInitStartOffset > aOffset || mClusterOffset > aOffset) {
    return std::min(
        mLastInitStartOffset >= 0 ? mLastInitStartOffset : INT64_MAX,
        mClusterOffset >= 0 ? mClusterOffset : INT64_MAX);
  }
  return mBlockEndOffset;
}

int64_t WebMBufferedParser::GetClusterOffset() const { return mClusterOffset; }

// SyncOffsetComparator and TimeComparator are slightly confusing, in that
// the nsTArray they're used with (mTimeMapping) is sorted by mEndOffset and
// these comparators are used on the other fields of WebMTimeDataOffset.
// This is only valid because timecodes are required to be monotonically
// increasing within a file (thus establishing an ordering relationship with
// mTimecode), and mEndOffset is derived from mSyncOffset.
struct SyncOffsetComparator {
  bool Equals(const WebMTimeDataOffset& a, const int64_t& b) const {
    return a.mSyncOffset == b;
  }

  bool LessThan(const WebMTimeDataOffset& a, const int64_t& b) const {
    return a.mSyncOffset < b;
  }
};

struct TimeComparator {
  bool Equals(const WebMTimeDataOffset& a, const uint64_t& b) const {
    return a.mTimecode == b;
  }

  bool LessThan(const WebMTimeDataOffset& a, const uint64_t& b) const {
    return a.mTimecode < b;
  }
};

bool WebMBufferedState::CalculateBufferedForRange(int64_t aStartOffset,
                                                  int64_t aEndOffset,
                                                  uint64_t* aStartTime,
                                                  uint64_t* aEndTime) {
  MutexAutoLock lock(mMutex);

  // Find the first WebMTimeDataOffset at or after aStartOffset.
  uint32_t start = mTimeMapping.IndexOfFirstElementGt(aStartOffset - 1,
                                                      SyncOffsetComparator());
  if (start == mTimeMapping.Length()) {
    return false;
  }

  // Find the first WebMTimeDataOffset at or before aEndOffset.
  uint32_t end = mTimeMapping.IndexOfFirstElementGt(aEndOffset);
  if (end > 0) {
    end -= 1;
  }

  // Range is empty.
  if (end <= start) {
    return false;
  }

  NS_ASSERTION(mTimeMapping[start].mSyncOffset >= aStartOffset &&
                   mTimeMapping[end].mEndOffset <= aEndOffset,
               "Computed time range must lie within data range.");
  if (start > 0) {
    NS_ASSERTION(mTimeMapping[start - 1].mSyncOffset < aStartOffset,
                 "Must have found least WebMTimeDataOffset for start");
  }
  if (end < mTimeMapping.Length() - 1) {
    NS_ASSERTION(mTimeMapping[end + 1].mEndOffset > aEndOffset,
                 "Must have found greatest WebMTimeDataOffset for end");
  }

  MOZ_ASSERT(mTimeMapping[end].mTimecode >= mTimeMapping[end - 1].mTimecode);
  uint64_t frameDuration =
      mTimeMapping[end].mTimecode - mTimeMapping[end - 1].mTimecode;
  *aStartTime = mTimeMapping[start].mTimecode;
  CheckedUint64 endTime{mTimeMapping[end].mTimecode};
  endTime += frameDuration;
  if (!endTime.isValid()) {
    WEBM_DEBUG("End time overflow during CalculateBufferedForRange.");
    return false;
  }
  *aEndTime = endTime.value();
  return true;
}

bool WebMBufferedState::GetOffsetForTime(uint64_t aTime, int64_t* aOffset) {
  MutexAutoLock lock(mMutex);

  if (mTimeMapping.IsEmpty()) {
    return false;
  }

  uint64_t time = aTime;
  if (time > 0) {
    time = time - 1;
  }
  uint32_t idx = mTimeMapping.IndexOfFirstElementGt(time, TimeComparator());
  if (idx == mTimeMapping.Length()) {
    // Clamp to end
    *aOffset = mTimeMapping[mTimeMapping.Length() - 1].mSyncOffset;
  } else {
    // Idx is within array or has been clamped to start
    *aOffset = mTimeMapping[idx].mSyncOffset;
  }
  return true;
}

void WebMBufferedState::NotifyDataArrived(const unsigned char* aBuffer,
                                          uint32_t aLength, int64_t aOffset) {
  uint32_t idx = mRangeParsers.IndexOfFirstElementGt(aOffset - 1);
  if (idx == 0 || !(mRangeParsers[idx - 1] == aOffset)) {
    // If the incoming data overlaps an already parsed range, adjust the
    // buffer so that we only reparse the new data.  It's also possible to
    // have an overlap where the end of the incoming data is within an
    // already parsed range, but we don't bother handling that other than by
    // avoiding storing duplicate timecodes when the parser runs.
    if (idx != mRangeParsers.Length() &&
        mRangeParsers[idx].mStartOffset <= aOffset) {
      // Complete overlap, skip parsing.
      if (aOffset + aLength <= mRangeParsers[idx].mCurrentOffset) {
        return;
      }

      // Partial overlap, adjust the buffer to parse only the new data.
      int64_t adjust = mRangeParsers[idx].mCurrentOffset - aOffset;
      NS_ASSERTION(adjust >= 0, "Overlap detection bug.");
      aBuffer += adjust;
      aLength -= uint32_t(adjust);
    } else {
      mRangeParsers.InsertElementAt(idx, WebMBufferedParser(aOffset));
      if (idx != 0) {
        mRangeParsers[idx].SetTimecodeScale(
            mRangeParsers[0].GetTimecodeScale());
      }
    }
  }

  {
    MutexAutoLock lock(mMutex);
    mRangeParsers[idx].Append(aBuffer, aLength, mTimeMapping);
  }

  // Merge parsers with overlapping regions and clean up the remnants.
  uint32_t i = 0;
  while (i + 1 < mRangeParsers.Length()) {
    if (mRangeParsers[i].mCurrentOffset >= mRangeParsers[i + 1].mStartOffset) {
      mRangeParsers[i + 1].mStartOffset = mRangeParsers[i].mStartOffset;
      mRangeParsers[i + 1].mInitEndOffset = mRangeParsers[i].mInitEndOffset;
      mRangeParsers.RemoveElementAt(i);
    } else {
      i += 1;
    }
  }
}

void WebMBufferedState::Reset() {
  MutexAutoLock lock(mMutex);
  mRangeParsers.Clear();
  mTimeMapping.Clear();
}

void WebMBufferedState::UpdateIndex(const MediaByteRangeSet& aRanges,
                                    MediaResource* aResource) {
  for (uint32_t index = 0; index < aRanges.Length(); index++) {
    const MediaByteRange& range = aRanges[index];
    int64_t offset = range.mStart;
    uint32_t length = range.mEnd - range.mStart;

    uint32_t idx = mRangeParsers.IndexOfFirstElementGt(offset - 1);
    if (!idx || !(mRangeParsers[idx - 1] == offset)) {
      // If the incoming data overlaps an already parsed range, adjust the
      // buffer so that we only reparse the new data.  It's also possible to
      // have an overlap where the end of the incoming data is within an
      // already parsed range, but we don't bother handling that other than by
      // avoiding storing duplicate timecodes when the parser runs.
      if (idx != mRangeParsers.Length() &&
          mRangeParsers[idx].mStartOffset <= offset) {
        // Complete overlap, skip parsing.
        if (offset + length <= mRangeParsers[idx].mCurrentOffset) {
          continue;
        }

        // Partial overlap, adjust the buffer to parse only the new data.
        int64_t adjust = mRangeParsers[idx].mCurrentOffset - offset;
        NS_ASSERTION(adjust >= 0, "Overlap detection bug.");
        offset += adjust;
        length -= uint32_t(adjust);
      } else {
        mRangeParsers.InsertElementAt(idx, WebMBufferedParser(offset));
        if (idx) {
          mRangeParsers[idx].SetTimecodeScale(
              mRangeParsers[0].GetTimecodeScale());
        }
      }
    }

    MediaResourceIndex res(aResource);
    while (length > 0) {
      static const uint32_t BLOCK_SIZE = 1048576;
      uint32_t block = std::min(length, BLOCK_SIZE);
      RefPtr<MediaByteBuffer> bytes = res.CachedMediaReadAt(offset, block);
      if (!bytes) {
        break;
      }
      NotifyDataArrived(bytes->Elements(), bytes->Length(), offset);
      length -= bytes->Length();
      offset += bytes->Length();
    }
  }
}

int64_t WebMBufferedState::GetInitEndOffset() {
  if (mRangeParsers.IsEmpty()) {
    return -1;
  }
  return mRangeParsers[0].mInitEndOffset;
}

bool WebMBufferedState::GetStartTime(uint64_t* aTime) {
  MutexAutoLock lock(mMutex);

  if (mTimeMapping.IsEmpty()) {
    return false;
  }

  uint32_t idx = mTimeMapping.IndexOfFirstElementGt(0, SyncOffsetComparator());
  if (idx == mTimeMapping.Length()) {
    return false;
  }

  *aTime = mTimeMapping[idx].mTimecode;
  return true;
}

bool WebMBufferedState::GetNextKeyframeTime(uint64_t aTime,
                                            uint64_t* aKeyframeTime) {
  MutexAutoLock lock(mMutex);
  int64_t offset = 0;
  bool rv = GetOffsetForTime(aTime, &offset);
  if (!rv) {
    return false;
  }
  uint32_t idx =
      mTimeMapping.IndexOfFirstElementGt(offset, SyncOffsetComparator());
  if (idx == mTimeMapping.Length()) {
    return false;
  }
  *aKeyframeTime = mTimeMapping[idx].mTimecode;
  return true;
}
}  // namespace mozilla

#undef WEBM_DEBUG
