/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DecoderTemplate_h
#define mozilla_dom_DecoderTemplate_h

#include <queue>

#include "SimpleMap.h"
#include "WebCodecsUtils.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/DecoderAgent.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/media/MediaUtils.h"
#include "nsStringFwd.h"

namespace mozilla {

class TrackInfo;

namespace dom {

class WebCodecsErrorCallback;
class Promise;
enum class CodecState : uint8_t;

template <typename DecoderType>
class DecoderTemplate : public DOMEventTargetHelper {
  using Self = DecoderTemplate<DecoderType>;
  using ConfigType = typename DecoderType::ConfigType;
  using ConfigTypeInternal = typename DecoderType::ConfigTypeInternal;
  using InputType = typename DecoderType::InputType;
  using InputTypeInternal = typename DecoderType::InputTypeInternal;
  using OutputType = typename DecoderType::OutputType;
  using OutputCallbackType = typename DecoderType::OutputCallbackType;

  /* ControlMessage classes */
 protected:
  class ConfigureMessage;
  class DecodeMessage;
  class FlushMessage;

  class ControlMessage {
   public:
    ControlMessage(WebCodecsId aConfigId) : mConfigId(aConfigId) {};
    virtual ~ControlMessage() = default;
    virtual void Cancel() = 0;
    virtual bool IsProcessing() = 0;

    virtual nsCString ToString() const = 0;
    virtual ConfigureMessage* AsConfigureMessage() { return nullptr; }
    virtual DecodeMessage* AsDecodeMessage() { return nullptr; }
    virtual FlushMessage* AsFlushMessage() { return nullptr; }

    // All the control message follows a specific configuration. Knowing the
    // associated configuration of a decode/flush request is helpful in
    // debugging logs. This id is used to identify which ConfigureMessage the
    // request follows.
    const WebCodecsId mConfigId;
  };

  class ConfigureMessage final
      : public ControlMessage,
        public MessageRequestHolder<DecoderAgent::ConfigurePromise> {
   public:
    static ConfigureMessage* Create(
        already_AddRefed<ConfigTypeInternal> aConfig);

    ~ConfigureMessage() = default;
    virtual void Cancel() override { Disconnect(); }
    virtual bool IsProcessing() override { return Exists(); };
    virtual nsCString ToString() const override;
    virtual ConfigureMessage* AsConfigureMessage() override { return this; }
    const ConfigTypeInternal& Config() { return *mConfig; }
    already_AddRefed<ConfigTypeInternal> TakeConfig() {
      return mConfig.forget();
    }

   private:
    ConfigureMessage(WebCodecsId aConfigId,
                     already_AddRefed<ConfigTypeInternal> aConfig);

    RefPtr<ConfigTypeInternal> mConfig;
    const nsCString mCodec;
  };

  class DecodeMessage final
      : public ControlMessage,
        public MessageRequestHolder<DecoderAgent::DecodePromise> {
   public:
    DecodeMessage(WebCodecsId aSeqId, WebCodecsId aConfigId,
                  UniquePtr<InputTypeInternal>&& aData);
    ~DecodeMessage() = default;
    virtual void Cancel() override { Disconnect(); }
    virtual bool IsProcessing() override { return Exists(); };
    virtual nsCString ToString() const override;
    virtual DecodeMessage* AsDecodeMessage() override { return this; }

    // The sequence id of a decode request associated with a specific
    // configuration.
    const WebCodecsId mSeqId;
    UniquePtr<InputTypeInternal> mData;
  };

  class FlushMessage final
      : public ControlMessage,
        public MessageRequestHolder<DecoderAgent::DecodePromise> {
   public:
    FlushMessage(WebCodecsId aSeqId, WebCodecsId aConfigId);
    ~FlushMessage() = default;
    virtual void Cancel() override { Disconnect(); }
    virtual bool IsProcessing() override { return Exists(); };
    virtual nsCString ToString() const override;
    virtual FlushMessage* AsFlushMessage() override { return this; }

    // The sequence id of a flush request associated with a specific
    // configuration.
    const WebCodecsId mSeqId;
    const int64_t mUniqueId;
  };

 protected:
  DecoderTemplate(nsIGlobalObject* aGlobalObject,
                  RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
                  RefPtr<OutputCallbackType>&& aOutputCallback);

  virtual ~DecoderTemplate() = default;

  /* WebCodecs interfaces */
 public:
  IMPL_EVENT_HANDLER(dequeue)

  CodecState State() const { return mState; };

  uint32_t DecodeQueueSize() const { return mDecodeQueueSize; };

  void Configure(const ConfigType& aConfig, ErrorResult& aRv);

  void Decode(InputType& aInput, ErrorResult& aRv);

  already_AddRefed<Promise> Flush(ErrorResult& aRv);

  void Reset(ErrorResult& aRv);

  void Close(ErrorResult& aRv);

  /* Type conversion functions for the Decoder implementation */
 protected:
  virtual already_AddRefed<MediaRawData> InputDataToMediaRawData(
      UniquePtr<InputTypeInternal>&& aData, TrackInfo& aInfo,
      const ConfigTypeInternal& aConfig) = 0;
  virtual nsTArray<RefPtr<OutputType>> DecodedDataToOutputType(
      nsIGlobalObject* aGlobalObject, const nsTArray<RefPtr<MediaData>>&& aData,
      const ConfigTypeInternal& aConfig) = 0;

