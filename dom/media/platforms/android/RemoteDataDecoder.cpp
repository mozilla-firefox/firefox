/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteDataDecoder.h"

#include <jni.h>

#include "AndroidBridge.h"
#include "AndroidBuild.h"
#include "AndroidDecoderModule.h"
#include "EMEDecoderModule.h"
#include "GLImages.h"
#include "JavaCallbacksSupport.h"
#include "MediaCodec.h"
#include "MediaData.h"
#include "MediaInfo.h"
#include "PerformanceRecorder.h"
#include "SimpleMap.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#include "XiphExtradata.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/fallible.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/java/CodecProxyWrappers.h"
#include "mozilla/java/GeckoSurfaceWrappers.h"
#include "mozilla/java/SampleBufferWrappers.h"
#include "mozilla/java/SampleWrappers.h"
#include "mozilla/java/SurfaceAllocatorWrappers.h"
#include "nsPromiseFlatString.h"
#include "nsThreadUtils.h"
#include "prlog.h"

#undef LOG
#define LOG(arg, ...)                                                      \
  MOZ_LOG_FMT(sAndroidDecoderModuleLog, mozilla::LogLevel::Debug,          \
              "RemoteDataDecoder({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)

using namespace mozilla;
using namespace mozilla::gl;
using media::TimeUnit;

namespace mozilla {

// Hold a reference to the output buffer until we're ready to release it back to
// the Java codec (for rendering or not).
class RenderOrReleaseOutput {
 public:
  RenderOrReleaseOutput(java::CodecProxy::Param aCodec,
                        java::Sample::Param aSample)
      : mMutex("AndroidRenderOrReleaseOutput"),
        mCodec(aCodec),
        mSample(aSample) {}

  virtual ~RenderOrReleaseOutput() { ReleaseOutput(false); }

 protected:
  void ReleaseOutput(bool aToRender) {
    MutexAutoLock lock(mMutex);
    if (mCodec && mSample) {
      mCodec->ReleaseOutput(mSample, aToRender);
      mCodec = nullptr;
      mSample = nullptr;
    }
  }

 private:
  Mutex mMutex;
  java::CodecProxy::GlobalRef mCodec MOZ_GUARDED_BY(mMutex);
  java::Sample::GlobalRef mSample MOZ_GUARDED_BY(mMutex);
};

static bool areSmpte432ColorPrimariesBuggy() {
  if (jni::GetAPIVersion() >= 34) {
    const auto socManufacturer =
        java::sdk::Build::SOC_MANUFACTURER()->ToString();
    if (socManufacturer.EqualsASCII("Google")) {
      return true;
    }
  }
  return false;
}

static bool areBT709ColorPrimariesMisreported() {
  if (jni::GetAPIVersion() >= 33) {
    const auto socModel = java::sdk::Build::SOC_MODEL()->ToString();
    if (socModel.EqualsASCII("Tensor") || socModel.EqualsASCII("GS201")) {
      return true;
    }
  }
  return false;
}

// Receives the Java callbacks on binder threads and forwards them to the
// decoder on mTaskQueue. mDecoder is only accessed on mTaskQueue.
class RemoteDataDecoder::CallbacksSupportBase : public JavaCallbacksSupport {
 public:
  explicit CallbacksSupportBase(RemoteDataDecoder* aDecoder)
      : mTaskQueue(aDecoder->mTaskQueue), mDecoder(aDecoder) {}

  // Stops forwarding callbacks to the decoder, which is shutting down.
  void Detach() {
    MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
    Cancel();
    mDecoder = nullptr;
  }

 protected:
  template <typename Decoder, typename Func>
  void RunOnTaskQueue(const char* aName, Func&& aFunc) {
    if (mTaskQueue->IsOnCurrentThread()) {
      if (mDecoder) {
        aFunc(static_cast<Decoder*>(mDecoder));
      }
      return;
    }
    nsresult rv = mTaskQueue->Dispatch(NS_NewRunnableFunction(
        aName, [self = this, func = std::forward<Func>(aFunc)]() mutable {
          if (self->mDecoder) {
            func(static_cast<Decoder*>(self->mDecoder));
          }
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

 private:
  const RefPtr<TaskQueue> mTaskQueue;
  RemoteDataDecoder* mDecoder;
};

class RemoteVideoDecoder final : public RemoteDataDecoder {
 public:
  // Render the output to the surface when the frame is sent
  // to compositor, or release it if not presented.
  class CompositeListener
      : private RenderOrReleaseOutput,
        public layers::SurfaceTextureImage::SetCurrentCallback {
   public:
    CompositeListener(java::CodecProxy::Param aCodec,
                      java::Sample::Param aSample)
        : RenderOrReleaseOutput(aCodec, aSample) {}

    bool operator()(bool aRender) override {
      ReleaseOutput(aRender);
      return true;
    }
  };

  class InputInfo {
   public:
    InputInfo() = default;

    InputInfo(const int64_t aDurationUs, const gfx::IntSize& aImageSize,
              const gfx::IntSize& aDisplaySize)
        : mDurationUs(aDurationUs),
          mImageSize(aImageSize),
          mDisplaySize(aDisplaySize) {}

    int64_t mDurationUs = {};
    gfx::IntSize mImageSize = {};
    gfx::IntSize mDisplaySize = {};
  };

  class CallbacksSupport final : public CallbacksSupportBase {
   public:
    explicit CallbacksSupport(RemoteVideoDecoder* aDecoder)
        : CallbacksSupportBase(aDecoder) {}

    void HandleInput(int64_t aTimestamp, bool aProcessed) override {
      RunOnTaskQueue<RemoteVideoDecoder>(
          "RemoteVideoDecoder::HandleInput",
          [aTimestamp, aProcessed](RemoteVideoDecoder* aDecoder) {
            aDecoder->UpdateInputStatus(aTimestamp, aProcessed);
          });
    }

    void HandleOutput(java::Sample::Param aSample,
                      java::SampleBuffer::Param aBuffer) override {
      MOZ_ASSERT(!aBuffer, "Video sample should be bufferless");
      RunOnTaskQueue<RemoteVideoDecoder>(
          "RemoteVideoDecoder::HandleOutput",
          [sample = java::Sample::GlobalRef(aSample)](
              RemoteVideoDecoder* aDecoder) mutable {
            aDecoder->ProcessOutput(std::move(sample));
          });
    }

    void HandleOutputFormatChanged(
        java::sdk::MediaFormat::Param aFormat) override {
      int32_t colorFormat = 0;
      aFormat->GetInteger(java::sdk::MediaFormat::KEY_COLOR_FORMAT,
                          &colorFormat);
      if (colorFormat == 0) {
        HandleError(
            MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        RESULT_DETAIL("Invalid color format:%d", colorFormat)));
        return;
      }

      Maybe<int32_t> colorRange;
      {
        int32_t range = 0;
        if (NS_SUCCEEDED(aFormat->GetInteger(
                java::sdk::MediaFormat::KEY_COLOR_RANGE, &range))) {
          colorRange.emplace(range);
        }
      }

      Maybe<int32_t> colorSpace;
      {
        int32_t space = 0;
        if (NS_SUCCEEDED(aFormat->GetInteger(
                java::sdk::MediaFormat::KEY_COLOR_STANDARD, &space))) {
          colorSpace.emplace(space);
        }
      }

      RunOnTaskQueue<RemoteVideoDecoder>(
          "RemoteVideoDecoder::HandleOutputFormatChanged",
          [colorFormat, colorRange, colorSpace](RemoteVideoDecoder* aDecoder) {
            aDecoder->ProcessOutputFormatChange(colorFormat, colorRange,
                                                colorSpace);
          });
    }

    void HandleError(const MediaResult& aError) override {
      RunOnTaskQueue<RemoteVideoDecoder>(
          "RemoteVideoDecoder::HandleError",
          [aError](RemoteVideoDecoder* aDecoder) { aDecoder->Error(aError); });
    }

    friend class RemoteDataDecoder;
  };

  RemoteVideoDecoder(const VideoInfo& aConfig,
                     java::sdk::MediaFormat::Param aFormat,
                     const nsString& aDrmStubId, Maybe<TrackingId> aTrackingId)
      : RemoteDataDecoder(MediaData::Type::VIDEO_DATA, aConfig.mMimeType,
                          aFormat, aDrmStubId),
        mConfig(aConfig),
        mTrackingId(std::move(aTrackingId)) {}

  ~RemoteVideoDecoder() {
    ProcessShutdown();
    if (mSurface) {
      java::SurfaceAllocator::DisposeSurface(mSurface);
    }
  }

  MediaResult ProcessConstruct() override {
    AssertOnTaskQueue();
    mSurface =
        java::GeckoSurface::LocalRef(java::SurfaceAllocator::AcquireSurface(
            mConfig.mImage.width, mConfig.mImage.height, false));
    if (!mSurface) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    }

    mSurfaceHandle = mSurface->GetHandle();

    // Register native methods.
    JavaCallbacksSupport::Init();

    mJavaCallbacks = java::CodecProxy::NativeCallbacks::New();
    if (!mJavaCallbacks) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    }
    JavaCallbacksSupport::AttachNative(
        mJavaCallbacks, std::make_unique<CallbacksSupport>(this));

    mJavaDecoder = java::CodecProxy::Create(
        false,  // false indicates to create a decoder and true denotes encoder
        mFormat, mSurface, mJavaCallbacks, mDrmStubId);
    if (mJavaDecoder == nullptr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    }
    return NS_OK;
  }

  MediaResult ProcessInit() override {
    AssertOnTaskQueue();
    MediaResult rv = RemoteDataDecoder::ProcessInit();
    if (NS_FAILED(rv)) {
      return rv;
    }

    mIsCodecSupportAdaptivePlayback =
        mJavaDecoder->IsAdaptivePlaybackSupported();
    mIsHardwareAccelerated = mJavaDecoder->IsHardwareAccelerated();

    mMediaInfoFlag = MediaInfoFlag::None;
    mMediaInfoFlag |= mIsHardwareAccelerated ? MediaInfoFlag::HardwareDecoding
                                             : MediaInfoFlag::SoftwareDecoding;
    if (mMimeType.EqualsLiteral("video/mp4") ||
        mMimeType.EqualsLiteral("video/avc")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_H264;
    } else if (mMimeType.EqualsLiteral("video/vp8")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_VP8;
    } else if (mMimeType.EqualsLiteral("video/vp9")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_VP9;
    } else if (mMimeType.EqualsLiteral("video/av1")) {
      mMediaInfoFlag |= MediaInfoFlag::VIDEO_AV1;
    }
    return NS_OK;
  }

  RefPtr<MediaDataDecoder::FlushPromise> ProcessFlush() override {
    AssertOnTaskQueue();
    mInputInfos.Clear();
    mSeekTarget.reset();
    mLatestOutputTime.reset();
    mPerformanceRecorder.Record(std::numeric_limits<int64_t>::max());
    return RemoteDataDecoder::ProcessFlush();
  }

  nsCString GetCodecName() const override {
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_H264) {
      return "h264"_ns;
    }
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_VP8) {
      return "vp8"_ns;
    }
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_VP9) {
      return "vp9"_ns;
    }
    if (mMediaInfoFlag & MediaInfoFlag::VIDEO_AV1) {
      return "av1"_ns;
    }
    return "unknown"_ns;
  }

  RefPtr<MediaDataDecoder::DecodePromise> ProcessDecode(
      MediaRawData* aSample) override {
    AssertOnTaskQueue();

    if (NeedsNewDecoder()) {
      return DecodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER,
                                            __func__);
    }

    const VideoInfo* config =
        aSample->mTrackInfo ? aSample->mTrackInfo->GetAsVideoInfo() : &mConfig;
    MOZ_ASSERT(config);

    mTrackingId.apply([&](const auto& aId) {
      MediaInfoFlag flag = mMediaInfoFlag;
      flag |= (aSample->mKeyframe ? MediaInfoFlag::KeyFrame
                                  : MediaInfoFlag::NonKeyFrame);
      mPerformanceRecorder.Start(aSample->mTime.ToMicroseconds(),
                                 "AndroidDecoder"_ns, aId, flag);
    });

    InputInfo info(aSample->mDuration.ToMicroseconds(), config->mImage,
                   config->mDisplay);
    mInputInfos.Insert(aSample->mTime.ToMicroseconds(), info);
    return RemoteDataDecoder::ProcessDecode(aSample);
  }

  bool SupportDecoderRecycling() const override {
    return mIsCodecSupportAdaptivePlayback;
  }

  void SetSeekThreshold(const TimeUnit& aTime) override {
    auto setter = [self = RefPtr{this}, aTime] {
      if (aTime.IsValid()) {
        self->mSeekTarget = Some(aTime);
      } else {
        self->mSeekTarget.reset();
      }
    };
    if (mTaskQueue->IsOnCurrentThread()) {
      setter();
    } else {
      nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
          "RemoteVideoDecoder::SetSeekThreshold", std::move(setter));
      nsresult rv = mTaskQueue->Dispatch(runnable.forget());
      MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
      (void)rv;
    }
  }

  bool IsUsefulData(const RefPtr<MediaData>& aSample) override {
    AssertOnTaskQueue();

    if (mLatestOutputTime && aSample->mTime < mLatestOutputTime.value()) {
      return false;
    }

    const TimeUnit endTime = aSample->GetEndTime();
    if (mSeekTarget && endTime <= mSeekTarget.value()) {
      return false;
    }

    mSeekTarget.reset();
    mLatestOutputTime = Some(endTime);
    return true;
  }

  bool IsHardwareAccelerated(nsACString& aFailureReason) const override {
    return mIsHardwareAccelerated;
  }

  ConversionRequired NeedsConversion() const override {
    return ConversionRequired::kNeedAnnexB;
  }

  Maybe<MediaDataDecoder::PropertyValue> GetDecodeProperty(
      MediaDataDecoder::PropertyName aName) const override {
    // Android has limited amount of output buffers. See Bug 794747.
    static constexpr uint32_t kNumOutputBuffers = 3;
    // SurfaceTexture can have only one current/renderable image at a time.
    // See Bug 1299068
    static constexpr uint32_t kNumCurrentImages = 1;
    switch (aName) {
      case PropertyName::MaxNumVideoBuffers:
        [[fallthrough]];
      case PropertyName::MinNumVideoBuffers:
        return Some(PropertyValue(kNumOutputBuffers));
      case PropertyName::MaxNumCurrentImages:
        return Some(PropertyValue(kNumCurrentImages));
      default:
        return MediaDataDecoder::GetDecodeProperty(aName);
    }
  }

 private:
  // Param and LocalRef are only valid for the duration of a JNI method call.
  // Use GlobalRef as the parameter type to keep the Java object referenced
  // until running.
  void ProcessOutput(java::Sample::GlobalRef&& aSample) {
    AssertOnTaskQueue();
    if (GetState() == State::SHUTDOWN) {
      aSample->Dispose();
      return;
    }

    UniquePtr<layers::SurfaceTextureImage::SetCurrentCallback> releaseSample(
        new CompositeListener(mJavaDecoder, aSample));

    // If our output surface has been released (due to the GPU process crashing)
    // then request a new decoder, which will in turn allocate a new
    // Surface. This is usually be handled by the Error() callback, but on some
    // devices (or at least on the emulator) the java decoder does not raise an
    // error when the Surface is released. So we raise this error here as well.
    if (NeedsNewDecoder()) {
      Error(MediaResult(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER,
                        RESULT_DETAIL("VideoCallBack::HandleOutput")));
      return;
    }

    java::sdk::MediaCodec::BufferInfo::LocalRef info = aSample->Info();
    MOZ_ASSERT(info);

    int32_t flags;
    bool ok = NS_SUCCEEDED(info->Flags(&flags));

    int32_t offset;
    ok &= NS_SUCCEEDED(info->Offset(&offset));

    int32_t size;
    ok &= NS_SUCCEEDED(info->Size(&size));

    int64_t presentationTimeUs;
    ok &= NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));

    if (!ok) {
      Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                        RESULT_DETAIL("VideoCallBack::HandleOutput")));
      return;
    }

    Maybe<InputInfo> inputInfo = mInputInfos.Take(presentationTimeUs);
    bool isEOS = !!(flags & java::sdk::MediaCodec::BUFFER_FLAG_END_OF_STREAM);
    if (!inputInfo && !isEOS) {
      LOG("No corresponding input");
      // Ignore output with no corresponding input.
      return;
    }

    LOG("flags={:x} size={} presentationTimeUs={}", flags, size,
        presentationTimeUs);
    if (inputInfo && (size > 0 || presentationTimeUs >= 0)) {
      bool forceBT709ColorSpace = false;
      // On certain devices SMPTE 432 color primaries are rendered incorrectly,
      // so we force BT709 to be used instead.
      // Color space 10 comes from the video in bug 1866020 and corresponds to
      // libstagefright's kColorStandardDCI_P3.
      // 65800 comes from the video in bug 1879720 and is vendor-specific.
      static bool isSmpte432Buggy = areSmpte432ColorPrimariesBuggy();
      if (isSmpte432Buggy &&
          (mColorSpace == Some(10) || mColorSpace == Some(65800))) {
        forceBT709ColorSpace = true;
      }

      // On certain devices the av1 decoder intermittently misreports some BT709
      // video frames as having BT609 color primaries. This results in a
      // flickering effect during playback whilst alternating between frames
      // which the GPU believes have different color spaces. To work around this
      // we force BT709 conversion to be used for all frames which the decoder
      // believes are BT601, as long as our demuxer has reported the color
      // primaries as BT709. See bug 1933055.
      static bool isBT709Misreported = areBT709ColorPrimariesMisreported();
      if (isBT709Misreported && mMediaInfoFlag & MediaInfoFlag::VIDEO_AV1 &&
          mConfig.mColorPrimaries == Some(gfx::ColorSpace2::BT709) &&
          // 4 = kColorStandardBT601_525
          mColorSpace == Some(4)) {
        forceBT709ColorSpace = true;
      }

      RefPtr<layers::Image> img = new layers::SurfaceTextureImage(
          mSurfaceHandle, inputInfo->mImageSize, false /* NOT continuous */,
          gl::OriginPos::BottomLeft, mConfig.HasAlpha(), forceBT709ColorSpace,
          /* aTransformOverride */ Nothing());
      img->AsSurfaceTextureImage()->RegisterSetCurrentCallback(
          std::move(releaseSample));

      RefPtr<VideoData> v = VideoData::CreateFromImage(
          inputInfo->mDisplaySize, offset,
          TimeUnit::FromMicroseconds(presentationTimeUs),
          TimeUnit::FromMicroseconds(inputInfo->mDurationUs), img.forget(),
          !!(flags & java::sdk::MediaCodec::BUFFER_FLAG_SYNC_FRAME),
          TimeUnit::FromMicroseconds(presentationTimeUs));

      mPerformanceRecorder.Record(presentationTimeUs, [&](DecodeStage& aStage) {
        using Cap = java::sdk::MediaCodecInfo::CodecCapabilities;
        using Fmt = java::sdk::MediaFormat;
        mColorFormat.apply([&](int32_t aFormat) {
          switch (aFormat) {
            case Cap::COLOR_Format32bitABGR8888:
            case Cap::COLOR_Format32bitARGB8888:
            case Cap::COLOR_Format32bitBGRA8888:
            case Cap::COLOR_FormatRGBAFlexible:
              aStage.SetImageFormat(DecodeStage::RGBA32);
              break;
            case Cap::COLOR_Format24bitBGR888:
            case Cap::COLOR_Format24bitRGB888:
            case Cap::COLOR_FormatRGBFlexible:
              aStage.SetImageFormat(DecodeStage::RGB24);
              break;
            case Cap::COLOR_FormatYUV411Planar:
            case Cap::COLOR_FormatYUV411PackedPlanar:
            case Cap::COLOR_FormatYUV420Planar:
            case Cap::COLOR_FormatYUV420PackedPlanar:
            case Cap::COLOR_FormatYUV420Flexible:
              aStage.SetImageFormat(DecodeStage::YUV420P);
              break;
            case Cap::COLOR_FormatYUV420SemiPlanar:
            case Cap::COLOR_FormatYUV420PackedSemiPlanar:
            case Cap::COLOR_QCOM_FormatYUV420SemiPlanar:
            case Cap::COLOR_TI_FormatYUV420PackedSemiPlanar:
              aStage.SetImageFormat(DecodeStage::NV12);
              break;
            case Cap::COLOR_FormatYCbYCr:
            case Cap::COLOR_FormatYCrYCb:
            case Cap::COLOR_FormatCbYCrY:
            case Cap::COLOR_FormatCrYCbY:
            case Cap::COLOR_FormatYUV422Planar:
            case Cap::COLOR_FormatYUV422PackedPlanar:
            case Cap::COLOR_FormatYUV422Flexible:
              aStage.SetImageFormat(DecodeStage::YUV422P);
              break;
            case Cap::COLOR_FormatYUV444Interleaved:
            case Cap::COLOR_FormatYUV444Flexible:
              aStage.SetImageFormat(DecodeStage::YUV444P);
              break;
            case Cap::COLOR_FormatSurface:
              aStage.SetImageFormat(DecodeStage::ANDROID_SURFACE);
              break;
            /* Added in API level 33
            case Cap::COLOR_FormatYUVP010:
              aStage.SetImageFormat(DecodeStage::P010);
              break;
            */
            default:
              NS_WARNING(nsPrintfCString("Unhandled color format %d (0x%08x)",
                                         aFormat, aFormat)
                             .get());
          }
        });
        mColorRange.apply([&](int32_t aRange) {
          switch (aRange) {
            case Fmt::COLOR_RANGE_FULL:
              aStage.SetColorRange(gfx::ColorRange::FULL);
              break;
            case Fmt::COLOR_RANGE_LIMITED:
              aStage.SetColorRange(gfx::ColorRange::LIMITED);
              break;
            default:
              NS_WARNING(nsPrintfCString("Unhandled color range %d (0x%08x)",
                                         aRange, aRange)
                             .get());
          }
        });
        mColorSpace.apply([&](int32_t aSpace) {
          switch (aSpace) {
            case Fmt::COLOR_STANDARD_BT2020:
              aStage.SetYUVColorSpace(gfx::YUVColorSpace::BT2020);
              break;
            case Fmt::COLOR_STANDARD_BT601_NTSC:
            case Fmt::COLOR_STANDARD_BT601_PAL:
              aStage.SetYUVColorSpace(gfx::YUVColorSpace::BT601);
              break;
            case Fmt::COLOR_STANDARD_BT709:
              aStage.SetYUVColorSpace(gfx::YUVColorSpace::BT709);
              break;
            default:
              NS_WARNING(nsPrintfCString("Unhandled color space %d (0x%08x)",
                                         aSpace, aSpace)
                             .get());
          }
        });
        aStage.SetResolution(v->mImage->GetSize().Width(),
                             v->mImage->GetSize().Height());
        aStage.SetStartTimeAndEndTime(v->mTime.ToMicroseconds(),
                                      v->GetEndTime().ToMicroseconds());
      });

      RemoteDataDecoder::UpdateOutputStatus(std::move(v));
    }

    if (isEOS) {
      DrainComplete();
    }
  }

  void ProcessOutputFormatChange(int32_t aColorFormat,
                                 Maybe<int32_t> aColorRange,
                                 Maybe<int32_t> aColorSpace) {
    AssertOnTaskQueue();

    mColorFormat = Some(aColorFormat);
    mColorRange = aColorRange;
    mColorSpace = aColorSpace;
  }

  bool NeedsNewDecoder() const override {
    return !mSurface || mSurface->IsReleased();
  }

  const VideoInfo mConfig;
  java::GeckoSurface::GlobalRef mSurface;
  AndroidSurfaceTextureHandle mSurfaceHandle{};
  // Can be accessed on any thread, but only written during init.
  bool mIsCodecSupportAdaptivePlayback = false;
  bool mIsHardwareAccelerated = false;
  // Only accessed on mTaskQueue.
  SimpleMap<int64_t, InputInfo, ThreadSafePolicy> mInputInfos;
  // Only accessed on mTaskQueue.
  Maybe<TimeUnit> mSeekTarget;
  Maybe<TimeUnit> mLatestOutputTime;
  Maybe<int32_t> mColorFormat;
  Maybe<int32_t> mColorRange;
  Maybe<int32_t> mColorSpace;
  // Only accessed on mTaskQueue.
  // Tracking id for the performance recorder.
  const Maybe<TrackingId> mTrackingId;
  // Can be accessed on any thread, but only written during init.
  // Pre-filled decode info used by the performance recorder.
  MediaInfoFlag mMediaInfoFlag = {};
  // Only accessed on mTaskQueue.
  // Records decode performance to the profiler.
  PerformanceRecorderMulti<DecodeStage> mPerformanceRecorder;
};

