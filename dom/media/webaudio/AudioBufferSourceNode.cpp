/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioBufferSourceNode.h"

#include <algorithm>
#include <limits>

#include "AlignmentUtils.h"
#include "AudioDestinationNode.h"
#include "AudioNodeEngine.h"
#include "AudioNodeTrack.h"
#include "AudioParamTimeline.h"
#include "Tracing.h"
#include "mozilla/dom/AudioBufferSourceNodeBinding.h"
#include "mozilla/dom/AudioParam.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsMathUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(AudioBufferSourceNode,
                                   AudioScheduledSourceNode, mBuffer,
                                   mPlaybackRate, mDetune)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AudioBufferSourceNode)
NS_INTERFACE_MAP_END_INHERITING(AudioScheduledSourceNode)

NS_IMPL_ADDREF_INHERITED(AudioBufferSourceNode, AudioScheduledSourceNode)
NS_IMPL_RELEASE_INHERITED(AudioBufferSourceNode, AudioScheduledSourceNode)

/**
 * Media-thread playback engine for AudioBufferSourceNode.
 * Nothing is played until a non-null buffer has been set (via
 * AudioNodeTrack::SetBuffer) and a non-zero mBufferSampleRate has been set
 * (via AudioNodeTrack::SetInt32Parameter)
 */
class AudioBufferSourceNodeEngine final : public AudioNodeEngine {
 public:
  AudioBufferSourceNodeEngine(AudioNode* aNode,
                              AudioDestinationNode* aDestination)
      : AudioNodeEngine(aNode),
        mStart(0.0),
        mBeginProcessing(0),
        mStop(TRACK_TIME_MAX),
        mResampler(nullptr),
        mRemainingResamplerTail(0),
        mRemainingFrames(TRACK_TICKS_MAX),
        mLoopStart(0),
        mLoopEnd(0),
        mBufferPosition(0),
        mBufferSampleRate(0),
        // mResamplerOutRate is initialized in UpdateResampler().
        mChannels(0),
        mDestination(aDestination->Track()),
        mPlaybackRateTimeline(1.0f),
        mDetuneTimeline(0.0f),
        mLoop(false) {}

  ~AudioBufferSourceNodeEngine() {
    if (mResampler) {
      speex_resampler_destroy(mResampler);
    }
  }

  void SetSourceTrack(AudioNodeTrack* aSource) { mSource = aSource; }

  void RecvTimelineEvent(uint32_t aIndex, AudioParamEvent& aEvent) override {
    MOZ_ASSERT(mDestination);
    aEvent.ConvertToTicks(mDestination);
    mRecomputeOutRate = true;

    switch (aIndex) {
      case AudioBufferSourceNode::PLAYBACKRATE:
        mPlaybackRateTimeline.InsertEvent<int64_t>(aEvent);
        break;
      case AudioBufferSourceNode::DETUNE:
        mDetuneTimeline.InsertEvent<int64_t>(aEvent);
        break;
      default:
        NS_ERROR("Bad AudioBufferSourceNodeEngine TimelineParameter");
    }
  }
  void SetTrackTimeParameter(uint32_t aIndex, TrackTime aParam) override {
    switch (aIndex) {
      case AudioBufferSourceNode::STOP:
        mStop = aParam;
        break;
      default:
        NS_ERROR("Bad AudioBufferSourceNodeEngine TrackTimeParameter");
    }
  }
  void SetDoubleParameter(uint32_t aIndex, double aParam) override {
    switch (aIndex) {
      case AudioBufferSourceNode::START:
        MOZ_ASSERT(!mStart, "Another START?");
        mStart = aParam * mDestination->mSampleRate;
        // Round to nearest
        mBeginProcessing = llround(mStart);
        break;
      case AudioBufferSourceNode::DURATION:
        MOZ_ASSERT(aParam >= 0);
        mRemainingFrames = llround(aParam * mBufferSampleRate);
        break;
      default:
        NS_ERROR("Bad AudioBufferSourceNodeEngine double parameter.");
    };
  }
  void SetInt32Parameter(uint32_t aIndex, int32_t aParam) override {
    switch (aIndex) {
      case AudioBufferSourceNode::SAMPLE_RATE:
        MOZ_ASSERT(aParam > 0);
        MOZ_ASSERT(mRecomputeOutRate);
        mBufferSampleRate = aParam;
        mSource->SetActive();
        break;
      case AudioBufferSourceNode::BUFFERSTART:
        MOZ_ASSERT(aParam >= 0);
        if (mBufferPosition == 0) {
          mBufferPosition = aParam;
        }
        break;
      case AudioBufferSourceNode::LOOP:
        mLoop = !!aParam;
        break;
      case AudioBufferSourceNode::LOOPSTART:
        MOZ_ASSERT(aParam >= 0);
        mLoopStart = aParam;
        break;
      case AudioBufferSourceNode::LOOPEND:
        MOZ_ASSERT(aParam >= 0);
        mLoopEnd = aParam;
        break;
      default:
        NS_ERROR("Bad AudioBufferSourceNodeEngine Int32Parameter");
    }
  }
  void SetBuffer(AudioChunk&& aBuffer) override {
    mBuffer = std::move(aBuffer);
  }

