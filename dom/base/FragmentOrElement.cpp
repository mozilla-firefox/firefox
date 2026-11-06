/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all element classes and DocumentFragment.
 */

#include "mozilla/dom/FragmentOrElement.h"

#include "DOMIntersectionObserver.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BloomFilter.h"
#include "mozilla/EffectSet.h"
#include "mozilla/ElementAnimationData.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventQueue.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollState.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TextEditor.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/CloseWatcher.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/NodeListBinding.h"
#include "mozilla/dom/RadioGroupContainer.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/StylePropertyMap.h"
#include "mozilla/dom/StylePropertyMapReadOnly.h"
#include "mozilla/htmlaccel/htmlaccelEnabled.h"
#include "nsAtom.h"
#include "nsDOMAttributeMap.h"
#include "nsDOMCSSAttrDeclaration.h"
#include "nsDOMTokenList.h"
#include "nsError.h"
#include "nsIDocumentEncoder.h"
#include "nsIFrame.h"
#include "nsNameSpaceManager.h"
#include "nsString.h"
#ifdef MOZ_MAY_HAVE_HTMLACCEL
#  include "mozilla/htmlaccel/htmlaccelNotInline.h"
#endif
#include "nsCCUncollectableMarker.h"
#include "nsChildContentList.h"
#include "nsContentUtils.h"
#include "nsCycleCollector.h"
#include "nsDOMMutationObserver.h"
#include "nsGkAtoms.h"
#include "nsIControllers.h"
#include "nsNodeInfoManager.h"
#include "nsNodeSupportsWeakRefTearoff.h"
#include "nsPIDOMWindow.h"
#include "nsWindowSizes.h"
#include "nsWrapperCacheInlines.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

using namespace mozilla;
using namespace mozilla::dom;

uint64_t nsMutationGuard::sGeneration = 0;

//----------------------------------------------------------------------

static inline JSObject* GetJSObjectChild(nsWrapperCache* aCache) {
  return aCache->PreservingWrapper() ? aCache->GetWrapperPreserveColor()
                                     : nullptr;
}

static bool NeedsScriptTraverse(nsINode* aNode) {
  if (aNode->PreservingWrapper() && aNode->GetWrapperPreserveColor()) {
    return !aNode->HasKnownLiveWrapperAndDoesNotNeedTracing(aNode);
  }
  return aNode->IsDocument() && !aNode->HasNothingToTrace(aNode);
}

//----------------------------------------------------------------------

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsAttrChildContentList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsAttrChildContentList)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(nsAttrChildContentList, mNode)

// If the wrapper is known-live, the list can't be part of a garbage cycle.
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsAttrChildContentList)
  return tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsAttrChildContentList)
  return tmp->HasKnownLiveWrapperAndDoesNotNeedTracing(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsAttrChildContentList)
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

NS_INTERFACE_TABLE_HEAD(nsAttrChildContentList)
  NS_WRAPPERCACHE_INTERFACE_TABLE_ENTRY
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsAttrChildContentList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

JSObject* nsAttrChildContentList::WrapObject(
    JSContext* cx, JS::Handle<JSObject*> aGivenProto) {
  return NodeList_Binding::Wrap(cx, this, aGivenProto);
}

uint32_t nsAttrChildContentList::Length() {
  return mNode ? mNode->GetChildCount() : 0;
}

nsIContent* nsAttrChildContentList::Item(uint32_t aIndex) {
  if (mNode) {
    return mNode->GetChildAt_Deprecated(aIndex);
  }

  return nullptr;
}

int32_t nsAttrChildContentList::IndexOf(nsIContent* aContent) {
  if (mNode) {
    return mNode->ComputeIndexOf_Deprecated(aContent);
  }

  return -1;
}

//----------------------------------------------------------------------
uint32_t nsParentNodeChildContentList::Length() {
  return mNode ? mNode->GetChildCount() : 0;
}

nsIContent* nsParentNodeChildContentList::Item(uint32_t aIndex) {
  if (!mIsCacheValid) {
    if (MOZ_UNLIKELY(!mNode)) {
      return nullptr;
    }
    // Try to avoid the cache for some common cases, see bug 1917511.
    if (aIndex == 0) {
      return mNode->GetFirstChild();
    }
    uint32_t childCount = mNode->GetChildCount();
    if (aIndex >= childCount) {
      return nullptr;
    }
    if (aIndex + 1 == childCount) {
      return mNode->GetLastChild();
    }
    ValidateCache();
    MOZ_ASSERT(mIsCacheValid);
  }
  return mCachedChildArray.SafeElementAt(aIndex, nullptr);
}

int32_t nsParentNodeChildContentList::IndexOf(nsIContent* aContent) {
  EnsureCacheValid();
  return mCachedChildArray.IndexOf(aContent);
}

void nsParentNodeChildContentList::ValidateCache() {
  MOZ_ASSERT(!mIsCacheValid);
  MOZ_ASSERT(mCachedChildArray.IsEmpty());

  if (MOZ_UNLIKELY(!mNode)) {
    return;
  }

  for (nsIContent* node = mNode->GetFirstChild(); node;
       node = node->GetNextSibling()) {
    mCachedChildArray.AppendElement(node);
  }
  mIsCacheValid = true;
}

//----------------------------------------------------------------------

HTMLCollection* FragmentOrElement::Children() {
  nsDOMSlots* slots = DOMSlots();

  if (!slots->mChildrenList) {
    slots->mChildrenList =
        new ContentList(this, kNameSpaceID_Wildcard, nsGkAtoms::_asterisk,
                        nsGkAtoms::_asterisk, false);
  }

  return slots->mChildrenList;
}

uint32_t FragmentOrElement::ChildElementCount() {
  if (!HasChildren()) {
    return 0;
  }
  return Children()->Length();
}

//----------------------------------------------------------------------

static const size_t MaxDOMSlotSizeAllowed =
#ifdef HAVE_64BIT_BUILD
    128;
#else
    64;
#endif

