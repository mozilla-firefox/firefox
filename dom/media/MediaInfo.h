/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MediaInfo_h
#define MediaInfo_h

#include "AudioConfig.h"
#include "ImageTypes.h"
#include "MediaData.h"
#include "TimeUnits.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Variant.h"
#include "mozilla/gfx/Point.h"  // for gfx::IntSize
#include "mozilla/gfx/Rect.h"   // for gfx::IntRect
#include "mozilla/gfx/Types.h"  // for gfx::ColorDepth
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"

namespace mozilla {

class AudioInfo;
class VideoInfo;
class TextInfo;

class MetadataTag {
 public:
  MetadataTag(const nsACString& aKey, const nsACString& aValue)
      : mKey(aKey), mValue(aValue) {}
  nsCString mKey;
  nsCString mValue;
  bool operator==(const MetadataTag& rhs) const {
    return mKey == rhs.mKey && mValue == rhs.mValue;
  }
};

using MetadataTags = nsTHashMap<nsCStringHashKey, nsCString>;

// Start codec specific data structs. If modifying these remember to also
// modify the MediaIPCUtils so that any new members are sent across IPC.

// Generic types, we should prefer a specific type when we can.

// Generic empty type. Prefer to use a specific type but not populate members
// if possible, as that helps with type checking.
struct NoCodecSpecificData {
  bool operator==(const NoCodecSpecificData& rhs) const { return true; }
};

// Generic binary blob type. Prefer not to use this structure. It's here to ease
// the transition to codec specific structures in the code.
struct AudioCodecSpecificBinaryBlob {
  bool operator==(const AudioCodecSpecificBinaryBlob& rhs) const {
    return *mBinaryBlob == *rhs.mBinaryBlob;
  }

  RefPtr<MediaByteBuffer> mBinaryBlob{new MediaByteBuffer};
};

// End generic types.

// Audio codec specific data types.

struct AacCodecSpecificData {
  bool operator==(const AacCodecSpecificData& rhs) const {
    return *mEsDescriptorBinaryBlob == *rhs.mEsDescriptorBinaryBlob &&
           *mDecoderConfigDescriptorBinaryBlob ==
               *rhs.mDecoderConfigDescriptorBinaryBlob;
  }
  // An explanation for the necessity of handling the encoder delay and the
  // padding is available here:
  // https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/QTFFAppenG/QTFFAppenG.html

  // The number of frames that should be skipped from the beginning of the
  // decoded stream.
  uint32_t mEncoderDelayFrames{0};

  // The total number of frames of the media, that is, excluding the encoder
  // delay and the padding of the last packet, that must be discarded.
  // Unset when the exact count is unknown (e.g. Matroska has no frame table
  // and counting would require reading the whole file).
  Maybe<uint64_t> mMediaFrameCount;

  // The bytes of the ES_Descriptor field parsed out of esds box. We store
  // this as a blob as some decoders want this.
  RefPtr<MediaByteBuffer> mEsDescriptorBinaryBlob{new MediaByteBuffer};

  // The bytes of the DecoderConfigDescriptor field within the parsed
  // ES_Descriptor. This is a subset of the ES_Descriptor, so it is technically
  // redundant to store both. However, some decoders expect this binary blob
  // instead of the whole ES_Descriptor, so both are stored for convenience
  // and clarity (rather than reparsing the ES_Descriptor).
  // TODO(bug 1768562): use a Span to track this rather than duplicating data.
  RefPtr<MediaByteBuffer> mDecoderConfigDescriptorBinaryBlob{
      new MediaByteBuffer};
};

struct FlacCodecSpecificData {
  bool operator==(const FlacCodecSpecificData& rhs) const {
    return *mStreamInfoBinaryBlob == *rhs.mStreamInfoBinaryBlob;
  }

