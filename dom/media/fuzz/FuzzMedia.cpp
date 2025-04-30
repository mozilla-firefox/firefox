/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ADTSDemuxer.h"
#include "BufferMediaResource.h"
#include "FlacDemuxer.h"
#include "FuzzingInterface.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "MP3Demuxer.h"
#include "MP4Demuxer.h"
#include "OggDemuxer.h"
#include "systemservices/MediaUtils.h"
#include "WaveDemuxer.h"
#include "WebMDemuxer.h"
#include "QueueObject.h"
#include "PlatformDecoderModule.h"
#include "mozilla/TaskQueue.h"
#include "PDMFactory.h"
#include "mozilla/gfx/gfxVars.h"
#include "MediaDataDecoderProxy.h"

using namespace mozilla;

class Benchmark;

class BenchmarkPlayback : public QueueObject {
  friend class Benchmark;
  BenchmarkPlayback(Benchmark* aGlobalState, MediaDataDemuxer* aDemuxer);
  void DemuxSamples();
  void DemuxNextSample();
  void GlobalShutdown();
  void InitDecoder(UniquePtr<TrackInfo>&& aInfo);

  void Output(MediaDataDecoder::DecodedData&& aResults);
  void Error(const MediaResult& aError);
  void InputExhausted();

  // Shutdown trackdemuxer and demuxer if any and shutdown the task queues.
  void FinalizeShutdown();

  Atomic<Benchmark*> mGlobalState;

  RefPtr<TaskQueue> mDecoderTaskQueue;
  RefPtr<MediaDataDecoder> mDecoder;

  // Object only accessed on Thread()
  RefPtr<MediaDataDemuxer> mDemuxer;
  RefPtr<MediaTrackDemuxer> mTrackDemuxer;
  nsTArray<RefPtr<MediaRawData>> mSamples;
  UniquePtr<TrackInfo> mInfo;
  size_t mSampleIndex;
  Maybe<TimeStamp> mDecodeStartTime;
  uint32_t mFrameCount;
  bool mFinished;
  bool mDrained;
};

// Init() must have been called at least once prior on the
// main thread.
class Benchmark : public QueueObject {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Benchmark)

  struct Parameters {
    Parameters()
        : mFramesToMeasure(UINT32_MAX),
          mStartupFrame(1),
          mTimeout(TimeDuration::Forever()) {}

    Parameters(uint32_t aFramesToMeasure, uint32_t aStartupFrame,
               uint32_t aStopAtFrame, const TimeDuration& aTimeout)
        : mFramesToMeasure(aFramesToMeasure),
          mStartupFrame(aStartupFrame),
          mStopAtFrame(Some(aStopAtFrame)),
          mTimeout(aTimeout) {}

    const uint32_t mFramesToMeasure;
    const uint32_t mStartupFrame;
    const Maybe<uint32_t> mStopAtFrame;
    const TimeDuration mTimeout;
  };

  typedef MozPromise<uint32_t, MediaResult, /* IsExclusive = */ true>
      BenchmarkPromise;

  explicit Benchmark(MediaDataDemuxer* aDemuxer,
                     const Parameters& aParameters = Parameters());
  RefPtr<BenchmarkPromise> Run();

  // Must be called on the main thread.
  static void Init();

 private:
  friend class BenchmarkPlayback;
  virtual ~Benchmark();
  void ReturnResult(uint32_t aDecodeFps);
  void ReturnError(const MediaResult& aError);
  void Dispose();
  const Parameters mParameters;
  RefPtr<Benchmark> mKeepAliveUntilComplete;
  BenchmarkPlayback mPlaybackState;
  MozPromiseHolder<BenchmarkPromise> mPromise;
};

class FuzzRunner {
 public:
  explicit FuzzRunner(Benchmark* aBenchmark) : mBenchmark(aBenchmark) {}

