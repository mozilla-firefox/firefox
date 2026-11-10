/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AppleATDecoder.h"

#include <CoreAudioTypes/CoreAudioBaseTypes.h>
#include <mozilla/Result.h>

#include <memory>

#include "ADTSDemuxer.h"
#include "Adts.h"
#include "ByteWriter.h"
#include "ErrorList.h"
#include "MP4Decoder.h"
#include "MediaInfo.h"
#include "MediaResult.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/Result.h"
#include "nsDebug.h"
#include "nsTArray.h"

#define LOG(...) \
  MOZ_LOG_FMT(mozilla::sPDMLog, mozilla::LogLevel::Debug, __VA_ARGS__)
#define FourCC2Str(n) \
  ((char[5]){(char)(n >> 24), (char)(n >> 16), (char)(n >> 8), (char)(n), 0})

const int AUDIO_OBJECT_TYPE_USAC = 42;
const UInt32 kDynamicRangeControlProperty =
    0x64726370;  // "drcp", not present in macOS headers

// Write ISO/IEC 14496-1 expandable size field (1-4 bytes) (8.3.3)
// Each byte encodes 7 bits of size with MSB as continuation flag
template <typename T>
static bool WriteDescriptor(mozilla::ByteWriter<T>& writer, uint8_t tag,
                            uint32_t size) {
#define TRY(x)    \
  if (!(x)) {     \
    return false; \
  }
  TRY(writer.WriteU8(tag));
  // Sizes are encoded as:
  // 0xxxxxxx                   - sizes 0 to 127 (1 byte)
  // 1xxxxxxx 0xxxxxxx          - sizes 128 to 16383 (2 bytes)
  // 1xxxxxxx 1xxxxxxx 0xxxxxxx - sizes 16384 to 2097151 (3 bytes)
  // 1xxxxxxx 1xxxxxxx 1xxxxxxx 0xxxxxxx - sizes 2097152+ (4 bytes)
  if (size < 0x80) {
    TRY(writer.WriteU8(size));
  } else if (size < 0x4000) {
    TRY(writer.WriteU8(0x80 | (size >> 7)));
    TRY(writer.WriteU8(size & 0x7F));
  } else if (size < 0x200000) {
    TRY(writer.WriteU8(0x80 | (size >> 14)));
    TRY(writer.WriteU8(0x80 | (size >> 7)));
    TRY(writer.WriteU8(size & 0x7F));
  } else {
    TRY(writer.WriteU8(0x80 | (size >> 21)));
    TRY(writer.WriteU8(0x80 | (size >> 14)));
    TRY(writer.WriteU8(0x80 | (size >> 7)));
    TRY(writer.WriteU8(size & 0x7F));
  }

  return true;
}

#undef TRY