static_assert(sizeof(nsINode::nsSlots) <= MaxDOMSlotSizeAllowed,
              "DOM slots cannot be grown without consideration");
static_assert(sizeof(FragmentOrElement::nsDOMSlots) <= MaxDOMSlotSizeAllowed,
              "DOM slots cannot be grown without consideration");

FragmentOrElement::nsDOMSlots::nsDOMSlots() { MOZ_COUNT_CTOR(nsDOMSlots); }

FragmentOrElement::nsDOMSlots::~nsDOMSlots() {
  MOZ_COUNT_DTOR(nsDOMSlots);

  if (mAttributeMap) {
    mAttributeMap->DropReference();
  }
}

void FragmentOrElement::nsDOMSlots::Traverse(
    nsCycleCollectionTraversalCallback& aCb) {
  nsIContent::nsContentSlots::Traverse(aCb);

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mStyle");
  aCb.NoteXPCOMChild(mStyle.get());

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mAttributeMap");
  aCb.NoteXPCOMChild(mAttributeMap.get());

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mChildrenList");
  aCb.NoteXPCOMChild(NS_ISUPPORTS_CAST(NodeList*, mChildrenList));

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mClassList");
  aCb.NoteXPCOMChild(mClassList.get());

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mComputedStyleMap");
  aCb.NoteXPCOMChild(mComputedStyleMap.get());

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mAttributeStyleMap");
  aCb.NoteXPCOMChild(mAttributeStyleMap.get());
}

void FragmentOrElement::nsDOMSlots::Unlink(nsINode& aNode) {
  nsIContent::nsContentSlots::Unlink(aNode);
  mStyle = nullptr;
  if (mAttributeMap) {
    mAttributeMap->DropReference();
    mAttributeMap = nullptr;
  }
  mChildrenList = nullptr;
  mClassList = nullptr;
  mComputedStyleMap = nullptr;
  mAttributeStyleMap = nullptr;
}

size_t FragmentOrElement::nsDOMSlots::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  nsExtendedContentSlots* extendedSlots = GetExtendedContentSlots();
  if (extendedSlots) {
    if (OwnsExtendedSlots()) {
      n += aMallocSizeOf(extendedSlots);
    }

    n += extendedSlots->SizeOfExcludingThis(aMallocSizeOf);
  }

  if (mAttributeMap) {
    n += mAttributeMap->SizeOfIncludingThis(aMallocSizeOf);
  }

  if (mChildrenList) {
    n += mChildrenList->SizeOfIncludingThis(aMallocSizeOf);
  }

  if (mComputedStyleMap) {
    n += mComputedStyleMap->SizeOfIncludingThis(aMallocSizeOf);
  }

  if (mAttributeStyleMap) {
    n += mAttributeStyleMap->SizeOfIncludingThis(aMallocSizeOf);
  }

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - Superclass members (nsINode::nsSlots)
  // - mStyle
  // - mClassList

  // The following member are not measured:
  // - mControllers: because it is non-owning
  return n;
}

FragmentOrElement::nsExtendedDOMSlots::nsExtendedDOMSlots() = default;

nsIContent::nsContentSlots* FragmentOrElement::CreateSlots() {
  void* mem = AllocateSlots(sizeof(nsDOMSlots));
  return new (mem) nsDOMSlots();
}

nsIContent::nsExtendedContentSlots* FragmentOrElement::CreateExtendedSlots() {
  void* mem = AllocateSlots(sizeof(nsExtendedDOMSlots));
  return new (mem) nsExtendedDOMSlots();
}

FragmentOrElement::nsExtendedDOMSlots* FragmentOrElement::ExtendedDOMSlots() {
  nsContentSlots* slots = GetExistingContentSlots();
  if (!slots) {
    void* mem = AllocateSlots(sizeof(FatSlots));
    FatSlots* fatSlots = new (mem) FatSlots();
    SetSlots(fatSlots);
    return fatSlots;
  }

  if (!slots->GetExtendedContentSlots()) {
    slots->SetExtendedContentSlots(CreateExtendedSlots(), true);
  }

  return static_cast<nsExtendedDOMSlots*>(slots->GetExtendedContentSlots());
}

FragmentOrElement::nsExtendedDOMSlots::~nsExtendedDOMSlots() = default;

void FragmentOrElement::nsExtendedDOMSlots::UnlinkExtendedSlots(
    nsIContent& aContent) {
  nsIContent::nsExtendedContentSlots::UnlinkExtendedSlots(aContent);

  // mShadowRoot will similarly be cleared explicitly from
  // FragmentOrElement::Unlink.
  mSMILOverrideStyle = nullptr;
  mControllers = nullptr;
  mLabelsList = nullptr;
  mPopoverData = nullptr;
  if (mCustomElementData) {
    mCustomElementData->Unlink();
    mCustomElementData = nullptr;
  }
  if (mAnimations) {
    mAnimations = nullptr;
    aContent.ClearMayHaveAnimations();
  }
  mExplicitlySetAttrElementMap.Clear();
  mAttrElementsMap.Clear();
  mRadioGroupContainer = nullptr;
  mPart = nullptr;
}

void FragmentOrElement::nsExtendedDOMSlots::TraverseExtendedSlots(
    nsCycleCollectionTraversalCallback& aCb) {
  nsIContent::nsExtendedContentSlots::TraverseExtendedSlots(aCb);

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mExtendedSlots->mSMILOverrideStyle");
  aCb.NoteXPCOMChild(mSMILOverrideStyle.get());

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mExtendedSlots->mControllers");
  aCb.NoteXPCOMChild(mControllers);

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mExtendedSlots->mLabelsList");
  aCb.NoteXPCOMChild(NS_ISUPPORTS_CAST(NodeList*, mLabelsList));

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mExtendedSlots->mShadowRoot");
  aCb.NoteXPCOMChild(NS_ISUPPORTS_CAST(nsIContent*, mShadowRoot));

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mSlots->mPart");
  aCb.NoteXPCOMChild(mPart.get());

  for (auto& tableEntry : mAttrElementsMap) {
    auto& [explicitlySetElements, cachedAttrElements] =
        *tableEntry.GetModifiableData();
    if (cachedAttrElements) {
      ImplCycleCollectionTraverse(aCb, *cachedAttrElements,
                                  "cached attribute elements entry", 0);
    }
  }

  if (mCustomElementData) {
    mCustomElementData->Traverse(aCb);
  }
  if (mAnimations) {
    mAnimations->Traverse(aCb);
  }
  ImplCycleCollectionTraverse(aCb, mRadioGroupContainer,
                              "mExtendedSlots->mRadioGroupContainer");
}

