/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaFormatReader.h"

#include <algorithm>
#include <map>
#include <queue>

#include "AllocationPolicy.h"
#ifdef MOZ_AV1
#  include "AOMDecoder.h"
#endif
#include "MediaData.h"
#include "MediaDataDecoderProxy.h"
#include "MediaInfo.h"
#include "MP4Decoder.h"
#include "PDMFactory.h"
#include "PerformanceRecorder.h"
#include "VideoFrameContainer.h"
#include "VideoUtils.h"
#include "VPXDecoder.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/CDMProxy.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/NotNull.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/Unused.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "nsContentUtils.h"
#include "nsLiteralString.h"
#include "nsPrintfCString.h"
#include "nsTHashSet.h"

using namespace mozilla::media;

static mozilla::LazyLogModule sFormatDecoderLog("MediaFormatReader");
mozilla::LazyLogModule gMediaDemuxerLog("MediaDemuxer");

#define LOG(arg, ...)                                                  \
  DDMOZ_LOG(sFormatDecoderLog, mozilla::LogLevel::Debug, "::%s: " arg, \
            __func__, ##__VA_ARGS__)
#define LOGV(arg, ...)                                                   \
  DDMOZ_LOG(sFormatDecoderLog, mozilla::LogLevel::Verbose, "::%s: " arg, \
            __func__, ##__VA_ARGS__)

#define NS_DispatchToMainThread(...) CompileError_UseAbstractMainThreadInstead

namespace mozilla {

using MediaDataDecoderID = void*;

/**
 * This class tracks shutdown promises to ensure all decoders are shut down
 * completely before MFR continues the rest of the shutdown procedure.
 */
class MediaFormatReader::ShutdownPromisePool {
 public:
  ShutdownPromisePool()
      : mOnShutdownComplete(new ShutdownPromise::Private(__func__)) {}

  // Return a promise which will be resolved when all the tracking promises
  // are resolved. Note no more promises should be added for tracking once
  // this function is called.
  RefPtr<ShutdownPromise> Shutdown();

  // Track a shutdown promise.
  void Track(const RefPtr<ShutdownPromise>& aPromise);

  // Shut down a decoder and track its shutdown promise.
  void ShutdownDecoder(already_AddRefed<MediaDataDecoder> aDecoder) {
    Track(RefPtr<MediaDataDecoder>(aDecoder)->Shutdown());
  }

 private:
  bool mShutdown = false;
  const RefPtr<ShutdownPromise::Private> mOnShutdownComplete;
  nsTHashSet<RefPtr<ShutdownPromise>> mPromises;
};

RefPtr<ShutdownPromise> MediaFormatReader::ShutdownPromisePool::Shutdown() {
  MOZ_DIAGNOSTIC_ASSERT(!mShutdown);
  mShutdown = true;
  if (mPromises.Count() == 0) {
    mOnShutdownComplete->Resolve(true, __func__);
  }
  return mOnShutdownComplete;
}

void MediaFormatReader::ShutdownPromisePool::Track(
    const RefPtr<ShutdownPromise>& aPromise) {
  MOZ_DIAGNOSTIC_ASSERT(!mShutdown);
  MOZ_DIAGNOSTIC_ASSERT(!mPromises.Contains(aPromise));
  mPromises.Insert(aPromise);
  aPromise->Then(AbstractThread::GetCurrent(), __func__, [aPromise, this]() {
    MOZ_DIAGNOSTIC_ASSERT(mPromises.Contains(aPromise));
    mPromises.Remove(aPromise);
    if (mShutdown && mPromises.Count() == 0) {
      mOnShutdownComplete->Resolve(true, __func__);
    }
  });
}

void MediaFormatReader::DecoderData::ShutdownDecoder() {
  MOZ_ASSERT(mOwner->OnTaskQueue());

  MutexAutoLock lock(mMutex);

  if (!mDecoder) {
    // No decoder to shut down.
    return;
  }

  if (mFlushing) {
    // Flush is is in action. Shutdown will be initiated after flush completes.
    MOZ_DIAGNOSTIC_ASSERT(mShutdownPromise);
    mOwner->mShutdownPromisePool->Track(mShutdownPromise->Ensure(__func__));
    // The order of decoder creation and shutdown is handled by LocalAllocPolicy
    // and ShutdownPromisePool. MFR can now reset these members to a fresh state
    // and be ready to create new decoders again without explicitly waiting for
    // flush/shutdown to complete.
    mShutdownPromise = nullptr;
    mFlushing = false;
  } else {
    // No flush is in action. We can shut down the decoder now.
    mOwner->mShutdownPromisePool->Track(mDecoder->Shutdown());
  }

  // mShutdownPromisePool will handle the order of decoder shutdown so
  // we can forget mDecoder and be ready to create a new one.
  mDecoder = nullptr;
  mDescription = "shutdown"_ns;
  mHasReportedVideoHardwareSupportTelemtry = false;
  mOwner->ScheduleUpdate(mType == MediaData::Type::AUDIO_DATA
                             ? TrackType::kAudioTrack
                             : TrackType::kVideoTrack);
}

void MediaFormatReader::DecoderData::Flush() {
  AUTO_PROFILER_LABEL("MediaFormatReader::Flush", MEDIA_PLAYBACK);
  MOZ_ASSERT(mOwner->OnTaskQueue());

  if (mFlushing || mFlushed) {
    // Flush still pending or already flushed, nothing more to do.
    return;
  }
  mDecodeRequest.DisconnectIfExists();
  mDrainRequest.DisconnectIfExists();
  mDrainState = DrainState::None;
  CancelWaitingForKey();
  mOutput.Clear();
  mNumSamplesInput = 0;
  mNumSamplesOutput = 0;
  mSizeOfQueue = 0;
  if (mDecoder) {
    TrackType type = mType == MediaData::Type::AUDIO_DATA
                         ? TrackType::kAudioTrack
                         : TrackType::kVideoTrack;
    mFlushing = true;
    MOZ_DIAGNOSTIC_ASSERT(!mShutdownPromise);
    mShutdownPromise = new SharedShutdownPromiseHolder();
    RefPtr<SharedShutdownPromiseHolder> p = mShutdownPromise;
    RefPtr<MediaDataDecoder> d = mDecoder;
    DDLOGEX2("MediaFormatReader::DecoderData", this, DDLogCategory::Log,
             "flushing", DDNoValue{});
    mDecoder->Flush()->Then(
        mOwner->OwnerThread(), __func__,
        [type, this, p, d]() {
          AUTO_PROFILER_LABEL("MediaFormatReader::Flush:Resolved",
                              MEDIA_PLAYBACK);
          DDLOGEX2("MediaFormatReader::DecoderData", this, DDLogCategory::Log,
                   "flushed", DDNoValue{});
          if (!p->IsEmpty()) {
            // Shutdown happened before flush completes.
            // Let's continue to shut down the decoder. Note
            // we don't access |this| because this decoder
            // is no longer managed by MFR::DecoderData.
            d->Shutdown()->ChainTo(p->Steal(), __func__);
            return;
          }
          mFlushing = false;
          mShutdownPromise = nullptr;
          mOwner->ScheduleUpdate(type);
        },
        [type, this, p, d](const MediaResult& aError) {
          AUTO_PROFILER_LABEL("MediaFormatReader::Flush:Rejected",
                              MEDIA_PLAYBACK);
          DDLOGEX2("MediaFormatReader::DecoderData", this, DDLogCategory::Log,
                   "flush_error", aError);
          if (!p->IsEmpty()) {
            d->Shutdown()->ChainTo(p->Steal(), __func__);
            return;
          }
          mFlushing = false;
          mShutdownPromise = nullptr;
          mOwner->NotifyError(type, aError);
        });
  }
  mFlushed = true;
}

class MediaFormatReader::DecoderFactory {
  using InitPromise = MediaDataDecoder::InitPromise;
  using TokenPromise = AllocPolicy::Promise;
  using Token = AllocPolicy::Token;
  using CreateDecoderPromise = PlatformDecoderModule::CreateDecoderPromise;

 public:
  explicit DecoderFactory(MediaFormatReader* aOwner)
      : mAudio(aOwner->mAudio, TrackInfo::kAudioTrack, aOwner->OwnerThread()),
        mVideo(aOwner->mVideo, TrackInfo::kVideoTrack, aOwner->OwnerThread()),
        mOwner(WrapNotNull(aOwner)) {
    DecoderDoctorLogger::LogConstruction("MediaFormatReader::DecoderFactory",
                                         this);
    DecoderDoctorLogger::LinkParentAndChild(
        aOwner, "decoder factory", "MediaFormatReader::DecoderFactory", this);
  }

  ~DecoderFactory() {
    DecoderDoctorLogger::LogDestruction("MediaFormatReader::DecoderFactory",
                                        this);
  }

  void CreateDecoder(TrackType aTrack);

  // Shutdown any decoder pending initialization and reset mAudio/mVideo to its
  // pristine state so CreateDecoder() is ready to be called again immediately.
  void ShutdownDecoder(TrackType aTrack) {
    MOZ_ASSERT(aTrack == TrackInfo::kAudioTrack ||
               aTrack == TrackInfo::kVideoTrack);
    auto& data = aTrack == TrackInfo::kAudioTrack ? mAudio : mVideo;
    data.mPolicy->Cancel();
    data.mTokenRequest.DisconnectIfExists();
    if (data.mLiveToken) {
      // We haven't completed creation of the decoder, and it hasn't been
      // initialised yet.
      data.mLiveToken = nullptr;
      // The decoder will be shutdown as soon as it's available and tracked by
      // the ShutdownPromisePool.
      mOwner->mShutdownPromisePool->Track(data.mCreateDecoderPromise->Then(
          mOwner->mTaskQueue, __func__,
          [](CreateDecoderPromise::ResolveOrRejectValue&& aResult) {
            if (aResult.IsReject()) {
              return ShutdownPromise::CreateAndResolve(true, __func__);
            }
            return aResult.ResolveValue()->Shutdown();
          }));
      // Free the token to leave room for a new decoder.
      data.mToken = nullptr;
    }
    data.mInitRequest.DisconnectIfExists();
    if (data.mDecoder) {
      mOwner->mShutdownPromisePool->ShutdownDecoder(data.mDecoder.forget());
    }
    data.mStage = Stage::None;
    MOZ_ASSERT(!data.mToken);
  }

 private:
  enum class Stage : int8_t { None, WaitForToken, CreateDecoder, WaitForInit };

  struct Data {
    Data(DecoderData& aOwnerData, TrackType aTrack, TaskQueue* aThread)
        : mOwnerData(aOwnerData),
          mTrack(aTrack),
          mPolicy(new SingleAllocPolicy(aTrack, aThread)) {}
    DecoderData& mOwnerData;
    const TrackType mTrack;
    RefPtr<SingleAllocPolicy> mPolicy;
    Stage mStage = Stage::None;
    RefPtr<Token> mToken;
    RefPtr<MediaDataDecoder> mDecoder;
    MozPromiseRequestHolder<TokenPromise> mTokenRequest;
    struct DecoderCancelled : public SupportsWeakPtr {
      NS_INLINE_DECL_REFCOUNTING_ONEVENTTARGET(DecoderCancelled)
     private:
      ~DecoderCancelled() = default;
    };
    // Set when decoder is about to be created. If cleared before the decoder
    // creation promise is resolved; it indicates that Shutdown() was called and
    // further processing such as initialization should stop.
    RefPtr<DecoderCancelled> mLiveToken;
    RefPtr<CreateDecoderPromise> mCreateDecoderPromise;
    MozPromiseRequestHolder<InitPromise> mInitRequest;
  } mAudio, mVideo;

  void RunStage(Data& aData);
  void DoCreateDecoder(Data& aData);
  void DoInitDecoder(Data& aData);

  // guaranteed to be valid by the owner.
  const NotNull<MediaFormatReader*> mOwner;
};

void MediaFormatReader::DecoderFactory::CreateDecoder(TrackType aTrack) {
  MOZ_ASSERT(aTrack == TrackInfo::kAudioTrack ||
             aTrack == TrackInfo::kVideoTrack);
  Data& data = aTrack == TrackInfo::kAudioTrack ? mAudio : mVideo;
  MOZ_DIAGNOSTIC_ASSERT_IF(mOwner->GetDecoderData(data.mTrack).IsEncrypted(),
                           mOwner->mCDMProxy);
  RunStage(data);
}

void MediaFormatReader::DecoderFactory::RunStage(Data& aData) {
  switch (aData.mStage) {
    case Stage::None: {
      MOZ_DIAGNOSTIC_ASSERT(!aData.mToken);
      aData.mPolicy->Alloc()
          ->Then(
              mOwner->OwnerThread(), __func__,
              [this, &aData](RefPtr<Token> aToken) {
                aData.mTokenRequest.Complete();
                aData.mToken = std::move(aToken);
                aData.mStage = Stage::CreateDecoder;
                RunStage(aData);
              },
              [&aData]() {
                aData.mTokenRequest.Complete();
                aData.mStage = Stage::None;
              })
          ->Track(aData.mTokenRequest);
      aData.mStage = Stage::WaitForToken;
      break;
    }

    case Stage::WaitForToken: {
      MOZ_DIAGNOSTIC_ASSERT(!aData.mToken);
      MOZ_DIAGNOSTIC_ASSERT(aData.mTokenRequest.Exists());
      break;
    }

    case Stage::CreateDecoder: {
      MOZ_DIAGNOSTIC_ASSERT(aData.mToken);
      MOZ_DIAGNOSTIC_ASSERT(!aData.mDecoder);
      MOZ_DIAGNOSTIC_ASSERT(!aData.mInitRequest.Exists());

      DoCreateDecoder(aData);
      aData.mStage = Stage::WaitForInit;
      break;
    }

    case Stage::WaitForInit: {
      MOZ_DIAGNOSTIC_ASSERT((aData.mDecoder && aData.mInitRequest.Exists()) ||
                            aData.mLiveToken);
      break;
    }
  }
}

void MediaFormatReader::DecoderFactory::DoCreateDecoder(Data& aData) {
  AUTO_PROFILER_LABEL("DecoderFactory::DoCreateDecoder", MEDIA_PLAYBACK);
  auto& ownerData = aData.mOwnerData;
  auto& decoder = mOwner->GetDecoderData(aData.mTrack);

  RefPtr<PDMFactory> platform = new PDMFactory();
  if (decoder.IsEncrypted()) {
    MOZ_DIAGNOSTIC_ASSERT(mOwner->mCDMProxy);
    platform->SetCDMProxy(mOwner->mCDMProxy);
  }

  RefPtr<PlatformDecoderModule::CreateDecoderPromise> p;
  MediaFormatReader* owner = mOwner;
  auto onWaitingForKeyEvent =
      [owner = ThreadSafeWeakPtr<MediaFormatReader>(owner)]() {
        RefPtr<MediaFormatReader> mfr(owner);
        MOZ_DIAGNOSTIC_ASSERT(mfr, "The MediaFormatReader didn't wait for us");
        return mfr ? &mfr->OnTrackWaitingForKeyProducer() : nullptr;
      };

  switch (aData.mTrack) {
    case TrackInfo::kAudioTrack: {
      p = platform->CreateDecoder(
          {*ownerData.GetCurrentInfo()->GetAsAudioInfo(), mOwner->mCrashHelper,
           CreateDecoderParams::UseNullDecoder(ownerData.mIsNullDecode),
           TrackInfo::kAudioTrack, std::move(onWaitingForKeyEvent),
           mOwner->mMediaEngineId, mOwner->mTrackingId,
           mOwner->mEncryptedCustomIdent
               ? CreateDecoderParams::EncryptedCustomIdent::True
               : CreateDecoderParams::EncryptedCustomIdent::False});
      break;
    }

    case TrackType::kVideoTrack: {
      // Decoders use the layers backend to decide if they can use hardware
      // decoding, so specify LAYERS_NONE if we want to forcibly disable it.
      using Option = CreateDecoderParams::Option;
      using OptionSet = CreateDecoderParams::OptionSet;

      p = platform->CreateDecoder(
          {*ownerData.GetCurrentInfo()->GetAsVideoInfo(),
           mOwner->mKnowsCompositor, mOwner->GetImageContainer(),
           mOwner->mCrashHelper,
           CreateDecoderParams::UseNullDecoder(ownerData.mIsNullDecode),
           TrackType::kVideoTrack, std::move(onWaitingForKeyEvent),
           CreateDecoderParams::VideoFrameRate(ownerData.mMeanRate.Mean()),
           OptionSet(ownerData.mHardwareDecodingDisabled
                         ? Option::HardwareDecoderNotAllowed
                         : Option::Default),
           mOwner->mMediaEngineId, mOwner->mTrackingId,
           mOwner->mEncryptedCustomIdent
               ? CreateDecoderParams::EncryptedCustomIdent::True
               : CreateDecoderParams::EncryptedCustomIdent::False});
      break;
    }

    default:
      p = PlatformDecoderModule::CreateDecoderPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
  }

  aData.mLiveToken = MakeRefPtr<Data::DecoderCancelled>();

  aData.mCreateDecoderPromise = p->Then(
      mOwner->OwnerThread(), __func__,
      [this, &aData, &ownerData, live = WeakPtr{aData.mLiveToken},
       owner = ThreadSafeWeakPtr<MediaFormatReader>(owner)](
          RefPtr<MediaDataDecoder>&& aDecoder) {
        if (!live) {
          return CreateDecoderPromise::CreateAndResolve(std::move(aDecoder),
                                                        __func__);
        }
        aData.mLiveToken = nullptr;
        aData.mDecoder = new MediaDataDecoderProxy(
            aDecoder.forget(), do_AddRef(ownerData.mTaskQueue.get()));
        aData.mDecoder = new AllocationWrapper(aData.mDecoder.forget(),
                                               aData.mToken.forget());
        DecoderDoctorLogger::LinkParentAndChild(
            aData.mDecoder.get(), "decoder",
            "MediaFormatReader::DecoderFactory", this);

        DoInitDecoder(aData);

        return CreateDecoderPromise::CreateAndResolve(aData.mDecoder, __func__);
      },
      [this, &aData,
       live = WeakPtr{aData.mLiveToken}](const MediaResult& aError) {
        NS_WARNING("Error constructing decoders");
        if (!live) {
          return CreateDecoderPromise::CreateAndReject(aError, __func__);
        }
        aData.mLiveToken = nullptr;
        aData.mToken = nullptr;
        aData.mStage = Stage::None;
        aData.mOwnerData.mDescription = aError.Description();
        DDLOGEX2("MediaFormatReader::DecoderFactory", this, DDLogCategory::Log,
                 "create_decoder_error", aError);
        mOwner->NotifyError(aData.mTrack, aError);

        return CreateDecoderPromise::CreateAndReject(aError, __func__);
      });
}

void MediaFormatReader::DecoderFactory::DoInitDecoder(Data& aData) {
  AUTO_PROFILER_LABEL("DecoderFactory::DoInitDecoder", MEDIA_PLAYBACK);
  auto& ownerData = aData.mOwnerData;

  DDLOGEX2("MediaFormatReader::DecoderFactory", this, DDLogCategory::Log,
           "initialize_decoder", DDNoValue{});
  aData.mDecoder->Init()
      ->Then(
          mOwner->OwnerThread(), __func__,
          [this, &aData, &ownerData](TrackType aTrack) {
            AUTO_PROFILER_LABEL("DecoderFactory::DoInitDecoder:Resolved",
                                MEDIA_PLAYBACK);
            aData.mInitRequest.Complete();
            aData.mStage = Stage::None;
            MutexAutoLock lock(ownerData.mMutex);
            ownerData.mDecoder = std::move(aData.mDecoder);
            ownerData.mDescription = ownerData.mDecoder->GetDescriptionName();
            DDLOGEX2("MediaFormatReader::DecoderFactory", this,
                     DDLogCategory::Log, "decoder_initialized", DDNoValue{});
            DecoderDoctorLogger::LinkParentAndChild(
                "MediaFormatReader::DecoderData", &ownerData, "decoder",
                ownerData.mDecoder.get());
            mOwner->SetVideoDecodeThreshold();
            mOwner->ScheduleUpdate(aTrack);
            if (aTrack == TrackInfo::kAudioTrack) {
              ownerData.mProcessName = ownerData.mDecoder->GetProcessName();
              ownerData.mCodecName = ownerData.mDecoder->GetCodecName();
            }
            nsCString needsConversion;
            switch (ownerData.mDecoder->NeedsConversion()) {
              case MediaDataDecoder::ConversionRequired::kNeedNone:
                needsConversion = "false";
                break;
              case MediaDataDecoder::ConversionRequired::kNeedAVCC:
                needsConversion = "AVCC";
                break;
              case MediaDataDecoder::ConversionRequired::kNeedAnnexB:
                needsConversion = "AnnexB";
                break;
              default:
                needsConversion = "Unknown";
            }
            nsCString dummy;
            MOZ_LOG_FMT(sFormatDecoderLog, mozilla::LogLevel::Debug,
                        "Decoder init finished for "
                        "{} codec: \"{}\", "
                        "description: \"{}\", "
                        "process: \"{}\", "
                        "hw: \"{}\", "
                        "needs conversion: \"{}\"",
                        (aTrack == TrackInfo::kVideoTrack) ? "video" : "audio",
                        ownerData.mDecoder->GetCodecName(),
                        ownerData.mDecoder->GetDescriptionName(),
                        ownerData.mDecoder->GetProcessName(),
                        ownerData.mDecoder->IsHardwareAccelerated(dummy),
                        needsConversion);
          },
          [this, &aData, &ownerData](const MediaResult& aError) {
            AUTO_PROFILER_LABEL("DecoderFactory::DoInitDecoder:Rejected",
                                MEDIA_PLAYBACK);
            aData.mInitRequest.Complete();
            MOZ_RELEASE_ASSERT(!ownerData.mDecoder,
                               "Can't have a decoder already set");
            aData.mStage = Stage::None;
            mOwner->mShutdownPromisePool->ShutdownDecoder(
                aData.mDecoder.forget());
            DDLOGEX2("MediaFormatReader::DecoderFactory", this,
                     DDLogCategory::Log, "initialize_decoder_error", aError);
            mOwner->NotifyError(aData.mTrack, aError);
          })
      ->Track(aData.mInitRequest);
}

// DemuxerProxy ensures that the original main demuxer is only ever accessed
// via its own dedicated task queue.
// This ensure that the reader's taskqueue will never blocked while a demuxer
// is itself blocked attempting to access the MediaCache or the MediaResource.
class MediaFormatReader::DemuxerProxy {
  using TrackType = TrackInfo::TrackType;
  class Wrapper;

 public:
  explicit DemuxerProxy(MediaDataDemuxer* aDemuxer)
      : mTaskQueue(TaskQueue::Create(
            GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
            "DemuxerProxy::mTaskQueue")),
        mData(new Data(aDemuxer)) {
    MOZ_COUNT_CTOR(DemuxerProxy);
  }

  MOZ_COUNTED_DTOR(DemuxerProxy)

  RefPtr<ShutdownPromise> Shutdown() {
    RefPtr<Data> data = std::move(mData);
    return InvokeAsync(mTaskQueue, __func__, [data]() {
      // We need to clear our reference to the demuxer now. So that in the event
      // the init promise wasn't resolved, such as what can happen with the
      // mediasource demuxer that is waiting on more data, it will force the
      // init promise to be rejected.
      data->mDemuxer = nullptr;
      data->mAudioDemuxer = nullptr;
      data->mVideoDemuxer = nullptr;
      return ShutdownPromise::CreateAndResolve(true, __func__);
    });
  }

  RefPtr<MediaDataDemuxer::InitPromise> Init();

  Wrapper* GetTrackDemuxer(TrackType aTrack, uint32_t aTrackNumber) {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    switch (aTrack) {
      case TrackInfo::kAudioTrack:
        return mData->mAudioDemuxer;
      case TrackInfo::kVideoTrack:
        return mData->mVideoDemuxer;
      default:
        return nullptr;
    }
  }

  uint32_t GetNumberTracks(TrackType aTrack) const {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    switch (aTrack) {
      case TrackInfo::kAudioTrack:
        return mData->mNumAudioTrack;
      case TrackInfo::kVideoTrack:
        return mData->mNumVideoTrack;
      default:
        return 0;
    }
  }

  bool IsSeekable() const {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    return mData->mSeekable;
  }

  bool IsSeekableOnlyInBufferedRanges() const {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    return mData->mSeekableOnlyInBufferedRange;
  }

  UniquePtr<EncryptionInfo> GetCrypto() const {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    if (!mData->mCrypto) {
      return nullptr;
    }
    auto crypto = MakeUnique<EncryptionInfo>();
    *crypto = *mData->mCrypto;
    return crypto;
  }

  RefPtr<NotifyDataArrivedPromise> NotifyDataArrived();

  bool ShouldComputeStartTime() const {
    MOZ_RELEASE_ASSERT(mData && mData->mInitDone);

    return mData->mShouldComputeStartTime;
  }

 private:
  const RefPtr<TaskQueue> mTaskQueue;
  struct Data {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Data)

    explicit Data(MediaDataDemuxer* aDemuxer)
        : mInitDone(false), mDemuxer(aDemuxer) {}

    Atomic<bool> mInitDone;
    // Only ever accessed over mTaskQueue once.
    RefPtr<MediaDataDemuxer> mDemuxer;
    // Only accessed once InitPromise has been resolved and immutable after.
    // So we can safely access them without the use of the mutex.
    uint32_t mNumAudioTrack = 0;
    RefPtr<Wrapper> mAudioDemuxer;
    uint32_t mNumVideoTrack = 0;
    RefPtr<Wrapper> mVideoDemuxer;
    bool mSeekable = false;
    bool mSeekableOnlyInBufferedRange = false;
    bool mShouldComputeStartTime = true;
    UniquePtr<EncryptionInfo> mCrypto;

   private:
    ~Data() = default;
  };
  RefPtr<Data> mData;
};

class MediaFormatReader::DemuxerProxy::Wrapper : public MediaTrackDemuxer {
 public:
  Wrapper(MediaTrackDemuxer* aTrackDemuxer, TaskQueue* aTaskQueue)
      : mMutex("TrackDemuxer Mutex"),
        mTaskQueue(aTaskQueue),
        mGetSamplesMayBlock(aTrackDemuxer->GetSamplesMayBlock()),
        mInfo(aTrackDemuxer->GetInfo()),
        mTrackDemuxer(aTrackDemuxer) {
    DecoderDoctorLogger::LogConstructionAndBase(
        "MediaFormatReader::DemuxerProxy::Wrapper", this,
        static_cast<const MediaTrackDemuxer*>(this));
    DecoderDoctorLogger::LinkParentAndChild(
        "MediaFormatReader::DemuxerProxy::Wrapper", this, "track demuxer",
        aTrackDemuxer);
  }

  UniquePtr<TrackInfo> GetInfo() const override {
    if (!mInfo) {
      return nullptr;
    }
    return mInfo->Clone();
  }

  RefPtr<SeekPromise> Seek(const TimeUnit& aTime) override {
    RefPtr<Wrapper> self = this;
    return InvokeAsync(
               mTaskQueue, __func__,
               [self, aTime]() { return self->mTrackDemuxer->Seek(aTime); })
        ->Then(
            mTaskQueue, __func__,
            [self](const TimeUnit& aTime) {
              self->UpdateRandomAccessPoint();
              return SeekPromise::CreateAndResolve(aTime, __func__);
            },
            [self](const MediaResult& aError) {
              self->UpdateRandomAccessPoint();
              return SeekPromise::CreateAndReject(aError, __func__);
            });
  }

  RefPtr<SamplesPromise> GetSamples(int32_t aNumSamples) override {
    RefPtr<Wrapper> self = this;
    return InvokeAsync(mTaskQueue, __func__,
                       [self, aNumSamples]() {
                         return self->mTrackDemuxer->GetSamples(aNumSamples);
                       })
        ->Then(
            mTaskQueue, __func__,
            [self](RefPtr<SamplesHolder> aSamples) {
              self->UpdateRandomAccessPoint();
              return SamplesPromise::CreateAndResolve(aSamples.forget(),
                                                      __func__);
            },
            [self](const MediaResult& aError) {
              self->UpdateRandomAccessPoint();
              return SamplesPromise::CreateAndReject(aError, __func__);
            });
  }

  bool GetSamplesMayBlock() const override { return mGetSamplesMayBlock; }

  void Reset() override {
    RefPtr<Wrapper> self = this;
    nsresult rv = mTaskQueue->Dispatch(NS_NewRunnableFunction(
        "MediaFormatReader::DemuxerProxy::Wrapper::Reset",
        [self]() { self->mTrackDemuxer->Reset(); }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    Unused << rv;
  }

  nsresult GetNextRandomAccessPoint(TimeUnit* aTime) override {
    MutexAutoLock lock(mMutex);
    if (NS_SUCCEEDED(mNextRandomAccessPointResult)) {
      *aTime = mNextRandomAccessPoint;
    }
    return mNextRandomAccessPointResult;
  }

  RefPtr<SkipAccessPointPromise> SkipToNextRandomAccessPoint(
      const TimeUnit& aTimeThreshold) override {
    RefPtr<Wrapper> self = this;
    return InvokeAsync(
               mTaskQueue, __func__,
               [self, aTimeThreshold]() {
                 return self->mTrackDemuxer->SkipToNextRandomAccessPoint(
                     aTimeThreshold);
               })
        ->Then(
            mTaskQueue, __func__,
            [self](uint32_t aVal) {
              self->UpdateRandomAccessPoint();
              return SkipAccessPointPromise::CreateAndResolve(aVal, __func__);
            },
            [self](const SkipFailureHolder& aError) {
              self->UpdateRandomAccessPoint();
              return SkipAccessPointPromise::CreateAndReject(aError, __func__);
            });
  }

  TimeIntervals GetBuffered() override {
    MutexAutoLock lock(mMutex);
    return mBuffered;
  }

  void BreakCycles() override {}

 private:
  Mutex mMutex MOZ_UNANNOTATED;
  const RefPtr<TaskQueue> mTaskQueue;
  const bool mGetSamplesMayBlock;
  const UniquePtr<TrackInfo> mInfo;
  // mTrackDemuxer is only ever accessed on demuxer's task queue.
  RefPtr<MediaTrackDemuxer> mTrackDemuxer;
  // All following members are protected by mMutex
  nsresult mNextRandomAccessPointResult = NS_OK;
  TimeUnit mNextRandomAccessPoint;
  TimeIntervals mBuffered;
  friend class DemuxerProxy;

  ~Wrapper() {
    RefPtr<MediaTrackDemuxer> trackDemuxer = std::move(mTrackDemuxer);
    nsresult rv = mTaskQueue->Dispatch(NS_NewRunnableFunction(
        "MediaFormatReader::DemuxerProxy::Wrapper::~Wrapper",
        [trackDemuxer]() { trackDemuxer->BreakCycles(); }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    Unused << rv;
    DecoderDoctorLogger::LogDestruction(
        "MediaFormatReader::DemuxerProxy::Wrapper", this);
  }

  void UpdateRandomAccessPoint() {
    MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
    if (!mTrackDemuxer) {
      // Detached.
      return;
    }
    MutexAutoLock lock(mMutex);
    mNextRandomAccessPointResult =
        mTrackDemuxer->GetNextRandomAccessPoint(&mNextRandomAccessPoint);
  }

  void UpdateBuffered() {
    MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
    if (!mTrackDemuxer) {
      // Detached.
      return;
    }
    MutexAutoLock lock(mMutex);
    mBuffered = mTrackDemuxer->GetBuffered();
  }
};

RefPtr<MediaDataDemuxer::InitPromise> MediaFormatReader::DemuxerProxy::Init() {
  AUTO_PROFILER_LABEL("DemuxerProxy::Init", MEDIA_PLAYBACK);
  using InitPromise = MediaDataDemuxer::InitPromise;

  RefPtr<Data> data = mData;
  RefPtr<TaskQueue> taskQueue = mTaskQueue;
  return InvokeAsync(mTaskQueue, __func__,
                     [data, taskQueue]() {
                       if (!data->mDemuxer) {
                         return InitPromise::CreateAndReject(
                             NS_ERROR_DOM_MEDIA_CANCELED, __func__);
                       }
                       return data->mDemuxer->Init();
                     })
      ->Then(
          taskQueue, __func__,
          [data, taskQueue]() {
            AUTO_PROFILER_LABEL("DemuxerProxy::Init:Resolved", MEDIA_PLAYBACK);
            if (!data->mDemuxer) {  // Was shutdown.
              return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                                  __func__);
            }
            data->mNumAudioTrack =
                data->mDemuxer->GetNumberTracks(TrackInfo::kAudioTrack);
            if (data->mNumAudioTrack) {
              RefPtr<MediaTrackDemuxer> d =
                  data->mDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
              if (d) {
                RefPtr<Wrapper> wrapper =
                    new DemuxerProxy::Wrapper(d, taskQueue);
                wrapper->UpdateBuffered();
                data->mAudioDemuxer = wrapper;
                DecoderDoctorLogger::LinkParentAndChild(
                    data->mDemuxer.get(), "decoder factory wrapper",
                    "MediaFormatReader::DecoderFactory::Wrapper",
                    wrapper.get());
              }
            }
            data->mNumVideoTrack =
                data->mDemuxer->GetNumberTracks(TrackInfo::kVideoTrack);
            if (data->mNumVideoTrack) {
              RefPtr<MediaTrackDemuxer> d =
                  data->mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
              if (d) {
                RefPtr<Wrapper> wrapper =
                    new DemuxerProxy::Wrapper(d, taskQueue);
                wrapper->UpdateBuffered();
                data->mVideoDemuxer = wrapper;
                DecoderDoctorLogger::LinkParentAndChild(
                    data->mDemuxer.get(), "decoder factory wrapper",
                    "MediaFormatReader::DecoderFactory::Wrapper",
                    wrapper.get());
              }
            }
            data->mCrypto = data->mDemuxer->GetCrypto();
            data->mSeekable = data->mDemuxer->IsSeekable();
            data->mSeekableOnlyInBufferedRange =
                data->mDemuxer->IsSeekableOnlyInBufferedRanges();
            data->mShouldComputeStartTime =
                data->mDemuxer->ShouldComputeStartTime();
            data->mInitDone = true;
            return InitPromise::CreateAndResolve(NS_OK, __func__);
          },
          [](const MediaResult& aError) {
            return InitPromise::CreateAndReject(aError, __func__);
          });
}

RefPtr<MediaFormatReader::NotifyDataArrivedPromise>
MediaFormatReader::DemuxerProxy::NotifyDataArrived() {
  RefPtr<Data> data = mData;
  return InvokeAsync(mTaskQueue, __func__, [data]() {
    if (!data->mDemuxer) {
      // Was shutdown.
      return NotifyDataArrivedPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    }
    data->mDemuxer->NotifyDataArrived();
    if (data->mAudioDemuxer) {
      data->mAudioDemuxer->UpdateBuffered();
    }
    if (data->mVideoDemuxer) {
      data->mVideoDemuxer->UpdateBuffered();
    }
    return NotifyDataArrivedPromise::CreateAndResolve(true, __func__);
  });
}

MediaFormatReader::MediaFormatReader(MediaFormatReaderInit& aInit,
                                     MediaDataDemuxer* aDemuxer)
    : mTaskQueue(
          TaskQueue::Create(GetMediaThreadPool(MediaThreadType::SUPERVISOR),
                            "MediaFormatReader::mTaskQueue",
                            /* aSupportsTailDispatch = */ true)),
      mAudio(this, MediaData::Type::AUDIO_DATA,
             StaticPrefs::media_audio_max_decode_error()),
      mVideo(this, MediaData::Type::VIDEO_DATA,
             StaticPrefs::media_video_max_decode_error()),
      mWorkingInfoChanged(false, "MediaFormatReader::mWorkingInfoChanged"),
      mWatchManager(this, OwnerThread()),
      mIsWatchingWorkingInfo(false),
      mDemuxer(new DemuxerProxy(aDemuxer)),
      mDemuxerInitDone(false),
      mPendingNotifyDataArrived(false),
      mLastReportedNumDecodedFrames(0),
      mPreviousDecodedKeyframeTime_us(sNoPreviousDecodedKeyframe),
      mKnowsCompositor(aInit.mKnowsCompositor),
      mInitDone(false),
      mTrackDemuxersMayBlock(false),
      mSeekScheduled(false),
      mVideoFrameContainer(aInit.mVideoFrameContainer),
      mCrashHelper(aInit.mCrashHelper),
      mDecoderFactory(new DecoderFactory(this)),
      mShutdownPromisePool(new ShutdownPromisePool()),
      mBuffered(mTaskQueue, TimeIntervals(),
                "MediaFormatReader::mBuffered (Canonical)"),
      mFrameStats(aInit.mFrameStats),
      mMediaDecoderOwnerID(aInit.mMediaDecoderOwnerID),
      mTrackingId(std::move(aInit.mTrackingId)),
      mReadMetadataStartTime(Nothing()),
      mReadMetaDataTime(TimeDuration::Zero()),
      mTotalWaitingForVideoDataTime(TimeDuration::Zero()),
      mEncryptedCustomIdent(false) {
  MOZ_ASSERT(aDemuxer);
  MOZ_COUNT_CTOR(MediaFormatReader);
  DDLINKCHILD("audio decoder data", "MediaFormatReader::DecoderDataWithPromise",
              &mAudio);
  DDLINKCHILD("video decoder data", "MediaFormatReader::DecoderDataWithPromise",
              &mVideo);
  DDLINKCHILD("demuxer", aDemuxer);
  mOnTrackWaitingForKeyListener = OnTrackWaitingForKey().Connect(
      mTaskQueue, this, &MediaFormatReader::NotifyWaitingForKey);
}

MediaFormatReader::~MediaFormatReader() {
  MOZ_COUNT_DTOR(MediaFormatReader);
  MOZ_ASSERT(mShutdown);
}

RefPtr<ShutdownPromise> MediaFormatReader::Shutdown() {
  MOZ_ASSERT(OnTaskQueue());
  LOG("");

  mDemuxerInitRequest.DisconnectIfExists();
  mNotifyDataArrivedPromise.DisconnectIfExists();
  mMetadataPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mSeekPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  mSkipRequest.DisconnectIfExists();
  mSetCDMPromise.RejectIfExists(
      MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                  "MediaFormatReader is shutting down"),
      __func__);

  if (mIsWatchingWorkingInfo) {
    mWatchManager.Unwatch(mWorkingInfoChanged,
                          &MediaFormatReader::NotifyTrackInfoUpdated);
  }
  mWatchManager.Shutdown();

  if (mAudio.HasPromise()) {
    mAudio.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  }
  if (mVideo.HasPromise()) {
    mVideo.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
  }

  if (HasAudio()) {
    mAudio.ResetDemuxer();
    mAudio.mTrackDemuxer->BreakCycles();
    {
      MutexAutoLock lock(mAudio.mMutex);
      mAudio.mTrackDemuxer = nullptr;
    }
    mAudio.ResetState();
    ShutdownDecoder(TrackInfo::kAudioTrack);
  }

  if (HasVideo()) {
    mVideo.ResetDemuxer();
    mVideo.mTrackDemuxer->BreakCycles();
    {
      MutexAutoLock lock(mVideo.mMutex);
      mVideo.mTrackDemuxer = nullptr;
    }
    mVideo.ResetState();
    ShutdownDecoder(TrackInfo::kVideoTrack);
  }

  mShutdownPromisePool->Track(mDemuxer->Shutdown());
  mDemuxer = nullptr;

  mOnTrackWaitingForKeyListener.Disconnect();

  mShutdown = true;
  return mShutdownPromisePool->Shutdown()->Then(
      OwnerThread(), __func__, this, &MediaFormatReader::TearDownDecoders,
      &MediaFormatReader::TearDownDecoders);
}

void MediaFormatReader::ShutdownDecoder(TrackType aTrack) {
  LOGV("%s", TrackTypeToStr(aTrack));

  // Shut down the pending decoder if any.
  mDecoderFactory->ShutdownDecoder(aTrack);

  auto& decoder = GetDecoderData(aTrack);
  // Flush the decoder if necessary.
  decoder.Flush();

  // Shut down the decoder if any.
  decoder.ShutdownDecoder();
}

void MediaFormatReader::NotifyTrackInfoUpdated() {
  MOZ_ASSERT(OnTaskQueue());
  if (mWorkingInfoChanged) {
    mWorkingInfoChanged = false;

    VideoInfo videoInfo;
    AudioInfo audioInfo;
    {
      MutexAutoLock lock(mVideo.mMutex);
      if (HasVideo()) {
        videoInfo = *mVideo.GetWorkingInfo()->GetAsVideoInfo();
      }
    }
    {
      MutexAutoLock lock(mAudio.mMutex);
      if (HasAudio()) {
        audioInfo = *mAudio.GetWorkingInfo()->GetAsAudioInfo();
      }
    }

    mTrackInfoUpdatedEvent.Notify(videoInfo, audioInfo);
  }
}

RefPtr<ShutdownPromise> MediaFormatReader::TearDownDecoders() {
  if (mAudio.mTaskQueue) {
    mAudio.mTaskQueue->BeginShutdown();
    mAudio.mTaskQueue->AwaitShutdownAndIdle();
    mAudio.mTaskQueue = nullptr;
  }
  if (mVideo.mTaskQueue) {
    mVideo.mTaskQueue->BeginShutdown();
    mVideo.mTaskQueue->AwaitShutdownAndIdle();
    mVideo.mTaskQueue = nullptr;
  }

  mDecoderFactory = nullptr;
  mVideoFrameContainer = nullptr;

  ReleaseResources();
  mBuffered.DisconnectAll();
  return mTaskQueue->BeginShutdown();
}

nsresult MediaFormatReader::Init() {
  MOZ_ASSERT(NS_IsMainThread(), "Must be on main thread.");

  mAudio.mTaskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                        "MFR::mAudio::mTaskQueue");

  mVideo.mTaskQueue =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
                        "MFR::mVideo::mTaskQueue");

  return NS_OK;
}

bool MediaFormatReader::ResolveSetCDMPromiseIfDone(TrackType aTrack) {
  // When a CDM proxy is set, MFR would shutdown the existing MediaDataDecoder
  // and would create new one for specific track in the next Update.
  MOZ_ASSERT(OnTaskQueue());

  if (mSetCDMPromise.IsEmpty()) {
    return true;
  }

  MOZ_ASSERT(mCDMProxy);
  if (mSetCDMForTracks.contains(aTrack)) {
    mSetCDMForTracks -= aTrack;
  }

  if (mSetCDMForTracks.isEmpty()) {
    LOGV("%s : Done ", __func__);
    mSetCDMPromise.Resolve(/* aResolveValue = */ true, __func__);
    if (HasAudio()) {
      ScheduleUpdate(TrackInfo::kAudioTrack);
    }
    if (HasVideo()) {
      ScheduleUpdate(TrackInfo::kVideoTrack);
    }
    return true;
  }
  LOGV("%s : %s track is ready.", __func__, TrackTypeToStr(aTrack));
  return false;
}

void MediaFormatReader::PrepareToSetCDMForTrack(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  LOGV("%s : %s", __func__, TrackTypeToStr(aTrack));

  mSetCDMForTracks += aTrack;
  if (mCDMProxy) {
    // An old cdm proxy exists, so detaching old cdm proxy by shutting down
    // MediaDataDecoder.
    ShutdownDecoder(aTrack);
  }
  ScheduleUpdate(aTrack);
}

bool MediaFormatReader::IsDecoderWaitingForCDM(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  return GetDecoderData(aTrack).IsEncrypted() &&
         mSetCDMForTracks.contains(aTrack) && !mCDMProxy;
}

RefPtr<SetCDMPromise> MediaFormatReader::SetCDMProxy(CDMProxy* aProxy) {
  MOZ_ASSERT(OnTaskQueue());
  LOGV("SetCDMProxy (%p)", aProxy);

  if (mShutdown) {
    return SetCDMPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                    "MediaFormatReader is shutting down"),
        __func__);
  }

  mSetCDMPromise.RejectIfExists(
      MediaResult(NS_ERROR_DOM_INVALID_STATE_ERR,
                  "Another new CDM proxy is being set."),
      __func__);

  // Shutdown all decoders as switching CDM proxy indicates that it's
  // inappropriate for the existing decoders to continue decoding via the old
  // CDM proxy.
  if (HasAudio()) {
    PrepareToSetCDMForTrack(TrackInfo::kAudioTrack);
  }
  if (HasVideo()) {
    PrepareToSetCDMForTrack(TrackInfo::kVideoTrack);
  }

  mCDMProxy = aProxy;

  if (!mInitDone || mSetCDMForTracks.isEmpty() || !mCDMProxy) {
    // 1) MFR is not initialized yet or
    // 2) Demuxer is initialized without active audio and video or
    // 3) A null cdm proxy is set
    // the promise can be resolved directly.
    mSetCDMForTracks.clear();
    return SetCDMPromise::CreateAndResolve(/* aResolveValue = */ true,
                                           __func__);
  }

  RefPtr<SetCDMPromise> p = mSetCDMPromise.Ensure(__func__);
  return p;
}