// ISO/IEC 14496-1 (7.2.6.5.1)
static mozilla::Result<nsTArray<uint8_t>, nsresult> CreateEsds(
    const nsTArray<uint8_t>& extradata) {
  nsTArray<uint8_t> esds;
  mozilla::ByteWriter<mozilla::BigEndian> writer(esds);
#define TRY(x)                                             \
  if (!(x)) {                                              \
    LOG("CreateEsds failed at line {}: {}", __LINE__, #x); \
    return mozilla::Err(nsresult::NS_ERROR_FAILURE);       \
  }

  // ES_Descriptor (ES_DescrTag = 0x03)
  // Size calculation breakdown:
  // - 3 bytes: ES_ID (2) + flags (1)
  // - 5 bytes: DecoderConfigDescriptor tag (1) + size field (4 max)
  // - 13 bytes: DecoderConfigDescriptor fixed content
  // - 5 bytes: DecoderSpecificInfo tag (1) + size field (4 max)
  // - extradata.Length(): AudioSpecificConfig data
  const uint32_t kESDescriptorHeaderSize = 3;        // ES_ID + flags
  const uint32_t kDecoderConfigDescrTagSize = 5;     // tag + size field
  const uint32_t kDecoderConfigDescrFixedSize = 13;  // fixed fields
  const uint32_t kDecoderSpecificInfoTagSize = 5;    // tag + size field
  const uint32_t esDescriptorSize =
      kESDescriptorHeaderSize + kDecoderConfigDescrTagSize +
      kDecoderConfigDescrFixedSize + kDecoderSpecificInfoTagSize +
      extradata.Length();
  WriteDescriptor(writer, 0x03, esDescriptorSize);
  TRY(writer.WriteU16(0x0000));  // ES_ID = 0
  TRY(writer.WriteU8(0x00));  // flags (streamDependenceFlag = 0, URL_Flag = 0,
                              // OCRstreamFlag = 0, streamPriority = 0)

  // DecoderConfigDescriptor (DecoderConfigDescrTag = 0x04)
  // ISO/IEC 14496-1 (7.2.6.6)
  const uint32_t decoderConfigDescrSize = kDecoderConfigDescrFixedSize +
                                          kDecoderSpecificInfoTagSize +
                                          extradata.Length();
  TRY(WriteDescriptor(writer, 0x04, decoderConfigDescrSize));
  TRY(writer.WriteU8(0x40));  // objectTypeIndication = 0x40 (MPEG-4 AAC)
  TRY(writer.WriteU8(
      0x15));  // streamType = 0x05 (AudioStream), upstream = 0, reserved = 1

  // bufferSizeDB = 0 (24 bits) - using default buffer size
  TRY(writer.WriteU8(0x00));
  TRY(writer.WriteU16(0x0000));

  TRY(writer.WriteU32(0x00000000));  // maxBitrate = 0 (no limit)
  TRY(writer.WriteU32(0x00000000));  // avgBitrate = 0 (unknown)

  // DecoderSpecificInfo (DecSpecificInfoTag = 0x05)
  // Contains the AudioSpecificConfig from ISO/IEC 14496-3 (7.2.6.7: to be
  // filled by classes extending it, we just write the extradata extracted from
  // the mp4)
  TRY(WriteDescriptor(writer, 0x05, extradata.Length()));
  TRY(writer.Write(extradata.Elements(), extradata.Length()));

  return esds;
}

#undef TRY

namespace mozilla {

AppleATDecoder::AppleATDecoder(const AudioInfo& aConfig)
    : mConfig(aConfig),
      mFileStreamError(false),
      mConverter(nullptr),
      mOutputFormat(),
      mStream(nullptr),
      mParsedFramesForAACMagicCookie(0),
      mErrored(false) {
  MOZ_COUNT_CTOR(AppleATDecoder);
  LOG("Creating Apple AudioToolbox decoder");
  LOG("Audio Decoder configuration: {} {} Hz {} channels {} bits per channel "
      "profile={} extended_profile={}",
      mConfig.mMimeType.get(), mConfig.mRate, mConfig.mChannels,
      mConfig.mBitDepth, mConfig.mProfile, mConfig.mExtendedProfile);

  if (mConfig.mMimeType.EqualsLiteral("audio/mpeg")) {
    mFormatID = kAudioFormatMPEGLayer3;
  } else if (mConfig.mMimeType.EqualsLiteral("audio/mp4a-latm")) {
    if (aConfig.mCodecSpecificConfig.is<AacCodecSpecificData>()) {
      const AacCodecSpecificData& aacCodecSpecificData =
          aConfig.mCodecSpecificConfig.as<AacCodecSpecificData>();

      // Check if this is xHE-AAC (USAC) based on profile or extended_profile
      if (mConfig.mProfile == AUDIO_OBJECT_TYPE_USAC ||
          mConfig.mExtendedProfile == AUDIO_OBJECT_TYPE_USAC) {
        mFormatID = kAudioFormatMPEGD_USAC;
        LOG("AppleATDecoder detected xHE-AAC/USAC format (profile={}, "
            "extended_profile={})",
            mConfig.mProfile, mConfig.mExtendedProfile);
      } else {
        mFormatID = kAudioFormatMPEG4AAC;
      }

      mEncoderDelay = aacCodecSpecificData.mEncoderDelayFrames;
      mTotalMediaFrames = aacCodecSpecificData.mMediaFrameCount.valueOr(0);
      LOG("AppleATDecoder (aac), found encoder delay ({}) and total frame "
          "count ({}) in codec-specific side data",
          mEncoderDelay, mTotalMediaFrames);
    } else {
      mFormatID = kAudioFormatMPEG4AAC;
    }
  } else {
    mFormatID = 0;
  }
}

AppleATDecoder::~AppleATDecoder() {
  MOZ_COUNT_DTOR(AppleATDecoder);
  MOZ_ASSERT(!mConverter);
}

RefPtr<MediaDataDecoder::InitPromise> AppleATDecoder::Init() {
  AUTO_PROFILER_LABEL("AppleATDecoder::Init", MEDIA_PLAYBACK);
  if (!mFormatID) {
    LOG("AppleATDecoder::Init failure: unknown format ID");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Non recognised format")),
        __func__);
  }
  if (!mConfig.IsValid()) {
    LOG("AppleATDecoder::Init failure: invalid configuration");
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    RESULT_DETAIL("Invalid configuration")),
        __func__);
  }
  mThread = GetCurrentSerialEventTarget();

  return InitPromise::CreateAndResolve(TrackType::kAudioTrack, __func__);
}

RefPtr<MediaDataDecoder::FlushPromise> AppleATDecoder::Flush() {
  AUTO_PROFILER_LABEL("AppleATDecoder::Flush", MEDIA_PLAYBACK);
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  LOG("Flushing AudioToolbox AAC decoder");
  mQueuedSamples.Clear();
  mDecodedSamples.Clear();

  if (mConverter) {
    OSStatus rv = AudioConverterReset(mConverter);
    if (rv) {
      LOG("Error {} resetting AudioConverter", static_cast<int>(rv));
    }
  }
  if (mErrored) {
    LOG("Flush error");
    mParsedFramesForAACMagicCookie = 0;
    mMagicCookie.Clear();
    ProcessShutdown();
    mErrored = false;
  }
  return FlushPromise::CreateAndResolve(true, __func__);
}

