/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "DeviceInputTrack.h"

#include "Tracing.h"

namespace mozilla {

#ifdef LOG_INTERNAL
#  undef LOG_INTERNAL
#endif  // LOG_INTERNAL
#define LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG_FMT(gMediaTrackGraphLog, LogLevel::level, msg, ##__VA_ARGS__)

#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(msg, ...) LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef LOGE
#  undef LOGE
#endif  // LOGE
#define LOGE(msg, ...) LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

// This can only be called in graph thread since mGraph->CurrentDriver() is
// graph thread only
#ifdef TRACK_GRAPH_LOG_INTERNAL
#  undef TRACK_GRAPH_LOG_INTERNAL
#endif  // TRACK_GRAPH_LOG_INTERNAL
#define TRACK_GRAPH_LOG_INTERNAL(level, msg, ...)                        \
  LOG_INTERNAL(level, "(Graph {}, Driver {}) DeviceInputTrack {}, " msg, \
               fmt::ptr(this->mGraph),                                   \
               fmt::ptr(this->mGraph->CurrentDriver()), fmt::ptr(this),  \
               ##__VA_ARGS__)

#ifdef TRACK_GRAPH_LOG
#  undef TRACK_GRAPH_LOG
#endif  // TRACK_GRAPH_LOG
#define TRACK_GRAPH_LOG(msg, ...) \
  TRACK_GRAPH_LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef TRACK_GRAPH_LOGV
#  undef TRACK_GRAPH_LOGV
#endif  // TRACK_GRAPH_LOGV
#define TRACK_GRAPH_LOGV(msg, ...) \
  TRACK_GRAPH_LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

#ifdef TRACK_GRAPH_LOGE
#  undef TRACK_GRAPH_LOGE
#endif  // TRACK_GRAPH_LOGE
#define TRACK_GRAPH_LOGE(msg, ...) \
  TRACK_GRAPH_LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

#ifdef CONSUMER_GRAPH_LOG_INTERNAL
#  undef CONSUMER_GRAPH_LOG_INTERNAL
#endif  // CONSUMER_GRAPH_LOG_INTERNAL
#define CONSUMER_GRAPH_LOG_INTERNAL(level, msg, ...)                    \
  LOG_INTERNAL(                                                         \
      level, "(Graph {}, Driver {}) DeviceInputConsumerTrack {}, " msg, \
      fmt::ptr(this->mGraph), fmt::ptr(this->mGraph->CurrentDriver()),  \
      fmt::ptr(this), ##__VA_ARGS__)

#ifdef CONSUMER_GRAPH_LOGV
#  undef CONSUMER_GRAPH_LOGV
#endif  // CONSUMER_GRAPH_LOGV
#define CONSUMER_GRAPH_LOGV(msg, ...) \
  CONSUMER_GRAPH_LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

DeviceInputConsumerTrack::DeviceInputConsumerTrack(TrackRate aSampleRate)
    : ProcessedMediaTrack(aSampleRate, MediaSegment::AUDIO,
                          new AudioSegment()) {}

void DeviceInputConsumerTrack::ConnectDeviceInput(
    CubebUtils::AudioDeviceID aId, AudioDataListener* aListener,
    const PrincipalHandle& aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(Graph());
  MOZ_ASSERT(aListener);
  MOZ_ASSERT(!mListener);
  MOZ_ASSERT(!mDeviceInputTrack);
  MOZ_ASSERT(mDeviceId.isNothing());
  MOZ_ASSERT(!mDeviceInputTrack,
             "Must disconnect a device input before connecting a new one");

  mListener = aListener;
  mDeviceId.emplace(aId);

  mDeviceInputTrack =
      DeviceInputTrack::OpenAudio(Graph(), aId, aPrincipal, this);
  LOG("Open device {} (DeviceInputTrack {}) for consumer {}", fmt::ptr(aId),
      fmt::ptr(mDeviceInputTrack.get()), fmt::ptr(this));
  mPort = AllocateInputPort(mDeviceInputTrack.get());
}

void DeviceInputConsumerTrack::DisconnectDeviceInput() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(Graph());

  if (!mListener) {
    MOZ_ASSERT(mDeviceId.isNothing());
    MOZ_ASSERT(!mDeviceInputTrack);
    return;
  }

  MOZ_ASSERT(mPort);
  MOZ_ASSERT(mDeviceInputTrack);
  MOZ_ASSERT(mDeviceId.isSome());

  LOG("Close device {} (DeviceInputTrack {}) for consumer {} ",
      fmt::ptr(*mDeviceId), fmt::ptr(mDeviceInputTrack.get()), fmt::ptr(this));
  mPort->Destroy();
  DeviceInputTrack::CloseAudio(mDeviceInputTrack.forget(), this);
  mListener = nullptr;
  mDeviceId = Nothing();
}

Maybe<CubebUtils::AudioDeviceID> DeviceInputConsumerTrack::DeviceId() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDeviceId;
}

NotNull<AudioDataListener*> DeviceInputConsumerTrack::GetAudioDataListener()
    const {
  MOZ_ASSERT(NS_IsMainThread());
  return WrapNotNull(mListener.get());
}

bool DeviceInputConsumerTrack::ConnectedToNativeDevice() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDeviceInputTrack && mDeviceInputTrack->AsNativeInputTrack();
}

