/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TestSerialPlatformService.h"

#include "SerialLogging.h"
#include "nsIPipe.h"

namespace mozilla::dom {

TestSerialPlatformService::TestSerialPlatformService() {
  AddDefaultMockPorts();
}

TestSerialPlatformService* TestSerialPlatformService::AsTestService() {
  return this;
}

void TestSerialPlatformService::Shutdown() {
  if (IsShutdown()) {
    return;
  }
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("TestSerialPlatformService[%p]::Shutdown", this));
  mMockPorts.Clear();
  SerialPlatformService::Shutdown();
}

void TestSerialPlatformService::AddDefaultMockPorts() {
  AddMockDevice(u"test-device-1"_ns, u"/dev/ttyUSB0"_ns, 0x2341, 0x0043);
  AddMockDevice(u"test-device-2"_ns, u"/dev/ttyUSB1"_ns, 0x0403, 0x6002);
  AddMockDevice(u"test-device-3"_ns, u"/dev/ttyACM0"_ns, 0x1a86, 0x7523);
  // A default Bluetooth Serial Port Profile (SPP) device so that filter
  // tests can exercise the bluetoothServiceClassId code path.
  AddMockDevice(u"test-bt-spp"_ns, u"/dev/rfcomm0"_ns, 0, 0,
                u"00001101-0000-1000-8000-00805f9b34fb"_ns);
}

nsresult TestSerialPlatformService::EnumeratePortsImpl(
    SerialPortList& aPorts, bool* aLikelyAccessDenied) {
  aPorts.Clear();

  for (const auto& port : mMockPorts) {
    aPorts.AppendElement(port.mInfo);
  }

  return NS_OK;
}

nsresult TestSerialPlatformService::OpenImpl(const nsString& aPortId,
                                             const IPCSerialOptions& aOptions) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (port->mIsOpen) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  nsCOMPtr<nsIAsyncInputStream> reader;
  nsCOMPtr<nsIAsyncOutputStream> writer;
  NS_NewPipe2(getter_AddRefs(reader), getter_AddRefs(writer),
              /* nonBlockingInput */ true, /* nonBlockingOutput */ true, 4096,
              UINT32_MAX);
  port->mPipeReadStream = reader;
  port->mPipeWriteStream = writer;

  port->mIsOpen = true;
  port->mOptions = aOptions;
  return NS_OK;
}

nsresult TestSerialPlatformService::CloseImpl(const nsString& aPortId) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!port->mIsOpen) {
    return NS_OK;
  }

  port->mIsOpen = false;
  if (port->mPipeWriteStream) {
    port->mPipeWriteStream->Close();
    port->mPipeWriteStream = nullptr;
  }
  port->mPipeReadStream = nullptr;
  return NS_OK;
}

nsresult TestSerialPlatformService::WriteImpl(const nsString& aPortId,
                                              Span<const uint8_t> aData) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port || !port->mIsOpen) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aData.IsEmpty()) {
    return NS_OK;
  }

  if (!port->mPipeWriteStream) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  uint32_t totalWritten = 0;
  while (totalWritten < aData.Length()) {
    uint32_t written = 0;
    nsresult rv = port->mPipeWriteStream->Write(
        reinterpret_cast<const char*>(aData.Elements() + totalWritten),
        aData.Length() - totalWritten, &written);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (written == 0) {
      return NS_ERROR_FAILURE;
    }
    totalWritten += written;
  }

  return NS_OK;
}

nsresult TestSerialPlatformService::DrainImpl(const nsString& aPortId) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port || !port->mIsOpen) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

nsresult TestSerialPlatformService::FlushImpl(const nsString& aPortId,
                                              bool aReceive) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port || !port->mIsOpen) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Don't recreate the pipe pair on flush. PlatformPipeReader holds a
  // duplicate of the read end and persists across flush/reattach cycles;
  // recreating the pair would leave it reading from a stale pipe.
  // Any buffered data is drained by SerialPortParent::StartReadPump.

  return NS_OK;
}

nsresult TestSerialPlatformService::SetSignalsImpl(
    const nsString& aPortId, const IPCSerialOutputSignals& aSignals) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port || !port->mIsOpen) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aSignals.dataTerminalReady().isSome()) {
    port->mOutputSignals.dataTerminalReady() = aSignals.dataTerminalReady();
  }
  if (aSignals.requestToSend().isSome()) {
    port->mOutputSignals.requestToSend() = aSignals.requestToSend();
  }
  if (aSignals.breakSignal().isSome()) {
    port->mOutputSignals.breakSignal() = aSignals.breakSignal();
  }
  return NS_OK;
}

