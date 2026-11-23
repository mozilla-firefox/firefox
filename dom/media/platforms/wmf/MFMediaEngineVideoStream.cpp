/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MFMediaEngineVideoStream.h"

#include "MFMediaEngineUtils.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/layers/DcompSurfaceImage.h"

namespace mozilla {

#define LOG(msg, ...)                                        \
  MOZ_LOG_FMT(gMFMediaEngineLog, LogLevel::Debug,            \
              "MFMediaStream={} ({}), " msg, fmt::ptr(this), \
              this->GetDescriptionName().get(), ##__VA_ARGS__)

#define LOGV(msg, ...)                                       \
  MOZ_LOG_FMT(gMFMediaEngineLog, LogLevel::Verbose,          \
              "MFMediaStream={} ({}), " msg, fmt::ptr(this), \
              this->GetDescriptionName().get(), ##__VA_ARGS__)

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

/* static */
MFMediaEngineVideoStream* MFMediaEngineVideoStream::Create(
    uint64_t aStreamId, const TrackInfo& aInfo, bool aIsEncryptedCustomInit,
    MFMediaSource* aParentSource) {
  MFMediaEngineVideoStream* stream;
  MOZ_ASSERT(aInfo.IsVideo());
  if (FAILED(MakeAndInitialize<MFMediaEngineVideoStream>(
          &stream, aStreamId, aInfo, aIsEncryptedCustomInit, aParentSource))) {
    return nullptr;
  }
  stream->mStreamType =
      GetStreamTypeFromMimeType(aInfo.GetAsVideoInfo()->mMimeType);
  MOZ_ASSERT(StreamTypeIsVideo(stream->mStreamType));
  stream->mHasReceivedInitialCreateDecoderConfig = false;
  stream->SetDCompSurfaceHandle(INVALID_HANDLE_VALUE, gfx::IntSize{});
  return stream;
}

void MFMediaEngineVideoStream::SetKnowsCompositor(
    layers::KnowsCompositor* aKnowsCompositor) {
  ComPtr<MFMediaEngineVideoStream> self = this;
  (void)mTaskQueue->Dispatch(NS_NewRunnableFunction(
      "MFMediaEngineStream::SetKnowsCompositor",
      [self, knowCompositor = RefPtr<layers::KnowsCompositor>{aKnowsCompositor},
       this]() {
        mKnowsCompositor = knowCompositor;
        LOG("Set SetKnowsCompositor={}", fmt::ptr(mKnowsCompositor.get()));
        ResolvePendingPromisesIfNeeded();
      }));
}

void MFMediaEngineVideoStream::SetFrameServerMode() {
  ComPtr<MFMediaEngineVideoStream> self = this;
  (void)mTaskQueue->Dispatch(NS_NewRunnableFunction(
      "MFMediaEngineVideoStream::SetFrameServerMode", [self, this]() {
        mFrameServerMode = true;
        LOG("Set frame server mode");
      }));
}

void MFMediaEngineVideoStream::SetDCompSurfaceHandle(HANDLE aDCompSurfaceHandle,
                                                     gfx::IntSize aDisplay) {
  ComPtr<MFMediaEngineVideoStream> self = this;
  (void)mTaskQueue->Dispatch(NS_NewRunnableFunction(
      "MFMediaEngineStream::SetDCompSurfaceHandle",
      [self, aDCompSurfaceHandle, aDisplay, this]() {
        if (mDCompSurfaceHandle == aDCompSurfaceHandle) {
          return;
        }
        mDCompSurfaceHandle = aDCompSurfaceHandle;
        mNeedRecreateImage = true;
        {
          MutexAutoLock lock(mMutex);
          if (aDCompSurfaceHandle != INVALID_HANDLE_VALUE &&
              aDisplay != mDisplay) {
            LOG("Update display [{}x{}] -> [{}x{}]", mDisplay.Width(),
                mDisplay.Height(), aDisplay.Width(), aDisplay.Height());
            mDisplay = aDisplay;
          }
        }
        LOG("Set DCompSurfaceHandle, handle={}", fmt::ptr(mDCompSurfaceHandle));
        ResolvePendingPromisesIfNeeded();
      }));
}

HRESULT MFMediaEngineVideoStream::CreateMediaType(const TrackInfo& aInfo,
                                                  IMFMediaType** aMediaType) {
  auto& videoInfo = *aInfo.GetAsVideoInfo();
  mIsEncrypted = videoInfo.mCrypto.IsEncrypted();

  GUID subType = VideoMimeTypeToMediaFoundationSubtype(videoInfo.mMimeType);
  NS_ENSURE_TRUE(subType != GUID_NULL, MF_E_TOPO_CODEC_NOT_FOUND);

  // https://docs.microsoft.com/en-us/windows/win32/medfound/media-type-attributes
  ComPtr<IMFMediaType> mediaType;
  RETURN_IF_FAILED(wmf::MFCreateMediaType(&mediaType));
  RETURN_IF_FAILED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
  RETURN_IF_FAILED(mediaType->SetGUID(MF_MT_SUBTYPE, subType));

  const auto& image = videoInfo.mImage;
  UINT32 imageWidth = image.Width();
  UINT32 imageHeight = image.Height();
  RETURN_IF_FAILED(MFSetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE,
                                      imageWidth, imageHeight));

  UINT32 displayWidth = videoInfo.mDisplay.Width();
  UINT32 displayHeight = videoInfo.mDisplay.Height();
  LOG("Media type: image=[{}x{}], display=[{}x{}], mime={}, encrypted={}, "
      "clearlead={}",
      imageWidth, imageHeight, displayWidth, displayHeight,
      videoInfo.mMimeType.get(), mIsEncrypted,
      mIsEncryptedCustomInit && !mIsEncrypted);
  {
    MutexAutoLock lock(mMutex);
    mDisplay = videoInfo.mDisplay;
  }
  // PAR = DAR / SAR = (DW / DH) / (SW / SH) = (DW * SH) / (DH * SW)
  RETURN_IF_FAILED(MFSetAttributeRatio(
      mediaType.Get(), MF_MT_PIXEL_ASPECT_RATIO, displayWidth * imageHeight,
      displayHeight * imageWidth));

  // https://docs.microsoft.com/en-us/windows/win32/api/mfobjects/ns-mfobjects-mfoffset
  // The value of the MFOffset number is value + (fract / 65536.0f).
  static const auto ToMFOffset = [](float aValue) {
    MFOffset offset;
    offset.value = static_cast<short>(aValue);
    offset.fract = static_cast<WORD>(65536 * (aValue - offset.value));
    return offset;
  };
  MFVideoArea area;
  area.OffsetX = ToMFOffset(videoInfo.ImageRect().x);
  area.OffsetY = ToMFOffset(videoInfo.ImageRect().y);
  area.Area = {(LONG)imageWidth, (LONG)imageHeight};
  RETURN_IF_FAILED(mediaType->SetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&area,
                                      sizeof(area)));

  // https://docs.microsoft.com/en-us/windows/win32/api/mfapi/ne-mfapi-mfvideorotationformat
  static const auto ToMFVideoRotationFormat = [](VideoRotation aRotation) {
    using Rotation = VideoRotation;
    switch (aRotation) {
      case Rotation::kDegree_0:
        return MFVideoRotationFormat_0;
      case Rotation::kDegree_90:
        return MFVideoRotationFormat_90;
      case Rotation::kDegree_180:
        return MFVideoRotationFormat_180;
      default:
        MOZ_ASSERT(aRotation == Rotation::kDegree_270);
        return MFVideoRotationFormat_270;
    }
  };
  const auto rotation = ToMFVideoRotationFormat(videoInfo.mRotation);
  RETURN_IF_FAILED(mediaType->SetUINT32(MF_MT_VIDEO_ROTATION, rotation));

  const auto transFunc = ToMFVideoTransFunc(videoInfo.mTransferFunction);
  RETURN_IF_FAILED(mediaType->SetUINT32(MF_MT_TRANSFER_FUNCTION, transFunc));

  static const auto ToMFVideoPrimaries =
      [](const Maybe<gfx::YUVColorSpace>& aColorSpace) {
        using YUVColorSpace = gfx::YUVColorSpace;
        if (!aColorSpace) {
          return MFVideoPrimaries_Unknown;
        }
        // https://docs.microsoft.com/en-us/windows/win32/api/mfobjects/ne-mfobjects-mfvideoprimaries
        switch (*aColorSpace) {
          case YUVColorSpace::BT601:
            return MFVideoPrimaries_Unknown;
          case YUVColorSpace::BT709:
            return MFVideoPrimaries_BT709;
          case YUVColorSpace::BT2020:
            return MFVideoPrimaries_BT2020;
          case YUVColorSpace::Identity:
            return MFVideoPrimaries_BT709;
          default:
            return MFVideoPrimaries_Unknown;
        }
      };
  const auto videoPrimaries = ToMFVideoPrimaries(videoInfo.mColorSpace);
  RETURN_IF_FAILED(mediaType->SetUINT32(MF_MT_VIDEO_PRIMARIES, videoPrimaries));

  LOG("Created video type, subtype={}, image=[{}x{}], display=[{}x{}], "
      "rotation={}, tranFuns={}, primaries={}, encrypted={}",
      GUIDToStr(subType), imageWidth, imageHeight, displayWidth, displayHeight,
      MFVideoRotationFormatToStr(rotation),
      MFVideoTransferFunctionToStr(transFunc),
      MFVideoPrimariesToStr(videoPrimaries), mIsEncrypted);
  if (IsEncrypted()) {
    ComPtr<IMFMediaType> protectedMediaType;
    RETURN_IF_FAILED(wmf::MFWrapMediaType(mediaType.Get(),
                                          MFMediaType_Protected, subType,
                                          protectedMediaType.GetAddressOf()));
    LOG("Wrap MFMediaType_Video into MFMediaType_Protected");
    *aMediaType = protectedMediaType.Detach();
  } else {
    *aMediaType = mediaType.Detach();
  }
  return S_OK;
}

bool MFMediaEngineVideoStream::HasEnoughRawData() const {
  // If more than this much raw video is queued, we'll hold off request more
  // video.
  return mRawDataQueueForFeedingEngine.PreciseDuration() >=
         StaticPrefs::media_wmf_media_engine_raw_data_threshold_video();
}

bool MFMediaEngineVideoStream::IsDCompImageReady() {
  AssertOnTaskQueue();
  if (!mDCompSurfaceHandle || mDCompSurfaceHandle == INVALID_HANDLE_VALUE) {
    LOGV("Can't create image without a valid dcomp surface handle");
    return false;
  }

  if (!mKnowsCompositor) {
    LOGV("Can't create image without the knows compositor");
    return false;
  }

  if (!mDcompSurfaceImage || mNeedRecreateImage) {
    MutexAutoLock lock(mMutex);
    // DirectComposition only supports RGBA. We use DXGI_FORMAT_B8G8R8A8_UNORM
    // as a default because we can't know what format the dcomp surface is.
    // https://docs.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionsurfacefactory-createsurface
    mDcompSurfaceImage = new layers::DcompSurfaceImage(
        mDCompSurfaceHandle, mDisplay, gfx::SurfaceFormat::B8G8R8A8,
        mKnowsCompositor);
    mNeedRecreateImage = false;
    LOG("Created dcomp surface image, handle={}, size=[{},{}]",
        fmt::ptr(mDCompSurfaceHandle), mDisplay.Width(), mDisplay.Height());
  }
  return true;
}

RefPtr<MediaDataDecoder::DecodePromise> MFMediaEngineVideoStream::OutputData(
    RefPtr<MediaRawData> aSample) {
  if (IsShutdown()) {
    mVideoDecodeBeforeDcompPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                                  __func__);
    return MediaDataDecoder::DecodePromise::CreateAndReject(
        MediaResult(NS_ERROR_FAILURE,
                    RESULT_DETAIL("MFMediaEngineVideoStream is shutdown")),
        __func__);
  }
  AssertOnTaskQueue();
  NotifyNewData(aSample);
  MediaDataDecoder::DecodedData outputs;
  if (RefPtr<MediaData> outputData = OutputDataInternal()) {
    outputs.AppendElement(outputData);
    LOGV("Output data [{},{}]", outputData->mTime.ToMicroseconds(),
         outputData->GetEndTime().ToMicroseconds());
  }
  if (ShouldDelayVideoDecodeBeforeDcompReady()) {
    LOG("Dcomp isn't ready and we already have enough video data. We will send "
        "them back together at one when Dcomp is ready");
    return mVideoDecodeBeforeDcompPromise.Ensure(__func__);
  }
  return MediaDataDecoder::DecodePromise::CreateAndResolve(std::move(outputs),
                                                           __func__);
}

