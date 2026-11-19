/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SerialPortParent.h"

#include "SerialLogging.h"
#include "SerialPortPumps.h"
#include "mozilla/Services.h"
#include "mozilla/dom/SerialManagerParent.h"
#include "mozilla/dom/SerialPlatformService.h"
#include "mozilla/dom/SerialPortIPCTypes.h"
#include "mozilla/ipc/DataPipe.h"
#include "mozilla/ipc/Endpoint.h"
#include "nsHashPropertyBag.h"
#include "nsIObserverService.h"
#include "nsStreamUtils.h"

namespace mozilla::dom {

namespace {

// NS_AsyncCopy completion callback for the read pump. aClosure is a strong
// reference to the DataPipeSender, transferred from StartReadPump.
void OnReadCopyComplete(void* aClosure, nsresult aStatus) {
  RefPtr<mozilla::ipc::DataPipeSender> sender =
      dont_AddRef(static_cast<mozilla::ipc::DataPipeSender*>(aClosure));
  // The device stream never ends on its own while the port is usable, so
  // anything other than a cancellation or a parity error means the device
  // was lost. Closing with NS_OK would surface as {done: true} in content.
  if (aStatus != NS_BASE_STREAM_CLOSED &&
      aStatus != NS_ERROR_DOM_SERIAL_PARITY_ERROR) {
    aStatus = NS_ERROR_DOM_NETWORK_ERR;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("OnReadCopyComplete closing read pipe with 0x%08x",
           static_cast<uint32_t>(aStatus)));
  sender->CloseWithStatus(aStatus);
}

}  // namespace

SerialPortParent::SerialPortParent(const nsString& aPortId, uint64_t aBrowserId,
                                   SerialDeviceChangeProxy* aProxy)
    : mPortId(aPortId), mBrowserId(aBrowserId), mDeviceChangeProxy(aProxy) {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p] created for port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));
}

SerialPortParent::~SerialPortParent() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortParent[%p] destroyed for port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));
}

mozilla::ipc::IPCResult SerialPortParent::RecvOpen(
    const IPCSerialOptions& aOptions, OpenResolver&& aResolver) {
  if (mIsOpen) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("SerialPortParent[%p]::RecvOpen failed: port '%s' already open",
             this, NS_ConvertUTF16toUTF8(mPortId).get()));
    aResolver(NS_ERROR_ALREADY_INITIALIZED);
    return IPC_OK();
  }

  if (aOptions.bufferSize() > kMaxSerialBufferSize) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Warning,
        ("SerialPortParent[%p]::RecvOpen rejecting oversized bufferSize "
         "%u for port '%s'",
         this, aOptions.bufferSize(), NS_ConvertUTF16toUTF8(mPortId).get()));
    return IPC_FAIL(this, "bufferSize exceeds maximum");
  }

  MOZ_LOG(
      gWebSerialLog, LogLevel::Info,
      ("SerialPortParent[%p]::RecvOpen opening port '%s' (baudRate=%u, "
       "dataBits=%u, stopBits=%u, parity=%u, bufferSize=%u, flowControl=%u)",
       this, NS_ConvertUTF16toUTF8(mPortId).get(), aOptions.baudRate(),
       aOptions.dataBits(), aOptions.stopBits(),
       static_cast<unsigned>(aOptions.parity()), aOptions.bufferSize(),
       static_cast<unsigned>(aOptions.flowControl())));

  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  nsresult rv = NS_ERROR_FAILURE;

  if (service) {
    service->AssertIsOnIOThread();

    // Validate portId against enumerated ports before opening.
    bool portFound = false;
    SerialPortList ports;
    nsresult enumRv = service->EnumeratePorts(ports);
    if (NS_SUCCEEDED(enumRv)) {
      for (const auto& portInfo : ports) {
        if (portInfo.id() == mPortId) {
          portFound = true;
          break;
        }
      }
    }

    if (portFound) {
      rv = service->Open(mPortId, aOptions);
    } else {
      MOZ_LOG(gWebSerialLog, LogLevel::Warning,
              ("SerialPortParent[%p]::RecvOpen portId '%s' not found in "
               "enumerated ports, rejecting open",
               this, NS_ConvertUTF16toUTF8(mPortId).get()));
      rv = NS_ERROR_NOT_AVAILABLE;
    }
  }

  if (NS_FAILED(rv)) {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("SerialPortParent[%p]::RecvOpen failed for port '%s': 0x%08x", this,
         NS_ConvertUTF16toUTF8(mPortId).get(), static_cast<uint32_t>(rv)));
    aResolver(rv);
    return IPC_OK();
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p]::RecvOpen succeeded for port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));

  mIsOpen = true;
  NotifySharingStateChanged();
  mPipeCapacity = std::max(aOptions.bufferSize(), kMinSerialPortPumpSize);
  mDetectParityErrors = aOptions.parity() != ParityType::None;

  aResolver(NS_OK);
  return IPC_OK();
}

