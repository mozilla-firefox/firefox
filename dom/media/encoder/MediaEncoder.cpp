/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEncoder.h"

#include <algorithm>

#include "AudioNodeEngine.h"
#include "AudioNodeTrack.h"
#include "DriftCompensation.h"
#include "MediaDecoder.h"
#include "MediaTrackGraph.h"
#include "MediaTrackListener.h"
#include "Muxer.h"
#include "OggWriter.h"
#include "OpusTrackEncoder.h"
#include "TimeUnits.h"
#include "Tracing.h"
#include "VP8TrackEncoder.h"
#include "WebMWriter.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/AudioNode.h"
#include "mozilla/dom/AudioStreamTrack.h"
#include "mozilla/dom/Blob.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/MediaStreamTrack.h"
#include "mozilla/dom/MutableBlobStorage.h"
#include "mozilla/dom/VideoStreamTrack.h"
#include "mozilla/gfx/Point.h"  // IntSize
#include "nsMimeTypes.h"
#include "nsThreadUtils.h"

mozilla::LazyLogModule gMediaEncoderLog("MediaEncoder");
#define LOG(type, msg) \
  MOZ_LOG_FMT(gMediaEncoderLog, type, MOZ_LOG_EXPAND_ARGS msg)

namespace mozilla {

using namespace dom;
using namespace media;

namespace {
class BlobStorer : public MutableBlobStorageCallback {
  MozPromiseHolder<MediaEncoder::BlobPromise> mHolder;

  virtual ~BlobStorer() = default;

 public:
  BlobStorer() = default;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(BlobStorer, override)

  void BlobStoreCompleted(MutableBlobStorage*, BlobImpl* aBlobImpl,
                          nsresult aRv) override {
    MOZ_ASSERT(NS_IsMainThread());
    if (NS_FAILED(aRv)) {
      mHolder.Reject(aRv, __func__);
      return;
    }

    mHolder.Resolve(aBlobImpl, __func__);
  }

  RefPtr<MediaEncoder::BlobPromise> Promise() {
    return mHolder.Ensure(__func__);
  }
};
}  // namespace

class MediaEncoder::AudioTrackListener : public MediaTrackListener {
  // Private so that a listener and its encoder strong reference cycle are not
  // created without registering for NotifyRemoved() to break the cycle and
  // resolve mShutdownPromise.
  AudioTrackListener(RefPtr<DriftCompensator> aDriftCompensator,
                     RefPtr<MediaEncoder> aMediaEncoder)
      : mInitialized(false),
        mDriftCompensator(std::move(aDriftCompensator)),
        mMediaEncoder(std::move(aMediaEncoder)),
        mEncoderThread(mMediaEncoder->mEncoderThread),
        mShutdownPromise(mShutdownHolder.Ensure(__func__)) {
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mMediaEncoder->mAudioEncoder);
    MOZ_ASSERT(mEncoderThread);
  }

 public:
  template <typename TRACK>
  static RefPtr<AudioTrackListener> Create(DriftCompensator* aDriftCompensator,
                                           MediaEncoder* aMediaEncoder,
                                           TRACK* aTrack) {
    RefPtr listener = new AudioTrackListener(aDriftCompensator, aMediaEncoder);
    aTrack->AddListener(listener);
    return listener;
  }

  void NotifyQueuedChanges(MediaTrackGraph* aGraph, TrackTime aTrackOffset,
                           const MediaSegment& aQueuedMedia) override {
    TRACE_COMMENT("MediaEncoder::NotifyQueuedChanges", "%p",
                  mMediaEncoder.get());
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mEncoderThread);

    if (!mInitialized) {
      mDriftCompensator->NotifyAudioStart(TimeStamp::Now());
      mInitialized = true;
    }

    mDriftCompensator->NotifyAudio(aQueuedMedia.GetDuration());

    const AudioSegment& audio = static_cast<const AudioSegment&>(aQueuedMedia);

    AudioSegment copy;
    copy.AppendSlice(audio, 0, audio.GetDuration());

