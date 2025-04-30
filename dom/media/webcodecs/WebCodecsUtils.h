/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_DOM_WEBCODECS_WEBCODECSUTILS_H
#define MOZILLA_DOM_WEBCODECS_WEBCODECSUTILS_H

#include "ErrorList.h"
#include "MediaData.h"
#include "PlatformEncoderModule.h"
#include "js/TypeDecls.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/Result.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/AudioDataBinding.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/VideoEncoderBinding.h"
#include "mozilla/dom/VideoFrameBinding.h"

namespace mozilla {

#define WEBCODECS_MARKER(codecType, desc, options, markerType, ...)    \
  do {                                                                 \
    if (profiler_is_collecting_markers()) {                            \
      nsFmtCString marker(FMT_STRING("{}{}"), codecType, desc);        \
      PROFILER_MARKER(                                                 \
          ProfilerString8View::WrapNullTerminatedString(marker.get()), \
          MEDIA_RT, options, markerType, __VA_ARGS__);                 \
    }                                                                  \
  } while (0)

#define WEBCODECS_MARKER_INTERVAL_START(type, desc)                      \
  WEBCODECS_MARKER(type, desc, {MarkerTiming::IntervalStart()}, Tracing, \
                   "WebCodecs")
#define WEBCODECS_MARKER_INTERVAL_END(type, desc)                      \
  WEBCODECS_MARKER(type, desc, {MarkerTiming::IntervalEnd()}, Tracing, \
                   "WebCodecs")

class AutoWebCodecsMarker {
 public:
  AutoWebCodecsMarker(const char* aType, const char* aDesc)
      : mType(aType), mDesc(aDesc) {
    WEBCODECS_MARKER_INTERVAL_START(mType, mDesc);
  }

  AutoWebCodecsMarker(AutoWebCodecsMarker&& aOther) noexcept {
    MOZ_ASSERT(!aOther.mEnded, "Ended marker should not be moved");
    mType = std::move(aOther.mType);
    mDesc = std::move(aOther.mDesc);
    mEnded = std::move(aOther.mEnded);
    aOther.mEnded = true;
  }

  AutoWebCodecsMarker& operator=(AutoWebCodecsMarker&& aOther) noexcept {
    if (this != &aOther) {
      MOZ_ASSERT(!aOther.mEnded, "Ended marker should not be moved");
      mType = std::move(aOther.mType);
      mDesc = std::move(aOther.mDesc);
      mEnded = std::move(aOther.mEnded);
      aOther.mEnded = true;
    }
    return *this;
  }

  AutoWebCodecsMarker(const AutoWebCodecsMarker& aOther) = delete;
  AutoWebCodecsMarker& operator=(const AutoWebCodecsMarker& aOther) = delete;

  ~AutoWebCodecsMarker() { End(); }

  void End() {
    if (!mEnded) {
      WEBCODECS_MARKER_INTERVAL_END(mType, mDesc);
      mEnded = true;
    }
  }

