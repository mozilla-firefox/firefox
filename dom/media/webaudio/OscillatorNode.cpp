/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OscillatorNode.h"

#include "AudioDestinationNode.h"
#include "AudioNodeEngine.h"
#include "AudioNodeTrack.h"
#include "Tracing.h"
#include "WebAudioUtils.h"
#include "blink/PeriodicWave.h"
#include "nsContentUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(OscillatorNode, AudioScheduledSourceNode,
                                   mPeriodicWave, mFrequency, mDetune)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(OscillatorNode)
NS_INTERFACE_MAP_END_INHERITING(AudioScheduledSourceNode)

NS_IMPL_ADDREF_INHERITED(OscillatorNode, AudioScheduledSourceNode)
NS_IMPL_RELEASE_INHERITED(OscillatorNode, AudioScheduledSourceNode)

class OscillatorNodeEngine final : public AudioNodeEngine {
 public:
  OscillatorNodeEngine(AudioNode* aNode, AudioDestinationNode* aDestination)
      : AudioNodeEngine(aNode),
        mSource(nullptr),
        mDestination(aDestination->Track()),
        mStart(-1),
        mStop(TRACK_TIME_MAX)
        // Keep the default values in sync with OscillatorNode::OscillatorNode.
        ,
        mFrequency(440.f),
        mDetune(0.f),
        mType(OscillatorType::Sine),
        mPhase(0.),
        mFinalFrequency(0.),
        mPhaseIncrement(0.),
        mRecomputeParameters(true),
        mCustomDisableNormalization(false) {
    MOZ_ASSERT(NS_IsMainThread());
    mBasicWaveFormCache = aDestination->Context()->GetBasicWaveFormCache();
  }

  void SetSourceTrack(AudioNodeTrack* aSource) { mSource = aSource; }

  enum Parameters {
    FREQUENCY,
    DETUNE,
    TYPE,
    DISABLE_NORMALIZATION,
    START,
    STOP,
  };
  void RecvTimelineEvent(uint32_t aIndex, AudioParamEvent& aEvent) override {
    mRecomputeParameters = true;

    MOZ_ASSERT(mDestination);

    aEvent.ConvertToTicks(mDestination);

    switch (aIndex) {
      case FREQUENCY:
        mFrequency.InsertEvent<int64_t>(aEvent);
        break;
      case DETUNE:
        mDetune.InsertEvent<int64_t>(aEvent);
        break;
      default:
        NS_ERROR("Bad OscillatorNodeEngine TimelineParameter");
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
        NS_ERROR("Bad OscillatorNodeEngine TrackTimeParameter");
    }
  }

  void SetInt32Parameter(uint32_t aIndex, int32_t aParam) override {
    switch (aIndex) {
      case TYPE:
        // Set the new type.
        mType = static_cast<OscillatorType>(aParam);
        if (mType == OscillatorType::Sine) {
          // Forget any previous custom data.
          mCustomDisableNormalization = false;
          mPeriodicWave = nullptr;
          mRecomputeParameters = true;
        }
        switch (mType) {
          case OscillatorType::Sine:
            mPhase = 0.0;
            break;
          case OscillatorType::Square:
          case OscillatorType::Triangle:
          case OscillatorType::Sawtooth:
            mPeriodicWave = mBasicWaveFormCache->GetBasicWaveForm(mType);
            break;
          case OscillatorType::Custom:
            break;
          default:
            NS_ERROR("Bad OscillatorNodeEngine type parameter.");
        }
        // End type switch.
        break;
      case DISABLE_NORMALIZATION:
        MOZ_ASSERT(aParam >= 0, "negative custom array length");
        mCustomDisableNormalization = static_cast<uint32_t>(aParam);
        break;
      default:
        NS_ERROR("Bad OscillatorNodeEngine Int32Parameter.");
    }
    // End index switch.
  }