bool MediaFormatReader::IsWaitingOnCDMResource() {
  MOZ_ASSERT(OnTaskQueue());
  return IsEncrypted() && !mCDMProxy;
}

RefPtr<MediaFormatReader::MetadataPromise>
MediaFormatReader::AsyncReadMetadata() {
  AUTO_PROFILER_LABEL("MediaFormatReader::AsyncReadMetadata", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  MOZ_DIAGNOSTIC_ASSERT(mMetadataPromise.IsEmpty());

  if (mInitDone) {
    // We are returning from dormant.
    MetadataHolder metadata;
    metadata.mInfo = MakeUnique<MediaInfo>(mInfo);
    return MetadataPromise::CreateAndResolve(std::move(metadata), __func__);
  }

  if (!mReadMetadataStartTime) {
    mReadMetadataStartTime = Some(TimeStamp::Now());
  }

  RefPtr<MetadataPromise> p = mMetadataPromise.Ensure(__func__);

  mDemuxer->Init()
      ->Then(OwnerThread(), __func__, this,
             &MediaFormatReader::OnDemuxerInitDone,
             &MediaFormatReader::OnDemuxerInitFailed)
      ->Track(mDemuxerInitRequest);
  return p;
}

void MediaFormatReader::OnDemuxerInitDone(const MediaResult& aResult) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnDemuxerInitDone", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  mDemuxerInitRequest.Complete();

  if (NS_FAILED(aResult) && StaticPrefs::media_playback_warnings_as_errors()) {
    mMetadataPromise.Reject(aResult, __func__);
    return;
  }

  mDemuxerInitDone = true;

  UniquePtr<MetadataTags> tags(MakeUnique<MetadataTags>());

  RefPtr<PDMFactory> platform;
  if (!IsWaitingOnCDMResource()) {
    platform = new PDMFactory();
  }

  // To decode, we need valid video and a place to put it.
  bool videoActive = !!mDemuxer->GetNumberTracks(TrackInfo::kVideoTrack) &&
                     GetImageContainer();

  if (videoActive) {
    // We currently only handle the first video track.
    MutexAutoLock lock(mVideo.mMutex);
    mVideo.mTrackDemuxer = mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    if (!mVideo.mTrackDemuxer) {
      LOG("No video track demuxer");
      mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
      return;
    }

    UniquePtr<TrackInfo> videoInfo = mVideo.mTrackDemuxer->GetInfo();
    videoActive = videoInfo && videoInfo->IsValid();
    if (videoActive) {
      if (platform &&
          platform->SupportsMimeType(videoInfo->mMimeType).isEmpty()) {
        // We have no decoder for this track. Error.
        LOG("No supported decoder for video track (%s)",
            videoInfo->mMimeType.get());
        if (!videoInfo->mMimeType.IsEmpty()) {
          mozilla::glean::media_playback::not_supported_video_per_mime_type
              .Get(videoInfo->mMimeType)
              .Add(1);
        }
        mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
        return;
      }
      mInfo.mVideo = *videoInfo->GetAsVideoInfo();
      mVideo.mWorkingInfo = MakeUnique<VideoInfo>(mInfo.mVideo);
      for (const MetadataTag& tag : videoInfo->mTags) {
        tags->InsertOrUpdate(tag.mKey, tag.mValue);
      }
      mWorkingInfoChanged = true;
      mVideo.mOriginalInfo = std::move(videoInfo);
      mTrackDemuxersMayBlock |= mVideo.mTrackDemuxer->GetSamplesMayBlock();
    } else {
      mVideo.mTrackDemuxer->BreakCycles();
      mVideo.mTrackDemuxer = nullptr;
    }
  }

  bool audioActive = !!mDemuxer->GetNumberTracks(TrackInfo::kAudioTrack);
  if (audioActive) {
    MutexAutoLock lock(mAudio.mMutex);
    mAudio.mTrackDemuxer = mDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
    if (!mAudio.mTrackDemuxer) {
      LOG("No audio track demuxer");
      mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
      return;
    }

    UniquePtr<TrackInfo> audioInfo = mAudio.mTrackDemuxer->GetInfo();
    // We actively ignore audio tracks that we know we can't play.
    audioActive = audioInfo && audioInfo->IsValid() &&
                  (!platform ||
                   !platform->SupportsMimeType(audioInfo->mMimeType).isEmpty());

    if (audioActive) {
      mInfo.mAudio = *audioInfo->GetAsAudioInfo();
      mAudio.mWorkingInfo = MakeUnique<AudioInfo>(mInfo.mAudio);
      for (const MetadataTag& tag : audioInfo->mTags) {
        tags->InsertOrUpdate(tag.mKey, tag.mValue);
      }
      mWorkingInfoChanged = true;
      mAudio.mOriginalInfo = std::move(audioInfo);
      mTrackDemuxersMayBlock |= mAudio.mTrackDemuxer->GetSamplesMayBlock();
    } else {
      mAudio.mTrackDemuxer->BreakCycles();
      mAudio.mTrackDemuxer = nullptr;
    }
  }

  UniquePtr<EncryptionInfo> crypto = mDemuxer->GetCrypto();
  if (crypto && crypto->IsEncrypted()) {
    // Try and dispatch 'encrypted'. Won't go if ready state still HAVE_NOTHING.
    for (uint32_t i = 0; i < crypto->mInitDatas.Length(); i++) {
      mOnEncrypted.Notify(crypto->mInitDatas[i].mInitData,
                          crypto->mInitDatas[i].mType);
    }
    mInfo.mCrypto = *crypto;
  }

  auto videoDuration = HasVideo() ? mInfo.mVideo.mDuration : TimeUnit::Zero();
  auto audioDuration = HasAudio() ? mInfo.mAudio.mDuration : TimeUnit::Zero();

  // If the duration is 0 on both audio and video, it mMetadataDuration is to be
  // Nothing(). Duration will use buffered ranges.
  LOG("videoDuration=%" PRId64 ", audioDuration=%" PRId64,
      videoDuration.ToMicroseconds(), audioDuration.ToMicroseconds());
  if (videoDuration.IsPositive() || audioDuration.IsPositive()) {
    auto duration = std::max(videoDuration, audioDuration);
    LOG("Determine mMetadataDuration=%" PRId64, duration.ToMicroseconds());
    mInfo.mMetadataDuration = Some(duration);
  }

  mInfo.mMediaSeekable = mDemuxer->IsSeekable();
  mInfo.mMediaSeekableOnlyInBufferedRanges =
      mDemuxer->IsSeekableOnlyInBufferedRanges();

  if (!videoActive && !audioActive) {
    LOG("No active audio or video track");
    mMetadataPromise.Reject(NS_ERROR_DOM_MEDIA_METADATA_ERR, __func__);
    return;
  }

  mTags = std::move(tags);
  mInitDone = true;

  // Try to get the start time.
  // For MSE case, the start time of each track is assumed to be 0.
  // For others, we must demux the first sample to know the start time for each
  // track.
  if (!mDemuxer->ShouldComputeStartTime()) {
    mAudio.mFirstDemuxedSampleTime.emplace(TimeUnit::Zero());
    mVideo.mFirstDemuxedSampleTime.emplace(TimeUnit::Zero());
  } else {
    if (HasAudio()) {
      RequestDemuxSamples(TrackInfo::kAudioTrack);
    }

    if (HasVideo()) {
      RequestDemuxSamples(TrackInfo::kVideoTrack);
    }
  }

  if (aResult != NS_OK) {
    mOnDecodeWarning.Notify(aResult);
  }

  MaybeResolveMetadataPromise();
}

