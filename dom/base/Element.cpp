/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all element classes; this provides an implementation
 * of DOM Core's Element, implements nsIContent, provides
 * utility methods for subclasses, and so forth.
 */

#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"

#include <cstddef>
#include <inttypes.h>
#include <initializer_list>
#include <utility>
#include "DOMMatrix.h"
#include "ExpandedPrincipal.h"
#include "PresShellInlines.h"
#include "jsapi.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/AnimationComparator.h"
#include "mozilla/AnimationTarget.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/CORSMode.h"
#include "mozilla/Components.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/EditorBase.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/EffectSet.h"
#include "mozilla/ElementAnimationData.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/FullscreenChange.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/InternalMutationEvent.h"
#include "mozilla/Likely.h"
#include "mozilla/LinkedList.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/NotNull.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellForwards.h"
#include "mozilla/ReflowOutput.h"
#include "mozilla/RefPtr.h"
#include "mozilla/RelativeTo.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/ServoStyleConstsInlines.h"
#include "mozilla/SizeOfState.h"
#include "mozilla/SourceLocation.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/StaticString.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/Try.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/AnimatableBinding.h"
#include "mozilla/dom/Animation.h"
#include "mozilla/dom/Attr.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/CloseWatcher.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/CSPViolationData.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/Flex.h"
#include "mozilla/dom/FragmentOrElement.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/Grid.h"
#include "mozilla/dom/HTMLDivElement.h"
#include "mozilla/dom/HTMLElement.h"
#include "mozilla/dom/HTMLParagraphElement.h"
#include "mozilla/dom/HTMLPreElement.h"
#include "mozilla/dom/HTMLSpanElement.h"
#include "mozilla/dom/HTMLTableCellElement.h"
#include "mozilla/dom/HTMLTemplateElement.h"
#include "mozilla/dom/KeyframeAnimationOptionsBinding.h"
#include "mozilla/dom/KeyframeEffect.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/MutationEventBinding.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/PointerEventHandler.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Sanitizer.h"
#include "mozilla/dom/SVGElement.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/Text.h"
#include "mozilla/dom/TrustedHTML.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/UnbindContext.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/dom/XULCommandEvent.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/gfx/BasePoint.h"
#include "mozilla/gfx/BaseRect.h"
#include "mozilla/gfx/BaseSize.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/widget/Screen.h"
#include "nsAtom.h"
#include "nsAttrName.h"
#include "nsAttrValueInlines.h"
#include "nsAttrValueOrString.h"
#include "nsBaseHashtable.h"
#include "nsBlockFrame.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsCSSPseudoElements.h"
#include "nsCompatibility.h"
#include "nsContainerFrame.h"
#include "nsContentList.h"
#include "nsContentListDeclarations.h"
#include "nsCoord.h"
#include "nsDOMAttributeMap.h"
#include "nsDOMCSSAttrDeclaration.h"
#include "nsDOMMutationObserver.h"
#include "nsDOMString.h"
#include "nsDOMStringMap.h"
#include "nsDOMTokenList.h"
#include "nsDocShell.h"
#include "nsError.h"
#include "nsFlexContainerFrame.h"
#include "nsFocusManager.h"
#include "nsFrameState.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsGridContainerFrame.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIAutoCompletePopup.h"
#include "nsIBrowser.h"
#include "nsIContentInlines.h"
#include "nsIDOMXULButtonElement.h"
#include "nsIDOMXULContainerElement.h"
#include "nsIDOMXULControlElement.h"
#include "nsIDOMXULMenuListElement.h"
#include "nsIDOMXULMultSelectCntrlEl.h"
#include "nsIDOMXULRadioGroupElement.h"
#include "nsIDOMXULRelatedElement.h"
#include "nsIDOMXULSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDocShell.h"
#include "nsIFocusManager.h"
#include "nsIFrame.h"
#include "nsIGlobalObject.h"
#include "nsIIOService.h"
#include "nsIInterfaceRequestor.h"
#include "nsIMemoryReporter.h"
#include "nsIPrincipal.h"
#include "nsIScriptError.h"
#include "nsISpeculativeConnect.h"
#include "nsISupports.h"
#include "nsISupportsUtils.h"
#include "nsIURI.h"
#include "nsLayoutUtils.h"
#include "nsLineBox.h"
#include "nsLiteralString.h"
#include "nsNameSpaceManager.h"
#include "nsNodeInfoManager.h"
#include "nsPIDOMWindow.h"
#include "nsPoint.h"
#include "nsPresContext.h"
#include "nsQueryFrame.h"
#include "nsRefPtrHashtable.h"
#include "nsSize.h"
#include "nsString.h"
#include "nsStyleConsts.h"
#include "nsStyleStruct.h"
#include "nsStyledElement.h"
#include "nsTArray.h"
#include "nsTextNode.h"
#include "nsThreadUtils.h"
#include "nsViewManager.h"
#include "nsWindowSizes.h"

#include "nsXULElement.h"

#ifdef DEBUG
#  include "nsRange.h"
#endif

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

using mozilla::gfx::Matrix4x4;

namespace mozilla::dom {

// Verify sizes of nodes. We use a template rather than a direct static
// assert so that the error message actually displays the sizes.
// On 32 bit systems the actual allocated size varies a bit between
// OSes/compilers.
//
// We need different numbers on certain build types to deal with the owning
// thread pointer that comes with the non-threadsafe refcount on
// nsIContent.
#ifdef MOZ_THREAD_SAFETY_OWNERSHIP_CHECKS_SUPPORTED
#  define EXTRA_DOM_NODE_BYTES 8
#else
#  define EXTRA_DOM_NODE_BYTES 0
#endif

#define ASSERT_NODE_SIZE(type, opt_size_64, opt_size_32)              \
  template <int a, int sizeOn64, int sizeOn32>                        \
  struct Check##type##Size {                                          \
    static_assert((sizeof(void*) == 8 && a == sizeOn64) ||            \
                      (sizeof(void*) == 4 && a <= sizeOn32),          \
                  "DOM size changed");                                \
  };                                                                  \
  Check##type##Size<sizeof(type), opt_size_64 + EXTRA_DOM_NODE_BYTES, \
                    opt_size_32 + EXTRA_DOM_NODE_BYTES>               \
      g##type##CES;

// Note that mozjemalloc uses a 16 byte quantum, so 64, 80 and 128 are
// bucket sizes.
ASSERT_NODE_SIZE(Element, 128, 80);
ASSERT_NODE_SIZE(HTMLDivElement, 128, 80);
ASSERT_NODE_SIZE(HTMLElement, 128, 80);
ASSERT_NODE_SIZE(HTMLParagraphElement, 128, 80);
ASSERT_NODE_SIZE(HTMLPreElement, 128, 80);
ASSERT_NODE_SIZE(HTMLSpanElement, 128, 80);
ASSERT_NODE_SIZE(HTMLTableCellElement, 128, 80);
ASSERT_NODE_SIZE(Text, 120, 80);

#undef ASSERT_NODE_SIZE
#undef EXTRA_DOM_NODE_BYTES

}  // namespace mozilla::dom

nsAtom* nsIContent::DoGetID() const {
  MOZ_ASSERT(HasID(), "Unexpected call");
  MOZ_ASSERT(IsElement(), "Only elements can have IDs");

  return AsElement()->GetParsedAttr(nsGkAtoms::id)->GetAtomValue();
}

nsIFrame* nsIContent::GetPrimaryFrame(mozilla::FlushType aType) {
  Document* doc = GetComposedDoc();
  if (!doc) {
    return nullptr;
  }

  // Cause a flush, so we get up-to-date frame information.
  if (aType != mozilla::FlushType::None) {
    doc->FlushPendingNotifications(aType);
  }

  auto* frame = GetPrimaryFrame();
  if (!frame) {
    return nullptr;
  }

  if (aType == mozilla::FlushType::Layout) {
    frame->PresShell()->EnsureReflowIfFrameHasHiddenContent(frame);
    frame = GetPrimaryFrame();
  }

  return frame;
}

