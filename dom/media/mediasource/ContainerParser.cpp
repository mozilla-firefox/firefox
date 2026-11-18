/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContainerParser.h"

#include <algorithm>

#include "AtomType.h"
#include "BufferReader.h"
#include "ByteStream.h"
#include "MP4Interval.h"
#include "MPEGAudioByteStream.h"
#include "MediaData.h"
#include "MoofParser.h"
#include "SampleIterator.h"
#include "SourceBufferResource.h"
#include "WebMBufferedParser.h"
#include "mozilla/Casting.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/Try.h"
#include "nsMimeTypes.h"

extern mozilla::LogModule* GetMediaSourceSamplesLog();

#define MSE_DEBUG(arg, ...)                                               \
  DDMOZ_LOG_FMT(GetMediaSourceSamplesLog(), mozilla::LogLevel::Debug,     \
                "({})::{}: " arg, mType.OriginalString().get(), __func__, \
                ##__VA_ARGS__)
#define MSE_DEBUGV(arg, ...)                                              \
  DDMOZ_LOG_FMT(GetMediaSourceSamplesLog(), mozilla::LogLevel::Verbose,   \
                "({})::{}: " arg, mType.OriginalString().get(), __func__, \
                ##__VA_ARGS__)
#define MSE_DEBUGVEX(_this, arg, ...)                           \
  DDMOZ_LOGEX_FMT(_this, GetMediaSourceSamplesLog(),            \
                  mozilla::LogLevel::Verbose, "({})::{}: " arg, \
                  mType.OriginalString().get(), __func__, ##__VA_ARGS__)

namespace mozilla {

ContainerParser::ContainerParser(const MediaContainerType& aType)
    : mHasInitData(false), mTotalParsed(0), mGlobalOffset(0), mType(aType) {}

ContainerParser::~ContainerParser() = default;

MediaResult ContainerParser::IsInitSegmentPresent(const MediaSpan& aData) {
  MSE_DEBUG(
      "aLength={} [{:x}{:x}{:x}{:x}]", aData.Length(),
      aData.Length() > 0 ? aData[0] : 0, aData.Length() > 1 ? aData[1] : 0,
      aData.Length() > 2 ? aData[2] : 0, aData.Length() > 3 ? aData[3] : 0);
  return NS_ERROR_NOT_AVAILABLE;
}

MediaResult ContainerParser::IsMediaSegmentPresent(const MediaSpan& aData) {
  MSE_DEBUG(
      "aLength={} [{:x}{:x}{:x}{:x}]", aData.Length(),
      aData.Length() > 0 ? aData[0] : 0, aData.Length() > 1 ? aData[1] : 0,
      aData.Length() > 2 ? aData[2] : 0, aData.Length() > 3 ? aData[3] : 0);
  return NS_ERROR_NOT_AVAILABLE;
}

MediaResult ContainerParser::ParseStartAndEndTimestamps(const MediaSpan& aData,
                                                        media::TimeUnit& aStart,
                                                        media::TimeUnit& aEnd) {
  return NS_ERROR_NOT_AVAILABLE;
}

bool ContainerParser::TimestampsFuzzyEqual(int64_t aLhs, int64_t aRhs) {
  return llabs(aLhs - aRhs) <= GetRoundingError();
}

int64_t ContainerParser::GetRoundingError() {
  NS_WARNING("Using default ContainerParser::GetRoundingError implementation");
  return 0;
}

bool ContainerParser::HasCompleteInitData() {
  return mHasInitData && !!mInitData->Length();
}

MediaByteBuffer* ContainerParser::InitData() { return mInitData; }

MediaByteRange ContainerParser::InitSegmentRange() {
  return mCompleteInitSegmentRange;
}

MediaByteRange ContainerParser::MediaHeaderRange() {
  return mCompleteMediaHeaderRange;
}

MediaByteRange ContainerParser::MediaSegmentRange() {
  return mCompleteMediaSegmentRange;
}

DDLoggedTypeDeclNameAndBase(WebMContainerParser, ContainerParser);

class WebMContainerParser
    : public ContainerParser,
      public DecoderDoctorLifeLogger<WebMContainerParser> {
 public:
  explicit WebMContainerParser(const MediaContainerType& aType)
      : ContainerParser(aType), mParser(0), mOffset(0) {}

  static const unsigned NS_PER_USEC = 1000;

  MediaResult IsInitSegmentPresent(const MediaSpan& aData) override {
    ContainerParser::IsInitSegmentPresent(aData);
    if (aData.Length() < 4) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    WebMBufferedParser parser(0);
    nsTArray<WebMTimeDataOffset> mapping;
    if (auto result = parser.Append(aData.Elements(), aData.Length(), mapping);
        NS_FAILED(result)) {
      return result;
    }
    return parser.mInitEndOffset > 0 ? NS_OK : NS_ERROR_NOT_AVAILABLE;
  }

  MediaResult IsMediaSegmentPresent(const MediaSpan& aData) override {
    ContainerParser::IsMediaSegmentPresent(aData);
    if (aData.Length() < 4) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    WebMBufferedParser parser(0);
    nsTArray<WebMTimeDataOffset> mapping;
    parser.AppendMediaSegmentOnly();
    if (auto result = parser.Append(aData.Elements(), aData.Length(), mapping);
        NS_FAILED(result)) {
      return result;
    }
    return parser.GetClusterOffset() >= 0 ? NS_OK : NS_ERROR_NOT_AVAILABLE;
  }

  MediaResult ParseStartAndEndTimestamps(const MediaSpan& aData,
                                         media::TimeUnit& aStart,
                                         media::TimeUnit& aEnd) override {
    bool initSegment = NS_SUCCEEDED(IsInitSegmentPresent(aData));

    if (mLastMapping &&
        (initSegment || NS_SUCCEEDED(IsMediaSegmentPresent(aData)))) {
      // The last data contained a complete cluster but we can only detect it
      // now that a new one is starting.
      // We use mOffset as end position to ensure that any blocks not reported
      // by WebMBufferParser are properly skipped.
      mCompleteMediaSegmentRange =
          MediaByteRange(mLastMapping.ref().mSyncOffset, mOffset) +
          mGlobalOffset;
      mLastMapping.reset();
      MSE_DEBUG("New cluster found at start, ending previous one");
      return NS_ERROR_NOT_AVAILABLE;
    }

    if (initSegment) {
      mOffset = 0;
      mParser = WebMBufferedParser(0);
      mOverlappedMapping.Clear();
      mInitData = new MediaByteBuffer();
      mResource = new SourceBufferResource();
      DDLINKCHILD("resource", mResource.get());
      mCompleteInitSegmentRange = MediaByteRange();
      mCompleteMediaHeaderRange = MediaByteRange();
      mCompleteMediaSegmentRange = MediaByteRange();
      mGlobalOffset = mTotalParsed;
    }

    // XXX if it only adds new mappings, overlapped but not available
    // (e.g. overlap < 0) frames are "lost" from the reported mappings here.
    nsTArray<WebMTimeDataOffset> mapping;
    mapping.AppendElements(mOverlappedMapping);
    mOverlappedMapping.Clear();
    if (auto result = mParser.Append(aData.Elements(), aData.Length(), mapping);
        NS_FAILED(result)) {
      return result;
    }
    if (mResource) {
      mResource->AppendData(aData);
    }

    // XXX This is a bit of a hack.  Assume if there are no timecodes
    // present and it's an init segment that it's _just_ an init segment.
    // We should be more precise.
    if (initSegment || !HasCompleteInitData()) {
      if (mParser.mInitEndOffset > 0) {
        MOZ_DIAGNOSTIC_ASSERT(mInitData && mResource &&
                              mParser.mInitEndOffset <=
                                  mResource->GetCachedDataEnd(0));
        if (!mInitData->SetLength(mParser.mInitEndOffset, fallible)) {
          // Super unlikely OOM
          return NS_ERROR_OUT_OF_MEMORY;
        }
        mCompleteInitSegmentRange =
            MediaByteRange(0, mParser.mInitEndOffset) + mGlobalOffset;
        char* buffer = reinterpret_cast<char*>(mInitData->Elements());
        mResource->ReadFromCache(buffer, 0, mParser.mInitEndOffset);
        MSE_DEBUG("Stashed init of {} bytes.", mParser.mInitEndOffset);
        mResource = nullptr;
      } else {
        MSE_DEBUG("Incomplete init found.");
      }
      mHasInitData = true;
    }
    mOffset += aData.Length();
    mTotalParsed += aData.Length();

    if (mapping.IsEmpty()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    // Calculate media range for first media segment.

    // Check if we have a cluster finishing in the current data.
    uint32_t endIdx = mapping.Length() - 1;
    bool foundNewCluster = false;
    while (mapping[0].mSyncOffset != mapping[endIdx].mSyncOffset) {
      endIdx -= 1;
      foundNewCluster = true;
    }

    int32_t completeIdx = endIdx;
    while (completeIdx >= 0 && mOffset < mapping[completeIdx].mEndOffset) {
      MSE_DEBUG("block is incomplete, missing: {}",
                mapping[completeIdx].mEndOffset - mOffset);
      completeIdx -= 1;
    }

    // Save parsed blocks for which we do not have all data yet.
    mOverlappedMapping.AppendElements(mapping.Elements() + completeIdx + 1,
                                      mapping.Length() - completeIdx - 1);

    if (completeIdx < 0) {
      mLastMapping.reset();
      return NS_ERROR_NOT_AVAILABLE;
    }

    if (mCompleteMediaHeaderRange.IsEmpty()) {
      mCompleteMediaHeaderRange =
          MediaByteRange(mapping[0].mSyncOffset, mapping[0].mEndOffset) +
          mGlobalOffset;
    }

    if (foundNewCluster && mOffset >= mapping[endIdx].mEndOffset) {
      // We now have all information required to delimit a complete cluster.
      int64_t endOffset = mapping[endIdx + 1].mSyncOffset;
      if (mapping[endIdx + 1].mInitOffset > mapping[endIdx].mInitOffset) {
        // We have a new init segment before this cluster.
        endOffset = mapping[endIdx + 1].mInitOffset;
      }
      mCompleteMediaSegmentRange =
          MediaByteRange(mapping[endIdx].mSyncOffset, endOffset) +
          mGlobalOffset;
    } else if (mapping[endIdx].mClusterEndOffset >= 0 &&
               mOffset >= mapping[endIdx].mClusterEndOffset) {
      mCompleteMediaSegmentRange =
          MediaByteRange(
              mapping[endIdx].mSyncOffset,
              mParser.EndSegmentOffset(mapping[endIdx].mClusterEndOffset)) +
          mGlobalOffset;
    }

    Maybe<WebMTimeDataOffset> previousMapping;
    if (completeIdx) {
      previousMapping = Some(mapping[completeIdx - 1]);
    } else {
      previousMapping = mLastMapping;
    }

    mLastMapping = Some(mapping[completeIdx]);

    if (!previousMapping && completeIdx + 1u >= mapping.Length()) {
      // We have no previous nor next block available,
      // so we can't estimate this block's duration.
      return NS_ERROR_NOT_AVAILABLE;
    }

    uint64_t frameDuration =
        (completeIdx + 1u < mapping.Length())
            ? mapping[completeIdx + 1].mTimecode -
                  mapping[completeIdx].mTimecode
            : mapping[completeIdx].mTimecode - previousMapping.ref().mTimecode;
    aStart = media::TimeUnit::FromNanoseconds(
        AssertedCast<int64_t>(mapping[0].mTimecode));
    aEnd = media::TimeUnit::FromNanoseconds(
        AssertedCast<int64_t>(mapping[completeIdx].mTimecode + frameDuration));

    MSE_DEBUG("[{}, {}] [fso={}, leo={}, l={} processedIdx={} fs={}]",
              aStart.ToMicroseconds(), aEnd.ToMicroseconds(),
              mapping[0].mSyncOffset, mapping[completeIdx].mEndOffset,
              mapping.Length(), completeIdx, mCompleteMediaSegmentRange.mEnd);

    return NS_OK;
  }

  int64_t GetRoundingError() override {
    int64_t error = mParser.GetTimecodeScale() / NS_PER_USEC;
    return error * 2;
  }

 private:
  WebMBufferedParser mParser;
  nsTArray<WebMTimeDataOffset> mOverlappedMapping;
  int64_t mOffset;
  Maybe<WebMTimeDataOffset> mLastMapping;
};

DDLoggedTypeDeclNameAndBase(MP4Stream, ByteStream);

class MP4Stream : public ByteStream, public DecoderDoctorLifeLogger<MP4Stream> {
 public:
  explicit MP4Stream(SourceBufferResource* aResource);
  virtual ~MP4Stream();
  nsresult ReadAt(int64_t aOffset, void* aBuffer, size_t aCount,
                  size_t* aBytesRead) override;
  nsresult CachedReadAt(int64_t aOffset, void* aBuffer, size_t aCount,
                        size_t* aBytesRead) override;
  bool Length(int64_t* aSize) override;
  const uint8_t* GetContiguousAccess(int64_t aOffset, size_t aSize) override;

 private:
  RefPtr<SourceBufferResource> mResource;
};

MP4Stream::MP4Stream(SourceBufferResource* aResource) : mResource(aResource) {
  MOZ_COUNT_CTOR(MP4Stream);
  MOZ_ASSERT(aResource);
  DDLINKCHILD("resource", aResource);
}

MP4Stream::~MP4Stream() { MOZ_COUNT_DTOR(MP4Stream); }

nsresult MP4Stream::ReadAt(int64_t aOffset, void* aBuffer, size_t aCount,
                           size_t* aBytesRead) {
  return CachedReadAt(aOffset, aBuffer, aCount, aBytesRead);
}

nsresult MP4Stream::CachedReadAt(int64_t aOffset, void* aBuffer, size_t aCount,
                                 size_t* aBytesRead) {
  nsresult rv = mResource->ReadFromCache(reinterpret_cast<char*>(aBuffer),
                                         aOffset, aCount);
  if (NS_FAILED(rv)) {
    *aBytesRead = 0;
    return rv;
  }
  *aBytesRead = aCount;
  return rv;
}

const uint8_t* MP4Stream::GetContiguousAccess(int64_t aOffset, size_t aSize) {
  return mResource->GetContiguousAccess(aOffset, aSize);
}

bool MP4Stream::Length(int64_t* aSize) {
  if (mResource->GetLength() < 0) return false;
  *aSize = mResource->GetLength();
  return true;
}

DDLoggedTypeDeclNameAndBase(MP4ContainerParser, ContainerParser);

class MP4ContainerParser : public ContainerParser,
                           public DecoderDoctorLifeLogger<MP4ContainerParser> {
 public:
  explicit MP4ContainerParser(const MediaContainerType& aType)
      : ContainerParser(aType) {}

  MediaResult IsInitSegmentPresent(const MediaSpan& aData) override {
    ContainerParser::IsInitSegmentPresent(aData);
    // Each MP4 atom has a chunk size and chunk type. The root chunk in an MP4
    // file is the 'ftyp' atom followed by a file type. We just check for a
    // vaguely valid 'ftyp' atom.
    if (aData.Length() < 8) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    AtomParser parser(*this, aData, AtomParser::StopAt::eInitSegment);
    if (!parser.IsValid()) {
      return MediaResult(
          NS_ERROR_FAILURE,
          RESULT_DETAIL("Invalid Top-Level Box:%s", parser.LastInvalidBox()));
    }
    return parser.StartWithInitSegment() ? NS_OK : NS_ERROR_NOT_AVAILABLE;
  }

  MediaResult IsMediaSegmentPresent(const MediaSpan& aData) override {
    if (aData.Length() < 8) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    AtomParser parser(*this, aData, AtomParser::StopAt::eMediaSegment);
    if (!parser.IsValid()) {
      return MediaResult(
          NS_ERROR_FAILURE,
          RESULT_DETAIL("Invalid Box:%s", parser.LastInvalidBox()));
    }
    return parser.StartWithMediaSegment() ? NS_OK : NS_ERROR_NOT_AVAILABLE;
  }

 private:
  class AtomParser {
   public:
    enum class StopAt { eInitSegment, eMediaSegment, eEnd };

    AtomParser(const MP4ContainerParser& aParser, const MediaSpan& aData,
               StopAt aStop = StopAt::eEnd) {
      mValid = Init(aParser, aData, aStop).isOk();
    }

    Result<Ok, nsresult> Init(const MP4ContainerParser& aParser,
                              const MediaSpan& aData, StopAt aStop) {
      const MediaContainerType& mType(
          aParser.ContainerType());  // for logging macro.
      BufferReader reader(aData);
      AtomType initAtom("moov");
      AtomType mediaAtom("moof");
      AtomType dataAtom("mdat");

      // Valid top-level boxes defined in ISO/IEC 14496-12 (Table 1)
      static const AtomType validBoxes[] = {
          "ftyp", "moov",          // init segment
          "pdin", "free", "sidx",  // optional prior moov box
          "styp", "moof", "mdat",  // media segment
          "mfra", "skip", "meta", "meco", "ssix", "prft",  // others.
          "pssh",         // optional with encrypted EME, though ignored.
          "emsg",         // ISO23009-1:2014 Section 5.10.3.3
          "bloc", "uuid"  // boxes accepted by chrome.
      };

      while (reader.Remaining() >= 8) {
        uint64_t size = MOZ_TRY(reader.ReadU32());
        const uint8_t* typec = reader.Peek(4);
        AtomType type(MOZ_TRY(reader.ReadU32()));
        // We've seen fourcc not being ASCII in the wild. In this rare case,
        // print hex values instead of the ascii representation.
        if (isprint(typec[0]) && isprint(typec[1]) && isprint(typec[2]) &&
            isprint(typec[3])) {
          MSE_DEBUGVEX(&aParser, "Checking atom:'{}{}{}{}' @ {}",
                       static_cast<char>(typec[0]), static_cast<char>(typec[1]),
                       static_cast<char>(typec[2]), static_cast<char>(typec[3]),
                       static_cast<uint32_t>(reader.Offset()) - 8);
        } else {
          MSE_DEBUGVEX(
              &aParser,
              "Checking atom (not ASCII):'0x{:02x}{:02x}{:02x}{:02x}' @ {}",
              typec[0], typec[1], typec[2], typec[3],
              static_cast<uint32_t>(reader.Offset()) - 8);
        }
        if (std::find(std::begin(validBoxes), std::end(validBoxes), type) ==
            std::end(validBoxes)) {
          // No valid box found, no point continuing.
          mLastInvalidBox[0] = typec[0];
          mLastInvalidBox[1] = typec[1];
          mLastInvalidBox[2] = typec[2];
          mLastInvalidBox[3] = typec[3];
          mLastInvalidBox[4] = '\0';
          return Err(NS_ERROR_FAILURE);
        }
        if (mInitOffset.isNothing() && AtomType(type) == initAtom) {
          mInitOffset = Some(reader.Offset());
        }
        if (mMediaOffset.isNothing() && AtomType(type) == mediaAtom) {
          mMediaOffset = Some(reader.Offset());
        }
        if (mDataOffset.isNothing() && AtomType(type) == dataAtom) {
          mDataOffset = Some(reader.Offset());
        }
        if (size == 1) {
          // 64 bits size.
          size = MOZ_TRY(reader.ReadU64());
        } else if (size == 0) {
          // Atom extends to the end of the buffer, it can't have what we're
          // looking for.
          break;
        }
        if (reader.Remaining() < size - 8) {
          // Incomplete atom.
          break;
        }
        reader.Read(size - 8);

        if (aStop == StopAt::eInitSegment && (mInitOffset || mMediaOffset)) {
          // When we're looking for an init segment, if we encountered a media
          // segment, it we will need to be processed first. So we can stop
          // right away if we have found a media segment.
          break;
        }
        if (aStop == StopAt::eMediaSegment &&
            (mInitOffset || (mMediaOffset && mDataOffset))) {
          // When we're looking for a media segment, if we encountered an init
          // segment, it we will need to be processed first. So we can stop
          // right away if we have found an init segment.
          break;
        }
      }

      return Ok();
    }

    bool StartWithInitSegment() const {
      return mInitOffset.isSome() && (mMediaOffset.isNothing() ||
                                      mInitOffset.ref() < mMediaOffset.ref());
    }
    bool StartWithMediaSegment() const {
      return mMediaOffset.isSome() && (mInitOffset.isNothing() ||
                                       mMediaOffset.ref() < mInitOffset.ref());
    }
    bool IsValid() const { return mValid; }
    const char* LastInvalidBox() const { return mLastInvalidBox; }

   private:
    Maybe<size_t> mInitOffset;
    Maybe<size_t> mMediaOffset;
    Maybe<size_t> mDataOffset;
    bool mValid;
    char mLastInvalidBox[5]{};
  };

 public:
  MediaResult ParseStartAndEndTimestamps(const MediaSpan& aData,
                                         media::TimeUnit& aStart,
                                         media::TimeUnit& aEnd) override {
    bool initSegment = NS_SUCCEEDED(IsInitSegmentPresent(aData));
    if (initSegment) {
      mResource = new SourceBufferResource();
      DDLINKCHILD("resource", mResource.get());
      mStream = new MP4Stream(mResource);
      // We use a timestampOffset of 0 for ContainerParser, and require
      // consumers of ParseStartAndEndTimestamps to add their timestamp offset
      // manually. This allows the ContainerParser to be shared across different
      // timestampOffsets.
      mParser =
          std::make_unique<MoofParser>(mStream, AsVariant(ParseAllTracks{}),
                                       /* aIsAudio = */ false);
      DDLINKCHILD("parser", mParser.get());
      mInitData = new MediaByteBuffer();
      mCompleteInitSegmentRange = MediaByteRange();
      mCompleteMediaHeaderRange = MediaByteRange();
      mCompleteMediaSegmentRange = MediaByteRange();
      mGlobalOffset = mTotalParsed;
    } else if (!mStream || !mParser) {
      mTotalParsed += aData.Length();
      return NS_ERROR_NOT_AVAILABLE;
    }

    MOZ_DIAGNOSTIC_ASSERT(mResource && mParser && mInitData,
                          "Should have received an init segment first");

    mResource->AppendData(aData);
    MediaByteRangeSet byteRanges;
    byteRanges += MediaByteRange(int64_t(mParser->mOffset),
                                 mResource->GetCachedDataEnd(mParser->mOffset));
    mParser->RebuildFragmentedIndex(byteRanges);

    if (initSegment || !HasCompleteInitData()) {
      MediaByteRange& range = mParser->mInitRange;
      if (range.Length()) {
        mCompleteInitSegmentRange = range + mGlobalOffset;
        if (!mInitData->SetLength(range.Length(), fallible)) {
          // Super unlikely OOM
          return NS_ERROR_OUT_OF_MEMORY;
        }
        char* buffer = reinterpret_cast<char*>(mInitData->Elements());
        mResource->ReadFromCache(buffer, range.mStart, range.Length());
        MSE_DEBUG("Stashed init of {} bytes.", range.Length());
      } else {
        MSE_DEBUG("Incomplete init found.");
      }
      mHasInitData = true;
    }
    mTotalParsed += aData.Length();

    MP4Interval<media::TimeUnit> compositionRange =
        mParser->GetCompositionRange(byteRanges);

    mCompleteMediaHeaderRange =
        mParser->FirstCompleteMediaHeader() + mGlobalOffset;
    mCompleteMediaSegmentRange =
        mParser->FirstCompleteMediaSegment() + mGlobalOffset;

    if (HasCompleteInitData()) {
      mResource->EvictData(mParser->mOffset, mParser->mOffset);
    }

    if (compositionRange.IsNull()) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    aStart = compositionRange.start;
    aEnd = compositionRange.end;
    MSE_DEBUG("[{}, {}]", aStart.ToMicroseconds(), aEnd.ToMicroseconds());
    return NS_OK;
  }

  // Gaps of up to 35ms (marginally longer than a single frame at 30fps) are
  // considered to be sequential frames.
  int64_t GetRoundingError() override { return 35000; }

 private:
  RefPtr<MP4Stream> mStream;
  UniquePtr<MoofParser> mParser;
};
DDLoggedTypeDeclNameAndBase(ADTSContainerParser, ContainerParser);

class ADTSContainerParser
    : public ContainerParser,
      public DecoderDoctorLifeLogger<ADTSContainerParser> {
 public:
  explicit ADTSContainerParser(const MediaContainerType& aType)
      : ContainerParser(aType) {}

  typedef struct {
    size_t header_length;  // Length of just the initialization data.
    size_t frame_length;   // Includes header_length;
    uint8_t aac_frames;    // Number of AAC frames in the ADTS frame.
    bool have_crc;
  } Header;

  /// Helper to parse the ADTS header, returning data we care about.
  /// Returns true if the header is parsed successfully.
  /// Returns false if the header is invalid or incomplete,
  /// without modifying the passed-in Header object.
  bool Parse(const MediaSpan& aData, Header& header) {
    // ADTS initialization segments are just the packet header.
    if (aData.Length() < 7) {
      MSE_DEBUG("buffer too short for header.");
      return false;
    }
    // Check 0xfffx sync word plus layer 0.
    if ((aData[0] != 0xff) || ((aData[1] & 0xf6) != 0xf0)) {
      MSE_DEBUG("no syncword.");
      return false;
    }
    bool have_crc = !(aData[1] & 0x01);
    if (have_crc && aData.Length() < 9) {
      MSE_DEBUG("buffer too short for header with crc.");
      return false;
    }
    uint8_t frequency_index = (aData[2] & 0x3c) >> 2;
    MOZ_ASSERT(frequency_index < 16);
    if (frequency_index == 15) {
      MSE_DEBUG("explicit frequency disallowed.");
      return false;
    }
    size_t header_length = have_crc ? 9 : 7;
    size_t data_length = ((aData[3] & 0x03) << 11) | ((aData[4] & 0xff) << 3) |
                         ((aData[5] & 0xe0) >> 5);
    uint8_t frames = (aData[6] & 0x03) + 1;
    MOZ_ASSERT(frames > 0);
    MOZ_ASSERT(frames < 4);

    // Return successfully parsed data.
    header.header_length = header_length;
    header.frame_length = header_length + data_length;
    header.aac_frames = frames;
    header.have_crc = have_crc;
    return true;
  }

  MediaResult IsInitSegmentPresent(const MediaSpan& aData) override {
    // Call superclass for logging.
    ContainerParser::IsInitSegmentPresent(aData);

    Header header;
    if (!Parse(aData, header)) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    MSE_DEBUGV("{} byte frame {} aac frames{}",
               static_cast<unsigned long long>(header.frame_length),
               static_cast<int>(header.aac_frames),
               header.have_crc ? " crc" : "");

    return NS_OK;
  }

  MediaResult IsMediaSegmentPresent(const MediaSpan& aData) override {
    // Call superclass for logging.
    ContainerParser::IsMediaSegmentPresent(aData);

    // Make sure we have a header so we know how long the frame is.
    // NB this assumes the media segment buffer starts with an
    // initialization segment. Since every frame has an ADTS header
    // this is a normal place to divide packets, but we can re-parse
    // mInitData if we need to handle separate media segments.
    Header header;
    if (!Parse(aData, header)) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    // We're supposed to return true as long as aData contains the
    // start of a media segment, whether or not it's complete. So
    // return true if we have any data beyond the header.
    if (aData.Length() <= header.header_length) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    // We should have at least a partial frame.
    return NS_OK;
  }

  MediaResult ParseStartAndEndTimestamps(const MediaSpan& aData,
                                         media::TimeUnit& aStart,
                                         media::TimeUnit& aEnd) override {
    // ADTS header.
    Header header;
    if (!Parse(aData, header)) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    mHasInitData = true;
    mCompleteInitSegmentRange =
        MediaByteRange(0, int64_t(header.header_length));

    // Cache raw header in case the caller wants a copy.
    mInitData = new MediaByteBuffer(header.header_length);
    mInitData->AppendElements(aData.Elements(), header.header_length);

    // Check that we have enough data for the frame body.
    if (aData.Length() < header.frame_length) {
      MSE_DEBUGV(
          "Not enough data for {} byte frame"
          " in {} byte buffer.",
          static_cast<unsigned long long>(header.frame_length),
          static_cast<unsigned long long>(aData.Length()));
      return NS_ERROR_NOT_AVAILABLE;
    }
    mCompleteMediaSegmentRange =
        MediaByteRange(header.header_length, header.frame_length);
    // The ADTS MediaSource Byte Stream Format document doesn't
    // define media header. Just treat it the same as the whole
    // media segment.
    mCompleteMediaHeaderRange = mCompleteMediaSegmentRange;

    MSE_DEBUG("[{}, {}]", aStart.ToMicroseconds(), aEnd.ToMicroseconds());
    // We don't update timestamps, regardless.
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Audio shouldn't have gaps.
  // Especially when we generate the timestamps ourselves.
  int64_t GetRoundingError() override { return 0; }
};

DDLoggedTypeDeclNameAndBase(MPEGAudioContainerParser, ContainerParser);

// https://w3c.github.io/mse-byte-stream-format-mpeg-audio/
class MPEGAudioContainerParser
    : public ContainerParser,
      public DecoderDoctorLifeLogger<MPEGAudioContainerParser> {
 public:
  explicit MPEGAudioContainerParser(const MediaContainerType& aType)
      : ContainerParser(aType) {}

  // Every MPEG audio frame header is an initialization segment, and the coded
  // audio that follows it is a media segment. The header alone is enough to
  // report the initialization segment, as the byte stream format says.
  // Chromium waits for the whole frame.
  bool HasImplicitInitSegment() const override { return true; }

  // Metadata frames are ignored. Garbage violates the byte stream format, but
  // is ignored too rather than failing the append, as in Chromium. Media
  // segments only hold MPEG audio frames.
  size_t SkipIgnorableBytesAtStart(const MediaSpan& aData) override {
    const Span<const uint8_t> data = AsSpan(aData);
    size_t ignored = 0;
    while (ignored < data.Length()) {
      if (mSkip) {
        const size_t skipped = static_cast<size_t>(
            std::min<uint64_t>(mSkip, data.Length() - ignored));
        ignored += skipped;
        mSkip -= skipped;
        continue;
      }
      const mpeg_audio::Unit unit = mpeg_audio::ParseUnit(
          data.From(ignored), mResyncing, /* aHeaderOnly = */ true);
      switch (unit.mType) {
        case mpeg_audio::Unit::Type::Metadata:
          mSkip = unit.mLength;
          mResyncing = false;
          break;
        case mpeg_audio::Unit::Type::Garbage:
          MSE_DEBUG("ignoring {} bytes of garbage", unit.mLength);
          MOZ_ASSERT(unit.mLength <= data.Length() - ignored);
          ignored += unit.mLength;
          mResyncing = true;
          break;
        case mpeg_audio::Unit::Type::Frame:
        case mpeg_audio::Unit::Type::InfoFrame:
          // Once confirmed, a frame header that followed garbage starts a
          // segment like any other.
          mResyncing = false;
          return ignored;
        case mpeg_audio::Unit::Type::NeedMoreData:
        case mpeg_audio::Unit::Type::Error:
          return ignored;
      }
    }
    return ignored;
  }

  MediaResult IsInitSegmentPresent(const MediaSpan& aData) override {
    ContainerParser::IsInitSegmentPresent(aData);
    mpeg_audio::FrameInfo frame;
    MediaResult rv = ParseFrameHeaderAtStart(AsSpan(aData), frame);
    if (NS_FAILED(rv)) {
      return rv;
    }
    // A header that keeps the current configuration would only add the same
    // track description again, so only report one that changes it. This
    // avoids recreating the demuxer for each frame.
    if (mConfig && mConfig->HasSameConfig(frame)) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    return NS_OK;
  }

  MediaResult IsMediaSegmentPresent(const MediaSpan& aData) override {
    ContainerParser::IsMediaSegmentPresent(aData);
    if (!mConfig) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    mpeg_audio::FrameInfo frame;
    MediaResult rv = ParseFrameHeaderAtStart(AsSpan(aData), frame);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (!mConfig->HasSameConfig(frame)) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    // The media segment is the coded audio of the frame, so it only starts
    // once data follows the header. Chromium starts it with the header,
    // except for the first one.
    return aData.Length() > mpeg_audio::kFrameHeaderSize
               ? NS_OK
               : NS_ERROR_NOT_AVAILABLE;
  }

  // Timestamps are generated, so this never returns any. It parses the frame
  // header at the start of aData when there is no current configuration, and
  // otherwise sets MediaSegmentRange() to end after the last complete frame,
  // or before a unit that isn't a frame with the current configuration.
  MediaResult ParseStartAndEndTimestamps(const MediaSpan& aData,
                                         media::TimeUnit& aStart,
                                         media::TimeUnit& aEnd) override {
    const uint64_t dataOffset = mParsed;
    mParsed += aData.Length();
    if (mSegmentEnded) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    Span<const uint8_t> buffer = AsSpan(aData);
    uint64_t bufferOffset = dataOffset;
    if (!mPending.IsEmpty()) {
      bufferOffset = dataOffset - mPending.Length();
      if (!mPending.AppendElements(aData.Elements(), aData.Length(),
                                   fallible)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      buffer = Span<const uint8_t>(mPending);
    }

    Result<size_t, MediaResult> consumed =
        mConfig ? ParseMediaFrames(buffer, bufferOffset)
                : ParseInitSegment(buffer, bufferOffset);
    if (consumed.isErr()) {
      return consumed.unwrapErr();
    }

    // Keep the bytes of an incomplete frame until more data arrives.
    const size_t remaining = buffer.Length() - consumed.inspect();
    if (mSegmentEnded || !remaining) {
      mPending.Clear();
    } else if (buffer.Elements() == mPending.Elements()) {
      mPending.RemoveElementsAt(0, consumed.inspect());
    } else if (!mPending.AppendElements(buffer.Elements() + consumed.inspect(),
                                        remaining, fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (mSegmentEnd > mSegmentStart) {
      mCompleteMediaSegmentRange =
          MediaByteRange(AssertedCast<int64_t>(mSegmentStart),
                         AssertedCast<int64_t>(mSegmentEnd));
      mCompleteMediaHeaderRange = mCompleteMediaSegmentRange;
    }
    MSE_DEBUGV("media segment [{}, {}) pending={}", mSegmentStart, mSegmentEnd,
               mPending.Length());
    return NS_ERROR_NOT_AVAILABLE;
  }

  int64_t GetRoundingError() override { return 0; }

 private:
  static Span<const uint8_t> AsSpan(const MediaSpan& aData) {
    return Span(aData.Elements(), aData.Length());
  }

  static MediaResult InvalidData() {
    return MediaResult(NS_ERROR_FAILURE, "Invalid MPEG audio byte stream");
  }

  // Parses the frame header at the start of aData, as
  // SkipIgnorableBytesAtStart() removes any bytes before it.
  MediaResult ParseFrameHeaderAtStart(Span<const uint8_t> aData,
                                      mpeg_audio::FrameInfo& aFrame) const {
    const mpeg_audio::Unit unit =
        mpeg_audio::ParseUnit(aData, mResyncing, /* aHeaderOnly = */ true);
    switch (unit.mType) {
      case mpeg_audio::Unit::Type::NeedMoreData:
        return NS_ERROR_NOT_AVAILABLE;
      case mpeg_audio::Unit::Type::Error:
        return InvalidData();
      case mpeg_audio::Unit::Type::Frame:
      case mpeg_audio::Unit::Type::InfoFrame:
        aFrame = unit.mFrame;
        return NS_OK;
      case mpeg_audio::Unit::Type::Metadata:
      case mpeg_audio::Unit::Type::Garbage:
        break;
    }
    MOZ_ASSERT_UNREACHABLE("Bytes to ignore must be removed first");
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Returns the number of bytes consumed. The parser is recreated once the
  // initialization segment has been handled, so all of aData is consumed once
  // the frame header is found.
  Result<size_t, MediaResult> ParseInitSegment(Span<const uint8_t> aData,
                                               uint64_t aDataOffset) {
    mpeg_audio::FrameInfo frame;
    MediaResult rv = ParseFrameHeaderAtStart(aData, frame);
    if (rv == NS_ERROR_NOT_AVAILABLE) {
      return 0;
    }
    if (NS_FAILED(rv)) {
      return Err(rv);
    }
    mConfig = Some(frame);
    mInitData = new MediaByteBuffer();
    mInitData->AppendElements(aData.Elements(), mpeg_audio::kFrameHeaderSize);
    mHasInitData = true;
    mSegmentStart = mSegmentEnd = aDataOffset + aData.Length();
    MSE_DEBUG("init segment at {}: layer={} rate={} channels={}", aDataOffset,
              frame.mLayer, frame.mSampleRate, frame.mChannels);
    return aData.Length();
  }

  Result<size_t, MediaResult> ParseMediaFrames(Span<const uint8_t> aData,
                                               uint64_t aDataOffset) {
    size_t consumed = 0;
    while (consumed < aData.Length()) {
      const mpeg_audio::Unit unit = mpeg_audio::ParseUnit(
          aData.From(consumed), /* aConfirmSync = */ false);
      switch (unit.mType) {
        case mpeg_audio::Unit::Type::NeedMoreData:
          return consumed;
        case mpeg_audio::Unit::Type::Error:
          return Err(InvalidData());
        case mpeg_audio::Unit::Type::Metadata:
        case mpeg_audio::Unit::Type::Garbage:
          // These bytes are removed while waiting for the next segment.
          EndSegment();
          return consumed;
        case mpeg_audio::Unit::Type::Frame:
        case mpeg_audio::Unit::Type::InfoFrame:
          if (!mConfig->HasSameConfig(unit.mFrame)) {
            // This frame header is a new initialization segment.
            EndSegment();
            return consumed;
          }
          MOZ_ASSERT(unit.mLength <= aData.Length() - consumed);
          consumed += unit.mLength;
          mSegmentEnd = aDataOffset + consumed;
          break;
      }
    }
    return consumed;
  }

  void EndSegment() {
    // IsMediaSegmentPresent() only starts a media segment at a frame.
    MOZ_ASSERT(mSegmentEnd > mSegmentStart);
    mSegmentEnded = true;
  }

  // The configuration of the current initialization segment.
  Maybe<mpeg_audio::FrameInfo> mConfig;
  // Total bytes passed to ParseStartAndEndTimestamps.
  uint64_t mParsed = 0;
  // The incomplete frame that ends the data parsed so far.
  nsTArray<uint8_t> mPending;
  // Bytes left to ignore in a metadata frame.
  uint64_t mSkip = 0;
  // Whether the next frame header must be confirmed by those that follow it,
  // after garbage.
  bool mResyncing = false;
  uint64_t mSegmentStart = 0;
  uint64_t mSegmentEnd = 0;
  // Whether the media segment ended before a unit that it can't hold.
  bool mSegmentEnded = false;
};

/*static*/
UniquePtr<ContainerParser> ContainerParser::CreateForMIMEType(
    const MediaContainerType& aType) {
  if (aType.Type() == MEDIAMIMETYPE(VIDEO_WEBM) ||
      aType.Type() == MEDIAMIMETYPE(AUDIO_WEBM)) {
    return std::make_unique<WebMContainerParser>(aType);
  }

  if (aType.Type() == MEDIAMIMETYPE(VIDEO_MP4) ||
      aType.Type() == MEDIAMIMETYPE(AUDIO_MP4)) {
    return std::make_unique<MP4ContainerParser>(aType);
  }
  if (aType.Type() == MEDIAMIMETYPE("audio/aac")) {
    return std::make_unique<ADTSContainerParser>(aType);
  }
  if (aType.Type() == MEDIAMIMETYPE(AUDIO_MP3)) {
    return std::make_unique<MPEGAudioContainerParser>(aType);
  }

  return std::make_unique<ContainerParser>(aType);
}

#undef MSE_DEBUG
#undef MSE_DEBUGV
#undef MSE_DEBUGVEX

}  // namespace mozilla
