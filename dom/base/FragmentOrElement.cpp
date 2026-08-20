/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all element classes and DocumentFragment.
 */

#include "mozilla/dom/FragmentOrElement.h"

#include "DOMIntersectionObserver.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/EffectSet.h"
#include "mozilla/ElementAnimationData.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Attr.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/CloseWatcher.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLHeadingElement.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/RadioGroupContainer.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/StylePropertyMap.h"
#include "mozilla/dom/StylePropertyMapReadOnly.h"
#include "mozilla/dom/TreeIterator.h"
#include "mozilla/dom/UnbindContext.h"
#include "mozilla/mozInlineSpellChecker.h"
#include "nsAtom.h"
#include "nsDOMAttributeMap.h"
#include "nsDOMCSSAttrDeclaration.h"
#include "nsDOMTokenList.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsIAnonymousContentCreator.h"
#include "nsIControllers.h"
#include "nsIDocumentEncoder.h"
#include "nsIFrame.h"
#include "nsNameSpaceManager.h"
#include "nsNetUtil.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsXULElement.h"
#ifdef DEBUG
#  include "nsRange.h"
#endif

#include "ChildIterator.h"
#include "NodeUbiReporting.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/BloomFilter.h"
#include "mozilla/Sprintf.h"
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/NodeListBinding.h"
#include "mozilla/dom/SVGUseElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/htmlaccel/htmlaccelEnabled.h"
#ifdef MOZ_MAY_HAVE_HTMLACCEL
#  include "mozilla/htmlaccel/htmlaccelNotInline.h"
#endif
#include "nsCCUncollectableMarker.h"
#include "nsChildContentList.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsCycleCollector.h"
#include "nsDOMMutationObserver.h"
#include "nsFrameLoader.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIWidget.h"
#include "nsLayoutUtils.h"
#include "nsNodeInfoManager.h"
#include "nsPIDOMWindow.h"
#include "nsWindowSizes.h"
#include "nsWrapperCacheInlines.h"
#include "xpcpublic.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

using namespace mozilla;
using namespace mozilla::dom;

uint64_t nsMutationGuard::sGeneration = 0;

NS_IMPL_CYCLE_COLLECTION_CLASS(nsIContent)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsIContent)
  MOZ_ASSERT_UNREACHABLE("Our subclasses don't call us");
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsIContent)
  MOZ_ASSERT_UNREACHABLE("Our subclasses don't call us");
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN(nsIContent)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  // Don't bother to QI to cycle collection, because our CC impl is
  // not doing anything anyway.
  NS_INTERFACE_MAP_ENTRY(nsIContent)
  NS_INTERFACE_MAP_ENTRY(nsINode)
  NS_INTERFACE_MAP_ENTRY(mozilla::dom::EventTarget)
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsISupportsWeakReference,
                                 new nsNodeSupportsWeakRefTearoff(this))
  // DOM bindings depend on the identity pointer being the
  // same as nsINode (which nsIContent inherits).
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsIContent)

NS_IMPL_DOMARENA_DESTROY(nsIContent)

NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE_AND_DESTROY(nsIContent,
                                                               LastRelease(),
                                                               Destroy())

nsIContent* nsIContent::FindFirstNonChromeOnlyAccessContent() const {
  // This handles also nested native anonymous content.
  // Oops, this function signature allows casting const to non-const.  (Then
  // again, so does GetFirstChild()->GetParent().)
  for (nsIContent* content = const_cast<nsIContent*>(this); content;
       content = content->GetClosestNativeAnonymousSubtreeRootParentOrHost()) {
    if (!content->ChromeOnlyAccess()) {
      return content;
    }
  }
  return nullptr;
}

void nsIContent::UnbindFromTree(nsINode* aNewParent,
                                const BatchRemovalState* aBatchState) {
  UnbindContext context(*this, aBatchState);
  context.SetIsMove(aNewParent != nullptr);
  UnbindFromTree(context);
}