mozilla::ipc::IPCResult SerialPortParent::RecvClose(CloseResolver&& aResolver) {
  if (!mIsOpen) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("SerialPortParent[%p]::RecvClose: port '%s' already closed", this,
             NS_ConvertUTF16toUTF8(mPortId).get()));
    aResolver(NS_OK);
    return IPC_OK();
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p]::RecvClose closing port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));

  StopPumpsBeforeClose();

  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  nsresult rv = NS_ERROR_FAILURE;
  if (service) {
    service->AssertIsOnIOThread();
    rv = service->Close(mPortId);
  }

  if (NS_SUCCEEDED(rv)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Info,
            ("SerialPortParent[%p]::RecvClose succeeded for port '%s'", this,
             NS_ConvertUTF16toUTF8(mPortId).get()));
  } else {
    MOZ_LOG(
        gWebSerialLog, LogLevel::Error,
        ("SerialPortParent[%p]::RecvClose failed for port '%s': 0x%08x", this,
         NS_ConvertUTF16toUTF8(mPortId).get(), static_cast<uint32_t>(rv)));
  }
  mIsOpen = false;
  NotifySharingStateChanged();

  aResolver(rv);
  return IPC_OK();
}

void SerialPortParent::StopReadPump(nsresult aReason) {
  if (mReadCopierCtx) {
    MOZ_LOG(gWebSerialLog, LogLevel::Debug,
            ("SerialPortParent[%p]::StopReadPump cancelling copy for port '%s' "
             "(reason=0x%08x)",
             this, NS_ConvertUTF16toUTF8(mPortId).get(),
             static_cast<uint32_t>(aReason)));
    NS_CancelAsyncCopy(mReadCopierCtx, aReason);
    mReadCopierCtx = nullptr;
  }
  // mPlatformInputStream is kept alive: on Windows, IOCP associates the kernel
  // file object permanently, so re-registering a new duplicate handle fails.
  // The reader is only destroyed in DestroyPlatformReader (port close).
}

void SerialPortParent::DestroyPlatformReader() {
  if (!mPlatformInputStream) {
    return;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortParent[%p]::DestroyPlatformReader calling "
           "CloseWithStatus for port '%s'",
           this, NS_ConvertUTF16toUTF8(mPortId).get()));
  // PlatformPipeReader cancels pending I/O and dispatches the handle close
  // to a background thread with NS_DISPATCH_EVENT_MAY_BLOCK, so USB-serial
  // driver cleanup cannot block the IPC I/O thread.
  mPlatformInputStream->CloseWithStatus(NS_ERROR_ABORT);
  mPlatformInputStream = nullptr;
}

void SerialPortParent::StopWritePump() {
  if (mWritePump) {
    mWritePump->Stop();
    mWritePump = nullptr;
  }
  if (mWritePipeReceiver) {
    mWritePipeReceiver->CloseWithStatus(NS_BASE_STREAM_CLOSED);
    mWritePipeReceiver = nullptr;
  }
}