 private:
  const char* mType;
  const char* mDesc;
  bool mEnded = false;
};

namespace gfx {
enum class ColorRange : uint8_t;
enum class ColorSpace2 : uint8_t;
enum class SurfaceFormat : int8_t;
enum class TransferFunction : uint8_t;
enum class YUVColorSpace : uint8_t;
}  // namespace gfx

using WebCodecsId = size_t;

extern std::atomic<WebCodecsId> sNextId;

struct EncoderConfigurationChangeList;

namespace dom {

/*
 * The followings are helpers for WebCodecs methods.
 */

nsTArray<nsCString> GuessContainers(const nsAString& aCodec);

Maybe<nsString> ParseCodecString(const nsAString& aCodec);

bool IsSameColorSpace(const VideoColorSpaceInit& aLhs,
                      const VideoColorSpaceInit& aRhs);

/*
 * Below are helpers for conversion among Maybe, Optional, and Nullable.
 */

template <typename T>
Maybe<T> OptionalToMaybe(const Optional<T>& aOptional) {
  if (aOptional.WasPassed()) {
    return Some(aOptional.Value());
  }
  return Nothing();
}

template <typename T>
const T* OptionalToPointer(const Optional<T>& aOptional) {
  return aOptional.WasPassed() ? &aOptional.Value() : nullptr;
}

template <typename T>
Maybe<T> NullableToMaybe(const Nullable<T>& aNullable) {
  if (!aNullable.IsNull()) {
    return Some(aNullable.Value());
  }
  return Nothing();
}

template <typename T>
Nullable<T> MaybeToNullable(const Maybe<T>& aOptional) {
  if (aOptional.isSome()) {
    return Nullable<T>(aOptional.value());
  }
  return Nullable<T>();
}

/*
 * Below are helpers to operate ArrayBuffer or ArrayBufferView.
 */

Result<Ok, nsresult> CloneBuffer(JSContext* aCx,
                                 OwningAllowSharedBufferSource& aDest,
                                 const OwningAllowSharedBufferSource& aSrc,
                                 ErrorResult& aRv);

Result<RefPtr<MediaByteBuffer>, nsresult> GetExtraDataFromArrayBuffer(
    const OwningAllowSharedBufferSource& aBuffer);

bool CopyExtradataToDescription(JSContext* aCx, Span<const uint8_t>& aSrc,
                                OwningAllowSharedBufferSource& aDest);

/*
 * The following are utilities to convert between VideoColorSpace values to
 * gfx's values.
 */

struct VideoColorSpaceInternal {
  explicit VideoColorSpaceInternal(const VideoColorSpaceInit& aColorSpaceInit);
  VideoColorSpaceInternal() = default;
  VideoColorSpaceInternal(const bool& aFullRange,
                          const VideoMatrixCoefficients& aMatrix,
                          const VideoColorPrimaries& aPrimaries,
                          const VideoTransferCharacteristics& aTransfer)
      : mFullRange(Some(aFullRange)),
        mMatrix(Some(aMatrix)),
        mPrimaries(Some(aPrimaries)),
        mTransfer(Some(aTransfer)) {}
  VideoColorSpaceInternal(const VideoColorSpaceInternal& aOther) = default;
  VideoColorSpaceInternal(VideoColorSpaceInternal&& aOther) = default;

  VideoColorSpaceInternal& operator=(const VideoColorSpaceInternal& aOther) =
      default;
  VideoColorSpaceInternal& operator=(VideoColorSpaceInternal&& aOther) =
      default;

  bool operator==(const VideoColorSpaceInternal& aOther) const {
    return mFullRange == aOther.mFullRange && mMatrix == aOther.mMatrix &&
           mPrimaries == aOther.mPrimaries && mTransfer == aOther.mTransfer;
  }
  bool operator!=(const VideoColorSpaceInternal& aOther) const {
    return !(*this == aOther);
  }

  VideoColorSpaceInit ToColorSpaceInit() const;
  nsCString ToString() const;

  Maybe<bool> mFullRange;
  Maybe<VideoMatrixCoefficients> mMatrix;
  Maybe<VideoColorPrimaries> mPrimaries;
  Maybe<VideoTransferCharacteristics> mTransfer;
};

gfx::ColorRange ToColorRange(bool aIsFullRange);

gfx::YUVColorSpace ToColorSpace(VideoMatrixCoefficients aMatrix);

gfx::TransferFunction ToTransferFunction(
    VideoTransferCharacteristics aTransfer);

gfx::ColorSpace2 ToPrimaries(VideoColorPrimaries aPrimaries);

bool ToFullRange(const gfx::ColorRange& aColorRange);

Maybe<VideoMatrixCoefficients> ToMatrixCoefficients(
    const gfx::YUVColorSpace& aColorSpace);

Maybe<VideoTransferCharacteristics> ToTransferCharacteristics(
    const gfx::TransferFunction& aTransferFunction);

Maybe<VideoColorPrimaries> ToPrimaries(const gfx::ColorSpace2& aColorSpace);

/*
 * The following are utilities to convert from gfx's formats to
 * VideoPixelFormats.
 */

enum class ImageBitmapFormat : uint8_t;
enum class VideoPixelFormat : uint8_t;

Maybe<VideoPixelFormat> SurfaceFormatToVideoPixelFormat(
    gfx::SurfaceFormat aFormat);

Maybe<VideoPixelFormat> ImageBitmapFormatToVideoPixelFormat(
    ImageBitmapFormat aFormat);

template <typename T>
class MessageRequestHolder {
 public:
  MessageRequestHolder() = default;
  ~MessageRequestHolder() = default;

  MozPromiseRequestHolder<T>& Request() { return mRequest; }
  void Disconnect() { mRequest.DisconnectIfExists(); }
  void Complete() { mRequest.Complete(); }
  bool Exists() const { return mRequest.Exists(); }

