/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VP8TrackEncoder.h"

#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>

#include "DriftCompensation.h"
#include "ImageConversion.h"
#include "VideoSegment.h"
#include "VideoUtils.h"
#include "WebMWriter.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/dom/ImageBitmapBinding.h"
#include "mozilla/dom/ImageUtils.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/media/MediaUtils.h"
#include "prsystem.h"

namespace mozilla {

LazyLogModule gVP8TrackEncoderLog("VP8TrackEncoder");
#define VP8LOG(level, msg, ...) \
  MOZ_LOG_FMT(gVP8TrackEncoderLog, level, msg, ##__VA_ARGS__)

constexpr int DEFAULT_BITRATE_BPS = 2500000;
constexpr int DEFAULT_KEYFRAME_INTERVAL_MS = 10000;
constexpr int DYNAMIC_MAXKFDIST_CHECK_INTERVAL = 5;
constexpr float DYNAMIC_MAXKFDIST_DIFFACTOR = 0.4;
constexpr float DYNAMIC_MAXKFDIST_KFINTERVAL_FACTOR = 0.75;
constexpr int I420_STRIDE_ALIGN = 16;

using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::media;
using namespace mozilla::dom;

namespace {

// Cap on input dimensions. Without this, valid int32_t dimensions can produce
// large-but-non-overflowing size_t values, causing the OS to thrash or kill
// lower priority processes when there are too many page faults. 16384 (16K)
// bounds the allocation to ~384 MB.
constexpr int32_t kMaxI420Dimension = 16384;

template <int Alignment>
mozilla::Result<size_t, nsresult> CalculateI420FrameSize(int32_t aWidth,
                                                         int32_t aHeight) {
  MOZ_ASSERT(Alignment >= 1 && Alignment <= 16);

  if (aWidth <= 0 || aHeight <= 0 || aWidth > kMaxI420Dimension ||
      aHeight > kMaxI420Dimension) {
    return Err(NS_ERROR_INVALID_ARG);
  }
  constexpr size_t N = static_cast<size_t>(Alignment);
  auto aligned = [](size_t v) -> size_t {
    // The `- 1` avoids overreaching when `v % N == 0`.
    return v < N ? N : ((v - 1) / N + 1) * N;
  };
  const size_t w = static_cast<size_t>(aWidth);
  const size_t h = static_cast<size_t>(aHeight);
  const size_t yPlaneSize = aligned(w) * h;
  const size_t uvPlaneSize = aligned((w + 1) / 2) * ((h + 1) / 2);
  return yPlaneSize + uvPlaneSize * 2;
}

nsresult CreateEncoderConfig(int32_t aWidth, int32_t aHeight,
                             uint32_t aVideoBitrate, TrackRate aTrackRate,
                             int32_t aMaxKeyFrameDistance,
                             vpx_codec_enc_cfg_t* config) {
  // Encoder configuration structure.
  memset(config, 0, sizeof(vpx_codec_enc_cfg_t));
  if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), config, 0)) {
    VP8LOG(LogLevel::Error, "Failed to get default configuration");
    return NS_ERROR_FAILURE;
  }

  config->g_w = aWidth;
  config->g_h = aHeight;
  // TODO: Maybe we should have various aFrameRate bitrate pair for each
  // devices? or for different platform

  // rc_target_bitrate needs kbit/s
  config->rc_target_bitrate = std::max(
      1U, (aVideoBitrate != 0 ? aVideoBitrate : DEFAULT_BITRATE_BPS) / 1000);

  // Setting the time base of the codec
  config->g_timebase.num = 1;
  config->g_timebase.den = aTrackRate;

  // No error resilience as this is not intended for UDP transports
  config->g_error_resilient = 0;

  // Allow some frame lagging for large timeslices (when low latency is not
  // needed)
  /*std::min(10U, mKeyFrameInterval / 200)*/
  config->g_lag_in_frames = 0;