size_t FragmentOrElement::nsExtendedDOMSlots::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n =
      nsIContent::nsExtendedContentSlots::SizeOfExcludingThis(aMallocSizeOf);

  // We own mSMILOverrideStyle but there seems to be no memory reporting on CSS
  // declarations?  At least report the memory the declaration takes up
  // directly.
  if (mSMILOverrideStyle) {
    n += aMallocSizeOf(mSMILOverrideStyle);
  }

  // We don't really own mSMILOverrideStyleDeclaration.  mSMILOverrideStyle owns
  // it.

  // We don't seem to have memory reporting for nsXULControllers.  At least
  // report the memory it's using directly.
  if (mControllers) {
    n += aMallocSizeOf(mControllers);
  }

  if (mSavedScrollState) {
    n += aMallocSizeOf(mSavedScrollState.get());
  }

  if (mLabelsList) {
    n += mLabelsList->SizeOfIncludingThis(aMallocSizeOf);
  }

  // mShadowRoot should be handled during normal DOM tree memory reporting, just
  // like kids, siblings, etc.

  if (mCustomElementData) {
    n += mCustomElementData->SizeOfIncludingThis(aMallocSizeOf);
  }

  if (mRadioGroupContainer) {
    n += mRadioGroupContainer->SizeOfIncludingThis(aMallocSizeOf);
  }

  return n;
}

FragmentOrElement::FragmentOrElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsIContent(std::move(aNodeInfo)) {}

FragmentOrElement::~FragmentOrElement() {
  MOZ_ASSERT(!IsInUncomposedDoc(),
             "Please remove this from the document properly");
  if (GetParent()) {
    NS_RELEASE(mParent);
  }
}

void FragmentOrElement::GetTextContentInternal(nsAString& aTextContent,
                                               OOMReporter& aError) {
  if (!nsContentUtils::GetNodeTextContent(this, true, aTextContent, fallible)) {
    aError.ReportOOM();
  }
}

void FragmentOrElement::SetTextContentInternal(
    const nsAString& aTextContent, nsIPrincipal* aSubjectPrincipal,
    ErrorResult& aError, MutationEffectOnScript aMutationEffectOnScript) {
  bool tryReuse = false;
  if (!aTextContent.IsEmpty()) {
    if (nsIContent* firstChild = GetFirstChild()) {
      tryReuse = firstChild->NodeType() == TEXT_NODE &&
                 !firstChild->GetNextSibling() &&
                 firstChild->OwnedOnlyByTheDOMAndFrameTrees() &&
#ifdef ACCESSIBILITY
                 !GetAccService() &&
#endif
                 !OwnerDoc()->MayHaveDOMMutationObservers() &&
                 !MaybeNeedsToNotifyDevToolsOfNodeRemovalsInOwnerDoc();
    }
  }

  aError = nsContentUtils::SetNodeTextContent(this, aTextContent, tryReuse,
                                              aMutationEffectOnScript);
}

void FragmentOrElement::DestroyContent() {
  // Drop any servo data. We do this before the RemovedFromDocument call below
  // so that it doesn't need to try to keep the style state sane when shuffling
  // around the flattened tree.
  //
  // TODO(emilio): I suspect this can be asserted against instead, with a bit of
  // effort to avoid calling Document::Destroy with a shell...
  if (IsElement()) {
    AsElement()->ClearServoData();
  }

#ifdef DEBUG
  uint32_t oldChildCount = GetChildCount();
#endif

  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    child->DestroyContent();
    MOZ_ASSERT(child->GetParent() == this,
               "Mutating the tree during XBL destructors is evil");
  }

  MOZ_ASSERT(oldChildCount == GetChildCount(),
             "Mutating the tree during XBL destructors is evil");

  if (ShadowRoot* shadowRoot = GetShadowRoot()) {
    shadowRoot->DestroyContent();
  }
}

void FragmentOrElement::SaveSubtreeState() {
  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    child->SaveSubtreeState();
  }

  // FIXME(bug 1469277): Pretty sure this wants to dig into shadow trees as
  // well.
}

//----------------------------------------------------------------------

// nsISupports implementation

#define SUBTREE_UNBINDINGS_PER_RUNNABLE 500

class ContentUnbinder : public Runnable {
 public:
  ContentUnbinder() : Runnable("ContentUnbinder") { mLast = this; }

  ~ContentUnbinder() { Run(); }

  void UnbindSubtree(nsIContent* aNode) {
    if (!aNode->HasChildren()) {
      return;
    }
    if (aNode->NodeType() != nsINode::ELEMENT_NODE &&
        aNode->NodeType() != nsINode::DOCUMENT_FRAGMENT_NODE) {
      return;
    }
    auto* container = static_cast<FragmentOrElement*>(aNode);
    // Invalidate cached array of child nodes
    container->InvalidateChildNodes();
    BatchRemovalState state{};
    while (nsCOMPtr<nsIContent> child = container->GetLastChild()) {
      // Hold a strong ref to the node when we remove it, because we may be
      // the last reference to it.  We need to call DisconnectChild()
      // before calling UnbindFromTree, since this last can notify various
      // observers and they should really see consistent tree state.
      // If this code changes, change the corresponding code in
      // FragmentOrElement's and Document's unlink impls.
      container->DisconnectChild(child);
      UnbindSubtree(child);
      child->UnbindFromTree(/* aNewParent = */ nullptr, &state);
      state.mIsFirst = false;
    }
  }

