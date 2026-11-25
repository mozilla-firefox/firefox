/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidDataEncoder.h"

#include "AnnexB.h"
#include "H264.h"
#include "ImageContainer.h"
#include "ImageConversion.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Logging.h"
#include "nsThreadUtils.h"

namespace mozilla {

extern LazyLogModule sPEMLog;
#define AND_ENC_LOG(arg, ...)                                               \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Debug,                            \
              "AndroidDataEncoder({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)
#define AND_ENC_LOGE(arg, ...)                                              \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Error,                            \
              "AndroidDataEncoder({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)

#define REJECT_IF_ERROR()                                                \
  do {                                                                   \
    if (mError) {                                                        \
      auto error = mError.value();                                       \
      mError.reset();                                                    \
      return EncodePromise::CreateAndReject(std::move(error), __func__); \
    }                                                                    \
  } while (0)

RefPtr<MediaDataEncoder::InitPromise> AndroidDataEncoder::Init() {
  // Sanity-check the input size for Android software encoder fails to do it.
  if (mConfig.mSize.width == 0 || mConfig.mSize.height == 0) {
    return InitPromise::CreateAndReject(NS_ERROR_ILLEGAL_VALUE, __func__);
  }

  return InvokeAsync(mTaskQueue, this, __func__,
                     &AndroidDataEncoder::ProcessInit);
}

const char* AndroidEncoderMimeType(CodecType aCodec) {
  switch (aCodec) {
    case CodecType::H264:
      return "video/avc";
    case CodecType::VP8:
      return "video/x-vnd.on2.vp8";
    case CodecType::VP9:
      return "video/x-vnd.on2.vp9";
    default:
      return "";
  }
}

static int32_t ToAndroidAVCProfile(H264_PROFILE aProfile) {
  using CodecProfileLevel = java::sdk::MediaCodecInfo::CodecProfileLevel;
  switch (aProfile) {
    case H264_PROFILE_BASE:
      return CodecProfileLevel::AVCProfileBaseline;
    case H264_PROFILE_MAIN:
      return CodecProfileLevel::AVCProfileMain;
    case H264_PROFILE_EXTENDED:
      return CodecProfileLevel::AVCProfileExtended;
    case H264_PROFILE_HIGH:
      return CodecProfileLevel::AVCProfileHigh;
    case H264_PROFILE_UNKNOWN:
      break;
  }
  return 0;
}

static int32_t ToAndroidAVCLevel(H264_LEVEL aLevel) {
  using CodecProfileLevel = java::sdk::MediaCodecInfo::CodecProfileLevel;
  switch (aLevel) {
    case H264_LEVEL::H264_LEVEL_1:
      return CodecProfileLevel::AVCLevel1;
    case H264_LEVEL::H264_LEVEL_1_1:
      // H264_LEVEL_1_b has the same value as H264_LEVEL_1_1, while
      // AVCLevel1b and AVCLevel11 differ. Since we can't tell the
      // difference, we just ignore the 1_b case.
      return CodecProfileLevel::AVCLevel11;
    case H264_LEVEL::H264_LEVEL_1_2:
      return CodecProfileLevel::AVCLevel12;
    case H264_LEVEL::H264_LEVEL_1_3:
      return CodecProfileLevel::AVCLevel13;
    case H264_LEVEL::H264_LEVEL_2:
      return CodecProfileLevel::AVCLevel2;
    case H264_LEVEL::H264_LEVEL_2_1:
      return CodecProfileLevel::AVCLevel21;
    case H264_LEVEL::H264_LEVEL_2_2:
      return CodecProfileLevel::AVCLevel22;
    case H264_LEVEL::H264_LEVEL_3:
      return CodecProfileLevel::AVCLevel3;
    case H264_LEVEL::H264_LEVEL_3_1:
      return CodecProfileLevel::AVCLevel31;
    case H264_LEVEL::H264_LEVEL_3_2:
      return CodecProfileLevel::AVCLevel32;
    case H264_LEVEL::H264_LEVEL_4:
      return CodecProfileLevel::AVCLevel4;
    case H264_LEVEL::H264_LEVEL_4_1:
      return CodecProfileLevel::AVCLevel41;
    case H264_LEVEL::H264_LEVEL_4_2:
      return CodecProfileLevel::AVCLevel42;
    case H264_LEVEL::H264_LEVEL_5:
      return CodecProfileLevel::AVCLevel5;
    case H264_LEVEL::H264_LEVEL_5_1:
      return CodecProfileLevel::AVCLevel51;
    case H264_LEVEL::H264_LEVEL_5_2:
      return CodecProfileLevel::AVCLevel52;
    case H264_LEVEL::H264_LEVEL_6:
      return CodecProfileLevel::AVCLevel6;
    case H264_LEVEL::H264_LEVEL_6_1:
      return CodecProfileLevel::AVCLevel61;
    case H264_LEVEL::H264_LEVEL_6_2:
      return CodecProfileLevel::AVCLevel62;
  }
  return 0;
}

using FormatResult = Result<java::sdk::MediaFormat::LocalRef, MediaResult>;

FormatResult ToMediaFormat(const EncoderConfig& aConfig) {
  nsresult rv = NS_OK;
  java::sdk::MediaFormat::LocalRef format;
  rv = java::sdk::MediaFormat::CreateVideoFormat(
      AndroidEncoderMimeType(aConfig.mCodec), aConfig.mSize.width,
      aConfig.mSize.height, &format);
  NS_ENSURE_SUCCESS(
      rv, FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                   "fail to create Java MediaFormat object")));

  rv =
      format->SetInteger(java::sdk::MediaFormat::KEY_BITRATE_MODE, 2 /* CBR */);
  NS_ENSURE_SUCCESS(rv, FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                                 "fail to set bitrate mode")));

  rv = format->SetInteger(java::sdk::MediaFormat::KEY_BIT_RATE,
                          AssertedCast<int>(aConfig.mBitrate));
  NS_ENSURE_SUCCESS(rv, FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                                 "fail to set bitrate")));

  // COLOR_FormatYUV420SemiPlanar(NV12) is the most widely supported
  // format.
  rv = format->SetInteger(java::sdk::MediaFormat::KEY_COLOR_FORMAT, 0x15);
  NS_ENSURE_SUCCESS(rv, FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                                 "fail to set color format")));

  rv = format->SetInteger(java::sdk::MediaFormat::KEY_FRAME_RATE,
                          aConfig.mFramerate);
  NS_ENSURE_SUCCESS(rv, FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                                 "fail to set frame rate")));

  // Ensure interval >= 1. A negative value means no key frames are
  // requested after the first frame. A zero value means a stream
  // containing all key frames is requested.
  int32_t intervalInSec = AssertedCast<int32_t>(
      std::max<size_t>(1, aConfig.mKeyframeInterval / aConfig.mFramerate));
  rv = format->SetInteger(java::sdk::MediaFormat::KEY_I_FRAME_INTERVAL,
                          intervalInSec);
  NS_ENSURE_SUCCESS(rv,
                    FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                             "fail to set I-frame interval")));

  if (aConfig.mCodec == CodecType::H264 &&
      aConfig.mCodecSpecific.is<H264Specific>()) {
    const auto& h264 = aConfig.mCodecSpecific.as<H264Specific>();
    if (int32_t profile = ToAndroidAVCProfile(h264.mProfile)) {
      rv = format->SetInteger(java::sdk::MediaFormat::KEY_PROFILE, profile);
      NS_ENSURE_SUCCESS(rv,
                        FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                                 "fail to set profile")));
    }
    if (int32_t level = ToAndroidAVCLevel(h264.mLevel)) {
      rv = format->SetInteger(java::sdk::MediaFormat::KEY_LEVEL, level);
      NS_ENSURE_SUCCESS(rv,
                        FormatResult(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                                 "fail to set level")));
    }
  }

  return format;
}

