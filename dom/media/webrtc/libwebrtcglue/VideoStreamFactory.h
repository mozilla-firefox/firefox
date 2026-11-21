/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef VideoStreamFactory_h
#define VideoStreamFactory_h

#include "CodecConfig.h"
#include "mozilla/Atomics.h"
#include "mozilla/DataMutex.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/UniquePtr.h"
#include "api/video/video_source_interface.h"
#include "common_video/framerate_controller.h"
#include "rtc_base/time_utils.h"
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

  static ResolutionAndBitrateLimits GetLimitsFor(unsigned int aWidth,
                                                 unsigned int aHeight,
                                                 int aCapBps = 0);

  VideoStreamFactory(VideoCodecConfig aConfig,
                     webrtc::VideoCodecMode aCodecMode, int aMinBitrate,
                     int aStartBitrate, int aPrefMaxBitrate,
                     int aNegotiatedMaxBitrate,
                     const rtc::VideoSinkWants& aWants, bool aLockScaling)
      : mCodecMode(aCodecMode),
        mMaxFramerateForAllStreams(std::numeric_limits<unsigned int>::max()),
        mCodecConfig(std::forward<VideoCodecConfig>(aConfig)),
        mMinBitrate(aMinBitrate),
        mStartBitrate(aStartBitrate),
        mPrefMaxBitrate(aPrefMaxBitrate),
        mNegotiatedMaxBitrate(aNegotiatedMaxBitrate),
        mFramerateController("VideoStreamFactory::mFramerateController"),
        mWants(aWants),
        mLockScaling(aLockScaling) {}

  // This gets called off-main thread and may hold internal webrtc.org
  // locks. May *NOT* lock the conduit's mutex, to avoid deadlocks.
  std::vector<webrtc::VideoStream> CreateEncoderStreams(
      int aWidth, int aHeight,
      const webrtc::VideoEncoderConfig& aConfig) override;

  /**
   * Called by CreateEncoderStreams and
   * WebrtcVideoConduit::OnControlConfigChange to set VideoStream.max_framerate.
   */
  void SelectMaxFramerate(int aWidth, int aHeight,
                          const VideoCodecConfig::Encoding& aEncoding,
                          webrtc::VideoStream& aVideoStream);

  /**
   * Function to select and change the encoding resolution based on incoming
   * frame size and current available bandwidth.
   * @param width, height: dimensions of the frame
   */
  void SelectMaxFramerateForAllStreams(unsigned short aWidth,
                                       unsigned short aHeight);

  /**
   * Function to determine if the frame should be dropped based on the given
   * frame's resolution (combined with the factory's scaleResolutionDownBy) or
   * timestamp.
   * @param aFrame frame to be evaluated.
   * @return true if frame should be dropped, false otehrwise.
   */
  bool ShouldDropFrame(const webrtc::VideoFrame& aFrame);

 private:
  /**
   * Function to calculate a scaled down width and height based on
   * scaleDownByResolution, maxFS, and max pixel count settings.
   * @param aWidth current frame width
   * @param aHeight current frame height
   * @param aScaleDownByResolution value to scale width and height down by.
   * @param aMaxPixelCount maximum number of pixels wanted in a frame.
   * @return a gfx:IntSize containing  width and height to use. These may match
   * the aWidth and aHeight passed in if no scaling was needed.
   */
  gfx::IntSize CalculateScaledResolution(int aWidth, int aHeight,
                                         double aScaleDownByResolution,
                                         unsigned int aMaxPixelCount);

  /**
   * Function to select and change the encoding frame rate based on incoming
   * frame rate, current frame size and max-mbps setting.
   * @param aOldFramerate current framerate
   * @param aSendingWidth width of frames being sent
   * @param aSendingHeight height of frames being sent
   * @return new framerate meeting max-mbps requriements based on frame size
   */
  unsigned int SelectFrameRate(unsigned int aOldFramerate,
                               unsigned short aSendingWidth,
                               unsigned short aSendingHeight);

  // Used to limit number of streams for screensharing.
  Atomic<webrtc::VideoCodecMode> mCodecMode;

  // The framerate we're currently sending at.
  Atomic<unsigned int> mMaxFramerateForAllStreams;

  // The current send codec config, containing simulcast layer configs.
  const VideoCodecConfig mCodecConfig;

  // Bitrate limits in bps.
  const int mMinBitrate = 0;
  const int mStartBitrate = 0;
  const int mPrefMaxBitrate = 0;
  const int mNegotiatedMaxBitrate = 0;

  // DatamMutex used as object is mutated from a libwebrtc thread and
  // a seperate thread used to pass video frames to libwebrtc.
  DataMutex<webrtc::FramerateController> mFramerateController;

  const rtc::VideoSinkWants mWants;
  const bool mLockScaling;
};

}  // namespace mozilla

#endif
