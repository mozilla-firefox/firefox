/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/AudioData.h"
#include "mozilla/dom/AudioDataBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/StructuredCloneTags.h"
#include "nsStringFwd.h"
#include "nsFmtString.h"

#include <utility>

#include "AudioSampleFormat.h"
#include "WebCodecsUtils.h"
#include "js/StructuredClone.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"

extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

#define LOGD(fmt, ...) \
  MOZ_LOG_FMT(gWebCodecsLog, LogLevel::Debug, fmt, ##__VA_ARGS__)

#define LOGE(fmt, ...) \
  MOZ_LOG_FMT(gWebCodecsLog, LogLevel::Error, fmt, ##__VA_ARGS__)

[[nodiscard]] Result<Ok, nsCString> LogAndReturnErr(const char* aLiteral) {
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("%s", aLiteral));
  return Err(nsCString(aLiteral));
}

template <typename... Args>
[[nodiscard]] Result<Ok, nsCString> LogAndReturnErr(
    fmt::format_string<Args...> aFmt, Args&&... aArgs) {
  nsAutoCStringN<100> str;
  str.AppendVfmt(aFmt, fmt::make_format_args(aArgs...));
  MOZ_LOG(gWebCodecsLog, LogLevel::Debug, ("%s", str.get()));
  return Err(str);
}

// Only needed for refcounted objects.
//
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(AudioData)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(AudioData)
  tmp->CloseIfNeeded();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(AudioData)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(AudioData)
// AudioData should be released as soon as its refcount drops to zero,
// without waiting for async deletion by the cycle collector, since it may hold
// a large-size PCM buffer.
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(AudioData, CloseIfNeeded())
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AudioData)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/*
 * W3C Webcodecs AudioData implementation
 */

AudioData::AudioData(nsIGlobalObject* aParent,
                     const AudioDataSerializedData& aData)
    : mParent(aParent),
      mTimestamp(aData.mTimestamp),
      mNumberOfChannels(aData.mNumberOfChannels),
      mNumberOfFrames(aData.mNumberOfFrames),
      mSampleRate(aData.mSampleRate),
      mAudioSampleFormat(aData.mAudioSampleFormat),
      // The resource is not copied, but referenced
      mResource(aData.mResource) {
  MOZ_ASSERT(mParent);
  MOZ_ASSERT(mResource,
             "Resource should always be present then receiving a transfer.");
}

AudioData::AudioData(const AudioData& aOther)
    : mParent(aOther.mParent),
      mTimestamp(aOther.mTimestamp),
      mNumberOfChannels(aOther.mNumberOfChannels),
      mNumberOfFrames(aOther.mNumberOfFrames),
      mSampleRate(aOther.mSampleRate),
      mAudioSampleFormat(aOther.mAudioSampleFormat),
      // The resource is not copied, but referenced
      mResource(aOther.mResource) {
  MOZ_ASSERT(mParent);
}

Result<already_AddRefed<AudioDataResource>, nsresult>
AudioDataResource::Construct(const OwningAllowSharedBufferSource& aInit) {
  FallibleTArray<uint8_t> copied;
  uint8_t* rv = ProcessTypedArraysFixed(
      aInit, [&](const Span<uint8_t>& aData) -> uint8_t* {
        return copied.AppendElements(aData.Elements(), aData.Length(),
                                     fallible);
      });
  if (!rv) {
    LOGE("AudioDataResource::Ctor: OOM");
    return Err(NS_ERROR_OUT_OF_MEMORY);
  }
  return MakeAndAddRef<AudioDataResource>(std::move(copied));
}

AudioData::AudioData(
    nsIGlobalObject* aParent,
    already_AddRefed<mozilla::dom::AudioDataResource> aResource,
    const AudioDataInit& aInit)
    : mParent(aParent),
      mTimestamp(aInit.mTimestamp),
      mNumberOfChannels(aInit.mNumberOfChannels),
      mNumberOfFrames(aInit.mNumberOfFrames),
      mSampleRate(aInit.mSampleRate),
      mAudioSampleFormat(Some(aInit.mFormat)),
      mResource(std::move(aResource)) {
  MOZ_ASSERT(mParent);
}