RefPtr<MediaDataEncoder::InitPromise> AndroidDataEncoder::ProcessInit() {
  AssertOnTaskQueue();
  MOZ_ASSERT(!mJavaEncoder);

  java::sdk::MediaCodec::BufferInfo::LocalRef bufferInfo;
  if (NS_FAILED(java::sdk::MediaCodec::BufferInfo::New(&bufferInfo)) ||
      !bufferInfo) {
    return InitPromise::CreateAndReject(NS_ERROR_OUT_OF_MEMORY, __func__);
  }
  mInputBufferInfo = bufferInfo;

  FormatResult result = ToMediaFormat(mConfig);
  if (result.isErr()) {
    return InitPromise::CreateAndReject(result.unwrapErr(), __func__);
  }
  mFormat = result.unwrap();

  // Register native methods.
  JavaCallbacksSupport::Init();

  mJavaCallbacks = java::CodecProxy::NativeCallbacks::New();
  if (!mJavaCallbacks) {
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    "cannot create Java callback object"),
        __func__);
  }
  JavaCallbacksSupport::AttachNative(mJavaCallbacks,
                                     std::make_unique<CallbacksSupport>(this));

  mJavaEncoder = java::CodecProxy::Create(true /* encoder */, mFormat, nullptr,
                                          mJavaCallbacks, u""_ns);
  if (!mJavaEncoder) {
    return InitPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                    "cannot create Java encoder object"),
        __func__);
  }

  mIsHardwareAccelerated = mJavaEncoder->IsHardwareAccelerated();
  mDrainState = DrainState::DRAINABLE;

  return InitPromise::CreateAndResolve(true, __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> AndroidDataEncoder::Encode(
    const MediaData* aSample) {
  RefPtr<AndroidDataEncoder> self = this;
  MOZ_ASSERT(aSample != nullptr);

  return InvokeAsync(
      mTaskQueue, __func__,
      [self, sample = RefPtr<MediaData>(const_cast<MediaData*>(aSample))]() {
        return self->ProcessEncode({sample});
      });
}

// TODO(Bug 1984936): For realtime mode, resolve the promise after the first
// sample's result is available, then continue processing remaining samples.
// This allows the caller to keep submitting new samples while the encoder
// handles pending ones.
RefPtr<MediaDataEncoder::EncodePromise> AndroidDataEncoder::Encode(
    nsTArray<RefPtr<MediaData>>&& aSamples) {
  RefPtr<AndroidDataEncoder> self = this;
  MOZ_ASSERT(!aSamples.IsEmpty());

  return InvokeAsync(mTaskQueue, __func__,
                     [self, samples = std::move(aSamples)]() mutable {
                       return self->ProcessEncode(std::move(samples));
                     });
}

static decltype(auto) CeilingOfHalf(decltype(gfx::IntSize::width) aValue) {
  MOZ_ASSERT(aValue >= 0);
  return aValue - aValue / 2;
}

// Convert the sample into the NV12 layout the codec was configured for,
// scaling it when its size differs from aDestSize.
static Result<jni::ByteBuffer::LocalRef, MediaResult> ConvertToNV12Buffer(
    RefPtr<const VideoData>& aSample, RefPtr<MediaByteBuffer>& aYUVBuffer,
    const gfx::IntSize& aDestSize, int aStride, int aYPlaneHeight) {
  if (aDestSize.IsEmpty()) {
    return Err(MediaResult(NS_ERROR_INVALID_ARG, "destination size is empty"));
  }

  // If we have a stride or height passed in from the Codec we need to use
  // those.
  const int yStride = aStride != 0 ? aStride : aDestSize.width;
  const int sliceHeight = aYPlaneHeight != 0 ? aYPlaneHeight : aDestSize.height;
  if (yStride < aDestSize.width || sliceHeight < aDestSize.height) {
    return Err(MediaResult(NS_ERROR_INVALID_ARG,
                           "stride or slice height is too small"));
  }

  const int chromaWidth = CeilingOfHalf(aDestSize.width);
  const int chromaHeight = CeilingOfHalf(aDestSize.height);
  CheckedInt<size_t> yLength = CheckedInt<size_t>(yStride) * sliceHeight;
  CheckedInt<size_t> length = yLength +
                              CheckedInt<size_t>(yStride) * (chromaHeight - 1) +
                              CheckedInt<size_t>(chromaWidth) * 2;
  if (!length.isValid()) {
    return Err(MediaResult(NS_ERROR_INVALID_ARG,
                           "calculated buffer length is invalid"));
  }

  if (!aYUVBuffer || aYUVBuffer->Capacity() < length.value()) {
    aYUVBuffer = MakeRefPtr<MediaByteBuffer>(length.value());
  }
  aYUVBuffer->SetLength(length.value());

  nsresult r = ConvertToNV12(aSample->mImage, aYUVBuffer->Elements(), yStride,
                             aYUVBuffer->Elements() + yLength.value(), yStride,
                             aDestSize);
  if (NS_FAILED(r)) {
    return Err(MediaResult(r, "conversion to NV12 failed"));
  }

  jni::ByteBuffer::LocalRef buffer =
      jni::ByteBuffer::New(aYUVBuffer->Elements(), aYUVBuffer->Length());
  if (!buffer) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                           "failed to create Java byte buffer"));
  }
  return buffer;
}