already_AddRefed<MediaData> MFMediaEngineVideoStream::OutputDataInternal() {
  AssertOnTaskQueue();
  if (mRawDataQueueForGeneratingOutput.GetSize() == 0) {
    return nullptr;
  }
  if (mFrameServerMode) {
    RefPtr<MediaRawData> discarded =
        mRawDataQueueForGeneratingOutput.PopFront();
    return nullptr;
  }
  if (!IsDCompImageReady()) {
    return nullptr;
  }
  RefPtr<MediaRawData> sample = mRawDataQueueForGeneratingOutput.PopFront();
  RefPtr<VideoData> output;
  {
    MutexAutoLock lock(mMutex);
    output = VideoData::CreateFromImage(
        mDisplay, sample->mOffset, sample->mTime, sample->mDuration,
        mDcompSurfaceImage, sample->mKeyframe, sample->mTimecode);
  }
  return output.forget();
}

RefPtr<MediaDataDecoder::DecodePromise> MFMediaEngineVideoStream::Drain() {
  if (IsShutdown()) {
    mPendingDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    mVideoDecodeBeforeDcompPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                                  __func__);
    return MediaDataDecoder::DecodePromise::CreateAndReject(
        MediaResult(NS_ERROR_FAILURE,
                    RESULT_DETAIL("MFMediaEngineVideoStream is shutdown")),
        __func__);
  }
  AssertOnTaskQueue();
  MediaDataDecoder::DecodedData outputs;
  if (mFrameServerMode) {
    mRawDataQueueForGeneratingOutput.Reset();
    if (!mSampleRequestTokens.empty() &&
        mRawDataQueueForFeedingEngine.GetSize() == 0) {
      NotifyEndEvent();
    }
    return MediaDataDecoder::DecodePromise::CreateAndResolve(std::move(outputs),
                                                             __func__);
  }
  // Without pending output (e.g. right after a flush) there is nothing to wait
  // for, and sending the end event would end the engine's presentation.
  if (mRawDataQueueForGeneratingOutput.GetSize() > 0 && !IsDCompImageReady()) {
    LOGV("Waiting for dcomp image for draining");
    // A workaround for a special case where we have sent all input data to the
    // media engine, and waiting for an output. Sometime media engine would
    // never return the first frame to us, unless we notify it the end event,
    // which happens on the case where the video only contains one frame. If we
    // don't send end event to the media engine, the drain promise would be
    // pending forever.
    if (!mSampleRequestTokens.empty() &&
        mRawDataQueueForFeedingEngine.GetSize() == 0) {
      NotifyEndEvent();
    }
    return mPendingDrainPromise.Ensure(__func__);
  }
  return MFMediaEngineStream::Drain();
}