/* static */
nsTArray<RefPtr<MediaByteBuffer>> RemoteDataDecoder::GetAudioCodecSpecificData(
    const AudioInfo& aConfig) {
  nsTArray<RefPtr<MediaByteBuffer>> csd;
  const AudioCodecSpecificVariant& config = aConfig.mCodecSpecificConfig;

  if (config.is<FlacCodecSpecificData>()) {
    // MediaCodec expects the fLaC marker followed by the STREAMINFO block.
    static constexpr size_t kStreamInfoSize = 34;
    const MediaByteBuffer& streamInfo =
        *config.as<FlacCodecSpecificData>().mStreamInfoBinaryBlob;
    if (streamInfo.Length() < kStreamInfoSize) {
      return csd;
    }
    RefPtr<MediaByteBuffer> buffer = new MediaByteBuffer();
    if (streamInfo.Length() >= 4 && !memcmp(streamInfo.Elements(), "fLaC", 4)) {
      buffer->AppendElements(streamInfo);
    } else {
      static constexpr uint8_t kHeader[] = {'f',
                                            'L',
                                            'a',
                                            'C',
                                            0x80 /* last block, STREAMINFO */,
                                            0x00,
                                            0x00,
                                            kStreamInfoSize};
      buffer->AppendElements(kHeader, std::size(kHeader));
      buffer->AppendElements(streamInfo.Elements(), kStreamInfoSize);
    }
    csd.AppendElement(std::move(buffer));
    return csd;
  }

  if (config.is<VorbisCodecSpecificData>()) {
    // MediaCodec expects the identification header followed by the setup
    // header.
    MediaByteBuffer& headersBlob =
        *config.as<VorbisCodecSpecificData>().mHeadersBinaryBlob;
    AutoTArray<unsigned char*, 3> headers;
    AutoTArray<size_t, 3> headerLens;
    if (!XiphExtradataToHeaders(headers, headerLens, headersBlob.Elements(),
                                headersBlob.Length()) ||
        headers.Length() != 3) {
      return csd;
    }
    for (size_t i : {0, 2}) {
      RefPtr<MediaByteBuffer> buffer = new MediaByteBuffer();
      buffer->AppendElements(headers[i], headerLens[i]);
      csd.AppendElement(std::move(buffer));
    }
    return csd;
  }

  if (config.is<OpusCodecSpecificData>()) {
    // MediaCodec expects the identification header, followed by the codec
    // delay and the seek pre-roll in nanoseconds.
    static constexpr size_t kIdentificationHeaderMinSize = 19;
    static constexpr int64_t kSeekPreRollNs = 80000000;
    const MediaByteBuffer& header =
        *config.as<OpusCodecSpecificData>().mHeadersBinaryBlob;
    if (header.Length() < kIdentificationHeaderMinSize) {
      return csd;
    }
    RefPtr<MediaByteBuffer> headerBuffer = new MediaByteBuffer();
    headerBuffer->AppendElements(header);
    csd.AppendElement(std::move(headerBuffer));
    // Round the codec delay up so that MediaCodec's conversion back to frames,
    // which truncates, yields the exact pre-skip.
    const int64_t preSkip = LittleEndian::readUint16(header.Elements() + 10);
    const int64_t codecDelayNs = (preSkip * 1000000000 + 47999) / 48000;
    for (int64_t value : {codecDelayNs, kSeekPreRollNs}) {
      RefPtr<MediaByteBuffer> buffer = new MediaByteBuffer(sizeof(value));
      buffer->SetLength(sizeof(value));
      LittleEndian::writeInt64(buffer->Elements(), value);
      csd.AppendElement(std::move(buffer));
    }
    return csd;
  }

  // TODO(bug 1768564): implement further type checking for codec data.
  RefPtr<MediaByteBuffer> blob = ForceGetAudioCodecSpecificBlob(config);
  if (blob->Length() >= 2) {
    csd.AppendElement(std::move(blob));
  }
  return csd;
}