  void SetBuffer(AudioChunk&& aBuffer) override {
    MOZ_ASSERT(aBuffer.ChannelCount() == 2,
               "PeriodicWave should have sent two channels");
    MOZ_ASSERT(aBuffer.mVolume == 1.0f);
    mPeriodicWave = WebCore::PeriodicWave::create(
        mSource->mSampleRate, aBuffer.ChannelData<float>()[0],
        aBuffer.ChannelData<float>()[1], aBuffer.mDuration,
        mCustomDisableNormalization);
  }

  void IncrementPhase() {
    const float twoPiFloat = float(2 * M_PI);
    mPhase += mPhaseIncrement;
    if (mPhase > twoPiFloat) {
      mPhase -= twoPiFloat;
    } else if (mPhase < -twoPiFloat) {
      mPhase += twoPiFloat;
    }
  }

  // Returns true if the final frequency (and thus the phase increment) changed,
  // false otherwise. This allow some optimizations at callsite.
  bool UpdateParametersIfNeeded(size_t aIndexInBlock,
                                const float aFrequency[WEBAUDIO_BLOCK_SIZE],
                                const float aDetune[WEBAUDIO_BLOCK_SIZE]) {
    // Shortcut if frequency-related AudioParam are not automated, and we
    // already have computed the frequency information and related parameters.
    if (!ParametersMayNeedUpdate()) {
      return false;
    }

    float detune = aDetune[aIndexInBlock];
    if (detune != mLastDetune) {
      mLastDetune = detune;
      // Single-precision fdlibm_exp2f() would sometimes amplify rounding
      // error in the division for large detune.
      // https://bugzilla.mozilla.org/show_bug.cgi?id=1849806#c4
      mDetuneRatio = fdlibm_exp2(detune / 1200.);
    }
    float finalFrequency = aFrequency[aIndexInBlock] * mDetuneRatio;
    mRecomputeParameters = false;

    if (finalFrequency == mFinalFrequency) {
      return false;
    }

    mFinalFrequency = finalFrequency;
    mPhaseIncrement = 2 * M_PI * finalFrequency / mSource->mSampleRate;
    return true;
  }

  void FillBounds(float* output, TrackTime ticks, uint32_t& start,
                  uint32_t& end) {
    MOZ_ASSERT(output);
    static_assert(TrackTime(WEBAUDIO_BLOCK_SIZE) < UINT_MAX,
                  "WEBAUDIO_BLOCK_SIZE overflows interator bounds.");
    start = 0;
    if (ticks < mStart) {
      start = mStart - ticks;
      for (uint32_t i = 0; i < start; ++i) {
        output[i] = 0.0;
      }
    }
    end = WEBAUDIO_BLOCK_SIZE;
    if (ticks + end > mStop) {
      end = mStop - ticks;
      for (uint32_t i = end; i < WEBAUDIO_BLOCK_SIZE; ++i) {
        output[i] = 0.0;
      }
    }
  }

  void ComputeSine(float* aOutput, uint32_t aStart, uint32_t aEnd,
                   const float aFrequency[WEBAUDIO_BLOCK_SIZE],
                   const float aDetune[WEBAUDIO_BLOCK_SIZE]) {
    for (uint32_t i = aStart; i < aEnd; ++i) {
      // We ignore the return value, changing the frequency has no impact on
      // performances here.
      UpdateParametersIfNeeded(i, aFrequency, aDetune);

      aOutput[i] = fdlibm_sinf(mPhase);

      IncrementPhase();
    }
  }

  bool ParametersMayNeedUpdate() {
    return !mDetune.HasSimpleValue() || !mFrequency.HasSimpleValue() ||
           mRecomputeParameters;
  }

