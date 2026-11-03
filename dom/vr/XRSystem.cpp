/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XRSystem.h"

#include "VRDisplayClient.h"
#include "VRManagerChild.h"
#include "gfxVR.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BindingCallContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/PermissionsPolicyUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/XRPermissionRequest.h"
#include "mozilla/dom/XRSession.h"
#include "nsGlobalWindowInner.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

using namespace gfx;

////////////////////////////////////////////////////////////////////////////////
// XRSystem cycle collection
NS_IMPL_CYCLE_COLLECTION_CLASS(XRSystem)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(XRSystem,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mActiveImmersiveSession)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInlineSessions)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mIsSessionSupportedRequests)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mRequestSessionRequestsWaitingForRuntimeDetection)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRequestSessionRequestsWithoutHardware)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mRequestSessionRequestsWaitingForEnumeration)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(XRSystem, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mActiveImmersiveSession)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInlineSessions)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIsSessionSupportedRequests)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(
      mRequestSessionRequestsWaitingForRuntimeDetection)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRequestSessionRequestsWithoutHardware)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRequestSessionRequestsWaitingForEnumeration)
  // Don't need NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER because
  // DOMEventTargetHelper does it for us.
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(XRSystem, DOMEventTargetHelper)

JSObject* XRSystem::WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) {
  return XRSystem_Binding::Wrap(aCx, this, aGivenProto);
}

// static
already_AddRefed<XRSystem> XRSystem::Create(nsPIDOMWindowInner* aWindow) {
  MOZ_ASSERT(aWindow);

  RefPtr<XRSystem> service = new XRSystem(aWindow);
  return service.forget();
}

XRSystem::XRSystem(nsPIDOMWindowInner* aWindow)
    : DOMEventTargetHelper(aWindow),
      mShuttingDown(false),
      mPendingImmersiveSession(false),
      mEnumerationInFlight(false) {
  // Unregister with VRManagerChild
  VRManagerChild* vmc = VRManagerChild::Get();
  if (vmc) {
    vmc->AddListener(this);
  }
}

void XRSystem::Shutdown() {
  MOZ_ASSERT(!mShuttingDown);
  mShuttingDown = true;

  // Unregister from VRManagerChild
  if (VRManagerChild::IsCreated()) {
    VRManagerChild* vmc = VRManagerChild::Get();
    vmc->RemoveListener(this);
  }
}

void XRSystem::SessionEnded(XRSession* aSession) {
  if (mActiveImmersiveSession == aSession) {
    mActiveImmersiveSession = nullptr;
  }
  mInlineSessions.RemoveElement(aSession);
}

already_AddRefed<Promise> XRSystem::IsSessionSupported(XRSessionMode aMode,
                                                       ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  NS_ENSURE_TRUE(global, nullptr);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  NS_ENSURE_TRUE(!aRv.Failed(), nullptr);

  if (aMode == XRSessionMode::Inline) {
    promise->MaybeResolve(true);
    return promise.forget();
  }

  if (mIsSessionSupportedRequests.IsEmpty()) {
    gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
    vm->DetectRuntimes();
  }

  RefPtr<IsSessionSupportedRequest> request =
      new IsSessionSupportedRequest(aMode, promise);
  mIsSessionSupportedRequests.AppendElement(request);
  return promise.forget();
}

