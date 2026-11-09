/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "midirMIDIPlatformService.h"

#include "MIDILog.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/MIDIPlatformRunnables.h"
#include "mozilla/dom/MIDIPort.h"
#include "mozilla/dom/MIDIPortInterface.h"
#include "mozilla/dom/MIDIPortParent.h"
#include "mozilla/dom/MIDITypes.h"
#include "mozilla/dom/MIDIUtils.h"
#include "mozilla/dom/midi/midir_impl_ffi_generated.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsIThread.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

static_assert(sizeof(TimeStamp) == sizeof(GeckoTimeStamp));

/**
 * Runnable used for to send messages asynchronously on the I/O thread.
 */
class SendRunnable : public MIDIBackgroundRunnable {
 public:
  explicit SendRunnable(const nsAString& aPortID, const MIDIMessage& aMessage)
      : MIDIBackgroundRunnable("SendRunnable"),
        mPortID(aPortID),
        mMessage(aMessage) {}
  ~SendRunnable() = default;
  virtual void RunInternal() {
    MIDIPlatformService::AssertThread();
    if (!MIDIPlatformService::IsRunning()) {
      // Some send operations might outlive the service, bail out and do nothing
      return;
    }
    midirMIDIPlatformService* srv =
        static_cast<midirMIDIPlatformService*>(MIDIPlatformService::Get());
    srv->SendMessage(mPortID, mMessage);
  }

 private:
  nsString mPortID;
  MIDIMessage mMessage;
};

// static
StaticMutex midirMIDIPlatformService::gOwnerThreadMutex;

// static
constinit nsCOMPtr<nsISerialEventTarget> midirMIDIPlatformService::gOwnerThread;

midirMIDIPlatformService::midirMIDIPlatformService()
    : mImplementation(nullptr) {
  StaticMutexAutoLock lock(gOwnerThreadMutex);
  gOwnerThread = OwnerThread();
}

midirMIDIPlatformService::~midirMIDIPlatformService() {
  LOG("midir_impl_shutdown");
  if (mImplementation) {
    midir_impl_shutdown(mImplementation);
  }
  StaticMutexAutoLock lock(gOwnerThreadMutex);
  gOwnerThread = nullptr;
}

// static
void midirMIDIPlatformService::AddPort(const nsString* aId,
                                       const nsString* aName, bool aInput) {
  MIDIPortType type = aInput ? MIDIPortType::Input : MIDIPortType::Output;
  MIDIPortInfo port(*aId, *aName, u""_ns, u""_ns, type);
  MIDIPlatformService::Get()->AddPortInfo(port);
}

// static
void midirMIDIPlatformService::RemovePort(const nsString* aId,
                                          const nsString* aName, bool aInput) {
  MIDIPortType type = aInput ? MIDIPortType::Input : MIDIPortType::Output;
  MIDIPortInfo port(*aId, *aName, u""_ns, u""_ns, type);
  MIDIPlatformService::Get()->RemovePortInfo(port);
}

void midirMIDIPlatformService::Init() {
  if (mImplementation) {
    return;
  }

  mImplementation = midir_impl_init(AddPort);

  if (mImplementation) {
    MIDIPlatformService::Get()->SendPortList();
  } else {
    LOG("midir_impl_init failure");
  }
}

// static
void midirMIDIPlatformService::CheckAndReceive(const nsString* aId,
                                               const uint8_t* aData,
                                               size_t aLength,
                                               const GeckoTimeStamp* aTimeStamp,
                                               uint64_t aMicros) {
  nsTArray<uint8_t> data;
  data.AppendElements(aData, aLength);
  const TimeStamp* openTime = reinterpret_cast<const TimeStamp*>(aTimeStamp);
  TimeStamp timestamp = *openTime + TimeDuration::FromMicroseconds(aMicros);
  MIDIMessage message(data, timestamp);
  LogMIDIMessage(message, *aId, MIDIPortType::Input);
  nsTArray<MIDIMessage> messages;
  messages.AppendElement(message);

  nsCOMPtr<nsIRunnable> r(new ReceiveRunnable(*aId, messages));
  StaticMutexAutoLock lock(gOwnerThreadMutex);
  if (gOwnerThread) {
    gOwnerThread->Dispatch(r, NS_DISPATCH_NORMAL);
  }
}

void midirMIDIPlatformService::Refresh() {
  midir_impl_refresh(mImplementation, AddPort, RemovePort);
}

void midirMIDIPlatformService::Open(MIDIPortParent* aPort) {
  AssertThread();
  MOZ_ASSERT(aPort);
  nsString id = aPort->MIDIPortInterface::Id();
  TimeStamp openTimeStamp = TimeStamp::Now();
  if (midir_impl_open_port(mImplementation, &id,
                           reinterpret_cast<GeckoTimeStamp*>(&openTimeStamp),
                           CheckAndReceive)) {
    LOG("MIDI port open: %s at t=%lf", NS_ConvertUTF16toUTF8(id).get(),
        (openTimeStamp - TimeStamp::ProcessCreation()).ToSeconds());
    nsCOMPtr<nsIRunnable> r(new SetStatusRunnable(
        aPort, aPort->DeviceState(), MIDIPortConnectionState::Open));
    OwnerThread()->Dispatch(r.forget());
  } else {
    LOG("MIDI port open failed: %s", NS_ConvertUTF16toUTF8(id).get());
  }
}

void midirMIDIPlatformService::Stop() {
  // Nothing to do here AFAIK
}

void midirMIDIPlatformService::ScheduleSend(const nsAString& aPortId) {
  AssertThread();
  LOG("MIDI port schedule send %s", NS_ConvertUTF16toUTF8(aPortId).get());
  nsTArray<MIDIMessage> messages;
  GetMessages(aPortId, messages);
  TimeStamp now = TimeStamp::Now();
  for (const auto& message : messages) {
    if (message.timestamp().IsNull()) {
      SendMessage(aPortId, message);
    } else {
      double delay = (message.timestamp() - now).ToMilliseconds();
      if (delay < 1.0) {
        SendMessage(aPortId, message);
      } else {
        nsCOMPtr<nsIRunnable> r(new SendRunnable(aPortId, message));
        OwnerThread()->DelayedDispatch(r.forget(),
                                       static_cast<uint32_t>(delay));
      }
    }
  }
}

void midirMIDIPlatformService::ScheduleClose(MIDIPortParent* aPort) {
  AssertThread();
  MOZ_ASSERT(aPort);
  nsString id = aPort->MIDIPortInterface::Id();
  LOG("MIDI port schedule close %s", NS_ConvertUTF16toUTF8(id).get());
  if (aPort->ConnectionState() == MIDIPortConnectionState::Open) {
    midir_impl_close_port(mImplementation, &id);
    nsCOMPtr<nsIRunnable> r(new SetStatusRunnable(
        aPort, aPort->DeviceState(), MIDIPortConnectionState::Closed));
    OwnerThread()->Dispatch(r.forget());
  }
}

void midirMIDIPlatformService::SendMessage(const nsAString& aPortId,
                                           const MIDIMessage& aMessage) {
  LOG("MIDI send message on %s", NS_ConvertUTF16toUTF8(aPortId).get());
  LogMIDIMessage(aMessage, aPortId, MIDIPortType::Output);
  midir_impl_send(mImplementation, &aPortId, &aMessage.data());
}