RefPtr<MediaDataDecoder::DecodePromise> AppleATDecoder::Drain() {
  AUTO_PROFILER_LABEL("AppleATDecoder::Drain", MEDIA_PLAYBACK);
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  LOG("Draining AudioToolbox AAC decoder");
  return DecodePromise::CreateAndResolve(DecodedData(), __func__);
}

RefPtr<ShutdownPromise> AppleATDecoder::Shutdown() {
  AUTO_PROFILER_LABEL("AppleATDecoder::Shutdown", MEDIA_PLAYBACK);
  // mThread may not be set if Init hasn't been called first.
  MOZ_ASSERT(!mThread || mThread->IsOnCurrentThread());
  ProcessShutdown();
  return ShutdownPromise::CreateAndResolve(true, __func__);
}

void AppleATDecoder::ProcessShutdown() {
  // mThread may not be set if Init hasn't been called first.
  MOZ_ASSERT(!mThread || mThread->IsOnCurrentThread());

  if (mStream) {
    OSStatus rv = AudioFileStreamClose(mStream);
    if (rv) {
      LOG("error {} disposing of AudioFileStream", static_cast<int>(rv));
      return;
    }
    mStream = nullptr;
  }

  if (mConverter) {
    LOG("Shutdown: Apple AudioToolbox AAC decoder");
    OSStatus rv = AudioConverterDispose(mConverter);
    if (rv) {
      LOG("error {} disposing of AudioConverter", static_cast<int>(rv));
    }
    mConverter = nullptr;
  }
}

nsCString AppleATDecoder::GetCodecName() const {
  switch (mFormatID) {
    case kAudioFormatMPEGLayer3:
      return "mp3"_ns;
    case kAudioFormatMPEG4AAC:
      return "aac"_ns;
    case kAudioFormatMPEGD_USAC:
      return "xhe-aac"_ns;
    default:
      return "unknown"_ns;
  }
}

struct PassthroughUserData {
  UInt32 mChannels;
  UInt32 mDataSize;
  const void* mData;
  AudioStreamPacketDescription mPacket;
};

// Error value we pass through the decoder to signal that nothing
// has gone wrong during decoding and we're done processing the packet.
const uint32_t kNoMoreDataErr = 'MOAR';

static OSStatus _PassthroughInputDataCallback(
    AudioConverterRef aAudioConverter, UInt32* aNumDataPackets /* in/out */,
    AudioBufferList* aData /* in/out */,
    AudioStreamPacketDescription** aPacketDesc, void* aUserData) {
  PassthroughUserData* userData = (PassthroughUserData*)aUserData;
  if (!userData->mDataSize) {
    *aNumDataPackets = 0;
    return kNoMoreDataErr;
  }

  if (aPacketDesc) {
    userData->mPacket.mStartOffset = 0;
    userData->mPacket.mVariableFramesInPacket = 0;
    userData->mPacket.mDataByteSize = userData->mDataSize;
    *aPacketDesc = &userData->mPacket;
  }

  aData->mBuffers[0].mNumberChannels = userData->mChannels;
  aData->mBuffers[0].mDataByteSize = userData->mDataSize;
  aData->mBuffers[0].mData = const_cast<void*>(userData->mData);

  // No more data to provide following this run.
  userData->mDataSize = 0;

  return noErr;
}

RefPtr<MediaDataDecoder::DecodePromise> AppleATDecoder::Decode(
    MediaRawData* aSample) {
  AUTO_PROFILER_LABEL("AppleATDecoder::Decode", MEDIA_PLAYBACK);
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  LOG("mp4 input sample pts={} duration={} {} {} bytes audio",
      aSample->mTime.ToString().get(), aSample->GetEndTime().ToString().get(),
      aSample->mKeyframe ? " keyframe" : "",
      (unsigned long long)aSample->Size());

  MediaResult rv = NS_OK;
  if (!mConverter) {
    LOG("Lazily initing the decoder");
    rv = SetupDecoder(aSample);
    if (rv != NS_OK && rv != NS_ERROR_NOT_INITIALIZED) {
      LOG("Decoder not initialized");
      return DecodePromise::CreateAndReject(rv, __func__);
    }
  }

  if (mIsADTS) {
    bool rv = ADTS::StripHeader(aSample);
    if (!rv) {
      LOG("Stripping the ADTS header in AppleATDecoder failed");
    }
  }

  mQueuedSamples.AppendElement(aSample);

  if (rv == NS_OK) {
    for (size_t i = 0; i < mQueuedSamples.Length(); i++) {
      rv = DecodeSample(mQueuedSamples[i]);
      if (NS_FAILED(rv)) {
        LOG("Decoding error");
        mErrored = true;
        return DecodePromise::CreateAndReject(rv, __func__);
      }
    }
    mQueuedSamples.Clear();
  }

  DecodedData results = std::move(mDecodedSamples);
  mDecodedSamples = DecodedData();
  return DecodePromise::CreateAndResolve(std::move(results), __func__);
}

