/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CrossGraphPort.h"
#include "DeviceInputTrack.h"
#include "MediaTrackGraphImpl.h"
#include "StaticComponents.h"
#include "VideoUtils.h"
#include "gmock/gmock.h"
#include "gtest/gtest-printers.h"
#include "gtest/gtest.h"
#ifdef MOZ_WEBRTC
#  include "MediaEngineWebRTCAudio.h"
#endif  // MOZ_WEBRTC
#include "MockCubeb.h"
#include "WavDumper.h"
#include "mozilla/Components.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/GenericFactory.h"
#include "mozilla/Preferences.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StateMirroring.h"
#include "mozilla/gtest/MozHelpers.h"
#include "mozilla/gtest/WaitFor.h"
#include "mozilla/ipc/IOThread.h"
#include "nsComponentManager.h"
#include "nsITargetShutdownTask.h"
#include "nsIThreadInternal.h"
#include "nsXPCOMPrivate.h"

using namespace mozilla;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;
using testing::StrEq;
using testing::StrictMock;

// Short-hand for InvokeAsync on the current thread.
#define InvokeAsync(f) InvokeAsync(GetCurrentSerialEventTarget(), __func__, f)

// Short-hand for DispatchToCurrentThread with a function.
#define DispatchFunction(f) \
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(__func__, f))

// Short-hand for DispatchToCurrentThread with a method with arguments
#define DispatchMethod(t, m, args...) \
  NS_DispatchToCurrentThread(NewRunnableMethod(__func__, t, m, ##args))

// Short-hand for draining the current threads event queue, i.e. processing
// those runnables dispatched per above.
#define ProcessEventQueue()                     \
  while (NS_ProcessNextEvent(nullptr, false)) { \
  }

#define TEST_WithTailDispatch(suite, name)                          \
  static void Do##name();                                           \
  TEST(suite, name)                                                 \
  {                                                                 \
    MOZ_ALWAYS_SUCCEEDS(GetMainThreadSerialEventTarget()->Dispatch( \
        NS_NewRunnableFunction("##name", &Do##name)));              \
    NS_ProcessPendingEvents(nullptr);                               \
  }                                                                 \
  static void Do##name()

namespace {
#ifdef MOZ_WEBRTC
/*
 * Common ControlMessages
 */
struct StartInputProcessing : public ControlMessage {
  const RefPtr<AudioProcessingTrack> mProcessingTrack;
  const RefPtr<AudioInputProcessing> mInputProcessing;

  StartInputProcessing(AudioProcessingTrack* aTrack,
                       AudioInputProcessing* aInputProcessing)
      : ControlMessage(aTrack),
        mProcessingTrack(aTrack),
        mInputProcessing(aInputProcessing) {}
  void Run() override { mInputProcessing->Start(mTrack->Graph()); }
};

struct StopInputProcessing : public ControlMessage {
  const RefPtr<AudioInputProcessing> mInputProcessing;

  explicit StopInputProcessing(AudioProcessingTrack* aTrack,
                               AudioInputProcessing* aInputProcessing)
      : ControlMessage(aTrack), mInputProcessing(aInputProcessing) {}
  void Run() override { mInputProcessing->Stop(mTrack->Graph()); }
};

void QueueApplySettings(AudioProcessingTrack* aTrack,
                        AudioInputProcessing* aInputProcessing,
                        const MediaEnginePrefs& aSettings) {
  aTrack->QueueControlMessageWithNoShutdown(
      [inputProcessing = RefPtr{aInputProcessing}, aSettings,
       // If the track is not connected to a device then the particular
       // AudioDeviceID (nullptr) passed to ReevaluateInputDevice() is not
       // important.
       deviceId = aTrack->DeviceId().valueOr(nullptr),
       graph = aTrack->Graph()] {
        inputProcessing->ApplySettings(graph, deviceId, aSettings);
      });
}

void QueueExpectIsPassThrough(AudioProcessingTrack* aTrack,
                              AudioInputProcessing* aInputProcessing) {
  aTrack->QueueControlMessageWithNoShutdown(
      [inputProcessing = RefPtr{aInputProcessing}, graph = aTrack->Graph()] {
        EXPECT_EQ(inputProcessing->IsPassThrough(graph), true);
      });
}
#endif  // MOZ_WEBRTC

class GoFaster : public ControlMessage {
  MockCubeb* mCubeb;

 public:
  explicit GoFaster(MockCubeb* aCubeb)
      : ControlMessage(nullptr), mCubeb(aCubeb) {}
  void Run() override { mCubeb->GoFaster(); }
};

struct StartNonNativeInput : public ControlMessage {
  const RefPtr<NonNativeInputTrack> mInputTrack;
  RefPtr<AudioInputSource> mInputSource;

  StartNonNativeInput(NonNativeInputTrack* aInputTrack,
                      RefPtr<AudioInputSource>&& aInputSource)
      : ControlMessage(aInputTrack),
        mInputTrack(aInputTrack),
        mInputSource(std::move(aInputSource)) {}
  void Run() override { mInputTrack->StartAudio(std::move(mInputSource)); }
};

struct StopNonNativeInput : public ControlMessage {
  const RefPtr<NonNativeInputTrack> mInputTrack;

  explicit StopNonNativeInput(NonNativeInputTrack* aInputTrack)
      : ControlMessage(aInputTrack), mInputTrack(aInputTrack) {}
  void Run() override { mInputTrack->StopAudio(); }
};

// Helper for detecting when fallback driver has been switched away, for use
// with RunningMode::Manual.
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

class MockAudioDataListener : public AudioDataListener {
 protected:
  ~MockAudioDataListener() = default;

 public:
  MockAudioDataListener() = default;

  MOCK_METHOD(uint32_t, RequestedInputChannelCount, (MediaTrackGraph*),
              (const));
  MOCK_METHOD(cubeb_input_processing_params, RequestedInputProcessingParams,
              (MediaTrackGraph*), (const));
  MOCK_METHOD(bool, IsVoiceInput, (MediaTrackGraph*), (const));
  MOCK_METHOD(void, DeviceChanged, (MediaTrackGraph*));
  MOCK_METHOD(void, Disconnect, (MediaTrackGraph*));
  MOCK_METHOD(void, NotifySetRequestedInputProcessingParams,
              (MediaTrackGraph*, int, cubeb_input_processing_params));
  MOCK_METHOD(void, NotifySetRequestedInputProcessingParamsResult,
              (MediaTrackGraph*, int,
               (const Result<cubeb_input_processing_params, int>&)));
};

class MockProcessedMediaTrack : public ProcessedMediaTrack {
 public:
  explicit MockProcessedMediaTrack(TrackRate aRate)
      : ProcessedMediaTrack(aRate, MediaSegment::AUDIO, new AudioSegment()) {
    ON_CALL(*this, ProcessInput)
        .WillByDefault([segment = GetData<AudioSegment>()](
                           GraphTime aFrom, GraphTime aTo, uint32_t aFlags) {
          segment->AppendNullData(aTo - aFrom);
        });
    ON_CALL(*this, AddListenerImpl)
        .WillByDefault(
            [self = MOZ_KnownLive(this)](
                already_AddRefed<MediaTrackListener> aListener) mutable {
              self->MediaTrack::AddListenerImpl(std::move(aListener));
            });
    ON_CALL(*this, RemoveListenerImpl)
        .WillByDefault([self = MOZ_KnownLive(this)](
                           MediaTrackListener* aListener) mutable {
          self->MediaTrack::RemoveListenerImpl(aListener);
        });
  }

  MOCK_METHOD(void, ProcessInput,
              (GraphTime aFrom, GraphTime aTo, uint32_t aFlags), (override));
  MOCK_METHOD(void, AddListenerImpl, (already_AddRefed<MediaTrackListener>),
              (override));
  MOCK_METHOD(void, RemoveListenerImpl, (MediaTrackListener*), (override));

  uint32_t NumberOfChannels() const override { return 2; };
};
}  // namespace

/*
 * The set of tests here are a bit special. In part because they're async and
 * depends on the graph thread to function. In part because they depend on main
 * thread stable state to send messages to the graph.
 *
 * Any message sent from the main thread to the graph through the graph's
 * various APIs are scheduled to run in stable state. Stable state occurs after
 * a task in the main thread eventloop has run to completion.
 *
 * Since gtests are generally sync and on main thread, calling into the graph
 * may schedule a stable state runnable but with no task in the eventloop to
 * trigger stable state. Therefore care must be taken to always call into the
 * graph from a task, typically via InvokeAsync or a dispatch to main thread.
 */

TEST_WithTailDispatch(TestAudioTrackGraph, DifferentDeviceIDs) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* g1 = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ nullptr, AbstractThread::MainThread());

  MediaTrackGraph* g2 = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ reinterpret_cast<cubeb_devid>(1),
      AbstractThread::MainThread());

  MediaTrackGraph* g1_2 = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ nullptr, AbstractThread::MainThread());

  MediaTrackGraph* g2_2 = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ reinterpret_cast<cubeb_devid>(1),
      AbstractThread::MainThread());

  EXPECT_NE(g1, g2) << "Different graphs due to different device ids";
  EXPECT_EQ(g1, g1_2) << "Same graphs for same device ids";
  EXPECT_EQ(g2, g2_2) << "Same graphs for same device ids";

  for (MediaTrackGraph* g : {g1, g2}) {
    // Dummy track with an audio output to make graph rolling with an
    // AudioCallbackDriver. Add it and remove it to remove the graph from the
    // global hash table and let it shutdown.

    using SourceTrackPromise = MozPromise<SourceMediaTrack*, nsresult, true>;
    auto p = InvokeAsync([g] {
      SourceMediaTrack* track = g->CreateSourceTrack(MediaSegment::AUDIO);
      track->AddAudioOutput(reinterpret_cast<void*>(1),
                            g->PrimaryOutputDeviceID(), 0);
      return SourceTrackPromise::CreateAndResolve(track, __func__);
    });

    WaitFor(cubeb->StreamInitEvent());
    RefPtr<SourceMediaTrack> dummySource = WaitFor(p).unwrap();

    DispatchMethod(dummySource, &SourceMediaTrack::Destroy);

    WaitFor(cubeb->StreamDestroyEvent());
  }
}

TEST_WithTailDispatch(TestAudioTrackGraph, SetOutputDeviceID) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Set the output device id in GetInstance method confirm that it is the one
  // used in cubeb_stream_init.
  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ reinterpret_cast<cubeb_devid>(2),
      AbstractThread::MainThread());

  // Dummy track with an audio output to make graph rolling with an
  // AudioCallbackDriver. Add it and remove it to remove the graph from the
  // global hash table and let it shutdown.
  RefPtr<SourceMediaTrack> dummySource;
  DispatchFunction([&] {
    dummySource = graph->CreateSourceTrack(MediaSegment::AUDIO);
    dummySource->AddAudioOutput(reinterpret_cast<void*>(1),
                                graph->PrimaryOutputDeviceID(), 0);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());

  EXPECT_EQ(stream->GetOutputDeviceID(), reinterpret_cast<cubeb_devid>(2))
      << "After init confirm the expected output device id";

  // Test has finished, destroy the track to shutdown the MTG.
  DispatchMethod(dummySource, &SourceMediaTrack::Destroy);
  WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph, StreamName) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Initialize a graph with a system thread driver to check that the stream
  // name survives the driver switch.
  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ reinterpret_cast<cubeb_devid>(1),
      AbstractThread::MainThread());
  nsLiteralCString name1("name1");
  graph->CurrentDriver()->SetStreamName(name1);

  // Dummy track with an audio output to start the graph rolling and switch to
  // an AudioCallbackDriver.
  RefPtr<SourceMediaTrack> dummySource;
  DispatchFunction([&] {
    dummySource = graph->CreateSourceTrack(MediaSegment::AUDIO);
    dummySource->AddAudioOutput(reinterpret_cast<void*>(1),
                                graph->PrimaryOutputDeviceID(), 0);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_EQ(stream->StreamName(), name1);

  // Test a name change on an existing stream.
  nsLiteralCString name2("name2");
  DispatchFunction([&] {
    graph->QueueControlMessageWithNoShutdown(
        [&] { graph->CurrentDriver()->SetStreamName(name2); });
  });
  nsCString name = WaitFor(stream->NameSetEvent());
  EXPECT_EQ(name, name2);

  // Test has finished. Destroy the track to shutdown the MTG.
  DispatchMethod(dummySource, &SourceMediaTrack::Destroy);
  WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      AudioDriverImmediateStreamNameChange) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // Initialize a graph with an audio thread driver,
  // which will immediately start a fallback system clock driver.
  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      /*OutputDeviceID*/ reinterpret_cast<cubeb_devid>(1),
      AbstractThread::MainThread());
  nsLiteralCString name0("name0");
  graph->CurrentDriver()->SetStreamName(name0);

  // Dummy track with an audio output to start the graph rolling and keep the
  // AudioCallbackDriver.
  RefPtr<SourceMediaTrack> dummySource;
  // Test a name change immediately after the fallback driver starts.
  nsLiteralCString name1("name1");
  DispatchFunction([&] {
    dummySource = graph->CreateSourceTrack(MediaSegment::AUDIO);
    dummySource->AddAudioOutput(reinterpret_cast<void*>(1),
                                graph->PrimaryOutputDeviceID(), 0);
    graph->QueueControlMessageWithNoShutdown(
        [&] { graph->CurrentDriver()->SetStreamName(name1); });
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  // Connect a listener to ensure that this (main) thread gets events if the
  // name has not yet changed.
  MediaEventListener nameSetListener = stream->NameSetEvent().Connect(
      NS_GetCurrentThread(), [](const nsCString&) {});
  // Wait for a name change, if it has not yet changed.
  SpinEventLoopUntil("TestAudioTrackGraph, OfflineDestruction"_ns,
                     [&] { return stream->StreamName() != name0; });
  nameSetListener.Disconnect();
  EXPECT_EQ(stream->StreamName(), name1);

  // Test has finished. Destroy the track to shutdown the MTG.
  DispatchMethod(dummySource, &SourceMediaTrack::Destroy);
  WaitFor(cubeb->StreamDestroyEvent());
}

// Round-trip a control message through the graph thread so that the graph has
// iterated, and CheckDriver() run, before the state is inspected.
static void WaitForGraphIteration(MediaTrackGraphImpl* aGraph) {
  MozPromiseHolder<GenericPromise> holder;
  RefPtr<GenericPromise> promise = holder.Ensure(__func__);
  DispatchFunction([&] {
    aGraph->QueueControlMessageWithNoShutdown(
        [&] { holder.Resolve(true, __func__); });
  });
  (void)WaitFor(promise);
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      AudioTrackWithoutDeviceUsesSystemClockDriver) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  uint32_t streamsInited = 0;
  MediaEventListener initListener = cubeb->StreamInitEvent().Connect(
      NS_GetCurrentThread(),
      [&](const RefPtr<SmartMockCubebStream>&) { ++streamsInited; });

  // An audio track that is neither fed by an input device nor rendered to an
  // output device does not need an AudioCallbackDriver, so no audio stream is
  // opened for it.
  RefPtr<SourceMediaTrack> dummySource;
  DispatchFunction(
      [&] { dummySource = graph->CreateSourceTrack(MediaSegment::AUDIO); });
  WaitForGraphIteration(graph);
  WaitForGraphIteration(graph);
  {
    MonitorAutoLock lock(graph->GetMonitor());
    EXPECT_TRUE(graph->CurrentDriver()->AsSystemClockDriver());
  }
  initListener.Disconnect();
  EXPECT_EQ(streamsInited, 0U);

  // Adding an audio output switches to an AudioCallbackDriver.
  DispatchFunction([&] {
    dummySource->AddAudioOutput(reinterpret_cast<void*>(1),
                                graph->PrimaryOutputDeviceID(), 0);
  });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasOutput);
  EXPECT_FALSE(stream->mHasInput);

  // Removing the only audio output switches back to a SystemClockDriver and
  // releases the audio stream.
  DispatchFunction(
      [&] { dummySource->RemoveAudioOutput(reinterpret_cast<void*>(1)); });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream.get());
  {
    MonitorAutoLock lock(graph->GetMonitor());
    EXPECT_TRUE(graph->CurrentDriver()->AsSystemClockDriver());
  }

  // Test has finished. Add an output again so that graph shutdown can be
  // observed through the stream, then destroy the track to shutdown the MTG.
  DispatchFunction([&] {
    dummySource->AddAudioOutput(reinterpret_cast<void*>(1),
                                graph->PrimaryOutputDeviceID(), 0);
  });
  (void)WaitFor(cubeb->StreamInitEvent());
  DispatchMethod(dummySource, &SourceMediaTrack::Destroy);
  WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      SuspendedOrEndedTrackOutputUsesSystemClockDriver) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  uint32_t streamsInited = 0;
  MediaEventListener initListener = cubeb->StreamInitEvent().Connect(
      NS_GetCurrentThread(),
      [&](const RefPtr<SmartMockCubebStream>&) { ++streamsInited; });

  // A suspended track with an audio output, like the destination track of an
  // AudioContext that is not allowed to start, renders only silence. Active
  // audio tracks without an output, like a remote WebRTC track that is not
  // attached to a sink, have nothing to render. Together they do not need an
  // AudioCallbackDriver.
  RefPtr<SourceMediaTrack> suspendedSource;
  RefPtr<SourceMediaTrack> activeSource;
  RefPtr<SourceMediaTrack> otherSource;
  DispatchFunction([&] {
    suspendedSource = graph->CreateSourceTrack(MediaSegment::AUDIO);
    suspendedSource->Suspend();
    suspendedSource->AddAudioOutput(reinterpret_cast<void*>(1),
                                    graph->PrimaryOutputDeviceID(), 0);
    activeSource = graph->CreateSourceTrack(MediaSegment::AUDIO);
    otherSource = graph->CreateSourceTrack(MediaSegment::AUDIO);
  });
  WaitForGraphIteration(graph);
  WaitForGraphIteration(graph);
  {
    MonitorAutoLock lock(graph->GetMonitor());
    EXPECT_TRUE(graph->CurrentDriver()->AsSystemClockDriver());
  }
  initListener.Disconnect();
  EXPECT_EQ(streamsInited, 0U);

  // Resuming the track with the output switches to an AudioCallbackDriver.
  DispatchFunction([&] { suspendedSource->Resume(); });
  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasOutput);
  EXPECT_FALSE(stream->mHasInput);

  // Suspending it again, as AudioContext.suspend() does, switches back to a
  // SystemClockDriver and releases the audio stream.
  DispatchFunction([&] { suspendedSource->Suspend(); });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream.get());

  // An audio output on an active track switches to an AudioCallbackDriver.
  DispatchFunction([&] {
    activeSource->AddAudioOutput(reinterpret_cast<void*>(1),
                                 graph->PrimaryOutputDeviceID(), 0);
  });
  stream = WaitFor(cubeb->StreamInitEvent());

  // Once that track has ended, its output does not count either, although
  // another audio track is still active. End() takes effect immediately, so
  // dispatch it to run after WaitFor() has connected its listener.
  DispatchFunction([&] { activeSource->End(); });
  destroyedStream = WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream.get());

  // Test has finished. Add an output to the remaining active track so that
  // graph shutdown can be observed through the stream, then destroy the tracks
  // to shutdown the MTG.
  DispatchFunction([&] {
    otherSource->AddAudioOutput(reinterpret_cast<void*>(1),
                                graph->PrimaryOutputDeviceID(), 0);
  });
  (void)WaitFor(cubeb->StreamInitEvent());
  DispatchFunction([&] {
    suspendedSource->Destroy();
    activeSource->Destroy();
    otherSource->Destroy();
  });
  WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph, OfflineDestruction) {
  RefPtr graph = static_cast<MediaTrackGraphImpl*>(
      MediaTrackGraph::CreateNonRealtimeInstance(48000));
  DispatchFunction([&] {
    // Add and remove a dummy track to trigger graph shutdown.
    RefPtr dummyTrack = new MockProcessedMediaTrack(graph->GraphRate());
    graph->AddTrack(dummyTrack, {});
    dummyTrack->Destroy();
  });
  // Wait until `graph` has the only reference to the graph.
  SpinEventLoopUntil("TestAudioTrackGraph, OfflineDestruction"_ns, [&] {
    graph.get()->AddRef();
    return graph.get()->Release() == 1;
  });
}

