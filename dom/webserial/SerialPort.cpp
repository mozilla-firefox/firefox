/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SerialPort.h"

#include "SerialLogging.h"
#include "SerialPortPumps.h"
#include "SerialPortStreamAlgorithms.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/Serial.h"
#include "mozilla/dom/SerialPortBinding.h"
#include "mozilla/dom/SerialPortChild.h"
#include "mozilla/dom/SerialPortIPCTypes.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/UnderlyingSourceCallbackHelpers.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WritableStream.h"
#include "mozilla/ipc/DataPipe.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(SerialPort)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SerialPort,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSerial)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReadable)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWritable)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChild)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOpenPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mClosePromise)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SerialPort,
                                                DOMEventTargetHelper)
  tmp->Shutdown();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSerial)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReadable)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWritable)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mChild)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOpenPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mClosePromise)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(SerialPort, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(SerialPort, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SerialPort)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

SerialPort::SerialPort(const IPCSerialPortInfo& aInfo, Serial* aSerial)
    : DOMEventTargetHelper(aSerial->GetRelevantGlobal()),
      mSerial(aSerial),
      mInfo(aInfo) {
  nsPIDOMWindowInner* window = aSerial->GetOwnerWindow();
  if (window) {
    if (Document* doc = window->GetExtantDoc()) {
      // Disallow putting this page in the bfcache to ensure that
      // when we navigate away the OS resources associated with this
      // SerialPort get properly cleaned up.
      doc->DisallowBFCaching();
    }
  }
  MOZ_LOG(
      gWebSerialLog, LogLevel::Info,
      ("SerialPort[%p] created for port '%s' (%s)", this,
       NS_ConvertUTF16toUTF8(mInfo.id()).get(), window ? "window" : "worker"));
}

void SerialPort::UpdateWorkerRef() {
  if (NS_IsMainThread()) {
    return;
  }

  bool needsRef = false;
  if (!mHasShutdown && mState != State::Forgetting &&
      mState != State::Forgotten) {
    EventListenerManager* elm = GetExistingListenerManager();
    bool hasListeners = elm && (elm->HasListenersFor(u"connect"_ns) ||
                                elm->HasListenersFor(u"disconnect"_ns));
    // The port effectively holds OS resources while opened, and while closing
    // is in progress. Both states require the worker to remain alive so that
    // close IPC callbacks can complete.
    bool isActive = (mState == State::Opened) || (mState == State::Closing);
    needsRef = isActive || hasListeners;
  }

  if (needsRef && !mWorkerRef) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    if (workerPrivate) {
      RefPtr<SerialPort> self = this;
      mWorkerRef = StrongWorkerRef::Create(workerPrivate, "SerialPort",
                                           [self]() { self->Shutdown(); });
    }
  } else if (!needsRef && mWorkerRef) {
    mWorkerRef = nullptr;
  }
}

void SerialPort::EventListenerAdded(nsAtom* aType) {
  DOMEventTargetHelper::EventListenerAdded(aType);
  UpdateWorkerRef();
}

void SerialPort::EventListenerRemoved(nsAtom* aType) {
  DOMEventTargetHelper::EventListenerRemoved(aType);
  UpdateWorkerRef();
}

SerialPort::~SerialPort() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPort[%p] destroyed for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));
  MOZ_ASSERT(mHasShutdown);
}

void SerialPort::Shutdown() {
  if (mHasShutdown) {
    return;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPort[%p] shutting down port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));
  mHasShutdown = true;

  if (mState == State::Opened || mState == State::Closing) {
    mState = State::Closed;
    // Don't have to wait for this
    RefPtr<Promise> ignoredPromise = CloseStreams(StreamCloseMode::Forced);
  }

  if (mOpenPromise) {
    mOpenPromise->MaybeRejectWithAbortError("Port was shut down");
    mOpenPromise = nullptr;
  }
  if (mClosePromise) {
    mClosePromise->MaybeRejectWithNetworkError("Port was shut down");
    mClosePromise = nullptr;
  }

  if (mChild) {
    mChild->Shutdown();
    mChild = nullptr;
  }

  mWorkerRef = nullptr;
}

