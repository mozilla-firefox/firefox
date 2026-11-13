/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MoofParser.h"

#include <limits>

#include "Box.h"
#include "MP4Interval.h"
#include "MediaDataDemuxer.h"
#include "SinfParser.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/Logging.h"
#include "mozilla/Try.h"

#define LOG_ERROR(name, arg, ...)                                             \
  MOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Error,                     \
              MOZ_STRINGIFY(name) "({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)
#define LOG_WARN(name, arg, ...)                                              \
  MOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Warning,                   \
              MOZ_STRINGIFY(name) "({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)
#define LOG_DEBUG(name, arg, ...)                                             \
  MOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Debug,                     \
              MOZ_STRINGIFY(name) "({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)

namespace mozilla {

using TimeUnit = media::TimeUnit;

const uint32_t kKeyIdSize = 16;

namespace {

Result<MediaByteRange, nsresult> MakeMediaByteRangeChecked(
    const CheckedUint64& aStart, uint64_t aLength) {
  CheckedUint64 end = aStart + aLength;
  if (!aStart.isValid() || !end.isValid() ||
      aStart.value() > uint64_t(INT64_MAX) ||
      end.value() > uint64_t(INT64_MAX)) {
    return Err(NS_ERROR_FAILURE);
  }
  return MediaByteRange(static_cast<int64_t>(aStart.value()),
                        static_cast<int64_t>(end.value()));
}

}  // namespace

bool MoofParser::RebuildFragmentedIndex(const MediaByteRangeSet& aByteRanges) {
  BoxContext context(mSource, aByteRanges);
  return RebuildFragmentedIndex(context);
}

bool MoofParser::RebuildFragmentedIndex(const MediaByteRangeSet& aByteRanges,
                                        bool* aCanEvict) {
  MOZ_ASSERT(aCanEvict);
  if (*aCanEvict && mMoofs.Length() > 1) {
    MOZ_ASSERT(mMoofs.Length() == mMediaRanges.Length());
    mMoofs.RemoveElementsAt(0, mMoofs.Length() - 1);
    mMediaRanges.RemoveElementsAt(0, mMediaRanges.Length() - 1);
    *aCanEvict = true;
  } else {
    *aCanEvict = false;
  }
  return RebuildFragmentedIndex(aByteRanges);
}

bool MoofParser::RebuildFragmentedIndex(BoxContext& aContext) {
  LOG_DEBUG(
      Moof, "Starting, mTrackParseMode={}, track#={} (ignore if multitrack).",
      mTrackParseMode.is<ParseAllTracks>() ? "multitrack" : "single track",
      mTrackParseMode.is<ParseAllTracks>() ? 0
                                           : mTrackParseMode.as<uint32_t>());
  bool foundValidMoof = false;

  for (Box box(&aContext, mOffset); box.IsAvailable();
       mOffset = box.NextOffset(), box = box.Next()) {
    if (box.IsType("moov") && mInitRange.IsEmpty()) {
      mInitRange = MediaByteRange(0, box.Range().mEnd);
      ParseMoov(box);
    } else if (box.IsType("moof")) {
      Moof moof(box, mTrackParseMode, mTrex, mMvhd, mMdhd, mEdts, mSinf,
                mIsAudio, &mLastDecodeTime, mTracksEndCts);

      if (!moof.IsValid()) {
        continue;  // Skip to next box.
      }

      if (!mMoofs.IsEmpty()) {
        // Stitch time ranges together in the case of a (hopefully small) time
        // range gap between moofs.
        mMoofs.LastElement().FixRounding(moof);
      }

      mMediaRanges.AppendElement(moof.mRange);
      mMoofs.AppendElement(std::move(moof));
      foundValidMoof = true;
    } else if (box.IsType("mdat") && !Moofs().IsEmpty()) {
      // Check if we have all our data from last moof.
      Moof& moof = Moofs().LastElement();
      media::Interval<int64_t> datarange(moof.mMdatRange.mStart,
                                         moof.mMdatRange.mEnd, 0);
      media::Interval<int64_t> mdat(box.Range().mStart, box.Range().mEnd, 0);
      if (datarange.Intersects(mdat)) {
        mMediaRanges.LastElement() =
            mMediaRanges.LastElement().Span(box.Range());
      }
    }
  }
  MOZ_ASSERT(mTrackParseMode.is<ParseAllTracks>() ||
                 mTrex.mTrackId == mTrackParseMode.as<uint32_t>(),
             "If not parsing all tracks, mTrex should have the same track id "
             "as the track being parsed.");
  LOG_DEBUG(Moof, "Done, foundValidMoof={}.",
            foundValidMoof ? "true" : "false");
  return foundValidMoof;
}

MediaByteRange MoofParser::FirstCompleteMediaHeader() {
  if (Moofs().IsEmpty()) {
    return MediaByteRange();
  }
  return Moofs()[0].mRange;
}

MediaByteRange MoofParser::FirstCompleteMediaSegment() {
  for (uint32_t i = 0; i < mMediaRanges.Length(); i++) {
    if (mMediaRanges[i].Contains(Moofs()[i].mMdatRange)) {
      return mMediaRanges[i];
    }
  }
  return MediaByteRange();
}

const CencSampleEncryptionInfoEntry* MoofParser::GetSampleEncryptionEntry(
    size_t aMoof, size_t aSample) const {
  if (aMoof >= mMoofs.Length()) {
    return nullptr;
  }
  return mMoofs[aMoof].GetSampleEncryptionEntry(
      aSample, &mTrackSampleToGroupEntries, &mTrackSampleEncryptionInfoEntries);
}

DDLoggedTypeDeclNameAndBase(BlockingStream, ByteStream);

class BlockingStream : public ByteStream,
                       public DecoderDoctorLifeLogger<BlockingStream> {
 public:
  explicit BlockingStream(ByteStream* aStream) : mStream(aStream) {
    DDLINKCHILD("stream", aStream);
  }

  nsresult ReadAt(int64_t offset, void* data, size_t size,
                  size_t* bytes_read) override {
    return mStream->ReadAt(offset, data, size, bytes_read);
  }

  nsresult CachedReadAt(int64_t offset, void* data, size_t size,
                        size_t* bytes_read) override {
    return mStream->ReadAt(offset, data, size, bytes_read);
  }

  virtual bool Length(int64_t* size) override { return mStream->Length(size); }

 private:
  RefPtr<ByteStream> mStream;
};

nsresult MoofParser::BlockingReadNextMoof() {
  LOG_DEBUG(Moof, "Starting.");
  int64_t length = std::numeric_limits<int64_t>::max();
  mSource->Length(&length);
  RefPtr<BlockingStream> stream = new BlockingStream(mSource);
  MediaByteRangeSet byteRanges(MediaByteRange(0, length));

  BoxContext context(stream, byteRanges);
  Box box(&context, mOffset);
  for (; box.IsAvailable(); box = box.Next()) {
    if (box.IsType("moof")) {
      MediaByteRangeSet parseByteRanges(
          MediaByteRange(mOffset, box.Range().mEnd));
      BoxContext parseContext(stream, parseByteRanges);
      if (RebuildFragmentedIndex(parseContext)) {
        LOG_DEBUG(Moof, "Succeeded on RebuildFragmentedIndex, returning NS_OK");
        return NS_OK;
      }
    }
  }
  nsresult rv = box.Offset() == length ? NS_ERROR_DOM_MEDIA_END_OF_STREAM
                                       : box.InitStatus();
  LOG_DEBUG(Moof, "Couldn't read next moof, returning {}",
            GetStaticErrorName(rv));
  return rv;
}

void MoofParser::ScanForMetadata(mozilla::MediaByteRange& aMoov) {
  LOG_DEBUG(Moof, "Starting.");
  int64_t length = std::numeric_limits<int64_t>::max();
  mSource->Length(&length);
  MediaByteRangeSet byteRanges;
  byteRanges += MediaByteRange(0, length);
  RefPtr<BlockingStream> stream = new BlockingStream(mSource);

  BoxContext context(stream, byteRanges);
  for (Box box(&context, mOffset); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("moov")) {
      aMoov = box.Range();
      break;
    }
  }
  mInitRange = aMoov;
  LOG_DEBUG(Moof, "Done, mInitRange.mStart={}, mInitRange.mEnd={}",
            mInitRange.mStart, mInitRange.mEnd);
}

