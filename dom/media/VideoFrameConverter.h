/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VideoFrameConverter_h
#define VideoFrameConverter_h

#include "ImageContainer.h"
#include "ImageConversion.h"
#include "Pacer.h"
#include "PerformanceRecorder.h"
#include "VideoSegment.h"
#include "api/video/adapted_video_track_source.h"
#include "api/video/video_frame.h"
#include "common_video/include/video_frame_buffer.h"
#include "common_video/include/video_frame_buffer_pool.h"
#include "jsapi/RTCStatsReport.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ImageUtils.h"
#include "nsISupportsImpl.h"
#include "nsThreadUtils.h"

// The number of frame buffers VideoFrameConverter may create before returning
// errors.
// Sometimes these are released synchronously but they can be forwarded all the
// way to the encoder for asynchronous encoding. With a pool size of 5,
// we allow 1 buffer for the current conversion, and 4 buffers to be queued at
// the encoder.
#define CONVERTER_BUFFER_POOL_SIZE 5

extern mozilla::LazyLogModule gMediaPipelineLog;
#define LOG(level, msg, ...) \
  MOZ_LOG_FMT(gMediaPipelineLog, level, msg, ##__VA_ARGS__)

namespace mozilla {

enum class FrameDroppingPolicy {
  Allowed,
  Disabled,
};

// An async video frame format converter.
//
// Input is typically a MediaTrackListener driven by MediaTrackGraph.
//
// Output is exposed through webrtc::AdaptedVideoTrackSource, which implements
// webrtc::VideoSourceInterface<webrtc::VideoFrame>.
template <FrameDroppingPolicy DropPolicy = FrameDroppingPolicy::Allowed>
class VideoFrameConverterImpl : public webrtc::AdaptedVideoTrackSource {
 protected:
  explicit VideoFrameConverterImpl(
      already_AddRefed<nsISerialEventTarget> aTarget,
      const dom::RTCStatsTimestampMaker& aTimestampMaker, bool aLockScaling)
      : mTimestampMaker(aTimestampMaker),
        mTarget(aTarget),
        mLockScaling(aLockScaling),
        mPacer(MakeAndAddRef<Pacer<FrameToProcess>>(
            do_AddRef(mTarget), mIdleFrameDuplicationInterval)),
        mScalingPool(false, CONVERTER_BUFFER_POOL_SIZE),
        mConversionPool(false, CONVERTER_BUFFER_POOL_SIZE) {
    MOZ_COUNT_CTOR(VideoFrameConverterImpl);
  }

  // AdaptedVideoTrackSource impl -- we don't expect any of these to be called.
  // They are in libwebrtc because they are used by blink to communicate
  // properties from a video track source to their libwebrtc integration layer.
  // We signal this elsewhere.
  void GenerateKeyFrame() override {
    MOZ_CRASH("Unexpected VideoFrameConverterImpl::GenerateKeyFrame");
  }
  SourceState state() const override {
    MOZ_CRASH("Unexpected VideoFrameConverterImpl::state");
  }
  bool remote() const override {
    MOZ_CRASH("Unexpected VideoFrameConverterImpl::remote");
  }
  bool is_screencast() const override {
    MOZ_CRASH("Unexpected VideoFrameConverterImpl::is_screencast");
  }
  std::optional<bool> needs_denoising() const override {
    MOZ_CRASH("Unexpected VideoFrameConverterImpl::needs_denoising");
  }

  void RegisterListener() {
    mPacingListener = mPacer->PacedItemEvent().Connect(
        mTarget,
        [self = RefPtr(this)](FrameToProcess&& aFrame, TimeStamp aTime) {
          self->QueueForProcessing(std::move(aFrame.mImage), aTime,
                                   aFrame.mSize, aFrame.mForceBlack,
                                   aFrame.mRotation);
        });
  }

 public:
  using webrtc::VideoSourceInterface<webrtc::VideoFrame>::AddOrUpdateSink;
  using webrtc::VideoSourceInterface<webrtc::VideoFrame>::RemoveSink;

  void QueueVideoChunk(const VideoChunk& aChunk, bool aForceBlack) {
    gfx::IntSize size = aChunk.mFrame.GetIntrinsicSize();
    if (size.width == 0 || size.height == 0) {
      return;
    }

    TimeStamp t = aChunk.mTimeStamp;
    MOZ_ASSERT(!t.IsNull());

    mPacer->Enqueue(FrameToProcess(aChunk.mFrame.GetImage(), t, size,
                                   aForceBlack, aChunk.mRotation),
                    t);
  }

  /**
   * An active VideoFrameConverter actively converts queued video frames.
   * While inactive, we keep track of the frame most recently queued for
   * processing, so it can be immediately sent out once activated.
   */
  void SetActive(bool aActive) {
    MOZ_ALWAYS_SUCCEEDS(mTarget->Dispatch(NS_NewRunnableFunction(
        __func__, [self = RefPtr<VideoFrameConverterImpl>(this), this, aActive,
                   time = TimeStamp::Now()] {
          if (mActive == aActive) {
            return;
          }
          LOG(LogLevel::Debug, "VideoFrameConverter {} is now {}",
              fmt::ptr(this), aActive ? "active" : "inactive");
          mActive = aActive;
          if (aActive && mLastFrameQueuedForProcessing.Serial() != -2) {
            // After activating, we re-process the last image that was queued
            // for processing so it can be immediately sent. The image is reset
            // so it doesn't get dropped if within the duplicate frame interval.
            QueueForProcessing(std::move(mLastFrameQueuedForProcessing.mImage),
                               std::max(mLastFrameQueuedForProcessing.mTime +
                                            TimeDuration::FromMicroseconds(1),
                                        time),
                               mLastFrameQueuedForProcessing.mSize,
                               mLastFrameQueuedForProcessing.mForceBlack,
                               mLastFrameQueuedForProcessing.mRotation);
          }
        })));
  }

  void SetTrackEnabled(bool aTrackEnabled) {
    MOZ_ALWAYS_SUCCEEDS(mTarget->Dispatch(NS_NewRunnableFunction(
        __func__, [self = RefPtr<VideoFrameConverterImpl>(this), this,
                   aTrackEnabled, time = TimeStamp::Now()] {
          if (mTrackEnabled == aTrackEnabled) {
            return;
          }
          LOG(LogLevel::Debug, "VideoFrameConverterImpl {} Track is now {}",
              fmt::ptr(this), aTrackEnabled ? "enabled" : "disabled");
          mTrackEnabled = aTrackEnabled;
          if (!aTrackEnabled) {
            // After disabling we immediately send a frame as black, so it can
            // be seen quickly, even if no frames are flowing. If no frame has
            // been queued for processing yet, we use the FrameToProcess default
            // size (640x480).
            QueueForProcessing(/* aImage= */ nullptr,
                               std::max(mLastFrameQueuedForProcessing.mTime +
                                            TimeDuration::FromMicroseconds(1),
                                        time),
                               mLastFrameQueuedForProcessing.mSize,
                               /* aForceBlack= */ true,
                               mLastFrameQueuedForProcessing.mRotation);
          }
        })));
  }

  void SetTrackingId(TrackingId aTrackingId) {
    MOZ_ALWAYS_SUCCEEDS(mTarget->Dispatch(NS_NewRunnableFunction(
        __func__, [self = RefPtr<VideoFrameConverterImpl>(this), this,
                   id = std::move(aTrackingId)]() mutable {
          mTrackingId = Some(std::move(id));
        })));
  }

  void SetIdleFrameDuplicationInterval(TimeDuration aInterval) {
    MOZ_ALWAYS_SUCCEEDS(mTarget->Dispatch(NS_NewRunnableFunction(
        __func__, [self = RefPtr(this), this, aInterval] {
          mIdleFrameDuplicationInterval = aInterval;
        })));
    mPacer->SetDuplicationInterval(aInterval);
  }

  void Shutdown() {
    mPacer->Shutdown()->Then(
        mTarget, __func__,
        [self = RefPtr<VideoFrameConverterImpl>(this), this] {
          mPacingListener.DisconnectIfExists();
          mScalingPool.Release();
          mConversionPool.Release();
          mLastFrameQueuedForProcessing = FrameToProcess();
          mLastFrameConverted = Nothing();
        });
  }

 protected:
  struct FrameToProcess {
    FrameToProcess() = default;

    FrameToProcess(RefPtr<layers::Image> aImage, TimeStamp aTime,
                   gfx::IntSize aSize, bool aForceBlack,
                   VideoRotation aRotation)
        : mImage(std::move(aImage)),
          mTime(aTime),
          mSize(aSize),
          mForceBlack(aForceBlack),
          mRotation(aRotation) {}

    RefPtr<layers::Image> mImage;
    TimeStamp mTime = TimeStamp::Now();
    gfx::IntSize mSize = gfx::IntSize(640, 480);
    bool mForceBlack = false;
    VideoRotation mRotation = VideoRotation::kDegree_0;

    int32_t Serial() const {
      if (mForceBlack) {
        // Set the last-img check to indicate black.
        // -1 is not a guaranteed invalid serial. See bug 1262134.
        return -1;
      }
      if (!mImage) {
        // Set the last-img check to indicate reset.
        // -2 is not a guaranteed invalid serial. See bug 1262134.
        return -2;
      }
      return mImage->GetSerial();
    }
  };

  struct FrameConverted {
    FrameConverted(webrtc::VideoFrame aFrame, gfx::IntSize aOriginalSize,
                   int32_t aSerial)
        : mFrame(std::move(aFrame)),
          mOriginalSize(aOriginalSize),
          mSerial(aSerial) {}

    webrtc::VideoFrame mFrame;
    gfx::IntSize mOriginalSize;
    int32_t mSerial;
  };

  MOZ_COUNTED_DTOR_VIRTUAL(VideoFrameConverterImpl)

  void VideoFrameConverted(const webrtc::VideoFrame& aVideoFrame,
                           gfx::IntSize aOriginalSize, int32_t aSerial) {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());

    LOG(LogLevel::Verbose,
        "VideoFrameConverterImpl {}: Converted a frame. Diff from last: "
        "{:.3f}ms",
        fmt::ptr(this),
        static_cast<double>(aVideoFrame.timestamp_us() -
                            (mLastFrameConverted
                                 ? mLastFrameConverted->mFrame.timestamp_us()
                                 : aVideoFrame.timestamp_us())) /
            1000);

    // Check that time doesn't go backwards
    MOZ_ASSERT_IF(mLastFrameConverted,
                  aVideoFrame.timestamp_us() >
                      mLastFrameConverted->mFrame.timestamp_us());

    mLastFrameConverted =
        Some(FrameConverted(aVideoFrame, aOriginalSize, aSerial));

    OnFrame(aVideoFrame);
  }

  void QueueForProcessing(RefPtr<layers::Image> aImage, TimeStamp aTime,
                          gfx::IntSize aSize, bool aForceBlack,
                          VideoRotation aRotation) {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());

    FrameToProcess frame{std::move(aImage), aTime, aSize,
                         aForceBlack || !mTrackEnabled, aRotation};

    if (frame.mTime <= mLastFrameQueuedForProcessing.mTime) {
      LOG(LogLevel::Debug,
          "VideoFrameConverterImpl {}: Dropping a frame because time did not "
          "progress ({:.3f}s)",
          fmt::ptr(this),
          (mLastFrameQueuedForProcessing.mTime - frame.mTime).ToSeconds());
      return;
    }

    if (frame.Serial() == mLastFrameQueuedForProcessing.Serial()) {
      // This is the same frame as the last one. We limit the same-frame rate,
      // and rewrite the time so the frame-gap is in multiples of the
      // duplication interval.
      //
      // The pacer only starts duplicating frames if there is no flow of frames
      // into it. There are other reasons the same frame could repeat here, and
      // at a shorter interval than the duplication interval. For instance after
      // the sender is disabled (SetTrackEnabled) but there is still a flow of
      // frames into the pacer. All disabled frames have the same serial.
      if (auto diff = frame.mTime - mLastFrameQueuedForProcessing.mTime;
          diff >= mIdleFrameDuplicationInterval) {
        auto diff_us = static_cast<int64_t>(diff.ToMicroseconds());
        auto idle_interval_us = static_cast<int64_t>(
            mIdleFrameDuplicationInterval.ToMicroseconds());
        auto multiples = diff_us / idle_interval_us;
        MOZ_ASSERT(multiples > 0);
        LOG(LogLevel::Verbose,
            "VideoFrameConverterImpl {}: Rewrote time interval for a duplicate "
            "frame from {:.3f}s to {:.3f}s",
            fmt::ptr(this),
            (frame.mTime - mLastFrameQueuedForProcessing.mTime).ToSeconds(),
            (mIdleFrameDuplicationInterval * multiples).ToSeconds());
        frame.mTime = mLastFrameQueuedForProcessing.mTime +
                      (mIdleFrameDuplicationInterval * multiples);
      } else {
        LOG(LogLevel::Verbose,
            "VideoFrameConverterImpl {}: Dropping a duplicate frame because "
            "the duplication interval ({:.3f}s) hasn't passed ({:.3f}s)",
            fmt::ptr(this), mIdleFrameDuplicationInterval.ToSeconds(),
            (frame.mTime - mLastFrameQueuedForProcessing.mTime).ToSeconds());
        return;
      }
    }

    mLastFrameQueuedForProcessing = std::move(frame);

    if (!mActive) {
      LOG(LogLevel::Debug,
          "VideoFrameConverterImpl {}: Ignoring a frame because we're inactive",
          fmt::ptr(this));
      return;
    }

    MOZ_ALWAYS_SUCCEEDS(mTarget->Dispatch(NewRunnableMethod<FrameToProcess>(
        "VideoFrameConverterImpl::ProcessVideoFrame", this,
        &VideoFrameConverterImpl::ProcessVideoFrame,
        mLastFrameQueuedForProcessing)));
  }

  static webrtc::VideoRotation ToWebrtcVideoRotation(VideoRotation aRotation) {
    switch (aRotation) {
      case VideoRotation::kDegree_90:
        return webrtc::kVideoRotation_90;
      case VideoRotation::kDegree_180:
        return webrtc::kVideoRotation_180;
      case VideoRotation::kDegree_270:
        return webrtc::kVideoRotation_270;
      default:
        return webrtc::kVideoRotation_0;
    }
  }

  void ProcessVideoFrame(const FrameToProcess& aFrame) {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());

    auto convert = [this,
                    &aFrame]() -> webrtc::scoped_refptr<webrtc::I420Buffer> {
      webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
          mConversionPool.CreateI420Buffer(aFrame.mSize.width,
                                           aFrame.mSize.height);
      if (!buffer) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
        ++mConversionFramesDropped;
#endif
        MOZ_DIAGNOSTIC_ASSERT(mConversionFramesDropped <= 100,
                              "Conversion buffers must be leaking");
        LOG(LogLevel::Warning,
            "VideoFrameConverterImpl {}: Creating a conversion buffer failed",
            fmt::ptr(this));
        return nullptr;
      }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
      mConversionFramesDropped = 0;
#endif
      PerformanceRecorder<CopyVideoStage> rec(
          "VideoFrameConverterImpl::ConvertToI420"_ns, *mTrackingId,
          buffer->width(), buffer->height());
      nsresult rv = ConvertToI420(aFrame.mImage, buffer->MutableDataY(),
                                  buffer->StrideY(), buffer->MutableDataU(),
                                  buffer->StrideU(), buffer->MutableDataV(),
                                  buffer->StrideV(), aFrame.mSize);

      if (NS_FAILED(rv)) {
        LOG(LogLevel::Warning,
            "VideoFrameConverterImpl {}: Image conversion failed",
            fmt::ptr(this));
        return nullptr;
      }
      rec.Record();
      return buffer;
    };

    auto cropAndScale =
        [this, &aFrame](
            const webrtc::scoped_refptr<webrtc::I420BufferInterface>& aSrc,
            int aCrop_x, int aCrop_y, int aCrop_w, int aCrop_h, int aOut_width,
            int aOut_height)
        -> webrtc::scoped_refptr<webrtc::I420BufferInterface> {
      webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
          mScalingPool.CreateI420Buffer(aOut_width, aOut_height);
      if (!buffer) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
        ++mScalingFramesDropped;
        MOZ_DIAGNOSTIC_ASSERT(mScalingFramesDropped <= 100,
                              "Scaling buffers must be leaking");
#endif
        LOG(LogLevel::Warning,
            "VideoFrameConverterImpl {}: Creating a scaling buffer failed",
            fmt::ptr(this));
        return nullptr;
      }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
      mScalingFramesDropped = 0;