  int32_t number_of_cores = PR_GetNumberOfProcessors();
  if (aWidth * aHeight > 1920 * 1080 && number_of_cores >= 8) {
    config->g_threads = 4;  // 4 threads for > 1080p.
  } else if (aWidth * aHeight > 1280 * 960 && number_of_cores >= 6) {
    config->g_threads = 3;  // 3 threads for 1080p.
  } else if (aWidth * aHeight > 640 * 480 && number_of_cores >= 3) {
    config->g_threads = 2;  // 2 threads for qHD/HD.
  } else {
    config->g_threads = 1;  // 1 thread for VGA or less
  }

  // rate control settings

  // No frame dropping
  config->rc_dropframe_thresh = 0;
  // Variable bitrate
  config->rc_end_usage = VPX_VBR;
  // Single pass encoding
  config->g_pass = VPX_RC_ONE_PASS;
  // ffmpeg doesn't currently support streams that use resize.
  // Therefore, for safety, we should turn it off until it does.
  config->rc_resize_allowed = 0;
  // Allows 100% under target bitrate to compensate for prior overshoot
  config->rc_undershoot_pct = 100;
  // Allows 15% over target bitrate to compensate for prior undershoot
  config->rc_overshoot_pct = 15;
  // Tells the decoding application to buffer 500ms before beginning playback
  config->rc_buf_initial_sz = 500;
  // The decoding application will try to keep 600ms of buffer during playback
  config->rc_buf_optimal_sz = 600;
  // The decoding application may buffer 1000ms worth of encoded data
  config->rc_buf_sz = 1000;

  // We set key frame interval to automatic and try to set kf_max_dist so that
  // the encoder chooses to put keyframes slightly more often than
  // mKeyFrameInterval (which will encode with VPX_EFLAG_FORCE_KF when reached).
  config->kf_mode = VPX_KF_AUTO;
  config->kf_max_dist = aMaxKeyFrameDistance;

  return NS_OK;
}
}  // namespace

VP8TrackEncoder::VP8TrackEncoder(RefPtr<DriftCompensator> aDriftCompensator,
                                 TrackRate aTrackRate,
                                 MediaQueue<EncodedFrame>& aEncodedDataQueue,
                                 FrameDroppingMode aFrameDroppingMode,
                                 Maybe<float> aKeyFrameIntervalFactor)
    : VideoTrackEncoder(std::move(aDriftCompensator), aTrackRate,
                        aEncodedDataQueue, aFrameDroppingMode),
      mKeyFrameInterval(
          TimeDuration::FromMilliseconds(DEFAULT_KEYFRAME_INTERVAL_MS)),
      mKeyFrameIntervalFactor(aKeyFrameIntervalFactor.valueOr(
          DYNAMIC_MAXKFDIST_KFINTERVAL_FACTOR)) {
  MOZ_COUNT_CTOR(VP8TrackEncoder);
  CalculateMaxKeyFrameDistance().apply(
      [&](auto aKfd) { SetMaxKeyFrameDistance(aKfd); });
}

VP8TrackEncoder::~VP8TrackEncoder() {
  Destroy();
  MOZ_COUNT_DTOR(VP8TrackEncoder);
}

void VP8TrackEncoder::Destroy() {
  if (mInitialized) {
    vpx_codec_destroy(&mVPXContext);
  }

  mInitialized = false;
}

Maybe<int32_t> VP8TrackEncoder::CalculateMaxKeyFrameDistance(
    Maybe<float> aEstimatedFrameRate /* = Nothing() */) const {
  if (!aEstimatedFrameRate && mMeanFrameDuration.empty()) {
    // Not enough data to make a new calculation.
    return Nothing();
  }

  // Calculate an estimation of our current framerate
  const float estimatedFrameRate = aEstimatedFrameRate.valueOrFrom(
      [&] { return 1.0f / mMeanFrameDuration.mean().ToSeconds(); });
  // Set a kf_max_dist that should avoid triggering the VPX_EFLAG_FORCE_KF flag
  return Some(std::max(
      1, static_cast<int32_t>(estimatedFrameRate * mKeyFrameIntervalFactor *
                              mKeyFrameInterval.ToSeconds())));
}