class RemoteAudioDecoder final : public RemoteDataDecoder {
 public:
  RemoteAudioDecoder(const AudioInfo& aConfig,
                     java::sdk::MediaFormat::Param aFormat,
                     const nsString& aDrmStubId)
      : RemoteDataDecoder(MediaData::Type::AUDIO_DATA, aConfig.mMimeType,
                          aFormat, aDrmStubId),
        mOutputChannels(AssertedCast<int32_t>(aConfig.mChannels)),
        mOutputSampleRate(AssertedCast<int32_t>(aConfig.mRate)) {
    JNIEnv* const env = jni::GetEnvForThread();

    bool formatHasCSD = false;
    NS_ENSURE_SUCCESS_VOID(aFormat->ContainsKey(u"csd-0"_ns, &formatHasCSD));
    if (formatHasCSD) {
      return;
    }

    mCodecSpecificData = GetAudioCodecSpecificData(aConfig);
    for (size_t i = 0; i < mCodecSpecificData.Length(); ++i) {
      jni::ByteBuffer::LocalRef buffer(env);
      buffer = jni::ByteBuffer::New(mCodecSpecificData[i]->Elements(),
                                    mCodecSpecificData[i]->Length());
      nsAutoString name(u"csd-"_ns);
      name.AppendInt(uint32_t(i));
      NS_ENSURE_SUCCESS_VOID(aFormat->SetByteBuffer(name, buffer));
    }
  }