    nsresult rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
        "mozilla::AudioTrackEncoder::AppendAudioSegment",
        [encoder = mMediaEncoder, copy = std::move(copy)]() mutable {
          encoder->mAudioEncoder->AppendAudioSegment(std::move(copy));
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

  void NotifyEnded(MediaTrackGraph* aGraph) override {
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mMediaEncoder->mAudioEncoder);
    MOZ_ASSERT(mEncoderThread);

    nsresult rv = mEncoderThread->Dispatch(
        NS_NewRunnableFunction("mozilla::AudioTrackEncoder::NotifyEndOfStream",
                               [encoder = mMediaEncoder] {
                                 encoder->mAudioEncoder->NotifyEndOfStream();
                               }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

  void NotifyRemoved(MediaTrackGraph* aGraph) override {
    nsresult rv = mEncoderThread->Dispatch(
        NS_NewRunnableFunction("mozilla::AudioTrackEncoder::NotifyEndOfStream",
                               [encoder = mMediaEncoder] {
                                 encoder->mAudioEncoder->NotifyEndOfStream();
                               }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;

    mMediaEncoder = nullptr;
    mEncoderThread = nullptr;
    mShutdownHolder.Resolve(true, __func__);
  }

  const RefPtr<GenericNonExclusivePromise>& OnShutdown() const {
    return mShutdownPromise;
  }

 private:
  bool mInitialized;
  const RefPtr<DriftCompensator> mDriftCompensator;
  RefPtr<MediaEncoder> mMediaEncoder;
  RefPtr<TaskQueue> mEncoderThread;
  MozPromiseHolder<GenericNonExclusivePromise> mShutdownHolder;
  const RefPtr<GenericNonExclusivePromise> mShutdownPromise;
};

class MediaEncoder::VideoTrackListener : public DirectMediaTrackListener {
  explicit VideoTrackListener(RefPtr<MediaEncoder> aMediaEncoder)
      : mDirectConnected(false),
        mInitialized(false),
        mRemoved(false),
        mPendingAdvanceCurrentTime(false),
        mMediaEncoder(std::move(aMediaEncoder)),
        mEncoderThread(mMediaEncoder->mEncoderThread),
        mShutdownPromise(mShutdownHolder.Ensure(__func__)) {
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mEncoderThread);
  }

 public:
  static RefPtr<VideoTrackListener> Create(MediaEncoder* aMediaEncoder,
                                           VideoStreamTrack* aTrack) {
    RefPtr listener = new VideoTrackListener(aMediaEncoder);
    aTrack->AddDirectListener(listener);
    aTrack->AddListener(listener);
    return listener;
  }

  void NotifyDirectListenerInstalled(InstallationResult aResult) override {
    if (aResult == InstallationResult::SUCCESS) {
      LOG(LogLevel::Info, ("Video track direct listener installed"));
      mDirectConnected = true;
    } else {
      LOG(LogLevel::Info, ("Video track failed to install direct listener"));
      MOZ_ASSERT(!mDirectConnected);
      return;
    }
  }

  void NotifyDirectListenerUninstalled() override {
    mDirectConnected = false;

    if (mRemoved) {
      mMediaEncoder = nullptr;
      mEncoderThread = nullptr;
    }
  }

  void NotifyQueuedChanges(MediaTrackGraph* aGraph, TrackTime aTrackOffset,
                           const MediaSegment& aQueuedMedia) override {
    TRACE_COMMENT("MediaEncoder::NotifyQueuedChanges", "%p",
                  mMediaEncoder.get());
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mMediaEncoder->mVideoEncoder);
    MOZ_ASSERT(mEncoderThread);

    mCurrentTime = TimeStamp::Now();
    if (!mInitialized) {
      nsresult rv = mEncoderThread->Dispatch(
          NS_NewRunnableFunction("mozilla::VideoTrackEncoder::SetStartOffset",
                                 [encoder = mMediaEncoder, now = mCurrentTime] {
                                   encoder->mVideoEncoder->SetStartOffset(now);
                                 }));
      MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
      (void)rv;
      mInitialized = true;
    }

    if (!mPendingAdvanceCurrentTime) {
      mPendingAdvanceCurrentTime = true;
      nsresult rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
          "mozilla::VideoTrackEncoder::AdvanceCurrentTime",
          [encoder = mMediaEncoder, now = mCurrentTime] {
            encoder->mVideoListener->mPendingAdvanceCurrentTime = false;
            encoder->mVideoEncoder->AdvanceCurrentTime(now);
          }));
      MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
      (void)rv;
    }
  }

  void NotifyRealtimeTrackData(MediaTrackGraph* aGraph, TrackTime aTrackOffset,
                               const MediaSegment& aMedia) override {
    TRACE_COMMENT("MediaEncoder::NotifyRealtimeTrackData", "%p",
                  mMediaEncoder.get());
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mMediaEncoder->mVideoEncoder);
    MOZ_ASSERT(mEncoderThread);
    MOZ_ASSERT(aMedia.GetType() == MediaSegment::VIDEO);

    const VideoSegment& video = static_cast<const VideoSegment&>(aMedia);
    VideoSegment copy;
    for (VideoSegment::ConstChunkIterator iter(video); !iter.IsEnded();
         iter.Next()) {
      copy.AppendFrame(do_AddRef(iter->mFrame.GetImage()),
                       iter->mFrame.GetIntrinsicSize(),
                       iter->mFrame.GetPrincipalHandle(),
                       iter->mFrame.GetForceBlack(), iter->mTimeStamp);
    }

    nsresult rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
        "mozilla::VideoTrackEncoder::AppendVideoSegment",
        [encoder = mMediaEncoder, copy = std::move(copy)]() mutable {
          encoder->mVideoEncoder->AppendVideoSegment(std::move(copy));
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

  void NotifyEnabledStateChanged(MediaTrackGraph* aGraph,
                                 bool aEnabled) override {
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mMediaEncoder->mVideoEncoder);
    MOZ_ASSERT(mEncoderThread);

    nsresult rv;
    if (aEnabled) {
      rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
          "mozilla::VideoTrackEncoder::Enable",
          [encoder = mMediaEncoder, now = TimeStamp::Now()] {
            encoder->mVideoEncoder->Enable(now);
          }));
    } else {
      rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
          "mozilla::VideoTrackEncoder::Disable",
          [encoder = mMediaEncoder, now = TimeStamp::Now()] {
            encoder->mVideoEncoder->Disable(now);
          }));
    }
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

  void NotifyEnded(MediaTrackGraph* aGraph) override {
    MOZ_ASSERT(mMediaEncoder);
    MOZ_ASSERT(mMediaEncoder->mVideoEncoder);
    MOZ_ASSERT(mEncoderThread);

    nsresult rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
        "mozilla::VideoTrackEncoder::NotifyEndOfStream",
        [encoder = mMediaEncoder, now = mCurrentTime] {
          if (!now.IsNull()) {
            encoder->mVideoEncoder->AdvanceCurrentTime(now);
          }
          encoder->mVideoEncoder->NotifyEndOfStream();
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

  void NotifyRemoved(MediaTrackGraph* aGraph) override {
    nsresult rv = mEncoderThread->Dispatch(NS_NewRunnableFunction(
        "mozilla::VideoTrackEncoder::NotifyEndOfStream",
        [encoder = mMediaEncoder, now = mCurrentTime] {
          if (!now.IsNull()) {
            encoder->mVideoEncoder->AdvanceCurrentTime(now);
          }
          encoder->mVideoEncoder->NotifyEndOfStream();
        }));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;

    mRemoved = true;

    if (!mDirectConnected) {
      mMediaEncoder = nullptr;
      mEncoderThread = nullptr;
    }

    mShutdownHolder.Resolve(true, __func__);
  }

  const RefPtr<GenericNonExclusivePromise>& OnShutdown() const {
    return mShutdownPromise;
  }

 private:
  bool mDirectConnected;
  bool mInitialized;
  bool mRemoved;
  TimeStamp mCurrentTime;
  Atomic<bool> mPendingAdvanceCurrentTime;
  RefPtr<MediaEncoder> mMediaEncoder;
  RefPtr<TaskQueue> mEncoderThread;
  MozPromiseHolder<GenericNonExclusivePromise> mShutdownHolder;
  const RefPtr<GenericNonExclusivePromise> mShutdownPromise;
};

class MediaEncoder::EncoderListener : public TrackEncoderListener {
 public:
  EncoderListener(TaskQueue* aEncoderThread, MediaEncoder* aEncoder)
      : mEncoderThread(aEncoderThread), mEncoder(aEncoder) {}

  void Forget() {
    MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
    mEncoder = nullptr;
  }

  void Initialized(TrackEncoder* aTrackEncoder) override {
    MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
    MOZ_ASSERT(aTrackEncoder->IsInitialized());

    if (!mEncoder) {
      return;
    }

    mEncoder->UpdateInitialized();
  }

  void Started(TrackEncoder* aTrackEncoder) override {
    MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
    MOZ_ASSERT(aTrackEncoder->IsStarted());

    if (!mEncoder) {
      return;
    }

    mEncoder->UpdateStarted();
  }

  void Error(TrackEncoder* aTrackEncoder) override {
    MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

    if (!mEncoder) {
      return;
    }

    mEncoder->SetError();
  }

 protected:
  RefPtr<TaskQueue> mEncoderThread;
  RefPtr<MediaEncoder> mEncoder;
};

MediaEncoder::MediaEncoder(
    RefPtr<TaskQueue> aEncoderThread,
    RefPtr<DriftCompensator> aDriftCompensator,
    UniquePtr<ContainerWriter> aWriter,
    UniquePtr<AudioTrackEncoder> aAudioEncoder,
    UniquePtr<VideoTrackEncoder> aVideoEncoder,
    UniquePtr<MediaQueue<EncodedFrame>> aEncodedAudioQueue,
    UniquePtr<MediaQueue<EncodedFrame>> aEncodedVideoQueue,
    const nsAString& aMimeType, uint64_t aMaxMemory, TimeDuration aTimeslice)
    : mMainThread(GetMainThreadSerialEventTarget()),
      mEncoderThread(std::move(aEncoderThread)),
      mDriftCompensator(std::move(aDriftCompensator)),
      mEncodedAudioQueue(std::move(aEncodedAudioQueue)),
      mEncodedVideoQueue(std::move(aEncodedVideoQueue)),
      mMuxer(MakeUnique<Muxer>(std::move(aWriter), *mEncodedAudioQueue,
                               *mEncodedVideoQueue)),
      mAudioEncoder(std::move(aAudioEncoder)),
      mVideoEncoder(std::move(aVideoEncoder)),
      mEncoderListener(MakeAndAddRef<EncoderListener>(mEncoderThread, this)),
      mMimeType(aMimeType),
      mMaxMemory(aMaxMemory),
      mTimeslice(aTimeslice),
      mStartTime(TimeStamp::Now()),
      mInitialized(false),
      mStarted(false),
      mCompleted(false),
      mError(false) {
  if (!mAudioEncoder) {
    mMuxedAudioEndTime = TimeUnit::FromInfinity();
    mEncodedAudioQueue->Finish();
  }
  if (!mVideoEncoder) {
    mMuxedVideoEndTime = TimeUnit::FromInfinity();
    mEncodedVideoQueue->Finish();
  }
}

void MediaEncoder::RegisterListeners() {
  if (mAudioEncoder) {
    mAudioPushListener = mEncodedAudioQueue->PushEvent().Connect(
        mEncoderThread, this, &MediaEncoder::OnEncodedAudioPushed);
    mAudioFinishListener = mEncodedAudioQueue->FinishEvent().Connect(
        mEncoderThread, this, &MediaEncoder::MaybeShutdown);
    MOZ_ALWAYS_SUCCEEDS(mEncoderThread->Dispatch(NS_NewRunnableFunction(
        "mozilla::AudioTrackEncoder::RegisterListener",
        [self = RefPtr<MediaEncoder>(this), this] {
          mAudioEncoder->RegisterListener(mEncoderListener);
        })));
  }
  if (mVideoEncoder) {
    mVideoPushListener = mEncodedVideoQueue->PushEvent().Connect(
        mEncoderThread, this, &MediaEncoder::OnEncodedVideoPushed);
    mVideoFinishListener = mEncodedVideoQueue->FinishEvent().Connect(
        mEncoderThread, this, &MediaEncoder::MaybeShutdown);
    MOZ_ALWAYS_SUCCEEDS(mEncoderThread->Dispatch(NS_NewRunnableFunction(
        "mozilla::VideoTrackEncoder::RegisterListener",
        [self = RefPtr<MediaEncoder>(this), this] {
          mVideoEncoder->RegisterListener(mEncoderListener);
        })));
  }
}

MediaEncoder::~MediaEncoder() {
  MOZ_ASSERT(!mAudioTrack);
  MOZ_ASSERT(!mVideoTrack);
  MOZ_ASSERT(!mAudioNode);
  MOZ_ASSERT(!mInputPort);
  MOZ_ASSERT(!mPipeTrack);
}

void MediaEncoder::EnsureGraphTrackFrom(MediaTrack* aTrack) {
  if (mGraphTrack) {
    return;
  }
  MOZ_DIAGNOSTIC_ASSERT(!aTrack->IsDestroyed());
  mGraphTrack = MakeAndAddRef<SharedDummyTrack>(
      aTrack->Graph()->CreateSourceTrack(MediaSegment::VIDEO));
}

void MediaEncoder::Suspend() {
  mGraphTrack->mTrack->QueueControlMessageWithNoShutdown(
      [self = RefPtr<MediaEncoder>(this), this] {
        TRACE("MediaEncoder::Suspend (graph)");
        if (NS_FAILED(mEncoderThread->Dispatch(
                NS_NewRunnableFunction("MediaEncoder::Suspend (encoder)",
                                       [self, this, now = TimeStamp::Now()] {
                                         if (mAudioEncoder) {
                                           mAudioEncoder->Suspend();
                                         }
                                         if (mVideoEncoder) {
                                           mVideoEncoder->Suspend(now);
                                         }
                                       })))) {
          // QueueControlMessageWithNoShutdown added an extra async step, and
          // now `thread` has shut down.
          return;
        }
      });
}

void MediaEncoder::Resume() {
  mGraphTrack->mTrack->QueueControlMessageWithNoShutdown(
      [self = RefPtr<MediaEncoder>(this), this] {
        TRACE("MediaEncoder::Resume (graph)");
        if (NS_FAILED(mEncoderThread->Dispatch(
                NS_NewRunnableFunction("MediaEncoder::Resume (encoder)",
                                       [self, this, now = TimeStamp::Now()] {
                                         if (mAudioEncoder) {
                                           mAudioEncoder->Resume();
                                         }
                                         if (mVideoEncoder) {
                                           mVideoEncoder->Resume(now);
                                         }
                                       })))) {
          // QueueControlMessageWithNoShutdown added an extra async step, and
          // now `thread` has shut down.
          return;
        }
      });
}

void MediaEncoder::ConnectAudioNode(AudioNode* aNode, uint32_t aOutput) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mAudioNode) {
    MOZ_ASSERT(false, "Only one audio node supported");
    return;
  }

  // Only AudioNodeTrack of kind EXTERNAL_OUTPUT stores output audio data in
  // the track (see AudioNodeTrack::AdvanceOutputSegment()). That means
  // forwarding input track in recorder session won't be able to copy data from
  // the track of non-destination node. Create a pipe track in this case.
  if (aNode->NumberOfOutputs() > 0) {
    AudioContext* ctx = aNode->Context();
    AudioNodeEngine* engine = new AudioNodeEngine(nullptr);
    AudioNodeTrack::Flags flags = AudioNodeTrack::EXTERNAL_OUTPUT |
                                  AudioNodeTrack::NEED_MAIN_THREAD_ENDED;
    mPipeTrack = AudioNodeTrack::Create(ctx, engine, flags, ctx->Graph());
    AudioNodeTrack* ns = aNode->GetTrack();
    if (ns) {
      mInputPort = mPipeTrack->AllocateInputPort(aNode->GetTrack(), 0, aOutput);
    }
  }

  mAudioNode = aNode;

  if (mPipeTrack) {
    mAudioListener =
        AudioTrackListener::Create(mDriftCompensator, this, mPipeTrack.get());
    EnsureGraphTrackFrom(mPipeTrack);
  } else {
    mAudioListener = AudioTrackListener::Create(mDriftCompensator, this,
                                                mAudioNode->GetTrack());
    EnsureGraphTrackFrom(mAudioNode->GetTrack());
  }
}