already_AddRefed<Promise> XRSystem::RequestSession(
    XRSessionMode aMode, const XRSessionInit& aOptions, CallerType aCallerType,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  NS_ENSURE_TRUE(global, nullptr);

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  NS_ENSURE_TRUE(!aRv.Failed(), nullptr);

  bool immersive = (aMode == XRSessionMode::Immersive_vr ||
                    aMode == XRSessionMode::Immersive_ar);

  // The document must be a responsible document, active and focused.
  nsCOMPtr<Document> responsibleDocument = GetDocumentIfCurrent();
  if (!responsibleDocument) {
    // The document is not trustworthy
    promise->MaybeRejectWithSecurityError("This document is not responsible.");
    return promise.forget();
  }

  if (immersive || aOptions.mRequiredFeatures.WasPassed() ||
      aOptions.mOptionalFeatures.WasPassed()) {
    if (!responsibleDocument->HasValidTransientUserGestureActivation() &&
        aCallerType != CallerType::System &&
        StaticPrefs::dom_vr_require_gesture()) {
      // A user gesture is required.
      promise->MaybeRejectWithSecurityError("A user gesture is required.");
      return promise.forget();
    }
  }

  nsTArray<XRReferenceSpaceType> requiredReferenceSpaceTypes;
  nsTArray<XRReferenceSpaceType> optionalReferenceSpaceTypes;

  /**
   * By default, all sessions will require XRReferenceSpaceType::Viewer
   * and immersive sessions will require XRReferenceSpaceType::Local.
   *
   * https://www.w3.org/TR/webxr/#default-features
   */
  requiredReferenceSpaceTypes.AppendElement(XRReferenceSpaceType::Viewer);
  if (immersive) {
    requiredReferenceSpaceTypes.AppendElement(XRReferenceSpaceType::Local);
  }

  if (aOptions.mRequiredFeatures.WasPassed()) {
    for (const nsString& val : aOptions.mRequiredFeatures.Value()) {
      Maybe<XRReferenceSpaceType> type =
          StringToEnum<XRReferenceSpaceType>(val);
      if (type.isNothing()) {
        promise->MaybeRejectWithNotSupportedError(
            "A required feature for the XRSession is not available.");
        return promise.forget();
      }
      requiredReferenceSpaceTypes.AppendElement(type.value());
    }
  }

  if (aOptions.mOptionalFeatures.WasPassed()) {
    for (const nsString& val : aOptions.mOptionalFeatures.Value()) {
      Maybe<XRReferenceSpaceType> type =
          StringToEnum<XRReferenceSpaceType>(val);
      if (type.isSome()) {
        optionalReferenceSpaceTypes.AppendElement(type.value());
      }
    }
  }

  if (immersive) {
    if (mPendingImmersiveSession || mActiveImmersiveSession) {
      promise->MaybeRejectWithInvalidStateError(
          "There can only be one immersive XRSession.");
      return promise.forget();
    }
    mPendingImmersiveSession = true;
  }

  bool isChromeSession = aCallerType == CallerType::System;
  uint32_t presentationGroup =
      isChromeSession ? gfx::kVRGroupChrome : gfx::kVRGroupContent;
  RefPtr<RequestSessionRequest> request = new RequestSessionRequest(
      aMode, presentationGroup, promise, requiredReferenceSpaceTypes,
      optionalReferenceSpaceTypes);
  if (request->WantsHardware()) {
    QueueSessionRequestWithEnumeration(request);
  } else {
    QueueSessionRequestWithoutEnumeration(request);
  }

  return promise.forget();
}

void XRSystem::QueueSessionRequestWithEnumeration(
    RequestSessionRequest* aRequest) {
  MOZ_ASSERT(aRequest->WantsHardware());
  mRequestSessionRequestsWaitingForRuntimeDetection.AppendElement(aRequest);
  gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
  vm->DetectRuntimes();
}

void XRSystem::QueueSessionRequestWithoutEnumeration(
    RequestSessionRequest* aRequest) {
  MOZ_ASSERT(!aRequest->NeedsHardware());
  mRequestSessionRequestsWithoutHardware.AppendElement(aRequest);

  ResolveSessionRequestsWithoutHardware();
}

bool XRSystem::CancelHardwareRequest(RequestSessionRequest* aRequest) {
  if (!aRequest->NeedsHardware()) {
    // If hardware access was an optional requirement and the user
    // opted not to provide access, queue the request
    // to be resolved without hardware.
    QueueSessionRequestWithoutEnumeration(aRequest);
    return false;
  }

  if (aRequest->IsImmersive()) {
    mPendingImmersiveSession = false;
  }
  return true;
}