 protected:
  // DecoderTemplate can run on either main thread or worker thread.
  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(DecoderTemplate);
  }

  Result<Ok, nsresult> ResetInternal(const nsresult& aResult);
  // Calling this method calls the error callback synchronously.
  MOZ_CAN_RUN_SCRIPT
  void CloseInternal(const nsresult& aResult);
  // Calling this method doesn't call the error calback.
  Result<Ok, nsresult> CloseInternalWithAbort();

  MOZ_CAN_RUN_SCRIPT void ReportError(const nsresult& aResult);
  MOZ_CAN_RUN_SCRIPT void OutputDecodedData(
      const nsTArray<RefPtr<MediaData>>&& aData,
      const ConfigTypeInternal& aConfig);

  void ScheduleDequeueEventIfNeeded();
  nsresult FireEvent(nsAtom* aTypeWithOn, const nsAString& aEventType);

  void ProcessControlMessageQueue();
  void CancelPendingControlMessagesAndFlushPromises(const nsresult& aResult);

  // Queue a task to the control thread. This is to be used when a task needs to
  // perform multiple steps.
  template <typename Func>
  void QueueATask(const char* aName, Func&& aSteps);

  MessageProcessedResult ProcessConfigureMessage(
      UniquePtr<ControlMessage>& aMessage);

  MessageProcessedResult ProcessDecodeMessage(
      UniquePtr<ControlMessage>& aMessage);

  MessageProcessedResult ProcessFlushMessage(
      UniquePtr<ControlMessage>& aMessage);

  // Returns true when mAgent can be created.
  bool CreateDecoderAgent(DecoderAgent::Id aId,
                          RefPtr<ConfigTypeInternal>&& aConfig,
                          UniquePtr<TrackInfo>&& aInfo);
  void DestroyDecoderAgentIfAny();

  // Constant in practice, only set in ctor.
  RefPtr<WebCodecsErrorCallback> mErrorCallback;
  RefPtr<OutputCallbackType> mOutputCallback;

  CodecState mState;
  bool mKeyChunkRequired;

  bool mMessageQueueBlocked;
  std::queue<UniquePtr<ControlMessage>> mControlMessageQueue;
  UniquePtr<ControlMessage> mProcessingMessage;

  // When a flush request is initiated, a promise is created and stored in
  // mPendingFlushPromises until it is settled in the task delivering the flush
  // result or Reset() is called before the promise is settled.
  SimpleMap<int64_t, RefPtr<Promise>> mPendingFlushPromises;

  uint32_t mDecodeQueueSize;
  bool mDequeueEventScheduled;

  // A unique id tracking the ConfigureMessage and will be used as the
  // DecoderAgent's Id.
  uint32_t mLatestConfigureId;
  // Tracking how many decode data has been enqueued and this number will be
  // used as the DecodeMessage's sequence Id.
  size_t mDecodeCounter;
  // Tracking how many flush request has been enqueued and this number will be
  // used as the FlushMessage's sequence Id.
  size_t mFlushCounter;

  // DecoderAgent will be created every time "configure" is being processed, and
  // will be destroyed when "reset" or another "configure" is called (spec
  // allows calling two "configure" without a "reset" in between).
  RefPtr<DecoderAgent> mAgent;
  RefPtr<ConfigTypeInternal> mActiveConfig;

  // Used to add a nsIAsyncShutdownBlocker on main thread to block
  // xpcom-shutdown before the underlying MediaDataDecoder is created. The
  // blocker will be held until the underlying MediaDataDecoder has been shut
  // down. This blocker guarantees RemoteDecoderManagerChild's thread, where the
  // underlying RemoteMediaDataDecoder is on, outlives the
  // RemoteMediaDataDecoder, since the thread releasing, which happens on main
  // thread when getting a xpcom-shutdown signal, is blocked by the added
  // blocker. As a result, RemoteMediaDataDecoder can safely work on worker
  // thread with a holding blocker (otherwise, if RemoteDecoderManagerChild
  // releases its thread on main thread before RemoteMediaDataDecoder's
  // Shutdown() task run on worker thread, RemoteMediaDataDecoder has no thread
  // to run).
  UniquePtr<media::ShutdownBlockingTicket> mShutdownBlocker;

  // Held to make sure the dispatched tasks can be done before worker is going
  // away. As long as this worker-ref is held somewhere, the tasks dispatched to
  // the worker can be executed (otherwise the tasks would be canceled). This
  // ref should be activated as long as the underlying MediaDataDecoder is
  // alive, and should keep alive until mShutdownBlocker is dropped, so all
  // MediaDataDecoder's tasks and mShutdownBlocker-releasing task can be
  // executed.
  // TODO: Use StrongWorkerRef instead if this is always used in the same
  // thread?
  RefPtr<ThreadSafeWorkerRef> mWorkerRef;

  AsyncDurationTracker mAsyncDurationTracker;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_DecoderTemplate_h