RefPtr<MediaDataDecoder::FlushPromise> MFMediaEngineVideoStream::Flush() {
  if (IsShutdown()) {
    mPendingDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    mVideoDecodeBeforeDcompPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                                  __func__);
    return MediaDataDecoder::FlushPromise::CreateAndReject(
        MediaResult(NS_ERROR_FAILURE,
                    RESULT_DETAIL("MFMediaEngineVideoStream is shutdown")),
        __func__);
  }
  AssertOnTaskQueue();
  auto promise = MFMediaEngineStream::Flush();
  mPendingDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mVideoDecodeBeforeDcompPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                                __func__);
  return promise;
}

void MFMediaEngineVideoStream::ResolvePendingPromisesIfNeeded() {
  AssertOnTaskQueue();
  if (!IsDCompImageReady()) {
    return;
  }

  // Resolve decoding promise first, then drain promise
  if (!mVideoDecodeBeforeDcompPromise.IsEmpty()) {
    MediaDataDecoder::DecodedData outputs;
    while (RefPtr<MediaData> outputData = OutputDataInternal()) {
      outputs.AppendElement(outputData);
      LOGV("Output data [{},{}]", outputData->mTime.ToMicroseconds(),
           outputData->GetEndTime().ToMicroseconds());
    }
    mVideoDecodeBeforeDcompPromise.Resolve(std::move(outputs), __func__);
    LOG("Resolved video decode before Dcomp promise");
  }

  // This drain promise could return no data, if all data has been processed in
  // the decoding promise.
  if (!mPendingDrainPromise.IsEmpty()) {
    MediaDataDecoder::DecodedData outputs;
    while (RefPtr<MediaData> outputData = OutputDataInternal()) {
      outputs.AppendElement(outputData);
      LOGV("Output data [{},{}]", outputData->mTime.ToMicroseconds(),
           outputData->GetEndTime().ToMicroseconds());
    }
    mPendingDrainPromise.Resolve(std::move(outputs), __func__);
    LOG("Resolved pending drain promise");
  }
}