bool XRSystem::OnXRPermissionRequestAllow() {
  if (!gfx::VRManagerChild::IsCreated()) {
    // It's possible that this callback returns after
    // we have already started shutting down.
    return false;
  }
  if (!mEnumerationInFlight) {
    mEnumerationInFlight = true;
    gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
    (void)vm->EnumerateVRDisplays();
  }
  return mEnumerationInFlight ||
         !mRequestSessionRequestsWaitingForEnumeration.IsEmpty();
}

void XRSystem::OnXRPermissionRequestCancel() {
  nsTArray<RefPtr<RequestSessionRequest>> requestSessionRequests(
      std::move(mRequestSessionRequestsWaitingForEnumeration));
  for (RefPtr<RequestSessionRequest>& request : requestSessionRequests) {
    if (CancelHardwareRequest(request)) {
      request->mPromise->MaybeRejectWithSecurityError(
          "A device supporting the requested session "
          "configuration could not be found.");
    }
  }
}

bool XRSystem::PermissionsPolicyBlocked() const {
  nsGlobalWindowInner* win = GetOwnerWindow();
  if (!win) {
    return true;
  }
  RefPtr<XRPermissionRequest> request =
      new XRPermissionRequest(win, win->WindowID());
  return !(request->CheckPermissionDelegate());
}

bool XRSystem::HasActiveImmersiveSession() const {
  return mActiveImmersiveSession;
}

void XRSystem::ResolveSessionRequestsWithoutHardware() {
  // Resolve promises returned by RequestSession
  nsTArray<RefPtr<gfx::VRDisplayClient>> displays;
  // Try resolving support without a device, for inline sessions.
  displays.AppendElement(nullptr);

  nsTArray<RefPtr<RequestSessionRequest>> requestSessionRequests(
      std::move(mRequestSessionRequestsWithoutHardware));

  ResolveSessionRequests(requestSessionRequests, displays);
}

void XRSystem::NotifyEnumerationCompleted() {
  // Enumeration has completed.
  mEnumerationInFlight = false;

  if (!gfx::VRManagerChild::IsCreated()) {
    // It's possible that this callback returns after
    // we have already started shutting down.
    return;
  }

  // Resolve promises returned by RequestSession
  nsTArray<RefPtr<gfx::VRDisplayClient>> displays;
  gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
  vm->GetVRDisplays(displays);

  nsTArray<RefPtr<RequestSessionRequest>> requestSessionRequests(
      std::move(mRequestSessionRequestsWaitingForEnumeration));

  ResolveSessionRequests(requestSessionRequests, displays);
}

void XRSystem::ResolveSessionRequests(
    nsTArray<RefPtr<RequestSessionRequest>>& aRequests,
    const nsTArray<RefPtr<gfx::VRDisplayClient>>& aDisplays) {
  for (RefPtr<RequestSessionRequest>& request : aRequests) {
    RefPtr<XRSession> session;
    if (request->IsImmersive()) {
      mPendingImmersiveSession = false;
    }
    // Select an XR device
    for (const RefPtr<gfx::VRDisplayClient>& display : aDisplays) {
      nsTArray<XRReferenceSpaceType> enabledReferenceSpaceTypes;
      if (request->ResolveSupport(display, enabledReferenceSpaceTypes)) {
        if (request->IsImmersive()) {
          session = XRSession::CreateImmersiveSession(
              GetOwnerWindow(), this, display, request->GetPresentationGroup(),
              enabledReferenceSpaceTypes);
          mActiveImmersiveSession = session;
        } else {
          session = XRSession::CreateInlineSession(GetOwnerWindow(), this,
                                                   enabledReferenceSpaceTypes);
          mInlineSessions.AppendElement(session);
        }
        request->mPromise->MaybeResolve(session);
        break;
      }
    }
    if (!session) {
      request->mPromise->MaybeRejectWithNotSupportedError(
          "A device supporting the required XRSession configuration "
          "could not be found.");
    }
  }
}

void XRSystem::NotifyDetectRuntimesCompleted() {
  ResolveIsSessionSupportedRequests();
  if (!mRequestSessionRequestsWaitingForRuntimeDetection.IsEmpty()) {
    ProcessSessionRequestsWaitingForRuntimeDetection();
  }
}

