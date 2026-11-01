/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ARIAMap.h"
#include "AccAttributes.h"
#include "AccIterator.h"
#include "AnchorPositioningUtils.h"
#include "CachedTableAccessible.h"
#include "DocAccessible-inl.h"
#include "EventTree.h"
#include "HTMLImageMapAccessible.h"
#include "LocalAccessible-inl.h"
#include "Relation.h"
#include "TreeWalker.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/Components.h"  // for mozilla::components
#include "mozilla/EditorBase.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/PerfStats.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/a11y/DocAccessibleChild.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsAccUtils.h"
#include "nsAccessibilityService.h"
#include "nsEventShell.h"
#include "nsFocusManager.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "nsIEditingSession.h"
#include "nsIFrame.h"
#include "nsIIOService.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIMutationObserver.h"
#include "nsIURI.h"
#include "nsIWebNavigation.h"
#include "nsImageFrame.h"
#include "nsLayoutUtils.h"
#include "nsPIDOMWindow.h"
#include "nsTextEquivUtils.h"
#include "xpcAccessibleDocument.h"

using namespace mozilla;
using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// Static member initialization

static nsStaticAtom* const kRelationAttrs[] = {
    nsGkAtoms::aria_labelledby,   nsGkAtoms::aria_describedby,
    nsGkAtoms::aria_details,      nsGkAtoms::aria_owns,
    nsGkAtoms::aria_controls,     nsGkAtoms::aria_flowto,
    nsGkAtoms::aria_errormessage, nsGkAtoms::_for,
    nsGkAtoms::control,           nsGkAtoms::popovertarget,
    nsGkAtoms::commandfor,        nsGkAtoms::aria_activedescendant,
    nsGkAtoms::aria_actions};

static nsStaticAtom* const kSingleElementRelationIdlAttrs[] = {
    nsGkAtoms::popovertarget, nsGkAtoms::commandfor};

////////////////////////////////////////////////////////////////////////////////
// Constructor/desctructor

DocAccessible::DocAccessible(dom::Document* aDocument,
                             PresShell* aPresShell)
    :  // XXX don't pass a document to the LocalAccessible constructor so that
       // we don't set mDoc until our vtable is fully setup.  If we set mDoc
       // before setting up the vtable we will call LocalAccessible::AddRef()
       // but not the overrides of it for subclasses.  It is important to call
       // those overrides to avoid confusing leak checking machinary.
      HyperTextAccessible(nullptr, nullptr),
      // XXX aaronl should we use an algorithm for the initial cache size?
      mAccessibleCache(kDefaultCacheLength),
      mNodeToAccessibleMap(kDefaultCacheLength),
      mDocumentNode(aDocument),
      mLoadState(eTreeConstructionPending),
      mDocFlags(0),
      mViewportCacheDirty(false),
      mLoadEventType(0),
      mPrevStateBits(0),
      mPresShell(aPresShell),
      mIPCDoc(nullptr) {
  mGenericTypes |= eDocument;
  mStateFlags |= eNotNodeMapEntry;
  mDoc = this;

  MOZ_ASSERT(mPresShell, "should have been given a pres shell");
  mPresShell->SetDocAccessible(this);
}

DocAccessible::~DocAccessible() {
  NS_ASSERTION(!mPresShell, "LastRelease was never called!?!");
}

////////////////////////////////////////////////////////////////////////////////
// nsISupports

NS_IMPL_CYCLE_COLLECTION_CLASS(DocAccessible)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(DocAccessible,
                                                  LocalAccessible)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mNotificationController)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChildDocuments)
  for (const auto& hashEntry : tmp->mDependentIDsHashes.Values()) {
    for (const auto& providers : hashEntry->Values()) {
      for (int32_t provIdx = providers->Length() - 1; provIdx >= 0; provIdx--) {
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
            cb, "content of dependent ids hash entry of document accessible");

        const auto& provider = (*providers)[provIdx];
        cb.NoteXPCOMChild(provider->mContent);
      }
    }
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAccessibleCache)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAnchorJumpElm)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInvalidationList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingUpdates)
  for (const auto& ar : tmp->mARIAOwnsHash.Values()) {
    for (uint32_t i = 0; i < ar->Length(); i++) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mARIAOwnsHash entry item");
      cb.NoteXPCOMChild(ar->ElementAt(i));
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(DocAccessible, LocalAccessible)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mNotificationController)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mChildDocuments)
  tmp->mDependentIDsHashes.Clear();
  tmp->mNodeToAccessibleMap.Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAccessibleCache)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAnchorJumpElm)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInvalidationList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingUpdates)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
  tmp->mARIAOwnsHash.Clear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DocAccessible)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END_INHERITING(HyperTextAccessible)

NS_IMPL_ADDREF_INHERITED(DocAccessible, HyperTextAccessible)
NS_IMPL_RELEASE_INHERITED(DocAccessible, HyperTextAccessible)

////////////////////////////////////////////////////////////////////////////////
// nsIAccessible

ENameValueFlag DocAccessible::DirectName(nsString& aName) const {
  aName.Truncate();

  if (mParent) {
    mParent->Name(aName);  // Allow owning iframe to override the name
  }
  if (aName.IsEmpty()) {
    // Allow name via aria-labelledby or title attribute
    LocalAccessible::DirectName(aName);
  }
  if (aName.IsEmpty()) {
    Title(aName);  // Try title element
  }
  if (aName.IsEmpty()) {  // Last resort: use URL
    URL(aName);
  }

  if (aName.IsEmpty()) {
    aName.SetIsVoid(true);
  }

  return eNameOK;
}

// LocalAccessible public method
role DocAccessible::NativeRole() const {
  nsCOMPtr<nsIDocShell> docShell = nsCoreUtils::GetDocShellFor(mDocumentNode);
  if (docShell) {
    nsCOMPtr<nsIDocShellTreeItem> sameTypeRoot;
    docShell->GetInProcessSameTypeRootTreeItem(getter_AddRefs(sameTypeRoot));
    int32_t itemType = docShell->ItemType();
    if (sameTypeRoot == docShell) {
      // Root of content or chrome tree
      if (itemType == nsIDocShellTreeItem::typeChrome) {
        return roles::CHROME_WINDOW;
      }

      if (itemType == nsIDocShellTreeItem::typeContent) {
        return roles::DOCUMENT;
      }
    } else if (itemType == nsIDocShellTreeItem::typeContent) {
      return roles::DOCUMENT;
    }
  }

  return roles::PANE;  // Fall back;
}

EDescriptionValueFlag DocAccessible::Description(nsString& aDescription) const {
  if (mParent) mParent->Description(aDescription);

  if (HasOwnContent() && aDescription.IsEmpty()) {
    nsTextEquivUtils::GetTextEquivFromIDRefs(this, nsGkAtoms::aria_describedby,
                                             aDescription);
  }
  return eDescriptionFromARIA;
}

// LocalAccessible public method
uint64_t DocAccessible::NativeState() const {
  // Document is always focusable.
  uint64_t state =
      states::FOCUSABLE;  // keep in sync with NativeInteractiveState() impl
  if (FocusMgr()->IsFocused(this)) state |= states::FOCUSED;

  // Expose stale state until the document is ready (DOM is loaded and tree is
  // constructed).
  if (!HasLoadState(eReady)) state |= states::STALE;

  // Expose state busy until the document and all its subdocuments is completely
  // loaded.
  if (!HasLoadState(eCompletelyLoaded)) state |= states::BUSY;

  nsIFrame* frame = GetFrame();
  if (!frame || !frame->IsVisibleConsideringAncestors(
                    nsIFrame::VISIBILITY_CROSS_CHROME_CONTENT_BOUNDARY)) {
    state |= states::INVISIBLE | states::OFFSCREEN;
  }

  RefPtr<EditorBase> editorBase = GetEditor();
  state |= editorBase ? states::EDITABLE : states::READONLY;

  // GetFrame() returns the root frame, which is normally what we want. However,
  // user-select: none might be set on the body, in which case this won't be
  // exposed on the root frame. Therefore, we explicitly use the body frame
  // here (if any).
  dom::Element* bodyEl = mDocumentNode->GetBodyElement();
  nsIFrame* bodyFrame = bodyEl ? bodyEl->GetPrimaryFrame() : nullptr;
  if ((state & states::EDITABLE) || (bodyFrame && bodyFrame->IsSelectable())) {
    // If the accessible is editable the layout selectable state only disables
    // mouse selection, but keyboard (shift+arrow) selection is still possible.
    state |= states::SELECTABLE_TEXT;
  }

  return state;
}

uint64_t DocAccessible::NativeInteractiveState() const {
  // Document is always focusable.
  return states::FOCUSABLE;
}

bool DocAccessible::NativelyUnavailable() const { return false; }

// LocalAccessible public method
void DocAccessible::ApplyARIAState(uint64_t* aState) const {
  // Grab states from content element.
  if (mContent) LocalAccessible::ApplyARIAState(aState);

  // Allow iframe/frame etc. to have final state override via ARIA.
  if (mParent) mParent->ApplyARIAState(aState);
}

Accessible* DocAccessible::FocusedChild() {
  // Return an accessible for the current global focus, which does not have to
  // be contained within the current document.
  return FocusMgr()->FocusedAccessible();
}

void DocAccessible::TakeFocus() const {
  // Focus the document.
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  RefPtr<dom::Element> newFocus;
  dom::AutoHandlingUserInputStatePusher inputStatePusher(true);
  fm->MoveFocus(mDocumentNode->GetWindow(), nullptr,
                nsFocusManager::MOVEFOCUS_ROOT, 0, getter_AddRefs(newFocus));
}

// HyperTextAccessible method
already_AddRefed<EditorBase> DocAccessible::GetEditor() const {
  // Check if document is editable (designMode="on" case). Otherwise check if
  // the body element, or the root element when there is no body, is editable.
  // The editable flag cascades down from the root, so it's usually sufficient
  // to check the attribute's existence on the body. However content _can_ be
  // shoehorned directly into the root element, so we still check the root if
  // the body doesn't exist.
  dom::Element* editableEl = mDocumentNode->GetBodyElement();
  if (!editableEl) {
    editableEl = mDocumentNode->GetRootElement();
  }
  if (!mDocumentNode->IsInDesignMode() &&
      (!editableEl || !editableEl->HasFlag(NODE_IS_EDITABLE))) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShell> docShell = mDocumentNode->GetDocShell();
  if (!docShell) {
    return nullptr;
  }

  nsCOMPtr<nsIEditingSession> editingSession;
  docShell->GetEditingSession(getter_AddRefs(editingSession));
  if (!editingSession) return nullptr;  // No editing session interface

  RefPtr<HTMLEditor> htmlEditor =
      editingSession->GetHTMLEditorForWindow(mDocumentNode->GetWindow());
  if (!htmlEditor) {
    return nullptr;
  }

  bool isEditable = false;
  htmlEditor->GetIsDocumentEditable(&isEditable);
  if (isEditable) {
    return htmlEditor.forget();
  }

  return nullptr;
}

// DocAccessible public method