void MediaFormatReader::MaybeResolveMetadataPromise() {
  MOZ_ASSERT(OnTaskQueue());

  if ((HasAudio() && mAudio.mFirstDemuxedSampleTime.isNothing()) ||
      (HasVideo() && mVideo.mFirstDemuxedSampleTime.isNothing())) {
    return;
  }

  TimeUnit startTime =
      std::min(mAudio.mFirstDemuxedSampleTime.refOr(TimeUnit::FromInfinity()),
               mVideo.mFirstDemuxedSampleTime.refOr(TimeUnit::FromInfinity()));

  if (!startTime.IsInfinite()) {
    mInfo.mStartTime = startTime;  // mInfo.mStartTime is initialized to 0.
    LOG("Set start time=%s", mInfo.mStartTime.ToString().get());
  }

  MetadataHolder metadata;
  metadata.mInfo = MakeUnique<MediaInfo>(mInfo);
  metadata.mTags = mTags->Count() ? std::move(mTags) : nullptr;

  // We now have all the informations required to calculate the initial buffered
  // range.
  mHasStartTime = true;
  UpdateBuffered();

  mWatchManager.Watch(mWorkingInfoChanged,
                      &MediaFormatReader::NotifyTrackInfoUpdated);
  mIsWatchingWorkingInfo = true;

  if (mReadMetadataStartTime) {
    mReadMetaDataTime = TimeStamp::Now() - *mReadMetadataStartTime;
    mReadMetadataStartTime.reset();
  }

  mMetadataPromise.Resolve(std::move(metadata), __func__);
}

bool MediaFormatReader::IsEncrypted() const {
  return (HasAudio() && mAudio.GetCurrentInfo()->mCrypto.IsEncrypted()) ||
         (HasVideo() && mVideo.GetCurrentInfo()->mCrypto.IsEncrypted());
}

void MediaFormatReader::OnDemuxerInitFailed(const MediaResult& aError) {
  mDemuxerInitRequest.Complete();
  mMetadataPromise.Reject(aError, __func__);
}

