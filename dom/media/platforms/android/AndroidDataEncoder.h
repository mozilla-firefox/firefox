/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORMS_ANDROID_ANDROIDDATAENCODER_H_
#define DOM_MEDIA_PLATFORMS_ANDROID_ANDROIDDATAENCODER_H_

#include "JavaCallbacksSupport.h"
#include "MediaData.h"
#include "PlatformEncoderModule.h"
#include "gtest/MozGtestFriend.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"

namespace mozilla {

// The MediaCodec MIME type for aCodec, or "" if unsupported.
const char* AndroidEncoderMimeType(CodecType aCodec);

class AndroidDataEncoder final : public MediaDataEncoder {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidDataEncoder, final);

  AndroidDataEncoder(const EncoderConfig& aConfig,
                     const RefPtr<TaskQueue>& aTaskQueue)
      : mConfig(aConfig), mTaskQueue(aTaskQueue) {
    MOZ_ASSERT(mConfig.mSize.width > 0 && mConfig.mSize.height > 0);
    MOZ_ASSERT(mTaskQueue);
  }

  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<EncodePromise> Encode(nsTArray<RefPtr<MediaData>>&& aSamples) override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) override;
  RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override {
    // General reconfiguration interface not implemented right now
    return MediaDataEncoder::ReconfigurationPromise::CreateAndReject(
        NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
  };

  nsCString GetDescriptionName() const override { return "Android Encoder"_ns; }

 private:
  class CallbacksSupport final : public JavaCallbacksSupport {
   public:
    explicit CallbacksSupport(AndroidDataEncoder* aEncoder)
        : mMutex("AndroidDataEncoder::CallbacksSupport") {
      MutexAutoLock lock(mMutex);
      mEncoder = aEncoder;
    }

    ~CallbacksSupport() {
      MutexAutoLock lock(mMutex);
      mEncoder = nullptr;
    }

    void HandleInput(int64_t aTimestamp, bool aProcessed) override;
    void HandleOutput(java::Sample::Param aSample,
                      java::SampleBuffer::Param aBuffer) override;
    void HandleOutputFormatChanged(
        java::sdk::MediaFormat::Param aFormat) override;
    void HandleError(const MediaResult& aError) override;

   private:
    Mutex mMutex;
    AndroidDataEncoder* mEncoder MOZ_GUARDED_BY(mMutex);
  };
  friend class CallbacksSupport;

  FRIEND_TEST(MediaDataEncoderTest, AndroidDoesNotWriteBeyondConfiguredFrame);

  ~AndroidDataEncoder() override { MOZ_ASSERT(!mJavaEncoder); }

  // Methods only called on mTaskQueue.
  RefPtr<InitPromise> ProcessInit();
  RefPtr<EncodePromise> ProcessEncode(nsTArray<RefPtr<MediaData>>&& aSamples);
  RefPtr<EncodePromise> ProcessDrain();
  RefPtr<ShutdownPromise> ProcessShutdown();
  void ProcessInput();
  void ProcessOutput(java::Sample::GlobalRef&& aSample,
                     java::SampleBuffer::GlobalRef&& aBuffer);
  RefPtr<MediaRawData> GetOutputData(java::SampleBuffer::Param aBuffer,
                                     const int32_t aOffset, const int32_t aSize,
                                     const bool aIsKeyFrame);
  RefPtr<MediaRawData> GetOutputDataH264(java::SampleBuffer::Param aBuffer,
                                         const int32_t aOffset,
                                         const int32_t aSize,
                                         const bool aIsKeyFrame);
  void Error(const MediaResult& aError);

  void AssertOnTaskQueue() const {
    MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
  }

  EncoderConfig mConfig;

  RefPtr<TaskQueue> mTaskQueue;

  // Can be accessed on any thread, but only written on during init.
  bool mIsHardwareAccelerated = false;

  java::CodecProxy::GlobalRef mJavaEncoder;
  java::CodecProxy::NativeCallbacks::GlobalRef mJavaCallbacks;
  java::sdk::MediaFormat::GlobalRef mFormat;
  // Preallocated Java object used as a reusable storage for input buffer
  // information. Contents must be changed only on mTaskQueue.
  java::sdk::MediaCodec::BufferInfo::GlobalRef mInputBufferInfo;

  MozPromiseHolder<EncodePromise> mDrainPromise;

  // Accessed on mTaskqueue only.
  RefPtr<MediaByteBuffer> mYUVBuffer;
  EncodedData mEncodedData;
  // SPS/PPS NALUs for realtime usage, avcC otherwise.
  RefPtr<MediaByteBuffer> mConfigData;
  // Record the input sample duration because MediaCodec doesn’t provide it.
  media::TimeUnit mInputSampleDuration = media::TimeUnit::Invalid();

  enum class DrainState { DRAINABLE, DRAINING, DRAINED };
  DrainState mDrainState = DrainState::DRAINABLE;

  Maybe<MediaResult> mError;
};

}  // namespace mozilla

#endif
