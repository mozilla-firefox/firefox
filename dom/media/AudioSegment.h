/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_AUDIOSEGMENT_H_
#define MOZILLA_AUDIOSEGMENT_H_

#include <speex/speex_resampler.h>

#include "AudioChannelFormat.h"
#include "AudioSampleFormat.h"
#include "MediaSegment.h"
#include "SharedBuffer.h"
#include "WebAudioUtils.h"
#include "mozilla/ScopeExit.h"
#include "nsAutoRef.h"
#ifdef MOZILLA_INTERNAL_API
#  include "mozilla/TimeStamp.h"
#endif
#include <float.h>

namespace mozilla {
struct AudioChunk;
class AudioSegment;
}  // namespace mozilla
MOZ_DECLARE_RELOCATE_USING_MOVE_CONSTRUCTOR(mozilla::AudioChunk)

/**
 * This allows compilation of nsTArray<AudioSegment> and
 * AutoTArray<AudioSegment> since without it, static analysis fails on the
 * mChunks member being a non-memmovable AutoTArray.
 *
 * Note that AudioSegment(const AudioSegment&) is deleted, so this should
 * never come into effect.
 */
MOZ_DECLARE_RELOCATE_USING_MOVE_CONSTRUCTOR(mozilla::AudioSegment)

namespace mozilla {

template <typename T>
class SharedChannelArrayBuffer : public ThreadSharedObject {
 public:
  explicit SharedChannelArrayBuffer(nsTArray<nsTArray<T> >&& aBuffers)
      : mBuffers(std::move(aBuffers)) {}

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const override {
    size_t amount = 0;
    amount += mBuffers.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (size_t i = 0; i < mBuffers.Length(); i++) {
      amount += mBuffers[i].ShallowSizeOfExcludingThis(aMallocSizeOf);
    }

    return amount;
  }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  nsTArray<nsTArray<T> > mBuffers;
};

class AudioMixer;

/**
 * For auto-arrays etc, guess this as the common number of channels.
 */
const int GUESS_AUDIO_CHANNELS = 2;

// We ensure that the graph advances in steps that are multiples of the Web
// Audio block size
const uint32_t WEBAUDIO_BLOCK_SIZE_BITS = 7;
const uint32_t WEBAUDIO_BLOCK_SIZE = 1 << WEBAUDIO_BLOCK_SIZE_BITS;

template <typename SrcT, typename DestT>
void InterleaveAndConvertBuffer(const SrcT* const* aSourceChannels,
                                uint32_t aLength, float aVolume,
                                uint32_t aChannelCount, DestT* aOutput) {
  for (size_t channel = 0; channel < aChannelCount; ++channel) {
    DestT* output = aOutput + channel;
    if (aSourceChannels[channel]) {
      for (size_t i = 0; i < aLength; ++i) {
        float v =
            ConvertAudioSample<float>(aSourceChannels[channel][i]) * aVolume;
        *output = FloatToAudioSample<DestT>(v);
        output += aChannelCount;
      }
    } else {
      for (size_t i = 0; i < aLength; ++i) {
        *output = static_cast<DestT>(0);
        output += aChannelCount;
      }
    }
  }
}

template <typename SrcT, typename DestT>
void DeinterleaveAndConvertBuffer(const SrcT* aSourceBuffer, uint32_t aFrames,
                                  uint32_t aChannels, DestT** aOutput) {
  for (size_t i = 0; i < aChannels; i++) {
    size_t interleavedIndex = i;
    for (size_t j = 0; j < aFrames; j++) {
      aOutput[i][j] =
          ConvertAudioSample<DestT>(aSourceBuffer[interleavedIndex]);
      interleavedIndex += aChannels;
    }
  }
}

class SilentChannel {
 public:
  static const int AUDIO_PROCESSING_FRAMES = 640; /* > 10ms of 48KHz audio */
  static const uint8_t
      gZeroChannel[MAX_AUDIO_SAMPLE_SIZE * AUDIO_PROCESSING_FRAMES];
  // We take advantage of the fact that zero in float and zero in int have the
  // same all-zeros bit layout.
  template <typename T>
  static const T* ZeroChannel();
};

/**
 * Given an array of input channels (aChannelData), downmix to aOutputChannels,
 * interleave the channel data. A total of aOutputChannels*aDuration
 * interleaved samples will be copied to a channel buffer in aOutput.
 */
template <typename SrcT, typename DestT>
void DownmixAndInterleave(Span<const SrcT* const> aChannelData,
                          int32_t aDuration, float aVolume,
                          uint32_t aOutputChannels, DestT* aOutput) {
  if (aChannelData.Length() == aOutputChannels) {
    InterleaveAndConvertBuffer(aChannelData.Elements(), aDuration, aVolume,
                               aOutputChannels, aOutput);
  } else {
    AutoTArray<SrcT*, GUESS_AUDIO_CHANNELS> outputChannelData;
    AutoTArray<SrcT,
               SilentChannel::AUDIO_PROCESSING_FRAMES * GUESS_AUDIO_CHANNELS>
        outputBuffers;
    outputChannelData.SetLength(aOutputChannels);
    outputBuffers.SetLength(aDuration * aOutputChannels);
    for (uint32_t i = 0; i < aOutputChannels; i++) {
      outputChannelData[i] = outputBuffers.Elements() + aDuration * i;
    }
    AudioChannelsDownMix<SrcT, SrcT>(aChannelData, outputChannelData,
                                     aDuration);
    InterleaveAndConvertBuffer(outputChannelData.Elements(), aDuration, aVolume,
                               aOutputChannels, aOutput);
  }
}

/**
 * An AudioChunk represents a multi-channel buffer of audio samples.
 * It references an underlying ThreadSharedObject which manages the lifetime
 * of the buffer. An AudioChunk maintains its own duration and channel data
 * pointers so it can represent a subinterval of a buffer without copying.
 * An AudioChunk can store its individual channels anywhere; it maintains
 * separate pointers to each channel's buffer.
 */
struct AudioChunk {
  using SampleFormat = mozilla::AudioSampleFormat;