TEST_WithTailDispatch(TestAudioTrackGraph, NotifyDeviceStarted) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  RefPtr<SourceMediaTrack> dummySource;
  (void)WaitForResolve(InvokeAsync([&] {
    // Dummy track to make graph rolling. Add it and remove it to remove the
    // graph from the global hash table and let it shutdown.
    dummySource = graph->CreateSourceTrack(MediaSegment::AUDIO);
    dummySource->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    return graph->NotifyWhenDeviceStarted(nullptr);
  }));

  {
    MediaTrackGraphImpl* graph = dummySource->GraphImpl();
    MonitorAutoLock lock(graph->GetMonitor());
    EXPECT_TRUE(graph->CurrentDriver()->AsAudioCallbackDriver());
    EXPECT_TRUE(graph->CurrentDriver()->ThreadRunning());
  }

  // Test has finished, destroy the track to shutdown the MTG.
  DispatchMethod(dummySource, &SourceMediaTrack::Destroy);
  WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph, NonNativeInputTrackStartAndStop) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;

  // Add a NonNativeInputTrack with an audio output to graph, making graph
  // create an output-only AudioCallbackDriver.
  RefPtr<NonNativeInputTrack> track;
  DispatchFunction([&] {
    track = new NonNativeInputTrack(graph->GraphRate(), deviceId,
                                    PRINCIPAL_HANDLE_NONE);
    graph->AddTrack(track, {});
    track->AddAudioOutput(reinterpret_cast<void*>(1),
                          graph->PrimaryOutputDeviceID(), 0);
  });

  RefPtr<SmartMockCubebStream> driverStream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_FALSE(driverStream->mHasInput);
  EXPECT_TRUE(driverStream->mHasOutput);

  // Main test below:
  {
    const AudioInputSource::Id sourceId = 1;
    const uint32_t channels = 2;
    const TrackRate rate = 48000;

    // Start and stop the audio in NonNativeInputTrack.
    {
      struct DeviceInfo {
        uint32_t mChannelCount;
        AudioInputType mType;
      };
      using DeviceQueryPromise =
          MozPromise<DeviceInfo, nsresult, /* IsExclusive = */ true>;

      struct DeviceQueryMessage : public ControlMessage {
        const NonNativeInputTrack* mInputTrack;
        MozPromiseHolder<DeviceQueryPromise> mHolder;

        DeviceQueryMessage(NonNativeInputTrack* aInputTrack,
                           MozPromiseHolder<DeviceQueryPromise>&& aHolder)
            : ControlMessage(aInputTrack),
              mInputTrack(aInputTrack),
              mHolder(std::move(aHolder)) {}
        void Run() override {
          DeviceInfo info = {mInputTrack->NumberOfChannels(),
                             mInputTrack->DevicePreference()};
          AbstractThread::MainThread()->Dispatch(NS_NewRunnableFunction(
              "TestAudioTrackGraph::DeviceQueryMessage",
              [holder = std::move(mHolder), devInfo = info]() mutable {
                holder.Resolve(devInfo, __func__);
              }));
        }
      };

      // No input channels and device preference before start.
      {
        MozPromiseHolder<DeviceQueryPromise> h;
        RefPtr<DeviceQueryPromise> p = h.Ensure(__func__);
        DispatchFunction([&] {
          track->GraphImpl()->AppendMessage(
              std::make_unique<DeviceQueryMessage>(track.get(), std::move(h)));
        });
        Result<DeviceInfo, nsresult> r = WaitFor(p);
        ASSERT_TRUE(r.isOk());
        DeviceInfo info = r.unwrap();

        EXPECT_EQ(info.mChannelCount, 0U);
        EXPECT_EQ(info.mType, AudioInputType::Unknown);
      }

      DispatchFunction([&] {
        track->GraphImpl()->AppendMessage(std::make_unique<StartNonNativeInput>(
            track.get(), MakeRefPtr<AudioInputSource>(
                             MakeRefPtr<AudioInputSourceListener>(track.get()),
                             sourceId, deviceId, channels, true /* voice */,
                             PRINCIPAL_HANDLE_NONE, rate, graph->GraphRate())));
      });
      RefPtr<SmartMockCubebStream> nonNativeStream =
          WaitFor(cubeb->StreamInitEvent());
      EXPECT_TRUE(nonNativeStream->mHasInput);
      EXPECT_FALSE(nonNativeStream->mHasOutput);
      EXPECT_EQ(nonNativeStream->GetInputDeviceID(), deviceId);
      EXPECT_EQ(nonNativeStream->InputChannels(), channels);
      EXPECT_EQ(nonNativeStream->SampleRate(), static_cast<uint32_t>(rate));

      // Input channels and device preference should be set after start.
      {
        MozPromiseHolder<DeviceQueryPromise> h;
        RefPtr<DeviceQueryPromise> p = h.Ensure(__func__);
        DispatchFunction([&] {
          track->GraphImpl()->AppendMessage(
              std::make_unique<DeviceQueryMessage>(track.get(), std::move(h)));
        });
        Result<DeviceInfo, nsresult> r = WaitFor(p);
        ASSERT_TRUE(r.isOk());
        DeviceInfo info = r.unwrap();

        EXPECT_EQ(info.mChannelCount, channels);
        EXPECT_EQ(info.mType, AudioInputType::Voice);
      }

      (void)WaitFor(nonNativeStream->FramesProcessedEvent());

      DispatchFunction([&] {
        track->GraphImpl()->AppendMessage(
            std::make_unique<StopNonNativeInput>(track.get()));
      });
      RefPtr<SmartMockCubebStream> destroyedStream =
          WaitFor(cubeb->StreamDestroyEvent());
      EXPECT_EQ(destroyedStream.get(), nonNativeStream.get());

      // No input channels and device preference after stop.
      {
        MozPromiseHolder<DeviceQueryPromise> h;
        RefPtr<DeviceQueryPromise> p = h.Ensure(__func__);
        DispatchFunction([&] {
          track->GraphImpl()->AppendMessage(
              std::make_unique<DeviceQueryMessage>(track.get(), std::move(h)));
        });
        Result<DeviceInfo, nsresult> r = WaitFor(p);
        ASSERT_TRUE(r.isOk());
        DeviceInfo info = r.unwrap();

        EXPECT_EQ(info.mChannelCount, 0U);
        EXPECT_EQ(info.mType, AudioInputType::Unknown);
      }
    }

    // Make sure the NonNativeInputTrack can restart and stop its audio.
    {
      DispatchFunction([&] {
        track->GraphImpl()->AppendMessage(std::make_unique<StartNonNativeInput>(
            track.get(), MakeRefPtr<AudioInputSource>(
                             MakeRefPtr<AudioInputSourceListener>(track.get()),
                             sourceId, deviceId, channels, true,
                             PRINCIPAL_HANDLE_NONE, rate, graph->GraphRate())));
      });
      RefPtr<SmartMockCubebStream> nonNativeStream =
          WaitFor(cubeb->StreamInitEvent());
      EXPECT_TRUE(nonNativeStream->mHasInput);
      EXPECT_FALSE(nonNativeStream->mHasOutput);
      EXPECT_EQ(nonNativeStream->GetInputDeviceID(), deviceId);
      EXPECT_EQ(nonNativeStream->InputChannels(), channels);
      EXPECT_EQ(nonNativeStream->SampleRate(), static_cast<uint32_t>(rate));

      (void)WaitFor(nonNativeStream->FramesProcessedEvent());

      DispatchFunction([&] {
        track->GraphImpl()->AppendMessage(
            std::make_unique<StopNonNativeInput>(track.get()));
      });
      RefPtr<SmartMockCubebStream> destroyedStream =
          WaitFor(cubeb->StreamDestroyEvent());
      EXPECT_EQ(destroyedStream.get(), nonNativeStream.get());
    }
  }

  // Clean up.
  DispatchFunction([&] { track->Destroy(); });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), driverStream.get());
}

TEST_WithTailDispatch(TestAudioTrackGraph, NonNativeInputTrackErrorCallback) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;

  // Add a NonNativeInputTrack with an audio output to graph, making graph
  // create an output-only AudioCallbackDriver.
  RefPtr<NonNativeInputTrack> track;
  DispatchFunction([&] {
    track = new NonNativeInputTrack(graph->GraphRate(), deviceId,
                                    PRINCIPAL_HANDLE_NONE);
    graph->AddTrack(track, {});
    track->AddAudioOutput(reinterpret_cast<void*>(1),
                          graph->PrimaryOutputDeviceID(), 0);
  });

  RefPtr<SmartMockCubebStream> driverStream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_FALSE(driverStream->mHasInput);
  EXPECT_TRUE(driverStream->mHasOutput);

  // Main test below:
  {
    const AudioInputSource::Id sourceId = 1;
    const uint32_t channels = 2;
    const TrackRate rate = 48000;

    // Launch and start the non-native audio stream.
    DispatchFunction([&] {
      track->GraphImpl()->AppendMessage(std::make_unique<StartNonNativeInput>(
          track.get(), MakeRefPtr<AudioInputSource>(
                           MakeRefPtr<AudioInputSourceListener>(track.get()),
                           sourceId, deviceId, channels, true,
                           PRINCIPAL_HANDLE_NONE, rate, graph->GraphRate())));
    });
    RefPtr<SmartMockCubebStream> nonNativeStream =
        WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(nonNativeStream->mHasInput);
    EXPECT_FALSE(nonNativeStream->mHasOutput);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), deviceId);
    EXPECT_EQ(nonNativeStream->InputChannels(), channels);
    EXPECT_EQ(nonNativeStream->SampleRate(), static_cast<uint32_t>(rate));

    // Make sure the audio stream is running.
    (void)WaitFor(nonNativeStream->FramesProcessedEvent());

    // Force an error. This results in the audio stream destroying.
    DispatchFunction([&] { nonNativeStream->ForceError(); });
    WaitFor(nonNativeStream->ErrorForcedEvent());

    RefPtr<SmartMockCubebStream> destroyedStream =
        WaitFor(cubeb->StreamDestroyEvent());
    EXPECT_EQ(destroyedStream.get(), nonNativeStream.get());
  }

  // Make sure it's ok to call audio stop again.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(
        std::make_unique<StopNonNativeInput>(track.get()));
  });

  // Clean up.
  DispatchFunction([&] { track->Destroy(); });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), driverStream.get());
}

class TestDeviceInputConsumerTrack : public DeviceInputConsumerTrack {
 public:
  static TestDeviceInputConsumerTrack* Create(MediaTrackGraph* aGraph) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    TestDeviceInputConsumerTrack* track =
        new TestDeviceInputConsumerTrack(aGraph->GraphRate());
    aGraph->AddTrack(track, {});
    return track;
  }

  void Destroy() {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    DisconnectDeviceInput();
    DeviceInputConsumerTrack::Destroy();
  }

  void ProcessInput(GraphTime aFrom, GraphTime aTo, uint32_t aFlags) override {
    MOZ_RELEASE_ASSERT(aFrom < aTo);

    if (mInputs.IsEmpty()) {
      GetData<AudioSegment>()->AppendNullData(aTo - aFrom);
    } else {
      MOZ_RELEASE_ASSERT(mInputs.Length() == 1);
      AudioSegment data;
      DeviceInputConsumerTrack::GetInputSourceData(data, aFrom, aTo);
      GetData<AudioSegment>()->AppendFrom(&data);
    }
  };

  uint32_t NumberOfChannels() const override {
    if (mInputs.IsEmpty()) {
      return 0;
    }
    DeviceInputTrack* t = mInputs[0]->GetSource()->AsDeviceInputTrack();
    MOZ_RELEASE_ASSERT(t);
    return t->NumberOfChannels();
  }

 private:
  explicit TestDeviceInputConsumerTrack(TrackRate aSampleRate)
      : DeviceInputConsumerTrack(aSampleRate) {}
};

TEST_WithTailDispatch(TestAudioTrackGraph, DeviceChangedCallback) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graphImpl = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  class TestAudioDataListener : public StrictMock<MockAudioDataListener> {
   public:
    TestAudioDataListener(uint32_t aChannelCount, bool aIsVoice) {
      EXPECT_CALL(*this, RequestedInputChannelCount)
          .WillRepeatedly(Return(aChannelCount));
      EXPECT_CALL(*this, RequestedInputProcessingParams)
          .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));
      EXPECT_CALL(*this, IsVoiceInput).WillRepeatedly(Return(aIsVoice));
      {
        InSequence s;
        EXPECT_CALL(*this, DeviceChanged);
        EXPECT_CALL(*this, Disconnect);
      }
    }

   private:
    ~TestAudioDataListener() = default;
  };

  // Create a full-duplex AudioCallbackDriver by creating a NativeInputTrack.
  const CubebUtils::AudioDeviceID device1 = (CubebUtils::AudioDeviceID)1;
  RefPtr<TestAudioDataListener> listener1 = new TestAudioDataListener(1, false);
  RefPtr<TestDeviceInputConsumerTrack> track1 =
      TestDeviceInputConsumerTrack::Create(graphImpl);
  track1->ConnectDeviceInput(device1, listener1.get(), PRINCIPAL_HANDLE_NONE);

  EXPECT_TRUE(track1->ConnectedToNativeDevice());
  EXPECT_FALSE(track1->ConnectedToNonNativeDevice());
  auto started =
      InvokeAsync([&] { return graphImpl->NotifyWhenDeviceStarted(nullptr); });
  RefPtr<SmartMockCubebStream> stream1 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream1->mHasInput);
  EXPECT_TRUE(stream1->mHasOutput);
  EXPECT_EQ(stream1->GetInputDeviceID(), device1);
  (void)WaitFor(started);

  // Create a NonNativeInputTrack, and make sure its DeviceChangeCallback works.
  const CubebUtils::AudioDeviceID device2 = (CubebUtils::AudioDeviceID)2;
  RefPtr<TestAudioDataListener> listener2 = new TestAudioDataListener(2, true);
  RefPtr<TestDeviceInputConsumerTrack> track2 =
      TestDeviceInputConsumerTrack::Create(graphImpl);
  track2->ConnectDeviceInput(device2, listener2.get(), PRINCIPAL_HANDLE_NONE);

  EXPECT_FALSE(track2->ConnectedToNativeDevice());
  EXPECT_TRUE(track2->ConnectedToNonNativeDevice());
  RefPtr<SmartMockCubebStream> stream2 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_FALSE(stream2->mHasOutput);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);

  // Produce a device-changed event for the NonNativeInputTrack.
  DispatchFunction([&] { stream2->ForceDeviceChanged(); });
  WaitFor(stream2->DeviceChangeForcedEvent());

  // Produce a device-changed event for the NativeInputTrack.
  DispatchFunction([&] { stream1->ForceDeviceChanged(); });
  WaitFor(stream1->DeviceChangeForcedEvent());

  // Destroy the NonNativeInputTrack.
  DispatchFunction([&] {
    track2->DisconnectDeviceInput();
    track2->Destroy();
  });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream2.get());

  // Destroy the NativeInputTrack.
  DispatchFunction([&] {
    track1->DisconnectDeviceInput();
    track1->Destroy();
  });
  destroyedStream = WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream1.get());
}

// The native audio stream (a.k.a. GraphDriver) and the non-native audio stream
// should always be the same as the max requested input channel of its paired
// DeviceInputTracks. This test checks if the audio stream paired with the
// DeviceInputTrack will follow the max requested input channel or not.
//
// The main focus for this test is to make sure DeviceInputTrack::OpenAudio and
// ::CloseAudio works as what we expect. Besides, This test also confirms
// MediaTrackGraph::ReevaluateInputDevice works correctly by using a
// test-only AudioDataListener.
//
// This test is pretty similar to RestartAudioIfProcessingMaxChannelCountChanged
// below, which tests the same thing but using AudioProcessingTrack.
// AudioProcessingTrack is the consumer of the  DeviceInputTrack used in wild.
// It has its own customized AudioDataListener. However, it only tests when
// MOZ_WEBRTC is defined.
TEST_WithTailDispatch(TestAudioTrackGraph,
                      RestartAudioIfMaxChannelCountChanged) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());
  auto unforcer = WaitFor(cubeb->ForceAudioThread()).unwrap();
  (void)unforcer;

  MediaTrackGraph* graphImpl = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // A test-only AudioDataListener that simulates AudioInputProcessing's setter
  // and getter for the input channel count.
  class TestAudioDataListener : public StrictMock<MockAudioDataListener> {
   public:
    TestAudioDataListener(uint32_t aChannelCount, bool aIsVoice) {
      EXPECT_CALL(*this, RequestedInputChannelCount)
          .WillRepeatedly(Return(aChannelCount));
      EXPECT_CALL(*this, RequestedInputProcessingParams)
          .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));
      EXPECT_CALL(*this, IsVoiceInput).WillRepeatedly(Return(aIsVoice));
      EXPECT_CALL(*this, Disconnect);
    }
    // Main thread API
    void SetInputChannelCount(MediaTrackGraph* aGraph,
                              CubebUtils::AudioDeviceID aDevice,
                              uint32_t aChannelCount) {
      MOZ_RELEASE_ASSERT(NS_IsMainThread());
      static_cast<MediaTrackGraphImpl*>(aGraph)
          ->QueueControlMessageWithNoShutdown(
              [this, self = RefPtr(this), aGraph, aDevice, aChannelCount] {
                EXPECT_CALL(*this, RequestedInputChannelCount)
                    .WillRepeatedly(Return(aChannelCount));
                aGraph->ReevaluateInputDevice(aDevice);
              });
    }

   private:
    ~TestAudioDataListener() = default;
  };

  // Request a new input channel count and expect to have a new stream.
  auto setNewChannelCount = [&](const RefPtr<TestAudioDataListener>& aListener,
                                RefPtr<SmartMockCubebStream>& aStream,
                                uint32_t aChannelCount) {
    ASSERT_TRUE(!!aListener);
    ASSERT_TRUE(!!aStream);
    ASSERT_TRUE(aStream->mHasInput);
    ASSERT_NE(aChannelCount, 0U);

    const CubebUtils::AudioDeviceID device = aStream->GetInputDeviceID();

    bool destroyed = false;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          destroyed = aDestroyed.get() == aStream.get();
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          newStream = aCreated;
        });

    DispatchFunction([&] {
      aListener->SetInputChannelCount(graphImpl, device, aChannelCount);
    });

    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, RestartAudioIfMaxChannelCountChanged) #1"_ns,
        [&] { return destroyed && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aStream = newStream;
  };

  // Open a new track and expect to have a new stream.
  auto openTrack = [&](RefPtr<SmartMockCubebStream>& aCurrentStream,
                       RefPtr<TestDeviceInputConsumerTrack>& aTrack,
                       const RefPtr<TestAudioDataListener>& aListener,
                       CubebUtils::AudioDeviceID aDevice) {
    ASSERT_TRUE(!!aCurrentStream);
    ASSERT_TRUE(aCurrentStream->mHasInput);
    ASSERT_TRUE(!aTrack);
    ASSERT_TRUE(!!aListener);

    bool destroyed = false;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          destroyed = aDestroyed.get() == aCurrentStream.get();
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          newStream = aCreated;
        });

    aTrack = TestDeviceInputConsumerTrack::Create(graphImpl);
    aTrack->ConnectDeviceInput(aDevice, aListener.get(), PRINCIPAL_HANDLE_NONE);

    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, RestartAudioIfMaxChannelCountChanged) #2"_ns,
        [&] { return destroyed && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aCurrentStream = newStream;
  };

  // Test for the native input device first then non-native device. The
  // non-native device will be destroyed before the native device in case of
  // causing a driver switching.

  // Test for the native device.
  const CubebUtils::AudioDeviceID nativeDevice = (CubebUtils::AudioDeviceID)1;
  RefPtr<TestDeviceInputConsumerTrack> track1;
  RefPtr<TestAudioDataListener> listener1;
  RefPtr<SmartMockCubebStream> nativeStream;
  RefPtr<TestDeviceInputConsumerTrack> track2;
  RefPtr<TestAudioDataListener> listener2;
  {
    // Open a 1-channel NativeInputTrack.
    listener1 = new TestAudioDataListener(1, false);
    track1 = TestDeviceInputConsumerTrack::Create(graphImpl);
    track1->ConnectDeviceInput(nativeDevice, listener1.get(),
                               PRINCIPAL_HANDLE_NONE);

    EXPECT_TRUE(track1->ConnectedToNativeDevice());
    EXPECT_FALSE(track1->ConnectedToNonNativeDevice());
    auto started = InvokeAsync(
        [&] { return graphImpl->NotifyWhenDeviceStarted(nullptr); });
    nativeStream = WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(nativeStream->mHasInput);
    EXPECT_TRUE(nativeStream->mHasOutput);
    EXPECT_EQ(nativeStream->GetInputDeviceID(), nativeDevice);
    (void)WaitFor(started);

    // Open a 2-channel NativeInputTrack and wait for a new driver since the
    // max-channel for the native device becomes 2 now.
    listener2 = new TestAudioDataListener(2, false);
    openTrack(nativeStream, track2, listener2, nativeDevice);
    EXPECT_EQ(nativeStream->InputChannels(), 2U);

    // Set the second NativeInputTrack to 1-channel and wait for a new driver
    // since the max-channel for the native device becomes 1 now.
    setNewChannelCount(listener2, nativeStream, 1);
    EXPECT_EQ(nativeStream->InputChannels(), 1U);

    // Set the first NativeInputTrack to 2-channel and wait for a new driver
    // since the max input channel for the native device becomes 2 now.
    setNewChannelCount(listener1, nativeStream, 2);
    EXPECT_EQ(nativeStream->InputChannels(), 2U);
  }

  // Test for the non-native device.
  {
    const CubebUtils::AudioDeviceID nonNativeDevice =
        (CubebUtils::AudioDeviceID)2;

    // Open a 1-channel NonNativeInputTrack.
    RefPtr<TestAudioDataListener> listener3 =
        new TestAudioDataListener(1, false);
    RefPtr<TestDeviceInputConsumerTrack> track3 =
        TestDeviceInputConsumerTrack::Create(graphImpl);
    track3->ConnectDeviceInput(nonNativeDevice, listener3.get(),
                               PRINCIPAL_HANDLE_NONE);
    EXPECT_FALSE(track3->ConnectedToNativeDevice());
    EXPECT_TRUE(track3->ConnectedToNonNativeDevice());

    RefPtr<SmartMockCubebStream> nonNativeStream =
        WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(nonNativeStream->mHasInput);
    EXPECT_FALSE(nonNativeStream->mHasOutput);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), nonNativeDevice);
    EXPECT_EQ(nonNativeStream->InputChannels(), 1U);

    // Open a 2-channel NonNativeInputTrack and wait for a new stream since
    // the max-channel for the non-native device becomes 2 now.
    RefPtr<TestAudioDataListener> listener4 =
        new TestAudioDataListener(2, false);
    RefPtr<TestDeviceInputConsumerTrack> track4;
    openTrack(nonNativeStream, track4, listener4, nonNativeDevice);
    EXPECT_EQ(nonNativeStream->InputChannels(), 2U);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), nonNativeDevice);

    // Set the second NonNativeInputTrack to 1-channel and wait for a new
    // driver since the max-channel for the non-native device becomes 1 now.
    setNewChannelCount(listener4, nonNativeStream, 1);
    EXPECT_EQ(nonNativeStream->InputChannels(), 1U);

    // Set the first NonNativeInputTrack to 2-channel and wait for a new
    // driver since the max input channel for the non-native device becomes 2
    // now.
    setNewChannelCount(listener3, nonNativeStream, 2);
    EXPECT_EQ(nonNativeStream->InputChannels(), 2U);

    // Close the second NonNativeInputTrack (1-channel) then the first one
    // (2-channel) so we won't result in another stream creation.
    DispatchFunction([&] {
      track4->DisconnectDeviceInput();
      track4->Destroy();
    });
    DispatchFunction([&] {
      track3->DisconnectDeviceInput();
      track3->Destroy();
    });
    RefPtr<SmartMockCubebStream> destroyedStream =
        WaitFor(cubeb->StreamDestroyEvent());
    EXPECT_EQ(destroyedStream.get(), nonNativeStream.get());
  }

  // Tear down for the native device.
  {
    // Close the second NativeInputTrack (1-channel) then the first one
    // (2-channel) so we won't have driver switching.
    DispatchFunction([&] {
      track2->DisconnectDeviceInput();
      track2->Destroy();
    });
    DispatchFunction([&] {
      track1->DisconnectDeviceInput();
      track1->Destroy();
    });
    RefPtr<SmartMockCubebStream> destroyedStream =
        WaitFor(cubeb->StreamDestroyEvent());
    EXPECT_EQ(destroyedStream.get(), nativeStream.get());
  }
}