nsresult TestSerialPlatformService::GetSignalsImpl(
    const nsString& aPortId, IPCSerialInputSignals& aSignals) {
  MockSerialPort* port = FindPort(aPortId);
  if (!port || !port->mIsOpen) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  aSignals = IPCSerialInputSignals{
      port->mOutputSignals.dataTerminalReady().valueOr(
          false),                                           // dataCarrierDetect
      port->mOutputSignals.requestToSend().valueOr(false),  // clearToSend
      false,                                                // ringIndicator
      port->mOutputSignals.dataTerminalReady().valueOr(false)  // dataSetReady
  };

  return NS_OK;
}

nsresult TestSerialPlatformService::GetReadStreamImpl(
    const nsString& aPortId, uint32_t aBufferSize, bool aDetectParityErrors,
    nsIAsyncInputStream** aStream) {
  // The mock returns its pipe stream directly; parity-error detection is only
  // exercised on real platform services.
  MockSerialPort* port = FindPort(aPortId);
  if (!port || !port->mIsOpen) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (!port->mPipeReadStream) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsCOMPtr<nsIAsyncInputStream> stream = port->mPipeReadStream;
  stream.forget(aStream);
  return NS_OK;
}

MockSerialPort TestSerialPlatformService::CreateMockPort(
    const nsString& aId, const nsString& aPath, uint16_t aVendorId,
    uint16_t aProductId, const nsString& aBluetoothServiceClassId) {
  MockSerialPort port;
  port.mInfo.id() = aId;
  port.mInfo.path() = aPath;
  port.mInfo.friendlyName() = aId;
  if (aBluetoothServiceClassId.IsEmpty()) {
    port.mInfo.usbVendorId() = Some(aVendorId);
    port.mInfo.usbProductId() = Some(aProductId);
  } else {
    port.mInfo.bluetoothServiceClassId() = Some(aBluetoothServiceClassId);
  }
  return port;
}

void TestSerialPlatformService::AddMockDevice(
    const nsString& aId, const nsString& aPath, uint16_t aVendorId,
    uint16_t aProductId, const nsString& aBluetoothServiceClassId) {
  mMockPorts.AppendElement(CreateMockPort(aId, aPath, aVendorId, aProductId,
                                          aBluetoothServiceClassId));
}

MockSerialPort* TestSerialPlatformService::FindPort(const nsString& aPortId) {
  for (auto& port : mMockPorts) {
    if (port.mInfo.id() == aPortId) {
      return &port;
    }
  }
  return nullptr;
}

void TestSerialPlatformService::SimulateDeviceConnection(
    const nsString& aId, const nsString& aPath, uint16_t aVendorId,
    uint16_t aProductId, const nsString& aBluetoothServiceClassId) {
  MockSerialPort port = CreateMockPort(aId, aPath, aVendorId, aProductId,
                                       aBluetoothServiceClassId);
  IPCSerialPortInfo info = port.mInfo;
  mMockPorts.AppendElement(std::move(port));
  NotifyPortConnected(info);
}

void TestSerialPlatformService::SimulateDeviceDisconnection(
    const nsString& aId) {
  // Like a real unplug, end the device's data stream before the
  // device-change notification arrives.
  if (MockSerialPort* port = FindPort(aId); port && port->mPipeWriteStream) {
    port->mPipeWriteStream->Close();
  }
  mMockPorts.RemoveElementsBy(
      [&aId](const MockSerialPort& port) { return port.mInfo.id() == aId; });
  NotifyPortDisconnected(aId);
}

void TestSerialPlatformService::RemoveAllMockDevices() {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("TestSerialPlatformService::RemoveAllMockDevices removing %zu "
           "devices",
           mMockPorts.Length()));

  // Notify disconnection for each device before removing
  for (const auto& mockPort : mMockPorts) {
    NotifyPortDisconnected(mockPort.mInfo.id());
  }

  mMockPorts.Clear();
}

void TestSerialPlatformService::ResetToDefaultMockDevices() {
  MOZ_LOG(gWebSerialLog, LogLevel::Info,
          ("TestSerialPlatformService::ResetToDefaultMockDevices"));
  RemoveAllMockDevices();
  AddDefaultMockPorts();
}
}  // namespace mozilla::dom