void SerialPortParent::StartReadPump(
    already_AddRefed<mozilla::ipc::DataPipeSender> aReadPipeSender) {
  mReadPipeSender = aReadPipeSender;

  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  if (!service) {
    return;
  }

  if (!mPlatformInputStream) {
    uint32_t bufferSize = std::max(mPipeCapacity, kMinSerialPortPumpSize);
    nsresult rv =
        service->GetReadStream(mPortId, bufferSize, mDetectParityErrors,
                               getter_AddRefs(mPlatformInputStream));
    if (NS_FAILED(rv) || !mPlatformInputStream) {
      MOZ_LOG(gWebSerialLog, LogLevel::Error,
              ("SerialPortParent[%p]::StartReadPump GetReadStream failed for "
               "port '%s': 0x%08x",
               this, NS_ConvertUTF16toUTF8(mPortId).get(),
               static_cast<uint32_t>(rv)));
      // The port is open, so failing to read from it means the device was
      // lost, e.g. it was unplugged before this pipe was attached.
      mReadPipeSender->CloseWithStatus(NS_ERROR_DOM_NETWORK_ERR);
      mReadPipeSender = nullptr;
      return;
    }
  }

  // aCloseSource=false: PlatformPipeReader must outlive the copy because its
  // IOCP registration is permanent per file object on Windows, so we reuse
  // it across flush/reattach cycles.
  // aCloseSink=false: OnReadCopyComplete closes the sink so that it can map
  // the copy's completion status to the error content should see.
  RefPtr<mozilla::ipc::DataPipeSender> closureSender = mReadPipeSender;
  nsresult rv =
      NS_AsyncCopy(mPlatformInputStream, mReadPipeSender, service->IOThread(),
                   NS_ASYNCCOPY_VIA_READSEGMENTS, 4096, OnReadCopyComplete,
                   closureSender.get(), /* aCloseSource = */ false,
                   /* aCloseSink = */ false, getter_AddRefs(mReadCopierCtx));
  if (NS_SUCCEEDED(rv)) {
    // Ownership is transferred to OnReadCopyComplete.
    (void)closureSender.forget().take();
  } else {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("SerialPortParent[%p]::StartReadPump NS_AsyncCopy failed for "
             "port '%s': 0x%08x",
             this, NS_ConvertUTF16toUTF8(mPortId).get(),
             static_cast<uint32_t>(rv)));
    mReadPipeSender->CloseWithStatus(rv);
    mReadPipeSender = nullptr;
    return;
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p]::StartReadPump started async read for "
           "port '%s' (pipeCapacity=%u)",
           this, NS_ConvertUTF16toUTF8(mPortId).get(), mPipeCapacity));
}

void SerialPortParent::StartWritePump(
    already_AddRefed<mozilla::ipc::DataPipeReceiver> aWritePipeReceiver) {
  mWritePipeReceiver = aWritePipeReceiver;

  mWritePump =
      MakeRefPtr<webserial::SerialPortWritePump>(mPortId, mWritePipeReceiver);
  mWritePump->Start();
}

mozilla::ipc::IPCResult SerialPortParent::RecvAttachReadPipe(
    const RefPtr<mozilla::ipc::DataPipeSender>& aReadPipeSender) {
  if (!mIsOpen || !aReadPipeSender) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("SerialPortParent[%p]::RecvAttachReadPipe: port '%s' not open",
             this, NS_ConvertUTF16toUTF8(mPortId).get()));
    // Content only attaches a read pipe while the port is open, so the port
    // closing in the meantime means the device was disconnected.
    if (aReadPipeSender) {
      aReadPipeSender->CloseWithStatus(NS_ERROR_DOM_NETWORK_ERR);
    }
    return IPC_OK();
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p]::RecvAttachReadPipe for port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));

  // Clean up any existing read pipe/pump from a previous readable stream.
  StopReadPump();

  RefPtr<mozilla::ipc::DataPipeSender> sender = aReadPipeSender;
  StartReadPump(sender.forget());

  return IPC_OK();
}