already_AddRefed<mozilla::MediaByteBuffer> MoofParser::Metadata() {
  LOG_DEBUG(Moof, "Starting.");
  MediaByteRange moov;
  ScanForMetadata(moov);
  CheckedInt<MediaByteBuffer::size_type> moovLength = moov.Length();
  if (!moovLength.isValid() || !moovLength.value()) {
    // No moov, or cannot be used as array size.
    LOG_WARN(Moof,
             "Did not get usable moov length while trying to parse Metadata.");
    return nullptr;
  }

  RefPtr<MediaByteBuffer> metadata = new MediaByteBuffer();
  if (!metadata->SetLength(moovLength.value(), fallible)) {
    LOG_ERROR(Moof, "OOM");
    return nullptr;
  }

  RefPtr<BlockingStream> stream = new BlockingStream(mSource);
  size_t read;
  nsresult rv = stream->ReadAt(moov.mStart, metadata->Elements(),
                               moovLength.value(), &read);
  if (NS_FAILED(rv) || read != moovLength.value()) {
    LOG_WARN(Moof, "Failed to read moov while trying to parse Metadata.");
    return nullptr;
  }
  LOG_DEBUG(Moof, "Done, found metadata.");
  return metadata.forget();
}

MP4Interval<TimeUnit> MoofParser::GetCompositionRange(
    const MediaByteRangeSet& aByteRanges) {
  LOG_DEBUG(Moof, "Starting.");
  MP4Interval<TimeUnit> compositionRange;
  BoxContext context(mSource, aByteRanges);
  for (size_t i = 0; i < mMoofs.Length(); i++) {
    Moof& moof = mMoofs[i];
    Box box(&context, moof.mRange.mStart);
    if (box.IsAvailable()) {
      compositionRange = compositionRange.Extents(moof.mTimeRange);
    }
  }
  LOG_DEBUG(Moof, "Done, compositionRange.start={}, compositionRange.end={}.",
            compositionRange.start.ToMicroseconds(),
            compositionRange.end.ToMicroseconds());
  return compositionRange;
}

bool MoofParser::ReachedEnd() {
  int64_t length;
  return mSource->Length(&length) && mOffset == length;
}

void MoofParser::ParseMoov(const Box& aBox) {
  LOG_DEBUG(Moof, "Starting.");
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("mvhd")) {
      mMvhd = Mvhd(box);
    } else if (box.IsType("trak")) {
      ParseTrak(box);
    } else if (box.IsType("mvex")) {
      ParseMvex(box);
    }
  }
  LOG_DEBUG(Moof, "Done.");
}

void MoofParser::ParseTrak(const Box& aBox) {
  LOG_DEBUG(Trak, "Starting.");
  Tkhd tkhd;
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("tkhd")) {
      tkhd = Tkhd(box);
    } else if (box.IsType("mdia")) {
      if (mTrackParseMode.is<ParseAllTracks>() ||
          tkhd.mTrackId == mTrackParseMode.as<uint32_t>()) {
        ParseMdia(box);
      }
    } else if (box.IsType("edts") &&
               (mTrackParseMode.is<ParseAllTracks>() ||
                tkhd.mTrackId == mTrackParseMode.as<uint32_t>())) {
      mEdts = Edts(box);
    }
  }
  LOG_DEBUG(Trak, "Done.");
}

void MoofParser::ParseMdia(const Box& aBox) {
  LOG_DEBUG(Mdia, "Starting.");
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("mdhd")) {
      mMdhd = Mdhd(box);
    } else if (box.IsType("minf")) {
      ParseMinf(box);
    }
  }
  LOG_DEBUG(Mdia, "Done.");
}

void MoofParser::ParseMvex(const Box& aBox) {
  LOG_DEBUG(Mvex, "Starting.");
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("trex")) {
      Trex trex = Trex(box);
      if (mTrackParseMode.is<ParseAllTracks>() ||
          trex.mTrackId == mTrackParseMode.as<uint32_t>()) {
        mTrex = trex;
      }
    }
  }
  LOG_DEBUG(Mvex, "Done.");
}

void MoofParser::ParseMinf(const Box& aBox) {
  LOG_DEBUG(Minf, "Starting.");
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("stbl")) {
      ParseStbl(box);
    }
  }
  LOG_DEBUG(Minf, "Done.");
}

void MoofParser::ParseStbl(const Box& aBox) {
  LOG_DEBUG(Stbl, "Starting.");
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("stsd")) {
      ParseStsd(box);
    } else if (box.IsType("sgpd")) {
      Sgpd sgpd(box);
      if (sgpd.IsValid() && sgpd.mGroupingType == "seig") {
        mTrackSampleEncryptionInfoEntries.Clear();
        if (!mTrackSampleEncryptionInfoEntries.AppendElements(
                sgpd.mEntries, mozilla::fallible)) {
          LOG_ERROR(Stbl, "OOM");
          return;
        }
      }
    } else if (box.IsType("sbgp")) {
      Sbgp sbgp(box);
      if (sbgp.IsValid() && sbgp.mGroupingType == "seig") {
        mTrackSampleToGroupEntries.Clear();
        if (!mTrackSampleToGroupEntries.AppendElements(sbgp.mEntries,
                                                       mozilla::fallible)) {
          LOG_ERROR(Stbl, "OOM");
          return;
        }
      }
    }
  }
  LOG_DEBUG(Stbl, "Done.");
}

