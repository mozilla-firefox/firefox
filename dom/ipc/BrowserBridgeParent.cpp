/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessibleParent.h"
#  include "nsAccessibilityService.h"
#endif

#include "mozilla/Monitor.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/dom/BrowserBridgeParent.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/InputAPZContext.h"

using namespace mozilla::ipc;
using namespace mozilla::layout;
using namespace mozilla::hal;

namespace mozilla::dom {

BrowserBridgeParent::BrowserBridgeParent() = default;

BrowserBridgeParent::~BrowserBridgeParent() { Destroy(); }

nsresult BrowserBridgeParent::InitWithProcess(
    BrowserParent* aParentBrowser, ContentParent* aContentParent,
    const WindowGlobalInit& aWindowInit, uint32_t aChromeFlags, TabId aTabId) {
  MOZ_ASSERT(!CanSend(),
             "This should be called before the object is connected to IPC");
  MOZ_DIAGNOSTIC_ASSERT(!aContentParent->IsLaunching());
  MOZ_DIAGNOSTIC_ASSERT(!aContentParent->IsDead());

  RefPtr<CanonicalBrowsingContext> browsingContext =
      CanonicalBrowsingContext::Get(aWindowInit.context().mBrowsingContextId);
  if (!browsingContext || browsingContext->IsDiscarded()) {
    return NS_ERROR_UNEXPECTED;
  }
  if (!browsingContext->Group()->IsKnownForChildID(
          aParentBrowser->OtherChildID())) {
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !browsingContext->GetBrowserParent(),
      "BrowsingContext must have had previous BrowserParent cleared");

  MOZ_DIAGNOSTIC_ASSERT(
      aParentBrowser->Manager() != aContentParent,
      "Cannot create OOP iframe in the same process as its parent document");

  // Unfortunately, due to the current racy destruction of BrowsingContext
  // instances when Fission is enabled, while `browsingContext` may not be
  // discarded, an ancestor might be.
  //
  // A discarded ancestor will cause us issues when creating our `BrowserParent`
  // in the new content process, so abort the attempt if we have one.
  //
  // FIXME: We should never have a non-discarded BrowsingContext with discarded
  // ancestors. (bug 1634759)
  if (NS_WARN_IF(!browsingContext->AncestorsAreCurrent())) {
    return NS_ERROR_UNEXPECTED;
  }

  // Ensure that our content process is subscribed to our newly created
  // BrowsingContextGroup.
  browsingContext->Group()->EnsureHostProcess(aContentParent);
  browsingContext->SetOwnerProcessId(aContentParent->ChildID());

  browsingContext->Group()->NotifyFocusedOrActiveBrowsingContextToProcess(
      aContentParent);

  // Construct the BrowserParent object for our subframe.
  auto browserParent = MakeRefPtr<BrowserParent>(
      aContentParent, aTabId, *aParentBrowser, browsingContext, aChromeFlags);
  browserParent->SetBrowserBridgeParent(this);

  ContentProcessManager* cpm = ContentProcessManager::GetSingleton();
  if (!cpm) {
    return NS_ERROR_UNEXPECTED;
  }
  cpm->RegisterRemoteFrame(browserParent);

  // Open a remote endpoint for our PBrowser actor.
  ManagedEndpoint<PBrowserChild> childEp =
      aContentParent->OpenPBrowserEndpoint(browserParent);
  if (NS_WARN_IF(!childEp.IsValid())) {
    MOZ_ASSERT(false, "Browser Open Endpoint Failed");
    return NS_ERROR_FAILURE;
  }

  RefPtr<WindowGlobalParent> windowParent =
      WindowGlobalParent::CreateDisconnected(aWindowInit, aContentParent);
  if (!windowParent) {
    return NS_ERROR_UNEXPECTED;
  }

  ManagedEndpoint<PWindowGlobalChild> windowChildEp =
      browserParent->OpenPWindowGlobalEndpoint(windowParent);
  if (NS_WARN_IF(!windowChildEp.IsValid())) {
    MOZ_ASSERT(false, "WindowGlobal Open Endpoint Failed");
    return NS_ERROR_FAILURE;
  }

  MOZ_DIAGNOSTIC_ASSERT(!browsingContext->IsDiscarded(),
                        "bc cannot have become discarded");

  // Tell the content process to set up its PBrowserChild.
  bool ok = aContentParent->SendConstructBrowser(
      std::move(childEp), std::move(windowChildEp), aTabId,
      browserParent->AsIPCTabContext(), aWindowInit, aChromeFlags,
      aContentParent->ChildID(), aContentParent->IsForBrowser(),
      /* aIsTopLevel */ false);
  if (NS_WARN_IF(!ok)) {
    MOZ_ASSERT(false, "Browser Constructor Failed");
    return NS_ERROR_FAILURE;
  }

  // Set our BrowserParent object to the newly created browser.
  mBrowserParent = std::move(browserParent);
  mBrowserParent->SetOwnerElement(aParentBrowser->GetOwnerElement());
  mBrowserParent->InitRendering();

  GetBrowsingContext()->SetCurrentBrowserParent(mBrowserParent);

  windowParent->Init();
  return NS_OK;
}

CanonicalBrowsingContext* BrowserBridgeParent::GetBrowsingContext() {
  return mBrowserParent->GetBrowsingContext();
}

BrowserParent* BrowserBridgeParent::Manager() {
  MOZ_ASSERT(CanSend());
  return mozilla::ipc::ActorCast<BrowserParent>(
      PBrowserBridgeParent::Manager());
}

void BrowserBridgeParent::Destroy() {
  if (mBrowserParent) {
#ifdef ACCESSIBILITY
    if (mEmbedderAccessibleDoc && !mEmbedderAccessibleDoc->IsShutdown()) {
      mEmbedderAccessibleDoc->RemovePendingOOPChildDoc(this);
    }
#endif
    mBrowserParent->Destroy();
    mBrowserParent->SetBrowserBridgeParent(nullptr);
    mBrowserParent = nullptr;
  }
  if (CanSend()) {
    (void)Send__delete__(this);
  }
}

IPCResult BrowserBridgeParent::RecvShow(const OwnerShowInfo& aOwnerInfo) {
  mBrowserParent->AttachWindowRenderer();
  (void)mBrowserParent->SendShow(mBrowserParent->GetShowInfo(), aOwnerInfo);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvScrollbarPreferenceChanged(
    ScrollbarPreference aPref) {
  (void)mBrowserParent->SendScrollbarPreferenceChanged(aPref);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvLoadURL(nsDocShellLoadState* aLoadState) {
  (void)mBrowserParent->SendLoadURL(WrapNotNull(aLoadState),
                                    mBrowserParent->GetShowInfo());
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvResumeLoad(uint64_t aPendingSwitchID) {
  mBrowserParent->ResumeLoad(aPendingSwitchID);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvUpdateDimensions(
    const LayoutDeviceIntRect& aRect, const LayoutDeviceIntSize& aSize) {
  mBrowserParent->UpdateDimensions(aRect, aSize);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvUpdateEffects(const EffectsInfo& aEffects) {
  (void)mBrowserParent->SendUpdateEffects(aEffects);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvUpdateRemotePrintSettings(
    const embedding::PrintData& aPrintData) {
  (void)mBrowserParent->SendUpdateRemotePrintSettings(aPrintData);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvRenderLayers(const bool& aEnabled) {
  (void)mBrowserParent->SendRenderLayers(aEnabled);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvNavigateByKey(
    const bool& aForward, const bool& aForDocumentNavigation) {
  (void)mBrowserParent->SendNavigateByKey(aForward, aForDocumentNavigation);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvBeginDestroy() {
  Destroy();
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvDispatchSynthesizedMouseEvent(
    const WidgetMouseEvent& aEvent) {
  if (aEvent.mMessage != eMouseMove ||
      aEvent.mReason != WidgetMouseEvent::eSynthesized) {
    return IPC_FAIL(this, "Unexpected event type");
  }

  nsCOMPtr<nsIWidget> widget = Manager()->GetWidget();
  if (!widget) {
    return IPC_OK();
  }

  WidgetMouseEvent event = aEvent;
  event.mWidget = std::move(widget);
  // Convert mRefPoint from the dispatching child process coordinate space
  // to the parent coordinate space. The SendRealMouseEvent call will convert
  // it into the dispatchee child process coordinate space
  event.mRefPoint = Manager()->TransformChildToParent(event.mRefPoint);
  // We need to set up an InputAPZContext on the stack because
  // BrowserParent::SendRealMouseEvent requires one. But the only thing in
  // that context that is actually used in this scenario is the layers id,
  // and we already have that on the mouse event.
  layers::InputAPZContext context(
      layers::ScrollableLayerGuid(event.mLayersId, 0,
                                  layers::ScrollableLayerGuid::NULL_SCROLL_ID),
      0, nsEventStatus_eIgnore);
  mBrowserParent->SendRealMouseEvent(event);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvWillChangeProcess() {
  (void)mBrowserParent->SendWillChangeProcess();
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvActivate(uint64_t aActionId) {
  mBrowserParent->Activate(aActionId);
  return IPC_OK();
}

IPCResult BrowserBridgeParent::RecvDeactivate(const bool& aWindowLowering,
                                              uint64_t aActionId) {
  mBrowserParent->Deactivate(aWindowLowering, aActionId);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserBridgeParent::RecvUpdateRemoteStyle(
    const StyleImageRendering& aImageRendering) {
  (void)mBrowserParent->SendUpdateRemoteStyle(aImageRendering);
  return IPC_OK();
}

#ifdef ACCESSIBILITY
a11y::DocAccessibleParent* BrowserBridgeParent::GetDocAccessibleParent() {
  auto* embeddedBrowser = GetBrowserParent();
  if (!embeddedBrowser) {
    return nullptr;
  }
  a11y::DocAccessibleParent* docAcc =
      embeddedBrowser->GetTopLevelDocAccessible();
  return docAcc && !docAcc->IsShutdown() ? docAcc : nullptr;
}

IPCResult BrowserBridgeParent::RecvSetEmbedderAccessible(
    PDocAccessibleParent* aDoc, uint64_t aID) {
#  if defined(ANDROID)
  MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
#  endif
  if (!aDoc && !mEmbedderAccessibleDoc) {
    return IPC_FAIL(this, "Embedder doc shouldn't be cleared if it wasn't set");
  }
  if (mEmbedderAccessibleDoc && aDoc && mEmbedderAccessibleDoc != aDoc) {
    return IPC_FAIL(this,
                    "Embedder doc shouldn't change from one doc to another");
  }
  if (aDoc && aDoc->Manager() != Manager()) {
    return IPC_FAIL(this, "Embedder doc not managed by our PBrowser");
  }
  if (!aDoc && mEmbedderAccessibleDoc &&
      !mEmbedderAccessibleDoc->IsShutdown()) {
    // We're clearing the embedder doc, so remove the pending child doc addition
    // (if any).
    mEmbedderAccessibleDoc->RemovePendingOOPChildDoc(this);
  }
  mEmbedderAccessibleDoc = static_cast<a11y::DocAccessibleParent*>(aDoc);
  mEmbedderAccessibleID = aID;
  if (!aDoc) {
    if (aID) {
      return IPC_FAIL(this, "Attempt to clear embedder but id given");
    }
    return IPC_OK();
  }
  if (!aID) {
    return IPC_FAIL(this, "Attempt to set embedder without id");
  }
  if (GetDocAccessibleParent()) {
    // The embedded DocAccessibleParent has already been created. This can
    // happen if, for example, an iframe is hidden and then shown or an iframe
    // Accessible is re-created. In the case of re-creation, the old iframe
    // Accessible still exists at this point because this IPDL message is
    // received *before* we receive the accessibility hide and show events. This
    // is okay; DocAccessibleParent will store this as a pending OOP child
    // document and add it when the new OuterDocAccessible arrives.
    mEmbedderAccessibleDoc->AddChildDoc(this);
  }
  return IPC_OK();
}

a11y::DocAccessibleParent* BrowserBridgeParent::GetEmbedderAccessibleDoc() {
  return mEmbedderAccessibleDoc && !mEmbedderAccessibleDoc->IsShutdown()
             ? mEmbedderAccessibleDoc.get()
             : nullptr;
}
#endif

void BrowserBridgeParent::ActorDestroy(ActorDestroyReason aWhy) { Destroy(); }

}  // namespace mozilla::dom