MediaDataDecoder::ConversionRequired MFMediaEngineVideoStream::NeedsConversion()
    const {
  return mStreamType == WMFStreamType::H264 ||
                 mStreamType == WMFStreamType::HEVC
             ? MediaDataDecoder::ConversionRequired::kNeedAnnexB
             : MediaDataDecoder::ConversionRequired::kNeedNone;
}

void MFMediaEngineVideoStream::SetConfig(const TrackInfo& aConfig) {
  MOZ_ASSERT(aConfig.IsVideo());
  ComPtr<MFMediaEngineStream> self = this;
  (void)mTaskQueue->Dispatch(
      NS_NewRunnableFunction("MFMediaEngineStream::SetConfig",
                             [self, info = *aConfig.GetAsVideoInfo(), this]() {
                               if (mHasReceivedInitialCreateDecoderConfig) {
                                 // Here indicating a new config for video,
                                 // which is triggered by the media change
                                 // monitor, so we need to update the config.
                                 UpdateConfig(info);
                               }
                               mHasReceivedInitialCreateDecoderConfig = true;
                             }));
}

void MFMediaEngineVideoStream::UpdateConfig(const VideoInfo& aInfo) {
  AssertOnTaskQueue();
  // Disable explicit format change event for H264/HEVC to allow switching to
  // the new stream without a full re-create, which will be much faster. This is
  // also due to the fact that the MFT decoder can handle some format changes
  // without a format change event. For format changes that the MFT decoder
  // cannot support (e.g. codec change), the playback will fail later with
  // MF_E_INVALIDMEDIATYPE (0xC00D36B4).
  if (mStreamType == WMFStreamType::H264 ||
      mStreamType == WMFStreamType::HEVC) {
    return;
  }

  LOG("Video config changed, will update stream descriptor");
  PROFILER_MARKER_TEXT("VideoConfigChange", MEDIA_PLAYBACK, {},
                       nsPrintfCString("stream=%s, id=%" PRIu64,
                                       GetDescriptionName().get(), mStreamId));
  ComPtr<IMFMediaType> mediaType;
  RETURN_VOID_IF_FAILED(CreateMediaType(aInfo, mediaType.GetAddressOf()));
  RETURN_VOID_IF_FAILED(GenerateStreamDescriptor(mediaType));
  RETURN_VOID_IF_FAILED(mMediaEventQueue->QueueEventParamUnk(
      MEStreamFormatChanged, GUID_NULL, S_OK, mediaType.Get()));
}