void MoofParser::ParseStsd(const Box& aBox) {
  LOG_DEBUG(Stsd, "Starting.");
  if (mTrackParseMode.is<ParseAllTracks>()) {
    // It is not a sane operation to try and map sample description boxes from
    // multiple tracks onto the parser, which is modeled around storing metadata
    // for a single track.
    LOG_DEBUG(Stsd, "Early return due to multitrack parser.");
    return;
  }
  MOZ_ASSERT(
      mSampleDescriptions.IsEmpty(),
      "Shouldn't have any sample descriptions yet when starting to parse stsd");
  uint32_t numberEncryptedEntries = 0;
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    SampleDescriptionEntry sampleDescriptionEntry{false};
    if (box.IsType("encv") || box.IsType("enca")) {
      ParseEncrypted(box);
      sampleDescriptionEntry.mIsEncryptedEntry = true;
      numberEncryptedEntries++;
    }
    if (!mSampleDescriptions.AppendElement(sampleDescriptionEntry,
                                           mozilla::fallible)) {
      LOG_ERROR(Stsd, "OOM");
      return;
    }
  }
  if (mSampleDescriptions.IsEmpty()) {
    LOG_WARN(Stsd,
             "No sample description entries found while parsing Stsd! This "
             "shouldn't happen, as the spec requires one for each track!");
  }
  if (numberEncryptedEntries > 1) {
    LOG_WARN(Stsd,
             "More than one encrypted sample description entry found while "
             "parsing track! We don't expect this, and it will likely break "
             "during fragment look up!");
  }
  LOG_DEBUG(Stsd,
            "Done, numberEncryptedEntries={}, mSampleDescriptions.Length={}",
            numberEncryptedEntries, mSampleDescriptions.Length());
}

void MoofParser::ParseEncrypted(const Box& aBox) {
  LOG_DEBUG(Moof, "Starting.");
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    // Some MP4 files have been found to have multiple sinf boxes in the same
    // enc* box. This does not match spec anyway, so just choose the first
    // one that parses properly.
    if (box.IsType("sinf")) {
      mSinf = Sinf(box);

      if (mSinf.IsValid()) {
        break;
      }
    }
  }
  LOG_DEBUG(Moof, "Done.");
}

class CtsComparator {
 public:
  bool Equals(Sample* const aA, Sample* const aB) const {
    return aA->mCompositionRange.start == aB->mCompositionRange.start;
  }
  bool LessThan(Sample* const aA, Sample* const aB) const {
    return aA->mCompositionRange.start < aB->mCompositionRange.start;
  }
};

Moof::Moof(const Box& aBox, const TrackParseMode& aTrackParseMode,
           const Trex& aTrex, const Mvhd& aMvhd, const Mdhd& aMdhd,
           const Edts& aEdts, const Sinf& aSinf, const bool aIsAudio,
           uint64_t* aDecodeTime, nsTArray<TrackEndCts>& aTracksEndCts)
    : mRange(aBox.Range()),
      mTfhd(aTrex),
      // Do not reporting discontuities less than 35ms
      mMaxRoundingError(TimeUnit::FromSeconds(0.035)) {
  LOG_DEBUG(
      Moof, "Starting, aTrackParseMode={}, track#={} (ignore if multitrack).",
      aTrackParseMode.is<ParseAllTracks>() ? "multitrack" : "single track",
      aTrackParseMode.is<ParseAllTracks>() ? 0
                                           : aTrackParseMode.as<uint32_t>());
  MOZ_ASSERT(aTrackParseMode.is<ParseAllTracks>() ||
                 aTrex.mTrackId == aTrackParseMode.as<uint32_t>(),
             "If not parsing all tracks, aTrex should have the same track id "
             "as the track being parsed.");
  nsTArray<Box> psshBoxes;
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("traf")) {
      ParseTraf(box, aTrackParseMode, aTrex, aMvhd, aMdhd, aEdts, aSinf,
                aIsAudio, aDecodeTime);
    }
    if (box.IsType("pssh")) {
      psshBoxes.AppendElement(box);
    }
  }

  // The EME spec requires that PSSH boxes which are contiguous in the
  // file are dispatched to the media element in a single "encrypted" event.
  // So append contiguous boxes here.
  for (size_t i = 0; i < psshBoxes.Length(); ++i) {
    const Box& box = psshBoxes[i];
    if (i == 0 || box.Offset() != psshBoxes[i - 1].NextOffset()) {
      mPsshes.AppendElement();
    }
    nsTArray<uint8_t>& pssh = mPsshes.LastElement();
    pssh.AppendElements(std::move(box.ReadCompleteBox()));
  }

  if (IsValid()) {
    if (mIndex.Length()) {
      // Ensure the samples are contiguous with no gaps.
      nsTArray<Sample*> ctsOrder;
      for (auto& sample : mIndex) {
        ctsOrder.AppendElement(&sample);
      }
      ctsOrder.Sort(CtsComparator());

      for (size_t i = 1; i < ctsOrder.Length(); i++) {
        ctsOrder[i - 1]->mCompositionRange.end =
            ctsOrder[i]->mCompositionRange.start;
      }

      // Ensure that there are no gaps between the first sample in this
      // Moof and the preceeding Moof.
      if (!ctsOrder.IsEmpty()) {
        bool found = false;
        // Track ID of the track we're parsing.
        const uint32_t trackId = aTrex.mTrackId;
        // Find the previous CTS end time of Moof preceeding the Moofs we just
        // parsed, for the track we're parsing.
        for (auto& prevCts : aTracksEndCts) {
          if (prevCts.mTrackId == trackId) {
            // We ensure there are no gaps in samples' CTS between the last
            // sample in a Moof, and the first sample in the next Moof, if
            // they're within these many Microseconds of each other.
            const TimeUnit CROSS_MOOF_CTS_MERGE_THRESHOLD =
                TimeUnit::FromMicroseconds(1);
            // We have previously parsed a Moof for this track. Smooth the gap
            // between samples for this track across the Moof bounary.
            if (ctsOrder[0]->mCompositionRange.start > prevCts.mCtsEndTime &&
                ctsOrder[0]->mCompositionRange.start - prevCts.mCtsEndTime <=
                    CROSS_MOOF_CTS_MERGE_THRESHOLD) {
              ctsOrder[0]->mCompositionRange.start = prevCts.mCtsEndTime;
            }
            prevCts.mCtsEndTime = ctsOrder.LastElement()->mCompositionRange.end;
            found = true;
            break;
          }
        }
        if (!found) {
          // We've not parsed a Moof for this track yet. Save its CTS end
          // time for the next Moof we parse.
          aTracksEndCts.AppendElement(TrackEndCts(
              trackId, ctsOrder.LastElement()->mCompositionRange.end));
        }
      }

      // In MP4, the duration of a sample is defined as the delta between two
      // decode timestamps. The operation above has updated the duration of each
      // sample as a Sample's duration is mCompositionRange.end -
      // mCompositionRange.start MSE's TrackBuffersManager expects dts that
      // increased by the sample's duration, so we rewrite the dts accordingly.
      TimeUnit presentationDuration =
          ctsOrder.LastElement()->mCompositionRange.end -
          ctsOrder[0]->mCompositionRange.start;
      auto decodeOffset =
          aMdhd.ToTimeUnit(CheckedInt64(*aDecodeTime) - aEdts.mMediaStart);
      auto offsetOffset = aMvhd.ToTimeUnit(aEdts.mEmptyOffset);
      TimeUnit endDecodeTime =
          (decodeOffset.isOk() && offsetOffset.isOk())
              ? decodeOffset.unwrap() + offsetOffset.unwrap()
              : TimeUnit::Zero(aMvhd.mTimescale);
      TimeUnit decodeDuration = endDecodeTime - mIndex[0].mDecodeTime;
      double adjust = 0.;
      if (!presentationDuration.IsZero()) {
        double num = decodeDuration.ToSeconds();
        double denom = presentationDuration.ToSeconds();
        if (denom != 0.) {
          adjust = num / denom;
        }
      }

      TimeUnit dtsOffset = mIndex[0].mDecodeTime;
      TimeUnit compositionDuration(0, aMvhd.mTimescale);
      // Adjust the dts, ensuring that the new adjusted dts will never be
      // greater than decodeTime (the next moof's decode start time).
      for (auto& sample : mIndex) {
        sample.mDecodeTime = dtsOffset + compositionDuration.MultDouble(adjust);
        compositionDuration += sample.mCompositionRange.Length();
      }
      mTimeRange =
          MP4Interval<TimeUnit>(ctsOrder[0]->mCompositionRange.start,
                                ctsOrder.LastElement()->mCompositionRange.end);
    }
    // No need to retrieve auxiliary encryption data if we have a senc box: we
    // won't use it in SampleIterator::GetNext()
    if (!mSencValid) {
      ProcessCencAuxInfo(aSinf.mDefaultEncryptionType);
    }
  }
  LOG_DEBUG(Moof, "Done.");
}