void VP8TrackEncoder::SetMaxKeyFrameDistance(int32_t aMaxKeyFrameDistance) {
  if (mInitialized) {
    VP8LOG(
        LogLevel::Debug,
        "{} SetMaxKeyFrameDistance() set kf_max_dist to {} based on estimated "
        "framerate {:.2f}fps keyframe-factor {:.2f} and keyframe-interval "
        "{:.2f}s",
        fmt::ptr(this), aMaxKeyFrameDistance,
        1 / mMeanFrameDuration.mean().ToSeconds(), mKeyFrameIntervalFactor,
        mKeyFrameInterval.ToSeconds());
    DebugOnly<nsresult> rv =
        Reconfigure(mFrameWidth, mFrameHeight, aMaxKeyFrameDistance);
    MOZ_ASSERT(
        NS_SUCCEEDED(rv),
        "Reconfig for new key frame distance with proven size should succeed");
  } else {
    VP8LOG(LogLevel::Debug, "{} SetMaxKeyFrameDistance() distance={}",
           fmt::ptr(this), aMaxKeyFrameDistance);
    mMaxKeyFrameDistance = Some(aMaxKeyFrameDistance);
  }
}

nsresult VP8TrackEncoder::Init(int32_t aWidth, int32_t aHeight,
                               int32_t aDisplayWidth, int32_t aDisplayHeight,
                               float aEstimatedFrameRate) {
  auto frameSizeResult =
      CalculateI420FrameSize<I420_STRIDE_ALIGN>(aWidth, aHeight);
  if (frameSizeResult.isErr()) {
    VP8LOG(LogLevel::Warning, "Invalid size: {}x{}", aWidth, aHeight);
    return NS_ERROR_FAILURE;
  }

  if (aDisplayWidth < 1 || aDisplayHeight < 1) {
    return NS_ERROR_FAILURE;
  }

  if (aEstimatedFrameRate <= 0) {
    return NS_ERROR_FAILURE;
  }

  int32_t maxKeyFrameDistance =
      *CalculateMaxKeyFrameDistance(Some(aEstimatedFrameRate));

  nsresult rv = InitInternal(aWidth, aHeight, maxKeyFrameDistance);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(!mI420Frame);
  MOZ_ASSERT(mI420FrameSize == 0);
  const size_t frameSize = frameSizeResult.unwrap();
  mI420Frame.reset(new (fallible) uint8_t[frameSize]);
  mI420FrameSize = mI420Frame ? frameSize : 0;
  if (!mI420Frame) {
    VP8LOG(LogLevel::Warning, "Allocating I420 frame of size {} failed",
           frameSize);
    return NS_ERROR_FAILURE;
  }
  vpx_img_wrap(&mVPXImageWrapper, VPX_IMG_FMT_I420, aWidth, aHeight,
               I420_STRIDE_ALIGN, mI420Frame.get());

  if (!mMetadata) {
    mMetadata = MakeAndAddRef<VP8Metadata>();
    mMetadata->mWidth = aWidth;
    mMetadata->mHeight = aHeight;
    mMetadata->mDisplayWidth = aDisplayWidth;
    mMetadata->mDisplayHeight = aDisplayHeight;

    VP8LOG(LogLevel::Info,
           "{} Init() created metadata. width={}, height={}, displayWidth={}, "
           "displayHeight={}, framerate={:.2f}",
           fmt::ptr(this), mMetadata->mWidth, mMetadata->mHeight,
           mMetadata->mDisplayWidth, mMetadata->mDisplayHeight,
           aEstimatedFrameRate);

    SetInitialized();
  }

  return NS_OK;
}