  // A binary blob of the data from the METADATA_BLOCK_STREAMINFO block
  // in the flac header.
  // See https://xiph.org/flac/format.html#metadata_block_streaminfo
  // Consumers of this data (ffmpeg) take a blob, so we don't parse the data,
  // just store the blob. For headerless flac files this will be left empty.
  RefPtr<MediaByteBuffer> mStreamInfoBinaryBlob{new MediaByteBuffer};
};

struct Mp3CodecSpecificData final {
  bool operator==(const Mp3CodecSpecificData& rhs) const = default;

  auto MutTiedFields() {
    return std::tie(mEncoderDelayFrames, mEncoderPaddingFrames,
                    mDecoderDelayFrames);
  }

  // The number of frames that should be skipped from the beginning of the
  // decoded stream.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1566389 for more info.
  uint32_t mEncoderDelayFrames{0};

  // The number of frames that should be skipped from the end of the decoded
  // stream.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1566389 for more info.
  uint32_t mEncoderPaddingFrames{0};

  // The number of frames by which the decoded audio lags the timestamps of the
  // frames, when the demuxer's timestamps don't account for the decoder delay.
  // MediaSourceTrackDemuxer moves the decoded audio earlier by it.
  uint32_t mDecoderDelayFrames{0};
};

struct OpusCodecSpecificData {
  bool operator==(const OpusCodecSpecificData& rhs) const {
    return mContainerCodecDelayFrames == rhs.mContainerCodecDelayFrames &&
           mOutputChannels == rhs.mOutputChannels &&
           *mHeadersBinaryBlob == *rhs.mHeadersBinaryBlob;
  }
  // The codec delay (aka pre-skip) in audio frames.
  // See https://tools.ietf.org/html/rfc7845#section-4.2 for more info.
  // This member should store the codec delay parsed from the container file.
  // In some cases (such as the ogg container), this information is derived
  // from the same headers stored in the header blob, making storing this
  // separately redundant. However, other containers store the delay in
  // addition to the header blob, in which case we can check this container
  // delay against the header delay to ensure they're consistent.
  int64_t mContainerCodecDelayFrames{-1};

  // A binary blob of opus header data, specifically the Identification Header.
  // See https://datatracker.ietf.org/doc/html/rfc7845.html#section-5.1
  RefPtr<MediaByteBuffer> mHeadersBinaryBlob{new MediaByteBuffer};

  // The number of channels the audio will be rendered as, which may be fewer
  // than the stream has when playback is configured to downmix. Opus codes
  // wide stereo using intensity stereo phase inversion, which cancels itself
  // out once the two channels are mixed together, so a decoder must disable
  // it when this is 1.
  uint32_t mOutputChannels{0};
};

struct VorbisCodecSpecificData {
  bool operator==(const VorbisCodecSpecificData& rhs) const {
    return *mHeadersBinaryBlob == *rhs.mHeadersBinaryBlob;
  }