bool DeviceInputConsumerTrack::ConnectedToNonNativeDevice() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDeviceInputTrack && mDeviceInputTrack->AsNonNativeInputTrack();
}

DeviceInputTrack* DeviceInputConsumerTrack::GetDeviceInputTrackGraphThread()
    const {
  AssertOnGraphThread();

  if (mInputs.IsEmpty()) {
    return nullptr;
  }
  MOZ_ASSERT(mInputs.Length() == 1);
  MediaTrack* track = mInputs[0]->GetSource();
  MOZ_ASSERT(track->AsDeviceInputTrack());
  return static_cast<DeviceInputTrack*>(track);
}

void DeviceInputConsumerTrack::GetInputSourceData(AudioSegment& aOutput,
                                                  GraphTime aFrom,
                                                  GraphTime aTo) const {
  AssertOnGraphThread();
  MOZ_ASSERT(aOutput.IsEmpty());
  MOZ_ASSERT(mInputs.Length() == 1);

  MediaInputPort* port = mInputs[0];
  MediaTrack* source = port->GetSource();
  GraphTime next;
  for (GraphTime t = aFrom; t < aTo; t = next) {
    MediaInputPort::InputInterval interval =
        MediaInputPort::GetNextInputInterval(port, t);
    interval.mEnd = std::min(interval.mEnd, aTo);

    const bool inputEnded =
        source->Ended() &&
        source->GetEnd() <=
            source->GraphTimeToTrackTimeWithBlocking(interval.mStart);

    TrackTime ticks = interval.mEnd - interval.mStart;
    next = interval.mEnd;

    if (interval.mStart >= interval.mEnd) {
      break;
    }

    if (inputEnded) {
      aOutput.AppendNullData(ticks);
      CONSUMER_GRAPH_LOGV(
          "Getting {} ticks of null data from input port source (ended input)",
          ticks);
    } else if (interval.mInputIsBlocked) {
      aOutput.AppendNullData(ticks);
      CONSUMER_GRAPH_LOGV(
          "Getting {} ticks of null data from input port source (blocked "
          "input)",
          ticks);
    } else if (source->IsSuspended()) {
      aOutput.AppendNullData(ticks);
      CONSUMER_GRAPH_LOGV(
          "Getting {} ticks of null data from input port source (source is "
          "suspended)",
          ticks);
    } else {
      TrackTime start =
          source->GraphTimeToTrackTimeWithBlocking(interval.mStart);
      TrackTime end = source->GraphTimeToTrackTimeWithBlocking(interval.mEnd);
      MOZ_ASSERT(source->GetData<AudioSegment>()->GetDuration() >= end);
      aOutput.AppendSlice(*source->GetData<AudioSegment>(), start, end);
      CONSUMER_GRAPH_LOGV(
          "Getting {} ticks of real data from input port source {}",
          end - start, fmt::ptr(source));
    }
  }
}