void XRSystem::ResolveIsSessionSupportedRequests() {
  // Resolve promises returned by IsSessionSupported
  gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();
  nsTArray<RefPtr<IsSessionSupportedRequest>> isSessionSupportedRequests(
      std::move(mIsSessionSupportedRequests));
  bool permissionsPolicyBlocked = PermissionsPolicyBlocked();

  for (RefPtr<IsSessionSupportedRequest>& request :
       isSessionSupportedRequests) {
    if (permissionsPolicyBlocked) {
      request->mPromise->MaybeRejectWithSecurityError(
          "The xr-spatial-tracking permissions policy is required.");
      continue;
    }

    bool supported = false;
    switch (request->GetSessionMode()) {
      case XRSessionMode::Immersive_vr:
        supported = vm->RuntimeSupportsVR();
        break;
      case XRSessionMode::Immersive_ar:
        supported = vm->RuntimeSupportsAR();
        break;
      default:
        break;
    }
    request->mPromise->MaybeResolve(supported);
  }
}

void XRSystem::ProcessSessionRequestsWaitingForRuntimeDetection() {
  bool alreadyRequestedPermission =
      !mRequestSessionRequestsWaitingForEnumeration.IsEmpty();
  bool permissionsPolicyBlocked = PermissionsPolicyBlocked();
  gfx::VRManagerChild* vm = gfx::VRManagerChild::Get();

  nsTArray<RefPtr<RequestSessionRequest>> sessionRequests(
      std::move(mRequestSessionRequestsWaitingForRuntimeDetection));

  for (RefPtr<RequestSessionRequest>& request : sessionRequests) {
    bool compatibleRuntime = false;
    switch (request->GetSessionMode()) {
      case XRSessionMode::Immersive_vr:
        compatibleRuntime = vm->RuntimeSupportsVR();
        break;
      case XRSessionMode::Immersive_ar:
        compatibleRuntime = vm->RuntimeSupportsAR();
        break;
      case XRSessionMode::Inline:
        compatibleRuntime = vm->RuntimeSupportsInline();
        break;
      default:
        break;
    }
    if (!compatibleRuntime) {
      // If none of the requested sessions are supported by a
      // runtime, early exit without showing a permission prompt.
      if (CancelHardwareRequest(request)) {
        request->mPromise->MaybeRejectWithNotSupportedError(
            "A device supporting the required XRSession configuration "
            "could not be found.");
      }
      continue;
    }
    if (permissionsPolicyBlocked) {
      // Don't show a permission prompt if blocked by permissions policy.
      if (CancelHardwareRequest(request)) {
        request->mPromise->MaybeRejectWithSecurityError(
            "The xr-spatial-tracking permissions policy is required.");
      }
      continue;
    }
    // To continue evaluating this request, it must wait for hardware
    // enumeration and permission request.
    mRequestSessionRequestsWaitingForEnumeration.AppendElement(request);
  }

  if (!mRequestSessionRequestsWaitingForEnumeration.IsEmpty() &&
      !alreadyRequestedPermission) {
    /**
     * Inline sessions will require only a user gesture
     * and should not trigger XR permission UI.
     * This is not a problem currently, as the only platforms
     * allowing xr-spatial-tracking for inline sessions do not
     * present a modal XR permission UI (e.g. Android).
     */
    GetOwnerWindow()->RequestXRPermission();
  }
}

void XRSystem::NotifyVRDisplayMounted(uint32_t aDisplayID) {}
void XRSystem::NotifyVRDisplayUnmounted(uint32_t aDisplayID) {}

void XRSystem::NotifyVRDisplayConnect(uint32_t aDisplayID) {
  DispatchTrustedEvent(u"devicechange"_ns);
}

void XRSystem::NotifyVRDisplayDisconnect(uint32_t aDisplayID) {
  DispatchTrustedEvent(u"devicechange"_ns);
}

void XRSystem::NotifyVRDisplayPresentChange(uint32_t aDisplayID) {}
void XRSystem::NotifyPresentationGenerationChanged(uint32_t aDisplayID) {
  if (mActiveImmersiveSession) {
    mActiveImmersiveSession->ExitPresent();
  }
}
bool XRSystem::GetStopActivityStatus() const { return true; }