MediaResult AppleATDecoder::DecodeSample(MediaRawData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  // Array containing the queued decoded audio frames, about to be output.
  nsTArray<AudioDataValue> outputData;
  UInt32 channels = mOutputFormat.mChannelsPerFrame;
  // Pick a multiple of the frame size close to a power of two
  // for efficient allocation. We're mainly using this decoder to decode AAC,
  // that has packets of 1024 audio frames.
  const uint32_t MAX_AUDIO_FRAMES = 1024;
  const uint32_t maxDecodedSamples = MAX_AUDIO_FRAMES * channels;

  // Descriptions for _decompressed_ audio packets. ignored.
  auto packets =
      std::make_unique<AudioStreamPacketDescription[]>(MAX_AUDIO_FRAMES);

  // This API insists on having packets spoon-fed to it from a callback.
  // This structure exists only to pass our state.
  PassthroughUserData userData = {channels, (UInt32)aSample->Size(),
                                  aSample->Data()};

  // Decompressed audio buffer
  AlignedAudioBuffer decoded(maxDecodedSamples);
  if (!decoded) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  do {
    AudioBufferList decBuffer;
    decBuffer.mNumberBuffers = 1;
    decBuffer.mBuffers[0].mNumberChannels = channels;
    decBuffer.mBuffers[0].mDataByteSize =
        maxDecodedSamples * sizeof(AudioDataValue);
    decBuffer.mBuffers[0].mData = decoded.get();

    // in: the max number of packets we can handle from the decoder.
    // out: the number of packets the decoder is actually returning.
    UInt32 numFrames = MAX_AUDIO_FRAMES;

    OSStatus rv = AudioConverterFillComplexBuffer(
        mConverter, _PassthroughInputDataCallback, &userData,
        &numFrames /* in/out */, &decBuffer, packets.get());

    if (rv && rv != kNoMoreDataErr) {
      LOG("Error decoding audio sample: {}\n", static_cast<int>(rv));
      return MediaResult(
          NS_ERROR_DOM_MEDIA_DECODE_ERR,
          RESULT_DETAIL("Error decoding audio sample: %d @ %s",
                        static_cast<int>(rv), aSample->mTime.ToString().get()));
    }

    if (numFrames) {
      AudioDataValue* outputFrames = decoded.get();
      outputData.AppendElements(outputFrames, numFrames * channels);
    }

    if (rv == kNoMoreDataErr) {
      break;
    }
  } while (true);

  if (outputData.IsEmpty()) {
    return NS_OK;
  }

  size_t numFrames = outputData.Length() / channels;
  int rate = AssertedCast<int>(mOutputFormat.mSampleRate);
  media::TimeUnit duration(numFrames, rate);
  if (!duration.IsValid()) {
    NS_WARNING("Invalid count of accumulated audio samples");
    return MediaResult(
        NS_ERROR_DOM_MEDIA_OVERFLOW_ERR,
        RESULT_DETAIL(
            "Invalid count of accumulated audio samples: num:%llu rate:%d",
            uint64_t(numFrames), rate));
  }

  LOG("Decoded audio packet [{}, {}] (duration: {})\n",
      aSample->mTime.ToString().get(), aSample->GetEndTime().ToString().get(),
      duration.ToString().get());

  AudioSampleBuffer data(outputData.Elements(), outputData.Length());
  if (!data.Data()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  if (mChannelLayout && !mAudioConverter) {
    AudioConfig in(*mChannelLayout, channels, rate);
    AudioConfig out(AudioConfig::ChannelLayout::SMPTEDefault(*mChannelLayout),
                    channels, rate);
    mAudioConverter = std::make_unique<AudioConverter>(in, out);
  }
  if (mAudioConverter && mChannelLayout && mChannelLayout->IsValid()) {
    MOZ_ASSERT(mAudioConverter->CanWorkInPlace());
    data = mAudioConverter->Process(std::move(data));
  }

  RefPtr<AudioData> audio = new AudioData(
      aSample->mOffset, aSample->mTime, data.Forget(), channels, rate,
      mChannelLayout && mChannelLayout->IsValid()
          ? mChannelLayout->Map()
          : AudioConfig::ChannelLayout::UNKNOWN_MAP);
  MOZ_DIAGNOSTIC_ASSERT(duration == audio->mDuration, "must be equal");
  mDecodedSamples.AppendElement(std::move(audio));
  return NS_OK;
}

MediaResult AppleATDecoder::GetInputAudioDescription(
    AudioStreamBasicDescription& aDesc, const nsTArray<uint8_t>& aExtraData) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  // Request the properties from CoreAudio using the codec magic cookie
  AudioFormatInfo formatInfo;
  PodZero(&formatInfo.mASBD);
  formatInfo.mASBD.mFormatID = mFormatID;
  if (mFormatID == kAudioFormatMPEG4AAC) {
    formatInfo.mASBD.mFormatFlags = mConfig.mExtendedProfile;
  }
  formatInfo.mMagicCookieSize = aExtraData.Length();
  formatInfo.mMagicCookie = aExtraData.Elements();

  UInt32 formatListSize;
  // Attempt to retrieve the default format using
  // kAudioFormatProperty_FormatInfo method.
  // This method only retrieves the FramesPerPacket information required
  // by the decoder, which depends on the codec type and profile.
  aDesc.mFormatID = mFormatID;
  aDesc.mChannelsPerFrame = mConfig.mChannels;
  aDesc.mSampleRate = mConfig.mRate;
  UInt32 inputFormatSize = sizeof(aDesc);
  OSStatus rv;

  if (mFormatID == kAudioFormatMPEGD_USAC && aExtraData.Length() > 0) {
    // For xHE-AAC/USAC, we need to use the magic cookie to get the format info
    aDesc.mFormatID = mFormatID;
    aDesc.mChannelsPerFrame = mConfig.mChannels;
    aDesc.mSampleRate = mConfig.mRate;

    rv = AudioFormatGetProperty(kAudioFormatProperty_FormatInfo,
                                aExtraData.Length(), aExtraData.Elements(),
                                &inputFormatSize, &aDesc);
  } else {
    rv = AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, 0, nullptr,
                                &inputFormatSize, &aDesc);
  }

  if (NS_WARN_IF(rv)) {
    return MediaResult(
        NS_ERROR_FAILURE,
        RESULT_DETAIL("Unable to get format info:%d", int32_t(rv)));
  }

  // If any of the methods below fail, we will return the default format as
  // created using kAudioFormatProperty_FormatInfo above.
  rv = AudioFormatGetPropertyInfo(kAudioFormatProperty_FormatList,
                                  sizeof(formatInfo), &formatInfo,
                                  &formatListSize);
  if (rv || (formatListSize % sizeof(AudioFormatListItem))) {
    return NS_OK;
  }
  size_t listCount = formatListSize / sizeof(AudioFormatListItem);
  auto formatList = std::make_unique<AudioFormatListItem[]>(listCount);

  rv = AudioFormatGetProperty(kAudioFormatProperty_FormatList,
                              sizeof(formatInfo), &formatInfo, &formatListSize,
                              formatList.get());
  if (rv) {
    return NS_OK;
  }
  LOG("found {} available audio stream(s)",
      formatListSize / sizeof(AudioFormatListItem));
  // Get the index number of the first playable format.
  // This index number will be for the highest quality layer the platform
  // is capable of playing.
  UInt32 itemIndex;
  UInt32 indexSize = sizeof(itemIndex);
  rv = AudioFormatGetProperty(kAudioFormatProperty_FirstPlayableFormatFromList,
                              formatListSize, formatList.get(), &indexSize,
                              &itemIndex);
  if (rv) {
    return NS_OK;
  }

  aDesc = formatList[itemIndex].mASBD;

  return NS_OK;
}