AudioData::AudioData(
    nsIGlobalObject* aParent,
    already_AddRefed<mozilla::dom::AudioDataResource> aResource,
    int64_t aTimestamp, uint32_t aNumberOfChannels, uint32_t aNumberOfFrames,
    float aSampleRate, AudioSampleFormat aAudioSampleFormat)
    : mParent(aParent),
      mTimestamp(aTimestamp),
      mNumberOfChannels(aNumberOfChannels),
      mNumberOfFrames(aNumberOfFrames),
      mSampleRate(aSampleRate),
      mAudioSampleFormat(Some(aAudioSampleFormat)),
      mResource(aResource) {
  MOZ_ASSERT(mParent);
}

nsIGlobalObject* AudioData::GetParentObject() const {
  AssertIsOnOwningThread();

  return mParent.get();
}

JSObject* AudioData::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();

  return AudioData_Binding::Wrap(aCx, this, aGivenProto);
}

Result<Ok, nsCString> IsValidAudioDataInit(const AudioDataInit& aInit) {
  // The sample rate is an uint32_t within Gecko
  uint32_t integerSampleRate = SaturatingCast<uint32_t>(aInit.mSampleRate);
  if (integerSampleRate == 0) {
    return LogAndReturnErr("sampleRate must be positive");
  }
  if (aInit.mNumberOfFrames == 0) {
    return LogAndReturnErr("mNumberOfFrames must be positive");
  }
  if (aInit.mNumberOfChannels == 0) {
    return LogAndReturnErr("mNumberOfChannels must be positive");
  }

  CheckedInt<uint64_t> bytesNeeded = aInit.mNumberOfFrames;
  bytesNeeded *= aInit.mNumberOfChannels;
  bytesNeeded *= BytesPerSamples(aInit.mFormat);

  if (!bytesNeeded.isValid()) {
    return LogAndReturnErr(
        FMT_STRING("Overflow when computing the number of bytes needed to hold "
                   "audio samples ({}*{}*{})"),
        aInit.mNumberOfFrames, aInit.mNumberOfChannels,
        BytesPerSamples(aInit.mFormat));
  }

  uint64_t arraySizeBytes = ProcessTypedArraysFixed(
      aInit.mData, [&](const Span<uint8_t>& aData) -> uint64_t {
        return aData.LengthBytes();
      });
  if (arraySizeBytes < bytesNeeded.value()) {
    return LogAndReturnErr(
        FMT_STRING("Array of size {} not big enough, should be at least {}"),
        arraySizeBytes, bytesNeeded.value());
  }
  return Ok();
}

