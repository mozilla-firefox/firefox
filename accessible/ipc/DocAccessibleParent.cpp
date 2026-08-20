/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocAccessibleParent.h"

#include "ARIAMap.h"
#include "CacheConstants.h"
#include "CachedTableAccessible.h"
#ifdef MOZ_ENABLE_SKIA_PDF
#  include "mozilla/a11y/PdfStructTreeBuilder.h"
#endif
#include "Relation.h"
#include "RootAccessible.h"
#include "TextRange.h"
#include "mozilla/Components.h"  // for mozilla::components
#include "mozilla/PerfStats.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/a11y/Platform.h"
#include "mozilla/dom/BrowserBridgeParent.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentParent.h"
#include "nsAccUtils.h"
#include "nsAccessibilityService.h"
#include "nsIIOService.h"
#include "xpcAccEvents.h"
#include "xpcAccessibleDocument.h"

#if defined(XP_WIN)
#  include "Compatibility.h"
#  include "nsWinUtils.h"
#endif

#if defined(ANDROID)
#  define ACQUIRE_ANDROID_LOCK \
    MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
#else
#  define ACQUIRE_ANDROID_LOCK \
    do {                       \
    } while (0);
#endif

namespace mozilla {

namespace a11y {
uint64_t DocAccessibleParent::sMaxDocID = 0;

DocAccessibleParent::DocAccessibleParent()
    : RemoteAccessible(this),
#if defined(XP_WIN)
      mEmulatedWindowHandle(nullptr),
#endif  // defined(XP_WIN)
#ifdef MOZ_WIDGET_COCOA
      mFocusedAccBounds(Nothing()),
#endif
      mTopLevel(false),
      mTopLevelInContentProcess(false),
      mShutdown(false),
      mFocus(0),
      mCaretId(0),
      mCaretOffset(-1),
      mIsCaretAtEndOfLine(false) {
  sMaxDocID++;
  mActorID = sMaxDocID;
  MOZ_ASSERT(!LiveDocs().Get(mActorID));
  LiveDocs().InsertOrUpdate(mActorID, this);
}

DocAccessibleParent::~DocAccessibleParent() {
  UnregisterWeakMemoryReporter(this);
  LiveDocs().Remove(mActorID);
  MOZ_ASSERT(mChildDocs.Length() == 0);
  MOZ_ASSERT(!ParentDoc());
}

uint64_t DocAccessibleParent::EffectiveCacheDomains() const {
  if (dom::CanonicalBrowsingContext* bc = GetBrowsingContext()) {
    if (bc->Top()->GetIsPrinting()) {
      return kPdfCacheDomains;
    }
  }
  return nsAccessibilityService::GetActiveCacheDomains();
}

bool DocAccessibleParent::RequestDomainsIfInactive(
    uint64_t aRequiredCacheDomains) {
  const uint64_t activeCacheDomains = EffectiveCacheDomains();
  const bool isMissingRequiredCacheDomain =
      (aRequiredCacheDomains & ~activeCacheDomains) != 0;
  if (!isMissingRequiredCacheDomain) {
    return false;
  }
  nsAccessibilityService* accService = GetAccService();
  if (!accService) {
    return true;
  }
  if (!accService->ShouldAllowNewCacheDomains()) {
    // The fields aren't in the cache, but we've been told not to ask for
    // more right now (e.g. we're inside a CacheDomainActivationBlocker).
    return true;
  }
  aRequiredCacheDomains = GetCacheDomainSuperset(aRequiredCacheDomains);
  const uint64_t cacheDomains = aRequiredCacheDomains | activeCacheDomains;
#if defined(ANDROID)
  // We might not be on the main Android thread, but we must be in order to
  // send IPDL messages. Dispatch to the main thread to set cache domains.
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("a11y::SetCacheDomains", [cacheDomains]() {
        if (nsAccessibilityService* accService = GetAccService()) {
          accService->SetCacheDomains(cacheDomains);
        }
      }));
#else
  accService->SetCacheDomains(cacheDomains);
#endif
  return true;
}

already_AddRefed<DocAccessibleParent> DocAccessibleParent::New() {
  RefPtr<DocAccessibleParent> dap(new DocAccessibleParent());
  // We need to do this with a non-zero reference count.  The easiest way is to
  // do it in this static method and hide the constructor.
  RegisterWeakMemoryReporter(dap);
  return dap.forget();
}

void DocAccessibleParent::SetBrowsingContext(
    dom::CanonicalBrowsingContext* aBrowsingContext) {
  mBrowsingContext = aBrowsingContext;
}

dom::BrowserParent* DocAccessibleParent::Manager() const {
  return static_cast<dom::BrowserParent*>(PDocAccessibleParent::Manager());
}

mozilla::ipc::IPCResult DocAccessibleParent::ProcessShowEvent(
    nsTArray<AccessibleData>&& aNewTree, const bool& aEventSuppressed,
    const bool& aComplete, const bool& aFromUser) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessibleParent::ProcessShowEvent", A11Y, {},
                            ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_ProcessShowEvent>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  ACQUIRE_ANDROID_LOCK

  MOZ_ASSERT(CheckDocTree());

  if (aNewTree.IsEmpty()) {
    return IPC_FAIL(this, "No children being added");
  }

  RemoteAccessible* root = nullptr;
  RemoteAccessible* rootParent = nullptr;
  RemoteAccessible* lastParent = this;
  uint64_t lastParentID = 0;
  for (const auto& accData : aNewTree) {
    // Avoid repeated hash lookups when there are multiple children of the same
    // parent.
    RemoteAccessible* parent = accData.ParentID() == lastParentID
                                   ? lastParent
                                   : GetAccessible(accData.ParentID());
    // XXX This should really never happen, but sometimes we fail to fire the
    // required show events.
    if (!parent) {
      NS_ERROR("adding child to unknown accessible");
#ifdef DEBUG
      return IPC_FAIL(this, "unknown parent accessible");
#else
      return IPC_OK();
#endif
    }

    lastParent = parent;
    lastParentID = accData.ParentID();

    uint32_t childIdx = accData.IndexInParent();
    if (childIdx > parent->ChildCount()) {
      NS_ERROR("invalid index to add child at");
#ifdef DEBUG
      return IPC_FAIL(this, "invalid index");
#else
      return IPC_OK();
#endif
    }

    RemoteAccessible* child = CreateAcc(accData);
    if (!child) {
      // This shouldn't happen.
      return IPC_FAIL(this, "failed to add children");
    }
    if (!root && !mPendingShowChild) {
      // This is the first Accessible, which is the root of the shown subtree.
      root = child;
      rootParent = parent;
      if (!aComplete) {
        // This is the first message for a show event split across multiple
        // messages. Save the show target for subsequent messages and return.
        mPendingShowChild = accData.ID();
        mPendingShowParent = accData.ParentID();
        mPendingShowIndex = accData.IndexInParent();
        if (!rootParent->IsDoc() && !rootParent->RemoteParent()) {
          return IPC_FAIL(this, "Attempt to split show with detached root");
        }
      }
    }
    // If this show event has been split across multiple messages and this is
    // not the last message, don't attach the shown root to the tree yet.
    // Otherwise, clients might crawl the incomplete subtree and they won't get
    // mutation events for the remaining pieces.
    if (aComplete || root != child) {
      if (!AttachChild(parent, childIdx, child)) {
        return IPC_FAIL(this, "failed to attach child");
      }
    }
  }