nsresult VP8TrackEncoder::InitInternal(int32_t aWidth, int32_t aHeight,
                                       int32_t aMaxKeyFrameDistance) {
  MOZ_ASSERT(CalculateI420FrameSize<I420_STRIDE_ALIGN>(aWidth, aHeight).isOk());

  if (aWidth < 1 || aHeight < 1) {
    return NS_ERROR_FAILURE;
  }

  if (mInitialized) {
    MOZ_ASSERT(false);
    return NS_ERROR_FAILURE;
  }

  VP8LOG(LogLevel::Debug,
         "{} InitInternal(). width={}, height={}, kf_max_dist={}",
         fmt::ptr(this), aWidth, aHeight, aMaxKeyFrameDistance);

  // Encoder configuration structure.
  vpx_codec_enc_cfg_t config;
  nsresult rv = CreateEncoderConfig(aWidth, aHeight, mVideoBitrate, mTrackRate,
                                    aMaxKeyFrameDistance, &config);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  vpx_codec_flags_t flags = 0;
  flags |= VPX_CODEC_USE_OUTPUT_PARTITION;
  if (vpx_codec_enc_init(&mVPXContext, vpx_codec_vp8_cx(), &config, flags)) {
    return NS_ERROR_FAILURE;
  }

  vpx_codec_control(&mVPXContext, VP8E_SET_STATIC_THRESHOLD, 1);
  vpx_codec_control(&mVPXContext, VP8E_SET_CPUUSED, 15);
  vpx_codec_control(&mVPXContext, VP8E_SET_TOKEN_PARTITIONS,
                    VP8_TWO_TOKENPARTITION);

  mFrameWidth = aWidth;
  mFrameHeight = aHeight;
  mMaxKeyFrameDistance = Some(aMaxKeyFrameDistance);

  return NS_OK;
}

nsresult VP8TrackEncoder::Reconfigure(int32_t aWidth, int32_t aHeight,
                                      int32_t aMaxKeyFrameDistance) {
  if (!mInitialized) {
    MOZ_ASSERT(false);
    return NS_ERROR_FAILURE;
  }

  bool needsReInit = aMaxKeyFrameDistance != *mMaxKeyFrameDistance;

  if (aWidth != mFrameWidth || aHeight != mFrameHeight) {
    VP8LOG(LogLevel::Info, "Dynamic resolution change ({}x{} -> {}x{}).",
           mFrameWidth, mFrameHeight, aWidth, aHeight);
    auto newSizeResult =
        CalculateI420FrameSize<I420_STRIDE_ALIGN>(aWidth, aHeight);
    if (newSizeResult.isErr()) {
      VP8LOG(LogLevel::Warning, "Invalid size: {}x{}", aWidth, aHeight);
      return NS_ERROR_FAILURE;
    }
    const size_t newSize = newSizeResult.unwrap();
    if (newSize > mI420FrameSize) {
      needsReInit = true;
      mI420Frame.reset(new (fallible) uint8_t[newSize]);
      mI420FrameSize = mI420Frame ? newSize : 0;
    }
    if (!mI420Frame) {
      VP8LOG(LogLevel::Warning, "Allocating I420 frame of size {} failed",
             newSize);
      return NS_ERROR_FAILURE;
    }
    vpx_img_wrap(&mVPXImageWrapper, VPX_IMG_FMT_I420, aWidth, aHeight,
                 I420_STRIDE_ALIGN, mI420Frame.get());
  }

  if (needsReInit) {
    Destroy();
    mMaxKeyFrameDistance = Some(aMaxKeyFrameDistance);
    nsresult rv = InitInternal(aWidth, aHeight, aMaxKeyFrameDistance);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
    mInitialized = true;
    return NS_OK;
  }

  // Encoder configuration structure.
  vpx_codec_enc_cfg_t config;
  nsresult rv = CreateEncoderConfig(aWidth, aHeight, mVideoBitrate, mTrackRate,
                                    aMaxKeyFrameDistance, &config);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
  // Set new configuration
  if (vpx_codec_enc_config_set(&mVPXContext, &config) != VPX_CODEC_OK) {
    VP8LOG(LogLevel::Error, "Failed to set new configuration");
    return NS_ERROR_FAILURE;
  }

  mFrameWidth = aWidth;
  mFrameHeight = aHeight;

  return NS_OK;
}