  NS_IMETHOD Run() override {
    nsAutoScriptBlocker scriptBlocker;
    uint32_t len = mSubtreeRoots.Length();
    if (len) {
      for (uint32_t i = 0; i < len; ++i) {
        UnbindSubtree(mSubtreeRoots[i]);
      }
      mSubtreeRoots.Clear();
    }
    nsCycleCollector_dispatchDeferredDeletion();
    if (this == sContentUnbinder) {
      sContentUnbinder = nullptr;
      if (mNext) {
        RefPtr<ContentUnbinder> next;
        next.swap(mNext);
        sContentUnbinder = next;
        next->mLast = mLast;
        mLast = nullptr;
        NS_DispatchToCurrentThreadQueue(next.forget(),
                                        EventQueuePriority::Idle);
      }
    }
    return NS_OK;
  }

  static void UnbindAll() {
    RefPtr<ContentUnbinder> ub = sContentUnbinder;
    sContentUnbinder = nullptr;
    while (ub) {
      ub->Run();
      ub = ub->mNext;
    }
  }

  static void Append(nsIContent* aSubtreeRoot) {
    if (!sContentUnbinder) {
      sContentUnbinder = new ContentUnbinder();
      nsCOMPtr<nsIRunnable> e = sContentUnbinder;
      NS_DispatchToCurrentThreadQueue(e.forget(), EventQueuePriority::Idle);
    }

    if (sContentUnbinder->mLast->mSubtreeRoots.Length() >=
        SUBTREE_UNBINDINGS_PER_RUNNABLE) {
      sContentUnbinder->mLast->mNext = new ContentUnbinder();
      sContentUnbinder->mLast = sContentUnbinder->mLast->mNext;
    }
    sContentUnbinder->mLast->mSubtreeRoots.AppendElement(aSubtreeRoot);
  }

 private:
  AutoTArray<nsCOMPtr<nsIContent>, SUBTREE_UNBINDINGS_PER_RUNNABLE>
      mSubtreeRoots;
  RefPtr<ContentUnbinder> mNext;
  ContentUnbinder* mLast;
  static ContentUnbinder* sContentUnbinder;
};

ContentUnbinder* ContentUnbinder::sContentUnbinder = nullptr;

void FragmentOrElement::ClearContentUnbinder() { ContentUnbinder::UnbindAll(); }

// Note, _INHERITED macro isn't used here since nsINode implementations are
// rather special.
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(FragmentOrElement)

// We purposefully don't UNLINK_BEGIN_INHERITED here.
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(FragmentOrElement)
  nsIContent::Unlink(tmp);

  // Unlink child content (and unbind our subtree).
  if (tmp->UnoptimizableCCNode() || !nsCCUncollectableMarker::sGeneration) {
    // Don't allow script to run while we're unbinding everything.
    nsAutoScriptBlocker scriptBlocker;
    BatchRemovalState state{};
    while (nsCOMPtr<nsIContent> child = tmp->GetLastChild()) {
      // Hold a strong ref to the node when we remove it, because we may be
      // the last reference to it.
      // If this code changes, change the corresponding code in Document's
      // unlink impl and ContentUnbinder::UnbindSubtree.
      tmp->DisconnectChild(child);
      child->UnbindFromTree(/* aNewParent = */ nullptr, &state);
      state.mIsFirst = false;
    }
  } else if (!tmp->GetParent() && tmp->HasChildren()) {
    ContentUnbinder::Append(tmp);
  } /* else {
    The subtree root will end up to a ContentUnbinder, and that will
    unbind the child nodes.
  } */

  if (ShadowRoot* shadowRoot = tmp->GetShadowRoot()) {
    nsAutoScriptBlocker scriptBlocker;
    shadowRoot->Unbind();
    tmp->ExtendedDOMSlots()->mShadowRoot = nullptr;
  }

  if (tmp->IsElement()) {
    auto* element = tmp->AsElement();
    if (MOZ_UNLIKELY(element->HasFlag(ELEMENT_HAS_EDIT_CONTEXT))) {
      element->ClearEditContext();
    }
    CustomElementRegistry::RemoveScopedRegistry(*element);
  }

NS_IMPL_CYCLE_COLLECTION_UNLINK_END

void FragmentOrElement::MarkNodeChildren(nsINode* aNode) {
  JSObject* o = GetJSObjectChild(aNode);
  if (o) {
    JS::ExposeObjectToActiveJS(o);
  }

  EventListenerManager* elm = aNode->GetExistingListenerManager();
  if (elm) {
    elm->MarkForCC();
  }
}

nsINode* FindOptimizableSubtreeRoot(nsINode* aNode) {
  nsINode* p;
  while ((p = aNode->GetParentNode())) {
    if (aNode->UnoptimizableCCNode()) {
      return nullptr;
    }
    aNode = p;
  }

  if (aNode->UnoptimizableCCNode()) {
    return nullptr;
  }
  return aNode;
}

StaticAutoPtr<nsTHashSet<nsINode*>> gCCBlackMarkedNodes;

static void ClearBlackMarkedNodes() {
  if (!gCCBlackMarkedNodes) {
    return;
  }
  for (nsINode* n : *gCCBlackMarkedNodes) {
    n->SetCCMarkedRoot(false);
    n->SetInCCBlackTree(false);
  }
  gCCBlackMarkedNodes = nullptr;
}

// static
void FragmentOrElement::RemoveBlackMarkedNode(nsINode* aNode) {
  if (!gCCBlackMarkedNodes) {
    return;
  }
  gCCBlackMarkedNodes->Remove(aNode);
}

static bool IsCertainlyAliveNode(nsINode* aNode, Document* aDoc) {
  MOZ_ASSERT(aNode->GetComposedDoc() == aDoc);

  // Marked to be in-CC-generation or if the document is an svg image that's
  // being kept alive by the image cache. (Note that an svg image's internal
  // SVG document will receive an OnPageHide() call when it gets purged from
  // the image cache; hence, we use IsVisible() as a hint that the document is
  // actively being kept alive by the cache.)
  return nsCCUncollectableMarker::InGeneration(aDoc->GetMarkedCCGeneration()) ||
         (nsCCUncollectableMarker::sGeneration && aDoc->IsBeingUsedAsImage() &&
          aDoc->IsVisible());
}