void DocAccessible::URL(nsAString& aURL) const {
  aURL.Truncate();
  nsCOMPtr<nsISupports> container = mDocumentNode->GetContainer();
  nsCOMPtr<nsIWebNavigation> webNav(do_GetInterface(container));
  if (MOZ_UNLIKELY(!webNav)) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  webNav->GetCurrentURI(getter_AddRefs(uri));
  if (MOZ_UNLIKELY(!uri)) {
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
  nsAutoCString theURL;
  if (NS_SUCCEEDED(exposableURI->GetSpec(theURL))) {
    CopyUTF8toUTF16(theURL, aURL);
  }
}

void DocAccessible::Title(nsString& aTitle) const {
  mDocumentNode->GetTitle(aTitle);
}

void DocAccessible::MimeType(nsAString& aType) const {
  mDocumentNode->GetContentType(aType);
}

void DocAccessible::DocType(nsAString& aType) const {
  dom::DocumentType* docType = mDocumentNode->GetDoctype();
  if (docType) docType->GetPublicId(aType);
}

// Certain cache domain updates might require updating other cache domains.
// This function takes the given cache domains and returns those cache domains
// plus any other required associated cache domains. Made for use with
// QueueCacheUpdate.
static uint64_t GetCacheDomainsQueueUpdateSuperset(uint64_t aCacheDomains) {
  // Text domain updates imply updates to the TextOffsetAttributes and
  // TextBounds domains.
  if (aCacheDomains & CacheDomain::Text) {
    aCacheDomains |= CacheDomain::TextOffsetAttributes;
    aCacheDomains |= CacheDomain::TextBounds;
  }
  // Bounds domain updates imply updates to the TextBounds domain.
  if (aCacheDomains & CacheDomain::Bounds) {
    aCacheDomains |= CacheDomain::TextBounds;
  }
  return aCacheDomains;
}

bool DocAccessible::IsPrintDoc() const {
  if (!mDocumentNode || !mDocumentNode->IsStaticDocument()) {
    return false;
  }
  // A DocAccessible should only ever be created for a static document if it's a
  // print document.
  MOZ_ASSERT(mDocumentNode->GetBrowsingContext()->Top()->GetIsPrinting());
  return true;
}

uint64_t DocAccessible::EffectiveCacheDomains() const {
  if (IsPrintDoc()) {
    return kPdfCacheDomains;
  }
  return nsAccessibilityService::GetActiveCacheDomains();
}

void DocAccessible::QueueCacheUpdate(LocalAccessible* aAcc, uint64_t aNewDomain,
                                     bool aBypassActiveDomains) {
  if (!mIPCDoc || !HasLoadState(eTreeConstructed)) {
    // If we're still building the initial tree, we don't need to queue cache
    // updates because the initial cache hasn't been sent yet. The update will
    // be included as part of the initial cache.
    return;
  }
  // These strong references aren't necessary because WithEntryHandle is
  // guaranteed to run synchronously. However, static analysis complains without
  // them.
  RefPtr<DocAccessible> self = this;
  RefPtr<LocalAccessible> acc = aAcc;
  size_t arrayIndex =
      mQueuedCacheUpdatesHash.WithEntryHandle(aAcc, [self, acc](auto&& entry) {
        if (entry.HasEntry()) {
          // This LocalAccessible has already been queued. Return its index in
          // the queue array so we can update its queued domains.
          return entry.Data();
        }
        // Add this LocalAccessible to the queue array.
        size_t index = self->mQueuedCacheUpdatesArray.Length();
        self->mQueuedCacheUpdatesArray.EmplaceBack(std::make_pair(acc, 0));
        // Also add it to the hash map so we can avoid processing the same
        // LocalAccessible twice.
        return entry.Insert(index);
      });

  // We may need to bypass the active domain restriction when populating domains
  // for the first time. In that case, queue cache updates regardless of domain.
  if (aBypassActiveDomains) {
    auto& [arrayAcc, domain] = mQueuedCacheUpdatesArray[arrayIndex];
    MOZ_ASSERT(arrayAcc == aAcc);
    domain |= aNewDomain;
    Controller()->ScheduleProcessing();
    return;
  }

  // Potentially queue updates for required related domains.
  const uint64_t newDomains = GetCacheDomainsQueueUpdateSuperset(aNewDomain);

  // Only queue cache updates for domains that are active.
  const uint64_t domainsToUpdate = EffectiveCacheDomains() & newDomains;

  // Avoid queueing cache updates if we have no domains to update.
  if (domainsToUpdate == CacheDomain::None) {
    return;
  }

  auto& [arrayAcc, domain] = mQueuedCacheUpdatesArray[arrayIndex];
  MOZ_ASSERT(arrayAcc == aAcc);
  domain |= domainsToUpdate;
  Controller()->ScheduleProcessing();
}

void DocAccessible::QueueCacheUpdateForDependentRelations(
    LocalAccessible* aAcc, const nsAttrValue* aOldId) {
  if (!mIPCDoc || !aAcc || !aAcc->IsInDocument() || aAcc->IsDefunct()) {
    return;
  }
  dom::Element* el = aAcc->Elm();
  if (!el) {
    return;
  }

  // We call this function when we've noticed an ID change, or when an acc
  // is getting bound to its document. We need to ensure any existing accs
  // that depend on this acc's ID or Element have their relation cache entries
  // updated.
  RelatedAccIterator iter(this, el, nullptr);
  while (LocalAccessible* relatedAcc = iter.Next()) {
    if (relatedAcc->IsDefunct() || !relatedAcc->IsInDocument() ||
        mInsertedAccessibles.Contains(relatedAcc)) {
      continue;
    }
    QueueCacheUpdate(relatedAcc, CacheDomain::Relations);
  }

  if (aOldId && !aOldId->IsEmptyString()) {
    // If we have an old ID, we need to update any accessibles that depended on
    // that ID as well.
    MOZ_ASSERT(aOldId->Type() == nsAttrValue::eAtom);
    RefPtr<nsAtom> id = aOldId->GetAtomValue();
    auto* providers = GetRelProviders(el, id);
    if (providers) {
      for (auto& provider : *providers) {
        if (LocalAccessible* oldRelatedAcc =
                GetAccessible(provider->mContent)) {
          QueueCacheUpdate(oldRelatedAcc, CacheDomain::Relations);
        }
      }
    }
  }

  if (const nsIFrame* anchorFrame = nsCoreUtils::GetAnchorForPositionedFrame(
          mPresShell, aAcc->GetFrame())) {
    // If this accessible is anchored, retrieve the anchor and update its
    // relations.
    if (LocalAccessible* anchorAcc = GetAccessible(anchorFrame->GetContent())) {
      if (!mInsertedAccessibles.Contains(anchorAcc)) {
        QueueCacheUpdate(anchorAcc, CacheDomain::Relations);
      }
    }
  }

  if (nsIFrame* positionedFrame = nsCoreUtils::GetPositionedFrameForAnchor(
          mPresShell, aAcc->GetFrame())) {
    // If this accessible is an anchor, retrieve the positioned frame and
    // refresh the cache on all its anchors.
    if (LocalAccessible* targetAcc =
            GetAccessible(positionedFrame->GetContent())) {
      RefreshAnchorRelationCacheForTarget(targetAcc);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible

void DocAccessible::Init() {
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eDocCreate)) {
    logging::DocCreate("document initialize", mDocumentNode, this);
  }
#endif

  // Our WindowGlobal might already be managing a PDocAccessible for a
  // document that hasn't been shut down yet; e.g. the initial about:blank. Shut
  // that one down now so we don't end up with two DocAccessibles alive for the
  // same WindowGlobal.
  if (dom::WindowGlobalChild* wgc = mDocumentNode->GetWindowGlobalChild()) {
    if (auto* actor =
            LoneManagedOrNullAsserts(wgc->ManagedPDocAccessibleChild())) {
      if (DocAccessible* prevDocAcc =
              static_cast<DocAccessibleChild*>(actor)->GetDocAccessible()) {
        prevDocAcc->Shutdown();
      }
    }
  }

  // Initialize notification controller.
  mNotificationController =
      MakeRefPtr<NotificationController>(this, mPresShell);

  // Mark the DocAccessible as loaded if its DOM document is already loaded at
  // this point. This can happen for one of three reasons:
  // 1. A11y was started late.
  // 2. DOM loading for a document (probably an in-process iframe) completed
  // before its Accessible container was created.
  // 3. The PresShell for the document was created after DOM loading completed.
  // In that case, we tried to create the DocAccessible when DOM loading
  // completed, but we can't create a DocAccessible without a PresShell, so
  // this failed. The DocAccessible was subsequently created due to a layout
  // notification.
  if (mDocumentNode->GetReadyStateEnum() ==
          dom::Document::READYSTATE_COMPLETE &&
      !mDocumentNode->IsUncommittedInitialDocument()) {
    mLoadState |= eDOMLoaded;
    // If this happened due to reasons 1 or 2, it isn't *necessary* to fire a
    // doc load complete event. If it happened due to reason 3, we need to fire
    // doc load complete because clients (especially tests) might be waiting
    // for the document to load using this event. We can't distinguish why this
    // happened at this point, so just fire it regardless. It won't do any
    // harm even if it isn't necessary. We set mLoadEventType here and it will
    // be fired in ProcessLoad as usual.
    mLoadEventType = nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE;
  } else if (mDocumentNode->IsUncommittedInitialDocument()) {
    // The initial about:blank always has its readyState as "complete"
    // even if it didn't fire a load event yet. We cannot know whether
    // it will load, so mark it loaded to avoid waiting for it.
    mLoadState |= eDOMLoaded;
  }

  AddEventListeners();
}

void DocAccessible::Shutdown() {
  if (!mPresShell) {  // already shutdown
    return;
  }

#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eDocDestroy)) {
    logging::DocDestroy("document shutdown", mDocumentNode, this);
  }
#endif

  // Mark the document as shutdown before AT is notified about the document
  // removal from its container (valid for root documents on ATK and due to
  // some reason for MSAA, refer to bug 757392 for details).
  MOZ_DIAGNOSTIC_ASSERT(!IsDefunct(),
                        "Already marked defunct. Reentrant shutdown!");
  mStateFlags |= eIsDefunct;

  if (mNotificationController) {
    mNotificationController->Shutdown();
    mNotificationController = nullptr;
  }

  RemoveEventListeners();

  if (mParent) {
    DocAccessible* parentDocument = mParent->Document();
    if (parentDocument) parentDocument->RemoveChildDocument(this);

    mParent->RemoveChild(this);
    MOZ_ASSERT(!mParent, "Parent has to be null!");
  }

  mPresShell->SetDocAccessible(nullptr);
  mPresShell = nullptr;  // Avoid reentrancy

  // Walk the array backwards because child documents remove themselves from the
  // array as they are shutdown.
  int32_t childDocCount = mChildDocuments.Length();
  for (int32_t idx = childDocCount - 1; idx >= 0; idx--) {
    mChildDocuments[idx]->Shutdown();
  }

  mChildDocuments.Clear();
  // mQueuedCacheUpdates* can contain a reference to this document (ex. if the
  // doc is scrollable and we're sending a scroll position update). Clear the
  // map here to avoid creating ref cycles.
  mQueuedCacheUpdatesArray.Clear();
  mQueuedCacheUpdatesHash.Clear();

  // XXX thinking about ordering?
  if (mIPCDoc) {
    MOZ_ASSERT(IPCAccessibilityActive());
    mIPCDoc->Shutdown();
    MOZ_ASSERT(!mIPCDoc);
  }

  mDependentIDsHashes.Clear();
  mDependentElementsMap.Clear();
  mNodeToAccessibleMap.Clear();

  mAnchorJumpElm = nullptr;
  mInvalidationList.Clear();
  mPendingUpdates.Clear();

  for (auto iter = mAccessibleCache.Iter(); !iter.Done(); iter.Next()) {
    LocalAccessible* accessible = iter.Data();
    MOZ_ASSERT(accessible);
    if (accessible) {
      // This might have been focused with FocusManager::ActiveItemChanged. In
      // that case, we must notify FocusManager so that it clears the active
      // item. Otherwise, it will hold on to a defunct Accessible. Normally,
      // this happens in UnbindFromDocument, but we don't call that when the
      // whole document shuts down.
      if (FocusMgr()->WasLastFocused(accessible)) {
        FocusMgr()->ActiveItemChanged(nullptr);
#ifdef A11Y_LOG
        if (logging::IsEnabled(logging::eFocus)) {
          logging::ActiveItemChangeCausedBy("doc shutdown", accessible);
        }
#endif
      }
      if (!accessible->IsDefunct()) {
        // Unlink parent to avoid its cleaning overhead in shutdown.
        accessible->mParent = nullptr;
        accessible->Shutdown();
      }
    }
    iter.Remove();
  }

  HyperTextAccessible::Shutdown();

  MOZ_ASSERT(GetAccService());
  GetAccService()->NotifyOfDocumentShutdown(this, mDocumentNode);
  mDocumentNode = nullptr;
}

nsIFrame* DocAccessible::GetFrame() const {
  nsIFrame* root = nullptr;
  if (mPresShell) {
    root = mPresShell->GetRootFrame();
  }

  return root;
}

nsINode* DocAccessible::GetNode() const { return mDocumentNode; }

// DocAccessible protected member
nsRect DocAccessible::RelativeBounds(nsIFrame** aRelativeFrame) const {
  *aRelativeFrame = GetFrame();

  dom::Document* document = mDocumentNode;
  dom::Document* parentDoc = nullptr;

  nsRect bounds;
  while (document) {
    PresShell* presShell = document->GetPresShell();
    if (!presShell) {
      return nsRect();
    }

    nsRect scrollPort;
    ScrollContainerFrame* sf = presShell->GetRootScrollContainerFrame();
    if (sf) {
      scrollPort = sf->GetScrollPortRect();
    } else {
      nsIFrame* rootFrame = presShell->GetRootFrame();
      if (!rootFrame) return nsRect();

      scrollPort = rootFrame->GetRect();
    }

    if (parentDoc) {  // After first time thru loop
      // XXXroc bogus code! scrollPort is relative to the viewport of
      // this document, but we're intersecting rectangles derived from
      // multiple documents and assuming they're all in the same coordinate
      // system. See bug 514117.
      bounds.IntersectRect(scrollPort, bounds);
    } else {  // First time through loop
      bounds = scrollPort;
    }

    document = parentDoc = document->GetInProcessParentDocument();
  }

  return bounds;
}

// DocAccessible protected member
nsresult DocAccessible::AddEventListeners() {
  SelectionMgr()->AddDocSelectionListener(mPresShell);
  return NS_OK;
}

// DocAccessible protected member
nsresult DocAccessible::RemoveEventListeners() {
  // Remove listeners associated with content documents
  NS_ASSERTION(mDocumentNode, "No document during removal of listeners.");

  if (mScrollWatchTimer) {
    mScrollWatchTimer->Cancel();
    mScrollWatchTimer = nullptr;
    NS_RELEASE_THIS();  // Kung fu death grip
  }

  SelectionMgr()->RemoveDocSelectionListener(mPresShell);
  return NS_OK;
}

void DocAccessible::ScrollTimerCallback(nsITimer* /*aTimer*/, void* aClosure) {
  DocAccessible* docAcc = reinterpret_cast<DocAccessible*>(aClosure);

  if (docAcc) {
    // Dispatch a scroll-end for all entries in table. They have not
    // been scrolled in at least `kScrollEventInterval`.
    for (auto iter = docAcc->mLastScrollingDispatch.Iter(); !iter.Done();
         iter.Next()) {
      docAcc->DispatchScrollingEvent(iter.Key(),
                                     nsIAccessibleEvent::EVENT_SCROLLING_END);
      iter.Remove();
    }

    if (docAcc->mScrollWatchTimer) {
      docAcc->mScrollWatchTimer = nullptr;
      NS_RELEASE(docAcc);  // Release kung fu death grip
    }
  }
}

void DocAccessible::HandleScroll(nsINode* aTarget) {
  nsINode* target = aTarget;
  LocalAccessible* targetAcc = GetAccessible(target);
  if (!targetAcc && target->IsInNativeAnonymousSubtree()) {
    // The scroll event for textareas comes from a native anonymous div. We need
    // the closest non-anonymous ancestor to get the right Accessible.
    target = target->GetClosestNativeAnonymousSubtreeRootParentOrHost();
    targetAcc = GetAccessible(target);
  }
  // Regardless of our scroll timer, we need to send a cache update
  // to ensure the next Bounds() query accurately reflects our position
  // after scrolling.
  if (targetAcc) {
    QueueCacheUpdate(targetAcc, CacheDomain::ScrollPosition);
  }

  const uint32_t kScrollEventInterval = 100;
  // If we haven't dispatched a scrolling event for a target in at least
  // kScrollEventInterval milliseconds, dispatch one now.
  mLastScrollingDispatch.WithEntryHandle(target, [&](auto&& lastDispatch) {
    const TimeStamp now = TimeStamp::Now();

    if (!lastDispatch ||
        (now - lastDispatch.Data()).ToMilliseconds() >= kScrollEventInterval) {
      // We can't fire events on a document whose tree isn't constructed yet.
      if (HasLoadState(eTreeConstructed)) {
        DispatchScrollingEvent(target, nsIAccessibleEvent::EVENT_SCROLLING);
      }
      lastDispatch.InsertOrUpdate(now);
    }
  });

  // If timer callback is still pending, push it 100ms into the future.
  // When scrolling ends and we don't fire HandleScroll anymore, the
  // timer callback will fire and dispatch an EVENT_SCROLLING_END.
  if (!mScrollWatchTimer) {
    // Can only fail on OOM and in that case we'd crash.
    mScrollWatchTimer = NS_NewTimer();
    NS_ADDREF_THIS();  // Kung fu death grip
  }
  mScrollWatchTimer->InitWithNamedFuncCallback(
      ScrollTimerCallback, this, kScrollEventInterval, nsITimer::TYPE_ONE_SHOT,
      "a11y::DocAccessible::ScrollPositionDidChange"_ns);
}

std::pair<nsPoint, nsRect> DocAccessible::ComputeScrollData(
    const LocalAccessible* aAcc, bool aShouldScaleByResolution) {
  nsPoint scrollPoint;
  nsRect scrollRange;

  if (nsIFrame* frame = aAcc->GetFrame()) {
    ScrollContainerFrame* sf = aAcc == this
                                   ? mPresShell->GetRootScrollContainerFrame()
                                   : frame->GetScrollTargetFrame();

    // If there is no scrollable frame, it's likely a scroll in a popup, like
    // <select>. Return a scroll offset and range of 0. The scroll info
    // is currently only used on Android, and popups are rendered natively
    // there.
    if (sf) {
      scrollPoint = sf->GetScrollPosition();
      scrollRange = sf->GetScrollRange();

      if (aShouldScaleByResolution) {
        scrollPoint = scrollPoint.ApplyResolution(mPresShell->GetResolution());
        scrollRange.ScaleRoundOut(mPresShell->GetResolution());
      }
    }
  }

  return {scrollPoint, scrollRange};
}

// When a reflected element IDL attribute changes, we might get the following
// synchronous calls:
// 1. AttributeWillChange for the element.
// 2. AttributeWillChange for the content attribute.
// 3. AttributeChanged for the content attribute.
// 4. AttributeChanged for the element.
// Since the content attribute value is "" for any element, we won't always get
// 2 or 3. Even if we do, they might occur after the element has already
// changed, which means we can't detect any relevant state changes there; e.g.
// mPrevStateBits. Thus, we need 1 and 4, and we must ignore 2 and 3. To
// facilitate this, sIsAttrElementChanging will be set to true for 2 and 3.
static bool sIsAttrElementChanging = false;

void DocAccessible::AttributeWillChange(dom::Element* aElement,
                                        int32_t /*aNameSpaceID*/,
                                        nsAtom* aAttribute,
                                        AttrModType aModType) {
  if (sIsAttrElementChanging) {
    // See the comment above the definition of sIsAttrElementChanging.
    return;
  }
  LocalAccessible* accessible = GetAccessible(aElement);
  if (!accessible) {
    if (aElement != mContent) return;

    accessible = this;
  }

  // Update dependent IDs cache. Take care of elements that are accessible
  // because dependent IDs cache doesn't contain IDs from non accessible
  // elements. We do this for attribute additions as well because there might
  // be an ElementInternals default value.
  RemoveDependentIDsFor(accessible, aAttribute);
  RemoveDependentElementsFor(accessible, aAttribute);

  if (aAttribute == nsGkAtoms::id) {
    if (accessible->IsActiveDescendant()) {
      auto event =
          MakeRefPtr<AccStateChangeEvent>(accessible, states::ACTIVE, false);
      FireDelayedEvent(event);
    }

    RelocateARIAOwnedIfNeeded(aElement);
  }

  if (aAttribute == nsGkAtoms::aria_activedescendant) {
    if (LocalAccessible* activeDescendant = accessible->CurrentItem()) {
      auto event = MakeRefPtr<AccStateChangeEvent>(activeDescendant,
                                                   states::ACTIVE, false);
      FireDelayedEvent(event);
    }
  }

  if ((aModType == AttrModType::Modification ||
       aModType == AttrModType::Removal)) {
    // If this is a modification or removal of aria-actions, and the
    // accessible's name is calculated by the subtree, there may be a change to
    // the name of the accessible.
    // If this is a modification or removal of an id, an aria-actions relation
    // might be severed, and thus change the name of any ancestors.
    // XXX: We don't track the actual changes, so the name change event might
    // be fired for not actual name change, but better to fire an event than to
    // not.
    MaybeHandleChangeToAriaActions(accessible, aAttribute);
  }

  // If attribute affects accessible's state, store the old state so we can
  // later compare it against the state of the accessible after the attribute
  // change.
  if (accessible->AttributeChangesState(aAttribute)) {
    mPrevStateBits = accessible->State();
  } else {
    mPrevStateBits = 0;
  }
}

void DocAccessible::AttributeChanged(dom::Element* aElement,
                                     int32_t aNameSpaceID, nsAtom* aAttribute,
                                     AttrModType aModType,
                                     const nsAttrValue* aOldValue) {
  if (!HasLoadState(eTreeConstructed)) {
    // We haven't built the initial tree yet, so there's nothing to mutate.
    return;
  }
  if (sIsAttrElementChanging) {
    // See the comment above the definition of sIsAttrElementChanging.
    return;
  }
  NS_ASSERTION(!IsDefunct(),
               "Attribute changed called on defunct document accessible!");

  // Proceed even if the element is not accessible because element may become
  // accessible if it gets certain attribute.
  if (UpdateAccessibleOnAttrChange(aElement, aAttribute)) return;

  // Update the accessible tree on aria-hidden change. Make sure to not create
  // a tree under aria-hidden='true'.
  if (aAttribute == nsGkAtoms::aria_hidden) {
    if (aria::IsValidARIAHidden(aElement)) {
      ContentRemoved(aElement);
    } else {
      ContentInserted(aElement, aElement->GetNextSibling());
    }
    return;
  }

  if (aAttribute == nsGkAtoms::slot &&
      !aElement->GetFlattenedTreeParentNode() && aElement != mContent) {
    // Element is inside a shadow host but is no longer slotted.
    mDoc->ContentRemoved(aElement);
    return;
  }

  LocalAccessible* accessible = GetAccessibleOrDocument(aElement);
  if (!accessible) {
    if (aModType == AttrModType::Addition &&
        aria::AttrCharacteristicsFor(aAttribute) & ATTR_GLOBAL) {
      // The element doesn't have an Accessible, but a global ARIA attribute
      // was just added, which means we should probably create an Accessible.
      ContentInserted(aElement, aElement->GetNextSibling());
      return;
    }
    if (aAttribute == nsGkAtoms::contenteditable) {
      // This element might have become editable, but it currently has no
      // Accessible. Create one if appropriate.
      ContentInserted(aElement, aElement->GetNextSibling());
      return;
    }
    // The element doesn't have an Accessible, so ignore the attribute
    // change.
    return;
  }

  MOZ_ASSERT(accessible->IsBoundToParent() || accessible->IsDoc(),
             "DOM attribute change on an accessible detached from the tree");

  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::id) {
    // TODO(1983819): updates to referenceTarget should trigger these same
    // actions
    dom::Element* elm = accessible->Elm();
    RelocateARIAOwnedIfNeeded(elm);
    ARIAActiveDescendantIDMaybeMoved(accessible);
    QueueCacheUpdate(accessible, CacheDomain::DOMNodeIDAndClass);
    QueueCacheUpdateForDependentRelations(accessible, aOldValue);
  }

  // The activedescendant universal property redirects accessible focus events
  // to the element with the id that activedescendant points to. Make sure
  // the tree up to date before processing. In other words, when a node has just
  // been inserted, the tree won't be up to date yet, so we must always schedule
  // an async notification so that a newly inserted node will be present in
  // the tree.
  if (aAttribute == nsGkAtoms::aria_activedescendant) {
    mNotificationController
        ->ScheduleNotification<DocAccessible, LocalAccessible>(
            this, &DocAccessible::ARIAActiveDescendantChanged, accessible);
  }

  // Defer to accessible any needed actions like changing states or emiting
  // events.
  accessible->DOMAttributeChanged(aNameSpaceID, aAttribute, aModType, aOldValue,
                                  mPrevStateBits);

  // Update dependent IDs cache. We handle elements with accessibles.
  // If the accessible or element with the ID doesn't exist yet the cache will
  // be updated when they are added.
  if (IsAdditionOrModification(aModType)) {
    AddDependentIDsFor(accessible, aAttribute);
    AddDependentElementsFor(accessible, aAttribute);

    // If this is a modification or addition of aria-actions, and the
    // accessible's name is calculated by the subtree, there may be a change to
    // the name of the accessible.
    // If this is a modification or addition of an id, an aria-actions relation
    // might be restored, and thus change the name of any ancestors.
    // XXX: We don't track the actual changes, so the name change event might
    // be fired for not actual name change, but better to fire an event than to
    // not.
    // In the case of a modification we may have already queued a name
    // change event in the `AttributeWillChange` stage, but we rely on
    // EventQueue to quash any duplicates.
    MaybeHandleChangeToAriaActions(accessible, aAttribute);
  }
}