  bool BegunResampling() { return mBeginProcessing == -TRACK_TIME_MAX; }

  void UpdateResampler(int32_t aOutRate, uint32_t aChannels) {
    if (mResampler &&
        (aChannels != mChannels ||
         // If the resampler has begun, then it will have moved
         // mBufferPosition to after the samples it has read, but it hasn't
         // output its buffered samples.  Keep using the resampler, even if
         // the rates now match, so that this latent segment is output.
         (aOutRate == mBufferSampleRate && !BegunResampling()))) {
      speex_resampler_destroy(mResampler);
      mResampler = nullptr;
      mRemainingResamplerTail = 0;
      mBeginProcessing = llround(mStart);
    }

    if (aChannels == 0 || (aOutRate == mBufferSampleRate && !mResampler)) {
      mResamplerOutRate = aOutRate;
      return;
    }

    if (!mResampler) {
      mChannels = aChannels;
      mResampler = speex_resampler_init(mChannels, mBufferSampleRate, aOutRate,
                                        SPEEX_RESAMPLER_QUALITY_MIN, nullptr);
      if (!mResampler) {
        return;
      }
    } else {
      if (mResamplerOutRate == aOutRate) {
        return;
      }
      int result =
          speex_resampler_set_rate(mResampler, mBufferSampleRate, aOutRate);
      if (result != RESAMPLER_ERR_SUCCESS) {
        WEB_AUDIO_API_LOG("speex_resampler_set_rate failed: {}", result);
        // mResampler den_rate and num_rate might have been updated, despite
        // the error, in which case the resampler will output zeros but
        // still consume input.  Continue below to update mBeginProcessing
        // for any change in resampler behavior.
      }
    }

    mResamplerOutRate = aOutRate;

    if (!BegunResampling()) {
      // Low pass filter effects from the resampler mean that samples before
      // the start time are influenced by resampling the buffer.  The input
      // latency indicates half the filter width.
      int64_t inputLatency = speex_resampler_get_input_latency(mResampler);
      uint32_t ratioNum, ratioDen;
      speex_resampler_get_ratio(mResampler, &ratioNum, &ratioDen);
      // The output subsample resolution supported in aligning the resampler
      // is ratioNum.  First round the start time to the nearest subsample.
      int64_t subsample = llround(mStart * ratioNum);
      // Now include the leading effects of the filter, and round *up* to the
      // next whole tick, because there is no effect on samples outside the
      // filter width.
      mBeginProcessing =
          (subsample - inputLatency * ratioDen + ratioNum - 1) / ratioNum;
    }
  }

  // Borrow a full buffer of size WEBAUDIO_BLOCK_SIZE from the source buffer
  // at offset aSourceOffset.  This avoids copying memory.
  void BorrowFromInputBuffer(AudioBlock* aOutput, uint32_t aChannels) {
    aOutput->SetBuffer(mBuffer.mBuffer);
    aOutput->mChannelData.SetLength(aChannels);
    for (uint32_t i = 0; i < aChannels; ++i) {
      aOutput->mChannelData[i] =
          mBuffer.ChannelData<float>()[i] + mBufferPosition;
    }
    aOutput->mVolume = mBuffer.mVolume;
    aOutput->mBufferFormat = AUDIO_FORMAT_FLOAT32;
  }

