/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "BlankDecoderModule.h"
#include "DecodedStream.h"
#include "MediaData.h"
#include "MediaQueue.h"
#include "MediaSinkTestUtils.h"
#include "MediaTrackGraphImpl.h"
#include "MediaTrackListener.h"
#include "MockCubeb.h"
#include "gtest/gtest.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsJSEnvironment.h"

using mozilla::media::TimeUnit;
using testing::Test;

namespace mozilla {
enum MediaType { Audio = 1, Video = 2, AudioVideo = Audio | Video };

template <MediaType Type>
CopyableTArray<RefPtr<ProcessedMediaTrack>> CreateOutputTracks(
    MediaTrackGraphImpl* aGraph) {
  CopyableTArray<RefPtr<ProcessedMediaTrack>> outputTracks;
  if constexpr (Type & Audio) {
    outputTracks.AppendElement(
        aGraph->CreateForwardedInputTrack(MediaSegment::AUDIO));
  }
  if constexpr (Type & Video) {
    outputTracks.AppendElement(
        aGraph->CreateForwardedInputTrack(MediaSegment::VIDEO));
  }
  return outputTracks;
}

template <MediaType Type>
MediaInfo CreateMediaInfo() {
  MediaInfo info;
  info.mStartTime = TimeUnit::Zero();
  if constexpr (Type & Audio) {
    info.EnableAudio();
  }
  if constexpr (Type & Video) {
    info.EnableVideo();
  }
  return info;
}

class OnFallbackListener : public MediaTrackListener {
  const RefPtr<MediaTrack> mTrack;
  Atomic<bool> mOnFallback{true};

 public:
  explicit OnFallbackListener(MediaTrack* aTrack) : mTrack(aTrack) {}

  void Reset() { mOnFallback = true; }
  bool OnFallback() { return mOnFallback; }

  void NotifyOutput(MediaTrackGraph*, TrackTime) override {
    if (auto* ad =
            mTrack->GraphImpl()->CurrentDriver()->AsAudioCallbackDriver()) {
      mOnFallback = ad->OnFallback();
    }
  }
};

template <MediaType Type>
class TestDecodedStream : public Test {
 public:
  static constexpr TrackRate kRate = 48000;
  static constexpr uint32_t kChannels = 2;
  const RefPtr<MockCubeb> mMockCubeb;
  RefPtr<SmartMockCubebStream> mMockCubebStream;
  MediaQueue<AudioData> mAudioQueue;
  MediaQueue<VideoData> mVideoQueue;
  RefPtr<MediaTrackGraphImpl> mGraph;
  nsMainThreadPtrHandle<SharedDummyTrack> mDummyTrack;
  CopyableTArray<RefPtr<ProcessedMediaTrack>> mOutputTracks;
  Canonical<PrincipalHandle> mCanonicalOutputPrincipal;
  RefPtr<DecodedStream> mDecodedStream;

  TestDecodedStream()
      : mMockCubeb(MakeRefPtr<MockCubeb>(MockCubeb::RunningMode::Manual)),
        mCanonicalOutputPrincipal(
            AbstractThread::GetCurrent(), PRINCIPAL_HANDLE_NONE,
            "TestDecodedStream::mCanonicalOutputPrincipal") {
    MOZ_ASSERT(NS_IsMainThread());
  };