void DocAccessible::ARIAAttributeDefaultWillChange(dom::Element* aElement,
                                                   nsAtom* aAttribute,
                                                   AttrModType aModType) {
  NS_ASSERTION(!IsDefunct(),
               "Attribute changed called on defunct document accessible!");

  if (aElement->HasAttr(aAttribute)) {
    return;
  }

  AttributeWillChange(aElement, kNameSpaceID_None, aAttribute, aModType);
}

void DocAccessible::ARIAAttributeDefaultChanged(dom::Element* aElement,
                                                nsAtom* aAttribute,
                                                AttrModType aModType) {
  NS_ASSERTION(!IsDefunct(),
               "Attribute changed called on defunct document accessible!");

  if (aElement->HasAttr(aAttribute)) {
    return;
  }

  AttributeChanged(aElement, kNameSpaceID_None, aAttribute, aModType, nullptr);
}

void DocAccessible::ARIAActiveDescendantChanged(LocalAccessible* aAccessible) {
  if (dom::Element* elm = aAccessible->Elm()) {
    nsAutoString id;
    if (dom::Element* activeDescendantElm =
            nsCoreUtils::GetAriaActiveDescendantElement(elm)) {
      LocalAccessible* activeDescendant = GetAccessible(activeDescendantElm);
      if (activeDescendant) {
        auto event = MakeRefPtr<AccStateChangeEvent>(activeDescendant,
                                                     states::ACTIVE, true);
        FireDelayedEvent(event);
        if (aAccessible->IsActiveWidget()) {
          FocusMgr()->ActiveItemChanged(activeDescendant, false);
#ifdef A11Y_LOG
          if (logging::IsEnabled(logging::eFocus)) {
            logging::ActiveItemChangeCausedBy("ARIA activedescedant changed",
                                              activeDescendant);
          }
#endif
        }
        return;
      }
    }

    // aria-activedescendant was cleared or changed to a non-existent node.
    // Move focus back to the element itself if it has DOM focus.
    if (aAccessible->IsActiveWidget()) {
      FocusMgr()->ActiveItemChanged(aAccessible, false);
#ifdef A11Y_LOG
      if (logging::IsEnabled(logging::eFocus)) {
        logging::ActiveItemChangeCausedBy("ARIA activedescedant cleared",
                                          aAccessible);
      }
#endif
    }
  }
}

void DocAccessible::ElementStateChanged(dom::Document* /*aDocument*/,
                                        dom::Element* aElement,
                                        dom::ElementState aStateMask) {
  const bool isEditable =
      aElement->State().HasState(dom::ElementState::READWRITE);
  if (aStateMask.HasState(dom::ElementState::READWRITE) &&
      IsBodyElement(aElement)) {
    // Any editable state reflected on the body element needs to be forwarded
    // to the doc accessible. We do this here instead of below because the body
    // is not guaranteed to have its own acc.
    auto event =
        MakeRefPtr<AccStateChangeEvent>(this, states::EDITABLE, isEditable);
    FireDelayedEvent(event);
    event =
        MakeRefPtr<AccStateChangeEvent>(this, states::READONLY, !isEditable);
    FireDelayedEvent(event);
  }

  LocalAccessible* accessible =
      aElement == mContent ? this : GetAccessible(aElement);

  if (!accessible) {
    return;
  }

  if (aStateMask.HasState(dom::ElementState::READWRITE) &&
      !accessible->IsTextField()) {
    auto event = MakeRefPtr<AccStateChangeEvent>(accessible, states::EDITABLE,
                                                 isEditable);
    FireDelayedEvent(event);
    if (accessible == this || aElement->IsHTMLElement(nsGkAtoms::article)) {
      // We want <article> to behave like a document in terms of readonly state.
      event = MakeRefPtr<AccStateChangeEvent>(accessible, states::READONLY,
                                              !isEditable);
      FireDelayedEvent(event);
    }

    if (aElement->HasAttr(nsGkAtoms::aria_owns)) {
      // If this has aria-owns, update children that are relocated into here.
      // If we are becoming editable, put them back into their original
      // containers, if we are becoming readonly, acquire them.
      mNotificationController->ScheduleRelocation(accessible);
    }

    // If this is a node inside of a newly editable subtree, it needs to be
    // un-aria-owned. And inversely, if the node becomes uneditable, allow the
    // node to be aria-owned.
    RelocateARIAOwnedIfNeeded(aElement);
  }

  if (aStateMask.HasState(dom::ElementState::CHECKED)) {
    const bool checked = aElement->State().HasState(dom::ElementState::CHECKED);
    LocalAccessible* widget = accessible->ContainerWidget();
    if (widget && widget->IsSelect()) {
      // Changing selection here changes what we cache for
      // the viewport.
      SetViewportCacheDirty(true);
      AccSelChangeEvent::SelChangeType selChangeType =
          checked ? AccSelChangeEvent::eSelectionAdd
                  : AccSelChangeEvent::eSelectionRemove;
      auto event =
          MakeRefPtr<AccSelChangeEvent>(widget, accessible, selChangeType);
      FireDelayedEvent(event);

      if (aElement->IsXULElement(nsGkAtoms::radio) && checked) {
        FocusMgr()->ActiveItemChanged(accessible);
#ifdef A11Y_LOG
        if (logging::IsEnabled(logging::eFocus)) {
          logging::ActiveItemChangeCausedBy("RadioStateChange", accessible);
        }
#endif
      }
      return;
    }

    auto event =
        MakeRefPtr<AccStateChangeEvent>(accessible, states::CHECKED, checked);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::INVALID)) {
    auto event = MakeRefPtr<AccStateChangeEvent>(accessible, states::INVALID);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::REQUIRED)) {
    auto event = MakeRefPtr<AccStateChangeEvent>(accessible, states::REQUIRED);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::MODAL)) {
    const bool isModal = aElement->State().HasState(dom::ElementState::MODAL);
    auto event =
        MakeRefPtr<AccStateChangeEvent>(accessible, states::MODAL, isModal);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::VISITED)) {
    auto event =
        MakeRefPtr<AccStateChangeEvent>(accessible, states::TRAVERSED, true);
    FireDelayedEvent(event);
  }

  // We only expose dom::ElementState::DEFAULT on buttons, but we can get
  // notifications for other controls like checkboxes.
  if (aStateMask.HasState(dom::ElementState::DEFAULT) &&
      accessible->IsButton()) {
    auto event = MakeRefPtr<AccStateChangeEvent>(accessible, states::DEFAULT);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::INDETERMINATE)) {
    auto event = MakeRefPtr<AccStateChangeEvent>(accessible, states::MIXED);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::DISABLED) &&
      !nsAccUtils::ARIAAttrValueIs(aElement, nsGkAtoms::aria_disabled,
                                   nsGkAtoms::_true, eCaseMatters)) {
    // The DOM disabled state has changed and there is no aria-disabled="true"
    // taking precedence.
    auto event =
        MakeRefPtr<AccStateChangeEvent>(accessible, states::UNAVAILABLE);
    FireDelayedEvent(event);
    event = MakeRefPtr<AccStateChangeEvent>(accessible, states::ENABLED);
    FireDelayedEvent(event);
    event = MakeRefPtr<AccStateChangeEvent>(accessible, states::SENSITIVE);
    FireDelayedEvent(event);
    // This likely changes focusability as well.
    event = MakeRefPtr<AccStateChangeEvent>(accessible, states::FOCUSABLE);
    FireDelayedEvent(event);
  }

  if (aStateMask.HasState(dom::ElementState::POPOVER_OPEN)) {
    // When a popover opens or closes, invalidate relations so invokers
    // can re-calculate if they have a DESCRIBED_BY or DETAILS relation;
    // (the popover tree might update to have new interactive elements,
    // which would change this association).
    QueueCacheUpdateForPopoverInvokers(aElement);
  }

  if (aStateMask.HasAtLeastOneOfStates(dom::ElementState::HEADING_LEVEL_BITS)) {
    QueueCacheUpdate(accessible, CacheDomain::GroupInfo);
  }
}

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible

#ifdef A11Y_LOG
nsresult DocAccessible::HandleAccEvent(AccEvent* aEvent) {
  if (logging::IsEnabled(logging::eDocLoad)) {
    logging::DocLoadEventHandled(aEvent);
  }

  return HyperTextAccessible::HandleAccEvent(aEvent);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Public members

nsPresContext* DocAccessible::PresContext() const {
  return mPresShell->GetPresContext();
}

void* DocAccessible::GetNativeWindow() const {
  if (!mPresShell) {
    return nullptr;
  }
  if (nsIWidget* widget = mPresShell->GetRootWidget()) {
    return widget->GetNativeData(NS_NATIVE_WINDOW);
  }
  return nullptr;
}

LocalAccessible* DocAccessible::GetAccessibleByUniqueIDInSubtree(
    void* aUniqueID) {
  LocalAccessible* child = GetAccessibleByUniqueID(aUniqueID);
  if (child) return child;

  uint32_t childDocCount = mChildDocuments.Length();
  for (uint32_t childDocIdx = 0; childDocIdx < childDocCount; childDocIdx++) {
    DocAccessible* childDocument = mChildDocuments.ElementAt(childDocIdx);
    child = childDocument->GetAccessibleByUniqueIDInSubtree(aUniqueID);
    if (child) return child;
  }

  return nullptr;
}

LocalAccessible* DocAccessible::GetAccessibleOrContainer(
    nsINode* aNode, bool aNoContainerIfPruned) const {
  if (!aNode || !aNode->GetComposedDoc()) {
    return nullptr;
  }

  nsINode* start = aNode;
  if (auto* shadowRoot = dom::ShadowRoot::FromNode(aNode)) {
    // This can happen, for example, when called within
    // SelectionManager::ProcessSelectionChanged due to focusing a direct
    // child of a shadow root.
    // GetFlattenedTreeParent works on children of a shadow root, but not the
    // shadow root itself.
    start = shadowRoot->GetHost();
    if (!start) {
      return nullptr;
    }
  }

  for (nsINode* currNode : dom::InclusiveFlatTreeAncestors(*start)) {
    // No container if is inside of aria-hidden subtree.
    if (aNoContainerIfPruned && currNode->IsElement() &&
        aria::IsValidARIAHidden(currNode->AsElement())) {
      return nullptr;
    }

    // Check if node is in zero-sized map
    if (aNoContainerIfPruned && currNode->IsHTMLElement(nsGkAtoms::map)) {
      if (nsIFrame* frame = currNode->AsContent()->GetPrimaryFrame()) {
        if (nsLayoutUtils::GetAllInFlowRectsUnion(frame, frame->GetParent())
                .IsEmpty()) {
          return nullptr;
        }
      }
    }

    if (LocalAccessible* accessible = GetAccessible(currNode)) {
      return accessible;
    }
  }

  return nullptr;
}

LocalAccessible* DocAccessible::GetContainerAccessible(nsINode* aNode) const {
  return aNode ? GetAccessibleOrContainer(aNode->GetFlattenedTreeParentNode())
               : nullptr;
}

LocalAccessible* DocAccessible::GetAccessibleOrDescendant(
    nsINode* aNode) const {
  LocalAccessible* acc = GetAccessibleOrDocument(aNode);
  if (acc) return acc;

  acc = GetContainerAccessible(aNode);
  if (acc) {
    TreeWalker walker(acc, aNode->AsContent(),
                      TreeWalker::eWalkCache | TreeWalker::eScoped);
    return walker.Next();
  }

  return nullptr;
}

void DocAccessible::BindToDocument(LocalAccessible* aAccessible,
                                   const nsRoleMapEntry* aRoleMapEntry) {
  // Put into DOM node cache.
  if (aAccessible->IsNodeMapEntry()) {
    mNodeToAccessibleMap.InsertOrUpdate(aAccessible->GetNode(), aAccessible);
  }

  // Put into unique ID cache.
  mAccessibleCache.InsertOrUpdate(aAccessible->UniqueID(), RefPtr{aAccessible});

  aAccessible->SetRoleMapEntry(aRoleMapEntry);

  if (aAccessible->HasOwnContent()) {
    AddDependentIDsFor(aAccessible);
    AddDependentElementsFor(aAccessible);

    nsIContent* content = aAccessible->GetContent();
    if (content->IsElement() &&
        nsAccUtils::HasARIAAttr(content->AsElement(), nsGkAtoms::aria_owns)) {
      mNotificationController->ScheduleRelocation(aAccessible);
    }
  }

  if (HasLoadState(eTreeConstructed)) {
    // Not in initial tree construction phase.
    // We need to track inserted accessibles so we don't mark them as moved
    // before their initial show event.
    // If this is the initial tree construction, we will push the tree to the
    // parent process before we process moves, so we will always need to mark a
    // relocated accessible as moved.
    mInsertedAccessibles.EnsureInserted(aAccessible);
  }

  QueueCacheUpdateForDependentRelations(aAccessible);
}

void DocAccessible::UnbindFromDocument(LocalAccessible* aAccessible) {
  NS_ASSERTION(mAccessibleCache.GetWeak(aAccessible->UniqueID()),
               "Unbinding the unbound accessible!");

  MOZ_ASSERT(!mARIAOwnsHash.Contains(aAccessible),
             "Container still lingering in mARIAOwnsHash");

  // Fire focus event on accessible having DOM focus if last focus was removed
  // from the tree.
  if (FocusMgr()->WasLastFocused(aAccessible)) {
    FocusMgr()->ActiveItemChanged(nullptr);
#ifdef A11Y_LOG
    if (logging::IsEnabled(logging::eFocus)) {
      logging::ActiveItemChangeCausedBy("tree shutdown", aAccessible);
    }
#endif
  }

  // Remove an accessible from node-to-accessible map if it exists there.
  if (aAccessible->IsNodeMapEntry() &&
      mNodeToAccessibleMap.Get(aAccessible->GetNode()) == aAccessible) {
    mNodeToAccessibleMap.Remove(aAccessible->GetNode());
  }

  aAccessible->mStateFlags |= eIsNotInDocument;

  // Update XPCOM part.
  xpcAccessibleDocument* xpcDoc = GetAccService()->GetCachedXPCDocument(this);
  if (xpcDoc) xpcDoc->NotifyOfShutdown(aAccessible);

  void* uniqueID = aAccessible->UniqueID();

  NS_ASSERTION(!aAccessible->IsDefunct(), "Shutdown the shutdown accessible!");
  aAccessible->Shutdown();

  mAccessibleCache.Remove(uniqueID);
}

void DocAccessible::ContentInserted(nsIContent* aStartChildNode,
                                    nsIContent* aEndChildNode) {
  // Ignore content insertions until we constructed accessible tree. Otherwise
  // schedule tree update on content insertion after layout.
  if (!mNotificationController || !HasLoadState(eTreeConstructed)) {
    return;
  }

  // The frame constructor guarantees that only ranges with the same parent
  // arrive here in presence of dynamic changes to the page, see
  // nsCSSFrameConstructor::IssueSingleInsertNotifications' callers.
  nsINode* parent = aStartChildNode->GetFlattenedTreeParentNode();
  if (!parent) {
    return;
  }

  LocalAccessible* container = AccessibleOrTrueContainer(parent);
  if (!container) {
    return;
  }

  AutoTArray<nsCOMPtr<nsIContent>, 10> list;
  for (nsIContent* node = aStartChildNode; node != aEndChildNode;
       node = node->GetNextSibling()) {
    MOZ_ASSERT(parent == node->GetFlattenedTreeParentNode());
    if (PruneOrInsertSubtree(node)) {
      list.AppendElement(node);
    }
  }

  mNotificationController->ScheduleContentInsertion(container, list);
}

void DocAccessible::ScheduleTreeUpdate(nsIContent* aContent) {
  if (mPendingUpdates.Contains(aContent)) {
    return;
  }
  mPendingUpdates.AppendElement(aContent);
  mNotificationController->ScheduleProcessing();
}

void DocAccessible::ProcessPendingUpdates() {
  auto updates = std::move(mPendingUpdates);
  for (auto update : updates) {
    if (update->GetComposedDoc() != mDocumentNode) {
      continue;
    }
    // The pruning logic will take care of avoiding unnecessary notifications.
    ContentInserted(update, update->GetNextSibling());
  }
}

bool DocAccessible::PruneOrInsertSubtree(nsIContent* aRoot,
                                         bool aIsInsertRoot) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessible::PruneOrInsertSubtree", A11Y, {},
                            ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_PruneOrInsertSubtree>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  bool insert = false;

  // In the case that we are, or are in, a shadow host, we need to assure
  // some accessibles are removed if they are not rendered anymore.
  nsIContent* shadowHost =
      aRoot->GetShadowRoot() ? aRoot : aRoot->GetContainingShadowHost();
  if (shadowHost) {
    // Check all explicit children in the host, if they are not slotted
    // then remove their accessibles and subtrees.
    for (nsIContent* childNode = shadowHost->GetFirstChild(); childNode;
         childNode = childNode->GetNextSibling()) {
      if (!childNode->GetPrimaryFrame() &&
          !nsCoreUtils::CanCreateAccessibleWithoutFrame(childNode)) {
        ContentRemoved(childNode);
      }
    }

    // If this is a slot, check to see if its fallback content is rendered,
    // if not - remove it.
    if (aRoot->IsHTMLElement(nsGkAtoms::slot)) {
      for (nsIContent* childNode = aRoot->GetFirstChild(); childNode;
           childNode = childNode->GetNextSibling()) {
        if (!childNode->GetPrimaryFrame() &&
            !nsCoreUtils::CanCreateAccessibleWithoutFrame(childNode)) {
          ContentRemoved(childNode);
        }
      }
    }
  }

  // If we already have an accessible, check if we need to remove it, recreate
  // it, or keep it in place.
  LocalAccessible* acc = GetAccessible(aRoot);
  if (acc) {
    MOZ_ASSERT(aRoot == acc->GetContent(),
               "LocalAccessible has differing content!");
#ifdef A11Y_LOG
    if (logging::IsEnabled(logging::eTree)) {
      logging::MsgBegin(
          "TREE", "inserted content already has accessible; doc: %p", this);
      logging::Node("content node", aRoot);
      logging::AccessibleInfo("accessible node", acc);
      logging::MsgEnd();
    }
#endif

    nsIFrame* frame = acc->GetFrame();
    if (frame) {
      acc->MaybeQueueCacheUpdateForStyleChanges();
      // If we got a new frame after being display:contents, we may have become
      // focusable.
      if (frame->IsFocusable()) {
        auto event = MakeRefPtr<AccStateChangeEvent>(acc, states::FOCUSABLE);
        FireDelayedEvent(event);
      }
    }

    // LocalAccessible has no frame and it's not display:contents. Remove it.
    // As well as removing the a11y subtree, we must also remove Accessibles
    // for DOM descendants, since some of these might be relocated Accessibles
    // and their DOM nodes are now hidden as well.
    if (!frame && !nsCoreUtils::CanCreateAccessibleWithoutFrame(aRoot)) {
      ContentRemoved(aRoot);
      return false;
    }

    if (!frame && aRoot->IsElement() &&
        aRoot->AsElement()->IsDisplayContents() && acc->mOldComputedStyle) {
      // This element has probably just become display: contents. We won't be
      // notified of a computed style change in this case. Also, the bounds we
      // cached previously are now invalid, but our normal bounds change
      // notifications won't fire either. Therefore, queue cache updates for
      // both.
      QueueCacheUpdate(acc, CacheDomain::Style | CacheDomain::Bounds);
      // If we became display: contents, we may have lost the focusable state.
      auto event =
          MakeRefPtr<AccStateChangeEvent>(acc, states::FOCUSABLE, false);
      FireDelayedEvent(event);
    }

    // If the frame is hidden because its ancestor is specified with
    // `content-visibility: hidden`, remove its Accessible.
    if (frame && frame->IsHiddenByContentVisibilityOnAnyAncestor(
                     nsIFrame::IncludeContentVisibility::Hidden)) {
      ContentRemoved(aRoot);
      return false;
    }

    // If it's a XULLabel that was reported as inserted, it was probably
    // reframed because a `value` attribute was added or removed. The anonymous
    // content backing @value is rebuilt, so the accessible needs to be
    // recreated. Remove it, and schedule for reconstruction. A label we merely
    // descended into was not reported as inserted, so nothing tells us it needs
    // recreating.
    if (aIsInsertRoot && acc->IsXULLabel()) {
      ContentRemoved(acc);
      return true;
    }

    if (frame && frame->IsReplaced() && frame->AccessibleType() == eImageType &&
        !aRoot->IsHTMLElement(nsGkAtoms::img)) {
      // This is an image specified using the CSS content property which
      // replaces the content of the node. Its frame might be reconstructed,
      // which means its alt text might have changed. We expose the alt text
      // as the name, so fire a name change event.
      // We will schedule this for reinsertion below, and prune any children if
      // they exist.
      FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, acc);
    } else if (frame &&
               (acc->IsImage() != (frame->AccessibleType() == eImageType))) {
      // It is a broken image that is being reframed because it either got
      // or lost an `alt` tag that would rerender this node as text.
      ContentRemoved(aRoot);
      return true;
    }

    // If the frame is an OuterDoc frame but this isn't an OuterDocAccessible,
    // we need to recreate the LocalAccessible. This can happen for embed or
    // object elements if their embedded content changes to be web content.
    if (frame && !acc->IsOuterDoc() &&
        frame->AccessibleType() == eOuterDocType) {
      ContentRemoved(aRoot);
      return true;
    }

    // If the content is focused, and is being re-framed, reset the selection
    // listener for the node because the previous selection listener is on the
    // old frame.
    if (aRoot->IsElement() && FocusMgr()->HasDOMFocus(aRoot)) {
      SelectionMgr()->SetControlSelectionListener(aRoot->AsElement());
    }

    // If the accessible is a table, or table part, its layout table
    // status may have changed. We need to invalidate the associated
    // mac table cache, which listens for the following event. We don't
    // use this cache when the core cache is enabled, so to minimise event
    // traffic only fire this event when that cache is off.
    if (acc->IsTable() || acc->IsTableRow() || acc->IsTableCell()) {
      LocalAccessible* table = nsAccUtils::TableFor(acc);
      if (table && table->IsTable()) {
        QueueCacheUpdate(table, CacheDomain::Table);
      }
    }

    // The accessible can be reparented or reordered in its parent.
    // We schedule it for reinsertion. For example, a slotted element
    // can change its slot attribute to a different slot.
    insert = true;

    // If the frame is invisible, remove it.
    // Normally, layout sends explicit a11y notifications for visibility
    // changes (see SendA11yNotifications in RestyleManager). However, if a
    // visibility change also reconstructs the frame, we must handle it here.
    if (frame && !frame->StyleVisibility()->IsVisible()) {
      ContentRemoved(aRoot);
      // There might be visible descendants, so we want to walk the subtree.
      // However, we know we don't want to reinsert this node, so we set insert
      // to false.
      insert = false;
    }
  } else {
    // If there is no current accessible, and the node has a frame, or is
    // display:contents, schedule it for insertion.
    if (aRoot->GetPrimaryFrame() ||
        nsCoreUtils::CanCreateAccessibleWithoutFrame(aRoot)) {
      // This may be a new subtree, the insertion process will recurse through
      // its descendants.
      if (!GetAccessibleOrDescendant(aRoot)) {
        return true;
      }

      // Content is not an accessible, but has accessible descendants.
      // We schedule this container for insertion strictly for the case where it
      // itself now needs an accessible. We will still need to recurse into the
      // descendant content to prune accessibles, and in all likelyness to
      // insert accessibles since accessible insertions will likeley get missed
      // in an existing subtree.
      insert = true;
    }
  }

  if (LocalAccessible* container = AccessibleOrTrueContainer(aRoot)) {
    AutoTArray<nsCOMPtr<nsIContent>, 10> list;
    dom::AllChildrenIterator iter =
        dom::AllChildrenIterator(aRoot, nsIContent::eAllChildren, true);
    while (nsIContent* childNode = iter.GetNextChild()) {
      if (PruneOrInsertSubtree(childNode, /* aIsInsertRoot */ false)) {
        list.AppendElement(childNode);
      }
    }

    if (!list.IsEmpty()) {
      mNotificationController->ScheduleContentInsertion(container, list);
    }
  }

  return insert;
}

void DocAccessible::RecreateAccessible(nsIContent* aContent) {
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eTree)) {
    logging::MsgBegin("TREE", "accessible recreated");
    logging::Node("content", aContent);
    logging::MsgEnd();
  }
#endif

  // XXX: we shouldn't recreate whole accessible subtree, instead we should
  // subclass hide and show events to handle them separately and implement their
  // coalescence with normal hide and show events. Note, in this case they
  // should be coalesced with normal show/hide events.
  ContentRemoved(aContent);
  ContentInserted(aContent, aContent->GetNextSibling());
}

void DocAccessible::ProcessInvalidationList() {
  // Invalidate children of container accessible for each element in
  // invalidation list. Allow invalidation list insertions while container
  // children are recached.
  for (uint32_t idx = 0; idx < mInvalidationList.Length(); idx++) {
    nsIContent* content = mInvalidationList[idx];
    if (!HasAccessible(content) && content->HasID()) {
      LocalAccessible* container = GetContainerAccessible(content);
      if (container) {
        // Check if the node is a target of aria-owns, and if so, don't process
        // it here and let DoARIAOwnsRelocation process it.
        AttrRelProviders* list =
            GetRelProviders(content->AsElement(), content->GetID());
        bool shouldProcess = !!list;
        if (shouldProcess) {
          for (uint32_t idx = 0; idx < list->Length(); idx++) {
            if (list->ElementAt(idx)->mRelAttr == nsGkAtoms::aria_owns) {
              shouldProcess = false;
              break;
            }
          }

          if (shouldProcess) {
            ProcessContentInserted(container, content);
          }
        }
      }
    }
  }

  mInvalidationList.Clear();
}