AudioConfig::Channel ConvertChannelLabel(AudioChannelLabel id) {
  switch (id) {
    case kAudioChannelLabel_Left:
      return AudioConfig::CHANNEL_FRONT_LEFT;
    case kAudioChannelLabel_Right:
      return AudioConfig::CHANNEL_FRONT_RIGHT;
    case kAudioChannelLabel_Mono:
    case kAudioChannelLabel_Center:
      return AudioConfig::CHANNEL_FRONT_CENTER;
    case kAudioChannelLabel_LFEScreen:
      return AudioConfig::CHANNEL_LFE;
    case kAudioChannelLabel_LeftSurround:
      return AudioConfig::CHANNEL_SIDE_LEFT;
    case kAudioChannelLabel_RightSurround:
      return AudioConfig::CHANNEL_SIDE_RIGHT;
    case kAudioChannelLabel_CenterSurround:
      return AudioConfig::CHANNEL_BACK_CENTER;
    case kAudioChannelLabel_RearSurroundLeft:
      return AudioConfig::CHANNEL_BACK_LEFT;
    case kAudioChannelLabel_RearSurroundRight:
      return AudioConfig::CHANNEL_BACK_RIGHT;
    default:
      return AudioConfig::CHANNEL_INVALID;
  }
}

// Will set mChannelLayout if a channel layout could properly be identified
// and is supported.
nsresult AppleATDecoder::SetupChannelLayout() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  // Determine the channel layout.
  UInt32 propertySize;
  UInt32 size;
  OSStatus status = AudioConverterGetPropertyInfo(
      mConverter, kAudioConverterOutputChannelLayout, &propertySize, nullptr);
  if (status || !propertySize) {
    LOG("Couldn't get channel layout property ({})", FourCC2Str(status));
    return NS_ERROR_FAILURE;
  }

  auto data = std::make_unique<uint8_t[]>(propertySize);
  size = propertySize;
  status = AudioConverterGetProperty(
      mConverter, kAudioConverterInputChannelLayout, &size, data.get());
  if (status || size != propertySize) {
    LOG("Couldn't get channel layout property ({})", FourCC2Str(status));
    return NS_ERROR_FAILURE;
  }

  AudioChannelLayout* layout =
      reinterpret_cast<AudioChannelLayout*>(data.get());
  AudioChannelLayoutTag tag = layout->mChannelLayoutTag;

  // if tag is kAudioChannelLayoutTag_UseChannelDescriptions then the structure
  // directly contains the the channel layout mapping.
  // If tag is kAudioChannelLayoutTag_UseChannelBitmap then the layout will
  // be defined via the bitmap and can be retrieved using
  // kAudioFormatProperty_ChannelLayoutForBitmap property.
  // Otherwise the tag itself describes the layout.
  if (tag != kAudioChannelLayoutTag_UseChannelDescriptions) {
    AudioFormatPropertyID property =
        tag == kAudioChannelLayoutTag_UseChannelBitmap
            ? kAudioFormatProperty_ChannelLayoutForBitmap
            : kAudioFormatProperty_ChannelLayoutForTag;

    if (property == kAudioFormatProperty_ChannelLayoutForBitmap) {
      status = AudioFormatGetPropertyInfo(
          property, sizeof(UInt32), &layout->mChannelBitmap, &propertySize);
    } else {
      status = AudioFormatGetPropertyInfo(
          property, sizeof(AudioChannelLayoutTag), &tag, &propertySize);
    }
    if (status || !propertySize) {
      LOG("Couldn't get channel layout property info ({}:{})",
          FourCC2Str(property), FourCC2Str(status));
      return NS_ERROR_FAILURE;
    }
    data = std::make_unique<uint8_t[]>(propertySize);
    layout = reinterpret_cast<AudioChannelLayout*>(data.get());
    size = propertySize;

    if (property == kAudioFormatProperty_ChannelLayoutForBitmap) {
      status = AudioFormatGetProperty(property, sizeof(UInt32),
                                      &layout->mChannelBitmap, &size, layout);
    } else {
      status = AudioFormatGetProperty(property, sizeof(AudioChannelLayoutTag),
                                      &tag, &size, layout);
    }
    if (status || size != propertySize) {
      LOG("Couldn't get channel layout property ({}:{})", FourCC2Str(property),
          FourCC2Str(status));
      return NS_ERROR_FAILURE;
    }
    // We have retrieved the channel layout from the tag or bitmap.
    // We can now directly use the channel descriptions.
    layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
  }

  if (layout->mNumberChannelDescriptions != mOutputFormat.mChannelsPerFrame) {
    LOG("Not matching the original channel number");
    return NS_ERROR_FAILURE;
  }

  AutoTArray<AudioConfig::Channel, 8> channels;
  channels.SetLength(layout->mNumberChannelDescriptions);
  for (uint32_t i = 0; i < layout->mNumberChannelDescriptions; i++) {
    AudioChannelLabel id = layout->mChannelDescriptions[i].mChannelLabel;
    AudioConfig::Channel channel = ConvertChannelLabel(id);
    channels[i] = channel;
  }
  mChannelLayout = std::make_unique<AudioConfig::ChannelLayout>(
      mOutputFormat.mChannelsPerFrame, channels.Elements());
  return NS_OK;
}