RefPtr<MediaDataEncoder::EncodePromise> AndroidDataEncoder::ProcessEncode(
    nsTArray<RefPtr<MediaData>>&& aSamples) {
  AssertOnTaskQueue();

  REJECT_IF_ERROR();

  // TODO(Bug 1984936): Looping here for large batches is inefficient, as it can
  // take excessive shared memory and file descriptors due to passing both input
  // and output buffers between the content and media codec processes.
  for (auto& s : aSamples) {
    RefPtr<const VideoData> sample(s->As<const VideoData>());
    MOZ_ASSERT(sample);

    mInputSampleDuration = s->mDuration;

    // Bug 1789846: Check with the Encoder if MediaCodec has a stride or height
    // value to use.
    auto r = ConvertToNV12Buffer(sample, mYUVBuffer, mConfig.mSize,
                                 mJavaEncoder->GetInputFormatStride(),
                                 mJavaEncoder->GetInputFormatYPlaneHeight());
    if (r.isErr()) {
      return EncodePromise::CreateAndReject(r.unwrapErr(), __func__);
    }
    jni::ByteBuffer::LocalRef buffer = r.unwrap();
    MOZ_ASSERT(buffer);
    if (s->mKeyframe) {
      mInputBufferInfo->Set(0, AssertedCast<int32_t>(mYUVBuffer->Length()),
                            s->mTime.ToMicroseconds(),
                            java::sdk::MediaCodec::BUFFER_FLAG_SYNC_FRAME);
    } else {
      mInputBufferInfo->Set(0, AssertedCast<int32_t>(mYUVBuffer->Length()),
                            s->mTime.ToMicroseconds(), 0);
    }

    mJavaEncoder->Input(buffer, mInputBufferInfo, nullptr);
  }

  if (mEncodedData.Length() > 0) {
    EncodedData pending = std::move(mEncodedData);
    return EncodePromise::CreateAndResolve(std::move(pending), __func__);
  }
  return EncodePromise::CreateAndResolve(EncodedData(), __func__);
}