bool Moof::GetAuxInfo(AtomType aType,
                      FallibleTArray<MediaByteRange>* aByteRanges) {
  LOG_DEBUG(Moof, "Starting.");
  aByteRanges->Clear();

  Saiz* saiz = nullptr;
  for (int i = 0;; i++) {
    if (i == mSaizs.Length()) {
      LOG_DEBUG(Moof, "Could not find saiz matching aType. Returning false.");
      return false;
    }
    if (mSaizs[i].mAuxInfoType == aType) {
      saiz = &mSaizs[i];
      break;
    }
  }
  Saio* saio = nullptr;
  for (int i = 0;; i++) {
    if (i == mSaios.Length()) {
      LOG_DEBUG(Moof, "Could not find saio matching aType. Returning false.");
      return false;
    }
    if (mSaios[i].mAuxInfoType == aType) {
      saio = &mSaios[i];
      break;
    }
  }

  if (saio->mOffsets.Length() == 1) {
    if (!aByteRanges->SetCapacity(saiz->mSampleInfoSize.Length(),
                                  mozilla::fallible)) {
      LOG_ERROR(Moof, "OOM");
      return false;
    }
    CheckedUint64 offset =
        CheckedUint64(mTfhd.mBaseDataOffset) + CheckedUint64(saio->mOffsets[0]);
    for (size_t i = 0; i < saiz->mSampleInfoSize.Length(); i++) {
      auto range = MakeMediaByteRangeChecked(offset, saiz->mSampleInfoSize[i]);
      if (range.isErr()) {
        LOG_WARN(Moof, "Invalid aux info offset");
        return false;
      }
      if (!aByteRanges->AppendElement(range.unwrap(), mozilla::fallible)) {
        LOG_ERROR(Moof, "OOM");
        return false;
      }
      offset += saiz->mSampleInfoSize[i];
    }
    LOG_DEBUG(
        Moof,
        "Saio has 1 entry. aByteRanges populated accordingly. Returning true.");
    return true;
  }

  if (saio->mOffsets.Length() == saiz->mSampleInfoSize.Length()) {
    if (!aByteRanges->SetCapacity(saiz->mSampleInfoSize.Length(),
                                  mozilla::fallible)) {
      LOG_ERROR(Moof, "OOM");
      return false;
    }
    for (size_t i = 0; i < saio->mOffsets.Length(); i++) {
      CheckedUint64 offset =
          CheckedUint64(mRange.mStart) + CheckedUint64(saio->mOffsets[i]);
      auto range = MakeMediaByteRangeChecked(offset, saiz->mSampleInfoSize[i]);
      if (range.isErr()) {
        LOG_WARN(Moof, "Invalid aux info offset");
        return false;
      }
      if (!aByteRanges->AppendElement(range.unwrap(), mozilla::fallible)) {
        LOG_ERROR(Moof, "OOM");
        return false;
      }
    }
    LOG_DEBUG(
        Moof,
        "Saio and saiz have same number of entries. aByteRanges populated "
        "accordingly. Returning true.");
    return true;
  }

  LOG_DEBUG(Moof,
            "Moof::GetAuxInfo could not find any Aux info, returning false.");
  return false;
}

bool Moof::ProcessCencAuxInfo(AtomType aScheme) {
  LOG_DEBUG(Moof, "Starting.");
  FallibleTArray<MediaByteRange> cencRanges;
  if (!GetAuxInfo(aScheme, &cencRanges) ||
      cencRanges.Length() != mIndex.Length()) {
    LOG_DEBUG(Moof, "Couldn't find cenc aux info.");
    return false;
  }
  for (int i = 0; i < cencRanges.Length(); i++) {
    mIndex[i].mCencRange = cencRanges[i];
  }
  LOG_DEBUG(Moof, "Found cenc aux info and stored on index.");
  return true;
}

const CencSampleEncryptionInfoEntry* Moof::GetSampleEncryptionEntry(
    size_t aSample,
    const FallibleTArray<SampleToGroupEntry>* aTrackSampleToGroupEntries,
    const FallibleTArray<CencSampleEncryptionInfoEntry>*
        aTrackSampleEncryptionInfoEntries) const {
  const SampleToGroupEntry* sampleToGroupEntry = nullptr;

  // Default to using the sample to group entries for the fragment, otherwise
  // fall back to the sample to group entries for the track.
  const FallibleTArray<SampleToGroupEntry>* sampleToGroupEntries =
      mFragmentSampleToGroupEntries.Length() != 0
          ? &mFragmentSampleToGroupEntries
          : aTrackSampleToGroupEntries;

  if (!sampleToGroupEntries) {
    return nullptr;
  }

  uint32_t seen = 0;

  for (const SampleToGroupEntry& entry : *sampleToGroupEntries) {
    if (seen + entry.mSampleCount > aSample) {
      sampleToGroupEntry = &entry;
      break;
    }
    seen += entry.mSampleCount;
  }

  // ISO-14496-12 Section 8.9.2.3 and 8.9.4 : group description index
  // (1) ranges from 1 to the number of sample group entries in the track
  // level SampleGroupDescription Box, or (2) takes the value 0 to
  // indicate that this sample is a member of no group, in this case, the
  // sample is associated with the default values specified in
  // TrackEncryption Box, or (3) starts at 0x10001, i.e. the index value
  // 1, with the value 1 in the top 16 bits, to reference fragment-local
  // SampleGroupDescription Box.

  // According to the spec, ISO-14496-12, the sum of the sample counts in this
  // box should be equal to the total number of samples, and, if less, the
  // reader should behave as if an extra SampleToGroupEntry existed, with
  // groupDescriptionIndex 0.

  if (!sampleToGroupEntry || sampleToGroupEntry->mGroupDescriptionIndex == 0) {
    return nullptr;
  }

  const FallibleTArray<CencSampleEncryptionInfoEntry>* entries =
      aTrackSampleEncryptionInfoEntries;

  uint32_t groupIndex = sampleToGroupEntry->mGroupDescriptionIndex;

  // If the first bit is set to a one, then we should use the sample group
  // descriptions from the fragment.
  if (groupIndex > SampleToGroupEntry::kFragmentGroupDescriptionIndexBase) {
    groupIndex -= SampleToGroupEntry::kFragmentGroupDescriptionIndexBase;
    entries = &mFragmentSampleEncryptionInfoEntries;
  }

  if (!entries) {
    return nullptr;
  }

  // The group_index is one based.
  return groupIndex > entries->Length() ? nullptr
                                        : &entries->ElementAt(groupIndex - 1);
}