  AudioChunk() = default;

  template <typename T>
  AudioChunk(already_AddRefed<ThreadSharedObject> aBuffer,
             const nsTArray<const T*>& aChannelData, TrackTime aDuration,
             PrincipalHandle aPrincipalHandle)
      : mDuration(aDuration),
        mBuffer(aBuffer),
        mBufferFormat(AudioSampleTypeToFormat<T>::Format),
        mPrincipalHandle(std::move(aPrincipalHandle)) {
    MOZ_ASSERT(!mBuffer == aChannelData.IsEmpty(), "Appending invalid data ?");
    for (const T* data : aChannelData) {
      mChannelData.AppendElement(data);
    }
  }

  // Generic methods
  void SliceTo(TrackTime aStart, TrackTime aEnd) {
    MOZ_ASSERT(aStart >= 0, "Slice out of bounds: invalid start");
    MOZ_ASSERT(aStart < aEnd, "Slice out of bounds: invalid range");
    MOZ_ASSERT(aEnd <= mDuration, "Slice out of bounds: invalid end");

    if (mBuffer) {
      MOZ_ASSERT(aStart < INT32_MAX,
                 "Can't slice beyond 32-bit sample lengths");
      for (uint32_t channel = 0; channel < mChannelData.Length(); ++channel) {
        mChannelData[channel] = AddAudioSampleOffset(
            mChannelData[channel], mBufferFormat, int32_t(aStart));
      }
    }
    mDuration = aEnd - aStart;
  }
  TrackTime GetDuration() const { return mDuration; }
  bool CanCombineWithFollowing(const AudioChunk& aOther) const {
    if (aOther.mBuffer != mBuffer) {
      return false;
    }
    if (!mBuffer) {
      return true;
    }
    if (aOther.mVolume != mVolume) {
      return false;
    }
    if (aOther.mPrincipalHandle != mPrincipalHandle) {
      return false;
    }
    NS_ASSERTION(aOther.mBufferFormat == mBufferFormat,
                 "Wrong metadata about buffer");
    NS_ASSERTION(aOther.mChannelData.Length() == mChannelData.Length(),
                 "Mismatched channel count");
    if (mDuration > INT32_MAX) {
      return false;
    }
    for (uint32_t channel = 0; channel < mChannelData.Length(); ++channel) {
      if (aOther.mChannelData[channel] !=
          AddAudioSampleOffset(mChannelData[channel], mBufferFormat,
                               int32_t(mDuration))) {
        return false;
      }
    }
    return true;
  }
  bool IsNull() const { return mBuffer == nullptr; }
  void SetNull(TrackTime aDuration) {
    mBuffer = nullptr;
    mChannelData.Clear();
    mDuration = aDuration;
    mVolume = 1.0f;
    mBufferFormat = AUDIO_FORMAT_SILENCE;
    mPrincipalHandle = PRINCIPAL_HANDLE_NONE;
  }

