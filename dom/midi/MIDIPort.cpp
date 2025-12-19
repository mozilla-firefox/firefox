/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MIDIPort.h"

#include "MIDILog.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MIDIAccess.h"
#include "mozilla/dom/MIDIConnectionEvent.h"
#include "mozilla/dom/MIDIPortChild.h"
#include "mozilla/dom/MIDITypes.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsContentUtils.h"
#include "nsISupportsImpl.h"  // for MOZ_COUNT_CTOR, MOZ_COUNT_DTOR

using namespace mozilla::ipc;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(MIDIPort, DOMEventTargetHelper,
                                   mMIDIAccessParent, mOpeningPromise,
                                   mClosingPromise)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(MIDIPort, DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MIDIPort)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(MIDIPort, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(MIDIPort, DOMEventTargetHelper)

MIDIPort::MIDIPort(nsPIDOMWindowInner* aWindow)
    : DOMEventTargetHelper(aWindow), mKeepAlive(false) {
  MOZ_ASSERT(aWindow);

  if (Document* aDoc = aWindow->GetExtantDoc()) {
    aDoc->DisallowBFCaching();
  }
}

MIDIPort::~MIDIPort() {
  if (Port()) {
    // If the IPC port channel is still alive at this point, it means we're
    // probably CC'ing this port object. Send the shutdown message to also clean
    // up the IPC channel.
    Port()->SendShutdown();
  }
}

bool MIDIPort::Initialize(const MIDIPortInfo& aPortInfo, bool aSysexEnabled,
                          MIDIAccess* aMIDIAccessParent) {
  MOZ_ASSERT(aMIDIAccessParent);
  nsCOMPtr<Document> document = GetDocumentIfCurrent();
  if (!document) {
    return false;
  }

  nsCOMPtr<nsIURI> uri = document->GetDocumentURI();
  if (!uri) {
    return false;
  }

  nsAutoCString origin;
  nsresult rv = nsContentUtils::GetWebExposedOriginSerialization(uri, origin);
  if (NS_FAILED(rv)) {
    return false;
  }
  RefPtr<MIDIPortChild> port =
      new MIDIPortChild(aPortInfo, aSysexEnabled, this);
  if (NS_FAILED(port->GenerateStableId(origin))) {
    return false;
  }
  PBackgroundChild* b = BackgroundChild::GetForCurrentThread();
  MOZ_ASSERT(b,
             "Should always have a valid BackgroundChild when creating a port "
             "object!");

  // Create the endpoints and bind the one on the child side.
  Endpoint<PMIDIPortParent> parentEndpoint;
  Endpoint<PMIDIPortChild> childEndpoint;
  MOZ_ALWAYS_SUCCEEDS(
      PMIDIPort::CreateEndpoints(&parentEndpoint, &childEndpoint));
  MOZ_ALWAYS_TRUE(childEndpoint.Bind(port));

  if (!b->SendCreateMIDIPort(std::move(parentEndpoint), aPortInfo,
                             aSysexEnabled)) {
    return false;
  }

  mMIDIAccessParent = aMIDIAccessParent;
  mPortHolder.Init(port.forget());
  LOG("MIDIPort::Initialize (%s, %s)",
      NS_ConvertUTF16toUTF8(Port()->Name()).get(),
      GetEnumString(Port()->Type()).get());
  return true;
}

void MIDIPort::UnsetIPCPort() {
  LOG("MIDIPort::UnsetIPCPort (%s, %s)",
      NS_ConvertUTF16toUTF8(Port()->Name()).get(),
      GetEnumString(Port()->Type()).get());
  mPortHolder.Clear();
}

void MIDIPort::GetId(nsString& aRetVal) const {
  MOZ_ASSERT(Port());
  aRetVal = Port()->StableId();
}

void MIDIPort::GetManufacturer(nsString& aRetVal) const {
  MOZ_ASSERT(Port());
  aRetVal = Port()->Manufacturer();
}

void MIDIPort::GetName(nsString& aRetVal) const {
  MOZ_ASSERT(Port());
  aRetVal = Port()->Name();
}

void MIDIPort::GetVersion(nsString& aRetVal) const {
  MOZ_ASSERT(Port());
  aRetVal = Port()->Version();
}

MIDIPortType MIDIPort::Type() const {
  MOZ_ASSERT(Port());
  return Port()->Type();
}

MIDIPortConnectionState MIDIPort::Connection() const {
  MOZ_ASSERT(Port());
  return Port()->ConnectionState();
}

MIDIPortDeviceState MIDIPort::State() const {
  MOZ_ASSERT(Port());
  return Port()->DeviceState();
}

bool MIDIPort::SysexEnabled() const {
  MOZ_ASSERT(Port());
  return Port()->SysexEnabled();
}

already_AddRefed<Promise> MIDIPort::Open(ErrorResult& aError) {
  LOG("MIDIPort::Open");
  MOZ_ASSERT(Port());
  if (mOpeningPromise) {
    return do_AddRef(mOpeningPromise);
  }
  RefPtr<Promise> p = Promise::Create(GetOwnerGlobal(), aError);
  if (aError.Failed()) {
    return nullptr;
  }
  mOpeningPromise = p;
  Port()->SendOpen();
  return p.forget();
}

already_AddRefed<Promise> MIDIPort::Close(ErrorResult& aError) {
  LOG("MIDIPort::Close");
  MOZ_ASSERT(Port());
  if (mClosingPromise) {
    return do_AddRef(mClosingPromise);
  }
  RefPtr<Promise> p = Promise::Create(GetOwnerGlobal(), aError);
  if (aError.Failed()) {
    return nullptr;
  }
  mClosingPromise = p;
  Port()->SendClose();
  return p.forget();
}

void MIDIPort::FireStateChangeEvent() {
  if (!GetOwnerWindow()) {
    return;  // Ignore changes once we've been disconnected from the owner
  }

  StateChange();

  MOZ_ASSERT(Port());
  if (Port()->ConnectionState() == MIDIPortConnectionState::Open ||
      Port()->ConnectionState() == MIDIPortConnectionState::Pending) {
    if (mOpeningPromise) {
      mOpeningPromise->MaybeResolve(this);
      mOpeningPromise = nullptr;
    }
  } else if (Port()->ConnectionState() == MIDIPortConnectionState::Closed) {
    if (mOpeningPromise) {
      mOpeningPromise->MaybeReject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
      mOpeningPromise = nullptr;
    }
    if (mClosingPromise) {
      mClosingPromise->MaybeResolve(this);
      mClosingPromise = nullptr;
    }
  }

  if (Port()->DeviceState() == MIDIPortDeviceState::Connected &&
      Port()->ConnectionState() == MIDIPortConnectionState::Pending) {
    Port()->SendOpen();
  }

  if (Port()->ConnectionState() == MIDIPortConnectionState::Open ||
      (Port()->DeviceState() == MIDIPortDeviceState::Connected &&
       Port()->ConnectionState() == MIDIPortConnectionState::Pending)) {
    KeepAliveOnStatechange();
  } else {
    DontKeepAliveOnStatechange();
  }

  // Fire MIDIAccess events first so that the port is no longer in the port
  // maps.
  if (mMIDIAccessParent) {
    mMIDIAccessParent->FireConnectionEvent(this);
  }

  MIDIConnectionEventInit init;
  init.mPort = this;
  RefPtr<MIDIConnectionEvent> event(
      MIDIConnectionEvent::Constructor(this, u"statechange"_ns, init));
  DispatchTrustedEvent(event);
}

void MIDIPort::StateChange() {}

void MIDIPort::Receive(const nsTArray<MIDIMessage>& aMsg) {
  MOZ_CRASH("We should never get here!");
}

void MIDIPort::DisconnectFromOwner() {
  if (Port()) {
    Port()->SendClose();
  }
  DontKeepAliveOnStatechange();

  DOMEventTargetHelper::DisconnectFromOwner();
}

void MIDIPort::KeepAliveOnStatechange() {
  if (!mKeepAlive) {
    mKeepAlive = true;
    KeepAliveIfHasListenersFor(nsGkAtoms::onstatechange);
  }
}

void MIDIPort::DontKeepAliveOnStatechange() {
  if (mKeepAlive) {
    IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onstatechange);
    mKeepAlive = false;
  }
}

const nsString& MIDIPort::StableId() { return Port()->StableId(); }

}  // namespace mozilla::dom