/* static */
NotNull<RefPtr<DeviceInputTrack>> DeviceInputTrack::OpenAudio(
    MediaTrackGraph* aGraph, CubebUtils::AudioDeviceID aDeviceId,
    const PrincipalHandle& aPrincipalHandle,
    DeviceInputConsumerTrack* aConsumer) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aConsumer);
  MOZ_ASSERT(aGraph == aConsumer->Graph());

  RefPtr<DeviceInputTrack> track =
      aGraph->GetDeviceInputTrackMainThread(aDeviceId);
  if (track) {
    MOZ_ASSERT(!track->mConsumerTracks.IsEmpty());
    track->AddDataListener(aConsumer->GetAudioDataListener());
  } else {
    // Create a NativeInputTrack or NonNativeInputTrack, depending on whether
    // the given graph already has a native device or not.
    if (aGraph->GetNativeInputTrackMainThread()) {
      // A native device is already in use. This device will be a non-native
      // device.
      track = new NonNativeInputTrack(aGraph->GraphRate(), aDeviceId,
                                      aPrincipalHandle);
    } else {
      // No native device is in use. This device will be the native device.
      track = new NativeInputTrack(aGraph->GraphRate(), aDeviceId,
                                   aPrincipalHandle);
    }
    LOG("Create {}NativeInputTrack {} in MTG {} for device {}",
        (track->AsNativeInputTrack() ? "" : "Non"), fmt::ptr(track.get()),
        fmt::ptr(aGraph), fmt::ptr(aDeviceId));
    aGraph->AddTrack(track, {});
    // Add the listener before opening the device so the device passed to
    // OpenAudioInput always has a non-zero input channel count.
    track->AddDataListener(aConsumer->GetAudioDataListener());
    aGraph->OpenAudioInput(track);
  }
  MOZ_ASSERT(track->AsNativeInputTrack() || track->AsNonNativeInputTrack());
  MOZ_ASSERT(track->mDeviceId == aDeviceId);

  MOZ_ASSERT(!track->mConsumerTracks.Contains(aConsumer));
  track->mConsumerTracks.AppendElement(aConsumer);

  LOG("DeviceInputTrack {} (device {}: {}native) in MTG {} has {} users now",
      fmt::ptr(track.get()), fmt::ptr(track->mDeviceId),
      (track->AsNativeInputTrack() ? "" : "non-"), fmt::ptr(aGraph),
      track->mConsumerTracks.Length());
  if (track->mConsumerTracks.Length() > 1) {
    track->ReevaluateInputDevice();
  }

  return WrapNotNull(track);
}

/* static */
void DeviceInputTrack::CloseAudio(already_AddRefed<DeviceInputTrack> aTrack,
                                  DeviceInputConsumerTrack* aConsumer) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<DeviceInputTrack> track = aTrack;
  MOZ_ASSERT(track);

  track->RemoveDataListener(aConsumer->GetAudioDataListener());
  DebugOnly<bool> removed = track->mConsumerTracks.RemoveElement(aConsumer);
  MOZ_ASSERT(removed);
  LOG("DeviceInputTrack {} (device {}) in MTG {} has {} users now",
      fmt::ptr(track.get()), fmt::ptr(track->mDeviceId),
      fmt::ptr(track->Graph()), track->mConsumerTracks.Length());
  if (track->mConsumerTracks.IsEmpty()) {
    track->Graph()->CloseAudioInput(track);
    track->Destroy();
  } else {
    track->ReevaluateInputDevice();
  }
}

const nsTArray<RefPtr<DeviceInputConsumerTrack>>&
DeviceInputTrack::GetConsumerTracks() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mConsumerTracks;
}

DeviceInputTrack::DeviceInputTrack(TrackRate aSampleRate,
                                   CubebUtils::AudioDeviceID aDeviceId,
                                   const PrincipalHandle& aPrincipalHandle)
    : ProcessedMediaTrack(aSampleRate, MediaSegment::AUDIO, new AudioSegment()),
      mDeviceId(aDeviceId),
      mPrincipalHandle(aPrincipalHandle) {}

uint32_t DeviceInputTrack::MaxRequestedInputChannels() const {
  AssertOnGraphThreadOrNotRunning();
  uint32_t maxInputChannels = 0;
  for (const auto& listener : mListeners) {
    maxInputChannels = std::max(maxInputChannels,
                                listener->RequestedInputChannelCount(mGraph));
  }
  return maxInputChannels;
}