HTMLSlotElement* nsIContent::GetAssignedSlotForSelection() const {
  HTMLSlotElement* const assignedSlot = GetAssignedSlot();
  if (!assignedSlot) {
    return nullptr;
  }
  ShadowRoot* const containingShadowRoot = assignedSlot->GetContainingShadow();
  return containingShadowRoot && !containingShadowRoot->IsUAWidget()
             ? assignedSlot
             : nullptr;
}

// https://dom.spec.whatwg.org/#dom-slotable-assignedslot
HTMLSlotElement* nsIContent::GetAssignedSlotByMode() const {
  /**
   * Get slotable's assigned slot for the result of
   * find a slot with open flag UNSET [1].
   *
   * [1] https://dom.spec.whatwg.org/#assign-a-slot
   */
  HTMLSlotElement* slot = GetAssignedSlot();
  if (!slot) {
    return nullptr;
  }

  MOZ_ASSERT(GetParent());
  MOZ_ASSERT(GetParent()->GetShadowRoot());

  /**
   * Additional check for open flag SET:
   *   If slotable’s parent’s shadow root's mode is not "open",
   *   then return null.
   */
  if (GetParent()->GetShadowRoot()->IsClosed()) {
    return nullptr;
  }

  return slot;
}

nsIContent::IMEState nsIContent::GetDesiredIMEState() {
  if (!IsEditable() || !IsInComposedDoc()) {
    // Check for the special case where we're dealing with elements which don't
    // have the editable flag set, but are readwrite (such as text controls).
    if (!IsElement() ||
        !AsElement()->State().HasState(ElementState::READWRITE)) {
      return IMEState(IMEEnabled::Disabled);
    }
  }
  // NOTE: The content for independent editors (e.g., input[type=text],
  // textarea) must override this method, so, we don't need to worry about
  // that here.
  nsIContent* editableAncestor = GetEditingHost();

  // This is in another editable content, use the result of it.
  if (editableAncestor && editableAncestor != this) {
    return editableAncestor->GetDesiredIMEState();
  }
  Document* doc = GetComposedDoc();
  if (!doc) {
    return IMEState(IMEEnabled::Disabled);
  }
  nsPresContext* pc = doc->GetPresContext();
  if (!pc) {
    return IMEState(IMEEnabled::Disabled);
  }
  HTMLEditor* htmlEditor = nsContentUtils::GetHTMLEditor(pc);
  if (!htmlEditor) {
    return IMEState(IMEEnabled::Disabled);
  }
  // FYI: HTMLEditor::GetPreferredIMEState() is infallible.
  return htmlEditor->GetPreferredIMEState().unwrap();
}

bool nsIContent::HasIndependentSelection() const {
  nsIFrame* frame = GetPrimaryFrame();
  return frame && frame->IsInsideTextControl();
}

dom::Element* nsIContent::GetEditingHost() const {
  // If this isn't editable, return nullptr.
  if (!IsEditable()) {
    return nullptr;
  }

  Document* doc = GetComposedDoc();
  if (!doc) {
    return nullptr;
  }

  dom::Element* editableParentElement = nullptr;
  for (dom::Element* parent = GetParentElement();
       parent && parent->HasFlag(NODE_IS_EDITABLE);
       parent = editableParentElement->GetParentElement()) {
    editableParentElement = parent;
  }
  // If this is in designMode, and we have reached the root <html> element (i.e.
  // no contenteditable=false along the way), we should return the <body>
  // instead. Otherwise, we return the outermost editable element.
  if (IsInDesignMode() && editableParentElement &&
      editableParentElement->IsHTMLElement(nsGkAtoms::html) &&
      !IsInShadowTree()) {
    // FIXME: There may be no <body> or it may not be editable.
    // In such cases we should use root element instead.
    auto* body = doc->GetBodyElement();
    // return null if body has contenteditable=false
    return body && body->IsEditable() ? body : nullptr;
  }
  return editableParentElement
             ? editableParentElement
             : dom::Element::FromNode(const_cast<nsIContent*>(this));
}

