/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DecoderTypes_h
#define mozilla_dom_DecoderTypes_h

#include "MediaData.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/AudioData.h"
#include "mozilla/dom/AudioDecoderBinding.h"
#include "mozilla/dom/EncodedAudioChunk.h"
#include "mozilla/dom/EncodedVideoChunk.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/VideoDecoderBinding.h"
#include "mozilla/dom/VideoFrame.h"
#include "nsStringFwd.h"
#include "nsTLiteralString.h"

namespace mozilla {

class TrackInfo;
class MediaByteBuffer;

namespace dom {

class VideoDecoderConfigInternal {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VideoDecoderConfigInternal);

  static RefPtr<VideoDecoderConfigInternal> Create(
      const VideoDecoderConfig& aConfig);

  VideoDecoderConfigInternal(const nsAString& aCodec,
                             Maybe<uint32_t>&& aCodedHeight,
                             Maybe<uint32_t>&& aCodedWidth,
                             Maybe<VideoColorSpaceInternal>&& aColorSpace,
                             already_AddRefed<MediaByteBuffer> aDescription,
                             Maybe<uint32_t>&& aDisplayAspectHeight,
                             Maybe<uint32_t>&& aDisplayAspectWidth,
                             const HardwareAcceleration& aHardwareAcceleration,
                             Maybe<bool>&& aOptimizeForLatency);

  nsCString ToString() const;

  bool Equals(const VideoDecoderConfigInternal& aOther) const {
    if (mDescription != aOther.mDescription) {
      return false;
    }
    if (mDescription && aOther.mDescription) {
      auto lhs = mDescription;
      auto rhs = aOther.mDescription;
      if (lhs->Length() != rhs->Length()) {
        return false;
      }
      if (!ArrayEqual(lhs->Elements(), rhs->Elements(), lhs->Length())) {
        return false;
      }
    }
    return mCodec.Equals(aOther.mCodec) &&
           mCodedHeight == aOther.mCodedHeight &&
           mCodedWidth == aOther.mCodedWidth &&
           mColorSpace == aOther.mColorSpace &&
           mDisplayAspectHeight == aOther.mDisplayAspectWidth &&
           mHardwareAcceleration == aOther.mHardwareAcceleration &&
           mOptimizeForLatency == aOther.mOptimizeForLatency;
  }

  nsString mCodec;
  Maybe<uint32_t> mCodedHeight;
  Maybe<uint32_t> mCodedWidth;
  Maybe<VideoColorSpaceInternal> mColorSpace;
  RefPtr<MediaByteBuffer> mDescription;
  Maybe<uint32_t> mDisplayAspectHeight;
  Maybe<uint32_t> mDisplayAspectWidth;
  HardwareAcceleration mHardwareAcceleration;
  Maybe<bool> mOptimizeForLatency;

 private:
  ~VideoDecoderConfigInternal() = default;
};

class VideoDecoderTraits {
 public:
  static constexpr nsLiteralCString Name = "VideoDecoder"_ns;
  using ConfigType = VideoDecoderConfig;
  using ConfigTypeInternal = VideoDecoderConfigInternal;
  using InputType = EncodedVideoChunk;
  using InputTypeInternal = EncodedVideoChunkData;
  using OutputType = VideoFrame;
  using OutputCallbackType = VideoFrameOutputCallback;

  static bool IsSupported(const ConfigTypeInternal& aConfig);
  static Result<UniquePtr<TrackInfo>, nsresult> CreateTrackInfo(
      const ConfigTypeInternal& aConfig);
  static bool Validate(const ConfigType& aConfig, nsCString& aErrorMessage);
  static RefPtr<ConfigTypeInternal> CreateConfigInternal(
      const ConfigType& aConfig);
  static bool IsKeyChunk(const InputType& aInput);
  static UniquePtr<InputTypeInternal> CreateInputInternal(
      const InputType& aInput);
};

class AudioDecoderConfigInternal {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioDecoderConfigInternal);

  static RefPtr<AudioDecoderConfigInternal> Create(
      const AudioDecoderConfig& aConfig);

  AudioDecoderConfigInternal(const nsAString& aCodec, uint32_t aSampleRate,
                             uint32_t aNumberOfChannels,
                             already_AddRefed<MediaByteBuffer> aDescription);

  bool Equals(const AudioDecoderConfigInternal& aOther) const {
    if (mDescription != aOther.mDescription) {
      return false;
    }
    if (mDescription && aOther.mDescription) {
      auto lhs = mDescription;
      auto rhs = aOther.mDescription;
      if (lhs->Length() != rhs->Length()) {
        return false;
      }
      if (!ArrayEqual(lhs->Elements(), rhs->Elements(), lhs->Length())) {
        return false;
      }
    }
    return mCodec.Equals(aOther.mCodec) && mSampleRate == aOther.mSampleRate &&
           mNumberOfChannels == aOther.mNumberOfChannels &&
           mOptimizeForLatency == aOther.mOptimizeForLatency;
  }
  nsCString ToString() const;

  nsString mCodec;
  uint32_t mSampleRate;
  uint32_t mNumberOfChannels;
  RefPtr<MediaByteBuffer> mDescription;
  // Compilation fix, should be abstracted by DecoderAgent since those are not
  // supported
  HardwareAcceleration mHardwareAcceleration =
      HardwareAcceleration::No_preference;
  Maybe<bool> mOptimizeForLatency;

 private:
  ~AudioDecoderConfigInternal() = default;
};

class AudioDecoderTraits {
 public:
  static constexpr nsLiteralCString Name = "AudioDecoder"_ns;
  using ConfigType = AudioDecoderConfig;
  using ConfigTypeInternal = AudioDecoderConfigInternal;
  using InputType = EncodedAudioChunk;
  using InputTypeInternal = EncodedAudioChunkData;
  using OutputType = AudioData;
  using OutputCallbackType = AudioDataOutputCallback;

  static bool IsSupported(const ConfigTypeInternal& aConfig);
  static Result<UniquePtr<TrackInfo>, nsresult> CreateTrackInfo(
      const ConfigTypeInternal& aConfig);
  static bool Validate(const ConfigType& aConfig, nsCString& aErrorMessage);
  static RefPtr<ConfigTypeInternal> CreateConfigInternal(
      const ConfigType& aConfig);
  static bool IsKeyChunk(const InputType& aInput);
  static UniquePtr<InputTypeInternal> CreateInputInternal(
      const InputType& aInput);
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_DecoderTypes_h