bool DeviceInputTrack::HasVoiceInput() const {
  AssertOnGraphThreadOrNotRunning();
  for (const auto& listener : mListeners) {
    if (listener->IsVoiceInput(mGraph)) {
      return true;
    }
  }
  return false;
}

AudioInputProcessingParamsRequest
DeviceInputTrack::UpdateRequestedProcessingParams() {
  AssertOnGraphThreadOrNotRunning();
  Maybe<cubeb_input_processing_params> params;
  for (const auto& listener : mListeners) {
    if (params) {
      *params &= listener->RequestedInputProcessingParams(mGraph);
    } else {
      params = Some(listener->RequestedInputProcessingParams(mGraph));
    }
  }

  if (auto p = params.valueOr(CUBEB_INPUT_PROCESSING_PARAM_NONE);
      p != mProcessingParamsRequest.mParams) {
    mProcessingParamsRequest.mParams = p;
    mProcessingParamsRequest.mGeneration =
        Graph()->ProcessingParamsGeneration();

    TRACK_GRAPH_LOG(
        "{}NativeInputTrack notifying of setting requested processing params "
        "{} (Gen {})",
        (AsNonNativeInputTrack() ? "Non" : ""),
        CubebUtils::ProcessingParamsToString(mProcessingParamsRequest.mParams)
            .get(),
        mProcessingParamsRequest.mGeneration);

    NotifySetRequestedProcessingParams(Graph(),
                                       mProcessingParamsRequest.mGeneration,
                                       mProcessingParamsRequest.mParams);
  }

  return mProcessingParamsRequest;
}

void DeviceInputTrack::DeviceChanged(MediaTrackGraph* aGraph) const {
  AssertOnGraphThreadOrNotRunning();
  MOZ_ASSERT(aGraph == mGraph,
             "Receive device changed signal from another graph");
  TRACK_GRAPH_LOG("DeviceChanged");
  for (const auto& listener : mListeners) {
    listener->DeviceChanged(aGraph);
  }
}

void DeviceInputTrack::NotifySetRequestedProcessingParams(
    MediaTrackGraph* aGraph, int aGeneration,
    cubeb_input_processing_params aRequestedParams) {
  AssertOnGraphThread();
  for (const auto& listener : mListeners) {
    listener->NotifySetRequestedInputProcessingParams(mGraph, aGeneration,
                                                      aRequestedParams);
  }
}

void DeviceInputTrack::NotifySetRequestedProcessingParamsResult(
    MediaTrackGraph* aGraph, int aGeneration,
    const Result<cubeb_input_processing_params, int>& aResult) {
  AssertOnGraphThread();
  for (const auto& listener : mListeners) {
    listener->NotifySetRequestedInputProcessingParamsResult(mGraph, aGeneration,
                                                            aResult);
  }
}

void DeviceInputTrack::ReevaluateInputDevice() {
  MOZ_ASSERT(NS_IsMainThread());
  QueueControlMessageWithNoShutdown([self = RefPtr{this}, this] {
    TRACE("DeviceInputTrack::ReevaluateInputDevice ControlMessage");
    Graph()->ReevaluateInputDevice(mDeviceId);
  });
}

void DeviceInputTrack::AddDataListener(AudioDataListener* aListener) {
  MOZ_ASSERT(NS_IsMainThread());
  QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, this, listener = RefPtr{aListener}] {
        TRACE("DeviceInputTrack::AddDataListener ControlMessage");
        MOZ_ASSERT(!mListeners.Contains(listener.get()),
                   "Don't add a listener twice.");
        mListeners.AppendElement(listener.get());
      });
}

void DeviceInputTrack::RemoveDataListener(AudioDataListener* aListener) {
  MOZ_ASSERT(NS_IsMainThread());
  QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, this, listener = RefPtr{aListener}] {
        TRACE("DeviceInputTrack::RemoveDataListener ControlMessage");
        DebugOnly<bool> wasPresent = mListeners.RemoveElement(listener.get());
        MOZ_ASSERT(wasPresent, "Remove an unknown listener");
        listener->Disconnect(Graph());
      });
}