  // Copy aNumberOfFrames frames from the source buffer at offset aSourceOffset
  // and put it at offset aBufferOffset in the destination buffer.
  template <typename T>
  void CopyFromInputBuffer(AudioBlock* aOutput, uint32_t aChannels,
                           uintptr_t aOffsetWithinBlock,
                           uint32_t aNumberOfFrames) {
    MOZ_ASSERT(mBuffer.mVolume == 1.0f);
    for (uint32_t i = 0; i < aChannels; ++i) {
      float* baseChannelData = aOutput->ChannelFloatsForWrite(i);
      ConvertAudioSamples(mBuffer.ChannelData<T>()[i] + mBufferPosition,
                          baseChannelData + aOffsetWithinBlock,
                          aNumberOfFrames);
    }
  }

  // Resamples input data to an output buffer, according to |mBufferSampleRate|
  // and the playbackRate/detune. The number of frames consumed/produced depends
  // on the amount of space remaining in both the input and output buffer, and
  // the playback rate (that is, the ratio between the output samplerate and the
  // input samplerate).
  void CopyFromInputBufferWithResampling(AudioBlock* aOutput,
                                         uint32_t aChannels,
                                         uint32_t* aOffsetWithinBlock,
                                         uint32_t aAvailableInOutput,
                                         TrackTime* aCurrentPosition,
                                         uint32_t aBufferMax) {
    if (*aOffsetWithinBlock == 0) {
      aOutput->AllocateChannels(aChannels);
    }
    SpeexResamplerState* resampler = mResampler;
    MOZ_ASSERT(aChannels > 0);

    if (mBufferPosition < aBufferMax) {
      uint32_t availableInInputBuffer = aBufferMax - mBufferPosition;
      uint32_t ratioNum, ratioDen;
      speex_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
      // Limit the number of input samples copied and possibly
      // format-converted for resampling by estimating how many will be used.
      // This may be a little small if still filling the resampler with
      // initial data, but we'll get called again and it will work out.
      uint32_t inputLimit = aAvailableInOutput * ratioNum / ratioDen + 10;
      if (!BegunResampling()) {
        // First time the resampler is used.
        uint32_t inputLatency = speex_resampler_get_input_latency(resampler);
        inputLimit += inputLatency;
        // If starting after mStart, then play from the beginning of the
        // buffer, but correct for input latency.  If starting before mStart,
        // then align the resampler so that the time corresponding to the
        // first input sample is mStart.
        int64_t skipFracNum = static_cast<int64_t>(inputLatency) * ratioDen;
        double leadTicks = mStart - *aCurrentPosition;
        if (leadTicks > 0.0) {
          // Round to nearest output subsample supported by the resampler at
          // these rates.
          int64_t leadSubsamples = llround(leadTicks * ratioNum);
          MOZ_ASSERT(leadSubsamples <= skipFracNum,
                     "mBeginProcessing is wrong?");
          skipFracNum -= leadSubsamples;
        }
        speex_resampler_set_skip_frac_num(
            resampler, std::min<int64_t>(skipFracNum, UINT32_MAX));

        mBeginProcessing = -TRACK_TIME_MAX;
      }
      inputLimit = std::min(inputLimit, availableInInputBuffer);

      MOZ_ASSERT(mBuffer.mVolume == 1.0f);
      for (uint32_t i = 0; true;) {
        uint32_t inSamples = inputLimit;

        uint32_t outSamples = aAvailableInOutput;
        float* outputData =
            aOutput->ChannelFloatsForWrite(i) + *aOffsetWithinBlock;

        if (mBuffer.mBufferFormat == AUDIO_FORMAT_FLOAT32) {
          const float* inputData =
              mBuffer.ChannelData<float>()[i] + mBufferPosition;
          WebAudioUtils::SpeexResamplerProcess(
              resampler, i, inputData, &inSamples, outputData, &outSamples);
        } else {
          MOZ_ASSERT(mBuffer.mBufferFormat == AUDIO_FORMAT_S16);
          const int16_t* inputData =
              mBuffer.ChannelData<int16_t>()[i] + mBufferPosition;
          WebAudioUtils::SpeexResamplerProcess(
              resampler, i, inputData, &inSamples, outputData, &outSamples);
        }
        if (++i == aChannels) {
          mBufferPosition += inSamples;
          mRemainingFrames -= inSamples;
          MOZ_ASSERT(mBufferPosition <= mBuffer.GetDuration());
          MOZ_ASSERT(mRemainingFrames >= 0);
          *aOffsetWithinBlock += outSamples;
          *aCurrentPosition += outSamples;
          if ((!mLoop && inSamples == availableInInputBuffer) ||
              mRemainingFrames == 0) {
            // We'll feed in enough zeros to empty out the resampler's memory.
            // This handles the output latency as well as capturing the low
            // pass effects of the resample filter.
            mRemainingResamplerTail =
                2 * speex_resampler_get_input_latency(resampler) - 1;
          }
          return;
        }
      }
    } else {
      for (uint32_t i = 0; true;) {
        uint32_t inSamples = mRemainingResamplerTail;
        uint32_t outSamples = aAvailableInOutput;
        float* outputData =
            aOutput->ChannelFloatsForWrite(i) + *aOffsetWithinBlock;

        // AudioDataValue* for aIn selects the function that does not try to
        // copy and format-convert input data.
        WebAudioUtils::SpeexResamplerProcess(
            resampler, i, static_cast<AudioDataValue*>(nullptr), &inSamples,
            outputData, &outSamples);
        if (++i == aChannels) {
          MOZ_ASSERT(inSamples <= mRemainingResamplerTail);
          mRemainingResamplerTail -= inSamples;
          *aOffsetWithinBlock += outSamples;
          *aCurrentPosition += outSamples;
          break;
        }
      }
    }
  }