void MFMediaEngineVideoStream::ShutdownCleanUpOnTaskQueue() {
  AssertOnTaskQueue();
  mPendingDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mVideoDecodeBeforeDcompPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED,
                                                __func__);
  // mDCompSurfaceHandle is owned by DcompSurfaceTexture, which closes it in
  // its destructor. Do not call CloseHandle here.
}

void MFMediaEngineVideoStream::SendRequestSampleEvent(bool aIsEnough) {
  AssertOnTaskQueue();
  MFMediaEngineStream::SendRequestSampleEvent(aIsEnough);
  // We need more data to be sent in, we should resolve the promise to allow
  // more input data to be sent.
  if (!aIsEnough && !mVideoDecodeBeforeDcompPromise.IsEmpty()) {
    LOG("Resolved pending input promise to allow more input be sent in");
    mVideoDecodeBeforeDcompPromise.Resolve(MediaDataDecoder::DecodedData{},
                                           __func__);
  }
}

bool MFMediaEngineVideoStream::IsEnded() const {
  AssertOnTaskQueue();
  // If a video only contains one frame, the media engine won't return a decoded
  // frame before we tell it the track is already ended. However, due to the
  // constraint of our media pipeline, the format reader won't notify EOS until
  // the draining finishes, which causes a deadlock. Therefore, we would
  // consider having pending drain promise as a sign of EOS as well, in order to
  // get the decoded frame and revolve the drain promise.
  return (mReceivedEOS || !mPendingDrainPromise.IsEmpty()) &&
         mRawDataQueueForFeedingEngine.GetSize() == 0;
}

bool MFMediaEngineVideoStream::IsEncrypted() const {
  return mIsEncrypted || mIsEncryptedCustomInit;
}

bool MFMediaEngineVideoStream::ShouldDelayVideoDecodeBeforeDcompReady() {
  return !mFrameServerMode && HasEnoughRawData() && !IsDCompImageReady();
}

nsCString MFMediaEngineVideoStream::GetCodecName() const {
  switch (mStreamType) {
    case WMFStreamType::H264:
      return "h264"_ns;
    case WMFStreamType::VP8:
      return "vp8"_ns;
    case WMFStreamType::VP9:
      return "vp9"_ns;
    case WMFStreamType::AV1:
      return "av1"_ns;
    case WMFStreamType::HEVC:
      return "hevc"_ns;
    default:
      return "unknown"_ns;
  };
}

#undef LOG
#undef LOGV

}  // namespace mozilla