NativeInputTrack::NativeInputTrack(TrackRate aSampleRate,
                                   CubebUtils::AudioDeviceID aDeviceId,
                                   const PrincipalHandle& aPrincipalHandle)
    : DeviceInputTrack(aSampleRate, aDeviceId, aPrincipalHandle),
      mIsBufferingAppended(false),
      mInputChannels(0) {}

void NativeInputTrack::DestroyImpl() {
  AssertOnGraphThreadOrNotRunning();
  mPendingData.Clear();
  ProcessedMediaTrack::DestroyImpl();
}

void NativeInputTrack::ProcessInput(GraphTime aFrom, GraphTime aTo,
                                    uint32_t aFlags) {
  AssertOnGraphThread();
  TRACE_COMMENT("NativeInputTrack::ProcessInput", "%p", this);

  TRACK_GRAPH_LOGV("(Native) ProcessInput from {} to {}, needs {} frames",
                   aFrom, aTo, aTo - aFrom);

  TrackTime from = GraphTimeToTrackTime(aFrom);
  TrackTime to = GraphTimeToTrackTime(aTo);
  MOZ_ASSERT(from < to);

  MOZ_ASSERT_IF(!mIsBufferingAppended, mPendingData.IsEmpty());

  TrackTime need = to - from;
  TrackTime dataNeed = std::min(mPendingData.GetDuration(), need);
  TrackTime silenceNeed = std::max(need - dataNeed, (TrackTime)0);

  // TODO (bug 1879353): Reenable assertion.
  // MOZ_ASSERT_IF(dataNeed > 0, silenceNeed == 0);

  GetData<AudioSegment>()->AppendSlice(mPendingData, 0, dataNeed);
  mPendingData.RemoveLeading(dataNeed);
  GetData<AudioSegment>()->AppendNullData(silenceNeed);

  // TODO (bug 1879353): Remove as assertion above will hold.
  if (dataNeed > 0 && silenceNeed > 0) {
    NotifyInputStopped(mGraph);
  }
}

uint32_t NativeInputTrack::NumberOfChannels() const {
  AssertOnGraphThreadOrNotRunning();
  return mInputChannels;
}

void NativeInputTrack::NotifyInputStopped(MediaTrackGraph* aGraph) {
  AssertOnGraphThreadOrNotRunning();
  MOZ_ASSERT(aGraph == mGraph,
             "Receive input stopped signal from another graph");
  TRACK_GRAPH_LOG("(Native) NotifyInputStopped");
  mInputChannels = 0;
  mIsBufferingAppended = false;
  mPendingData.Clear();
}

void NativeInputTrack::NotifyInputData(MediaTrackGraph* aGraph,
                                       const AudioDataValue* aBuffer,
                                       size_t aFrames, TrackRate aRate,
                                       uint32_t aChannels,
                                       uint32_t aAlreadyBuffered) {
  AssertOnGraphThread();
  MOZ_ASSERT(aGraph == mGraph, "Receive input data from another graph");
  TRACK_GRAPH_LOGV(
      "NotifyInputData: frames={}, rate={}, channel={}, alreadyBuffered={}",
      aFrames, aRate, aChannels, aAlreadyBuffered);

  if (!mIsBufferingAppended) {
    // First time we see live frames getting added. Use what's already buffered
    // in the driver's scratch buffer as a starting point.
    MOZ_ASSERT(mPendingData.IsEmpty());
    constexpr TrackTime buffering = WEBAUDIO_BLOCK_SIZE;
    const TrackTime remaining =
        buffering - static_cast<TrackTime>(aAlreadyBuffered);
    mPendingData.AppendNullData(remaining);
    mIsBufferingAppended = true;
    TRACK_GRAPH_LOG("Set mIsBufferingAppended by appending {} frames.",
                    remaining);
  }

  MOZ_ASSERT(aChannels);
  if (!mInputChannels) {
    mInputChannels = aChannels;
  }
  mPendingData.AppendFromInterleavedBuffer(aBuffer, aFrames, aChannels,
                                           mPrincipalHandle);
}

NonNativeInputTrack::NonNativeInputTrack(
    TrackRate aSampleRate, CubebUtils::AudioDeviceID aDeviceId,
    const PrincipalHandle& aPrincipalHandle)
    : DeviceInputTrack(aSampleRate, aDeviceId, aPrincipalHandle),
      mAudioSource(nullptr),
      mSourceIdNumber(0) {}