  /**
   * Fill aOutput with as many zero frames as we can, and advance
   * aOffsetWithinBlock and aCurrentPosition based on how many frames we write.
   * This will never advance aOffsetWithinBlock past WEBAUDIO_BLOCK_SIZE or
   * aCurrentPosition past aMaxPos.  This function knows when it needs to
   * allocate the output buffer, and also optimizes the case where it can avoid
   * memory allocations.
   */
  void FillWithZeroes(AudioBlock* aOutput, uint32_t aChannels,
                      uint32_t* aOffsetWithinBlock, TrackTime* aCurrentPosition,
                      TrackTime aMaxPos) {
    MOZ_ASSERT(*aCurrentPosition < aMaxPos);
    uint32_t numFrames = std::min<TrackTime>(
        WEBAUDIO_BLOCK_SIZE - *aOffsetWithinBlock, aMaxPos - *aCurrentPosition);
    if (numFrames == WEBAUDIO_BLOCK_SIZE || !aChannels) {
      aOutput->SetNull(WEBAUDIO_BLOCK_SIZE);
    } else {
      if (*aOffsetWithinBlock == 0) {
        aOutput->AllocateChannels(aChannels);
      }
      WriteZeroesToAudioBlock(aOutput, *aOffsetWithinBlock, numFrames);
    }
    *aOffsetWithinBlock += numFrames;
    *aCurrentPosition += numFrames;
  }

