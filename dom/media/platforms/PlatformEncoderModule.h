/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(PlatformEncoderModule_h_)
#  define PlatformEncoderModule_h_

#  include "EncoderConfig.h"
#  include "MP4Decoder.h"
#  include "MediaCodecsSupport.h"
#  include "MediaResult.h"
#  include "VPXDecoder.h"
#  include "VideoUtils.h"
#  include "mozilla/Maybe.h"
#  include "mozilla/MozPromise.h"
#  include "mozilla/RefPtr.h"
#  include "mozilla/TaskQueue.h"
#  include "mozilla/dom/ImageBitmapBinding.h"
#  include "nsISupportsImpl.h"

namespace mozilla {

class MediaDataEncoder;
class MediaData;
class EncoderConfigurationChangeList;

class PlatformEncoderModule {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PlatformEncoderModule)

  virtual already_AddRefed<MediaDataEncoder> CreateVideoEncoder(
      const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
    return nullptr;
  };

  virtual already_AddRefed<MediaDataEncoder> CreateAudioEncoder(
      const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
    return nullptr;
  };

  using CreateEncoderPromise = MozPromise<RefPtr<MediaDataEncoder>, MediaResult,
                                          /* IsExclusive = */ true>;

  // Indicates if the PlatformDecoderModule supports encoding of a codec.
  virtual media::EncodeSupportSet Supports(
      const EncoderConfig& aConfig) const = 0;
  virtual media::EncodeSupportSet SupportsCodec(CodecType aCodecType) const = 0;

  using SupportsEncoderPromise =
      MozPromise<media::EncodeSupportSet, nsresult, /* IsExclusive = */ true>;

  // Asynchronous variant of Supports(). The default resolves with the
  // synchronous result; modules that proxy to a remote process override this
  // to first wait until that process has reported its codec support, so the
  // answer reflects accurate hardware capabilities. Must be called off the
  // main thread.
  virtual RefPtr<SupportsEncoderPromise> SupportsAsync(
      const EncoderConfig& aConfig) const {
    return SupportsEncoderPromise::CreateAndResolve(Supports(aConfig),
                                                    __func__);
  }

  // Returns a readable name for this Platform Encoder Module
  virtual const char* GetName() const = 0;

  // Asychronously create an encoder
  virtual RefPtr<PlatformEncoderModule::CreateEncoderPromise>
  AsyncCreateEncoder(const EncoderConfig& aEncoderConfig,
                     const RefPtr<TaskQueue>& aTaskQueue);

 protected:
  PlatformEncoderModule() = default;
  virtual ~PlatformEncoderModule() = default;
};

class MediaDataEncoder {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  using InitPromise = MozPromise<bool, MediaResult, /* IsExclusive = */ true>;
  using EncodedData = nsTArray<RefPtr<MediaRawData>>;
  using EncodePromise =
      MozPromise<EncodedData, MediaResult, /* IsExclusive = */ true>;
  using ReconfigurationPromise =
      MozPromise<bool, MediaResult, /* IsExclusive = */ true>;

  // Initialize the encoder. It should be ready to encode once the returned
  // promise resolves. The encoder should do any initialization here, rather
  // than in its constructor or PlatformEncoderModule::Create*Encoder(),
  // so that if the client needs to shutdown during initialization,
  // it can call Shutdown() to cancel this operation. Any initialization
  // that requires blocking the calling thread in this function *must*
  // be done here so that it can be canceled by calling Shutdown()!
  virtual RefPtr<InitPromise> Init() = 0;

  // Inserts a sample into the encoder's encode pipeline. The EncodePromise it
  // returns will be resolved with already encoded MediaRawData at the moment,
  // or empty when there is none available yet.
  virtual RefPtr<EncodePromise> Encode(const MediaData* aSample) = 0;

  // Inserts a batch of samples into the encoder's encode pipeline. The
  // EncodePromise it returns will be resolved with already encoded MediaRawData
  // at the moment, or empty when there is none available yet.
  virtual RefPtr<EncodePromise> Encode(nsTArray<RefPtr<MediaData>>&& aSamples) {
    MOZ_ASSERT_UNREACHABLE("Encode samples in a batch is not implemented");
    return EncodePromise::CreateAndReject(
        MediaResult(NS_ERROR_NOT_IMPLEMENTED,
                    "Encode samples in a batch is not implemented"),
        __func__);
  }