namespace mozilla::dom {

const DOMTokenListSupportedToken Element::sSupportedBlockingValues[] = {
    "render", nullptr};

nsDOMAttributeMap* Element::Attributes() {
  nsDOMSlots* slots = DOMSlots();
  if (!slots->mAttributeMap) {
    slots->mAttributeMap = new nsDOMAttributeMap(this);
  }

  return slots->mAttributeMap;
}

void Element::SetPointerCapture(int32_t aPointerId, ErrorResult& aError) {
  if (OwnerDoc()->ShouldResistFingerprinting(RFPTarget::PointerId) &&
      aPointerId != PointerEventHandler::GetSpoofedPointerIdForRFP()) {
    aError.ThrowNotFoundError("Invalid pointer id");
    return;
  }
  const PointerInfo* pointerInfo =
      PointerEventHandler::GetPointerInfo(aPointerId);
  if (!pointerInfo) {
    aError.ThrowNotFoundError("Invalid pointer id");
    return;
  }
  if (!IsInComposedDoc()) {
    aError.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  if (OwnerDoc()->GetPointerLockElement()) {
    // Throw an exception 'InvalidStateError' while the page has a locked
    // element.
    aError.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  // XXX If pointerInfo->mIsSynthesizedForTests does not match the last
  // WidgetPointerEvent's mFlags.mIsSynthesizedForTests, should we treat it
  // as unknown pointerId?
  if (!pointerInfo->mActiveState ||
      pointerInfo->mActiveDocument != OwnerDoc()) {
    return;
  }
  PointerEventHandler::RequestPointerCaptureById(aPointerId, this);
}

void Element::ReleasePointerCapture(int32_t aPointerId, ErrorResult& aError) {
  if (OwnerDoc()->ShouldResistFingerprinting(RFPTarget::PointerId) &&
      aPointerId != PointerEventHandler::GetSpoofedPointerIdForRFP()) {
    aError.ThrowNotFoundError("Invalid pointer id");
    return;
  }
  if (!PointerEventHandler::GetPointerInfo(aPointerId)) {
    aError.ThrowNotFoundError("Invalid pointer id");
    return;
  }
  if (HasPointerCapture(aPointerId)) {
    PointerEventHandler::ReleasePointerCaptureById(aPointerId);
  }
}

bool Element::HasPointerCapture(long aPointerId) {
  PointerCaptureInfo* pointerCaptureInfo =
      PointerEventHandler::GetPointerCaptureInfo(aPointerId);
  if (pointerCaptureInfo && pointerCaptureInfo->mPendingElement == this) {
    return true;
  }
  return false;
}

const nsAttrValue* Element::GetSVGAnimatedClass() const {
  MOZ_ASSERT(MayHaveClass() && IsSVGElement(), "Unexpected call");
  return static_cast<const SVGElement*>(this)->GetAnimatedClassName();
}

NS_IMETHODIMP
Element::QueryInterface(REFNSIID aIID, void** aInstancePtr) {
  if (aIID.Equals(NS_GET_IID(Element))) {
    NS_ADDREF_THIS();
    *aInstancePtr = this;
    return NS_OK;
  }

  NS_ASSERTION(aInstancePtr, "QueryInterface requires a non-NULL destination!");
  nsresult rv = FragmentOrElement::QueryInterface(aIID, aInstancePtr);
  if (NS_SUCCEEDED(rv)) {
    return NS_OK;
  }

  return NS_NOINTERFACE;
}

void Element::NotifyStateChange(ElementState aStates) {
  MOZ_ASSERT(!aStates.IsEmpty());
  if (Document* doc = GetComposedDoc()) {
    nsAutoScriptBlocker scriptBlocker;
    doc->ElementStateChanged(this, aStates);
  }
}

}  // namespace mozilla::dom

void nsIContent::UpdateEditableState(bool aNotify) {
  if (IsInNativeAnonymousSubtree()) {
    // Don't propagate the editable flag into native anonymous subtrees.
    if (IsRootOfNativeAnonymousSubtree()) {
      return;
    }

    // We allow setting the flag on NAC (explicitly, see
    // nsTextControlFrame::CreateAnonymousContent for example), but not
    // unsetting it.
    //
    // Otherwise, just the act of binding the NAC subtree into our non-anonymous
    // parent would clear the flag, which is not good. As we shouldn't move NAC
    // around, this is fine.
    if (HasFlag(NODE_IS_EDITABLE)) {
      return;
    }
  }

  nsIContent* parent = GetParent();
  SetEditableFlag(parent && parent->HasFlag(NODE_IS_EDITABLE));
}

namespace mozilla::dom {

void Element::UpdateEditableState(bool aNotify) {
  nsIContent::UpdateEditableState(aNotify);
  UpdateReadOnlyState(aNotify);
}

bool Element::IsReadOnlyInternal() const { return !IsEditable(); }

void Element::UpdateReadOnlyState(bool aNotify) {
  auto oldState = State();
  if (IsReadOnlyInternal()) {
    RemoveStatesSilently(ElementState::READWRITE);
    AddStatesSilently(ElementState::READONLY);
  } else {
    RemoveStatesSilently(ElementState::READONLY);
    AddStatesSilently(ElementState::READWRITE);
  }
  if (!aNotify) {
    return;
  }
  const auto newState = State();
  if (newState != oldState) {
    NotifyStateChange(newState ^ oldState);
  }
}

Maybe<int32_t> Element::GetTabIndexAttrValue() {
  const nsAttrValue* attrVal = GetParsedAttr(nsGkAtoms::tabindex);
  if (attrVal && attrVal->Type() == nsAttrValue::eInteger) {
    return Some(attrVal->GetIntegerValue());
  }

  return Nothing();
}

int32_t Element::TabIndex() {
  Maybe<int32_t> attrVal = GetTabIndexAttrValue();
  if (attrVal.isSome()) {
    return attrVal.value();
  }

  return TabIndexDefault();
}

void Element::Focus(const FocusOptions& aOptions, CallerType aCallerType,
                    ErrorResult& aError) {
  const RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  if (MOZ_UNLIKELY(!fm)) {
    return;
  }
  const OwningNonNull<Element> kungFuDeathGrip(*this);
  // Also other browsers seem to have the hack to not re-focus (and flush) when
  // the element is already focused.
  // Until https://github.com/whatwg/html/issues/4512 is clarified, we'll
  // maintain interoperatibility by not re-focusing, independent of aOptions.
  // I.e., `focus({ preventScroll: true})` followed by `focus( { preventScroll:
  // false })` won't re-focus.
  if (fm->CanSkipFocus(this)) {
    fm->NotifyOfReFocus(kungFuDeathGrip);
    fm->NeedsFlushBeforeEventHandling(this);
    return;
  }
  uint32_t fmFlags = nsFocusManager::ProgrammaticFocusFlags(aOptions);
  if (aCallerType == CallerType::NonSystem) {
    fmFlags |= nsIFocusManager::FLAG_NONSYSTEMCALLER;
  }
  aError = fm->SetFocus(kungFuDeathGrip, fmFlags);
}

void Element::SetTabIndex(int32_t aTabIndex, mozilla::ErrorResult& aError) {
  nsAutoString value;
  value.AppendInt(aTabIndex);

  SetAttr(nsGkAtoms::tabindex, value, aError);
}

void Element::SetShadowRoot(ShadowRoot* aShadowRoot) {
  nsExtendedDOMSlots* slots = ExtendedDOMSlots();
  MOZ_ASSERT(!aShadowRoot || !slots->mShadowRoot,
             "We shouldn't clear the shadow root without unbind first");
  slots->mShadowRoot = aShadowRoot;
}

void Element::SetLastRememberedBSize(float aBSize) {
  ExtendedDOMSlots()->mLastRememberedBSize = Some(aBSize);
}

void Element::SetLastRememberedISize(float aISize) {
  ExtendedDOMSlots()->mLastRememberedISize = Some(aISize);
}

void Element::RemoveLastRememberedBSize() {
  if (nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots()) {
    slots->mLastRememberedBSize.reset();
  }
}

void Element::RemoveLastRememberedISize() {
  if (nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots()) {
    slots->mLastRememberedISize.reset();
  }
}

void Element::Blur(mozilla::ErrorResult& aError) {
  if (!ShouldBlur(this)) {
    return;
  }

  Document* doc = GetComposedDoc();
  if (!doc) {
    return;
  }

  if (nsCOMPtr<nsPIDOMWindowOuter> win = doc->GetWindow()) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      aError = fm->ClearFocus(win);
    }
  }
}

ElementState Element::StyleStateFromLocks() const {
  StyleStateLocks locksAndValues = LockedStyleStates();
  ElementState locks = locksAndValues.mLocks;
  ElementState values = locksAndValues.mValues;
  ElementState state = (mState & ~locks) | (locks & values);

  if (state.HasState(ElementState::VISITED)) {
    return state & ~ElementState::UNVISITED;
  }
  if (state.HasState(ElementState::UNVISITED)) {
    return state & ~ElementState::VISITED;
  }

  return state;
}

Element::StyleStateLocks Element::LockedStyleStates() const {
  StyleStateLocks* locks =
      static_cast<StyleStateLocks*>(GetProperty(nsGkAtoms::lockedStyleStates));
  if (locks) {
    return *locks;
  }
  return StyleStateLocks();
}

void Element::NotifyStyleStateChange(ElementState aStates) {
  if (RefPtr<Document> doc = GetComposedDoc()) {
    if (RefPtr<PresShell> presShell = doc->GetPresShell()) {
      nsAutoScriptBlocker scriptBlocker;
      presShell->ElementStateChanged(doc, this, aStates);
    }
  }
}

void Element::LockStyleStates(ElementState aStates, bool aEnabled) {
  StyleStateLocks* locks = new StyleStateLocks(LockedStyleStates());

  locks->mLocks |= aStates;
  if (aEnabled) {
    locks->mValues |= aStates;
  } else {
    locks->mValues &= ~aStates;
  }

  if (aStates.HasState(ElementState::VISITED)) {
    locks->mLocks &= ~ElementState::UNVISITED;
  }
  if (aStates.HasState(ElementState::UNVISITED)) {
    locks->mLocks &= ~ElementState::VISITED;
  }

  SetProperty(nsGkAtoms::lockedStyleStates, locks,
              nsINode::DeleteProperty<StyleStateLocks>);
  SetHasLockedStyleStates();

  NotifyStyleStateChange(aStates);
}

void Element::UnlockStyleStates(ElementState aStates) {
  StyleStateLocks* locks = new StyleStateLocks(LockedStyleStates());

  locks->mLocks &= ~aStates;

  if (locks->mLocks.IsEmpty()) {
    RemoveProperty(nsGkAtoms::lockedStyleStates);
    ClearHasLockedStyleStates();
    delete locks;
  } else {
    SetProperty(nsGkAtoms::lockedStyleStates, locks,
                nsINode::DeleteProperty<StyleStateLocks>);
  }

  NotifyStyleStateChange(aStates);
}

void Element::ClearStyleStateLocks() {
  StyleStateLocks locks = LockedStyleStates();

  RemoveProperty(nsGkAtoms::lockedStyleStates);
  ClearHasLockedStyleStates();

  NotifyStyleStateChange(locks.mLocks);
}

/* virtual */
nsINode* Element::GetScopeChainParent() const { return OwnerDoc(); }

JSObject* Element::WrapNode(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return Element_Binding::Wrap(aCx, this, aGivenProto);
}

nsDOMTokenList* Element::ClassList() {
  nsDOMSlots* slots = DOMSlots();
  if (!slots->mClassList) {
    slots->mClassList = new nsDOMTokenList(this, nsGkAtoms::_class);
  }
  return slots->mClassList;
}

nsDOMTokenList* Element::Part() {
  nsExtendedDOMSlots* slots = ExtendedDOMSlots();
  if (!slots->mPart) {
    slots->mPart = new nsDOMTokenList(this, nsGkAtoms::part);
  }
  return slots->mPart;
}

void Element::RecompileScriptEventListeners() {
  for (uint32_t i = 0, count = mAttrs.AttrCount(); i < count; ++i) {
    BorrowedAttrInfo attrInfo = mAttrs.AttrInfoAt(i);

    // Eventlistenener-attributes are always in the null namespace
    if (!attrInfo.mName->IsAtom()) {
      continue;
    }

    nsAtom* attr = attrInfo.mName->Atom();
    if (!IsEventAttributeName(attr)) {
      continue;
    }

    nsAutoString value;
    attrInfo.mValue->ToString(value);
    SetEventHandler(GetEventNameForAttr(attr), value, true);
  }
}

void Element::GetAttributeNames(nsTArray<nsString>& aResult) {
  uint32_t count = mAttrs.AttrCount();
  for (uint32_t i = 0; i < count; ++i) {
    const nsAttrName* name = mAttrs.AttrNameAt(i);
    name->GetQualifiedName(*aResult.AppendElement());
  }
}

already_AddRefed<nsIHTMLCollection> Element::GetElementsByTagName(
    const nsAString& aLocalName) {
  return NS_GetContentList(this, kNameSpaceID_Unknown, aLocalName);
}

ScrollContainerFrame* Element::GetScrollContainerFrame(nsIFrame** aFrame,
                                                       FlushType aFlushType) {
  nsIFrame* frame = GetPrimaryFrame(aFlushType);
  if (aFrame) {
    *aFrame = frame;
  }
  if (frame) {
    if (frame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
      // It's unclear what to return for SVG frames, so just return null.
      return nullptr;
    }

    if (ScrollContainerFrame* scrollContainerFrame =
            frame->GetScrollTargetFrame()) {
      MOZ_ASSERT(!OwnerDoc()->IsScrollingElement(this),
                 "How can we have a scroll container frame if we're the "
                 "scrollingElement for our document?");
      return scrollContainerFrame;
    }
  }

  Document* doc = OwnerDoc();
  // Note: This IsScrollingElement() call can flush frames, if we're the body of
  // a quirks mode document.
  const bool isScrollingElement = doc->IsScrollingElement(this);
  if (isScrollingElement) {
    // Our scroll info should map to the root scroll container frame if there is
    // one.
    if (PresShell* presShell = doc->GetPresShell()) {
      if (ScrollContainerFrame* rootScrollContainerFrame =
              presShell->GetRootScrollContainerFrame()) {
        if (aFrame) {
          *aFrame = rootScrollContainerFrame;
        }
        return rootScrollContainerFrame;
      }
    }
  }
  if (aFrame) {
    // Re-get *aFrame if the caller asked for it, because that frame flush can
    // kill it.
    *aFrame = GetPrimaryFrame(FlushType::None);
  }
  return nullptr;
}

bool Element::CheckVisibility(const CheckVisibilityOptions& aOptions) {
  nsIFrame* f =
      GetPrimaryFrame(aOptions.mFlush ? FlushType::Frames : FlushType::None);
  if (!f) {
    // 1. If this does not have an associated box, return false.
    return false;
  }

  EnumSet includeContentVisibility = {
      nsIFrame::IncludeContentVisibility::Hidden};
  if (aOptions.mContentVisibilityAuto) {
    includeContentVisibility += nsIFrame::IncludeContentVisibility::Auto;
  }
  // Steps 2 and 5
  if (f->IsHiddenByContentVisibilityOnAnyAncestor(includeContentVisibility)) {
    // 2. If a shadow-including ancestor of this has content-visibility: hidden,
    // return false.
    // 5. If a shadow-including ancestor of this skips its content due to
    // has content-visibility: auto, return false.
    return false;
  }

  if ((aOptions.mOpacityProperty || aOptions.mCheckOpacity) &&
      f->Style()->IsInOpacityZeroSubtree()) {
    // 3. If the checkOpacity dictionary member of options is true, and this, or
    // a shadow-including ancestor of this, has a computed opacity value of 0,
    // return false.
    return false;
  }

  if ((aOptions.mVisibilityProperty || aOptions.mCheckVisibilityCSS) &&
      !f->StyleVisibility()->IsVisible()) {
    // 4. If the checkVisibilityCSS dictionary member of options is true, and
    // this is invisible, return false.
    return false;
  }

  // 6. Return true
  return true;
}

void Element::ScrollIntoView(const BooleanOrScrollIntoViewOptions& aObject) {
  if (aObject.IsScrollIntoViewOptions()) {
    return ScrollIntoView(aObject.GetAsScrollIntoViewOptions());
  }

  MOZ_DIAGNOSTIC_ASSERT(aObject.IsBoolean());

  ScrollIntoViewOptions options;
  if (aObject.GetAsBoolean()) {
    options.mBlock = ScrollLogicalPosition::Start;
    options.mInline = ScrollLogicalPosition::Nearest;
  } else {
    options.mBlock = ScrollLogicalPosition::End;
    options.mInline = ScrollLogicalPosition::Nearest;
  }
  return ScrollIntoView(options);
}

void Element::ScrollIntoView(const ScrollIntoViewOptions& aOptions) {
  Document* document = GetComposedDoc();
  if (!document) {
    return;
  }

  // Get the presentation shell
  RefPtr<PresShell> presShell = document->GetPresShell();
  if (!presShell) {
    return;
  }

  const auto ToWhereToScroll =
      [](ScrollLogicalPosition aPosition) -> WhereToScroll {
    switch (aPosition) {
      case ScrollLogicalPosition::Start:
        return WhereToScroll::Start;
      case ScrollLogicalPosition::Center:
        return WhereToScroll::Center;
      case ScrollLogicalPosition::End:
        return WhereToScroll::End;
      case ScrollLogicalPosition::Nearest:
        break;
    }
    return WhereToScroll::Nearest;
  };

  const auto block = ToWhereToScroll(aOptions.mBlock);
  const auto inline_ = ToWhereToScroll(aOptions.mInline);

  ScrollFlags scrollFlags = ScrollFlags::ScrollOverflowHidden |
                            ScrollFlags::TriggeredByScript |
                            ScrollFlags::AxesAreLogical;
  if (aOptions.mBehavior == ScrollBehavior::Smooth) {
    scrollFlags |= ScrollFlags::ScrollSmooth;
  } else if (aOptions.mBehavior == ScrollBehavior::Auto) {
    scrollFlags |= ScrollFlags::ScrollSmoothAuto;
  }

  presShell->ScrollContentIntoView(
      this, ScrollAxis(block, WhenToScroll::Always),
      ScrollAxis(inline_, WhenToScroll::Always), scrollFlags);
}

void Element::ScrollTo(double aXScroll, double aYScroll) {
  ScrollToOptions options;
  options.mLeft.Construct(aXScroll);
  options.mTop.Construct(aYScroll);
  ScrollTo(options);
}

void Element::ScrollTo(const ScrollToOptions& aOptions) {
  // When the scroll top is 0, we don't need to flush layout to scroll to that
  // point; we know 0 is always in range.  At least we think so...  But we do
  // need to flush frames so we ensure we find the right scrollable frame if
  // there is one. If it's nonzero, we need to flush layout because we need to
  // figure out what our real scrollTopMax is.
  //
  // If we have a left value, we can't assume things based on it's value,
  // depending on our direction and layout 0 may or may not be in our scroll
  // range.  So we need to flush layout no matter what then.
  const bool needsLayoutFlush =
      aOptions.mLeft.WasPassed() ||
      (aOptions.mTop.WasPassed() && aOptions.mTop.Value() != 0.0);

  nsIFrame* frame;
  ScrollContainerFrame* sf = GetScrollContainerFrame(
      &frame, needsLayoutFlush ? FlushType::Layout : FlushType::Frames);
  if (!sf) {
    return;
  }
  CSSIntPoint scrollPos = sf->GetRoundedScrollPositionCSSPixels();
  if (aOptions.mLeft.WasPassed()) {
    scrollPos.x = int32_t(mozilla::ToZeroIfNonfinite(
        frame->Style()->EffectiveZoom().Zoom(aOptions.mLeft.Value())));
  }
  if (aOptions.mTop.WasPassed()) {
    scrollPos.y = int32_t(mozilla::ToZeroIfNonfinite(
        frame->Style()->EffectiveZoom().Zoom(aOptions.mTop.Value())));
  }
  ScrollMode scrollMode = sf->IsSmoothScroll(aOptions.mBehavior)
                              ? ScrollMode::SmoothMsd
                              : ScrollMode::Instant;
  sf->ScrollToCSSPixels(scrollPos, scrollMode);
}

void Element::ScrollBy(double aXScrollDif, double aYScrollDif) {
  ScrollToOptions options;
  options.mLeft.Construct(aXScrollDif);
  options.mTop.Construct(aYScrollDif);
  ScrollBy(options);
}

void Element::ScrollBy(const ScrollToOptions& aOptions) {
  nsIFrame* frame;
  ScrollContainerFrame* sf = GetScrollContainerFrame(&frame);
  if (!sf) {
    return;
  }

  CSSIntPoint scrollDelta;
  if (aOptions.mLeft.WasPassed()) {
    scrollDelta.x = int32_t(mozilla::ToZeroIfNonfinite(
        frame->Style()->EffectiveZoom().Zoom(aOptions.mLeft.Value())));
  }

  if (aOptions.mTop.WasPassed()) {
    scrollDelta.y = int32_t(mozilla::ToZeroIfNonfinite(
        frame->Style()->EffectiveZoom().Zoom(aOptions.mTop.Value())));
  }

  auto scrollMode = sf->IsSmoothScroll(aOptions.mBehavior)
                        ? ScrollMode::SmoothMsd
                        : ScrollMode::Instant;
  sf->ScrollByCSSPixels(scrollDelta, scrollMode);
}

int32_t Element::ScrollTop() {
  return CSSPixel::FromAppUnitsRounded(GetScrollOrigin().y);
}

void Element::SetScrollTop(int32_t aScrollTop) {
  ScrollToOptions options;
  options.mTop.Construct(aScrollTop);
  ScrollTo(options);
}

int32_t Element::ScrollLeft() {
  return CSSPixel::FromAppUnitsRounded(GetScrollOrigin().x);
}

void Element::SetScrollLeft(int32_t aScrollLeft) {
  ScrollToOptions options;
  options.mLeft.Construct(aScrollLeft);
  ScrollTo(options);
}

void Element::MozScrollSnap() {
  if (ScrollContainerFrame* sf =
          GetScrollContainerFrame(nullptr, FlushType::None)) {
    sf->ScrollSnap();
  }
}

nsRect Element::GetScrollRange() {
  nsIFrame* frame;
  ScrollContainerFrame* sf = GetScrollContainerFrame(&frame);
  if (!sf) {
    return nsRect();
  }
  return frame->Style()->EffectiveZoom().Unzoom(sf->GetScrollRange());
}

int32_t Element::ScrollTopMin() {
  return CSSPixel::FromAppUnitsRounded(GetScrollRange().Y());
}

int32_t Element::ScrollTopMax() {
  return CSSPixel::FromAppUnitsRounded(GetScrollRange().YMost());
}

int32_t Element::ScrollLeftMin() {
  return CSSPixel::FromAppUnitsRounded(GetScrollRange().X());
}

int32_t Element::ScrollLeftMax() {
  return CSSPixel::FromAppUnitsRounded(GetScrollRange().XMost());
}

static nsSize GetScrollRectSizeForOverflowVisibleFrame(nsIFrame* aFrame) {
  if (!aFrame || aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    return nsSize();
  }

  // This matches WebKit and Blink, which in turn (apparently, according to
  // their source) matched old IE.
  const nsRect paddingRect = aFrame->GetPaddingRectRelativeToSelf();
  const nsRect overflowRect = [&] {
    OverflowAreas overflowAreas(paddingRect, paddingRect);
    // Add the scrollable overflow areas of children (if any) to the
    // paddingRect, as if aFrame was a scrolled frame. It's important to start
    // with the paddingRect, otherwise if there are no children the overflow
    // rect will be 0,0,0,0 which will force the point 0,0 to be included in the
    // final rect.
    aFrame->UnionChildOverflow(overflowAreas, /* aAsIfScrolled = */ true);
    // Make sure that an empty padding-rect's edges are included, by adding
    // the padding-rect in again with UnionEdges.
    return overflowAreas.ScrollableOverflow().UnionEdges(paddingRect);
  }();

  auto directions =
      ScrollContainerFrame::ComputePerAxisScrollDirections(aFrame);
  const nscoord height = directions.mToBottom
                             ? overflowRect.YMost() - paddingRect.Y()
                             : paddingRect.YMost() - overflowRect.Y();
  const nscoord width = directions.mToRight
                            ? overflowRect.XMost() - paddingRect.X()
                            : paddingRect.XMost() - overflowRect.X();
  return nsSize(width, height);
}

nsSize Element::GetScrollSize() {
  nsIFrame* frame;
  nsSize size;
  if (ScrollContainerFrame* sf = GetScrollContainerFrame(&frame)) {
    size = sf->GetScrollRange().Size() + sf->GetScrollPortRect().Size();
  } else {
    size = GetScrollRectSizeForOverflowVisibleFrame(frame);
  }
  if (!frame) {
    return size;
  }
  return frame->Style()->EffectiveZoom().Unzoom(size);
}

nsPoint Element::GetScrollOrigin() {
  nsIFrame* frame;
  ScrollContainerFrame* sf = GetScrollContainerFrame(&frame);
  if (!sf) {
    return nsPoint();
  }
  return frame->Style()->EffectiveZoom().Unzoom(sf->GetScrollPosition());
}

int32_t Element::ScrollHeight() {
  return nsPresContext::AppUnitsToIntCSSPixels(GetScrollSize().height);
}

int32_t Element::ScrollWidth() {
  return nsPresContext::AppUnitsToIntCSSPixels(GetScrollSize().width);
}

nsRect Element::GetClientAreaRect() {
  Document* doc = OwnerDoc();
  nsPresContext* presContext = doc->GetPresContext();

  // We can avoid a layout flush if this is the scrolling element of the
  // document, we have overlay scrollbars, and we aren't embedded in another
  // document
  if (presContext && presContext->UseOverlayScrollbars() &&
      !doc->StyleOrLayoutObservablyDependsOnParentDocumentLayout() &&
      doc->IsScrollingElement(this)) {
    if (PresShell* presShell = doc->GetPresShell()) {
      // Ensure up to date dimensions, but don't reflow
      if (RefPtr<nsViewManager> viewManager = presShell->GetViewManager()) {
        viewManager->FlushDelayedResize();
      }
      return nsRect(nsPoint(), presContext->GetVisibleArea().Size());
    }
  }

  nsIFrame* frame;
  if (ScrollContainerFrame* sf = GetScrollContainerFrame(&frame)) {
    nsRect scrollPort = sf->GetScrollPortRect();

    if (!sf->IsRootScrollFrameOfDocument()) {
      MOZ_ASSERT(frame);
      // We want the offset to be relative to `frame`, not `sf`... Except for
      // the root scroll frame, which is an ancestor of frame rather than a
      // descendant and thus this wouldn't particularly make sense.
      if (frame != sf) {
        scrollPort.MoveBy(sf->GetOffsetTo(frame));
      }
    }

    // The scroll port value might be expanded to the minimum scale size, we
    // should limit the size to the ICB in such cases.
    scrollPort.SizeTo(sf->GetLayoutSize());
    return frame->Style()->EffectiveZoom().Unzoom(scrollPort);
  }

  if (frame &&
      // The display check is OK even though we're not looking at the style
      // frame, because the style frame only differs from "frame" for tables,
      // and table wrappers have the same display as the table itself.
      (!frame->StyleDisplay()->IsInlineFlow() || frame->IsReplaced())) {
    // Special case code to make client area work even when there isn't
    // a scroll view, see bug 180552, bug 227567.
    return frame->Style()->EffectiveZoom().Unzoom(
        frame->GetPaddingRect() - frame->GetPositionIgnoringScrolling());
  }

  // SVG nodes reach here and just return 0
  return nsRect();
}

int32_t Element::ScreenX() {
  nsIFrame* frame = GetPrimaryFrame(FlushType::Layout);
  return frame ? frame->GetScreenRect().x : 0;
}

int32_t Element::ScreenY() {
  nsIFrame* frame = GetPrimaryFrame(FlushType::Layout);
  return frame ? frame->GetScreenRect().y : 0;
}

already_AddRefed<nsIScreen> Element::GetScreen() {
  // Flush layout to guarantee that frames are created if needed, and preserve
  // behavior.
  Unused << GetPrimaryFrame(FlushType::Frames);
  if (nsIWidget* widget = nsContentUtils::WidgetForContent(this)) {
    return widget->GetWidgetScreen();
  }
  return nullptr;
}

double Element::CurrentCSSZoom() {
  nsIFrame* f = GetPrimaryFrame(FlushType::Frames);
  if (!f) {
    return 1.0;
  }
  return f->Style()->EffectiveZoom().ToFloat();
}

already_AddRefed<DOMRect> Element::GetBoundingClientRect() {
  RefPtr<DOMRect> rect = new DOMRect(ToSupports(OwnerDoc()));

  nsIFrame* frame = GetPrimaryFrame(FlushType::Layout);
  if (!frame) {
    // display:none, perhaps? Return the empty rect
    return rect.forget();
  }

  rect->SetLayoutRect(frame->GetBoundingClientRect());
  return rect.forget();
}

already_AddRefed<DOMRectList> Element::GetClientRects() {
  RefPtr<DOMRectList> rectList = new DOMRectList(this);

  nsIFrame* frame = GetPrimaryFrame(FlushType::Layout);
  if (!frame) {
    // display:none, perhaps? Return an empty list
    return rectList.forget();
  }

  nsLayoutUtils::RectListBuilder builder(rectList);
  nsLayoutUtils::GetAllInFlowRects(
      frame, nsLayoutUtils::GetContainingBlockForClientRect(frame), &builder,
      nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms);
  return rectList.forget();
}

const DOMTokenListSupportedToken Element::sAnchorAndFormRelValues[] = {
    "noreferrer", "noopener", "opener", nullptr};

// https://html.spec.whatwg.org/multipage/urls-and-fetching.html#lazy-loading-attribute
static constexpr nsAttrValue::EnumTable kLoadingTable[] = {
    {"eager", Element::Loading::Eager},
    {"lazy", Element::Loading::Lazy},
    {nullptr, 0}};

void Element::GetLoading(nsAString& aValue) const {
  GetEnumAttr(nsGkAtoms::loading, kLoadingTable[0].tag, aValue);
}

bool Element::ParseLoadingAttribute(const nsAString& aValue,
                                    nsAttrValue& aResult) {
  return aResult.ParseEnumValue(aValue, kLoadingTable,
                                /* aCaseSensitive = */ false, kLoadingTable);
}

Element::Loading Element::LoadingState() const {
  const nsAttrValue* val = mAttrs.GetAttr(nsGkAtoms::loading);
  if (!val) {
    return Loading::Eager;
  }
  return static_cast<Loading>(val->GetEnumValue());
}

//----------------------------------------------------------------------

void Element::AddToIdTable(nsAtom* aId) {
  NS_ASSERTION(HasID(), "Node doesn't have an ID?");
  if (IsInShadowTree()) {
    ShadowRoot* containingShadow = GetContainingShadow();
    containingShadow->AddToIdTable(this, aId);
  } else {
    Document* doc = GetUncomposedDoc();
    if (doc && !IsInNativeAnonymousSubtree()) {
      doc->AddToIdTable(this, aId);
    }
  }
}

void Element::RemoveFromIdTable() {
  if (!HasID()) {
    return;
  }

  nsAtom* id = DoGetID();
  if (IsInShadowTree()) {
    ShadowRoot* containingShadow = GetContainingShadow();
    // Check for containingShadow because it may have
    // been deleted during unlinking.
    if (containingShadow) {
      containingShadow->RemoveFromIdTable(this, id);
    }
  } else {
    Document* doc = GetUncomposedDoc();
    if (doc && !IsInNativeAnonymousSubtree()) {
      doc->RemoveFromIdTable(this, id);
    }
  }
}

void Element::SetSlot(const nsAString& aName, ErrorResult& aError) {
  aError = SetAttr(kNameSpaceID_None, nsGkAtoms::slot, aName, true);
}

void Element::GetSlot(nsAString& aName) { GetAttr(nsGkAtoms::slot, aName); }

// https://dom.spec.whatwg.org/#dom-element-shadowroot
ShadowRoot* Element::GetShadowRootByMode() const {
  /**
   * 1. Let shadow be context object's shadow root.
   * 2. If shadow is null or its mode is "closed", then return null.
   */
  ShadowRoot* shadowRoot = GetShadowRoot();
  if (!shadowRoot || shadowRoot->IsClosed()) {
    return nullptr;
  }

  /**
   * 3. Return shadow.
   */
  return shadowRoot;
}

bool Element::CanAttachShadowDOM() const {
  /**
   * If context object's namespace is not the HTML namespace,
   * return false.
   *
   * Deviate from the spec here to allow shadow dom attachement to
   * XUL elements.
   */
  if (!IsHTMLElement() &&
      !(IsXULElement() &&
        nsContentUtils::AllowXULXBLForPrincipal(NodePrincipal()))) {
    return false;
  }

  /**
   * If context object's local name is not
   *    a valid custom element name, "article", "aside", "blockquote",
   *    "body", "div", "footer", "h1", "h2", "h3", "h4", "h5", "h6",
   *    "header", "main" "nav", "p", "section", "search", or "span",
   *  return false.
   */
  nsAtom* nameAtom = NodeInfo()->NameAtom();
  uint32_t namespaceID = NodeInfo()->NamespaceID();
  if (!(nsContentUtils::IsCustomElementName(nameAtom, namespaceID) ||
        nameAtom == nsGkAtoms::article || nameAtom == nsGkAtoms::aside ||
        nameAtom == nsGkAtoms::blockquote || nameAtom == nsGkAtoms::body ||
        nameAtom == nsGkAtoms::div || nameAtom == nsGkAtoms::footer ||
        nameAtom == nsGkAtoms::h1 || nameAtom == nsGkAtoms::h2 ||
        nameAtom == nsGkAtoms::h3 || nameAtom == nsGkAtoms::h4 ||
        nameAtom == nsGkAtoms::h5 || nameAtom == nsGkAtoms::h6 ||
        nameAtom == nsGkAtoms::header || nameAtom == nsGkAtoms::main ||
        nameAtom == nsGkAtoms::nav || nameAtom == nsGkAtoms::p ||
        nameAtom == nsGkAtoms::section || nameAtom == nsGkAtoms::search ||
        nameAtom == nsGkAtoms::span)) {
    return false;
  }

  /**
   * 3. If context object’s local name is a valid custom element name, or
   *    context object’s is value is not null, then:
   *    If definition is not null and definition’s disable shadow is true, then
   *    return false.
   */
  // It will always have CustomElementData when the element is a valid custom
  // element or has is value.
  if (CustomElementData* ceData = GetCustomElementData()) {
    CustomElementDefinition* definition = ceData->GetCustomElementDefinition();
    // If the definition is null, the element possible hasn't yet upgraded.
    // Fallback to use LookupCustomElementDefinition to find its definition.
    if (!definition) {
      definition = nsContentUtils::LookupCustomElementDefinition(
          NodeInfo()->GetDocument(), nameAtom, namespaceID,
          ceData->GetCustomElementType());
    }

    if (definition && definition->mDisableShadow) {
      return false;
    }
  }

  return true;
}

// https://dom.spec.whatwg.org/#dom-element-attachshadow
already_AddRefed<ShadowRoot> Element::AttachShadow(const ShadowRootInit& aInit,
                                                   ErrorResult& aError) {
  /**
   * Step 1, 2, and 3.
   */
  if (!CanAttachShadowDOM()) {
    aError.ThrowNotSupportedError("Unable to attach ShadowDOM");
    return nullptr;
  }

  /**
   * 4. If element is a shadow host, then:
   */
  if (RefPtr<ShadowRoot> root = GetShadowRoot()) {
    /**
     *  1. Let currentShadowRoot be element’s shadow root.
     *
     *  2. If any of the following are true:
     *      currentShadowRoot’s declarative is false; or
     *      currentShadowRoot’s mode is not mode,
     *  then throw a "NotSupportedError" DOMException.
     */
    if (!root->IsDeclarative() || root->Mode() != aInit.mMode) {
      aError.ThrowNotSupportedError(
          "Unable to re-attach to existing ShadowDOM");
      return nullptr;
    }
    /**
     * 3. Otherwise:
     *      1. Remove all of currentShadowRoot’s children, in tree order.
     *      2. Set currentShadowRoot’s declarative to false.
     *      3. Return.
     */
    root->ReplaceChildren(nullptr, aError);
    root->SetIsDeclarative(ShadowRootDeclarative::No);
    return root.forget();
  }

  if (StaticPrefs::dom_webcomponents_shadowdom_report_usage()) {
    OwnerDoc()->ReportShadowDOMUsage();
  }

  return AttachShadowWithoutNameChecks(
      aInit.mMode, DelegatesFocus(aInit.mDelegatesFocus), aInit.mSlotAssignment,
      ShadowRootClonable(aInit.mClonable),
      ShadowRootSerializable(aInit.mSerializable));
}

already_AddRefed<ShadowRoot> Element::AttachShadowWithoutNameChecks(
    ShadowRootMode aMode, DelegatesFocus aDelegatesFocus,
    SlotAssignmentMode aSlotAssignment, ShadowRootClonable aClonable,
    ShadowRootSerializable aSerializable) {
  nsAutoScriptBlocker scriptBlocker;

  auto* nim = mNodeInfo->NodeInfoManager();
  RefPtr<mozilla::dom::NodeInfo> nodeInfo =
      nim->GetNodeInfo(nsGkAtoms::documentFragmentNodeName, nullptr,
                       kNameSpaceID_None, DOCUMENT_FRAGMENT_NODE);

  // If there are no children, the flat tree is not changing due to the presence
  // of the shadow root, so we don't need to invalidate style / layout.
  //
  // This is a minor optimization, but also works around nasty stuff like
  // bug 1397876.
  if (Document* doc = GetComposedDoc()) {
    if (PresShell* presShell = doc->GetPresShell()) {
      presShell->ShadowRootWillBeAttached(*this);
    }
  }

  /**
   * 5. Let shadow be a new shadow root whose node document is
   *    context object's node document, host is context object,
   *    and mode is init's mode.
   */
  RefPtr<ShadowRoot> shadowRoot = new (nim)
      ShadowRoot(this, aMode, aDelegatesFocus, aSlotAssignment, aClonable,
                 aSerializable, ShadowRootDeclarative::No, nodeInfo.forget());

  if (NodeOrAncestorHasDirAuto()) {
    shadowRoot->SetAncestorHasDirAuto();
  }

  /**
   * 7. If this’s custom element state is "precustomized" or "custom", then set
   *    shadow’s available to element internals to true.
   */
  CustomElementData* ceData = GetCustomElementData();
  if (ceData && (ceData->mState == CustomElementData::State::ePrecustomized ||
                 ceData->mState == CustomElementData::State::eCustom)) {
    shadowRoot->SetAvailableToElementInternals();
  }

  /**
   * 9. Set context object's shadow root to shadow.
   */
  SetShadowRoot(shadowRoot);

  // Dispatch a "shadowrootattached" event for devtools if needed.
  if (MOZ_UNLIKELY(
          nim->GetDocument()->DevToolsAnonymousAndShadowEventsEnabled())) {
    AsyncEventDispatcher* dispatcher = new AsyncEventDispatcher(
        this, u"shadowrootattached"_ns, CanBubble::eYes,
        ChromeOnlyDispatch::eYes, Composed::eYes);
    dispatcher->PostDOMEvent();
  }

  const LinkedList<AbstractRange>* ranges =
      GetExistingClosestCommonInclusiveAncestorRanges();
  if (ranges) {
    for (const AbstractRange* range : *ranges) {
      if (range->MayCrossShadowBoundary()) {
        MOZ_ASSERT(range->IsDynamicRange());
        CrossShadowBoundaryRange* crossBoundaryRange =
            range->AsDynamicRange()->GetCrossShadowBoundaryRange();
        MOZ_ASSERT(crossBoundaryRange);
        // We may have previously selected this node before it
        // becomes a shadow host, so we need to reset the values
        // in RangeBoundaries to accommodate the change.
        crossBoundaryRange->NotifyNodeBecomesShadowHost(this);
      }
    }
  }
  /**
   * 10. Return shadow.
   */
  return shadowRoot.forget();
}

void Element::AttachAndSetUAShadowRoot(NotifyUAWidgetSetup aNotify,
                                       DelegatesFocus aDelegatesFocus) {
  MOZ_DIAGNOSTIC_ASSERT(!CanAttachShadowDOM(),
                        "Cannot be used to attach UI shadow DOM");
  if (OwnerDoc()->IsStaticDocument()) {
    return;
  }

  if (!GetShadowRoot()) {
    RefPtr<ShadowRoot> shadowRoot =
        AttachShadowWithoutNameChecks(ShadowRootMode::Closed, aDelegatesFocus);
    shadowRoot->SetIsUAWidget();
  }

  MOZ_ASSERT(GetShadowRoot()->IsUAWidget());
  if (aNotify == NotifyUAWidgetSetup::Yes) {
    NotifyUAWidgetSetupOrChange();
  }
}

void Element::NotifyUAWidgetSetupOrChange() {
  MOZ_ASSERT(IsInComposedDoc());
  Document* doc = OwnerDoc();
  if (doc->IsStaticDocument()) {
    return;
  }

  // Schedule a runnable, ensure the event dispatches before
  // returning to content script.
  // This event cause UA Widget to construct or cause onchange callback
  // of existing UA Widget to run; dispatching this event twice should not cause
  // UA Widget to re-init.
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "Element::NotifyUAWidgetSetupOrChange::UAWidgetSetupOrChange",
      [self = RefPtr<Element>(this), doc = RefPtr<Document>(doc)]() {
        nsContentUtils::DispatchChromeEvent(doc, self,
                                            u"UAWidgetSetupOrChange"_ns,
                                            CanBubble::eYes, Cancelable::eNo);
      }));
}