// static
bool FragmentOrElement::CanSkipInCC(nsINode* aNode) {
  // Don't try to optimize anything during shutdown.
  if (nsCCUncollectableMarker::sGeneration == 0) {
    return false;
  }

  Document* currentDoc = aNode->GetComposedDoc();
  if (currentDoc && IsCertainlyAliveNode(aNode, currentDoc)) {
    return !NeedsScriptTraverse(aNode);
  }

  // Bail out early if aNode is somewhere in anonymous content,
  // or otherwise unusual.
  if (aNode->UnoptimizableCCNode()) {
    return false;
  }

  nsINode* root = currentDoc ? static_cast<nsINode*>(currentDoc)
                             : FindOptimizableSubtreeRoot(aNode);
  if (!root) {
    return false;
  }

  // Subtree has been traversed already.
  if (root->CCMarkedRoot()) {
    return root->InCCBlackTree() && !NeedsScriptTraverse(aNode);
  }

  if (!gCCBlackMarkedNodes) {
    gCCBlackMarkedNodes = new nsTHashSet<nsINode*>(1020);
  }

  // nodesToUnpurple contains nodes which will be removed
  // from the purple buffer if the DOM tree is known-live.
  AutoTArray<nsIContent*, 1020> nodesToUnpurple;
  // grayNodes need script traverse, so they aren't removed from
  // the purple buffer, but are marked to be in known-live subtree so that
  // traverse is faster.
  AutoTArray<nsINode*, 1020> grayNodes;

  bool foundLiveWrapper = root->HasKnownLiveWrapper();
  if (root != currentDoc) {
    currentDoc = nullptr;
    if (NeedsScriptTraverse(root)) {
      grayNodes.AppendElement(root);
    } else if (static_cast<nsIContent*>(root)->IsPurple()) {
      nodesToUnpurple.AppendElement(static_cast<nsIContent*>(root));
    }
  }

  // Traverse the subtree and check if we could know without CC
  // that it is known-live.
  // Note, this traverse is non-virtual and inline, so it should be a lot faster
  // than CC's generic traverse.
  for (nsIContent* node = root->GetFirstChild(); node;
       node = node->GetNextNode(root)) {
    foundLiveWrapper = foundLiveWrapper || node->HasKnownLiveWrapper();
    if (foundLiveWrapper && currentDoc) {
      // If we can mark the whole document known-live, no need to optimize
      // so much, since when the next purple node in the document will be
      // handled, it is fast to check that currentDoc is in CCGeneration.
      break;
    }
    if (NeedsScriptTraverse(node)) {
      // Gray nodes need real CC traverse.
      grayNodes.AppendElement(node);
    } else if (node->IsPurple()) {
      nodesToUnpurple.AppendElement(node);
    }
  }

  root->SetCCMarkedRoot(true);
  root->SetInCCBlackTree(foundLiveWrapper);
  gCCBlackMarkedNodes->Insert(root);

  if (!foundLiveWrapper) {
    return false;
  }

  if (currentDoc) {
    // Special case documents. If we know the document is known-live,
    // we can mark the document to be in CCGeneration.
    currentDoc->MarkUncollectableForCCGeneration(
        nsCCUncollectableMarker::sGeneration);
  } else {
    for (uint32_t i = 0; i < grayNodes.Length(); ++i) {
      nsINode* node = grayNodes[i];
      node->SetInCCBlackTree(true);
      gCCBlackMarkedNodes->Insert(node);
    }
  }

  // Subtree is known-live, we can remove non-gray purple nodes from
  // purple buffer.
  for (uint32_t i = 0; i < nodesToUnpurple.Length(); ++i) {
    nsIContent* purple = nodesToUnpurple[i];
    // Can't remove currently handled purple node.
    if (purple != aNode) {
      purple->RemovePurple();
    }
  }
  return !NeedsScriptTraverse(aNode);
}

AutoTArray<nsINode*, 1020>* gPurpleRoots = nullptr;
AutoTArray<nsIContent*, 1020>* gNodesToUnbind = nullptr;

void ClearCycleCollectorCleanupData() {
  if (gPurpleRoots) {
    uint32_t len = gPurpleRoots->Length();
    for (uint32_t i = 0; i < len; ++i) {
      nsINode* n = gPurpleRoots->ElementAt(i);
      n->SetIsPurpleRoot(false);
    }
    delete gPurpleRoots;
    gPurpleRoots = nullptr;
  }
  if (gNodesToUnbind) {
    uint32_t len = gNodesToUnbind->Length();
    for (uint32_t i = 0; i < len; ++i) {
      nsIContent* c = gNodesToUnbind->ElementAt(i);
      c->SetIsPurpleRoot(false);
      ContentUnbinder::Append(c);
    }
    delete gNodesToUnbind;
    gNodesToUnbind = nullptr;
  }
}

static bool ShouldClearPurple(nsIContent* aContent) {
  MOZ_ASSERT(aContent);
  if (aContent->IsPurple()) {
    return true;
  }

  JSObject* o = GetJSObjectChild(aContent);
  if (o && JS::ObjectIsMarkedGray(o)) {
    return true;
  }

  if (aContent->HasListenerManager()) {
    return true;
  }

  return aContent->HasProperties();
}

// If aNode is not optimizable, but is an element
// with a frame in a document which has currently active presshell,
// we can act as if it was optimizable. When the primary frame dies, aNode
// will end up to the purple buffer because of the refcount change.
bool NodeHasActiveFrame(Document* aCurrentDoc, nsINode* aNode) {
  return aCurrentDoc->GetPresShell() && aNode->IsElement() &&
         aNode->AsElement()->GetPrimaryFrame();
}

