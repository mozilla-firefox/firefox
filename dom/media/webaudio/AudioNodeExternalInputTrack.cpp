/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioNodeExternalInputTrack.h"

#include "AlignedTArray.h"
#include "AlignmentUtils.h"
#include "AudioChannelFormat.h"
#include "AudioNodeEngine.h"
#include "mozilla/dom/MediaStreamAudioSourceNode.h"

using namespace mozilla::dom;

namespace mozilla {

extern LazyLogModule gMediaDecoderLog;

#define LOG(msg, ...)                                                \
  MOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Debug,                     \
              "AudioNodeExternalInputTrack={} " msg, fmt::ptr(this), \
              ##__VA_ARGS__)

AudioNodeExternalInputTrack::AudioNodeExternalInputTrack(
    AudioNodeEngine* aEngine, TrackRate aSampleRate)
    : AudioNodeTrack(aEngine, NO_TRACK_FLAGS, aSampleRate) {
  MOZ_COUNT_CTOR(AudioNodeExternalInputTrack);
}

AudioNodeExternalInputTrack::~AudioNodeExternalInputTrack() {
  MOZ_COUNT_DTOR(AudioNodeExternalInputTrack);
}

/* static */
already_AddRefed<AudioNodeExternalInputTrack>
AudioNodeExternalInputTrack::Create(MediaTrackGraph* aGraph,
                                    AudioNodeEngine* aEngine) {
  AudioContext* ctx = aEngine->NodeMainThread()->Context();
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aGraph == ctx->Graph());

  RefPtr<AudioNodeExternalInputTrack> track =
      new AudioNodeExternalInputTrack(aEngine, aGraph->GraphRate());
  track->mSuspendedCount += ctx->ShouldSuspendNewTrack();
  aGraph->AddTrack(track, {});
  return track.forget();
}

/**
 * Copies the data in aInput to aOffsetInBlock within aBlock.
 * aBlock must have been allocated with AllocateInputBlock and have a channel
 * count that's a superset of the channels in aInput.
 */
template <typename T>
static void CopyChunkToBlock(AudioChunk& aInput, AudioBlock* aBlock,
                             uint32_t aOffsetInBlock) {
  uint32_t blockChannels = aBlock->ChannelCount();
  AutoTArray<const T*, 2> channels;
  if (aInput.IsNull()) {
    channels.SetLength(blockChannels);
    PodZero(channels.Elements(), blockChannels);
  } else {
    Span inputChannels = aInput.ChannelData<T>();
    channels.SetLength(inputChannels.Length());
    PodCopy(channels.Elements(), inputChannels.Elements(), channels.Length());
    if (channels.Length() != blockChannels) {
      // We only need to upmix here because aBlock's channel count has been
      // chosen to be a superset of the channel count of every chunk.
      AudioChannelsUpMix(&channels, blockChannels, static_cast<T*>(nullptr));
    }
  }

  for (uint32_t c = 0; c < blockChannels; ++c) {
    float* outputData = aBlock->ChannelFloatsForWrite(c) + aOffsetInBlock;
    if (channels[c]) {
      ConvertAudioSamplesWithScale(channels[c], outputData,
                                   aInput.GetDuration(), aInput.mVolume);
    } else {
      PodZero(outputData, aInput.GetDuration());
    }
  }
}

/**
 * Converts the data in aSegment to a single chunk aBlock. aSegment must have
 * duration WEBAUDIO_BLOCK_SIZE. aFallbackChannelCount is a superset of the
 * channels in every chunk of aSegment. aBlock must be float format or null.
 */
static void ConvertSegmentToAudioBlock(AudioSegment* aSegment,
                                       AudioBlock* aBlock,
                                       int32_t aFallbackChannelCount) {
  NS_ASSERTION(aSegment->GetDuration() == WEBAUDIO_BLOCK_SIZE,
               "Bad segment duration");

  {
    AudioSegment::ChunkIterator ci(*aSegment);
    NS_ASSERTION(!ci.IsEnded(), "Should be at least one chunk!");
    if (ci->GetDuration() == WEBAUDIO_BLOCK_SIZE &&
        (ci->IsNull() || ci->mBufferFormat == AUDIO_FORMAT_FLOAT32)) {
      bool aligned = true;
      for (size_t i = 0; i < ci->mChannelData.Length(); ++i) {
        if (!IS_ALIGNED16(ci->mChannelData[i])) {
          aligned = false;
          break;
        }
      }

      // Return this chunk directly to avoid copying data.
      if (aligned) {
        *aBlock = *ci;
        return;
      }
    }
  }

  aBlock->AllocateChannels(aFallbackChannelCount);

  uint32_t duration = 0;
  for (AudioSegment::ChunkIterator ci(*aSegment); !ci.IsEnded(); ci.Next()) {
    switch (ci->mBufferFormat) {
      case AUDIO_FORMAT_S16: {
        CopyChunkToBlock<int16_t>(*ci, aBlock, duration);
        break;
      }
      case AUDIO_FORMAT_FLOAT32: {
        CopyChunkToBlock<float>(*ci, aBlock, duration);
        break;
      }
      case AUDIO_FORMAT_SILENCE: {
        // The actual type of the sample does not matter here, but we still need
        // to send some audio to the graph.
        CopyChunkToBlock<float>(*ci, aBlock, duration);
        break;
      }
    }
    duration += ci->GetDuration();
  }
}