void SerialPort::DisconnectFromOwner() {
  Shutdown();
  DOMEventTargetHelper::DisconnectFromOwner();
}

void SerialPort::HandleReadableErrored(ReadableStream* aStream, bool aFatal) {
  if (aFatal) {
    mReadFatal = true;
  }
  if (mReadable == aStream) {
    mReadable = nullptr;
  }
}

JSObject* SerialPort::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return SerialPort_Binding::Wrap(aCx, this, aGivenProto);
}

void SerialPort::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = true;
  aVisitor.SetParentTarget(mSerial, false);
}

already_AddRefed<Promise> SerialPort::Open(const SerialOptions& aOptions,
                                           ErrorResult& aRv) {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // https://wicg.github.io/serial/#dom-serialport-open
  // Step 1: Let promise be a new promise.
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::Open called for port '%s' with baudRate=%u, "
           "dataBits=%u, stopBits=%u, parity=%u, bufferSize=%u, flowControl=%u",
           this, NS_ConvertUTF16toUTF8(mInfo.id()).get(), aOptions.mBaudRate,
           aOptions.mDataBits, aOptions.mStopBits,
           static_cast<uint8_t>(aOptions.mParity), aOptions.mBufferSize,
           static_cast<uint8_t>(aOptions.mFlowControl)));

  // Step 2: If this.[[state]] is not "closed", reject promise with an
  // InvalidStateError DOMException and return promise.
  switch (mState) {
    case State::Closed:
      break;
    case State::Opening:
      promise->MaybeRejectWithInvalidStateError("Port is being opened");
      return promise.forget();
    case State::Opened:
      promise->MaybeRejectWithInvalidStateError("Port is already open");
      return promise.forget();
    case State::Closing:
      promise->MaybeRejectWithInvalidStateError("Port is being closed");
      return promise.forget();
    case State::Forgetting:
    case State::Forgotten:
      promise->MaybeRejectWithInvalidStateError("Port has been forgotten");
      return promise.forget();
  }
  MOZ_ASSERT(mState == State::Closed);

  // Step 3: If options["baudRate"] is 0, reject promise with a
  // TypeError and return promise.
  if (aOptions.mBaudRate == 0) {
    promise->MaybeRejectWithTypeError("Invalid baud rate");
    return promise.forget();
  }

  // Step 4: If options["dataBits"] is not 7 or 8, reject promise with a
  // TypeError and return promise.
  if (aOptions.mDataBits != 7 && aOptions.mDataBits != 8) {
    promise->MaybeRejectWithTypeError("Data bits must be 7 or 8");
    return promise.forget();
  }

  // Step 5: If options["stopBits"] is not 1 or 2, reject promise with a
  // TypeError and return promise.
  if (aOptions.mStopBits != 1 && aOptions.mStopBits != 2) {
    promise->MaybeRejectWithTypeError("Stop bits must be 1 or 2");
    return promise.forget();
  }

  // Step 6: If options["bufferSize"] is 0, reject promise with a TypeError
  // and return promise.
  if (aOptions.mBufferSize == 0) {
    promise->MaybeRejectWithTypeError("Invalid buffer size");
    return promise.forget();
  }

  // Step 7: Optionally, if options["bufferSize"] is larger than the
  // implementation is able to support, reject promise with a TypeError.
  if (aOptions.mBufferSize > kMaxSerialBufferSize) {
    promise->MaybeRejectWithTypeError(
        "Requested buffer size exceeds the maximum supported size");
    return promise.forget();
  }

  if (!mChild) {
    promise->MaybeRejectWithNotSupportedError("Port actor not available");
    return promise.forget();
  }

  // Step 8: Set this.[[state]] to "opening".
  mState = State::Opening;
  mOpenPromise = promise;

  IPCSerialOptions options{aOptions.mBaudRate,   aOptions.mDataBits,
                           aOptions.mStopBits,   aOptions.mParity,
                           aOptions.mBufferSize, aOptions.mFlowControl};

  // Step 9 (in parallel): Request the operating system to open the serial port
  // with the given connection parameters. The PSerialPort actor was created
  // eagerly when the port was granted, so we can send Open directly.
  RefPtr<SerialPortChild> child = mChild;
  RefPtr<SerialPort> self = this;

  child->SendOpen(options)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self, bufferSize = options.bufferSize()](nsresult aResult) {
        if (self->mHasShutdown) {
          return;
        }
        if (NS_SUCCEEDED(aResult)) {
          MOZ_LOG(gWebSerialLog, LogLevel::Info,
                  ("SerialPort[%p] opened successfully for port '%s'",
                   self.get(), NS_ConvertUTF16toUTF8(self->mInfo.id()).get()));
          if (self->mState == State::Opening) {
            // Step 9.3: Set this.[[state]] to "opened".
            self->mState = State::Opened;
            self->mReadFatal = false;
            self->UpdateWorkerRef();
            // Step 9.4: Set this.[[bufferSize]].
            self->mBufferSize = bufferSize;
            self->mPipeCapacity = std::max(bufferSize, kMinSerialPortPumpSize);
            // Streams are created lazily by GetReadable()/GetWritable().
            // Step 9.5: Resolve promise with undefined.
            if (self->mOpenPromise) {
              self->mOpenPromise->MaybeResolveWithUndefined();
            }
          } else if (self->mOpenPromise) {
            // Forget()/MarkForgotten() moved us out of Opening while the IPC
            // was in flight. The promise is rejected rather than resolved
            // because the port is no longer usable.
            self->mOpenPromise->MaybeRejectWithAbortError(
                "Port was forgotten while opening");
          }
          self->mOpenPromise = nullptr;
        } else {
          // Step 9.2: Reject promise with a NetworkError.
          MOZ_LOG(gWebSerialLog, LogLevel::Error,
                  ("SerialPort[%p] failed to open port '%s': error 0x%08x",
                   self.get(), NS_ConvertUTF16toUTF8(self->mInfo.id()).get(),
                   static_cast<uint32_t>(aResult)));
          if (self->mState == State::Opening) {
            self->mState = State::Closed;
          }
          if (self->mOpenPromise) {
            self->mOpenPromise->MaybeRejectWithNetworkError(
                "Failed to open port");
            self->mOpenPromise = nullptr;
          }
        }
      },
      [self](mozilla::ipc::ResponseRejectReason aReason) {
        if (self->mHasShutdown) {
          return;
        }
        MOZ_LOG(gWebSerialLog, LogLevel::Error,
                ("SerialPort[%p] failed to open port '%s': IPC error "
                 "(reason: %d)",
                 self.get(), NS_ConvertUTF16toUTF8(self->mInfo.id()).get(),
                 static_cast<int>(aReason)));
        if (self->mState == State::Opening) {
          self->mState = State::Closed;
        }
        if (self->mOpenPromise) {
          self->mOpenPromise->MaybeRejectWithNetworkError(
              "Failed to open port: IPC communication error");
          self->mOpenPromise = nullptr;
        }
      });

  // Step 10: Return promise.
  return promise.forget();
}