void MediaFormatReader::ReadUpdatedMetadata(MediaInfo* aInfo) {
  // Called on the MDSM's TaskQueue.
  {
    MutexAutoLock lock(mVideo.mMutex);
    if (HasVideo()) {
      aInfo->mVideo = *mVideo.GetWorkingInfo()->GetAsVideoInfo();
    }
  }
  {
    MutexAutoLock lock(mAudio.mMutex);
    if (HasAudio()) {
      aInfo->mAudio = *mAudio.GetWorkingInfo()->GetAsAudioInfo();
    }
  }
}

MediaFormatReader::DecoderData& MediaFormatReader::GetDecoderData(
    TrackType aTrack) {
  MOZ_ASSERT(aTrack == TrackInfo::kAudioTrack ||
             aTrack == TrackInfo::kVideoTrack);
  if (aTrack == TrackInfo::kAudioTrack) {
    return mAudio;
  }
  return mVideo;
}

Maybe<TimeUnit> MediaFormatReader::ShouldSkip(TimeUnit aTimeThreshold,
                                              bool aRequestNextVideoKeyFrame) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(HasVideo());

  if (!StaticPrefs::media_decoder_skip_to_next_key_frame_enabled()) {
    return Nothing();
  }

  // Ensure we have no pending seek going as skip-to-keyframe could return out
  // of date information.
  if (mVideo.HasInternalSeekPending()) {
    return Nothing();
  }

  TimeUnit nextKeyframe;
  nsresult rv = mVideo.mTrackDemuxer->GetNextRandomAccessPoint(&nextKeyframe);
  if (NS_FAILED(rv)) {
    // Only OggTrackDemuxer with video type gets into here.
    // We don't support skip-to-next-frame for this case.
    return Nothing();
  }

  const bool isNextKeyframeValid =
      nextKeyframe.ToMicroseconds() >= 0 && !nextKeyframe.IsInfinite();
  // If we request the next keyframe, only return times greater than
  // aTimeThreshold. Otherwise, data will be already behind the threshold and
  // will be eventually discarded somewhere in the media pipeline.
  if (aRequestNextVideoKeyFrame && isNextKeyframeValid &&
      nextKeyframe > aTimeThreshold) {
    return Some(nextKeyframe);
  }

  const bool isNextVideoBehindTheThreshold =
      (isNextKeyframeValid && nextKeyframe <= aTimeThreshold) ||
      GetInternalSeekTargetEndTime() < aTimeThreshold;
  return isNextVideoBehindTheThreshold ? Some(aTimeThreshold) : Nothing();
}

RefPtr<MediaFormatReader::VideoDataPromise> MediaFormatReader::RequestVideoData(
    const TimeUnit& aTimeThreshold, bool aRequestNextVideoKeyFrame) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(!mVideo.HasPromise(), "No duplicate sample requests");
  // Requesting video can be done independently from audio, even during audio
  // seeking. But it shouldn't happen if we're doing video seek.
  if (!IsAudioOnlySeeking()) {
    MOZ_DIAGNOSTIC_ASSERT(mSeekPromise.IsEmpty(),
                          "No sample requests allowed while seeking");
    MOZ_DIAGNOSTIC_ASSERT(!mVideo.mSeekRequest.Exists() ||
                          mVideo.mTimeThreshold.isSome());
    MOZ_DIAGNOSTIC_ASSERT(!IsSeeking(), "called mid-seek");
  }
  LOGV("RequestVideoData(%" PRId64 "), requestNextKeyFrame=%d",
       aTimeThreshold.ToMicroseconds(), aRequestNextVideoKeyFrame);

  if (!HasVideo()) {
    LOG("called with no video track");
    return VideoDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                             __func__);
  }

  if (IsSeeking()) {
    LOG("called mid-seek. Rejecting.");
    return VideoDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  if (mShutdown) {
    NS_WARNING("RequestVideoData on shutdown MediaFormatReader!");
    return VideoDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  if (Maybe<TimeUnit> target =
          ShouldSkip(aTimeThreshold, aRequestNextVideoKeyFrame)) {
    PROFILER_MARKER_UNTYPED("RequestVideoData SkipVideoDemuxToNextKeyFrame",
                            MEDIA_PLAYBACK);
    RefPtr<VideoDataPromise> p = mVideo.EnsurePromise(__func__);
    SkipVideoDemuxToNextKeyFrame(*target);
    return p;
  }

  RefPtr<VideoDataPromise> p = mVideo.EnsurePromise(__func__);
  ScheduleUpdate(TrackInfo::kVideoTrack);

  return p;
}

void MediaFormatReader::OnDemuxFailed(TrackType aTrack,
                                      const MediaResult& aError) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnDemuxFailed", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOG("Failed to demux %s, failure:%s",
      aTrack == TrackType::kVideoTrack ? "video" : "audio",
      aError.ErrorName().get());
  auto& decoder = GetDecoderData(aTrack);
  decoder.mDemuxRequest.Complete();
  switch (aError.Code()) {
    case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
      DDLOG(DDLogCategory::Log,
            aTrack == TrackType::kVideoTrack ? "video_demux_interruption"
                                             : "audio_demux_interruption",
            aError);
      if (!decoder.mWaitingForDataStartTime) {
        decoder.RequestDrain();
      }
      NotifyEndOfStream(aTrack);
      break;
    case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
      DDLOG(DDLogCategory::Log,
            aTrack == TrackType::kVideoTrack ? "video_demux_interruption"
                                             : "audio_demux_interruption",
            aError);
      if (!decoder.mWaitingForDataStartTime) {
        decoder.RequestDrain();
      } else {
        // mWaitingForDataStartTime was already set.  The decoder is being
        // primed after an internal seek.  A drain has already been performed.
        MOZ_ASSERT(decoder.mTimeThreshold.isSome() ||
                   decoder.mNumSamplesInput == decoder.mNumSamplesOutput);
      }
      NotifyWaitingForData(aTrack);
      break;
    case NS_ERROR_DOM_MEDIA_CANCELED:
      DDLOG(DDLogCategory::Log,
            aTrack == TrackType::kVideoTrack ? "video_demux_interruption"
                                             : "audio_demux_interruption",
            aError);
      if (decoder.HasPromise()) {
        decoder.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
      }
      break;
    default:
      DDLOG(DDLogCategory::Log,
            aTrack == TrackType::kVideoTrack ? "video_demux_error"
                                             : "audio_demux_error",
            aError);
      NotifyError(aTrack, aError);
      break;
  }
}

void MediaFormatReader::DoDemuxVideo() {
  AUTO_PROFILER_LABEL("MediaFormatReader::DoDemuxVideo", MEDIA_PLAYBACK);
  using SamplesPromise = MediaTrackDemuxer::SamplesPromise;

  DDLOG(DDLogCategory::Log, "video_demuxing", DDNoValue{});
  PerformanceRecorder<PlaybackStage> perfRecorder(
      MediaStage::RequestDemux,
      mVideo.GetCurrentInfo()->GetAsVideoInfo()->mImage.height);
  auto p = mVideo.mTrackDemuxer->GetSamples(1);

  RefPtr<MediaFormatReader> self = this;
  if (mVideo.mFirstDemuxedSampleTime.isNothing()) {
    p = p->Then(
        OwnerThread(), __func__,
        [self](RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
          AUTO_PROFILER_LABEL("MediaFormatReader::DoDemuxVideo:Resolved",
                              MEDIA_PLAYBACK);
          DDLOGEX(self.get(), DDLogCategory::Log, "video_first_demuxed",
                  DDNoValue{});
          self->OnFirstDemuxCompleted(TrackInfo::kVideoTrack, aSamples);
          return SamplesPromise::CreateAndResolve(aSamples.forget(), __func__);
        },
        [self](const MediaResult& aError) {
          AUTO_PROFILER_LABEL("MediaFormatReader::DoDemuxVideo:Rejected",
                              MEDIA_PLAYBACK);
          DDLOGEX(self.get(), DDLogCategory::Log, "video_first_demuxing_error",
                  aError);
          self->OnFirstDemuxFailed(TrackInfo::kVideoTrack, aError);
          return SamplesPromise::CreateAndReject(aError, __func__);
        });
  }

  p->Then(
       OwnerThread(), __func__,
       [self, perfRecorder(std::move(perfRecorder))](
           const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) mutable {
         perfRecorder.Record();
         self->OnVideoDemuxCompleted(aSamples);
       },
       [self](const MediaResult& aError) { self->OnVideoDemuxFailed(aError); })
      ->Track(mVideo.mDemuxRequest);
}

void MediaFormatReader::OnVideoDemuxCompleted(
    const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnVideoDemuxCompleted",
                      MEDIA_PLAYBACK);
  LOGV("%zu video samples demuxed (sid:%d)", aSamples->GetSamples().Length(),
       aSamples->GetSamples()[0]->mTrackInfo
           ? aSamples->GetSamples()[0]->mTrackInfo->GetID()
           : 0);
  DDLOG(DDLogCategory::Log, "video_demuxed_samples",
        uint64_t(aSamples->GetSamples().Length()));
  mVideo.mDemuxRequest.Complete();
  MOZ_ASSERT(mVideo.mQueuedSamples.IsEmpty());
  mVideo.mQueuedSamples = aSamples->GetMovableSamples();
  ScheduleUpdate(TrackInfo::kVideoTrack);
}

RefPtr<MediaFormatReader::AudioDataPromise>
MediaFormatReader::RequestAudioData() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(!mAudio.HasPromise(), "No duplicate sample requests");
  // Requesting audio can be done independently from video, even during video
  // seeking. But it shouldn't happen if we're doing audio seek.
  if (!IsVideoOnlySeeking()) {
    MOZ_DIAGNOSTIC_ASSERT(mSeekPromise.IsEmpty(),
                          "No sample requests allowed while seeking");
    MOZ_DIAGNOSTIC_ASSERT(!mAudio.mSeekRequest.Exists() ||
                          mAudio.mTimeThreshold.isSome());
    MOZ_DIAGNOSTIC_ASSERT(!IsSeeking(), "called mid-seek");
  }
  LOGV("");

  if (!HasAudio()) {
    LOG("called with no audio track");
    return AudioDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                             __func__);
  }

  if (IsSeeking()) {
    LOG("called mid-seek. Rejecting.");
    return AudioDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  if (mShutdown) {
    NS_WARNING("RequestAudioData on shutdown MediaFormatReader!");
    return AudioDataPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CANCELED,
                                             __func__);
  }

  RefPtr<AudioDataPromise> p = mAudio.EnsurePromise(__func__);
  ScheduleUpdate(TrackInfo::kAudioTrack);

  return p;
}

void MediaFormatReader::DoDemuxAudio() {
  AUTO_PROFILER_LABEL("MediaFormatReader::DoDemuxAudio", MEDIA_PLAYBACK);
  using SamplesPromise = MediaTrackDemuxer::SamplesPromise;

  DDLOG(DDLogCategory::Log, "audio_demuxing", DDNoValue{});
  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::RequestDemux);
  auto p = mAudio.mTrackDemuxer->GetSamples(1);

  RefPtr<MediaFormatReader> self = this;
  if (mAudio.mFirstDemuxedSampleTime.isNothing()) {
    p = p->Then(
        OwnerThread(), __func__,
        [self](RefPtr<MediaTrackDemuxer::SamplesHolder> aSamples) {
          AUTO_PROFILER_LABEL("MediaFormatReader::DoDemuxAudio:Resolved",
                              MEDIA_PLAYBACK);
          DDLOGEX(self.get(), DDLogCategory::Log, "audio_first_demuxed",
                  DDNoValue{});
          self->OnFirstDemuxCompleted(TrackInfo::kAudioTrack, aSamples);
          return SamplesPromise::CreateAndResolve(aSamples.forget(), __func__);
        },
        [self](const MediaResult& aError) {
          AUTO_PROFILER_LABEL("MediaFormatReader::DoDemuxAudio:Rejected",
                              MEDIA_PLAYBACK);
          DDLOGEX(self.get(), DDLogCategory::Log, "audio_first_demuxing_error",
                  aError);
          self->OnFirstDemuxFailed(TrackInfo::kAudioTrack, aError);
          return SamplesPromise::CreateAndReject(aError, __func__);
        });
  }

  p->Then(
       OwnerThread(), __func__,
       [self, perfRecorder(std::move(perfRecorder))](
           const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) mutable {
         perfRecorder.Record();
         self->OnAudioDemuxCompleted(aSamples);
       },
       [self](const MediaResult& aError) { self->OnAudioDemuxFailed(aError); })
      ->Track(mAudio.mDemuxRequest);
}

void MediaFormatReader::OnAudioDemuxCompleted(
    const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) {
  LOGV("%zu audio samples demuxed (sid:%d)", aSamples->GetSamples().Length(),
       aSamples->GetSamples()[0]->mTrackInfo
           ? aSamples->GetSamples()[0]->mTrackInfo->GetID()
           : 0);
  DDLOG(DDLogCategory::Log, "audio_demuxed_samples",
        uint64_t(aSamples->GetSamples().Length()));
  mAudio.mDemuxRequest.Complete();
  MOZ_ASSERT(mAudio.mQueuedSamples.IsEmpty());
  mAudio.mQueuedSamples = aSamples->GetMovableSamples();
  ScheduleUpdate(TrackInfo::kAudioTrack);
}