already_AddRefed<TrackMetadataBase> VP8TrackEncoder::GetMetadata() {
  AUTO_PROFILER_LABEL("VP8TrackEncoder::GetMetadata", OTHER);

  MOZ_ASSERT(mInitialized);

  if (!mInitialized) {
    return nullptr;
  }

  MOZ_ASSERT(mMetadata);
  return do_AddRef(mMetadata);
}

Result<RefPtr<EncodedFrame>, nsresult> VP8TrackEncoder::ExtractEncodedData() {
  vpx_codec_iter_t iter = nullptr;
  EncodedFrame::FrameType frameType = EncodedFrame::VP8_P_FRAME;
  auto frameData = MakeRefPtr<EncodedFrame::FrameData>();
  const vpx_codec_cx_pkt_t* pkt = nullptr;
  while ((pkt = vpx_codec_get_cx_data(&mVPXContext, &iter)) != nullptr) {
    switch (pkt->kind) {
      case VPX_CODEC_CX_FRAME_PKT: {
        // Copy the encoded data from libvpx to frameData
        frameData->AppendElements((uint8_t*)pkt->data.frame.buf,
                                  pkt->data.frame.sz);
        break;
      }
      default: {
        break;
      }
    }
    // End of frame
    if ((pkt->data.frame.flags & VPX_FRAME_IS_FRAGMENT) == 0) {
      if (pkt->data.frame.flags & VPX_FRAME_IS_KEY) {
        frameType = EncodedFrame::VP8_I_FRAME;
      }
      break;
    }
  }

  if (frameData->IsEmpty()) {
    return RefPtr<EncodedFrame>(nullptr);
  }

  if (!pkt) {
    // This check silences a coverity warning about accessing a null pkt below.
    return RefPtr<EncodedFrame>(nullptr);
  }

  if (pkt->data.frame.flags & VPX_FRAME_IS_KEY) {
    // Update the since-last-keyframe counter, and account for this frame's
    // time.
    TrackTime frameTime = pkt->data.frame.pts;
    DebugOnly<TrackTime> frameDuration = pkt->data.frame.duration;
    MOZ_ASSERT(frameTime + frameDuration <= mEncodedTimestamp);
    mDurationSinceLastKeyframe =
        std::min(mDurationSinceLastKeyframe, mEncodedTimestamp - frameTime);
  }

  // Convert the timestamp and duration to Usecs.
  media::TimeUnit timestamp = media::TimeUnit(pkt->data.frame.pts, mTrackRate);
  if (!timestamp.IsValid()) {
    NS_ERROR("Microsecond timestamp overflow");
    return Err(NS_ERROR_DOM_MEDIA_OVERFLOW_ERR);
  }

  mExtractedDuration += pkt->data.frame.duration;
  if (!mExtractedDuration.isValid()) {
    NS_ERROR("Duration overflow");
    return Err(NS_ERROR_DOM_MEDIA_OVERFLOW_ERR);
  }

  media::TimeUnit totalDuration =
      media::TimeUnit(mExtractedDuration.value(), mTrackRate);
  if (!totalDuration.IsValid()) {
    NS_ERROR("Duration overflow");
    return Err(NS_ERROR_DOM_MEDIA_OVERFLOW_ERR);
  }

  media::TimeUnit duration = totalDuration - mExtractedDurationUs;
  if (!duration.IsValid()) {
    NS_ERROR("Duration overflow");
    return Err(NS_ERROR_DOM_MEDIA_OVERFLOW_ERR);
  }

  mExtractedDurationUs = totalDuration;

  VP8LOG(LogLevel::Verbose,
         "ExtractEncodedData TimeStamp {:.2f}, Duration {:.2f}, FrameType {}",
         timestamp.ToSeconds(), duration.ToSeconds(),
         static_cast<int>(frameType));

  if (static_cast<int>(totalDuration.ToSeconds()) /
          DYNAMIC_MAXKFDIST_CHECK_INTERVAL >
      static_cast<int>(mLastKeyFrameDistanceUpdate.ToSeconds()) /
          DYNAMIC_MAXKFDIST_CHECK_INTERVAL) {
    // The interval has passed since the last keyframe update. Update again.
    mLastKeyFrameDistanceUpdate = totalDuration;
    const int32_t maxKfDistance =
        CalculateMaxKeyFrameDistance().valueOr(*mMaxKeyFrameDistance);
    const float diffFactor =
        static_cast<float>(maxKfDistance) / *mMaxKeyFrameDistance;
    VP8LOG(LogLevel::Debug, "maxKfDistance: {}, factor: {:.2f}", maxKfDistance,
           diffFactor);
    if (std::abs(1.0 - diffFactor) > DYNAMIC_MAXKFDIST_DIFFACTOR) {
      SetMaxKeyFrameDistance(maxKfDistance);
    }
  }

  return MakeRefPtr<EncodedFrame>(timestamp, duration.ToMicroseconds(),
                                  PR_USEC_PER_SEC, frameType,
                                  std::move(frameData));
}