  /**
   * Copy as many frames as possible from the source buffer to aOutput, and
   * advance aOffsetWithinBlock and aCurrentPosition based on how many frames
   * we write.  This will never advance aOffsetWithinBlock past
   * WEBAUDIO_BLOCK_SIZE, or aCurrentPosition past mStop.  It takes data from
   * the buffer at aBufferOffset, and never takes more data than aBufferMax.
   * This function knows when it needs to allocate the output buffer, and also
   * optimizes the case where it can avoid memory allocations.
   */
  void CopyFromBuffer(AudioBlock* aOutput, uint32_t aChannels,
                      uint32_t* aOffsetWithinBlock, TrackTime* aCurrentPosition,
                      uint32_t aBufferMax) {
    MOZ_ASSERT(*aCurrentPosition < mStop);
    uint32_t availableInOutput = std::min<TrackTime>(
        WEBAUDIO_BLOCK_SIZE - *aOffsetWithinBlock, mStop - *aCurrentPosition);
    if (mResampler) {
      CopyFromInputBufferWithResampling(aOutput, aChannels, aOffsetWithinBlock,
                                        availableInOutput, aCurrentPosition,
                                        aBufferMax);
      return;
    }

    if (aChannels == 0) {
      aOutput->SetNull(WEBAUDIO_BLOCK_SIZE);
      // There is no attempt here to limit advance so that mBufferPosition is
      // limited to aBufferMax.  The only observable affect of skipping the
      // check would be in the precise timing of the ended event if the loop
      // attribute is reset after playback has looped.
      *aOffsetWithinBlock += availableInOutput;
      *aCurrentPosition += availableInOutput;
      // Rounding at the start and end of the period means that fractional
      // increments essentially accumulate if outRate remains constant.  If
      // outRate is varying, then accumulation happens on average but not
      // precisely.
      TrackTicks start =
          *aCurrentPosition * mBufferSampleRate / mResamplerOutRate;
      TrackTicks end = (*aCurrentPosition + availableInOutput) *
                       mBufferSampleRate / mResamplerOutRate;
      mBufferPosition += end - start;
      return;
    }

    uint32_t numFrames =
        std::min(aBufferMax - mBufferPosition, availableInOutput);

    bool shouldBorrow = false;
    if (numFrames == WEBAUDIO_BLOCK_SIZE &&
        mBuffer.mBufferFormat == AUDIO_FORMAT_FLOAT32) {
      shouldBorrow = true;
      for (uint32_t i = 0; i < aChannels; ++i) {
        if (!IS_ALIGNED16(mBuffer.ChannelData<float>()[i] + mBufferPosition)) {
          shouldBorrow = false;
          break;
        }
      }
    }
    MOZ_ASSERT(mBufferPosition < aBufferMax);
    if (shouldBorrow) {
      BorrowFromInputBuffer(aOutput, aChannels);
    } else {
      if (*aOffsetWithinBlock == 0) {
        aOutput->AllocateChannels(aChannels);
      }
      if (mBuffer.mBufferFormat == AUDIO_FORMAT_FLOAT32) {
        CopyFromInputBuffer<float>(aOutput, aChannels, *aOffsetWithinBlock,
                                   numFrames);
      } else {
        MOZ_ASSERT(mBuffer.mBufferFormat == AUDIO_FORMAT_S16);
        CopyFromInputBuffer<int16_t>(aOutput, aChannels, *aOffsetWithinBlock,
                                     numFrames);
      }
    }
    *aOffsetWithinBlock += numFrames;
    *aCurrentPosition += numFrames;
    mBufferPosition += numFrames;
    mRemainingFrames -= numFrames;
  }

  int32_t ComputeFinalOutSampleRate(float aPlaybackRate, float aDetune) {
    float computedPlaybackRate = aPlaybackRate * fdlibm_exp2f(aDetune / 1200.f);
    if (std::isnan(computedPlaybackRate)) {
      computedPlaybackRate = 1.0f;
    }
    // Make sure the playback rate is something our resampler can work with
    int32_t rate = WebAudioUtils::TruncateFloatToInt<int32_t>(
        mSource->mSampleRate / computedPlaybackRate);
    return rate > 0 ? rate : mBufferSampleRate;
  }

  void UpdateSampleRateIfNeeded(uint32_t aChannels, TrackTime aTrackPosition) {
    bool simplePlaybackRate = mPlaybackRateTimeline.HasSimpleValue();
    bool simpleDetune = mDetuneTimeline.HasSimpleValue();

    if (simplePlaybackRate && simpleDetune && !mRecomputeOutRate) {
      return;  // skipping the slow exp2f() for the detune
    }
    mRecomputeOutRate = false;

    float playbackRate;
    float detune;
    if (simplePlaybackRate) {
      playbackRate = mPlaybackRateTimeline.GetValue();
    } else {
      playbackRate =
          mPlaybackRateTimeline.GetComplexValueAtTime(aTrackPosition);
    }
    if (simpleDetune) {
      detune = mDetuneTimeline.GetValue();
    } else {
      detune = mDetuneTimeline.GetComplexValueAtTime(aTrackPosition);
    }

    int32_t outRate = ComputeFinalOutSampleRate(playbackRate, detune);
    UpdateResampler(outRate, aChannels);
  }