class AutoRelease final {
 public:
  AutoRelease(java::CodecProxy::Param aEncoder, java::Sample::Param aSample)
      : mEncoder(aEncoder), mSample(aSample) {}

  ~AutoRelease() { mEncoder->ReleaseOutput(mSample, false); }

 private:
  java::CodecProxy::GlobalRef mEncoder;
  java::Sample::GlobalRef mSample;
};

static bool IsAVCC(EncoderConfig::CodecSpecific& aCodecSpecific) {
  return aCodecSpecific.is<H264Specific>() &&
         aCodecSpecific.as<H264Specific>().mFormat == H264BitStreamFormat::AVC;
}

static RefPtr<MediaByteBuffer> ExtractCodecConfig(
    java::SampleBuffer::Param aBuffer, const int32_t aOffset,
    const int32_t aSize, const bool aAsAVCC) {
  auto config = MakeRefPtr<MediaByteBuffer>(aSize);
  config->SetLength(aSize);
  NS_ENSURE_SUCCESS(
      aBuffer->NativeCopy(reinterpret_cast<jlong>(config->Elements()),
                          config->Length(), aOffset, aSize),
      nullptr);
  if (!aAsAVCC) {
    return config;
  }
  return AnnexB::ExtractExtraDataForAVCC(*config);
}