  ~RemoteAudioDecoder() { ProcessShutdown(); }

  MediaResult ProcessConstruct() override {
    AssertOnTaskQueue();
    // Register native methods.
    JavaCallbacksSupport::Init();

    mJavaCallbacks = java::CodecProxy::NativeCallbacks::New();
    if (!mJavaCallbacks) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    }
    JavaCallbacksSupport::AttachNative(
        mJavaCallbacks, std::make_unique<CallbacksSupport>(this));

    mJavaDecoder = java::CodecProxy::Create(false, mFormat, nullptr,
                                            mJavaCallbacks, mDrmStubId);
    if (mJavaDecoder == nullptr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
    }

    return NS_OK;
  }

  nsCString GetCodecName() const override {
    if (mMimeType.EqualsLiteral("audio/mp4a-latm")) {
      return "aac"_ns;
    }
    return "unknown"_ns;
  }

  RefPtr<FlushPromise> ProcessFlush() override {
    AssertOnTaskQueue();
    mFirstDemuxedSampleTime.reset();
    return RemoteDataDecoder::ProcessFlush();
  }

  RefPtr<DecodePromise> ProcessDecode(MediaRawData* aSample) override {
    AssertOnTaskQueue();
    if (!mFirstDemuxedSampleTime) {
      MOZ_ASSERT(aSample->mTime.IsValid());
      mFirstDemuxedSampleTime.emplace(aSample->mTime);
    }
    return RemoteDataDecoder::ProcessDecode(aSample);
  }

 private:
  class CallbacksSupport final : public CallbacksSupportBase {
   public:
    explicit CallbacksSupport(RemoteAudioDecoder* aDecoder)
        : CallbacksSupportBase(aDecoder) {}

    void HandleInput(int64_t aTimestamp, bool aProcessed) override {
      RunOnTaskQueue<RemoteAudioDecoder>(
          "RemoteAudioDecoder::HandleInput",
          [aTimestamp, aProcessed](RemoteAudioDecoder* aDecoder) {
            aDecoder->UpdateInputStatus(aTimestamp, aProcessed);
          });
    }

    void HandleOutput(java::Sample::Param aSample,
                      java::SampleBuffer::Param aBuffer) override {
      MOZ_ASSERT(aBuffer, "Audio sample should have buffer");
      RunOnTaskQueue<RemoteAudioDecoder>(
          "RemoteAudioDecoder::HandleOutput",
          [sample = java::Sample::GlobalRef(aSample),
           buffer = java::SampleBuffer::GlobalRef(aBuffer)](
              RemoteAudioDecoder* aDecoder) mutable {
            aDecoder->ProcessOutput(std::move(sample), std::move(buffer));
          });
    }

    void HandleOutputFormatChanged(
        java::sdk::MediaFormat::Param aFormat) override {
      int32_t outputChannels = 0;
      aFormat->GetInteger(u"channel-count"_ns, &outputChannels);
      AudioConfig::ChannelLayout layout(outputChannels);
      if (!layout.IsValid()) {
        HandleError(MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            RESULT_DETAIL("Invalid channel layout:%d", outputChannels)));
        return;
      }

      int32_t sampleRate = 0;
      aFormat->GetInteger(u"sample-rate"_ns, &sampleRate);
      LOG("Audio output format changed: channels:{} sample rate:{}",
          outputChannels, sampleRate);

      RunOnTaskQueue<RemoteAudioDecoder>(
          "RemoteAudioDecoder::HandleOutputFormatChanged",
          [outputChannels, sampleRate](RemoteAudioDecoder* aDecoder) {
            aDecoder->ProcessOutputFormatChange(outputChannels, sampleRate);
          });
    }

    void HandleError(const MediaResult& aError) override {
      RunOnTaskQueue<RemoteAudioDecoder>(
          "RemoteAudioDecoder::HandleError",
          [aError](RemoteAudioDecoder* aDecoder) { aDecoder->Error(aError); });
    }
  };

  bool IsSampleTimeSmallerThanFirstDemuxedSampleTime(int64_t aTime) const {
    return mFirstDemuxedSampleTime->ToMicroseconds() > aTime;
  }

  bool ShouldDiscardSample(int64_t aSession) const {
    AssertOnTaskQueue();
    // HandleOutput() runs on Android binder thread pool and could be preempted
    // by RemoteDateDecoder task queue. That means ProcessOutput() could be
    // scheduled after Shutdown() or Flush(). We won't need the
    // sample which is returned after calling Shutdown() and Flush(). We can
    // check mFirstDemuxedSampleTime to know whether the Flush() has been
    // called, becasue it would be reset in Flush().
    return GetState() == State::SHUTDOWN || !mFirstDemuxedSampleTime ||
           mSession != aSession;
  }

  // Param and LocalRef are only valid for the duration of a JNI method call.
  // Use GlobalRef as the parameter type to keep the Java object referenced
  // until running.
  void ProcessOutput(java::Sample::GlobalRef&& aSample,
                     java::SampleBuffer::GlobalRef&& aBuffer) {
    AssertOnTaskQueue();

    LOG("ProcessOutput");

    if (ShouldDiscardSample(aSample->Session()) || !aBuffer->IsValid()) {
      aSample->Dispose();
      LOG("Discarding sample");
      return;
    }

    RenderOrReleaseOutput autoRelease(mJavaDecoder, aSample);

    java::sdk::MediaCodec::BufferInfo::LocalRef info = aSample->Info();
    MOZ_ASSERT(info);

    int32_t flags = 0;
    bool ok = NS_SUCCEEDED(info->Flags(&flags));
    bool isEOS = !!(flags & java::sdk::MediaCodec::BUFFER_FLAG_END_OF_STREAM);

    int32_t offset;
    ok &= NS_SUCCEEDED(info->Offset(&offset));

    int64_t presentationTimeUs;
    ok &= NS_SUCCEEDED(info->PresentationTimeUs(&presentationTimeUs));

    int32_t size;
    ok &= NS_SUCCEEDED(info->Size(&size));

    if (!ok ||
        (IsSampleTimeSmallerThanFirstDemuxedSampleTime(presentationTimeUs) &&
         !isEOS)) {
      LOG("ProcessOutput: decoding error ok[{}], pts[{}], eos[{}]",
          ok ? "true" : "false", presentationTimeUs, isEOS ? "true" : "false");
      Error(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__));
      return;
    }

    if (size > 0) {
      const int32_t sampleSize = sizeof(int16_t);
      const int32_t numSamples = size / sampleSize;

      InflatableShortBuffer audio(numSamples);
      if (!audio) {
        Error(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
        LOG("OOM while allocating temporary output buffer");
        return;
      }
      nsresult rv =
          aBuffer->NativeCopy(reinterpret_cast<jlong>(audio.get()),
                              audio.Length() * sampleSize, offset, size);
      if (NS_FAILED(rv)) {
        LOG("Fail to copy audio buffer");
        Error(MediaResult(rv, __func__));
        return;
      }
      AlignedFloatBuffer converted = audio.Inflate();

      TimeUnit pts = TimeUnit::FromMicroseconds(presentationTimeUs);

      LOG("Decoded: {} frames of {} audio, pts: {}, {} channels, {} Hz",
          numSamples / mOutputChannels,
          sampleSize == sizeof(int16_t) ? "int16" : "f32", pts.ToString().get(),
          mOutputChannels, mOutputSampleRate);

      RefPtr<AudioData> data = new AudioData(
          0, pts, std::move(converted), mOutputChannels, mOutputSampleRate);

      UpdateOutputStatus(std::move(data));
    } else {
      LOG("ProcessOutput but size 0");
    }

    if (isEOS) {
      DrainComplete();
    }
  }

  void ProcessOutputFormatChange(int32_t aChannels, int32_t aSampleRate) {
    AssertOnTaskQueue();

    mOutputChannels = aChannels;
    mOutputSampleRate = aSampleRate;
  }

  int32_t mOutputChannels{};
  int32_t mOutputSampleRate{};
  Maybe<TimeUnit> mFirstDemuxedSampleTime;
  nsTArray<RefPtr<MediaByteBuffer>> mCodecSpecificData;
};

