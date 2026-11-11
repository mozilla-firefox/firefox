/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_AUDIONODETRACK_H_
#define MOZILLA_AUDIONODETRACK_H_

#include "AlignedTArray.h"
#include "AudioBlock.h"
#include "AudioSegment.h"
#include "MediaTrackGraph.h"
#include "mozilla/dom/AudioNodeBinding.h"

namespace WebCore {
class Reverb;
}  // namespace WebCore

namespace mozilla {

namespace dom {
struct ThreeDPoint;
struct AudioParamEvent;
class AudioContext;
}  // namespace dom

class AbstractThread;
class ThreadSharedFloatArrayBufferList;
class AudioNodeEngine;
class AudioNodeExternalInputTrack;

typedef AlignedAutoTArray<float, GUESS_AUDIO_CHANNELS * WEBAUDIO_BLOCK_SIZE, 16>
    DownmixBufferType;

/**
 * An AudioNodeTrack produces one audio track with ID AUDIO_TRACK.
 * The start time of the AudioTrack is aligned to the start time of the
 * AudioContext's destination node track, plus some multiple of BLOCK_SIZE
 * samples.
 *
 * An AudioNodeTrack has an AudioNodeEngine plugged into it that does the
 * actual audio processing. AudioNodeTrack contains the glue code that
 * integrates audio processing with the MediaTrackGraph.
 */
class AudioNodeTrack : public ProcessedMediaTrack {
  typedef dom::ChannelCountMode ChannelCountMode;
  typedef dom::ChannelInterpretation ChannelInterpretation;

 public:
  typedef mozilla::dom::AudioContext AudioContext;

  enum { AUDIO_TRACK = 1 };

  typedef AutoTArray<AudioBlock, 1> OutputChunks;

  // Flags re main thread updates and track output.
  typedef unsigned Flags;
  enum : Flags {
    NO_TRACK_FLAGS = 0U,
    NEED_MAIN_THREAD_ENDED = 1U << 0,
    NEED_MAIN_THREAD_CURRENT_TIME = 1U << 1,
    // Internal AudioNodeTracks can only pass their output to another
    // AudioNode, whereas external AudioNodeTracks can pass their output
    // to other ProcessedMediaTracks or hardware audio output.
    EXTERNAL_OUTPUT = 1U << 2,
  };
  /**
   * Create a track that will process audio for an AudioNode.
   * Takes ownership of aEngine.
   * aGraph is required and equals the graph of aCtx in most cases. An exception
   * is AudioDestinationNode where the context's graph hasn't been set up yet.
   */
  static already_AddRefed<AudioNodeTrack> Create(AudioContext* aCtx,
                                                 AudioNodeEngine* aEngine,
                                                 Flags aKind,
                                                 MediaTrackGraph* aGraph);

 protected:
  /**
   * Transfers ownership of aEngine to the new AudioNodeTrack.
   */
  AudioNodeTrack(AudioNodeEngine* aEngine, Flags aFlags, TrackRate aSampleRate);

  ~AudioNodeTrack();

 public:
  // Control API
  /**
   * Sets a parameter that's a time relative to some track's played time.
   * This time is converted to a time relative to this track when it's set.
   */
  void SetTrackTimeParameter(uint32_t aIndex, AudioContext* aContext,
                             double aTrackTime);
  void SetDoubleParameter(uint32_t aIndex, double aValue);
  void SetInt32Parameter(uint32_t aIndex, int32_t aValue);
  void SetThreeDPointParameter(uint32_t aIndex, const dom::ThreeDPoint& aValue);
  void SetBuffer(AudioChunk&& aBuffer);
  void SetReverb(WebCore::Reverb* aReverb, uint32_t aImpulseChannelCount);
  // This sends a single event to the timeline on the MTG thread side.
  void SendTimelineEvent(uint32_t aIndex, const dom::AudioParamEvent& aEvent);
  // This consumes the contents of aData.  aData will be emptied after this
  // returns.
  void SetRawArrayData(nsTArray<float>&& aData);
  void SetChannelMixingParameters(uint32_t aNumberOfChannels,
                                  ChannelCountMode aChannelCountMoe,
                                  ChannelInterpretation aChannelInterpretation);
  void SetPassThrough(bool aPassThrough);
  void SendRunnable(already_AddRefed<nsIRunnable> aRunnable);
  ChannelInterpretation GetChannelInterpretation() {
    return mChannelInterpretation;
  }

  void SetAudioParamHelperTrack() {
    MOZ_ASSERT(!mAudioParamTrack, "Can only do this once");
    mAudioParamTrack = true;
  }
  // The value for channelCount on an AudioNode, but on the audio thread side.
  uint32_t NumberOfChannels() const override;