void Element::NotifyUAWidgetTeardown(UnattachShadowRoot aUnattachShadowRoot) {
  MOZ_ASSERT(IsInComposedDoc());
  if (!GetShadowRoot()) {
    return;
  }
  MOZ_ASSERT(GetShadowRoot()->IsUAWidget());
  if (aUnattachShadowRoot == UnattachShadowRoot::Yes) {
    UnattachShadow();
  }

  Document* doc = OwnerDoc();
  if (doc->IsStaticDocument()) {
    return;
  }

  // The runnable will dispatch an event to tear down UA Widget.
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "Element::NotifyUAWidgetTeardownAndUnattachShadow::UAWidgetTeardown",
      [self = RefPtr<Element>(this), doc = RefPtr<Document>(doc)]() {
        // Bail out if the element is being collected by CC
        bool hasHadScriptObject = true;
        nsIScriptGlobalObject* scriptObject =
            doc->GetScriptHandlingObject(hasHadScriptObject);
        if (!scriptObject && hasHadScriptObject) {
          return;
        }

        Unused << nsContentUtils::DispatchChromeEvent(
            doc, self, u"UAWidgetTeardown"_ns, CanBubble::eYes,
            Cancelable::eNo);
      }));
}

void Element::UnattachShadow() {
  RefPtr<ShadowRoot> shadowRoot = GetShadowRoot();
  if (!shadowRoot) {
    return;
  }

  nsAutoScriptBlocker scriptBlocker;

  if (RefPtr<Document> doc = GetComposedDoc()) {
    if (PresShell* presShell = doc->GetPresShell()) {
      presShell->DestroyFramesForAndRestyle(this);
#ifdef ACCESSIBILITY
      // We need to notify the accessibility service here explicitly because,
      // even though we're going to reconstruct the _host_, the shadow root and
      // its children are never really going to come back. We could plumb that
      // further down to DestroyFramesForAndRestyle and add a new flag to
      // nsCSSFrameConstructor::ContentRemoved or such, but this seems simpler
      // instead.
      if (nsAccessibilityService* accService = GetAccService()) {
        accService->ContentRemoved(presShell, shadowRoot);
      }
#endif
    }
    // ContentRemoved doesn't really run script in the cases we care about (it
    // can only call ClearFocus when removing iframes and so on...)
    [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY {
      if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
        fm->ContentRemoved(doc, shadowRoot);
      }
    }();
  }
  MOZ_ASSERT(!GetPrimaryFrame());

  shadowRoot->Unattach();
  SetShadowRoot(nullptr);
}

void Element::GetAttribute(const nsAString& aName, DOMString& aReturn) {
  const nsAttrValue* val = mAttrs.GetAttr(
      aName,
      IsHTMLElement() && IsInHTMLDocument() ? eIgnoreCase : eCaseMatters);
  if (val) {
    val->ToString(aReturn);
  } else {
    aReturn.SetNull();
  }
}

bool Element::ToggleAttribute(const nsAString& aName,
                              const Optional<bool>& aForce,
                              nsIPrincipal* aTriggeringPrincipal,
                              ErrorResult& aError) {
  aError = nsContentUtils::CheckQName(aName, false);
  if (aError.Failed()) {
    return false;
  }

  nsAutoString nameToUse;
  const nsAttrName* name = InternalGetAttrNameFromQName(aName, &nameToUse);
  if (!name) {
    if (aForce.WasPassed() && !aForce.Value()) {
      return false;
    }
    RefPtr<nsAtom> nameAtom = NS_AtomizeMainThread(nameToUse);
    if (!nameAtom) {
      aError.Throw(NS_ERROR_OUT_OF_MEMORY);
      return false;
    }
    aError = SetAttr(kNameSpaceID_None, nameAtom, u""_ns, aTriggeringPrincipal,
                     true);
    return true;
  }
  if (aForce.WasPassed() && aForce.Value()) {
    return true;
  }
  // Hold a strong reference here so that the atom or nodeinfo doesn't go
  // away during UnsetAttr. If it did UnsetAttr would be left with a
  // dangling pointer as argument without knowing it.
  nsAttrName tmp(*name);

  aError = UnsetAttr(name->NamespaceID(), name->LocalName(), true);
  return false;
}