void NonNativeInputTrack::DestroyImpl() {
  AssertOnGraphThreadOrNotRunning();
  if (mAudioSource) {
    mAudioSource->Stop();
    mAudioSource = nullptr;
  }
  ProcessedMediaTrack::DestroyImpl();
}

void NonNativeInputTrack::ProcessInput(GraphTime aFrom, GraphTime aTo,
                                       uint32_t aFlags) {
  AssertOnGraphThread();
  TRACE_COMMENT("NonNativeInputTrack::ProcessInput", "%p", this);

  TRACK_GRAPH_LOGV("(NonNative) ProcessInput from {} to {}, needs {} frames",
                   aFrom, aTo, aTo - aFrom);

  TrackTime from = GraphTimeToTrackTime(aFrom);
  TrackTime to = GraphTimeToTrackTime(aTo);
  MOZ_ASSERT(from < to);

  TrackTime delta = to - from;
  if (!mAudioSource) {
    GetData<AudioSegment>()->AppendNullData(delta);
    return;
  }

  AudioInputSource::Consumer consumer = AudioInputSource::Consumer::Same;
  // GraphRunner keeps the same thread.
  MOZ_ASSERT(!HasGraphThreadChanged());

  ReevaluateProcessingParams();

  AudioSegment data = mAudioSource->GetAudioSegment(delta, consumer);
  MOZ_ASSERT(data.GetDuration() == delta);
  GetData<AudioSegment>()->AppendFrom(&data);
}

uint32_t NonNativeInputTrack::NumberOfChannels() const {
  AssertOnGraphThreadOrNotRunning();
  return mAudioSource ? mAudioSource->mChannelCount : 0;
}

void NonNativeInputTrack::StartAudio(
    RefPtr<AudioInputSource>&& aAudioInputSource) {
  AssertOnGraphThread();
  MOZ_ASSERT(aAudioInputSource->mPrincipalHandle == mPrincipalHandle);
  MOZ_ASSERT(aAudioInputSource->mDeviceId == mDeviceId);

  TRACK_GRAPH_LOG("StartAudio with source {}",
                  fmt::ptr(aAudioInputSource.get()));
#ifdef DEBUG
  mGraphThreadId = std::this_thread::get_id();
#endif
  mAudioSource = std::move(aAudioInputSource);
  mAudioSource->Init();
  ReevaluateProcessingParams();
  mAudioSource->Start();
}

void NonNativeInputTrack::StopAudio() {
  AssertOnGraphThread();

  TRACK_GRAPH_LOG("StopAudio from source {}", fmt::ptr(mAudioSource.get()));
  if (!mAudioSource) {
    return;
  }
  mAudioSource->Stop();
  mAudioSource = nullptr;
#ifdef DEBUG
  mGraphThreadId = std::thread::id();
#endif
}

AudioInputType NonNativeInputTrack::DevicePreference() const {
  AssertOnGraphThreadOrNotRunning();
  return mAudioSource && mAudioSource->mIsVoice ? AudioInputType::Voice
                                                : AudioInputType::Unknown;
}

void NonNativeInputTrack::NotifyDeviceChanged(uint32_t aSourceId) {
  AssertOnGraphThreadOrNotRunning();

  // No need to forward the notification if the audio input has been stopped or
  // restarted by it users.
  if (!mAudioSource || mAudioSource->mId != aSourceId) {
    TRACK_GRAPH_LOG("(NonNative) NotifyDeviceChanged: No need to forward");
    return;
  }

  TRACK_GRAPH_LOG("(NonNative) NotifyDeviceChanged");
  // Forward the notification.
  DeviceInputTrack::DeviceChanged(mGraph);
}

void NonNativeInputTrack::NotifyInputStopped(uint32_t aSourceId) {
  AssertOnGraphThreadOrNotRunning();

  // No need to forward the notification if the audio input has been stopped or
  // restarted by it users.
  if (!mAudioSource || mAudioSource->mId != aSourceId) {
    TRACK_GRAPH_LOG("(NonNative) NotifyInputStopped: No need to forward");
    return;
  }

  TRACK_GRAPH_LOGE(
      "(NonNative) NotifyInputStopped: audio unexpectedly stopped");
  // Destory the underlying audio stream if it's stopped unexpectedly.
  mAudioSource->Stop();
}

