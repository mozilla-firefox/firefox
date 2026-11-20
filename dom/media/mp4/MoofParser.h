/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOOF_PARSER_H_
#define MOOF_PARSER_H_

#include "Atom.h"
#include "AtomType.h"
#include "ByteStream.h"
#include "MP4Interval.h"
#include "MediaResource.h"
#include "SinfParser.h"
#include "TimeUnits.h"
#include "mozilla/Variant.h"

namespace mozilla {

class Box;
class BoxContext;
class BoxReader;
class Moof;

// Used to track the CTS end time of the last sample of a track
// in the preceeding Moof, so that we can smooth tracks' timestamps
// across Moofs.
struct TrackEndCts {
  TrackEndCts(uint32_t aTrackId, const media::TimeUnit& aCtsEndTime)
      : mTrackId(aTrackId), mCtsEndTime(aCtsEndTime) {}
  uint32_t mTrackId;
  media::TimeUnit mCtsEndTime;
};

class Mvhd : public Atom {
 public:
  Mvhd()
      : mCreationTime(0), mModificationTime(0), mTimescale(0), mDuration(0) {}
  explicit Mvhd(const Box& aBox);

  Result<media::TimeUnit, nsresult> ToTimeUnit(
      CheckedInt64 aTimescaleUnits) const {
    if (!aTimescaleUnits.isValid()) {
      NS_WARNING("invalid aTimescaleUnits");
      return Err(NS_ERROR_FAILURE);
    }
    if (!mTimescale) {
      NS_WARNING("invalid mTimescale");
      return Err(NS_ERROR_FAILURE);
    }
    return media::TimeUnit(aTimescaleUnits, mTimescale);
  }

  uint64_t mCreationTime;
  uint64_t mModificationTime;
  uint32_t mTimescale;
  uint64_t mDuration;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

class Tkhd : public Mvhd {
 public:
  Tkhd() : mTrackId(0) {}
  explicit Tkhd(const Box& aBox);

  uint32_t mTrackId;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

class Mdhd : public Mvhd {
 public:
  Mdhd() = default;
  explicit Mdhd(const Box& aBox);
};

class Trex : public Atom {
 public:
  explicit Trex(uint32_t aTrackId)
      : mFlags(0),
        mTrackId(aTrackId),
        mDefaultSampleDescriptionIndex(0),
        mDefaultSampleDuration(0),
        mDefaultSampleSize(0),
        mDefaultSampleFlags(0) {}

  explicit Trex(const Box& aBox);

  uint32_t mFlags;
  uint32_t mTrackId;
  uint32_t mDefaultSampleDescriptionIndex;
  uint32_t mDefaultSampleDuration;
  uint32_t mDefaultSampleSize;
  uint32_t mDefaultSampleFlags;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

class Tfhd : public Trex {
 public:
  explicit Tfhd(const Trex& aTrex) : Trex(aTrex), mBaseDataOffset(0) {
    mValid = aTrex.IsValid();
  }
  Tfhd(const Box& aBox, const Trex& aTrex);

  uint64_t mBaseDataOffset;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

class Tfdt : public Atom {
 public:
  Tfdt() : mBaseMediaDecodeTime(0) {}
  explicit Tfdt(const Box& aBox);

  uint64_t mBaseMediaDecodeTime;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

class Edts : public Atom {
 public:
  Edts() : mMediaStart(0), mEmptyOffset(0) {}
  explicit Edts(const Box& aBox);
  virtual bool IsValid() const override {
    // edts is optional
    return true;
  }

  int64_t mMediaStart;
  int64_t mEmptyOffset;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

struct Sample {
  mozilla::MediaByteRange mByteRange;
  // Crypto information coming from senc box: shall be used first
  CopyableTArray<uint8_t> mIV;
  CopyableTArray<uint32_t> mPlainSizes;
  // The number of encrypted bytes in each subsample. The nth element in the
  // array is the number of encrypted bytes at the start of the nth subsample.
  CopyableTArray<uint32_t> mEncryptedSizes;
  // Crypto information coming from saio box: shall be used if no senc
  // information is present
  mozilla::MediaByteRange mCencRange;
  media::TimeUnit mDecodeTime;
  MP4Interval<media::TimeUnit> mCompositionRange;
  bool mSync;
};

class Saiz final : public Atom {
 public:
  Saiz(const Box& aBox, AtomType aDefaultType);

  AtomType mAuxInfoType;
  uint32_t mAuxInfoTypeParameter;
  FallibleTArray<uint8_t> mSampleInfoSize;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

class Saio final : public Atom {
 public:
  Saio(const Box& aBox, AtomType aDefaultType);

  AtomType mAuxInfoType;
  uint32_t mAuxInfoTypeParameter;
  FallibleTArray<uint64_t> mOffsets;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

struct SampleToGroupEntry {
 public:
  static const uint32_t kTrackGroupDescriptionIndexBase = 0;
  static const uint32_t kFragmentGroupDescriptionIndexBase = 0x10000;