static RefPtr<PlatformDecoderModule::CreateDecoderPromise> ConstructDecoder(
    const CreateDecoderParams& aParams, RemoteDataDecoder* aDecoder,
    CDMProxy* aProxy, RefPtr<MediaDataDecoder::InitPromise>&& aConstruct) {
  RefPtr<MediaDataDecoder> decoder = aDecoder;
  if (aProxy) {
    decoder = new EMEMediaDataDecoderProxy(aParams, decoder.forget(), aProxy);
  }
  return aConstruct->Then(
      GetCurrentSerialEventTarget(), __func__,
      [decoder = std::move(decoder)](
          MediaDataDecoder::InitPromise::ResolveOrRejectValue&& aValue) {
        if (aValue.IsReject()) {
          return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
              std::move(aValue.RejectValue()), __func__);
        }
        return PlatformDecoderModule::CreateDecoderPromise::CreateAndResolve(
            decoder, __func__);
      });
}

/* static */
RefPtr<PlatformDecoderModule::CreateDecoderPromise>
RemoteDataDecoder::CreateAudioDecoder(const CreateDecoderParams& aParams,
                                      const nsString& aDrmStubId,
                                      CDMProxy* aProxy) {
  const AudioInfo& config = aParams.AudioConfig();
  java::sdk::MediaFormat::LocalRef format;
  nsresult rv = java::sdk::MediaFormat::CreateAudioFormat(
      config.mMimeType, config.mRate, config.mChannels, &format);
  if (NS_FAILED(rv)) {
    return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
        MediaResult(rv, __func__), __func__);
  }
  // format->SetInteger(java::sdk::MediaFormat::KEY_PCM_ENCODING,
  //                    java::sdk::AudioFormat::ENCODING_PCM_FLOAT);

  RefPtr<RemoteDataDecoder> decoder =
      new RemoteAudioDecoder(config, format, aDrmStubId);
  return ConstructDecoder(aParams, decoder, aProxy, decoder->Construct());
}