already_AddRefed<Promise> SerialPort::SetSignals(
    const SerialOutputSignals& aSignals, ErrorResult& aRv) {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPort[%p]::SetSignals called for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  if (mState == State::Forgetting || mState == State::Forgotten) {
    promise->MaybeRejectWithInvalidStateError("Port has been forgotten");
    return promise.forget();
  }

  if (mState != State::Opened) {
    promise->MaybeRejectWithInvalidStateError("Port is not open");
    return promise.forget();
  }

  if (!aSignals.mDataTerminalReady.WasPassed() &&
      !aSignals.mRequestToSend.WasPassed() && !aSignals.mBreak.WasPassed()) {
    promise->MaybeRejectWithTypeError(
        "At least one signal must be specified in setSignals()");
    return promise.forget();
  }

  if (!mChild) {
    promise->MaybeRejectWithInvalidStateError("Port not initialized");
    return promise.forget();
  }

  IPCSerialOutputSignals signals{
      aSignals.mDataTerminalReady.WasPassed()
          ? Some(aSignals.mDataTerminalReady.Value())
          : Nothing(),
      aSignals.mRequestToSend.WasPassed()
          ? Some(aSignals.mRequestToSend.Value())
          : Nothing(),
      aSignals.mBreak.WasPassed() ? Some(aSignals.mBreak.Value()) : Nothing()};

  RefPtr<SerialPortChild> child = mChild;
  nsISerialEventTarget* actorTarget = child->GetActorEventTarget();

  if (!actorTarget) {
    promise->MaybeRejectWithNetworkError("Actor not available");
    return promise.forget();
  }

  InvokeAsync(actorTarget, "SerialPort::SendSetSignals",
              [child = std::move(child), signals = std::move(signals)]() {
                return child->SendSetSignals(signals);
              })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise](nsresult aResult) {
            if (NS_SUCCEEDED(aResult)) {
              promise->MaybeResolveWithUndefined();
            } else {
              promise->MaybeRejectWithNetworkError(
                  nsPrintfCString("Failed to set signals: 0x%08x",
                                  static_cast<uint32_t>(aResult)));
            }
          },
          [promise](mozilla::ipc::ResponseRejectReason) {
            promise->MaybeRejectWithNetworkError(
                "Failed to set signals: IPC communication error");
          });

  return promise.forget();
}