void MediaEncoder::ConnectMediaStreamTrack(MediaStreamTrack* aTrack) {
  MOZ_ASSERT(NS_IsMainThread());

  if (aTrack->Ended()) {
    MOZ_ASSERT_UNREACHABLE("Cannot connect ended track");
    return;
  }

  EnsureGraphTrackFrom(aTrack->GetTrack());

  if (AudioStreamTrack* audio = aTrack->AsAudioStreamTrack()) {
    if (!mAudioEncoder) {
      // No audio encoder for this audio track. It could be disabled.
      LOG(LogLevel::Warning, ("Cannot connect to audio track - no encoder"));
      return;
    }

    MOZ_ASSERT(!mAudioTrack, "Only one audio track supported.");
    MOZ_ASSERT(!mAudioListener, "One audio listener");

    LOG(LogLevel::Info, ("Connected to audio track {}", fmt::ptr(aTrack)));

    mAudioTrack = audio;
    mAudioListener = AudioTrackListener::Create(mDriftCompensator, this, audio);
  } else if (VideoStreamTrack* video = aTrack->AsVideoStreamTrack()) {
    if (!mVideoEncoder) {
      // No video encoder for this video track. It could be disabled.
      LOG(LogLevel::Warning, ("Cannot connect to video track - no encoder"));
      return;
    }

    MOZ_ASSERT(!mVideoTrack, "Only one video track supported.");
    MOZ_ASSERT(!mVideoListener, "One video listener");

    LOG(LogLevel::Info, ("Connected to video track {}", fmt::ptr(aTrack)));

    mVideoTrack = video;
    mVideoListener = VideoTrackListener::Create(this, video);
  } else {
    MOZ_ASSERT(false, "Unknown track type");
  }
}