  // A binary blob of headers in the 'extradata' format (the format ffmpeg
  // expects for packing the extradata field). This is also the format some
  // containers use for storing the data. Specifically, this format consists of
  // the page_segments field, followed by the segment_table field, followed by
  // the three Vorbis header packets, respectively the identification header,
  // the comments header, and the setup header, in that order.
  // See also https://xiph.org/vorbis/doc/framing.html and
  // https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-610004.2
  RefPtr<MediaByteBuffer> mHeadersBinaryBlob{new MediaByteBuffer};
};

struct WaveCodecSpecificData {
  bool operator==(const WaveCodecSpecificData& rhs) const { return true; }
  // Intentionally empty. We don't store any wave specific data, but this
  // variant is useful for type checking.
};

using AudioCodecSpecificVariant =
    mozilla::Variant<NoCodecSpecificData, AudioCodecSpecificBinaryBlob,
                     AacCodecSpecificData, FlacCodecSpecificData,
                     Mp3CodecSpecificData, OpusCodecSpecificData,
                     VorbisCodecSpecificData, WaveCodecSpecificData>;

// Returns a binary blob representation of the AudioCodecSpecificVariant. This
// does not guarantee that a binary representation exists. Will return an empty
// buffer if no representation exists. Prefer `GetAudioCodecSpecificBlob` which
// asserts if getting a blob is unexpected for a given codec config.
inline already_AddRefed<MediaByteBuffer> ForceGetAudioCodecSpecificBlob(
    const AudioCodecSpecificVariant& v) {
  return v.match(
      [](const NoCodecSpecificData&) {
        return RefPtr<MediaByteBuffer>(new MediaByteBuffer).forget();
      },
      [](const AudioCodecSpecificBinaryBlob& binaryBlob) {
        return RefPtr<MediaByteBuffer>(binaryBlob.mBinaryBlob).forget();
      },
      [](const AacCodecSpecificData& aacData) {
        // We return the mDecoderConfigDescriptor blob here, as it is more
        // commonly used by decoders at time of writing than the
        // ES_Descriptor data. However, consumers of this data should
        // prefer getting one or the other specifically, rather than
        // calling this.
        return RefPtr<MediaByteBuffer>(
                   aacData.mDecoderConfigDescriptorBinaryBlob)
            .forget();
      },
      [](const FlacCodecSpecificData& flacData) {
        return RefPtr<MediaByteBuffer>(flacData.mStreamInfoBinaryBlob).forget();
      },
      [](const Mp3CodecSpecificData&) {
        return RefPtr<MediaByteBuffer>(new MediaByteBuffer).forget();
      },
      [](const OpusCodecSpecificData& opusData) {
        return RefPtr<MediaByteBuffer>(opusData.mHeadersBinaryBlob).forget();
      },
      [](const VorbisCodecSpecificData& vorbisData) {
        return RefPtr<MediaByteBuffer>(vorbisData.mHeadersBinaryBlob).forget();
      },
      [](const WaveCodecSpecificData&) {
        return RefPtr<MediaByteBuffer>(new MediaByteBuffer).forget();
      });
}

// Same as `ForceGetAudioCodecSpecificBlob` but with extra asserts to ensure
// we're not trying to get a binary blob from codecs where we don't store the
// information as a blob or where a blob is ambiguous.
inline already_AddRefed<MediaByteBuffer> GetAudioCodecSpecificBlob(
    const AudioCodecSpecificVariant& v) {
  return ForceGetAudioCodecSpecificBlob(v);
}

// End audio codec specific data types.

// End codec specific data structs.

class TrackInfo {
 public:
  enum TrackType { kUndefinedTrack, kAudioTrack, kVideoTrack, kTextTrack };
  TrackInfo(TrackType aType, const nsAString& aId, const nsAString& aKind,
            const nsAString& aLabel, const nsAString& aLanguage, bool aEnabled,
            uint32_t aTrackId)
      : mId(aId),
        mKind(aKind),
        mLabel(aLabel),
        mLanguage(aLanguage),
        mEnabled(aEnabled),
        mTrackId(aTrackId),
        mIsRenderedExternally(false),
        mType(aType) {
    MOZ_COUNT_CTOR(TrackInfo);
  }

  // Only used for backward compatibility. Do not use in new code.
  void Init(const nsAString& aId, const nsAString& aKind,
            const nsAString& aLabel, const nsAString& aLanguage,
            bool aEnabled) {
    mId = aId;
    mKind = aKind;
    mLabel = aLabel;
    mLanguage = aLanguage;
    mEnabled = aEnabled;
  }

  // Fields common with MediaTrack object.
  nsString mId;
  nsString mKind;
  nsString mLabel;
  nsString mLanguage;
  bool mEnabled;

  uint32_t mTrackId;

  nsCString mMimeType;
  media::TimeUnit mDuration;
  media::TimeUnit mMediaTime;
  uint32_t mTimeScale = 0;
  CryptoTrack mCrypto;

  CopyableTArray<MetadataTag> mTags;