  AudioNodeTrack* AsAudioNodeTrack() override { return this; }
  void AddInput(MediaInputPort* aPort) override;
  void RemoveInput(MediaInputPort* aPort) override;

  // Graph thread only
  void SetTrackTimeParameterImpl(uint32_t aIndex, MediaTrack* aRelativeToTrack,
                                 double aTrackTime);
  void SetChannelMixingParametersImpl(
      uint32_t aNumberOfChannels, ChannelCountMode aChannelCountMoe,
      ChannelInterpretation aChannelInterpretation);
  void ProcessInput(GraphTime aFrom, GraphTime aTo, uint32_t aFlags) override;
  /**
   * Produce the next block of output, before input is provided.
   * ProcessInput() will be called later, and it then should not change
   * the output.  This is used only for DelayNodeEngine in a feedback loop.
   */
  void ProduceOutputBeforeInput(GraphTime aFrom);
  bool IsAudioParamTrack() const { return mAudioParamTrack; }

  const OutputChunks& LastChunks() const { return mLastChunks; }
  bool MainThreadNeedsUpdates() const override {
    return ((mFlags & NEED_MAIN_THREAD_ENDED) && mEnded) ||
           (mFlags & NEED_MAIN_THREAD_CURRENT_TIME);
  }

  // Any thread
  AudioNodeEngine* Engine() { return mEngine.get(); }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override;
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override;

  void SizeOfAudioNodesIncludingThis(MallocSizeOf aMallocSizeOf,
                                     AudioNodeSizes& aUsage) const;

  /*
   * SetActive() is called when either an active input is added or the engine
   * for a source node transitions from inactive to active.  This is not
   * called from engines for processing nodes because they only become active
   * when there are active input tracks, in which case this track is already
   * active.
   */
  void SetActive();
  /*
   * ScheduleCheckForInactive() is called during track processing when the
   * engine transitions from active to inactive, or the track finishes.  It
   * schedules a call to CheckForInactive() after track processing.
   */
  void ScheduleCheckForInactive();

  virtual AudioNodeExternalInputTrack* AsAudioNodeExternalInputTrack() {
    return nullptr;
  }

 protected:
  void OnGraphThreadDone() override;
  void DestroyImpl() override;

  /*
   * CheckForInactive() is called when the engine transitions from active to
   * inactive, or an active input is removed, or the track finishes.  If the
   * track is now inactive, then mInputChunks will be cleared and mLastChunks
   * will be set to null.  ProcessBlock() will not be called on the engine
   * again until SetActive() is called.
   */
  void CheckForInactive();

  void AdvanceOutputSegment();
  void FinishOutput();
  void AccumulateInputChunk(uint32_t aInputIndex, const AudioBlock& aChunk,
                            AudioBlock* aBlock,
                            DownmixBufferType* aDownmixBuffer);
  void UpMixDownMixChunk(const AudioBlock* aChunk, uint32_t aOutputChannelCount,
                         nsTArray<const float*>& aOutputChannels,
                         DownmixBufferType& aDownmixBuffer);

  uint32_t ComputedNumberOfChannels(uint32_t aInputChannelCount);
  void ObtainInputBlock(AudioBlock& aTmpChunk, uint32_t aPortIndex);
  void IncrementActiveInputCount();
  void DecrementActiveInputCount();

  // The engine that will generate output for this node.
  const UniquePtr<AudioNodeEngine> mEngine;
  // The mixed input blocks are kept from iteration to iteration to avoid
  // reallocating channel data arrays and any buffers for mixing.
  OutputChunks mInputChunks;
  // The last block produced by this node.
  OutputChunks mLastChunks;
  // Whether this is an internal or external track
  const Flags mFlags;
  // The number of input tracks that may provide non-silent input.
  uint32_t mActiveInputCount = 0;
  // The number of input channels that this track requires. 0 means don't care.
  uint32_t mNumberOfInputChannels;
  // The mixing modes
  ChannelCountMode mChannelCountMode;
  ChannelInterpretation mChannelInterpretation;
  // Tracks are considered active if the track has not finished and either
  // the engine is active or there are active input tracks.
  bool mIsActive;
  // Whether the track should be marked as ended as soon
  // as the current time range has been computed block by block.
  bool mMarkAsEndedAfterThisBlock;
  // Whether the track is an AudioParamHelper track.
  bool mAudioParamTrack;
  // Whether the track just passes its input through.
  bool mPassThrough;
};

}  // namespace mozilla

#endif /* MOZILLA_AUDIONODETRACK_H_ */
