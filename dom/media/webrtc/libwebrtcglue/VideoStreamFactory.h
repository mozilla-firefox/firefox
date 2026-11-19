/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef VideoStreamFactory_h
#define VideoStreamFactory_h

#include "CodecConfig.h"
#include "mozilla/Atomics.h"
#include "mozilla/EventTargetCapability.h"
#include "mozilla/gfx/Point.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
class VideoFrame;
}

namespace mozilla {

// Factory class for VideoStreams... vie_encoder.cc will call this to
// reconfigure.
class VideoStreamFactory
    : public webrtc::VideoEncoderConfig::VideoStreamFactoryInterface {
 public:
  struct ResolutionAndBitrateLimits {
    int resolution_in_mb;
    int min_bitrate_bps;
    int start_bitrate_bps;
    int max_bitrate_bps;
  };

  static ResolutionAndBitrateLimits GetLimitsFor(gfx::IntSize aSize,
                                                 int aCapBps = 0);

  VideoStreamFactory(VideoCodecConfig aConfig, int aMinBitrate,
                     int aStartBitrate, int aPrefMaxBitrate,
                     int aNegotiatedMaxBitrate)
      : mMaxFramerateForAllStreams(std::numeric_limits<unsigned int>::max()),
        mCodecConfig(std::move(aConfig)),
        mMinBitrate(aMinBitrate),
        mStartBitrate(aStartBitrate),
        mPrefMaxBitrate(aPrefMaxBitrate),
        mNegotiatedMaxBitrate(aNegotiatedMaxBitrate) {}

  // This gets called off-main thread and may hold internal webrtc.org
  // locks. May *NOT* lock the conduit's mutex, to avoid deadlocks.
  std::vector<webrtc::VideoStream> CreateEncoderStreams(
      const webrtc::FieldTrialsView& field_trials, int aWidth, int aHeight,
      const webrtc::VideoEncoderConfig& aConfig) override
      MOZ_EXCLUDES(mEncodeQueue);

  // Called right before CreateEncoderStreams with info about the encoder
  // instance used.
  void SetEncoderInfo(const webrtc::VideoEncoder::EncoderInfo& aInfo) override
      MOZ_EXCLUDES(mEncodeQueue);

  /**
   * Called by CreateEncoderStreams and
   * WebrtcVideoConduit::OnControlConfigChange to set VideoStream.max_framerate.
   */
  void SelectResolutionAndMaxFramerate(
      gfx::IntSize aSize, const VideoCodecConfig::Encoding& aEncoding,
      webrtc::VideoStream& aVideoStream) MOZ_REQUIRES(mEncodeQueue);

  /**
   * Function to select and change the encoding resolution based on incoming
   * frame size and current available bandwidth.
   * @param aSize: dimensions of the frame
   */
  void SelectMaxFramerateForAllStreams(gfx::IntSize aSize);

 private:
  /**
   * Function to calculate a scaled down width and height based on
   * scaleDownByResolution, maxFS, and max pixel count settings.
   * @param aSize current frame size
   * @param aScaleDownByResolution value to scale width and height down by.
   * @return a gfx:IntSize containing  width and height to use. These may match
   *         the aSize passed in if no scaling was needed.
   */
  gfx::IntSize CalculateScaledResolution(gfx::IntSize aSize,
                                         double aScaleDownByResolution)
      MOZ_REQUIRES(mEncodeQueue);

  /**
   * Function to select and change the encoding frame rate based on incoming
   * frame rate, current frame size and max-mbps setting.
   * @param aOldFramerate current framerate
   * @param aSendingSize size of frames being sent
   * @return new framerate meeting max-mbps requriements based on frame size
   */
  unsigned int SelectFrameRate(unsigned int aOldFramerate, gfx::IntSize aSize);

  // The framerate we're currently sending at.
  Atomic<unsigned int> mMaxFramerateForAllStreams;

  Maybe<EventTargetCapability<nsISerialEventTarget>> mEncodeQueue;
  Maybe<int> mRequestedResolutionAlignment MOZ_GUARDED_BY(mEncodeQueue);

  // The current send codec config, containing simulcast layer configs.
  const VideoCodecConfig mCodecConfig;

  // Bitrate limits in bps.
  const int mMinBitrate = 0;
  const int mStartBitrate = 0;
  const int mPrefMaxBitrate = 0;
  const int mNegotiatedMaxBitrate = 0;
};

}  // namespace mozilla

#endif