  void ProcessBlock(AudioNodeTrack* aTrack, GraphTime aFrom,
                    const AudioBlock& aInput, AudioBlock* aOutput,
                    bool* aFinished) override {
    TRACE("AudioBufferSourceNodeEngine::ProcessBlock");
    if (mBufferSampleRate == 0) {
      // start() has not yet been called or no buffer has yet been set
      aOutput->SetNull(WEBAUDIO_BLOCK_SIZE);
      return;
    }

    TrackTime streamPosition = mDestination->GraphTimeToTrackTime(aFrom);
    uint32_t channels = mBuffer.ChannelCount();

    UpdateSampleRateIfNeeded(channels, streamPosition);

    uint32_t written = 0;
    while (true) {
      if ((mStop != TRACK_TIME_MAX && streamPosition >= mStop) ||
          (!mRemainingResamplerTail &&
           ((mBufferPosition >= mBuffer.GetDuration() && !mLoop) ||
            mRemainingFrames <= 0))) {
        if (written != WEBAUDIO_BLOCK_SIZE) {
          FillWithZeroes(aOutput, channels, &written, &streamPosition,
                         TRACK_TIME_MAX);
        }
        *aFinished = true;
        break;
      }
      if (written == WEBAUDIO_BLOCK_SIZE) {
        break;
      }
      if (streamPosition < mBeginProcessing) {
        FillWithZeroes(aOutput, channels, &written, &streamPosition,
                       mBeginProcessing);
        continue;
      }

      TrackTicks bufferLeft;
      if (mLoop) {
        // mLoopEnd can become less than mBufferPosition when a LOOPEND engine
        // parameter is received after "loopend" is changed on the node or a
        // new buffer with lower samplerate is set.
        if (mBufferPosition >= mLoopEnd) {
          mBufferPosition = mLoopStart;
        }
        bufferLeft =
            std::min<TrackTicks>(mRemainingFrames, mLoopEnd - mBufferPosition);
      } else {
        bufferLeft =
            std::min(mRemainingFrames, mBuffer.GetDuration() - mBufferPosition);
      }

      CopyFromBuffer(aOutput, channels, &written, &streamPosition,
                     bufferLeft + mBufferPosition);
    }
  }

  bool IsActive() const override {
    // Whether buffer has been set and start() has been called.
    return mBufferSampleRate != 0;
  }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override {
    // Not owned:
    // - mBuffer - shared w/ AudioNode
    // - mPlaybackRateTimeline - shared w/ AudioNode
    // - mDetuneTimeline - shared w/ AudioNode

    size_t amount = AudioNodeEngine::SizeOfExcludingThis(aMallocSizeOf);

    // NB: We need to modify speex if we want the full memory picture, internal
    //     fields that need measuring noted below.
    // - mResampler->mem
    // - mResampler->sinc_table
    // - mResampler->last_sample
    // - mResampler->magic_samples
    // - mResampler->samp_frac_num
    amount += aMallocSizeOf(mResampler);

    return amount;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  double mStart;  // including the fractional position between ticks
  // Low pass filter effects from the resampler mean that samples before the
  // start time are influenced by resampling the buffer.  mBeginProcessing
  // includes the extent of this filter.  The special value of -TRACK_TIME_MAX
  // indicates that the resampler has begun processing.
  TrackTime mBeginProcessing;
  TrackTime mStop;
  AudioChunk mBuffer;
  SpeexResamplerState* mResampler;
  // mRemainingResamplerTail, like mBufferPosition
  // is measured in input buffer samples.
  uint32_t mRemainingResamplerTail;
  TrackTicks mRemainingFrames;
  uint32_t mLoopStart;
  uint32_t mLoopEnd;
  uint32_t mBufferPosition;
  int32_t mBufferSampleRate;
  int32_t mResamplerOutRate;  // used if mResampler is set or mChannels == 0
  uint32_t mChannels;
  RefPtr<AudioNodeTrack> mDestination;

  // mSource deletes the engine in its destructor.
  AudioNodeTrack* MOZ_NON_OWNING_REF mSource;
  AudioParamTimeline mPlaybackRateTimeline;
  AudioParamTimeline mDetuneTimeline;
  bool mLoop;
  bool mRecomputeOutRate = true;
};

AudioBufferSourceNode::AudioBufferSourceNode(AudioContext* aContext)
    : AudioScheduledSourceNode(aContext, 2, ChannelCountMode::Max,
                               ChannelInterpretation::Speakers),
      mLoopStart(0.0),
      mLoopEnd(0.0),
      // mOffset and mDuration are initialized in Start().
      mLoop(false),
      mStartCalled(false),
      mBufferSet(false),
      mWatchManager(this, AbstractThread::MainThread()),
      mTrackEnded(AbstractThread::MainThread(), false,
                  "AudioBufferSourceNode::mTrackEnded") {
  mPlaybackRate = CreateAudioParam(PLAYBACKRATE, u"playbackRate"_ns, 1.0f);
  mDetune = CreateAudioParam(DETUNE, u"detune"_ns, 0.0f);
  AudioBufferSourceNodeEngine* engine =
      new AudioBufferSourceNodeEngine(this, aContext->Destination());
  mTrack = AudioNodeTrack::Create(aContext, engine,
                                  AudioNodeTrack::NEED_MAIN_THREAD_ENDED,
                                  aContext->Graph());
  engine->SetSourceTrack(mTrack);
  mTrackEnded.Connect(&mTrack->CanonicalEnded());
  mWatchManager.Watch(mTrackEnded, &AudioBufferSourceNode::OnTrackEnded);
}

/* static */
already_AddRefed<AudioBufferSourceNode> AudioBufferSourceNode::Create(
    JSContext* aCx, AudioContext& aAudioContext,
    const AudioBufferSourceOptions& aOptions) {
  RefPtr<AudioBufferSourceNode> audioNode =
      new AudioBufferSourceNode(&aAudioContext);

  if (aOptions.mBuffer.WasPassed()) {
    ErrorResult ignored;
    MOZ_ASSERT(aCx);
    audioNode->SetBuffer(aCx, aOptions.mBuffer.Value(), ignored);
  }

  audioNode->Detune()->SetInitialValue(aOptions.mDetune);
  audioNode->SetLoop(aOptions.mLoop);
  audioNode->SetLoopEnd(aOptions.mLoopEnd);
  audioNode->SetLoopStart(aOptions.mLoopStart);
  audioNode->PlaybackRate()->SetInitialValue(aOptions.mPlaybackRate);

  return audioNode.forget();
}
void AudioBufferSourceNode::DestroyMediaTrack() {
  mWatchManager.Shutdown();
  mTrackEnded.DisconnectIfConnected();
  AudioNode::DestroyMediaTrack();
}

size_t AudioBufferSourceNode::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t amount = AudioNode::SizeOfExcludingThis(aMallocSizeOf);

