/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConstantSourceNode.h"

#include "AudioDestinationNode.h"
#include "AudioNodeEngine.h"
#include "AudioNodeTrack.h"
#include "Tracing.h"
#include "nsContentUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(ConstantSourceNode, AudioScheduledSourceNode,
                                   mOffset)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ConstantSourceNode)
NS_INTERFACE_MAP_END_INHERITING(AudioScheduledSourceNode)

NS_IMPL_ADDREF_INHERITED(ConstantSourceNode, AudioScheduledSourceNode)
NS_IMPL_RELEASE_INHERITED(ConstantSourceNode, AudioScheduledSourceNode)

class ConstantSourceNodeEngine final : public AudioNodeEngine {
 public:
  ConstantSourceNodeEngine(AudioNode* aNode, AudioDestinationNode* aDestination)
      : AudioNodeEngine(aNode),
        mSource(nullptr),
        mDestination(aDestination->Track()),
        mStart(-1),
        mStop(TRACK_TIME_MAX)
        // Keep the default values in sync with
        // ConstantSourceNode::ConstantSourceNode.
        ,
        mOffset(1.0f) {
    MOZ_ASSERT(NS_IsMainThread());
  }

  void SetSourceTrack(AudioNodeTrack* aSource) { mSource = aSource; }

  enum Parameters {
    OFFSET,
    START,
    STOP,
  };
  void RecvTimelineEvent(uint32_t aIndex, AudioParamEvent& aEvent) override {
    MOZ_ASSERT(mDestination);

    aEvent.ConvertToTicks(mDestination);

    switch (aIndex) {
      case OFFSET:
        mOffset.InsertEvent<int64_t>(aEvent);
        break;
      default:
        NS_ERROR("Bad ConstantSourceNodeEngine TimelineParameter");
    }
  }

  void SetTrackTimeParameter(uint32_t aIndex, TrackTime aParam) override {
    switch (aIndex) {
      case START:
        mStart = aParam;
        mSource->SetActive();
        break;
      case STOP:
        mStop = aParam;
        break;
      default:
        NS_ERROR("Bad ConstantSourceNodeEngine TrackTimeParameter");
    }
  }

  void ProcessBlock(AudioNodeTrack* aTrack, GraphTime aFrom,
                    const AudioBlock& aInput, AudioBlock* aOutput,
                    bool* aFinished) override {
    MOZ_ASSERT(mSource == aTrack, "Invalid source track");
    TRACE("ConstantSourceNodeEngine::ProcessBlock");

    TrackTime ticks = mDestination->GraphTimeToTrackTime(aFrom);
    if (mStart == -1) {
      aOutput->SetNull(WEBAUDIO_BLOCK_SIZE);
      return;
    }

    if (ticks + WEBAUDIO_BLOCK_SIZE <= mStart || ticks >= mStop ||
        mStop <= mStart) {
      aOutput->SetNull(WEBAUDIO_BLOCK_SIZE);
    } else {
      aOutput->AllocateChannels(1);
      float* output = aOutput->ChannelFloatsForWrite(0);
      uint32_t writeOffset = 0;

      if (ticks < mStart) {
        MOZ_ASSERT(mStart - ticks <= WEBAUDIO_BLOCK_SIZE);
        uint32_t count = mStart - ticks;
        std::fill_n(output, count, 0.0f);
        writeOffset += count;
      }

      MOZ_ASSERT(ticks + writeOffset >= mStart);
      MOZ_ASSERT(mStop - ticks >= writeOffset);
      uint32_t count =
          std::min<TrackTime>(WEBAUDIO_BLOCK_SIZE, mStop - ticks) - writeOffset;

      if (mOffset.HasSimpleValue()) {
        float value = mOffset.GetValue();
        std::fill_n(output + writeOffset, count, value);
      } else {
        mOffset.GetValuesAtTime(ticks + writeOffset, output + writeOffset,
                                count);
      }

      writeOffset += count;

      std::fill_n(output + writeOffset, WEBAUDIO_BLOCK_SIZE - writeOffset,
                  0.0f);
    }

    if (ticks + WEBAUDIO_BLOCK_SIZE >= mStop) {
      // We've finished playing.
      *aFinished = true;
    }
  }