void MediaFormatReader::NotifyNewOutput(
    TrackType aTrack, MediaDataDecoder::DecodedData&& aResults) {
  AUTO_PROFILER_LABEL("MediaFormatReader::NotifyNewOutput", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  if (aResults.IsEmpty()) {
    DDLOG(DDLogCategory::Log,
          aTrack == TrackInfo::kAudioTrack ? "decoded_audio" : "decoded_video",
          "no output samples");
  } else {
    for (auto&& sample : aResults) {
      if (DecoderDoctorLogger::IsDDLoggingEnabled()) {
        switch (sample->mType) {
          case MediaData::Type::AUDIO_DATA:
            DDLOGPR(DDLogCategory::Log,
                    aTrack == TrackInfo::kAudioTrack ? "decoded_audio"
                                                     : "decoded_got_audio!?",
                    "{\"type\":\"AudioData\", \"offset\":%" PRIi64
                    ", \"time_us\":%" PRIi64 ", \"timecode_us\":%" PRIi64
                    ", \"duration_us\":%" PRIi64 ", \"frames\":%" PRIu32
                    ", \"channels\":%" PRIu32 ", \"rate\":%" PRIu32
                    ", \"bytes\":%zu}",
                    sample->mOffset, sample->mTime.ToMicroseconds(),
                    sample->mTimecode.ToMicroseconds(),
                    sample->mDuration.ToMicroseconds(),
                    sample->As<AudioData>()->Frames(),
                    sample->As<AudioData>()->mChannels,
                    sample->As<AudioData>()->mRate,
                    sample->As<AudioData>()->Data().Length());
            break;
          case MediaData::Type::VIDEO_DATA:
            DDLOGPR(DDLogCategory::Log,
                    aTrack == TrackInfo::kVideoTrack ? "decoded_video"
                                                     : "decoded_got_video!?",
                    "{\"type\":\"VideoData\", \"offset\":%" PRIi64
                    ", \"time_us\":%" PRIi64 ", \"timecode_us\":%" PRIi64
                    ", \"duration_us\":%" PRIi64
                    ", \"kf\":%s, \"size\":[%" PRIi32 ",%" PRIi32 "]}",
                    sample->mOffset, sample->mTime.ToMicroseconds(),
                    sample->mTimecode.ToMicroseconds(),
                    sample->mDuration.ToMicroseconds(),
                    sample->mKeyframe ? "true" : "false",
                    sample->As<VideoData>()->mDisplay.width,
                    sample->As<VideoData>()->mDisplay.height);
            break;
          case MediaData::Type::RAW_DATA:
            DDLOGPR(DDLogCategory::Log,
                    aTrack == TrackInfo::kAudioTrack   ? "decoded_audio"
                    : aTrack == TrackInfo::kVideoTrack ? "decoded_video"
                                                       : "decoded_?",
                    "{\"type\":\"RawData\", \"offset\":%" PRIi64
                    " \"time_us\":%" PRIi64 ", \"timecode_us\":%" PRIi64
                    ", \"duration_us\":%" PRIi64 ", \"kf\":%s}",
                    sample->mOffset, sample->mTime.ToMicroseconds(),
                    sample->mTimecode.ToMicroseconds(),
                    sample->mDuration.ToMicroseconds(),
                    sample->mKeyframe ? "true" : "false");
            break;
          case MediaData::Type::NULL_DATA:
            DDLOGPR(DDLogCategory::Log,
                    aTrack == TrackInfo::kAudioTrack   ? "decoded_audio"
                    : aTrack == TrackInfo::kVideoTrack ? "decoded_video"
                                                       : "decoded_?",
                    "{\"type\":\"NullData\", \"offset\":%" PRIi64
                    " \"time_us\":%" PRIi64 ", \"timecode_us\":%" PRIi64
                    ", \"duration_us\":%" PRIi64 ", \"kf\":%s}",
                    sample->mOffset, sample->mTime.ToMicroseconds(),
                    sample->mTimecode.ToMicroseconds(),
                    sample->mDuration.ToMicroseconds(),
                    sample->mKeyframe ? "true" : "false");
            break;
        }
      }
      LOGV("Received new %s sample time:%" PRId64 " duration:%" PRId64,
           TrackTypeToStr(aTrack), sample->mTime.ToMicroseconds(),
           sample->mDuration.ToMicroseconds());
      decoder.mOutput.AppendElement(sample);
      decoder.mNumSamplesOutput++;
      decoder.mNumOfConsecutiveDecodingError = 0;
      decoder.mNumOfConsecutiveRDDOrGPUCrashes = 0;
      if (aTrack == TrackInfo::kAudioTrack) {
        decoder.mNumOfConsecutiveUtilityCrashes = 0;
      }
      if (sample->mType == MediaData::Type::VIDEO_DATA) {
        nsCString dummy;
        bool wasHardwareAccelerated = decoder.mIsHardwareAccelerated;
        decoder.mIsHardwareAccelerated =
            mVideo.mDecoder->IsHardwareAccelerated(dummy);
        if (!decoder.mHasReportedVideoHardwareSupportTelemtry ||
            wasHardwareAccelerated != decoder.mIsHardwareAccelerated) {
          decoder.mHasReportedVideoHardwareSupportTelemtry = true;
          VideoData* videoData = sample->As<VideoData>();
          glean::media::video_hardware_decoding_support
              .Get(decoder.GetCurrentInfo()->mMimeType)
              .Set(!!decoder.mIsHardwareAccelerated);
          static constexpr gfx::IntSize HD_VIDEO_SIZE{1280, 720};
          if (videoData->mDisplay.width >= HD_VIDEO_SIZE.Width() &&
              videoData->mDisplay.height >= HD_VIDEO_SIZE.Height()) {
            glean::media::video_hd_hardware_decoding_support
                .Get(decoder.GetCurrentInfo()->mMimeType)
                .Set(!!decoder.mIsHardwareAccelerated);
          }
        }
        // As the real video decoder creation might be delayed, we want to
        // update the decoder name again, instead of using the wrong name.
        if (decoder.mNumSamplesOutput == 1) {
          decoder.mDescription = mVideo.mDecoder->GetDescriptionName();
        }
      }
      decoder.mDecodePerfRecorder->Record(
          sample->mTime.ToMicroseconds(),
          [startTime = sample->mTime.ToMicroseconds(),
           endTime = sample->GetEndTime().ToMicroseconds(),
           flag =
               sample->mType == MediaData::Type::VIDEO_DATA &&
                       decoder.mIsHardwareAccelerated
                   ? MediaInfoFlag::HardwareDecoding
                   : MediaInfoFlag::SoftwareDecoding](PlaybackStage& aStage) {
            aStage.SetStartTimeAndEndTime(startTime, endTime);
            aStage.AddFlag(flag);
          });
    }
  }
  LOG("Done processing new %s samples", TrackTypeToStr(aTrack));

  if (!aResults.IsEmpty()) {
    // We have decoded our first frame, we can now starts to skip future errors.
    decoder.mFirstFrameTime.reset();
  }
  ScheduleUpdate(aTrack);
}

void MediaFormatReader::NotifyError(TrackType aTrack,
                                    const MediaResult& aError) {
  MOZ_ASSERT(OnTaskQueue());
  NS_WARNING(aError.Description().get());
  LOG("%s Decoding error: %s", TrackTypeToStr(aTrack),
      aError.Description().get());
  auto& decoder = GetDecoderData(aTrack);
  decoder.mError = decoder.HasFatalError() ? decoder.mError : Some(aError);

  ScheduleUpdate(aTrack);
}

void MediaFormatReader::NotifyWaitingForData(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  LOGV("%s", TrackTypeToStr(aTrack));
  auto& decoder = GetDecoderData(aTrack);
  // mWaitingForDataStartTime may have already been set before draining the
  // decoder.
  if (!decoder.mWaitingForDataStartTime) {
    decoder.mWaitingForDataStartTime.emplace(TimeStamp::Now());
  }
  if (decoder.mTimeThreshold) {
    // An InternalSeek() is in progress, which might be performed while
    // mWaitingForDataStartTime is set, such as when priming the decoder after
    // a drain.  Set mWaiting to indicate that the internal seek cannot make
    // progress.
    decoder.mTimeThreshold.ref().mWaiting = true;
  }
  ScheduleUpdate(aTrack);
}

void MediaFormatReader::NotifyWaitingForKey(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  mOnWaitingForKey.Notify();
  if (!decoder.mDecodeRequest.Exists()) {
    LOGV("WaitingForKey received while no pending decode. Ignoring");
    return;
  }
  decoder.mWaitingForKey = true;
  ScheduleUpdate(aTrack);
}

void MediaFormatReader::NotifyEndOfStream(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  decoder.mDemuxEOS = true;
  ScheduleUpdate(aTrack);
}

bool MediaFormatReader::NeedInput(DecoderData& aDecoder) {
  // The decoder will not be fed a new raw sample until the current decoding
  // requests has completed.
  return (aDecoder.HasPromise() || aDecoder.mTimeThreshold.isSome()) &&
         !aDecoder.HasPendingDrain() && !aDecoder.HasFatalError() &&
         !aDecoder.mDemuxRequest.Exists() && !aDecoder.mOutput.Length() &&
         !aDecoder.HasInternalSeekPending() &&
         !aDecoder.mDecodeRequest.Exists();
}

void MediaFormatReader::ScheduleUpdate(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  if (mShutdown) {
    return;
  }
  auto& decoder = GetDecoderData(aTrack);
  MOZ_RELEASE_ASSERT(decoder.GetCurrentInfo(),
                     "Can only schedule update when track exists");

  if (decoder.mUpdateScheduled) {
    return;
  }
  LOGV("SchedulingUpdate(%s)", TrackTypeToStr(aTrack));
  decoder.mUpdateScheduled = true;
  RefPtr<nsIRunnable> task(NewRunnableMethod<TrackType>(
      "MediaFormatReader::Update", this, &MediaFormatReader::Update, aTrack));
  nsresult rv = OwnerThread()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

bool MediaFormatReader::UpdateReceivedNewData(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);

  if (!decoder.mReceivedNewData) {
    LOGV("!decoder.mReceivedNewData");
    return false;
  }

  // We do not want to clear mWaitingForDataStartTime while there are pending
  // demuxing or seeking operations that could affect the value of this flag.
  // This is in order to ensure that we will retry once they complete as we may
  // now have new data that could potentially allow those operations to
  // successfully complete if tried again.
  LOGV("%s", TrackTypeToStr(aTrack));
  if (decoder.mSeekRequest.Exists()) {
    // Nothing more to do until this operation complete.
    return true;
  }

  if (aTrack == TrackType::kVideoTrack && mSkipRequest.Exists()) {
    LOGV("Skipping in progress, nothing more to do");
    return true;
  }

  if (decoder.mDemuxRequest.Exists()) {
    LOGV("decoder.mDemuxRequest.Exists()");
    // We may have pending operations to process, so we want to continue
    // after UpdateReceivedNewData returns.
    return false;
  }

  if (decoder.HasPendingDrain()) {
    LOGV("decoder.HasPendingDrain()");
    // We do not want to clear mWaitingForDataStartTime or mDemuxEOS while
    // a drain is in progress in order to properly complete the operation.
    // The drain may provide more frames that should be returned before
    // rejection of a MediaData promise.
    return false;
  }

  decoder.mReceivedNewData = false;
  if (decoder.mTimeThreshold) {
    decoder.mTimeThreshold.ref().mWaiting = false;
  }
  if (aTrack == TrackType::kVideoTrack && decoder.mWaitingForDataStartTime) {
    mTotalWaitingForVideoDataTime +=
        TimeStamp::Now() - *decoder.mWaitingForDataStartTime;
  }
  decoder.mWaitingForDataStartTime.reset();

  if (decoder.HasFatalError()) {
    LOGV("decoder.HasFatalError()");
    return false;
  }

  if (!mSeekPromise.IsEmpty() &&
      (!IsVideoOnlySeeking() || aTrack == TrackInfo::kVideoTrack)) {
    MOZ_ASSERT(!decoder.HasPromise());
    MOZ_DIAGNOSTIC_ASSERT(
        (IsVideoOnlySeeking() || !mAudio.mTimeThreshold) &&
            !mVideo.mTimeThreshold,
        "InternalSeek must have been aborted when Seek was first called");
    MOZ_DIAGNOSTIC_ASSERT(
        (IsVideoOnlySeeking() || !mAudio.HasWaitingPromise()) &&
            !mVideo.HasWaitingPromise(),
        "Waiting promises must have been rejected when Seek was first called");
    if (mVideo.mSeekRequest.Exists() ||
        (!IsVideoOnlySeeking() && mAudio.mSeekRequest.Exists())) {
      // Already waiting for a seek to complete. Nothing more to do.
      return true;
    }
    LOG("Attempting Seek");
    ScheduleSeek();
    return true;
  }
  if (decoder.HasInternalSeekPending() || decoder.HasWaitingPromise()) {
    if (decoder.HasInternalSeekPending()) {
      LOG("Attempting Internal Seek");
      InternalSeek(aTrack, decoder.mTimeThreshold.ref());
    }
    if (decoder.HasWaitingPromise() && !decoder.IsWaitingForKey()) {
      MOZ_ASSERT(!decoder.HasPromise());
      LOG("We have new data. Resolving WaitingPromise");
      decoder.mWaitingPromise.Resolve(decoder.mType, __func__);
      MOZ_ASSERT(!decoder.IsWaitingForData());
    }
    return true;
  }
  return false;
}

void MediaFormatReader::RequestDemuxSamples(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  MOZ_ASSERT(!decoder.mDemuxRequest.Exists());

  if (!decoder.mQueuedSamples.IsEmpty()) {
    // No need to demux new samples.
    return;
  }

  if (decoder.mDemuxEOS) {
    // Nothing left to demux.
    // We do not want to attempt to demux while in waiting for data mode
    // as it would retrigger an unnecessary drain.
    return;
  }

  LOGV("Requesting extra demux %s", TrackTypeToStr(aTrack));
  if (aTrack == TrackInfo::kVideoTrack) {
    DoDemuxVideo();
  } else {
    DoDemuxAudio();
  }
}

void MediaFormatReader::DecoderData::StartRecordDecodingPerf(
    const TrackType aTrack, const MediaRawData* aSample) {
  if (!mDecodePerfRecorder) {
    mDecodePerfRecorder.reset(new PerformanceRecorderMulti<PlaybackStage>());
  }
  const int32_t height = aTrack == TrackInfo::kVideoTrack
                             ? GetCurrentInfo()->GetAsVideoInfo()->mImage.height
                             : 0;
  MediaInfoFlag flag = MediaInfoFlag::None;
  flag |=
      aSample->mKeyframe ? MediaInfoFlag::KeyFrame : MediaInfoFlag::NonKeyFrame;
  if (aTrack == TrackInfo::kVideoTrack) {
    const nsCString& mimeType = GetCurrentInfo()->mMimeType;
    if (MP4Decoder::IsH264(mimeType)) {
      flag |= MediaInfoFlag::VIDEO_H264;
    } else if (VPXDecoder::IsVPX(mimeType, VPXDecoder::VP8)) {
      flag |= MediaInfoFlag::VIDEO_VP8;
    } else if (VPXDecoder::IsVPX(mimeType, VPXDecoder::VP9)) {
      flag |= MediaInfoFlag::VIDEO_VP9;
    } else if (MP4Decoder::IsHEVC(mimeType)) {
      flag |= MediaInfoFlag::VIDEO_HEVC;
    }
#ifdef MOZ_AV1
    else if (AOMDecoder::IsAV1(mimeType)) {
      flag |= MediaInfoFlag::VIDEO_AV1;
    }
#endif
  }
  mDecodePerfRecorder->Start(aSample->mTime.ToMicroseconds(),
                             MediaStage::RequestDecode, height, flag);
}

void MediaFormatReader::DecodeDemuxedSamples(TrackType aTrack,
                                             MediaRawData* aSample) {
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  RefPtr<MediaFormatReader> self = this;
  decoder.mFlushed = false;
  DDLOGPR(DDLogCategory::Log,
          aTrack == TrackInfo::kAudioTrack   ? "decode_audio"
          : aTrack == TrackInfo::kVideoTrack ? "decode_video"
                                             : "decode_?",
          "{\"type\":\"MediaRawData\", \"offset\":%" PRIi64
          ", \"bytes\":%zu, \"time_us\":%" PRIi64 ", \"timecode_us\":%" PRIi64
          ", \"duration_us\":%" PRIi64 ",%s%s}",
          aSample->mOffset, aSample->Size(), aSample->mTime.ToMicroseconds(),
          aSample->mTimecode.ToMicroseconds(),
          aSample->mDuration.ToMicroseconds(), aSample->mKeyframe ? " kf" : "",
          aSample->mEOS ? " eos" : "");

  decoder.StartRecordDecodingPerf(aTrack, aSample);
  if (mMediaEngineId && aSample->mCrypto.IsEncrypted()) {
    aSample->mShouldCopyCryptoToRemoteRawData = true;
  }
  decoder.mDecoder->Decode(aSample)
      ->Then(
          mTaskQueue, __func__,
          [self, aTrack,
           &decoder](MediaDataDecoder::DecodedData&& aResults) mutable {
            decoder.mDecodeRequest.Complete();
            self->NotifyNewOutput(aTrack, std::move(aResults));
          },
          [self, aTrack, &decoder](const MediaResult& aError) {
            decoder.mDecodeRequest.Complete();
            self->NotifyError(aTrack, aError);
          })
      ->Track(decoder.mDecodeRequest);
}

void MediaFormatReader::HandleDemuxedSamples(
    TrackType aTrack, FrameStatistics::AutoNotifyDecoded& aA) {
  MOZ_ASSERT(OnTaskQueue());

  auto& decoder = GetDecoderData(aTrack);

  if (decoder.mFlushing) {
    LOGV("Decoder operation in progress, let it complete.");
    return;
  }

  if (decoder.mQueuedSamples.IsEmpty()) {
    return;
  }

  RefPtr<MediaRawData> sample = decoder.mQueuedSamples[0];
  const RefPtr<TrackInfoSharedPtr> info = sample->mTrackInfo;

  if (info && decoder.mLastStreamSourceID != info->GetID()) {
    nsTArray<RefPtr<MediaRawData>> samples;
    if (decoder.mDecoder) {
      bool recyclable =
          StaticPrefs::media_decoder_recycle_enabled() &&
          decoder.mDecoder->SupportDecoderRecycling() &&
          (*info)->mCrypto.mCryptoScheme ==
              decoder.GetCurrentInfo()->mCrypto.mCryptoScheme &&
          (*info)->mMimeType == decoder.GetCurrentInfo()->mMimeType;
      LOG("%s stream id has changed from:%d to:%d, recyclable=%d, "
          "alwaysRecyle=%d",
          TrackTypeToStr(aTrack), decoder.mLastStreamSourceID, info->GetID(),
          recyclable, decoder.mDecoder->ShouldDecoderAlwaysBeRecycled());
      recyclable |= decoder.mDecoder->ShouldDecoderAlwaysBeRecycled();
      if (!recyclable && decoder.mTimeThreshold.isNothing() &&
          (decoder.mNextStreamSourceID.isNothing() ||
           decoder.mNextStreamSourceID.ref() != info->GetID())) {
        LOG("draining decoder for stream id change.");
        decoder.RequestDrain();
        decoder.mNextStreamSourceID = Some(info->GetID());
        ScheduleUpdate(aTrack);
        return;
      }

      // If flushing is required, it will clear our array of queued samples.
      // So we may need to make a copy.
      samples = decoder.mQueuedSamples.Clone();
      if (!recyclable) {
        LOG("Decoder does not support recycling, recreate decoder.");
        ShutdownDecoder(aTrack);
        // We're going to be using a new decoder following the change of content
        // We can attempt to use hardware decoding again.
        decoder.mHardwareDecodingDisabled = false;
        decoder.mFirstFrameTime = Some(sample->mTime);
      } else if (decoder.HasWaitingPromise()) {
        decoder.Flush();
      }
    }

    nsPrintfCString markerString(
        "%s stream id changed from:%" PRIu32 " to:%" PRIu32,
        TrackTypeToStr(aTrack), decoder.mLastStreamSourceID, info->GetID());
    PROFILER_MARKER_TEXT("StreamID Change", MEDIA_PLAYBACK, {}, markerString);
    LOG("%s", markerString.get());

    decoder.mNextStreamSourceID.reset();
    decoder.mLastStreamSourceID = info->GetID();
    decoder.mInfo = info;
    {
      MutexAutoLock lock(decoder.mMutex);
      if (aTrack == TrackInfo::kAudioTrack) {
        decoder.mWorkingInfo = MakeUnique<AudioInfo>(*info->GetAsAudioInfo());
      } else if (aTrack == TrackInfo::kVideoTrack) {
        decoder.mWorkingInfo = MakeUnique<VideoInfo>(*info->GetAsVideoInfo());
      }
      mWorkingInfoChanged = true;
    }

    decoder.mMeanRate.Reset();

    if (sample->mKeyframe) {
      if (samples.Length()) {
        decoder.mQueuedSamples = std::move(samples);
      }
    } else {
      auto time = TimeInterval(sample->mTime, sample->GetEndTime());
      InternalSeekTarget seekTarget =
          decoder.mTimeThreshold.refOr(InternalSeekTarget(time, false));
      LOG("Stream change occurred on a non-keyframe. Seeking to:%" PRId64,
          sample->mTime.ToMicroseconds());
      InternalSeek(aTrack, seekTarget);
      return;
    }
  }

  // Calculate the average frame rate. The first frame will be accounted
  // for twice.
  decoder.mMeanRate.Update(sample->mDuration);

  if (!decoder.mDecoder) {
    // In Clear Lead situation, the `mInfo` could change from unencrypted to
    // encrypted so we need to ensure the CDM proxy is ready before creating a
    // decoder.
    if (decoder.IsEncrypted() &&
        (IsWaitingOnCDMResource() || !ResolveSetCDMPromiseIfDone(aTrack))) {
      return;
    }
    mDecoderFactory->CreateDecoder(aTrack);
    return;
  }

  LOGV("%s Input:%" PRId64 " (dts:%" PRId64 " kf:%d)", TrackTypeToStr(aTrack),
       sample->mTime.ToMicroseconds(), sample->mTimecode.ToMicroseconds(),
       sample->mKeyframe);
  decoder.mNumSamplesInput++;
  decoder.mSizeOfQueue++;
  if (aTrack == TrackInfo::kVideoTrack) {
    aA.mStats.mParsedFrames++;
  }

  DecodeDemuxedSamples(aTrack, sample);

  decoder.mQueuedSamples.RemoveElementAt(0);
}

media::TimeUnit MediaFormatReader::GetInternalSeekTargetEndTime() const {
  MOZ_ASSERT(OnTaskQueue());
  return mVideo.mTimeThreshold ? mVideo.mTimeThreshold->EndTime()
                               : TimeUnit::FromInfinity();
}

void MediaFormatReader::InternalSeek(TrackType aTrack,
                                     const InternalSeekTarget& aTarget) {
  MOZ_ASSERT(OnTaskQueue());
  LOG("%s internal seek to %f", TrackTypeToStr(aTrack),
      aTarget.Time().ToSeconds());

  auto& decoder = GetDecoderData(aTrack);
  decoder.Flush();
  decoder.ResetDemuxer();
  decoder.mTimeThreshold = Some(aTarget);
  DDLOG(DDLogCategory::Log, "seeking", DDNoValue{});
  RefPtr<MediaFormatReader> self = this;
  decoder.mTrackDemuxer->Seek(decoder.mTimeThreshold.ref().Time())
      ->Then(
          OwnerThread(), __func__,
          [self, aTrack](TimeUnit aTime) {
            DDLOGEX(self.get(), DDLogCategory::Log, "seeked", DDNoValue{});
            auto& decoder = self->GetDecoderData(aTrack);
            decoder.mSeekRequest.Complete();
            MOZ_ASSERT(decoder.mTimeThreshold,
                       "Seek promise must be disconnected when "
                       "timethreshold is reset");
            decoder.mTimeThreshold.ref().mHasSeeked = true;
            self->SetVideoDecodeThreshold();
            self->ScheduleUpdate(aTrack);
          },
          [self, aTrack](const MediaResult& aError) {
            auto& decoder = self->GetDecoderData(aTrack);
            decoder.mSeekRequest.Complete();
            switch (aError.Code()) {
              case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
                DDLOGEX(self.get(), DDLogCategory::Log, "seeking_interrupted",
                        aError);
                self->NotifyWaitingForData(aTrack);
                break;
              case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
                DDLOGEX(self.get(), DDLogCategory::Log, "seeking_interrupted",
                        aError);
                decoder.mTimeThreshold.reset();
                self->NotifyEndOfStream(aTrack);
                break;
              case NS_ERROR_DOM_MEDIA_CANCELED:
                DDLOGEX(self.get(), DDLogCategory::Log, "seeking_interrupted",
                        aError);
                decoder.mTimeThreshold.reset();
                break;
              default:
                DDLOGEX(self.get(), DDLogCategory::Log, "seeking_error",
                        aError);
                decoder.mTimeThreshold.reset();
                self->NotifyError(aTrack, aError);
                break;
            }
          })
      ->Track(decoder.mSeekRequest);
}

void MediaFormatReader::DrainDecoder(TrackType aTrack) {
  AUTO_PROFILER_LABEL("MediaFormatReader::DrainDecoder", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  auto& decoder = GetDecoderData(aTrack);
  if (decoder.mDrainState == DrainState::Draining) {
    return;
  }
  if (!decoder.mDecoder ||
      (decoder.mDrainState != DrainState::PartialDrainPending &&
       decoder.mNumSamplesInput == decoder.mNumSamplesOutput)) {
    // No frames to drain.
    LOGV("Draining %s with nothing to drain", TrackTypeToStr(aTrack));
    decoder.mDrainState = DrainState::DrainAborted;
    ScheduleUpdate(aTrack);
    return;
  }

  decoder.mDrainState = DrainState::Draining;

  DDLOG(DDLogCategory::Log, "draining", DDNoValue{});
  RefPtr<MediaFormatReader> self = this;
  decoder.mDecoder->Drain()
      ->Then(
          mTaskQueue, __func__,
          [self, aTrack, &decoder](MediaDataDecoder::DecodedData&& aResults) {
            decoder.mDrainRequest.Complete();
            DDLOGEX(self.get(), DDLogCategory::Log, "drained", DDNoValue{});
            if (aResults.IsEmpty()) {
              decoder.mDrainState = DrainState::DrainCompleted;
            } else {
              self->NotifyNewOutput(aTrack, std::move(aResults));
              // Let's see if we have any more data available to drain.
              decoder.mDrainState = DrainState::PartialDrainPending;
            }
            self->ScheduleUpdate(aTrack);
          },
          [self, aTrack, &decoder](const MediaResult& aError) {
            decoder.mDrainRequest.Complete();
            DDLOGEX(self.get(), DDLogCategory::Log, "draining_error", aError);
            self->NotifyError(aTrack, aError);
          })
      ->Track(decoder.mDrainRequest);
  LOG("Requesting %s decoder to drain", TrackTypeToStr(aTrack));
}

// See https://firefox-source-docs.mozilla.org/media/MediaFormatReader.html
// for a state diagram overview.
void MediaFormatReader::Update(TrackType aTrack) {
  AUTO_PROFILER_LABEL("MediaFormatReader::Update", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  LOGV("Processing update for %s", TrackTypeToStr(aTrack));

  bool needOutput = false;
  auto& decoder = GetDecoderData(aTrack);
  decoder.mUpdateScheduled = false;

  if (!mInitDone) {
    return;
  }

  if (aTrack == TrackType::kVideoTrack && mSkipRequest.Exists()) {
    LOGV("Skipping in progress, nothing more to do");
    return;
  }

  if (UpdateReceivedNewData(aTrack)) {
    LOGV("Nothing more to do");
    return;
  }

  if (decoder.mSeekRequest.Exists()) {
    LOGV("Seeking hasn't completed, nothing more to do");
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !decoder.HasInternalSeekPending() ||
          (!decoder.mOutput.Length() && !decoder.mQueuedSamples.Length()),
      "No frames can be demuxed or decoded while an internal seek is pending");

  // Record number of frames decoded and parsed. Automatically update the
  // stats counters using the AutoNotifyDecoded stack-based class.
  FrameStatistics::AutoNotifyDecoded a(mFrameStats);

  // Drop any frames found prior our internal seek target.
  while (decoder.mTimeThreshold && decoder.mOutput.Length()) {
    RefPtr<MediaData>& output = decoder.mOutput[0];
    InternalSeekTarget target = decoder.mTimeThreshold.ref();
    auto time = output->mTime;
    if (time >= target.Time()) {
      // We have reached our internal seek target.
      decoder.mTimeThreshold.reset();
      // We might have dropped some keyframes.
      if (aTrack == TrackType::kVideoTrack) {
        mPreviousDecodedKeyframeTime_us = sNoPreviousDecodedKeyframe;
      }
    }
    if (time < target.Time() || (target.mDropTarget && target.Contains(time))) {
      LOGV("Internal Seeking: Dropping %s frame time:%f wanted:%f (kf:%d)",
           TrackTypeToStr(aTrack), output->mTime.ToSeconds(),
           target.Time().ToSeconds(), output->mKeyframe);
      decoder.mOutput.RemoveElementAt(0);
      decoder.mSizeOfQueue -= 1;
    }
  }

  while (decoder.mOutput.Length() &&
         decoder.mOutput[0]->mType == MediaData::Type::NULL_DATA) {
    LOGV("Dropping null data. Time: %" PRId64,
         decoder.mOutput[0]->mTime.ToMicroseconds());
    decoder.mOutput.RemoveElementAt(0);
    decoder.mSizeOfQueue -= 1;
  }

  if (decoder.HasPromise()) {
    needOutput = true;
    if (decoder.mOutput.Length()) {
      RefPtr<MediaData> output = decoder.mOutput[0];
      decoder.mOutput.RemoveElementAt(0);
      decoder.mSizeOfQueue -= 1;
      decoder.mLastDecodedSampleTime =
          Some(TimeInterval(output->mTime, output->GetEndTime()));
      decoder.mNumSamplesOutputTotal++;
      ReturnOutput(output, aTrack);
      // We have a decoded sample ready to be returned.
      if (aTrack == TrackType::kVideoTrack) {
        uint64_t delta =
            decoder.mNumSamplesOutputTotal - mLastReportedNumDecodedFrames;
        a.mStats.mDecodedFrames = static_cast<uint32_t>(delta);
        mLastReportedNumDecodedFrames = decoder.mNumSamplesOutputTotal;
        if (output->mKeyframe) {
          if (mPreviousDecodedKeyframeTime_us <
              output->mTime.ToMicroseconds()) {
            // There is a previous keyframe -> Record inter-keyframe stats.
            uint64_t segment_us = output->mTime.ToMicroseconds() -
                                  mPreviousDecodedKeyframeTime_us;
            a.mStats.mInterKeyframeSum_us += segment_us;
            a.mStats.mInterKeyframeCount += 1;
            if (a.mStats.mInterKeyFrameMax_us < segment_us) {
              a.mStats.mInterKeyFrameMax_us = segment_us;
            }
          }
          mPreviousDecodedKeyframeTime_us = output->mTime.ToMicroseconds();
        }
      }
    } else if (decoder.HasFatalError()) {
      nsCString mimeType = decoder.GetCurrentInfo()->mMimeType;
      if (!mimeType.IsEmpty()) {
        glean::media_playback::DecodeErrorExtra extraData;
        extraData.mimeType = Some(mimeType);
        extraData.errorName = Some(decoder.mError->ErrorName());
        if (mCDMProxy) {
          extraData.keySystem =
              Some(NS_ConvertUTF16toUTF8(mCDMProxy->KeySystem()));
        }
        extraData.decoderName = Some(decoder.mDescription);
        extraData.isHardwareAccelerated = Some(decoder.mIsHardwareAccelerated);
        glean::media_playback::decode_error.Record(Some(extraData));
      }
      LOG("Rejecting %s promise for %s : DECODE_ERROR", TrackTypeToStr(aTrack),
          mimeType.get());
      decoder.RejectPromise(decoder.mError.ref(), __func__);
      return;
    } else if (decoder.HasCompletedDrain()) {
      if (decoder.mDemuxEOS) {
        LOG("Rejecting %s promise: EOS", TrackTypeToStr(aTrack));
        decoder.RejectPromise(NS_ERROR_DOM_MEDIA_END_OF_STREAM, __func__);
      } else if (decoder.mWaitingForDataStartTime) {
        if (decoder.mDrainState == DrainState::DrainCompleted &&
            decoder.mLastDecodedSampleTime && !decoder.mNextStreamSourceID) {
          // We have completed draining the decoder following WaitingForData.
          // Prime the decoder so that it is ready to resume from the
          // last sample decoded.
          LOG("Seeking to last sample time: %" PRId64,
              decoder.mLastDecodedSampleTime.ref().mStart.ToMicroseconds());
          InternalSeek(aTrack, InternalSeekTarget(
                                   decoder.mLastDecodedSampleTime.ref(), true));
        }
        if (!decoder.mReceivedNewData) {
          LOG("Rejecting %s promise: WAITING_FOR_DATA", TrackTypeToStr(aTrack));
          decoder.RejectPromise(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
        }
      }

      decoder.mDrainState = DrainState::None;

      // Now that draining has completed, we check if we have received
      // new data again as the result may now be different from the earlier
      // run.
      if (UpdateReceivedNewData(aTrack) || decoder.mSeekRequest.Exists()) {
        LOGV("Completed drain: Nothing more to do");
        return;
      }
    } else if (decoder.IsWaitingForData() && !decoder.HasPendingDrain()) {
      // This path is triggered when an internal seek request on mTrackDemuxer
      // returns NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, in which case there is
      // no drain.  No new output samples will be available until more data
      // arrives.
      MOZ_ASSERT(!decoder.mDemuxRequest.Exists());
      MOZ_ASSERT(decoder.mQueuedSamples.IsEmpty());
      // mReceivedNewData would have caused UpdateReceivedNewData() to clear
      // mWaitingForDataStartTime.
      MOZ_ASSERT(!decoder.mReceivedNewData);
      LOG("Rejecting %s promise: WAITING_FOR_DATA during internal seek",
          TrackTypeToStr(aTrack));
      decoder.RejectPromise(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
      // Wait until more data arrives.
      return;
    } else if (decoder.mDemuxEOS && !decoder.HasPendingDrain() &&
               decoder.mQueuedSamples.IsEmpty()) {
      // It is possible to transition from WAITING_FOR_DATA directly to EOS
      // state during the internal seek; in which case no draining would occur.
      // There is no more samples left to be decoded and we are already in
      // EOS state. We can immediately reject the data promise.
      LOG("Rejecting %s promise: EOS", TrackTypeToStr(aTrack));
      decoder.RejectPromise(NS_ERROR_DOM_MEDIA_END_OF_STREAM, __func__);
    } else if (decoder.mWaitingForKey) {
      LOG("Rejecting %s promise: WAITING_FOR_DATA due to waiting for key",
          TrackTypeToStr(aTrack));
      decoder.RejectPromise(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    } else if (IsDecoderWaitingForCDM(aTrack)) {
      // Rejecting the promise could lead to entering buffering state for MDSM,
      // once a qualified(with the same key system and sessions created by the
      // same InitData) new cdm proxy is set, decoding can be resumed.
      LOG("Rejecting %s promise: WAITING_FOR_DATA due to waiting for CDM",
          TrackTypeToStr(aTrack));
      decoder.RejectPromise(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    }
  }

  if (decoder.mDrainState == DrainState::DrainRequested ||
      decoder.mDrainState == DrainState::PartialDrainPending) {
    if (decoder.mOutput.IsEmpty()) {
      DrainDecoder(aTrack);
    }
    return;
  }

  if (decoder.mError && !decoder.HasFatalError()) {
    MOZ_RELEASE_ASSERT(!decoder.HasInternalSeekPending(),
                       "No error can occur while an internal seek is pending");

    nsCString error;
    bool firstFrameDecodingFailedWithHardware =
        decoder.mFirstFrameTime &&
        decoder.mError.ref() == NS_ERROR_DOM_MEDIA_DECODE_ERR &&
        decoder.mDecoder && decoder.mDecoder->IsHardwareAccelerated(error) &&
        !decoder.mHardwareDecodingDisabled;
    bool needsNewDecoder =
        decoder.mError.ref() == NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER ||
        firstFrameDecodingFailedWithHardware;
    // Limit number of process restarts after crash
    if ((decoder.mError.ref() ==
             NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_RDD_OR_GPU_ERR &&
         decoder.mNumOfConsecutiveRDDOrGPUCrashes++ <
             decoder.mMaxConsecutiveRDDOrGPUCrashes) ||
        (decoder.mError.ref() ==
             NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_UTILITY_ERR &&
         decoder.mNumOfConsecutiveUtilityCrashes++ <
             decoder.mMaxConsecutiveUtilityCrashes)) {
      needsNewDecoder = true;
    }
    // For MF CDM crash, it needs to be handled differently. We need to shutdown
    // current decoder and report that error to the state machine in order to
    // let it to determine if playback can keep going or not.
    if (decoder.mError.ref() ==
        NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_MF_CDM_ERR) {
      LOG("Error: notify MF CDM crash and shutdown %s decoder",
          TrackTypeToStr(aTrack));
      ShutdownDecoder(aTrack);
      decoder.RejectPromise(decoder.mError.ref(), __func__);
      decoder.mError.reset();
      return;
    }
#ifdef XP_LINUX
    // We failed to decode on Linux with HW decoder,
    // give it another try without HW decoder.
    if (decoder.mError.ref() == NS_ERROR_DOM_MEDIA_DECODE_ERR &&
        decoder.mDecoder->IsHardwareAccelerated(error)) {
      LOG("Error: %s decode error, disable HW acceleration",
          TrackTypeToStr(aTrack));
      needsNewDecoder = true;
      decoder.mHardwareDecodingDisabled = true;
    }
    // RDD process crashed on Linux, give it another try without HW decoder.
    if (decoder.mError.ref() ==
        NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_RDD_OR_GPU_ERR) {
      LOG("Error: %s remote decoder crashed, disable HW acceleration",
          TrackTypeToStr(aTrack));
      decoder.mHardwareDecodingDisabled = true;
    }
#endif
    // We don't want to expose crash error so switch to
    // NS_ERROR_DOM_MEDIA_DECODE_ERR.
    if (decoder.mError.ref() ==
            NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_RDD_OR_GPU_ERR ||
        decoder.mError.ref() ==
            NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_UTILITY_ERR) {
      decoder.mError = Some(MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                                        RESULT_DETAIL("Unable to decode")));
    }
    if (!needsNewDecoder && ++decoder.mNumOfConsecutiveDecodingError >
                                decoder.mMaxConsecutiveDecodingError) {
      DDLOG(DDLogCategory::Log, "too_many_decode_errors", decoder.mError.ref());
      NotifyError(aTrack, decoder.mError.ref());
      return;
    }

    if (firstFrameDecodingFailedWithHardware) {
      decoder.mHardwareDecodingDisabled = true;
    }
    decoder.mError.reset();

    LOG("%s decoded error count %d RDD crashes count %d",
        TrackTypeToStr(aTrack), decoder.mNumOfConsecutiveDecodingError,
        decoder.mNumOfConsecutiveRDDOrGPUCrashes);

    if (needsNewDecoder) {
      LOG("Error: %s needs a new decoder", TrackTypeToStr(aTrack));
      ShutdownDecoder(aTrack);
    }
    if (decoder.mFirstFrameTime) {
      TimeInterval seekInterval = TimeInterval(decoder.mFirstFrameTime.ref(),
                                               decoder.mFirstFrameTime.ref());
      InternalSeek(aTrack, InternalSeekTarget(seekInterval, false));
      return;
    }

    TimeUnit nextKeyframe;
    if (aTrack == TrackType::kVideoTrack &&
        NS_SUCCEEDED(
            decoder.mTrackDemuxer->GetNextRandomAccessPoint(&nextKeyframe)) &&
        !nextKeyframe.IsInfinite()) {
      SkipVideoDemuxToNextKeyFrame(
          decoder.mLastDecodedSampleTime.refOr(TimeInterval()).Length());
    } else if (aTrack == TrackType::kAudioTrack) {
      decoder.Flush();
    } else {
      DDLOG(DDLogCategory::Log, "no_keyframe", NS_ERROR_DOM_MEDIA_FATAL_ERR);
      // We can't recover from this error.
      NotifyError(aTrack, NS_ERROR_DOM_MEDIA_FATAL_ERR);
    }
    return;
  }

  bool needInput = NeedInput(decoder);

  LOGV("Update(%s) ni=%d no=%d in:%" PRIu64 " out:%" PRIu64
       " qs=%u decoding:%d flushing:%d desc:%s pending:%u waiting:%d eos:%d "
       "ds:%d sid:%u waitcdm:%d",
       TrackTypeToStr(aTrack), needInput, needOutput, decoder.mNumSamplesInput,
       decoder.mNumSamplesOutput, uint32_t(size_t(decoder.mSizeOfQueue)),
       decoder.mDecodeRequest.Exists(), decoder.mFlushing,
       decoder.mDescription.get(), uint32_t(decoder.mOutput.Length()),
       !!decoder.mWaitingForDataStartTime, decoder.mDemuxEOS,
       int32_t(decoder.mDrainState), decoder.mLastStreamSourceID,
       IsDecoderWaitingForCDM(aTrack));

  if (IsWaitingOnCDMResource() || !ResolveSetCDMPromiseIfDone(aTrack)) {
    // If the content is encrypted, MFR won't start to create decoder until
    // CDMProxy is set.
    return;
  }

  if ((decoder.IsWaitingForData() &&
       // If mTimeThreshold is set then more samples might need to be demuxed
       // to advance from the random access point to the target, but if its
       // mWaiting is set then demuxing would continue to fail.
       (!decoder.mTimeThreshold || decoder.mTimeThreshold.ref().mWaiting)) ||
      (decoder.IsWaitingForKey())) {
    // Nothing more we can do at present.
    LOGV("Still waiting for data or key. data(%d)/key(%d)",
         !!decoder.mWaitingForDataStartTime, decoder.mWaitingForKey);
    return;
  }

  if (decoder.CancelWaitingForKey()) {
    LOGV("No longer waiting for key. Resolving waiting promise");
    return;
  }

  if (!needInput) {
    LOGV("No need for additional input (pending:%u)",
         uint32_t(decoder.mOutput.Length()));
    return;
  }

  // Demux samples if we don't have some.
  RequestDemuxSamples(aTrack);

  HandleDemuxedSamples(aTrack, a);
  // At least one of RequestDemuxSamples() or HandleDemuxedSamples() was a
  // no-op, so only one operation is in progress at one time.
  MOZ_ASSERT(!decoder.mDemuxRequest.Exists() ||
             !decoder.mDecodeRequest.Exists());
}

void MediaFormatReader::ReturnOutput(MediaData* aData, TrackType aTrack) {
  AUTO_PROFILER_LABEL("MediaFormatReader::ReturnOutput", MEDIA_PLAYBACK);
  MOZ_ASSERT(GetDecoderData(aTrack).HasPromise());
  MOZ_DIAGNOSTIC_ASSERT(aData->mType != MediaData::Type::NULL_DATA);
  LOG("Resolved data promise for %s [%" PRId64 ", %" PRId64 "]",
      TrackTypeToStr(aTrack), aData->mTime.ToMicroseconds(),
      aData->GetEndTime().ToMicroseconds());

  if (aTrack == TrackInfo::kAudioTrack) {
    AudioData* audioData = aData->As<AudioData>();

    if (audioData->mChannels != mInfo.mAudio.mChannels ||
        audioData->mRate != mInfo.mAudio.mRate) {
      LOG("change of audio format (rate:%d->%d). "
          "This is an unsupported configuration",
          mInfo.mAudio.mRate, audioData->mRate);
      mInfo.mAudio.mRate = audioData->mRate;
      mInfo.mAudio.mChannels = audioData->mChannels;
      MutexAutoLock lock(mAudio.mMutex);
      mAudio.mWorkingInfo->GetAsAudioInfo()->mRate = audioData->mRate;
      mAudio.mWorkingInfo->GetAsAudioInfo()->mChannels = audioData->mChannels;
      mWorkingInfoChanged = true;
    }
    mAudio.ResolvePromise(audioData, __func__);
  } else if (aTrack == TrackInfo::kVideoTrack) {
    VideoData* videoData = aData->As<VideoData>();

    if (videoData->mDisplay != mInfo.mVideo.mDisplay) {
      LOG("change of video display size (%dx%d->%dx%d)",
          mInfo.mVideo.mDisplay.width, mInfo.mVideo.mDisplay.height,
          videoData->mDisplay.width, videoData->mDisplay.height);
      mInfo.mVideo.mDisplay = videoData->mDisplay;
      MutexAutoLock lock(mVideo.mMutex);
      mVideo.mWorkingInfo->GetAsVideoInfo()->mDisplay = videoData->mDisplay;
      mWorkingInfoChanged = true;
    }

    mozilla::gfx::ColorDepth colorDepth = videoData->GetColorDepth();
    if (colorDepth != mInfo.mVideo.mColorDepth) {
      LOG("change of video color depth (enum %u -> enum %u)",
          (unsigned)mInfo.mVideo.mColorDepth, (unsigned)colorDepth);
      mInfo.mVideo.mColorDepth = colorDepth;
      MutexAutoLock lock(mVideo.mMutex);
      mVideo.mWorkingInfo->GetAsVideoInfo()->mColorDepth = colorDepth;
      mWorkingInfoChanged = true;
    }

    TimeUnit nextKeyframe;
    if (!mVideo.HasInternalSeekPending() &&
        NS_SUCCEEDED(
            mVideo.mTrackDemuxer->GetNextRandomAccessPoint(&nextKeyframe))) {
      videoData->SetNextKeyFrameTime(nextKeyframe);
    }

    mVideo.ResolvePromise(videoData, __func__);
  }
}

size_t MediaFormatReader::SizeOfVideoQueueInFrames() {
  return SizeOfQueue(TrackInfo::kVideoTrack);
}

size_t MediaFormatReader::SizeOfAudioQueueInFrames() {
  return SizeOfQueue(TrackInfo::kAudioTrack);
}

size_t MediaFormatReader::SizeOfQueue(TrackType aTrack) {
  auto& decoder = GetDecoderData(aTrack);
  return decoder.mSizeOfQueue;
}

RefPtr<MediaFormatReader::WaitForDataPromise> MediaFormatReader::WaitForData(
    MediaData::Type aType) {
  MOZ_ASSERT(OnTaskQueue());
  TrackType trackType = aType == MediaData::Type::VIDEO_DATA
                            ? TrackType::kVideoTrack
                            : TrackType::kAudioTrack;
  auto& decoder = GetDecoderData(trackType);
  if (!decoder.IsWaitingForData() && !decoder.IsWaitingForKey()) {
    // We aren't waiting for anything.
    return WaitForDataPromise::CreateAndResolve(decoder.mType, __func__);
  }
  RefPtr<WaitForDataPromise> p = decoder.mWaitingPromise.Ensure(__func__);
  ScheduleUpdate(trackType);
  return p;
}

nsresult MediaFormatReader::ResetDecode(const TrackSet& aTracks) {
  AUTO_PROFILER_LABEL("MediaFormatReader::ResetDecode", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOGV("");

  mSeekPromise.RejectIfExists(NS_OK, __func__);
  mSkipRequest.DisconnectIfExists();

  // Do the same for any data wait promises.
  if (aTracks.contains(TrackInfo::kAudioTrack)) {
    mAudio.mWaitingPromise.RejectIfExists(
        WaitForDataRejectValue(MediaData::Type::AUDIO_DATA,
                               WaitForDataRejectValue::CANCELED),
        __func__);
  }

  if (aTracks.contains(TrackInfo::kVideoTrack)) {
    mVideo.mWaitingPromise.RejectIfExists(
        WaitForDataRejectValue(MediaData::Type::VIDEO_DATA,
                               WaitForDataRejectValue::CANCELED),
        __func__);
  }

  // Reset miscellaneous seeking state.
  mPendingSeekTime.reset();

  if (HasVideo() && aTracks.contains(TrackInfo::kVideoTrack)) {
    mVideo.ResetDemuxer();
    mVideo.mFirstFrameTime = Some(media::TimeUnit::Zero());
    Reset(TrackInfo::kVideoTrack);
    if (mVideo.HasPromise()) {
      mVideo.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    }
  }

  if (HasAudio() && aTracks.contains(TrackInfo::kAudioTrack)) {
    mAudio.ResetDemuxer();
    mVideo.mFirstFrameTime = Some(media::TimeUnit::Zero());
    Reset(TrackInfo::kAudioTrack);
    if (mAudio.HasPromise()) {
      mAudio.RejectPromise(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
    }
  }

  return NS_OK;
}

void MediaFormatReader::Reset(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  LOG("Reset(%s) BEGIN", TrackTypeToStr(aTrack));

  auto& decoder = GetDecoderData(aTrack);

  decoder.ResetState();
  decoder.Flush();

  LOG("Reset(%s) END", TrackTypeToStr(aTrack));
}

void MediaFormatReader::DropDecodedSamples(TrackType aTrack) {
  MOZ_ASSERT(OnTaskQueue());
  auto& decoder = GetDecoderData(aTrack);
  size_t lengthDecodedQueue = decoder.mOutput.Length();
  if (lengthDecodedQueue && decoder.mTimeThreshold.isSome()) {
    auto time = decoder.mOutput.LastElement()->mTime;
    if (time >= decoder.mTimeThreshold.ref().Time()) {
      // We would have reached our internal seek target.
      decoder.mTimeThreshold.reset();
    }
  }
  decoder.mOutput.Clear();
  decoder.mSizeOfQueue -= lengthDecodedQueue;
  if (aTrack == TrackInfo::kVideoTrack && mFrameStats) {
    mFrameStats->Accumulate({0, 0, 0, lengthDecodedQueue, 0, 0});
  }
}

void MediaFormatReader::SkipVideoDemuxToNextKeyFrame(TimeUnit aTimeThreshold) {
  AUTO_PROFILER_LABEL("MediaFormatReader::SkipVideoDemuxToNextKeyFrame",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOG("Skipping up to %" PRId64, aTimeThreshold.ToMicroseconds());

  // We've reached SkipVideoDemuxToNextKeyFrame when our decoding is late.
  // As such we can drop all already decoded samples and discard all pending
  // samples.
  DropDecodedSamples(TrackInfo::kVideoTrack);

  mVideo.mTrackDemuxer->SkipToNextRandomAccessPoint(aTimeThreshold)
      ->Then(OwnerThread(), __func__, this,
             &MediaFormatReader::OnVideoSkipCompleted,
             &MediaFormatReader::OnVideoSkipFailed)
      ->Track(mSkipRequest);
}

void MediaFormatReader::VideoSkipReset(uint32_t aSkipped) {
  PROFILER_MARKER_UNTYPED("SkippedVideoDecode", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  // Some frames may have been output by the decoder since we initiated the
  // videoskip process and we know they would be late.
  DropDecodedSamples(TrackInfo::kVideoTrack);
  // Report the pending frames as dropped.
  if (mFrameStats) {
    uint32_t droppedDecoderCount = SizeOfVideoQueueInFrames();
    mFrameStats->Accumulate({0, 0, 0, droppedDecoderCount, 0, 0});
  }

  // Cancel any pending demux request and pending demuxed samples.
  mVideo.mDemuxRequest.DisconnectIfExists();
  Reset(TrackType::kVideoTrack);

  if (mFrameStats) {
    mFrameStats->Accumulate({aSkipped, 0, 0, aSkipped, 0, 0});
  }

  mVideo.mNumSamplesSkippedTotal += aSkipped;
}

void MediaFormatReader::OnVideoSkipCompleted(uint32_t aSkipped) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnVideoSkipCompleted",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOG("Skipping succeeded, skipped %u frames", aSkipped);
  mSkipRequest.Complete();

  DDLOG(DDLogCategory::Log, "video_skipped", DDNoValue());

  VideoSkipReset(aSkipped);

  ScheduleUpdate(TrackInfo::kVideoTrack);
}

void MediaFormatReader::OnVideoSkipFailed(
    MediaTrackDemuxer::SkipFailureHolder aFailure) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnVideoSkipFailed", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOG("Skipping failed, skipped %u frames", aFailure.mSkipped);
  mSkipRequest.Complete();

  switch (aFailure.mFailure.Code()) {
    case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
    case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
      DDLOG(DDLogCategory::Log, "video_skipping_interruption",
            aFailure.mFailure);
      // Some frames may have been output by the decoder since we initiated the
      // videoskip process and we know they would be late.
      DropDecodedSamples(TrackInfo::kVideoTrack);
      // We can't complete the skip operation, will just service a video frame
      // normally.
      ScheduleUpdate(TrackInfo::kVideoTrack);
      break;
    case NS_ERROR_DOM_MEDIA_CANCELED:
      DDLOG(DDLogCategory::Log, "video_skipping_interruption",
            aFailure.mFailure);
      if (mVideo.HasPromise()) {
        mVideo.RejectPromise(aFailure.mFailure, __func__);
      }
      break;
    default:
      DDLOG(DDLogCategory::Log, "video_skipping_error", aFailure.mFailure);
      NotifyError(TrackType::kVideoTrack, aFailure.mFailure);
      break;
  }
}

RefPtr<MediaFormatReader::SeekPromise> MediaFormatReader::Seek(
    const SeekTarget& aTarget) {
  AUTO_PROFILER_LABEL("MediaFormatReader::Seek", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  LOG("aTarget=(%" PRId64 "), track=%s", aTarget.GetTime().ToMicroseconds(),
      SeekTarget::EnumValueToString(aTarget.GetTrack()));

  MOZ_DIAGNOSTIC_ASSERT(mSeekPromise.IsEmpty());
  MOZ_DIAGNOSTIC_ASSERT(mPendingSeekTime.isNothing());
  // Should reset data request, and no pending internal seek.
  if (aTarget.IsAllTracks()) {
    MOZ_DIAGNOSTIC_ASSERT(!mVideo.HasPromise());
    MOZ_DIAGNOSTIC_ASSERT(!mAudio.HasPromise());
    MOZ_DIAGNOSTIC_ASSERT(mVideo.mTimeThreshold.isNothing());
    MOZ_DIAGNOSTIC_ASSERT(mAudio.mTimeThreshold.isNothing());
  } else if (aTarget.IsVideoOnly()) {
    MOZ_DIAGNOSTIC_ASSERT(!mVideo.HasPromise());
    MOZ_DIAGNOSTIC_ASSERT(mVideo.mTimeThreshold.isNothing());
  } else if (aTarget.IsAudioOnly()) {
    MOZ_DIAGNOSTIC_ASSERT(!mAudio.HasPromise());
    MOZ_DIAGNOSTIC_ASSERT(mAudio.mTimeThreshold.isNothing());
  }

  if (!mInfo.mMediaSeekable && !mInfo.mMediaSeekableOnlyInBufferedRanges) {
    LOG("Seek() END (Unseekable)");
    return SeekPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  if (mShutdown) {
    return SeekPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  SetSeekTarget(aTarget);

  RefPtr<SeekPromise> p = mSeekPromise.Ensure(__func__);

  ScheduleSeek();

  return p;
}

void MediaFormatReader::SetSeekTarget(const SeekTarget& aTarget) {
  MOZ_ASSERT(OnTaskQueue());

  mOriginalSeekTarget = aTarget;
  mFallbackSeekTime = mPendingSeekTime = Some(aTarget.GetTime());
}

void MediaFormatReader::ScheduleSeek() {
  if (mSeekScheduled) {
    return;
  }
  mSeekScheduled = true;
  nsresult rv = OwnerThread()->Dispatch(NewRunnableMethod(
      "MediaFormatReader::AttemptSeek", this, &MediaFormatReader::AttemptSeek));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

void MediaFormatReader::AttemptSeek() {
  AUTO_PROFILER_LABEL("MediaFormatReader::AttemptSeek", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  mSeekScheduled = false;

  if (mPendingSeekTime.isNothing()) {
    LOGV("AttemptSeek, no pending seek time?");
    return;
  }

  // Only reset the demuxers targeted by this SeekTarget, to avoid A/V sync
  // issues.
  const bool isSeekingAudio = HasAudio() && !mOriginalSeekTarget.IsVideoOnly();
  const bool isSeekingVideo = HasVideo() && !mOriginalSeekTarget.IsAudioOnly();
  LOG("AttemptSeek, seekingAudio=%d, seekingVideo=%d", isSeekingAudio,
      isSeekingVideo);
  if (isSeekingVideo) {
    mVideo.ResetDemuxer();
    mVideo.ResetState();
  }
  if (isSeekingAudio) {
    mAudio.ResetDemuxer();
    mAudio.ResetState();
  }

  // If seeking both tracks, seek the video track, and then the audio track when
  // the video track seek has completed. Otherwise, only seek a specific track.
  if (isSeekingVideo) {
    DoVideoSeek();
  } else if (isSeekingAudio) {
    DoAudioSeek();
  } else {
    MOZ_CRASH();
  }
}

void MediaFormatReader::OnSeekFailed(TrackType aTrack,
                                     const MediaResult& aError) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnSeekFailed", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOGV("%s failure:%s", TrackTypeToStr(aTrack), aError.ErrorName().get());
  if (aTrack == TrackType::kVideoTrack) {
    mVideo.mSeekRequest.Complete();
  } else {
    mAudio.mSeekRequest.Complete();
  }

  if (aError == NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
    if (HasVideo() && aTrack == TrackType::kAudioTrack &&
        mFallbackSeekTime.isSome() &&
        mPendingSeekTime.ref() != mFallbackSeekTime.ref()) {
      // We have failed to seek audio where video seeked to earlier.
      // Attempt to seek instead to the closest point that we know we have in
      // order to limit A/V sync discrepency.

      // Ensure we have the most up to date buffered ranges.
      UpdateReceivedNewData(TrackType::kAudioTrack);
      Maybe<TimeUnit> nextSeekTime;
      // Find closest buffered time found after video seeked time.
      for (const auto& timeRange : mAudio.mTimeRanges) {
        if (timeRange.mStart >= mPendingSeekTime.ref()) {
          nextSeekTime.emplace(timeRange.mStart);
          break;
        }
      }
      if (nextSeekTime.isNothing() ||
          nextSeekTime.ref() > mFallbackSeekTime.ref()) {
        nextSeekTime = Some(mFallbackSeekTime.ref());
        LOG("Unable to seek audio to video seek time. A/V sync may be broken");
      } else {
        mFallbackSeekTime.reset();
      }
      mPendingSeekTime = nextSeekTime;
      DoAudioSeek();
      return;
    }
    NotifyWaitingForData(aTrack);
  }
  MOZ_ASSERT(!mVideo.mSeekRequest.Exists() && !mAudio.mSeekRequest.Exists());
  mPendingSeekTime.reset();

  auto type = aTrack == TrackType::kAudioTrack ? MediaData::Type::AUDIO_DATA
                                               : MediaData::Type::VIDEO_DATA;
  mSeekPromise.RejectIfExists(SeekRejectValue(type, aError), __func__);
}

void MediaFormatReader::DoVideoSeek() {
  AUTO_PROFILER_LABEL("MediaFormatReader::DoVideoSeek", MEDIA_PLAYBACK);
  MOZ_ASSERT(mPendingSeekTime.isSome());
  LOGV("Seeking video to %" PRId64, mPendingSeekTime.ref().ToMicroseconds());
  MOZ_DIAGNOSTIC_ASSERT(!IsAudioOnlySeeking());
  MOZ_DIAGNOSTIC_ASSERT(!mVideo.mSeekRequest.Exists());
  auto seekTime = mPendingSeekTime.ref();
  mVideo.mTrackDemuxer->Seek(seekTime)
      ->Then(OwnerThread(), __func__, this,
             &MediaFormatReader::OnVideoSeekCompleted,
             &MediaFormatReader::OnVideoSeekFailed)
      ->Track(mVideo.mSeekRequest);
}

void MediaFormatReader::OnVideoSeekCompleted(TimeUnit aTime) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnVideoSeekCompleted",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOGV("Video seeked to %" PRId64, aTime.ToMicroseconds());
  mVideo.mSeekRequest.Complete();

  mVideo.mFirstFrameTime = Some(aTime);
  mPreviousDecodedKeyframeTime_us = sNoPreviousDecodedKeyframe;

  SetVideoDecodeThreshold();

  if (HasAudio() && !mOriginalSeekTarget.IsVideoOnly()) {
    MOZ_ASSERT(mPendingSeekTime.isSome());
    if (mOriginalSeekTarget.IsFast()) {
      // We are performing a fast seek. We need to seek audio to where the
      // video seeked to, to ensure proper A/V sync once playback resume.
      mPendingSeekTime = Some(aTime);
    }
    DoAudioSeek();
  } else {
    mPendingSeekTime.reset();
    mSeekPromise.ResolveIfExists(aTime, __func__);
  }
}

void MediaFormatReader::OnVideoSeekFailed(const MediaResult& aError) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnVideoSeekFailed", MEDIA_PLAYBACK);
  mPreviousDecodedKeyframeTime_us = sNoPreviousDecodedKeyframe;
  OnSeekFailed(TrackType::kVideoTrack, aError);
}

void MediaFormatReader::SetVideoDecodeThreshold() {
  MOZ_ASSERT(OnTaskQueue());

  if (!HasVideo() || !mVideo.mDecoder) {
    return;
  }

  if (!mVideo.mTimeThreshold && !IsSeeking()) {
    return;
  }

  TimeUnit threshold;
  if (mVideo.mTimeThreshold) {
    // For internalSeek.
    threshold = mVideo.mTimeThreshold.ref().Time();
  } else if (IsSeeking()) {
    // If IsSeeking() is true, then video seek must have completed already.
    TimeUnit keyframe;
    if (NS_FAILED(mVideo.mTrackDemuxer->GetNextRandomAccessPoint(&keyframe))) {
      return;
    }

    // If the key frame is invalid/infinite, it means the target position is
    // closing to end of stream. We don't want to skip any frame at this point.
    threshold = keyframe.IsValid() && !keyframe.IsInfinite()
                    ? mOriginalSeekTarget.GetTime()
                    : TimeUnit::Invalid();
  } else {
    return;
  }

  if (threshold.IsValid()) {
    LOG("Set seek threshold to %" PRId64, threshold.ToMicroseconds());
  } else {
    LOG("Resetting seek threshold");
  }
  mVideo.mDecoder->SetSeekThreshold(threshold);
}

void MediaFormatReader::DoAudioSeek() {
  AUTO_PROFILER_LABEL("MediaFormatReader::DoAudioSeek", MEDIA_PLAYBACK);
  MOZ_ASSERT(mPendingSeekTime.isSome());
  LOGV("Seeking audio to %" PRId64, mPendingSeekTime.ref().ToMicroseconds());
  MOZ_DIAGNOSTIC_ASSERT(!IsVideoOnlySeeking());
  MOZ_DIAGNOSTIC_ASSERT(!mAudio.mSeekRequest.Exists());
  auto seekTime = mPendingSeekTime.ref();
  mAudio.mTrackDemuxer->Seek(seekTime)
      ->Then(OwnerThread(), __func__, this,
             &MediaFormatReader::OnAudioSeekCompleted,
             &MediaFormatReader::OnAudioSeekFailed)
      ->Track(mAudio.mSeekRequest);
}

void MediaFormatReader::OnAudioSeekCompleted(TimeUnit aTime) {
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("MediaFormatReader::OnAudioSeekCompleted",
                      MEDIA_PLAYBACK);
  LOGV("Audio seeked to %" PRId64, aTime.ToMicroseconds());
  mAudio.mSeekRequest.Complete();
  mAudio.mFirstFrameTime = Some(aTime);
  mPendingSeekTime.reset();
  mSeekPromise.ResolveIfExists(aTime, __func__);
}

void MediaFormatReader::OnAudioSeekFailed(const MediaResult& aError) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnAudioSeekFailed", MEDIA_PLAYBACK);
  OnSeekFailed(TrackType::kAudioTrack, aError);
}

void MediaFormatReader::ReleaseResources() {
  LOGV("");
  if (mShutdown) {
    return;
  }
  ShutdownDecoder(TrackInfo::kAudioTrack);
  ShutdownDecoder(TrackInfo::kVideoTrack);
}

bool MediaFormatReader::VideoIsHardwareAccelerated() const {
  return mVideo.mIsHardwareAccelerated;
}

void MediaFormatReader::NotifyTrackDemuxers() {
  MOZ_ASSERT(OnTaskQueue());

  LOGV("");

  if (!mInitDone) {
    return;
  }

  if (HasVideo()) {
    mVideo.mReceivedNewData = true;
    ScheduleUpdate(TrackType::kVideoTrack);
  }
  if (HasAudio()) {
    mAudio.mReceivedNewData = true;
    ScheduleUpdate(TrackType::kAudioTrack);
  }
}

void MediaFormatReader::NotifyDataArrived() {
  AUTO_PROFILER_LABEL("MediaFormatReader::NotifyDataArrived", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown || !mDemuxer || !mDemuxerInitDone) {
    return;
  }

  if (mNotifyDataArrivedPromise.Exists()) {
    // Already one in progress. Set the dirty flag so we can process it later.
    mPendingNotifyDataArrived = true;
    return;
  }

  RefPtr<MediaFormatReader> self = this;
  mDemuxer->NotifyDataArrived()
      ->Then(
          OwnerThread(), __func__,
          [self]() {
            AUTO_PROFILER_LABEL("MediaFormatReader::NotifyDataArrived:Resolved",
                                MEDIA_PLAYBACK);
            self->mNotifyDataArrivedPromise.Complete();
            self->UpdateBuffered();
            self->NotifyTrackDemuxers();
            if (self->mPendingNotifyDataArrived) {
              self->mPendingNotifyDataArrived = false;
              self->NotifyDataArrived();
            }
          },
          [self]() { self->mNotifyDataArrivedPromise.Complete(); })
      ->Track(mNotifyDataArrivedPromise);
}

void MediaFormatReader::UpdateMediaEngineId(uint64_t aMediaEngineId) {
  LOG("Update external media engine Id %" PRIu64, aMediaEngineId);
  mMediaEngineId = Some(aMediaEngineId);
}

void MediaFormatReader::UpdateBuffered() {
  AUTO_PROFILER_LABEL("MediaFormatReader::UpdateBuffered", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  if (!mInitDone || !mHasStartTime) {
    mBuffered = TimeIntervals();
    return;
  }

  if (HasVideo()) {
    mVideo.mTimeRanges = mVideo.mTrackDemuxer->GetBuffered();
    bool hasLastEnd;
    auto lastEnd = mVideo.mTimeRanges.GetEnd(&hasLastEnd);
    if (hasLastEnd) {
      if (mVideo.mLastTimeRangesEnd &&
          mVideo.mLastTimeRangesEnd.ref() < lastEnd) {
        // New data was added after our previous end, we can clear the EOS flag.
        mVideo.mDemuxEOS = false;
        ScheduleUpdate(TrackInfo::kVideoTrack);
      }
      mVideo.mLastTimeRangesEnd = Some(lastEnd);
    }
  }
  if (HasAudio()) {
    mAudio.mTimeRanges = mAudio.mTrackDemuxer->GetBuffered();
    bool hasLastEnd;
    auto lastEnd = mAudio.mTimeRanges.GetEnd(&hasLastEnd);
    if (hasLastEnd) {
      if (mAudio.mLastTimeRangesEnd &&
          mAudio.mLastTimeRangesEnd.ref() < lastEnd) {
        // New data was added after our previous end, we can clear the EOS flag.
        mAudio.mDemuxEOS = false;
        ScheduleUpdate(TrackInfo::kAudioTrack);
      }
      mAudio.mLastTimeRangesEnd = Some(lastEnd);
    }
  }

  media::TimeIntervals intervals;
  if (HasAudio() && HasVideo()) {
    intervals = media::Intersection(mVideo.mTimeRanges, mAudio.mTimeRanges);
  } else if (HasAudio()) {
    intervals = mAudio.mTimeRanges;
  } else if (HasVideo()) {
    intervals = mVideo.mTimeRanges;
  }

  if (intervals.IsEmpty() || intervals.GetStart() == TimeUnit::Zero()) {
    // IntervalSet already starts at 0 or is empty, nothing to shift.
    mBuffered = intervals;
  } else {
    LOG("Subtract start time for buffered range, startTime=%" PRId64,
        mInfo.mStartTime.ToMicroseconds());
    mBuffered = intervals.Shift(TimeUnit::Zero() - mInfo.mStartTime);
  }
}

layers::ImageContainer* MediaFormatReader::GetImageContainer() {
  return mVideoFrameContainer ? mVideoFrameContainer->GetImageContainer()
                              : nullptr;
}

RefPtr<GenericPromise> MediaFormatReader::RequestDebugInfo(
    dom::MediaFormatReaderDebugInfo& aInfo) {
  if (!OnTaskQueue()) {
    // Run the request on the task queue if it's not already.
    return InvokeAsync(mTaskQueue, __func__,
                       [this, self = RefPtr{this}, &aInfo] {
                         return RequestDebugInfo(aInfo);
                       });
  }
  GetDebugInfo(aInfo);
  return GenericPromise::CreateAndResolve(true, __func__);
}

void MediaFormatReader::GetDebugInfo(dom::MediaFormatReaderDebugInfo& aInfo) {
  MOZ_ASSERT(OnTaskQueue(),
             "Don't call this off the task queue, it's going to touch a lot of "
             "data members");
  nsCString result;
  nsAutoCString audioDecoderName("unavailable");
  nsAutoCString videoDecoderName = audioDecoderName;
  nsAutoCString audioType("none");
  nsAutoCString videoType("none");

  AudioInfo audioInfo;
  if (HasAudio()) {
    audioInfo = *mAudio.GetWorkingInfo()->GetAsAudioInfo();
    audioDecoderName = mAudio.mDecoder ? mAudio.mDecoder->GetDescriptionName()
                                       : mAudio.mDescription;
    audioType = audioInfo.mMimeType;
    aInfo.mAudioState.mNeedInput = NeedInput(mAudio);
    aInfo.mAudioState.mHasPromise = mAudio.HasPromise();
    aInfo.mAudioState.mWaitingPromise = !mAudio.mWaitingPromise.IsEmpty();
    aInfo.mAudioState.mHasDemuxRequest = mAudio.mDemuxRequest.Exists();
    aInfo.mAudioState.mDemuxQueueSize = mAudio.mQueuedSamples.Length();
    aInfo.mAudioState.mHasDecoder = mAudio.mDecodeRequest.Exists();
    aInfo.mAudioState.mTimeTreshold =
        mAudio.mTimeThreshold ? mAudio.mTimeThreshold.ref().Time().ToSeconds()
                              : -1.0;
    aInfo.mAudioState.mTimeTresholdHasSeeked =
        mAudio.mTimeThreshold ? mAudio.mTimeThreshold.ref().mHasSeeked : false;
    aInfo.mAudioState.mNumSamplesInput = mAudio.mNumSamplesInput;
    aInfo.mAudioState.mNumSamplesOutput = mAudio.mNumSamplesOutput;
    aInfo.mAudioState.mQueueSize = mAudio.mSizeOfQueue;
    aInfo.mAudioState.mPending = mAudio.mOutput.Length();
    aInfo.mAudioState.mWaitingForData = !!mAudio.mWaitingForDataStartTime;
    aInfo.mAudioState.mDemuxEOS = mAudio.mDemuxEOS;
    aInfo.mAudioState.mDrainState = int32_t(mAudio.mDrainState);
    aInfo.mAudioState.mWaitingForKey = mAudio.mWaitingForKey;
    aInfo.mAudioState.mLastStreamSourceID = mAudio.mLastStreamSourceID;
  }

  CopyUTF8toUTF16(audioDecoderName, aInfo.mAudioDecoderName);
  CopyUTF8toUTF16(audioType, aInfo.mAudioType);
  aInfo.mAudioChannels = audioInfo.mChannels;
  aInfo.mAudioRate = audioInfo.mRate;
  aInfo.mAudioFramesDecoded = mAudio.mNumSamplesOutputTotal;

  VideoInfo videoInfo;
  if (HasVideo()) {
    videoInfo = *mVideo.GetWorkingInfo()->GetAsVideoInfo();
    videoDecoderName = mVideo.mDecoder ? mVideo.mDecoder->GetDescriptionName()
                                       : mVideo.mDescription;
    videoType = videoInfo.mMimeType;
    aInfo.mVideoState.mNeedInput = NeedInput(mVideo);
    aInfo.mVideoState.mHasPromise = mVideo.HasPromise();
    aInfo.mVideoState.mWaitingPromise = !mVideo.mWaitingPromise.IsEmpty();
    aInfo.mVideoState.mHasDemuxRequest = mVideo.mDemuxRequest.Exists();
    aInfo.mVideoState.mDemuxQueueSize = mVideo.mQueuedSamples.Length();
    aInfo.mVideoState.mHasDecoder = mVideo.mDecodeRequest.Exists();
    aInfo.mVideoState.mTimeTreshold =
        mVideo.mTimeThreshold ? mVideo.mTimeThreshold.ref().Time().ToSeconds()
                              : -1.0;
    aInfo.mVideoState.mTimeTresholdHasSeeked =
        mVideo.mTimeThreshold ? mVideo.mTimeThreshold.ref().mHasSeeked : false;
    aInfo.mVideoState.mNumSamplesInput = mVideo.mNumSamplesInput;
    aInfo.mVideoState.mNumSamplesOutput = mVideo.mNumSamplesOutput;
    aInfo.mVideoState.mQueueSize = mVideo.mSizeOfQueue;
    aInfo.mVideoState.mPending = mVideo.mOutput.Length();
    aInfo.mVideoState.mWaitingForData = !!mVideo.mWaitingForDataStartTime;
    aInfo.mVideoState.mDemuxEOS = mVideo.mDemuxEOS;
    aInfo.mVideoState.mDrainState = int32_t(mVideo.mDrainState);
    aInfo.mVideoState.mWaitingForKey = mVideo.mWaitingForKey;
    aInfo.mVideoState.mLastStreamSourceID = mVideo.mLastStreamSourceID;
    aInfo.mTotalReadMetadataTimeMs = mReadMetaDataTime.ToMilliseconds();
    aInfo.mTotalWaitingForVideoDataTimeMs =
        mTotalWaitingForVideoDataTime.ToMilliseconds();
  }

  CopyUTF8toUTF16(videoDecoderName, aInfo.mVideoDecoderName);
  CopyUTF8toUTF16(videoType, aInfo.mVideoType);
  aInfo.mVideoWidth =
      videoInfo.mDisplay.width < 0 ? 0 : videoInfo.mDisplay.width;
  aInfo.mVideoHeight =
      videoInfo.mDisplay.height < 0 ? 0 : videoInfo.mDisplay.height;
  aInfo.mVideoRate = mVideo.mMeanRate.Mean();
  aInfo.mVideoHardwareAccelerated = VideoIsHardwareAccelerated();
  aInfo.mVideoNumSamplesOutputTotal = mVideo.mNumSamplesOutputTotal;
  aInfo.mVideoNumSamplesSkippedTotal = mVideo.mNumSamplesSkippedTotal;

  // Looking at dropped frames
  FrameStatisticsData stats = mFrameStats->GetFrameStatisticsData();
  aInfo.mFrameStats.mDroppedDecodedFrames = stats.mDroppedDecodedFrames;
  aInfo.mFrameStats.mDroppedSinkFrames = stats.mDroppedSinkFrames;
  aInfo.mFrameStats.mDroppedCompositorFrames = stats.mDroppedCompositorFrames;
}

void MediaFormatReader::SetVideoNullDecode(bool aIsNullDecode) {
  MOZ_ASSERT(OnTaskQueue());
  return SetNullDecode(TrackType::kVideoTrack, aIsNullDecode);
}

void MediaFormatReader::UpdateCompositor(
    already_AddRefed<layers::KnowsCompositor> aCompositor) {
  MOZ_ASSERT(OnTaskQueue());
  mKnowsCompositor = aCompositor;
}

void MediaFormatReader::SetNullDecode(TrackType aTrack, bool aIsNullDecode) {
  MOZ_ASSERT(OnTaskQueue());

  auto& decoder = GetDecoderData(aTrack);
  if (decoder.mIsNullDecode == aIsNullDecode) {
    return;
  }

  LOG("%s, decoder.mIsNullDecode = %d => aIsNullDecode = %d",
      TrackTypeToStr(aTrack), decoder.mIsNullDecode, aIsNullDecode);

  decoder.mIsNullDecode = aIsNullDecode;
  ShutdownDecoder(aTrack);
}

void MediaFormatReader::OnFirstDemuxCompleted(
    TrackInfo::TrackType aType,
    const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) {
  AUTO_PROFILER_LABEL("MediaFormatReader::OnFirstDemuxCompleted",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  auto& decoder = GetDecoderData(aType);
  MOZ_ASSERT(decoder.mFirstDemuxedSampleTime.isNothing());
  decoder.mFirstDemuxedSampleTime.emplace(aSamples->GetSamples()[0]->mTime);
  MaybeResolveMetadataPromise();
}

void MediaFormatReader::OnFirstDemuxFailed(TrackInfo::TrackType aType,
                                           const MediaResult& aError) {
  MOZ_ASSERT(OnTaskQueue());

  if (mShutdown) {
    return;
  }

  auto& decoder = GetDecoderData(aType);
  MOZ_ASSERT(decoder.mFirstDemuxedSampleTime.isNothing());
  decoder.mFirstDemuxedSampleTime.emplace(TimeUnit::FromInfinity());
  MaybeResolveMetadataPromise();
}

void MediaFormatReader::SetEncryptedCustomIdent() {
  LOG("Set mEncryptedCustomIdent");
  mEncryptedCustomIdent = true;
}

}  // namespace mozilla

#undef NS_DispatchToMainThread
#undef LOGV
#undef LOG