// This test is pretty similar to SwitchNativeAudioProcessingTrack below, which
// tests the same thing but using AudioProcessingTrack. AudioProcessingTrack is
// the consumer of the  DeviceInputTrack used in wild. It has its own customized
// AudioDataListener. However, it only tests when MOZ_WEBRTC is defined.
TEST_WithTailDispatch(TestAudioTrackGraph, SwitchNativeInputDevice) {
  class TestAudioDataListener : public StrictMock<MockAudioDataListener> {
   public:
    TestAudioDataListener(uint32_t aChannelCount, bool aIsVoice) {
      EXPECT_CALL(*this, RequestedInputChannelCount)
          .WillRepeatedly(Return(aChannelCount));
      EXPECT_CALL(*this, RequestedInputProcessingParams)
          .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));
      EXPECT_CALL(*this, IsVoiceInput).WillRepeatedly(Return(aIsVoice));
    }

   private:
    ~TestAudioDataListener() = default;
  };

  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  auto switchNativeDevice = [&](RefPtr<SmartMockCubebStream>&&
                                    aCurrentNativeStream,
                                RefPtr<TestDeviceInputConsumerTrack>&
                                    aCurrentNativeTrack,
                                RefPtr<SmartMockCubebStream>& aNextNativeStream,
                                RefPtr<TestDeviceInputConsumerTrack>&
                                    aNextNativeTrack) {
    ASSERT_TRUE(aCurrentNativeStream->mHasInput);
    ASSERT_TRUE(aCurrentNativeStream->mHasOutput);
    ASSERT_TRUE(aNextNativeStream->mHasInput);
    ASSERT_FALSE(aNextNativeStream->mHasOutput);

    std::cerr << "Switching native input from device "
              << aCurrentNativeStream->GetInputDeviceID() << " to "
              << aNextNativeStream->GetInputDeviceID() << std::endl;

    uint32_t destroyed = 0;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          if (aDestroyed.get() == aCurrentNativeStream.get() ||
              aDestroyed.get() == aNextNativeStream.get()) {
            std::cerr << "cubeb stream " << aDestroyed.get() << " (device "
                      << aDestroyed->GetInputDeviceID()
                      << ") has been destroyed" << std::endl;
            destroyed += 1;
          }
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          // Make sure new stream has input, to prevent from getting a
          // temporary output-only AudioCallbackDriver after closing current
          // native device but before setting a new native input.
          if (aCreated->mHasInput) {
            ASSERT_TRUE(aCreated->mHasOutput);
            newStream = aCreated;
          }
        });

    std::cerr << "Close device " << aCurrentNativeStream->GetInputDeviceID()
              << std::endl;
    DispatchFunction([&] {
      aCurrentNativeTrack->DisconnectDeviceInput();
      aCurrentNativeTrack->Destroy();
    });

    std::cerr << "Wait for the switching" << std::endl;
    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, SwitchNativeInputDevice)"_ns,
        [&] { return destroyed >= 2 && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aCurrentNativeStream = nullptr;
    aNextNativeStream = newStream;

    std::cerr << "Now the native input is device "
              << aNextNativeStream->GetInputDeviceID() << std::endl;
  };

  // Open a DeviceInputConsumerTrack for device 1.
  const CubebUtils::AudioDeviceID device1 = (CubebUtils::AudioDeviceID)1;
  RefPtr<TestDeviceInputConsumerTrack> track1 =
      TestDeviceInputConsumerTrack::Create(graph);
  RefPtr<TestAudioDataListener> listener1 = new TestAudioDataListener(1, false);
  EXPECT_CALL(*listener1, Disconnect);
  track1->ConnectDeviceInput(device1, listener1, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track1->DeviceId().value(), device1);

  auto started =
      InvokeAsync([&] { return graph->NotifyWhenDeviceStarted(nullptr); });

  RefPtr<SmartMockCubebStream> stream1 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream1->mHasInput);
  EXPECT_TRUE(stream1->mHasOutput);
  EXPECT_EQ(stream1->InputChannels(), 1U);
  EXPECT_EQ(stream1->GetInputDeviceID(), device1);
  (void)WaitFor(started);
  std::cerr << "Device " << device1 << " is opened (stream " << stream1.get()
            << ")" << std::endl;

  // Open a DeviceInputConsumerTrack for device 2.
  const CubebUtils::AudioDeviceID device2 = (CubebUtils::AudioDeviceID)2;
  RefPtr<TestDeviceInputConsumerTrack> track2 =
      TestDeviceInputConsumerTrack::Create(graph);
  RefPtr<TestAudioDataListener> listener2 = new TestAudioDataListener(2, false);
  EXPECT_CALL(*listener2, Disconnect).Times(2);
  track2->ConnectDeviceInput(device2, listener2, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track2->DeviceId().value(), device2);

  RefPtr<SmartMockCubebStream> stream2 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_FALSE(stream2->mHasOutput);
  EXPECT_EQ(stream2->InputChannels(), 2U);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);
  std::cerr << "Device " << device2 << " is opened (stream " << stream2.get()
            << ")" << std::endl;

  // Open a DeviceInputConsumerTrack for device 3.
  const CubebUtils::AudioDeviceID device3 = (CubebUtils::AudioDeviceID)3;
  RefPtr<TestDeviceInputConsumerTrack> track3 =
      TestDeviceInputConsumerTrack::Create(graph);
  RefPtr<TestAudioDataListener> listener3 = new TestAudioDataListener(1, false);
  EXPECT_CALL(*listener3, Disconnect).Times(2);
  track3->ConnectDeviceInput(device3, listener3, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track3->DeviceId().value(), device3);

  RefPtr<SmartMockCubebStream> stream3 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream3->mHasInput);
  EXPECT_FALSE(stream3->mHasOutput);
  EXPECT_EQ(stream3->InputChannels(), 1U);
  EXPECT_EQ(stream3->GetInputDeviceID(), device3);
  std::cerr << "Device " << device3 << " is opened (stream " << stream3.get()
            << ")" << std::endl;

  // Close device 1, so the native input device is switched from device 1 to
  // device 2.
  switchNativeDevice(std::move(stream1), track1, stream2, track2);
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_TRUE(stream2->mHasOutput);
  EXPECT_EQ(stream2->InputChannels(), 2U);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);
  {
    NativeInputTrack* native = track2->Graph()->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!!native);
    EXPECT_EQ(native->mDeviceId, device2);
  }

  // Close device 2, so the native input device is switched from device 2 to
  // device 3.
  switchNativeDevice(std::move(stream2), track2, stream3, track3);
  EXPECT_TRUE(stream3->mHasInput);
  EXPECT_TRUE(stream3->mHasOutput);
  EXPECT_EQ(stream3->InputChannels(), 1U);
  EXPECT_EQ(stream3->GetInputDeviceID(), device3);
  {
    NativeInputTrack* native = track3->Graph()->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!!native);
    EXPECT_EQ(native->mDeviceId, device3);
  }

  // Clean up.
  std::cerr << "Close device " << device3 << std::endl;
  DispatchFunction([&] {
    track3->DisconnectDeviceInput();
    track3->Destroy();
  });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream3.get());
  {
    NativeInputTrack* native = graph->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!native);
  }
  std::cerr << "No native input now" << std::endl;
}

#ifdef MOZ_WEBRTC
TEST_WithTailDispatch(TestAudioTrackGraph, ErrorCallback) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;

  // Dummy track to make graph rolling. Add it and remove it to remove the
  // graph from the global hash table and let it shutdown.
  //
  // We open an input through this track so that there's something triggering
  // EnsureNextIteration on the fallback driver after the callback driver has
  // gotten the error, and to check that a replacement cubeb_stream receives
  // output from the graph.
  RefPtr<AudioProcessingTrack> processingTrack;
  RefPtr<AudioInputProcessing> listener;
  auto started = InvokeAsync([&] {
    processingTrack = AudioProcessingTrack::Create(graph);
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(processingTrack, listener);
    processingTrack->SetInputProcessing(listener);
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(processingTrack, listener));
    processingTrack->ConnectDeviceInput(deviceId, listener,
                                        PRINCIPAL_HANDLE_NONE);
    EXPECT_EQ(processingTrack->DeviceId().value(), deviceId);
    processingTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    return graph->NotifyWhenDeviceStarted(nullptr);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  Result<bool, nsresult> rv = WaitFor(started);
  EXPECT_TRUE(rv.unwrapOr(false));

  // Force a cubeb state_callback error and see that we don't crash.
  DispatchFunction([&] { stream->ForceError(); });

  // Wait for the error to take effect, and the driver to restart and receive
  // output.
  bool errored = false;
  MediaEventListener errorListener = stream->ErrorForcedEvent().Connect(
      AbstractThread::GetCurrent(), [&] { errored = true; });
  stream = WaitFor(cubeb->StreamInitEvent());
  WaitFor(stream->FramesVerifiedEvent());
  // The error event is notified after CUBEB_STATE_ERROR triggers other
  // threads to init a new cubeb_stream, so there is a theoretical chance that
  // `errored` might not be set when `stream` is set.
  errorListener.Disconnect();
  EXPECT_TRUE(errored);

  // Clean up.
  DispatchFunction([&] {
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(processingTrack, listener));
    processingTrack->DisconnectDeviceInput();
    processingTrack->Destroy();
  });
  WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph, AudioProcessingTrack) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());
  auto unforcer = WaitFor(cubeb->ForceAudioThread()).unwrap();
  (void)unforcer;

  // Start on a system clock driver, then switch to full-duplex in one go. If we
  // did output-then-full-duplex we'd risk a second NotifyWhenDeviceStarted
  // resolving early after checking the first audio driver only.
  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;

  RefPtr<AudioProcessingTrack> processingTrack;
  RefPtr<ProcessedMediaTrack> outputTrack;
  RefPtr<MediaInputPort> port;
  RefPtr<AudioInputProcessing> listener;
  auto p = InvokeAsync([&] {
    processingTrack = AudioProcessingTrack::Create(graph);
    outputTrack = graph->CreateForwardedInputTrack(MediaSegment::AUDIO);
    outputTrack->QueueSetAutoend(false);
    outputTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    port = outputTrack->AllocateInputPort(processingTrack);
    /* Primary graph: Open Audio Input through SourceMediaTrack */
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(processingTrack, listener);
    processingTrack->SetInputProcessing(listener);
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(processingTrack, listener));
    // Device id does not matter. Ignore.
    processingTrack->ConnectDeviceInput(deviceId, listener,
                                        PRINCIPAL_HANDLE_NONE);
    return graph->NotifyWhenDeviceStarted(nullptr);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  (void)WaitFor(p);

  // Wait for a second worth of audio data. GoFaster is dispatched through a
  // ControlMessage so that it is called in the first audio driver iteration.
  // Otherwise the audio driver might be going very fast while the fallback
  // system clock driver is still in an iteration.
  DispatchFunction([&] {
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<GoFaster>(cubeb));
  });
  uint32_t totalFrames = 0;
  WaitUntil(stream->FramesVerifiedEvent(), [&](uint32_t aFrames) {
    totalFrames += aFrames;
    return totalFrames > static_cast<uint32_t>(graph->GraphRate());
  });
  cubeb->DontGoFaster();

  // Clean up.
  DispatchFunction([&] {
    outputTrack->RemoveAudioOutput((void*)1);
    outputTrack->Destroy();
    port->Destroy();
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(processingTrack, listener));
    processingTrack->DisconnectDeviceInput();
    processingTrack->Destroy();
  });

  uint32_t inputRate = stream->SampleRate();
  uint32_t inputFrequency = stream->InputFrequency();
  uint64_t preSilenceSamples;
  uint32_t estimatedFreq;
  uint32_t nrDiscontinuities;
  std::tie(preSilenceSamples, estimatedFreq, nrDiscontinuities) =
      WaitFor(stream->OutputVerificationEvent());

  EXPECT_EQ(estimatedFreq, inputFrequency);
  std::cerr << "PreSilence: " << preSilenceSamples << std::endl;
  // We buffer 128 frames. See NativeInputTrack::NotifyInputData.
  EXPECT_GE(preSilenceSamples, 128U);
  // If the fallback system clock driver is doing a graph iteration before the
  // first audio driver iteration comes in, that iteration is ignored and
  // results in zeros. It takes one fallback driver iteration *after* the audio
  // driver has started to complete the switch, *usually* resulting two
  // 10ms-iterations of silence; sometimes only one.
  EXPECT_LE(preSilenceSamples, 128U + 2 * inputRate / 100 /* 2*10ms */);
  // The waveform from AudioGenerator starts at 0, but we don't control its
  // ending, so we expect a discontinuity there.
  EXPECT_LE(nrDiscontinuities, 1U);
}

TEST_WithTailDispatch(TestAudioTrackGraph, ReConnectDeviceInput) {
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  // 48k is a native processing rate, and avoids a resampling pass compared
  // to 44.1k. The resampler may add take a few frames to stabilize, which show
  // as unexected discontinuities in the test.
  const TrackRate rate = 48000;
  // Use a drift factor so that we don't dont produce perfect 10ms-chunks.
  // This will exercise whatever buffers are in the audio processing pipeline,
  // and the bookkeeping surrounding them.
  const long step = 10 * rate * 1111 / 1000 / PR_MSEC_PER_SEC;

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1, rate, nullptr,
      AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;

  RefPtr<AudioProcessingTrack> processingTrack;
  RefPtr<ProcessedMediaTrack> outputTrack;
  RefPtr<MediaInputPort> port;
  RefPtr<AudioInputProcessing> listener;
  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    processingTrack = AudioProcessingTrack::Create(graph);
    outputTrack = graph->CreateForwardedInputTrack(MediaSegment::AUDIO);
    outputTrack->QueueSetAutoend(false);
    outputTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    port = outputTrack->AllocateInputPort(processingTrack);

    const int32_t channelCount = 2;
    listener = new AudioInputProcessing(channelCount);
    processingTrack->SetInputProcessing(listener);
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(processingTrack, listener));
    processingTrack->ConnectDeviceInput(deviceId, listener,
                                        PRINCIPAL_HANDLE_NONE);
    MediaEnginePrefs settings;
    settings.mChannels = channelCount;
    settings.mAgcOn = true;  // Turn off pass-through.
    // AGC1 Mode 0 interferes with AudioVerifier's frequency estimation
    // through zero-crossing counts.
    settings.mAgc2Forced = true;
    QueueApplySettings(processingTrack, listener, settings);

    fallbackListener = new OnFallbackListener(processingTrack);
    processingTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);

  while (
      stream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Iterate for a second worth of audio data.
  for (long frames = 0; frames < graph->GraphRate(); frames += step) {
    stream->ManualDataCallback(step);
  }

  // Close the input to see that no asserts go off due to bad state.
  DispatchFunction([&] { processingTrack->DisconnectDeviceInput(); });

  // Dispatch the disconnect message.
  ProcessEventQueue();
  // Run the disconnect message and switch driver.
  auto initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  EXPECT_EQ(stream->ManualDataCallback(0), MockCubebStream::KeepProcessing::No);
  std::tie(stream) = WaitFor(initPromise).unwrap()[0];
  EXPECT_FALSE(stream->mHasInput);

  while (
      stream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the new AudioCallbackDriver to come into effect.
  fallbackListener->Reset();
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Output-only. Iterate for another second before unmuting.
  for (long frames = 0; frames < graph->GraphRate(); frames += step) {
    stream->ManualDataCallback(step);
  }

  // Re-open the input to again see that no asserts go off due to bad state.
  DispatchFunction([&] {
    // Device id does not matter. Ignore.
    processingTrack->ConnectDeviceInput(deviceId, listener,
                                        PRINCIPAL_HANDLE_NONE);
  });
  // Dispatch the connect message.
  ProcessEventQueue();
  // Run the connect message and switch driver.
  initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  EXPECT_EQ(stream->ManualDataCallback(0), MockCubebStream::KeepProcessing::No);
  std::tie(stream) = WaitFor(initPromise).unwrap()[0];
  EXPECT_TRUE(stream->mHasInput);

  while (
      stream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the new AudioCallbackDriver to come into effect.
  fallbackListener->Reset();
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Full-duplex. Iterate for another second before finishing.
  for (long frames = 0; frames < graph->GraphRate(); frames += step) {
    stream->ManualDataCallback(step);
  }

  // Clean up.
  DispatchFunction([&] {
    processingTrack->RemoveListener(fallbackListener);
    outputTrack->RemoveAudioOutput((void*)1);
    outputTrack->Destroy();
    port->Destroy();
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(processingTrack, listener));
    processingTrack->DisconnectDeviceInput();
    processingTrack->Destroy();
  });

  // Dispatch the clean-up messages.
  ProcessEventQueue();
  // Run the clean-up messages and shut down driver.
  EXPECT_EQ(stream->ManualDataCallback(0), MockCubebStream::KeepProcessing::No);

  uint32_t inputFrequency = stream->InputFrequency();
  uint64_t preSilenceSamples;
  uint32_t estimatedFreq;
  uint32_t nrDiscontinuities;
  std::tie(preSilenceSamples, estimatedFreq, nrDiscontinuities) =
      WaitFor(stream->OutputVerificationEvent());

  EXPECT_EQ(estimatedFreq, inputFrequency);
  std::cerr << "PreSilence: " << preSilenceSamples << "\n";
  // We buffer 128 frames. See NativeInputTrack::NotifyInputData.
  // When not in passthrough the AudioInputProcessing packetizer also buffers
  // 10ms of silence, pulled in from NativeInputTrack when being run by the
  // fallback SystemClockDriver.
  EXPECT_EQ(preSilenceSamples, WEBAUDIO_BLOCK_SIZE + rate / 100);
  // The waveform from AudioGenerator starts at 0, but we don't control its
  // ending, so we expect a discontinuity there. Note that this check is only
  // for the waveform on the stream *after* re-opening the input.
  EXPECT_LE(nrDiscontinuities, 1U);
}

// Sum the signal to mono and compute the root mean square, in float32,
// regardless of the input format.
float rmsf32(AudioDataValue* aSamples, uint32_t aChannels, uint32_t aFrames) {
  float downmixed;
  float rms = 0.;
  uint32_t readIdx = 0;
  for (uint32_t i = 0; i < aFrames; i++) {
    downmixed = 0.;
    for (uint32_t j = 0; j < aChannels; j++) {
      downmixed += ConvertAudioSample<float>(aSamples[readIdx++]);
    }
    rms += downmixed * downmixed;
  }
  rms = rms / aFrames;
  return sqrt(rms);
}

TEST_WithTailDispatch(TestAudioTrackGraph, AudioProcessingTrackDisabling) {
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;

  RefPtr<AudioProcessingTrack> processingTrack;
  RefPtr<ProcessedMediaTrack> outputTrack;
  RefPtr<MediaInputPort> port;
  RefPtr<AudioInputProcessing> listener;
  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    processingTrack = AudioProcessingTrack::Create(graph);
    outputTrack = graph->CreateForwardedInputTrack(MediaSegment::AUDIO);
    outputTrack->QueueSetAutoend(false);
    outputTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    port = outputTrack->AllocateInputPort(processingTrack);
    /* Primary graph: Open Audio Input through SourceMediaTrack */
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(processingTrack, listener);
    processingTrack->SetInputProcessing(listener);
    processingTrack->ConnectDeviceInput(deviceId, listener,
                                        PRINCIPAL_HANDLE_NONE);
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(processingTrack, listener));
    fallbackListener = new OnFallbackListener(processingTrack);
    processingTrack->AddListener(fallbackListener);
  });

  ProcessEventQueue();

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);

  while (
      stream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  stream->SetOutputRecordingEnabled(true);

  // Wait for a second worth of audio data.
  const long step = graph->GraphRate() / 100;  // 10ms
  for (long frames = 0; frames < graph->GraphRate(); frames += step) {
    stream->ManualDataCallback(step);
  }

  const uint32_t ITERATION_COUNT = 5;
  uint32_t iterations = ITERATION_COUNT;
  DisabledTrackMode nextMode = DisabledTrackMode::SILENCE_BLACK;
  while (iterations--) {
    // toggle the track enabled mode, wait a second, do this ITERATION_COUNT
    // times
    DispatchFunction([&] {
      processingTrack->SetDisabledTrackMode(nextMode);
      if (nextMode == DisabledTrackMode::SILENCE_BLACK) {
        nextMode = DisabledTrackMode::ENABLED;
      } else {
        nextMode = DisabledTrackMode::SILENCE_BLACK;
      }
    });

    ProcessEventQueue();

    for (long frames = 0; frames < graph->GraphRate(); frames += step) {
      stream->ManualDataCallback(step);
    }
  }

  // Clean up.
  DispatchFunction([&] {
    outputTrack->RemoveAudioOutput((void*)1);
    outputTrack->Destroy();
    port->Destroy();
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(processingTrack, listener));
    processingTrack->RemoveListener(fallbackListener);
    processingTrack->DisconnectDeviceInput();
    processingTrack->Destroy();
  });

  ProcessEventQueue();

  // Close the input and switch driver.
  while (stream->ManualDataCallback(0) != MockCubebStream::KeepProcessing::No) {
    std::cerr << "Waiting for switch...\n";
  }

  uint64_t preSilenceSamples;
  uint32_t estimatedFreq;
  uint32_t nrDiscontinuities;
  std::tie(preSilenceSamples, estimatedFreq, nrDiscontinuities) =
      WaitFor(stream->OutputVerificationEvent());

  auto data = stream->TakeRecordedOutput();

  // check that there is non-silence and silence at the expected time in the
  // stereo recording, while allowing for a bit of scheduling uncertainty, by
  // checking half a second after the theoretical muting/unmuting.
  // non-silence starts around: 0s, 2s, 4s
  // silence start around: 1s, 3s, 5s
  // To detect silence or non-silence, we compute the RMS of the signal for
  // 100ms.
  float noisyTime_s[] = {0.5, 2.5, 4.5};
  float silenceTime_s[] = {1.5, 3.5, 5.5};

  uint32_t rate = graph->GraphRate();
  for (float& time : noisyTime_s) {
    uint32_t startIdx = time * rate * 2 /* stereo */;
    EXPECT_NE(rmsf32(&(data[startIdx]), 2, rate / 10), 0.0);
  }

  for (float& time : silenceTime_s) {
    uint32_t startIdx = time * rate * 2 /* stereo */;
    EXPECT_EQ(rmsf32(&(data[startIdx]), 2, rate / 10), 0.0);
  }
}