void DocAccessible::ProcessQueuedCacheUpdates(uint64_t aInitialDomains) {
  AUTO_PROFILER_MARKER_UNTYPED("DocAccessible::ProcessQueuedCacheUpdates", A11Y,
                               {});
  PerfStats::AutoMetricRecording<
      PerfStats::Metric::A11Y_ProcessQueuedCacheUpdate>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  nsTArray<CacheData> data;
  for (const auto& [acc, domain] : mQueuedCacheUpdatesArray) {
    if (acc && acc->IsInDocument() && !acc->IsDefunct()) {
      RefPtr<AccAttributes> fields = acc->BundleFieldsForCache(
          domain, CacheUpdateType::Update, aInitialDomains);

      if (fields->Count()) {
        data.AppendElement(CacheData(
            acc->IsDoc() ? 0 : reinterpret_cast<uint64_t>(acc->UniqueID()),
            fields));
      }
    }
  }

  mQueuedCacheUpdatesArray.Clear();
  mQueuedCacheUpdatesHash.Clear();

  if (mViewportCacheDirty) {
    RefPtr<AccAttributes> fields =
        BundleFieldsForCache(CacheDomain::Viewport, CacheUpdateType::Update);
    if (fields->Count()) {
      data.AppendElement(CacheData(0, fields));
    }
    mViewportCacheDirty = false;
  }

  if (data.Length()) {
    MOZ_ASSERT(IPCDoc(), "Shouldn't have queued updates with no IPC doc");
    IPCDoc()->SendCache(CacheUpdateType::Update, data);
  }
}

void DocAccessible::SendAccessiblesWillMove() {
  if (!mIPCDoc) {
    return;
  }
  nsTArray<uint64_t> ids;
  for (LocalAccessible* acc : mMovedAccessibles) {
    // If acc is defunct or not in a document, it was removed after it was
    // moved.
    if (!acc->IsDefunct() && acc->IsInDocument()) {
      ids.AppendElement(reinterpret_cast<uintptr_t>(acc->UniqueID()));
      // acc might have been re-parented. Since we cache bounds relative to the
      // parent, we need to update the cache.
      QueueCacheUpdate(acc, CacheDomain::Bounds);
    }
  }
  if (!ids.IsEmpty()) {
    mIPCDoc->SendAccessiblesWillMove(ids);
  }
}

LocalAccessible* DocAccessible::GetAccessibleEvenIfNotInMap(
    nsINode* aNode) const {
  if (!aNode->IsContent() ||
      !aNode->AsContent()->IsHTMLElement(nsGkAtoms::area)) {
    return GetAccessible(aNode);
  }

  // XXX Incorrect when multiple images use the same map; the area only tracks
  // the image frame that last set up the map.
  nsIFrame* frame =
      nsLayoutUtils::GetPrimaryOrAreaImageFrame(aNode->AsContent());
  nsImageFrame* imageFrame = do_QueryFrame(frame);
  if (imageFrame) {
    LocalAccessible* parent = GetAccessible(imageFrame->GetContent());
    if (parent) {
      if (HTMLImageMapAccessible* imageMap = parent->AsImageMap()) {
        return imageMap->GetChildAccessibleFor(aNode);
      }
      return nullptr;
    }
  }

  return GetAccessible(aNode);
}

////////////////////////////////////////////////////////////////////////////////
// Protected members

void DocAccessible::NotifyOfLoading(bool aIsReloading) {
  // Mark the document accessible as loading, if it stays alive then we'll mark
  // it as loaded when we receive proper notification.
  mLoadState &= ~eDOMLoaded;

  if (!IsLoadEventTarget()) return;

  if (aIsReloading && !mLoadEventType &&
      // We can't fire events on a document whose tree isn't constructed yet.
      HasLoadState(eTreeConstructed)) {
    // Fire reload and state busy events on existing document accessible while
    // event from user input flag can be calculated properly and accessible
    // is alive. When new document gets loaded then this one is destroyed.
    auto reloadEvent =
        MakeRefPtr<AccEvent>(nsIAccessibleEvent::EVENT_DOCUMENT_RELOAD, this);
    nsEventShell::FireEvent(reloadEvent);
  }

  // Fire state busy change event. Use delayed event since we don't care
  // actually if event isn't delivered when the document goes away like a shot.
  auto stateEvent = MakeRefPtr<AccStateChangeEvent>(this, states::BUSY, true);
  FireDelayedEvent(stateEvent);
}

void DocAccessible::DoInitialUpdate() {
  AUTO_PROFILER_MARKER_UNTYPED("DocAccessible::DoInitialUpdate", A11Y, {});
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_DoInitialUpdate>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  if (nsCoreUtils::IsTopLevelContentDocInProcess(mDocumentNode)) {
    mDocFlags |= eTopLevelContentDocInProcess;
    if (ShouldSendToParentProcess()) {
      if (dom::WindowGlobalChild* wgc = mDocumentNode->GetWindowGlobalChild()) {
        // In content processes, top level content documents are always
        // RootAccessibles.
        MOZ_ASSERT(IsRoot());
        if (!IPCDoc()) {
          RefPtr<DocAccessibleChild> ipcDoc = new DocAccessibleChild(this, wgc);
          MOZ_RELEASE_ASSERT(
              wgc->SendPDocAccessibleConstructor(ipcDoc, 0, IsPrintDoc()));
          // trying to recover from this failing is problematic
          SetIPCDoc(ipcDoc);
        }
      }
    }
  }

  // Set up a root element and ARIA role mapping.
  UpdateRootElement();

  // Build initial tree.
  CacheChildrenInSubtree(this);
  // CacheChildrenInSubtree doesn't handle aria-owns relocations. Process the
  // initial relocations now so they're reflected in the initial tree. This is
  // more efficient for IPC because it avoids sending the initial tree and then
  // immediately moving nodes afterward. It also avoids mutation event spam for
  // clients. Mutation events are suppressed because we don't have the
  // eTreeConstructed load state yet.
  mNotificationController->ProcessRelocations();

  mLoadState |= eTreeConstructed;

#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eVerbose)) {
    logging::Tree("TREE", "Initial subtree", this);
  }
  if (logging::IsEnabled(logging::eTreeSize)) {
    logging::TreeSize("TREE SIZE", "Initial subtree", this);
  }
#endif

  // Fire a reorder event on the OuterDocAccessible after the document tree is
  // constructed. Note that since this reorder event is processed by the parent
  // document, events targeted to this child document may be fired prior to this
  // reorder event. We don't fire a reorder event for remote documents; the
  // parent process handles that.
  if (!IPCDoc() && !IsRoot()) {
    LocalAccessible* parent = LocalParent();
    // If this is a static clone document for printing in the parent process, it
    // won't have a parent because it isn't bound to a XUL <browser> element.
    // That's okay.
    MOZ_ASSERT(parent || mDocumentNode->IsStaticDocument());
    if (parent) {
      auto reorderEvent = MakeRefPtr<AccReorderEvent>(LocalParent());
      ParentDocument()->FireDelayedEvent(reorderEvent);
    }
  }

  if (AppShutdown::IsShutdownImpending()) {
    return;
  }
  if (ShouldSendToParentProcess()) {
    DocAccessibleChild* ipcDoc = IPCDoc();
    MOZ_ASSERT(ipcDoc);
    if (ipcDoc) {
      // Send an initial update for this document and its attributes. Each acc
      // contained in this doc will have its initial update sent in
      // `InsertIntoIpcTree`.
      SendCache(EffectiveCacheDomains(), CacheUpdateType::Initial);

      for (auto idx = 0U; idx < mChildren.Length(); idx++) {
        ipcDoc->InsertIntoIpcTree(mChildren.ElementAt(idx), true);
      }
      ipcDoc->SendQueuedMutationEvents();
    }
  }
}

void DocAccessible::ProcessLoad() {
  mLoadState |= eCompletelyLoaded;

#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eDocLoad)) {
    logging::DocCompleteLoad(this, IsLoadEventTarget());
  }
#endif

  // Do not fire document complete/stop events for root chrome document
  // accessibles because screen readers start working on focus event in the case
  // of root chrome documents.
  if (!IsLoadEventTarget()) return;

  // Fire complete/load stopped if the load event type is given.
  if (mLoadEventType) {
    auto loadEvent = MakeRefPtr<AccEvent>(mLoadEventType, this);
    FireDelayedEvent(loadEvent);

    mLoadEventType = 0;
  }

  // Fire busy state change event.
  auto stateEvent = MakeRefPtr<AccStateChangeEvent>(this, states::BUSY, false);
  FireDelayedEvent(stateEvent);
}

void DocAccessible::AddDependentIDsFor(LocalAccessible* aRelProvider,
                                       nsAtom* aRelAttr) {
  dom::Element* relProviderEl = aRelProvider->Elm();
  if (!relProviderEl) return;

  for (auto relAttr : kRelationAttrs) {
    if (aRelAttr && aRelAttr != relAttr) continue;

    if (relAttr == nsGkAtoms::_for) {
      if (!relProviderEl->IsAnyOfHTMLElements(nsGkAtoms::label,
                                              nsGkAtoms::output)) {
        continue;
      }

    } else if (relAttr == nsGkAtoms::control) {
      if (!relProviderEl->IsAnyOfXULElements(nsGkAtoms::label,
                                             nsGkAtoms::description)) {
        continue;
      }
    }

    if (const nsAttrValue* parsedAttrValue =
            relProviderEl->GetParsedAttr(relAttr)) {
      if (parsedAttrValue->GetAtomCount() == 0) {
        continue;
      }
      MOZ_ASSERT(
          parsedAttrValue->Type() == nsAttrValue::eAtomArray ||
              parsedAttrValue->Type() == nsAttrValue::eAtom,
          "Attribute used for adding accessible relations must be parsed.");
      for (uint32_t i = 0; i < parsedAttrValue->GetAtomCount(); i++) {
        auto id = parsedAttrValue->AtomAt(static_cast<int32_t>(i));
        AttrRelProviders* providers =
            GetOrCreateRelProviders(relProviderEl, id);
        if (!providers) {
          continue;
        }

        AttrRelProvider* provider = new AttrRelProvider(relAttr, relProviderEl);
        if (!provider) {
          continue;
        }

        providers->AppendElement(provider);

        // We've got here during the children caching. If the referenced
        // content is not accessible then store it to pend its container
        // children invalidation (this happens immediately after the caching
        // is finished).
        dom::DocumentOrShadowRoot* docOrShadowRoot =
            relProviderEl->GetUncomposedDocOrConnectedShadowRoot();
        if (!docOrShadowRoot) {
          continue;
        }

        nsIContent* dependentContent = docOrShadowRoot->GetElementById(id);
        if (dependentContent && !HasAccessible(dependentContent)) {
          mInvalidationList.AppendElement(dependentContent);
        }
      }
    }
    // If the relation attribute is given then we don't have anything else to
    // check.
    if (aRelAttr) break;
  }

  // Make sure to schedule the tree update if needed.
  mNotificationController->ScheduleProcessing();
}

void DocAccessible::RemoveDependentIDsFor(LocalAccessible* aRelProvider,
                                          nsAtom* aRelAttr) {
  dom::Element* relProviderElm = aRelProvider->Elm();
  if (!relProviderElm) return;

  for (auto relationAttr : kRelationAttrs) {
    const nsStaticAtom* relAttr = relationAttr;
    if (aRelAttr && aRelAttr != relationAttr) continue;

    if (const nsAttrValue* parsedAttrValue =
            relProviderElm->GetParsedAttr(relAttr)) {
      if (parsedAttrValue->GetAtomCount() == 0) {
        continue;
      }
      MOZ_ASSERT(
          parsedAttrValue->Type() == nsAttrValue::eAtomArray ||
              parsedAttrValue->Type() == nsAttrValue::eAtom,
          "Attribute used for removing accessible relations must be parsed.");

      for (uint32_t i = 0; i < parsedAttrValue->GetAtomCount(); i++) {
        nsAtom* id = parsedAttrValue->AtomAt(static_cast<int32_t>(i));

        AttrRelProviders* providers = GetRelProviders(relProviderElm, id);
        if (providers) {
          providers->RemoveElementsBy(
              [relAttr, relProviderElm](const auto& provider) {
                return provider->mRelAttr == relAttr &&
                       provider->mContent == relProviderElm;
              });

          RemoveRelProvidersIfEmpty(relProviderElm, id);
        }
      }
    }

    // If the relation attribute is given then we don't have anything else to
    // check.
    if (aRelAttr) break;
  }
}

void DocAccessible::AddDependentElementsFor(LocalAccessible* aRelProvider,
                                            nsAtom* aRelAttr) {
  dom::Element* providerEl = aRelProvider->Elm();
  if (!providerEl) {
    return;
  }
  for (nsStaticAtom* attr : kSingleElementRelationIdlAttrs) {
    if (aRelAttr && aRelAttr != attr) {
      continue;
    }
    if (dom::Element* targetEl =
            providerEl->GetExplicitlySetAttrElement(attr)) {
      AttrRelProviders& providers =
          mDependentElementsMap.LookupOrInsert(targetEl);
      AttrRelProvider* provider = new AttrRelProvider(attr, providerEl);
      providers.AppendElement(provider);
    }
    // If the relation attribute was given, we've already handled it. We don't
    // have anything else to check.
    if (aRelAttr) {
      break;
    }
  }

  aria::AttrWithCharacteristicsIterator multipleElementsRelationIter(
      ATTR_REFLECT_ELEMENTS);
  while (multipleElementsRelationIter.Next()) {
    nsStaticAtom* attr = multipleElementsRelationIter.AttrName();
    if (aRelAttr && aRelAttr != attr) {
      continue;
    }
    if (auto elements = nsAccUtils::GetARIAElementsAttr(providerEl, attr)) {
      for (auto targetEl : *elements) {
        AttrRelProviders& providers =
            mDependentElementsMap.LookupOrInsert(targetEl);
        AttrRelProvider* provider = new AttrRelProvider(attr, providerEl);
        providers.AppendElement(provider);
      }
    }
    // If the relation attribute was given, we've already handled it. We don't
    // have anything else to check.
    if (aRelAttr) {
      break;
    }
  }
}

void DocAccessible::RemoveDependentElementsFor(LocalAccessible* aRelProvider,
                                               nsAtom* aRelAttr) {
  dom::Element* providerEl = aRelProvider->Elm();
  if (!providerEl) {
    return;
  }
  for (nsStaticAtom* attr : kSingleElementRelationIdlAttrs) {
    if (aRelAttr && aRelAttr != attr) {
      continue;
    }
    if (dom::Element* targetEl =
            providerEl->GetExplicitlySetAttrElement(attr)) {
      if (auto providers = mDependentElementsMap.Lookup(targetEl)) {
        providers.Data().RemoveElementsBy([attr,
                                           providerEl](const auto& provider) {
          return provider->mRelAttr == attr && provider->mContent == providerEl;
        });
        if (providers.Data().IsEmpty()) {
          providers.Remove();
        }
      }
    }
    // If the relation attribute was given, we've already handled it. We don't
    // have anything else to check.
    if (aRelAttr) {
      break;
    }
  }

  aria::AttrWithCharacteristicsIterator multipleElementsRelationIter(
      ATTR_REFLECT_ELEMENTS);
  while (multipleElementsRelationIter.Next()) {
    nsStaticAtom* attr = multipleElementsRelationIter.AttrName();
    if (aRelAttr && aRelAttr != attr) {
      continue;
    }
    if (auto elements = nsAccUtils::GetARIAElementsAttr(providerEl, attr)) {
      for (auto targetEl : *elements) {
        if (auto providers = mDependentElementsMap.Lookup(targetEl)) {
          providers.Data().RemoveElementsBy(
              [attr, providerEl](const auto& provider) {
                return provider->mRelAttr == attr &&
                       provider->mContent == providerEl;
              });
          if (providers.Data().IsEmpty()) {
            providers.Remove();
          }
        }
      }
    }

    // If the relation attribute was given, we've already handled it. We don't
    // have anything else to check.
    if (aRelAttr) {
      break;
    }
  }
}