AudioInputSource::Id NonNativeInputTrack::GenerateSourceId() {
  AssertOnGraphThread();
  return mSourceIdNumber++;
}

void NonNativeInputTrack::ReevaluateProcessingParams() {
  AssertOnGraphThread();
  MOZ_ASSERT(mAudioSource);
  auto request = UpdateRequestedProcessingParams();
  if (mRequestedProcessingParamsGeneration == request.mGeneration) {
    return;
  }
  auto generation = mRequestedProcessingParamsGeneration = request.mGeneration;
  auto params = request.mParams;
  using Promise = AudioInputSource::SetRequestedProcessingParamsPromise;
  mAudioSource->SetRequestedProcessingParams(params)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [this, self = RefPtr(this),
       generation](Promise::ResolveOrRejectValue&& aValue) {
        if (IsDestroyed()) {
          return;
        }
        auto result = ([&]() -> Result<cubeb_input_processing_params, int> {
          if (aValue.IsResolve()) {
            return aValue.ResolveValue();
          }
          return Err(aValue.RejectValue());
        })();
        QueueControlMessageWithNoShutdown([this, self = RefPtr(this),
                                           generation,
                                           result = std::move(result)] {
          NotifySetRequestedProcessingParamsResult(Graph(), generation, result);
        });
      });
}

#ifdef DEBUG
bool NonNativeInputTrack::HasGraphThreadChanged() {
  AssertOnGraphThread();

  std::thread::id currentId = std::this_thread::get_id();
  if (mGraphThreadId == currentId) {
    return false;
  }
  mGraphThreadId = currentId;
  return true;
}
#endif  // DEBUG

AudioInputSourceListener::AudioInputSourceListener(NonNativeInputTrack* aOwner)
    : mOwner(aOwner) {}

void AudioInputSourceListener::AudioDeviceChanged(
    AudioInputSource::Id aSourceId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwner);

  if (mOwner->IsDestroyed()) {
    LOG("NonNativeInputTrack {} has been destroyed. No need to forward the "
        "audio device-changed notification",
        fmt::ptr(mOwner.get()));
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(mOwner->Graph());
  mOwner->QueueControlMessageWithNoShutdown([inputTrack = mOwner, aSourceId] {
    TRACE("NonNativeInputTrack::AudioDeviceChanged ControlMessage");
    inputTrack->NotifyDeviceChanged(aSourceId);
  });
}

void AudioInputSourceListener::AudioStateCallback(
    AudioInputSource::Id aSourceId,
    AudioInputSource::EventListener::State aState) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwner);

  const char* state =
      aState == AudioInputSource::EventListener::State::Started   ? "started"
      : aState == AudioInputSource::EventListener::State::Stopped ? "stopped"
      : aState == AudioInputSource::EventListener::State::Drained ? "drained"
                                                                  : "error";

  if (mOwner->IsDestroyed()) {
    LOG("NonNativeInputTrack {} has been destroyed. No need to forward the "
        "audio state-changed({}) notification",
        fmt::ptr(mOwner.get()), state);
    return;
  }

  if (aState == AudioInputSource::EventListener::State::Started) {
    LOG("We can ignore {} notification for NonNativeInputTrack {}", state,
        fmt::ptr(mOwner.get()));
    return;
  }

  LOG("Notify audio stopped due to entering {} state", state);

  MOZ_DIAGNOSTIC_ASSERT(mOwner->Graph());
  mOwner->QueueControlMessageWithNoShutdown([inputTrack = mOwner, aSourceId] {
    TRACE("NonNativeInputTrack::AudioStateCallback ControlMessage");
    inputTrack->NotifyInputStopped(aSourceId);
  });
}

#undef LOG_INTERNAL
#undef LOG
#undef LOGE
#undef TRACK_GRAPH_LOG_INTERNAL
#undef TRACK_GRAPH_LOG
#undef TRACK_GRAPH_LOGV
#undef TRACK_GRAPH_LOGE
#undef CONSUMER_GRAPH_LOG_INTERNAL
#undef CONSUMER_GRAPH_LOGV

}  // namespace mozilla