TEST_WithTailDispatch(TestAudioTrackGraph, SetRequestedInputChannelCount) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Open a 2-channel native input stream.
  const CubebUtils::AudioDeviceID device1 = (CubebUtils::AudioDeviceID)1;
  RefPtr<AudioProcessingTrack> track1 = AudioProcessingTrack::Create(graph);
  RefPtr<AudioInputProcessing> listener1 = new AudioInputProcessing(2);
  track1->SetInputProcessing(listener1);
  QueueExpectIsPassThrough(track1, listener1);
  track1->GraphImpl()->AppendMessage(
      std::make_unique<StartInputProcessing>(track1, listener1));
  track1->ConnectDeviceInput(device1, listener1, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track1->DeviceId().value(), device1);

  auto started =
      InvokeAsync([&] { return graph->NotifyWhenDeviceStarted(nullptr); });

  RefPtr<SmartMockCubebStream> stream1 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream1->mHasInput);
  EXPECT_TRUE(stream1->mHasOutput);
  EXPECT_EQ(stream1->InputChannels(), 2U);
  EXPECT_EQ(stream1->GetInputDeviceID(), device1);
  (void)WaitFor(started);

  // Open a 1-channel non-native input stream.
  const CubebUtils::AudioDeviceID device2 = (CubebUtils::AudioDeviceID)2;
  RefPtr<AudioProcessingTrack> track2 = AudioProcessingTrack::Create(graph);
  RefPtr<AudioInputProcessing> listener2 = new AudioInputProcessing(1);
  track2->SetInputProcessing(listener2);
  QueueExpectIsPassThrough(track2, listener2);
  track2->GraphImpl()->AppendMessage(
      std::make_unique<StartInputProcessing>(track2, listener2));
  track2->ConnectDeviceInput(device2, listener2, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track2->DeviceId().value(), device2);

  RefPtr<SmartMockCubebStream> stream2 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_FALSE(stream2->mHasOutput);
  EXPECT_EQ(stream2->InputChannels(), 1U);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);

  // Request a new input channel count. This should re-create new input stream
  // accordingly.
  auto setNewChannelCount = [&](const RefPtr<AudioProcessingTrack> aTrack,
                                const RefPtr<AudioInputProcessing>& aListener,
                                RefPtr<SmartMockCubebStream>& aStream,
                                int32_t aChannelCount) {
    bool destroyed = false;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          destroyed = aDestroyed.get() == aStream.get();
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          newStream = aCreated;
        });

    MediaEnginePrefs settings;
    settings.mChannels = aChannelCount;
    QueueApplySettings(aTrack, aListener, settings);

    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, SetRequestedInputChannelCount)"_ns,
        [&] { return destroyed && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aStream = newStream;
  };

  // Set the native input stream's input channel count to 1.
  setNewChannelCount(track1, listener1, stream1, 1);
  EXPECT_TRUE(stream1->mHasInput);
  EXPECT_TRUE(stream1->mHasOutput);
  EXPECT_EQ(stream1->InputChannels(), 1U);
  EXPECT_EQ(stream1->GetInputDeviceID(), device1);

  // Set the non-native input stream's input channel count to 2.
  setNewChannelCount(track2, listener2, stream2, 2);
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_FALSE(stream2->mHasOutput);
  EXPECT_EQ(stream2->InputChannels(), 2U);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);

  // Close the non-native input stream.
  DispatchFunction([&] {
    track2->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(track2, listener2));
    track2->DisconnectDeviceInput();
    track2->Destroy();
  });
  RefPtr<SmartMockCubebStream> destroyed = WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyed.get(), stream2.get());

  // Close the native input stream.
  DispatchFunction([&] {
    track1->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(track1, listener1));
    track1->DisconnectDeviceInput();
    track1->Destroy();
  });
  destroyed = WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyed.get(), stream1.get());
}

// The native audio stream (a.k.a. GraphDriver) and the non-native audio stream
// should always be the same as the max requested input channel of its paired
// AudioProcessingTracks. This test checks if the audio stream paired with the
// AudioProcessingTrack will follow the max requested input channel or not.
//
// This test is pretty similar to RestartAudioIfMaxChannelCountChanged above,
// which makes sure the related DeviceInputTrack operations for the test here
// works correctly. Instead of using a test-only AudioDataListener, we use
// AudioInputProcessing here to simulate the real world use case.
TEST_WithTailDispatch(TestAudioTrackGraph,
                      RestartAudioIfProcessingMaxChannelCountChanged) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());
  auto unforcer = WaitFor(cubeb->ForceAudioThread()).unwrap();
  (void)unforcer;

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Request a new input channel count and expect to have a new stream.
  auto setNewChannelCount = [&](const RefPtr<AudioProcessingTrack>& aTrack,
                                const RefPtr<AudioInputProcessing>& aListener,
                                RefPtr<SmartMockCubebStream>& aStream,
                                int32_t aChannelCount) {
    ASSERT_TRUE(!!aTrack);
    ASSERT_TRUE(!!aListener);
    ASSERT_TRUE(!!aStream);
    ASSERT_TRUE(aStream->mHasInput);
    ASSERT_NE(aChannelCount, 0);

    bool destroyed = false;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          destroyed = aDestroyed.get() == aStream.get();
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          newStream = aCreated;
        });

    MediaEnginePrefs settings;
    settings.mChannels = aChannelCount;
    QueueApplySettings(aTrack, aListener, settings);

    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, RestartAudioIfProcessingMaxChannelCountChanged) #1"_ns,
        [&] { return destroyed && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aStream = newStream;
  };

  // Open a new track and expect to have a new stream.
  auto openTrack = [&](RefPtr<SmartMockCubebStream>& aCurrentStream,
                       RefPtr<AudioProcessingTrack>& aTrack,
                       RefPtr<AudioInputProcessing>& aListener,
                       CubebUtils::AudioDeviceID aDevice,
                       uint32_t aChannelCount) {
    ASSERT_TRUE(!!aCurrentStream);
    ASSERT_TRUE(aCurrentStream->mHasInput);
    ASSERT_TRUE(aChannelCount > aCurrentStream->InputChannels());
    ASSERT_TRUE(!aTrack);
    ASSERT_TRUE(!aListener);

    bool destroyed = false;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          destroyed = aDestroyed.get() == aCurrentStream.get();
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          newStream = aCreated;
        });

    aTrack = AudioProcessingTrack::Create(graph);
    aListener = new AudioInputProcessing(aChannelCount);
    aTrack->SetInputProcessing(aListener);
    QueueExpectIsPassThrough(aTrack, aListener);
    aTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(aTrack, aListener));

    DispatchFunction([&] {
      aTrack->ConnectDeviceInput(aDevice, aListener, PRINCIPAL_HANDLE_NONE);
    });

    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, RestartAudioIfProcessingMaxChannelCountChanged) #2"_ns,
        [&] { return destroyed && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aCurrentStream = newStream;
  };

  // Test for the native input device first then non-native device. The
  // non-native device will be destroyed before the native device in case of
  // causing a native-device-switching.

  // Test for the native device.
  const CubebUtils::AudioDeviceID nativeDevice = (CubebUtils::AudioDeviceID)1;
  RefPtr<AudioProcessingTrack> track1;
  RefPtr<AudioInputProcessing> listener1;
  RefPtr<SmartMockCubebStream> nativeStream;
  RefPtr<AudioProcessingTrack> track2;
  RefPtr<AudioInputProcessing> listener2;
  {
    // Open a 1-channel AudioProcessingTrack for the native device.
    track1 = AudioProcessingTrack::Create(graph);
    listener1 = new AudioInputProcessing(1);
    track1->SetInputProcessing(listener1);
    QueueExpectIsPassThrough(track1, listener1);
    track1->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(track1, listener1));
    track1->ConnectDeviceInput(nativeDevice, listener1, PRINCIPAL_HANDLE_NONE);
    EXPECT_EQ(track1->DeviceId().value(), nativeDevice);

    auto started =
        InvokeAsync([&] { return graph->NotifyWhenDeviceStarted(nullptr); });

    nativeStream = WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(nativeStream->mHasInput);
    EXPECT_TRUE(nativeStream->mHasOutput);
    EXPECT_EQ(nativeStream->InputChannels(), 1U);
    EXPECT_EQ(nativeStream->GetInputDeviceID(), nativeDevice);
    (void)WaitFor(started);

    // Open a 2-channel AudioProcessingTrack for the native device and wait for
    // a new driver since the max-channel for the native device becomes 2 now.
    openTrack(nativeStream, track2, listener2, nativeDevice, 2);
    EXPECT_EQ(nativeStream->InputChannels(), 2U);

    // Set the second AudioProcessingTrack for the native device to 1-channel
    // and wait for a new driver since the max-channel for the native device
    // becomes 1 now.
    setNewChannelCount(track2, listener2, nativeStream, 1);
    EXPECT_EQ(nativeStream->InputChannels(), 1U);

    // Set the first AudioProcessingTrack for the native device to 2-channel and
    // wait for a new driver since the max input channel for the native device
    // becomes 2 now.
    setNewChannelCount(track1, listener1, nativeStream, 2);
    EXPECT_EQ(nativeStream->InputChannels(), 2U);
  }

  // Test for the non-native device.
  {
    const CubebUtils::AudioDeviceID nonNativeDevice =
        (CubebUtils::AudioDeviceID)2;

    // Open a 1-channel AudioProcessingTrack for the non-native device.
    RefPtr<AudioProcessingTrack> track3 = AudioProcessingTrack::Create(graph);
    RefPtr<AudioInputProcessing> listener3 = new AudioInputProcessing(1);
    track3->SetInputProcessing(listener3);
    QueueExpectIsPassThrough(track3, listener3);
    track3->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(track3, listener3));
    track3->ConnectDeviceInput(nonNativeDevice, listener3,
                               PRINCIPAL_HANDLE_NONE);
    EXPECT_EQ(track3->DeviceId().value(), nonNativeDevice);

    RefPtr<SmartMockCubebStream> nonNativeStream =
        WaitFor(cubeb->StreamInitEvent());
    EXPECT_TRUE(nonNativeStream->mHasInput);
    EXPECT_FALSE(nonNativeStream->mHasOutput);
    EXPECT_EQ(nonNativeStream->InputChannels(), 1U);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), nonNativeDevice);

    // Open a 2-channel AudioProcessingTrack for the non-native device and wait
    // for a new stream since the max-channel for the non-native device becomes
    // 2 now.
    RefPtr<AudioProcessingTrack> track4;
    RefPtr<AudioInputProcessing> listener4;
    openTrack(nonNativeStream, track4, listener4, nonNativeDevice, 2);
    EXPECT_EQ(nonNativeStream->InputChannels(), 2U);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), nonNativeDevice);

    // Set the second AudioProcessingTrack for the non-native to 1-channel and
    // wait for a new driver since the max-channel for the non-native device
    // becomes 1 now.
    setNewChannelCount(track4, listener4, nonNativeStream, 1);
    EXPECT_EQ(nonNativeStream->InputChannels(), 1U);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), nonNativeDevice);

    // Set the first AudioProcessingTrack for the non-native device to 2-channel
    // and wait for a new driver since the max input channel for the non-native
    // device becomes 2 now.
    setNewChannelCount(track3, listener3, nonNativeStream, 2);
    EXPECT_EQ(nonNativeStream->InputChannels(), 2U);
    EXPECT_EQ(nonNativeStream->GetInputDeviceID(), nonNativeDevice);

    // Close the second AudioProcessingTrack (1-channel) for the non-native
    // device then the first one (2-channel) so we won't result in another
    // stream creation.
    DispatchFunction([&] {
      track4->GraphImpl()->AppendMessage(
          std::make_unique<StopInputProcessing>(track4, listener4));
      track4->DisconnectDeviceInput();
      track4->Destroy();
    });
    DispatchFunction([&] {
      track3->GraphImpl()->AppendMessage(
          std::make_unique<StopInputProcessing>(track3, listener3));
      track3->DisconnectDeviceInput();
      track3->Destroy();
    });
    RefPtr<SmartMockCubebStream> destroyedStream =
        WaitFor(cubeb->StreamDestroyEvent());
    EXPECT_EQ(destroyedStream.get(), nonNativeStream.get());
  }

  // Tear down for the native device.
  {
    // Close the second AudioProcessingTrack (1-channel) for the native device
    // then the first one (2-channel) so we won't have driver switching.
    DispatchFunction([&] {
      track2->GraphImpl()->AppendMessage(
          std::make_unique<StopInputProcessing>(track2, listener2));
      track2->DisconnectDeviceInput();
      track2->Destroy();
    });
    DispatchFunction([&] {
      track1->GraphImpl()->AppendMessage(
          std::make_unique<StopInputProcessing>(track1, listener1));
      track1->DisconnectDeviceInput();
      track1->Destroy();
    });
    RefPtr<SmartMockCubebStream> destroyedStream =
        WaitFor(cubeb->StreamDestroyEvent());
    EXPECT_EQ(destroyedStream.get(), nativeStream.get());
  }
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      SetInputChannelCountBeforeAudioCallbackDriver) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Set the input channel count of AudioInputProcessing, which will force
  // MediaTrackGraph to re-evaluate input device, when the MediaTrackGraph is
  // driven by the SystemClockDriver.

  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  RefPtr<AudioProcessingTrack> track;
  RefPtr<AudioInputProcessing> listener;
  DispatchFunction([&] {
    track = AudioProcessingTrack::Create(graph);
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(track, listener);
    track->SetInputProcessing(listener);

    MediaEnginePrefs settings;
    settings.mChannels = 1;
    QueueApplySettings(track, listener, settings);
  });

  // No AudioCallbackDriver is created yet: the track has neither an input
  // device nor an audio output.

  // Open a full-duplex AudioCallbackDriver.
  RefPtr<MediaInputPort> port;
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(track, listener));
    track->ConnectDeviceInput(deviceId, listener, PRINCIPAL_HANDLE_NONE);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_TRUE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), 1U);

  (void)WaitFor(
      InvokeAsync([&] { return graph->NotifyWhenDeviceStarted(nullptr); }));

  // Clean up.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(track, listener));
    track->DisconnectDeviceInput();
    track->Destroy();
  });
  (void)WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      StartAudioDeviceBeforeStartingAudioProcessing) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Create a duplex AudioCallbackDriver
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  RefPtr<AudioProcessingTrack> track;
  RefPtr<AudioInputProcessing> listener;
  DispatchFunction([&] {
    track = AudioProcessingTrack::Create(graph);
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(track, listener);
    track->SetInputProcessing(listener);
    // Start audio device without starting audio processing.
    track->ConnectDeviceInput(deviceId, listener, PRINCIPAL_HANDLE_NONE);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_TRUE(stream->mHasOutput);

  // Wait for a second to make sure audio output callback has been fired.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(std::make_unique<GoFaster>(cubeb));
  });
  {
    uint32_t totalFrames = 0;
    WaitUntil(stream->FramesProcessedEvent(), [&](uint32_t aFrames) {
      totalFrames += aFrames;
      return totalFrames > static_cast<uint32_t>(graph->GraphRate());
    });
  }
  cubeb->DontGoFaster();

  // Start the audio processing.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(track, listener));
  });

  // Wait for a second to make sure audio output callback has been fired.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(std::make_unique<GoFaster>(cubeb));
  });
  {
    uint32_t totalFrames = 0;
    WaitUntil(stream->FramesProcessedEvent(), [&](uint32_t aFrames) {
      totalFrames += aFrames;
      return totalFrames > static_cast<uint32_t>(graph->GraphRate());
    });
  }
  cubeb->DontGoFaster();

  // Clean up.
  DispatchFunction([&] {
    track->DisconnectDeviceInput();
    track->Destroy();
  });
  (void)WaitFor(cubeb->StreamDestroyEvent());
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      StopAudioProcessingBeforeStoppingAudioDevice) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Create a duplex AudioCallbackDriver
  const CubebUtils::AudioDeviceID deviceId = (CubebUtils::AudioDeviceID)1;
  RefPtr<AudioProcessingTrack> track;
  RefPtr<AudioInputProcessing> listener;
  DispatchFunction([&] {
    track = AudioProcessingTrack::Create(graph);
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(track, listener);
    track->SetInputProcessing(listener);
    track->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(track, listener));
    track->ConnectDeviceInput(deviceId, listener, PRINCIPAL_HANDLE_NONE);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_TRUE(stream->mHasOutput);

  // Wait for a second to make sure audio output callback has been fired.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(std::make_unique<GoFaster>(cubeb));
  });
  {
    uint32_t totalFrames = 0;
    WaitUntil(stream->FramesProcessedEvent(), [&](uint32_t aFrames) {
      totalFrames += aFrames;
      return totalFrames > static_cast<uint32_t>(graph->GraphRate());
    });
  }
  cubeb->DontGoFaster();

  // Stop the audio processing
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(track, listener));
  });

  // Wait for a second to make sure audio output callback has been fired.
  DispatchFunction([&] {
    track->GraphImpl()->AppendMessage(std::make_unique<GoFaster>(cubeb));
  });
  {
    uint32_t totalFrames = 0;
    WaitUntil(stream->FramesProcessedEvent(), [&](uint32_t aFrames) {
      totalFrames += aFrames;
      return totalFrames > static_cast<uint32_t>(graph->GraphRate());
    });
  }
  cubeb->DontGoFaster();

  // Clean up.
  DispatchFunction([&] {
    track->DisconnectDeviceInput();
    track->Destroy();
  });
  (void)WaitFor(cubeb->StreamDestroyEvent());
}