#endif
      PerformanceRecorder<CopyVideoStage> rec(
          "VideoFrameConverterImpl::CropAndScale"_ns, *mTrackingId,
          aSrc->width(), aSrc->height());
      LOG(LogLevel::Verbose,
          "VideoFrameConverterImpl {}: Scaling image {}, {}x{} -> {}x{}",
          fmt::ptr(this), aFrame.Serial(), aFrame.mSize.Width(),
          aFrame.mSize.Height(), aOut_width, aOut_height);
      buffer->CropAndScaleFrom(*aSrc, aCrop_x, aCrop_y, aCrop_w, aCrop_h);
      rec.Record();
      return buffer;
    };

    const webrtc::Timestamp time =
        dom::RTCStatsTimestamp::FromMozTime(mTimestampMaker, aFrame.mTime)
            .ToRealtime();

    const bool sameAsLastConverted =
        mLastFrameConverted && aFrame.Serial() == mLastFrameConverted->mSerial;
    const gfx::IntSize inSize =
        sameAsLastConverted ? mLastFrameConverted->mOriginalSize : aFrame.mSize;

    int crop_x{}, crop_y{}, crop_width{}, crop_height{}, out_width{},
        out_height{};
    bool keep =
        AdaptFrame(inSize.Width(), inSize.Height(), time.us(), &out_width,
                   &out_height, &crop_width, &crop_height, &crop_x, &crop_y);

    if (mLockScaling) {
      crop_x = crop_y = 0;
      crop_width = out_width = inSize.Width();
      crop_height = out_height = inSize.Height();
    }

    if (out_width == 0 || out_height == 0) {
      LOG(LogLevel::Verbose,
          "VideoFrameConverterImpl {}: Skipping a frame because it has no "
          "pixels",
          fmt::ptr(this));
      OnFrameDropped();
      return;
    }

    if constexpr (DropPolicy == FrameDroppingPolicy::Allowed) {
      if (!keep) {
        LOG(LogLevel::Verbose,
            "VideoFrameConverterImpl {}: Dropping a frame because of SinkWants",
            fmt::ptr(this));
        // AdaptFrame has already called OnFrameDropped.
        return;
      }
      if (aFrame.mTime < mLastFrameQueuedForProcessing.mTime) {
        LOG(LogLevel::Verbose,
            "VideoFrameConverterImpl {}: Dropping a frame that is {:.3f} "
            "seconds "
            "before latest",
            fmt::ptr(this),
            (mLastFrameQueuedForProcessing.mTime - aFrame.mTime).ToSeconds());
        OnFrameDropped();
        return;
      }
    }

    if (sameAsLastConverted) {
      if (out_width == mLastFrameConverted->mFrame.width() &&
          out_height == mLastFrameConverted->mFrame.height()) {
        // This is the same input frame as last time. Avoid a conversion.
        LOG(LogLevel::Verbose,
            "VideoFrameConverterImpl {}: Re-converting last frame {}. "
            "Re-using with same resolution.",
            fmt::ptr(this), aFrame.Serial());
        webrtc::VideoFrame frame = mLastFrameConverted->mFrame;
        frame.set_timestamp_us(time.us());
        VideoFrameConverted(frame, mLastFrameConverted->mOriginalSize,
                            mLastFrameConverted->mSerial);
        return;
      }
    }

    if (aFrame.mForceBlack) {
      // Send a black image.
      webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
          mScalingPool.CreateI420Buffer(out_width, out_height);
      if (!buffer) {
        MOZ_DIAGNOSTIC_CRASH(
            "Buffers not leaving scope except for "
            "reconfig, should never leak");
        LOG(LogLevel::Warning,
            "VideoFrameConverterImpl {}: Creating a buffer for a black video "
            "frame failed",
            fmt::ptr(this));
        OnFrameDropped();
        return;
      }

      LOG(LogLevel::Verbose,
          "VideoFrameConverterImpl {}: Sending a black video frame. "
          "CropAndScale: {}x{} -> {}x{}",
          fmt::ptr(this), aFrame.mSize.Width(), aFrame.mSize.Height(),
          out_width, out_height);
      webrtc::I420Buffer::SetBlack(buffer.get());

      VideoFrameConverted(
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(buffer)
              .set_timestamp_us(time.us())
              .set_rotation(ToWebrtcVideoRotation(aFrame.mRotation))
              .build(),
          inSize, aFrame.Serial());
      return;
    }

    if (!aFrame.mImage) {
      // Don't send anything for null images.
      return;
    }

    MOZ_ASSERT(aFrame.mImage->GetSize() == aFrame.mSize);

    webrtc::scoped_refptr<webrtc::I420BufferInterface> srcFrame;
    RefPtr<layers::PlanarYCbCrImage> image =
        aFrame.mImage->AsPlanarYCbCrImage();
    if (image) {
      dom::ImageUtils utils(image);
      Maybe<dom::ImageBitmapFormat> format = utils.GetFormat();
      if (format.isSome() &&
          format.value() == dom::ImageBitmapFormat::YUV420P &&
          image->GetData()) {
        const layers::PlanarYCbCrData* data = image->GetData();
        srcFrame = webrtc::WrapI420Buffer(
            aFrame.mImage->GetSize().width, aFrame.mImage->GetSize().height,
            data->mYChannel, data->mYStride, data->mCbChannel,
            data->mCbCrStride, data->mCrChannel, data->mCbCrStride,
            [image] { /* keep reference alive*/ });

        LOG(LogLevel::Verbose,
            "VideoFrameConverterImpl {}: Avoiding a conversion for image {}",
            fmt::ptr(this), aFrame.Serial());
      }
    }

    if (!srcFrame) {
      srcFrame = convert();
    }

    if (!srcFrame) {
      OnFrameDropped();
      return;
    }

    if (srcFrame->width() == out_width && srcFrame->height() == out_height) {
      LOG(LogLevel::Verbose,
          "VideoFrameConverterImpl {}: Avoiding scaling for image {}, "
          "Dimensions: {}x{}",
          fmt::ptr(this), aFrame.Serial(), out_width, out_height);
      VideoFrameConverted(
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(srcFrame)
              .set_timestamp_us(time.us())
              .set_rotation(ToWebrtcVideoRotation(aFrame.mRotation))
              .build(),
          inSize, aFrame.Serial());
      return;
    }

    if (webrtc::scoped_refptr<webrtc::I420BufferInterface> buffer =
            cropAndScale(webrtc::scoped_refptr(srcFrame), crop_x, crop_y,
                         crop_width, crop_height, out_width, out_height)) {
      VideoFrameConverted(
          webrtc::VideoFrame::Builder()
              .set_video_frame_buffer(buffer)
              .set_timestamp_us(time.us())
              .set_rotation(ToWebrtcVideoRotation(aFrame.mRotation))
              .build(),
          inSize, aFrame.Serial());
    }
  }

 public:
  const dom::RTCStatsTimestampMaker mTimestampMaker;
  const nsCOMPtr<nsISerialEventTarget> mTarget;
  const bool mLockScaling;

 protected:
  TimeDuration mIdleFrameDuplicationInterval = TimeDuration::Forever();

  // Used to pace future frames close to their rendering-time. Thread-safe.
  const RefPtr<Pacer<FrameToProcess>> mPacer;

  // Accessed only from mTarget.
  MediaEventListener mPacingListener;
  webrtc::VideoFrameBufferPool mScalingPool;
  webrtc::VideoFrameBufferPool mConversionPool;
  FrameToProcess mLastFrameQueuedForProcessing;
  Maybe<FrameConverted> mLastFrameConverted;
  bool mActive = false;
  bool mTrackEnabled = true;
  Maybe<TrackingId> mTrackingId;
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  size_t mConversionFramesDropped = 0;
  size_t mScalingFramesDropped = 0;
#endif
};

class VideoFrameConverter
    : public webrtc::RefCountedObject<
          VideoFrameConverterImpl<FrameDroppingPolicy::Allowed>> {
 protected:
  VideoFrameConverter(already_AddRefed<nsISerialEventTarget> aTarget,
                      const dom::RTCStatsTimestampMaker& aTimestampMaker,
                      bool aLockScaling)
      : webrtc::RefCountedObject<VideoFrameConverterImpl>(
            std::move(aTarget), aTimestampMaker, aLockScaling) {}

 public:
  static already_AddRefed<VideoFrameConverter> Create(
      already_AddRefed<nsISerialEventTarget> aTarget,
      const dom::RTCStatsTimestampMaker& aTimestampMaker, bool aLockScaling) {
    RefPtr<VideoFrameConverter> converter = new VideoFrameConverter(
        std::move(aTarget), aTimestampMaker, aLockScaling);
    converter->RegisterListener();
    return converter.forget();
  }
};

}  // namespace mozilla

#undef LOG

#endif  // VideoFrameConverter_h