void MediaEncoder::RemoveMediaStreamTrack(MediaStreamTrack* aTrack) {
  if (!aTrack) {
    MOZ_ASSERT(false);
    return;
  }

  if (AudioStreamTrack* audio = aTrack->AsAudioStreamTrack()) {
    if (audio != mAudioTrack) {
      MOZ_ASSERT(false, "Not connected to this audio track");
      return;
    }

    if (mAudioListener) {
      audio->RemoveListener(mAudioListener);
    }
    mAudioTrack = nullptr;
  } else if (VideoStreamTrack* video = aTrack->AsVideoStreamTrack()) {
    if (video != mVideoTrack) {
      MOZ_ASSERT(false, "Not connected to this video track");
      return;
    }

    if (mVideoListener) {
      video->RemoveDirectListener(mVideoListener);
      video->RemoveListener(mVideoListener);
    }
    mVideoTrack = nullptr;
  }
}

/* static */
already_AddRefed<MediaEncoder> MediaEncoder::CreateEncoder(
    RefPtr<TaskQueue> aEncoderThread, const nsAString& aMimeType,
    uint32_t aAudioBitrate, uint32_t aVideoBitrate, uint8_t aTrackTypes,
    TrackRate aTrackRate, uint64_t aMaxMemory, TimeDuration aTimeslice) {
  AUTO_PROFILER_LABEL("MediaEncoder::CreateEncoder", OTHER);

  UniquePtr<ContainerWriter> writer;
  UniquePtr<AudioTrackEncoder> audioEncoder;
  UniquePtr<VideoTrackEncoder> videoEncoder;
  auto encodedAudioQueue = MakeUnique<MediaQueue<EncodedFrame>>();
  auto encodedVideoQueue = MakeUnique<MediaQueue<EncodedFrame>>();
  auto driftCompensator =
      MakeRefPtr<DriftCompensator>(aEncoderThread, aTrackRate);

  Maybe<MediaContainerType> mimeType = MakeMediaContainerType(aMimeType);
  if (!mimeType) {
    return nullptr;
  }

  for (const auto& codec : mimeType->ExtendedType().Codecs().Range()) {
    if (codec.EqualsLiteral("opus")) {
      MOZ_ASSERT(!audioEncoder);
      audioEncoder =
          MakeUnique<OpusTrackEncoder>(aTrackRate, *encodedAudioQueue);
    } else if (codec.EqualsLiteral("vp8") || codec.EqualsLiteral("vp8.0")) {
      MOZ_ASSERT(!videoEncoder);
      if (Preferences::GetBool("media.recorder.video.frame_drops", true)) {
        videoEncoder = MakeUnique<VP8TrackEncoder>(driftCompensator, aTrackRate,
                                                   *encodedVideoQueue,
                                                   FrameDroppingMode::ALLOW);
      } else {
        videoEncoder = MakeUnique<VP8TrackEncoder>(driftCompensator, aTrackRate,
                                                   *encodedVideoQueue,
                                                   FrameDroppingMode::DISALLOW);
      }
    } else {
      MOZ_CRASH("Unknown codec");
    }
  }

  if (mimeType->Type() == MEDIAMIMETYPE(VIDEO_WEBM) ||
      mimeType->Type() == MEDIAMIMETYPE(AUDIO_WEBM)) {
    MOZ_ASSERT_IF(mimeType->Type() == MEDIAMIMETYPE(AUDIO_WEBM), !videoEncoder);
    writer = MakeUnique<WebMWriter>();
  } else if (mimeType->Type() == MEDIAMIMETYPE(AUDIO_OGG)) {
    MOZ_ASSERT(audioEncoder);
    MOZ_ASSERT(!videoEncoder);
    writer = MakeUnique<OggWriter>();
  }
  NS_ENSURE_TRUE(writer, nullptr);

  LOG(LogLevel::Info,
      ("Create encoder result:a[{}]({} bps) v[{}]({} bps) w[{}] mimeType = "
       "{}.",
       fmt::ptr(audioEncoder.get()), aAudioBitrate,
       fmt::ptr(videoEncoder.get()), aVideoBitrate, fmt::ptr(writer.get()),
       NS_ConvertUTF16toUTF8(aMimeType).get()));

  if (audioEncoder) {
    audioEncoder->SetWorkerThread(aEncoderThread);
    if (aAudioBitrate != 0) {
      audioEncoder->SetBitrate(aAudioBitrate);
    }
  }
  if (videoEncoder) {
    videoEncoder->SetWorkerThread(aEncoderThread);
    if (aVideoBitrate != 0) {
      videoEncoder->SetBitrate(aVideoBitrate);
    }
  }
  RefPtr<MediaEncoder> encoder = new MediaEncoder(
      std::move(aEncoderThread), std::move(driftCompensator), std::move(writer),
      std::move(audioEncoder), std::move(videoEncoder),
      std::move(encodedAudioQueue), std::move(encodedVideoQueue), aMimeType,
      aMaxMemory, aTimeslice);

  encoder->RegisterListeners();

  return encoder.forget();
}