mozilla::ipc::IPCResult SerialPortParent::RecvAttachWritePipe(
    const RefPtr<mozilla::ipc::DataPipeReceiver>& aWritePipeReceiver) {
  if (!mIsOpen || !aWritePipeReceiver) {
    MOZ_LOG(gWebSerialLog, LogLevel::Warning,
            ("SerialPortParent[%p]::RecvAttachWritePipe: port '%s' not open",
             this, NS_ConvertUTF16toUTF8(mPortId).get()));
    if (aWritePipeReceiver) {
      aWritePipeReceiver->CloseWithStatus(NS_ERROR_NOT_AVAILABLE);
    }
    return IPC_OK();
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p]::RecvAttachWritePipe for port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));

  // Clean up any existing write pipe/pump from a previous writable stream.
  StopWritePump();

  RefPtr<mozilla::ipc::DataPipeReceiver> receiver = aWritePipeReceiver;
  StartWritePump(receiver.forget());

  return IPC_OK();
}

mozilla::ipc::IPCResult SerialPortParent::RecvSetSignals(
    const IPCSerialOutputSignals& aSignals, SetSignalsResolver&& aResolver) {
  if (!mIsOpen) {
    aResolver(NS_ERROR_NOT_AVAILABLE);
    return IPC_OK();
  }

  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  nsresult rv = NS_ERROR_FAILURE;
  if (service) {
    service->AssertIsOnIOThread();
    rv = service->SetSignals(mPortId, aSignals);
  }

  aResolver(rv);
  return IPC_OK();
}

mozilla::ipc::IPCResult SerialPortParent::RecvGetSignals(
    GetSignalsResolver&& aResolver) {
  IPCSerialInputSignals signals(
      /* dataCarrierDetect */ false, /* clearToSend */ false,
      /* ringIndicator */ false, /* dataSetReady */ false);

  if (!mIsOpen) {
    aResolver(std::tuple(NS_ERROR_NOT_AVAILABLE, signals));
    return IPC_OK();
  }

  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  nsresult rv = NS_ERROR_FAILURE;
  if (service) {
    service->AssertIsOnIOThread();
    rv = service->GetSignals(mPortId, signals);
  }

  aResolver(std::tuple(rv, signals));
  return IPC_OK();
}

mozilla::ipc::IPCResult SerialPortParent::RecvDrain(DrainResolver&& aResolver) {
  if (!mIsOpen) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("SerialPortParent[%p]::RecvDrain failed: port '%s' not open", this,
             NS_ConvertUTF16toUTF8(mPortId).get()));
    aResolver(NS_ERROR_NOT_AVAILABLE);
    return IPC_OK();
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortParent[%p]::RecvDrain draining buffers for port '%s'",
           this, NS_ConvertUTF16toUTF8(mPortId).get()));

  // There is no ordering guarantee between DataPipe notifications and IPC
  // messages. The child closes its DataPipeSender before sending Drain, but
  // the DataPipe closure notification may not have reached the parent yet,
  // meaning the write pump may still have unconsumed data. We must wait for
  // the write pipe to be fully closed (all data written to the device)
  // before draining OS transmit buffers.
  auto completeDrain = [portId = mPortId, aResolver = std::move(aResolver)]() {
    RefPtr<SerialPlatformService> service =
        SerialPlatformService::GetInstance();
    nsresult rv = NS_ERROR_FAILURE;
    if (service) {
      service->AssertIsOnIOThread();
      rv = service->Drain(portId);
    }
    aResolver(rv);
  };

  if (mWritePump) {
    mWritePump->OnPipeClosed(
        NS_NewRunnableFunction("DrainAfterPipeClosed", completeDrain));
    return IPC_OK();
  }
  completeDrain();
  return IPC_OK();
}

mozilla::ipc::IPCResult SerialPortParent::RecvFlush(bool aReceive,
                                                    FlushResolver&& aResolver) {
  if (!mIsOpen) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("SerialPortParent[%p]::RecvFlush failed: port '%s' not open", this,
             NS_ConvertUTF16toUTF8(mPortId).get()));
    aResolver(NS_ERROR_NOT_AVAILABLE);
    return IPC_OK();
  }

  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortParent[%p]::RecvFlush flushing %s buffers for port '%s'",
           this, aReceive ? "receive" : "transmit",
           NS_ConvertUTF16toUTF8(mPortId).get()));

  if (aReceive) {
    StopReadPump();
  } else {
    StopWritePump();
  }

  RefPtr<SerialPlatformService> service = SerialPlatformService::GetInstance();
  nsresult rv = NS_ERROR_FAILURE;
  if (service) {
    service->AssertIsOnIOThread();
    rv = service->Flush(mPortId, aReceive);
  }

  aResolver(rv);
  return IPC_OK();
}