/**
 * Encoding flow in Encode():
 * 1: Assert valid state.
 * 2: Encode the video chunks in mSourceSegment in a for-loop.
 * 2.1: The duration is taken straight from the video chunk's duration.
 * 2.2: Setup the video chunk with mVPXImageWrapper by PrepareRawFrame().
 * 2.3: Pass frame to vp8 encoder by vpx_codec_encode().
 * 2.4: Extract the encoded frame from encoder by ExtractEncodedData().
 * 2.5: Set the nextEncodeOperation for the next frame.
 * 2.6: If we are not skipping the next frame, add the encoded frame to
 *      mEncodedDataQueue. If we are skipping the next frame, extend the encoded
 *      frame's duration in the next run of the loop.
 * 3. Clear aSegment.
 */
nsresult VP8TrackEncoder::Encode(VideoSegment* aSegment) {
  MOZ_ASSERT(mInitialized);
  MOZ_ASSERT(!IsEncodingComplete());

  AUTO_PROFILER_LABEL("VP8TrackEncoder::Encode", OTHER);

  EncodeOperation nextEncodeOperation = ENCODE_NORMAL_FRAME;

  RefPtr<EncodedFrame> encodedFrame;
  for (VideoSegment::ChunkIterator iter(*aSegment); !iter.IsEnded();
       iter.Next()) {
    VideoChunk& chunk = *iter;

    VP8LOG(LogLevel::Verbose,
           "nextEncodeOperation is {} for frame of duration {}",
           static_cast<int>(nextEncodeOperation), chunk.GetDuration());

    TimeStamp timebase = TimeStamp::Now();

    // Encode frame.
    if (nextEncodeOperation != SKIP_FRAME) {
      MOZ_ASSERT(!encodedFrame);
      nsresult rv = PrepareRawFrame(chunk);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

      // Encode the data with VP8 encoder
      int flags = 0;
      if (nextEncodeOperation == ENCODE_I_FRAME) {
        VP8LOG(LogLevel::Warning,
               "MediaRecorder lagging behind. Encoding keyframe.");
        flags |= VPX_EFLAG_FORCE_KF;
      }

      // Sum duration of non-key frames and force keyframe if exceeded the
      // given keyframe interval
      if (mKeyFrameInterval > TimeDuration::FromSeconds(0)) {
        if (media::TimeUnit(mDurationSinceLastKeyframe, mTrackRate)
                .ToTimeDuration() >= mKeyFrameInterval) {
          VP8LOG(LogLevel::Warning,
                 "Reached mKeyFrameInterval without seeing a keyframe. Forcing "
                 "one. time: {:.2f}, interval: {:.2f}",
                 media::TimeUnit(mDurationSinceLastKeyframe, mTrackRate)
                     .ToSeconds(),
                 mKeyFrameInterval.ToSeconds());
          mDurationSinceLastKeyframe = 0;
          flags |= VPX_EFLAG_FORCE_KF;
        }
        mDurationSinceLastKeyframe += chunk.GetDuration();
      }

      if (vpx_codec_encode(&mVPXContext, &mVPXImageWrapper, mEncodedTimestamp,
                           (unsigned long)chunk.GetDuration(), flags,
                           VPX_DL_REALTIME)) {
        VP8LOG(LogLevel::Error, "vpx_codec_encode failed to encode the frame.");
        return NS_ERROR_FAILURE;
      }

      // Move forward the mEncodedTimestamp.
      mEncodedTimestamp += chunk.GetDuration();

      // Extract the encoded data from the underlying encoder and push it to
      // mEncodedDataQueue.
      auto result = ExtractEncodedData();
      if (result.isErr()) {
        VP8LOG(LogLevel::Error, "ExtractEncodedData failed.");
        return NS_ERROR_FAILURE;
      }

      MOZ_ASSERT(result.inspect(),
                 "We expected a frame here. EOS is handled explicitly later");
      encodedFrame = result.unwrap();
    } else {
      // SKIP_FRAME

      MOZ_DIAGNOSTIC_ASSERT(encodedFrame);

      if (mKeyFrameInterval > TimeDuration::FromSeconds(0)) {
        mDurationSinceLastKeyframe += chunk.GetDuration();
      }

      // Move forward the mEncodedTimestamp.
      mEncodedTimestamp += chunk.GetDuration();

      // Extend the duration of the last encoded frame in mEncodedDataQueue
      // because this frame will be skipped.
      VP8LOG(LogLevel::Warning,
             "MediaRecorder lagging behind. Skipping a frame.");

      mExtractedDuration += chunk.mDuration;
      if (!mExtractedDuration.isValid()) {
        NS_ERROR("skipped duration overflow");
        return NS_ERROR_DOM_MEDIA_OVERFLOW_ERR;
      }

      media::TimeUnit totalDuration =
          media::TimeUnit(mExtractedDuration.value(), mTrackRate);
      media::TimeUnit skippedDuration = totalDuration - mExtractedDurationUs;
      mExtractedDurationUs = totalDuration;
      if (!skippedDuration.IsValid()) {
        NS_ERROR("skipped duration overflow");
        return NS_ERROR_DOM_MEDIA_OVERFLOW_ERR;
      }

      encodedFrame = MakeRefPtr<EncodedFrame>(
          encodedFrame->mTime,
          encodedFrame->mDuration + skippedDuration.ToMicroseconds(),
          encodedFrame->mDurationBase, encodedFrame->mFrameType,
          encodedFrame->mFrameData);
    }

    mMeanFrameEncodeDuration.insert(TimeStamp::Now() - timebase);
    mMeanFrameDuration.insert(
        media::TimeUnit(chunk.GetDuration(), mTrackRate).ToTimeDuration());
    nextEncodeOperation = GetNextEncodeOperation(
        mMeanFrameEncodeDuration.mean(), mMeanFrameDuration.mean());

    if (nextEncodeOperation != SKIP_FRAME) {
      // Note that the next operation might be SKIP_FRAME even if there is no
      // next frame.
      mEncodedDataQueue.Push(encodedFrame.forget());
    }
  }

  if (encodedFrame) {
    // Push now if we ended on a SKIP_FRAME before.
    mEncodedDataQueue.Push(encodedFrame.forget());
  }

  // Remove the chunks we have processed.
  aSegment->Clear();

  if (mEndOfStream) {
    // EOS: Extract the remaining frames from the underlying encoder.
    VP8LOG(LogLevel::Debug, "mEndOfStream is true");
    // No more frames will be encoded. Clearing temporary frames saves some
    // memory.
    if (mI420Frame) {
      mI420Frame = nullptr;
      mI420FrameSize = 0;
    }
    // mMuteFrame must be released before gfx shutdown. We do it now since it
    // may be too late when this VP8TrackEncoder gets destroyed.
    mMuteFrame = nullptr;
    // Bug 1243611, keep calling vpx_codec_encode and vpx_codec_get_cx_data
    // until vpx_codec_get_cx_data return null.
    while (true) {
      if (vpx_codec_encode(&mVPXContext, nullptr, mEncodedTimestamp, 0, 0,
                           VPX_DL_REALTIME)) {
        return NS_ERROR_FAILURE;
      }
      auto result = ExtractEncodedData();
      if (result.isErr()) {
        return NS_ERROR_FAILURE;
      }
      if (!result.inspect()) {
        // Null means end-of-stream.
        break;
      }
      mEncodedDataQueue.Push(result.unwrap().forget());
    }
    mEncodedDataQueue.Finish();
  }

  return NS_OK;
}