  uint32_t ChannelCount() const { return mChannelData.Length(); }

  bool IsMuted() const { return mVolume == 0.0f; }

  size_t SizeOfExcludingThisIfUnshared(MallocSizeOf aMallocSizeOf) const {
    return SizeOfExcludingThis(aMallocSizeOf, true);
  }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf, bool aUnshared) const {
    size_t amount = 0;

    // Possibly owned:
    // - mBuffer - Can hold data that is also in the decoded audio queue. If it
    //             is not shared, or unshared == false it gets counted.
    if (mBuffer && (!aUnshared || !mBuffer->IsShared())) {
      amount += mBuffer->SizeOfIncludingThis(aMallocSizeOf);
    }

    // Memory in the array is owned by mBuffer.
    amount += mChannelData.ShallowSizeOfExcludingThis(aMallocSizeOf);
    return amount;
  }

  template <typename T>
  Span<const T* const> ChannelData() const {
    MOZ_ASSERT(AudioSampleTypeToFormat<T>::Format == mBufferFormat);
    return Span(reinterpret_cast<const T* const*>(mChannelData.Elements()),
                mChannelData.Length());
  }

  /**
   * ChannelFloatsForWrite() should be used only when mBuffer is owned solely
   * by the calling thread.
   */
  template <typename T>
  T* ChannelDataForWrite(size_t aChannel) {
    MOZ_ASSERT(AudioSampleTypeToFormat<T>::Format == mBufferFormat);
    MOZ_ASSERT(!mBuffer->IsShared());
    // Array access check for 1905287
    if (aChannel >= mChannelData.Length()) {
      MOZ_CRASH_UNSAFE_PRINTF(
          "Invalid index: aChannel: %zu, mChannelData size: %zu\n", aChannel,
          mChannelData.Length());
    }
    return static_cast<T*>(const_cast<void*>(mChannelData[aChannel]));
  }

  template <typename T>
  static AudioChunk FromInterleavedBuffer(
      const T* aBuffer, size_t aFrames, uint32_t aChannels,
      const PrincipalHandle& aPrincipalHandle) {
    CheckedInt<size_t> bufferSize(sizeof(T));
    bufferSize *= aFrames;
    bufferSize *= aChannels;
    RefPtr<SharedBuffer> buffer = SharedBuffer::Create(bufferSize);

    AutoTArray<T*, 8> deinterleaved;
    if (aChannels == 1) {
      PodCopy(static_cast<T*>(buffer->Data()), aBuffer, aFrames);
      deinterleaved.AppendElement(static_cast<T*>(buffer->Data()));
    } else {
      deinterleaved.SetLength(aChannels);
      T* samples = static_cast<T*>(buffer->Data());

      size_t offset = 0;
      for (uint32_t i = 0; i < aChannels; ++i) {
        deinterleaved[i] = samples + offset;
        offset += aFrames;
      }

      DeinterleaveAndConvertBuffer(aBuffer, static_cast<uint32_t>(aFrames),
                                   aChannels, deinterleaved.Elements());
    }

    AutoTArray<const T*, GUESS_AUDIO_CHANNELS> channelData;
    channelData.AppendElements(deinterleaved);
    return AudioChunk(buffer.forget(), channelData,
                      static_cast<TrackTime>(aFrames), aPrincipalHandle);
  }

