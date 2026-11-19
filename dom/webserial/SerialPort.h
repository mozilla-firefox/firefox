/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SerialPort_h
#define mozilla_dom_SerialPort_h

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/SerialPortChild.h"
#include "mozilla/dom/SerialPortInfo.h"
#include "mozilla/dom/WorkerRef.h"

namespace mozilla::ipc {
class DataPipeReceiver;
class DataPipeSender;
}  // namespace mozilla::ipc

namespace mozilla::dom {

class Promise;
class ReadableStream;
class Serial;
class SerialPortReadAlgorithms;
class SerialPortWriteAlgorithms;
class WritableStream;
struct SerialOptions;
struct SerialOutputSignals;
struct SerialPortInfo;

class SerialPort final : public DOMEventTargetHelper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(SerialPort, DOMEventTargetHelper)

  SerialPort(const IPCSerialPortInfo& aInfo, Serial* aSerial);

  void EventListenerAdded(nsAtom* aType) override;
  void EventListenerRemoved(nsAtom* aType) override;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;

  already_AddRefed<Promise> Open(const SerialOptions& aOptions,
                                 ErrorResult& aRv);

  already_AddRefed<Promise> SetSignals(const SerialOutputSignals& aSignals,
                                       ErrorResult& aRv);

  already_AddRefed<Promise> GetSignals(ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> Close(ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> Forget(ErrorResult& aRv);

  // Mark this port as forgotten without the full Forget() flow (no IPC,
  // no manager callback). Used when another context (e.g. a worker) has
  // already performed the forget and the manager has been updated.
  MOZ_CAN_RUN_SCRIPT void MarkForgotten();

  // https://wicg.github.io/serial/#dom-serialport-connected
  // Returns this.[[connected]]: whether the device is physically connected.
  bool Connected() const { return mPhysicallyPresent; }

  enum class State : uint8_t {
    Closed,
    Opening,
    Opened,
    Closing,
    Forgetting,
    Forgotten,
  };

  bool IsForgotten() const { return mState == State::Forgotten; }

  // Returns whether the device is physically connected to the system.
  // This does not indicate whether the port is open or being used.
  bool PhysicallyPresent() const { return mPhysicallyPresent; }

  // Silently marks this port as physically present without dispatching a
  // connect event. Used when returning a port from requestPort(), since the
  // port is guaranteed to be connected (GetAvailablePorts only lists connected
  // devices) but the async RecvPortConnected notification may not have arrived.
  void MarkPhysicallyPresent() { mPhysicallyPresent = true; }

  void GetInfo(SerialPortInfo& aRetVal, ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT ReadableStream* GetReadable();

  // https://wicg.github.io/serial/#dom-serialport-writable
  MOZ_CAN_RUN_SCRIPT WritableStream* GetWritable();

  IMPL_EVENT_HANDLER(connect)
  IMPL_EVENT_HANDLER(disconnect)

  const nsString& Id() const { return mInfo.id(); }

  const IPCSerialPortInfo& GetPortInfo() const { return mInfo; }

  MOZ_CAN_RUN_SCRIPT void NotifyConnected();
  MOZ_CAN_RUN_SCRIPT void NotifyDisconnected();

  // Called when the PSerialPort child actor is destroyed externally (e.g.
  // by the parent closing the channel due to permission revocation).
  MOZ_CAN_RUN_SCRIPT void OnActorDestroyed();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Shutdown();

  void DisconnectFromOwner() override;

  RefPtr<SerialPortChild> GetChild() const { return mChild; }

  void SetChild(SerialPortChild* aChild) { mChild = aChild; }

  // Called after the pull algorithm errors aStream. Clears [[readable]] so a
  // later readable access or close() doesn't use the errored stream. aFatal
  // sets [[readFatal]] so that readable returns null until the port is
  // reopened.
  void HandleReadableErrored(ReadableStream* aStream, bool aFatal);

 private:
  ~SerialPort() override;

  enum class StreamCloseMode : uint8_t {
    // Cancel the readable and abort the writable, running their underlying
    // source/sink algorithms.
    Graceful,
    // Error both streams without running author-observable algorithms or
    // dispatching events.
    Forced,
    // Like Forced, but with the NetworkError the spec requires when the
    // device has been lost.
    Disconnected,
  };

  MOZ_CAN_RUN_SCRIPT ReadableStream* CreateReadableStream();
  MOZ_CAN_RUN_SCRIPT WritableStream* CreateWritableStream();
  void CloseAfterStreamsClosed();
  void SettleClosePromise(nsresult aResult);
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> CloseStreams(
      StreamCloseMode aMode);
  void UpdateWorkerRef();

  RefPtr<Serial> mSerial;
  IPCSerialPortInfo mInfo;
  State mState = State::Closed;
  // Whether the hardware device is physically connected to the system
  bool mPhysicallyPresent = true;
  bool mHasShutdown = false;
  bool mReadFatal = false;
  uint32_t mBufferSize = 0;
  uint32_t mPipeCapacity = 0;

  RefPtr<ReadableStream> mReadable;
  RefPtr<WritableStream> mWritable;
  RefPtr<SerialPortChild> mChild;

  RefPtr<Promise> mOpenPromise;
  RefPtr<Promise> mClosePromise;

  // Prevents the worker from shutting down while this port is open or
  // has connect/disconnect event listeners. Managed by UpdateWorkerRef().
  RefPtr<StrongWorkerRef> mWorkerRef;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_SerialPort_h