MediaResult AppleATDecoder::SetupDecoder(MediaRawData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  static const uint32_t MAX_FRAMES = 2;

  bool isADTS =
      ADTS::FrameHeader::MatchesSync(Span{aSample->Data(), aSample->Size()});

  if (isADTS) {
    ADTS::FrameParser parser;
    if (!parser.Parse(0, aSample->Data(), aSample->Data() + aSample->Size())) {
      LOG("ADTS frame parsing error");
      return NS_ERROR_NOT_INITIALIZED;
    }

    AudioCodecSpecificBinaryBlob blob;
    ADTS::InitAudioSpecificConfig(parser.FirstFrame(), blob.mBinaryBlob);
    mConfig.mCodecSpecificConfig = AudioCodecSpecificVariant{std::move(blob)};
    mConfig.mProfile = mConfig.mExtendedProfile =
        parser.FirstFrame().Header().mObjectType;
    mIsADTS = true;

    if (mFormatID == kAudioFormatMPEG4AAC &&
        mConfig.mExtendedProfile == AUDIO_OBJECT_TYPE_USAC) {
      LOG("Detected xHE-AAC profile 42 (USAC), switching to "
          "kAudioFormatMPEGD_USAC");
      mFormatID = kAudioFormatMPEGD_USAC;
    }
  }

  if (mFormatID == kAudioFormatMPEG4AAC && mConfig.mExtendedProfile == 2 &&
      mParsedFramesForAACMagicCookie < MAX_FRAMES) {
    LOG("Attempting to get implicit AAC magic cookie");
    // Check for implicit SBR signalling if stream is AAC-LC
    // This will provide us with an updated magic cookie for use with
    // GetInputAudioDescription.
    if (NS_SUCCEEDED(GetImplicitAACMagicCookie(aSample)) &&
        !mMagicCookie.Length() && !isADTS) {
      // nothing found yet, will try again later
      LOG("Getting implicit AAC magic cookie failed");
      mParsedFramesForAACMagicCookie++;
      LOG("Not initialized -- need magic cookie");
      return NS_ERROR_NOT_INITIALIZED;
    }
    // An error occurred, fallback to using default stream description
  }

  LOG("Initializing Apple AudioToolbox decoder");

  // Should we try and use magic cookie data from the AAC data? We do this if
  // - We have an AAC config &
  // - We do not aleady have magic cookie data.
  // Otherwise we just use the existing cookie (which may be empty).
  bool shouldUseAacMagicCookie =
      mConfig.mCodecSpecificConfig.is<AacCodecSpecificData>() &&
      mMagicCookie.IsEmpty();

  nsTArray<uint8_t>& magicCookie =
      shouldUseAacMagicCookie
          ? *mConfig.mCodecSpecificConfig.as<AacCodecSpecificData>()
                 .mEsDescriptorBinaryBlob
          : mMagicCookie;
  AudioStreamBasicDescription inputFormat;
  PodZero(&inputFormat);

  MediaResult rv = GetInputAudioDescription(inputFormat, magicCookie);
  if (NS_FAILED(rv)) {
    LOG("GetInputAudioDescription failure");
    return rv;
  }
  if (inputFormat.mChannelsPerFrame == 0 ||
      inputFormat.mChannelsPerFrame > AudioInfo::MAX_CHANNEL_COUNT ||
      inputFormat.mSampleRate < 1 ||
      inputFormat.mSampleRate > AudioInfo::MAX_RATE) {
    LOG("Unsupported input format: {} channels at {} Hz",
        inputFormat.mChannelsPerFrame, inputFormat.mSampleRate);
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        RESULT_DETAIL("Unsupported input format: %u channels at %f Hz",
                      inputFormat.mChannelsPerFrame, inputFormat.mSampleRate));
  }
  // Fill in the output format manually.
  PodZero(&mOutputFormat);
  mOutputFormat.mFormatID = kAudioFormatLinearPCM;
  mOutputFormat.mSampleRate = inputFormat.mSampleRate;
  mOutputFormat.mChannelsPerFrame = inputFormat.mChannelsPerFrame;
  mOutputFormat.mBitsPerChannel = 32;
  mOutputFormat.mFormatFlags = kLinearPCMFormatFlagIsFloat | 0;
  // Set up the decoder so it gives us one sample per frame
  mOutputFormat.mFramesPerPacket = 1;
  mOutputFormat.mBytesPerPacket = mOutputFormat.mBytesPerFrame =
      mOutputFormat.mChannelsPerFrame * mOutputFormat.mBitsPerChannel / 8;

  OSStatus status =
      AudioConverterNew(&inputFormat, &mOutputFormat, &mConverter);
  if (status) {
    LOG("Error {} constructing AudioConverter", int(status));
    mConverter = nullptr;
    return MediaResult(
        NS_ERROR_FAILURE,
        RESULT_DETAIL("Error constructing AudioConverter:%d", int32_t(status)));
  }

  if (magicCookie.Length() && mFormatID == kAudioFormatMPEG4AAC) {
    status = AudioConverterSetProperty(
        mConverter, kAudioConverterDecompressionMagicCookie,
        magicCookie.Length(), magicCookie.Elements());
    if (status) {
      LOG("Error setting AudioConverter AAC cookie:{}", int32_t(status));
      ProcessShutdown();
      return MediaResult(
          NS_ERROR_FAILURE,
          RESULT_DETAIL("Error setting AudioConverter AAC cookie:%d",
                        int32_t(status)));
    }
  } else if (magicCookie.Length() && mFormatID == kAudioFormatMPEGD_USAC) {
    auto maybeEsdsData = CreateEsds(magicCookie);
    if (maybeEsdsData.isErr()) {
      return MediaResult(NS_ERROR_FAILURE,
                         RESULT_DETAIL("Couldn't create ESDS data"));
    }
    nsTArray<uint8_t> esdsData = maybeEsdsData.unwrap();
    status = AudioConverterSetProperty(
        mConverter, kAudioConverterDecompressionMagicCookie,
        magicCookie.Length(), magicCookie.Elements());
    if (status) {
      LOG("AudioConvertSetProperty failed: {}", int32_t(status));
      return MediaResult(NS_ERROR_FAILURE,
                         RESULT_DETAIL("AudioConverterSetProperty failed: %d",
                                       int32_t(status)));
    }
  }

  if (NS_FAILED(SetupChannelLayout())) {
    NS_WARNING("Couldn't retrieve channel layout, will use default layout");
  }

  if (mFormatID == kAudioFormatMPEG4AAC &&
      mConfig.mExtendedProfile == AUDIO_OBJECT_TYPE_USAC) {
    const Float32 kDefaultLoudness = -16.0;
    status = AudioConverterSetProperty(
        mConverter, kAudioCodecPropertyProgramTargetLevel,
        sizeof(kDefaultLoudness), &kDefaultLoudness);
    if (status != noErr) {
      LOG("AudioConverterSetProperty() failed to set loudness: {}",
          int(status));
      // Non-fatal error, continue
    }

    // Dynamic range control setting isn't in the SDK yet
    // https://developer.apple.com/documentation/http-live-streaming/providing-metadata-for-xhe-aac-video-soundtracks
    // Values: none=0, night=1, noisy=2, limited=3
    const UInt32 kDefaultEffectType = 3;
    status = AudioConverterSetProperty(mConverter, kDynamicRangeControlProperty,
                                       sizeof(kDefaultEffectType),
                                       &kDefaultEffectType);
    if (status != noErr) {
      LOG("AudioConverterSetProperty() failed to set DRC effect type: {}",
          int(status));
      // Non-fatal error, continue
    }
  }

  return NS_OK;
}