// CanSkip checks if aNode is known-live, and if it is, returns true. If aNode
// is in a known-live DOM tree, CanSkip may also remove other objects from
// purple buffer and unmark event listeners and user data.  If the root of the
// DOM tree is a document, less optimizations are done since checking the
// liveness of the current document is usually fast and we don't want slow down
// such common cases.
bool FragmentOrElement::CanSkip(nsINode* aNode, bool aRemovingAllowed) {
  // Don't try to optimize anything during shutdown.
  if (nsCCUncollectableMarker::sGeneration == 0) {
    return false;
  }

  bool unoptimizable = aNode->UnoptimizableCCNode();
  Document* currentDoc = aNode->GetComposedDoc();
  if (currentDoc && IsCertainlyAliveNode(aNode, currentDoc) &&
      (!unoptimizable || NodeHasActiveFrame(currentDoc, aNode))) {
    MarkNodeChildren(aNode);
    return true;
  }

  if (unoptimizable) {
    return false;
  }

  nsINode* root = currentDoc ? static_cast<nsINode*>(currentDoc)
                             : FindOptimizableSubtreeRoot(aNode);
  if (!root) {
    return false;
  }

  // Subtree has been traversed already, and aNode has
  // been handled in a way that doesn't require revisiting it.
  if (root->IsPurpleRoot()) {
    return false;
  }

  // nodesToClear contains nodes which are either purple or
  // gray.
  AutoTArray<nsIContent*, 1020> nodesToClear;

  bool foundLiveWrapper = root->HasKnownLiveWrapper();
  bool domOnlyCycle = false;
  if (root != currentDoc) {
    currentDoc = nullptr;
    if (!foundLiveWrapper) {
      domOnlyCycle = static_cast<nsIContent*>(root)->OwnedOnlyByTheDOMTree();
    }
    if (ShouldClearPurple(static_cast<nsIContent*>(root))) {
      nodesToClear.AppendElement(static_cast<nsIContent*>(root));
    }
  }

  // Traverse the subtree and check if we could know without CC
  // that it is known-live.
  // Note, this traverse is non-virtual and inline, so it should be a lot faster
  // than CC's generic traverse.
  for (nsIContent* node = root->GetFirstChild(); node;
       node = node->GetNextNode(root)) {
    foundLiveWrapper = foundLiveWrapper || node->HasKnownLiveWrapper();
    if (foundLiveWrapper) {
      domOnlyCycle = false;
      if (currentDoc) {
        // If we can mark the whole document live, no need to optimize
        // so much, since when the next purple node in the document will be
        // handled, it is fast to check that the currentDoc is in CCGeneration.
        break;
      }
      // No need to put stuff to the nodesToClear array, if we can clear it
      // already here.
      if (node->IsPurple() && (node != aNode || aRemovingAllowed)) {
        node->RemovePurple();
      }
      MarkNodeChildren(node);
    } else {
      domOnlyCycle = domOnlyCycle && node->OwnedOnlyByTheDOMTree();
      if (ShouldClearPurple(node)) {
        // Collect interesting nodes which we can clear if we find that
        // they are kept alive in a known-live tree or are in a DOM-only cycle.
        nodesToClear.AppendElement(node);
      }
    }
  }

  if (!currentDoc || !foundLiveWrapper) {
    root->SetIsPurpleRoot(true);
    if (domOnlyCycle) {
      if (!gNodesToUnbind) {
        gNodesToUnbind = new AutoTArray<nsIContent*, 1020>();
      }
      gNodesToUnbind->AppendElement(static_cast<nsIContent*>(root));
      for (uint32_t i = 0; i < nodesToClear.Length(); ++i) {
        nsIContent* n = nodesToClear[i];
        if ((n != aNode || aRemovingAllowed) && n->IsPurple()) {
          n->RemovePurple();
        }
      }
      return true;
    } else {
      if (!gPurpleRoots) {
        gPurpleRoots = new AutoTArray<nsINode*, 1020>();
      }
      gPurpleRoots->AppendElement(root);
    }
  }

  if (!foundLiveWrapper) {
    return false;
  }

  if (currentDoc) {
    // Special case documents. If we know the document is known-live,
    // we can mark the document to be in CCGeneration.
    currentDoc->MarkUncollectableForCCGeneration(
        nsCCUncollectableMarker::sGeneration);
    MarkNodeChildren(currentDoc);
  }

  // Subtree is known-live, so we can remove purple nodes from
  // purple buffer and mark stuff that to be certainly alive.
  for (uint32_t i = 0; i < nodesToClear.Length(); ++i) {
    nsIContent* n = nodesToClear[i];
    MarkNodeChildren(n);
    // Can't remove currently handled purple node,
    // unless aRemovingAllowed is true.
    if ((n != aNode || aRemovingAllowed) && n->IsPurple()) {
      n->RemovePurple();
    }
  }
  return true;
}

bool FragmentOrElement::CanSkipThis(nsINode* aNode) {
  if (nsCCUncollectableMarker::sGeneration == 0) {
    return false;
  }
  if (aNode->HasKnownLiveWrapper()) {
    return true;
  }
  Document* c = aNode->GetComposedDoc();
  return ((c && IsCertainlyAliveNode(aNode, c)) || aNode->InCCBlackTree()) &&
         !NeedsScriptTraverse(aNode);
}