  void Run() {
    // Assert we're on the main thread, otherwise `done` must be synchronized.
    MOZ_ASSERT(NS_IsMainThread());
    bool done = false;

    Benchmark::Init();
    mBenchmark->Run()->Then(
        // Non DocGroup-version of AbstractThread::MainThread() is fine for
        // testing.
        AbstractThread::MainThread(), __func__,
        [&](uint32_t aDecodeFps) { done = true; }, [&]() { done = true; });

    // Wait until benchmark completes.
    SpinEventLoopUntil("FuzzRunner::Run"_ns, [&]() { return done; });
  }

 private:
  RefPtr<Benchmark> mBenchmark;
};

Benchmark::Benchmark(MediaDataDemuxer* aDemuxer, const Parameters& aParameters)
    : QueueObject(
          TaskQueue::Create(GetMediaThreadPool(MediaThreadType::SUPERVISOR),
                            "Benchmark::QueueObject")),
      mParameters(aParameters),
      mPlaybackState(this, aDemuxer) {
  MOZ_COUNT_CTOR(Benchmark);
}

Benchmark::~Benchmark() { MOZ_COUNT_DTOR(Benchmark); }

RefPtr<Benchmark::BenchmarkPromise> Benchmark::Run() {
  RefPtr<Benchmark> self = this;
  mKeepAliveUntilComplete = this;
  return InvokeAsync(Thread(), __func__, [self] {
    RefPtr<BenchmarkPromise> p = self->mPromise.Ensure(__func__);
    self->mPlaybackState.Dispatch(NS_NewRunnableFunction(
        "Benchmark::Run", [self]() { self->mPlaybackState.DemuxSamples(); }));
    return p;
  });
}

void Benchmark::ReturnResult(uint32_t aDecodeFps) {
  MOZ_ASSERT(OnThread());

  mPromise.ResolveIfExists(aDecodeFps, __func__);
}

void Benchmark::ReturnError(const MediaResult& aError) {
  MOZ_ASSERT(OnThread());

  mPromise.RejectIfExists(aError, __func__);
}

void Benchmark::Dispose() {
  MOZ_ASSERT(OnThread());

  mKeepAliveUntilComplete = nullptr;
}

void Benchmark::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::gfx::gfxVars::Initialize();
}

BenchmarkPlayback::BenchmarkPlayback(Benchmark* aGlobalState,
                                     MediaDataDemuxer* aDemuxer)
    : QueueObject(
          TaskQueue::Create(GetMediaThreadPool(MediaThreadType::SUPERVISOR),
                            "BenchmarkPlayback::QueueObject")),
      mGlobalState(aGlobalState),
      mDecoderTaskQueue(TaskQueue::Create(
          GetMediaThreadPool(MediaThreadType::PLATFORM_DECODER),
          "BenchmarkPlayback::mDecoderTaskQueue")),
      mDemuxer(aDemuxer),
      mSampleIndex(0),
      mFrameCount(0),
      mFinished(false),
      mDrained(false) {}

void BenchmarkPlayback::DemuxSamples() {
  MOZ_ASSERT(OnThread());

  RefPtr<Benchmark> ref(mGlobalState);
  mDemuxer->Init()->Then(
      Thread(), __func__,
      [this, ref](nsresult aResult) {
        MOZ_ASSERT(OnThread());
        if (mDemuxer->GetNumberTracks(TrackInfo::kVideoTrack)) {
          mTrackDemuxer = mDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
        } else if (mDemuxer->GetNumberTracks(TrackInfo::kAudioTrack)) {
          mTrackDemuxer = mDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
        }
        if (!mTrackDemuxer) {
          Error(MediaResult(NS_ERROR_FAILURE, "Can't create track demuxer"));
          return;
        }
        DemuxNextSample();
      },
      [this, ref](const MediaResult& aError) { Error(aError); });
}