void Moof::ParseTraf(const Box& aBox, const TrackParseMode& aTrackParseMode,
                     const Trex& aTrex, const Mvhd& aMvhd, const Mdhd& aMdhd,
                     const Edts& aEdts, const Sinf& aSinf, const bool aIsAudio,
                     uint64_t* aDecodeTime) {
  LOG_DEBUG(
      Traf, "Starting, aTrackParseMode={}, track#={} (ignore if multitrack).",
      aTrackParseMode.is<ParseAllTracks>() ? "multitrack" : "single track",
      aTrackParseMode.is<ParseAllTracks>() ? 0
                                           : aTrackParseMode.as<uint32_t>());
  MOZ_ASSERT(aDecodeTime);
  MOZ_ASSERT(aTrackParseMode.is<ParseAllTracks>() ||
                 aTrex.mTrackId == aTrackParseMode.as<uint32_t>(),
             "If not parsing all tracks, aTrex should have the same track id "
             "as the track being parsed.");
  Tfdt tfdt;

  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("tfhd")) {
      mTfhd = Tfhd(box, aTrex);
    } else if (aTrackParseMode.is<ParseAllTracks>() ||
               mTfhd.mTrackId == aTrackParseMode.as<uint32_t>()) {
      if (box.IsType("tfdt")) {
        tfdt = Tfdt(box);
      } else if (box.IsType("sgpd")) {
        Sgpd sgpd(box);
        if (sgpd.IsValid() && sgpd.mGroupingType == "seig") {
          mFragmentSampleEncryptionInfoEntries.Clear();
          if (!mFragmentSampleEncryptionInfoEntries.AppendElements(
                  sgpd.mEntries, mozilla::fallible)) {
            LOG_ERROR(Moof, "OOM");
            return;
          }
        }
      } else if (box.IsType("sbgp")) {
        Sbgp sbgp(box);
        if (sbgp.IsValid() && sbgp.mGroupingType == "seig") {
          mFragmentSampleToGroupEntries.Clear();
          if (!mFragmentSampleToGroupEntries.AppendElements(
                  sbgp.mEntries, mozilla::fallible)) {
            LOG_ERROR(Moof, "OOM");
            return;
          }
        }
      } else if (box.IsType("saiz")) {
        if (!mSaizs.AppendElement(Saiz(box, aSinf.mDefaultEncryptionType),
                                  mozilla::fallible)) {
          LOG_ERROR(Moof, "OOM");
          return;
        }
      } else if (box.IsType("saio")) {
        if (!mSaios.AppendElement(Saio(box, aSinf.mDefaultEncryptionType),
                                  mozilla::fallible)) {
          LOG_ERROR(Moof, "OOM");
          return;
        }
      }
    }
  }
  if (aTrackParseMode.is<uint32_t>() &&
      mTfhd.mTrackId != aTrackParseMode.as<uint32_t>()) {
    LOG_DEBUG(Traf,
              "Early return as not multitrack parser and track id didn't match "
              "mTfhd.mTrackId={}",
              mTfhd.mTrackId);
    return;
  }
  // Second pass: search for trun boxes and senc boxes.
  uint64_t decodeTime =
      tfdt.IsValid() ? tfdt.mBaseMediaDecodeTime : *aDecodeTime;
  Box sencBox;
  for (Box box = aBox.FirstChild(); box.IsAvailable(); box = box.Next()) {
    if (box.IsType("trun")) {
      if (ParseTrun(box, aMvhd, aMdhd, aEdts, aIsAudio, &decodeTime).isOk()) {
        mValid = true;
      } else {
        LOG_WARN(Moof, "ParseTrun failed");
        mValid = false;
        return;
      }
    } else if (box.IsType("senc")) {
      LOG_DEBUG(Moof, "Found senc box");
      sencBox = box;
    }
  }

  // senc box found: parse it.
  // We need to parse senc boxes in another pass because we need potential sgpd
  // and sbgp boxes to have been parsed, as they might override the IV size and
  // as such the size of senc entries.
  // trun box shall have been parsed as well, so mIndex has been filled.
  if (sencBox.IsAvailable()) {
    if (ParseSenc(sencBox, aSinf).isErr()) [[unlikely]] {
      LOG_WARN(Moof, "ParseSenc failed");
    }
  }

  *aDecodeTime = decodeTime;
  LOG_DEBUG(Traf, "Done, setting aDecodeTime={}.", decodeTime);
}

void Moof::FixRounding(const Moof& aMoof) {
  TimeUnit gap = aMoof.mTimeRange.start - mTimeRange.end;
  if (gap.IsPositive() && gap <= mMaxRoundingError) {
    mTimeRange.end = aMoof.mTimeRange.start;
  }
}

Result<Ok, nsresult> Moof::ParseSenc(const Box& aBox, const Sinf& aSinf) {
  // If we already had a senc box, ignore following ones
  // Not sure how likely this could be in real life
  if (mSencValid) [[unlikely]] {
    LOG_WARN(Moof, "Already found a valid senc box, ignoring new one");
    return Ok();
  }

  BoxReader reader(aBox);
  const uint8_t version = MOZ_TRY(reader->ReadU8());
  const uint32_t flags = MOZ_TRY(reader->ReadU24());
  const uint32_t sampleCount = MOZ_TRY(reader->ReadU32());
  // ISO/IEC 23001-7 §7.2:
  // "sample_count is the number of protected samples in the containing track or
  // track fragment. This value SHALL be either zero (0) or the total number of
  // samples in the track or track fragment."
  if (sampleCount == 0) {
    LOG_DEBUG(Moof, "senc box has 0 sample_count");
    // Though having sample_count = 0 seems to be compliant, return without
    // error but don't set mSencValid to true in case there is another senc box
    // or saio/saiz auxiliary data
    return Ok();
  }
  if (sampleCount != mIndex.Length()) {
    LOG_ERROR(Moof, "Invalid sample count in senc box: expecting {}, got {}\n",
              mIndex.Length(), sampleCount);
    return Err(NS_ERROR_FAILURE);
  }

  if (version == 0) {
    for (size_t i = 0; i < sampleCount; ++i) {
      Sample& sample = mIndex[i];
      const CencSampleEncryptionInfoEntry* sampleInfo =
          GetSampleEncryptionEntry(i);
      uint8_t ivSize = sampleInfo ? sampleInfo->mIVSize : aSinf.mDefaultIVSize;
      if (!reader->ReadArray(sample.mIV, ivSize)) {
        return Err(MediaResult::Logged(
            NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
            RESULT_DETAIL("sample InitializationVector error"),
            gMediaDemuxerLog));
      }
      // Clear arrays, to be safe, in the (unlikely and invalid) case we started
      // to parse a previous senc box but it failed halfway.
      sample.mPlainSizes.Clear();
      sample.mEncryptedSizes.Clear();
      const bool useSubSampleEncryption = flags & 0x02;
      if (useSubSampleEncryption) {
        uint16_t subsampleCount = MOZ_TRY(reader->ReadU16());
        for (uint16_t i = 0; i < subsampleCount; ++i) {
          uint16_t bytesOfClearData = MOZ_TRY(reader->ReadU16());
          uint32_t bytesOfProtectedData = MOZ_TRY(reader->ReadU32());
          sample.mPlainSizes.AppendElement(bytesOfClearData);
          sample.mEncryptedSizes.AppendElement(bytesOfProtectedData);
        }
      } else {
        // No UseSubSampleEncryption flag means the entire sample is encrypted.
        sample.mPlainSizes.AppendElement(0);
        sample.mEncryptedSizes.AppendElement(sample.mByteRange.Length());
      }
    }
  } else if (version == 1) {
    // TODO
    LOG_ERROR(Senc, "version {} not supported yet", version);
    return Err(NS_ERROR_FAILURE);
  } else if (version == 2) {
    // TODO
    LOG_ERROR(Senc, "version {} not supported yet", version);
    return Err(NS_ERROR_FAILURE);
  } else {
    LOG_ERROR(Senc, "Unknown version {}", version);
    return Err(NS_ERROR_FAILURE);
  }
  mSencValid = true;
  return Ok();
}