  // True if the track is gonna be (decrypted)/decoded and
  // rendered directly by non-gecko components.
  bool mIsRenderedExternally;

  virtual AudioInfo* GetAsAudioInfo() { return nullptr; }
  virtual VideoInfo* GetAsVideoInfo() { return nullptr; }
  virtual TextInfo* GetAsTextInfo() { return nullptr; }
  virtual const AudioInfo* GetAsAudioInfo() const { return nullptr; }
  virtual const VideoInfo* GetAsVideoInfo() const { return nullptr; }
  virtual const TextInfo* GetAsTextInfo() const { return nullptr; }

  bool IsAudio() const { return !!GetAsAudioInfo(); }
  bool IsVideo() const { return !!GetAsVideoInfo(); }
  bool IsText() const { return !!GetAsTextInfo(); }
  TrackType GetType() const { return mType; }

  virtual nsCString ToString() const;

  bool virtual IsValid() const = 0;

  virtual UniquePtr<TrackInfo> Clone() const = 0;

  MOZ_COUNTED_DTOR_VIRTUAL(TrackInfo)

 protected:
  TrackInfo(const TrackInfo& aOther) {
    mId = aOther.mId;
    mKind = aOther.mKind;
    mLabel = aOther.mLabel;
    mLanguage = aOther.mLanguage;
    mEnabled = aOther.mEnabled;
    mTrackId = aOther.mTrackId;
    mMimeType = aOther.mMimeType;
    mDuration = aOther.mDuration;
    mMediaTime = aOther.mMediaTime;
    mCrypto = aOther.mCrypto;
    mIsRenderedExternally = aOther.mIsRenderedExternally;
    mType = aOther.mType;
    mTags = aOther.mTags.Clone();
    MOZ_COUNT_CTOR(TrackInfo);
  }
  bool IsEqualTo(const TrackInfo& rhs) const;

 private:
  TrackType mType;
};

// String version of track type.
const char* TrackTypeToStr(TrackInfo::TrackType aTrack);

enum class VideoRotation {
  kDegree_0 = 0,
  kDegree_90 = 90,
  kDegree_180 = 180,
  kDegree_270 = 270,
  // Keep in sync with VideoRotationValidator.
};

// Stores info relevant to presenting media frames.
class VideoInfo : public TrackInfo {
 public:
  VideoInfo() : VideoInfo(-1, -1) {}

  VideoInfo(int32_t aWidth, int32_t aHeight)
      : VideoInfo(gfx::IntSize(aWidth, aHeight)) {}

  explicit VideoInfo(const gfx::IntSize& aSize)
      : TrackInfo(kVideoTrack, u"2"_ns, u"main"_ns, u""_ns, u""_ns, true, 2),
        mDisplay(aSize),
        mStereoMode(StereoMode::MONO),
        mImage(aSize),
        mCodecSpecificConfig(new MediaByteBuffer),
        mExtraData(new MediaByteBuffer),
        mRotation(VideoRotation::kDegree_0) {}

  VideoInfo(const VideoInfo& aOther) : TrackInfo(aOther) {
    if (aOther.mCodecSpecificConfig) {
      mCodecSpecificConfig = new MediaByteBuffer();
      mCodecSpecificConfig->AppendElements(
          reinterpret_cast<uint8_t*>(aOther.mCodecSpecificConfig->Elements()),
          aOther.mCodecSpecificConfig->Length());
    }
    if (aOther.mExtraData) {
      mExtraData = new MediaByteBuffer();
      mExtraData->AppendElements(
          reinterpret_cast<uint8_t*>(aOther.mExtraData->Elements()),
          aOther.mExtraData->Length());
    }
    mDisplay = aOther.mDisplay;
    mStereoMode = aOther.mStereoMode;
    mImage = aOther.mImage;
    mRotation = aOther.mRotation;
    mColorDepth = aOther.mColorDepth;
    mColorSpace = aOther.mColorSpace;
    mColorPrimaries = aOther.mColorPrimaries;
    mTransferFunction = aOther.mTransferFunction;
    mHDRMetadata = aOther.mHDRMetadata;
    mColorRange = aOther.mColorRange;
    mImageRect = aOther.mImageRect;
    mAlphaPresent = aOther.mAlphaPresent;
    mFrameRate = aOther.mFrameRate;
  };