nsresult nsIContent::LookupNamespaceURIInternal(
    const nsAString& aNamespacePrefix, nsAString& aNamespaceURI) const {
  if (aNamespacePrefix.EqualsLiteral("xml")) {
    // Special-case for xml prefix
    aNamespaceURI.AssignLiteral("http://www.w3.org/XML/1998/namespace");
    return NS_OK;
  }

  if (aNamespacePrefix.EqualsLiteral("xmlns")) {
    // Special-case for xmlns prefix
    aNamespaceURI.AssignLiteral("http://www.w3.org/2000/xmlns/");
    return NS_OK;
  }

  RefPtr<nsAtom> name;
  if (!aNamespacePrefix.IsEmpty()) {
    name = NS_Atomize(aNamespacePrefix);
    NS_ENSURE_TRUE(name, NS_ERROR_OUT_OF_MEMORY);
  } else {
    name = nsGkAtoms::xmlns;
  }
  // Trace up the content parent chain looking for the namespace
  // declaration that declares aNamespacePrefix.
  for (Element* element = GetAsElementOrParentElement(); element;
       element = element->GetParentElement()) {
    if (element->GetAttr(kNameSpaceID_XMLNS, name, aNamespaceURI)) {
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;
}

nsIContent* nsIContent::GetInclusiveEditableAncestor() const {
  if (IsEditable()) {
    return const_cast<nsIContent*>(this);
  }
  for (auto* const content : AncestorsOfType<nsIContent>()) {
    if (content->IsEditable()) {
      return content;
    }
  }
  return nullptr;
}

nsAtom* nsIContent::GetLang() const {
  for (const Element* element = GetAsElementOrParentElement(); element;
       element = element->GetParentElement()) {
    if (!element->GetAttrCount()) {
      continue;
    }

    // xml:lang has precedence over lang on HTML elements (see
    // XHTML1 section C.7).
    const nsAttrValue* attr =
        element->GetParsedAttr(nsGkAtoms::lang, kNameSpaceID_XML);
    if (!attr && element->SupportsLangAttr()) {
      attr = element->GetParsedAttr(nsGkAtoms::lang);
    }
    if (attr) {
      MOZ_ASSERT(attr->Type() == nsAttrValue::eAtom);
      MOZ_ASSERT(attr->GetAtomValue());
      return attr->GetAtomValue();
    }
  }

  return nullptr;
}

nsIURI* nsIContent::GetBaseURI(bool aTryUseXHRDocBaseURI) const {
  if (SVGUseElement* use = GetContainingSVGUseShadowHost()) {
    if (URLExtraData* data = use->GetContentURLData()) {
      return data->BaseURI();
    }
  }

  return OwnerDoc()->GetBaseURI(aTryUseXHRDocBaseURI);
}

nsIURI* nsIContent::GetBaseURIForStyleAttr() const {
  if (SVGUseElement* use = GetContainingSVGUseShadowHost()) {
    if (URLExtraData* data = use->GetContentURLData()) {
      return data->BaseURI();
    }
  }
  // This also ignores the case that SVG inside XBL binding.
  // But it is probably fine.
  return OwnerDoc()->GetDocBaseURI();
}

already_AddRefed<URLExtraData> nsIContent::GetURLDataForStyleAttr(
    nsIPrincipal* aSubjectPrincipal) const {
  if (SVGUseElement* use = GetContainingSVGUseShadowHost()) {
    if (URLExtraData* data = use->GetContentURLData()) {
      return do_AddRef(data);
    }
  }
  auto* doc = OwnerDoc();
  if (aSubjectPrincipal && aSubjectPrincipal != NodePrincipal()) {
    nsCOMPtr<nsIReferrerInfo> referrerInfo =
        doc->ReferrerInfoForInternalCSSAndSVGResources();
    // TODO: Cache this?
    return MakeAndAddRef<URLExtraData>(doc->GetDocBaseURI(), referrerInfo,
                                       aSubjectPrincipal);
  }
  return do_AddRef(doc->DefaultStyleAttrURLData());
}

void nsIContent::UpdateHeadingElementsOffsetChange() {
  TreeIterator<FlattenedChildIterator> iter(*this);
  for (; iter.GetCurrent(); iter.GetNext()) {
    if (auto* heading = HTMLHeadingElement::FromNode(iter.GetCurrent())) {
      heading->UpdateLevel(true);
    }
  }
}

void nsIContent::ConstructUbiNode(void* storage) {
  JS::ubi::Concrete<nsIContent>::construct(storage, this);
}

bool nsIContent::InclusiveDescendantMayNeedSpellchecking(HTMLEditor* aEditor) {
  // Return true if the node may have elements as children, since those or their
  // descendants may have spellcheck attributes.
  return HasFlag(NODE_MAY_HAVE_ELEMENT_CHILDREN) ||
         mozInlineSpellChecker::ShouldSpellCheckNode(aEditor, this);
}

//----------------------------------------------------------------------

static inline JSObject* GetJSObjectChild(nsWrapperCache* aCache) {
  return aCache->PreservingWrapper() ? aCache->GetWrapperPreserveColor()
                                     : nullptr;
}

static bool NeedsScriptTraverse(nsINode* aNode) {
  return aNode->PreservingWrapper() && aNode->GetWrapperPreserveColor() &&
         !aNode->HasKnownLiveWrapperAndDoesNotNeedTracing(aNode);
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

NS_IMPL_CYCLE_COLLECTION(nsNodeSupportsWeakRefTearoff, mNode)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNodeSupportsWeakRefTearoff)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END_AGGREGATED(mNode)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsNodeSupportsWeakRefTearoff)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsNodeSupportsWeakRefTearoff)

NS_IMETHODIMP
nsNodeSupportsWeakRefTearoff::GetWeakReference(
    nsIWeakReference** aInstancePtr) {
  nsINode::nsSlots* slots = mNode->Slots();
  if (!slots->mWeakReference) {
    slots->mWeakReference = new nsNodeWeakReference(mNode);
  }

  NS_ADDREF(*aInstancePtr = slots->mWeakReference);

  return NS_OK;
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

void nsIContent::nsExtendedContentSlots::UnlinkExtendedSlots(nsIContent&) {
  mAssignedSlot = nullptr;
}

void nsIContent::nsExtendedContentSlots::TraverseExtendedSlots(
    nsCycleCollectionTraversalCallback& aCb) {
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(aCb, "mExtendedSlots->mAssignedSlot");
  aCb.NoteXPCOMChild(NS_ISUPPORTS_CAST(nsIContent*, mAssignedSlot.get()));
}

nsIContent::nsExtendedContentSlots::nsExtendedContentSlots() = default;

nsIContent::nsContentSlots* nsIContent::CreateSlots() {
  void* mem = AllocateSlots(sizeof(nsContentSlots));
  return new (mem) nsContentSlots();
}

nsIContent::nsExtendedContentSlots* nsIContent::CreateExtendedSlots() {
  void* mem = AllocateSlots(sizeof(nsExtendedContentSlots));
  return new (mem) nsExtendedContentSlots();
}

nsIContent::nsExtendedContentSlots::~nsExtendedContentSlots() {
  MOZ_ASSERT(!mManualSlotAssignment);
}

size_t nsIContent::nsExtendedContentSlots::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  // For now, nothing to measure here.  We don't actually own any of our
  // members.
  return 0;
}

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
  if (mRadioGroupContainer) {
    RadioGroupContainer::Traverse(mRadioGroupContainer.get(), aCb);
  }
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

static nsINode* FindChromeAccessOnlySubtreeOwnerForEvents(nsINode* aNode) {
  if (!aNode->ChromeOnlyAccessForEvents()) {
    return aNode;
  }
  return aNode->GetClosestNativeAnonymousSubtreeRootParentOrHost();
}

nsINode* FindChromeAccessOnlySubtreeOwnerForEvents(EventTarget* aTarget) {
  nsINode* node = nsINode::FromEventTargetOrNull(aTarget);
  if (!node) {
    return nullptr;
  }
  return FindChromeAccessOnlySubtreeOwnerForEvents(node);
}