already_AddRefed<Promise> SerialPort::GetSignals(ErrorResult& aRv) {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPort[%p]::GetSignals called for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  if (mState == State::Forgetting || mState == State::Forgotten) {
    promise->MaybeRejectWithInvalidStateError("Port has been forgotten");
    return promise.forget();
  }

  if (mState != State::Opened) {
    promise->MaybeRejectWithInvalidStateError("Port is not open");
    return promise.forget();
  }

  if (!mChild) {
    promise->MaybeRejectWithInvalidStateError("Port not initialized");
    return promise.forget();
  }

  RefPtr<SerialPortChild> child = mChild;
  nsISerialEventTarget* actorTarget = child->GetActorEventTarget();

  if (!actorTarget) {
    promise->MaybeRejectWithNetworkError("Actor not available");
    return promise.forget();
  }

  InvokeAsync(actorTarget, "SerialPort::SendGetSignals",
              [child]() { return child->SendGetSignals(); })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise](
              const std::tuple<nsresult, IPCSerialInputSignals>& aResult) {
            nsresult rv = std::get<0>(aResult);
            if (NS_SUCCEEDED(rv)) {
              const IPCSerialInputSignals& ipcSignals = std::get<1>(aResult);
              SerialInputSignals result;
              result.mDataCarrierDetect = ipcSignals.dataCarrierDetect();
              result.mClearToSend = ipcSignals.clearToSend();
              result.mRingIndicator = ipcSignals.ringIndicator();
              result.mDataSetReady = ipcSignals.dataSetReady();
              promise->MaybeResolve(result);
            } else {
              promise->MaybeRejectWithNetworkError(nsPrintfCString(
                  "Failed to get signals: 0x%08x", static_cast<uint32_t>(rv)));
            }
          },
          [promise](mozilla::ipc::ResponseRejectReason) {
            promise->MaybeRejectWithNetworkError(
                "Failed to get signals: IPC communication error");
          });

  return promise.forget();
}