  // Attempt to reconfigure the encoder on the fly. This can fail if the
  // underlying PEM doesn't support this type of reconfiguration.
  virtual RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>&
          aConfigurationChanges) = 0;

  // Causes all complete samples in the pipeline that can be encoded to be
  // output. It indicates that there is no more input sample to insert.
  // This function is asynchronous.
  // The MediaDataEncoder shall resolve the pending EncodePromise with drained
  // samples. Drain will be called multiple times until the resolved
  // EncodePromise is empty which indicates that there are no more samples to
  // drain.
  virtual RefPtr<EncodePromise> Drain() = 0;

  // Cancels all init/encode/drain operations, and shuts down the encoder. The
  // platform encoder should clean up any resources it's using and release
  // memory etc. The shutdown promise will be resolved once the encoder has
  // completed shutdown. The client will delete the decoder once the promise is
  // resolved.
  // The ShutdownPromise must only ever be resolved.
  virtual RefPtr<ShutdownPromise> Shutdown() = 0;

  virtual RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) {
    return GenericPromise::CreateAndResolve(true, __func__);
  }

  // Decoder needs to decide whether or not hardware acceleration is supported
  // after creating. It doesn't need to call Init() before calling this
  // function.
  virtual bool IsHardwareAccelerated(nsACString& aFailureReason) const {
    return false;
  }

  // Return the name of the MediaDataEncoder, only used for encoding.
  // May be accessed in a non thread-safe fashion.
  virtual nsCString GetDescriptionName() const = 0;

  friend class PlatformEncoderModule;

 protected:
  virtual ~MediaDataEncoder() = default;
};

// Wrap a type to make it unique. This allows using ergonomically in the Variant
// below. Simply aliasing with `using` isn't enough, because typedefs in C++
// don't produce strong types, so two integer variants result in
// the same type, making it ambiguous to the Variant code.
// T is the type to be wrapped. Phantom is a type that is only used to
// disambiguate and should be unique in the program.
template <typename T, typename Phantom>
class StrongTypedef {
 public:
  StrongTypedef() = default;
  explicit StrongTypedef(T const& value) : mValue(value) {}
  explicit StrongTypedef(T&& value) : mValue(std::move(value)) {}
  T& get() { return mValue; }
  T const& get() const { return mValue; }

  auto MutTiedFields() { return std::tie(mValue); }

 private:
  T mValue{};

  friend struct IPC::ParamTraits<StrongTypedef<T, Phantom>>;
};

// Dimensions of the video frames
using DimensionsChange =
    StrongTypedef<gfx::IntSize, struct DimensionsChangeType>;
// Expected display size of the encoded frames, can influence encoding
using DisplayDimensionsChange =
    StrongTypedef<Maybe<gfx::IntSize>, struct DisplayDimensionsChangeType>;
// If present, the bitrate in kbps of the encoded stream. If absent, let the
// platform decide.
using BitrateChange = StrongTypedef<Maybe<uint32_t>, struct BitrateChangeType>;
// If present, the expected framerate of the output video stream. If absent,
// infer from the input frames timestamp.
using FramerateChange =
    StrongTypedef<Maybe<double>, struct FramerateChangeType>;
// The bitrate mode (variable, constant) of the encoding
using BitrateModeChange =
    StrongTypedef<BitrateMode, struct BitrateModeChangeType>;
// The usage for the encoded stream, this influence latency, ordering, etc.
using UsageChange = StrongTypedef<Usage, struct UsageChangeType>;
// If present, the expected content of the video frames (screen, movie, etc.).
// The value the string can have isn't decided just yet. When absent, the
// encoder uses generic settings.
using ContentHintChange =
    StrongTypedef<Maybe<nsString>, struct ContentHintTypeType>;
// If present, the new sample-rate of the audio
using SampleRateChange = StrongTypedef<uint32_t, struct SampleRateChangeType>;
// If present, the new sample-rate of the audio
using NumberOfChannelsChange =
    StrongTypedef<uint32_t, struct NumberOfChannelsChangeType>;

// A change to a parameter of an encoder instance.
using EncoderConfigurationItem =
    Variant<DimensionsChange, DisplayDimensionsChange, BitrateModeChange,
            BitrateChange, FramerateChange, UsageChange, ContentHintChange,
            SampleRateChange, NumberOfChannelsChange>;

// A list of changes to an encoder configuration, that _might_ be able to change
// on the fly. Not all encoder modules can adjust their configuration on the
// fly.
class EncoderConfigurationChangeList {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(EncoderConfigurationChangeList)
  bool Empty() const { return mChanges.IsEmpty(); }
  template <typename T>
  void Push(const T& aItem) {
    mChanges.AppendElement(aItem);
  }
  nsString ToString() const;

  nsTArray<EncoderConfigurationItem> mChanges;

 private:
  ~EncoderConfigurationChangeList() = default;
};

// Just by inspecting the configuration and before asking the PEM, it's
// sometimes possible to know that a media won't be able to be encoded. For
// example, VP8 encodes the frame size on 14 bits, so a resolution of more than
// 16383x16383 pixels cannot work.
bool CanLikelyEncode(const EncoderConfig& aConfig);

}  // namespace mozilla

#endif /* PlatformEncoderModule_h_ */