void AndroidDataEncoder::ProcessOutput(
    java::Sample::GlobalRef&& aSample,
    java::SampleBuffer::GlobalRef&& aBuffer) {
  if (!mTaskQueue->IsCurrentThreadIn()) {
    nsresult rv =
        mTaskQueue->Dispatch(NewRunnableMethod<java::Sample::GlobalRef&&,
                                               java::SampleBuffer::GlobalRef&&>(
            "AndroidDataEncoder::ProcessOutput", this,
            &AndroidDataEncoder::ProcessOutput, std::move(aSample),
            std::move(aBuffer)));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
    return;
  }
  AssertOnTaskQueue();

  if (!mJavaEncoder) {
    return;
  }

  AutoRelease releaseSample(mJavaEncoder, aSample);

  java::sdk::MediaCodec::BufferInfo::LocalRef info = aSample->Info();
  MOZ_ASSERT(info);

  int32_t flags;
  bool ok = NS_SUCCEEDED(info->Flags(&flags));
  bool isEOS =
      ok && !!(flags & java::sdk::MediaCodec::BUFFER_FLAG_END_OF_STREAM);
  if (isEOS) {
    mDrainState = DrainState::DRAINED;
  }

  int32_t offset;
  ok &= NS_SUCCEEDED(info->Offset(&offset));

  int32_t size;
  ok &= NS_SUCCEEDED(info->Size(&size));

  int64_t presentationTimeUs;
  ok &= NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));

  if (!ok) {
    Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                      "fail to get output buffer info"_ns));
    return;
  }

  if (size > 0) {
    if ((flags & java::sdk::MediaCodec::BUFFER_FLAG_CODEC_CONFIG) != 0) {
      auto configData = ExtractCodecConfig(aBuffer, offset, size,
                                           IsAVCC(mConfig.mCodecSpecific));
      if (configData) {
        mConfigData = std::move(configData);
      } else {
        MOZ_ASSERT_UNREACHABLE("Bad config data!");
        Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                          "fail to extract codec config"_ns));
      }
      return;
    }
    RefPtr<MediaRawData> output;
    if (mConfig.mCodec == CodecType::H264) {
      output = GetOutputDataH264(
          aBuffer, offset, size,
          !!(flags & java::sdk::MediaCodec::BUFFER_FLAG_KEY_FRAME));
    } else {
      output = GetOutputData(
          aBuffer, offset, size,
          !!(flags & java::sdk::MediaCodec::BUFFER_FLAG_KEY_FRAME));
    }
    if (!output) {
      Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        "fail to copy sample buffer"_ns));
      return;
    }
    output->mEOS = isEOS;
    output->mTime = media::TimeUnit::FromMicroseconds(presentationTimeUs);
    output->mDuration = mInputSampleDuration;
    mEncodedData.AppendElement(std::move(output));
  }

  if (!mDrainPromise.IsEmpty()) {
    EncodedData pending = std::move(mEncodedData);
    mDrainPromise.Resolve(std::move(pending), __func__);
  }
}

RefPtr<MediaRawData> AndroidDataEncoder::GetOutputData(
    java::SampleBuffer::Param aBuffer, const int32_t aOffset,
    const int32_t aSize, const bool aIsKeyFrame) {
  // Copy frame data from Java buffer.
  auto output = MakeRefPtr<MediaRawData>();
  UniquePtr<MediaRawDataWriter> writer(output->CreateWriter());
  if (!writer->SetSize(aSize)) {
    AND_ENC_LOGE("fail to allocate output buffer: size={}", aSize);
    return nullptr;
  }

  NS_ENSURE_SUCCESS(aBuffer->NativeCopy(reinterpret_cast<jlong>(writer->Data()),
                                        writer->Size(), aOffset, aSize),
                    nullptr);
  output->mKeyframe = aIsKeyFrame;

  return output;
}

// AVC/H.264 frame can be in avcC or Annex B and needs extra conversion steps.
RefPtr<MediaRawData> AndroidDataEncoder::GetOutputDataH264(
    java::SampleBuffer::Param aBuffer, const int32_t aOffset,
    const int32_t aSize, const bool aIsKeyFrame) {
  auto output = MakeRefPtr<MediaRawData>();

  size_t prependSize = 0;
  RefPtr<MediaByteBuffer> avccHeader;
  bool asAVCC = IsAVCC(mConfig.mCodecSpecific);
  if (aIsKeyFrame && mConfigData) {
    if (asAVCC) {
      avccHeader = mConfigData;
    } else {
      prependSize = mConfigData->Length();
    }
  }

  UniquePtr<MediaRawDataWriter> writer(output->CreateWriter());
  if (!writer->SetSize(prependSize + aSize)) {
    AND_ENC_LOGE("fail to allocate output buffer");
    return nullptr;
  }

  if (prependSize > 0) {
    PodCopy(writer->Data(), mConfigData->Elements(), prependSize);
  }

  NS_ENSURE_SUCCESS(
      aBuffer->NativeCopy(reinterpret_cast<jlong>(writer->Data() + prependSize),
                          writer->Size() - prependSize, aOffset, aSize),
      nullptr);

  if (asAVCC && !AnnexB::ConvertSampleToAVCC(output, avccHeader)) {
    AND_ENC_LOGE("fail to convert annex-b sample to AVCC");
    return nullptr;
  }

  output->mKeyframe = aIsKeyFrame;

  return output;
}