RequestSessionRequest::RequestSessionRequest(
    XRSessionMode aSessionMode, uint32_t aPresentationGroup, Promise* aPromise,
    const nsTArray<XRReferenceSpaceType>& aRequiredReferenceSpaceTypes,
    const nsTArray<XRReferenceSpaceType>& aOptionalReferenceSpaceTypes)
    : mPromise(aPromise),
      mSessionMode(aSessionMode),
      mPresentationGroup(aPresentationGroup),
      mRequiredReferenceSpaceTypes(aRequiredReferenceSpaceTypes.Clone()),
      mOptionalReferenceSpaceTypes(aOptionalReferenceSpaceTypes.Clone()) {}

bool RequestSessionRequest::ResolveSupport(
    const gfx::VRDisplayClient* aDisplay,
    nsTArray<XRReferenceSpaceType>& aEnabledReferenceSpaceTypes) const {
  if (aDisplay) {
    if (!aDisplay->GetIsConnected()) {
      return false;
    }
    if ((aDisplay->GetDisplayInfo().GetPresentingGroups() &
         mPresentationGroup) != 0) {
      return false;
    }

    const gfx::VRDisplayInfo& info = aDisplay->GetDisplayInfo();
    switch (mSessionMode) {
      case XRSessionMode::Inline:
        if (!bool(info.mDisplayState.capabilityFlags &
                  gfx::VRDisplayCapabilityFlags::Cap_Inline)) {
          return false;
        }
        break;
      case XRSessionMode::Immersive_vr:
        if (!bool(info.mDisplayState.capabilityFlags &
                  gfx::VRDisplayCapabilityFlags::Cap_ImmersiveVR)) {
          return false;
        }
        break;
      case XRSessionMode::Immersive_ar:
        if (!bool(info.mDisplayState.capabilityFlags &
                  gfx::VRDisplayCapabilityFlags::Cap_ImmersiveAR)) {
          return false;
        }
        break;
      default:
        break;
    }
  } else if (mSessionMode != XRSessionMode::Inline) {
    // If we don't have a device, we can only support inline sessions
    return false;
  }

  // All sessions support XRReferenceSpaceType::Viewer by default
  aEnabledReferenceSpaceTypes.AppendElement(XRReferenceSpaceType::Viewer);

  // Immersive sessions support XRReferenceSpaceType::Local by default
  if (IsImmersive()) {
    aEnabledReferenceSpaceTypes.AppendElement(XRReferenceSpaceType::Local);
  }

  for (XRReferenceSpaceType type : mRequiredReferenceSpaceTypes) {
    if (aDisplay) {
      if (!aDisplay->IsReferenceSpaceTypeSupported(type)) {
        return false;
      }
    } else if (type != XRReferenceSpaceType::Viewer) {
      // If we don't have a device, We only support
      // XRReferenceSpaceType::Viewer
      return false;
    }
    if (!aEnabledReferenceSpaceTypes.Contains(type)) {
      aEnabledReferenceSpaceTypes.AppendElement(type);
    }
  }
  if (aDisplay) {
    for (XRReferenceSpaceType type : mOptionalReferenceSpaceTypes) {
      if (aDisplay->IsReferenceSpaceTypeSupported(type) &&
          !aEnabledReferenceSpaceTypes.Contains(type)) {
        aEnabledReferenceSpaceTypes.AppendElement(type);
      }
    }
  }
  return true;
}

bool RequestSessionRequest::IsImmersive() const {
  return (mSessionMode == XRSessionMode::Immersive_vr ||
          mSessionMode == XRSessionMode::Immersive_ar);
}

bool RequestSessionRequest::WantsHardware() const {
  for (XRReferenceSpaceType type : mOptionalReferenceSpaceTypes) {
    // Any XRReferenceSpaceType other than Viewer requires hardware
    if (type != XRReferenceSpaceType::Viewer) {
      return true;
    }
  }
  return NeedsHardware();
}

