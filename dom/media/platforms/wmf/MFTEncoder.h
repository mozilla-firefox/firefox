/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_PLATFORM_WMF_MFTENCODER_H
#define DOM_MEDIA_PLATFORM_WMF_MFTENCODER_H

#include <wrl.h>

#include <deque>

#include "EncoderConfig.h"
#include "WMF.h"
#include "mozilla/DataMutex.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/EnumSet.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsDeque.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

namespace mozilla {

class MFTEventSource;

class MFTEncoder final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MFTEncoder)

  struct InputSample {
    RefPtr<IMFSample> mSample{};
    bool mKeyFrameRequested = false;
  };
  using MPEGHeader = nsTArray<UINT8>;
  struct OutputSample {
    RefPtr<IMFSample> mSample{};
    MPEGHeader mHeader;
  };

  using EncodedData = nsTArray<OutputSample>;
  using EncodePromise =
      MozPromise<EncodedData, MediaResult, /* IsExclusive = */ true>;

  enum class HWPreference {
    HardwareOnly,
    SoftwareOnly,
    PreferHardware,
    PreferSoftware
  };
  explicit MFTEncoder(const HWPreference aHWPreference)
      : mHWPreference(aHWPreference) {}

  HRESULT Create(const GUID& aSubtype, const gfx::IntSize& aFrameSize,
                 const EncoderConfig::CodecSpecific& aCodecSpecific);
  HRESULT Destroy();
  HRESULT SetMediaTypes(IMFMediaType* aInputType, IMFMediaType* aOutputType);
  HRESULT SetModes(const EncoderConfig& aConfig);
  HRESULT SetBitrate(UINT32 aBitsPerSec);
  bool IsHardwareAccelerated() const;

  RefPtr<EncodePromise> Encode(nsTArray<InputSample>&& aInputs);
  RefPtr<EncodePromise> Drain();

  HRESULT CreateInputSample(RefPtr<IMFSample>* aSample, size_t aSize);

  Result<MPEGHeader, HRESULT> GetMPEGSequenceHeader();

  static nsCString GetFriendlyName(const GUID& aSubtype);

  static void ClearCache();

  struct Info final {
    GUID mSubtype;
    nsCString mName;
  };
  struct Factory {
    MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(
        Provider, (HW_AMD, HW_Intel, HW_NVIDIA, HW_Qualcomm, HW_Unknown, SW))

    Provider mProvider;
    Microsoft::WRL::ComPtr<IMFActivate> mActivate;
    nsCString mName;

    Factory(Provider aProvider,
            Microsoft::WRL::ComPtr<IMFActivate>&& aActivate);
    Factory(Factory&& aOther) = default;
    Factory(const Factory& aOther) = delete;
    ~Factory();

    explicit operator bool() const { return mActivate; }

    HRESULT Shutdown();
  };

 private:
  friend class MFTEventSource;

  ~MFTEncoder() { Destroy(); };

  static inline StaticMutex sInfoMutex;
  static inline StaticAutoPtr<nsTArray<Info>> sInfos MOZ_GUARDED_BY(sInfoMutex);

  static nsTArray<Info>* Infos() MOZ_REQUIRES(sInfoMutex);
  static nsTArray<Info> Enumerate();
  static Maybe<Info> GetInfo(const GUID& aSubtype);

  // APIs for synchronous processing model.
  Result<EncodedData, MediaResult> EncodeSync(nsTArray<InputSample>&& aInputs);
  Result<EncodedData, MediaResult> DrainSync();
  Result<EncodedData, HRESULT> PullOutputs();

  // APIs for asynchronous processing model for regular usage.
  Result<EncodedData, MediaResult> EncodeAsync(nsTArray<InputSample>&& aInputs);
  Result<EncodedData, MediaResult> DrainAsync();

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(
      ProcessedResult, (AllAvailableInputsProcessed, InputProcessed,
                        OutputHeaderYielded, OutputDataYielded, DrainComplete));
  using ProcessedResults = EnumSet<ProcessedResult>;
  Result<ProcessedResults, HRESULT> ProcessPendingEvents();
  Result<MediaEventType, HRESULT> GetPendingEvent();

  // For realtime usage in asynchronous processing model only.
  RefPtr<EncodePromise> EncodeWithAsyncCallback(
      nsTArray<InputSample>&& aInputs);
  RefPtr<EncodePromise> DrainWithAsyncCallback();
  RefPtr<EncodePromise> PrepareForDrain();
  RefPtr<EncodePromise> StartDraining();
  bool MaybeArmTimer();
  void EventHandler(MediaEventType aEventType, HRESULT aStatus);
  void MaybeResolveOrRejectEncodePromise(bool aResolveAll = false);
  void MaybeResolveOrRejectDrainPromise();
  void MaybeResolveOrRejectPreDrainPromise();
  void MaybeResolveOrRejectAnyPendingPromise(
      const MediaResult& aResult = NS_OK);

  // APIs for asynchronous processing model regardless of usages.
  Result<ProcessedResult, HRESULT> ProcessEvent(MediaEventType aType);
  Result<ProcessedResult, HRESULT> ProcessInput();
  Result<ProcessedResult, HRESULT> ProcessOutput();
  Result<ProcessedResult, HRESULT> ProcessDrainComplete();
  Result<ProcessedResult, HRESULT> ProcessPendingInputs();

  // Utilities for both processing models.
  class OutputResult {
   public:
    explicit OutputResult(already_AddRefed<IMFSample> aSample)
        : mSample(aSample), mHeader() {}
    explicit OutputResult(MPEGHeader&& aHeader)
        : mSample(nullptr), mHeader(std::move(aHeader)) {}
    bool IsSample() const { return mSample != nullptr; }
    bool IsHeader() const { return !IsSample(); }
    already_AddRefed<IMFSample> TakeSample() {
      MOZ_ASSERT(IsSample());
      return mSample.forget();
    }
    MPEGHeader TakeHeader() {
      MOZ_ASSERT(IsHeader());
      return std::move(mHeader);
    }

   private:
    RefPtr<IMFSample> mSample;
    MPEGHeader mHeader;
  };
  Result<OutputResult, HRESULT> GetOutputOrNewHeader();
  // Set the output type to the first available type found for the output
  // stream.
  HRESULT UpdateOutputType();
  HRESULT ProcessOutput(RefPtr<IMFSample>& aSample, DWORD& aOutputStatus,
                        DWORD& aBufferStatus);
  HRESULT ProcessInput(InputSample&& aInput);

  bool IsAsync() const { return mAsyncEventSource; }

  // Return true when successfully enabled, false for MFT that doesn't support
  // async processing model, and error otherwise.
  using AsyncMFTResult = Result<bool, HRESULT>;
  AsyncMFTResult AttemptEnableAsync();
  HRESULT GetStreamIDs();
  GUID MatchInputSubtype(IMFMediaType* aInputType);
  HRESULT SendMFTMessage(MFT_MESSAGE_TYPE aMsg, ULONG_PTR aData);

  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(
      State,
      (Uninited, Initializing, Inited, Encoding, PreDraining, Draining, Error));
  void SetState(State aState);

  const HWPreference mHWPreference;
  RefPtr<IMFTransform> mEncoder;
  // For MFT object creation. See
  // https://docs.microsoft.com/en-us/windows/win32/medfound/activation-objects
  Maybe<Factory> mFactory;
  // For encoder configuration. See
  // https://docs.microsoft.com/en-us/windows/win32/directshow/encoder-api
  RefPtr<ICodecAPI> mConfig;

  DWORD mInputStreamID;
  DWORD mOutputStreamID;
  MFT_INPUT_STREAM_INFO mInputStreamInfo;
  MFT_OUTPUT_STREAM_INFO mOutputStreamInfo;
  bool mOutputStreamProvidesSample;

  State mState = State::Uninited;
  bool mIsRealtime = false;

  // The following members are used only for asynchronous processing model
  size_t mNumNeedInput;
  std::deque<InputSample> mPendingInputs;

  nsTArray<OutputSample> mOutputs;
  // Holds a temporary MPEGSequenceHeader to be attached to the first output
  // packet after format renegotiation.
  MPEGHeader mOutputHeader;

  RefPtr<MFTEventSource> mAsyncEventSource;

  // The following members are used only for realtime asynchronous processing
  // model.
  std::deque<RefPtr<EncodePromise::Private>> mEncodePromises;
  MediaResult mPendingError;
  MozPromiseHolder<EncodePromise> mDrainPromise;
  MozPromiseHolder<EncodePromise> mPreDrainPromise;
  // Use to resolve the encode promise if mAsyncEventSource doesn't response in
  // time.
  nsCOMPtr<nsITimer> mTimer;
};

