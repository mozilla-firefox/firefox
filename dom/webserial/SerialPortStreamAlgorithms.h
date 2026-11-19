/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SerialPortStreamAlgorithms_h
#define mozilla_dom_SerialPortStreamAlgorithms_h

#include "mozilla/dom/UnderlyingSinkCallbackHelpers.h"
#include "mozilla/dom/UnderlyingSourceCallbackHelpers.h"

namespace mozilla::dom {

class SerialPort;

// Subclass of WritableStreamToOutputAlgorithms that adds serial-specific
// close (drain) and abort (flush) behavior. The base class handles all
// data writing to the DataPipeSender via WriteCallbackImpl.
class SerialPortWriteAlgorithms final
    : public WritableStreamToOutputAlgorithms {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(SerialPortWriteAlgorithms,
                                           WritableStreamToOutputAlgorithms)

  SerialPortWriteAlgorithms(nsIGlobalObject* aParent,
                            nsIAsyncOutputStream* aOutput, SerialPort* aPort);

  // Drain transmit buffers when the writable stream is closed.
  already_AddRefed<Promise> CloseCallbackImpl(JSContext* aCx,
                                              ErrorResult& aRv) override;

  // Flush (discard) transmit buffers when the writable stream is aborted.
  already_AddRefed<Promise> AbortCallbackImpl(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
      ErrorResult& aRv) override;

  void ReleaseObjects() override;

 protected:
  ~SerialPortWriteAlgorithms() override;

 private:
  already_AddRefed<Promise> CloseOrAbortImpl(bool aDrain, ErrorResult& aRv);

  RefPtr<SerialPort> mPort;
};

// Subclass of InputToReadableStreamAlgorithms that adds serial-specific
// cancel behavior. The base class handles all BYOB read logic; this class
// adds receive-buffer flushing when readable.cancel() is called.
class SerialPortReadAlgorithms final : public InputToReadableStreamAlgorithms {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(SerialPortReadAlgorithms,
                                           InputToReadableStreamAlgorithms)

  SerialPortReadAlgorithms(JSContext* aCx, nsIAsyncInputStream* aInput,
                           ReadableStream* aStream, SerialPort* aPort);

  already_AddRefed<Promise> CancelCallbackImpl(
      JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
      ErrorResult& aRv) override;

  // Maps serial-specific receive error codes (e.g. parity errors) to the
  // corresponding named DOMException required by the Web Serial spec.
  void BuildErrorValue(JSContext* aCx, nsresult aError,
                       JS::MutableHandle<JS::Value> aErrorValue) override;

  // Runs the spec's "handle closing the readable stream" steps after the
  // stream is errored, and sets [[readFatal]] if the device was lost.
  void ErrorPropagation(JSContext* aCx, ReadableStream* aStream,
                        nsresult aError) override;

  void ReleaseObjects() override;

 protected:
  ~SerialPortReadAlgorithms() override;

 private:
  RefPtr<SerialPort> mPort;
  nsCOMPtr<nsIAsyncInputStream> mInputStream;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_SerialPortStreamAlgorithms_h