  SampleToGroupEntry(uint32_t aSampleCount, uint32_t aGroupDescriptionIndex)
      : mSampleCount(aSampleCount),
        mGroupDescriptionIndex(aGroupDescriptionIndex) {}

  uint32_t mSampleCount;
  uint32_t mGroupDescriptionIndex;
};

class Sbgp final : public Atom  // SampleToGroup box.
{
 public:
  explicit Sbgp(const Box& aBox);

  AtomType mGroupingType;
  uint32_t mGroupingTypeParam;
  FallibleTArray<SampleToGroupEntry> mEntries;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

// Stores information form CencSampleEncryptionInformationGroupEntry (seig).
// Cenc here refers to the common encryption standard, rather than the specific
// cenc scheme from that standard. This structure is used for all encryption
// schemes. I.e. it is used for both cenc and cbcs, not just cenc.
struct CencSampleEncryptionInfoEntry final {
 public:
  CencSampleEncryptionInfoEntry() = default;

  Result<Ok, nsresult> Init(BoxReader& aReader);

  bool mIsEncrypted = false;
  uint8_t mIVSize = 0;
  CopyableTArray<uint8_t> mKeyId;
  uint8_t mCryptByteBlock = 0;
  uint8_t mSkipByteBlock = 0;
  CopyableTArray<uint8_t> mConsantIV;
};

class Sgpd final : public Atom  // SampleGroupDescription box.
{
 public:
  explicit Sgpd(const Box& aBox);

  AtomType mGroupingType;
  FallibleTArray<CencSampleEncryptionInfoEntry> mEntries;

 protected:
  Result<Ok, nsresult> Parse(const Box& aBox);
};

// Audio/video entries from the sample description box (stsd). We only need to
// store if these are encrypted, so do not need a specialized class for
// different audio and video data. Currently most of the parsing of these
// entries is by the mp4parse-rust, but moof pasrser needs to know which of
// these are encrypted when parsing the track fragment header (tfhd).
struct SampleDescriptionEntry {
  bool mIsEncryptedEntry = false;
};

// Used to indicate in variants if all tracks should be parsed.
struct ParseAllTracks {};

using TrackParseMode = Variant<ParseAllTracks, uint32_t>;

class Moof final : public Atom {
 public:
  Moof(const Box& aBox, const TrackParseMode& aTrackParseMode,
       const Trex& aTrex, const Mvhd& aMvhd, const Mdhd& aMdhd,
       const Edts& aEdts, const Sinf& aSinf, const bool aIsAudio,
       uint64_t* aDecodeTime, nsTArray<TrackEndCts>& aTracksEndCts);
  void FixRounding(const Moof& aMoof);

  // Retrieve CencSampleEncryptionInfoEntry for a given sample number.
  // Optionally, you can provide track's group boxes (sbgp): they will be used
  // if the moof fragment does not contain a sbgp box.
  const CencSampleEncryptionInfoEntry* GetSampleEncryptionEntry(
      size_t aSample,
      const FallibleTArray<SampleToGroupEntry>* aTrackSampleToGroupEntries =
          nullptr,
      const FallibleTArray<CencSampleEncryptionInfoEntry>*
          aTrackSampleEncryptionInfoEntries = nullptr) const;

  // Returns true if a senc box has been found, successfully parsed, and
  // contains crypto info
  bool SencIsValid() const { return mSencValid; }

  mozilla::MediaByteRange mRange;
  mozilla::MediaByteRange mMdatRange;
  MP4Interval<media::TimeUnit> mTimeRange;
  FallibleTArray<Sample> mIndex;

  FallibleTArray<CencSampleEncryptionInfoEntry>
      mFragmentSampleEncryptionInfoEntries;
  FallibleTArray<SampleToGroupEntry> mFragmentSampleToGroupEntries;

  Tfhd mTfhd;
  FallibleTArray<Saiz> mSaizs;
  FallibleTArray<Saio> mSaios;
  nsTArray<nsTArray<uint8_t>> mPsshes;

 private:
  // aDecodeTime is updated to the end of the parsed TRAF on return.
  void ParseTraf(const Box& aBox, const TrackParseMode& aTrackParseMode,
                 const Trex& aTrex, const Mvhd& aMvhd, const Mdhd& aMdhd,
                 const Edts& aEdts, const Sinf& aSinf, const bool aIsAudio,
                 uint64_t* aDecodeTime);
  // aDecodeTime is updated to the end of the parsed TRUN on return.
  Result<Ok, nsresult> ParseTrun(const Box& aBox, const Mvhd& aMvhd,
                                 const Mdhd& aMdhd, const Edts& aEdts,
                                 const bool aIsAudio, uint64_t* aDecodeTime);
  Result<Ok, nsresult> ParseSenc(const Box& aBox, const Sinf& aSinf);
  // Process the sample auxiliary information used by common encryption.
  // aScheme is used to select the appropriate auxiliary information and should
  // be set based on the encryption scheme used by the track being processed.
  // Note, the term cenc here refers to the standard, not the specific scheme
  // from that standard. I.e. this function is used to handle up auxiliary
  // information from the cenc and cbcs schemes.
  bool ProcessCencAuxInfo(AtomType aScheme);
  bool GetAuxInfo(AtomType aType, FallibleTArray<MediaByteRange>* aByteRanges);