nsresult MediaEncoder::GetEncodedData(
    nsTArray<nsTArray<uint8_t>>* aOutputBufs) {
  AUTO_PROFILER_LABEL("MediaEncoder::GetEncodedData", OTHER);

  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

  LOG(LogLevel::Verbose,
      ("GetEncodedData TimeStamp = {}", GetEncodeTimeStamp()));

  if (!mInitialized) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = mMuxer->GetData(aOutputBufs);
  if (mMuxer->IsFinished()) {
    mCompleted = true;
  }

  LOG(LogLevel::Verbose,
      ("END GetEncodedData TimeStamp={} "
       "mCompleted={}, aComplete={}, vComplete={}",
       GetEncodeTimeStamp(), mCompleted,
       !mAudioEncoder || mAudioEncoder->IsEncodingComplete(),
       !mVideoEncoder || mVideoEncoder->IsEncodingComplete()));

  return rv;
}

void MediaEncoder::MaybeShutdown() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
  if (!mEncodedAudioQueue->IsFinished()) {
    LOG(LogLevel::Debug,
        ("MediaEncoder {} not shutting down, audio is still live",
         fmt::ptr(this)));
    return;
  }

  if (!mEncodedVideoQueue->IsFinished()) {
    LOG(LogLevel::Debug,
        ("MediaEncoder {} not shutting down, video is still live",
         fmt::ptr(this)));
    return;
  }

  mShutdownEvent.Notify();

  // Stop will Shutdown() gracefully.
  (void)InvokeAsync(mMainThread, this, __func__, &MediaEncoder::Stop);
}