// This test is pretty similar to SwitchNativeInputDevice above, which makes
// sure the related DeviceInputTrack operations for the test here works
// correctly. Instead of using a test-only DeviceInputTrack consumer, we use
// AudioProcessingTrack here to simulate the real world use case.
TEST_WithTailDispatch(TestAudioTrackGraph, SwitchNativeAudioProcessingTrack) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  auto switchNativeDevice = [&](RefPtr<SmartMockCubebStream>&&
                                    aCurrentNativeStream,
                                RefPtr<AudioProcessingTrack>&
                                    aCurrentNativeTrack,
                                RefPtr<AudioInputProcessing>&
                                    aCurrentNativeListener,
                                RefPtr<SmartMockCubebStream>& aNextNativeStream,
                                RefPtr<AudioProcessingTrack>&
                                    aNextNativeTrack) {
    ASSERT_TRUE(aCurrentNativeStream->mHasInput);
    ASSERT_TRUE(aCurrentNativeStream->mHasOutput);
    ASSERT_TRUE(aNextNativeStream->mHasInput);
    ASSERT_FALSE(aNextNativeStream->mHasOutput);

    std::cerr << "Switching native input from device "
              << aCurrentNativeStream->GetInputDeviceID() << " to "
              << aNextNativeStream->GetInputDeviceID() << std::endl;

    uint32_t destroyed = 0;
    MediaEventListener destroyListener = cubeb->StreamDestroyEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aDestroyed) {
          if (aDestroyed.get() == aCurrentNativeStream.get() ||
              aDestroyed.get() == aNextNativeStream.get()) {
            std::cerr << "cubeb stream " << aDestroyed.get() << " (device "
                      << aDestroyed->GetInputDeviceID()
                      << ") has been destroyed" << std::endl;
            destroyed += 1;
          }
        });

    RefPtr<SmartMockCubebStream> newStream;
    MediaEventListener restartListener = cubeb->StreamInitEvent().Connect(
        AbstractThread::GetCurrent(),
        [&](const RefPtr<SmartMockCubebStream>& aCreated) {
          // Make sure new stream has input, to prevent from getting a
          // temporary output-only AudioCallbackDriver after closing current
          // native device but before setting a new native input.
          if (aCreated->mHasInput) {
            ASSERT_TRUE(aCreated->mHasOutput);
            newStream = aCreated;
          }
        });

    std::cerr << "Close device " << aCurrentNativeStream->GetInputDeviceID()
              << std::endl;
    DispatchFunction([&] {
      aCurrentNativeTrack->GraphImpl()->AppendMessage(
          std::make_unique<StopInputProcessing>(aCurrentNativeTrack,
                                                aCurrentNativeListener));
      aCurrentNativeTrack->DisconnectDeviceInput();
      aCurrentNativeTrack->Destroy();
    });

    std::cerr << "Wait for the switching" << std::endl;
    SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
        "TEST_WithTailDispatch(TestAudioTrackGraph, SwitchNativeAudioProcessingTrack)"_ns,
        [&] { return destroyed >= 2 && newStream; });

    destroyListener.Disconnect();
    restartListener.Disconnect();

    aCurrentNativeStream = nullptr;
    aNextNativeStream = newStream;

    std::cerr << "Now the native input is device "
              << aNextNativeStream->GetInputDeviceID() << std::endl;
  };

  // Open a AudioProcessingTrack for device 1.
  const CubebUtils::AudioDeviceID device1 = (CubebUtils::AudioDeviceID)1;
  RefPtr<AudioProcessingTrack> track1 = AudioProcessingTrack::Create(graph);
  RefPtr<AudioInputProcessing> listener1 = new AudioInputProcessing(1);
  track1->SetInputProcessing(listener1);
  QueueExpectIsPassThrough(track1, listener1);
  track1->GraphImpl()->AppendMessage(
      std::make_unique<StartInputProcessing>(track1, listener1));
  track1->ConnectDeviceInput(device1, listener1, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track1->DeviceId().value(), device1);

  auto started =
      InvokeAsync([&] { return graph->NotifyWhenDeviceStarted(nullptr); });

  RefPtr<SmartMockCubebStream> stream1 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream1->mHasInput);
  EXPECT_TRUE(stream1->mHasOutput);
  EXPECT_EQ(stream1->InputChannels(), 1U);
  EXPECT_EQ(stream1->GetInputDeviceID(), device1);
  (void)WaitFor(started);
  std::cerr << "Device " << device1 << " is opened (stream " << stream1.get()
            << ")" << std::endl;

  // Open a AudioProcessingTrack for device 2.
  const CubebUtils::AudioDeviceID device2 = (CubebUtils::AudioDeviceID)2;
  RefPtr<AudioProcessingTrack> track2 = AudioProcessingTrack::Create(graph);
  RefPtr<AudioInputProcessing> listener2 = new AudioInputProcessing(2);
  track2->SetInputProcessing(listener2);
  QueueExpectIsPassThrough(track2, listener2);
  track2->GraphImpl()->AppendMessage(
      std::make_unique<StartInputProcessing>(track2, listener2));
  track2->ConnectDeviceInput(device2, listener2, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track2->DeviceId().value(), device2);

  RefPtr<SmartMockCubebStream> stream2 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_FALSE(stream2->mHasOutput);
  EXPECT_EQ(stream2->InputChannels(), 2U);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);
  std::cerr << "Device " << device2 << " is opened (stream " << stream2.get()
            << ")" << std::endl;

  // Open a AudioProcessingTrack for device 3.
  const CubebUtils::AudioDeviceID device3 = (CubebUtils::AudioDeviceID)3;
  RefPtr<AudioProcessingTrack> track3 = AudioProcessingTrack::Create(graph);
  RefPtr<AudioInputProcessing> listener3 = new AudioInputProcessing(1);
  track3->SetInputProcessing(listener3);
  QueueExpectIsPassThrough(track3, listener3);
  track3->GraphImpl()->AppendMessage(
      std::make_unique<StartInputProcessing>(track3, listener3));
  track3->ConnectDeviceInput(device3, listener3, PRINCIPAL_HANDLE_NONE);
  EXPECT_EQ(track3->DeviceId().value(), device3);

  RefPtr<SmartMockCubebStream> stream3 = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream3->mHasInput);
  EXPECT_FALSE(stream3->mHasOutput);
  EXPECT_EQ(stream3->InputChannels(), 1U);
  EXPECT_EQ(stream3->GetInputDeviceID(), device3);
  std::cerr << "Device " << device3 << " is opened (stream " << stream3.get()
            << ")" << std::endl;

  // Close device 1, so the native input device is switched from device 1 to
  // device 2.
  switchNativeDevice(std::move(stream1), track1, listener1, stream2, track2);
  EXPECT_TRUE(stream2->mHasInput);
  EXPECT_TRUE(stream2->mHasOutput);
  EXPECT_EQ(stream2->InputChannels(), 2U);
  EXPECT_EQ(stream2->GetInputDeviceID(), device2);
  {
    NativeInputTrack* native = track2->Graph()->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!!native);
    EXPECT_EQ(native->mDeviceId, device2);
  }

  // Close device 2, so the native input device is switched from device 2 to
  // device 3.
  switchNativeDevice(std::move(stream2), track2, listener2, stream3, track3);
  EXPECT_TRUE(stream3->mHasInput);
  EXPECT_TRUE(stream3->mHasOutput);
  EXPECT_EQ(stream3->InputChannels(), 1U);
  EXPECT_EQ(stream3->GetInputDeviceID(), device3);
  {
    NativeInputTrack* native = track3->Graph()->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!!native);
    EXPECT_EQ(native->mDeviceId, device3);
  }

  // Clean up.
  std::cerr << "Close device " << device3 << std::endl;
  DispatchFunction([&] {
    track3->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(track3, listener3));
    track3->DisconnectDeviceInput();
    track3->Destroy();
  });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream3.get());
  {
    NativeInputTrack* native = graph->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!native);
  }
  std::cerr << "No native input now" << std::endl;
}

void TestCrossGraphPort(uint32_t aInputRate, uint32_t aOutputRate,
                        float aDriftFactor, uint32_t aRunTimeSeconds = 10,
                        uint32_t aNumExpectedUnderruns = 0) {
  std::cerr << "TestCrossGraphPort input: " << aInputRate
            << ", output: " << aOutputRate << ", driftFactor: " << aDriftFactor
            << std::endl;

  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  /* Primary graph: Create the graph. */
  MediaTrackGraph* primary = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER,
      /*Window ID*/ 1, aInputRate, nullptr, AbstractThread::MainThread());

  /* Partner graph: Create the graph. */
  MediaTrackGraph* partner = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1, aOutputRate,
      /*OutputDeviceID*/ reinterpret_cast<cubeb_devid>(1),
      AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID inputDeviceId = (CubebUtils::AudioDeviceID)1;

  RefPtr<AudioProcessingTrack> processingTrack;
  RefPtr<AudioInputProcessing> listener;
  RefPtr<OnFallbackListener> primaryFallbackListener;
  DispatchFunction([&] {
    /* Primary graph: Create input track and open it */
    processingTrack = AudioProcessingTrack::Create(primary);
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(processingTrack, listener);
    processingTrack->SetInputProcessing(listener);
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(processingTrack, listener));
    processingTrack->ConnectDeviceInput(inputDeviceId, listener,
                                        PRINCIPAL_HANDLE_NONE);
    primaryFallbackListener = new OnFallbackListener(processingTrack);
    processingTrack->AddListener(primaryFallbackListener);
  });

  RefPtr<SmartMockCubebStream> inputStream = WaitFor(cubeb->StreamInitEvent());

  while (
      inputStream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the primary AudioCallbackDriver to come into effect.
  while (primaryFallbackListener->OnFallback()) {
    EXPECT_EQ(inputStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  RefPtr<CrossGraphTransmitter> transmitter;
  RefPtr<MediaInputPort> port;
  RefPtr<CrossGraphReceiver> receiver;
  RefPtr<OnFallbackListener> partnerFallbackListener;
  DispatchFunction([&] {
    processingTrack->RemoveListener(primaryFallbackListener);

    /* Partner graph: Create CrossGraphReceiver */
    receiver = partner->CreateCrossGraphReceiver(primary->GraphRate());

    /* Primary graph: Create CrossGraphTransmitter */
    transmitter = primary->CreateCrossGraphTransmitter(receiver);

    /* How the input track connects to another ProcessedMediaTrack.
     * Check in MediaManager how it is connected to AudioStreamTrack. */
    port = transmitter->AllocateInputPort(processingTrack);
    receiver->AddAudioOutput((void*)1, partner->PrimaryOutputDeviceID(), 0);

    partnerFallbackListener = new OnFallbackListener(receiver);
    receiver->AddListener(partnerFallbackListener);
  });

  RefPtr<SmartMockCubebStream> partnerStream =
      WaitFor(cubeb->StreamInitEvent());

  while (
      partnerStream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Process the CrossGraphTransmitter on the primary graph.
  EXPECT_EQ(inputStream->ManualDataCallback(0),
            MockCubebStream::KeepProcessing::Yes);

  // Wait for the partner AudioCallbackDriver to come into effect.
  while (partnerFallbackListener->OnFallback()) {
    EXPECT_EQ(partnerStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  DispatchFunction([&] { receiver->RemoveListener(partnerFallbackListener); });
  ProcessEventQueue();

  nsIThread* currentThread = NS_GetCurrentThread();
  cubeb_state inputState = CUBEB_STATE_STARTED;
  MediaEventListener inputStateListener = inputStream->StateEvent().Connect(
      currentThread, [&](cubeb_state aState) { inputState = aState; });
  cubeb_state partnerState = CUBEB_STATE_STARTED;
  MediaEventListener partnerStateListener = partnerStream->StateEvent().Connect(
      currentThread, [&](cubeb_state aState) { partnerState = aState; });

  const media::TimeUnit runtime = media::TimeUnit::FromSeconds(aRunTimeSeconds);
  // 10ms per iteration.
  const media::TimeUnit step = media::TimeUnit::FromSeconds(0.01);
  {
    media::TimeUnit pos = media::TimeUnit::Zero();
    long inputFrames = 0;
    long outputFrames = 0;
    while (pos < runtime) {
      pos += step;
      const long newInputFrames = pos.ToTicksAtRate(aInputRate);
      const long newOutputFrames =
          (pos.MultDouble(aDriftFactor)).ToTicksAtRate(aOutputRate);
      EXPECT_EQ(inputStream->ManualDataCallback(newInputFrames - inputFrames),
                MockCubebStream::KeepProcessing::Yes);
      EXPECT_EQ(
          partnerStream->ManualDataCallback(newOutputFrames - outputFrames),
          MockCubebStream::KeepProcessing::Yes);

      inputFrames = newInputFrames;
      outputFrames = newOutputFrames;
    }
  }

  DispatchFunction([&] {
    // Clean up on MainThread
    receiver->RemoveAudioOutput((void*)1);
    receiver->Destroy();
    transmitter->Destroy();
    port->Destroy();
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(processingTrack, listener));
    processingTrack->DisconnectDeviceInput();
    processingTrack->Destroy();
  });

  ProcessEventQueue();

  EXPECT_EQ(inputStream->ManualDataCallback(128),
            MockCubebStream::KeepProcessing::No);
  EXPECT_EQ(partnerStream->ManualDataCallback(128),
            MockCubebStream::KeepProcessing::No);

  uint32_t inputFrequency = inputStream->InputFrequency();

  uint64_t preSilenceSamples;
  float estimatedFreq;
  uint32_t nrDiscontinuities;
  std::tie(preSilenceSamples, estimatedFreq, nrDiscontinuities) =
      WaitFor(partnerStream->OutputVerificationEvent());

  EXPECT_NEAR(estimatedFreq, inputFrequency / aDriftFactor, 5);
  // Note that pre-silence is in the output rate. The buffering is on the input
  // side. There is one block buffered in NativeInputTrack. Then
  // AudioDriftCorrection sets its pre-buffering so that *after* the first
  // resample of real input data, the buffer contains enough data to match the
  // desired level, which is initially 50ms. I.e. silence = buffering -
  // inputStep + outputStep. Note that the steps here are rounded up to block
  // size.
  const media::TimeUnit inputBuffering(WEBAUDIO_BLOCK_SIZE, aInputRate);
  const media::TimeUnit buffering =
      media::TimeUnit::FromSeconds(0.05).ToBase(aInputRate);
  const media::TimeUnit inputStepSize(
      MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(
          step.ToTicksAtRate(aInputRate)),
      aInputRate);
  const media::TimeUnit outputStepSize =
      media::TimeUnit(MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(
                          step.ToBase(aOutputRate)
                              .MultDouble(aDriftFactor)
                              .ToTicksAtRate(aOutputRate)),
                      aOutputRate)
          .ToBase(aInputRate);
  const uint32_t expectedPreSilence =
      (outputStepSize + inputBuffering + buffering - inputStepSize)
          .ToBase(aInputRate)
          .ToBase<media::TimeUnit::CeilingPolicy>(aOutputRate)
          .ToTicksAtRate(aOutputRate);
  // Use a margin of 0.1% of the expected pre-silence, since the resampler is
  // adapting to drift and will process the pre-silence frames. Because of
  // rounding errors, we don't use a margin lower than 1.
  const uint32_t margin = std::max(1U, expectedPreSilence / 1000);
  EXPECT_NEAR(preSilenceSamples, expectedPreSilence, margin);
  // The waveform from AudioGenerator starts at 0, but we don't control its
  // ending, so we expect a discontinuity there. For each expected underrun
  // there could be an additional 2 discontinuities (start and end of the silent
  // period).
  EXPECT_LE(nrDiscontinuities, 1U + 2 * aNumExpectedUnderruns);

  SpinEventLoopUntil("streams have stopped"_ns, [&] {
    return inputState == CUBEB_STATE_STOPPED &&
           partnerState == CUBEB_STATE_STOPPED;
  });
  inputStateListener.Disconnect();
  partnerStateListener.Disconnect();
}

TEST_WithTailDispatch(TestAudioTrackGraph, CrossGraphPort) {
  TestCrossGraphPort(44100, 44100, 1);
  TestCrossGraphPort(44100, 44100, 1.006);
  TestCrossGraphPort(44100, 44100, 0.994);

  TestCrossGraphPort(48000, 44100, 1);
  TestCrossGraphPort(48000, 44100, 1.006);
  TestCrossGraphPort(48000, 44100, 0.994);

  TestCrossGraphPort(44100, 48000, 1);
  TestCrossGraphPort(44100, 48000, 1.006);
  TestCrossGraphPort(44100, 48000, 0.994);

  TestCrossGraphPort(52110, 17781, 1);
  TestCrossGraphPort(52110, 17781, 1.006);
  TestCrossGraphPort(52110, 17781, 0.994);
}

TEST_WithTailDispatch(TestAudioTrackGraph, CrossGraphPortUnderrun) {
  TestCrossGraphPort(44100, 44100, 1.01, 30, 1);
  TestCrossGraphPort(44100, 44100, 1.03, 40, 3);

  TestCrossGraphPort(48000, 44100, 1.01, 30, 1);
  TestCrossGraphPort(48000, 44100, 1.03, 40, 3);

  TestCrossGraphPort(44100, 48000, 1.01, 30, 1);
  TestCrossGraphPort(44100, 48000, 1.03, 40, 3);

  TestCrossGraphPort(52110, 17781, 1.01, 30, 1);
  TestCrossGraphPort(52110, 17781, 1.03, 40, 3);
}

TEST_WithTailDispatch(TestAudioTrackGraph, SecondaryOutputDevice) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const TrackRate primaryRate = 48000;
  const TrackRate secondaryRate = 44100;  // for secondary output device

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER,
      /*Window ID*/ 1, primaryRate, nullptr, AbstractThread::MainThread());

  RefPtr<AudioProcessingTrack> processingTrack;
  RefPtr<AudioInputProcessing> listener;
  DispatchFunction([&] {
    /* Create an input track and connect it to a device */
    processingTrack = AudioProcessingTrack::Create(graph);
    listener = new AudioInputProcessing(2);
    QueueExpectIsPassThrough(processingTrack, listener);
    processingTrack->SetInputProcessing(listener);
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StartInputProcessing>(processingTrack, listener));
    processingTrack->ConnectDeviceInput(nullptr, listener,
                                        PRINCIPAL_HANDLE_NONE);
  });
  RefPtr<SmartMockCubebStream> primaryStream =
      WaitFor(cubeb->StreamInitEvent());

  const void* secondaryDeviceID = CubebUtils::AudioDeviceID(2);
  DispatchFunction([&] {
    processingTrack->AddAudioOutput(nullptr, secondaryDeviceID, secondaryRate);
    processingTrack->SetAudioOutputVolume(nullptr, 0.f);
  });
  RefPtr<SmartMockCubebStream> secondaryStream =
      WaitFor(cubeb->StreamInitEvent());
  EXPECT_EQ(secondaryStream->GetOutputDeviceID(), secondaryDeviceID);
  EXPECT_EQ(static_cast<TrackRate>(secondaryStream->SampleRate()),
            secondaryRate);

  nsIThread* currentThread = NS_GetCurrentThread();
  uint32_t audioFrames = 0;  // excludes pre-silence
  MediaEventListener audioListener =
      secondaryStream->FramesVerifiedEvent().Connect(
          currentThread, [&](uint32_t aFrames) { audioFrames += aFrames; });

  // Wait for 100ms of pre-silence to verify that SetAudioOutputVolume() is
  // effective.
  uint32_t processedFrames = 0;
  WaitUntil(secondaryStream->FramesProcessedEvent(), [&](uint32_t aFrames) {
    processedFrames += aFrames;
    return processedFrames > static_cast<uint32_t>(secondaryRate / 10);
  });
  EXPECT_EQ(audioFrames, 0U) << "audio frames at zero volume";

  secondaryStream->SetOutputRecordingEnabled(true);
  DispatchFunction(
      [&] { processingTrack->SetAudioOutputVolume(nullptr, 1.f); });

  // Wait for enough audio after initial silence to check the frequency.
  SpinEventLoopUntil("200ms of audio"_ns, [&] {
    return audioFrames > static_cast<uint32_t>(secondaryRate / 5);
  });
  audioListener.Disconnect();

  // Stop recording now so as not to record the discontinuity when the
  // CrossGraphReceiver is removed from the secondary graph before its
  // AudioCallbackDriver is stopped.
  secondaryStream->SetOutputRecordingEnabled(false);

  DispatchFunction([&] { processingTrack->RemoveAudioOutput(nullptr); });
  WaitFor(secondaryStream->OutputVerificationEvent());
  // The frequency from OutputVerificationEvent() is estimated by
  // AudioVerifier from a zero-crossing count.  When the discontinuity from
  // the volume change is resampled, the discontinuity presents as
  // oscillations, which increase the zero-crossing count and corrupt the
  // frequency estimate.  Trim off sufficient leading from the output to
  // remove this discontinuity.
  uint32_t channelCount = secondaryStream->OutputChannels();
  nsTArray<AudioDataValue> output = secondaryStream->TakeRecordedOutput();
  size_t leadingIndex = 0;
  for (; leadingIndex < output.Length() && output[leadingIndex] == 0.f;
       leadingIndex += channelCount) {
  };
  leadingIndex += 10 * channelCount;  // skip discontinuity oscillations
  EXPECT_LT(leadingIndex, output.Length());
  auto trimmed = Span(output).From(std::min(leadingIndex, output.Length()));
  size_t frameCount = trimmed.Length() / channelCount;
  uint32_t inputFrequency = primaryStream->InputFrequency();
  AudioVerifier<AudioDataValue> verifier(secondaryRate, inputFrequency);
  verifier.AppendDataInterleaved(trimmed.Elements(), frameCount, channelCount);
  EXPECT_EQ(verifier.EstimatedFreq(), inputFrequency);
  // AudioVerifier considers the previous value before the initial sample to
  // be zero and so considers any initial sample >> 0 to be a discontinuity.
  EXPECT_EQ(verifier.CountDiscontinuities(), 1U);

  DispatchFunction([&] {
    // Clean up
    processingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(processingTrack, listener));
    processingTrack->DisconnectDeviceInput();
    processingTrack->Destroy();
  });
  WaitFor(primaryStream->OutputVerificationEvent());
}