// https://wicg.github.io/serial/#dom-serialport-close
already_AddRefed<Promise> SerialPort::Close(ErrorResult& aRv) {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Step 1: Let promise be a new promise.
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::Close called for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  // Step 2: If this.[[state]] is not "opened", reject promise with an
  // InvalidStateError DOMException and return promise.
  switch (mState) {
    case State::Opened:
      break;
    case State::Closing:
      promise->MaybeRejectWithInvalidStateError("Port is being closed");
      return promise.forget();
    case State::Forgetting:
    case State::Forgotten:
      promise->MaybeRejectWithInvalidStateError("Port has been forgotten");
      return promise.forget();
    case State::Closed:
    case State::Opening:
      promise->MaybeRejectWithInvalidStateError("Port is not open");
      return promise.forget();
  }
  MOZ_ASSERT(mState == State::Opened);

  // Steps 3-8: Cancel the readable stream (step 3), abort the writable
  // stream (step 4), and let combinedPromise be the result of getting a
  // promise to wait for all (steps 5-8). Our pendingClosePromise is
  // effectively resolved once both streams are nulled (step 6), so the
  // combined promise just waits for the cancel and abort to finish.
  RefPtr<Promise> combinedPromise = CloseStreams(StreamCloseMode::Graceful);
  if (!combinedPromise) {
    combinedPromise = Promise::CreateResolvedWithUndefined(global, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  // Step 9: Set this.[[state]] to "closing".
  mState = State::Closing;
  mClosePromise = promise;

  // Step 10: Upon fulfillment of combinedPromise, run the following steps
  // in parallel: request the operating system close the serial port.
  // Step 10 (rejection): Upon rejection of combinedPromise with reason r,
  // reject promise with r.
  combinedPromise->AddCallbacksWithCycleCollectedArgs(
      [](JSContext*, JS::Handle<JS::Value>, ErrorResult&, SerialPort* aSelf) {
        aSelf->CloseAfterStreamsClosed();
      },
      [](JSContext*, JS::Handle<JS::Value> aReason, ErrorResult&,
         SerialPort* aSelf) {
        if (aSelf->mHasShutdown) {
          return;
        }
        // Forget()/MarkForgotten() may have transitioned to a terminal state.
        if (aSelf->mState == State::Closing) {
          aSelf->mState = State::Closed;
        }
        aSelf->UpdateWorkerRef();
        if (RefPtr<Promise> closePromise = aSelf->mClosePromise.forget()) {
          closePromise->MaybeReject(aReason);
        }
      },
      RefPtr(this));

  // Step 11: Return promise.
  return promise.forget();
}

already_AddRefed<Promise> SerialPort::Forget(ErrorResult& aRv) {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::Forget called for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  // Step 2.1: Set this.[[state]] to "forgetting".
  const bool wasActive =
      (mState == State::Opened) || (mState == State::Closing);
  mState = State::Forgetting;

  if (mSerial) {
    RefPtr<Serial> serial = mSerial;
    serial->ForgetPort(mInfo.id());
  }

  if (wasActive) {
    // Don't have to wait for this
    RefPtr<Promise> ignoredPromise = CloseStreams(StreamCloseMode::Graceful);
  }

  UpdateWorkerRef();

  if (mChild) {
    RefPtr<SerialPortChild> child = mChild;
    nsISerialEventTarget* actorTarget = child->GetActorEventTarget();

    if (!actorTarget) {
      // Step 2.3: Set this.[[state]] to "forgotten".
      mState = State::Forgotten;
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }

    RefPtr<SerialPort> self = this;
    InvokeAsync(actorTarget, "SerialPort::SendForget",
                [child = std::move(child)]() { return child->SendClose(); })
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [promise, self](nsresult aResult) {
              // Step 2.3: Set this.[[state]] to "forgotten".
              self->mState = State::Forgotten;
              promise->MaybeResolveWithUndefined();
            },
            [promise, self](mozilla::ipc::ResponseRejectReason aReason) {
              self->mState = State::Forgotten;
              promise->MaybeResolveWithUndefined();
            });
  } else {
    mState = State::Forgotten;
    promise->MaybeResolveWithUndefined();
  }

  return promise.forget();
}

void SerialPort::MarkForgotten() {
  if (mState == State::Forgetting || mState == State::Forgotten) {
    return;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::MarkForgotten for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  const bool wasActive =
      (mState == State::Opened) || (mState == State::Closing);
  mState = State::Forgotten;

  if (wasActive) {
    // Don't have to wait for this
    RefPtr<Promise> ignoredPromise = CloseStreams(StreamCloseMode::Graceful);
  }

  UpdateWorkerRef();
}

void SerialPort::GetInfo(SerialPortInfo& aRetVal, ErrorResult& aRv) {
  if (mInfo.usbVendorId().isSome()) {
    aRetVal.mUsbVendorId.Construct(mInfo.usbVendorId().value());
  }

  if (mInfo.usbProductId().isSome()) {
    aRetVal.mUsbProductId.Construct(mInfo.usbProductId().value());
  }

  if (mInfo.bluetoothServiceClassId().isSome()) {
    OwningStringOrUnsignedLong uuid;
    uuid.SetAsString() = mInfo.bluetoothServiceClassId().value();
    aRetVal.mBluetoothServiceClassId.Construct(uuid);
  }
}

ReadableStream* SerialPort::GetReadable() {
  if (mState != State::Opened || mReadFatal) {
    return nullptr;
  }
  // Per spec, SerialPort.readable attribute becomes null after reader.cancel().
  // Detect the closed state and clear the reference so a fresh stream is
  // created.
  if (mReadable && mReadable->State() == ReadableStream::ReaderState::Closed) {
    mReadable = nullptr;
  }
  if (!mReadable) {
    return CreateReadableStream();
  }
  return mReadable;
}

WritableStream* SerialPort::GetWritable() {
  if (mState != State::Opened) {
    return nullptr;
  }
  // Per spec, writable becomes null after writer.close() or writer.abort().
  // Detect any non-writable state and clear the reference so a fresh
  // stream is created.
  if (mWritable &&
      mWritable->State() != WritableStream::WriterState::Writable) {
    mWritable = nullptr;
  }
  if (!mWritable) {
    return CreateWritableStream();
  }
  return mWritable;
}

void SerialPort::OnActorDestroyed() {
  if (mHasShutdown) {
    return;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::OnActorDestroyed for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  // Clear the child reference first since the actor is already destroyed.
  // This prevents MarkForgotten from trying to use the dead actor.
  mChild = nullptr;

  // Mark the port as forgotten (closes streams, updates worker ref).
  MarkForgotten();

  // Remove from Serial's port list so it no longer appears in getPorts().
  if (mSerial) {
    RefPtr<Serial> serial = mSerial;
    serial->ForgetPort(mInfo.id());
  }
}

void SerialPort::NotifyConnected() {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p] connected for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));

  mPhysicallyPresent = true;

  auto event = MakeRefPtr<Event>(this, nullptr, nullptr);
  event->InitEvent(u"connect"_ns, true, false);
  event->SetTrusted(true);
  DispatchTrustedEvent(event);
}