  const PrincipalHandle& GetPrincipalHandle() const { return mPrincipalHandle; }

  // aOutputChannels must contain pointers to channel data of length mDuration.
  void DownMixTo(Span<AudioDataValue* const> aOutputChannels) const;

  TrackTime mDuration = 0;             // in frames within the buffer
  RefPtr<ThreadSharedObject> mBuffer;  // the buffer object whose lifetime is
                                       // managed; null means data is all zeroes
  // one pointer per channel; empty if and only if mBuffer is null
  CopyableAutoTArray<const void*, GUESS_AUDIO_CHANNELS> mChannelData;
  float mVolume = 1.0f;  // volume multiplier to apply
  // format of frames in mBuffer (or silence if mBuffer is null)
  SampleFormat mBufferFormat = AUDIO_FORMAT_SILENCE;
  // principalHandle for the data in this chunk.
  // This can be compared to an nsIPrincipal* when back on main thread.
  PrincipalHandle mPrincipalHandle = PRINCIPAL_HANDLE_NONE;
};

/**
 * A list of audio samples consisting of a sequence of slices of SharedBuffers.
 * The audio rate is determined by the track, not stored in this class.
 */
class AudioSegment final : public MediaSegmentBase<AudioSegment, AudioChunk> {
  // The channel count that MaxChannelCount() returned last time it was called.
  uint32_t mMemoizedMaxChannelCount = 0;

 public:
  typedef mozilla::AudioSampleFormat SampleFormat;

  AudioSegment() : MediaSegmentBase<AudioSegment, AudioChunk>(AUDIO) {}

  AudioSegment(AudioSegment&& aSegment) = default;

  AudioSegment(const AudioSegment&) = delete;
  AudioSegment& operator=(const AudioSegment&) = delete;

  ~AudioSegment() = default;

  // Resample the whole segment in place.  `aResampler` is an instance of a
  // resampler, initialized with `aResamplerChannelCount` channels. If this
  // function finds a chunk with more channels, `aResampler` is destroyed and a
  // new resampler is created, and `aResamplerChannelCount` is updated with the
  // new channel count value.
  // Rates must be non-zero. Clears the segment and returns an error if the
  // output size is not representable or the sample rates are outside the range
  // supported by the resampler.
  [[nodiscard]] nsresult ResampleChunks(
      nsAutoRef<SpeexResamplerState>& aResampler,
      uint32_t* aResamplerChannelCount, uint32_t aInRate, uint32_t aOutRate);

  template <typename T>
  void AppendFrames(already_AddRefed<ThreadSharedObject> aBuffer,
                    const nsTArray<const T*>& aChannelData, TrackTime aDuration,
                    const PrincipalHandle& aPrincipalHandle) {
    AppendAndConsumeChunk(AudioChunk(std::move(aBuffer), aChannelData,
                                     aDuration, aPrincipalHandle));
  }
  void AppendSegment(const AudioSegment* aSegment) {
    MOZ_ASSERT(aSegment);

    for (const AudioChunk& c : aSegment->mChunks) {
      AudioChunk* chunk = AppendChunk(c.GetDuration());
      chunk->mBuffer = c.mBuffer;
      chunk->mChannelData = c.mChannelData;
      chunk->mBufferFormat = c.mBufferFormat;
      chunk->mPrincipalHandle = c.mPrincipalHandle;
    }
  }
  template <typename T>
  void AppendFromInterleavedBuffer(const T* aBuffer, size_t aFrames,
                                   uint32_t aChannels,
                                   const PrincipalHandle& aPrincipalHandle) {
    AppendAndConsumeChunk(AudioChunk::FromInterleavedBuffer<T>(
        aBuffer, aFrames, aChannels, aPrincipalHandle));
  }
  // Write the segement data into an interleaved buffer. Do mixing if the
  // AudioChunk's channel count in the segment is different from aChannels.
  // Returns sample count of the converted audio data. The converted data will
  // be stored into aBuffer.
  size_t WriteToInterleavedBuffer(nsTArray<AudioDataValue>& aBuffer,
                                  uint32_t aChannels) const;
  // Consumes aChunk, and append it to the segment if its duration is not zero.
  void AppendAndConsumeChunk(AudioChunk&& aChunk) {
    AudioChunk unused;
    AudioChunk* chunk = &unused;

    // Always consume aChunk. The chunk's mBuffer can be non-null even if its
    // duration is 0.
    auto consume = MakeScopeExit([&] {
      chunk->mBuffer = std::move(aChunk.mBuffer);
      chunk->mChannelData = std::move(aChunk.mChannelData);

      MOZ_ASSERT(chunk->mBuffer || chunk->mChannelData.IsEmpty(),
                 "Appending invalid data ?");

      chunk->mVolume = aChunk.mVolume;
      chunk->mBufferFormat = aChunk.mBufferFormat;
      chunk->mPrincipalHandle = std::move(aChunk.mPrincipalHandle);
    });

    if (aChunk.GetDuration() == 0) {
      return;
    }

    if (!mChunks.IsEmpty() &&
        mChunks.LastElement().CanCombineWithFollowing(aChunk)) {
      mChunks.LastElement().mDuration += aChunk.GetDuration();
      mDuration += aChunk.GetDuration();
      return;
    }

    chunk = AppendChunk(aChunk.mDuration);
  }
  void ApplyVolume(float aVolume);
  // Mix the segment into a mixer, keeping it planar, up or down mixing to
  // aChannelCount channels.
  void Mix(AudioMixer& aMixer, uint32_t aChannelCount, uint32_t aSampleRate);