  bool IsActive() const override {
    // start() has been called.
    return mStart != -1;
  }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override {
    size_t amount = AudioNodeEngine::SizeOfExcludingThis(aMallocSizeOf);

    // Not owned:
    // - mSource
    // - mDestination
    // - mOffset (internal ref owned by node)

    return amount;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  // mSource deletes the engine in its destructor.
  AudioNodeTrack* MOZ_NON_OWNING_REF mSource;
  RefPtr<AudioNodeTrack> mDestination;
  TrackTime mStart;
  TrackTime mStop;
  AudioParamTimeline mOffset;
};

ConstantSourceNode::ConstantSourceNode(AudioContext* aContext)
    : AudioScheduledSourceNode(aContext, 2, ChannelCountMode::Max,
                               ChannelInterpretation::Speakers),
      mStartCalled(false),
      mWatchManager(this, AbstractThread::MainThread()),
      mTrackEnded(AbstractThread::MainThread(), false,
                  "ConstantSourceNode::mTrackEnded") {
  mOffset =
      CreateAudioParam(ConstantSourceNodeEngine::OFFSET, u"offset"_ns, 1.0f);
  ConstantSourceNodeEngine* engine =
      new ConstantSourceNodeEngine(this, aContext->Destination());
  mTrack = AudioNodeTrack::Create(aContext, engine,
                                  AudioNodeTrack::NEED_MAIN_THREAD_ENDED,
                                  aContext->Graph());
  engine->SetSourceTrack(mTrack);
  mTrackEnded.Connect(&mTrack->CanonicalEnded());
  mWatchManager.Watch(mTrackEnded, &ConstantSourceNode::OnTrackEnded);
}

ConstantSourceNode::~ConstantSourceNode() = default;

size_t ConstantSourceNode::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t amount = AudioNode::SizeOfExcludingThis(aMallocSizeOf);

  amount += mOffset->SizeOfIncludingThis(aMallocSizeOf);
  return amount;
}

size_t ConstantSourceNode::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

JSObject* ConstantSourceNode::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return ConstantSourceNode_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<ConstantSourceNode> ConstantSourceNode::Constructor(
    const GlobalObject& aGlobal, AudioContext& aContext,
    const ConstantSourceOptions& aOptions) {
  RefPtr<ConstantSourceNode> object = new ConstantSourceNode(&aContext);
  object->mOffset->SetInitialValue(aOptions.mOffset);
  return object.forget();
}

void ConstantSourceNode::DestroyMediaTrack() {
  mWatchManager.Shutdown();
  mTrackEnded.DisconnectIfConnected();
  AudioNode::DestroyMediaTrack();
}

void ConstantSourceNode::Start(double aWhen, ErrorResult& aRv) {
  if (!WebAudioUtils::IsTimeValid(aWhen)) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("start time");
    return;
  }

  if (mStartCalled) {
    aRv.ThrowInvalidStateError("Can't call start() more than once");
    return;
  }
  mStartCalled = true;

  if (!mTrack) {
    return;
  }

  mTrack->SetTrackTimeParameter(ConstantSourceNodeEngine::START, Context(),
                                aWhen);

  MarkActive();
  Context()->StartBlockedAudioContextIfAllowed();
}

void ConstantSourceNode::Stop(double aWhen, ErrorResult& aRv) {
  if (!WebAudioUtils::IsTimeValid(aWhen)) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("stop time");
    return;
  }

  if (!mStartCalled) {
    aRv.ThrowInvalidStateError("Can't call stop() without calling start()");
    return;
  }

  if (!mTrack || !Context()) {
    return;
  }

  mTrack->SetTrackTimeParameter(ConstantSourceNodeEngine::STOP, Context(),
                                std::max(0.0, aWhen));
}

void ConstantSourceNode::OnTrackEnded() {
  MOZ_ASSERT(mTrackEnded);

  class EndedEventDispatcher final : public Runnable {
   public:
    explicit EndedEventDispatcher(ConstantSourceNode* aNode)
        : mozilla::Runnable("EndedEventDispatcher"), mNode(aNode) {}
    NS_IMETHOD Run() override {
      // If it's not safe to run scripts right now, schedule this to run later
      if (!nsContentUtils::IsSafeToRunScript()) {
        nsContentUtils::AddScriptRunner(this);
        return NS_OK;
      }

      mNode->DispatchTrustedEvent(u"ended"_ns);
      // Release track resources.
      mNode->DestroyMediaTrack();
      return NS_OK;
    }

   private:
    RefPtr<ConstantSourceNode> mNode;
  };

  Context()->Dispatch(do_AddRef(new EndedEventDispatcher(this)));

  // Drop the playing reference
  // Warning: The below line might delete this.
  MarkInactive();
}

}  // namespace mozilla::dom
