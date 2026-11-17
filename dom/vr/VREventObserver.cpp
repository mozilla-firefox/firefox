/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VREventObserver.h"

#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"

namespace mozilla::dom {

using namespace gfx;

/**
 * This class is used by nsGlobalWindowInner to implement
 * window.onvrdisplayactivate, window.onvrdisplaydeactivate,
 * window.onvrdisplayconnected, window.onvrdisplaydisconnected, and
 * window.onvrdisplaypresentchange.
 */
VREventObserver::VREventObserver(nsGlobalWindowInner* aGlobalWindow)
    : mWindow(aGlobalWindow), mStopActivity(false) {
  MOZ_ASSERT(aGlobalWindow);

  VRManagerChild* vmc = VRManagerChild::Get();
  if (vmc) {
    vmc->AddListener(this);
  }
}

VREventObserver::~VREventObserver() { DisconnectFromOwner(); }

void VREventObserver::DisconnectFromOwner() {
  // In the event that nsGlobalWindowInner is deallocated, VREventObserver may
  // still be AddRef'ed elsewhere.  Ensure that we don't UAF by
  // dereferencing mWindow.
  mWindow = nullptr;

  // Unregister from VRManagerChild
  if (VRManagerChild::IsCreated()) {
    VRManagerChild* vmc = VRManagerChild::Get();
    vmc->RemoveListener(this);
  }
  mStopActivity = true;
}

void VREventObserver::StartActivity() {
  mStopActivity = false;
  VRManagerChild* vmc = VRManagerChild::Get();
  vmc->StartActivity();
}

void VREventObserver::StopActivity() {
  mStopActivity = true;
  VRManagerChild* vmc = VRManagerChild::Get();
  vmc->StopActivity();
}

bool VREventObserver::GetStopActivityStatus() const { return mStopActivity; }

void VREventObserver::NotifyAfterLoad() {
  if (VRManagerChild::IsCreated()) {
    VRManagerChild* vmc = VRManagerChild::Get();
    vmc->FireDOMVRDisplayConnectEventsForLoad(this);
  }
}

void VREventObserver::NotifyVRDisplayMounted(uint32_t aDisplayID) {}

void VREventObserver::NotifyVRDisplayNavigation(uint32_t aDisplayID) {}

void VREventObserver::NotifyVRDisplayRequested(uint32_t aDisplayID) {}

void VREventObserver::NotifyVRDisplayUnmounted(uint32_t aDisplayID) {}

void VREventObserver::NotifyVRDisplayConnect(uint32_t aDisplayID) {}

void VREventObserver::NotifyVRDisplayDisconnect(uint32_t aDisplayID) {}

void VREventObserver::NotifyVRDisplayPresentChange(uint32_t aDisplayID) {}

void VREventObserver::NotifyPresentationGenerationChanged(uint32_t aDisplayID) {
}

void VREventObserver::NotifyEnumerationCompleted() {}

void VREventObserver::NotifyDetectRuntimesCompleted() {
  if (mWindow && mWindow->IsCurrentInnerWindow()) {
    MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
    mWindow->NotifyDetectXRRuntimesCompleted();
  }
}

}  // namespace mozilla::dom