Result<Ok, nsresult> Moof::ParseTrun(const Box& aBox, const Mvhd& aMvhd,
                                     const Mdhd& aMdhd, const Edts& aEdts,
                                     const bool aIsAudio,
                                     uint64_t* aDecodeTime) {
  LOG_DEBUG(Trun, "Starting.");
  if (!mTfhd.IsValid() || !aMvhd.IsValid() || !aMdhd.IsValid() ||
      !aEdts.IsValid()) {
    LOG_WARN(
        Moof, "Invalid dependencies: mTfhd({}) aMvhd({}) aMdhd({}) aEdts({})",
        mTfhd.IsValid(), aMvhd.IsValid(), aMdhd.IsValid(), !aEdts.IsValid());
    return Err(NS_ERROR_FAILURE);
  }

  BoxReader reader(aBox);
  if (!reader->CanReadType<uint32_t>()) {
    LOG_WARN(Moof, "Incomplete Box (missing flags)");
    return Err(NS_ERROR_FAILURE);
  }
  uint32_t flags = MOZ_TRY(reader->ReadU32());

  if (!reader->CanReadType<uint32_t>()) {
    LOG_WARN(Moof, "Incomplete Box (missing sampleCount)");
    return Err(NS_ERROR_FAILURE);
  }
  uint32_t sampleCount = MOZ_TRY(reader->ReadU32());
  if (sampleCount == 0) {
    LOG_DEBUG(Trun, "Trun with no samples, returning.");
    return Ok();
  }

  CheckedUint64 offset = mTfhd.mBaseDataOffset;
  if (flags & 0x01) {
    offset += MOZ_TRY(reader->ReadU32());
  }
  uint32_t firstSampleFlags = mTfhd.mDefaultSampleFlags;
  if (flags & 0x04) {
    firstSampleFlags = MOZ_TRY(reader->ReadU32());
  }

  // Bug 2004835: use CheckedInt to make sure we don't overflow.
  // Reinterpret the tfdt as signed before assigning to CheckedInt.
  // Some encoders store AAC pre-roll delay as 2^64-N (e.g. 2^64-2048), which
  // is valid per ISO 14496-12 but exceeds INT64_MAX. The static_cast gives a
  // small negative value (-N) that the existing edit-list mechanism treats as
  // pre-roll, keeping the presentation timestamps correct. Accumulation
  // overflow is still caught by the isValid() check below.
  CheckedInt64 decodeTime = static_cast<int64_t>(*aDecodeTime);

  if (!mIndex.SetCapacity(mIndex.Length() + sampleCount, fallible)) {
    LOG_ERROR(Moof, "Out of Memory");
    return Err(NS_ERROR_FAILURE);
  }

  for (size_t i = 0; i < sampleCount; i++) {
    uint32_t sampleDuration = mTfhd.mDefaultSampleDuration;
    if (flags & 0x100) {
      sampleDuration = MOZ_TRY(reader->ReadU32());
    }
    uint32_t sampleSize = mTfhd.mDefaultSampleSize;
    if (flags & 0x200) {
      sampleSize = MOZ_TRY(reader->ReadU32());
    }
    uint32_t sampleFlags = i ? mTfhd.mDefaultSampleFlags : firstSampleFlags;
    if (flags & 0x400) {
      sampleFlags = MOZ_TRY(reader->ReadU32());
    }
    int32_t ctsOffset = 0;
    if (flags & 0x800) {
      ctsOffset = MOZ_TRY(reader->Read32());
    }

    if (sampleSize) {
      Sample sample;
      auto byteRange = MakeMediaByteRangeChecked(offset, sampleSize);
      if (byteRange.isErr()) {
        LOG_WARN(Moof, "Invalid sample offset");
        return Err(NS_ERROR_FAILURE);
      }
      sample.mByteRange = byteRange.unwrap();
      offset += sampleSize;

      TimeUnit decodeOffset =
          MOZ_TRY(aMdhd.ToTimeUnit(decodeTime - aEdts.mMediaStart));
      TimeUnit emptyOffset = MOZ_TRY(aMvhd.ToTimeUnit(aEdts.mEmptyOffset));
      sample.mDecodeTime = decodeOffset + emptyOffset;
      TimeUnit startCts =
          MOZ_TRY(aMdhd.ToTimeUnit(decodeTime + ctsOffset - aEdts.mMediaStart));
      TimeUnit endCts = MOZ_TRY(aMdhd.ToTimeUnit(
          decodeTime + ctsOffset + sampleDuration - aEdts.mMediaStart));
      sample.mCompositionRange =
          MP4Interval<TimeUnit>(startCts + emptyOffset, endCts + emptyOffset);
      // Sometimes audio streams don't properly mark their samples as keyframes,
      // because every audio sample is a keyframe.
      sample.mSync = !(sampleFlags & 0x1010000) || aIsAudio;

      MOZ_ALWAYS_TRUE(mIndex.AppendElement(sample, fallible));

      mMdatRange = mMdatRange.Span(sample.mByteRange);
    }
    decodeTime += sampleDuration;
  }
  TimeUnit roundTime = MOZ_TRY(aMdhd.ToTimeUnit(sampleCount));
  mMaxRoundingError = roundTime + mMaxRoundingError;

  if (!decodeTime.isValid()) {
    LOG_WARN(Moof, "Decode time overflow in ParseTrun");
    return Err(NS_ERROR_FAILURE);
  }
  // The int64_t -> uint64_t conversion preserves the bit pattern; the next
  // ParseTrun call reinterprets it back via static_cast<int64_t>, recovering
  // the correct (possibly negative) decode time.
  *aDecodeTime = decodeTime.value();

  LOG_DEBUG(Trun, "Done.");
  return Ok();
}

Tkhd::Tkhd(const Box& aBox) : mTrackId(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Tkhd, "Parse failed");
  }
}