void Element::SetAttribute(const nsAString& aName, const nsAString& aValue,
                           nsIPrincipal* aTriggeringPrincipal,
                           ErrorResult& aError) {
  aError = nsContentUtils::CheckQName(aName, false);
  if (aError.Failed()) {
    return;
  }

  nsAutoString nameToUse;
  const nsAttrName* name = InternalGetAttrNameFromQName(aName, &nameToUse);
  if (!name) {
    RefPtr<nsAtom> nameAtom = NS_AtomizeMainThread(nameToUse);
    if (!nameAtom) {
      aError.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    aError = SetAttr(kNameSpaceID_None, nameAtom, aValue, aTriggeringPrincipal,
                     true);
    return;
  }

  aError = SetAttr(name->NamespaceID(), name->LocalName(), name->GetPrefix(),
                   aValue, aTriggeringPrincipal, true);
}

void Element::RemoveAttribute(const nsAString& aName, ErrorResult& aError) {
  const nsAttrName* name = InternalGetAttrNameFromQName(aName);

  if (!name) {
    // If there is no canonical nsAttrName for this attribute name, then the
    // attribute does not exist and we can't get its namespace ID and
    // local name below, so we return early.
    return;
  }

  // Hold a strong reference here so that the atom or nodeinfo doesn't go
  // away during UnsetAttr. If it did UnsetAttr would be left with a
  // dangling pointer as argument without knowing it.
  nsAttrName tmp(*name);

  aError = UnsetAttr(name->NamespaceID(), name->LocalName(), true);
}

Attr* Element::GetAttributeNode(const nsAString& aName) {
  return Attributes()->GetNamedItem(aName);
}

already_AddRefed<Attr> Element::SetAttributeNode(Attr& aNewAttr,
                                                 ErrorResult& aError) {
  RefPtr<nsDOMAttributeMap> attrMap = Attributes();
  return attrMap->SetNamedItemNS(aNewAttr, aError);
}

already_AddRefed<Attr> Element::RemoveAttributeNode(Attr& aAttribute,
                                                    ErrorResult& aError) {
  Element* elem = aAttribute.GetElement();
  if (elem != this) {
    aError.Throw(NS_ERROR_DOM_NOT_FOUND_ERR);
    return nullptr;
  }

  nsAutoString nameSpaceURI;
  aAttribute.NodeInfo()->GetNamespaceURI(nameSpaceURI);
  return Attributes()->RemoveNamedItemNS(
      nameSpaceURI, aAttribute.NodeInfo()->LocalName(), aError);
}

void Element::GetAttributeNS(const nsAString& aNamespaceURI,
                             const nsAString& aLocalName, nsAString& aReturn) {
  int32_t nsid = nsNameSpaceManager::GetInstance()->GetNameSpaceID(
      aNamespaceURI, nsContentUtils::IsChromeDoc(OwnerDoc()));

  if (nsid == kNameSpaceID_Unknown) {
    // Unknown namespace means no attribute.
    SetDOMStringToNull(aReturn);
    return;
  }

  RefPtr<nsAtom> name = NS_AtomizeMainThread(aLocalName);
  bool hasAttr = GetAttr(nsid, name, aReturn);
  if (!hasAttr) {
    SetDOMStringToNull(aReturn);
  }
}

void Element::SetAttributeNS(const nsAString& aNamespaceURI,
                             const nsAString& aQualifiedName,
                             const nsAString& aValue,
                             nsIPrincipal* aTriggeringPrincipal,
                             ErrorResult& aError) {
  RefPtr<mozilla::dom::NodeInfo> ni;
  aError = nsContentUtils::GetNodeInfoFromQName(
      aNamespaceURI, aQualifiedName, mNodeInfo->NodeInfoManager(),
      ATTRIBUTE_NODE, getter_AddRefs(ni));
  if (aError.Failed()) {
    return;
  }

  aError = SetAttr(ni->NamespaceID(), ni->NameAtom(), ni->GetPrefixAtom(),
                   aValue, aTriggeringPrincipal, true);
}

already_AddRefed<nsIPrincipal> Element::CreateDevtoolsPrincipal() {
  // Return an ExpandedPrincipal that subsumes this Element's Principal,
  // and expands this Element's CSP to allow the actions that devtools
  // needs to perform.
  AutoTArray<nsCOMPtr<nsIPrincipal>, 1> allowList = {NodePrincipal()};
  RefPtr<ExpandedPrincipal> dtPrincipal = ExpandedPrincipal::Create(
      allowList, NodePrincipal()->OriginAttributesRef());

  if (nsIContentSecurityPolicy* csp = GetCsp()) {
    RefPtr<nsCSPContext> dtCsp = new nsCSPContext();
    dtCsp->InitFromOther(static_cast<nsCSPContext*>(csp));
    dtCsp->SetSkipAllowInlineStyleCheck(true);

    dtPrincipal->SetCsp(dtCsp);
  }

  return dtPrincipal.forget();
}

void Element::SetAttribute(
    const nsAString& aName,
    const TrustedHTMLOrTrustedScriptOrTrustedScriptURLOrString& aValue,
    nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError) {
  aError = nsContentUtils::CheckQName(aName, false);
  if (aError.Failed()) {
    return;
  }

  nsAutoString nameToUse;
  const nsAttrName* name = InternalGetAttrNameFromQName(aName, &nameToUse);
  if (!name) {
    RefPtr<nsAtom> nameAtom = NS_AtomizeMainThread(nameToUse);
    Maybe<nsAutoString> compliantStringHolder;
    const nsAString* compliantString =
        TrustedTypeUtils::GetTrustedTypesCompliantAttributeValue(
            *this, nameAtom, kNameSpaceID_None, aValue, compliantStringHolder,
            aError);
    if (aError.Failed()) {
      return;
    }
    aError = SetAttr(kNameSpaceID_None, nameAtom, *compliantString,
                     aTriggeringPrincipal, true);
    return;
  }

  Maybe<nsAutoString> compliantStringHolder;
  RefPtr<nsAtom> attributeName = name->LocalName();
  nsMutationGuard guard;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantAttributeValue(
          *this, attributeName, name->NamespaceID(), aValue,
          compliantStringHolder, aError);
  if (aError.Failed()) {
    return;
  }
  if (!guard.Mutated(0)) {
    aError = SetAttr(name->NamespaceID(), name->LocalName(), name->GetPrefix(),
                     *compliantString, aTriggeringPrincipal, true);
    return;
  }

  // GetTrustedTypesCompliantAttributeValue may have modified mAttrs and made
  // the result of InternalGetAttrNameFromQName above invalid. It may now return
  // a different value, perhaps a nullptr. To be safe, just call the version of
  // Element::SetAttribute accepting a string value.
  SetAttribute(aName, *compliantString, aTriggeringPrincipal, aError);
}

void Element::SetAttributeNS(
    const nsAString& aNamespaceURI, const nsAString& aQualifiedName,
    const TrustedHTMLOrTrustedScriptOrTrustedScriptURLOrString& aValue,
    nsIPrincipal* aTriggeringPrincipal, ErrorResult& aError) {
  RefPtr<mozilla::dom::NodeInfo> ni;
  aError = nsContentUtils::GetNodeInfoFromQName(
      aNamespaceURI, aQualifiedName, mNodeInfo->NodeInfoManager(),
      ATTRIBUTE_NODE, getter_AddRefs(ni));
  if (aError.Failed()) {
    return;
  }

  Maybe<nsAutoString> compliantStringHolder;
  RefPtr<nsAtom> attributeName = ni->NameAtom();
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantAttributeValue(
          *this, attributeName, ni->NamespaceID(), aValue,
          compliantStringHolder, aError);
  if (aError.Failed()) {
    return;
  }
  aError = SetAttr(ni->NamespaceID(), ni->NameAtom(), ni->GetPrefixAtom(),
                   *compliantString, aTriggeringPrincipal, true);
}

void Element::SetAttributeDevtools(const nsAString& aName,
                                   const nsAString& aValue,
                                   ErrorResult& aError) {
  // Run this through SetAttribute with a devtools-ready principal.
  RefPtr<nsIPrincipal> dtPrincipal = CreateDevtoolsPrincipal();
  SetAttribute(aName, aValue, dtPrincipal, aError);
}

void Element::SetAttributeDevtoolsNS(const nsAString& aNamespaceURI,
                                     const nsAString& aLocalName,
                                     const nsAString& aValue,
                                     ErrorResult& aError) {
  // Run this through SetAttributeNS with a devtools-ready principal.
  RefPtr<nsIPrincipal> dtPrincipal = CreateDevtoolsPrincipal();
  SetAttributeNS(aNamespaceURI, aLocalName, aValue, dtPrincipal, aError);
}

void Element::RemoveAttributeNS(const nsAString& aNamespaceURI,
                                const nsAString& aLocalName,
                                ErrorResult& aError) {
  RefPtr<nsAtom> name = NS_AtomizeMainThread(aLocalName);
  int32_t nsid = nsNameSpaceManager::GetInstance()->GetNameSpaceID(
      aNamespaceURI, nsContentUtils::IsChromeDoc(OwnerDoc()));

  if (nsid == kNameSpaceID_Unknown) {
    // If the namespace ID is unknown, it means there can't possibly be an
    // existing attribute. We would need a known namespace ID to pass into
    // UnsetAttr, so we return early if we don't have one.
    return;
  }

  aError = UnsetAttr(nsid, name, true);
}

Attr* Element::GetAttributeNodeNS(const nsAString& aNamespaceURI,
                                  const nsAString& aLocalName) {
  return GetAttributeNodeNSInternal(aNamespaceURI, aLocalName);
}

Attr* Element::GetAttributeNodeNSInternal(const nsAString& aNamespaceURI,
                                          const nsAString& aLocalName) {
  return Attributes()->GetNamedItemNS(aNamespaceURI, aLocalName);
}

already_AddRefed<Attr> Element::SetAttributeNodeNS(Attr& aNewAttr,
                                                   ErrorResult& aError) {
  RefPtr<nsDOMAttributeMap> attrMap = Attributes();
  return attrMap->SetNamedItemNS(aNewAttr, aError);
}

already_AddRefed<nsIHTMLCollection> Element::GetElementsByTagNameNS(
    const nsAString& aNamespaceURI, const nsAString& aLocalName,
    ErrorResult& aError) {
  int32_t nameSpaceId = kNameSpaceID_Wildcard;

  if (!aNamespaceURI.EqualsLiteral("*")) {
    aError = nsNameSpaceManager::GetInstance()->RegisterNameSpace(aNamespaceURI,
                                                                  nameSpaceId);
    if (aError.Failed()) {
      return nullptr;
    }
  }

  NS_ASSERTION(nameSpaceId != kNameSpaceID_Unknown, "Unexpected namespace ID!");

  return NS_GetContentList(this, nameSpaceId, aLocalName);
}

bool Element::HasAttributeNS(const nsAString& aNamespaceURI,
                             const nsAString& aLocalName) const {
  int32_t nsid = nsNameSpaceManager::GetInstance()->GetNameSpaceID(
      aNamespaceURI, nsContentUtils::IsChromeDoc(OwnerDoc()));

  if (nsid == kNameSpaceID_Unknown) {
    // Unknown namespace means no attr...
    return false;
  }

  RefPtr<nsAtom> name = NS_AtomizeMainThread(aLocalName);
  return HasAttr(nsid, name);
}

already_AddRefed<nsIHTMLCollection> Element::GetElementsByClassName(
    const nsAString& aClassNames) {
  return nsContentUtils::GetElementsByClassName(this, aClassNames);
}

bool Element::HasSharedRoot(const Element* aElement) const {
  nsINode* root = SubtreeRoot();
  nsINode* attrSubtreeRoot = aElement->SubtreeRoot();
  do {
    if (root == attrSubtreeRoot) {
      return true;
    }
    auto* shadow = ShadowRoot::FromNode(root);
    if (!shadow || !shadow->GetHost()) {
      break;
    }
    root = shadow->GetHost()->SubtreeRoot();
  } while (true);
  return false;
}

Element* Element::GetElementByIdInDocOrSubtree(nsAtom* aID) const {
  if (auto* docOrShadowRoot = GetContainingDocumentOrShadowRoot()) {
    return docOrShadowRoot->GetElementById(aID);
  }

  return nsContentUtils::MatchElementId(SubtreeRoot()->AsContent(), aID);
}

Element* Element::GetAttrAssociatedElement(nsAtom* aAttr) const {
  if (const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots()) {
    nsWeakPtr weakAttrEl = slots->mExplicitlySetAttrElementMap.Get(aAttr);
    if (nsCOMPtr<Element> attrEl = do_QueryReferent(weakAttrEl)) {
      // If reflectedTarget's explicitly set attr-element |attrEl| is
      // a descendant of any of element's shadow-including ancestors, then
      // return |atrEl|.
      if (HasSharedRoot(attrEl)) {
        return attrEl;
      }
      return nullptr;
    }
  }

  const nsAttrValue* value = GetParsedAttr(aAttr);
  if (!value) {
    return nullptr;
  }

  MOZ_ASSERT(value->Type() == nsAttrValue::eAtom,
             "Attribute used for attr associated element must be parsed");

  return GetElementByIdInDocOrSubtree(value->GetAtomValue());
}

void Element::GetAttrAssociatedElements(
    nsAtom* aAttr, bool* aUseCachedValue,
    Nullable<nsTArray<RefPtr<Element>>>& aElements) {
  MOZ_ASSERT(aElements.IsNull());

  auto& [explicitlySetAttrElements, cachedAttrElements] =
      ExtendedDOMSlots()->mAttrElementsMap.LookupOrInsert(aAttr);

  // https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#attr-associated-elements
  auto getAttrAssociatedElements =
      [&, &explicitlySetAttrElements =
              explicitlySetAttrElements]() -> Maybe<nsTArray<RefPtr<Element>>> {
    nsTArray<RefPtr<Element>> elements;

    if (explicitlySetAttrElements) {
      // 3. If reflectedTarget's explicitly set attr-elements is not null
      for (const nsWeakPtr& weakEl : *explicitlySetAttrElements) {
        // For each attrElement in reflectedTarget's explicitly set
        // attr-elements:
        if (nsCOMPtr<Element> attrEl = do_QueryReferent(weakEl)) {
          // If attrElement is not a descendant of any of element's
          // shadow-including ancestors, then continue.
          if (!HasSharedRoot(attrEl)) {
            continue;
          }
          // Append attrElement to elements.
          elements.AppendElement(attrEl);
        }
      }
    } else {
      // 4. Otherwise
      //   1. Let contentAttributeValue be the result of running
      //   reflectedTarget's get the content attribute.
      const nsAttrValue* value = GetParsedAttr(aAttr);
      //   2. If contentAttributeValue is null, then return null.
      if (!value) {
        return Nothing();
      }

      //   3. Let tokens be contentAttributeValue, split on ASCII whitespace.
      MOZ_ASSERT(value->Type() == nsAttrValue::eAtomArray ||
                     value->Type() == nsAttrValue::eAtom,
                 "Attribute used for attr associated elements must be parsed");
      for (uint32_t i = 0; i < value->GetAtomCount(); i++) {
        // For each id of tokens:
        if (auto* candidate = GetElementByIdInDocOrSubtree(
                value->AtomAt(static_cast<int32_t>(i)))) {
          // Append candidate to elements.
          elements.AppendElement(candidate);
        }
      }
    }

    return Some(std::move(elements));
  };

  // getter steps:
  // 1. Let elements be the result of running this's get the attr-associated
  // elements.
  auto elements = getAttrAssociatedElements();

  if (elements && elements == cachedAttrElements) {
    // 2. If the contents of elements is equal to the contents of this's cached
    // attr-associated elements, then return this's cached attr-associated
    // elements object.
    MOZ_ASSERT(!*aUseCachedValue);
    *aUseCachedValue = true;
    return;
  }

  // 3. Let elementsAsFrozenArray be elements, converted to a FrozenArray<T>?.
  //    (the binding code takes aElements and returns it as a FrozenArray)
  // 5. Set this's cached attr-associated elements object to
  //    elementsAsFrozenArray.
  //    (the binding code stores the attr-associated elements object in a slot)
  // 6. Return elementsAsFrozenArray.
  if (elements) {
    aElements.SetValue(elements->Clone());
  }

  // 4. Set this's cached attr-associated elements to elements.
  cachedAttrElements = std::move(elements);
}

void Element::ClearExplicitlySetAttrElement(nsAtom* aAttr) {
  if (auto* slots = GetExistingExtendedDOMSlots()) {
    slots->mExplicitlySetAttrElementMap.Remove(aAttr);
  }
}

void Element::ClearExplicitlySetAttrElements(nsAtom* aAttr) {
  if (auto* slots = GetExistingExtendedDOMSlots()) {
    slots->mAttrElementsMap.Remove(aAttr);
  }
}

void Element::ExplicitlySetAttrElement(nsAtom* aAttr, Element* aElement) {
#ifdef ACCESSIBILITY
  nsAccessibilityService* accService = GetAccService();
#endif
  // Accessibility requires that no other attribute changes occur between
  // AttrElementWillChange and AttrElementChanged. Scripts could cause
  // this, so don't let them run here. We do this even if accessibility isn't
  // running so that the JS behavior is consistent regardless of accessibility.
  // Otherwise, JS might be able to use this difference to determine whether
  // accessibility is running, which would be a privacy concern.
  nsAutoScriptBlocker scriptBlocker;
  if (aElement) {
#ifdef ACCESSIBILITY
    if (accService) {
      accService->NotifyAttrElementWillChange(this, aAttr);
    }
#endif
    SetAttr(aAttr, EmptyString(), IgnoreErrors());
    nsExtendedDOMSlots* slots = ExtendedDOMSlots();
    slots->mExplicitlySetAttrElementMap.InsertOrUpdate(
        aAttr, do_GetWeakReference(aElement));
#ifdef ACCESSIBILITY
    if (accService) {
      accService->NotifyAttrElementChanged(this, aAttr);
    }
#endif
    return;
  }

#ifdef ACCESSIBILITY
  if (accService) {
    accService->NotifyAttrElementWillChange(this, aAttr);
  }
#endif
  ClearExplicitlySetAttrElement(aAttr);
  UnsetAttr(aAttr, IgnoreErrors());
#ifdef ACCESSIBILITY
  if (accService) {
    accService->NotifyAttrElementChanged(this, aAttr);
  }
#endif
}

void Element::ExplicitlySetAttrElements(
    nsAtom* aAttr,
    const Nullable<Sequence<OwningNonNull<Element>>>& aElements) {
#ifdef ACCESSIBILITY
  nsAccessibilityService* accService = GetAccService();
#endif
  // Accessibility requires that no other attribute changes occur between
  // AttrElementWillChange and AttrElementChanged. Scripts could cause
  // this, so don't let them run here. We do this even if accessibility isn't
  // running so that the JS behavior is consistent regardless of accessibility.
  // Otherwise, JS might be able to use this difference to determine whether
  // accessibility is running, which would be a privacy concern.
  nsAutoScriptBlocker scriptBlocker;

#ifdef ACCESSIBILITY
  if (accService) {
    accService->NotifyAttrElementWillChange(this, aAttr);
  }
#endif

  if (aElements.IsNull()) {
    ClearExplicitlySetAttrElements(aAttr);
    UnsetAttr(aAttr, IgnoreErrors());
  } else {
    SetAttr(aAttr, EmptyString(), IgnoreErrors());
    auto& entry = ExtendedDOMSlots()->mAttrElementsMap.LookupOrInsert(aAttr);
    entry.first.emplace(nsTArray<nsWeakPtr>());
    for (Element* el : aElements.Value()) {
      entry.first->AppendElement(do_GetWeakReference(el));
    }
  }

#ifdef ACCESSIBILITY
  if (accService) {
    accService->NotifyAttrElementChanged(this, aAttr);
  }
#endif
}

Element* Element::GetExplicitlySetAttrElement(nsAtom* aAttr) const {
  if (const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots()) {
    nsWeakPtr weakAttrEl = slots->mExplicitlySetAttrElementMap.Get(aAttr);
    if (nsCOMPtr<Element> attrEl = do_QueryReferent(weakAttrEl)) {
      return attrEl;
    }
  }
  return nullptr;
}

void Element::GetExplicitlySetAttrElements(
    nsAtom* aAttr, nsTArray<Element*>& aElements) const {
  if (const nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots()) {
    if (auto attrElementsMaybeEntry = slots->mAttrElementsMap.Lookup(aAttr)) {
      auto& [attrElements, cachedAttrElements] = attrElementsMaybeEntry.Data();
      if (attrElements) {
        for (const nsWeakPtr& weakEl : *attrElements) {
          if (nsCOMPtr<Element> attrEl = do_QueryReferent(weakEl)) {
            aElements.AppendElement(attrEl);
          }
        }
      }
    }
  }
}

void Element::GetElementsWithGrid(nsTArray<RefPtr<Element>>& aElements) {
  nsINode* cur = this;
  while (cur) {
    if (cur->IsElement()) {
      Element* elem = cur->AsElement();

      if (elem->GetPrimaryFrame()) {
        // See if this has a GridContainerFrame. Use the same method that
        // nsGridContainerFrame uses, which deals with some edge cases.
        if (nsGridContainerFrame::GetGridContainerFrame(
                elem->GetPrimaryFrame())) {
          aElements.AppendElement(elem);
        }
      }

      // Only allow the traversal to go through the children if the element
      // does have a display.
      if (elem->HasServoData()) {
        cur = cur->GetNextNode(this);
        continue;
      }
    }

    // Either this isn't an element, or it has `display: none`.
    // Continue with the traversal but ignore all the children.
    cur = cur->GetNextNonChildNode(this);
  }
}

bool Element::HasVisibleScrollbars() {
  ScrollContainerFrame* scrollFrame = GetScrollContainerFrame();
  return scrollFrame && !scrollFrame->GetScrollbarVisibility().isEmpty();
}

nsresult Element::BindToTree(BindContext& aContext, nsINode& aParent) {
  MOZ_ASSERT(aParent.IsContent() || aParent.IsDocument(),
             "Must have content or document parent!");
  MOZ_ASSERT(aParent.OwnerDoc() == OwnerDoc(),
             "Must have the same owner document");
  MOZ_ASSERT(OwnerDoc() == &aContext.OwnerDoc(), "These should match too");
  MOZ_ASSERT(!IsInUncomposedDoc(), "Already have a document.  Unbind first!");
  MOZ_ASSERT(!IsInComposedDoc(), "Already have a document.  Unbind first!");
  // Note that as we recurse into the kids, they'll have a non-null parent.  So
  // only assert if our parent is _changing_ while we have a parent.
  MOZ_ASSERT(!GetParentNode() || &aParent == GetParentNode(),
             "Already have a parent.  Unbind first!");

  const bool hadParent = !!GetParentNode();

  if (aParent.IsInNativeAnonymousSubtree()) {
    SetFlags(NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE);
  }
  if (IsRootOfNativeAnonymousSubtree()) {
    aParent.SetMayHaveAnonymousChildren();
  } else if (aParent.HasFlag(NODE_HAS_BEEN_IN_UA_WIDGET)) {
    SetFlags(NODE_HAS_BEEN_IN_UA_WIDGET);
  }
  if (aParent.HasFlag(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR)) {
    SetFlags(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR);
  }
  aParent.SetFlags(NODE_MAY_HAVE_ELEMENT_CHILDREN);

  // Now set the parent.
  mParent = &aParent;
  if (!hadParent && aParent.IsContent()) {
    SetParentIsContent(true);
    NS_ADDREF(mParent);
  }
  MOZ_ASSERT(!!GetParent() == aParent.IsContent());

  MOZ_ASSERT(!HasAnyOfFlags(Element::kAllServoDescendantBits));

  // Finally, set the document
  if (aParent.IsInUncomposedDoc() || aParent.IsInShadowTree()) {
    // We no longer need to track the subtree pointer (and in fact we'll assert
    // if we do this any later).
    ClearSubtreeRootPointer();
    SetIsConnected(aParent.IsInComposedDoc());

    if (aParent.IsInUncomposedDoc()) {
      SetIsInDocument();
    } else {
      SetFlags(NODE_IS_IN_SHADOW_TREE);
      MOZ_ASSERT(aParent.IsContent() &&
                 aParent.AsContent()->GetContainingShadow());
      ExtendedDOMSlots()->mContainingShadow =
          aParent.AsContent()->GetContainingShadow();
    }
    // Clear the lazy frame construction bits.
    UnsetFlags(NODE_NEEDS_FRAME | NODE_DESCENDANTS_NEED_FRAMES);
  } else {
    // If we're not in the doc and not in a shadow tree,
    // update our subtree pointer.
    SetSubtreeRootPointer(aParent.SubtreeRoot());
  }

  if (IsInComposedDoc()) {
    if (IsPendingMappedAttributeEvaluation()) {
      aContext.OwnerDoc().ScheduleForPresAttrEvaluation(this);
    }
    // Connected callback must be enqueued whenever a custom element becomes
    // connected.
    if (CustomElementData* data = GetCustomElementData()) {
      if (data->mState == CustomElementData::State::eCustom) {
        nsContentUtils::EnqueueLifecycleCallback(
            ElementCallbackType::eConnected, this, {});
      } else {
        // Step 7.7.2.2 https://dom.spec.whatwg.org/#concept-node-insert
        nsContentUtils::TryToUpgradeElement(this);
      }
    }
  }

  // This has to be here, rather than in nsGenericHTMLElement::BindToTree,
  //  because it has to happen after updating the parent pointer, but before
  //  recursively binding the kids.
  SetDirOnBind(this, nsIContent::FromNode(aParent));

  UpdateEditableState(false);

  // Call BindToTree on shadow root children.
  nsresult rv;
  if (ShadowRoot* shadowRoot = GetShadowRoot()) {
    rv = shadowRoot->Bind();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Now recurse into our kids. Ensure this happens after binding the shadow
  // root so that directionality of slots is updated.
  {
    for (nsIContent* child = GetFirstChild(); child;
         child = child->GetNextSibling()) {
      rv = child->BindToTree(aContext, *this);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  MutationObservers::NotifyParentChainChanged(this);

  // Ensure we only run this once, in the case we move the ShadowRoot around.
  if (aContext.SubtreeRootChanges()) {
    if (HasPartAttribute()) {
      if (ShadowRoot* shadow = GetContainingShadow()) {
        shadow->PartAdded(*this);
      }
    }
    if (HasID()) {
      AddToIdTable(DoGetID());
    }
    HandleShadowDOMRelatedInsertionSteps(hadParent);
  }

  if (MayHaveStyle()) {
    // If MayHaveStyle() is true, we must be an nsStyledElement.
    static_cast<nsStyledElement*>(this)->ReparseStyleAttribute(
        /* aForceInDataDoc = */ false);
  }

  // XXXbz script execution during binding can trigger some of these
  // postcondition asserts....  But we do want that, since things will
  // generally be quite broken when that happens.
  MOZ_ASSERT(OwnerDoc() == aParent.OwnerDoc(), "Bound to wrong document");
  MOZ_ASSERT(IsInComposedDoc() == aContext.InComposedDoc());
  MOZ_ASSERT(IsInUncomposedDoc() == aContext.InUncomposedDoc());
  MOZ_ASSERT(&aParent == GetParentNode(), "Bound to wrong parent node");
  MOZ_ASSERT(aParent.IsInUncomposedDoc() == IsInUncomposedDoc());
  MOZ_ASSERT(aParent.IsInComposedDoc() == IsInComposedDoc());
  MOZ_ASSERT(aParent.IsInShadowTree() == IsInShadowTree());
  MOZ_ASSERT(aParent.SubtreeRoot() == SubtreeRoot());
  return NS_OK;
}

static bool WillDetachFromShadowOnUnbind(const Element& aElement,
                                         bool aNullParent) {
  // If our parent still is in a shadow tree by now, and we're not removing
  // ourselves from it, then we're still going to be in a shadow tree after
  // this.
  return aElement.IsInShadowTree() &&
         (aNullParent || !aElement.GetParent()->IsInShadowTree());
}

void Element::UnbindFromTree(UnbindContext& aContext) {
  const bool nullParent = aContext.IsUnbindRoot(this);

  HandleShadowDOMRelatedRemovalSteps(nullParent);

  if (HasFlag(ELEMENT_IN_CONTENT_IDENTIFIER_FOR_LCP)) {
    OwnerDoc()->ContentIdentifiersForLCP().Remove(this);
    UnsetFlags(ELEMENT_IN_CONTENT_IDENTIFIER_FOR_LCP);
  }

  if (HasFlag(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR) &&
      !IsHTMLElement(nsGkAtoms::datalist)) {
    if (nullParent) {
      UnsetFlags(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR);
    } else {
      nsIContent* parent = GetParent();
      MOZ_ASSERT(parent);
      if (!parent->HasFlag(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR)) {
        UnsetFlags(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR);
      }
    }
  }

  const bool detachingFromShadow =
      WillDetachFromShadowOnUnbind(*this, nullParent);
  // Make sure to only remove from the ID table if our subtree root is actually
  // changing.
  if (IsInUncomposedDoc() || detachingFromShadow) {
    RemoveFromIdTable();
  }

  if (detachingFromShadow && HasPartAttribute()) {
    if (ShadowRoot* shadow = GetContainingShadow()) {
      shadow->PartRemoved(*this);
    }
  }

  // Make sure to unbind this node before doing the kids
  Document* document = GetComposedDoc();

  if (HasPointerLock()) {
    PointerLockManager::Unlock("Element::UnbindFromTree");
  }
  if (mState.HasState(ElementState::FULLSCREEN)) {
    // The element being removed is an ancestor of the fullscreen element,
    // exit fullscreen state.
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                    OwnerDoc(), nsContentUtils::eDOM_PROPERTIES,
                                    "RemovedFullscreenElement");
    // Fully exit fullscreen.
    Document::ExitFullscreenInDocTree(OwnerDoc());
  }

  MOZ_ASSERT_IF(HasServoData(), document);
  MOZ_ASSERT_IF(HasServoData(), IsInNativeAnonymousSubtree());
  if (document) {
    ClearServoData(document);
  }

  // Ensure that CSS transitions don't continue on an element at a
  // different place in the tree (even if reinserted before next
  // animation refresh).
  //
  // We need to delete the properties while we're still in document
  // (if we were in document) so that they can look up the
  // PendingAnimationTracker on the document and remove their animations,
  // and so they can find their pres context for dispatching cancel events.
  //
  // FIXME(bug 522599): Need a test for this.
  // FIXME(emilio): Why not clearing the effect set as well?
  if (auto* data = GetAnimationData()) {
    data->ClearAllAnimationCollections();
  }

  if (nullParent) {
    if (GetParent()) {
      RefPtr<nsINode> p;
      p.swap(mParent);
    } else {
      mParent = nullptr;
    }
    SetParentIsContent(false);
  }

#ifdef DEBUG
  // If we can get access to the PresContext, then we sanity-check that
  // we're not leaving behind a pointer to ourselves as the PresContext's
  // cached provider of the viewport's scrollbar styles.
  if (document) {
    nsPresContext* presContext = document->GetPresContext();
    if (presContext) {
      MOZ_ASSERT(this != presContext->GetViewportScrollStylesOverrideElement(),
                 "Leaving behind a raw pointer to this element (as having "
                 "propagated scrollbar styles) - that's dangerous...");
    }
  }

#  ifdef ACCESSIBILITY
  MOZ_ASSERT(!GetAccService() || !GetAccService()->HasAccessible(this),
             "An accessible for this element still exists!");
#  endif
#endif

  ClearInDocument();
  SetIsConnected(false);
  if (HasElementCreatedFromPrototypeAndHasUnmodifiedL10n()) {
    if (document) {
      document->mL10nProtoElements.Remove(this);
    }
    ClearElementCreatedFromPrototypeAndHasUnmodifiedL10n();
  }

  if (nullParent || !mParent->IsInShadowTree()) {
    UnsetFlags(NODE_IS_IN_SHADOW_TREE);

    // Begin keeping track of our subtree root.
    SetSubtreeRootPointer(nullParent ? this : mParent->SubtreeRoot());

    if (nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots()) {
      slots->mContainingShadow = nullptr;
    }
  }

  if (document) {
    // Disconnected must be enqueued whenever a connected custom element becomes
    // disconnected.
    if (CustomElementData* data = GetCustomElementData()) {
      if (data->mState == CustomElementData::State::eCustom) {
        nsContentUtils::EnqueueLifecycleCallback(
            ElementCallbackType::eDisconnected, this, {});
      } else {
        // Remove an unresolved custom element that is a candidate for upgrade
        // when a custom element is disconnected.
        nsContentUtils::UnregisterUnresolvedElement(this);
      }
    }

    if (IsPendingMappedAttributeEvaluation()) {
      document->UnscheduleForPresAttrEvaluation(this);
    }

    if (HasLastRememberedBSize() || HasLastRememberedISize()) {
      // Make sure the element is observed so that remembered sizes are kept
      // until the next time "ResizeObserver events are determined and
      // delivered". See "Disconnected element" tests from
      // css/css-sizing/contain-intrinsic-size/auto-006.html
      document->ObserveForLastRememberedSize(*this);
    }
  }

  // This has to be here, rather than in nsGenericHTMLElement::UnbindFromTree,
  //  because it has to happen after unsetting the parent pointer, but before
  //  recursively unbinding the kids.
  ResetDir(this);

  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    child->UnbindFromTree(aContext);
  }

  MutationObservers::NotifyParentChainChanged(this);

  // Unbind children of shadow root.
  if (ShadowRoot* shadowRoot = GetShadowRoot()) {
    shadowRoot->Unbind();
  }

  MOZ_ASSERT(!HasAnyOfFlags(kAllServoDescendantBits));
  MOZ_ASSERT(!document || document->GetServoRestyleRoot() != this);
}

UniquePtr<SMILAttr> Element::GetAnimatedAttr(int32_t aNamespaceID,
                                             nsAtom* aName) {
  return nullptr;
}

nsDOMCSSAttributeDeclaration* Element::SMILOverrideStyle() {
  Element::nsExtendedDOMSlots* slots = ExtendedDOMSlots();

  if (!slots->mSMILOverrideStyle) {
    slots->mSMILOverrideStyle = new nsDOMCSSAttributeDeclaration(this, true);
  }

  return slots->mSMILOverrideStyle;
}

DeclarationBlock* Element::GetSMILOverrideStyleDeclaration() {
  Element::nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  return slots ? slots->mSMILOverrideStyleDeclaration.get() : nullptr;
}

void Element::SetSMILOverrideStyleDeclaration(DeclarationBlock& aDeclaration) {
  ExtendedDOMSlots()->mSMILOverrideStyleDeclaration = &aDeclaration;

  // Only need to request a restyle if we're in a document.  (We might not
  // be in a document, if we're clearing animation effects on a target node
  // that's been detached since the previous animation sample.)
  if (Document* doc = GetComposedDoc()) {
    if (PresShell* presShell = doc->GetPresShell()) {
      presShell->RestyleForAnimation(this, RestyleHint::RESTYLE_SMIL);
    }
  }
}

bool Element::IsLabelable() const { return false; }

bool Element::IsInteractiveHTMLContent() const { return false; }

DeclarationBlock* Element::GetInlineStyleDeclaration() const {
  if (!MayHaveStyle()) {
    return nullptr;
  }
  const nsAttrValue* attrVal = mAttrs.GetAttr(nsGkAtoms::style);
  if (!attrVal || attrVal->Type() != nsAttrValue::eCSSDeclaration) {
    return nullptr;
  }
  return attrVal->GetCSSDeclarationValue();
}

void Element::InlineStyleDeclarationWillChange(MutationClosureData& aData) {
  MOZ_ASSERT_UNREACHABLE("Element::InlineStyleDeclarationWillChange");
}

nsresult Element::SetInlineStyleDeclaration(DeclarationBlock& aDeclaration,
                                            MutationClosureData& aData) {
  MOZ_ASSERT_UNREACHABLE("Element::SetInlineStyleDeclaration");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP_(bool)
Element::IsAttributeMapped(const nsAtom* aAttribute) const { return false; }

nsMapRuleToAttributesFunc Element::GetAttributeMappingFunction() const {
  return &MapNoAttributesInto;
}

void Element::MapNoAttributesInto(mozilla::MappedDeclarationsBuilder&) {}

nsChangeHint Element::GetAttributeChangeHint(const nsAtom* aAttribute,
                                             int32_t aModType) const {
  return nsChangeHint(0);
}

void Element::SetMappedDeclarationBlock(
    already_AddRefed<StyleLockedDeclarationBlock> aDeclarations) {
  MOZ_ASSERT(IsPendingMappedAttributeEvaluation());
  mAttrs.SetMappedDeclarationBlock(std::move(aDeclarations));
  MOZ_ASSERT(!IsPendingMappedAttributeEvaluation());
}

bool Element::FindAttributeDependence(const nsAtom* aAttribute,
                                      const MappedAttributeEntry* const aMaps[],
                                      uint32_t aMapCount) {
  for (uint32_t mapindex = 0; mapindex < aMapCount; ++mapindex) {
    for (const MappedAttributeEntry* map = aMaps[mapindex]; map->attribute;
         ++map) {
      if (aAttribute == map->attribute) {
        return true;
      }
    }
  }

  return false;
}

already_AddRefed<mozilla::dom::NodeInfo> Element::GetExistingAttrNameFromQName(
    const nsAString& aStr) const {
  const nsAttrName* name = InternalGetAttrNameFromQName(aStr);
  if (!name) {
    return nullptr;
  }

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  if (name->IsAtom()) {
    nodeInfo = mNodeInfo->NodeInfoManager()->GetNodeInfo(
        name->Atom(), nullptr, kNameSpaceID_None, ATTRIBUTE_NODE);
  } else {
    nodeInfo = name->NodeInfo();
  }

  return nodeInfo.forget();
}

// static
bool Element::ShouldBlur(nsIContent* aContent) {
  // Determine if the current element is focused, if it is not focused
  // then we should not try to blur
  Document* document = aContent->GetComposedDoc();
  if (!document) return false;

  nsCOMPtr<nsPIDOMWindowOuter> window = document->GetWindow();
  if (!window) return false;

  nsCOMPtr<nsPIDOMWindowOuter> focusedFrame;
  nsIContent* contentToBlur = nsFocusManager::GetFocusedDescendant(
      window, nsFocusManager::eOnlyCurrentWindow, getter_AddRefs(focusedFrame));

  if (!contentToBlur) {
    return false;
  }

  if (contentToBlur == aContent) {
    return true;
  }

  ShadowRoot* root = aContent->GetShadowRoot();
  if (root && root->DelegatesFocus() &&
      contentToBlur->IsShadowIncludingInclusiveDescendantOf(root)) {
    return true;
  }
  return false;
}

/* static */
nsresult Element::DispatchEvent(nsPresContext* aPresContext,
                                WidgetEvent* aEvent, nsIContent* aTarget,
                                bool aFullDispatch, nsEventStatus* aStatus) {
  MOZ_ASSERT(aTarget, "Must have target");
  MOZ_ASSERT(aEvent, "Must have source event");
  MOZ_ASSERT(aStatus, "Null out param?");

  if (!aPresContext) {
    return NS_OK;
  }

  RefPtr<PresShell> presShell = aPresContext->GetPresShell();
  if (!presShell) {
    return NS_OK;
  }

  if (aFullDispatch) {
    return presShell->HandleEventWithTarget(aEvent, nullptr, aTarget, aStatus);
  }

  return presShell->HandleDOMEventWithTarget(aTarget, aEvent, aStatus);
}

/* static */
nsresult Element::DispatchClickEvent(nsPresContext* aPresContext,
                                     WidgetInputEvent* aSourceEvent,
                                     nsIContent* aTarget, bool aFullDispatch,
                                     const EventFlags* aExtraEventFlags,
                                     nsEventStatus* aStatus) {
  MOZ_ASSERT(aTarget, "Must have target");
  MOZ_ASSERT(aSourceEvent, "Must have source event");
  MOZ_ASSERT(aStatus, "Null out param?");

  WidgetPointerEvent event(aSourceEvent->IsTrusted(), ePointerClick,
                           aSourceEvent->mWidget);
  event.mRefPoint = aSourceEvent->mRefPoint;
  uint32_t clickCount = 1;
  float pressure = 0;
  uint32_t pointerId = 0;  // Use the default value here.
  uint16_t inputSource = 0;
  WidgetMouseEvent* sourceMouseEvent = aSourceEvent->AsMouseEvent();
  if (sourceMouseEvent) {
    clickCount = sourceMouseEvent->mClickCount;
    pressure = sourceMouseEvent->mPressure;
    pointerId = sourceMouseEvent->pointerId;
    inputSource = sourceMouseEvent->mInputSource;
  } else if (aSourceEvent->mClass == eKeyboardEventClass) {
    event.mFlags.mIsPositionless = true;
    inputSource = MouseEvent_Binding::MOZ_SOURCE_KEYBOARD;
    // pointerId definition in Pointer Events:
    // > The pointerId value of -1 MUST be reserved and used to indicate events
    // > that were generated by something other than a pointing device.
    pointerId = -1;
  }
  event.mPressure = pressure;
  event.mClickCount = clickCount;
  event.pointerId = pointerId;
  event.mInputSource = inputSource;
  event.mModifiers = aSourceEvent->mModifiers;
  if (aExtraEventFlags) {
    // Be careful not to overwrite existing flags!
    event.mFlags.Union(*aExtraEventFlags);
  }

  return DispatchEvent(aPresContext, &event, aTarget, aFullDispatch, aStatus);
}

//----------------------------------------------------------------------
nsresult Element::LeaveLink(nsPresContext* aPresContext) {
  if (!aPresContext || !aPresContext->Document()->LinkHandlingEnabled()) {
    return NS_OK;
  }
  nsIDocShell* shell = aPresContext->Document()->GetDocShell();
  if (!shell) {
    return NS_OK;
  }
  return nsDocShell::Cast(shell)->OnLeaveLink();
}

void Element::SetEventHandler(nsAtom* aEventName, const nsAString& aValue,
                              bool aDefer) {
  Document* ownerDoc = OwnerDoc();
  if (ownerDoc->IsLoadedAsData()) {
    // Make this a no-op rather than throwing an error to avoid
    // the error causing problems setting the attribute.
    return;
  }

  MOZ_ASSERT(aEventName, "Must have event name!");
  bool defer = true;
  EventListenerManager* manager =
      GetEventListenerManagerForAttr(aEventName, &defer);
  if (!manager) {
    return;
  }

  defer = defer && aDefer;  // only defer if everyone agrees...
  manager->SetEventHandler(aEventName, aValue, defer,
                           !nsContentUtils::IsChromeDoc(ownerDoc), this);
}

//----------------------------------------------------------------------

const nsAttrName* Element::InternalGetAttrNameFromQName(
    const nsAString& aStr, nsAutoString* aNameToUse) const {
  MOZ_ASSERT(!aNameToUse || aNameToUse->IsEmpty());
  const nsAttrName* val = nullptr;
  if (IsHTMLElement() && IsInHTMLDocument()) {
    nsAutoString lower;
    nsAutoString& outStr = aNameToUse ? *aNameToUse : lower;
    nsContentUtils::ASCIIToLower(aStr, outStr);
    val = mAttrs.GetExistingAttrNameFromQName(outStr);
    if (val) {
      outStr.Truncate();
    }
  } else {
    val = mAttrs.GetExistingAttrNameFromQName(aStr);
    if (!val && aNameToUse) {
      *aNameToUse = aStr;
    }
  }

  return val;
}

bool Element::MaybeCheckSameAttrVal(int32_t aNamespaceID, const nsAtom* aName,
                                    const nsAtom* aPrefix,
                                    const nsAttrValueOrString& aValue,
                                    bool aNotify, nsAttrValue& aOldValue,
                                    uint8_t* aModType, bool* aHasListeners,
                                    bool* aOldValueSet) {
  bool modification = false;
  *aHasListeners =
      aNotify && nsContentUtils::WantMutationEvents(
                     this, NS_EVENT_BITS_MUTATION_ATTRMODIFIED, this);
  *aOldValueSet = false;

  // If we have no listeners and aNotify is false, we are almost certainly
  // coming from the content sink and will almost certainly have no previous
  // value.  Even if we do, setting the value is cheap when we have no
  // listeners and don't plan to notify.  The check for aNotify here is an
  // optimization, the check for *aHasListeners is a correctness issue.
  if (*aHasListeners || aNotify) {
    BorrowedAttrInfo info(GetAttrInfo(aNamespaceID, aName));
    if (info.mValue) {
      // Check whether the old value is the same as the new one.  Note that we
      // only need to actually _get_ the old value if we have listeners or
      // if the element is a custom element (because it may have an
      // attribute changed callback).
      if (*aHasListeners || GetCustomElementData()) {
        // Need to store the old value.
        //
        // If the current attribute value contains a pointer to some other data
        // structure that gets updated in the process of setting the attribute
        // we'll no longer have the old value of the attribute. Therefore, we
        // should serialize the attribute value now to keep a snapshot.
        //
        // We have to serialize the value anyway in order to create the
        // mutation event so there's no cost in doing it now.
        aOldValue.SetToSerialized(*info.mValue);
        *aOldValueSet = true;
      }
      bool valueMatches = aValue.EqualsAsStrings(*info.mValue);
      if (valueMatches && aPrefix == info.mName->GetPrefix()) {
        return true;
      }
      modification = true;
    }
  }
  *aModType = modification
                  ? static_cast<uint8_t>(MutationEvent_Binding::MODIFICATION)
                  : static_cast<uint8_t>(MutationEvent_Binding::ADDITION);
  return false;
}

bool Element::OnlyNotifySameValueSet(int32_t aNamespaceID, nsAtom* aName,
                                     nsAtom* aPrefix,
                                     const nsAttrValueOrString& aValue,
                                     bool aNotify, nsAttrValue& aOldValue,
                                     uint8_t* aModType, bool* aHasListeners,
                                     bool* aOldValueSet) {
  if (!MaybeCheckSameAttrVal(aNamespaceID, aName, aPrefix, aValue, aNotify,
                             aOldValue, aModType, aHasListeners,
                             aOldValueSet)) {
    return false;
  }

  nsAutoScriptBlocker scriptBlocker;
  MutationObservers::NotifyAttributeSetToCurrentValue(this, aNamespaceID,
                                                      aName);
  return true;
}

nsresult Element::SetClassAttrFromParser(nsAtom* aValue) {
  // Keep this in sync with SetAttr and SetParsedAttr below.

  nsAttrValue value;
  value.ParseAtomArray(aValue);

  Document* document = GetComposedDoc();
  mozAutoDocUpdate updateBatch(document, false);

  // In principle, BeforeSetAttr should be called here if a node type
  // existed that wanted to do something special for class, but there
  // is no such node type, so calling SetMayHaveClass() directly.
  SetMayHaveClass();

  return SetAttrAndNotify(kNameSpaceID_None, nsGkAtoms::_class,
                          nullptr,  // prefix
                          nullptr,  // old value
                          value, nullptr,
                          static_cast<uint8_t>(MutationEvent_Binding::ADDITION),
                          false,  // hasListeners
                          false,  // notify
                          kCallAfterSetAttr, document, updateBatch);
}

nsresult Element::SetAttr(int32_t aNamespaceID, nsAtom* aName, nsAtom* aPrefix,
                          const nsAString& aValue,
                          nsIPrincipal* aSubjectPrincipal, bool aNotify) {
  // Keep this in sync with SetParsedAttr below and SetSingleClassFromParser
  // above.

  NS_ENSURE_ARG_POINTER(aName);
  NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown,
               "Don't call SetAttr with unknown namespace");

  uint8_t modType;
  bool hasListeners;
  nsAttrValue oldValue;
  bool oldValueSet;

  {
    const nsAttrValueOrString value(aValue);
    if (OnlyNotifySameValueSet(aNamespaceID, aName, aPrefix, value, aNotify,
                               oldValue, &modType, &hasListeners,
                               &oldValueSet)) {
      OnAttrSetButNotChanged(aNamespaceID, aName, value, aNotify);
      return NS_OK;
    }
  }

  // Hold a script blocker while calling ParseAttribute since that can call
  // out to id-observers
  Document* document = GetComposedDoc();
  mozAutoDocUpdate updateBatch(document, aNotify);

  if (aNotify) {
    MutationObservers::NotifyAttributeWillChange(this, aNamespaceID, aName,
                                                 modType);
  }

  nsAttrValue attrValue;
  if (!ParseAttribute(aNamespaceID, aName, aValue, aSubjectPrincipal,
                      attrValue)) {
    attrValue.SetTo(aValue);
  }

  BeforeSetAttr(aNamespaceID, aName, &attrValue, aNotify);

  PreIdMaybeChange(aNamespaceID, aName, &attrValue);

  return SetAttrAndNotify(aNamespaceID, aName, aPrefix,
                          oldValueSet ? &oldValue : nullptr, attrValue,
                          aSubjectPrincipal, modType, hasListeners, aNotify,
                          kCallAfterSetAttr, document, updateBatch);
}

nsresult Element::SetParsedAttr(int32_t aNamespaceID, nsAtom* aName,
                                nsAtom* aPrefix, nsAttrValue& aParsedValue,
                                bool aNotify) {
  // Keep this in sync with SetAttr and SetSingleClassFromParser above

  NS_ENSURE_ARG_POINTER(aName);
  NS_ASSERTION(aNamespaceID != kNameSpaceID_Unknown,
               "Don't call SetAttr with unknown namespace");

  uint8_t modType;
  bool hasListeners;
  nsAttrValue oldValue;
  bool oldValueSet;

  {
    const nsAttrValueOrString value(aParsedValue);
    if (OnlyNotifySameValueSet(aNamespaceID, aName, aPrefix, value, aNotify,
                               oldValue, &modType, &hasListeners,
                               &oldValueSet)) {
      OnAttrSetButNotChanged(aNamespaceID, aName, value, aNotify);
      return NS_OK;
    }
  }

  Document* document = GetComposedDoc();
  mozAutoDocUpdate updateBatch(document, aNotify);

  if (aNotify) {
    MutationObservers::NotifyAttributeWillChange(this, aNamespaceID, aName,
                                                 modType);
  }

  BeforeSetAttr(aNamespaceID, aName, &aParsedValue, aNotify);

  PreIdMaybeChange(aNamespaceID, aName, &aParsedValue);

  return SetAttrAndNotify(aNamespaceID, aName, aPrefix,
                          oldValueSet ? &oldValue : nullptr, aParsedValue,
                          nullptr, modType, hasListeners, aNotify,
                          kCallAfterSetAttr, document, updateBatch);
}

nsresult Element::SetAttrAndNotify(
    int32_t aNamespaceID, nsAtom* aName, nsAtom* aPrefix,
    const nsAttrValue* aOldValue, nsAttrValue& aParsedValue,
    nsIPrincipal* aSubjectPrincipal, uint8_t aModType, bool aFireMutation,
    bool aNotify, bool aCallAfterSetAttr, Document* aComposedDocument,
    const mozAutoDocUpdate& aGuard) {
  nsMutationGuard::DidMutate();

  // Copy aParsedValue for later use since it will be lost when we call
  // SetAndSwapAttr below
  nsAttrValue valueForAfterSetAttr;
  if (aCallAfterSetAttr || GetCustomElementData()) {
    valueForAfterSetAttr.SetTo(aParsedValue);
  }

  bool hadValidDir = false;
  bool hadDirAuto = false;
  bool oldValueSet;

  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::dir) {
      hadValidDir = HasValidDir() || IsHTMLElement(nsGkAtoms::bdi);
      hadDirAuto = HasDirAuto();  // already takes bdi into account
    }

    MOZ_TRY(mAttrs.SetAndSwapAttr(aName, aParsedValue, &oldValueSet));
    if (IsAttributeMapped(aName) && !IsPendingMappedAttributeEvaluation()) {
      mAttrs.InfallibleMarkAsPendingPresAttributeEvaluation();
      if (Document* doc = GetComposedDoc()) {
        doc->ScheduleForPresAttrEvaluation(this);
      }
    }
  } else {
    RefPtr<mozilla::dom::NodeInfo> ni =
        mNodeInfo->NodeInfoManager()->GetNodeInfo(aName, aPrefix, aNamespaceID,
                                                  ATTRIBUTE_NODE);
    MOZ_TRY(mAttrs.SetAndSwapAttr(ni, aParsedValue, &oldValueSet));
  }

  PostIdMaybeChange(aNamespaceID, aName, &valueForAfterSetAttr);

  // If the old value owns its own data, we know it is OK to keep using it.
  // oldValue will be null if there was no previously set value
  const nsAttrValue* oldValue;
  if (aParsedValue.StoresOwnData()) {
    if (oldValueSet) {
      oldValue = &aParsedValue;
    } else {
      oldValue = nullptr;
    }
  } else {
    // No need to conditionally assign null here. If there was no previously
    // set value for the attribute, aOldValue will already be null.
    oldValue = aOldValue;
  }

  if (HasElementCreatedFromPrototypeAndHasUnmodifiedL10n() &&
      aNamespaceID == kNameSpaceID_None &&
      (aName == nsGkAtoms::datal10nid || aName == nsGkAtoms::datal10nargs)) {
    ClearElementCreatedFromPrototypeAndHasUnmodifiedL10n();
    if (aComposedDocument) {
      aComposedDocument->mL10nProtoElements.Remove(this);
    }
  }

  const CustomElementData* data = GetCustomElementData();
  if (data && data->mState == CustomElementData::State::eCustom) {
    CustomElementDefinition* definition = data->GetCustomElementDefinition();
    MOZ_ASSERT(definition, "Should have a valid CustomElementDefinition");

    if (definition->IsInObservedAttributeList(aName)) {
      nsAutoString ns;
      nsNameSpaceManager::GetInstance()->GetNameSpaceURI(aNamespaceID, ns);

      LifecycleCallbackArgs args;
      args.mName = aName;
      if (aModType == MutationEvent_Binding::ADDITION) {
        args.mOldValue = VoidString();
      } else {
        if (oldValue) {
          oldValue->ToString(args.mOldValue);
        } else {
          // If there is no old value, get the value of the uninitialized
          // attribute that was swapped with aParsedValue.
          aParsedValue.ToString(args.mOldValue);
        }
      }
      valueForAfterSetAttr.ToString(args.mNewValue);
      args.mNamespaceURI = ns.IsEmpty() ? VoidString() : ns;

      nsContentUtils::EnqueueLifecycleCallback(
          ElementCallbackType::eAttributeChanged, this, args, definition);
    }
  }

  if (aCallAfterSetAttr) {
    AfterSetAttr(aNamespaceID, aName, &valueForAfterSetAttr, oldValue,
                 aSubjectPrincipal, aNotify);

    if (aNamespaceID == kNameSpaceID_None && aName == nsGkAtoms::dir) {
      OnSetDirAttr(this, &valueForAfterSetAttr, hadValidDir, hadDirAuto,
                   aNotify);
    }
  }

  if (aNotify) {
    // Don't pass aOldValue to AttributeChanged since it may not be reliable.
    // Callers only compute aOldValue under certain conditions which may not
    // be triggered by all nsIMutationObservers.
    MutationObservers::NotifyAttributeChanged(
        this, aNamespaceID, aName, aModType,
        aParsedValue.StoresOwnData() ? &aParsedValue : nullptr);
  }

  if (aFireMutation) {
    InternalMutationEvent mutation(true, eLegacyAttrModified);

    nsAutoString ns;
    nsNameSpaceManager::GetInstance()->GetNameSpaceURI(aNamespaceID, ns);
    Attr* attrNode =
        GetAttributeNodeNSInternal(ns, nsDependentAtomString(aName));
    mutation.mRelatedNode = attrNode;

    mutation.mAttrName = aName;
    nsAutoString newValue;
    GetAttr(aNamespaceID, aName, newValue);
    if (!newValue.IsEmpty()) {
      mutation.mNewAttrValue = NS_Atomize(newValue);
    }
    if (oldValue && !oldValue->IsEmptyString()) {
      mutation.mPrevAttrValue = oldValue->GetAsAtom();
    }
    mutation.mAttrChange = aModType;

    mozAutoSubtreeModified subtree(OwnerDoc(), this);
    AsyncEventDispatcher::RunDOMEventWhenSafe(*this, mutation);
  }

  return NS_OK;
}