void BenchmarkPlayback::DemuxNextSample() {
  MOZ_ASSERT(OnThread());

  RefPtr<Benchmark> ref(mGlobalState);
  RefPtr<MediaTrackDemuxer::SamplesPromise> promise =
      mTrackDemuxer->GetSamples();
  promise->Then(
      Thread(), __func__,
      [this, ref](RefPtr<MediaTrackDemuxer::SamplesHolder> aHolder) {
        mSamples.AppendElements(aHolder->GetMovableSamples());
        if (ref->mParameters.mStopAtFrame &&
            mSamples.Length() == ref->mParameters.mStopAtFrame.ref()) {
          InitDecoder(mTrackDemuxer->GetInfo());
        } else {
          Dispatch(
              NS_NewRunnableFunction("BenchmarkPlayback::DemuxNextSample",
                                     [this, ref]() { DemuxNextSample(); }));
        }
      },
      [this, ref](const MediaResult& aError) {
        switch (aError.Code()) {
          case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
            InitDecoder(mTrackDemuxer->GetInfo());
            break;
          default:
            Error(aError);
            break;
        }
      });
}

void BenchmarkPlayback::InitDecoder(UniquePtr<TrackInfo>&& aInfo) {
  MOZ_ASSERT(OnThread());

  if (!aInfo) {
    Error(MediaResult(NS_ERROR_FAILURE, "Invalid TrackInfo"));
    return;
  }

  RefPtr<PDMFactory> platform = new PDMFactory();
  mInfo = std::move(aInfo);
  RefPtr<Benchmark> ref(mGlobalState);
  platform->CreateDecoder(CreateDecoderParams{*mInfo})
      ->Then(
          Thread(), __func__,
          [this, ref](RefPtr<MediaDataDecoder>&& aDecoder) {
            mDecoder = new MediaDataDecoderProxy(
                aDecoder.forget(), do_AddRef(mDecoderTaskQueue.get()));
            mDecoder->Init()->Then(
                Thread(), __func__,
                [this, ref](TrackInfo::TrackType aTrackType) {
                  InputExhausted();
                },
                [this, ref](const MediaResult& aError) { Error(aError); });
          },
          [this, ref](const MediaResult& aError) { Error(aError); });
}

void BenchmarkPlayback::FinalizeShutdown() {
  MOZ_ASSERT(OnThread());

  MOZ_ASSERT(mFinished, "GlobalShutdown must have been run");
  MOZ_ASSERT(!mDecoder, "mDecoder must have been shutdown already");
  MOZ_ASSERT(!mDemuxer, "mDemuxer must have been shutdown already");
  MOZ_DIAGNOSTIC_ASSERT(mDecoderTaskQueue->IsEmpty());
  mDecoderTaskQueue = nullptr;

  RefPtr<Benchmark> ref(mGlobalState);
  ref->Thread()->Dispatch(NS_NewRunnableFunction(
      "BenchmarkPlayback::FinalizeShutdown", [ref]() { ref->Dispose(); }));
}

void BenchmarkPlayback::GlobalShutdown() {
  MOZ_ASSERT(OnThread());

  MOZ_ASSERT(!mFinished, "We've already shutdown");

  mFinished = true;

  if (mTrackDemuxer) {
    mTrackDemuxer->Reset();
    mTrackDemuxer->BreakCycles();
    mTrackDemuxer = nullptr;
  }
  mDemuxer = nullptr;

  if (mDecoder) {
    RefPtr<Benchmark> ref(mGlobalState);
    mDecoder->Flush()->Then(
        Thread(), __func__,
        [ref, this]() {
          mDecoder->Shutdown()->Then(
              Thread(), __func__, [ref, this]() { FinalizeShutdown(); },
              []() { MOZ_CRASH("not reached"); });
          mDecoder = nullptr;
          mInfo = nullptr;
        },
        []() { MOZ_CRASH("not reached"); });
  } else {
    FinalizeShutdown();
  }
}