void FragmentOrElement::InitCCCallbacks() {
  nsCycleCollector_setForgetSkippableCallback(ClearCycleCollectorCleanupData);
  nsCycleCollector_setBeforeUnlinkCallback(ClearBlackMarkedNodes);
}

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(FragmentOrElement)
  return FragmentOrElement::CanSkip(tmp, aRemovingAllowed);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(FragmentOrElement)
  return FragmentOrElement::CanSkipInCC(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(FragmentOrElement)
  return FragmentOrElement::CanSkipThis(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

// We purposefully don't TRAVERSE_BEGIN_INHERITED here.  All the bits
// we should traverse should be added here or in nsINode::Traverse.
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(FragmentOrElement)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    char name[512];
    uint32_t nsid = tmp->GetNameSpaceID();
    nsAtomCString localName(tmp->NodeInfo()->NameAtom());
    nsAutoCString uri;
    if (tmp->OwnerDoc()->GetDocumentURI()) {
      uri = tmp->OwnerDoc()->GetDocumentURI()->GetSpecOrDefault();
    }

    nsAutoString id;
    nsAtom* idAtom = tmp->GetID();
    if (idAtom) {
      id.AppendLiteral(" id='");
      id.Append(nsDependentAtomString(idAtom));
      id.ReplaceChar(char16_t('\n'), char16_t(' '));
      id.Append('\'');
    }

    nsAutoString classes;
    const nsAttrValue* classAttrValue =
        tmp->IsElement() ? tmp->AsElement()->GetClasses() : nullptr;
    if (classAttrValue) {
      classes.AppendLiteral(" class='");
      nsAutoString classString;
      classAttrValue->ToString(classString);
      classString.ReplaceChar(char16_t('\n'), char16_t(' '));
      classes.Append(classString);
      classes.Append('\'');
    }

    nsAutoCString orphan;
    if (!tmp->IsInComposedDoc()) {
      orphan.AppendLiteral(" (orphan)");
    }

    const char* nsuri = nsNameSpaceManager::GetNameSpaceDisplayName(nsid);
    SprintfLiteral(name, "FragmentOrElement %s %s%s%s%s %s", nsuri,
                   localName.get(), NS_ConvertUTF16toUTF8(id).get(),
                   NS_ConvertUTF16toUTF8(classes).get(), orphan.get(),
                   uri.get());
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name);
  } else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(FragmentOrElement, tmp->mRefCnt.get())
  }

  if (!nsIContent::Traverse(tmp, cb)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  if (tmp->IsElement()) {
    Element* element = tmp->AsElement();
    // Traverse attribute names.
    uint32_t i;
    uint32_t attrs = element->GetAttrCount();
    for (i = 0; i < attrs; i++) {
      const nsAttrName* name = element->GetUnsafeAttrNameAt(i);
      if (!name->IsAtom()) {
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mAttrs[i]->NodeInfo()");
        cb.NoteNativeChild(name->NodeInfo(),
                           NS_CYCLE_COLLECTION_PARTICIPANT(NodeInfo));
      }
    }
    Element::TraverseCustomElementRegistry(element, cb);
    if (MOZ_UNLIKELY(element->HasFlag(ELEMENT_HAS_EDIT_CONTEXT))) {
      auto* editContext = EditContext::GetForElement(*element);
      cb.NoteXPCOMChild(NS_ISUPPORTS_CAST(EventTarget*, editContext));
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN(FragmentOrElement)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(FragmentOrElement)
NS_INTERFACE_MAP_END_INHERITING(nsIContent)

//----------------------------------------------------------------------

const CharacterDataBuffer* FragmentOrElement::GetCharacterDataBuffer() const {
  return nullptr;
}

uint32_t FragmentOrElement::TextLength() const {
  // We can remove this assertion if it turns out to be useful to be able
  // to depend on this returning 0
  MOZ_ASSERT_UNREACHABLE("called FragmentOrElement::TextLength");

  return 0;
}

bool FragmentOrElement::TextIsOnlyWhitespace() { return false; }

bool FragmentOrElement::ThreadSafeTextIsOnlyWhitespace() const { return false; }

static inline bool IsVoidTag(const nsAtom* aTag) {
  static const nsAtom* voidElements[] = {
      nsGkAtoms::area,    nsGkAtoms::base,  nsGkAtoms::basefont,
      nsGkAtoms::bgsound, nsGkAtoms::br,    nsGkAtoms::col,
      nsGkAtoms::embed,   nsGkAtoms::frame, nsGkAtoms::hr,
      nsGkAtoms::img,     nsGkAtoms::input, nsGkAtoms::keygen,
      nsGkAtoms::link,    nsGkAtoms::meta,  nsGkAtoms::param,
      nsGkAtoms::source,  nsGkAtoms::track, nsGkAtoms::wbr};

  static mozilla::BitBloomFilter<12, nsAtom> sFilter;
  static bool sInitialized = false;
  if (!sInitialized) {
    sInitialized = true;
    for (auto& voidElement : voidElements) {
      sFilter.add(voidElement);
    }
  }

  if (sFilter.mightContain(aTag)) {
    for (auto& voidElement : voidElements) {
      if (aTag == voidElement) {
        return true;
      }
    }
  }
  return false;
}

/* static */
bool FragmentOrElement::IsHTMLVoid(const nsAtom* aLocalName) {
  return aLocalName && IsVoidTag(aLocalName);
}

void FragmentOrElement::GetMarkup(bool aIncludeSelf, nsAString& aMarkup) {
  aMarkup.Truncate();

  Document* doc = OwnerDoc();
  if (IsInHTMLDocument()) {
    nsContentUtils::SerializeNodeToMarkup(this, !aIncludeSelf, aMarkup, false,
                                          {});
    return;
  }

  nsAutoString contentType;
  doc->GetContentType(contentType);
  bool tryToCacheEncoder = !aIncludeSelf;

  nsCOMPtr<nsIDocumentEncoder> docEncoder = doc->GetCachedEncoder();
  if (!docEncoder) {
    docEncoder = do_createDocumentEncoder(
        PromiseFlatCString(NS_ConvertUTF16toUTF8(contentType)).get());
  }
  if (!docEncoder) {
    // This could be some type for which we create a synthetic document.  Try
    // again as XML
    contentType.AssignLiteral("application/xml");
    docEncoder = do_createDocumentEncoder("application/xml");
    // Don't try to cache the encoder since it would point to a different
    // contentType once it has been reinitialized.
    tryToCacheEncoder = false;
  }

  NS_ENSURE_TRUE_VOID(docEncoder);

  uint32_t flags = nsIDocumentEncoder::OutputEncodeBasicEntities |
                   // Output DOM-standard newlines
                   nsIDocumentEncoder::OutputLFLineBreak |
                   // Don't do linebreaking that's not present in
                   // the source
                   nsIDocumentEncoder::OutputRaw |
                   // Only check for mozdirty when necessary (bug 599983)
                   nsIDocumentEncoder::OutputIgnoreMozDirty;

  if (IsEditable()) {
    nsCOMPtr<Element> elem = do_QueryInterface(this);
    TextEditor* textEditor = elem ? elem->GetTextEditorInternal() : nullptr;
    if (textEditor && textEditor->OutputsMozDirty()) {
      flags &= ~nsIDocumentEncoder::OutputIgnoreMozDirty;
    }
  }

  DebugOnly<nsresult> rv = docEncoder->Init(doc, contentType, flags);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  if (aIncludeSelf) {
    docEncoder->SetNode(this);
  } else {
    docEncoder->SetContainerNode(this);
  }
  rv = docEncoder->EncodeToString(aMarkup);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (tryToCacheEncoder) {
    doc->SetCachedEncoder(docEncoder.forget());
  }
}

static bool ContainsMarkup(const nsAString& aStr) {
  // Note: we can't use FindCharInSet because null is one of the characters we
  // want to search for.
  const char16_t* start = aStr.BeginReading();
  const char16_t* end = aStr.EndReading();

#ifdef MOZ_MAY_HAVE_HTMLACCEL
  if (mozilla::htmlaccel::htmlaccelEnabled()) {
    // We need to check for the empty string in order to
    // dereference `start` for the '<' check. We might as well
    // check that we have a full SIMD stride.
    if (end - start >= 16) {
      // Optimize the case where the input starts with a tag.
      if (*start == u'<') {
        return true;
      }
      // Curiously, this doesn't look like much of an optimization on Zen 3,
      // but since it is an optimization on M3 Pro and Skylake, let's do this.
      return mozilla::htmlaccel::ContainsMarkup(start, end);
    }
  }
#endif

  while (start != end) {
    char16_t c = *start;
    if (c == char16_t('<') || c == char16_t('&') || c == char16_t('\r') ||
        c == char16_t('\0')) {
      return true;
    }
    ++start;
  }

  return false;
}

void FragmentOrElement::SetInnerHTMLInternal(const nsAString& aInnerHTML,
                                             ErrorResult& aError) {
  // Keep "this" alive should be guaranteed by the caller, and also the content
  // of a template element (if this is one) should never been released by from
  // this during this call.  Therefore, using raw pointer here is safe.
  FragmentOrElement* target = this;
  // Handle template case.
  if (target->IsTemplateElement()) {
    DocumentFragment* frag =
        static_cast<HTMLTemplateElement*>(target)->Content();
    MOZ_ASSERT(frag);
    target = frag;
  }
  // Fast-path for strings with no markup. Limit this to short strings, to
  // avoid ContainsMarkup taking too long. The choice for 100 is based on
  // gut feeling.
  //
  // Don't do this for elements with a weird parser insertion mode, for
  // instance setting innerHTML = "" on a <html> element should add the
  // optional <head> and <body> elements.
  if (!target->HasWeirdParserInsertionMode() && aInnerHTML.Length() < 100 &&
      !ContainsMarkup(aInnerHTML)) {
    aError = nsContentUtils::SetNodeTextContent(target, aInnerHTML, false);
    return;
  }

  const RefPtr<Document> doc = target->OwnerDoc();

  target->NotifyDevToolsOfRemovalsOfChildren();

  // Needed when innerHTML is used in combination with contenteditable
  mozAutoDocUpdate updateBatch(doc, true);

  // Remove childnodes.
  nsAutoMutationBatch mb(target, true, false);
  target->RemoveAllChildren(true);
  mb.RemovalDone();

  nsAutoScriptLoaderDisabler sld(doc);

  FragmentOrElement* parseContext = this;
  if (ShadowRoot* shadowRoot = ShadowRoot::FromNode(this)) {
    // Fix up the context to be the host of the ShadowRoot.  See
    // https://w3c.github.io/DOM-Parsing/#dom-innerhtml-innerhtml setter step 1.
    parseContext = shadowRoot->GetHost();
  }

  // https://html.spec.whatwg.org/#create-an-element-for-the-token
  // Step 6: Let registry be the result of looking up a custom element registry
  // given intendedParent.
  Maybe<RefPtr<CustomElementRegistry>> customElementRegistry =
      nsContentUtils::GetCustomElementRegistry(target);

  if (doc->IsHTMLDocument()) {
    doc->SuspendDOMNotifications();
    nsAtom* contextLocalName = parseContext->NodeInfo()->NameAtom();
    int32_t contextNameSpaceID = parseContext->GetNameSpaceID();

    aError = nsContentUtils::ParseFragmentHTML(
        aInnerHTML, target, contextLocalName, contextNameSpaceID,
        doc->GetCompatibilityMode() == eCompatibility_NavQuirks, true,
        nsContentUtils::kParseFragmentPrivilegedDefaultSanitization,
        std::move(customElementRegistry));
    doc->ResumeDOMNotifications();
    if (target->GetFirstChild()) {
      MutationObservers::NotifyContentAppended(target, target->GetFirstChild(),
                                               {});
    }
    mb.NodesAdded();
  } else {
    RefPtr<DocumentFragment> df = nsContentUtils::CreateContextualFragment(
        parseContext, aInnerHTML, true, std::move(customElementRegistry),
        aError);
    if (!aError.Failed()) {
      // Suppress assertion about node removal mutation events that can't have
      // listeners anyway, because no one has had the chance to register
      // mutation listeners on the fragment that comes from the parser.
      nsAutoScriptBlockerSuppressNodeRemoved scriptBlocker;

      target->AppendChild(*df, aError);
      mb.NodesAdded();
    }
  }
}

void FragmentOrElement::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                               size_t* aNodeSize) const {
  nsIContent::AddSizeOfExcludingThis(aSizes, aNodeSize);

  nsDOMSlots* slots = GetExistingDOMSlots();
  if (slots) {
    *aNodeSize += slots->SizeOfIncludingThis(aSizes.mState.mMallocSizeOf);
  }
}