void AudioNodeExternalInputTrack::ProcessInput(GraphTime aFrom, GraphTime aTo,
                                               uint32_t aFlags) {
  AssertOnGraphThread();
  // According to spec, number of outputs is always 1.
  MOZ_ASSERT(mLastChunks.Length() == 1);

  // GC stuff can result in our input track being destroyed before this track.
  // Handle that.
  if (!IsEnabled() || mInputs.IsEmpty() || mPassThrough) {
    mLastChunks[0].SetNull(WEBAUDIO_BLOCK_SIZE);
    return;
  }

  MOZ_ASSERT(mInputs.Length() == 1);

  MediaTrack* source = mInputs[0]->GetSource();
  AutoTArray<AudioSegment, 1> audioSegments;
  uint32_t inputChannels = 0;

  MOZ_ASSERT(source->GetData()->GetType() == MediaSegment::AUDIO,
             "AudioNodeExternalInputTrack shouldn't have a video input");

  const AudioSegment& inputSegment =
      *mInputs[0]->GetSource()->GetData<AudioSegment>();
  if (!inputSegment.IsNull()) {
    AudioSegment& segment = *audioSegments.AppendElement();
    GraphTime next;
    for (GraphTime t = aFrom; t < aTo; t = next) {
      MediaInputPort::InputInterval interval =
          MediaInputPort::GetNextInputInterval(mInputs[0], t);
      interval.mEnd = std::min(interval.mEnd, aTo);
      if (interval.mStart >= interval.mEnd) {
        break;
      }
      next = interval.mEnd;

      // We know this track does not block during the processing interval ---
      // we're not finished, we don't underrun, and we're not suspended.
      TrackTime outputStart = GraphTimeToTrackTime(interval.mStart);
      TrackTime outputEnd = GraphTimeToTrackTime(interval.mEnd);
      TrackTime ticks = outputEnd - outputStart;

      if (interval.mInputIsBlocked) {
        segment.AppendNullData(ticks);
      } else {
        // The input track is not blocked in this interval, so no need to call
        // GraphTimeToTrackTimeWithBlocking.
        TrackTime inputStart =
            std::min(inputSegment.GetDuration(),
                     source->GraphTimeToTrackTime(interval.mStart));
        TrackTime inputEnd =
            std::min(inputSegment.GetDuration(),
                     source->GraphTimeToTrackTime(interval.mEnd));

        segment.AppendSlice(inputSegment, inputStart, inputEnd);
        // Pad if we're looking past the end of the track
        segment.AppendNullData(ticks - (inputEnd - inputStart));
      }
    }
    segment.ApplyVolume(mVolume);

    for (AudioSegment::ChunkIterator iter(segment); !iter.IsEnded();
         iter.Next()) {
      inputChannels =
          GetAudioChannelsSuperset(inputChannels, iter->ChannelCount());
    }
  }

  uint32_t accumulateIndex = 0;
  if (inputChannels) {
    DownmixBufferType downmixBuffer;
    ASSERT_ALIGNED16(downmixBuffer.Elements());
    for (auto& audioSegment : audioSegments) {
      AudioBlock tmpChunk;
      ConvertSegmentToAudioBlock(&audioSegment, &tmpChunk, inputChannels);
      if (!tmpChunk.IsNull()) {
        if (accumulateIndex == 0) {
          mLastChunks[0].AllocateChannels(inputChannels);
        }
        AccumulateInputChunk(accumulateIndex, tmpChunk, &mLastChunks[0],
                             &downmixBuffer);
        accumulateIndex++;
      }
    }
  }
  if (accumulateIndex == 0) {
    mLastChunks[0].SetNull(WEBAUDIO_BLOCK_SIZE);
  }
}

bool AudioNodeExternalInputTrack::IsEnabled() {
  return ((MediaStreamAudioSourceNodeEngine*)Engine())->IsEnabled();
}

void AudioNodeExternalInputTrack::SetVolume(float aVolume) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("Set volume {}", aVolume);
  QueueControlMessageWithNoShutdown([self = RefPtr{this}, this, aVolume] {
    LOG("Apply volume {}", aVolume);
    mVolume = aVolume;
  });
}

#undef LOG

}  // namespace mozilla