 protected:
  MozPromiseRequestHolder<T> mRequest{};
};

enum class MessageProcessedResult { NotProcessed, Processed };

bool IsOnAndroid();
bool IsOnMacOS();
bool IsOnLinux();

// Wrap a type to make it unique. This allows using ergonomically in the Variant
// below. Simply aliasing with `using` isn't enough, because typedefs in C++
// don't produce strong types, so two integer variants result in
// the same type, making it ambiguous to the Variant code.
// T is the type to be wrapped. Phantom is a type that is only used to
// disambiguate and should be unique in the program.
template <typename T, typename Phantom>
class StrongTypedef {
 public:
  explicit StrongTypedef(T const& value) : mValue(value) {}
  explicit StrongTypedef(T&& value) : mValue(std::move(value)) {}
  T& get() { return mValue; }
  T const& get() const { return mValue; }

 private:
  T mValue;
};

using CodecChange = StrongTypedef<nsString, struct CodecChangeTypeWebCodecs>;
using DimensionsChange =
    StrongTypedef<gfx::IntSize, struct DimensionsChangeTypeWebCodecs>;
using DisplayDimensionsChange =
    StrongTypedef<Maybe<gfx::IntSize>,
                  struct DisplayDimensionsChangeTypeWebCodecs>;
using BitrateChange =
    StrongTypedef<Maybe<uint32_t>, struct BitrateChangeTypeWebCodecs>;
using FramerateChange =
    StrongTypedef<Maybe<double>, struct FramerateChangeTypeWebCodecs>;
using HardwareAccelerationChange =
    StrongTypedef<dom::HardwareAcceleration,
                  struct HardwareAccelerationChangeTypeWebCodecs>;
using AlphaChange =
    StrongTypedef<dom::AlphaOption, struct AlphaChangeTypeWebCodecs>;
using ScalabilityModeChange =
    StrongTypedef<Maybe<nsString>, struct ScalabilityModeChangeTypeWebCodecs>;
using BitrateModeChange = StrongTypedef<dom::VideoEncoderBitrateMode,
                                        struct BitrateModeChangeTypeWebCodecs>;
using LatencyModeChange =
    StrongTypedef<dom::LatencyMode, struct LatencyModeTypeChangeTypeWebCodecs>;
using ContentHintChange =
    StrongTypedef<Maybe<nsString>, struct ContentHintTypeTypeWebCodecs>;

using WebCodecsEncoderConfigurationItem =
    Variant<CodecChange, DimensionsChange, DisplayDimensionsChange,
            BitrateModeChange, BitrateChange, FramerateChange,
            HardwareAccelerationChange, AlphaChange, ScalabilityModeChange,
            LatencyModeChange, ContentHintChange>;

struct WebCodecsConfigurationChangeList {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebCodecsConfigurationChangeList)
  bool Empty() const { return mChanges.IsEmpty(); }
  template <typename T>
  void Push(const T& aItem) {
    mChanges.AppendElement(aItem);
  }
  // This returns true if it should be possible to attempt to reconfigure the
  // encoder on the fly. It can fail, in which case the encoder will be flushed
  // and a new one will be created with the new set of parameters.
  bool CanAttemptReconfigure() const;

  // Convert this to the format the underlying PEM can understand
  RefPtr<EncoderConfigurationChangeList> ToPEMChangeList() const;
  nsCString ToString() const;

  nsTArray<WebCodecsEncoderConfigurationItem> mChanges;

 private:
  ~WebCodecsConfigurationChangeList() = default;
};

nsCString ColorSpaceInitToString(
    const dom::VideoColorSpaceInit& aColorSpaceInit);

RefPtr<TaskQueue> GetWebCodecsEncoderTaskQueue();
VideoColorSpaceInternal FallbackColorSpaceForVideoContent();
VideoColorSpaceInternal FallbackColorSpaceForWebContent();

Maybe<CodecType> CodecStringToCodecType(const nsAString& aCodecString);

nsCString ConfigToString(const VideoDecoderConfig& aConfig);

// Returns true if a particular codec is supported by WebCodecs.
bool IsSupportedVideoCodec(const nsAString& aCodec);
bool IsSupportedAudioCodec(const nsAString& aCodec);

// Returns the codec string to use in Gecko for a particular container and
// codec name given by WebCodecs. This maps pcm description to the profile
// number, and simply returns the codec name for all other codecs.
nsCString ConvertCodecName(const nsCString& aContainer,
                           const nsCString& aCodec);

uint32_t BytesPerSamples(const mozilla::dom::AudioSampleFormat& aFormat);
}  // namespace dom
}  // namespace mozilla

#endif  // MOZILLA_DOM_WEBCODECS_WEBCODECSUTILS_H