void Element::TryReserveAttributeCount(uint32_t aAttributeCount) {
  (void)mAttrs.GrowTo(aAttributeCount);
}

bool Element::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                             const nsAString& aValue,
                             nsIPrincipal* aMaybeScriptedPrincipal,
                             nsAttrValue& aResult) {
  if (aAttribute == nsGkAtoms::lang) {
    aResult.ParseAtom(aValue);
    return true;
  }

  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::_class || aAttribute == nsGkAtoms::part ||
        aAttribute == nsGkAtoms::aria_controls ||
        aAttribute == nsGkAtoms::aria_describedby ||
        aAttribute == nsGkAtoms::aria_details ||
        aAttribute == nsGkAtoms::aria_errormessage ||
        aAttribute == nsGkAtoms::aria_flowto ||
        aAttribute == nsGkAtoms::aria_labelledby ||
        aAttribute == nsGkAtoms::aria_owns) {
      aResult.ParseAtomArray(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::exportparts) {
      aResult.ParsePartMapping(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::aria_activedescendant) {
      // String in aria-activedescendant is an id, so store as an atom.
      aResult.ParseAtom(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::id) {
      // Store id as an atom.  id="" means that the element has no id,
      // not that it has an emptystring as the id.
      if (aValue.IsEmpty()) {
        return false;
      }
      aResult.ParseAtom(aValue);
      return true;
    }
  }

  return false;
}

void Element::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                            const nsAttrValue* aValue, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::_class && aValue) {
      // Note: This flag is asymmetrical. It is never unset and isn't exact.
      // If it is ever made to be exact, we probably need to handle this
      // similarly to how ids are handled in PreIdMaybeChange and
      // PostIdMaybeChange.
      // Note that SetSingleClassFromParser inlines BeforeSetAttr and
      // calls SetMayHaveClass directly. Making a subclass take action
      // on the class attribute in a BeforeSetAttr override would
      // require revising SetSingleClassFromParser.
      SetMayHaveClass();
    }
  }
}

void Element::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                           const nsAttrValue* aValue,
                           const nsAttrValue* aOldValue,
                           nsIPrincipal* aMaybeScriptedPrincipal,
                           bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::part) {
      bool isPart = !!aValue;
      if (HasPartAttribute() != isPart) {
        SetHasPartAttribute(isPart);
        if (ShadowRoot* shadow = GetContainingShadow()) {
          if (isPart) {
            shadow->PartAdded(*this);
          } else {
            shadow->PartRemoved(*this);
          }
        }
      }
      MOZ_ASSERT(HasPartAttribute() == isPart);
    } else if (aName == nsGkAtoms::slot && GetParent()) {
      if (ShadowRoot* shadow = GetParent()->GetShadowRoot()) {
        shadow->MaybeReassignContent(*this);
      }
    } else if (aName == nsGkAtoms::aria_activedescendant) {
      ClearExplicitlySetAttrElement(aName);
    } else if (aName == nsGkAtoms::aria_controls ||
               aName == nsGkAtoms::aria_describedby ||
               aName == nsGkAtoms::aria_details ||
               aName == nsGkAtoms::aria_errormessage ||
               aName == nsGkAtoms::aria_flowto ||
               aName == nsGkAtoms::aria_labelledby ||
               aName == nsGkAtoms::aria_owns) {
      ClearExplicitlySetAttrElements(aName);
    }
  }
}

void Element::PreIdMaybeChange(int32_t aNamespaceID, nsAtom* aName,
                               const nsAttrValue* aValue) {
  if (aNamespaceID != kNameSpaceID_None || aName != nsGkAtoms::id) {
    return;
  }
  RemoveFromIdTable();
}

void Element::PostIdMaybeChange(int32_t aNamespaceID, nsAtom* aName,
                                const nsAttrValue* aValue) {
  if (aNamespaceID != kNameSpaceID_None || aName != nsGkAtoms::id) {
    return;
  }

  // id="" means that the element has no id, not that it has an empty
  // string as the id.
  if (aValue && !aValue->IsEmptyString()) {
    SetHasID();
    AddToIdTable(aValue->GetAtomValue());
  } else {
    ClearHasID();
  }
}

void Element::OnAttrSetButNotChanged(int32_t aNamespaceID, nsAtom* aName,
                                     const nsAttrValueOrString& aValue,
                                     bool aNotify) {
  const CustomElementData* data = GetCustomElementData();
  if (data && data->mState == CustomElementData::State::eCustom) {
    CustomElementDefinition* definition = data->GetCustomElementDefinition();
    MOZ_ASSERT(definition, "Should have a valid CustomElementDefinition");

    if (definition->IsInObservedAttributeList(aName)) {
      nsAutoString ns;
      nsNameSpaceManager::GetInstance()->GetNameSpaceURI(aNamespaceID, ns);

      nsAutoString value(aValue.String());
      LifecycleCallbackArgs args;
      args.mName = aName;
      args.mOldValue = value;
      args.mNewValue = value;
      args.mNamespaceURI = ns.IsEmpty() ? VoidString() : ns;

      nsContentUtils::EnqueueLifecycleCallback(
          ElementCallbackType::eAttributeChanged, this, args, definition);
    }
  }

  if (aNamespaceID == kNameSpaceID_None &&
      aName == nsGkAtoms::aria_activedescendant) {
    ClearExplicitlySetAttrElement(aName);
  }

  if (aNamespaceID == kNameSpaceID_None &&
      (aName == nsGkAtoms::aria_controls ||
       aName == nsGkAtoms::aria_describedby ||
       aName == nsGkAtoms::aria_details ||
       aName == nsGkAtoms::aria_errormessage ||
       aName == nsGkAtoms::aria_flowto || aName == nsGkAtoms::aria_labelledby ||
       aName == nsGkAtoms::aria_owns)) {
    ClearExplicitlySetAttrElements(aName);
  }
}

EventListenerManager* Element::GetEventListenerManagerForAttr(nsAtom* aAttrName,
                                                              bool* aDefer) {
  *aDefer = true;
  return GetOrCreateListenerManager();
}

bool Element::GetAttr(const nsAtom* aName, nsAString& aResult) const {
  const nsAttrValue* val = mAttrs.GetAttr(aName);
  if (!val) {
    aResult.Truncate();
    return false;
  }
  val->ToString(aResult);
  return true;
}

bool Element::GetAttr(int32_t aNameSpaceID, const nsAtom* aName,
                      nsAString& aResult) const {
  const nsAttrValue* val = mAttrs.GetAttr(aName, aNameSpaceID);
  if (!val) {
    aResult.Truncate();
    return false;
  }
  val->ToString(aResult);
  return true;
}

int32_t Element::FindAttrValueIn(int32_t aNameSpaceID, const nsAtom* aName,
                                 AttrArray::AttrValuesArray* aValues,
                                 nsCaseTreatment aCaseSensitive) const {
  return mAttrs.FindAttrValueIn(aNameSpaceID, aName, aValues, aCaseSensitive);
}

nsresult Element::UnsetAttr(int32_t aNameSpaceID, nsAtom* aName, bool aNotify) {
  NS_ASSERTION(nullptr != aName, "must have attribute name");

  int32_t index = mAttrs.IndexOfAttr(aName, aNameSpaceID);
  if (index < 0) {
    return NS_OK;
  }

  Document* document = GetComposedDoc();
  mozAutoDocUpdate updateBatch(document, aNotify);

  if (aNotify) {
    MutationObservers::NotifyAttributeWillChange(
        this, aNameSpaceID, aName, MutationEvent_Binding::REMOVAL);
  }

  BeforeSetAttr(aNameSpaceID, aName, nullptr, aNotify);

  bool hasMutationListeners =
      aNotify && nsContentUtils::WantMutationEvents(
                     this, NS_EVENT_BITS_MUTATION_ATTRMODIFIED, this);

  PreIdMaybeChange(aNameSpaceID, aName, nullptr);

  // Grab the attr node if needed before we remove it from the attr map
  RefPtr<Attr> attrNode;
  if (hasMutationListeners) {
    nsAutoString ns;
    nsNameSpaceManager::GetInstance()->GetNameSpaceURI(aNameSpaceID, ns);
    attrNode = GetAttributeNodeNSInternal(ns, nsDependentAtomString(aName));
  }

  // Clear the attribute out from attribute map.
  nsDOMSlots* slots = GetExistingDOMSlots();
  if (slots && slots->mAttributeMap) {
    slots->mAttributeMap->DropAttribute(aNameSpaceID, aName);
  }

  // The id-handling code, and in the future possibly other code, need to
  // react to unexpected attribute changes.
  nsMutationGuard::DidMutate();

  bool hadValidDir = false;
  bool hadDirAuto = false;

  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::dir) {
      hadValidDir = HasValidDir() || IsHTMLElement(nsGkAtoms::bdi);
      hadDirAuto = HasDirAuto();  // already takes bdi into account
    }
    if (IsAttributeMapped(aName) && !IsPendingMappedAttributeEvaluation()) {
      mAttrs.InfallibleMarkAsPendingPresAttributeEvaluation();
      if (Document* doc = GetComposedDoc()) {
        doc->ScheduleForPresAttrEvaluation(this);
      }
    }
  }

  nsAttrValue oldValue;
  MOZ_TRY(mAttrs.RemoveAttrAt(index, oldValue));

  PostIdMaybeChange(aNameSpaceID, aName, nullptr);

  const CustomElementData* data = GetCustomElementData();
  if (data && data->mState == CustomElementData::State::eCustom) {
    CustomElementDefinition* definition = data->GetCustomElementDefinition();
    MOZ_ASSERT(definition, "Should have a valid CustomElementDefinition");
    if (definition->IsInObservedAttributeList(aName)) {
      nsAutoString ns;
      nsNameSpaceManager::GetInstance()->GetNameSpaceURI(aNameSpaceID, ns);
      LifecycleCallbackArgs args;
      args.mName = aName;
      oldValue.ToString(args.mOldValue);
      args.mNewValue = VoidString();
      args.mNamespaceURI = ns.IsEmpty() ? VoidString() : ns;
      nsContentUtils::EnqueueLifecycleCallback(
          ElementCallbackType::eAttributeChanged, this, args, definition);
    }
  }

  AfterSetAttr(aNameSpaceID, aName, nullptr, &oldValue, nullptr, aNotify);

  if (aNotify) {
    // We can always pass oldValue here since there is no new value which could
    // have corrupted it.
    MutationObservers::NotifyAttributeChanged(
        this, aNameSpaceID, aName, MutationEvent_Binding::REMOVAL, &oldValue);
  }

  if (aNameSpaceID == kNameSpaceID_None && aName == nsGkAtoms::dir) {
    OnSetDirAttr(this, nullptr, hadValidDir, hadDirAuto, aNotify);
  }

  if (hasMutationListeners) {
    InternalMutationEvent mutation(true, eLegacyAttrModified);

    mutation.mRelatedNode = attrNode;
    mutation.mAttrName = aName;

    nsAutoString value;
    oldValue.ToString(value);
    if (!value.IsEmpty()) mutation.mPrevAttrValue = NS_Atomize(value);
    mutation.mAttrChange = MutationEvent_Binding::REMOVAL;

    mozAutoSubtreeModified subtree(OwnerDoc(), this);
    AsyncEventDispatcher::RunDOMEventWhenSafe(*this, mutation);
  }

  return NS_OK;
}

void Element::DescribeAttribute(uint32_t index,
                                nsAString& aOutDescription) const {
  // name
  mAttrs.AttrNameAt(index)->GetQualifiedName(aOutDescription);

  // value
  aOutDescription.AppendLiteral("=\"");
  nsAutoString value;
  mAttrs.AttrAt(index)->ToString(value);
  for (uint32_t i = value.Length(); i > 0; --i) {
    if (value[i - 1] == char16_t('"')) value.Insert(char16_t('\\'), i - 1);
  }
  aOutDescription.Append(value);
  aOutDescription.Append('"');
}

#ifdef MOZ_DOM_LIST
void Element::ListAttributes(FILE* out) const {
  uint32_t index, count = mAttrs.AttrCount();
  for (index = 0; index < count; index++) {
    nsAutoString attributeDescription;
    DescribeAttribute(index, attributeDescription);

    fputs(" ", out);
    fputs(NS_LossyConvertUTF16toASCII(attributeDescription).get(), out);
  }
}

void Element::List(FILE* out, int32_t aIndent, const nsCString& aPrefix) const {
  int32_t indent;
  for (indent = aIndent; --indent >= 0;) fputs("  ", out);

  fputs(aPrefix.get(), out);

  fputs(NS_LossyConvertUTF16toASCII(mNodeInfo->QualifiedName()).get(), out);

  fprintf(out, "@%p", (void*)this);

  ListAttributes(out);

  fprintf(out, " state=[%llx]",
          static_cast<unsigned long long>(State().GetInternalValue()));
  fprintf(out, " flags=[%08x]", static_cast<unsigned int>(GetFlags()));
  fprintf(out, " selectorflags=[%08x]",
          static_cast<unsigned int>(GetSelectorFlags()));
  if (IsClosestCommonInclusiveAncestorForRangeInSelection()) {
    const LinkedList<AbstractRange>* ranges =
        GetExistingClosestCommonInclusiveAncestorRanges();
    int32_t count = 0;
    if (ranges) {
      // Can't use range-based iteration on a const LinkedList, unfortunately.
      for (const AbstractRange* r = ranges->getFirst(); r; r = r->getNext()) {
        ++count;
      }
    }
    fprintf(out, " ranges:%d", count);
  }
  fprintf(out, " primaryframe=%p", static_cast<void*>(GetPrimaryFrame()));
  fprintf(out, " refcount=%" PRIuPTR "<", mRefCnt.get());

  nsIContent* child = GetFirstChild();
  if (child) {
    fputs("\n", out);

    for (; child; child = child->GetNextSibling()) {
      child->List(out, aIndent + 1);
    }

    for (indent = aIndent; --indent >= 0;) fputs("  ", out);
  }

  fputs(">\n", out);
}

void Element::DumpContent(FILE* out, int32_t aIndent, bool aDumpAll) const {
  int32_t indent;
  for (indent = aIndent; --indent >= 0;) fputs("  ", out);

  const nsString& buf = mNodeInfo->QualifiedName();
  fputs("<", out);
  fputs(NS_LossyConvertUTF16toASCII(buf).get(), out);

  if (aDumpAll) ListAttributes(out);

  fputs(">", out);

  if (aIndent) fputs("\n", out);

  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    int32_t indent = aIndent ? aIndent + 1 : 0;
    child->DumpContent(out, indent, aDumpAll);
  }
  for (indent = aIndent; --indent >= 0;) fputs("  ", out);
  fputs("</", out);
  fputs(NS_LossyConvertUTF16toASCII(buf).get(), out);
  fputs(">", out);

  if (aIndent) fputs("\n", out);
}
#endif