  void SetUp() override {
    MOZ_ASSERT(NS_IsMainThread());
    ENSURE_TAIL_DISPATCH(SetUp);

    CubebUtils::ForceSetCubebContext(mMockCubeb->AsCubebContext());

    mGraph = MediaTrackGraphImpl::GetInstance(
        MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1, kRate, nullptr,
        AbstractThread::MainThread());
    mDummyTrack = new nsMainThreadPtrHolder<SharedDummyTrack>(
        __func__,
        new SharedDummyTrack(mGraph->CreateSourceTrack(MediaSegment::AUDIO)));
    mOutputTracks = CreateOutputTracks<Type>(mGraph);
    mDecodedStream = MakeRefPtr<DecodedStream>(
        AbstractThread::GetCurrent(), mDummyTrack, mOutputTracks,
        &mCanonicalOutputPrincipal, /* aVolume = */ 1.0,
        /* aPlaybackRate = */ 1.0,
        /* aPreservesPitch = */ true,
        /* aShouldConfigAudioOutput = */ false,
        /* aDevice = */ nullptr, mAudioQueue, mVideoQueue);

    for (const auto& track : mOutputTracks) {
      track->QueueSetAutoend(false);
    }

    // Resume the dummy track and add an audio output to it, because only an
    // active audio track with an output device uses an AudioCallbackDriver.
    mDummyTrack->mTrack->Resume();
    mDummyTrack->mTrack->AddAudioOutput(reinterpret_cast<void*>(1),
                                        mGraph->PrimaryOutputDeviceID(), 0);

    RefPtr fallbackListener = new OnFallbackListener(mDummyTrack->mTrack);
    mDummyTrack->mTrack->AddListener(fallbackListener);

    // Dispatch a dummy task to ensure we trigger stable state.
    MOZ_ALWAYS_SUCCEEDS(
        NS_DispatchToMainThread(NS_NewRunnableFunction(__func__, [] {})));

    mMockCubebStream = WaitFor(mMockCubeb->StreamInitEvent());
    while (mMockCubebStream->State().isNothing()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(*mMockCubebStream->State(), CUBEB_STATE_STARTED);
    // Wait for the AudioCallbackDriver to come into effect.
    while (fallbackListener->OnFallback()) {
      ASSERT_EQ(mMockCubebStream->ManualDataCallback(1),
                MockCubebStream::KeepProcessing::Yes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void TearDown() override {
    MOZ_ASSERT(NS_IsMainThread());
    ENSURE_TAIL_DISPATCH(TearDown);

    // Destroy all tracks so they're removed from the graph.
    mDecodedStream->Shutdown();
    for (const auto& t : mOutputTracks) {
      t->Destroy();
    }
    mDummyTrack = nullptr;
    // DecodedStream also has a ref to the dummy track.
    mDecodedStream = nullptr;

    // Wait for the graph to shutdown. If all tracks are indeed removed, it will
    // not switch to another driver.
    MockCubebStream::KeepProcessing keepProcessing{};
    while ((keepProcessing = mMockCubebStream->ManualDataCallback(0)) ==
           MockCubebStream::KeepProcessing::Yes) {
      NS_ProcessPendingEvents(nullptr);
    }
    ASSERT_EQ(keepProcessing, MockCubebStream::KeepProcessing::No);

    // Dispatch a dummy task to ensure we trigger stable state.
    MOZ_ALWAYS_SUCCEEDS(
        NS_DispatchToMainThread(NS_NewRunnableFunction(__func__, [] {})));
    // Process the final track removal and run the stable state runnable.
    NS_ProcessPendingEvents(nullptr);
    // Process the shutdown runnable.
    NS_ProcessPendingEvents(nullptr);

    // Ensure the proxy release of AudioCallbackDriver has been processed on the
    // CubebOperation thread, as it holds a reference to the graph.
    RefPtr cubebOpsThread = CubebUtils::GetCubebOperationThread();
    SyncRunnable::DispatchToThread(
        cubebOpsThread,
        NS_NewRunnableFunction("TestDecodedStream AudioCallbackDriver release",
                               [] {}));

    // Graph should be shut down.
    ASSERT_TRUE(mGraph->OnGraphThreadOrNotRunning())
    << "Not on graph thread so graph must still be running!";
    ASSERT_EQ(mGraph->LifecycleState(),
              MediaTrackGraphImpl::LIFECYCLE_WAITING_FOR_THREAD_SHUTDOWN)
        << "The graph should be in its final state. Note it does not advance "
           "the state any further on thread shutdown.";
    CubebUtils::ForceSetCubebContext(nullptr);

    // mGraph should be the last or second last reference to the graph. The last
    // reference may be the JS-based shutdown blocker, which will eventually be
    // destroyed by CC and GC.
    MediaTrackGraphImpl* graph{};
    mGraph.forget(&graph);
    int32_t refcnt = static_cast<int32_t>(graph->Release());
    EXPECT_LE(refcnt, 1);

    // Attempt to release the last reference to the graph, to avoid its lifetime
    // reaching into future tests.
    nsJSContext::CycleCollectNow(CCReason::API);
    nsJSContext::GarbageCollectNow(JS::GCReason::API);
    NS_ProcessPendingEvents(nullptr);
  }

  MediaInfo CreateMediaInfo() { return mozilla::CreateMediaInfo<Type>(); }
};

using TestDecodedStreamA = TestDecodedStream<Audio>;
using TestDecodedStreamV = TestDecodedStream<Video>;
using TestDecodedStreamAV = TestDecodedStream<AudioVideo>;

TEST_F(TestDecodedStreamAV, StartStop) {
  ENSURE_TEST_TAIL_DISPATCH();

  mDecodedStream->Start(TimeUnit::Zero(), CreateMediaInfo());
  mDecodedStream->SetPlaying(true);
  mDecodedStream->Stop();
}
}  // namespace mozilla