// Test when AudioInputProcessing expects clock drift
TEST_WithTailDispatch(TestAudioTrackGraph, ClockDriftExpectation) {
  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  const TrackRate rate = 44100;

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER,
      /*Window ID*/ 1, rate, nullptr, AbstractThread::MainThread());

  auto createInputProcessing =
      [&](CubebUtils::AudioDeviceID aDeviceID,
          RefPtr<AudioProcessingTrack>* aProcessingTrack,
          RefPtr<AudioInputProcessing>* aInputProcessing) {
        /* Create an input track and connect it to a device */
        const int32_t channelCount = 2;
        RefPtr processingTrack = AudioProcessingTrack::Create(graph);
        RefPtr inputProcessing = new AudioInputProcessing(channelCount);
        processingTrack->SetInputProcessing(inputProcessing);
        MediaEnginePrefs settings;
        settings.mChannels = channelCount;
        settings.mAecOn = true;
        QueueApplySettings(processingTrack, inputProcessing, settings);
        processingTrack->GraphImpl()->AppendMessage(
            std::make_unique<StartInputProcessing>(processingTrack,
                                                   inputProcessing));
        processingTrack->ConnectDeviceInput(aDeviceID, inputProcessing,
                                            PRINCIPAL_HANDLE_NONE);
        aProcessingTrack->swap(processingTrack);
        aInputProcessing->swap(inputProcessing);
      };

  // Native input, which uses a duplex stream
  RefPtr<AudioProcessingTrack> processingTrack1;
  RefPtr<AudioInputProcessing> inputProcessing1;
  DispatchFunction([&] {
    createInputProcessing(nullptr, &processingTrack1, &inputProcessing1);
  });
  RefPtr<SmartMockCubebStream> primaryStream =
      WaitFor(cubeb->StreamInitEvent());
  EXPECT_GT(primaryStream->OutputChannels(), 0U);
  // Non-native input
  const auto* nonNativeInputDeviceID = CubebUtils::AudioDeviceID(1);
  RefPtr<AudioProcessingTrack> processingTrack2;
  RefPtr<AudioInputProcessing> inputProcessing2;
  DispatchFunction([&] {
    createInputProcessing(nonNativeInputDeviceID, &processingTrack2,
                          &inputProcessing2);
    processingTrack2->AddAudioOutput(nullptr, nullptr, rate);
  });

  RefPtr<SmartMockCubebStream> nonNativeInputStream =
      WaitFor(cubeb->StreamInitEvent());
  EXPECT_EQ(nonNativeInputStream->GetInputDeviceID(), nonNativeInputDeviceID);
  EXPECT_EQ(nonNativeInputStream->OutputChannels(), 0U);

  // Wait until non-native input signal reaches the output, when input
  // processing has run and so has been configured.
  WaitFor(primaryStream->FramesVerifiedEvent());

  const void* secondaryOutputDeviceID = CubebUtils::AudioDeviceID(2);
  DispatchFunction([&] {
    // Check input processing config with output to primary device.
    processingTrack1->QueueControlMessageWithNoShutdown([&] {
      EXPECT_FALSE(inputProcessing1->HadAECAndDrift());
      EXPECT_TRUE(inputProcessing2->HadAECAndDrift());
    });

    // Switch output to a secondary device.
    processingTrack2->RemoveAudioOutput(nullptr);
    processingTrack2->AddAudioOutput(nullptr, secondaryOutputDeviceID, rate);
  });

  RefPtr<SmartMockCubebStream> secondaryOutputStream =
      WaitFor(cubeb->StreamInitEvent());
  EXPECT_EQ(secondaryOutputStream->GetOutputDeviceID(),
            secondaryOutputDeviceID);

  WaitFor(secondaryOutputStream->FramesVerifiedEvent());
  DispatchFunction([&] {
    // Check input processing config with output to secondary device.
    processingTrack1->QueueControlMessageWithNoShutdown([&] {
      EXPECT_TRUE(inputProcessing1->HadAECAndDrift());
      EXPECT_TRUE(inputProcessing2->HadAECAndDrift());
    });
  });

  auto destroyInputProcessing = [&](AudioProcessingTrack* aProcessingTrack,
                                    AudioInputProcessing* aInputProcessing) {
    aProcessingTrack->GraphImpl()->AppendMessage(
        std::make_unique<StopInputProcessing>(aProcessingTrack,
                                              aInputProcessing));
    aProcessingTrack->DisconnectDeviceInput();
    aProcessingTrack->Destroy();
  };

  DispatchFunction([&] {
    // Clean up
    destroyInputProcessing(processingTrack1, inputProcessing1);
    destroyInputProcessing(processingTrack2, inputProcessing2);
  });
  // Wait for stream stop to ensure that expectations have been checked.
  WaitFor(nonNativeInputStream->OutputVerificationEvent());
}
#endif  // MOZ_WEBRTC