RefPtr<MediaDataEncoder::EncodePromise> AndroidDataEncoder::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &AndroidDataEncoder::ProcessDrain);
}

RefPtr<MediaDataEncoder::EncodePromise> AndroidDataEncoder::ProcessDrain() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mJavaEncoder);
  MOZ_ASSERT(mDrainPromise.IsEmpty());

  REJECT_IF_ERROR();

  switch (mDrainState) {
    case DrainState::DRAINABLE:
      mInputBufferInfo->Set(0, 0, -1,
                            java::sdk::MediaCodec::BUFFER_FLAG_END_OF_STREAM);
      mJavaEncoder->Input(nullptr, mInputBufferInfo, nullptr);
      mDrainState = DrainState::DRAINING;
      [[fallthrough]];
    case DrainState::DRAINING:
      if (mEncodedData.IsEmpty()) {
        return mDrainPromise.Ensure(__func__);  // Pending promise.
      }
      [[fallthrough]];
    case DrainState::DRAINED:
      if (mEncodedData.Length() > 0) {
        EncodedData pending = std::move(mEncodedData);
        return EncodePromise::CreateAndResolve(std::move(pending), __func__);
      } else {
        return EncodePromise::CreateAndResolve(EncodedData(), __func__);
      }
  }
}

RefPtr<ShutdownPromise> AndroidDataEncoder::Shutdown() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &AndroidDataEncoder::ProcessShutdown);
}

RefPtr<ShutdownPromise> AndroidDataEncoder::ProcessShutdown() {
  AssertOnTaskQueue();
  if (mJavaEncoder) {
    mJavaEncoder->Release();
    mJavaEncoder = nullptr;
  }

  if (mJavaCallbacks) {
    JavaCallbacksSupport::GetNative(mJavaCallbacks)->Cancel();
    JavaCallbacksSupport::DisposeNative(mJavaCallbacks);
    mJavaCallbacks = nullptr;
  }

  mFormat = nullptr;

  return ShutdownPromise::CreateAndResolve(true, __func__);
}

RefPtr<GenericPromise> AndroidDataEncoder::SetBitrate(uint32_t aBitsPerSec) {
  RefPtr<AndroidDataEncoder> self(this);
  return InvokeAsync(mTaskQueue, __func__, [self, aBitsPerSec]() {
    self->mJavaEncoder->SetBitrate(AssertedCast<int>(aBitsPerSec));
    return GenericPromise::CreateAndResolve(true, __func__);
  });
}

void AndroidDataEncoder::Error(const MediaResult& aError) {
  if (!mTaskQueue->IsCurrentThreadIn()) {
    nsresult rv = mTaskQueue->Dispatch(NewRunnableMethod<MediaResult>(
        "AndroidDataEncoder::Error", this, &AndroidDataEncoder::Error, aError));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
    return;
  }
  AssertOnTaskQueue();

  mError = Some(aError);
  if (!mDrainPromise.IsEmpty()) {
    mDrainPromise.Reject(aError, __func__);
  }
}

void AndroidDataEncoder::CallbacksSupport::HandleInput(int64_t aTimestamp,
                                                       bool aProcessed) {}

void AndroidDataEncoder::CallbacksSupport::HandleOutput(
    java::Sample::Param aSample, java::SampleBuffer::Param aBuffer) {
  MutexAutoLock lock(mMutex);
  if (mEncoder) {
    mEncoder->ProcessOutput(aSample, aBuffer);
  }
}

void AndroidDataEncoder::CallbacksSupport::HandleOutputFormatChanged(
    java::sdk::MediaFormat::Param aFormat) {}

void AndroidDataEncoder::CallbacksSupport::HandleError(
    const MediaResult& aError) {
  MutexAutoLock lock(mMutex);
  if (mEncoder) {
    mEncoder->Error(aError);
  }
}

}  // namespace mozilla

#undef AND_ENC_LOG
#undef AND_ENC_LOGE