  bool operator==(const VideoInfo& rhs) const;

  bool IsValid() const override {
    return mDisplay.width > 0 && mDisplay.height > 0;
  }

  VideoInfo* GetAsVideoInfo() override { return this; }

  const VideoInfo* GetAsVideoInfo() const override { return this; }

  UniquePtr<TrackInfo> Clone() const override {
    return std::make_unique<VideoInfo>(*this);
  }

  void SetAlpha(bool aAlphaPresent) { mAlphaPresent = aAlphaPresent; }

  bool HasAlpha() const { return mAlphaPresent; }

  gfx::IntRect ImageRect() const {
    if (!mImageRect) {
      return gfx::IntRect(0, 0, mImage.width, mImage.height);
    }
    return *mImageRect;
  }

  void SetImageRect(const gfx::IntRect& aRect) { mImageRect = Some(aRect); }
  void ResetImageRect() { mImageRect.reset(); }

  // Adopts an image size decoded from the bitstream. The picture rectangle is
  // expressed relative to the image size, so a change in size invalidates it
  // and it is discarded; an unchanged size keeps the existing rectangle.
  void AdoptImageSize(const gfx::IntSize& aImage) {
    if (mImage != aImage) {
      ResetImageRect();
    }
    mImage = aImage;
  }

  // Returned the crop rectangle scaled to aWidth/aHeight size relative to
  // mImage size.
  // If aWidth and aHeight are identical to the original
  // mImage.width/mImage.height then the scaling ratio will be 1. This is used
  // for when the frame size is different from what the container reports. This
  // is legal in WebM, and we will preserve the ratio of the crop rectangle as
  // it was reported relative to the picture size reported by the container.
  gfx::IntRect ScaledImageRect(int64_t aWidth, int64_t aHeight) const {
    if ((aWidth == mImage.width && aHeight == mImage.height) || !mImage.width ||
        !mImage.height) {
      return ImageRect();
    }

    gfx::IntRect imageRect = ImageRect();
    int64_t w = (aWidth * imageRect.Width()) / mImage.width;
    int64_t h = (aHeight * imageRect.Height()) / mImage.height;
    if (!w || !h) {
      return imageRect;
    }

    imageRect.x = AssertedCast<int>((imageRect.x * aWidth) / mImage.width);
    imageRect.y = AssertedCast<int>((imageRect.y * aHeight) / mImage.height);
    imageRect.SetWidth(AssertedCast<int>(w));
    imageRect.SetHeight(AssertedCast<int>(h));
    return imageRect;
  }

  VideoRotation ToSupportedRotation(int32_t aDegree) const {
    switch (aDegree) {
      case 90:
        return VideoRotation::kDegree_90;
      case 180:
        return VideoRotation::kDegree_180;
      case 270:
        return VideoRotation::kDegree_270;
      default:
        NS_WARNING_ASSERTION(aDegree == 0, "Invalid rotation degree, ignored");
        return VideoRotation::kDegree_0;
    }
  }

  nsCString ToString() const override;

  // Size in pixels at which the video is rendered. This is after it has
  // been scaled by its aspect ratio.
  gfx::IntSize mDisplay;

  // Indicates the frame layout for single track stereo videos.
  StereoMode mStereoMode;

  // Size of the decoded video's image.
  gfx::IntSize mImage;

  RefPtr<MediaByteBuffer> mCodecSpecificConfig;
  RefPtr<MediaByteBuffer> mExtraData;

  // Describing how many degrees video frames should be rotated in clock-wise to
  // get correct view.
  VideoRotation mRotation;