void SerialPort::NotifyDisconnected() {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p] disconnected for port '%s'", this,
           NS_ConvertUTF16toUTF8(mInfo.id()).get()));
  if (mState == State::Opened || mState == State::Closing) {
    mState = State::Closed;
  }
  mPhysicallyPresent = false;
  // Don't have to wait for this
  RefPtr<Promise> ignoredPromise = CloseStreams(StreamCloseMode::Disconnected);
  UpdateWorkerRef();

  auto event = MakeRefPtr<Event>(this, nullptr, nullptr);
  event->InitEvent(u"disconnect"_ns, true, false);
  event->SetTrusted(true);
  DispatchTrustedEvent(event);
}

// Thin subclass to access the protected SetUpByteNative.
class SerialByteReadableStream final : public ReadableStream {
 public:
  explicit SerialByteReadableStream(nsIGlobalObject* aGlobal)
      : ReadableStream(aGlobal, HoldDropJSObjectsCaller::Implicit) {}

  void SetUp(JSContext* aCx, UnderlyingSourceAlgorithmsWrapper& aAlgorithms,
             Maybe<double> aHighWaterMark, ErrorResult& aRv) {
    SetUpByteNative(aCx, aAlgorithms, aHighWaterMark, aRv);
  }
};

ReadableStream* SerialPort::CreateReadableStream() {
  MOZ_ASSERT(mState == State::Opened);
  MOZ_ASSERT(!mReadable);

  // Create a DataPipe pair locally. The child keeps the receiver (for the
  // ReadableStream) and sends the sender to the parent (for the read pump).
  RefPtr<mozilla::ipc::DataPipeSender> sender;
  RefPtr<mozilla::ipc::DataPipeReceiver> receiver;
  nsresult rv = mozilla::ipc::NewDataPipe(mPipeCapacity, getter_AddRefs(sender),
                                          getter_AddRefs(receiver));
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  // Send the sender endpoint to the parent so it can start the read pump.
  if (mChild) {
    RefPtr<SerialPortChild> child = mChild;
    nsISerialEventTarget* actorTarget = child->GetActorEventTarget();
    if (actorTarget) {
      actorTarget->Dispatch(NS_NewRunnableFunction(
          "SerialPort::AttachReadPipe", [child, sender = std::move(sender)]() {
            child->SendAttachReadPipe(sender);
          }));
    }
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(GetRelevantGlobal())) {
    return nullptr;
  }

  JSContext* cx = jsapi.cx();
  ErrorResult erv;

  nsCOMPtr<nsIAsyncInputStream> readInput = receiver.get();
  auto readableStream =
      MakeRefPtr<SerialByteReadableStream>(GetRelevantGlobal());
  RefPtr readAlgorithms =
      MakeRefPtr<SerialPortReadAlgorithms>(cx, readInput, readableStream, this);
  // Use a zero high water mark: the DataPipe itself provides buffering
  // (sized to mBufferSize), so the stream shouldn't eagerly pull before a
  // reader is acquired.
  readableStream->SetUp(cx, *readAlgorithms, Some(0.0), erv);
  if (erv.Failed()) {
    return nullptr;
  }
  mReadable = readableStream;

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::CreateReadableStream created readable=%p", this,
           mReadable.get()));
  return mReadable;
}