  media::TimeUnit mMaxRoundingError;
  bool mSencValid = false;
};

DDLoggedTypeDeclName(MoofParser);

class MoofParser : public DecoderDoctorLifeLogger<MoofParser> {
 public:
  MoofParser(ByteStream* aSource, const TrackParseMode& aTrackParseMode,
             const bool aIsAudio)
      : mSource(aSource),
        mOffset(0),
        mTrex(aTrackParseMode.is<uint32_t>() ? aTrackParseMode.as<uint32_t>()
                                             : 0),
        mIsAudio(aIsAudio),
        mLastDecodeTime(0),
        mTrackParseMode(aTrackParseMode) {
    // Setting mIsMultitrackParser is a nasty work around for calculating
    // the composition range for MSE that causes the parser to parse multiple
    // tracks. Ideally we'd store an array of tracks with different metadata
    // for each.
    DDLINKCHILD("source", aSource);
  }
  // Advance, looking for additional moov, moof, or mdat boxes in aByteRanges.
  // Return true or false to indicate whether a new valid moof is seen.
  bool RebuildFragmentedIndex(const mozilla::MediaByteRangeSet& aByteRanges);
  // If *aCanEvict is set to true. then will remove all moofs already parsed
  // from index then rebuild the index. *aCanEvict is set to true upon return if
  // some moofs were removed.
  bool RebuildFragmentedIndex(const mozilla::MediaByteRangeSet& aByteRanges,
                              bool* aCanEvict);
  bool RebuildFragmentedIndex(BoxContext& aContext);
  MP4Interval<media::TimeUnit> GetCompositionRange(
      const mozilla::MediaByteRangeSet& aByteRanges);
  bool ReachedEnd();
  void ParseMoov(const Box& aBox);
  void ParseTrak(const Box& aBox);
  void ParseMdia(const Box& aBox);
  void ParseMvex(const Box& aBox);

  void ParseMinf(const Box& aBox);
  void ParseStbl(const Box& aBox);
  void ParseStsd(const Box& aBox);
  void ParseEncrypted(const Box& aBox);

  // Similar to RebuildFragmentedIndex(), but advance only as far as the next
  // moof, only if there is a next moof, and block, waiting for the read, if
  // the ByteStream supports blocking reads.
  // Return NS_OK if a new valid moof is seen or
  // NS_ERROR_DOM_MEDIA_END_OF_STREAM if no fatal error occurs before reaching
  // end of stream.
  nsresult BlockingReadNextMoof();

  already_AddRefed<mozilla::MediaByteBuffer> Metadata();
  MediaByteRange FirstCompleteMediaSegment();
  MediaByteRange FirstCompleteMediaHeader();

  const CencSampleEncryptionInfoEntry* GetSampleEncryptionEntry(
      size_t moofNumber, size_t aMoof) const;

  mozilla::MediaByteRange mInitRange;
  RefPtr<ByteStream> mSource;
  uint64_t mOffset;
  Mvhd mMvhd;
  Mdhd mMdhd;
  Trex mTrex;
  Tfdt mTfdt;
  Edts mEdts;
  Sinf mSinf;

  FallibleTArray<CencSampleEncryptionInfoEntry>
      mTrackSampleEncryptionInfoEntries;
  FallibleTArray<SampleToGroupEntry> mTrackSampleToGroupEntries;
  FallibleTArray<SampleDescriptionEntry> mSampleDescriptions;

  nsTArray<Moof>& Moofs() { return mMoofs; }

 private:
  void ScanForMetadata(mozilla::MediaByteRange& aMoov);
  // Sets the end of each video sample at a link between aNewMoof and an
  // earlier moof to the start of the next sample in composition order: it
  // extends the sample, or cuts back an end that came from a later sample.
  void EndSamplesAtNextComposition(Moof& aNewMoof);
  nsTArray<Moof> mMoofs;
  nsTArray<MediaByteRange> mMediaRanges;
  nsTArray<TrackEndCts> mTracksEndCts;
  const bool mIsAudio;
  uint64_t mLastDecodeTime;
  // Either a ParseAllTracks if in multitrack mode, or an integer representing
  // the track_id for the track being parsed. If parsing a specific track, mTrex
  // should have an id matching mTrackParseMode.as<uint32_t>(). In this case 0
  // is a valid track id -- this is not allowed in the spec, but such mp4s
  // appear in the wild. In the ParseAllTracks case, mTrex can have an arbitrary
  // id based on the tracks being parsed.
  const TrackParseMode mTrackParseMode;
};
}  // namespace mozilla

#endif