RefPtr<GenericNonExclusivePromise> MediaEncoder::Shutdown() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
  if (mShutdownPromise) {
    return mShutdownPromise;
  }

  LOG(LogLevel::Info, ("MediaEncoder is shutting down."));

  AutoTArray<RefPtr<GenericNonExclusivePromise>, 2> shutdownPromises;
  if (mAudioListener) {
    shutdownPromises.AppendElement(mAudioListener->OnShutdown());
  }
  if (mVideoListener) {
    shutdownPromises.AppendElement(mVideoListener->OnShutdown());
  }

  mShutdownPromise =
      GenericNonExclusivePromise::All(mEncoderThread, shutdownPromises)
          ->Then(mEncoderThread, __func__,
                 [](const GenericNonExclusivePromise::AllPromiseType::
                        ResolveOrRejectValue& aValue) {
                   if (aValue.IsResolve()) {
                     return GenericNonExclusivePromise::CreateAndResolve(
                         true, __func__);
                   }
                   return GenericNonExclusivePromise::CreateAndReject(
                       aValue.RejectValue(), __func__);
                 });

  mShutdownPromise->Then(
      mEncoderThread, __func__, [self = RefPtr<MediaEncoder>(this), this] {
        if (mAudioEncoder) {
          mAudioEncoder->UnregisterListener(mEncoderListener);
        }
        if (mVideoEncoder) {
          mVideoEncoder->UnregisterListener(mEncoderListener);
        }
        mEncoderListener->Forget();
        mMuxer->Disconnect();
        mAudioPushListener.DisconnectIfExists();
        mAudioFinishListener.DisconnectIfExists();
        mVideoPushListener.DisconnectIfExists();
        mVideoFinishListener.DisconnectIfExists();
      });

  return mShutdownPromise;
}