  MOZ_ASSERT(CheckDocTree());

  if (!aComplete) {
    // This show event has been split into multiple messages, but this is
    // not the last message. There's nothing more to do here.
    return IPC_OK();
  }
  if (mPendingShowChild) {
    // This is the last message for a show event split across multiple
    // messages. Retrieve the saved show target, attach it to the tree and fire
    // an event if appropriate.
    rootParent = GetAccessible(mPendingShowParent);
    MOZ_ASSERT(rootParent);
    root = GetAccessible(mPendingShowChild);
    MOZ_ASSERT(root);
    if (!AttachChild(rootParent, mPendingShowIndex, root)) {
      return IPC_FAIL(this, "failed to attach pending show child");
    }
    mPendingShowChild = 0;
    mPendingShowParent = 0;
    mPendingShowIndex = 0;
  }

  // Just update, no events.
  if (aEventSuppressed) {
    return IPC_OK();
  }

  {
    // Scope for PerfStats
    AUTO_PROFILER_MARKER_TEXT("a11y::PlatformShowHideEvent", A11Y, {}, ""_ns);
    PerfStats::AutoMetricRecording<
        PerfStats::Metric::A11Y_PlatformShowHideEvent>
        autoRecording;
    // WITHIN THIS SCOPE, DO NOT ADD CODE ABOVE THIS BLOCK:
    // THIS CODE IS MEASURING TIMINGS.
    PlatformShowHideEvent(root, rootParent, true, aFromUser);
  }

  if (nsCOMPtr<nsIObserverService> obsService =
          services::GetObserverService()) {
    obsService->NotifyObservers(nullptr, NS_ACCESSIBLE_CACHE_TOPIC, nullptr);
  }

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  uint32_t type = nsIAccessibleEvent::EVENT_SHOW;
  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(root);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  nsINode* node = nullptr;
  auto event = MakeRefPtr<xpcAccEvent>(type, xpcAcc, doc, node, aFromUser);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

RemoteAccessible* DocAccessibleParent::CreateAcc(
    const AccessibleData& aAccData) {
  if (aAccData.ID() == 0) {
    MOZ_ASSERT_UNREACHABLE("An ID of 0 is reserved for the document itself");
    return nullptr;
  }

  RemoteAccessible* newProxy;
  if ((newProxy = GetAccessible(aAccData.ID()))) {
    // This is a move. Reuse the Accessible; don't destroy it.
    if (newProxy->RemoteParent()) {
      MOZ_ASSERT_UNREACHABLE(
          "Attempt to move RemoteAccessible which still has a parent!");
      return nullptr;
    }
    if (aAccData.ID() == mPendingShowChild) {
      MOZ_ASSERT_UNREACHABLE(
          "Attempt to move RemoteAccessible which has a pending parent");
      return nullptr;
    }
    MOZ_RELEASE_ASSERT(newProxy->ChildCount() == 0 || newProxy->IsOuterDoc(),
                       "Reused RemoteAccessible unexpectedly has children!");
    return newProxy;
  }

  if (!aria::IsRoleMapIndexValid(aAccData.RoleMapEntryIndex())) {
    MOZ_ASSERT_UNREACHABLE("Invalid role map entry index");
    return nullptr;
  }

  if (aAccData.GenericTypes() & eDocument || aAccData.Type() == eRootType ||
      aAccData.Type() == eApplicationType ||
      (aAccData.Type() == eImageType && aAccData.GenericTypes() & eHyperText)) {
    MOZ_ASSERT_UNREACHABLE("Invalid acc type");
    return nullptr;
  }

  newProxy = new RemoteAccessible(aAccData.ID(), this, aAccData.Role(),
                                  aAccData.Type(), aAccData.GenericTypes(),
                                  aAccData.RoleMapEntryIndex());
  mAccessibles.PutEntry(aAccData.ID())->mProxy = newProxy;

  if (RefPtr<AccAttributes> fields = aAccData.CacheFields()) {
    if (!newProxy->ApplyCache(CacheUpdateType::Initial, fields)) {
      return nullptr;
    }
  }

  return newProxy;
}

bool DocAccessibleParent::AttachChild(RemoteAccessible* aParent,
                                      uint32_t aIndex,
                                      RemoteAccessible* aChild) {
  if (!aParent || !aChild) {
    MOZ_ASSERT_UNREACHABLE("Null parent or child");
    return false;
  }

  if (aParent->IsOuterDoc()) {
    MOZ_ASSERT_UNREACHABLE("Cannot attach non-doc to OuterDoc");
    return false;
  }

  if (aIndex > aParent->ChildCount()) {
    MOZ_ASSERT_UNREACHABLE("Invalid index for attached child");
    return false;
  }

  if (aChild->RemoteParent()) {
    MOZ_ASSERT_UNREACHABLE(
        "Attempt to attach child which already has a parent!");
    return false;
  }

  if (!aParent->IsDoc() && !aParent->RemoteParent() &&
      aParent->ID() != mPendingShowChild) {
    MOZ_ASSERT_UNREACHABLE("Attempt to attach child to a detached parent!");
    return false;
  }

  MOZ_RELEASE_ASSERT(!mPendingShowChild || aChild->ID() != mPendingShowParent,
                     "Attempt to attach the pending show's parent as a child!");

  if (aParent == aChild) {
    MOZ_ASSERT_UNREACHABLE("Attempt to make an accessible its own child!");
    return false;
  }

  aParent->AddChildAt(aIndex, aChild);
  aChild->SetParent(aParent);
  // ProxyCreated might have already been called if aChild is being moved.
  if (!aChild->GetWrapper() && !IsPrintDoc()) {
    ProxyCreated(aChild);
  }
  if (aChild->IsTableRow() || aChild->IsTableCell()) {
    CachedTableAccessible::Invalidate(aChild);
  }
  if (aChild->IsOuterDoc()) {
    // We can only do this after ProxyCreated is called because it will fire an
    // event on aChild.
    mPendingOOPChildDocs.RemoveIf([&](dom::BrowserBridgeParent* bridge) {
      MOZ_ASSERT(bridge->GetBrowserParent(),
                 "Pending BrowserBridgeParent should be alive");
      if (bridge->GetEmbedderAccessibleId() != aChild->ID()) {
        return false;
      }
      MOZ_ASSERT(bridge->GetEmbedderAccessibleDoc() == this);
      if (DocAccessibleParent* childDoc = bridge->GetDocAccessibleParent()) {
        MOZ_DIAGNOSTIC_ASSERT(!childDoc->RemoteParent(),
                              "Pending OOP child doc shouldn't have parent "
                              "once new OuterDoc is attached");
        AddChildDoc(childDoc, aChild->ID(), false);
      }
      return true;
    });
  }

  return true;
}

void DocAccessibleParent::ShutdownOrPrepareForMove(RemoteAccessible* aAcc) {
  // Children might be removed or moved. Handle them the same way. We do this
  // before checking the moving IDs set in order to ensure that we handle moved
  // descendants properly. Avoid descending into the children of outer documents
  // for moves since they are added and removed differently to normal children.
  if (!aAcc->IsOuterDoc()) {
    // Even if some children are kept, those will be re-attached when we handle
    // the show event. For now, clear all of them by moving them to a temporary.
    auto children{std::move(aAcc->mChildren)};
    for (RefPtr<RemoteAccessible>& childRef : children) {
      RemoteAccessible* child = childRef.get();
      // Drop our reference before recursing so that if child is being
      // removed, the refcount assertion in its Shutdown() reflects only
      // mDoc's reference.
      childRef = nullptr;
      if (child == aAcc) {
        MOZ_ASSERT_UNREACHABLE(
            "Somehow an accessible got added as a child of itself!");
      }
      ShutdownOrPrepareForMove(child);
    }
  }

  const uint64_t id = aAcc->ID();
  if (!mMovingIDs.Contains(id)) {
    // This Accessible is being removed.
    aAcc->Shutdown();
    return;
  }
  // This is a move. Moves are sent as a hide and then a show, but for a move,
  // we want to keep the Accessible alive for reuse later.
  if (aAcc->IsTable() || aAcc->IsTableRow() || aAcc->IsTableCell()) {
    // For table cells, it's important that we do this before the parent is
    // cleared because CachedTableAccessible::Invalidate needs the ancestry.
    CachedTableAccessible::Invalidate(aAcc);
  }
  if (aAcc->IsHyperText()) {
    aAcc->InvalidateCachedHyperTextOffsets();
  }
  aAcc->SetParent(nullptr);
  mMovingIDs.EnsureRemoved(id);
}

mozilla::ipc::IPCResult DocAccessibleParent::ProcessHideEvent(
    const uint64_t& aRootID, const bool& aFromUser) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessibleParent::ProcessHideEvent", A11Y, {},
                            ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_ProcessHideEvent>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.
  ACQUIRE_ANDROID_LOCK