  /* mBuffer can be shared and is accounted for separately. */

  amount += mPlaybackRate->SizeOfIncludingThis(aMallocSizeOf);
  amount += mDetune->SizeOfIncludingThis(aMallocSizeOf);
  return amount;
}

size_t AudioBufferSourceNode::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

JSObject* AudioBufferSourceNode::WrapObject(JSContext* aCx,
                                            JS::Handle<JSObject*> aGivenProto) {
  return AudioBufferSourceNode_Binding::Wrap(aCx, this, aGivenProto);
}

void AudioBufferSourceNode::Start(double aWhen, double aOffset,
                                  const Optional<double>& aDuration,
                                  ErrorResult& aRv) {
  if (!WebAudioUtils::IsTimeValid(aWhen)) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("start time");
    return;
  }
  if (aOffset < 0) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("offset");
    return;
  }
  if (aDuration.WasPassed() && !WebAudioUtils::IsTimeValid(aDuration.Value())) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("duration");
    return;
  }

  if (mStartCalled) {
    aRv.ThrowInvalidStateError(
        "Start has already been called on this AudioBufferSourceNode.");
    return;
  }
  mStartCalled = true;

  AudioNodeTrack* ns = mTrack;
  if (!ns) {
    // Nothing to play, or we're already dead for some reason
    return;
  }

  // Remember our arguments so that we can use them when we get a new buffer.
  mOffset = aOffset;
  mDuration = aDuration.WasPassed() ? aDuration.Value()
                                    : std::numeric_limits<double>::min();

  WEB_AUDIO_API_LOG("{:f}: {} {} Start({:f}, {}, {})", Context()->CurrentTime(),
                    NodeType(), Id(), aWhen, aOffset, mDuration);

  // We can't send these parameters without a buffer because we don't know the
  // buffer's sample rate or length.
  if (mBuffer) {
    SendOffsetAndDurationParametersToTrack(ns);
  }

  // Don't set parameter unnecessarily
  if (aWhen > 0.0) {
    ns->SetDoubleParameter(START, aWhen);
  }

  Context()->StartBlockedAudioContextIfAllowed();
}

void AudioBufferSourceNode::Start(double aWhen, ErrorResult& aRv) {
  Start(aWhen, 0 /* offset */, Optional<double>(), aRv);
}