static void _MetadataCallback(void* aAppleATDecoder, AudioFileStreamID aStream,
                              AudioFileStreamPropertyID aProperty,
                              UInt32* aFlags) {
  AppleATDecoder* decoder = static_cast<AppleATDecoder*>(aAppleATDecoder);
  MOZ_RELEASE_ASSERT(decoder->mThread->IsOnCurrentThread());

  LOG("MetadataCallback receiving: '{}'", FourCC2Str(aProperty));
  if (aProperty == kAudioFileStreamProperty_MagicCookieData) {
    UInt32 size;
    Boolean writeable;
    OSStatus rv =
        AudioFileStreamGetPropertyInfo(aStream, aProperty, &size, &writeable);
    if (rv) {
      LOG("Couldn't get property info for '{}' ({})", FourCC2Str(aProperty),
          FourCC2Str(rv));
      decoder->mFileStreamError = true;
      return;
    }
    auto data = std::make_unique<uint8_t[]>(size);
    rv = AudioFileStreamGetProperty(aStream, aProperty, &size, data.get());
    if (rv) {
      LOG("Couldn't get property '{}' ({})", FourCC2Str(aProperty),
          FourCC2Str(rv));
      decoder->mFileStreamError = true;
      return;
    }
    decoder->mMagicCookie.AppendElements(data.get(), size);
  }
}