void nsIContent::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  // FIXME! Document how this event retargeting works, Bug 329124.
  aVisitor.mCanHandle = true;
  aVisitor.mMayHaveListenerManager = HasListenerManager();

  if (IsInShadowTree()) {
    aVisitor.mItemInShadowTree = true;
  }

  // Don't propagate mouseover and mouseout events when mouse is moving
  // inside chrome access only content.
  const bool isAnonForEvents = IsRootOfChromeAccessOnlySubtree();
  aVisitor.mRootOfClosedTree = isAnonForEvents;
  if ((aVisitor.mEvent->mMessage == eMouseOver ||
       aVisitor.mEvent->mMessage == eMouseOut ||
       aVisitor.mEvent->mMessage == ePointerOver ||
       aVisitor.mEvent->mMessage == ePointerOut) &&
      // Check if we should stop event propagation when event has just been
      // dispatched or when we're about to propagate from
      // chrome access only subtree or if we are about to propagate out of
      // a shadow root to a shadow root host.
      ((this == aVisitor.mEvent->mOriginalTarget && !ChromeOnlyAccess()) ||
       isAnonForEvents)) {
    nsIContent* relatedTarget = nsIContent::FromEventTargetOrNull(
        aVisitor.mEvent->AsMouseEvent()->mRelatedTarget);
    if (relatedTarget && relatedTarget->OwnerDoc() == OwnerDoc()) {
      // If current target is anonymous for events or we know that related
      // target is descendant of an element which is anonymous for events,
      // we may want to stop event propagation.
      // If this is the original target, aVisitor.mRelatedTargetIsInAnon
      // must be updated.
      if (isAnonForEvents || aVisitor.mRelatedTargetIsInAnon ||
          (aVisitor.mEvent->mOriginalTarget == this &&
           (aVisitor.mRelatedTargetIsInAnon =
                relatedTarget->ChromeOnlyAccessForEvents()))) {
        nsINode* anonOwner = FindChromeAccessOnlySubtreeOwnerForEvents(this);
        if (anonOwner) {
          nsINode* anonOwnerRelated =
              FindChromeAccessOnlySubtreeOwnerForEvents(relatedTarget);
          if (anonOwnerRelated) {
            // Note, anonOwnerRelated may still be inside some other
            // native anonymous subtree. The case where anonOwner is still
            // inside native anonymous subtree will be handled when event
            // propagates up in the DOM tree.
            while (anonOwner != anonOwnerRelated &&
                   anonOwnerRelated->ChromeOnlyAccessForEvents()) {
              anonOwnerRelated =
                  FindChromeAccessOnlySubtreeOwnerForEvents(anonOwnerRelated);
            }
            if (anonOwner == anonOwnerRelated) {
#ifdef DEBUG_smaug
              nsIContent* originalTarget = nsIContent::FromEventTargetOrNull(
                  aVisitor.mEvent->mOriginalTarget);
              nsAutoString ot, ct, rt;
              if (originalTarget) {
                originalTarget->NodeInfo()->NameAtom()->ToString(ot);
              }
              NodeInfo()->NameAtom()->ToString(ct);
              relatedTarget->NodeInfo()->NameAtom()->ToString(rt);
              printf(
                  "Stopping %s propagation:"
                  "\n\toriginalTarget=%s \n\tcurrentTarget=%s %s"
                  "\n\trelatedTarget=%s %s \n%s",
                  (aVisitor.mEvent->mMessage == eMouseOver) ? "mouseover"
                                                            : "mouseout",
                  NS_ConvertUTF16toUTF8(ot).get(),
                  NS_ConvertUTF16toUTF8(ct).get(),
                  isAnonForEvents
                      ? "(is native anonymous)"
                      : (ChromeOnlyAccess() ? "(is in native anonymous subtree)"
                                            : ""),
                  NS_ConvertUTF16toUTF8(rt).get(),
                  relatedTarget->ChromeOnlyAccess()
                      ? "(is in native anonymous subtree)"
                      : "",
                  (originalTarget &&
                   relatedTarget->FindFirstNonChromeOnlyAccessContent() ==
                       originalTarget->FindFirstNonChromeOnlyAccessContent())
                      ? ""
                      : "Wrong event propagation!?!\n");
#endif
              aVisitor.SetParentTarget(nullptr, false);
              // Event should not propagate to non-anon content.
              aVisitor.mCanHandle = isAnonForEvents;
              return;
            }
          }
        }
      }
    }
  }

  // Event parent is the assigned slot, if node is assigned, or node's parent
  // otherwise.
  HTMLSlotElement* slot = GetAssignedSlot();
  nsIContent* parent = slot ? slot : GetParent();

  // Event may need to be retargeted if this is the root of a native anonymous
  // content subtree.
  if (isAnonForEvents) {
    aVisitor.mEventTargetAtParent = parent;
  } else if (parent && aVisitor.mOriginalTargetIsInAnon) {
    nsIContent* content =
        nsIContent::FromEventTargetOrNull(aVisitor.mEvent->mTarget);
    if (content &&
        content->GetClosestNativeAnonymousSubtreeRootParentOrHost() == parent) {
      aVisitor.mEventTargetAtParent = parent;
    }
  }

  if (!aVisitor.mEvent->mFlags.mComposedInNativeAnonymousContent &&
      isAnonForEvents && OwnerDoc()->GetWindow()) {
    aVisitor.SetParentTarget(OwnerDoc()->GetWindow()->GetParentTarget(), true);
  } else if (parent) {
    aVisitor.SetParentTarget(parent, false);
    if (slot) {
      ShadowRoot* root = slot->GetContainingShadow();
      if (root && root->IsClosed()) {
        aVisitor.mParentIsSlotInClosedTree = true;
      }
    }
  } else {
    aVisitor.SetParentTarget(GetComposedDoc(), false);
  }

  if (!ChromeOnlyAccessForEvents() &&
      !aVisitor.mRelatedTargetRetargetedInCurrentScope) {
    // We don't support Shadow DOM in native anonymous content yet.
    aVisitor.mRelatedTargetRetargetedInCurrentScope = true;
    if (aVisitor.mEvent->mOriginalRelatedTarget) {
      // https://dom.spec.whatwg.org/#concept-event-dispatch
      // Step 3.
      // "Let relatedTarget be the result of retargeting event's relatedTarget
      //  against target if event's relatedTarget is non-null, and null
      //  otherwise."
      //
      // This is a bit complicated because the event might be from native
      // anonymous content, but we need to deal with non-native anonymous
      // content there.
      bool initialTarget = this == aVisitor.mEvent->mOriginalTarget;
      nsINode* originalTargetAsNode = nullptr;
      // Use of mOriginalTargetIsInAnon is an optimization here.
      if (!initialTarget && aVisitor.mOriginalTargetIsInAnon) {
        originalTargetAsNode = FindChromeAccessOnlySubtreeOwnerForEvents(
            aVisitor.mEvent->mOriginalTarget);
        initialTarget = originalTargetAsNode == this;
      }
      if (initialTarget) {
        nsINode* relatedTargetAsNode =
            FindChromeAccessOnlySubtreeOwnerForEvents(
                aVisitor.mEvent->mOriginalRelatedTarget);
        if (!originalTargetAsNode) {
          originalTargetAsNode =
              nsINode::FromEventTargetOrNull(aVisitor.mEvent->mOriginalTarget);
        }

        if (relatedTargetAsNode && originalTargetAsNode) {
          nsINode* retargetedRelatedTarget = nsContentUtils::Retarget(
              relatedTargetAsNode, originalTargetAsNode);
          if (originalTargetAsNode == retargetedRelatedTarget &&
              retargetedRelatedTarget != relatedTargetAsNode) {
            // Step 4.
            // "If target is relatedTarget and target is not event's
            //  relatedTarget, then return true."
            aVisitor.IgnoreCurrentTargetBecauseOfShadowDOMRetargeting();
            // Old code relies on mTarget to point to the first element which
            // was not added to the event target chain because of mCanHandle
            // being false, but in Shadow DOM case mTarget really should
            // point to a node in Shadow DOM.
            aVisitor.mEvent->mTarget = aVisitor.mTargetInKnownToBeHandledScope;
            return;
          }

          // Part of step 5. Retargeting target has happened already higher
          // up in this method.
          // "Append to an event path with event, target, targetOverride,
          //  relatedTarget, and false."
          aVisitor.mRetargetedRelatedTarget = retargetedRelatedTarget;
        }
      } else if (nsINode* relatedTargetAsNode =
                     FindChromeAccessOnlySubtreeOwnerForEvents(
                         aVisitor.mEvent->mOriginalRelatedTarget)) {
        // Step 11.3.
        // "Let relatedTarget be the result of retargeting event's
        // relatedTarget against parent if event's relatedTarget is non-null,
        // and null otherwise.".
        nsINode* retargetedRelatedTarget =
            nsContentUtils::Retarget(relatedTargetAsNode, this);
        nsINode* targetInKnownToBeHandledScope =
            FindChromeAccessOnlySubtreeOwnerForEvents(
                aVisitor.mTargetInKnownToBeHandledScope);
        // If aVisitor.mTargetInKnownToBeHandledScope wasn't nsINode,
        // targetInKnownToBeHandledScope will be null. This may happen when
        // dispatching event to Window object in a content page and
        // propagating the event to a chrome Element.
        if (targetInKnownToBeHandledScope &&
            IsShadowIncludingInclusiveDescendantOf(
                targetInKnownToBeHandledScope->SubtreeRoot())) {
          // Part of step 11.4.
          // "If target's root is a shadow-including inclusive ancestor of
          //  parent, then"
          // "...Append to an event path with event, parent, null,
          // relatedTarget, "   and slot-in-closed-tree."
          aVisitor.mRetargetedRelatedTarget = retargetedRelatedTarget;
        } else if (this == retargetedRelatedTarget) {
          // Step 11.5
          // "Otherwise, if parent and relatedTarget are identical, then set
          //  parent to null."
          aVisitor.IgnoreCurrentTargetBecauseOfShadowDOMRetargeting();
          // Old code relies on mTarget to point to the first element which
          // was not added to the event target chain because of mCanHandle
          // being false, but in Shadow DOM case mTarget really should
          // point to a node in Shadow DOM.
          aVisitor.mEvent->mTarget = aVisitor.mTargetInKnownToBeHandledScope;
          return;
        } else if (targetInKnownToBeHandledScope) {
          // Note, if targetInKnownToBeHandledScope is null,
          // mTargetInKnownToBeHandledScope could be Window object in content
          // page and we're in chrome document in the same process.

          // Step 11.6
          aVisitor.mRetargetedRelatedTarget = retargetedRelatedTarget;
        }
      }
    }

    if (aVisitor.mEvent->mClass == eTouchEventClass) {
      // Retarget touch objects.
      MOZ_ASSERT(!aVisitor.mRetargetedTouchTargets.isSome());
      aVisitor.mRetargetedTouchTargets.emplace();
      WidgetTouchEvent* touchEvent = aVisitor.mEvent->AsTouchEvent();
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      for (uint32_t i = 0; i < touches.Length(); ++i) {
        Touch* touch = touches[i];
        EventTarget* originalTarget = touch->mOriginalTarget;
        EventTarget* touchTarget = originalTarget;
        nsCOMPtr<nsINode> targetAsNode =
            nsINode::FromEventTargetOrNull(originalTarget);
        if (targetAsNode) {
          EventTarget* retargeted =
              nsContentUtils::Retarget(targetAsNode, this);
          if (retargeted) {
            touchTarget = retargeted;
          }
        }
        aVisitor.mRetargetedTouchTargets->AppendElement(touchTarget);
        touch->mTarget = touchTarget;
      }
      MOZ_ASSERT(aVisitor.mRetargetedTouchTargets->Length() ==
                 touches.Length());
    }
  }

  if (slot) {
    // Inform that we're about to exit the current scope.
    aVisitor.mRelatedTargetRetargetedInCurrentScope = false;
  }
}