  // Returns the maximum channel count across all chunks in this segment.
  // Should there be no chunk with a channel count we return the memoized return
  // value from last time this method was called.
  uint32_t MaxChannelCount() {
    uint32_t channelCount = 0;
    for (ChunkIterator ci(*this); !ci.IsEnded(); ci.Next()) {
      if (ci->ChannelCount()) {
        channelCount = std::max(channelCount, ci->ChannelCount());
      }
    }
    if (channelCount == 0) {
      return mMemoizedMaxChannelCount;
    }
    return mMemoizedMaxChannelCount = channelCount;
  }

  static Type StaticType() { return AUDIO; }

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
  }

  PrincipalHandle GetOldestPrinciple() const {
    const AudioChunk* chunk = mChunks.IsEmpty() ? nullptr : &mChunks[0];
    return chunk ? chunk->GetPrincipalHandle() : PRINCIPAL_HANDLE_NONE;
  }

  // Iterate on each chunks until the input function returns true.
  template <typename Function>
  void IterateOnChunks(const Function&& aFunction) {
    for (uint32_t idx = 0; idx < mChunks.Length(); idx++) {
      if (aFunction(&mChunks[idx])) {
        return;
      }
    }
  }

 private:
  template <typename T>
  [[nodiscard]] nsresult Resample(nsAutoRef<SpeexResamplerState>& aResampler,
                                  uint32_t* aResamplerChannelCount,
                                  uint32_t aInRate, uint32_t aOutRate);
};

template <typename SrcT>
void WriteChunk(const AudioChunk& aChunk, uint32_t aOutputChannels,
                float aVolume, AudioDataValue* aOutputBuffer) {
  CopyableAutoTArray<const SrcT*, GUESS_AUDIO_CHANNELS> channelData;
  channelData.AppendElements(aChunk.ChannelData<SrcT>());

  if (channelData.Length() < aOutputChannels) {
    // Up-mix. Note that this might actually make channelData have more
    // than aOutputChannels temporarily.
    AudioChannelsUpMix(&channelData, aOutputChannels,
                       static_cast<const SrcT*>(nullptr));
  }
  if (channelData.Length() > aOutputChannels) {
    // Down-mix.
    DownmixAndInterleave<SrcT>(channelData, aChunk.mDuration, aVolume,
                               aOutputChannels, aOutputBuffer);
  } else {
    InterleaveAndConvertBuffer(channelData.Elements(), aChunk.mDuration,
                               aVolume, aOutputChannels, aOutputBuffer);
  }
}

}  // namespace mozilla

#endif /* MOZILLA_AUDIOSEGMENT_H_ */