  MOZ_ASSERT(CheckDocTree());

  if (mPendingShowChild) {
    return IPC_FAIL(this, "Hide during split show");
  }

  // We shouldn't actually need this because mAccessibles shouldn't have an
  // entry for the document itself, but it doesn't hurt to be explicit.
  if (!aRootID) {
    return IPC_FAIL(this, "Trying to hide entire document?");
  }

  ProxyEntry* rootEntry = mAccessibles.GetEntry(aRootID);
  if (!rootEntry) {
    NS_ERROR("invalid root being removed!");
    return IPC_OK();
  }

  RemoteAccessible* root = rootEntry->mProxy;
  if (!root) {
    NS_ERROR("invalid root being removed!");
    return IPC_OK();
  }

  RemoteAccessible* parent = root->RemoteParent();
  {
    // Scope for PerfStats
    AUTO_PROFILER_MARKER_TEXT("a11y::PlatformShowHideEvent", A11Y, {}, ""_ns);
    PerfStats::AutoMetricRecording<
        PerfStats::Metric::A11Y_PlatformShowHideEvent>
        autoRecording;
    // WITHIN THIS SCOPE, DO NOT ADD CODE ABOVE THIS BLOCK:
    // THIS CODE IS MEASURING TIMINGS.
    PlatformShowHideEvent(root, parent, false, aFromUser);
  }

  RefPtr<xpcAccHideEvent> event = nullptr;
  if (nsCoreUtils::AccEventObserversExist()) {
    uint32_t type = nsIAccessibleEvent::EVENT_HIDE;
    xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(root);
    xpcAccessibleGeneric* xpcParent = GetXPCAccessible(parent);
    RemoteAccessible* next = root->RemoteNextSibling();
    xpcAccessibleGeneric* xpcNext = next ? GetXPCAccessible(next) : nullptr;
    RemoteAccessible* prev = root->RemotePrevSibling();
    xpcAccessibleGeneric* xpcPrev = prev ? GetXPCAccessible(prev) : nullptr;
    xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
    nsINode* node = nullptr;
    event = new xpcAccHideEvent(type, xpcAcc, doc, node, aFromUser, xpcParent,
                                xpcNext, xpcPrev);
  }

  parent->RemoveChild(root);
  ShutdownOrPrepareForMove(root);

  MOZ_ASSERT(CheckDocTree());

  if (event) {
    nsCoreUtils::DispatchAccEvent(std::move(event));
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvEvent(
    const uint64_t& aID, const uint32_t& aEventType) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }
  if (aEventType == 0 || aEventType >= nsIAccessibleEvent::EVENT_LAST_ENTRY) {
    MOZ_ASSERT_UNREACHABLE("Invalid event");
    return IPC_FAIL(this, "Invalid event");
  }

  RemoteAccessible* remote = GetAccessible(aID);
  if (!remote) {
    NS_ERROR("no proxy for event!");
    return IPC_OK();
  }

  FireEvent(remote, aEventType);
  return IPC_OK();
}