void SerialPortParent::NotifySharingStateChanged() {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SerialPortParent::NotifySharingStateChanged",
      [browserId = mBrowserId, connected = mIsOpen]() {
        nsCOMPtr<nsIObserverService> obs =
            mozilla::services::GetObserverService();
        if (!obs) {
          return;
        }

        auto props = MakeRefPtr<nsHashPropertyBag>();
        props->SetPropertyAsUint64(u"browserId"_ns, browserId);
        props->SetPropertyAsBool(u"connected"_ns, connected);

        obs->NotifyObservers(static_cast<nsIPropertyBag2*>(props),
                             "serial-device-state-changed", nullptr);
      }));
}

mozilla::ipc::IPCResult SerialPortParent::RecvClone(
    mozilla::ipc::Endpoint<PSerialPortParent>&& aEndpoint) {
  if (!aEndpoint.IsValid()) {
    return IPC_FAIL(this, "Invalid endpoint in RecvClone");
  }

  auto actor = MakeRefPtr<SerialPortParent>(mPortId, mBrowserId);
  if (!aEndpoint.Bind(actor)) {
    MOZ_LOG(gWebSerialLog, LogLevel::Error,
            ("SerialPortParent[%p]::RecvClone failed to bind for port '%s'",
             this, NS_ConvertUTF16toUTF8(mPortId).get()));
    return IPC_OK();
  }

  mClones.AppendElement(actor);
  return IPC_OK();
}

void SerialPortParent::NotifyConnected() {
  if (CanSend()) {
    (void)SendConnected();
  }
  for (const auto& clone : mClones) {
    clone->NotifyConnected();
  }
}

void SerialPortParent::NotifyDisconnected() {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("SerialPortParent[%p]::NotifyDisconnected for port '%s'", this,
           NS_ConvertUTF16toUTF8(mPortId).get()));

  if (mIsOpen) {
    StopReadPump(NS_ERROR_DOM_NETWORK_ERR);
    StopPumpsBeforeClose();
    mIsOpen = false;
    NotifySharingStateChanged();
    RefPtr<SerialPlatformService> service =
        SerialPlatformService::GetInstance();
    if (service) {
      service->Close(mPortId);
    }
  }

  if (CanSend()) {
    (void)SendDisconnected();
  }
  for (const auto& clone : mClones) {
    clone->NotifyDisconnected();
  }
}

void SerialPortParent::StopPumpsBeforeClose() {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortParent[%p]::StopPumpsBeforeClose for port '%s' "
           "(hasReader=%d, "
           "hasCopier=%d, hasWritePump=%d)",
           this, NS_ConvertUTF16toUTF8(mPortId).get(), !!mPlatformInputStream,
           !!mReadCopierCtx, !!mWritePump));
  StopReadPump();
  DestroyPlatformReader();
  StopWritePump();
}

void SerialPortParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_LOG(gWebSerialLog, LogLevel::Debug,
          ("SerialPortParent[%p]::ActorDestroy for port '%s' (reason: %d)",
           this, NS_ConvertUTF16toUTF8(mPortId).get(), (int)aWhy));

  if (mDeviceChangeProxy) {
    mDeviceChangeProxy->RemovePortActor(this);
    mDeviceChangeProxy = nullptr;
  }

  StopPumpsBeforeClose();

  if (mIsOpen) {
    RefPtr<SerialPlatformService> service =
        SerialPlatformService::GetInstance();
    if (service) {
      service->Close(mPortId);
    }
    mIsOpen = false;
  }

  // Covers the case where the port was still open when the actor went away
  // (e.g. content process crash or iframe navigation) so the browser sharing
  // indicator count stays in sync.
  NotifySharingStateChanged();

  nsTArray<RefPtr<SerialPortParent>> clones = std::move(mClones);
  for (const auto& clone : clones) {
    if (clone->CanSend()) {
      clone->Close();
    }
  }
}

}  // namespace mozilla::dom