bool DocAccessible::UpdateAccessibleOnAttrChange(dom::Element* aElement,
                                                 nsAtom* aAttribute) {
  if (aAttribute == nsGkAtoms::role) {
    // If we're dealing with a role change on the body or the root element
    // we may need to update the role exposed for the doc accessible.
    // We never want to recreate this acc during a role change, as doing
    // so would require we recreate all of the document's content too.
    // The doc acc has a small set of valid roles, none of which change
    // the interfaces we should expose, so skipping recreation is safe.
    if (mContent == aElement || IsBodyElement(aElement)) {
      UpdateDocRoleMapEntry();
      return true;
    }

    // Recreate the accessible when role is changed because we might require a
    // different accessible class for the new role or the accessible may expose
    // a different sets of interfaces (COM restriction).
    RecreateAccessible(aElement);

    return true;
  }

  if (aAttribute == nsGkAtoms::multiple) {
    if (dom::HTMLSelectElement* select =
            dom::HTMLSelectElement::FromNode(aElement)) {
      if (select->Size() <= 1) {
        // Adding the 'multiple' attribute to a select that has a size of 1
        // creates a listbox as opposed to a combobox with a popup combobox
        // list. Removing the attribute does the opposite.
        RecreateAccessible(aElement);
        return true;
      }
    }
  }

  if (aAttribute == nsGkAtoms::size &&
      aElement->IsHTMLElement(nsGkAtoms::select)) {
    // Changing the size of a select element can potentially change it from a
    // combobox button to a listbox with different underlying implementations.
    RecreateAccessible(aElement);
    return true;
  }

  if (aAttribute == nsGkAtoms::type) {
    // If the input[type] changes, we should recreate the accessible.
    RecreateAccessible(aElement);
    return true;
  }

  if (aAttribute == nsGkAtoms::href &&
      !nsCoreUtils::HasClickListener(aElement)) {
    // If the href is added or removed for a or area elements without click
    // listeners, we need to recreate the accessible since the role might have
    // changed. Without an href or click listener, the accessible must be a
    // generic.
    if (aElement->IsHTMLElement(nsGkAtoms::a) ||
        aElement->IsMathMLElement(nsGkAtoms::a)) {
      LocalAccessible* acc = GetAccessible(aElement);
      if (!acc) {
        return false;
      }
      if (acc->IsHTMLLink() != aElement->HasAttr(nsGkAtoms::href)) {
        RecreateAccessible(aElement);
        return true;
      }
    } else if (aElement->IsHTMLElement(nsGkAtoms::area)) {
      // For area accessibles, we have to recreate the entire image map, since
      // the image map accessible manages the tree itself.
      LocalAccessible* areaAcc = GetAccessibleEvenIfNotInMap(aElement);
      if (!areaAcc || !areaAcc->LocalParent()) {
        return false;
      }
      RecreateAccessible(areaAcc->LocalParent()->GetContent());
      return true;
    }
  }

  if (aElement->IsHTMLElement(nsGkAtoms::img) && aAttribute == nsGkAtoms::alt) {
    // If alt text changes on an img element, we may want to create or remove an
    // accessible for that img.
    if (nsAccessibilityService::ShouldCreateImgAccessible(aElement, this)) {
      if (GetAccessible(aElement)) {
        // If the accessible already exists, there's no need to create one.
        return false;
      }
      ContentInserted(aElement, aElement->GetNextSibling());
    } else {
      ContentRemoved(aElement);
    }
    return true;
  }

  if (aAttribute == nsGkAtoms::popover && aElement->IsHTMLElement()) {
    // Changing the popover attribute might change the role.
    RecreateAccessible(aElement);
    return true;
  }

  return false;
}

void DocAccessible::UpdateDocRoleMapEntry() {
  // The document exposes the body's role, or the root element's when the
  // document has no body.
  dom::Element* roleEl = mDocumentNode->GetBodyElement();
  if (!roleEl) {
    roleEl = mDocumentNode->GetRootElement();
  }
  const nsRoleMapEntry* entry = aria::GetRoleMap(roleEl);
  if (entry && !nsAccUtils::IsARIARoleAllowedOnContentDoc(entry->role) &&
      // Role alert isn't valid on the body element according to the ARIA spec,
      // but it's useful for our UI; e.g. the WebRTC sharing indicator.
      (entry->role != roles::ALERT || mDocumentNode->IsContentDocument())) {
    // If we have a role other than those listed above, it isn't valid on the
    // doc and we shouldn't expose it.
    entry = nullptr;
  }

  const uint8_t oldRoleMapEntryIndex = mRoleMapEntryIndex;
  SetRoleMapEntry(entry);
  // Some code (e.g. LocalAccessible::ContainerWidget) assumes that mContent
  // must be set if there is an ARIA role. If there is a root element, mContent
  // should already be set by the time this is called. If there is an ARIA role,
  // there must be a root element, since the ARIA role comes from the root or
  // the body.
  MOZ_ASSERT(!entry || mContent);
  if (mIPCDoc && mRoleMapEntryIndex != oldRoleMapEntryIndex) {
    mIPCDoc->SendRoleChangedEvent(mRoleMapEntryIndex);
  }
}

void DocAccessible::UpdateRootElement() {
  mContent = mDocumentNode->GetRootElement();
  UpdateDocRoleMapEntry();
}

/**
 * Content insertion helper.
 */
class InsertIterator final {
 public:
  InsertIterator(LocalAccessible* aContext,
                 const nsTArray<nsCOMPtr<nsIContent>>* aNodes)
      : mChild(nullptr),
        mChildBefore(nullptr),
        mWalker(aContext),
        mNodes(aNodes),
        mNodesIdx(0) {
    MOZ_ASSERT(aContext, "No context");
    MOZ_ASSERT(aNodes, "No nodes to search for accessible elements");
    MOZ_COUNT_CTOR(InsertIterator);
  }
  MOZ_COUNTED_DTOR(InsertIterator)

  LocalAccessible* Context() const { return mWalker.Context(); }
  LocalAccessible* Child() const { return mChild; }
  LocalAccessible* ChildBefore() const { return mChildBefore; }
  DocAccessible* Document() const { return mWalker.Document(); }

  /**
   * Iterates to a next accessible within the inserted content.
   */
  bool Next();

  void Rejected() {
    mChild = nullptr;
    mChildBefore = nullptr;
  }

 private:
  LocalAccessible* mChild;
  LocalAccessible* mChildBefore;
  TreeWalker mWalker;

  const nsTArray<nsCOMPtr<nsIContent>>* mNodes;
  nsTHashSet<nsPtrHashKey<const nsIContent>> mProcessedNodes;
  uint32_t mNodesIdx;
};

bool InsertIterator::Next() {
  if (mNodesIdx > 0) {
    // If we already processed the first node in the mNodes list,
    // check if we can just use the walker to get its next sibling.
    LocalAccessible* nextChild = mWalker.Next();
    if (nextChild) {
      mChildBefore = mChild;
      mChild = nextChild;
      return true;
    }
  }

  while (mNodesIdx < mNodes->Length()) {
    nsIContent* node = mNodes->ElementAt(mNodesIdx++);
    // Check to see if we already processed this node with this iterator.
    // this can happen if we get two redundant insertions in the case of a
    // text and frame insertion.
    if (!mProcessedNodes.EnsureInserted(node)) {
      continue;
    }

    LocalAccessible* container = Document()->AccessibleOrTrueContainer(
        node->GetFlattenedTreeParentNode(), true);
    // Ignore nodes that are not contained by the container anymore.
    // The container might be changed, for example, because of the subsequent
    // overlapping content insertion (i.e. other content was inserted between
    // this inserted content and its container or the content was reinserted
    // into different container of unrelated part of tree). To avoid a double
    // processing of the content insertion ignore this insertion notification.
    // Note, the inserted content might be not in tree at all at this point
    // what means there's no container. Ignore the insertion too.
    if (container != Context()) {
      continue;
    }

    if (!container->IsAcceptableChild(node)) {
      continue;
    }

#ifdef A11Y_LOG
    logging::TreeInfo("traversing an inserted node", logging::eVerbose,
                      "container", container, "node", node);
#endif

    nsIContent* prevNode = mChild ? mChild->GetContent() : nullptr;
    if (prevNode && prevNode->GetNextSibling() == node) {
      // If inserted nodes are siblings then just move the walker next.
      LocalAccessible* nextChild = mWalker.Scope(node);
      if (nextChild) {
        mChildBefore = mChild;
        mChild = nextChild;
        return true;
      }
    } else {
      // Otherwise use a new walker to find this node in the container's
      // subtree, and retrieve its preceding sibling.
      TreeWalker finder(container);
      if (finder.Seek(node)) {
        mChild = mWalker.Scope(node);
        if (mChild) {
          MOZ_ASSERT(!mChild->IsRelocated(), "child cannot be aria owned");
          mChildBefore = finder.Prev();
          return true;
        }
      }
    }
  }

  return false;
}

void DocAccessible::MaybeFireEventsForChangedPopover(LocalAccessible* aAcc) {
  dom::Element* el = aAcc->Elm();
  if (!el || !el->IsHTMLElement() || !el->HasAttr(nsGkAtoms::popover)) {
    return;  // Not a popover.
  }
  // A popover has just been inserted into or removed from the a11y tree, which
  // means it just appeared or disappeared. Fire expanded state changes on its
  // popovertarget invokers.
  Relation invokers(new RelatedAccIterator(mDoc, el, nsGkAtoms::popovertarget));
  // Additionally iterate over any commandfor invokers.
  invokers.AppendIter(new RelatedAccIterator(mDoc, el, nsGkAtoms::commandfor));
  while (Accessible* invoker = invokers.LocalNext()) {
    auto expandedChangeEvent =
        MakeRefPtr<AccStateChangeEvent>(invoker->AsLocal(), states::EXPANDED);
    FireDelayedEvent(expandedChangeEvent);
  }

  // When a popover opens or closes, invalidate relations so invokers can
  // re-calculate if they have a DESCRIBED_BY or DETAILS relation; (the popover
  // tree might update to have new interactive elements, which would change
  // this association).
  if (auto* htmlEl = nsGenericHTMLElement::FromNode(el)) {
    if (htmlEl->GetPopoverAttributeState() ==
        dom::PopoverAttributeState::Hint) {
      QueueCacheUpdateForPopoverInvokers(el);
    }
  }
}

void DocAccessible::QueueCacheUpdateForPopoverInvokers(
    dom::Element* aPopoverEl) {
  if (!mIPCDoc) {
    return;
  }
  for (nsStaticAtom* attr : {nsGkAtoms::popovertarget, nsGkAtoms::commandfor}) {
    RelatedAccIterator invokers(this, aPopoverEl, attr);
    while (Accessible* invoker = invokers.Next()) {
      if (LocalAccessible* localInvoker = invoker->AsLocal()) {
        if (localInvoker->IsDefunct() || !localInvoker->IsInDocument() ||
            mInsertedAccessibles.Contains(localInvoker)) {
          continue;
        }
        QueueCacheUpdate(localInvoker, CacheDomain::Relations);
      }
    }
  }
}

void DocAccessible::ProcessContentInserted(
    LocalAccessible* aContainer, const nsTArray<nsCOMPtr<nsIContent>>* aNodes) {
  // Process insertions if the container accessible is still in tree.
  if (!aContainer->IsInDocument()) {
    return;
  }

  // If new root content has been inserted then update it.
  if (aContainer == this) {
    UpdateRootElement();
  }

  InsertIterator iter(aContainer, aNodes);
  if (!iter.Next()) {
    return;
  }

#ifdef A11Y_LOG
  logging::TreeInfo("children before insertion", logging::eVerbose, aContainer);
#endif

  TreeMutation mt(aContainer);
  bool inserted = false;
  do {
    LocalAccessible* parent = iter.Child()->LocalParent();
    if (parent) {
      LocalAccessible* previousSibling = iter.ChildBefore();
      if (parent != aContainer ||
          iter.Child()->LocalPrevSibling() != previousSibling) {
        if (previousSibling && previousSibling->LocalParent() != aContainer) {
          // previousSibling hasn't been moved into aContainer yet.
          // previousSibling should be later in the insertion list, so the tree
          // will get adjusted when we process it later.
          MOZ_DIAGNOSTIC_ASSERT(parent == aContainer,
                                "Child moving to new parent, but previous "
                                "sibling in wrong parent");
          continue;
        }
#ifdef A11Y_LOG
        logging::TreeInfo("relocating accessible", 0, "old parent", parent,
                          "new parent", aContainer, "child", iter.Child(),
                          nullptr);
#endif
        MoveChild(iter.Child(), aContainer,
                  previousSibling ? previousSibling->IndexInParent() + 1 : 0);
        inserted = true;
      }
      continue;
    }

    if (aContainer->InsertAfter(iter.Child(), iter.ChildBefore())) {
#ifdef A11Y_LOG
      logging::TreeInfo("accessible was inserted", 0, "container", aContainer,
                        "child", iter.Child(), nullptr);
#endif

      CreateSubtree(iter.Child());
      mt.AfterInsertion(iter.Child());
      inserted = true;
      MaybeFireEventsForChangedPopover(iter.Child());
      continue;
    }

    MOZ_ASSERT_UNREACHABLE("accessible was rejected");
    iter.Rejected();
  } while (iter.Next());

  mt.Done();

#ifdef A11Y_LOG
  logging::TreeInfo("children after insertion", logging::eVerbose, aContainer);
#endif

  // We might not have actually inserted anything if layout frame reconstruction
  // occurred.
  if (inserted) {
    FireEventsOnInsertion(aContainer);
  }
}

void DocAccessible::ProcessContentInserted(LocalAccessible* aContainer,
                                           nsIContent* aNode) {
  if (!aContainer->IsInDocument()) {
    return;
  }

#ifdef A11Y_LOG
  logging::TreeInfo("children before insertion", logging::eVerbose, aContainer);
#endif

#ifdef A11Y_LOG
  logging::TreeInfo("traversing an inserted node", logging::eVerbose,
                    "container", aContainer, "node", aNode);
#endif

  TreeWalker walker(aContainer);
  if (aContainer->IsAcceptableChild(aNode) && walker.Seek(aNode)) {
    LocalAccessible* child = GetAccessible(aNode);
    if (!child) {
      child = GetAccService()->CreateAccessible(aNode, aContainer);
    }

    if (child) {
      TreeMutation mt(aContainer);
      if (!aContainer->InsertAfter(child, walker.Prev())) {
        return;
      }
      CreateSubtree(child);
      mt.AfterInsertion(child);
      mt.Done();

      FireEventsOnInsertion(aContainer);
    }
  }

#ifdef A11Y_LOG
  logging::TreeInfo("children after insertion", logging::eVerbose, aContainer);
#endif
}

void DocAccessible::FireEventsOnInsertion(LocalAccessible* aContainer) {
  // Check to see if change occurred inside an alert, and fire an EVENT_ALERT
  // if it did.
  if (aContainer->IsAlert() || aContainer->IsInsideAlert()) {
    LocalAccessible* ancestor = aContainer;
    do {
      if (ancestor->IsAlert()) {
        FireDelayedEvent(nsIAccessibleEvent::EVENT_ALERT, ancestor);
        break;
      }
    } while ((ancestor = ancestor->LocalParent()));
  }
}

void DocAccessible::ContentRemoved(LocalAccessible* aChild) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessible::ContentRemovedAcc", A11Y, {},
                            ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_ContentRemovedAcc>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  MOZ_DIAGNOSTIC_ASSERT(aChild != this, "Should never be called for the doc");
  LocalAccessible* parent = aChild->LocalParent();
  MOZ_DIAGNOSTIC_ASSERT(parent, "Unattached accessible from tree");