  // Should be 8, 10 or 12. Default value is 8.
  gfx::ColorDepth mColorDepth = gfx::ColorDepth::COLOR_8;

  // Matrix coefficients (if specified by the video) imply a colorspace.
  Maybe<gfx::YUVColorSpace> mColorSpace;

  // Color primaries are independent from the coefficients.
  Maybe<gfx::ColorSpace2> mColorPrimaries;

  // Transfer functions get their own member, which may not be strongly
  // correlated to the colorspace.
  Maybe<gfx::TransferFunction> mTransferFunction;

  Maybe<gfx::HDRMetadata> mHDRMetadata;

  // True indicates no restriction on Y, U, V values (otherwise 16-235 for 8
  // bits etc)
  gfx::ColorRange mColorRange = gfx::ColorRange::LIMITED;

  Maybe<int32_t> GetFrameRate() const { return mFrameRate; }
  void SetFrameRate(int32_t aRate) { mFrameRate = Some(aRate); }

 private:
  friend struct IPC::ParamTraits<VideoInfo>;

  // mImage may be cropped; currently only used with the WebM container.
  // If unset, no cropping is to occur.
  Maybe<gfx::IntRect> mImageRect;

  // Indicates whether or not frames may contain alpha information.
  bool mAlphaPresent = false;

  Maybe<int32_t> mFrameRate;
};

class AudioInfo : public TrackInfo {
 public:
  AudioInfo()
      : TrackInfo(kAudioTrack, u"1"_ns, u"main"_ns, u""_ns, u""_ns, true, 1),
        mRate(0),
        mChannels(0),
        mChannelMap(AudioConfig::ChannelLayout::UNKNOWN_MAP),
        mBitDepth(0),
        mProfile(0),
        mExtendedProfile(0) {}

  AudioInfo(const AudioInfo& aOther) = default;

  bool operator==(const AudioInfo& rhs) const;

  static const uint32_t MAX_RATE = 768000;
  static const uint32_t MAX_CHANNEL_COUNT = 256;

  bool IsValid() const override {
    return mChannels > 0 && mChannels <= MAX_CHANNEL_COUNT && mRate > 0 &&
           mRate <= MAX_RATE;
  }

  AudioInfo* GetAsAudioInfo() override { return this; }

  const AudioInfo* GetAsAudioInfo() const override { return this; }

  nsCString ToString() const override;

  UniquePtr<TrackInfo> Clone() const override {
    return std::make_unique<AudioInfo>(*this);
  }

  // Sample rate.
  uint32_t mRate;

  // Number of audio channels.
  uint32_t mChannels;
  // The AudioConfig::ChannelLayout map. Channels are ordered as per SMPTE
  // definition. A value of UNKNOWN_MAP indicates unknown layout.
  // ChannelMap is an unsigned bitmap compatible with Windows' WAVE and FFmpeg
  // channel map.
  AudioConfig::ChannelLayout::ChannelMap mChannelMap;

  // Bits per sample.
  uint32_t mBitDepth;

  // Codec profile.
  uint8_t mProfile;

  // Extended codec profile.
  uint8_t mExtendedProfile;

  AudioCodecSpecificVariant mCodecSpecificConfig{NoCodecSpecificData{}};
};

class EncryptionInfo {
 public:
  EncryptionInfo() : mEncrypted(false) {}

  struct InitData {
    template <typename AInitDatas>
    InitData(const nsAString& aType, AInitDatas&& aInitData)
        : mType(aType), mInitData(std::forward<AInitDatas>(aInitData)) {}

    // Encryption type to be passed to JS. Usually `cenc'.
    nsString mType;

    // Encryption data.
    CopyableTArray<uint8_t> mInitData;
  };
  using InitDatas = CopyableTArray<InitData>;

  // True if the stream has encryption metadata
  bool IsEncrypted() const { return mEncrypted; }

  void Reset() {
    mEncrypted = false;
    mInitDatas.Clear();
  }