void AudioBufferSourceNode::SendBufferParameterToTrack(JSContext* aCx) {
  AudioNodeTrack* ns = mTrack;
  if (!ns) {
    return;
  }

  if (mBuffer) {
    AudioChunk data = mBuffer->GetThreadSharedChannelsForRate(aCx);
    ns->SetBuffer(std::move(data));

    if (mStartCalled) {
      SendOffsetAndDurationParametersToTrack(ns);
    }
  } else {
    ns->SetBuffer(AudioChunk());

    MarkInactive();
  }
}

void AudioBufferSourceNode::SendOffsetAndDurationParametersToTrack(
    AudioNodeTrack* aTrack) {
  NS_ASSERTION(
      mBuffer && mStartCalled,
      "Only call this when we have a buffer and start() has been called");

  float rate = mBuffer->SampleRate();
  aTrack->SetInt32Parameter(SAMPLE_RATE, rate);

  int32_t offsetSamples = std::max(0, NS_lround(mOffset * rate));

  // Don't set parameter unnecessarily
  if (offsetSamples > 0) {
    aTrack->SetInt32Parameter(BUFFERSTART, offsetSamples);
  }

  if (mDuration != std::numeric_limits<double>::min()) {
    MOZ_ASSERT(mDuration >= 0.0);  // provided by Start()
    MOZ_ASSERT(rate >= 0.0f);      // provided by AudioBuffer::Create()
    aTrack->SetDoubleParameter(DURATION, mDuration);
  }
  MarkActive();
}

void AudioBufferSourceNode::Stop(double aWhen, ErrorResult& aRv) {
  if (!WebAudioUtils::IsTimeValid(aWhen)) {
    aRv.ThrowRangeError<MSG_VALUE_OUT_OF_RANGE>("stop time");
    return;
  }

  if (!mStartCalled) {
    aRv.ThrowInvalidStateError(
        "Start has not been called on this AudioBufferSourceNode.");
    return;
  }

  WEB_AUDIO_API_LOG("{:f}: {} {} Stop({:f})", Context()->CurrentTime(),
                    NodeType(), Id(), aWhen);

  AudioNodeTrack* ns = mTrack;
  if (!ns || !Context()) {
    // We've already stopped and had our track shut down
    return;
  }

  ns->SetTrackTimeParameter(STOP, Context(), std::max(0.0, aWhen));
}

void AudioBufferSourceNode::OnTrackEnded() {
  MOZ_ASSERT(mTrackEnded);

  class EndedEventDispatcher final : public Runnable {
   public:
    explicit EndedEventDispatcher(AudioBufferSourceNode* aNode)
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
    RefPtr<AudioBufferSourceNode> mNode;
  };

  Context()->Dispatch(do_AddRef(new EndedEventDispatcher(this)));

  // Drop the playing reference
  // Warning: The below line might delete this.
  MarkInactive();
}

void AudioBufferSourceNode::SendLoopParametersToTrack() {
  if (!mTrack) {
    return;
  }
  // Don't compute and set the loop parameters unnecessarily
  if (mLoop && mBuffer) {
    float rate = mBuffer->SampleRate();
    double length = (double(mBuffer->Length()) / mBuffer->SampleRate());
    double actualLoopStart, actualLoopEnd;
    if (mLoopStart >= 0.0 && mLoopEnd > 0.0 && mLoopStart < mLoopEnd) {
      MOZ_ASSERT(mLoopStart != 0.0 || mLoopEnd != 0.0);
      actualLoopStart = (mLoopStart > length) ? 0.0 : mLoopStart;
      actualLoopEnd = std::min(mLoopEnd, length);
    } else {
      actualLoopStart = 0.0;
      actualLoopEnd = length;
    }
    int32_t loopStartTicks = NS_lround(actualLoopStart * rate);
    int32_t loopEndTicks = NS_lround(actualLoopEnd * rate);
    if (loopStartTicks < loopEndTicks) {
      SendInt32ParameterToTrack(LOOPSTART, loopStartTicks);
      SendInt32ParameterToTrack(LOOPEND, loopEndTicks);
      SendInt32ParameterToTrack(LOOP, 1);
    } else {
      // Be explicit about looping not happening if the offsets make
      // looping impossible.
      SendInt32ParameterToTrack(LOOP, 0);
    }
  } else {
    SendInt32ParameterToTrack(LOOP, 0);
  }
}

}  // namespace mozilla::dom