bool RequestSessionRequest::NeedsHardware() const {
  for (XRReferenceSpaceType type : mRequiredReferenceSpaceTypes) {
    // Any XRReferenceSpaceType other than Viewer requires hardware
    if (type != XRReferenceSpaceType::Viewer) {
      return true;
    }
  }
  return false;
}

XRSessionMode RequestSessionRequest::GetSessionMode() const {
  return mSessionMode;
}

uint32_t RequestSessionRequest::GetPresentationGroup() const {
  return mPresentationGroup;
}

////////////////////////////////////////////////////////////////////////////////
// IsSessionSupportedRequest cycle collection
NS_IMPL_CYCLE_COLLECTION(IsSessionSupportedRequest, mPromise)

XRSessionMode IsSessionSupportedRequest::GetSessionMode() const {
  return mSessionMode;
}

////////////////////////////////////////////////////////////////////////////////
// XRRequestSessionPermissionRequest cycle collection
NS_IMPL_CYCLE_COLLECTION_INHERITED(XRRequestSessionPermissionRequest,
                                   ContentPermissionRequestBase)

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(
    XRRequestSessionPermissionRequest, ContentPermissionRequestBase)

XRRequestSessionPermissionRequest::XRRequestSessionPermissionRequest(
    nsPIDOMWindowInner* aWindow, nsIPrincipal* aNodePrincipal,
    AllowCallback&& aAllowCallback,
    AllowAnySiteCallback&& aAllowAnySiteCallback,
    CancelCallback&& aCancelCallback)
    : ContentPermissionRequestBase(aNodePrincipal, aWindow, "dom.xr"_ns,
                                   "xr"_ns),
      mAllowCallback(std::move(aAllowCallback)),
      mAllowAnySiteCallback(std::move(aAllowAnySiteCallback)),
      mCancelCallback(std::move(aCancelCallback)),
      mCallbackCalled(false) {
  mPermissionRequests.AppendElement(
      PermissionRequest(mType, nsTArray<nsString>()));
}

XRRequestSessionPermissionRequest::~XRRequestSessionPermissionRequest() {
  Cancel();
}

NS_IMETHODIMP
XRRequestSessionPermissionRequest::Cancel() {
  if (!mCallbackCalled) {
    mCallbackCalled = true;
    mCancelCallback();
  }
  return NS_OK;
}

NS_IMETHODIMP
XRRequestSessionPermissionRequest::Allow(JS::Handle<JS::Value> aChoices) {
  nsTArray<PermissionChoice> choices;
  nsresult rv = TranslateChoices(aChoices, mPermissionRequests, choices);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // There is no support to allow grants automatically from the prompting code
  // path.

  if (!mCallbackCalled) {
    mCallbackCalled = true;
    if (choices.Length() == 1 &&
        choices[0].choice().EqualsLiteral("allow-on-any-site")) {
      mAllowAnySiteCallback();
    } else if (choices.Length() == 1 &&
               choices[0].choice().EqualsLiteral("allow")) {
      mAllowCallback();
    }
  }
  return NS_OK;
}

already_AddRefed<XRRequestSessionPermissionRequest>
XRRequestSessionPermissionRequest::Create(
    nsPIDOMWindowInner* aWindow, AllowCallback&& aAllowCallback,
    AllowAnySiteCallback&& aAllowAnySiteCallback,
    CancelCallback&& aCancelCallback) {
  if (!aWindow) {
    return nullptr;
  }
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(aWindow);
  if (!win->GetPrincipal()) {
    return nullptr;
  }
  RefPtr<XRRequestSessionPermissionRequest> request =
      new XRRequestSessionPermissionRequest(
          aWindow, win->GetPrincipal(), std::move(aAllowCallback),
          std::move(aAllowAnySiteCallback), std::move(aCancelCallback));
  return request.forget();
}

////////////////////////////////////////////////////////////////////////////////
// RequestSessionRequest cycle collection
NS_IMPL_CYCLE_COLLECTION(RequestSessionRequest, mPromise)

}  // namespace mozilla::dom