  template <typename AInitDatas>
  void AddInitData(const nsAString& aType, AInitDatas&& aInitData) {
    mInitDatas.AppendElement(
        InitData(aType, std::forward<AInitDatas>(aInitData)));
    mEncrypted = true;
  }

  void AddInitData(const EncryptionInfo& aInfo) {
    mInitDatas.AppendElements(aInfo.mInitDatas);
    mEncrypted = !!mInitDatas.Length();
  }

  // One 'InitData' per encrypted buffer.
  InitDatas mInitDatas;

 private:
  bool mEncrypted;
};

class MediaInfo {
 public:
  bool HasVideo() const { return mVideo.IsValid(); }

  void EnableVideo() {
    if (HasVideo()) {
      return;
    }
    // Set dummy values so that HasVideo() will return true;
    // See VideoInfo::IsValid()
    mVideo.mDisplay = gfx::IntSize(1, 1);
  }

  bool HasAudio() const { return mAudio.IsValid(); }

  void EnableAudio() {
    if (HasAudio()) {
      return;
    }
    // Set dummy values so that HasAudio() will return true;
    // See AudioInfo::IsValid()
    mAudio.mChannels = 2;
    mAudio.mRate = 44100;
  }

  bool IsEncrypted() const {
    return (HasAudio() && mAudio.mCrypto.IsEncrypted()) ||
           (HasVideo() && mVideo.mCrypto.IsEncrypted());
  }

  bool HasValidMedia() const { return HasVideo() || HasAudio(); }

  // TODO: Store VideoInfo and AudioIndo in arrays to support multi-tracks.
  VideoInfo mVideo;
  AudioInfo mAudio;

  // If the metadata includes a duration, we store it here.
  media::NullableTimeUnit mMetadataDuration;

  // The Ogg reader tries to kinda-sorta compute the duration by seeking to the
  // end and determining the timestamp of the last frame. This isn't useful as
  // a duration until we know the start time, so we need to track it separately.
  media::NullableTimeUnit mUnadjustedMetadataEndTime;

  // True if the media is seekable (i.e. supports random access).
  bool mMediaSeekable = true;

  // True if the media is only seekable within its buffered ranges.
  bool mMediaSeekableOnlyInBufferedRanges = false;

  EncryptionInfo mCrypto;

  // The minimum of start times of audio and video tracks.
  // Use to map the zero time on the media timeline to the first frame.
  media::TimeUnit mStartTime;
};

class TrackInfoSharedPtr {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TrackInfoSharedPtr)
 public:
  TrackInfoSharedPtr(const TrackInfo& aOriginal, uint32_t aStreamID)
      : mInfo(aOriginal.Clone()),
        mStreamSourceID(aStreamID),
        mMimeType(mInfo->mMimeType) {}

  uint32_t GetID() const { return mStreamSourceID; }

  operator const TrackInfo*() const { return mInfo.get(); }

  const TrackInfo* operator*() const { return mInfo.get(); }

  const TrackInfo* operator->() const {
    MOZ_ASSERT(mInfo.get(), "dereferencing a UniquePtr containing nullptr");
    return mInfo.get();
  }

  const AudioInfo* GetAsAudioInfo() const {
    return mInfo ? mInfo->GetAsAudioInfo() : nullptr;
  }

  const VideoInfo* GetAsVideoInfo() const {
    return mInfo ? mInfo->GetAsVideoInfo() : nullptr;
  }

  const TextInfo* GetAsTextInfo() const {
    return mInfo ? mInfo->GetAsTextInfo() : nullptr;
  }

 private:
  ~TrackInfoSharedPtr() = default;
  UniquePtr<TrackInfo> mInfo;
  // A unique ID, guaranteed to change when changing streams.
  uint32_t mStreamSourceID;

 public:
  const nsCString& mMimeType;
};

}  // namespace mozilla

#endif  // MediaInfo_h
