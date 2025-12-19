/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MIDIAccessManager.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPrefs_midi.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FeaturePolicyUtils.h"
#include "mozilla/dom/MIDIAccess.h"
#include "mozilla/dom/MIDIManagerChild.h"
#include "mozilla/dom/MIDIPermissionRequest.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsIGlobalObject.h"

using namespace mozilla::ipc;

namespace mozilla::dom {

namespace {
// Singleton object for MIDIAccessManager
StaticRefPtr<MIDIAccessManager> gMIDIAccessManager;
}  // namespace

MIDIAccessManager::MIDIAccessManager() : mHasPortList(false), mChild(nullptr) {}

MIDIAccessManager::~MIDIAccessManager() = default;

// static
MIDIAccessManager* MIDIAccessManager::Get() {
  if (!gMIDIAccessManager) {
    gMIDIAccessManager = new MIDIAccessManager();
    ClearOnShutdown(&gMIDIAccessManager);
  }
  return gMIDIAccessManager;
}

// static
bool MIDIAccessManager::IsRunning() { return !!gMIDIAccessManager; }

already_AddRefed<Promise> MIDIAccessManager::RequestMIDIAccess(
    nsPIDOMWindowInner* aWindow, const MIDIOptions& aOptions,
    ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);
  nsCOMPtr<nsIGlobalObject> go = do_QueryInterface(aWindow);
  RefPtr<Promise> p = Promise::Create(go, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  nsCOMPtr<Document> doc = aWindow->GetDoc();
  if (NS_WARN_IF(!doc)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

#ifndef MOZ_WEBMIDI_MIDIR_IMPL
  if (!StaticPrefs::midi_testing()) {
    // If we don't have a MIDI implementation and testing is disabled we can't
    // allow accessing WebMIDI. However we don't want to return something
    // different from a normal rejection because we don't want websites to use
    // the error as a way to fingerprint users, so we throw a security error
    // as if the request had been rejected by the user.
    aRv.ThrowNotAllowedError("Access not allowed");
    return nullptr;
  }
#endif

  if (!FeaturePolicyUtils::IsFeatureAllowed(doc, u"midi"_ns)) {
    aRv.Throw(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return nullptr;
  }

  nsCOMPtr<nsIRunnable> permRunnable =
      new MIDIPermissionRequest(aWindow, p, aOptions);
  aRv = NS_DispatchToMainThread(permRunnable);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  return p.forget();
}

bool MIDIAccessManager::AddObserver(Observer<MIDIPortList>* aObserver) {
  // Add observer before we start the service, otherwise we can end up with
  // device lists being received before we have observers to send them to.
  mChangeObservers.AddObserver(aObserver);

  if (!mChild) {
    StartActor();
  } else {
    mChild->SendRefresh();
  }

  return true;
}

// Sets up the actor to talk to the parent.
//
// We Bootstrap the actor manually rather than using a constructor so that
// we can bind the parent endpoint to a dedicated task queue.
void MIDIAccessManager::StartActor() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mChild);

  // Grab PBackground.
  ::mozilla::ipc::PBackgroundChild* pBackground =
      BackgroundChild::GetOrCreateForCurrentThread();

  // Create the endpoints and bind the one on the child side.
  Endpoint<PMIDIManagerParent> parentEndpoint;
  Endpoint<PMIDIManagerChild> childEndpoint;
  MOZ_ALWAYS_SUCCEEDS(
      PMIDIManager::CreateEndpoints(&parentEndpoint, &childEndpoint));
  mChild = new MIDIManagerChild();
  MOZ_ALWAYS_TRUE(childEndpoint.Bind(mChild));

  // Kick over to the parent to connect things over there.
  pBackground->SendCreateMIDIManager(std::move(parentEndpoint));
}

void MIDIAccessManager::RemoveObserver(Observer<MIDIPortList>* aObserver) {
  mChangeObservers.RemoveObserver(aObserver);
  if (mChangeObservers.Length() == 0) {
    // If we're out of listeners, go ahead and shut down. Make sure to cleanup
    // the IPDL protocol also.
    if (mChild) {
      mChild->Shutdown();
      mChild = nullptr;
    }
    gMIDIAccessManager = nullptr;
  }
}

void MIDIAccessManager::SendRefresh() {
  if (mChild) {
    mChild->SendRefresh();
  }
}

void MIDIAccessManager::CreateMIDIAccess(nsPIDOMWindowInner* aWindow,
                                         bool aNeedsSysex, Promise* aPromise) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aPromise);
  RefPtr<MIDIAccess> a(new MIDIAccess(aWindow, aNeedsSysex, aPromise));
  if (NS_WARN_IF(!AddObserver(a))) {
    aPromise->MaybeReject(NS_ERROR_FAILURE);
    return;
  }
  if (!mHasPortList) {
    // Hold the access object until we get a connected device list.
    mAccessHolder.AppendElement(a);
  } else {
    // If we already have a port list, just send it to the MIDIAccess object now
    // so it can prepopulate its device list and resolve the promise.
    a->Notify(mPortList);
  }
}

void MIDIAccessManager::Update(const MIDIPortList& aPortList) {
  mPortList = aPortList;
  mChangeObservers.Broadcast(aPortList);
  if (!mHasPortList) {
    mHasPortList = true;
    // Now that we've broadcast the already-connected port list, content
    // should manage the lifetime of the MIDIAccess object, so we can clear the
    // keep-alive array.
    mAccessHolder.Clear();
  }
}

}  // namespace mozilla::dom
