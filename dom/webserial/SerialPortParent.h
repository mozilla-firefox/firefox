/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SerialPortParent_h
#define mozilla_dom_SerialPortParent_h

#include "mozilla/dom/PSerialPortParent.h"
#include "mozilla/dom/SerialPlatformService.h"
#include "mozilla/ipc/DataPipe.h"
#include "nsIAsyncInputStream.h"
#include "nsISupports.h"

namespace mozilla::dom {

class SerialDeviceChangeProxy;

namespace webserial {
class SerialPortWritePump;
}  // namespace webserial

// Parent-side actor for a serial port. Bound to the SerialPlatformService IO
// thread so const that I/O operations can be performed directly without
// dispatching to a separate thread. Data flows through DataPipes; control
// messages (Drain, Flush, Close) use IPC. Note that non-const members are only
// safe to access on the SerialPlatformService's IOThread.
class SerialPortParent final : public PSerialPortParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SerialPortParent, override)

  mozilla::ipc::IPCResult RecvOpen(const IPCSerialOptions& aOptions,
                                   OpenResolver&& aResolver);
  mozilla::ipc::IPCResult RecvClose(CloseResolver&& aResolver);
  mozilla::ipc::IPCResult RecvSetSignals(const IPCSerialOutputSignals& aSignals,
                                         SetSignalsResolver&& aResolver);
  mozilla::ipc::IPCResult RecvGetSignals(GetSignalsResolver&& aResolver);
  mozilla::ipc::IPCResult RecvDrain(DrainResolver&& aResolver);
  mozilla::ipc::IPCResult RecvFlush(bool aReceive, FlushResolver&& aResolver);
  mozilla::ipc::IPCResult RecvAttachReadPipe(
      const RefPtr<mozilla::ipc::DataPipeSender>& aReadPipeSender);
  mozilla::ipc::IPCResult RecvAttachWritePipe(
      const RefPtr<mozilla::ipc::DataPipeReceiver>& aWritePipeReceiver);
  mozilla::ipc::IPCResult RecvClone(
      mozilla::ipc::Endpoint<PSerialPortParent>&& aEndpoint);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  bool PortIdMatches(const nsAString& aPortId) const {
    return mPortId == aPortId;
  }

  // Called by SerialManagerParent when device connection state changes.
  void NotifyConnected();
  void NotifyDisconnected();

  SerialPortParent(const nsString& aPortId, uint64_t aBrowserId,
                   SerialDeviceChangeProxy* aProxy = nullptr);

 private:
  ~SerialPortParent();

  // This should only be called if the port is closing.
  void StopPumpsBeforeClose();

  // aReason is the status the read pipe is closed with, which content sees
  // as either the end of the stream (NS_BASE_STREAM_CLOSED) or an error.
  void StopReadPump(nsresult aReason = NS_BASE_STREAM_CLOSED);
  void DestroyPlatformReader();
  void StopWritePump();
  // Should only be called when there's a transition from open->closed or
  // closed->open, as otherwise the sharing indicator can get out of sync.
  void NotifySharingStateChanged();
  void StartReadPump(
      already_AddRefed<mozilla::ipc::DataPipeSender> aReadPipeSender);
  void StartWritePump(
      already_AddRefed<mozilla::ipc::DataPipeReceiver> aWritePipeReceiver);

  const nsString mPortId;
  const uint64_t mBrowserId;
  bool mIsOpen = false;
  uint32_t mPipeCapacity = 0;
  // Whether the port was opened with parity checking enabled (even/odd). When
  // set, the read stream is wrapped to detect parity errors and surface them
  // as a ParityError on the readable stream.
  bool mDetectParityErrors = false;

  // DataPipe endpoints held by the parent: the read pump writes device data
  // to mReadPipeSender, and the write pump reads JS data from
  // mWritePipeReceiver.
  RefPtr<mozilla::ipc::DataPipeSender> mReadPipeSender;
  RefPtr<mozilla::ipc::DataPipeReceiver> mWritePipeReceiver;

  nsCOMPtr<nsIAsyncInputStream> mPlatformInputStream;
  // Opaque context from NS_AsyncCopy used to cancel the in-flight copy from
  // mPlatformInputStream to mReadPipeSender without closing
  // mPlatformInputStream.
  nsCOMPtr<nsISupports> mReadCopierCtx;

  RefPtr<webserial::SerialPortWritePump> mWritePump;

  RefPtr<SerialDeviceChangeProxy> mDeviceChangeProxy;

  // Cloned actors sharing the same underlying port. When we get a
  // connect/disconnect notification, we forward it to all clones too.
  nsTArray<RefPtr<SerialPortParent>> mClones;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_SerialPortParent_h