#ifdef A11Y_LOG
  logging::TreeInfo("process content removal", 0, "container", parent, "child",
                    aChild, nullptr);
#endif

  if (aChild->IsRelocated()) {
    nsTArray<RefPtr<LocalAccessible>>* owned = mARIAOwnsHash.Get(parent);
    MOZ_ASSERT(owned, "IsRelocated flag is out of sync with mARIAOwnsHash");
    owned->RemoveElement(aChild);
    if (owned->Length() == 0) {
      mARIAOwnsHash.Remove(parent);
    }
  }

  // XXX: event coalescence may kill us
  RefPtr<LocalAccessible> kungFuDeathGripChild(aChild);

  TreeMutation mt(parent);
  mt.BeforeRemoval(aChild);

  if (aChild->IsDefunct()) {
    MOZ_ASSERT_UNREACHABLE("Event coalescence killed the accessible");
    mt.Done();
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(aChild->LocalParent(), "Alive but unparented");
  UncacheChildrenInSubtree(aChild);
  parent->RemoveChild(aChild);

  mt.Done();
}

void DocAccessible::ContentRemoved(nsIContent* aContentNode) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessible::ContentRemovedNode", A11Y, {},
                            ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_ContentRemovedNode>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  if (!mRemovedNodes.EnsureInserted(aContentNode)) {
    return;
  }

  // If child node is not accessible then look for its accessible children.
  LocalAccessible* acc = GetAccessible(aContentNode);
  if (acc) {
    ContentRemoved(acc);
  }

  dom::AllChildrenIterator iter =
      dom::AllChildrenIterator(aContentNode, nsIContent::eAllChildren, true);
  while (nsIContent* childNode = iter.GetNextChild()) {
    ContentRemoved(childNode);
  }

  // If this node has a shadow root, remove its explicit children too.
  // The host node may be removed after the shadow root was attached, and
  // before we asynchronously prune the light DOM and construct the shadow DOM.
  // If this is a case where the node does not have its own accessible, we will
  // not recurse into its current children, so we need to use an
  // ExplicitChildIterator in order to get its accessible children in the light
  // DOM, since they are not accessible anymore via AllChildrenIterator.
  if (aContentNode->GetShadowRoot()) {
    for (nsIContent* childNode = aContentNode->GetFirstChild(); childNode;
         childNode = childNode->GetNextSibling()) {
      ContentRemoved(childNode);
    }
  }
}

bool DocAccessible::RelocateARIAOwnedIfNeeded(nsIContent* aElement) {
  RelatedAccIterator owners(mDoc, aElement, nsGkAtoms::aria_owns);
  if (Accessible* owner = owners.Next()) {
    mNotificationController->ScheduleRelocation(owner->AsLocal());
    return true;
  }

  return false;
}

void DocAccessible::DoARIAOwnsRelocation(LocalAccessible* aOwner) {
  MOZ_ASSERT(aOwner, "aOwner must be a valid pointer");
  MOZ_ASSERT(aOwner->Elm(), "aOwner->Elm() must be a valid pointer");

#ifdef A11Y_LOG
  logging::TreeInfo("aria owns relocation", logging::eVerbose, aOwner);
#endif

  const nsRoleMapEntry* roleMap = aOwner->ARIARoleMap();
  if (roleMap && roleMap->role == roles::EDITCOMBOBOX) {
    // The READWRITE state of a combobox may sever aria-owns relations
    // we fallback to "controls" relations.
    QueueCacheUpdate(aOwner, CacheDomain::Relations);
  }

  nsTArray<RefPtr<LocalAccessible>>* owned =
      mARIAOwnsHash.GetOrInsertNew(aOwner);

  if (aOwner->Elm()->State().HasState(dom::ElementState::READWRITE)) {
    // The container is editable.
    PutChildrenBack(owned, 0);
    return;
  }

  AssociatedElementsIterator iter(this, aOwner->Elm(), nsGkAtoms::aria_owns);
  uint32_t idx = 0;
  while (dom::Element* childEl = iter.NextElem()) {
    if (childEl->State().HasState(dom::ElementState::READWRITE)) {
      dom::Element* parentEl = childEl->GetFlattenedTreeParentElement();
      if (parentEl &&
          parentEl->State().HasState(dom::ElementState::READWRITE)) {
        // The child is inside of an editable subtree, don't relocate it.
        continue;
      }
    }

    LocalAccessible* child = GetAccessible(childEl);
    auto insertIdx = aOwner->ChildCount() - owned->Length() + idx;

    // Make an attempt to create an accessible if it wasn't created yet.
    if (!child) {
      // An owned child cannot be an ancestor of the owner.
      bool ok = true;
      bool check = true;
      for (LocalAccessible* parent = aOwner; parent && !parent->IsDoc();
           parent = parent->LocalParent()) {
        if (check) {
          if (parent->Elm()->IsInclusiveDescendantOf(childEl)) {
            ok = false;
            break;
          }
        }
        // We need to do the DOM descendant check again whenever the DOM
        // lineage changes. If parent is relocated, that means the next
        // ancestor will have a different DOM lineage.
        check = parent->IsRelocated();
      }
      if (!ok) {
        continue;
      }

      if (aOwner->IsAcceptableChild(childEl)) {
        child = GetAccService()->CreateAccessible(childEl, aOwner);
        if (child) {
          TreeMutation imut(aOwner);
          aOwner->InsertChildAt(insertIdx, child);
          imut.AfterInsertion(child);
          imut.Done();

          child->SetRelocated(true);
          owned->InsertElementAt(idx, child);
          idx++;

          // Create subtree before adjusting the insertion index, since subtree
          // creation may alter children in the container.
          CreateSubtree(child);
          FireEventsOnInsertion(aOwner);
        }
      }
      continue;
    }

#ifdef A11Y_LOG
    logging::TreeInfo("aria owns traversal", logging::eVerbose, "candidate",
                      child, nullptr);
#endif

    if (owned->IndexOf(child) < idx) {
      continue;  // ignore second entry of same ID
    }

    // Same child on same position, no change.
    if (child->LocalParent() == aOwner) {
      int32_t indexInParent = child->IndexInParent();

      // The child is being placed in its current index,
      // eg. aria-owns='id1 id2 id3' is changed to aria-owns='id3 id2 id1'.
      if (indexInParent == static_cast<int32_t>(insertIdx)) {
        MOZ_ASSERT(child->IsRelocated(),
                   "A child, having an index in parent from aria ownded "
                   "indices range, has to be aria owned");
        MOZ_ASSERT(owned->ElementAt(idx) == child,
                   "Unexpected child in ARIA owned array");
        idx++;
        continue;
      }

      // The child is being inserted directly after its current index,
      // resulting in a no-move case. This will happen when a parent aria-owns
      // its last ordinal child:
      // <ul aria-owns='id2'><li id='id1'></li><li id='id2'></li></ul>
      if (indexInParent == static_cast<int32_t>(insertIdx) - 1) {
        MOZ_ASSERT(!child->IsRelocated(),
                   "Child should be in its ordinal position");
        child->SetRelocated(true);
        owned->InsertElementAt(idx, child);
        idx++;
        continue;
      }
    }

    MOZ_ASSERT(owned->SafeElementAt(idx) != child, "Already in place!");

    // A new child is found, check for loops.
    if (child->LocalParent() != aOwner) {
      // Child is aria-owned by another container, skip.
      if (child->IsRelocated()) {
        continue;
      }

      LocalAccessible* parent = aOwner;
      while (parent && parent != child && !parent->IsDoc()) {
        parent = parent->LocalParent();
      }
      // A referred child cannot be a parent of the owner.
      if (parent == child) {
        continue;
      }
    }

    if (MoveChild(child, aOwner, insertIdx)) {
      child->SetRelocated(true);
      MOZ_ASSERT(owned == mARIAOwnsHash.Get(aOwner));
      owned = mARIAOwnsHash.GetOrInsertNew(aOwner);
      owned->InsertElementAt(idx, child);
      idx++;
    }
  }

  // Put back children that are not seized anymore.
  PutChildrenBack(owned, idx);
  if (owned->Length() == 0) {
    mARIAOwnsHash.Remove(aOwner);
  }
}

void DocAccessible::PutChildrenBack(
    nsTArray<RefPtr<LocalAccessible>>* aChildren, uint32_t aStartIdx) {
  MOZ_ASSERT(aStartIdx <= aChildren->Length(), "Wrong removal index");

  for (auto idx = aStartIdx; idx < aChildren->Length(); idx++) {
    LocalAccessible* child = aChildren->ElementAt(idx);
    if (!child->IsInDocument()) {
      continue;
    }

    // Remove the child from the owner
    LocalAccessible* owner = child->LocalParent();
    if (!owner) {
      NS_ERROR("Cannot put the child back. No parent, a broken tree.");
      continue;
    }

#ifdef A11Y_LOG
    logging::TreeInfo("aria owns put child back", 0, "old parent", owner,
                      "child", child, nullptr);
#endif

    // Unset relocated flag to find an insertion point for the child.
    child->SetRelocated(false);

    nsIContent* content = child->GetContent();
    int32_t idxInParent = -1;
    LocalAccessible* origContainer =
        AccessibleOrTrueContainer(content->GetFlattenedTreeParentNode());
    // This node has probably been detached or removed from the DOM, so we have
    // nowhere to move it.
    if (!origContainer) {
      continue;
    }

    // If the target container or any of its ancestors aren't in the document,
    // there's no need to determine where the child should go for relocation
    // since the target tree is going away.
    bool origContainerHasOutOfDocAncestor = false;
    LocalAccessible* ancestor = origContainer;
    while (ancestor) {
      if (ancestor->IsDoc()) {
        break;
      }
      if (!ancestor->IsInDocument()) {
        origContainerHasOutOfDocAncestor = true;
        break;
      }
      ancestor = ancestor->LocalParent();
    }
    if (origContainerHasOutOfDocAncestor) {
      continue;
    }

    TreeWalker walker(origContainer);
    if (!walker.Seek(content)) {
      continue;
    }
    LocalAccessible* prevChild = walker.Prev();
    if (prevChild) {
      idxInParent = prevChild->IndexInParent() + 1;
      MOZ_DIAGNOSTIC_ASSERT(origContainer == prevChild->LocalParent(),
                            "Broken tree");
      origContainer = prevChild->LocalParent();
    } else {
      idxInParent = 0;
    }

    // The child may have already be in its ordinal place for 2 reasons:
    // 1. It was the last ordinal child, and the first aria-owned child.
    //    given:      <ul id="list" aria-owns="b"><li id="a"></li><li
    //    id="b"></li></ul> after load: $("list").setAttribute("aria-owns", "");
    // 2. The preceding adopted children were just reclaimed, eg:
    //    given:      <ul id="list"><li id="b"></li></ul>
    //    after load: $("list").setAttribute("aria-owns", "a b");
    //    later:      $("list").setAttribute("aria-owns", "");
    if (origContainer != owner || child->IndexInParent() != idxInParent) {
      // Only attempt to move the child if the target container would accept it.
      // Otherwise, just allow it to be removed from the tree, since it would
      // not be allowed in normal tree creation.
      if (origContainer->IsAcceptableChild(child->GetContent())) {
        DebugOnly<bool> moved = MoveChild(child, origContainer, idxInParent);
        MOZ_ASSERT(moved, "Failed to put child back.");
      }
    } else {
      MOZ_ASSERT(!child->LocalPrevSibling() ||
                     !child->LocalPrevSibling()->IsRelocated(),
                 "No relocated child should appear before this one");
      MOZ_ASSERT(!child->LocalNextSibling() ||
                     child->LocalNextSibling()->IsRelocated(),
                 "No ordinal child should appear after this one");
    }
  }

  aChildren->RemoveLastElements(aChildren->Length() - aStartIdx);
}

void DocAccessible::TrackMovedAccessible(LocalAccessible* aAcc) {
  if (!HasLoadState(eTreeConstructed)) {
    // We're still building the initial tree, so it hasn't been sent over IPC
    // yet. Any move at this point is thus an insertion from the perspective of
    // IPC.
    return;
  }
  MOZ_ASSERT(aAcc->mDoc == this);
  // If an Accessible is inserted and moved during the same tick, don't track
  // it as a move because it hasn't been shown yet.
  if (!mInsertedAccessibles.Contains(aAcc)) {
    mMovedAccessibles.EnsureInserted(aAcc);
  }
  // When we move an Accessible, we're also moving its descendants.
  if (aAcc->IsOuterDoc()) {
    // Don't descend into other documents.
    return;
  }
  for (uint32_t c = 0, count = aAcc->ContentChildCount(); c < count; ++c) {
    TrackMovedAccessible(aAcc->ContentChildAt(c));
  }
}

bool DocAccessible::MoveChild(LocalAccessible* aChild,
                              LocalAccessible* aNewParent,
                              int32_t aIdxInParent) {
  MOZ_ASSERT(aChild, "No child");
  MOZ_ASSERT(aChild->LocalParent(), "No parent");
  // We can't guarantee MoveChild works correctly for accessibilities storing
  // children outside mChildren.
  MOZ_ASSERT(
      aIdxInParent <= static_cast<int32_t>(aNewParent->mChildren.Length()),
      "Wrong insertion point for a moving child");

  LocalAccessible* curParent = aChild->LocalParent();

  if (!aNewParent->IsAcceptableChild(aChild->GetContent())) {
    return false;
  }

#ifdef A11Y_LOG
  logging::TreeInfo("move child", 0, "old parent", curParent, "new parent",
                    aNewParent, "child", aChild, nullptr);
#endif

  // Forget aria-owns info in case of ARIA owned element. The caller is expected
  // to update it if needed.
  if (aChild->IsRelocated()) {
    aChild->SetRelocated(false);
    nsTArray<RefPtr<LocalAccessible>>* owned = mARIAOwnsHash.Get(curParent);
    MOZ_ASSERT(owned, "IsRelocated flag is out of sync with mARIAOwnsHash");
    owned->RemoveElement(aChild);
    if (owned->Length() == 0) {
      mARIAOwnsHash.Remove(curParent);
    }
  }

  if (curParent == aNewParent) {
    MOZ_ASSERT(aChild->IndexInParent() != aIdxInParent, "No move case");
    curParent->RelocateChild(aIdxInParent, aChild);
    TrackMovedAccessible(aChild);

#ifdef A11Y_LOG
    logging::TreeInfo("move child: parent tree after", logging::eVerbose,
                      curParent);
#endif
    return true;
  }

  // If the child cannot be re-inserted into the tree, then make sure to remove
  // it from its present parent and then shutdown it.
  bool hasInsertionPoint =
      (aIdxInParent >= 0) &&
      (aIdxInParent <= static_cast<int32_t>(aNewParent->mChildren.Length()));

  TreeMutation rmut(curParent);
  rmut.BeforeRemoval(aChild, hasInsertionPoint && TreeMutation::kNoShutdown);
  curParent->RemoveChild(aChild);
  rmut.Done();

  // No insertion point for the child.
  if (!hasInsertionPoint) {
    return true;
  }

  TreeMutation imut(aNewParent);
  aNewParent->InsertChildAt(aIdxInParent, aChild);
  TrackMovedAccessible(aChild);
  imut.AfterInsertion(aChild);
  imut.Done();

#ifdef A11Y_LOG
  logging::TreeInfo("move child: old parent tree after", logging::eVerbose,
                    curParent);
  logging::TreeInfo("move child: new parent tree after", logging::eVerbose,
                    aNewParent);
#endif

  return true;
}