/* static */
RefPtr<PlatformDecoderModule::CreateDecoderPromise>
RemoteDataDecoder::CreateVideoDecoder(const CreateDecoderParams& aParams,
                                      const nsString& aDrmStubId,
                                      CDMProxy* aProxy) {
  const VideoInfo& config = aParams.VideoConfig();
  java::sdk::MediaFormat::LocalRef format;
  nsresult rv = java::sdk::MediaFormat::CreateVideoFormat(
      TranslateMimeType(config.mMimeType), config.mImage.width,
      config.mImage.height, &format);
  if (NS_FAILED(rv)) {
    return PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
        MediaResult(rv, __func__), __func__);
  }

  RefPtr<RemoteDataDecoder> decoder =
      new RemoteVideoDecoder(config, format, aDrmStubId, aParams.mTrackingId);
  return ConstructDecoder(aParams, decoder, aProxy, decoder->Construct());
}

RemoteDataDecoder::RemoteDataDecoder(MediaData::Type aType,
                                     const nsACString& aMimeType,
                                     java::sdk::MediaFormat::Param aFormat,
                                     const nsString& aDrmStubId)
    : mType(aType),
      mMimeType(aMimeType),
      mFormat(aFormat),
      mDrmStubId(aDrmStubId),
      mTaskQueue(TaskQueue::Create(
          GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
          "RemoteDataDecoder")),
      mSession(0),
      mNumPendingInputs(0) {}

RemoteDataDecoder::~RemoteDataDecoder() {
  AssertOnTaskQueue();
  MOZ_ASSERT(GetState() == State::SHUTDOWN);
  mTaskQueue->BeginShutdown();
}

