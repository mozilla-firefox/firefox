/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AppleVTDecoder_h
#define mozilla_AppleVTDecoder_h

#include <CoreFoundation/CFDictionary.h>  // For CFDictionaryRef
#include <CoreMedia/CoreMedia.h>          // For CMVideoFormatDescriptionRef
#include <VideoToolbox/VideoToolbox.h>    // For VTDecompressionSessionRef

#include "AppleDecoderModule.h"
#include "PerformanceRecorder.h"
#include "PlatformDecoderModule.h"
#include "ReorderQueue.h"
#include "TimeUnits.h"
#include "apple/AppleUtils.h"
#include "mozilla/Atomics.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/ProfilerUtils.h"
#include "mozilla/gfx/Types.h"

namespace mozilla {

DDLoggedTypeDeclNameAndBase(AppleVTDecoder, MediaDataDecoder);

class AppleVTDecoder final : public MediaDataDecoder,
                             public DecoderDoctorLifeLogger<AppleVTDecoder> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AppleVTDecoder, final);

  AppleVTDecoder(const VideoInfo& aConfig,
                 layers::ImageContainer* aImageContainer,
                 const CreateDecoderParams::OptionSet& aOptions,
                 layers::KnowsCompositor* aKnowsCompositor,
                 Maybe<TrackingId> aTrackingId);

  class AppleFrameRef {
   public:
    media::TimeUnit decode_timestamp;
    media::TimeUnit composition_timestamp;
    media::TimeUnit duration;
    int64_t byte_offset;
    bool is_sync_point;

    explicit AppleFrameRef(const MediaRawData& aSample)
        : decode_timestamp(aSample.mTimecode),
          composition_timestamp(aSample.mTime),
          duration(aSample.mDuration),
          byte_offset(aSample.mOffset),
          is_sync_point(aSample.mKeyframe) {}
  };

  RefPtr<InitPromise> Init() override;
  RefPtr<DecodePromise> Decode(MediaRawData* aSample) override;
  RefPtr<DecodePromise> Drain() override;
  RefPtr<FlushPromise> Flush() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  void SetSeekThreshold(const media::TimeUnit& aTime) override;

  bool IsHardwareAccelerated(nsACString& aFailureReason) const override {
    return mIsHardwareAccelerated;
  }

  nsCString GetDescriptionName() const override {
    return mIsHardwareAccelerated ? "apple hardware VT decoder"_ns
                                  : "apple software VT decoder"_ns;
  }

  nsCString GetCodecName() const override;

  ConversionRequired NeedsConversion() const override {
    if (mStreamType == StreamType::H264) {
      return ConversionRequired::kNeedAVCC;
    }
    if (mStreamType == StreamType::HEVC) {
      return ConversionRequired::kNeedHVCC;
    }
    return ConversionRequired::kNeedNone;
  }

  // Access from the taskqueue and the decoder's thread.
  // OutputFrame is thread-safe.
  void OutputFrame(CVPixelBufferRef aImage, AppleFrameRef aFrameRef);
  void OnDecodeError(OSStatus aError);

 private:
  friend class AppleDecoderModule;  // To access InitializeSession.
  virtual ~AppleVTDecoder();
  RefPtr<FlushPromise> ProcessFlush();
  RefPtr<DecodePromise> ProcessDrain();
  void ProcessShutdown();
  void ProcessDecode(MediaRawData* aSample);
  void MaybeResolveBufferedFrames();

  void MaybeRegisterCallbackThread();

  void AssertOnTaskQueue() { MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn()); }

  AppleFrameRef* CreateAppleFrameRef(const MediaRawData* aSample);
  CFDictionaryRef CreateOutputConfiguration();

  const RefPtr<MediaByteBuffer> mExtraData;
  const uint32_t mPictureWidth;
  const uint32_t mPictureHeight;
  const uint32_t mDisplayWidth;
  const uint32_t mDisplayHeight;
  const gfx::YUVColorSpace mColorSpace;
  const gfx::ColorSpace2 mColorPrimaries;
  const gfx::TransferFunction mTransferFunction;
  const gfx::ColorRange mColorRange;
  const gfx::ColorDepth mColorDepth;

  // Method to set up the decompression session.
  MediaResult InitializeSession();
  nsresult WaitForAsynchronousFrames();
  CFDictionaryRef CreateDecoderSpecification();
  CFDictionaryRef CreateDecoderExtensions();

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(StreamType,
                                                     (Unknown, H264, VP9, AV1,
                                                      HEVC));

  StreamType GetStreamType(const nsCString& aMimeType) const;
  uint32_t GetMaxRefFrames(bool aIsLowLatency) const;

  const StreamType mStreamType;
  const RefPtr<TaskQueue> mTaskQueue;
  const uint32_t mMaxRefFrames;
  const RefPtr<layers::ImageContainer> mImageContainer;
  const RefPtr<layers::KnowsCompositor> mKnowsCompositor;
  const bool mUseSoftwareImages;
  const Maybe<TrackingId> mTrackingId;

  // Set on reader/decode thread calling Flush() to indicate that output is
  // not required and so input samples on mTaskQueue need not be processed.
  Atomic<bool> mIsFlushing;
  std::atomic<ProfilerThreadId> mCallbackThreadId;
  // Protects mReorderQueue and mPromise.
  Monitor mMonitor MOZ_UNANNOTATED;
  ReorderQueue mReorderQueue;
  MozMonitoredPromiseHolder<DecodePromise> mPromise;

  // Decoded frame will be dropped if its pts is smaller than this
  // value. It shold be initialized before Input() or after Flush(). So it is
  // safe to access it in OutputFrame without protecting.
  Maybe<media::TimeUnit> mSeekTargetThreshold;

  AutoCFTypeRef<CMVideoFormatDescriptionRef> mFormat;
  AutoCFTypeRef<VTDecompressionSessionRef> mSession;
  Atomic<bool> mIsHardwareAccelerated;
  PerformanceRecorderMulti<DecodeStage> mPerformanceRecorder;
};

}  // namespace mozilla

#endif  // mozilla_AppleVTDecoder_h