RefPtr<GenericNonExclusivePromise> MediaEncoder::Stop() {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(LogLevel::Info, ("MediaEncoder {} Stop", fmt::ptr(this)));

  DisconnectTracks();

  return InvokeAsync(mEncoderThread, this, __func__, &MediaEncoder::Shutdown);
}

RefPtr<GenericNonExclusivePromise> MediaEncoder::Cancel() {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(LogLevel::Info, ("MediaEncoder {} Cancel", fmt::ptr(this)));

  DisconnectTracks();

  return InvokeAsync(mEncoderThread, __func__,
                     [self = RefPtr<MediaEncoder>(this), this]() {
                       if (mAudioEncoder) {
                         mAudioEncoder->Cancel();
                       }
                       if (mVideoEncoder) {
                         mVideoEncoder->Cancel();
                       }
                       return Shutdown();
                     });
}

bool MediaEncoder::HasError() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
  return mError;
}

void MediaEncoder::SetError() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

  if (mError) {
    return;
  }

  mError = true;
  mErrorEvent.Notify();
}

auto MediaEncoder::RequestData() -> RefPtr<BlobPromise> {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
  TimeUnit muxedEndTime = std::min(mMuxedAudioEndTime, mMuxedVideoEndTime);
  mLastBlobTime = muxedEndTime;
  mLastExtractTime = muxedEndTime;
  return Extract()->Then(
      mMainThread, __func__,
      [this, self = RefPtr<MediaEncoder>(this)](
          const GenericPromise::ResolveOrRejectValue& aValue) {
        // Even if rejected, we want to gather what has already been
        // extracted into the current blob and expose that.
        (void)NS_WARN_IF(aValue.IsReject());
        return GatherBlob();
      });
}

void MediaEncoder::MaybeCreateMutableBlobStorage() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mMutableBlobStorage) {
    mMutableBlobStorage = new MutableBlobStorage(
        MutableBlobStorage::eCouldBeInTemporaryFile, nullptr, mMaxMemory);
  }
}

void MediaEncoder::OnEncodedAudioPushed(const RefPtr<EncodedFrame>& aFrame) {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
  mMuxedAudioEndTime = aFrame->GetEndTime();
  MaybeExtractOrGatherBlob();
}

void MediaEncoder::OnEncodedVideoPushed(const RefPtr<EncodedFrame>& aFrame) {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());
  mMuxedVideoEndTime = aFrame->GetEndTime();
  MaybeExtractOrGatherBlob();
}

void MediaEncoder::MaybeExtractOrGatherBlob() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

  TimeUnit muxedEndTime = std::min(mMuxedAudioEndTime, mMuxedVideoEndTime);
  if ((muxedEndTime - mLastBlobTime).ToTimeDuration() >= mTimeslice) {
    LOG(LogLevel::Verbose,
        ("MediaEncoder {} Muxed {:.2f}s of data since last "
         "blob. Issuing new blob.",
         fmt::ptr(this), (muxedEndTime - mLastBlobTime).ToSeconds()));
    RequestData()->Then(mEncoderThread, __func__,
                        [this, self = RefPtr<MediaEncoder>(this)](
                            const BlobPromise::ResolveOrRejectValue& aValue) {
                          if (aValue.IsReject()) {
                            SetError();
                            return;
                          }
                          RefPtr<BlobImpl> blob = aValue.ResolveValue();
                          mDataAvailableEvent.Notify(std::move(blob));
                        });
  }

  if (muxedEndTime - mLastExtractTime > TimeUnit::FromSeconds(1)) {
    // Extract data from the muxer at least every second.
    LOG(LogLevel::Verbose,
        ("MediaEncoder {} Muxed {:.2f}s of data since last "
         "extract. Extracting more data into blob.",
         fmt::ptr(this), (muxedEndTime - mLastExtractTime).ToSeconds()));
    mLastExtractTime = muxedEndTime;
    (void)Extract();
  }
}

// Pull encoded media data from MediaEncoder and put into MutableBlobStorage.
RefPtr<GenericPromise> MediaEncoder::Extract() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

  LOG(LogLevel::Debug, ("MediaEncoder {} Extract", fmt::ptr(this)));

  AUTO_PROFILER_LABEL("MediaEncoder::Extract", OTHER);

  // Pull encoded media data from MediaEncoder
  nsTArray<nsTArray<uint8_t>> buffer;
  nsresult rv = GetEncodedData(&buffer);
  MOZ_ASSERT(rv != NS_ERROR_INVALID_ARG, "Invalid args can be prevented.");
  if (NS_FAILED(rv)) {
    MOZ_RELEASE_ASSERT(buffer.IsEmpty());
    // Even if we failed to encode more data, it might be time to push a blob
    // with already encoded data.
  }

  // To ensure Extract() promises are resolved in calling order, we always
  // invoke the main thread. Even when the encoded buffer is empty.
  return InvokeAsync(
      mMainThread, __func__,
      [self = RefPtr<MediaEncoder>(this), this, buffer = std::move(buffer)] {
        MaybeCreateMutableBlobStorage();
        for (const auto& part : buffer) {
          if (part.IsEmpty()) {
            continue;
          }

          nsresult rv =
              mMutableBlobStorage->Append(part.Elements(), part.Length());
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return GenericPromise::CreateAndReject(rv, __func__);
          }
        }
        return GenericPromise::CreateAndResolve(true, __func__);
      });
}