WritableStream* SerialPort::CreateWritableStream() {
  MOZ_ASSERT(mState == State::Opened);
  MOZ_ASSERT(!mWritable);

  // Create a DataPipe pair locally. The child keeps the sender (for the
  // WritableStream) and sends the receiver to the parent (for the write pump).
  RefPtr<mozilla::ipc::DataPipeSender> sender;
  RefPtr<mozilla::ipc::DataPipeReceiver> receiver;
  nsresult rv = mozilla::ipc::NewDataPipe(mPipeCapacity, getter_AddRefs(sender),
                                          getter_AddRefs(receiver));
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  // Send the receiver endpoint to the parent so it can start the write pump.
  if (mChild) {
    RefPtr<SerialPortChild> child = mChild;
    nsISerialEventTarget* actorTarget = child->GetActorEventTarget();
    if (actorTarget) {
      actorTarget->Dispatch(
          NS_NewRunnableFunction("SerialPort::AttachWritePipe",
                                 [child, receiver = std::move(receiver)]() {
                                   child->SendAttachWritePipe(receiver);
                                 }));
    }
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(GetRelevantGlobal())) {
    return nullptr;
  }

  JSContext* cx = jsapi.cx();
  ErrorResult erv;

  nsCOMPtr<nsIAsyncOutputStream> writeOutput = sender.get();
  RefPtr writeAlgorithms = MakeRefPtr<SerialPortWriteAlgorithms>(
      GetRelevantGlobal(), writeOutput, this);
  mWritable = WritableStream::CreateNative(
      cx, *GetRelevantGlobal(), *writeAlgorithms,
      Some(static_cast<double>(mBufferSize)), nullptr, erv);
  if (erv.Failed()) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::CreateWritableStream created writable=%p", this,
           mWritable.get()));
  return mWritable;
}

void SerialPort::CloseAfterStreamsClosed() {
  if (mHasShutdown) {
    return;
  }

  nsresult rv = NS_OK;
  // Every synchronous exit path marks the port as closed.
  auto markClosed = MakeScopeExit([&]() { SettleClosePromise(rv); });

  RefPtr<SerialPortChild> child = mChild;
  if (!child) {
    return;
  }

  nsISerialEventTarget* actorTarget = child->GetActorEventTarget();
  if (!actorTarget) {
    // Make SettleClosePromise reject the promise with an error.
    rv = NS_ERROR_FAILURE;
    return;
  }

  // Async path: SettleClosePromise handles cleanup in the callbacks.
  markClosed.release();

  RefPtr<SerialPort> self = this;
  nsCOMPtr<nsISerialEventTarget> owningThread = GetCurrentSerialEventTarget();
  InvokeAsync(actorTarget, "SerialPort::SendClose",
              [child]() { return child->SendClose(); })
      ->Then(
          owningThread, "SerialPort::Close::SendClose",
          [self](nsresult aResult) { self->SettleClosePromise(aResult); },
          [self](mozilla::ipc::ResponseRejectReason aReason) {
            self->SettleClosePromise(NS_ERROR_DOM_NETWORK_ERR);
          });
}