class MFTEventSource final : public IMFAsyncCallback {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MFTEventSource)

  // A basic IMFMediaEventGenerator wrapper that does not support retrieving
  // events from asynchronous callbacks when constructed this way. Events should
  // instead be obtained by calling GetEvent().
  explicit MFTEventSource(
      already_AddRefed<IMFMediaEventGenerator> aEventGenerator)
      : MFTEventSource(GetCurrentSerialEventTarget(), nullptr,
                       std::move(aEventGenerator)) {}
  // This constructor creates an MFTEventSource that forwards events from
  // asynchronous callbacks directly to the MFTEncoder's event handler. In this
  // usage, GetEvent() should not be called, as events are handled
  // automatically.
  MFTEventSource(MFTEncoder* aEncoder,
                 already_AddRefed<IMFMediaEventGenerator> aEventGenerator)
      : MFTEventSource(GetCurrentSerialEventTarget(), aEncoder,
                       std::move(aEventGenerator)) {}

  bool CanForwardEvents() const { return mEncoder; }
  Result<MediaEventType, HRESULT> GetEvent(DWORD aFlags);

  HRESULT BeginEventListening();

  // IMFAsyncCallback implementations:
  STDMETHODIMP GetParameters(DWORD* aFlags, DWORD* aQueue) override;
  // Invoke() can be called on any thread by the OS, but it will forward the
  // event to the MFTEncoder's working thread.
  STDMETHODIMP Invoke(IMFAsyncResult* aAsyncResult) override;
  STDMETHODIMP QueryInterface(REFIID aIID, void** aPPV) override;
  // AddRef() and Release() are implemented by
  // NS_INLINE_DECL_THREADSAFE_REFCOUNTING.

  using Id = size_t;
  const Id mId;

 private:
  MFTEventSource(nsISerialEventTarget* aEncoderThread, MFTEncoder* aEncoder,
                 already_AddRefed<IMFMediaEventGenerator> aEventGenerator);
  ~MFTEventSource();

  static Id GenerateId() {
    static Id sNextId = 0;
    return sNextId++;
  }

  // The following members are used to forwards events from any OS thread to the
  // MFTEncoder's working thread.
  const nsCOMPtr<nsISerialEventTarget> mEncoderThread;
  const RefPtr<MFTEncoder> mEncoder;
  // When acting as a simple wrapper for IMFMediaEventGenerator, mEventGenerator
  // is always accessed from a single thread, making locking effectively
  // cost-free. In scenarios where MFTEventSource forwards events to MFTEncoder,
  // mEventGenerator will be accessed from multiple threads: event requests are
  // made on the MFTEncoder's working thread (via BeginEventListening()), while
  // event delivery occurs on the OS thread (via Invoke()). Since these
  // operations do not happen concurrently, the overhead of DataMutex locking is
  // negligible. DataMutex is used here to clarify that event requests and
  // deliveries are performed on separate threads. Furthermore, because
  // MFTEncoder might release MFTEventSource while waiting for an event—and the
  // Windows Media Foundation documentation does not specify whether releasing
  // IMFMediaEventGenerator cancels pending event waits—we release
  // mEventGenerator in the MFTEventSource destructor to ensure all pending
  // events are properly handled, rather than resetting it when MFTEncoder
  // releases MFTEventSource.
  DataMutex<RefPtr<IMFMediaEventGenerator>> mEventGenerator;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_PLATFORM_WMF_MFTENCODER_H