RefPtr<MediaDataDecoder::InitPromise> RemoteDataDecoder::Construct() {
  RefPtr<RemoteDataDecoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    MediaResult rv = self->ProcessConstruct();
    if (NS_FAILED(rv)) {
      self->ProcessShutdown();
      return InitPromise::CreateAndReject(rv, __func__);
    }
    return InitPromise::CreateAndResolve(self->GetTrackType(), __func__);
  });
}

RefPtr<MediaDataDecoder::InitPromise> RemoteDataDecoder::Init() {
  RefPtr<RemoteDataDecoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    MediaResult rv = self->ProcessInit();
    if (NS_FAILED(rv)) {
      return InitPromise::CreateAndReject(rv, __func__);
    }
    return InitPromise::CreateAndResolve(self->GetTrackType(), __func__);
  });
}

MediaResult RemoteDataDecoder::ProcessInit() {
  AssertOnTaskQueue();
  if (NeedsNewDecoder()) {
    return MediaResult(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER, __func__);
  }
  if (mDecodeError) {
    return mDecodeError.extract();
  }

  java::sdk::MediaCodec::BufferInfo::LocalRef bufferInfo;
  if (NS_FAILED(java::sdk::MediaCodec::BufferInfo::New(&bufferInfo)) ||
      !bufferInfo) {
    return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
  }
  mInputBufferInfo = bufferInfo;
  return NS_OK;
}

RefPtr<MediaDataDecoder::DecodePromise> RemoteDataDecoder::Decode(
    MediaRawData* aSample) {
  return InvokeAsync<MediaRawData*>(mTaskQueue, this, __func__,
                                    &RemoteDataDecoder::ProcessDecode, aSample);
}

RefPtr<MediaDataDecoder::DecodePromise> RemoteDataDecoder::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &RemoteDataDecoder::ProcessDrain);
}

RefPtr<MediaDataDecoder::FlushPromise> RemoteDataDecoder::Flush() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &RemoteDataDecoder::ProcessFlush);
}

RefPtr<ShutdownPromise> RemoteDataDecoder::Shutdown() {
  RefPtr<RemoteDataDecoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    self->ProcessShutdown();
    return ShutdownPromise::CreateAndResolve(true, __func__);
  });
}

RefPtr<MediaDataDecoder::FlushPromise> RemoteDataDecoder::ProcessFlush() {
  AssertOnTaskQueue();
  MOZ_ASSERT(GetState() != State::SHUTDOWN);

  mDecodedData = DecodedData();
  UpdatePendingInputStatus(PendingOp::CLEAR);
  mDecodePromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mDrainPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  SetState(State::DRAINED);
  mJavaDecoder->Flush();
  return FlushPromise::CreateAndResolve(true, __func__);
}

RefPtr<MediaDataDecoder::DecodePromise> RemoteDataDecoder::ProcessDrain() {
  AssertOnTaskQueue();
  if (GetState() == State::SHUTDOWN) {
    return DecodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                          __func__);
  }
  RefPtr<DecodePromise> p = mDrainPromise.Ensure(__func__);
  if (GetState() == State::DRAINING || GetState() == State::DRAINED) {
    // Drain operation already in progress or complete.
    // There's no operation to perform other than returning any already
    // decoded data.
    ReturnDecodedData();
    return p;
  }

  SetState(State::DRAINING);
  mInputBufferInfo->Set(0, 0, -1,
                        java::sdk::MediaCodec::BUFFER_FLAG_END_OF_STREAM);
  mSession = mJavaDecoder->Input(nullptr, mInputBufferInfo, nullptr);
  return p;
}

