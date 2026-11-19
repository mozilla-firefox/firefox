/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SerialPortStreamAlgorithms.h"

#include "SerialLogging.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/SerialPort.h"
#include "mozilla/dom/SerialPortChild.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsError.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(SerialPortWriteAlgorithms,
                                   WritableStreamToOutputAlgorithms, mPort)
NS_IMPL_ADDREF_INHERITED(SerialPortWriteAlgorithms,
                         WritableStreamToOutputAlgorithms)
NS_IMPL_RELEASE_INHERITED(SerialPortWriteAlgorithms,
                          WritableStreamToOutputAlgorithms)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SerialPortWriteAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(WritableStreamToOutputAlgorithms)

SerialPortWriteAlgorithms::SerialPortWriteAlgorithms(
    nsIGlobalObject* aParent, nsIAsyncOutputStream* aOutput, SerialPort* aPort)
    : WritableStreamToOutputAlgorithms(aParent, aOutput), mPort(aPort) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortWriteAlgorithms[%p] created for port %p", this, aPort));
}

already_AddRefed<Promise> SerialPortWriteAlgorithms::CloseCallbackImpl(
    JSContext* aCx, ErrorResult& aRv) {
  return CloseOrAbortImpl(true, aRv);
}

already_AddRefed<Promise> SerialPortWriteAlgorithms::AbortCallbackImpl(
    JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
    ErrorResult& aRv) {
  return CloseOrAbortImpl(false, aRv);
}

already_AddRefed<Promise> SerialPortWriteAlgorithms::CloseOrAbortImpl(
    bool aDrain, ErrorResult& aRv) {
  if (!mPort) {
    aRv.ThrowInvalidStateError("Port is not initialized");
    return nullptr;
  }

  RefPtr<SerialPortChild> child = mPort->GetChild();
  if (!child) {
    aRv.ThrowInvalidStateError("Port is not connected");
    return nullptr;
  }

  if (aDrain) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Debug,
        ("SerialPortWriteAlgorithms[%p]::CloseCallbackImpl draining", this));
    // Close the DataPipe so the parent knows no more data is coming.
    CloseOutput();
  } else {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Debug,
        ("SerialPortWriteAlgorithms[%p]::AbortCallbackImpl flushing", this));
    CloseOutputWithStatus(NS_ERROR_ABORT);
  }

  RefPtr<Promise> promise =
      Promise::CreateInfallible(mPort->GetRelevantGlobal());

  nsCOMPtr<nsISerialEventTarget> owningThread = GetCurrentSerialEventTarget();
  nsISerialEventTarget* actorTarget = child->GetActorEventTarget();

  if (!actorTarget) {
    promise->MaybeRejectWithNetworkError("Actor not available");
    return promise.forget();
  }

  RefPtr<SerialPortWriteAlgorithms> self = this;
  nsCString operationName(aDrain ? "drain" : "flush");
  InvokeAsync(actorTarget,
              aDrain ? "SerialPortWriteAlgorithms::SendDrain"
                     : "SerialPortWriteAlgorithms::SendFlush",
              [child, aDrain]() {
                return aDrain ? child->SendDrain() : child->SendFlush(false);
              })
      ->Then(
          owningThread, __func__,
          [promise, self, aDrain, operationName](nsresult aResult) {
            if (NS_SUCCEEDED(aResult)) {
              MOZ_LOG(gWebSerialLog, LogLevel::Debug,
                      ("SerialPortWriteAlgorithms[%p] %s succeeded", self.get(),
                       operationName.get()));
              promise->MaybeResolveWithUndefined();
            } else {
              MOZ_LOG(gWebSerialLog, LogLevel::Error,
                      ("SerialPortWriteAlgorithms[%p] %s failed: 0x%08x",
                       self.get(), operationName.get(),
                       static_cast<uint32_t>(aResult)));
              if (aDrain) {
                promise->MaybeRejectWithNetworkError(
                    "Failed to drain transmit buffers");
              } else {
                promise->MaybeRejectWithNetworkError(
                    "Failed to discard transmit buffers");
              }
            }
          },
          [promise, self, aDrain,
           operationName](mozilla::ipc::ResponseRejectReason aReason) {
            MOZ_LOG(
                gWebSerialLog, LogLevel::Error,
                ("SerialPortWriteAlgorithms[%p] %s IPC error "
                 "(reason: %d)",
                 self.get(), operationName.get(), static_cast<int>(aReason)));
            if (aDrain) {
              promise->MaybeRejectWithNetworkError(
                  "Failed to drain transmit buffers: IPC "
                  "communication error");
            } else {
              promise->MaybeRejectWithNetworkError(
                  "Failed to discard transmit buffers: IPC "
                  "communication error");
            }
          });

  return promise.forget();
}

SerialPortWriteAlgorithms::~SerialPortWriteAlgorithms() = default;

void SerialPortWriteAlgorithms::ReleaseObjects() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortWriteAlgorithms[%p]::ReleaseObjects", this));
  WritableStreamToOutputAlgorithms::ReleaseObjects();
  mPort = nullptr;
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(SerialPortReadAlgorithms,
                                   InputToReadableStreamAlgorithms, mPort)
NS_IMPL_ADDREF_INHERITED(SerialPortReadAlgorithms,
                         InputToReadableStreamAlgorithms)