  void ComputeCustom(float* aOutput, uint32_t aStart, uint32_t aEnd,
                     const float aFrequency[WEBAUDIO_BLOCK_SIZE],
                     const float aDetune[WEBAUDIO_BLOCK_SIZE]) {
    MOZ_ASSERT(mPeriodicWave, "No custom waveform data");

    uint32_t periodicWaveSize = mPeriodicWave->periodicWaveSize();
    // Mask to wrap wave data indices into the range [0,periodicWaveSize).
    uint32_t indexMask = periodicWaveSize - 1;
    MOZ_ASSERT(periodicWaveSize && (periodicWaveSize & indexMask) == 0,
               "periodicWaveSize must be power of 2");
    float* higherWaveData = nullptr;
    float* lowerWaveData = nullptr;
    float tableInterpolationFactor;
    // Phase increment at frequency of 1 Hz.
    // mPhase runs [0,periodicWaveSize) here instead of [0,2*M_PI).
    float basePhaseIncrement = mPeriodicWave->rateScale();

    bool needToFetchWaveData =
        UpdateParametersIfNeeded(aStart, aFrequency, aDetune);

    bool parametersMayNeedUpdate = ParametersMayNeedUpdate();
    mPeriodicWave->waveDataForFundamentalFrequency(
        mFinalFrequency, lowerWaveData, higherWaveData,
        tableInterpolationFactor);

    for (uint32_t i = aStart; i < aEnd; ++i) {
      if (parametersMayNeedUpdate) {
        if (needToFetchWaveData) {
          mPeriodicWave->waveDataForFundamentalFrequency(
              mFinalFrequency, lowerWaveData, higherWaveData,
              tableInterpolationFactor);
        }
        needToFetchWaveData = UpdateParametersIfNeeded(i, aFrequency, aDetune);
      }
      // Bilinear interpolation between adjacent samples in each table.
      float floorPhase = floorf(mPhase);
      int j1Signed = static_cast<int>(floorPhase);
      uint32_t j1 = j1Signed & indexMask;
      uint32_t j2 = j1 + 1;
      j2 &= indexMask;

      float sampleInterpolationFactor = mPhase - floorPhase;

      float lower = (1.0f - sampleInterpolationFactor) * lowerWaveData[j1] +
                    sampleInterpolationFactor * lowerWaveData[j2];
      float higher = (1.0f - sampleInterpolationFactor) * higherWaveData[j1] +
                     sampleInterpolationFactor * higherWaveData[j2];
      aOutput[i] = (1.0f - tableInterpolationFactor) * lower +
                   tableInterpolationFactor * higher;

      // Calculate next phase position from wrapped value j1 to avoid loss of
      // precision at large values.
      mPhase =
          j1 + sampleInterpolationFactor + basePhaseIncrement * mFinalFrequency;
    }
  }

  void ComputeSilence(AudioBlock* aOutput) {
    aOutput->SetNull(WEBAUDIO_BLOCK_SIZE);
  }