void RemoteDataDecoder::ProcessShutdown() {
  AssertOnTaskQueue();
  if (GetState() == State::SHUTDOWN) {
    return;
  }
  LOG("Shutdown");
  SetState(State::SHUTDOWN);
  if (mJavaCallbacks) {
    static_cast<CallbacksSupportBase*>(
        JavaCallbacksSupport::GetNative(mJavaCallbacks))
        ->Detach();
  }

  if (mJavaDecoder) {
    mJavaDecoder->Release();
    mJavaDecoder = nullptr;
  }

  if (mJavaCallbacks) {
    // Callbacks already queued on mTaskQueue still refer to the native object,
    // so only dispose of it once they have run.
    nsresult rv = mTaskQueue->Dispatch(NS_NewRunnableFunction(
        "RemoteDataDecoder::DisposeCallbacks",
        [javaCallbacks = std::move(mJavaCallbacks)]() {
          JavaCallbacksSupport::DisposeNative(javaCallbacks);
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

  mFormat = nullptr;
}

using CryptoInfoResult =
    Result<java::sdk::MediaCodec::CryptoInfo::LocalRef, nsresult>;

static CryptoInfoResult GetCryptoInfoFromSample(const MediaRawData* aSample) {
  const auto& cryptoObj = aSample->mCrypto;
  java::sdk::MediaCodec::CryptoInfo::LocalRef cryptoInfo;

  if (!cryptoObj.IsEncrypted()) {
    return CryptoInfoResult(cryptoInfo);
  }

  nsresult rv = java::sdk::MediaCodec::CryptoInfo::New(&cryptoInfo);
  NS_ENSURE_SUCCESS(rv, CryptoInfoResult(rv));

  uint32_t numSubSamples = std::min<uint32_t>(
      cryptoObj.mPlainSizes.Length(), cryptoObj.mEncryptedSizes.Length());

  uint32_t totalSubSamplesSize = 0;
  for (const auto& size : cryptoObj.mPlainSizes) {
    totalSubSamplesSize += size;
  }
  for (const auto& size : cryptoObj.mEncryptedSizes) {
    totalSubSamplesSize += size;
  }

  // Deep copy the plain sizes so we can modify them.
  nsTArray<uint32_t> plainSizes = cryptoObj.mPlainSizes.Clone();
  uint32_t codecSpecificDataSize = aSample->Size() - totalSubSamplesSize;
  // Size of codec specific data("CSD") for Android java::sdk::MediaCodec usage
  // should be included in the 1st plain size if it exists.
  if (codecSpecificDataSize > 0 && !plainSizes.IsEmpty()) {
    // This shouldn't overflow as the the plain size should be UINT16_MAX at
    // most, and the CSD should never be that large. Checked int acts like a
    // diagnostic assert here to help catch if we ever have insane inputs.
    CheckedUint32 newLeadingPlainSize{plainSizes[0]};
    newLeadingPlainSize += codecSpecificDataSize;
    plainSizes[0] = newLeadingPlainSize.value();
  }

  static const int kExpectedIVLength = 16;
  nsTArray<uint8_t> tempIV(kExpectedIVLength);
  jint mode;
  switch (cryptoObj.mCryptoScheme) {
    case CryptoScheme::None:
      mode = java::sdk::MediaCodec::CRYPTO_MODE_UNENCRYPTED;
      MOZ_ASSERT(cryptoObj.mIV.Length() <= kExpectedIVLength);
      tempIV.AppendElements(cryptoObj.mIV);
      break;
    case CryptoScheme::Cenc:
      mode = java::sdk::MediaCodec::CRYPTO_MODE_AES_CTR;
      MOZ_ASSERT(cryptoObj.mIV.Length() <= kExpectedIVLength);
      tempIV.AppendElements(cryptoObj.mIV);
      break;
    case CryptoScheme::Cbcs:
    case CryptoScheme::Cbcs_1_9:
      mode = java::sdk::MediaCodec::CRYPTO_MODE_AES_CBC;
      MOZ_ASSERT(cryptoObj.mConstantIV.Length() <= kExpectedIVLength);
      tempIV.AppendElements(cryptoObj.mConstantIV);
      break;
  }
  auto tempIVLength = tempIV.Length();
  for (size_t i = tempIVLength; i < kExpectedIVLength; i++) {
    // Padding with 0
    tempIV.AppendElement(0);
  }

  MOZ_ASSERT(numSubSamples <= INT32_MAX);
  cryptoInfo->Set(static_cast<int32_t>(numSubSamples),
                  mozilla::jni::IntArray::From(plainSizes),
                  mozilla::jni::IntArray::From(cryptoObj.mEncryptedSizes),
                  mozilla::jni::ByteArray::From(cryptoObj.mKeyId),
                  mozilla::jni::ByteArray::From(tempIV), mode);
  if (mode == java::sdk::MediaCodec::CRYPTO_MODE_AES_CBC) {
    java::CodecProxy::SetCryptoPatternIfNeeded(
        cryptoInfo, cryptoObj.mCryptByteBlock, cryptoObj.mSkipByteBlock);
  }

  return CryptoInfoResult(cryptoInfo);
}

RefPtr<MediaDataDecoder::DecodePromise> RemoteDataDecoder::ProcessDecode(
    MediaRawData* aSample) {
  AssertOnTaskQueue();
  MOZ_ASSERT(GetState() != State::SHUTDOWN);
  MOZ_ASSERT(aSample != nullptr);
  jni::ByteBuffer::LocalRef bytes = jni::ByteBuffer::New(
      const_cast<uint8_t*>(aSample->Data()), aSample->Size(), fallible);
  if (!bytes) {
    return DecodePromise::CreateAndReject(
        MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__), __func__);
  }

  SetState(State::DRAINABLE);
  MOZ_ASSERT(aSample->Size() <= INT32_MAX);
  mInputBufferInfo->Set(0, static_cast<int32_t>(aSample->Size()),
                        aSample->mTime.ToMicroseconds(), 0);
  CryptoInfoResult crypto = GetCryptoInfoFromSample(aSample);
  if (crypto.isErr()) {
    return DecodePromise::CreateAndReject(
        MediaResult(crypto.unwrapErr(), __func__), __func__);
  }
  int64_t session =
      mJavaDecoder->Input(bytes, mInputBufferInfo, crypto.unwrap());
  if (session == java::CodecProxy::INVALID_SESSION) {
    return DecodePromise::CreateAndReject(
        MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__), __func__);
  }
  mSession = session;
  return mDecodePromise.Ensure(__func__);
}

void RemoteDataDecoder::UpdatePendingInputStatus(PendingOp aOp) {
  AssertOnTaskQueue();
  switch (aOp) {
    case PendingOp::INCREASE:
      mNumPendingInputs++;
      break;
    case PendingOp::DECREASE:
      mNumPendingInputs--;
      break;
    case PendingOp::CLEAR:
      mNumPendingInputs = 0;
      break;
  }
}

void RemoteDataDecoder::UpdateInputStatus(int64_t aTimestamp, bool aProcessed) {
  AssertOnTaskQueue();
  if (GetState() == State::SHUTDOWN) {
    return;
  }

  if (!aProcessed) {
    UpdatePendingInputStatus(PendingOp::INCREASE);
  } else if (HasPendingInputs()) {
    UpdatePendingInputStatus(PendingOp::DECREASE);
  }

  if (!HasPendingInputs() ||  // Input has been processed, request the next one.
      !mDecodedData.IsEmpty()) {  // Previous output arrived before Decode().
    ReturnDecodedData();
  }
}

void RemoteDataDecoder::UpdateOutputStatus(RefPtr<MediaData>&& aSample) {
  AssertOnTaskQueue();
  if (GetState() == State::SHUTDOWN) {
    LOG("Update output status, but decoder has been shut down, dropping the "
        "decoded results");
    return;
  }
  if (IsUsefulData(aSample)) {
    mDecodedData.AppendElement(std::move(aSample));
  } else {
    LOG("Decoded data, but not considered useful");
  }
  ReturnDecodedData();
}

void RemoteDataDecoder::ReturnDecodedData() {
  AssertOnTaskQueue();
  MOZ_ASSERT(GetState() != State::SHUTDOWN);

  // We only want to clear mDecodedData when we have resolved the promises.
  LOG("have decode promise={}, have drain promise={}, state={}",
      static_cast<int>(!mDecodePromise.IsEmpty()),
      static_cast<int>(!mDrainPromise.IsEmpty()), static_cast<int>(GetState()));
  MOZ_ASSERT(mDecodePromise.IsEmpty() || mDrainPromise.IsEmpty());

  if (!mDecodePromise.IsEmpty()) {
    // Return successfully decoded samples, even if there is an error, which
    // can be returned for a subsequent decode or drain request.
    if (!mDecodedData.IsEmpty() || mDecodeError.isNothing()) {
      mDecodePromise.Resolve(std::move(mDecodedData), __func__);
      MOZ_ASSERT(mDecodedData.IsEmpty());
    } else if (mDecodeError.isSome()) {
      mDecodePromise.Reject(mDecodeError.extract(), __func__);
    }
  } else if (!mDrainPromise.IsEmpty()) {
    if (!mDecodedData.IsEmpty() ||
        (GetState() == State::DRAINED && mDecodeError.isNothing())) {
      mDrainPromise.Resolve(std::move(mDecodedData), __func__);
      MOZ_ASSERT(mDecodedData.IsEmpty());
    } else if (mDecodeError.isSome()) {
      mDrainPromise.Reject(mDecodeError.extract(), __func__);
    }
  }
}

void RemoteDataDecoder::DrainComplete() {
  LOG("EOS");
  AssertOnTaskQueue();
  if (GetState() == State::SHUTDOWN) {
    return;
  }
  SetState(State::DRAINED);
  ReturnDecodedData();
}

void RemoteDataDecoder::Error(const MediaResult& aError) {
  AssertOnTaskQueue();
  if (GetState() == State::SHUTDOWN) {
    return;
  }

  LOG("ErrorName={} Message={}", aError.ErrorName().get(),
      aError.Message().get());
  // If we know we need a new decoder (eg because RemoteVideoDecoder's mSurface
  // has been released due to a GPU process crash) then override the error to
  // request a new decoder.
  if (NeedsNewDecoder()) {
    mDecodeError =
        Some(MediaResult(NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER, __func__));
  } else if (!mDecodeError.isSome()) {
    mDecodeError.emplace(aError);
  }  // else keep the first error to report.

  ReturnDecodedData();
}

void RemoteDataDecoder::SetState(RemoteDataDecoder::State aState) {
  LOG("{}", static_cast<int>(aState));
  AssertOnTaskQueue();
  mState = aState;
}

}  // namespace mozilla
#undef LOG