Element* nsIContent::GetAutofocusDelegate(IsFocusableFlags aFlags) const {
  for (nsINode* node = GetFirstChild(); node; node = node->GetNextNode(this)) {
    auto* descendant = Element::FromNode(*node);
    if (!descendant || !descendant->GetBoolAttr(nsGkAtoms::autofocus)) {
      continue;
    }

    nsIFrame* frame = descendant->GetPrimaryFrame();
    if (frame && frame->IsFocusable(aFlags)) {
      return descendant;
    }
  }
  return nullptr;
}

bool nsIContent::CanStartSelectionAsWebCompatHack() const {
  if (!StaticPrefs::dom_selection_mimic_chrome_tostring_enabled()) {
    return true;
  }

  for (const nsIContent* content = this; content;
       content = content->GetFlattenedTreeParent()) {
    if (content->IsEditable()) {
      return true;
    }
    nsIFrame* frame = content->GetPrimaryFrame();
    if (!frame) {
      return true;
    }
    if (!frame->IsSelectable()) {
      return false;
    }
  }

  return true;
}

Element* nsIContent::GetFocusDelegate(IsFocusableFlags aFlags) const {
  const nsIContent* whereToLook = this;
  if (ShadowRoot* root = GetShadowRoot()) {
    if (!root->DelegatesFocus()) {
      // 1. If focusTarget is a shadow host and its shadow root 's delegates
      // focus is false, then return null.
      return nullptr;
    }
    whereToLook = root;
  }

  auto IsFocusable = [&](Element* aElement) -> Focusable {
    nsIFrame* frame = aElement->GetPrimaryFrame();

    if (!frame) {
      return {};
    }

    return frame->IsFocusable(aFlags);
  };

  Element* potentialFocus = nullptr;
  for (nsINode* node = whereToLook->GetFirstChild(); node;
       node = node->GetNextNode(whereToLook)) {
    auto* el = Element::FromNode(*node);
    if (!el) {
      continue;
    }

    const bool autofocus = el->GetBoolAttr(nsGkAtoms::autofocus);

    if (autofocus) {
      if (IsFocusable(el)) {
        // Found an autofocus candidate.
        return el;
      }
    } else if (!potentialFocus) {
      if (Focusable focusable = IsFocusable(el)) {
        if (IsHTMLElement(nsGkAtoms::dialog)) {
          if (focusable.mTabIndex >= 0) {
            // If focusTarget is a dialog element and descendant is sequentially
            // focusable, then set focusableArea to descendant.
            potentialFocus = el;
          }
        } else {
          // This element could be the one if we can't find an
          // autofocus candidate which has the precedence.
          potentialFocus = el;
        }
      }
    }

    if (!autofocus && potentialFocus) {
      // Nothing else to do, we are not looking for more focusable elements
      // here.
      continue;
    }

    if (auto* shadow = el->GetShadowRoot()) {
      if (shadow->DelegatesFocus()) {
        if (Element* delegatedFocus = shadow->GetFocusDelegate(aFlags)) {
          if (autofocus) {
            // This element has autofocus and we found an focus delegates
            // in its descendants, so use the focus delegates
            return delegatedFocus;
          }
          if (!potentialFocus) {
            potentialFocus = delegatedFocus;
          }
        }
      }
    }
  }

  return potentialFocus;
}

Focusable nsIContent::IsFocusableWithoutStyle(IsFocusableFlags) {
  // Default, not tabbable
  return {};
}

void nsIContent::SetAssignedSlot(HTMLSlotElement* aSlot) {
  MOZ_ASSERT(aSlot || GetExistingExtendedContentSlots());
  ExtendedContentSlots()->mAssignedSlot = aSlot;
}

#ifdef MOZ_DOM_LIST
void nsIContent::Dump() { List(); }
#endif

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
    shadowRoot->Unbind();
    tmp->ExtendedDOMSlots()->mShadowRoot = nullptr;
  }

  if (tmp->IsElement()) {
    auto* element = tmp->AsElement();
    if (MOZ_UNLIKELY(element->HasFlag(ELEMENT_HAS_EDIT_CONTEXT))) {
      element->ClearEditContext();
    }
    Element::UnlinkCustomElementRegistry(element);
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
