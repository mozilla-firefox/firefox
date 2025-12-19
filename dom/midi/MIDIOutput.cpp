/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MIDIOutput.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MIDIOutputBinding.h"
#include "mozilla/dom/MIDIPortChild.h"
#include "mozilla/dom/MIDITypes.h"
#include "mozilla/dom/MIDIUtils.h"
#include "mozilla/dom/Performance.h"
#include "nsDOMNavigationTiming.h"
#include "nsGlobalWindowInner.h"

namespace mozilla::dom {

MIDIOutput::MIDIOutput(nsPIDOMWindowInner* aWindow) : MIDIPort(aWindow) {}

// static
RefPtr<MIDIOutput> MIDIOutput::Create(nsPIDOMWindowInner* aWindow,
                                      MIDIAccess* aMIDIAccessParent,
                                      const MIDIPortInfo& aPortInfo,
                                      const bool aSysexEnabled) {
  MOZ_ASSERT(static_cast<MIDIPortType>(aPortInfo.type()) ==
             MIDIPortType::Output);
  RefPtr<MIDIOutput> port = new MIDIOutput(aWindow);
  if (NS_WARN_IF(
          !port->Initialize(aPortInfo, aSysexEnabled, aMIDIAccessParent))) {
    return nullptr;
  }
  return port;
}

JSObject* MIDIOutput::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return MIDIOutput_Binding::Wrap(aCx, this, aGivenProto);
}

void MIDIOutput::Send(const Sequence<uint8_t>& aData,
                      const Optional<double>& aTimestamp, ErrorResult& aRv) {
  if (Port()->DeviceState() == MIDIPortDeviceState::Disconnected) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  // The timestamp passed to us is a DOMHighResTimestamp, which is in relation
  // to the start of navigation timing. This needs to be turned into a
  // TimeStamp before it hits the platform specific MIDI service.
  //
  // If timestamp is either not set or zero, set timestamp to now and send the
  // message ASAP.
  TimeStamp timestamp;
  if (aTimestamp.WasPassed() && aTimestamp.Value() != 0) {
    nsCOMPtr<Document> doc = GetOwnerWindow()->GetDoc();
    if (!doc) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }
    TimeDuration ts_diff = TimeDuration::FromMilliseconds(aTimestamp.Value());
    timestamp = GetOwnerWindow()
                    ->GetPerformance()
                    ->GetDOMTiming()
                    ->GetNavigationStartTimeStamp() +
                ts_diff;
  } else {
    timestamp = TimeStamp::Now();
  }

  nsTArray<MIDIMessage> msgArray;
  bool ret = MIDIUtils::ParseMessages(aData, timestamp, msgArray);
  if (!ret) {
    aRv.ThrowTypeError("Invalid MIDI message");
    return;
  }

  if (msgArray.IsEmpty()) {
    aRv.ThrowTypeError("Empty message array");
    return;
  }

  // TODO Move this check back to parse message so we don't have to iterate
  // twice.
  if (!SysexEnabled()) {
    for (auto& msg : msgArray) {
      if (MIDIUtils::IsSysexMessage(msg)) {
        aRv.Throw(NS_ERROR_DOM_NOT_ALLOWED_ERR);
        return;
      }
    }
  }
  Port()->SendSend(msgArray);
}

void MIDIOutput::Clear() {
  if (Port()->ConnectionState() == MIDIPortConnectionState::Closed) {
    return;
  }
  Port()->SendClear();
}

}  // namespace mozilla::dom
