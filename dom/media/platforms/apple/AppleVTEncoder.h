/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AppleVTEncoder_h_
#define mozilla_AppleVTEncoder_h_

#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>

#include "apple/AppleUtils.h"
#include "PlatformEncoderModule.h"

namespace mozilla {

namespace layers {
class Image;
}

class AppleVTEncoder final : public MediaDataEncoder {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AppleVTEncoder, final);

  AppleVTEncoder(const EncoderConfig& aConfig,
                 const RefPtr<TaskQueue>& aTaskQueue)
      : mConfig(aConfig),
        mTaskQueue(aTaskQueue),
        mHardwareNotAllowed(aConfig.mHardwarePreference ==
                            HardwarePreference::RequireSoftware),
        mError(NS_OK),
        mSession(nullptr),
        mTimer(nullptr) {
    MOZ_ASSERT(mConfig.mSize.width > 0 && mConfig.mSize.height > 0);
    MOZ_ASSERT(mTaskQueue);
  }

  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) override;

  nsCString GetDescriptionName() const override {
    MOZ_ASSERT(mSession);
    return mIsHardwareAccelerated ? "apple hardware VT encoder"_ns
                                  : "apple software VT encoder"_ns;
  }

  void OutputFrame(OSStatus aStatus, VTEncodeInfoFlags aFlags,
                   CMSampleBufferRef aBuffer);

 private:
  enum class EncodeResult { Success, EncodeError, FrameDropped, EmptyBuffer };

  virtual ~AppleVTEncoder() { MOZ_ASSERT(!mSession); }
  void ProcessEncode(const RefPtr<const VideoData>& aSample);
  RefPtr<ReconfigurationPromise> ProcessReconfigure(
      const RefPtr<const EncoderConfigurationChangeList>&
          aConfigurationChanges);
  void ProcessOutput(RefPtr<MediaRawData>&& aOutput, EncodeResult aResult);
  void ForceOutputIfNeeded();
  void MaybeResolveOrRejectEncodePromise();
  RefPtr<EncodePromise> ProcessDrain();
  RefPtr<ShutdownPromise> ProcessShutdown();

  void InvalidateSessionIfNeeded();
  MediaResult InitSession();
  CFDictionaryRef BuildSourceImageBufferAttributes(OSType aPixelFormat);
  CVPixelBufferRef CreateCVPixelBuffer(layers::Image* aSource);
  bool WriteExtraData(MediaRawData* aDst, CMSampleBufferRef aSrc,
                      const bool aAsAnnexB);

  bool SetAverageBitrate(uint32_t aBitsPerSec);
  bool SetConstantBitrate(uint32_t aBitsPerSec);
  bool SetBitrateAndMode(BitrateMode aBitrateMode, uint32_t aBitsPerSec);
  bool SetFrameRate(int64_t aFPS);
  bool SetRealtime(bool aEnabled);
  bool SetProfileLevel(H264_PROFILE aValue);
  bool IsSettingColorSpaceSupported() const;
  MediaResult SetColorSpace(const EncoderConfig::SampleFormat& aFormat);

  void AssertOnTaskQueue() { MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn()); }

  EncoderConfig mConfig;
  const RefPtr<TaskQueue> mTaskQueue;
  const bool mHardwareNotAllowed;
  // Accessed only in mTaskQueue.
  EncodedData mEncodedData;
  // Accessed only in mTaskQueue.
  MozPromiseHolder<EncodePromise> mEncodePromise;
  RefPtr<MediaByteBuffer> mAvcc;  // Stores latest avcC data.
  MediaResult mError;

  // Written by Init() but used only in task queue.
  AutoCFTypeRef<VTCompressionSessionRef> mSession;
  // Can be accessed on any thread, but only written on during init.
  Atomic<bool> mIsHardwareAccelerated;
  // Accessed only in mTaskQueue. Used for for OS versions < 11.
  nsCOMPtr<nsITimer> mTimer;
};

}  // namespace mozilla

#endif  // mozilla_AppleVTEncoder_h_
