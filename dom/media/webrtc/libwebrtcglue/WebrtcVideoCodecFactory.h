/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_WEBRTC_LIBWEBRTCGLUE_WEBRTCVIDEOCODECFACTORY_H_
#define DOM_MEDIA_WEBRTC_LIBWEBRTCGLUE_WEBRTCVIDEOCODECFACTORY_H_

#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "MediaEventSource.h"
#include "PerformanceRecorder.h"

namespace mozilla {
class GmpPluginNotifierInterface {
  virtual void DisconnectAll() = 0;
  virtual MediaEventSource<uint64_t>& CreatedGmpPluginEvent() = 0;
  virtual MediaEventSource<uint64_t>& ReleasedGmpPluginEvent() = 0;
};

class GmpPluginNotifier : public GmpPluginNotifierInterface {
 public:
  explicit GmpPluginNotifier(nsCOMPtr<nsISerialEventTarget> aOwningThread)
      : mOwningThread(std::move(aOwningThread)),
        mGmpPluginMutex("GmpPluginNotifier::mGmpPluginMutex"),
        mCreatedGmpPluginEvent(mOwningThread),
        mReleasedGmpPluginEvent(mOwningThread) {}

  ~GmpPluginNotifier() = default;

  void DisconnectAll() override {
    MOZ_ASSERT(mOwningThread->IsOnCurrentThread());
    MutexAutoLock lock(mGmpPluginMutex);
    mCreatedGmpPluginEvent.DisconnectAll();
    mReleasedGmpPluginEvent.DisconnectAll();
  }

  MediaEventSource<uint64_t>& CreatedGmpPluginEvent() override {
    return mCreatedGmpPluginEvent;
  }

  MediaEventSource<uint64_t>& ReleasedGmpPluginEvent() override {
    return mReleasedGmpPluginEvent;
  }

 protected:
  const nsCOMPtr<nsISerialEventTarget> mOwningThread;
  Mutex mGmpPluginMutex MOZ_UNANNOTATED;
  MediaEventForwarder<uint64_t> mCreatedGmpPluginEvent;
  MediaEventForwarder<uint64_t> mReleasedGmpPluginEvent;
};

class WebrtcVideoDecoderFactory : public GmpPluginNotifier,
                                  public webrtc::VideoDecoderFactory {
 public:
  WebrtcVideoDecoderFactory(nsCOMPtr<nsISerialEventTarget> aOwningThread,
                            std::string aPCHandle, TrackingId aTrackingId)
      : GmpPluginNotifier(std::move(aOwningThread)),
        mPCHandle(std::move(aPCHandle)),
        mTrackingId(std::move(aTrackingId)) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    MOZ_CRASH("Unexpected call");
    return std::vector<webrtc::SdpVideoFormat>();
  }

  std::unique_ptr<webrtc::VideoDecoder> Create(
      const webrtc::Environment& aEnv,
      const webrtc::SdpVideoFormat& aFormat) override;

 private:
  const std::string mPCHandle;
  const TrackingId mTrackingId;
};

class WebrtcVideoEncoderFactory : public GmpPluginNotifierInterface,
                                  public webrtc::VideoEncoderFactory {
  class InternalFactory : public GmpPluginNotifier,
                          public webrtc::VideoEncoderFactory {
   public:
    InternalFactory(nsCOMPtr<nsISerialEventTarget> aOwningThread,
                    std::string aPCHandle)
        : GmpPluginNotifier(std::move(aOwningThread)),
          mPCHandle(std::move(aPCHandle)) {}

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
      MOZ_CRASH("Unexpected call");
      return std::vector<webrtc::SdpVideoFormat>();
    }

    std::unique_ptr<webrtc::VideoEncoder> Create(
        const webrtc::Environment& env,
        const webrtc::SdpVideoFormat& format) override;

    bool Supports(const webrtc::SdpVideoFormat& aFormat);

   private:
    const std::string mPCHandle;
  };

 public:
  explicit WebrtcVideoEncoderFactory(
      nsCOMPtr<nsISerialEventTarget> aOwningThread, std::string aPCHandle)
      : mInternalFactory(MakeUnique<InternalFactory>(std::move(aOwningThread),
                                                     std::move(aPCHandle))) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    MOZ_CRASH("Unexpected call");
    return std::vector<webrtc::SdpVideoFormat>();
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& env,
      const webrtc::SdpVideoFormat& format) override;

  void DisconnectAll() override { mInternalFactory->DisconnectAll(); }

  MediaEventSource<uint64_t>& CreatedGmpPluginEvent() override {
    return mInternalFactory->CreatedGmpPluginEvent();
  }
  MediaEventSource<uint64_t>& ReleasedGmpPluginEvent() override {
    return mInternalFactory->ReleasedGmpPluginEvent();
  }

 private:
  const UniquePtr<InternalFactory> mInternalFactory;
};
}  // namespace mozilla

#endif