  void ProcessBlock(AudioNodeTrack* aTrack, GraphTime aFrom,
                    const AudioBlock& aInput, AudioBlock* aOutput,
                    bool* aFinished) override {
    MOZ_ASSERT(mSource == aTrack, "Invalid source track");
    TRACE("OscillatorNodeEngine::ProcessBlock");

    TrackTime ticks = mDestination->GraphTimeToTrackTime(aFrom);
    if (mStart == -1) {
      ComputeSilence(aOutput);
      return;
    }

    if (ticks + WEBAUDIO_BLOCK_SIZE <= mStart || ticks >= mStop ||
        mStop <= mStart) {
      ComputeSilence(aOutput);

    } else {
      aOutput->AllocateChannels(1);
      float* output = aOutput->ChannelFloatsForWrite(0);

      uint32_t start, end;
      FillBounds(output, ticks, start, end);
      MOZ_ASSERT(start < end);

      float frequency[WEBAUDIO_BLOCK_SIZE];
      float detune[WEBAUDIO_BLOCK_SIZE];
      if (ParametersMayNeedUpdate()) {
        if (mFrequency.HasSimpleValue()) {
          std::fill_n(frequency, WEBAUDIO_BLOCK_SIZE, mFrequency.GetValue());
        } else {
          mFrequency.GetValuesAtTime(ticks + start, frequency + start,
                                     end - start);
        }
        if (mDetune.HasSimpleValue()) {
          std::fill_n(detune, WEBAUDIO_BLOCK_SIZE, mDetune.GetValue());
        } else {
          mDetune.GetValuesAtTime(ticks + start, detune + start, end - start);
        }
      }

      // Synthesize the correct waveform.
      switch (mType) {
        case OscillatorType::Sine:
          ComputeSine(output, start, end, frequency, detune);
          break;
        case OscillatorType::Square:
        case OscillatorType::Triangle:
        case OscillatorType::Sawtooth:
        case OscillatorType::Custom:
          ComputeCustom(output, start, end, frequency, detune);
          break;
          // Avoid `default:` so that `-Wswitch` catches missing enumerators at
          // compile time.
      };
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
    // - mFrequency (internal ref owned by node)
    // - mDetune (internal ref owned by node)

    if (mPeriodicWave) {
      amount += mPeriodicWave->sizeOfIncludingThis(aMallocSizeOf);
    }

    return amount;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  // mSource deletes this engine in its destructor
  AudioNodeTrack* MOZ_NON_OWNING_REF mSource;
  RefPtr<AudioNodeTrack> mDestination;
  TrackTime mStart;
  TrackTime mStop;
  AudioParamTimeline mFrequency;
  AudioParamTimeline mDetune;
  OscillatorType mType;
  float mPhase;
  float mFinalFrequency;
  float mPhaseIncrement;
  float mLastDetune = 0.f;
  float mDetuneRatio = 1.f;  // 2^(mLastDetune/1200)
  bool mRecomputeParameters;
  RefPtr<BasicWaveFormCache> mBasicWaveFormCache;
  bool mCustomDisableNormalization;
  RefPtr<WebCore::PeriodicWave> mPeriodicWave;
};

OscillatorNode::OscillatorNode(AudioContext* aContext)
    : AudioScheduledSourceNode(aContext, 2, ChannelCountMode::Max,
                               ChannelInterpretation::Speakers),
      mType(OscillatorType::Sine),
      mStartCalled(false),
      mWatchManager(this, AbstractThread::MainThread()),
      mTrackEnded(AbstractThread::MainThread(), false,
                  "OscillatorNode::mTrackEnded") {
  mFrequency = CreateAudioParam(
      OscillatorNodeEngine::FREQUENCY, u"frequency"_ns, 440.0f,
      -(aContext->SampleRate() / 2), aContext->SampleRate() / 2);
  mDetune = CreateAudioParam(OscillatorNodeEngine::DETUNE, u"detune"_ns, 0.0f);
  OscillatorNodeEngine* engine =
      new OscillatorNodeEngine(this, aContext->Destination());
  mTrack = AudioNodeTrack::Create(aContext, engine,
                                  AudioNodeTrack::NEED_MAIN_THREAD_ENDED,
                                  aContext->Graph());
  engine->SetSourceTrack(mTrack);
  mTrackEnded.Connect(&mTrack->CanonicalEnded());
  mWatchManager.Watch(mTrackEnded, &OscillatorNode::OnTrackEnded);
}

/* static */
already_AddRefed<OscillatorNode> OscillatorNode::Create(
    AudioContext& aAudioContext, const OscillatorOptions& aOptions,
    ErrorResult& aRv) {
  RefPtr<OscillatorNode> audioNode = new OscillatorNode(&aAudioContext);

  audioNode->Initialize(aOptions, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  audioNode->Frequency()->SetInitialValue(aOptions.mFrequency);
  audioNode->Detune()->SetInitialValue(aOptions.mDetune);

  if (aOptions.mPeriodicWave.WasPassed()) {
    audioNode->SetPeriodicWave(aOptions.mPeriodicWave.Value());
  } else {
    audioNode->SetType(aOptions.mType, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  return audioNode.forget();
}

size_t OscillatorNode::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t amount = AudioNode::SizeOfExcludingThis(aMallocSizeOf);

  // For now only report if we know for sure that it's not shared.
  if (mPeriodicWave) {
    amount += mPeriodicWave->SizeOfIncludingThisIfNotShared(aMallocSizeOf);
  }
  amount += mFrequency->SizeOfIncludingThis(aMallocSizeOf);
  amount += mDetune->SizeOfIncludingThis(aMallocSizeOf);
  return amount;
}

size_t OscillatorNode::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

JSObject* OscillatorNode::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return OscillatorNode_Binding::Wrap(aCx, this, aGivenProto);
}

void OscillatorNode::DestroyMediaTrack() {
  mWatchManager.Shutdown();
  mTrackEnded.DisconnectIfConnected();
  AudioNode::DestroyMediaTrack();
}

void OscillatorNode::SendTypeToTrack() {
  if (!mTrack) {
    return;
  }
  if (mType == OscillatorType::Custom) {
    // The engine assumes we'll send the custom data before updating the type.
    SendPeriodicWaveToTrack();
  }
  SendInt32ParameterToTrack(OscillatorNodeEngine::TYPE,
                            static_cast<int32_t>(mType));
}

void OscillatorNode::SendPeriodicWaveToTrack() {
  NS_ASSERTION(mType == OscillatorType::Custom,
               "Sending custom waveform to engine thread with non-custom type");
  MOZ_ASSERT(mTrack, "Missing node track.");
  MOZ_ASSERT(mPeriodicWave, "Send called without PeriodicWave object.");
  SendInt32ParameterToTrack(OscillatorNodeEngine::DISABLE_NORMALIZATION,
                            mPeriodicWave->DisableNormalization());
  AudioChunk data = mPeriodicWave->GetThreadSharedBuffer();
  mTrack->SetBuffer(std::move(data));
}

void OscillatorNode::Start(double aWhen, ErrorResult& aRv) {
  WEB_AUDIO_API_LOG("{:f}: {} {} Start({:f})", Context()->CurrentTime(),
                    NodeType(), Id(), aWhen);

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
    // Nothing to play, or we're already dead for some reason
    return;
  }

  // TODO: Perhaps we need to do more here.
  mTrack->SetTrackTimeParameter(OscillatorNodeEngine::START, Context(), aWhen);

  MarkActive();
  Context()->StartBlockedAudioContextIfAllowed();
}

void OscillatorNode::Stop(double aWhen, ErrorResult& aRv) {
  WEB_AUDIO_API_LOG("{:f}: {} {} Stop({:f})", Context()->CurrentTime(),
                    NodeType(), Id(), aWhen);

  if (!WebAudioUtils::IsTimeValid(aWhen)) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("stop time");
    return;
  }

  if (!mStartCalled) {
    aRv.ThrowInvalidStateError("Can't call stop() without calling start()");
    return;
  }

  if (!mTrack || !Context()) {
    // We've already stopped and had our track shut down
    return;
  }

  // TODO: Perhaps we need to do more here.
  mTrack->SetTrackTimeParameter(OscillatorNodeEngine::STOP, Context(),
                                std::max(0.0, aWhen));
}

void OscillatorNode::OnTrackEnded() {
  MOZ_ASSERT(mTrackEnded);

  class EndedEventDispatcher final : public Runnable {
   public:
    explicit EndedEventDispatcher(OscillatorNode* aNode)
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
    RefPtr<OscillatorNode> mNode;
  };

  Context()->Dispatch(do_AddRef(new EndedEventDispatcher(this)));

  // Drop the playing reference
  // Warning: The below line might delete this.
  MarkInactive();
}

}  // namespace mozilla::dom