void Element::Describe(nsAString& aOutDescription, bool aShort) const {
  aOutDescription.Append(mNodeInfo->QualifiedName());
  aOutDescription.AppendPrintf("@%p", (void*)this);

  uint32_t index, count = mAttrs.AttrCount();
  for (index = 0; index < count; index++) {
    if (aShort) {
      const nsAttrName* name = mAttrs.AttrNameAt(index);
      if (!name->Equals(nsGkAtoms::id) && !name->Equals(nsGkAtoms::_class)) {
        continue;
      }
    }
    aOutDescription.Append(' ');
    nsAutoString attributeDescription;
    DescribeAttribute(index, attributeDescription);
    aOutDescription.Append(attributeDescription);
  }
}

bool Element::CheckHandleEventForLinksPrecondition(
    EventChainVisitor& aVisitor) const {
  // Make sure we actually are a link
  if (!IsLink()) {
    return false;
  }
  if (aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault ||
      (!aVisitor.mEvent->IsTrusted() &&
       (aVisitor.mEvent->mMessage != ePointerClick) &&
       (aVisitor.mEvent->mMessage != eKeyPress) &&
       (aVisitor.mEvent->mMessage != eLegacyDOMActivate)) ||
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented) {
    return false;
  }
  return true;
}

void Element::GetEventTargetParentForLinks(EventChainPreVisitor& aVisitor) {
  // Optimisation: return early if this event doesn't interest us.
  // IMPORTANT: this switch and the switch below it must be kept in sync!
  switch (aVisitor.mEvent->mMessage) {
    case eMouseOver:
    case eFocus:
    case eMouseOut:
    case eBlur:
      break;
    default:
      return;
  }

  // Make sure we meet the preconditions before continuing
  if (!CheckHandleEventForLinksPrecondition(aVisitor)) {
    return;
  }

  // We try to handle everything we can even when the URI is invalid. Though of
  // course we can't do stuff like updating the status bar, so return early here
  // instead.
  nsCOMPtr<nsIURI> absURI = GetHrefURI();
  if (!absURI) {
    return;
  }

  // We do the status bar updates in GetEventTargetParent so that the status bar
  // gets updated even if the event is consumed before we have a chance to set
  // it.
  switch (aVisitor.mEvent->mMessage) {
    // Set the status bar similarly for mouseover and focus
    case eMouseOver:
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      [[fallthrough]];
    case eFocus: {
      InternalFocusEvent* focusEvent = aVisitor.mEvent->AsFocusEvent();
      if (!focusEvent || !focusEvent->mIsRefocus) {
        nsAutoString target;
        GetLinkTarget(target);
        nsContentUtils::TriggerLink(this, absURI, target,
                                    /* click */ false);
        // Make sure any ancestor links don't also TriggerLink
        aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
      }
      break;
    }
    case eMouseOut:
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      [[fallthrough]];
    case eBlur: {
      nsresult rv = LeaveLink(aVisitor.mPresContext);
      if (NS_SUCCEEDED(rv)) {
        aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
      }
      break;
    }

    default:
      // switch not in sync with the optimization switch earlier in this
      // function
      MOZ_ASSERT_UNREACHABLE("switch statements not in sync");
  }
}

// This dispatches a 'chromelinkclick' CustomEvent to chrome-only listeners,
// so that frontend can handle middle-clicks and ctrl/cmd/shift/etc.-clicks
// on links, without getting a call for every single click the user makes.
// Only supported for click or auxclick events.
void Element::DispatchChromeOnlyLinkClickEvent(
    EventChainPostVisitor& aVisitor) {
  MOZ_ASSERT(aVisitor.mEvent->mMessage == ePointerAuxClick ||
                 aVisitor.mEvent->mMessage == ePointerClick,
             "DispatchChromeOnlyLinkClickEvent supports only click and "
             "auxclick source events");
  Document* doc = OwnerDoc();
  RefPtr<XULCommandEvent> event =
      new XULCommandEvent(doc, aVisitor.mPresContext, nullptr);
  RefPtr<dom::Event> mouseDOMEvent = aVisitor.mDOMEvent;
  if (!mouseDOMEvent) {
    mouseDOMEvent = EventDispatcher::CreateEvent(
        aVisitor.mEvent->mOriginalTarget, aVisitor.mPresContext,
        aVisitor.mEvent, u""_ns);
    NS_ADDREF(aVisitor.mDOMEvent = mouseDOMEvent);
  }

  MouseEvent* mouseEvent = mouseDOMEvent->AsMouseEvent();
  event->InitCommandEvent(
      u"chromelinkclick"_ns, /* CanBubble */ true,
      /* Cancelable */ true, nsGlobalWindowInner::Cast(doc->GetInnerWindow()),
      0, mouseEvent->CtrlKey(), mouseEvent->AltKey(), mouseEvent->ShiftKey(),
      mouseEvent->MetaKey(), mouseEvent->Button(), mouseDOMEvent,
      mouseEvent->InputSource(CallerType::System), IgnoreErrors());
  // Note: we're always trusted, but the event we pass as the `sourceEvent`
  // might not be. Frontend code will check that event's trusted property to
  // make that determination; doing it this way means we don't also start
  // acting on web-generated custom 'chromelinkclick' events which would
  // provide additional attack surface for a malicious actor.
  event->SetTrusted(true);
  event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;
  DispatchEvent(*event);
}