void SerialPort::SettleClosePromise(nsresult aResult) {
  if (mHasShutdown) {
    return;
  }
  // Set this.[[state]] to "closed", unless Forget()/MarkForgotten() raced
  // ahead and moved us into a terminal state.
  if (mState == State::Closing) {
    mState = State::Closed;
  }
  UpdateWorkerRef();
  if (RefPtr<Promise> closePromise = mClosePromise.forget()) {
    if (NS_SUCCEEDED(aResult)) {
      closePromise->MaybeResolveWithUndefined();
    } else {
      closePromise->MaybeRejectWithNetworkError(
          "Failed to close port: IPC communication error");
    }
  }
}

already_AddRefed<Promise> SerialPort::CloseStreams(StreamCloseMode aMode) {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    return nullptr;
  }

  if (!mReadable && !mWritable) {
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPort[%p]::CloseStreams closing streams "
           "(readable=%p, writable=%p, mode=%s)",
           this, mReadable.get(), mWritable.get(),
           aMode == StreamCloseMode::Forced         ? "forced"
           : aMode == StreamCloseMode::Disconnected ? "disconnected"
                                                    : "graceful"));

  AutoJSAPI jsapi;
  if (!jsapi.Init(global)) {
    return nullptr;
  }

  JSContext* cx = jsapi.cx();

  RefPtr<DOMException> exception = DOMException::Create(
      NS_ERROR_DOM_NETWORK_ERR, aMode == StreamCloseMode::Disconnected
                                    ? "The device has been lost."_ns
                                    : "Port has been closed"_ns);
  JS::Rooted<JS::Value> errorVal(cx);
  bool hasError = ToJSValue(cx, exception, &errorVal);

  // Error the streams instead of aborting, so we don't dispatchevents
  // or runauthor-visible algorithms. The OS resources are released by
  // mChild->Shutdown() in Shutdown(), or by the parent on disconnect.
  if (aMode == StreamCloseMode::Forced ||
      aMode == StreamCloseMode::Disconnected) {
    if (mReadable && hasError) {
      IgnoredErrorResult rv;
      RefPtr readable = mReadable;
      readable->ErrorNative(cx, errorVal, rv);
    }
    if (mWritable && hasError) {
      IgnoredErrorResult rv;
      RefPtr writable = mWritable;
      writable->ErrorNative(cx, errorVal, rv);
    }
    mReadable = nullptr;
    mWritable = nullptr;
    return nullptr;
  }

  // Cancel the readable and abort the writable, collecting their promises.
  nsTArray<RefPtr<Promise>> streamPromises;

  if (mReadable && hasError) {
    IgnoredErrorResult rv;
    RefPtr readable = mReadable;
    if (RefPtr<Promise> cancelPromise =
            readable->CancelNative(cx, errorVal, rv)) {
      streamPromises.AppendElement(std::move(cancelPromise));
    }
  }

  if (mWritable && hasError) {
    IgnoredErrorResult rv;
    RefPtr writable = mWritable;
    if (RefPtr<Promise> abortPromise =
            writable->AbortNative(cx, errorVal, rv)) {
      streamPromises.AppendElement(std::move(abortPromise));
    }
  }

  if (streamPromises.IsEmpty()) {
    mReadable = nullptr;
    mWritable = nullptr;
    return nullptr;
  }

  IgnoredErrorResult rv;
  RefPtr<Promise> combined = Promise::All(cx, streamPromises, rv);
  if (!combined) {
    mReadable = nullptr;
    mWritable = nullptr;
    return nullptr;
  }

  // Hold mReadable and mWritable until the cancel/abort promises settle.
  // Otherwise the streams may be cycle collected before finishing and those
  // promises may never resolve.
  combined->AddCallbacksWithCycleCollectedArgs(
      [](JSContext*, JS::Handle<JS::Value>, ErrorResult&, SerialPort* aSelf) {
        aSelf->mReadable = nullptr;
        aSelf->mWritable = nullptr;
      },
      [](JSContext*, JS::Handle<JS::Value>, ErrorResult&, SerialPort* aSelf) {
        aSelf->mReadable = nullptr;
        aSelf->mWritable = nullptr;
      },
      RefPtr(this));

  return combined.forget();
}

}  // namespace mozilla::dom