Result<Ok, nsresult> Tkhd::Parse(const Box& aBox) {
  BoxReader reader(aBox);
  uint32_t flags = MOZ_TRY(reader->ReadU32());
  uint8_t version = flags >> 24;
  if (version == 0) {
    uint32_t creationTime = MOZ_TRY(reader->ReadU32());
    uint32_t modificationTime = MOZ_TRY(reader->ReadU32());
    mTrackId = MOZ_TRY(reader->ReadU32());
    [[maybe_unused]] uint32_t reserved = MOZ_TRY(reader->ReadU32());
    uint32_t duration = MOZ_TRY(reader->ReadU32());
    NS_ASSERTION(!reserved, "reserved should be 0");

    mCreationTime = creationTime;
    mModificationTime = modificationTime;
    mDuration = duration;
  } else if (version == 1) {
    mCreationTime = MOZ_TRY(reader->ReadU64());
    mModificationTime = MOZ_TRY(reader->ReadU64());
    mTrackId = MOZ_TRY(reader->ReadU32());
    [[maybe_unused]] uint32_t reserved = MOZ_TRY(reader->ReadU32());
    NS_ASSERTION(!reserved, "reserved should be 0");
    mDuration = MOZ_TRY(reader->ReadU64());
  }
  return Ok();
}

Mvhd::Mvhd(const Box& aBox)
    : mCreationTime(0), mModificationTime(0), mTimescale(0), mDuration(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Mvhd, "Parse failed");
  }
}

Result<Ok, nsresult> Mvhd::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  uint32_t flags = MOZ_TRY(reader->ReadU32());
  uint8_t version = flags >> 24;

  if (version == 0) {
    uint32_t creationTime = MOZ_TRY(reader->ReadU32());
    uint32_t modificationTime = MOZ_TRY(reader->ReadU32());
    mTimescale = MOZ_TRY(reader->ReadU32());
    uint32_t duration = MOZ_TRY(reader->ReadU32());
    mCreationTime = creationTime;
    mModificationTime = modificationTime;
    mDuration = duration;
  } else if (version == 1) {
    mCreationTime = MOZ_TRY(reader->ReadU64());
    mModificationTime = MOZ_TRY(reader->ReadU64());
    mTimescale = MOZ_TRY(reader->ReadU32());
    mDuration = MOZ_TRY(reader->ReadU64());
  } else {
    return Err(NS_ERROR_FAILURE);
  }
  return Ok();
}

Mdhd::Mdhd(const Box& aBox) : Mvhd(aBox) {}

Trex::Trex(const Box& aBox)
    : mFlags(0),
      mTrackId(0),
      mDefaultSampleDescriptionIndex(0),
      mDefaultSampleDuration(0),
      mDefaultSampleSize(0),
      mDefaultSampleFlags(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Trex, "Parse failed");
  }
}

Result<Ok, nsresult> Trex::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  mFlags = MOZ_TRY(reader->ReadU32());
  mTrackId = MOZ_TRY(reader->ReadU32());
  mDefaultSampleDescriptionIndex = MOZ_TRY(reader->ReadU32());
  mDefaultSampleDuration = MOZ_TRY(reader->ReadU32());
  mDefaultSampleSize = MOZ_TRY(reader->ReadU32());
  mDefaultSampleFlags = MOZ_TRY(reader->ReadU32());

  return Ok();
}

Tfhd::Tfhd(const Box& aBox, const Trex& aTrex)
    : Trex(aTrex), mBaseDataOffset(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Tfhd, "Parse failed");
  }
}

Result<Ok, nsresult> Tfhd::Parse(const Box& aBox) {
  MOZ_ASSERT(aBox.IsType("tfhd"));
  MOZ_ASSERT(aBox.Parent()->IsType("traf"));
  MOZ_ASSERT(aBox.Parent()->Parent()->IsType("moof"));

  BoxReader reader(aBox);

  mFlags = MOZ_TRY(reader->ReadU32());
  mTrackId = MOZ_TRY(reader->ReadU32());
  mBaseDataOffset = aBox.Parent()->Parent()->Offset();
  if (mFlags & 0x01) {
    mBaseDataOffset = MOZ_TRY(reader->ReadU64());
  }
  if (mFlags & 0x02) {
    mDefaultSampleDescriptionIndex = MOZ_TRY(reader->ReadU32());
  }
  if (mFlags & 0x08) {
    mDefaultSampleDuration = MOZ_TRY(reader->ReadU32());
  }
  if (mFlags & 0x10) {
    mDefaultSampleSize = MOZ_TRY(reader->ReadU32());
  }
  if (mFlags & 0x20) {
    mDefaultSampleFlags = MOZ_TRY(reader->ReadU32());
  }

  return Ok();
}

Tfdt::Tfdt(const Box& aBox) : mBaseMediaDecodeTime(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Tfdt, "Parse failed");
  }
}

Result<Ok, nsresult> Tfdt::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  uint32_t flags = MOZ_TRY(reader->ReadU32());
  uint8_t version = flags >> 24;
  if (version == 0) {
    mBaseMediaDecodeTime = MOZ_TRY(reader->ReadU32());
  } else if (version == 1) {
    mBaseMediaDecodeTime = MOZ_TRY(reader->ReadU64());
  }
  return Ok();
}

Edts::Edts(const Box& aBox) : mMediaStart(0), mEmptyOffset(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Edts, "Parse failed");
  }
}

Result<Ok, nsresult> Edts::Parse(const Box& aBox) {
  Box child = aBox.FirstChild();
  if (!child.IsType("elst")) {
    return Err(NS_ERROR_FAILURE);
  }

  BoxReader reader(child);
  uint32_t flags = MOZ_TRY(reader->ReadU32());
  uint8_t version = flags >> 24;
  bool emptyEntry = false;
  uint32_t entryCount = MOZ_TRY(reader->ReadU32());
  for (uint32_t i = 0; i < entryCount; i++) {
    uint64_t segment_duration;
    int64_t media_time;
    if (version == 1) {
      segment_duration = MOZ_TRY(reader->ReadU64());
      media_time = MOZ_TRY(reader->Read64());
    } else {
      segment_duration = MOZ_TRY(reader->ReadU32());
      media_time = MOZ_TRY(reader->Read32());
    }
    if (media_time == -1 && i) {
      LOG_WARN(Edts, "Multiple empty edit, not handled");
    } else if (media_time == -1) {
      if (segment_duration > std::numeric_limits<int64_t>::max()) {
        NS_WARNING("Segment duration higher than int64_t max.");
        mEmptyOffset = std::numeric_limits<int64_t>::max();
      } else {
        mEmptyOffset = static_cast<int64_t>(segment_duration);
      }
      emptyEntry = true;
    } else if (i > 1 || (i > 0 && !emptyEntry)) {
      LOG_WARN(Edts,
               "More than one edit entry, not handled. A/V sync will be wrong");
      break;
    } else {
      mMediaStart = media_time;
    }
    MOZ_TRY(reader->ReadU32());  // media_rate_integer and media_rate_fraction
  }

  return Ok();
}

Saiz::Saiz(const Box& aBox, AtomType aDefaultType)
    : mAuxInfoType(aDefaultType), mAuxInfoTypeParameter(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Saiz, "Parse failed");
  }
}