auto MediaEncoder::GatherBlob() -> RefPtr<BlobPromise> {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mBlobPromise) {
    return mBlobPromise = GatherBlobImpl();
  }
  return mBlobPromise = mBlobPromise->Then(mMainThread, __func__,
                                           [self = RefPtr<MediaEncoder>(this)] {
                                             return self->GatherBlobImpl();
                                           });
}

auto MediaEncoder::GatherBlobImpl() -> RefPtr<BlobPromise> {
  RefPtr<BlobStorer> storer = MakeAndAddRef<BlobStorer>();
  MaybeCreateMutableBlobStorage();
  mMutableBlobStorage->GetBlobImplWhenReady(NS_ConvertUTF16toUTF8(mMimeType),
                                            storer);
  mMutableBlobStorage = nullptr;

  storer->Promise()->Then(
      mMainThread, __func__,
      [self = RefPtr<MediaEncoder>(this), p = storer->Promise()] {
        if (self->mBlobPromise == p) {
          // Reset BlobPromise.
          self->mBlobPromise = nullptr;
        }
      });

  return storer->Promise();
}

void MediaEncoder::DisconnectTracks() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mAudioNode) {
    mAudioNode->GetTrack()->RemoveListener(mAudioListener);
    if (mInputPort) {
      mInputPort->Destroy();
      mInputPort = nullptr;
    }
    if (mPipeTrack) {
      mPipeTrack->RemoveListener(mAudioListener);
      mPipeTrack->Destroy();
      mPipeTrack = nullptr;
    }
    mAudioNode = nullptr;
  }

  if (mAudioTrack) {
    RemoveMediaStreamTrack(mAudioTrack);
  }

  if (mVideoTrack) {
    RemoveMediaStreamTrack(mVideoTrack);
  }
}

bool MediaEncoder::IsWebMEncoderEnabled() {
  return StaticPrefs::media_encoder_webm_enabled();
}

void MediaEncoder::UpdateInitialized() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

  if (mInitialized) {
    // This could happen if an encoder re-inits due to a resolution change.
    return;
  }

  if (mAudioEncoder && !mAudioEncoder->IsInitialized()) {
    LOG(LogLevel::Debug, ("MediaEncoder {} UpdateInitialized waiting for audio",
                          fmt::ptr(this)));
    return;
  }

  if (mVideoEncoder && !mVideoEncoder->IsInitialized()) {
    LOG(LogLevel::Debug, ("MediaEncoder {} UpdateInitialized waiting for video",
                          fmt::ptr(this)));
    return;
  }

  MOZ_ASSERT(mMuxer->NeedsMetadata());
  nsTArray<RefPtr<TrackMetadataBase>> meta;
  if (mAudioEncoder && !*meta.AppendElement(mAudioEncoder->GetMetadata())) {
    LOG(LogLevel::Error, ("Audio metadata is null"));
    SetError();
    return;
  }
  if (mVideoEncoder && !*meta.AppendElement(mVideoEncoder->GetMetadata())) {
    LOG(LogLevel::Error, ("Video metadata is null"));
    SetError();
    return;
  }

  if (NS_FAILED(mMuxer->SetMetadata(meta))) {
    LOG(LogLevel::Error, ("SetMetadata failed"));
    SetError();
    return;
  }

  LOG(LogLevel::Info,
      ("MediaEncoder {} UpdateInitialized set metadata in muxer",
       fmt::ptr(this)));

  mInitialized = true;
}

void MediaEncoder::UpdateStarted() {
  MOZ_ASSERT(mEncoderThread->IsCurrentThreadIn());

  if (mStarted) {
    return;
  }

  if (mAudioEncoder && !mAudioEncoder->IsStarted()) {
    return;
  }

  if (mVideoEncoder && !mVideoEncoder->IsStarted()) {
    return;
  }

  mStarted = true;

  // Start issuing timeslice-based blobs.
  MOZ_ASSERT(mLastBlobTime == TimeUnit::Zero());

  mStartedEvent.Notify();
}

/*
 * SizeOfExcludingThis measures memory being used by the Media Encoder.
 * Currently it measures the size of the Encoder buffer and memory occupied
 * by mAudioEncoder, mVideoEncoder, and any current blob storage.
 */
auto MediaEncoder::SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf)
    -> RefPtr<SizeOfPromise> {
  MOZ_ASSERT(NS_IsMainThread());
  size_t blobStorageSize =
      mMutableBlobStorage ? mMutableBlobStorage->SizeOfCurrentMemoryBuffer()
                          : 0;

  return InvokeAsync(
      mEncoderThread, __func__,
      [self = RefPtr<MediaEncoder>(this), this, blobStorageSize,
       aMallocSizeOf]() {
        size_t size = 0;
        if (mAudioEncoder) {
          size += mAudioEncoder->SizeOfExcludingThis(aMallocSizeOf);
        }
        if (mVideoEncoder) {
          size += mVideoEncoder->SizeOfExcludingThis(aMallocSizeOf);
        }
        return SizeOfPromise::CreateAndResolve(blobStorageSize + size,
                                               __func__);
      });
}

}  // namespace mozilla

#undef LOG