static void _SampleCallback(void* aSBR, UInt32 aNumBytes, UInt32 aNumPackets,
                            const void* aData,
                            AudioStreamPacketDescription* aPackets) {}

nsresult AppleATDecoder::GetImplicitAACMagicCookie(MediaRawData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  bool isADTS =
      ADTS::FrameHeader::MatchesSync(Span{aSample->Data(), aSample->Size()});

  RefPtr<MediaRawData> adtssample = aSample;

  if (!isADTS) {
    // Prepend ADTS header to AAC audio.
    adtssample = aSample->Clone();
    if (!adtssample) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    auto frequency_index = ADTS::GetFrequencyIndex(mConfig.mRate);

    if (frequency_index.isErr()) {
      LOG("{} isn't a valid rate for AAC", mConfig.mRate);
      return NS_ERROR_FAILURE;
    }

    // Arbitrarily pick main profile if not specified
    int profile = mConfig.mProfile ? mConfig.mProfile : 1;
    bool rv = ADTS::ConvertSample(mConfig.mChannels, frequency_index.unwrap(),
                                  profile, adtssample);
    if (!rv) {
      LOG("Failed to apply ADTS header");
      return NS_ERROR_FAILURE;
    }
  }
  if (!mStream) {
    OSStatus rv = AudioFileStreamOpen(this, _MetadataCallback, _SampleCallback,
                                      kAudioFileAAC_ADTSType, &mStream);
    if (rv) {
      LOG("Couldn't open AudioFileStream");
      return NS_ERROR_FAILURE;
    }
  }

  OSStatus status = AudioFileStreamParseBytes(
      mStream, adtssample->Size(), adtssample->Data(), 0 /* discontinuity */);
  if (status) {
    LOG("Couldn't parse sample");
  }

  if (status || mFileStreamError || mMagicCookie.Length()) {
    // We have decoded a magic cookie or an error occurred as such
    // we won't need the stream any longer.
    AudioFileStreamClose(mStream);
    mStream = nullptr;
  }

  return (mFileStreamError || status) ? NS_ERROR_FAILURE : NS_OK;
}

}  // namespace mozilla

#undef LOG