void DocAccessibleParent::FireEvent(RemoteAccessible* aAcc,
                                    const uint32_t& aEventType) {
  if (aEventType == nsIAccessibleEvent::EVENT_REORDER ||
      aEventType == nsIAccessibleEvent::EVENT_INNER_REORDER) {
    uint32_t count = aAcc->ChildCount();
    for (uint32_t c = 0; c < count; ++c) {
      aAcc->RemoteChildAt(c)->InvalidateGroupInfo();
    }
  } else if (aEventType == nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE &&
             aAcc == this) {
    // A DocAccessible gets the STALE state while it is still loading, but we
    // don't fire a state change for that. That state might have been
    // included in the initial cache push, so clear it here.
    // We also clear the BUSY state here. Although we do fire a state change
    // for that, we fire it after doc load complete. It doesn't make sense
    // for the document to report BUSY after doc load complete and doing so
    // confuses JAWS.
    UpdateStateCache(states::STALE | states::BUSY, false);
  }

  PlatformEvent(aAcc, aEventType);

  if (!nsCoreUtils::AccEventObserversExist()) {
    return;
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(aAcc);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  nsINode* node = nullptr;
  bool fromUser = true;  // XXX fix me
  auto event = MakeRefPtr<xpcAccEvent>(aEventType, xpcAcc, doc, node, fromUser);
  nsCoreUtils::DispatchAccEvent(std::move(event));
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvStateChangeEvent(
    const uint64_t& aID, const uint64_t& aState, const bool& aEnabled) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  RemoteAccessible* target = GetAccessible(aID);
  if (!target) {
    NS_ERROR("we don't know about the target of a state change event!");
    return IPC_OK();
  }

  target->UpdateStateCache(aState, aEnabled);
  if (nsCOMPtr<nsIObserverService> obsService =
          services::GetObserverService()) {
    obsService->NotifyObservers(nullptr, NS_ACCESSIBLE_CACHE_TOPIC, nullptr);
  }
  PlatformStateChangeEvent(target, aState, aEnabled);

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(target);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  uint32_t type = nsIAccessibleEvent::EVENT_STATE_CHANGE;
  bool extra;
  uint32_t state = nsAccUtils::To32States(aState, &extra);
  bool fromUser = true;     // XXX fix this
  nsINode* node = nullptr;  // XXX can we do better?
  auto event = MakeRefPtr<xpcAccStateChangeEvent>(
      type, xpcAcc, doc, node, fromUser, state, extra, aEnabled);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvCaretMoveEvent(
    const uint64_t& aID, const LayoutDeviceIntRect& aCaretRect,
    const int32_t& aOffset, const bool& aIsSelectionCollapsed,
    const bool& aIsAtEndOfLine, const int32_t& aGranularity,
    const bool& aFromUser, const bool& aSuppressEvent) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  RemoteAccessible* proxy = GetAccessible(aID);
  if (!proxy) {
    NS_ERROR("unknown caret move event target!");
    return IPC_OK();
  }

  if (aOffset < -1) {
    return IPC_FAIL(this, "Invalid caret offset");
  }

  mCaretId = aID;
  mCaretOffset = aOffset;
  mIsCaretAtEndOfLine = aIsAtEndOfLine;
  mCaretRect = aCaretRect;
  if (aIsSelectionCollapsed) {
    // We don't fire selection events for collapsed selections, but we need to
    // ensure we don't have a stale cached selection; e.g. when selecting
    // forward and then unselecting backward.
    mTextSelections.ClearAndRetainStorage();
    mTextSelections.AppendElement(TextRangeData(aID, aID, aOffset, aOffset));
  }

  if (aSuppressEvent) {
    // We're just updating the cached caret, not notifying clients.
    return IPC_OK();
  }

  PlatformCaretMoveEvent(proxy, aOffset, aIsSelectionCollapsed, aGranularity,
                         aFromUser);

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(proxy);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  nsINode* node = nullptr;
  bool fromUser = true;  // XXX fix me
  uint32_t type = nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED;
  auto event = MakeRefPtr<xpcAccCaretMoveEvent>(
      type, xpcAcc, doc, node, fromUser, aOffset, aIsSelectionCollapsed,
      aIsAtEndOfLine, aGranularity);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::ProcessTextChangeEvent(
    const uint64_t& aID, const nsAString& aStr, const int32_t& aStart,
    const uint32_t& aLen, const bool& aIsInsert, const bool& aFromUser) {
  ACQUIRE_ANDROID_LOCK

  RemoteAccessible* target = GetAccessible(aID);
  if (!target) {
    NS_ERROR("text change event target is unknown!");
    return IPC_OK();
  }

  PlatformTextChangeEvent(target, aStr, aStart, aLen, aIsInsert, aFromUser);

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(target);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  uint32_t type = aIsInsert ? nsIAccessibleEvent::EVENT_TEXT_INSERTED
                            : nsIAccessibleEvent::EVENT_TEXT_REMOVED;
  nsINode* node = nullptr;
  auto event = MakeRefPtr<xpcAccTextChangeEvent>(
      type, xpcAcc, doc, node, aFromUser, aStart, aLen, aIsInsert, aStr);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvMutationEvents(
    nsTArray<MutationEventData>&& aData) {
  // We do not use ACQUIRE_ANDROID_LOCK here since we call functions that do
  // that for us. The lock is not re-entrant.
  mozilla::ipc::IPCResult result = IPC_OK();
  if (mShutdown) {
    return result;
  }
  for (MutationEventData& data : aData) {
    switch (data.type()) {
      case MutationEventData::Type::TCacheEventData: {
        CacheEventData& cacheEventData = data;
        result = RecvCache(cacheEventData.UpdateType(),
                           std::move(cacheEventData.aData()));
        break;
      }
      case MutationEventData::Type::TReorderEventData: {
        ReorderEventData& reorderEventData = data;
        result = RecvEvent(reorderEventData.ID(), reorderEventData.Type());
        break;
      }
      case MutationEventData::Type::THideEventData: {
        HideEventData& hideEventData = data;
        result = ProcessHideEvent(hideEventData.ID(),
                                  hideEventData.IsFromUserInput());
        break;
      }
      case MutationEventData::Type::TShowEventData: {
        ShowEventData& showEventData = data;
        result = ProcessShowEvent(
            std::move(showEventData.NewTree()), showEventData.EventSuppressed(),
            showEventData.Complete(), showEventData.FromUser());
        break;
      }
      case MutationEventData::Type::TTextChangeEventData: {
        TextChangeEventData& textChangeEventData = data;
        result = ProcessTextChangeEvent(
            textChangeEventData.ID(), textChangeEventData.Str(),
            textChangeEventData.Start(), textChangeEventData.Len(),
            textChangeEventData.IsInsert(), textChangeEventData.FromUser());
        break;
      }
      default:
        break;
    }
    if (!result) {
      return result;
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvRequestAckMutationEvents() {
  if (!mShutdown) {
    if (!mIsInitialTreeDone) {
      // This is the first request for an ACK, which means we now have the
      // initial tree.
      mIsInitialTreeDone = true;
      // If this document is already bound to its embedder, fire a reorder event
      // to notify the client that the embedded document is available. If not,
      // this will be handled when this document is bound in AddChildDoc.
      if (RemoteAccessible* parent = RemoteParent()) {
        parent->Document()->FireEvent(parent,
                                      nsIAccessibleEvent::EVENT_REORDER);
      }
    }
    (void)SendAckMutationEvents();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvSelectionEvent(
    const uint64_t& aID, const uint64_t& aWidgetID, const uint32_t& aType) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }
  if (aType == 0 || aType >= nsIAccessibleEvent::EVENT_LAST_ENTRY) {
    MOZ_ASSERT_UNREACHABLE("Invalid event");
    return IPC_FAIL(this, "Invalid event");
  }

  RemoteAccessible* target = GetAccessible(aID);
  RemoteAccessible* widget = GetAccessible(aWidgetID);
  if (!target || !widget) {
    NS_ERROR("invalid id in selection event");
    return IPC_OK();
  }

  PlatformSelectionEvent(target, widget, aType);
  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }
  xpcAccessibleGeneric* xpcTarget = GetXPCAccessible(target);
  xpcAccessibleDocument* xpcDoc = GetAccService()->GetXPCDocument(this);
  auto event =
      MakeRefPtr<xpcAccEvent>(aType, xpcTarget, xpcDoc, nullptr, false);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvScrollingEvent(
    const uint64_t& aID, const uint64_t& aType, const uint32_t& aScrollX,
    const uint32_t& aScrollY, const uint32_t& aMaxScrollX,
    const uint32_t& aMaxScrollY) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }
  if (aType == 0 || aType >= nsIAccessibleEvent::EVENT_LAST_ENTRY) {
    MOZ_ASSERT_UNREACHABLE("Invalid event");
    return IPC_FAIL(this, "Invalid event");
  }

  RemoteAccessible* target = GetAccessible(aID);
  if (!target) {
    NS_ERROR("no proxy for event!");
    return IPC_OK();
  }

#if defined(ANDROID)
  PlatformScrollingEvent(target, aType, aScrollX, aScrollY, aMaxScrollX,
                         aMaxScrollY);
#else
  PlatformEvent(target, aType);
#endif

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(target);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  nsINode* node = nullptr;
  bool fromUser = true;  // XXX: Determine if this was from user input.
  auto event = MakeRefPtr<xpcAccScrollingEvent>(aType, xpcAcc, doc, node,
                                                fromUser, aScrollX, aScrollY,
                                                aMaxScrollX, aMaxScrollY);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvCache(
    const mozilla::a11y::CacheUpdateType& aUpdateType,
    nsTArray<CacheData>&& aData) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessibleParent::RecvCache", A11Y, {}, ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_RecvCache>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  for (auto& entry : aData) {
    RemoteAccessible* remote = GetAccessible(entry.ID());
    if (!remote) {
      MOZ_ASSERT_UNREACHABLE("No remote found!");
      continue;
    }

    if (!remote->ApplyCache(aUpdateType, entry.Fields())) {
      return IPC_FAIL(this, "Invalid cache data");
    }
  }

  if (nsCOMPtr<nsIObserverService> obsService =
          services::GetObserverService()) {
    obsService->NotifyObservers(nullptr, NS_ACCESSIBLE_CACHE_TOPIC, nullptr);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvSelectedAccessiblesChanged(
    nsTArray<uint64_t>&& aSelectedIDs, nsTArray<uint64_t>&& aUnselectedIDs) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  for (auto& id : aSelectedIDs) {
    RemoteAccessible* remote = GetAccessible(id);
    if (!remote) {
      MOZ_ASSERT_UNREACHABLE("No remote found!");
      continue;
    }

    remote->UpdateStateCache(states::SELECTED, true);
  }

  for (auto& id : aUnselectedIDs) {
    RemoteAccessible* remote = GetAccessible(id);
    if (!remote) {
      MOZ_ASSERT_UNREACHABLE("No remote found!");
      continue;
    }

    remote->UpdateStateCache(states::SELECTED, false);
  }

  if (nsCOMPtr<nsIObserverService> obsService =
          services::GetObserverService()) {
    obsService->NotifyObservers(nullptr, NS_ACCESSIBLE_CACHE_TOPIC, nullptr);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvAccessiblesWillMove(
    nsTArray<uint64_t>&& aIDs) {
  for (uint64_t id : aIDs) {
    mMovingIDs.EnsureInserted(id);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvAnnouncementEvent(
    const uint64_t& aID, const nsAString& aAnnouncement,
    const uint16_t& aPriority) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  RemoteAccessible* target = GetAccessible(aID);
  if (!target) {
    NS_ERROR("no proxy for event!");
    return IPC_OK();
  }

  PlatformAnnouncementEvent(target, aAnnouncement, aPriority);

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(target);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  auto event = MakeRefPtr<xpcAccAnnouncementEvent>(
      nsIAccessibleEvent::EVENT_ANNOUNCEMENT, xpcAcc, doc, nullptr, false,
      aAnnouncement, aPriority);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvTextSelectionChangeEvent(
    const uint64_t& aID, nsTArray<TextRangeData>&& aSelection) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  RemoteAccessible* target = GetAccessible(aID);
  if (!target) {
    NS_ERROR("no proxy for event!");
    return IPC_OK();
  }

  mTextSelections.ClearAndRetainStorage();
  mTextSelections.AppendElements(aSelection);

#ifdef MOZ_WIDGET_COCOA
  AutoTArray<TextRange, 1> ranges;
  SelectionRanges(&ranges);
  PlatformTextSelectionChangeEvent(target, ranges);
#else
  PlatformEvent(target, nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED);
#endif

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }
  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(target);
  xpcAccessibleDocument* doc = nsAccessibilityService::GetXPCDocument(this);
  nsINode* node = nullptr;
  bool fromUser = true;  // XXX fix me
  auto event =
      MakeRefPtr<xpcAccEvent>(nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED,
                              xpcAcc, doc, node, fromUser);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvRoleChangedEvent(
    const a11y::role& aRole, const uint8_t& aRoleMapEntryIndex) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }
  if (!aria::IsRoleMapIndexValid(aRoleMapEntryIndex)) {
    MOZ_ASSERT_UNREACHABLE("Invalid role map entry index");
    return IPC_FAIL(this, "Invalid role map entry index");
  }

  mNativeRole = aRole;
  mRoleMapEntryIndex = aRoleMapEntryIndex;

#ifdef MOZ_WIDGET_COCOA
  PlatformRoleChangedEvent(this, aRole, aRoleMapEntryIndex);
#endif

  return IPC_OK();
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvBindChildDoc(
    NotNull<PDocAccessibleParent*> aChildDoc, const uint64_t& aID) {
  ACQUIRE_ANDROID_LOCK
  // One document should never directly be the child of another.
  // We should always have at least an outer doc accessible in between.
  MOZ_ASSERT(aID);
  if (!aID) return IPC_FAIL(this, "ID is 0!");

  if (mShutdown) {
    return IPC_OK();
  }

  MOZ_ASSERT(CheckDocTree());

  auto childDoc = static_cast<DocAccessibleParent*>(aChildDoc.get());
  if (childDoc->IsShutdown()) {
    return IPC_FAIL(this, "Attempt to bind a shutdown child doc");
  }
  if (childDoc->Manager() != Manager()) {
    return IPC_FAIL(this,
                    "Attempt to bind child doc from a different PBrowser");
  }

  ipc::IPCResult result = AddChildDoc(childDoc, aID, false);
  MOZ_ASSERT(result);
  MOZ_ASSERT(CheckDocTree());
  if (!result) {
    return result;
  }

  return result;
}

ipc::IPCResult DocAccessibleParent::AddChildDoc(DocAccessibleParent* aChildDoc,
                                                uint64_t aParentID,
                                                bool aCreating) {
  if (aChildDoc->RemoteParent()) {
    return IPC_FAIL(this,
                    "Attempt to add child doc which already has a parent");
  }
  if (aChildDoc->IsTopLevel()) {
    return IPC_FAIL(this, "Attempt to add a top level doc as a child");
  }
  if (aChildDoc->IsShutdown()) {
    return IPC_FAIL(this, "Attempt to add a shutdown child doc");
  }

  // We do not use GetAccessible here because we want to be sure to not get the
  // document it self.
  ProxyEntry* e = mAccessibles.GetEntry(aParentID);
  if (!e) {
    MOZ_ASSERT_UNREACHABLE("Binding to nonexistent proxy!");
    return IPC_FAIL(this, "binding to nonexistant proxy!");
  }

  RemoteAccessible* outerDoc = e->mProxy;
  MOZ_ASSERT(outerDoc);

  // OuterDocAccessibles are expected to only have a document as a child.
  // However for compatibility we tolerate replacing one document with another
  // here.
  if (!outerDoc->IsOuterDoc() || outerDoc->ChildCount() > 1 ||
      (outerDoc->ChildCount() == 1 && !outerDoc->RemoteChildAt(0)->IsDoc())) {
    MOZ_ASSERT_UNREACHABLE("Binding to parent that isn't a valid OuterDoc!");
    return IPC_FAIL(this, "Binding to parent that isn't a valid OuterDoc!");
  }

  if (outerDoc->ChildCount() == 1) {
    MOZ_ASSERT(outerDoc->RemoteChildAt(0)->AsDoc());
    outerDoc->RemoteChildAt(0)->AsDoc()->Unbind();
  }

  aChildDoc->SetParent(outerDoc);
  outerDoc->SetChildDoc(aChildDoc);
  mChildDocs.AppendElement(aChildDoc->mActorID);

  if (aCreating && !aChildDoc->IsPrintDoc()) {
    ProxyCreated(aChildDoc);
  }

  if (aChildDoc->IsTopLevelInContentProcess()) {
    // aChildDoc is an embedded document in a different content process to
    // this document.
#if defined(XP_WIN)
    if (nsWinUtils::IsWindowEmulationStarted()) {
      aChildDoc->SetEmulatedWindowHandle(mEmulatedWindowHandle);
    }
#endif  // defined(XP_WIN)
  }
  // We need to fire a reorder event on the embedder. We do this here rather
  // than in the content process for two reasons:
  // 1. It isn't possible for the content process to fire a reorder event on the
  // embedder when the embedded document is in a different process to its
  // embedder.
  // 2. Doing it here ensures that the event is fired after the child document
  // is bound. Otherwise, there could be a short period where the content
  // process has fired the reorder event, but the child document isn't bound
  // yet.
  // However, if the initial tree hasn't been received yet, we don't want to
  // fire the reorder event yet. That gets handled in
  // RecvRequestAckMutationEvents.
  if (aChildDoc->mIsInitialTreeDone) {
    FireEvent(outerDoc, nsIAccessibleEvent::EVENT_REORDER);
  }

  return IPC_OK();
}

ipc::IPCResult DocAccessibleParent::AddChildDoc(
    dom::BrowserBridgeParent* aBridge) {
  MOZ_ASSERT(aBridge->GetEmbedderAccessibleDoc() == this);
  uint64_t parentId = aBridge->GetEmbedderAccessibleId();
  MOZ_ASSERT(parentId);
  if (!mAccessibles.GetEntry(parentId)) {
    // Sometimes, this gets called before the embedder sends us the
    // OuterDocAccessible. We must add the child when the OuterDocAccessible
    // gets created later.
    mPendingOOPChildDocs.Insert(aBridge);
    return IPC_OK();
  }
  return AddChildDoc(aBridge->GetDocAccessibleParent(), parentId,
                     /* aCreating */ false);
}

mozilla::ipc::IPCResult DocAccessibleParent::RecvShutdown() {
  ACQUIRE_ANDROID_LOCK
  Destroy();

  auto mgr = Manager();
  if (!mgr->IsDestroyed()) {
    if (!PDocAccessibleParent::Send__delete__(this)) {
      return IPC_FAIL_NO_REASON(mgr);
    }
  }

  return IPC_OK();
}

void DocAccessibleParent::Destroy() {
  // If we are already shutdown that is because our containing tab parent is
  // shutting down in which case we don't need to do anything.
  if (mShutdown) {
    // Just in case there is a cycle in the document heirarchy.
    mParent = nullptr;
    mIndexInParent = -1;
    return;
  }

  mShutdown = true;
  mBrowsingContext = nullptr;

#ifdef ANDROID
  if (FocusMgr() && FocusMgr()->IsFocusedRemoteDoc(this)) {
    FocusMgr()->SetFocusedRemoteDoc(nullptr);
  }
#endif

  MOZ_DIAGNOSTIC_ASSERT(LiveDocs().Contains(mActorID));
  uint32_t childDocCount = mChildDocs.Length();
  for (uint32_t i = 0; i < childDocCount; i++) {
    for (uint32_t j = i + 1; j < childDocCount; j++) {
      MOZ_DIAGNOSTIC_ASSERT(mChildDocs[i] != mChildDocs[j]);
    }
  }

  // XXX This indirection through the hash map of live documents shouldn't be
  // needed, but be paranoid for now.
  uint64_t actorID = mActorID;
  for (uint32_t i = childDocCount - 1; i < childDocCount; i--) {
    DocAccessibleParent* thisDoc = LiveDocs().Get(actorID);
    MOZ_ASSERT(thisDoc);
    if (!thisDoc) {
      return;
    }

    thisDoc->ChildDocAt(i)->Destroy();
  }

  for (auto iter = mAccessibles.Iter(); !iter.Done(); iter.Next()) {
    RemoteAccessible* acc = iter.Get()->mProxy;
    MOZ_ASSERT(acc != this);
    if (acc->IsTable()) {
      CachedTableAccessible::Invalidate(acc);
    }
    ProxyDestroyed(acc);
    // acc and its parent/children hold strong references to each other, so
    // clear acc's children to break that cycle. Once every node in this loop
    // has done the same and had its mAccessibles entry removed below, no
    // references remain and every node is destroyed.
    acc->mChildren.Clear();
    acc->mDoc = nullptr;
    iter.Remove();
  }

  DocAccessibleParent* thisDoc = LiveDocs().Get(actorID);
  MOZ_ASSERT(thisDoc);
  if (!thisDoc) {
    return;
  }

  mChildren.Clear();
  // The code above should have already completely cleared these, but to be
  // extra safe make sure they are cleared here.
  thisDoc->mAccessibles.Clear();
  thisDoc->mChildDocs.Clear();

  DocManager::NotifyOfRemoteDocShutdown(thisDoc);
  thisDoc = LiveDocs().Get(actorID);
  MOZ_ASSERT(thisDoc);
  if (!thisDoc) {
    return;
  }

  ProxyDestroyed(thisDoc);
  thisDoc = LiveDocs().Get(actorID);
  MOZ_ASSERT(thisDoc);
  if (!thisDoc) {
    return;
  }

  if (IsTopLevel()) {
    GetAccService()->RemoteDocShutdown(this);
  } else {
    RemoteAccessible* outerDoc = RemoteParent();
    Unbind();
    if (outerDoc) {
      // When an iframe document is replaced (e.g. the src URL is changed),
      // there may be a short delay between the removal of the old document
      // handled here and the addition of the new document. This delay might be
      // long enough that we reuse OS specific ids (e.g. MSAA) from the old
      // document. Thus, we must notify clients when the old document is removed
      // so they process the removal before any ids are reused. Otherwise, a
      // client might assume the reused ids refer to Accessibles that have since
      // been removed, potentially causing loops and other breakage in client
      // tree caches such as screen reader virtual buffers. We then fire a
      // second reorder event when the new document is received; see AddChildDoc
      // and RecvRequestAckMutationEvents.
      outerDoc->Document()->FireEvent(outerDoc,
                                      nsIAccessibleEvent::EVENT_REORDER);
    }
  }
}

void DocAccessibleParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(CheckDocTree());
  if (!mShutdown) {
    ACQUIRE_ANDROID_LOCK
    Destroy();
  } else if (RemoteParent()) {
    ACQUIRE_ANDROID_LOCK
    Unbind();
  }
}

DocAccessibleParent* DocAccessibleParent::ParentDoc() const {
  if (RemoteAccessible* parent = RemoteParent()) {
    return parent->Document();
  }
  return nullptr;
}

bool DocAccessibleParent::CheckDocTree() const {
  size_t childDocs = mChildDocs.Length();
  for (size_t i = 0; i < childDocs; i++) {
    const DocAccessibleParent* childDoc = ChildDocAt(i);
    if (!childDoc || childDoc->ParentDoc() != this) return false;

    if (!childDoc->CheckDocTree()) {
      return false;
    }
  }

  return true;
}

xpcAccessibleGeneric* DocAccessibleParent::GetXPCAccessible(
    RemoteAccessible* aProxy) {
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  MOZ_ASSERT(doc);

  return doc->GetAccessible(aProxy);
}

#if defined(XP_WIN)
void DocAccessibleParent::MaybeInitWindowEmulation() {
  if (!nsWinUtils::IsWindowEmulationStarted()) {
    return;
  }

  // XXX get the bounds from the browserParent instead of poking at accessibles
  // which might not exist yet.
  LocalAccessible* outerDoc = OuterDocOfRemoteBrowser();
  if (!outerDoc) {
    return;
  }

  RootAccessible* rootDocument = outerDoc->RootAccessible();
  MOZ_ASSERT(rootDocument);

  bool isActive = true;
  LayoutDeviceIntRect rect(CW_USEDEFAULT, CW_USEDEFAULT, 0, 0);
  if (Compatibility::IsDolphin()) {
    rect = Bounds();
    LayoutDeviceIntRect rootRect = rootDocument->Bounds();
    rect.MoveToX(rootRect.X() - rect.X());
    rect.MoveToY(rect.Y() - rootRect.Y());

    auto browserParent = Manager();
    isActive = browserParent->GetDocShellIsActive();
  }

  // onCreate is guaranteed to be called synchronously by
  // nsWinUtils::CreateNativeWindow, so this reference isn't really necessary.
  // However, static analysis complains without it.
  RefPtr<DocAccessibleParent> thisRef = this;
  nsWinUtils::NativeWindowCreateProc onCreate([thisRef](HWND aHwnd) -> void {
    ::SetPropW(aHwnd, kPropNameDocAccParent,
               reinterpret_cast<HANDLE>(thisRef.get()));
    thisRef->SetEmulatedWindowHandle(aHwnd);
  });

  HWND parentWnd = reinterpret_cast<HWND>(rootDocument->GetNativeWindow());
  DebugOnly<HWND> hWnd = nsWinUtils::CreateNativeWindow(
      kClassNameTabContent, parentWnd, rect.X(), rect.Y(), rect.Width(),
      rect.Height(), isActive, &onCreate);
  MOZ_ASSERT(hWnd);
}

void DocAccessibleParent::SetEmulatedWindowHandle(HWND aWindowHandle) {
  if (!aWindowHandle && mEmulatedWindowHandle && IsTopLevel()) {
    ::DestroyWindow(mEmulatedWindowHandle);
  }
  mEmulatedWindowHandle = aWindowHandle;
}
#endif  // defined(XP_WIN)

mozilla::ipc::IPCResult DocAccessibleParent::RecvFocusEvent(
    const uint64_t& aID, const LayoutDeviceIntRect& aCaretRect) {
  ACQUIRE_ANDROID_LOCK
  if (mShutdown) {
    return IPC_OK();
  }

  RemoteAccessible* proxy = GetAccessible(aID);
  if (!proxy) {
    NS_ERROR("no proxy for event!");
    return IPC_OK();
  }

#ifdef ANDROID
  if (FocusMgr()) {
    FocusMgr()->SetFocusedRemoteDoc(this);
  }
#endif

  mFocus = aID;
  mCaretRect = aCaretRect;
#ifdef MOZ_WIDGET_COCOA
  if (PlatformShouldTrackFocusedAccLocation()) {
    mFocusedAccBounds = Some(proxy->Bounds());
  }
#endif
  PlatformFocusEvent(proxy);

  if (!nsCoreUtils::AccEventObserversExist()) {
    return IPC_OK();
  }

  xpcAccessibleGeneric* xpcAcc = GetXPCAccessible(proxy);
  xpcAccessibleDocument* doc = GetAccService()->GetXPCDocument(this);
  nsINode* node = nullptr;
  bool fromUser = true;  // XXX fix me
  auto event = MakeRefPtr<xpcAccEvent>(nsIAccessibleEvent::EVENT_FOCUS, xpcAcc,
                                       doc, node, fromUser);
  nsCoreUtils::DispatchAccEvent(std::move(event));

  return IPC_OK();
}

LayoutDeviceIntRect DocAccessibleParent::GetCachedCaretRect() {
  LayoutDeviceIntRect caretRect = mCaretRect;
  if (!caretRect.IsEmpty()) {
    // Reapply doc offset to the caret rect.
    LayoutDeviceIntRect docRect = Bounds();
    caretRect.MoveBy(docRect.X(), docRect.Y());
  }

  return caretRect;
}

void DocAccessibleParent::SelectionRanges(nsTArray<TextRange>* aRanges) const {
  aRanges->SetCapacity(mTextSelections.Length());
  for (const auto& data : mTextSelections) {
    // Selection ranges should usually be in sync with the tree. However, tree
    // and selection updates happen using separate IPDL calls, so it's possible
    // for a client selection query to arrive between them. Thus, we validate
    // the Accessibles and offsets here.
    auto* startAcc =
        const_cast<RemoteAccessible*>(GetAccessible(data.StartID()));
    auto* endAcc = const_cast<RemoteAccessible*>(GetAccessible(data.EndID()));
    if (!startAcc || !endAcc) {
      continue;
    }
    // Offset 0 is always valid, even if the container is empty.
    if (data.StartOffset() > 0) {
      uint32_t startCount = startAcc->CharacterCount();
      if (data.StartOffset() > static_cast<int32_t>(startCount)) {
        continue;
      }
    }
    if (data.EndOffset() > 0) {
      uint32_t endCount = endAcc->CharacterCount();
      if (data.EndOffset() > static_cast<int32_t>(endCount)) {
        continue;
      }
    }
    aRanges->AppendElement(TextRange(const_cast<DocAccessibleParent*>(this),
                                     startAcc, data.StartOffset(), endAcc,
                                     data.EndOffset()));
  }
}

Accessible* DocAccessibleParent::FocusedChild() {
  LocalAccessible* outerDoc = OuterDocOfRemoteBrowser();
  if (!outerDoc) {
    return nullptr;
  }

  RootAccessible* rootDocument = outerDoc->RootAccessible();
  return rootDocument->FocusedChild();
}

void DocAccessibleParent::URL(nsACString& aURL) const {
  if (!mBrowsingContext) {
    return;
  }
  nsCOMPtr<nsIURI> uri = mBrowsingContext->GetCurrentURI();
  if (!uri) {
    return;
  }
  // Let's avoid treating too long URI in the main process for avoiding
  // memory fragmentation as far as possible.
  if (uri->SchemeIs("data") || uri->SchemeIs("blob")) {
    return;
  }
  nsCOMPtr<nsIIOService> io = mozilla::components::IO::Service();
  if (NS_WARN_IF(!io)) {
    return;
  }
  nsCOMPtr<nsIURI> exposableURI;
  if (NS_FAILED(io->CreateExposableURI(uri, getter_AddRefs(exposableURI))) ||
      MOZ_UNLIKELY(!exposableURI)) {
    return;
  }
  exposableURI->GetSpec(aURL);
}

void DocAccessibleParent::URL(nsAString& aURL) const {
  nsAutoCString url;
  URL(url);
  CopyUTF8toUTF16(url, aURL);
}

void DocAccessibleParent::MimeType(nsAString& aMime) const {
  if (mCachedFields) {
    mCachedFields->GetAttribute(CacheKey::MimeType, aMime);
  }
}

Relation DocAccessibleParent::RelationByType(RelationType aType) const {
  // If the accessible is top-level, provide the NODE_CHILD_OF relation so that
  // MSAA clients can easily get to true parent instead of getting to oleacc's
  // ROLE_WINDOW accessible when window emulation is enabled which will prevent
  // us from going up further (because it is system generated and has no idea
  // about the hierarchy above it).
  if (aType == RelationType::NODE_CHILD_OF && IsTopLevel()) {
    return Relation(Parent());
  }

  return RemoteAccessible::RelationByType(aType);
}

DocAccessibleParent* DocAccessibleParent::GetFrom(
    dom::BrowsingContext* aBrowsingContext) {
  if (!aBrowsingContext) {
    return nullptr;
  }

  dom::BrowserParent* bp = aBrowsingContext->Canonical()->GetBrowserParent();
  if (!bp) {
    return nullptr;
  }

  const ManagedContainer<PDocAccessibleParent>& docs =
      bp->ManagedPDocAccessibleParent();
  for (auto* key : docs) {
    // Iterate over our docs until we find one with a browsing
    // context that matches the one we passed in. Return that
    // document.
    auto* doc = static_cast<a11y::DocAccessibleParent*>(key);
    if (doc->GetBrowsingContext() == aBrowsingContext) {
      return doc;
    }
  }

  return nullptr;
}

size_t DocAccessibleParent::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) {
  size_t size = 0;

  size += RemoteAccessible::SizeOfExcludingThis(aMallocSizeOf);

  size += mReverseRelations.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (auto i = mReverseRelations.Iter(); !i.Done(); i.Next()) {
    size += i.Data().ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (auto j = i.Data().Iter(); !j.Done(); j.Next()) {
      size += j.Data().ShallowSizeOfExcludingThis(aMallocSizeOf);
    }
  }

  size += mOnScreenAccessibles.ShallowSizeOfExcludingThis(aMallocSizeOf);

  size += mChildDocs.ShallowSizeOfExcludingThis(aMallocSizeOf);

  size += mAccessibles.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (auto i = mAccessibles.Iter(); !i.Done(); i.Next()) {
    size += i.Get()->mProxy->SizeOfIncludingThis(aMallocSizeOf);
  }

  size += mPendingOOPChildDocs.ShallowSizeOfExcludingThis(aMallocSizeOf);

  // The mTextSelections array contains structs of integers.  We can count them
  // by counting the size of the array - there's no deep structure here.
  size += mTextSelections.ShallowSizeOfExcludingThis(aMallocSizeOf);

  return size;
}

MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOfAccessibilityCache);

NS_IMETHODIMP
DocAccessibleParent::CollectReports(nsIHandleReportCallback* aHandleReport,
                                    nsISupports* aData, bool aAnon) {
  nsAutoCString path;

  if (aAnon) {
    path = nsPrintfCString("explicit/a11y/cache(%" PRIu64 ")", mActorID);
  } else {
    nsCString url;
    URL(url);
    url.ReplaceChar(
        '/', '\\');  // Tell the memory reporter this is not a path seperator.
    path = nsPrintfCString("explicit/a11y/cache(%s)", url.get());
  }

  aHandleReport->Callback(
      /* process */ ""_ns, path, KIND_HEAP, UNITS_BYTES,
      SizeOfIncludingThis(MallocSizeOfAccessibilityCache),
      nsLiteralCString("Size of the accessability cache for this document."),
      aData);

  return NS_OK;
}

NS_IMPL_QUERY_INTERFACE(DocAccessibleParent, nsIMemoryReporter)
NS_IMPL_ADDREF_INHERITED(DocAccessibleParent, RemoteAccessible)
NS_IMPL_RELEASE_INHERITED(DocAccessibleParent, RemoteAccessible)

#ifdef MOZ_ENABLE_SKIA_PDF
mozilla::ipc::IPCResult DocAccessibleParent::RecvPrinting() {
  if (dom::CanonicalBrowsingContext* bc = GetBrowsingContext()) {
    if (dom::WindowContext* wc = bc->GetCurrentWindowContext()) {
      PdfStructTreeBuilder::Init(wc);
    }
  }
  return IPC_OK();
}
#endif

DocAccessibleParent::AllowConstruction
DocAccessibleParent::ShouldAllowConstruction() const {
  if (IsPrintDoc()) {
#ifdef MOZ_ENABLE_SKIA_PDF
    if (!StaticPrefs::accessibility_tagged_pdf_output_enabled()) {
      return AllowConstruction::Disallow;
    }
    // We need the accessibility tree to generate a tagged PDF. We can do this
    // even if the accessibility service isn't running in the parent process.
    // However, we can only be generating a PDF if there's a PRemotePrintJob
    // actor in the BrowserParent ancestry.
    auto* bp = static_cast<dom::BrowserParent*>(Manager());
    while (bp) {
      if (!bp->Manager()->ManagedPRemotePrintJobParent().IsEmpty()) {
        return AllowConstruction::Allow;
      }
      dom::BrowserBridgeParent* bridge = bp->GetBrowserBridgeParent();
      if (!bridge) {
        break;
      }
      bp = bridge->Manager();
    }
#endif  // MOZ_ENABLE_SKIA_PDF
    return AllowConstruction::Disallow;
  }
  // For non-print documents, only allow construction if the accessibility
  // service is running here in the parent process.
  if (GetAccService()) {
    return AllowConstruction::Allow;
  }
  // If accessibility is activated and then quickly deactivated, there might
  // already be PDocAccessible messages in flight. To deal with this, check if
  // accessibility was ever activated in the associated content process. If it
  // was, we don't treat the construction as an error, but we mark the actor as
  // shut down and ignore it.
  if (Manager()->Manager()->WasA11yEverActivated()) {
    return AllowConstruction::AllowButIgnore;
  }
  return AllowConstruction::Disallow;
}

}  // namespace a11y
}  // namespace mozilla