void BenchmarkPlayback::Output(MediaDataDecoder::DecodedData&& aResults) {
  MOZ_ASSERT(OnThread());
  MOZ_ASSERT(!mFinished);

  RefPtr<Benchmark> ref(mGlobalState);
  mFrameCount += aResults.Length();
  if (!mDecodeStartTime && mFrameCount >= ref->mParameters.mStartupFrame) {
    mDecodeStartTime = Some(TimeStamp::Now());
  }
  TimeStamp now = TimeStamp::Now();
  uint32_t frames = mFrameCount - ref->mParameters.mStartupFrame;
  TimeDuration elapsedTime = now - mDecodeStartTime.refOr(now);
  if (((frames == ref->mParameters.mFramesToMeasure) &&
       mFrameCount > ref->mParameters.mStartupFrame && frames > 0) ||
      elapsedTime >= ref->mParameters.mTimeout || mDrained) {
    uint32_t decodeFps = frames / elapsedTime.ToSeconds();
    GlobalShutdown();
    ref->Dispatch(NS_NewRunnableFunction(
        "BenchmarkPlayback::Output",
        [ref, decodeFps]() { ref->ReturnResult(decodeFps); }));
  }
}

void BenchmarkPlayback::Error(const MediaResult& aError) {
  MOZ_ASSERT(OnThread());

  RefPtr<Benchmark> ref(mGlobalState);
  GlobalShutdown();
  ref->Dispatch(
      NS_NewRunnableFunction("BenchmarkPlayback::Error",
                             [ref, aError]() { ref->ReturnError(aError); }));
}

void BenchmarkPlayback::InputExhausted() {
  MOZ_ASSERT(OnThread());
  MOZ_ASSERT(!mFinished);

  if (mSampleIndex >= mSamples.Length()) {
    Error(MediaResult(NS_ERROR_FAILURE, "Nothing left to decode"));
    return;
  }

  RefPtr<MediaRawData> sample = mSamples[mSampleIndex];
  RefPtr<Benchmark> ref(mGlobalState);
  RefPtr<MediaDataDecoder::DecodePromise> p = mDecoder->Decode(sample);

  mSampleIndex++;
  if (mSampleIndex == mSamples.Length() && !ref->mParameters.mStopAtFrame) {
    // Complete current frame decode then drain if still necessary.
    p->Then(
        Thread(), __func__,
        [ref, this](MediaDataDecoder::DecodedData&& aResults) {
          Output(std::move(aResults));
          if (!mFinished) {
            mDecoder->Drain()->Then(
                Thread(), __func__,
                [ref, this](MediaDataDecoder::DecodedData&& aResults) {
                  mDrained = true;
                  Output(std::move(aResults));
                  MOZ_ASSERT(mFinished, "We must be done now");
                },
                [ref, this](const MediaResult& aError) { Error(aError); });
          }
        },
        [ref, this](const MediaResult& aError) { Error(aError); });
  } else {
    if (mSampleIndex == mSamples.Length() && ref->mParameters.mStopAtFrame) {
      mSampleIndex = 0;
    }
    // Continue decoding
    p->Then(
        Thread(), __func__,
        [ref, this](MediaDataDecoder::DecodedData&& aResults) {
          Output(std::move(aResults));
          if (!mFinished) {
            InputExhausted();
          }
        },
        [ref, this](const MediaResult& aError) { Error(aError); });
  }
}

#define MOZ_MEDIA_FUZZER(_name)                                         \
  static int FuzzingRunMedia##_name(const uint8_t* data, size_t size) { \
    if (!size) {                                                        \
      return 0;                                                         \
    }                                                                   \
    RefPtr<BufferMediaResource> resource =                              \
        new BufferMediaResource(data, size);                            \
    FuzzRunner runner(new Benchmark(new _name##Demuxer(resource)));     \
    runner.Run();                                                       \
    return 0;                                                           \
  }                                                                     \
  MOZ_FUZZING_INTERFACE_RAW(nullptr, FuzzingRunMedia##_name, Media##_name);

MOZ_MEDIA_FUZZER(ADTS);
MOZ_MEDIA_FUZZER(Flac);
MOZ_MEDIA_FUZZER(MP3);
MOZ_MEDIA_FUZZER(MP4);
MOZ_MEDIA_FUZZER(Ogg);
MOZ_MEDIA_FUZZER(WAV);
MOZ_MEDIA_FUZZER(WebM);