nsresult VP8TrackEncoder::PrepareRawFrame(VideoChunk& aChunk) {
  RefPtr<Image> img;
  if (aChunk.mFrame.GetForceBlack() || aChunk.IsNull()) {
    // Generate the black frame at the source image's size, or the encoder's
    // coded size when the chunk carries no image, matching the real-frame path.
    Image* chunkImage = aChunk.mFrame.GetImage();
    const gfx::IntSize blackImageSize =
        chunkImage ? chunkImage->GetSize()
                   : gfx::IntSize(mFrameWidth, mFrameHeight);
    if (!mMuteFrame || mMuteFrame->GetSize() != blackImageSize) {
      mMuteFrame = aChunk.mFrame.CloneAsBlackImage(blackImageSize);
    }
    if (!mMuteFrame) {
      VP8LOG(LogLevel::Warning, "Failed to allocate black image of size {}x{}",
             blackImageSize.width, blackImageSize.height);
      return NS_ERROR_FAILURE;
    }
    img = mMuteFrame;
  } else {
    img = aChunk.mFrame.GetImage();
  }

  gfx::IntSize imgSize = img->GetSize();
  if (imgSize != IntSize(mFrameWidth, mFrameHeight)) {
    nsresult rv =
        Reconfigure(imgSize.width, imgSize.height, *mMaxKeyFrameDistance);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  MOZ_ASSERT(mFrameWidth == imgSize.width);
  MOZ_ASSERT(mFrameHeight == imgSize.height);

  nsresult rv = ConvertToI420(img, mVPXImageWrapper.planes[VPX_PLANE_Y],
                              mVPXImageWrapper.stride[VPX_PLANE_Y],
                              mVPXImageWrapper.planes[VPX_PLANE_U],
                              mVPXImageWrapper.stride[VPX_PLANE_U],
                              mVPXImageWrapper.planes[VPX_PLANE_V],
                              mVPXImageWrapper.stride[VPX_PLANE_V], imgSize);
  if (NS_FAILED(rv)) {
    VP8LOG(LogLevel::Error, "Converting to I420 failed");
    return rv;
  }

  return NS_OK;
}

// These two define value used in GetNextEncodeOperation to determine the
// EncodeOperation for next target frame.
#define I_FRAME_RATIO (0.85)  // Effectively disabled, because perceived quality
#define SKIP_FRAME_RATIO (0.85)

/**
 * Compares the elapsed time from the beginning of GetEncodedTrack and
 * the processed frame duration in mSourceSegment
 * in order to set the nextEncodeOperation for next target frame.
 */
VP8TrackEncoder::EncodeOperation VP8TrackEncoder::GetNextEncodeOperation(
    TimeDuration aTimeElapsed, TimeDuration aProcessedDuration) {
  if (mFrameDroppingMode == FrameDroppingMode::DISALLOW) {
    return ENCODE_NORMAL_FRAME;
  }

  if (aTimeElapsed.ToSeconds() >
      aProcessedDuration.ToSeconds() * SKIP_FRAME_RATIO) {
    // The encoder is too slow.
    // We should skip next frame to consume the mSourceSegment.
    return SKIP_FRAME;
  }

  if (aTimeElapsed.ToSeconds() >
      aProcessedDuration.ToSeconds() * I_FRAME_RATIO) {
    // The encoder is a little slow.
    // We force the encoder to encode an I-frame to accelerate.
    return ENCODE_I_FRAME;
  }

  return ENCODE_NORMAL_FRAME;
}

}  // namespace mozilla

#undef VP8LOG