void DocAccessible::CacheChildrenInSubtree(LocalAccessible* aRoot,
                                           LocalAccessible** aFocusedAcc) {
  // If the accessible is focused then report a focus event after all related
  // mutation events.
  if (aFocusedAcc && !*aFocusedAcc &&
      FocusMgr()->HasDOMFocus(aRoot->GetContent())) {
    *aFocusedAcc = aRoot;
  }

  if (aRoot->KidsFromDOM()) {
    TreeMutation mt(aRoot, TreeMutation::kNoEvents);
    TreeWalker walker(aRoot);
    while (LocalAccessible* child = walker.Next()) {
      if (child->IsBoundToParent()) {
        MoveChild(child, aRoot, aRoot->mChildren.Length());
        continue;
      }

      aRoot->AppendChild(child);
      mt.AfterInsertion(child);

      CacheChildrenInSubtree(child, aFocusedAcc);
    }
    mt.Done();
  }

  // Fire events for ARIA elements.
  if (!aRoot->HasARIARole()) {
    return;
  }

  roles::Role role = aRoot->ARIARole();
  if (!aRoot->IsDoc()) {
    if (role == roles::DIALOG || role == roles::NON_NATIVE_DOCUMENT) {
      // XXX: we should delay document load complete event if the ARIA document
      // has aria-busy.
      FireDelayedEvent(nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE, aRoot);
    } else if (role == roles::MENUPOPUP && HasLoadState(eCompletelyLoaded)) {
      FireDelayedEvent(nsIAccessibleEvent::EVENT_MENUPOPUP_START, aRoot);
    } else if (role == roles::ALERT && HasLoadState(eCompletelyLoaded)) {
      FireDelayedEvent(nsIAccessibleEvent::EVENT_ALERT, aRoot);
    }
  }
}

void DocAccessible::UncacheChildrenInSubtree(LocalAccessible* aRoot) {
  MaybeFireEventsForChangedPopover(aRoot);
  if (aRoot->ARIARole() == roles::MENUPOPUP) {
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_MENUPOPUP_END, aRoot);
  }

  aRoot->mStateFlags |= eIsNotInDocument;
  RemoveDependentIDsFor(aRoot);
  RemoveDependentElementsFor(aRoot);

  // The parent of the removed subtree is about to be cleared, so we must do
  // this here rather than in LocalAccessible::UnbindFromParent because we need
  // the ancestry for this to work.
  if (aRoot->IsTable() || aRoot->IsTableRow() || aRoot->IsTableCell()) {
    CachedTableAccessible::Invalidate(aRoot);
  }

  // Put relocated children back in their original places instead of removing
  // them from the tree.
  nsTArray<RefPtr<LocalAccessible>>* owned = mARIAOwnsHash.Get(aRoot);
  if (owned) {
    PutChildrenBack(owned, 0);
    MOZ_ASSERT(owned->IsEmpty(),
               "Owned Accessibles should be cleared after PutChildrenBack.");
    mARIAOwnsHash.Remove(aRoot);
    owned = nullptr;
  }

  const uint32_t count = aRoot->ContentChildCount();
  for (uint32_t idx = 0; idx < count; ++idx) {
    LocalAccessible* child = aRoot->ContentChildAt(idx);

    MOZ_ASSERT(!child->IsRelocated(),
               "No children should be relocated here. They should all have "
               "been relocated by PutChildrenBack.");

    // Removing this accessible from the document doesn't mean anything about
    // accessibles for subdocuments, so skip removing those from the tree.
    if (!child->IsDoc()) {
      UncacheChildrenInSubtree(child);
    }
  }

  if (aRoot->IsNodeMapEntry() &&
      mNodeToAccessibleMap.Get(aRoot->GetNode()) == aRoot) {
    mNodeToAccessibleMap.Remove(aRoot->GetNode());
  }
}

void DocAccessible::ShutdownChildrenInSubtree(LocalAccessible* aAccessible) {
  AUTO_PROFILER_MARKER_TEXT("DocAccessible::ShutdownChildrenInSubtree", A11Y,
                            {}, ""_ns);
  PerfStats::AutoMetricRecording<
      PerfStats::Metric::A11Y_ShutdownChildrenInSubtree>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  MOZ_ASSERT(!nsAccessibilityService::IsShutdown());
  // Traverse through children and shutdown them before this accessible. When
  // child gets shutdown then it removes itself from children array of its
  // parent. Use jdx index to process the cases if child is not attached to the
  // parent and as result doesn't remove itself from its children.
  uint32_t count = aAccessible->ContentChildCount();
  for (uint32_t idx = 0, jdx = 0; idx < count; idx++) {
    LocalAccessible* child = aAccessible->ContentChildAt(jdx);
    if (!child->IsBoundToParent()) {
      NS_ERROR("Parent refers to a child, child doesn't refer to parent!");
      jdx++;
    }

    // Don't cross document boundaries. The outerdoc shutdown takes care about
    // its subdocument.
    if (!child->IsDoc()) {
      ShutdownChildrenInSubtree(child);
      if (nsAccessibilityService::IsShutdown()) {
        // If XPCOM is the only consumer (devtools & mochitests), shutting down
        // the child's subtree can cause a11y to shut down because the last
        // xpcom accessibles will be removed. In that case, return early, our
        // work is done.
        return;
      }
    }
  }

  UnbindFromDocument(aAccessible);
}

bool DocAccessible::IsLoadEventTarget() const {
  if (XRE_IsParentProcess() && !ParentDocument()) {
    // This document is definitely detached from the tree. We should not fire
    // events from detached documents, as this might confuse clients. Note that
    // in the content process, the parent document might be in a different
    // process, so we can't be sure it's detached in that case. Thus, we
    // restrict this check to the parent process where we can be certain.
    return false;
  }
  return mDocumentNode->GetBrowsingContext()->IsContent();
}

void DocAccessible::SetIPCDoc(DocAccessibleChild* aIPCDoc) {
  MOZ_ASSERT(!mIPCDoc || !aIPCDoc, "Clobbering an attached IPCDoc!");
  if (!aIPCDoc) {
    // If our IPC actor dies (e.g. because its WindowGlobalChild dies), clear
    // any queued cache updates, since we can never send them.
    mQueuedCacheUpdatesArray.Clear();
    mQueuedCacheUpdatesHash.Clear();
    mViewportCacheDirty = false;
  }
  mIPCDoc = aIPCDoc;
}

void DocAccessible::DispatchScrollingEvent(nsINode* aTarget,
                                           uint32_t aEventType) {
  LocalAccessible* acc = GetAccessible(aTarget);
  if (!acc) {
    return;
  }

  nsIFrame* frame = acc->GetFrame();
  if (!frame) {
    // Although the accessible had a frame at scroll time, it may now be gone
    // because of display: contents.
    return;
  }

  auto [scrollPoint, scrollRange] = ComputeScrollData(acc);

  int32_t appUnitsPerDevPixel =
      mPresShell->GetPresContext()->AppUnitsPerDevPixel();

  LayoutDeviceIntPoint scrollPointDP = LayoutDevicePoint::FromAppUnitsToNearest(
      scrollPoint, appUnitsPerDevPixel);
  LayoutDeviceIntRect scrollRangeDP =
      LayoutDeviceRect::FromAppUnitsToNearest(scrollRange, appUnitsPerDevPixel);

  auto event = MakeRefPtr<AccScrollingEvent>(
      aEventType, acc, scrollPointDP.x, scrollPointDP.y, scrollRangeDP.width,
      scrollRangeDP.height);
  nsEventShell::FireEvent(event);
}

void DocAccessible::ARIAActiveDescendantIDMaybeMoved(
    LocalAccessible* aAccessible) {
  LocalAccessible* widget = nullptr;
  if (aAccessible->IsActiveDescendant(&widget) && widget) {
    // The active descendant might have just been inserted and may not be in the
    // tree yet. Therefore, schedule this async to ensure the tree is up to
    // date.
    mNotificationController
        ->ScheduleNotification<DocAccessible, LocalAccessible>(
            this, &DocAccessible::ARIAActiveDescendantChanged, widget);
  }
}

bool DocAccessible::IsRootContent(nsINode* aNode) const {
  return mContent && mDocumentNode->GetRootElement() == mContent &&
         mContent == aNode;
}

bool DocAccessible::IsBodyElement(const nsINode* aNode) const {
  const bool isBody = aNode && mDocumentNode->GetBodyElement() == aNode;
  MOZ_ASSERT(!isBody || aNode->IsHTMLElement(nsGkAtoms::body));
  return isBody;
}

LocalAccessible* DocAccessible::GetAccessibleOrDocument(nsINode* aNode) const {
  if (IsRootContent(aNode)) {
    return const_cast<DocAccessible*>(this);
  }
  return GetAccessible(aNode);
}

LocalAccessible* DocAccessible::GetAccessible(nsINode* aNode) const {
  return aNode == mDocumentNode ? const_cast<DocAccessible*>(this)
                                : mNodeToAccessibleMap.Get(aNode);
}

void DocAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  aName.Truncate();
  if (aIndex != 0) {
    return;
  }
  if (HasPrimaryAction()) {
    aName.AssignLiteral("click");
  }
}

void DocAccessible::MaybeHandleChangeToHiddenNameOrDescription(
    nsIContent* aChild) {
  if (!HasLoadState(eTreeConstructed)) {
    return;
  }
  for (nsIContent* content = aChild; content; content = content->GetParent()) {
    if (HasAccessible(content)) {
      // This node isn't hidden. Events for name/description dependents will be
      // fired elsewhere.
      // ... but we do need to handle firing an event for text value changes on
      // meters, since inner meter text is never rendered by layout.
      if (content->IsHTMLElement(nsGkAtoms::meter)) {
        FireDelayedEvent(nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE,
                         GetAccessible(content));
      }
      break;
    }
    nsAtom* id = content->GetID();
    if (!id) {
      continue;
    }
    auto* providers = GetRelProviders(content->AsElement(), id);
    if (!providers) {
      continue;
    }
    for (auto& provider : *providers) {
      if (provider->mRelAttr != nsGkAtoms::aria_labelledby &&
          provider->mRelAttr != nsGkAtoms::aria_describedby) {
        continue;
      }
      LocalAccessible* dependentAcc = GetAccessible(provider->mContent);
      if (!dependentAcc) {
        continue;
      }
      FireDelayedEvent(provider->mRelAttr == nsGkAtoms::aria_labelledby
                           ? nsIAccessibleEvent::EVENT_NAME_CHANGE
                           : nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE,
                       dependentAcc);
    }
  }
}

void DocAccessible::MaybeHandleChangeToAriaActions(LocalAccessible* aAcc,
                                                   const nsAtom* aAttribute) {
  if (aAttribute == nsGkAtoms::aria_actions &&
      nsTextEquivUtils::HasNameRule(aAcc, eNameFromSubtreeIfReqRule)) {
    // Search for action targets in subtree, and fire a name change event
    // on aAcc if any are found.
    AssociatedElementsIterator iter(mDoc, aAcc->Elm(), nsGkAtoms::aria_actions);
    while (LocalAccessible* target = iter.Next()) {
      if (aAcc->IsAncestorOf(target)) {
        mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, aAcc);
        break;
      }
    }
  }

  if (aAttribute == nsGkAtoms::id) {
    RelatedAccIterator iter(mDoc, aAcc->Elm(), nsGkAtoms::aria_actions);
    while (LocalAccessible* host = iter.Next()) {
      // Search for any ancestor action hosts and fire a name change
      // if any are found that calculate their name from the subtree.
      if (host->IsAncestorOf(aAcc) &&
          nsTextEquivUtils::HasNameRule(host, eNameFromSubtreeIfReqRule)) {
        mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, host);
      }
    }
  }
}

void DocAccessible::AttrElementWillChange(dom::Element* aElement,
                                          nsAtom* aAttr) {
  MOZ_ASSERT(!sIsAttrElementChanging);
  AttributeWillChange(aElement, kNameSpaceID_None, aAttr,
                      AttrModType::Modification);
  // We might get notified about a related content attribute change. Ignore
  // it.
  sIsAttrElementChanging = true;
}

void DocAccessible::AttrElementChanged(dom::Element* aElement, nsAtom* aAttr) {
  MOZ_ASSERT(sIsAttrElementChanging);
  // The element has changed and the content attribute change notifications
  // (if any) have been sent.
  sIsAttrElementChanging = false;
  AttributeChanged(aElement, kNameSpaceID_None, aAttr,
                   AttrModType::Modification, nullptr);
}

bool DocAccessible::ProcessAnchorJump() {
  if (!mAnchorJumpElm) {
    return true;
  }
  LocalAccessible* target = GetAccessibleOrContainer(mAnchorJumpElm);
  if (!target) {
    // This node isn't in the tree.
    mAnchorJumpElm = nullptr;
    return true;
  }
  const Accessible* focusedAcc = FocusMgr()->FocusedAccessible();
  if (!focusedAcc || (focusedAcc != this && !focusedAcc->IsNonInteractive())) {
    // Focus is nowhere or on an interactive element. Ignore the anchor jump for
    // now.
    return false;
  }
  nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_SCROLLING_START, target);
  // We've processed this anchor jump now. Clear it so it isn't processed again.
  mAnchorJumpElm = nullptr;
  return true;
}

void DocAccessible::RefreshAnchorRelationCacheForTarget(
    LocalAccessible* aTarget) {
  nsIFrame* frame = aTarget->GetFrame();
  if (!frame || !frame->HasProperty(nsIFrame::AnchorPosReferences())) {
    return;
  }

  AnchorPosReferenceData* referencedAnchors =
      frame->GetProperty(nsIFrame::AnchorPosReferences());
  for (auto& entry : *referencedAnchors) {
    const auto& anchorName = entry.GetKey();
    if (const nsIFrame* anchorFrame = mPresShell->GetAnchorPosAnchor(
            anchorName, frame, referencedAnchors->mFrameTreeDepth)) {
      if (LocalAccessible* anchorAcc =
              GetAccessible(anchorFrame->GetContent())) {
        if (!mInsertedAccessibles.Contains(anchorAcc)) {
          QueueCacheUpdate(anchorAcc, CacheDomain::Relations);
        }
      }
    }
  }
}

void DocAccessible::BindChildDocument(DocAccessible* aDocument) {
  if (mDocumentNode->IsStaticDocument()) {
    // This is a static clone document for printing. It will never have a
    // refresh tick, so we need to bind directly here. This is fine because we
    // explicitly control when these DocAccessibles are created and built, and
    // we explicitly create and build a child document after its parent is
    // created and built.
    if (nsIContent* embedder =
            aDocument->DocumentNode()->GetEmbedderElement()) {
      LocalAccessible* embedderAcc = GetAccessible(embedder);
      if (embedderAcc && embedderAcc->AppendChild(aDocument)) {
        AppendChildDocument(aDocument);
        if (mIPCDoc) {
          MOZ_ASSERT(!aDocument->IPCDoc());
          dom::WindowGlobalChild* wgc =
              aDocument->DocumentNode()->GetWindowGlobalChild();
          MOZ_ASSERT(wgc);
          RefPtr<DocAccessibleChild> ipcDoc =
              new DocAccessibleChild(aDocument, wgc);
          aDocument->SetIPCDoc(ipcDoc);
          wgc->SendPDocAccessibleConstructor(ipcDoc, embedderAcc->ID(),
                                             aDocument->IsPrintDoc());
        }
      }
    }
    return;
  }
  // Sometimes, a child document can be created before its parent document; e.g.
  // when accessibility is started after the parent DOM document is created and
  // we get notified about an event in the child document first. In this case,
  // we can't bind to the parent document because its tree hasn't been built yet
  // and so the OuterDocAccessible doesn't exist yet. We can't simply force
  // building of the parent document tree here because this might not be a safe
  // time to do that; e.g. we might be in the middle of an interruptible layout
  // reflow for the parent document. To deal with that, we schedule the document
  // to be bound after the parent document's tree is built.
  mNotificationController->ScheduleChildDocBinding(aDocument);
}