TEST_WithTailDispatch(TestAudioTrackGraph, PlatformProcessing) {
  constexpr cubeb_input_processing_params allParams =
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION |
      CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL |
      CUBEB_INPUT_PROCESSING_PARAM_VOICE_ISOLATION;
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  cubeb->SetSupportedInputProcessingParams(allParams, CUBEB_OK);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID device = (CubebUtils::AudioDeviceID)1;

  // Set up mock listener.
  RefPtr<MockAudioDataListener> listener = MakeRefPtr<MockAudioDataListener>();
  EXPECT_CALL(*listener, IsVoiceInput).WillRepeatedly(Return(true));
  EXPECT_CALL(*listener, RequestedInputChannelCount).WillRepeatedly(Return(1));
  EXPECT_CALL(*listener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
  EXPECT_CALL(*listener, Disconnect);

  // Expectations.
  const Result<cubeb_input_processing_params, int> notSupportedResult(
      Err(CUBEB_ERROR_NOT_SUPPORTED));
  const Result<cubeb_input_processing_params, int> errorResult(
      Err(CUBEB_ERROR));
  const Result<cubeb_input_processing_params, int> noneResult(
      CUBEB_INPUT_PROCESSING_PARAM_NONE);
  const Result<cubeb_input_processing_params, int> echoResult(
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  const Result<cubeb_input_processing_params, int> noiseResult(
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  Atomic<int> numProcessingParamsResults(0);
  {
    InSequence s;
    // On first driver start.
    EXPECT_CALL(*listener,
                NotifySetRequestedInputProcessingParams(
                    graph, 1, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 1, Eq(std::ref(echoResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After requesting something else.
    EXPECT_CALL(*listener,
                NotifySetRequestedInputProcessingParams(
                    graph, 2, CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 2, Eq(std::ref(noiseResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After error request.
    EXPECT_CALL(*listener,
                NotifySetRequestedInputProcessingParams(
                    graph, 3, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 3, Eq(std::ref(errorResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After requesting None, because of the previous error.
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParams(
                               graph, 4, CUBEB_INPUT_PROCESSING_PARAM_NONE));
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 4, Eq(std::ref(noneResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After driver switch.
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 4, Eq(std::ref(noneResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After requesting something not supported.
    EXPECT_CALL(*listener,
                NotifySetRequestedInputProcessingParams(
                    graph, 5, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 5, Eq(std::ref(noneResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After requesting something with backend not supporting processing params.
    EXPECT_CALL(*listener,
                NotifySetRequestedInputProcessingParams(
                    graph, 6, CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
    EXPECT_CALL(*listener, NotifySetRequestedInputProcessingParamsResult(
                               graph, 6, Eq(std::ref(notSupportedResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
  }

  // Open a device.
  RefPtr<TestDeviceInputConsumerTrack> track;
  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    track = TestDeviceInputConsumerTrack::Create(graph);
    track->ConnectDeviceInput(device, listener, PRINCIPAL_HANDLE_NONE);
    fallbackListener = new OnFallbackListener(track);
    track->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_TRUE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), 1U);
  EXPECT_EQ(stream->GetInputDeviceID(), device);

  while (
      stream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto waitForResult = [&](int aNumResult) {
    while (numProcessingParamsResults < aNumResult) {
      EXPECT_EQ(stream->ManualDataCallback(0),
                MockCubebStream::KeepProcessing::Yes);
      NS_ProcessNextEvent();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the first result after driver creation.
  waitForResult(1);

  // Request new processing params.
  EXPECT_CALL(*listener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
  waitForResult(2);

  // Test with returning error on new request.
  cubeb->SetInputProcessingApplyRv(CUBEB_ERROR);
  EXPECT_CALL(*listener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
  waitForResult(3);

  // Test unsetting all params.
  cubeb->SetInputProcessingApplyRv(CUBEB_OK);
  EXPECT_CALL(*listener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));
  waitForResult(4);

  // Switch driver.
  EXPECT_CALL(*listener, RequestedInputChannelCount).WillRepeatedly(Return(2));
  // ReevaluateInputDevice() is not required for the native input, but is
  // called anyway.
  DispatchFunction([&] {
    track->QueueControlMessageWithNoShutdown(
        [&] { graph->ReevaluateInputDevice(device); });
  });
  ProcessEventQueue();
  // Process the reevaluation message and perform the switch.
  auto initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  EXPECT_EQ(stream->ManualDataCallback(0), MockCubebStream::KeepProcessing::No);
  std::tie(stream) = WaitFor(initPromise).unwrap()[0];
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_TRUE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), 2U);
  EXPECT_EQ(stream->GetInputDeviceID(), device);

  while (
      stream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the new AudioCallbackDriver to come into effect.
  fallbackListener->Reset();
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the first result after driver creation.
  waitForResult(5);

  // Test requesting something not supported.
  cubeb->SetSupportedInputProcessingParams(
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION |
          CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL |
          CUBEB_INPUT_PROCESSING_PARAM_VOICE_ISOLATION,
      CUBEB_OK);
  EXPECT_CALL(*listener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
  waitForResult(6);

  // Test requesting something when unsupported by backend.
  cubeb->SetSupportedInputProcessingParams(CUBEB_INPUT_PROCESSING_PARAM_NONE,
                                           CUBEB_ERROR_NOT_SUPPORTED);
  EXPECT_CALL(*listener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
  waitForResult(7);

  // Clean up.
  DispatchFunction([&] {
    track->RemoveListener(fallbackListener);
    track->Destroy();
  });
  ProcessEventQueue();
  DispatchFunction([&] {
    // Process the destroy message and shut down.
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::No);
  });
  RefPtr<SmartMockCubebStream> destroyedStream =
      WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_EQ(destroyedStream.get(), stream.get());
  {
    NativeInputTrack* native = graph->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!native);
  }
}

TEST_WithTailDispatch(TestAudioTrackGraph,
                      PlatformProcessingNonNativeToNativeSwitch) {
  constexpr cubeb_input_processing_params allParams =
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION |
      CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL |
      CUBEB_INPUT_PROCESSING_PARAM_VOICE_ISOLATION;
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  cubeb->SetSupportedInputProcessingParams(allParams, CUBEB_OK);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  const CubebUtils::AudioDeviceID firstDevice = (CubebUtils::AudioDeviceID)1;
  const CubebUtils::AudioDeviceID secondDevice = (CubebUtils::AudioDeviceID)2;

  // Set up mock listeners.
  RefPtr<MockAudioDataListener> firstListener =
      MakeRefPtr<MockAudioDataListener>();
  EXPECT_CALL(*firstListener, IsVoiceInput).WillRepeatedly(Return(true));
  EXPECT_CALL(*firstListener, RequestedInputChannelCount)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*firstListener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));

  RefPtr<MockAudioDataListener> secondListener =
      MakeRefPtr<MockAudioDataListener>();
  EXPECT_CALL(*secondListener, IsVoiceInput).WillRepeatedly(Return(true));
  EXPECT_CALL(*secondListener, RequestedInputChannelCount)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*secondListener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));

  // Expectations.
  const Result<cubeb_input_processing_params, int> noneResult(
      CUBEB_INPUT_PROCESSING_PARAM_NONE);
  const Result<cubeb_input_processing_params, int> echoResult(
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  const Result<cubeb_input_processing_params, int> noiseResult(
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  Atomic<int> numProcessingParamsResults(0);
  {
    InSequence s;
    // On first driver start.
    EXPECT_CALL(*firstListener, NotifySetRequestedInputProcessingParamsResult(
                                    graph, 0, Eq(std::ref(noneResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After param update.
    EXPECT_CALL(*firstListener,
                NotifySetRequestedInputProcessingParams(
                    graph, 1, CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
    EXPECT_CALL(*firstListener, NotifySetRequestedInputProcessingParamsResult(
                                    graph, 1, Eq(std::ref(noiseResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After second param update.
    EXPECT_CALL(*firstListener,
                NotifySetRequestedInputProcessingParams(
                    graph, 2, CUBEB_INPUT_PROCESSING_PARAM_NONE));
    EXPECT_CALL(*firstListener, NotifySetRequestedInputProcessingParamsResult(
                                    graph, 2, Eq(std::ref(noneResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // On non-native device's start.
    EXPECT_CALL(*secondListener,
                NotifySetRequestedInputProcessingParams(
                    graph, 3, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
    EXPECT_CALL(*secondListener, NotifySetRequestedInputProcessingParamsResult(
                                     graph, 3, Eq(std::ref(echoResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After switch to native device for second device.
    EXPECT_CALL(*firstListener, Disconnect);
    EXPECT_CALL(*secondListener, Disconnect);
    EXPECT_CALL(*secondListener,
                NotifySetRequestedInputProcessingParams(
                    graph, 4, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION));
    EXPECT_CALL(*secondListener, NotifySetRequestedInputProcessingParamsResult(
                                     graph, 4, Eq(std::ref(echoResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    // After param update.
    EXPECT_CALL(*secondListener,
                NotifySetRequestedInputProcessingParams(
                    graph, 5, CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
    EXPECT_CALL(*secondListener, NotifySetRequestedInputProcessingParamsResult(
                                     graph, 5, Eq(std::ref(noiseResult))))
        .WillOnce([&] { ++numProcessingParamsResults; });
    EXPECT_CALL(*secondListener, Disconnect);
  }

  // Open the first input device. It will be the native device of the graph.
  RefPtr<TestDeviceInputConsumerTrack> firstTrack;
  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    firstTrack = TestDeviceInputConsumerTrack::Create(graph);
    firstTrack->ConnectDeviceInput(firstDevice, firstListener,
                                   PRINCIPAL_HANDLE_NONE);
    fallbackListener = new OnFallbackListener(firstTrack);
    firstTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> nativeStream = WaitFor(cubeb->StreamInitEvent());
  RefPtr<SmartMockCubebStream> nonNativeStream;
  EXPECT_TRUE(nativeStream->mHasInput);
  EXPECT_TRUE(nativeStream->mHasOutput);
  EXPECT_EQ(nativeStream->InputChannels(), 1U);
  EXPECT_EQ(nativeStream->GetInputDeviceID(), firstDevice);

  while (
      nativeStream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(nativeStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto waitForResult = [&](int aNumResult) {
    while (numProcessingParamsResults < aNumResult) {
      EXPECT_EQ(nativeStream->ManualDataCallback(0),
                MockCubebStream::KeepProcessing::Yes);
      if (nonNativeStream) {
        EXPECT_EQ(nonNativeStream->ManualDataCallback(0),
                  MockCubebStream::KeepProcessing::Yes);
      }
      NS_ProcessNextEvent();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  // Wait for the first result after driver creation.
  waitForResult(1);

  // Request new processing params.
  EXPECT_CALL(*firstListener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
  waitForResult(2);

  // Again request new processing params, to move the processing params
  // generation of the first device higher so as to avoid ambiguity with the
  // processing params generation of the second device.
  EXPECT_CALL(*firstListener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));
  waitForResult(3);

  // Open the second input device. It will be a non-native device of the graph.
  RefPtr<TestDeviceInputConsumerTrack> secondTrack;
  DispatchFunction([&] {
    secondTrack = TestDeviceInputConsumerTrack::Create(graph);
    secondTrack->ConnectDeviceInput(secondDevice, secondListener,
                                    PRINCIPAL_HANDLE_NONE);
    secondTrack->AddListener(fallbackListener);
  });

  auto initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  DispatchFunction([&] {
    // TakeN dispatches a task that runs SpinEventLoopUntil while blocking the
    // caller. Make sure any event loop spinning that needs to be intertwined
    // with driving the graph runs in a nested task, so as to not block the test
    // flow. I.e. if the caller that gets blocked on the TakeN task is WaitFor
    // below, we will deadlock, because the graph must be iterated to unblock
    // the TakeN task.
    EXPECT_EQ(nativeStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    ProcessEventQueue();
    EXPECT_EQ(nativeStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
  });

  std::tie(nonNativeStream) = WaitFor(initPromise).unwrap()[0];
  EXPECT_TRUE(nonNativeStream->mHasInput);
  EXPECT_FALSE(nonNativeStream->mHasOutput);
  EXPECT_EQ(nonNativeStream->InputChannels(), 1U);
  EXPECT_EQ(nonNativeStream->GetInputDeviceID(), secondDevice);

  while (
      nonNativeStream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the first result after non-native device input creation.
  waitForResult(4);

  // Disconnect native input. First non-native input should be promoted to
  // native, and processing param generation should increase monotonically
  // through the switch.
  DispatchFunction([&] {
    firstTrack->RemoveListener(fallbackListener);
    secondTrack->AddListener(fallbackListener);
    firstTrack->DisconnectDeviceInput();
  });
  ProcessEventQueue();
  initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  // Process the disconnect message, perform the switch,
  // and check that the second device is now used with the new graph driver.
  EXPECT_EQ(nativeStream->ManualDataCallback(0),
            MockCubebStream::KeepProcessing::No);
  std::tie(nativeStream) = WaitFor(initPromise).unwrap()[0];
  EXPECT_TRUE(nativeStream->mHasInput);
  EXPECT_TRUE(nativeStream->mHasOutput);
  EXPECT_EQ(nativeStream->InputChannels(), 1U);
  EXPECT_EQ(nativeStream->GetInputDeviceID(), secondDevice);

  while (
      nativeStream->State()
          .map([](cubeb_state aState) { return aState != CUBEB_STATE_STARTED; })
          .valueOr(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Wait for the new AudioCallbackDriver to come into effect.
  fallbackListener->Reset();
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(nativeStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // So that waitForResult doesn't make callbacks to it anymore.
  nonNativeStream = nullptr;

  // Wait for the first result after driver creation.
  waitForResult(5);

  // Request new processing params.
  EXPECT_CALL(*secondListener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
  waitForResult(6);

  // Clean up.
  DispatchFunction([&] {
    firstTrack->Destroy();
    secondTrack->RemoveListener(fallbackListener);
    secondTrack->Destroy();
  });
  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] {
    ProcessEventQueue();
    // Process the destroy message and shut down native.
    EXPECT_EQ(nativeStream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::No);
  });
  RefPtr<SmartMockCubebStream> destroyedStream;
  std::tie(destroyedStream) = WaitFor(destroyPromise).unwrap()[0];
  EXPECT_EQ(destroyedStream, nativeStream);
  {
    NativeInputTrack* native = graph->GetNativeInputTrackMainThread();
    ASSERT_TRUE(!native);
  }
}

TEST_WithTailDispatch(TestAudioTrackGraph, EmptyProcessingInterval) {
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraph* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());
  RefPtr fallbackListener = new OnFallbackListener(processedTrack);
  MockFunction<void(const char* name)> checkpoint;
  {
    InSequence s;
    EXPECT_CALL(*processedTrack, AddListenerImpl).Times(1);
    EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("before single iteration")));
    EXPECT_CALL(*processedTrack, ProcessInput).Times(1);
    EXPECT_CALL(checkpoint, Call(StrEq("before empty iteration")));
    EXPECT_CALL(checkpoint, Call(StrEq("after empty iteration")));
  }
  DispatchFunction([&] {
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    processedTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(1),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // The graph is now run by ManualDataCallback().
  GraphTime processedUpTo0 = processedTrack->GetEnd();
  EXPECT_GT(processedUpTo0, 0u);
  checkpoint.Call("before single iteration");
  // 128 matches the block size used by MediaTrackGraph and so is expected to
  // trigger another block of processing.
  EXPECT_EQ(stream->ManualDataCallback(128),
            MockCubebStream::KeepProcessing::Yes);
  GraphTime processedUpTo1 = processedTrack->GetEnd();
  EXPECT_EQ(processedUpTo1, processedUpTo0 + 128);
  // Test that ProcessInput() is not called in a graph iteration with no
  // frames to be rendered.
  checkpoint.Call("before empty iteration");
  EXPECT_EQ(stream->ManualDataCallback(0),
            MockCubebStream::KeepProcessing::Yes);
  checkpoint.Call("after empty iteration");
  EXPECT_EQ(processedTrack->GetEnd(), processedUpTo1);

  DispatchFunction([&] { processedTrack->Destroy(); });
  ProcessEventQueue();
  // Process the destroy message and drain the stream.
  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] {
    while (stream->ManualDataCallback(0) ==
           MockCubebStream::KeepProcessing::Yes) {
    }
  });
  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
}

TEST_WithTailDispatch(TestAudioTrackGraph, DefaultOutputDeviceIDTracking) {
#ifdef ANDROID
  GTEST_SKIP() << "On Android CubebDeviceEnumerator, not the cubeb backend, "
                  "handles device enumeration, exposing only a single input "
                  "and output device. Both with devid 0.";
#else
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  AddDevices(cubeb, 1, CUBEB_DEVICE_TYPE_INPUT);
  AddDevices(cubeb, 2, CUBEB_DEVICE_TYPE_OUTPUT);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Mocks and expectations.
  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());
  RefPtr<MockAudioDataListener> dataListener =
      MakeRefPtr<MockAudioDataListener>();
  const Result<cubeb_input_processing_params, int> notSupportedResult(
      Err(CUBEB_ERROR_NOT_SUPPORTED));

  EXPECT_CALL(*processedTrack, AddListenerImpl).Times(1);
  EXPECT_CALL(*processedTrack, RemoveListenerImpl).Times(1);
  EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
  EXPECT_CALL(*dataListener, RequestedInputChannelCount)
      .WillRepeatedly(Return(2));
  EXPECT_CALL(*dataListener, RequestedInputProcessingParams)
      .WillRepeatedly(Return(CUBEB_INPUT_PROCESSING_PARAM_NONE));
  EXPECT_CALL(*dataListener, IsVoiceInput).WillRepeatedly(Return(true));
  {
    InSequence s;
    EXPECT_CALL(*dataListener, NotifySetRequestedInputProcessingParamsResult(
                                   graph, 0, Eq(std::ref(notSupportedResult))));
    EXPECT_CALL(*dataListener, Disconnect);
  }

  // Add a track to maintain an output-only audio driver.
  RefPtr fallbackListener = new OnFallbackListener(processedTrack);
  DispatchFunction([&] {
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    processedTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(1),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // The graph is now run by ManualDataCallback().

  EXPECT_EQ(graph->DefaultOutputDeviceID(), nullptr)
      << "Without an input track, the default output isn't tracked.";

  // Add an input track.
  RefPtr<TestDeviceInputConsumerTrack> inputTrack;
  DispatchFunction([&] {
    processedTrack->RemoveListener(fallbackListener);
    inputTrack = TestDeviceInputConsumerTrack::Create(graph);
    inputTrack->ConnectDeviceInput(CubebUtils::AudioDeviceID(1), dataListener,
                                   PRINCIPAL_HANDLE_NONE);
    fallbackListener = new OnFallbackListener(inputTrack);
    inputTrack->AddListener(fallbackListener);
  });
  auto initPromise = TakeN(cubeb->StreamInitEvent(), 1);
  ProcessEventQueue();
  // Process the disconnect message, perform the switch,
  // and check that the second device is now used with the new graph driver.
  EXPECT_EQ(stream->ManualDataCallback(0), MockCubebStream::KeepProcessing::No);
  std::tie(stream) = WaitFor(initPromise).unwrap()[0];
  EXPECT_TRUE(stream->mHasInput);
  EXPECT_TRUE(stream->mHasOutput);
  EXPECT_EQ(stream->InputChannels(), 2U);
  EXPECT_EQ(stream->GetInputDeviceID(), CubebUtils::AudioDeviceID(1));
  EXPECT_EQ(stream->GetOutputDeviceID(), nullptr);
  stream->ManualDataCallback(0);

  EXPECT_EQ(graph->DefaultOutputDeviceID(), CubebUtils::AudioDeviceID(2))
      << "With an input track, we are tracking the default output device.";

  cubeb->SetPreferredDevice(CubebUtils::AudioDeviceID(1),
                            CUBEB_DEVICE_TYPE_OUTPUT);
  ProcessEventQueue();
  EXPECT_EQ(graph->DefaultOutputDeviceID(), CubebUtils::AudioDeviceID(1))
      << "Default output device tracking follows system changes.";

  DispatchFunction([&] {
    inputTrack->RemoveListener(fallbackListener);
    inputTrack->Destroy();
    processedTrack->Destroy();
  });
  ProcessEventQueue();
  // Process the destroy message and drain the stream.
  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] {
    while (stream->ManualDataCallback(0) ==
           MockCubebStream::KeepProcessing::Yes) {
    }
  });
  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
#endif
}

class TrackDestroyShutdownFactory final : public nsIFactory {
  ~TrackDestroyShutdownFactory() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit TrackDestroyShutdownFactory(MediaTrack* aTrack)
      : mShutdownSvc(components::AsyncShutdown::Service()), mTrack(aTrack) {}

  NS_IMETHOD CreateInstance(const nsIID& aIID, void** aResult) override {
    if (!mTrack->IsDestroyed()) {
      // Emulate a track destroyed by an Unlink() during
      // nsCycleCollector::CollectWhite().
      mTrack->Destroy();
    }

    *aResult = do_AddRef(mShutdownSvc).take();
    return NS_OK;
  }

 private:
  const nsCOMPtr<nsIAsyncShutdownService> mShutdownSvc;
  const RefPtr<MediaTrack> mTrack;
};

NS_IMPL_ISUPPORTS(TrackDestroyShutdownFactory, nsIFactory)

TEST_WithTailDispatch(TestAudioTrackGraph, GraphRemovalInGetInstance) {
  TrackRate sampleRate1 = 24000;
  TrackRate sampleRate2 = 48000;
  const char* shutdownSvcContractId = "@mozilla.org/async-shutdown-service;1";
  nsID shutdownSvcClassId = xpcom::StaticComponents::LookupByContractID(
                                nsDependentCString(shutdownSvcContractId))
                                ->CID();

  MockCubeb* cubeb = new MockCubeb();
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  RefPtr graph1 = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1, sampleRate1,
      nullptr, AbstractThread::MainThread());

  // Dummy track to remove the graph from the global hash table.
  RefPtr<SourceMediaTrack> dummySource1;
  DispatchFunction([&] {
    dummySource1 = graph1->CreateSourceTrack(MediaSegment::AUDIO);
    // Add an audio output so that the graph stays on the AudioCallbackDriver
    // and its stream is not destroyed before the graph is destroyed.
    dummySource1->AddAudioOutput(reinterpret_cast<void*>(1),
                                 graph1->PrimaryOutputDeviceID(), 0);
  });

  (void)WaitFor(cubeb->StreamInitEvent());

#define TRACKDESTROYTEST_CID /* 6b5d42c9-9a03-4255-9016-73905216cdbe */ \
  {                                                                     \
    0x6b5d42c9, 0x9a03, 0x4255,                                         \
        {0x90, 0x16, 0x73, 0x90, 0x52, 0x16, 0xcd, 0xbe}                \
  }
  NS_DEFINE_NAMED_CID(TRACKDESTROYTEST_CID);

  // Register an async shutdown service that removes dummySource1 when
  // instantiated on demand.  The component manager makes this override the
  // static async shutdown service because that component is `overridable`.
  RefPtr factory = new TrackDestroyShutdownFactory(dummySource1);
  nsresult rv = nsComponentManagerImpl::gComponentManager->RegisterFactory(
      kTRACKDESTROYTEST_CID, "TrackDestroyTestService", shutdownSvcContractId,
      factory);
  EXPECT_EQ(rv, NS_OK);

  MediaTrackGraph* graph2;
  DispatchFunction([&] {
    EXPECT_FALSE(dummySource1->IsDestroyed());
    graph2 = MediaTrackGraphImpl::GetInstance(
        MediaTrackGraph::AUDIO_THREAD_DRIVER, /*Window ID*/ 1, sampleRate2,
        nullptr, AbstractThread::MainThread());
    EXPECT_TRUE(dummySource1->IsDestroyed());
  });
  // Destroying the last track in the graph triggered graph destruction.
  (void)WaitFor(cubeb->StreamDestroyEvent());
  EXPECT_TRUE(graph1->Destroyed());

  // Test has finished.
  // Restore the static async shutdown service.
  rv = nsComponentManagerImpl::gComponentManager->RegisterFactory(
      shutdownSvcClassId, nullptr, shutdownSvcContractId, nullptr);
  EXPECT_EQ(rv, NS_OK);
  // Clean up.
  rv = nsComponentManagerImpl::gComponentManager->UnregisterFactory(
      kTRACKDESTROYTEST_CID, factory);
  EXPECT_EQ(rv, NS_OK);
  // Destroy a track to shutdown the MTG.
  RefPtr<SourceMediaTrack> dummySource2;
  DispatchFunction([&] {
    dummySource2 = graph2->CreateSourceTrack(MediaSegment::AUDIO);
    dummySource2->AddAudioOutput(reinterpret_cast<void*>(1),
                                 graph2->PrimaryOutputDeviceID(), 0);
  });
  (void)WaitFor(cubeb->StreamInitEvent());
  DispatchMethod(dummySource2, &SourceMediaTrack::Destroy);
  (void)WaitFor(cubeb->StreamDestroyEvent());
}

TEST(TestAudioTrackGraph, MessageOrdering)
{
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Mocks and expectations.
  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());

  MockFunction<void(const char* name)> checkpoint;
  {
    InSequence s;
    EXPECT_CALL(checkpoint, Call(StrEq("Prior to tail dispatch")));
    EXPECT_CALL(*processedTrack, AddListenerImpl);
    EXPECT_CALL(checkpoint, Call(StrEq("1-main->graph")));
    EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("canonical1")));
    EXPECT_CALL(*processedTrack, ProcessInput);  // 2*128
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(*processedTrack, ProcessInput);  // 3*128
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("canonical3")));
    EXPECT_CALL(checkpoint, Call(StrEq("2-main->graph")));
    EXPECT_CALL(checkpoint, Call(StrEq("2-main->graph + 2-direct")));
    EXPECT_CALL(checkpoint, Call(StrEq("3-main->graph")));
    EXPECT_CALL(checkpoint, Call(StrEq("3-main->graph + 2-direct")));
    EXPECT_CALL(*processedTrack, ProcessInput);  // 4*128
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("2-main->graph + 3-graph->graph")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("3-main->graph + 2-direct + 2-graph->graph")));
    EXPECT_CALL(*processedTrack, ProcessInput);  // 5*128
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("2-main->graph + 1-graph->main")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("2-main->graph + 2-direct + graph->main")));
    EXPECT_CALL(checkpoint, Call(StrEq("3-main->graph + 1-graph->main")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("3-main->graph + 2-direct + 1-graph->main")));
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(checkpoint,
                Call(StrEq("2-main->graph + 3-graph->graph + graph->main")));
    EXPECT_CALL(
        checkpoint,
        Call(StrEq("3-main->graph + 2-direct + 2-graph->graph + graph->main")));
    EXPECT_CALL(checkpoint, Call(StrEq("processed task on main")))
        .Times(AtLeast(1));
    EXPECT_CALL(*processedTrack, RemoveListenerImpl);
  }

  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    // Add a track to maintain an output-only audio driver.
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    fallbackListener = new OnFallbackListener(processedTrack);
    processedTrack->AddListener(fallbackListener);

    // First dispatch to the graph.
    EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(
        __func__, [&] { checkpoint.Call("1-main->graph"); }))));
    checkpoint.Call("Prior to tail dispatch");
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  while (fallbackListener->OnFallback()) {
    EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // The graph is now run by ManualDataCallback().

  Canonical<const char*> canonical(AbstractThread::MainThread(), "canonical1",
                                   "TestCanonical");
  Mirror<const char*> mirror(graph, "", "TestMirror");
  using OnMirror = NiceMock<MockFunction<void()>>;
  auto onmirror = MakeRefPtr<media::Refcountable<OnMirror>>();
  ON_CALL(*onmirror, Call).WillByDefault([&] { checkpoint.Call(mirror); });
  WatchManager<media::Refcountable<OnMirror>> mgr(onmirror, graph);

  // Set up mirror. Note the graph->Dispatch is a regular task and
  // canonical.ConnectMirror will dispatch a state change task to update the
  // mirror. A tail dispatched per-thread group runnable will always run state
  // change tasks before regular tasks regardless of dispatch order. Make sure
  // to dispatch the regular task from another, earlier, main thread task.
  DispatchFunction([&] {
    EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(
        __func__, [&] { mgr.Watch(mirror, &OnMirror::Call); }))));
  });

  DispatchFunction([&] { canonical.ConnectMirror(&mirror); });

  // Run the setup tasks in the graph. They run prior to processing tracks.
  DispatchFunction([&] {
    EXPECT_EQ(stream->ManualDataCallback(2 * WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
  });

  DispatchFunction([&] {
    // Second dispatch to the graph. Don't fire the tail dispatcher yet. Iterate
    // the graph and do another dispatch first. Main thread runnables will be
    // grouped across both tasks.
    EXPECT_TRUE(
        NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(__func__, [&] {
          EXPECT_EQ(AbstractThread::GetCurrent(), graph);
          EXPECT_EQ(GetCurrentSerialEventTarget(), graph);
          graph->DispatchToMainThread(NS_NewRunnableFunction(
              "MessageOrdering main",
              [&] { checkpoint.Call("2-main->graph + 1-graph->main"); }));
          graph->TailDispatcher().AddDirectTask(
              NS_NewRunnableFunction("MessageOrdering direct", [&] {
                EXPECT_EQ(AbstractThread::GetCurrent(), graph);
                EXPECT_EQ(GetCurrentSerialEventTarget(), graph);
                graph->DispatchToMainThread(
                    NS_NewRunnableFunction("MessageOrdering main", [&] {
                      checkpoint.Call("2-main->graph + 2-direct + graph->main");
                    }));
                checkpoint.Call("2-main->graph + 2-direct");
              }));
          EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(
              NS_NewRunnableFunction("MessageOrdering nested", [&] {
                graph->DispatchToMainThread(
                    NS_NewRunnableFunction("MessageOrdering nested main", [&] {
                      checkpoint.Call(
                          "2-main->graph + 3-graph->graph + graph->main");
                    }));
                checkpoint.Call("2-main->graph + 3-graph->graph");
              }))));
          checkpoint.Call("2-main->graph");
        }))));

    // Update the canonical to a value that won't reach the mirror. There will
    // be another update before the next firing of the tail dispatcher.
    canonical = "canonical2";

    // Iteration that should not run the second dispatch's task.
    EXPECT_EQ(stream->ManualDataCallback(3 * WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);

    // Third dispatch to the graph.
    EXPECT_TRUE(
        NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(__func__, [&] {
          graph->DispatchToMainThread(NS_NewRunnableFunction(
              "MessageOrdering main",
              [&] { checkpoint.Call("3-main->graph + 1-graph->main"); }));
          graph->TailDispatcher().AddDirectTask(
              NS_NewRunnableFunction("MessageOrdering direct", [&] {
                graph->DispatchToMainThread(
                    NS_NewRunnableFunction("MessageOrdering main", [&] {
                      checkpoint.Call(
                          "3-main->graph + 2-direct + 1-graph->main");
                    }));
                EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(
                    "MessageOrdering direct nested", [&] {
                      graph->DispatchToMainThread(NS_NewRunnableFunction(
                          "MessageOrdering direct nested main", [&] {
                            checkpoint.Call(
                                "3-main->graph + 2-direct + 2-graph->graph + "
                                "graph->main");
                          }));
                      checkpoint.Call(
                          "3-main->graph + 2-direct + 2-graph->graph");
                    }))));
                checkpoint.Call("3-main->graph + 2-direct");
              }));
          checkpoint.Call("3-main->graph");
        }))));

    // Update the canonical before firing the tail dispatcher.
    canonical = "canonical3";
  });

  // Run graph runnables, at beginning of iteration.
  DispatchFunction([&] {
    EXPECT_EQ(stream->ManualDataCallback(4 * WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
  });

  // And self-dispatched runnables.
  DispatchFunction([&] {
    EXPECT_EQ(stream->ManualDataCallback(5 * WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
  });

  DispatchFunction([&] {
    EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(
        NS_NewRunnableFunction(__func__, [&] { mgr.Shutdown(); }))));
    canonical.DisconnectAll();
  });

  // Process main thread runnables.
  while (NS_ProcessNextEvent(nullptr, /*aMayWait=*/false)) {
    checkpoint.Call("processed task on main");
  }

  DispatchFunction([&] {
    processedTrack->RemoveListener(fallbackListener);
    processedTrack->Destroy();
  });
  // Process the destroy message and drain the stream.
  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] {
    while (stream->ManualDataCallback(0) ==
           MockCubebStream::KeepProcessing::Yes) {
    }
  });
  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
}

namespace {
class TestRunnable : public Runnable, public nsIDiscardableRunnable {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  TestRunnable(const char* aName, bool aDiscardable,
               MockFunction<void(const char*)>& aCheckpoint)
      : Runnable(aName),
        mName(aName),
        mDiscardable(aDiscardable),
        mCheckpoint(aCheckpoint) {}
  NS_IMETHODIMP Run() override {
    mCheckpoint.Call(nsPrintfCString("%s::%s", mName, __func__).get());
    return NS_OK;
  }

  void OnDiscard() override {
    mCheckpoint.Call(nsPrintfCString("%s::%s", mName, __func__).get());
  }

 protected:
  ~TestRunnable() override {
    mCheckpoint.Call(nsPrintfCString("~%s", mName).get());
  }

 private:
  const char* mName;
  const bool mDiscardable;
  MockFunction<void(const char*)>& mCheckpoint;
};
}  // namespace

NS_IMPL_ADDREF_INHERITED(TestRunnable, Runnable)
NS_IMPL_RELEASE_INHERITED(TestRunnable, Runnable)

NS_INTERFACE_MAP_BEGIN(TestRunnable)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIDiscardableRunnable, mDiscardable)
NS_INTERFACE_MAP_END_INHERITING(Runnable)

TEST(TestAudioTrackGraph, ShutdownMessages)
{
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Mocks and expectations.
  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());

  MockFunction<void(const char* name)> checkpoint;
  {
    InSequence s;
    EXPECT_CALL(checkpoint, Call(StrEq("Prior to tail dispatch")));
    EXPECT_CALL(*processedTrack, AddListenerImpl);
    EXPECT_CALL(checkpoint, Call(StrEq("First dispatch")));
    EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
    EXPECT_CALL(checkpoint, Call(StrEq("Now manual")));

    EXPECT_CALL(checkpoint,
                Call(StrEq("NonShutdownRunnable_BeforeShutdown::Run")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("ShutdownRunnable_BeforeShutdown::Run")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("ControlNoShutdown_BeforeShutdown::Run")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("ControlOrShutdown_BeforeShutdown::Run")));
    EXPECT_CALL(checkpoint, Call(StrEq("~NonShutdownRunnable_BeforeShutdown")));
    EXPECT_CALL(checkpoint, Call(StrEq("~ShutdownRunnable_BeforeShutdown")));

    EXPECT_CALL(checkpoint, Call(StrEq("Forced shutdown")));
    EXPECT_CALL(checkpoint, Call(StrEq("Before main thread cleanup")));

    EXPECT_CALL(checkpoint, Call(StrEq("NonShutdownRunnable_OnShutdown::Run")));
    EXPECT_CALL(checkpoint, Call(StrEq("~NonShutdownRunnable_OnShutdown")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("ShutdownRunnable_OnShutdown::OnDiscard")));
    EXPECT_CALL(checkpoint, Call(StrEq("~ShutdownRunnable_OnShutdown")));
    EXPECT_CALL(checkpoint, Call(StrEq("ControlOrShutdown_OnShutdown::Run")));

    EXPECT_CALL(checkpoint, Call(StrEq("After main thread cleanup")));

    EXPECT_CALL(checkpoint,
                Call(StrEq("NonShutdownRunnable_AfterShutdown::Run")));
    EXPECT_CALL(checkpoint, Call(StrEq("~NonShutdownRunnable_AfterShutdown")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("ShutdownRunnable_AfterShutdown::OnDiscard")));
    EXPECT_CALL(checkpoint, Call(StrEq("~ShutdownRunnable_AfterShutdown")));
    EXPECT_CALL(checkpoint,
                Call(StrEq("ControlOrShutdown_AfterShutdown::Run")));

    EXPECT_CALL(*processedTrack, RemoveListenerImpl);
    EXPECT_CALL(checkpoint, Call(StrEq("Final track removed")));

#ifndef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    EXPECT_CALL(checkpoint, Call(StrEq("NonShutdownRunnable_Empty::Run")));
    EXPECT_CALL(checkpoint, Call(StrEq("~NonShutdownRunnable_Empty")));
    EXPECT_CALL(checkpoint, Call(StrEq("ShutdownRunnable_Empty::OnDiscard")));
    EXPECT_CALL(checkpoint, Call(StrEq("~ShutdownRunnable_Empty")));
    EXPECT_CALL(checkpoint, Call(StrEq("ControlOrShutdown_Empty::Run")));
#endif

    EXPECT_CALL(checkpoint, Call(StrEq("Final call")));
  }

  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    // Add a track to maintain an output-only audio driver.
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    fallbackListener = new OnFallbackListener(processedTrack);
    processedTrack->AddListener(fallbackListener);

    // Dispatch some messages
    EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(
        __func__, [&] { checkpoint.Call("First dispatch"); }))));
    checkpoint.Call("Prior to tail dispatch");
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  DispatchFunction([&] {
    while (fallbackListener->OnFallback()) {
      EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
                MockCubebStream::KeepProcessing::Yes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // The graph is now run by ManualDataCallback().
    checkpoint.Call("Now manual");
  });

  const auto CreateControlMessageRunner([&](const char* aName) {
    return [&, aName] {
      checkpoint.Call(nsPrintfCString("%s::Run", aName).get());
    };
  });
  const auto CreateControlOrShutdownMessageRunner([&](const char* aName) {
    return [&, aName](MediaTrack::IsInShutdown) {
      checkpoint.Call(nsPrintfCString("%s::Run", aName).get());
    };
  });

  DispatchFunction([&] {
    // These will run before shutdown is triggered.
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(
        MakeAndAddRef<TestRunnable>("NonShutdownRunnable_BeforeShutdown",
                                    /*aDiscardable=*/false, checkpoint)));
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(MakeAndAddRef<TestRunnable>(
        "ShutdownRunnable_BeforeShutdown", /*aDiscardable=*/true, checkpoint)));
    graph->QueueControlMessageWithNoShutdown(
        CreateControlMessageRunner("ControlNoShutdown_BeforeShutdown"));
    graph->QueueControlOrShutdownMessage(CreateControlOrShutdownMessageRunner(
        "ControlOrShutdown_BeforeShutdown"));
  });

  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] { graph->ForceShutDown(); });

  DispatchFunction([&] {
    // Process the ForceShutdown message.
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::No);

    checkpoint.Call("Forced shutdown");
  });

  // These will be processed in the main-thread-cleanup stable state runnable.
  DispatchFunction([&] {
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(
        MakeAndAddRef<TestRunnable>("NonShutdownRunnable_OnShutdown",
                                    /*aDiscardable=*/false, checkpoint)));
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(MakeAndAddRef<TestRunnable>(
        "ShutdownRunnable_OnShutdown", /*aDiscardable=*/true, checkpoint)));
    graph->QueueControlMessageWithNoShutdown(
        CreateControlMessageRunner("ControlNoShutdown_OnShutdown"));
    graph->QueueControlOrShutdownMessage(
        CreateControlOrShutdownMessageRunner("ControlOrShutdown_OnShutdown"));

    checkpoint.Call("Before main thread cleanup");
  });

  DispatchFunction([&] {
    checkpoint.Call("After main thread cleanup");

    // These will be run immediately as the graph has already processed shutdown
    // on main thread.
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(
        MakeAndAddRef<TestRunnable>("NonShutdownRunnable_AfterShutdown",
                                    /*aDiscardable=*/false, checkpoint)));
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(MakeAndAddRef<TestRunnable>(
        "ShutdownRunnable_AfterShutdown", /*aDiscardable=*/true, checkpoint)));
    graph->QueueControlMessageWithNoShutdown(
        CreateControlMessageRunner("ControlNoShutdown_AfterShutdown"));
    graph->QueueControlOrShutdownMessage(CreateControlOrShutdownMessageRunner(
        "ControlOrShutdown_AfterShutdown"));

    processedTrack->RemoveListener(fallbackListener);
    processedTrack->Destroy();
  });

  DispatchFunction([&] { checkpoint.Call("Final track removed"); });

  DispatchFunction([&] {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    SAVE_GDB_SLEEP_LOCAL();
    EXPECT_DEATH_WRAP(
        MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(
            MakeAndAddRef<TestRunnable>("NonShutdownRunnable_Empty",
                                        /*aDiscardable=*/false, checkpoint))),
        "");
    EXPECT_DEATH_WRAP(
        MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(MakeAndAddRef<TestRunnable>(
            "ShutdownRunnable_Empty", /*aDiscardable=*/true, checkpoint))),
        "");
    EXPECT_DEATH_WRAP(
        graph->QueueControlMessageWithNoShutdown(
            CreateControlMessageRunner("ControlNoShutdown_Empty")),
        "");
    EXPECT_DEATH_WRAP(
        graph->QueueControlOrShutdownMessage(
            CreateControlOrShutdownMessageRunner("ControlOrShutdown_Empty")),
        "");
    RESTORE_GDB_SLEEP_LOCAL();
#else
    // These will be run immediately as the graph has already processed shutdown
    // on main thread.
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(MakeAndAddRef<TestRunnable>(
        "NonShutdownRunnable_Empty", /*aDiscardable=*/false, checkpoint)));
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(MakeAndAddRef<TestRunnable>(
        "ShutdownRunnable_Empty", /*aDiscardable=*/true, checkpoint)));
    graph->QueueControlMessageWithNoShutdown(
        CreateControlMessageRunner("ControlNoShutdown_Empty"));
    graph->QueueControlOrShutdownMessage(
        CreateControlOrShutdownMessageRunner("ControlOrShutdown_Empty"));
#endif

    checkpoint.Call("Final call");
  });

  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
  ProcessEventQueue();
}

namespace {
// Dispatches to the graph from a microtask. The microtask checkpoint runs
// after XPCOMThreadWrapper::AfterProcessNextEvent has fired the tail
// dispatcher for this turn of the event loop, so the runnable lands in a fresh
// tail dispatcher, and will not drain before the graph processes main thread
// updates in stable state.
class MicroTaskDispatcher final : public MicroTaskRunnable {
 public:
  MicroTaskDispatcher(MediaTrackGraphImpl* aGraph, const char* aName,
                      MockFunction<void(const char*)>& aCheckpoint)
      : mGraph(aGraph), mName(aName), mCheckpoint(aCheckpoint) {}

  MOZ_CAN_RUN_SCRIPT void Run(AutoSlowOperation&) override {
    MOZ_ALWAYS_SUCCEEDS(mGraph->Dispatch(MakeAndAddRef<TestRunnable>(
        mName, /*aDiscardable=*/true, mCheckpoint)));
  }

 private:
  MediaTrackGraphImpl* const mGraph;
  const char* const mName;
  MockFunction<void(const char*)>& mCheckpoint;
};
}  // namespace

// A control message queued from a microtask during the graph's forced-shutdown
// handover stays in the tail dispatcher, since XPCOMThreadWrapper cannot fire
// the tail dispatcher between the microtask checkpoint and stable state.
// This test checks that the graph's stable state task drains such messages.
TEST(TestAudioTrackGraph, TailDispatchFromMicroTaskDuringShutdown)
{
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());

  MockFunction<void(const char* name)> checkpoint;
  EXPECT_CALL(*processedTrack, AddListenerImpl);
  EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
  EXPECT_CALL(*processedTrack, RemoveListenerImpl);
  {
    InSequence s;
    EXPECT_CALL(checkpoint, Call(StrEq("Now manual")));
    EXPECT_CALL(checkpoint, Call(StrEq("Forced shutdown")));
    EXPECT_CALL(checkpoint, Call(StrEq("Before main thread cleanup")));
    EXPECT_CALL(checkpoint, Call(StrEq("After main thread cleanup")));

    // Both of these TestRunnables were queued from a microtask, i.e. after the
    // tail dispatcher had fired for that turn. Neither may be left in its tail
    // dispatcher.
    EXPECT_CALL(checkpoint, Call(StrEq("MicroTask_OnShutdown::OnDiscard")));
    EXPECT_CALL(checkpoint, Call(StrEq("~MicroTask_OnShutdown")));
    EXPECT_CALL(checkpoint, Call(StrEq("MicroTask_AfterShutdown::OnDiscard")));
    EXPECT_CALL(checkpoint, Call(StrEq("~MicroTask_AfterShutdown")));

    EXPECT_CALL(checkpoint, Call(StrEq("Final call")));
  }

  const auto QueueMicroTaskDispatch([&](const char* aName) {
    CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
    MOZ_RELEASE_ASSERT(ccjs);
    ccjs->DispatchToMicroTask(
        MakeAndAddRef<MicroTaskDispatcher>(graph, aName, checkpoint));
  });

  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    fallbackListener = new OnFallbackListener(processedTrack);
    processedTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  DispatchFunction([&] {
    while (fallbackListener->OnFallback()) {
      EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
                MockCubebStream::KeepProcessing::Yes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    checkpoint.Call("Now manual");
  });

  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] { graph->ForceShutDown(); });

  DispatchFunction([&] {
    // Process the ForceShutdown message.
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::No);
    checkpoint.Call("Forced shutdown");
  });

  // The control message dispatched from this microtask will be handled in the
  // main-thread-cleanup stable state runnable.
  DispatchFunction([&] {
    QueueMicroTaskDispatch("MicroTask_OnShutdown");
    checkpoint.Call("Before main thread cleanup");
    // As this function exits, the event loop runs the microtask checkpoint,
    // which dispatches the MicroTask_OnShutdown runnable to the graph. It only
    // reaches the tail dispatcher, which fires in the event loop's next
    // OnProcessNextEvent event, i.e. after "After main thread cleanup". The
    // tail dispatcher firing posts the stable state runnable, which runs after
    // the second microtask. This is why MicroTask_OnShutdown is expected to be
    // processed after the "After main thread cleanup" event.

    // Note that the graph processing the forced shutdown queued a regular
    // runnable to the main thread, to queue the stable state runnable. It gets
    // queued after all the other tasks already queued in the test and is
    // therefore irrelevant.
  });

  DispatchFunction([&] {
    checkpoint.Call("After main thread cleanup");
    QueueMicroTaskDispatch("MicroTask_AfterShutdown");
  });

  DispatchFunction([&] {
    processedTrack->RemoveListener(fallbackListener);
    processedTrack->Destroy();
  });

  DispatchFunction([&] { checkpoint.Call("Final call"); });

  (void)WaitFor(destroyPromise).unwrap()[0];
  ProcessEventQueue();
}

namespace {
class TestShutdownTask final : public nsITargetShutdownTask {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  TestShutdownTask(MediaTrackGraphImpl* aGraph,
                   MockFunction<void(const char*)>& aCheckpoint)
      : mGraph(aGraph), mCheckpoint(aCheckpoint) {}

  void TargetShutdown() override {
    EXPECT_TRUE(NS_IsMainThread());
    // Consumers of the graph as an event target, e.g. ipc::MessageChannel,
    // release-assert on this.
    EXPECT_TRUE(mGraph->IsOnCurrentThread());
    mCheckpoint.Call("TargetShutdown");
  }

 private:
  ~TestShutdownTask() = default;

  const RefPtr<MediaTrackGraphImpl> mGraph;
  MockFunction<void(const char*)>& mCheckpoint;
};
}  // namespace

NS_IMPL_ISUPPORTS(TestShutdownTask, nsITargetShutdownTask)

TEST(TestAudioTrackGraph, TargetShutdownTaskOnMainThread)
{
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  RefPtr<MediaTrackGraphImpl> graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Mocks and expectations.
  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());

  MockFunction<void(const char* name)> checkpoint;
  EXPECT_CALL(*processedTrack, AddListenerImpl);
  EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
  EXPECT_CALL(*processedTrack, RemoveListenerImpl);
  // Run from MediaTrackGraphShutDownRunnable, on the main thread, once the
  // graph has stopped and no longer has a driver to identify its thread with.
  // See bug 2065072.
  EXPECT_CALL(checkpoint, Call(StrEq("TargetShutdown")));
  {
    InSequence s;
    EXPECT_CALL(checkpoint, Call(StrEq("Now manual")));
    EXPECT_CALL(checkpoint, Call(StrEq("Forced shutdown")));
    EXPECT_CALL(checkpoint, Call(StrEq("Final call")));
  }

  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    // Add a track to maintain an output-only audio driver.
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    fallbackListener = new OnFallbackListener(processedTrack);
    processedTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  DispatchFunction([&] {
    while (fallbackListener->OnFallback()) {
      EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
                MockCubebStream::KeepProcessing::Yes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // The graph is now run by ManualDataCallback().
    checkpoint.Call("Now manual");
  });

  RefPtr shutdownTask = new TestShutdownTask(graph, checkpoint);
  DispatchFunction([&] {
    MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(NS_NewRunnableFunction(__func__, [&] {
      // Register from the graph thread, as ipc::MessageChannel::Open() does.
      EXPECT_TRUE(graph->IsOnCurrentThread());
      MOZ_ALWAYS_SUCCEEDS(graph->RegisterShutdownTask(shutdownTask));
    })));
  });

  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] {
    // Run the message that registers the shutdown task.
    EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
  });

  DispatchFunction([&] { graph->ForceShutDown(); });

  DispatchFunction([&] {
    // Process the ForceShutdown message, the graph's final iteration.
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::No);

    checkpoint.Call("Forced shutdown");
  });

  DispatchFunction([&] {
    processedTrack->RemoveListener(fallbackListener);
    processedTrack->Destroy();
  });

  DispatchFunction([&] { checkpoint.Call("Final call"); });

  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
  ProcessEventQueue();
}

namespace {
// Stands in for ipc::MessageChannel::MessageTask. When a dispatched IPC
// message is discarded, MessageTask::Cancel() calls
// ipc::MessageChannel::AssertWorkerThread(), which release-asserts
// IsOnCurrentThread() on the channel's worker thread -- the graph, for a
// channel opened on the graph thread.
class IPCTestRunnable final : public TestRunnable {
 public:
  IPCTestRunnable(MediaTrackGraphImpl* aGraph,
                  MockFunction<void(const char*)>& aCheckpoint)
      : TestRunnable("IPCDispatch", /*aDiscardable=*/true, aCheckpoint),
        mGraph(aGraph) {}

  void OnDiscard() override {
    EXPECT_TRUE(mGraph->IsOnCurrentThread());
    TestRunnable::OnDiscard();
  }

 private:
  ~IPCTestRunnable() = default;

  const RefPtr<MediaTrackGraphImpl> mGraph;
};
}  // namespace

TEST(TestAudioTrackGraph, IOThreadDispatchDuringShutdown)
{
  ASSERT_TRUE(ipc::IOThread::Get());

  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  RefPtr<MediaTrackGraphImpl> graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Mocks and expectations.
  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());

  MockFunction<void(const char* name)> checkpoint;
  EXPECT_CALL(*processedTrack, AddListenerImpl);
  EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(1));
  EXPECT_CALL(*processedTrack, RemoveListenerImpl);
  {
    InSequence s;
    EXPECT_CALL(checkpoint, Call(StrEq("Now manual")));
    EXPECT_CALL(checkpoint, Call(StrEq("Final track removed")));
    // The graph is no longer processing messages, so the dispatch is discarded
    // on the main thread rather than run. For an ipc::MessageChannel this is
    // MessageTask::Cancel(), which asserts that the main thread is now the
    // graph's thread, as IPCTestRunnable::OnDiscard() does here.
    EXPECT_CALL(checkpoint, Call(StrEq("IPCDispatch::OnDiscard")));
    EXPECT_CALL(checkpoint, Call(StrEq("~IPCDispatch")));
  }

  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    // Add a track to maintain an output-only audio driver.
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    fallbackListener = new OnFallbackListener(processedTrack);
    processedTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  DispatchFunction([&] {
    while (fallbackListener->OnFallback()) {
      EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
                MockCubebStream::KeepProcessing::Yes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // The graph is now run by ManualDataCallback().
    checkpoint.Call("Now manual");
  });

  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] { graph->ForceShutDown(); });

  DispatchFunction([&] {
    // Process the ForceShutdown message.
    EXPECT_EQ(stream->ManualDataCallback(0),
              MockCubebStream::KeepProcessing::No);
  });

  // Let the main-thread-cleanup stable state runnable run.
  DispatchFunction([&] {
    processedTrack->RemoveListener(fallbackListener);
    processedTrack->Destroy();
  });

  DispatchFunction([&] { checkpoint.Call("Final track removed"); });

  DispatchFunction([&] {
    // An AudioWorklet's PBackground ipc::MessageChannel is opened on the graph
    // thread, so IPC dispatches to the graph from the IPC I/O thread. Those
    // dispatches are not outlived by a main-thread controlled track or port,
    // since the channel is only closed when the graph shuts down. See bug
    // 2065075.
    MOZ_ALWAYS_SUCCEEDS(ipc::IOThread::Get()->GetEventTarget()->Dispatch(
        NS_NewRunnableFunction(__func__, [graph, &checkpoint] {
          MOZ_ALWAYS_SUCCEEDS(graph->Dispatch(
              MakeAndAddRef<IPCTestRunnable>(graph, checkpoint)));
        })));
  });

  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
  // Flush the I/O thread's graph dispatch, then process what it queued on the
  // main thread.
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchAndSpinEventLoopUntilComplete(
      "TestAudioTrackGraph, IOThreadDispatchDuringShutdown"_ns,
      ipc::IOThread::Get()->GetEventTarget(),
      NS_NewRunnableFunction(__func__, [] {})));
  ProcessEventQueue();
}

TEST(TestAudioTrackGraph, MessageAtomicity)
{
  MockCubeb* cubeb = new MockCubeb(MockCubeb::RunningMode::Manual);
  CubebUtils::ForceSetCubebContext(cubeb->AsCubebContext());

  MediaTrackGraphImpl* graph = MediaTrackGraphImpl::GetInstance(
      MediaTrackGraph::SYSTEM_THREAD_DRIVER, /*Window ID*/ 1,
      CubebUtils::PreferredSampleRate(/* aShouldResistFingerprinting */ false),
      nullptr, AbstractThread::MainThread());

  // Mocks and expectations.
  RefPtr processedTrack = new MockProcessedMediaTrack(graph->GraphRate());

  MockFunction<void(const char* name)> checkpoint;
  {
    InSequence s;
    EXPECT_CALL(*processedTrack, AddListenerImpl);
    EXPECT_CALL(*processedTrack, ProcessInput).Times(AtLeast(0));
    // All "Main" graph tasks dispatch in one group and run atomically.
    EXPECT_CALL(checkpoint, Call(StrEq("Main"))).Times(500);
    // All "Other" tq tasks dispatch in one group, then dispatch in one group to
    // the graph, and run atomically.
    EXPECT_CALL(checkpoint, Call(StrEq("Other"))).Times(500);
    EXPECT_CALL(*processedTrack, ProcessInput);
    EXPECT_CALL(*processedTrack, RemoveListenerImpl);
  }

  // Add a track to maintain an output-only audio driver.
  RefPtr<OnFallbackListener> fallbackListener;
  DispatchFunction([&] {
    graph->AddTrack(processedTrack, {});
    processedTrack->AddAudioOutput(reinterpret_cast<void*>(1), nullptr);
    fallbackListener = new OnFallbackListener(processedTrack);
    processedTrack->AddListener(fallbackListener);
  });

  RefPtr<SmartMockCubebStream> stream = WaitFor(cubeb->StreamInitEvent());
  while (stream->State().isNothing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(*stream->State(), CUBEB_STATE_STARTED);
  // Wait for the AudioCallbackDriver to come into effect.
  DispatchFunction([&] {
    while (fallbackListener->OnFallback()) {
      EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
                MockCubebStream::KeepProcessing::Yes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  // The graph is now run by ManualDataCallback().

  // Run the setup tasks in the graph. They run prior to processing tracks.
  DispatchFunction([&] {
    EXPECT_EQ(stream->ManualDataCallback(2 * WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
  });

  auto tq =
      TaskQueue::Create(GetMediaThreadPool(MediaThreadType::WEBRTC_WORKER),
                        __func__, TailDispatchPolicy::ConsistentOrdering);

  // Dispatch task A to the graph, then task B to another tail-dispatchable
  // target, then task C to the graph again. Tail dispatch preserves target
  // dispatch ordering by default, guaranteeing that a task dispatched from B to
  // the graph cannot run before A.
  // The graph requires task atomicity however, meaning that A and C must run in
  // the same graph iteration.
  // This is hard to test because the graph drains all direct tasks at the end
  // of the iteration, rather than in between tasks. Do *many* dispatches and
  // rely on racing with the other target dispatching to the graph, instead.
  DispatchFunction([&] {
    for (size_t i = 0; i < 500; ++i) {
      EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(
          NS_NewRunnableFunction(__func__, [&] { checkpoint.Call("Main"); }))));
      EXPECT_TRUE(
          NS_SUCCEEDED(tq->Dispatch(NS_NewRunnableFunction(__func__, [&] {
            EXPECT_TRUE(NS_SUCCEEDED(graph->Dispatch(NS_NewRunnableFunction(
                "TaskQueue.Task", [&] { checkpoint.Call("Other"); }))));
          }))));
    }
  });

  // Run the dispatched functions with tail dispatch after each.
  ProcessEventQueue();

  // Wait for tq to do all its dispatches.
  WaitForMirrors(tq);

  // Run graph runnables, at beginning of iteration.
  DispatchFunction([&] {
    EXPECT_EQ(stream->ManualDataCallback(WEBAUDIO_BLOCK_SIZE),
              MockCubebStream::KeepProcessing::Yes);
  });

  DispatchFunction([&] {
    processedTrack->RemoveListener(fallbackListener);
    processedTrack->Destroy();
  });

  // Process the destroy message and drain the stream.
  auto destroyPromise = TakeN(cubeb->StreamDestroyEvent(), 1);
  DispatchFunction([&] {
    while (stream->ManualDataCallback(0) ==
           MockCubebStream::KeepProcessing::Yes) {
    }
  });
  // Ensure the stream is no longer used by its MockCubeb before releasing our
  // reference, and before the next test might ForceSetCubebContext() to
  // destroy our cubeb.
  (void)WaitFor(destroyPromise).unwrap()[0];
}

#undef InvokeAsync
#undef TEST_WithTailDispatch
#undef DispatchFunction
#undef DispatchMethod
#undef ProcessEventQueue