NS_IMPL_RELEASE_INHERITED(SerialPortReadAlgorithms,
                          InputToReadableStreamAlgorithms)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SerialPortReadAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(InputToReadableStreamAlgorithms)

SerialPortReadAlgorithms::SerialPortReadAlgorithms(JSContext* aCx,
                                                   nsIAsyncInputStream* aInput,
                                                   ReadableStream* aStream,
                                                   SerialPort* aPort)
    : InputToReadableStreamAlgorithms(aCx, aInput, aStream),
      mPort(aPort),
      mInputStream(aInput) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortReadAlgorithms[%p] created for port %p", this, aPort));
}

already_AddRefed<Promise> SerialPortReadAlgorithms::CancelCallbackImpl(
    JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
    ErrorResult& aRv) {
  if (!mPort) {
    aRv.ThrowInvalidStateError("Port is not initialized");
    return nullptr;
  }

  RefPtr<SerialPortChild> child = mPort->GetChild();
  if (!child) {
    aRv.ThrowInvalidStateError("Port is not connected");
    return nullptr;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortReadAlgorithms[%p]::CancelCallbackImpl flushing receive",
           this));

  // Close the DataPipe to signal cancellation to the parent.
  if (mInputStream) {
    mInputStream->CloseWithStatus(NS_ERROR_ABORT);
  }

  RefPtr<Promise> promise =
      Promise::CreateInfallible(mPort->GetRelevantGlobal());

  nsCOMPtr<nsISerialEventTarget> owningThread = GetCurrentSerialEventTarget();
  nsISerialEventTarget* actorTarget = child->GetActorEventTarget();

  if (!actorTarget) {
    promise->MaybeRejectWithNetworkError("Actor not available");
    return promise.forget();
  }

  RefPtr<SerialPortReadAlgorithms> self = this;
  InvokeAsync(actorTarget, "SerialPortReadAlgorithms::SendFlush",
              [child]() { return child->SendFlush(true); })
      ->Then(
          owningThread, __func__,
          [promise, self](nsresult aResult) {
            if (NS_SUCCEEDED(aResult)) {
              MOZ_LOG(gWebSerialLog, LogLevel::Debug,
                      ("SerialPortReadAlgorithms[%p] flush receive succeeded",
                       self.get()));
              promise->MaybeResolveWithUndefined();
            } else {
              MOZ_LOG(
                  gWebSerialLog, LogLevel::Error,
                  ("SerialPortReadAlgorithms[%p] flush receive failed: 0x%08x",
                   self.get(), static_cast<uint32_t>(aResult)));
              promise->MaybeRejectWithNetworkError(
                  "Failed to discard receive buffers");
            }
          },
          [promise, self](mozilla::ipc::ResponseRejectReason aReason) {
            MOZ_LOG(gWebSerialLog, LogLevel::Error,
                    ("SerialPortReadAlgorithms[%p] flush receive IPC error "
                     "(reason: %d)",
                     self.get(), static_cast<int>(aReason)));
            promise->MaybeRejectWithNetworkError(
                "Failed to discard receive buffers: IPC communication error");
          });

  return promise.forget();
}

void SerialPortReadAlgorithms::BuildErrorValue(
    JSContext* aCx, nsresult aError, JS::MutableHandle<JS::Value> aErrorValue) {
  if (aError == NS_ERROR_DOM_SERIAL_PARITY_ERROR) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("SerialPortReadAlgorithms[%p]::BuildErrorValue surfacing "
             "ParityError",
             this));
    // DOMException::Create derives the name ("ParityError") and message from
    // the nsresult via domerr.msg.
    RefPtr<DOMException> exception = DOMException::Create(aError);
    if (exception && ToJSValue(aCx, *exception, aErrorValue)) {
      return;
    }
    // Fall through to the generic error if creation/wrapping failed.
  } else if (aError == NS_ERROR_DOM_NETWORK_ERR) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("SerialPortReadAlgorithms[%p]::BuildErrorValue surfacing "
             "NetworkError",
             this));
    RefPtr<DOMException> exception =
        DOMException::Create(aError, "The device has been lost."_ns);
    if (exception && ToJSValue(aCx, *exception, aErrorValue)) {
      return;
    }
    // Fall through to the generic error if creation/wrapping failed.
  }
  InputToReadableStreamAlgorithms::BuildErrorValue(aCx, aError, aErrorValue);
}

void SerialPortReadAlgorithms::ErrorPropagation(JSContext* aCx,
                                                ReadableStream* aStream,
                                                nsresult aError) {
  // Erroring the stream releases our objects, so hold on to the port.
  RefPtr<SerialPort> port = mPort;
  InputToReadableStreamAlgorithms::ErrorPropagation(aCx, aStream, aError);
  if (port && aStream->State() == ReadableStream::ReaderState::Errored) {
    port->HandleReadableErrored(
        aStream,
        /* aFatal = */ aError == NS_ERROR_DOM_NETWORK_ERR);
  }
}

SerialPortReadAlgorithms::~SerialPortReadAlgorithms() = default;

void SerialPortReadAlgorithms::ReleaseObjects() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortReadAlgorithms[%p]::ReleaseObjects", this));
  InputToReadableStreamAlgorithms::ReleaseObjects();
  mPort = nullptr;
  mInputStream = nullptr;
}

}  // namespace mozilla::dom