/* static */
already_AddRefed<AudioData> AudioData::Constructor(const GlobalObject& aGlobal,
                                                   const AudioDataInit& aInit,
                                                   ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  LOGD("[{}] AudioData(fmt: {}, rate: {}, ch: {}, ts: {})",
       fmt::ptr(global.get()), GetEnumString(aInit.mFormat).get(),
       aInit.mSampleRate, aInit.mNumberOfChannels, aInit.mTimestamp);
  if (!global) {
    LOGE("Global unavailable");
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  nsString errorMessage;
  auto rv = IsValidAudioDataInit(aInit);
  if (rv.isErr()) {
    LOGD("AudioData::Constructor failure (IsValidAudioDataInit)");
    aRv.ThrowTypeError(rv.inspectErr());
    return nullptr;
  }
  auto resource = AudioDataResource::Construct(aInit.mData);
  if (resource.isErr()) {
    LOGD("AudioData::Constructor failure (OOM)");
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  return MakeAndAddRef<mozilla::dom::AudioData>(global, resource.unwrap(),
                                                aInit);
}

// https://w3c.github.io/webcodecs/#dom-audiodata-format
Nullable<mozilla::dom::AudioSampleFormat> AudioData::GetFormat() const {
  AssertIsOnOwningThread();
  return MaybeToNullable(mAudioSampleFormat);
}

// https://w3c.github.io/webcodecs/#dom-audiodata-samplerate
float AudioData::SampleRate() const {
  AssertIsOnOwningThread();
  return mSampleRate;
}

// https://w3c.github.io/webcodecs/#dom-audiodata-numberofframes
uint32_t AudioData::NumberOfFrames() const {
  AssertIsOnOwningThread();
  return mNumberOfFrames;
}

// https://w3c.github.io/webcodecs/#dom-audiodata-numberofchannels
uint32_t AudioData::NumberOfChannels() const {
  AssertIsOnOwningThread();
  return mNumberOfChannels;
}

// https://w3c.github.io/webcodecs/#dom-audiodata-duration
uint64_t AudioData::Duration() const {
  AssertIsOnOwningThread();
  if (!mNumberOfFrames) {
    return 0;
  }
  // The spec isn't clear in which direction to convert to integer.
  // https://github.com/w3c/webcodecs/issues/726
  return static_cast<uint64_t>(
      static_cast<float>(USECS_PER_S * mNumberOfFrames) / mSampleRate);
}

// https://w3c.github.io/webcodecs/#dom-audiodata-timestamp
int64_t AudioData::Timestamp() const {
  AssertIsOnOwningThread();
  return mTimestamp;
}

struct CopyToSpec {
  CopyToSpec(uint32_t aFrameCount, uint32_t aFrameOffset, uint32_t mPlaneIndex,
             AudioSampleFormat aFormat)
      : mFrameCount(aFrameCount),
        mFrameOffset(aFrameOffset),
        mPlaneIndex(mPlaneIndex),
        mFormat(aFormat) {}

  const uint32_t mFrameCount;
  const uint32_t mFrameOffset;
  const uint32_t mPlaneIndex;
  const AudioSampleFormat mFormat;
};

bool IsInterleaved(const AudioSampleFormat& aFormat) {
  switch (aFormat) {
    case AudioSampleFormat::U8:
    case AudioSampleFormat::S16:
    case AudioSampleFormat::S32:
    case AudioSampleFormat::F32:
      return true;
    case AudioSampleFormat::U8_planar:
    case AudioSampleFormat::S16_planar:
    case AudioSampleFormat::S32_planar:
    case AudioSampleFormat::F32_planar:
      return false;
  };
  MOZ_ASSERT_UNREACHABLE("Invalid enum value");
  return false;
}

size_t AudioData::ComputeCopyElementCount(
    const AudioDataCopyToOptions& aOptions, ErrorResult& aRv) {
  // https://w3c.github.io/webcodecs/#compute-copy-element-count
  // 1, 2
  auto destFormat = mAudioSampleFormat;
  if (aOptions.mFormat.WasPassed()) {
    destFormat = OptionalToMaybe(aOptions.mFormat);
  }
  // 3, 4
  MOZ_ASSERT(destFormat.isSome());
  if (IsInterleaved(destFormat.value())) {
    if (aOptions.mPlaneIndex > 0) {
      auto msg = "Interleaved format, but plane index > 0"_ns;
      LOGD("{}", msg.get());
      aRv.ThrowRangeError(msg);
      return 0;
    }
  } else {
    if (aOptions.mPlaneIndex >= mNumberOfChannels) {
      auto msg = nsFmtCString(FMT_STRING("Plane index {} greater or equal "
                                         "than the number of channels {}"),
                              aOptions.mPlaneIndex, mNumberOfChannels);
      LOGD("{}", msg.get());
      aRv.ThrowRangeError(msg);
      return 0;
    }
  }
  // 5 -- conversion between all formats supported
  // 6 -- all planes have the same number of frames, always
  uint64_t frameCount = mNumberOfFrames;
  // 7
  if (aOptions.mFrameOffset >= frameCount) {
    auto msg = nsFmtCString(
        FMT_STRING("Frame offset of {} greater or equal than frame count {}"),
        aOptions.mFrameOffset, frameCount);
    LOGD("{}", msg.get());
    aRv.ThrowRangeError(msg);
    return 0;
  }
  // 8, 9
  uint64_t copyFrameCount = frameCount - aOptions.mFrameOffset;
  if (aOptions.mFrameCount.WasPassed()) {
    if (aOptions.mFrameCount.Value() > copyFrameCount) {
      auto msg = nsFmtCString(FMT_STRING("Passed copy frame count of {} "
                                         "greater than available source frames "
                                         "for copy of {}"),
                              aOptions.mFrameCount.Value(), copyFrameCount);
      LOGD("{}", msg.get());
      aRv.ThrowRangeError(msg);
      return 0;
    }
    copyFrameCount = aOptions.mFrameCount.Value();
  }
  // 10, 11
  uint64_t elementCount = copyFrameCount;
  if (IsInterleaved(destFormat.value())) {
    elementCount *= mNumberOfChannels;
  }
  return elementCount;
}

// https://w3c.github.io/webcodecs/#dom-audiodata-allocationsize
// This method returns an int, that can be zero in case of success or error.
// Caller should check aRv to determine success or error.
uint32_t AudioData::AllocationSize(const AudioDataCopyToOptions& aOptions,
                                   ErrorResult& aRv) {
  AssertIsOnOwningThread();
  if (!mResource) {
    auto msg = "allocationSize called on detached AudioData"_ns;
    LOGD("{}", msg.get());
    aRv.ThrowInvalidStateError(msg);
    return 0;
  }
  size_t copyElementCount = ComputeCopyElementCount(aOptions, aRv);
  if (aRv.Failed()) {
    LOGD("AudioData::AllocationSize failure");
    // ComputeCopyElementCount has set the exception type.
    return 0;
  }
  Maybe<mozilla::dom::AudioSampleFormat> destFormat = mAudioSampleFormat;
  if (aOptions.mFormat.WasPassed()) {
    destFormat = OptionalToMaybe(aOptions.mFormat);
  }
  if (destFormat.isNothing()) {
    auto msg = "AudioData has an unknown format"_ns;
    LOGD("{}", msg.get());
    // See https://github.com/w3c/webcodecs/issues/727 -- it isn't clear yet
    // what to do here
    aRv.ThrowRangeError(msg);
    return 0;
  }
  CheckedInt<size_t> bytesPerSample = BytesPerSamples(destFormat.ref());

  auto res = bytesPerSample * copyElementCount;
  if (res.isValid()) {
    return res.value();
  }
  aRv.ThrowRangeError("Allocation size too large");
  return 0;
}

template <typename S, typename D>
void CopySamples(Span<S> aSource, Span<D> aDest, uint32_t aSourceChannelCount,
                 const AudioSampleFormat aSourceFormat,
                 const CopyToSpec& aCopyToSpec) {
  if (IsInterleaved(aSourceFormat) && IsInterleaved(aCopyToSpec.mFormat)) {
    MOZ_ASSERT(aCopyToSpec.mPlaneIndex == 0);
    MOZ_ASSERT(aDest.Length() >= aCopyToSpec.mFrameCount);
    MOZ_ASSERT(aSource.Length() - aCopyToSpec.mFrameOffset >=
               aCopyToSpec.mFrameCount);
    // This turns into a regular memcpy if the types are in fact equal
    ConvertAudioSamples(aSource.data() + aCopyToSpec.mFrameOffset, aDest.data(),
                        aCopyToSpec.mFrameCount * aSourceChannelCount);
    return;
  }
  if (IsInterleaved(aSourceFormat) && !IsInterleaved(aCopyToSpec.mFormat)) {
    DebugOnly<size_t> sourceFrameCount = aSource.Length() / aSourceChannelCount;
    MOZ_ASSERT(aDest.Length() >= aCopyToSpec.mFrameCount);
    MOZ_ASSERT(aSource.Length() - aCopyToSpec.mFrameOffset >=
               aCopyToSpec.mFrameCount);
    // Interleaved to planar -- only copy samples of the correct channel to the
    // destination
    size_t readIndex = aCopyToSpec.mFrameOffset * aSourceChannelCount +
                       aCopyToSpec.mPlaneIndex;
    for (size_t i = 0; i < aCopyToSpec.mFrameCount; i++) {
      aDest[i] = ConvertAudioSample<D>(aSource[readIndex]);
      readIndex += aSourceChannelCount;
    }
    return;
  }

  if (!IsInterleaved(aSourceFormat) && IsInterleaved(aCopyToSpec.mFormat)) {
    // Planar to interleaved -- copy of all channels of the source into the
    // destination buffer.
    MOZ_ASSERT(aCopyToSpec.mPlaneIndex == 0);
    MOZ_ASSERT(aDest.Length() >= aCopyToSpec.mFrameCount * aSourceChannelCount);
    MOZ_ASSERT(aSource.Length() -
                   aCopyToSpec.mFrameOffset * aSourceChannelCount >=
               aCopyToSpec.mFrameCount * aSourceChannelCount);
    size_t writeIndex = 0;
    // Scan the source linearly and put each sample at the right position in the
    // destination interleaved buffer.
    size_t readIndex = 0;
    for (size_t channel = 0; channel < aSourceChannelCount; channel++) {
      writeIndex = channel;
      for (size_t i = 0; i < aCopyToSpec.mFrameCount; i++) {
        aDest[writeIndex] = ConvertAudioSample<D>(aSource[readIndex]);
        readIndex++;
        writeIndex += aSourceChannelCount;
      }
    }
    return;
  }
  if (!IsInterleaved(aSourceFormat) && !IsInterleaved(aCopyToSpec.mFormat)) {
    // Planar to Planar / convert + copy from the right index in the source.
    size_t framePerPlane = aSource.Length() / aSourceChannelCount;
    size_t offset = aCopyToSpec.mPlaneIndex * framePerPlane;
    MOZ_ASSERT(aDest.Length() >= aCopyToSpec.mFrameCount,
               "Destination buffer too small");
    MOZ_ASSERT(aSource.Length() >= offset + aCopyToSpec.mFrameCount,
               "Source buffer too small");
    for (uint32_t i = 0; i < aCopyToSpec.mFrameCount; i++) {
      aDest[i] =
          ConvertAudioSample<D>(aSource[offset + aCopyToSpec.mFrameOffset + i]);
    }
  }
}

nsCString AudioData::ToString() const {
  if (!mResource) {
    return nsCString("AudioData[detached]");
  }
  return nsFmtCString(FMT_STRING("AudioData[{} bytes {} {}Hz {} x {}ch]"),
                      mResource->Data().LengthBytes(),
                      GetEnumString(mAudioSampleFormat.value()).get(),
                      mSampleRate, mNumberOfFrames, mNumberOfChannels);
}

nsCString CopyToToString(size_t aDestBufSize,
                         const AudioDataCopyToOptions& aOptions) {
  return nsFmtCString(
      FMT_STRING(
          "AudioDataCopyToOptions[data: {} bytes, {}, frame count: {}, frame "
          "offset: {}, plane: {}]"),
      aDestBufSize,
      aOptions.mFormat.WasPassed()
          ? GetEnumString(aOptions.mFormat.Value()).get()
          : "null",
      aOptions.mFrameCount.WasPassed() ? aOptions.mFrameCount.Value() : 0,
      aOptions.mFrameOffset, aOptions.mPlaneIndex);
}

using DataSpanType =
    Variant<Span<uint8_t>, Span<int16_t>, Span<int32_t>, Span<float>>;

DataSpanType GetDataSpan(Span<uint8_t> aSpan, const AudioSampleFormat aFormat) {
  const size_t Length = aSpan.Length() / BytesPerSamples(aFormat);
  // TODO: Check size so Span can be reasonably constructed?
  switch (aFormat) {
    case AudioSampleFormat::U8:
    case AudioSampleFormat::U8_planar:
      return AsVariant(aSpan);
    case AudioSampleFormat::S16:
    case AudioSampleFormat::S16_planar:
      return AsVariant(Span(reinterpret_cast<int16_t*>(aSpan.data()), Length));
    case AudioSampleFormat::S32:
    case AudioSampleFormat::S32_planar:
      return AsVariant(Span(reinterpret_cast<int32_t*>(aSpan.data()), Length));
    case AudioSampleFormat::F32:
    case AudioSampleFormat::F32_planar:
      return AsVariant(Span(reinterpret_cast<float*>(aSpan.data()), Length));
  }
  MOZ_ASSERT_UNREACHABLE("Invalid enum value");
  return AsVariant(aSpan);
}

void CopySamples(DataSpanType& aSource, DataSpanType& aDest,
                 uint32_t aSourceChannelCount,
                 const AudioSampleFormat aSourceFormat,
                 const CopyToSpec& aCopyToSpec) {
  aSource.match([&](auto& src) {
    aDest.match([&](auto& dst) {
      CopySamples(src, dst, aSourceChannelCount, aSourceFormat, aCopyToSpec);
    });
  });
}

void DoCopy(Span<uint8_t> aSource, Span<uint8_t> aDest,
            const uint32_t aSourceChannelCount,
            const AudioSampleFormat aSourceFormat,
            const CopyToSpec& aCopyToSpec) {
  DataSpanType source = GetDataSpan(aSource, aSourceFormat);
  DataSpanType dest = GetDataSpan(aDest, aCopyToSpec.mFormat);
  CopySamples(source, dest, aSourceChannelCount, aSourceFormat, aCopyToSpec);
}

// https://w3c.github.io/webcodecs/#dom-audiodata-copyto
void AudioData::CopyTo(const AllowSharedBufferSource& aDestination,
                       const AudioDataCopyToOptions& aOptions,
                       ErrorResult& aRv) {
  AssertIsOnOwningThread();

  size_t destLength = ProcessTypedArraysFixed(
      aDestination, [&](const Span<uint8_t>& aData) -> size_t {
        return aData.LengthBytes();
      });

  LOGD("AudioData::CopyTo {} -> {}", ToString().get(),
       CopyToToString(destLength, aOptions).get(), 4);

  if (!mResource) {
    auto msg = "copyTo called on closed AudioData"_ns;
    LOGD("{}", msg.get());
    aRv.ThrowInvalidStateError(msg);
    return;
  }

  uint64_t copyElementCount = ComputeCopyElementCount(aOptions, aRv);
  if (aRv.Failed()) {
    LOGD("AudioData::CopyTo failed in ComputeCopyElementCount");
    return;
  }
  auto destFormat = mAudioSampleFormat;
  if (aOptions.mFormat.WasPassed()) {
    destFormat = OptionalToMaybe(aOptions.mFormat);
  }

  uint32_t bytesPerSample = BytesPerSamples(destFormat.value());
  CheckedInt<uint32_t> copyLength = bytesPerSample;
  copyLength *= copyElementCount;
  if (copyLength.value() > destLength) {
    auto msg = nsFmtCString(FMT_STRING("destination buffer of length {} too "
                                       "small for copying {} elements"),
                            destLength, bytesPerSample * copyElementCount);
    LOGD("{}", msg.get());
    aRv.ThrowRangeError(msg);
    return;
  }

  uint32_t framesToCopy = mNumberOfFrames - aOptions.mFrameOffset;
  if (aOptions.mFrameCount.WasPassed()) {
    framesToCopy = aOptions.mFrameCount.Value();
  }

  CopyToSpec copyToSpec(framesToCopy, aOptions.mFrameOffset,
                        aOptions.mPlaneIndex, destFormat.value());

  // Now a couple layers of macros to type the pointers and perform the actual
  // copy.
  ProcessTypedArraysFixed(aDestination, [&](const Span<uint8_t>& aData) {
    DoCopy(mResource->Data(), aData, mNumberOfChannels,
           mAudioSampleFormat.value(), copyToSpec);
  });
}

// https://w3c.github.io/webcodecs/#dom-audiodata-clone
already_AddRefed<AudioData> AudioData::Clone(ErrorResult& aRv) {
  AssertIsOnOwningThread();

  if (!mResource) {
    auto msg = "No media resource in the AudioData now"_ns;
    LOGD("{}", msg.get());
    aRv.ThrowInvalidStateError(msg);
    return nullptr;
  }

  return MakeAndAddRef<AudioData>(*this);
}

// https://w3c.github.io/webcodecs/#close-audiodata
void AudioData::Close() {
  AssertIsOnOwningThread();

  mResource = nullptr;
  mSampleRate = 0;
  mNumberOfFrames = 0;
  mNumberOfChannels = 0;
  mAudioSampleFormat = Nothing();
}

bool AudioData::IsClosed() const { return !mResource; }

// https://w3c.github.io/webcodecs/#ref-for-deserialization-steps%E2%91%A1
/* static */
JSObject* AudioData::ReadStructuredClone(JSContext* aCx,
                                         nsIGlobalObject* aGlobal,
                                         JSStructuredCloneReader* aReader,
                                         const AudioDataSerializedData& aData) {
  JS::Rooted<JS::Value> value(aCx, JS::NullValue());
  // To avoid a rooting hazard error from returning a raw JSObject* before
  // running the RefPtr destructor, RefPtr needs to be destructed before
  // returning the raw JSObject*, which is why the RefPtr<AudioData> is created
  // in the scope below. Otherwise, the static analysis infers the RefPtr cannot
  // be safely destructed while the unrooted return JSObject* is on the stack.
  {
    RefPtr<AudioData> frame = MakeAndAddRef<AudioData>(aGlobal, aData);
    if (!GetOrCreateDOMReflector(aCx, frame, &value) || !value.isObject()) {
      LOGE("GetOrCreateDOMReflect failure");
      return nullptr;
    }
  }
  return value.toObjectOrNull();
}

// https://w3c.github.io/webcodecs/#ref-for-audiodata%E2%91%A2%E2%91%A2
bool AudioData::WriteStructuredClone(JSStructuredCloneWriter* aWriter,
                                     StructuredCloneHolder* aHolder) const {
  AssertIsOnOwningThread();

  // AudioData closed
  if (!mResource) {
    LOGD("AudioData was already close in WriteStructuredClone");
    return false;
  }
  const uint32_t index = aHolder->AudioData().Length();
  // https://github.com/w3c/webcodecs/issues/717
  // For now, serialization is only allowed in the same address space, it's OK
  // to send a refptr here instead of copying the backing buffer.
  aHolder->AudioData().AppendElement(AudioDataSerializedData(*this));

  return !NS_WARN_IF(!JS_WriteUint32Pair(aWriter, SCTAG_DOM_AUDIODATA, index));
}

// https://w3c.github.io/webcodecs/#ref-for-transfer-steps
UniquePtr<AudioData::TransferredData> AudioData::Transfer() {
  AssertIsOnOwningThread();

  if (!mResource) {
    // Closed
    LOGD("AudioData was already close in Transfer");
    return nullptr;
  }

  // This adds a ref to the resource
  auto serialized = MakeUnique<AudioDataSerializedData>(*this);
  // This removes the ref to the resource, effectively transfering the backing
  // storage.
  Close();
  return serialized;
}

// https://w3c.github.io/webcodecs/#ref-for-transfer-receiving-steps
/* static */
already_AddRefed<AudioData> AudioData::FromTransferred(nsIGlobalObject* aGlobal,
                                                       TransferredData* aData) {
  MOZ_ASSERT(aData);

  return MakeAndAddRef<AudioData>(aGlobal, *aData);
}

void AudioData::CloseIfNeeded() {
  if (mResource) {
    mResource = nullptr;
  }
}

RefPtr<mozilla::AudioData> AudioData::ToAudioData() const {
  // Always convert to f32 interleaved for now, as this Gecko's prefered
  // internal audio representation for encoding and decoding.
  // mResource can be bigger than needed.
  Span<uint8_t> data = mResource->Data();
  CheckedUint64 sampleCount = mNumberOfFrames;
  sampleCount *= mNumberOfChannels;
  if (!sampleCount.isValid()) {
    LOGE("Overflow AudioData::ToAudioData when computing the number of frames");
    return nullptr;
  }
  AlignedAudioBuffer buf(sampleCount.value());
  if (!buf.Length()) {
    LOGE("OOM when allocating storage for AudioData conversion");
    return nullptr;
  }
  Span<uint8_t> storage(reinterpret_cast<uint8_t*>(buf.Data()), buf.Size());

  CopyToSpec spec(mNumberOfFrames, 0, 0, AudioSampleFormat::F32);

  DoCopy(data, storage, mNumberOfChannels, mAudioSampleFormat.value(), spec);

  return MakeRefPtr<mozilla::AudioData>(
      0, media::TimeUnit::FromMicroseconds(mTimestamp), std::move(buf),
      mNumberOfChannels, SaturatingCast<uint32_t>(mSampleRate));
}

#undef LOGD
#undef LOGE

}  // namespace mozilla::dom