nsresult Element::PostHandleEventForLinks(EventChainPostVisitor& aVisitor) {
  // Optimisation: return early if this event doesn't interest us.
  // IMPORTANT: this switch and the switch below it must be kept in sync!
  switch (aVisitor.mEvent->mMessage) {
    case eMouseDown:
    case ePointerClick:
    case ePointerAuxClick:
    case eLegacyDOMActivate:
    case eKeyPress:
      break;
    default:
      return NS_OK;
  }

  // Make sure we meet the preconditions before continuing
  if (!CheckHandleEventForLinksPrecondition(aVisitor)) {
    return NS_OK;
  }

  // We try to handle ~everything consistently even if the href is invalid
  // (GetHrefURI() returns null).
  nsresult rv = NS_OK;

  switch (aVisitor.mEvent->mMessage) {
    case eMouseDown: {
      if (!OwnerDoc()->LinkHandlingEnabled()) {
        break;
      }

      WidgetMouseEvent* const mouseEvent = aVisitor.mEvent->AsMouseEvent();
      mouseEvent->mFlags.mMultipleActionsPrevented |=
          mouseEvent->mButton == MouseButton::ePrimary ||
          mouseEvent->mButton == MouseButton::eMiddle;

      if (mouseEvent->mButton == MouseButton::ePrimary) {
        // For avoiding focus popup opened by clicking this link to get blurred,
        // we need this to get focused now.  However, if the mousedown occurs
        // in editable element in this link, we should not do this because its
        // editing host will get focus.
        if (IsInComposedDoc()) {
          Element* targetElement = Element::FromEventTargetOrNull(
              aVisitor.mEvent->GetDOMEventTarget());
          if (targetElement && targetElement->IsInclusiveDescendantOf(this) &&
              (!targetElement->IsEditable() ||
               targetElement->GetEditingHost() == this)) {
            if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
              RefPtr<Element> kungFuDeathGrip(this);
              fm->SetFocus(kungFuDeathGrip, nsIFocusManager::FLAG_BYMOUSE |
                                                nsIFocusManager::FLAG_NOSCROLL);
            }
          }
        }

        if (aVisitor.mPresContext) {
          EventStateManager::SetActiveManager(
              aVisitor.mPresContext->EventStateManager(), this);
        }

        // OK, we're pretty sure we're going to load, so warm up a speculative
        // connection to be sure we have one ready when we open the channel.
        if (nsIDocShell* shell = OwnerDoc()->GetDocShell()) {
          if (nsCOMPtr<nsIURI> absURI = GetHrefURI()) {
            if (nsCOMPtr<nsISpeculativeConnect> sc =
                    mozilla::components::IO::Service()) {
              nsCOMPtr<nsIInterfaceRequestor> ir = do_QueryInterface(shell);
              sc->SpeculativeConnect(absURI, NodePrincipal(), ir, false);
            }
          }
        }
      }
    } break;

    case ePointerClick: {
      WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
      if (mouseEvent->IsLeftClickEvent()) {
        if (!mouseEvent->IsControl() && !mouseEvent->IsMeta() &&
            !mouseEvent->IsAlt() && !mouseEvent->IsShift()) {
          if (OwnerDoc()->MayHaveDOMActivateListeners()) {
            // The default action is simply to dispatch DOMActivate.
            // But dispatch that only if needed.
            nsEventStatus status = nsEventStatus_eIgnore;
            // DOMActivate event should be trusted since the activation is
            // actually occurred even if the cause is an untrusted click event.
            InternalUIEvent actEvent(true, eLegacyDOMActivate, mouseEvent);
            actEvent.mDetail = 1;
            rv = EventDispatcher::Dispatch(this, aVisitor.mPresContext,
                                           &actEvent, nullptr, &status);
            if (NS_SUCCEEDED(rv)) {
              aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
            }
          } else {
            if (nsCOMPtr<nsIURI> absURI = GetHrefURI()) {
              // If you modify this code, tweak also the code handling
              // eLegacyDOMActivate.
              nsAutoString target;
              GetLinkTarget(target);
              nsContentUtils::TriggerLink(this, absURI, target,
                                          /* click */ true);
            }
            // Since we didn't dispatch DOMActivate because there were no
            // listeners, do still set mEventStatus as if it was dispatched
            // successfully.
            aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
          }
        }

        DispatchChromeOnlyLinkClickEvent(aVisitor);
      }
      break;
    }
    case ePointerAuxClick: {
      DispatchChromeOnlyLinkClickEvent(aVisitor);
      break;
    }
    case eLegacyDOMActivate: {
      // If you modify this code, tweak also the code handling
      // ePointerClick.
      if (aVisitor.mEvent->mOriginalTarget == this) {
        if (nsCOMPtr<nsIURI> absURI = GetHrefURI()) {
          nsAutoString target;
          GetLinkTarget(target);
          nsContentUtils::TriggerLink(this, absURI, target, /* click */ true);
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
      }
    } break;

    case eKeyPress: {
      WidgetKeyboardEvent* keyEvent = aVisitor.mEvent->AsKeyboardEvent();
      if (keyEvent && keyEvent->mKeyCode == NS_VK_RETURN) {
        nsEventStatus status = nsEventStatus_eIgnore;
        rv = DispatchClickEvent(aVisitor.mPresContext, keyEvent, this, false,
                                nullptr, &status);
        if (NS_SUCCEEDED(rv)) {
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
      }
    } break;

    default:
      // switch not in sync with the optimization switch earlier in this
      // function
      MOZ_ASSERT_UNREACHABLE("switch statements not in sync");
      return NS_ERROR_UNEXPECTED;
  }

  return rv;
}

// static
void Element::SanitizeLinkOrFormTarget(nsAString& aTarget) {
  // <https://html.spec.whatwg.org/multipage/semantics.html#get-an-element's-target>
  // 2. If target is not null, and contains an ASCII tab or newline and a U+003C
  // (<), then set target to "_blank".
  if (!aTarget.IsEmpty() && aTarget.FindCharInSet(u"\t\n\r") != kNotFound &&
      aTarget.Contains('<')) {
    aTarget.AssignLiteral("_blank");
  }
}

void Element::GetLinkTarget(nsAString& aTarget) {
  GetLinkTargetImpl(aTarget);
  SanitizeLinkOrFormTarget(aTarget);
}

void Element::GetLinkTargetImpl(nsAString& aTarget) { aTarget.Truncate(); }

nsresult Element::CopyInnerTo(Element* aDst, ReparseAttributes aReparse) {
  nsresult rv = aDst->mAttrs.EnsureCapacityToClone(mAttrs);
  NS_ENSURE_SUCCESS(rv, rv);

  const bool reparse = aReparse == ReparseAttributes::Yes;

  uint32_t count = mAttrs.AttrCount();
  for (uint32_t i = 0; i < count; ++i) {
    BorrowedAttrInfo info = mAttrs.AttrInfoAt(i);
    const nsAttrName* name = info.mName;
    const nsAttrValue* value = info.mValue;
    if (value->Type() == nsAttrValue::eCSSDeclaration) {
      MOZ_ASSERT(name->Equals(nsGkAtoms::style, kNameSpaceID_None));
      // We still clone CSS attributes, even in the `reparse` (cross-document)
      // case.  https://github.com/w3c/webappsec-csp/issues/212
      nsAttrValue valueCopy(*value);
      rv = aDst->SetParsedAttr(name->NamespaceID(), name->LocalName(),
                               name->GetPrefix(), valueCopy, false);
      NS_ENSURE_SUCCESS(rv, rv);

      value->GetCSSDeclarationValue()->SetImmutable();
    } else if (reparse) {
      nsAutoString valStr;
      value->ToString(valStr);
      rv = aDst->SetAttr(name->NamespaceID(), name->LocalName(),
                         name->GetPrefix(), valStr, false);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      nsAttrValue valueCopy(*value);
      rv = aDst->SetParsedAttr(name->NamespaceID(), name->LocalName(),
                               name->GetPrefix(), valueCopy, false);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  dom::NodeInfo* dstNodeInfo = aDst->NodeInfo();
  if (CustomElementData* data = GetCustomElementData()) {
    // The cloned node may be a custom element that may require
    // enqueing upgrade reaction.
    if (nsAtom* typeAtom = data->GetCustomElementType()) {
      aDst->SetCustomElementData(MakeUnique<CustomElementData>(typeAtom));
      MOZ_ASSERT(dstNodeInfo->NameAtom()->Equals(dstNodeInfo->LocalName()));
      CustomElementDefinition* definition =
          nsContentUtils::LookupCustomElementDefinition(
              dstNodeInfo->GetDocument(), dstNodeInfo->NameAtom(),
              dstNodeInfo->NamespaceID(), typeAtom);
      if (definition) {
        nsContentUtils::EnqueueUpgradeReaction(aDst, definition);
      }
    }
  }

  if (dstNodeInfo->GetDocument()->IsStaticDocument()) {
    // Propagate :defined state to the static clone.
    if (State().HasState(ElementState::DEFINED)) {
      aDst->SetDefined(true);
    }
  }

  return NS_OK;
}

Element* Element::Closest(const nsACString& aSelector, ErrorResult& aResult) {
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING("Element::Closest",
                                        LAYOUT_SelectorQuery, aSelector);
  const StyleSelectorList* list = ParseSelectorList(aSelector, aResult);
  if (!list) {
    return nullptr;
  }

  return const_cast<Element*>(Servo_SelectorList_Closest(this, list));
}

bool Element::Matches(const nsACString& aSelector, ErrorResult& aResult) {
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING("Element::Matches",
                                        LAYOUT_SelectorQuery, aSelector);
  const StyleSelectorList* list = ParseSelectorList(aSelector, aResult);
  if (!list) {
    return false;
  }

  return Servo_SelectorList_Matches(this, list);
}

static const nsAttrValue::EnumTable kCORSAttributeTable[] = {
    // Order matters here
    // See ParseCORSValue
    {"anonymous", CORS_ANONYMOUS},
    {"use-credentials", CORS_USE_CREDENTIALS},
    {nullptr, 0}};

/* static */
void Element::ParseCORSValue(const nsAString& aValue, nsAttrValue& aResult) {
  DebugOnly<bool> success =
      aResult.ParseEnumValue(aValue, kCORSAttributeTable, false,
                             // default value is anonymous if aValue is
                             // not a value we understand
                             &kCORSAttributeTable[0]);
  MOZ_ASSERT(success);
}

/* static */
CORSMode Element::StringToCORSMode(const nsAString& aValue) {
  if (aValue.IsVoid()) {
    return CORS_NONE;
  }

  nsAttrValue val;
  Element::ParseCORSValue(aValue, val);
  return CORSMode(val.GetEnumValue());
}

/* static */
CORSMode Element::AttrValueToCORSMode(const nsAttrValue* aValue) {
  if (!aValue) {
    return CORS_NONE;
  }

  return CORSMode(aValue->GetEnumValue());
}

/**
 * Returns nullptr if requests for fullscreen are allowed in the current
 * context. Requests are only allowed if the user initiated them (like with
 * a mouse-click or key press), unless this check has been disabled by
 * setting the pref "full-screen-api.allow-trusted-requests-only" to false
 * or if the caller is privileged. Feature policy may also deny requests.
 * If fullscreen is not allowed, a key for the error message is returned.
 */
static const char* GetFullscreenError(CallerType aCallerType,
                                      Document* aDocument) {
  MOZ_ASSERT(aDocument);

  // Privileged callers can always request fullscreen
  if (aCallerType == CallerType::System) {
    return nullptr;
  }

  if (nsContentUtils::IsPDFJS(aDocument->GetPrincipal())) {
    // The built-in pdf viewer can always request fullscreen
    return nullptr;
  }

  if (const char* error = aDocument->GetFullscreenError(aCallerType)) {
    return error;
  }

  // Bypass user interaction checks if preference is set
  if (!StaticPrefs::full_screen_api_allow_trusted_requests_only()) {
    return nullptr;
  }

  if (!aDocument->ConsumeTransientUserGestureActivation()) {
    return "FullscreenDeniedNotInputDriven";
  }

  // Entering full-screen on mouse mouse event is only allowed with left mouse
  // button
  if (StaticPrefs::full_screen_api_mouse_event_allow_left_button_only() &&
      (EventStateManager::sCurrentMouseBtn == MouseButton::eMiddle ||
       EventStateManager::sCurrentMouseBtn == MouseButton::eSecondary)) {
    return "FullscreenDeniedMouseEventOnlyLeftBtn";
  }

  return nullptr;
}

void Element::SetCapture(bool aRetargetToElement) {
  // If there is already an active capture, ignore this request. This would
  // occur if a splitter, frame resizer, etc had already captured and we don't
  // want to override those.
  if (!PresShell::GetCapturingContent()) {
    PresShell::SetCapturingContent(
        this, CaptureFlags::PreventDragStart |
                  (aRetargetToElement ? CaptureFlags::RetargetToElement
                                      : CaptureFlags::None));
  }
}

void Element::SetCaptureAlways(bool aRetargetToElement) {
  PresShell::SetCapturingContent(
      this, CaptureFlags::PreventDragStart | CaptureFlags::IgnoreAllowedState |
                (aRetargetToElement ? CaptureFlags::RetargetToElement
                                    : CaptureFlags::None));
}

void Element::ReleaseCapture() {
  if (PresShell::GetCapturingContent() == this) {
    PresShell::ReleaseCapturingContent();
  }
}

already_AddRefed<Promise> Element::RequestFullscreen(CallerType aCallerType,
                                                     ErrorResult& aRv) {
  auto request = FullscreenRequest::Create(this, aCallerType, aRv);
  RefPtr<Promise> promise = request->GetPromise();

  // Only grant fullscreen requests if this is called from inside a trusted
  // event handler (i.e. inside an event handler for a user initiated event).
  // This stops the fullscreen from being abused similar to the popups of old,
  // and it also makes it harder for bad guys' script to go fullscreen and
  // spoof the browser chrome/window and phish logins etc.
  // Note that requests for fullscreen inside a web app's origin are exempt
  // from this restriction.
  if (const char* error = GetFullscreenError(aCallerType, OwnerDoc())) {
    request->Reject(error);
  } else {
    OwnerDoc()->RequestFullscreen(std::move(request));
  }
  return promise.forget();
}

void Element::RequestPointerLock(CallerType aCallerType) {
  PointerLockManager::RequestLock(this, aCallerType);
}

already_AddRefed<Flex> Element::GetAsFlexContainer() {
  // We need the flex frame to compute additional info, and use
  // that annotated version of the frame.
  nsFlexContainerFrame* flexFrame =
      nsFlexContainerFrame::GetFlexFrameWithComputedInfo(
          GetPrimaryFrame(FlushType::Layout));

  if (flexFrame) {
    RefPtr<Flex> flex = new Flex(this, flexFrame);
    return flex.forget();
  }
  return nullptr;
}

void Element::GetGridFragments(nsTArray<RefPtr<Grid>>& aResult) {
  nsGridContainerFrame* frame =
      nsGridContainerFrame::GetGridFrameWithComputedInfo(
          GetPrimaryFrame(FlushType::Layout));

  // If we get a nsGridContainerFrame from the prior call,
  // all the next-in-flow frames will also be nsGridContainerFrames.
  while (frame) {
    // Get the existing Grid object, if it exists. This object is
    // guaranteed to be up-to-date because GetGridFrameWithComputedInfo
    // will delete an existing one when regenerating grid info.
    Grid* gridFragment = frame->GetGridFragmentInfo();
    if (!gridFragment) {
      // Grid constructor will add itself as a property to frame, and
      // its unlink method will remove itself if the frame still exists.
      gridFragment = new Grid(this, frame);
    }
    aResult.AppendElement(gridFragment);
    frame = static_cast<nsGridContainerFrame*>(frame->GetNextInFlow());
  }
}

bool Element::HasGridFragments() {
  return !!nsGridContainerFrame::GetGridFrameWithComputedInfo(
      GetPrimaryFrame(FlushType::Layout));
}

already_AddRefed<DOMMatrixReadOnly> Element::GetTransformToAncestor(
    Element& aAncestor) {
  nsIFrame* primaryFrame = GetPrimaryFrame();
  nsIFrame* ancestorFrame = aAncestor.GetPrimaryFrame();

  Matrix4x4 transform;
  if (primaryFrame) {
    // If aAncestor is not actually an ancestor of this (including nullptr),
    // then the call to GetTransformToAncestor will return the transform
    // all the way up through the parent chain.
    transform = nsLayoutUtils::GetTransformToAncestor(RelativeTo{primaryFrame},
                                                      RelativeTo{ancestorFrame},
                                                      nsIFrame::IN_CSS_UNITS)
                    .GetMatrix();
  }

  DOMMatrixReadOnly* matrix = new DOMMatrix(this, transform);
  RefPtr<DOMMatrixReadOnly> result(matrix);
  return result.forget();
}

already_AddRefed<DOMMatrixReadOnly> Element::GetTransformToParent() {
  nsIFrame* primaryFrame = GetPrimaryFrame();

  Matrix4x4 transform;
  if (primaryFrame) {
    nsIFrame* parentFrame = primaryFrame->GetParent();
    transform = nsLayoutUtils::GetTransformToAncestor(RelativeTo{primaryFrame},
                                                      RelativeTo{parentFrame},
                                                      nsIFrame::IN_CSS_UNITS)
                    .GetMatrix();
  }

  DOMMatrixReadOnly* matrix = new DOMMatrix(this, transform);
  RefPtr<DOMMatrixReadOnly> result(matrix);
  return result.forget();
}

already_AddRefed<DOMMatrixReadOnly> Element::GetTransformToViewport() {
  nsIFrame* primaryFrame = GetPrimaryFrame();
  Matrix4x4 transform;
  if (primaryFrame) {
    transform =
        nsLayoutUtils::GetTransformToAncestor(
            RelativeTo{primaryFrame},
            RelativeTo{nsLayoutUtils::GetDisplayRootFrame(primaryFrame)},
            nsIFrame::IN_CSS_UNITS)
            .GetMatrix();
  }

  DOMMatrixReadOnly* matrix = new DOMMatrix(this, transform);
  RefPtr<DOMMatrixReadOnly> result(matrix);
  return result.forget();
}

already_AddRefed<Animation> Element::Animate(
    JSContext* aContext, JS::Handle<JSObject*> aKeyframes,
    const UnrestrictedDoubleOrKeyframeAnimationOptions& aOptions,
    ErrorResult& aError) {
  nsCOMPtr<nsIGlobalObject> ownerGlobal = GetOwnerGlobal();
  if (!ownerGlobal) {
    aError.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  GlobalObject global(aContext, ownerGlobal->GetGlobalJSObject());
  MOZ_ASSERT(!global.Failed());

  // KeyframeEffect constructor doesn't follow the standard Xray calling
  // convention and needs to be called in caller's compartment.
  // This should match to RunConstructorInCallerCompartment attribute in
  // KeyframeEffect.webidl.
  RefPtr<KeyframeEffect> effect =
      KeyframeEffect::Constructor(global, this, aKeyframes, aOptions, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // Animation constructor follows the standard Xray calling convention and
  // needs to be called in the target element's realm.
  JSAutoRealm ar(aContext, global.Get());

  AnimationTimeline* timeline = OwnerDoc()->Timeline();
  RefPtr<Animation> animation = Animation::Constructor(
      global, effect, Optional<AnimationTimeline*>(timeline), aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (aOptions.IsKeyframeAnimationOptions()) {
    animation->SetId(aOptions.GetAsKeyframeAnimationOptions().mId);
  }

  animation->Play(aError, Animation::LimitBehavior::AutoRewind);
  if (aError.Failed()) {
    return nullptr;
  }

  return animation.forget();
}

void Element::GetAnimations(const GetAnimationsOptions& aOptions,
                            nsTArray<RefPtr<Animation>>& aAnimations) {
  if (Document* doc = GetComposedDoc()) {
    // We don't need to explicitly flush throttled animations here, since
    // updating the animation style of elements will never affect the set of
    // running animations and it's only the set of running animations that is
    // important here.
    //
    // NOTE: Any changes to the flags passed to the following call should
    // be reflected in the flags passed in DocumentOrShadowRoot::GetAnimations
    // too.
    doc->FlushPendingNotifications(
        ChangesToFlush(FlushType::Style, /* aFlushAnimations = */ false,
                       /* aUpdateRelevancy = */ false));
  }

  GetAnimationsWithoutFlush(aOptions, aAnimations);
}

static void GetAnimationsUnsorted(const Element* aElement,
                                  const PseudoStyleRequest& aPseudoRequest,
                                  nsTArray<RefPtr<Animation>>& aAnimations) {
  MOZ_ASSERT(aPseudoRequest.IsNotPseudo() ||
                 AnimationUtils::IsSupportedPseudoForAnimations(aPseudoRequest),
             "Unsupported pseudo type");
  MOZ_ASSERT(aElement, "Null element");

  EffectSet* effects = EffectSet::Get(aElement, aPseudoRequest);
  if (!effects) {
    return;
  }

  for (KeyframeEffect* effect : *effects) {
    MOZ_ASSERT(effect && effect->GetAnimation(),
               "Only effects associated with an animation should be "
               "added to an element's effect set");
    Animation* animation = effect->GetAnimation();

    MOZ_ASSERT(animation->IsRelevant(),
               "Only relevant animations should be added to an element's "
               "effect set");
    aAnimations.AppendElement(animation);
  }
}

// This traverses all the view transition pseudo-elements and get all of the
// animations on them.
static void GetAnimationsOfViewTransitionTree(
    const Element* aOriginatingElement,
    nsTArray<RefPtr<Animation>>& aAnimations) {
  if (!aOriginatingElement->IsRootElement()) {
    return;
  }

  const Document* doc = aOriginatingElement->OwnerDoc();
  const ViewTransition* vt = doc->GetActiveViewTransition();
  if (!vt) {
    return;
  }

  Element* root = vt->GetRoot();
  if (!root) {
    return;
  }

  for (const nsIContent* node = root; node; node = node->GetNextNode(root)) {
    if (!node->IsElement()) {
      continue;
    }
    const Element* pseudo = node->AsElement();
    const PseudoStyleRequest request(
        pseudo->GetPseudoElementType(),
        pseudo->HasName()
            ? pseudo->GetParsedAttr(nsGkAtoms::name)->GetAtomValue()
            : nullptr);
    GetAnimationsUnsorted(aOriginatingElement, request, aAnimations);
  }
}

void Element::GetAnimationsWithoutFlush(
    const GetAnimationsOptions& aOptions,
    nsTArray<RefPtr<Animation>>& aAnimations) {
  Element* elem = this;
  PseudoStyleRequest pseudoRequest;
  // For animations on generated-content elements, the animations are stored
  // on the parent element.
  if (IsGeneratedContentContainerForBefore()) {
    elem = GetParentElement();
    pseudoRequest.mType = PseudoStyleType::before;
  } else if (IsGeneratedContentContainerForAfter()) {
    elem = GetParentElement();
    pseudoRequest.mType = PseudoStyleType::after;
  } else if (IsGeneratedContentContainerForMarker()) {
    elem = GetParentElement();
    pseudoRequest.mType = PseudoStyleType::marker;
  }

  if (!elem) {
    return;
  }

  // FIXME: Bug 1935557. Rewrite this to support pseudoElement option.
  if (!aOptions.mSubtree || (pseudoRequest.mType == PseudoStyleType::before ||
                             pseudoRequest.mType == PseudoStyleType::after ||
                             pseudoRequest.mType == PseudoStyleType::marker)) {
    GetAnimationsUnsorted(elem, pseudoRequest, aAnimations);
  } else {
    for (nsIContent* node = this; node; node = node->GetNextNode(this)) {
      if (!node->IsElement()) {
        continue;
      }
      Element* element = node->AsElement();
      GetAnimationsUnsorted(element, PseudoStyleRequest::NotPseudo(),
                            aAnimations);
      GetAnimationsUnsorted(element, PseudoStyleRequest::Before(), aAnimations);
      GetAnimationsUnsorted(element, PseudoStyleRequest::After(), aAnimations);
      GetAnimationsUnsorted(element, PseudoStyleRequest::Marker(), aAnimations);
    }
    GetAnimationsOfViewTransitionTree(this, aAnimations);
  }
  aAnimations.Sort(AnimationPtrComparator<RefPtr<Animation>>());
}

void Element::CloneAnimationsFrom(const Element& aOther) {
  AnimationTimeline* const timeline = OwnerDoc()->Timeline();
  MOZ_ASSERT(timeline, "Timeline has not been set on the document yet");
  // Iterate through all pseudo types and copy the effects from each of the
  // other element's effect sets into this element's effect set.
  // FIXME: Bug 1929470. This funciton is for printing, and it may be tricky to
  // support view transitions. We have to revisit here after we support view
  // transitions to make sure we clone the animations properly.
  for (PseudoStyleType pseudoType :
       {PseudoStyleType::NotPseudo, PseudoStyleType::before,
        PseudoStyleType::after, PseudoStyleType::marker}) {
    // If the element has an effect set for this pseudo type (or not pseudo)
    // then copy the effects and animation properties.
    const PseudoStyleRequest request(pseudoType);
    if (auto* const effects = EffectSet::Get(&aOther, request)) {
      auto* const clonedEffects = EffectSet::GetOrCreate(this, request);
      for (KeyframeEffect* const effect : *effects) {
        auto* animation = effect->GetAnimation();
        if (animation->AsCSSTransition()) {
          // Don't clone transitions, for compat with other browsers.
          continue;
        }
        // Clone the effect.
        RefPtr<KeyframeEffect> clonedEffect = new KeyframeEffect(
            OwnerDoc(), OwningAnimationTarget{this, request}, *effect);

        // Clone the animation
        RefPtr<Animation> clonedAnimation = Animation::ClonePausedAnimation(
            OwnerDoc()->GetParentObject(), *animation, *clonedEffect,
            *timeline);
        if (!clonedAnimation) {
          continue;
        }
        clonedEffects->AddEffect(*clonedEffect);
      }
    }
  }
}

void Element::GetInnerHTML(nsAString& aInnerHTML, OOMReporter& aError) {
  GetMarkup(false, aInnerHTML);
}

void Element::GetInnerHTML(OwningTrustedHTMLOrNullIsEmptyString& aInnerHTML,
                           OOMReporter& aError) {
  GetInnerHTML(aInnerHTML.SetAsNullIsEmptyString(), aError);
}

void Element::SetInnerHTML(const TrustedHTMLOrNullIsEmptyString& aInnerHTML,
                           nsIPrincipal* aSubjectPrincipal,
                           ErrorResult& aError) {
  constexpr nsLiteralString sink = u"Element innerHTML"_ns;

  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aInnerHTML, sink, kTrustedTypesOnlySinkGroup, *this,
          compliantStringHolder, aError);

  if (aError.Failed()) {
    return;
  }

  SetInnerHTMLTrusted(*compliantString, aSubjectPrincipal, aError);
}

void Element::SetInnerHTMLTrusted(const nsAString& aInnerHTML,
                                  nsIPrincipal* aSubjectPrincipal,
                                  ErrorResult& aError) {
  SetInnerHTMLInternal(aInnerHTML, aError);
}

void Element::GetOuterHTML(OwningTrustedHTMLOrNullIsEmptyString& aOuterHTML) {
  GetMarkup(true, aOuterHTML.SetAsNullIsEmptyString());
}

void Element::SetOuterHTML(const TrustedHTMLOrNullIsEmptyString& aOuterHTML,
                           ErrorResult& aError) {
  constexpr nsLiteralString sink = u"Element outerHTML"_ns;

  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aOuterHTML, sink, kTrustedTypesOnlySinkGroup, *this,
          compliantStringHolder, aError);
  if (aError.Failed()) {
    return;
  }

  nsCOMPtr<nsINode> parent = GetParentNode();
  if (!parent) {
    return;
  }

  if (parent->NodeType() == DOCUMENT_NODE) {
    aError.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
    return;
  }

  if (OwnerDoc()->IsHTMLDocument()) {
    nsAtom* localName;
    int32_t namespaceID;
    if (parent->IsElement()) {
      localName = parent->NodeInfo()->NameAtom();
      namespaceID = parent->NodeInfo()->NamespaceID();
    } else {
      NS_ASSERTION(
          parent->NodeType() == DOCUMENT_FRAGMENT_NODE,
          "How come the parent isn't a document, a fragment or an element?");
      localName = nsGkAtoms::body;
      namespaceID = kNameSpaceID_XHTML;
    }
    RefPtr<DocumentFragment> fragment = new (OwnerDoc()->NodeInfoManager())
        DocumentFragment(OwnerDoc()->NodeInfoManager());
    nsContentUtils::ParseFragmentHTML(
        *compliantString, fragment, localName, namespaceID,
        OwnerDoc()->GetCompatibilityMode() == eCompatibility_NavQuirks, true);
    parent->ReplaceChild(*fragment, *this, aError);
    return;
  }

  nsCOMPtr<nsINode> context;
  if (parent->IsElement()) {
    context = parent;
  } else {
    NS_ASSERTION(
        parent->NodeType() == DOCUMENT_FRAGMENT_NODE,
        "How come the parent isn't a document, a fragment or an element?");
    RefPtr<mozilla::dom::NodeInfo> info =
        OwnerDoc()->NodeInfoManager()->GetNodeInfo(
            nsGkAtoms::body, nullptr, kNameSpaceID_XHTML, ELEMENT_NODE);
    context = NS_NewHTMLBodyElement(info.forget(), FROM_PARSER_FRAGMENT);
  }

  RefPtr<DocumentFragment> fragment = nsContentUtils::CreateContextualFragment(
      context, *compliantString, true, aError);
  if (aError.Failed()) {
    return;
  }
  parent->ReplaceChild(*fragment, *this, aError);
}

enum nsAdjacentPosition { eBeforeBegin, eAfterBegin, eBeforeEnd, eAfterEnd };

void Element::InsertAdjacentHTML(
    const nsAString& aPosition, const TrustedHTMLOrString& aTrustedHTMLOrString,
    ErrorResult& aError) {
  constexpr nsLiteralString kSink = u"Element insertAdjacentHTML"_ns;

  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aTrustedHTMLOrString, kSink, kTrustedTypesOnlySinkGroup, *this,
          compliantStringHolder, aError);

  if (aError.Failed()) {
    return;
  }

  nsAdjacentPosition position;
  if (aPosition.LowerCaseEqualsLiteral("beforebegin")) {
    position = eBeforeBegin;
  } else if (aPosition.LowerCaseEqualsLiteral("afterbegin")) {
    position = eAfterBegin;
  } else if (aPosition.LowerCaseEqualsLiteral("beforeend")) {
    position = eBeforeEnd;
  } else if (aPosition.LowerCaseEqualsLiteral("afterend")) {
    position = eAfterEnd;
  } else {
    aError.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return;
  }

  nsCOMPtr<nsIContent> destination;
  if (position == eBeforeBegin || position == eAfterEnd) {
    destination = GetParent();
    if (!destination) {
      aError.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
      return;
    }
  } else {
    destination = this;
  }

  // mozAutoDocUpdate keeps the owner document alive.  Therefore, using a raw
  // pointer here is safe.
  Document* const doc = OwnerDoc();

  // Needed when insertAdjacentHTML is used in combination with contenteditable
  mozAutoDocUpdate updateBatch(doc, true);
  nsAutoScriptLoaderDisabler sld(doc);

  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(doc, nullptr);

  // Parse directly into destination if possible
  if (doc->IsHTMLDocument() && !OwnerDoc()->MayHaveDOMMutationObservers() &&
      (position == eBeforeEnd || (position == eAfterEnd && !GetNextSibling()) ||
       (position == eAfterBegin && !GetFirstChild()))) {
    int32_t oldChildCount = destination->GetChildCount();
    int32_t contextNs = destination->GetNameSpaceID();
    nsAtom* contextLocal = destination->NodeInfo()->NameAtom();
    if (contextLocal == nsGkAtoms::html && contextNs == kNameSpaceID_XHTML) {
      // For compat with IE6 through IE9. Willful violation of HTML5 as of
      // 2011-04-06. CreateContextualFragment does the same already.
      // Spec bug: http://www.w3.org/Bugs/Public/show_bug.cgi?id=12434
      contextLocal = nsGkAtoms::body;
    }
    aError = nsContentUtils::ParseFragmentHTML(
        *compliantString, destination, contextLocal, contextNs,
        doc->GetCompatibilityMode() == eCompatibility_NavQuirks, true);
    // HTML5 parser has notified, but not fired mutation events.
    nsContentUtils::FireMutationEventsForDirectParsing(doc, destination,
                                                       oldChildCount);
    return;
  }

  // couldn't parse directly
  RefPtr<DocumentFragment> fragment = nsContentUtils::CreateContextualFragment(
      destination, *compliantString, true, aError);
  if (aError.Failed()) {
    return;
  }

  // Suppress assertion about node removal mutation events that can't have
  // listeners anyway, because no one has had the chance to register mutation
  // listeners on the fragment that comes from the parser.
  nsAutoScriptBlockerSuppressNodeRemoved scriptBlocker;

  switch (position) {
    case eBeforeBegin:
      destination->InsertBefore(*fragment, this, aError);
      break;
    case eAfterBegin:
      static_cast<nsINode*>(this)->InsertBefore(*fragment, GetFirstChild(),
                                                aError);
      break;
    case eBeforeEnd:
      static_cast<nsINode*>(this)->AppendChild(*fragment, aError);
      break;
    case eAfterEnd:
      destination->InsertBefore(*fragment, GetNextSibling(), aError);
      break;
  }
}

nsINode* Element::InsertAdjacent(const nsAString& aWhere, nsINode* aNode,
                                 ErrorResult& aError) {
  if (aWhere.LowerCaseEqualsLiteral("beforebegin")) {
    nsCOMPtr<nsINode> parent = GetParentNode();
    if (!parent) {
      return nullptr;
    }
    parent->InsertBefore(*aNode, this, aError);
  } else if (aWhere.LowerCaseEqualsLiteral("afterbegin")) {
    nsCOMPtr<nsINode> refNode = GetFirstChild();
    static_cast<nsINode*>(this)->InsertBefore(*aNode, refNode, aError);
  } else if (aWhere.LowerCaseEqualsLiteral("beforeend")) {
    static_cast<nsINode*>(this)->AppendChild(*aNode, aError);
  } else if (aWhere.LowerCaseEqualsLiteral("afterend")) {
    nsCOMPtr<nsINode> parent = GetParentNode();
    if (!parent) {
      return nullptr;
    }
    nsCOMPtr<nsINode> refNode = GetNextSibling();
    parent->InsertBefore(*aNode, refNode, aError);
  } else {
    aError.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return nullptr;
  }

  return aError.Failed() ? nullptr : aNode;
}

Element* Element::InsertAdjacentElement(const nsAString& aWhere,
                                        Element& aElement,
                                        ErrorResult& aError) {
  nsINode* newNode = InsertAdjacent(aWhere, &aElement, aError);
  MOZ_ASSERT(!newNode || newNode->IsElement());

  return newNode ? newNode->AsElement() : nullptr;
}

void Element::InsertAdjacentText(const nsAString& aWhere,
                                 const nsAString& aData, ErrorResult& aError) {
  RefPtr<nsTextNode> textNode = OwnerDoc()->CreateTextNode(aData);
  InsertAdjacent(aWhere, textNode, aError);
}

TextEditor* Element::GetTextEditorInternal() {
  TextControlElement* textControlElement = TextControlElement::FromNode(this);
  return textControlElement ? MOZ_KnownLive(textControlElement)->GetTextEditor()
                            : nullptr;
}

nsresult Element::SetBoolAttr(nsAtom* aAttr, bool aValue) {
  if (aValue) {
    return SetAttr(kNameSpaceID_None, aAttr, u""_ns, true);
  }

  return UnsetAttr(kNameSpaceID_None, aAttr, true);
}

void Element::GetEnumAttr(nsAtom* aAttr, const char* aDefault,
                          nsAString& aResult) const {
  GetEnumAttr(aAttr, aDefault, aDefault, aResult);
}

void Element::GetEnumAttr(nsAtom* aAttr, const char* aDefaultMissing,
                          const char* aDefaultInvalid,
                          nsAString& aResult) const {
  const nsAttrValue* attrVal = mAttrs.GetAttr(aAttr);

  aResult.Truncate();

  if (!attrVal) {
    if (aDefaultMissing) {
      AppendASCIItoUTF16(nsDependentCString(aDefaultMissing), aResult);
    } else {
      SetDOMStringToNull(aResult);
    }
  } else {
    if (attrVal->Type() == nsAttrValue::eEnum) {
      attrVal->GetEnumString(aResult, true);
    } else if (aDefaultInvalid) {
      AppendASCIItoUTF16(nsDependentCString(aDefaultInvalid), aResult);
    }
  }
}

void Element::SetOrRemoveNullableStringAttr(nsAtom* aName,
                                            const nsAString& aValue,
                                            ErrorResult& aError) {
  if (DOMStringIsNull(aValue)) {
    UnsetAttr(aName, aError);
  } else {
    SetAttr(aName, aValue, aError);
  }
}

Directionality Element::GetComputedDirectionality() const {
  if (nsIFrame* frame = GetPrimaryFrame()) {
    return frame->StyleVisibility()->mDirection == StyleDirection::Ltr
               ? Directionality::Ltr
               : Directionality::Rtl;
  }

  return GetDirectionality();
}

float Element::FontSizeInflation() {
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return -1.0;
  }

  if (nsLayoutUtils::FontSizeInflationEnabled(frame->PresContext())) {
    return nsLayoutUtils::FontSizeInflationFor(frame);
  }

  return 1.0;
}

void Element::GetImplementedPseudoElement(nsAString& aPseudo) const {
  PseudoStyleType pseudoType = GetPseudoElementType();
  if (pseudoType == PseudoStyleType::NotPseudo) {
    return SetDOMStringToNull(aPseudo);
  }
  nsDependentAtomString pseudo(nsCSSPseudoElements::GetPseudoAtom(pseudoType));

  // We want to use the modern syntax (::placeholder, etc), but the atoms only
  // contain one semi-colon.
  MOZ_ASSERT(pseudo.Length() > 2 && pseudo[0] == ':' && pseudo[1] != ':');

  aPseudo.Truncate();
  aPseudo.SetCapacity(pseudo.Length() + 1);
  aPseudo.Append(':');
  aPseudo.Append(pseudo);
}

// This function traverses the view transition pseudo-elements tree and finds
// the pseudo-element matched with |aRequest|.
static Element* SearchViewTransitionPseudo(const Element* aElement,
                                           const PseudoStyleRequest& aRequest) {
  // If |aElement| is not the root.
  if (!aElement->IsRootElement()) {
    return nullptr;
  }

  const Document* doc = aElement->OwnerDoc();
  const ViewTransition* vt = doc->GetActiveViewTransition();
  if (!vt) {
    return nullptr;
  }

  return vt->FindPseudo(aRequest);
}

Element* Element::GetPseudoElement(const PseudoStyleRequest& aRequest) const {
  switch (aRequest.mType) {
    case PseudoStyleType::NotPseudo:
      // It's unfortunate we have to do const cast, so we don't have to write
      // the almost duplicate function for the non-const function.
      return const_cast<Element*>(this);
    case PseudoStyleType::before:
      return nsLayoutUtils::GetBeforePseudo(this);
    case PseudoStyleType::after:
      return nsLayoutUtils::GetAfterPseudo(this);
    case PseudoStyleType::marker:
      return nsLayoutUtils::GetMarkerPseudo(this);
    case PseudoStyleType::viewTransition:
    case PseudoStyleType::viewTransitionGroup:
    case PseudoStyleType::viewTransitionImagePair:
    case PseudoStyleType::viewTransitionOld:
    case PseudoStyleType::viewTransitionNew: {
      Element* result = SearchViewTransitionPseudo(this, aRequest);
      MOZ_ASSERT(!result || result->GetPseudoElementType() == aRequest.mType,
                 "The type should match");
      MOZ_ASSERT(!result || !result->HasName() ||
                     result->GetParsedAttr(nsGkAtoms::name)->GetAtomValue() ==
                         aRequest.mIdentifier,
                 "The identifier should match");
      return result;
    }
    default:
      return nullptr;
  }
}

ReferrerPolicy Element::GetReferrerPolicyAsEnum() const {
  if (IsHTMLElement()) {
    return ReferrerPolicyFromAttr(GetParsedAttr(nsGkAtoms::referrerpolicy));
  }
  return ReferrerPolicy::_empty;
}

ReferrerPolicy Element::ReferrerPolicyFromAttr(
    const nsAttrValue* aValue) const {
  if (aValue && aValue->Type() == nsAttrValue::eEnum) {
    return ReferrerPolicy(aValue->GetEnumValue());
  }
  return ReferrerPolicy::_empty;
}

already_AddRefed<nsDOMStringMap> Element::Dataset() {
  nsExtendedDOMSlots* slots = ExtendedDOMSlots();
  if (!slots->mDataset) {
    // mDataset is a weak reference so assignment will not AddRef.
    // AddRef is called before returning the pointer.
    slots->mDataset = new nsDOMStringMap(this);
  }
  return do_AddRef(slots->mDataset);
}

void Element::ClearDataset() {
  nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  MOZ_ASSERT(slots && slots->mDataset,
             "Slots should exist and dataset should not be null.");
  slots->mDataset = nullptr;
}

template <class T>
void Element::GetCustomInterface(nsGetterAddRefs<T> aResult) {
  nsCOMPtr<nsISupports> iface = CustomElementRegistry::CallGetCustomInterface(
      this, NS_GET_TEMPLATE_IID(T));
  if (iface) {
    if (NS_SUCCEEDED(CallQueryInterface(iface, static_cast<T**>(aResult)))) {
      return;
    }
  }
}

void Element::ClearServoData(Document* aDoc) {
  MOZ_ASSERT(aDoc);
  if (HasServoData()) {
    Servo_Element_ClearData(this);
  } else {
    UnsetFlags(kAllServoDescendantBits | NODE_NEEDS_FRAME);
  }
  // Since this element is losing its servo data, nothing under it may have
  // servo data either, so we can forget restyles rooted at this element. This
  // is necessary for correctness, since we invoke ClearServoData in various
  // places where an element's flattened tree parent changes, and such a change
  // may also make an element invalid to be used as a restyle root.
  if (aDoc->GetServoRestyleRoot() == this) {
    aDoc->ClearServoRestyleRoot();
  }
}

bool Element::IsAutoPopover() const {
  const auto* htmlElement = nsGenericHTMLElement::FromNode(this);
  return htmlElement &&
         htmlElement->GetPopoverAttributeState() == PopoverAttributeState::Auto;
}

bool Element::IsPopoverOpen() const {
  const auto* htmlElement = nsGenericHTMLElement::FromNode(this);
  return htmlElement && htmlElement->PopoverOpen();
}

Element* Element::GetTopmostPopoverAncestor(const Element* aInvoker,
                                            bool isPopover) const {
  const Element* newPopover = this;

  nsTHashMap<nsPtrHashKey<const Element>, size_t> popoverPositions;
  size_t index = 0;
  for (Element* popover : OwnerDoc()->AutoPopoverList()) {
    popoverPositions.LookupOrInsert(popover, index++);
  }

  if (isPopover) {
    popoverPositions.LookupOrInsert(newPopover, index);
  }

  Element* topmostPopoverAncestor = nullptr;

  auto checkAncestor = [&](const Element* candidate) {
    if (!candidate) {
      return;
    }
    Element* candidateAncestor = candidate->GetNearestInclusiveOpenPopover();
    if (!candidateAncestor) {
      return;
    }
    size_t candidatePosition;
    if (popoverPositions.Get(candidateAncestor, &candidatePosition)) {
      size_t topmostPosition;
      if (!topmostPopoverAncestor ||
          (popoverPositions.Get(topmostPopoverAncestor, &topmostPosition) &&
           topmostPosition < candidatePosition)) {
        topmostPopoverAncestor = candidateAncestor;
      }
    }
  };

  checkAncestor(newPopover->GetFlattenedTreeParentElement());
  checkAncestor(aInvoker);

  return topmostPopoverAncestor;
}

ElementAnimationData& Element::CreateAnimationData() {
  MOZ_ASSERT(!GetAnimationData());
  SetMayHaveAnimations();
  auto* slots = ExtendedDOMSlots();
  slots->mAnimations = MakeUnique<ElementAnimationData>();
  return *slots->mAnimations;
}

PopoverData& Element::CreatePopoverData() {
  MOZ_ASSERT(!GetPopoverData());
  auto* slots = ExtendedDOMSlots();
  slots->mPopoverData = MakeUnique<PopoverData>();
  return *slots->mPopoverData;
}

void Element::ClearPopoverData() {
  nsExtendedDOMSlots* slots = GetExistingExtendedDOMSlots();
  if (slots) {
    slots->mPopoverData = nullptr;
  }
}

void Element::SetCustomElementData(UniquePtr<CustomElementData> aData) {
  SetHasCustomElementData();

  if (aData->mState != CustomElementData::State::eCustom) {
    SetDefined(false);
  }

  nsExtendedDOMSlots* slots = ExtendedDOMSlots();
  MOZ_ASSERT(!slots->mCustomElementData,
             "Custom element data may not be changed once set.");
#if DEBUG
  // We assert only XUL usage, since web may pass whatever as 'is' value
  if (NodeInfo()->NamespaceID() == kNameSpaceID_XUL) {
    nsAtom* name = NodeInfo()->NameAtom();
    nsAtom* type = aData->GetCustomElementType();
    // Check to see if the tag name is a dashed name.
    if (nsContentUtils::IsNameWithDash(name)) {
      // Assert that a tag name with dashes is always an autonomous custom
      // element.
      MOZ_ASSERT(type == name);
    } else {
      // Could still be an autonomous custom element with a non-dashed tag name.
      // Need the check below for sure.
      if (type != name) {
        // Assert that the name of the built-in custom element type is always
        // a dashed name.
        MOZ_ASSERT(nsContentUtils::IsNameWithDash(type));
      }
    }
  }
#endif
  slots->mCustomElementData = std::move(aData);
}

nsTArray<RefPtr<nsAtom>>& Element::EnsureCustomStates() {
  MOZ_ASSERT(IsHTMLElement());
  nsExtendedDOMSlots* slots = ExtendedDOMSlots();
  return slots->mCustomStates;
}

CustomElementDefinition* Element::GetCustomElementDefinition() const {
  CustomElementData* data = GetCustomElementData();
  if (!data) {
    return nullptr;
  }

  return data->GetCustomElementDefinition();
}

void Element::SetCustomElementDefinition(CustomElementDefinition* aDefinition) {
  CustomElementData* data = GetCustomElementData();
  MOZ_ASSERT(data);

  data->SetCustomElementDefinition(aDefinition);
}

already_AddRefed<nsIDOMXULButtonElement> Element::AsXULButton() {
  nsCOMPtr<nsIDOMXULButtonElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULContainerElement> Element::AsXULContainer() {
  nsCOMPtr<nsIDOMXULContainerElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULContainerItemElement> Element::AsXULContainerItem() {
  nsCOMPtr<nsIDOMXULContainerItemElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULControlElement> Element::AsXULControl() {
  nsCOMPtr<nsIDOMXULControlElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULMenuListElement> Element::AsXULMenuList() {
  nsCOMPtr<nsIDOMXULMenuListElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULMultiSelectControlElement>
Element::AsXULMultiSelectControl() {
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULRadioGroupElement> Element::AsXULRadioGroup() {
  nsCOMPtr<nsIDOMXULRadioGroupElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULRelatedElement> Element::AsXULRelated() {
  nsCOMPtr<nsIDOMXULRelatedElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULSelectControlElement> Element::AsXULSelectControl() {
  nsCOMPtr<nsIDOMXULSelectControlElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIDOMXULSelectControlItemElement>
Element::AsXULSelectControlItem() {
  nsCOMPtr<nsIDOMXULSelectControlItemElement> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIBrowser> Element::AsBrowser() {
  nsCOMPtr<nsIBrowser> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

already_AddRefed<nsIAutoCompletePopup> Element::AsAutoCompletePopup() {
  nsCOMPtr<nsIAutoCompletePopup> value;
  GetCustomInterface(getter_AddRefs(value));
  return value.forget();
}

nsPresContext* Element::GetPresContext(PresContextFor aFor) {
  // Get the document
  Document* doc =
      (aFor == eForComposedDoc) ? GetComposedDoc() : GetUncomposedDoc();
  if (doc) {
    return doc->GetPresContext();
  }

  return nullptr;
}

MOZ_DEFINE_MALLOC_SIZE_OF(ServoElementMallocSizeOf)
MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(ServoElementMallocEnclosingSizeOf)

void Element::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                     size_t* aNodeSize) const {
  FragmentOrElement::AddSizeOfExcludingThis(aSizes, aNodeSize);
  *aNodeSize += mAttrs.SizeOfExcludingThis(aSizes.mState.mMallocSizeOf);

  if (HasServoData()) {
    // Measure the ElementData object itself.
    aSizes.mLayoutElementDataObjects +=
        aSizes.mState.mMallocSizeOf(mServoData.Get());

    // Measure mServoData, excluding the ComputedValues. This measurement
    // counts towards the element's size. We use ServoElementMallocSizeOf and
    // ServoElementMallocEnclosingSizeOf rather than |aState.mMallocSizeOf| to
    // better distinguish in DMD's output the memory measured within Servo
    // code.
    *aNodeSize += Servo_Element_SizeOfExcludingThisAndCVs(
        ServoElementMallocSizeOf, ServoElementMallocEnclosingSizeOf,
        &aSizes.mState.mSeenPtrs, this);

    // Now measure just the ComputedValues (and style structs) under
    // mServoData. This counts towards the relevant fields in |aSizes|.
    if (auto* style = Servo_Element_GetMaybeOutOfDateStyle(this)) {
      if (!aSizes.mState.HaveSeenPtr(style)) {
        style->AddSizeOfIncludingThis(aSizes, &aSizes.mLayoutComputedValuesDom);
      }

      for (size_t i = 0; i < PseudoStyle::kEagerPseudoCount; i++) {
        if (auto* style = Servo_Element_GetMaybeOutOfDatePseudoStyle(this, i)) {
          if (!aSizes.mState.HaveSeenPtr(style)) {
            style->AddSizeOfIncludingThis(aSizes,
                                          &aSizes.mLayoutComputedValuesDom);
          }
        }
      }
    }
  }
}

#ifdef DEBUG
static bool BitsArePropagated(const Element* aElement, uint32_t aBits,
                              nsINode* aRestyleRoot) {
  const Element* curr = aElement;
  while (curr) {
    if (curr == aRestyleRoot) {
      return true;
    }
    if (!curr->HasAllFlags(aBits)) {
      return false;
    }
    nsINode* parentNode = curr->GetParentNode();
    curr = curr->GetFlattenedTreeParentElementForStyle();
    MOZ_ASSERT_IF(!curr,
                  parentNode == aElement->OwnerDoc() ||
                      parentNode == parentNode->OwnerDoc()->GetRootElement());
  }
  return true;
}
#endif

static inline void AssertNoBitsPropagatedFrom(nsINode* aRoot) {
#ifdef DEBUG
  if (!aRoot || !aRoot->IsElement()) {
    return;
  }

  auto* element = aRoot->GetFlattenedTreeParentElementForStyle();
  while (element) {
    MOZ_ASSERT(!element->HasAnyOfFlags(Element::kAllServoDescendantBits));
    element = element->GetFlattenedTreeParentElementForStyle();
  }
#endif
}

// Sets `aBits` on `aElement` and all of its flattened-tree ancestors up to and
// including aStopAt or the root element (whichever is encountered first), and
// as long as `aBitsToStopAt` isn't found anywhere in the chain.
static inline Element* PropagateBits(Element* aElement, uint32_t aBits,
                                     nsINode* aStopAt, uint32_t aBitsToStopAt) {
  Element* curr = aElement;
  while (curr && !curr->HasAllFlags(aBitsToStopAt)) {
    curr->SetFlags(aBits);
    if (curr == aStopAt) {
      break;
    }
    curr = curr->GetFlattenedTreeParentElementForStyle();
  }

  if (aBitsToStopAt != aBits && curr) {
    curr->SetFlags(aBits);
  }

  return curr;
}

// Notes that a given element is "dirty" with respect to the given descendants
// bit (which may be one of dirty descendants, dirty animation descendants, or
// need frame construction for descendants).
//
// This function operates on the dirty element itself, despite the fact that the
// bits are generally used to describe descendants. This allows restyle roots
// to be scoped as tightly as possible. On the first call to NoteDirtyElement
// since the last restyle, we don't set any descendant bits at all, and just set
// the element as the restyle root.
//
// Because the style traversal handles multiple tasks (styling,
// animation-ticking, and lazy frame construction), there are potentially three
// separate kinds of dirtiness to track. Rather than maintaining three separate
// restyle roots, we use a single root, and always bubble it up to be the
// nearest common ancestor of all the dirty content in the tree. This means that
// we need to track the types of dirtiness that the restyle root corresponds to,
// so SetServoRestyleRoot accepts a bitfield along with an element.
//
// The overall algorithm is as follows:
// * When the first dirty element is noted, we just set as the restyle root.
// * When additional dirty elements are noted, we propagate the given bit up
//   the tree, until we either reach the restyle root or the document root.
// * If we reach the document root, we then propagate the bits associated with
//   the restyle root up the tree until we cross the path of the new root. Once
//   we find this common ancestor, we record it as the restyle root, and then
//   clear the bits between the new restyle root and the document root.
// * If we have dirty content beneath multiple "document style traversal roots"
//   (which are the main DOM + each piece of document-level native-anoymous
//   content), we set the restyle root to the nsINode of the document itself.
//   This is the bail-out case where we traverse everything.
//
// Note that, since we track a root, we try to optimize the case where an
// element under the current root is dirtied, that's why we don't trivially use
// `nsContentUtils::GetCommonFlattenedTreeAncestorForStyle`.
static void NoteDirtyElement(Element* aElement, uint32_t aBits) {
  MOZ_ASSERT(aElement->IsInComposedDoc());

  // Check the existing root early on, since it may allow us to short-circuit
  // before examining the parent chain.
  Document* doc = aElement->GetComposedDoc();
  nsINode* existingRoot = doc->GetServoRestyleRoot();
  if (existingRoot == aElement) {
    doc->SetServoRestyleRootDirtyBits(doc->GetServoRestyleRootDirtyBits() |
                                      aBits);
    return;
  }

  nsINode* parent = aElement->GetFlattenedTreeParentNodeForStyle();
  if (!parent) {
    // The element is not in the flattened tree, bail.
    return;
  }

  if (MOZ_LIKELY(parent->IsElement())) {
    // If our parent is unstyled, we can inductively assume that it will be
    // traversed when the time is right, and that the traversal will reach us
    // when it happens. Nothing left to do.
    if (!parent->AsElement()->HasServoData()) {
      return;
    }

    // Similarly, if our parent already has the bit we're propagating, we can
    // assume everything is already set up.
    if (parent->HasAllFlags(aBits)) {
      return;
    }

    // If the parent is styled but is display:none, we're done.
    //
    // We can't check for a frame here, since <frame> elements inside <frameset>
    // still need to generate a frame, even if they're display: none. :(
    //
    // The servo traversal doesn't keep style data under display: none subtrees,
    // so in order for it to not need to cleanup each time anything happens in a
    // display: none subtree, we keep it clean.
    //
    // Also, we can't be much more smarter about using the parent's frame in
    // order to avoid work here, because since the style system keeps style data
    // in, e.g., subtrees under a leaf frame, missing restyles and such in there
    // has observable behavior via getComputedStyle, for example.
    if (Servo_Element_IsDisplayNone(parent->AsElement())) {
      return;
    }
  }

  if (PresShell* presShell = doc->GetPresShell()) {
    presShell->EnsureStyleFlush();
  }

  MOZ_ASSERT(parent->IsElement() || parent == doc);

  // The bit checks below rely on this to arrive to useful conclusions about the
  // shape of the tree.
  AssertNoBitsPropagatedFrom(existingRoot);

  // If there's no existing restyle root, or if the root is already aElement,
  // just note the root+bits and return.
  if (!existingRoot) {
    doc->SetServoRestyleRoot(aElement, aBits);
    return;
  }

  // There is an existing restyle root - walk up the tree from our element,
  // propagating bits as we go.
  const bool reachedDocRoot =
      !parent->IsElement() ||
      !PropagateBits(parent->AsElement(), aBits, existingRoot, aBits);

  uint32_t existingBits = doc->GetServoRestyleRootDirtyBits();
  if (!reachedDocRoot || existingRoot == doc) {
    // We're a descendant of the existing root. All that's left to do is to
    // make sure the bit we propagated is also registered on the root.
    doc->SetServoRestyleRoot(existingRoot, existingBits | aBits);
  } else {
    // We reached the root without crossing the pre-existing restyle root. We
    // now need to find the nearest common ancestor, so climb up from the
    // existing root, extending bits along the way.
    Element* rootParent = existingRoot->GetFlattenedTreeParentElementForStyle();
    // We can stop at the first occurrence of `aBits` in order to find the
    // common ancestor.
    if (Element* commonAncestor =
            PropagateBits(rootParent, existingBits, aElement, aBits)) {
      MOZ_ASSERT(commonAncestor == aElement ||
                 commonAncestor ==
                     nsContentUtils::GetCommonFlattenedTreeAncestorForStyle(
                         aElement, rootParent));

      // We found a common ancestor. Make that the new style root, and clear the
      // bits between the new style root and the document root.
      doc->SetServoRestyleRoot(commonAncestor, existingBits | aBits);
      Element* curr = commonAncestor;
      while ((curr = curr->GetFlattenedTreeParentElementForStyle())) {
        MOZ_ASSERT(curr->HasAllFlags(aBits));
        curr->UnsetFlags(aBits);
      }
      AssertNoBitsPropagatedFrom(commonAncestor);
    } else {
      // We didn't find a common ancestor element. That means we're descended
      // from two different document style roots, so the common ancestor is the
      // document.
      doc->SetServoRestyleRoot(doc, existingBits | aBits);
    }
  }

  // See the comment in Document::SetServoRestyleRoot about the !IsElement()
  // check there. Same justification here.
  MOZ_ASSERT(aElement == doc->GetServoRestyleRoot() ||
             !doc->GetServoRestyleRoot()->IsElement() ||
             nsContentUtils::ContentIsFlattenedTreeDescendantOfForStyle(
                 aElement, doc->GetServoRestyleRoot()));
  MOZ_ASSERT(aElement == doc->GetServoRestyleRoot() ||
             !doc->GetServoRestyleRoot()->IsElement() || !parent->IsElement() ||
             BitsArePropagated(parent->AsElement(), aBits,
                               doc->GetServoRestyleRoot()));
  MOZ_ASSERT(doc->GetServoRestyleRootDirtyBits() & aBits);
}

void Element::NoteDirtySubtreeForServo() {
  MOZ_ASSERT(IsInComposedDoc());
  MOZ_ASSERT(HasServoData());

  Document* doc = GetComposedDoc();
  nsINode* existingRoot = doc->GetServoRestyleRoot();
  uint32_t existingBits =
      existingRoot ? doc->GetServoRestyleRootDirtyBits() : 0;

  if (existingRoot && existingRoot->IsElement() && existingRoot != this &&
      nsContentUtils::ContentIsFlattenedTreeDescendantOfForStyle(
          existingRoot->AsElement(), this)) {
    PropagateBits(
        existingRoot->AsElement()->GetFlattenedTreeParentElementForStyle(),
        existingBits, this, existingBits);

    doc->ClearServoRestyleRoot();
  }

  NoteDirtyElement(this,
                   existingBits | ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO);
}

void Element::NoteDirtyForServo() {
  NoteDirtyElement(this, ELEMENT_HAS_DIRTY_DESCENDANTS_FOR_SERVO);
}

void Element::NoteAnimationOnlyDirtyForServo() {
  NoteDirtyElement(this,
                   ELEMENT_HAS_ANIMATION_ONLY_DIRTY_DESCENDANTS_FOR_SERVO);
}

void Element::NoteDescendantsNeedFramesForServo() {
  // Since lazy frame construction can be required for non-element nodes, this
  // Note() method operates on the parent of the frame-requiring content, unlike
  // the other Note() methods above (which operate directly on the element that
  // needs processing).
  NoteDirtyElement(this, NODE_DESCENDANTS_NEED_FRAMES);
  SetFlags(NODE_DESCENDANTS_NEED_FRAMES);
}

double Element::FirstLineBoxBSize() const {
  const nsBlockFrame* frame = do_QueryFrame(GetPrimaryFrame());
  if (!frame) {
    return 0.0;
  }
  nsBlockFrame::ConstLineIterator line = frame->LinesBegin();
  nsBlockFrame::ConstLineIterator lineEnd = frame->LinesEnd();
  return line != lineEnd
             ? nsPresContext::AppUnitsToDoubleCSSPixels(line->BSize())
             : 0.0;
}

// static
nsAtom* Element::GetEventNameForAttr(nsAtom* aAttr) {
  if (aAttr == nsGkAtoms::onwebkitanimationend) {
    return nsGkAtoms::onwebkitAnimationEnd;
  }
  if (aAttr == nsGkAtoms::onwebkitanimationiteration) {
    return nsGkAtoms::onwebkitAnimationIteration;
  }
  if (aAttr == nsGkAtoms::onwebkitanimationstart) {
    return nsGkAtoms::onwebkitAnimationStart;
  }
  if (aAttr == nsGkAtoms::onwebkittransitionend) {
    return nsGkAtoms::onwebkitTransitionEnd;
  }
  return aAttr;
}

void Element::RegUnRegAccessKey(bool aDoReg) {
  // first check to see if we have an access key
  nsAutoString accessKey;
  GetAttr(nsGkAtoms::accesskey, accessKey);
  if (accessKey.IsEmpty()) {
    return;
  }

  // We have an access key, so get the ESM from the pres context.
  if (nsPresContext* presContext = GetPresContext(eForComposedDoc)) {
    EventStateManager* esm = presContext->EventStateManager();

    // Register or unregister as appropriate.
    if (aDoReg) {
      esm->RegisterAccessKey(this, (uint32_t)accessKey.First());
    } else {
      esm->UnregisterAccessKey(this, (uint32_t)accessKey.First());
    }
  }
}

// https://wicg.github.io/sanitizer-api/#shadowroot-sethtml
void Element::SetHTML(const nsAString& aInnerHTML,
                      const SetHTMLOptions& aOptions, ErrorResult& aError) {
  // Step 1. Set and filter HTML using this (as target), this (as context
  // element), html, options, and true.

  // https://wicg.github.io/sanitizer-api/#set-and-filter-html
  // Step 1. If safe and contextElement’s local name is "script" and
  // contextElement’s namespace is the HTML namespace or the SVG namespace, then
  // return.
  if (IsHTMLElement(nsGkAtoms::script) || IsSVGElement(nsGkAtoms::script)) {
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                    OwnerDoc(), nsContentUtils::eDOM_PROPERTIES,
                                    "SetHTMLScript");
    return;
  }

  // Step 2. Let sanitizer be the result of calling get a sanitizer instance
  // from options with options and safe.
  nsCOMPtr<nsIGlobalObject> global = GetOwnerGlobal();
  if (!global) {
    aError.ThrowInvalidStateError("Missing owner global.");
    return;
  }
  RefPtr<Sanitizer> sanitizer =
      Sanitizer::GetInstance(global, aOptions.mSanitizer, true, aError);
  if (aError.Failed()) {
    return;
  }

  // Keep "this" alive should be guaranteed by the caller, and also the content
  // of a template element (if this is one) should never been released from this
  // during this call.  Therefore, using raw pointer here is safe.
  FragmentOrElement* target = this;
  // Handle template case.
  if (target->IsTemplateElement()) {
    DocumentFragment* frag =
        static_cast<HTMLTemplateElement*>(target)->Content();
    MOZ_ASSERT(frag);
    target = frag;
  }

  // TODO: Avoid parsing and implement a fast-path for non-markup input,
  // Filed as bug 1731215.

  // mozAutoSubtreeModified keeps the owner document alive.  Therefore, using a
  // raw pointer here is safe.
  Document* const doc = target->OwnerDoc();

  // Batch possible DOMSubtreeModified events.
  mozAutoSubtreeModified subtree(doc, nullptr);

  target->FireNodeRemovedForChildren();

  // Needed when innerHTML is used in combination with contenteditable
  mozAutoDocUpdate updateBatch(doc, true);

  // Remove childnodes.
  nsAutoMutationBatch mb(target, true, false);
  target->RemoveAllChildren(true);
  mb.RemovalDone();

  nsAutoScriptLoaderDisabler sld(doc);

  FragmentOrElement* parseContext = this;
  if (ShadowRoot* shadowRoot = ShadowRoot::FromNode(parseContext)) {
    // Fix up the context to be the host of the ShadowRoot.  See
    // https://w3c.github.io/DOM-Parsing/#dom-innerhtml-innerhtml setter step 1.
    parseContext = shadowRoot->GetHost();
  }

  // Step 3. Let newChildren be the result of the HTML fragment parsing
  // algorithm steps given contextElement, html, and true.
  // Step 4. Let fragment be a new DocumentFragment whose node document is
  // contextElement’s node document.
  // Step 5. For each node in newChildren, append node to fragment.

  // We MUST NOT cause any requests during parsing, so we'll
  // create an inert Document and parse into a new DocumentFragment.

  RefPtr<Document> inertDoc = nsContentUtils::CreateInertHTMLDocument(doc);
  if (!inertDoc) {
    aError = NS_ERROR_FAILURE;
    return;
  }

  RefPtr<DocumentFragment> fragment = new (inertDoc->NodeInfoManager())
      DocumentFragment(inertDoc->NodeInfoManager());

  nsAtom* contextLocalName = parseContext->NodeInfo()->NameAtom();
  int32_t contextNameSpaceID = parseContext->GetNameSpaceID();
  aError = nsContentUtils::ParseFragmentHTML(
      aInnerHTML, fragment, contextLocalName, contextNameSpaceID, false, true);
  if (aError.Failed()) {
    return;
  }

  // Suppress assertion about node removal mutation events that can't have
  // listeners anyway, because no one has had the chance to register
  // mutation listeners on the fragment that comes from the parser.
  nsAutoScriptBlockerSuppressNodeRemoved scriptBlocker;

  int32_t oldChildCount = static_cast<int32_t>(target->GetChildCount());

  // Step 6. Run sanitize on fragment using sanitizer and safe.
  sanitizer->Sanitize(fragment, /* aSafe */ true, aError);
  if (aError.Failed()) {
    return;
  }

  // Step 7. Replace all with fragment within target.
  target->AppendChild(*fragment, aError);
  if (aError.Failed()) {
    return;
  }

  mb.NodesAdded();
  nsContentUtils::FireMutationEventsForDirectParsing(doc, target,
                                                     oldChildCount);
}

void Element::GetHTML(const GetHTMLOptions& aOptions, nsAString& aResult) {
  if (aOptions.mSerializableShadowRoots || !aOptions.mShadowRoots.IsEmpty()) {
    nsContentUtils::SerializeNodeToMarkup<SerializeShadowRoots::Yes>(
        this, true, aResult, aOptions.mSerializableShadowRoots,
        aOptions.mShadowRoots);
  } else {
    nsContentUtils::SerializeNodeToMarkup<SerializeShadowRoots::No>(
        this, true, aResult, aOptions.mSerializableShadowRoots,
        aOptions.mShadowRoots);
  }
}

bool Element::Translate() const {
  if (const auto* parent = Element::FromNodeOrNull(mParent)) {
    return parent->Translate();
  }
  return true;
}

EditorBase* Element::GetExtantEditor() const {
  if (!IsInComposedDoc()) {
    return nullptr;
  }
  const bool isInDesignMode = IsInDesignMode();
  // Even if a text control element is an editing host, TextEditor handles
  // user input.  Therefore, we should return TextEditor (or nullptr) in this
  // case.  Note that text control element in the design mode does not work as
  // a text control.  Therefore, in that case, we should return HTMLEditor.
  if (!isInDesignMode) {
    if (const auto* textControlElement = TextControlElement::FromNode(this)) {
      if (textControlElement->IsSingleLineTextControlOrTextArea()) {
        return textControlElement->GetExtantTextEditor();
      }
    }
  }

  if (!isInDesignMode && !IsEditable()) {
    return nullptr;
  }
  // FYI: This never creates HTMLEditor immediately.
  nsDocShell* const docShell = nsDocShell::Cast(OwnerDoc()->GetDocShell());
  return docShell ? docShell->GetHTMLEditorInternal() : nullptr;
}

void Element::SetHTMLUnsafe(const TrustedHTMLOrString& aHTML,
                            ErrorResult& aError) {
  nsContentUtils::SetHTMLUnsafe(this, this, aHTML, false /*aIsShadowRoot*/,
                                aError);
}

// https://html.spec.whatwg.org/#event-beforematch
void Element::FireBeforematchEvent(ErrorResult& aRv) {
  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  event->InitEvent(u"beforematch"_ns,
                   /*aCanBubble=*/true,
                   /*aCancelable=*/false);

  event->SetTrusted(true);
  DispatchEvent(*event, aRv);
}

bool Element::BlockingContainsRender() const {
  const nsAttrValue* attrValue = GetParsedAttr(nsGkAtoms::blocking);
  if (!attrValue || !StaticPrefs::dom_element_blocking_enabled()) {
    return false;
  }
  MOZ_ASSERT(attrValue->Type() == nsAttrValue::eAtomArray,
             "Checking blocking attribute on element that doesn't parse it?");
  return attrValue->Contains(nsGkAtoms::render, eIgnoreCase);
}

}  // namespace mozilla::dom