Result<Ok, nsresult> Saiz::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  uint32_t flags = MOZ_TRY(reader->ReadU32());
  if (flags & 1) {
    mAuxInfoType = MOZ_TRY(reader->ReadU32());
    mAuxInfoTypeParameter = MOZ_TRY(reader->ReadU32());
  }
  uint8_t defaultSampleInfoSize = MOZ_TRY(reader->ReadU8());
  uint32_t count = MOZ_TRY(reader->ReadU32());
  if (defaultSampleInfoSize) {
    if (!mSampleInfoSize.SetLength(count, fallible)) {
      LOG_ERROR(Saiz, "OOM");
      return Err(NS_ERROR_FAILURE);
    }
    memset(mSampleInfoSize.Elements(), defaultSampleInfoSize,
           mSampleInfoSize.Length());
  } else {
    if (!reader->ReadArray(mSampleInfoSize, count)) {
      LOG_WARN(Saiz, "Incomplete Box (OOM or missing count:{})", count);
      return Err(NS_ERROR_FAILURE);
    }
  }
  return Ok();
}

Saio::Saio(const Box& aBox, AtomType aDefaultType)
    : mAuxInfoType(aDefaultType), mAuxInfoTypeParameter(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Saio, "Parse failed");
  }
}

Result<Ok, nsresult> Saio::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  uint32_t flags = MOZ_TRY(reader->ReadU32());
  uint8_t version = flags >> 24;
  if (flags & 1) {
    mAuxInfoType = MOZ_TRY(reader->ReadU32());
    mAuxInfoTypeParameter = MOZ_TRY(reader->ReadU32());
  }

  size_t count = MOZ_TRY(reader->ReadU32());
  if (!mOffsets.SetCapacity(count, fallible)) {
    LOG_ERROR(Saiz, "OOM");
    return Err(NS_ERROR_FAILURE);
  }
  if (version == 0) {
    for (size_t i = 0; i < count; i++) {
      uint32_t offset = MOZ_TRY(reader->ReadU32());
      MOZ_ALWAYS_TRUE(mOffsets.AppendElement(offset, fallible));
    }
  } else {
    for (size_t i = 0; i < count; i++) {
      uint64_t offset = MOZ_TRY(reader->ReadU64());
      MOZ_ALWAYS_TRUE(mOffsets.AppendElement(offset, fallible));
    }
  }
  return Ok();
}

Sbgp::Sbgp(const Box& aBox) : mGroupingTypeParam(0) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Sbgp, "Parse failed");
  }
}

Result<Ok, nsresult> Sbgp::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  uint32_t flags = MOZ_TRY(reader->ReadU32());
  const uint8_t version = flags >> 24;

  mGroupingType = MOZ_TRY(reader->ReadU32());

  if (version == 1) {
    mGroupingTypeParam = MOZ_TRY(reader->ReadU32());
  }

  uint32_t count = MOZ_TRY(reader->ReadU32());

  for (uint32_t i = 0; i < count; i++) {
    uint32_t sampleCount = MOZ_TRY(reader->ReadU32());
    uint32_t groupDescriptionIndex = MOZ_TRY(reader->ReadU32());

    SampleToGroupEntry entry(sampleCount, groupDescriptionIndex);
    if (!mEntries.AppendElement(entry, mozilla::fallible)) {
      LOG_ERROR(Sbgp, "OOM");
      return Err(NS_ERROR_FAILURE);
    }
  }
  return Ok();
}

Sgpd::Sgpd(const Box& aBox) {
  mValid = Parse(aBox).isOk();
  if (!mValid) {
    LOG_WARN(Sgpd, "Parse failed");
  }
}

Result<Ok, nsresult> Sgpd::Parse(const Box& aBox) {
  BoxReader reader(aBox);

  uint32_t flags = MOZ_TRY(reader->ReadU32());
  const uint8_t version = flags >> 24;

  mGroupingType = MOZ_TRY(reader->ReadU32());

  const uint32_t entrySize = sizeof(uint32_t) + kKeyIdSize;
  uint32_t defaultLength = 0;

  if (version == 1) {
    defaultLength = MOZ_TRY(reader->ReadU32());
    if (defaultLength < entrySize && defaultLength != 0) {
      return Err(NS_ERROR_FAILURE);
    }
  }

  uint32_t count = MOZ_TRY(reader->ReadU32());

  for (uint32_t i = 0; i < count; ++i) {
    if (version == 1 && defaultLength == 0) {
      uint32_t descriptionLength = MOZ_TRY(reader->ReadU32());
      if (descriptionLength < entrySize) {
        return Err(NS_ERROR_FAILURE);
      }
    }

    CencSampleEncryptionInfoEntry entry;
    bool valid = entry.Init(reader).isOk();
    if (!valid) {
      return Err(NS_ERROR_FAILURE);
    }
    if (!mEntries.AppendElement(entry, mozilla::fallible)) {
      LOG_ERROR(Sgpd, "OOM");
      return Err(NS_ERROR_FAILURE);
    }
  }
  return Ok();
}

Result<Ok, nsresult> CencSampleEncryptionInfoEntry::Init(BoxReader& aReader) {
  // Skip a reserved byte.
  MOZ_TRY(aReader->ReadU8());

  uint8_t pattern = MOZ_TRY(aReader->ReadU8());
  mCryptByteBlock = pattern >> 4;
  mSkipByteBlock = pattern & 0x0f;

  uint8_t isEncrypted = MOZ_TRY(aReader->ReadU8());
  mIsEncrypted = isEncrypted != 0;

  mIVSize = MOZ_TRY(aReader->ReadU8());

  // Read the key id.
  if (!mKeyId.SetLength(kKeyIdSize, fallible)) {
    LOG_ERROR(CencSampleEncryptionInfoEntry, "OOM");
    return Err(NS_ERROR_FAILURE);
  }
  for (uint32_t i = 0; i < kKeyIdSize; ++i) {
    mKeyId.ElementAt(i) = MOZ_TRY(aReader->ReadU8());
  }

  if (mIsEncrypted && mIVSize == 0) {
    // Protected content with 0 sized IV indicates a constant IV is present.
    // This is used for the cbcs scheme.
    uint8_t constantIVSize = MOZ_TRY(aReader->ReadU8());
    if (constantIVSize != 8 && constantIVSize != 16) {
      LOG_WARN(CencSampleEncryptionInfoEntry, "Unexpected constantIVSize: {}",
               constantIVSize);
      return Err(NS_ERROR_FAILURE);
    }
    if (!mConsantIV.SetLength(constantIVSize, mozilla::fallible)) {
      LOG_ERROR(CencSampleEncryptionInfoEntry, "OOM");
      return Err(NS_ERROR_FAILURE);
    }
    for (uint32_t i = 0; i < constantIVSize; ++i) {
      mConsantIV.ElementAt(i) = MOZ_TRY(aReader->ReadU8());
    }
  } else if (mIsEncrypted ? (mIVSize != 8 && mIVSize != 16) : mIVSize != 0) {
    LOG_WARN(CencSampleEncryptionInfoEntry,
             "Unexpected IVSize: {}, isEncrypted={}", mIVSize, mIsEncrypted);
    return Err(NS_ERROR_FAILURE);
  }
  LOG_DEBUG(CencSampleEncryptionInfoEntry,
            "isEncrypted={}, IVSize={}, constantIVSize={}", mIsEncrypted,
            mIVSize, mConsantIV.Length());

  return Ok();
}
}  // namespace mozilla

#undef LOG_DEBUG
#undef LOG_WARN
#undef LOG_ERROR
