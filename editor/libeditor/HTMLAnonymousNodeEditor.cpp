/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditor.h"

#include "CSSEditUtils.h"
#include "HTMLEditUtils.h"

#include "js/GCAPI.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/mozalloc.h"
#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsComputedDOMStyle.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsAtom.h"
#include "nsIContent.h"
#include "nsID.h"
#include "mozilla/dom/Document.h"
#include "nsIDocumentObserver.h"
#include "nsStubMutationObserver.h"
#include "nsINode.h"
#include "nsISupportsImpl.h"
#include "nsISupportsUtils.h"
#include "nsLiteralString.h"
#include "nsPresContext.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStyledElement.h"
#include "nsUnicharUtils.h"
#include "nscore.h"
#include "nsContentUtils.h"  // for nsAutoScriptBlocker
#include "nsROCSSPrimitiveValue.h"

class nsIDOMEventListener;

namespace mozilla {

using namespace dom;

// Retrieve the rounded number of CSS pixels from a computed CSS property.
//
// Note that this should only be called for properties whose resolved value
// is CSS pixels (like width, height, left, top, right, bottom, margin, padding,
// border-*-width, ...).
//
// See: https://drafts.csswg.org/cssom/#resolved-values
static int32_t GetCSSFloatValue(nsComputedDOMStyle* aComputedStyle,
                                const nsACString& aProperty) {
  MOZ_ASSERT(aComputedStyle);

  // get the computed CSSValue of the property
  nsAutoCString value;
  aComputedStyle->GetPropertyValue(aProperty, value);
  // We only care about resolved values, not a big deal if the element is
  // undisplayed, for example, and the value is "auto" or what not.
  nsresult rv = NS_OK;
  int32_t val = value.ToInteger(&rv);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "nsAString::ToInteger() failed");
  return NS_SUCCEEDED(rv) ? val : 0;
}

/******************************************************************************
 * mozilla::ElementDeletionObserver
 *****************************************************************************/

class ElementDeletionObserver final : public nsStubMultiMutationObserver {
 public:
  /**
   * Start observing the deletion of aNativeAnonNode and aObservedElement with
   * making a new instance. Then, the instance will be released automatically
   * when one of them is destroyed or the parent chain is changed.
   */
  static void StartObservingAndDeleteOnRemoval(nsIContent* aNativeAnonNode,
                                               Element* aObservedElement) {
    auto* observer =
        new ElementDeletionObserver(aNativeAnonNode, aObservedElement);
    observer->mSelf = observer;
  }

 protected:
  ElementDeletionObserver(nsIContent* aNativeAnonNode,
                          Element* aObservedElement)
      : mNativeAnonNode(aNativeAnonNode), mObservedElement(aObservedElement) {
    AddMutationObserverToNode(mNativeAnonNode);
    AddMutationObserverToNode(mObservedElement);
  }

  ~ElementDeletionObserver() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIMUTATIONOBSERVER_PARENTCHAINCHANGED
  NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

  nsIContent* mNativeAnonNode;
  Element* mObservedElement;
  RefPtr<ElementDeletionObserver> mSelf;
};

NS_IMPL_ISUPPORTS(ElementDeletionObserver, nsIMutationObserver)

void ElementDeletionObserver::ParentChainChanged(nsIContent* aContent)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  // If the native anonymous content has been unbound already in
  // DeleteRefToAnonymousNode, mNativeAnonNode's parentNode is null.
  if (aContent != mObservedElement || !mNativeAnonNode ||
      mNativeAnonNode->GetParent() != aContent) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(mSelf);
  RefPtr<ElementDeletionObserver> self = std::move(mSelf);

  // aContent is mObservedElement so that mNativeAnonNode may be stored only by
  // the MNC array. Let's grab it.
  mObservedElement->RemoveMutationObserver(self);
  mObservedElement = nullptr;
  nsCOMPtr<nsIContent> nativeAnonNode = mNativeAnonNode;
  mNativeAnonNode = nullptr;
  nativeAnonNode->RemoveMutationObserver(self);
  ManualNACPtr::RemoveContentFromNACArray(nativeAnonNode);
  // FYI: The call of RemoveContentFromNACArray() might have caused dispatching
  // a chrome event. So, if you want to refer a member after here, you should
  // grab it before the call.
}

void ElementDeletionObserver::NodeWillBeDestroyed(nsINode* aNode) {
  MOZ_DIAGNOSTIC_ASSERT(mSelf);
  MOZ_ASSERT(aNode == mNativeAnonNode || aNode == mObservedElement);

  nsAutoScriptBlocker scriptBlocker;
  // If either mObservedElement or mNativeAnonNode, we don't need to keep
  // observing the other. Therefore, we should stop observing the both and
  // release ourselves.
  RefPtr<ElementDeletionObserver> self = std::move(mSelf);
  mObservedElement->RemoveMutationObserver(self);
  mObservedElement = nullptr;
  const RefPtr nativeAnonNode = mNativeAnonNode;
  mNativeAnonNode = nullptr;
  nativeAnonNode->RemoveMutationObserver(self);
  nativeAnonNode->UnbindFromTree();
  // FYI: The call of UnbindFromTree() might have caused dispatching a chrome
  // event. So, if you want to refer a member after `scriptBlocker` above is
  // destroyed, you have to store the member with a strong pointer before the
  // call.
}

/******************************************************************************
 * mozilla::HTMLEditor
 *****************************************************************************/

ManualNACPtr HTMLEditor::CreateAnonymousElement(nsAtom* aTag,
                                                nsIContent& aParentContent,
                                                const nsAString& aAnonClass,
                                                bool aIsCreatedHidden) {
  // Don't put anonymous editor element into non-HTML element.
  // It is mainly for avoiding other anonymous element being inserted
  // into <svg:use>, but in general we probably don't want to insert
  // some random HTML anonymous element into a non-HTML element.
  if (!aParentContent.IsHTMLElement()) {
    return nullptr;
  }

  {
    Document* const doc = GetDocument();
    if (NS_WARN_IF(!doc) ||
        NS_WARN_IF(aParentContent.GetComposedDoc() != doc)) {
      return nullptr;
    }
  }

  RefPtr<PresShell> presShell = GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return nullptr;
  }

  // Neither <a>, <img> nor <span> will create a UA shadow, so,
  // CreateAnonymousElement() shouldn't run script.
  Maybe<JS::AutoAssertNoGC> maybeAssertNoGC;
  if (aTag == nsGkAtoms::span || aTag == nsGkAtoms::img) {
    maybeAssertNoGC.emplace();
  }

  // Create a new node through the element factory
  RefPtr<Element> newElement = CreateHTMLContent(aTag);
  if (!newElement) {
    NS_WARNING("EditorBase::CreateHTMLContent() failed");
    return nullptr;
  }

  if (aIsCreatedHidden) {
    nsresult rv = newElement->SetAttr(kNameSpaceID_None, nsGkAtoms::hidden,
                                      u""_ns, true, IsKnownNewAttr::Yes);
    if (NS_FAILED(rv)) {
      NS_WARNING("Element::SetAttr(nsGkAtoms::hidden, ...) failed");
      return nullptr;
    }
  }

  // add an _moz_anonclass attribute if needed
  if (!aAnonClass.IsEmpty()) {
    nsresult rv = newElement->SetAttr(kNameSpaceID_None, nsGkAtoms::_class,
                                      aAnonClass, true, IsKnownNewAttr::Yes);
    if (NS_FAILED(rv)) {
      NS_WARNING("Element::SetAttr(nsGkAtoms::_moz_anonclass) failed");
      return nullptr;
    }
  }

  // We need to put a script blocker before calling BindToTree()  and
  // UnbindFromTree() because some helper methods require it.
  nsAutoScriptBlocker scriptBlocker;

  // establish parenthood of the element
  newElement->SetIsNativeAnonymousRoot();
  BindContext context(*aParentContent.AsElement(),
                      BindContext::ForNativeAnonymous);
  if (NS_FAILED(newElement->BindToTree(context, aParentContent))) {
    NS_WARNING("Element::BindToTree(BindContext::ForNativeAnonymous) failed");
    newElement->UnbindFromTree();
    return nullptr;
  }

  ManualNACPtr newNativeAnonymousContent(newElement.forget());
  ElementDeletionObserver::StartObservingAndDeleteOnRemoval(
      newNativeAnonymousContent, aParentContent.AsElement());

#ifdef DEBUG
  // Editor anonymous content gets passed to PostRecreateFramesFor... Which
  // can't _really_ deal with anonymous content (because it can't get the frame
  // tree ordering right).  But for us the ordering doesn't matter so this is
  // sort of ok.
  newNativeAnonymousContent->SetProperty(nsGkAtoms::restylableAnonymousNode,
                                         reinterpret_cast<void*>(true));
#endif  // DEBUG

  // display the element
  presShell->ContentAppended(newNativeAnonymousContent, {});

  return newNativeAnonymousContent;
}

// Removes event listener and calls DeleteRefToAnonymousNode.
void HTMLEditor::RemoveListenerAndDeleteRef(const nsAString& aEvent,
                                            nsIDOMEventListener* aListener,
                                            bool aUseCapture,
                                            ManualNACPtr aElement,
                                            PresShell* aPresShell) {
  if (aElement) {
    aElement->RemoveEventListener(aEvent, aListener, aUseCapture);
  }
  DeleteRefToAnonymousNode(std::move(aElement), aPresShell);
}

// Deletes all references to an anonymous element
void HTMLEditor::DeleteRefToAnonymousNode(ManualNACPtr aContent,
                                          PresShell* aPresShell) {
  // call ContentRemoved() for the anonymous content
  // node so its references get removed from the frame manager's
  // undisplay map, and its layout frames get destroyed!

  if (NS_WARN_IF(!aContent)) {
    return;
  }

  if (NS_WARN_IF(!aContent->GetParent())) {
    // aContent was already removed?
    return;
  }

  nsAutoScriptBlocker scriptBlocker;
  // Need to check whether aPresShell has been destroyed (but not yet deleted).
  // See bug 338129.
  if (aContent->IsInComposedDoc() && aPresShell &&
      !aPresShell->IsDestroying()) {
    MOZ_ASSERT(aContent->IsRootOfNativeAnonymousSubtree());
    MOZ_ASSERT(!aContent->GetPreviousSibling(), "NAC has no siblings");

    // FIXME(emilio): This is the only caller to PresShell::ContentRemoved that
    // passes NAC into it. This is not great!
    aPresShell->ContentWillBeRemoved(aContent, {});
  }

  // The ManualNACPtr destructor will invoke UnbindFromTree.
}

void HTMLEditor::HideAnonymousEditingUIs() {
  if (mAbsolutelyPositionedObject) {
    HideGrabberInternal();
    NS_ASSERTION(!mAbsolutelyPositionedObject,
                 "HTMLEditor::HideGrabberInternal() failed, but ignored");
  }
  if (mInlineEditedCell) {
    HideInlineTableEditingUIInternal();
    NS_ASSERTION(
        !mInlineEditedCell,
        "HTMLEditor::HideInlineTableEditingUIInternal() failed, but ignored");
  }
  if (mResizedObject) {
    DebugOnly<nsresult> rvIgnored = HideResizersInternal();
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::HideResizersInternal() failed, but ignored");
    NS_ASSERTION(!mResizedObject,
                 "HTMLEditor::HideResizersInternal() failed, but ignored");
  }
}

void HTMLEditor::HideAnonymousEditingUIsIfUnnecessary() {
  // XXX Perhaps, this is wrong approach to hide multiple UIs because
  //     hiding one UI may causes overwriting existing UI with newly
  //     created one.  In such case, we will leak overwritten UI.
  if (mAbsolutelyPositionedObject) {
    const Element* const editingHost =
        mAbsolutelyPositionedObject->GetEditingHost();
    if (!IsAbsolutePositionEditorEnabled() || !editingHost ||
        editingHost->IsContentEditablePlainTextOnly()) {
      // XXX If we're moving something, we need to cancel or commit the
      //     operation now.
      HideGrabberInternal();
      NS_ASSERTION(!mAbsolutelyPositionedObject,
                   "HTMLEditor::HideGrabberInternal() failed, but ignored");
    }
  }
  if (mInlineEditedCell) {
    const Element* const editingHost = mInlineEditedCell->GetEditingHost();
    if (!IsInlineTableEditorEnabled() || !editingHost ||
        editingHost->IsContentEditablePlainTextOnly()) {
      // XXX If we're resizing a table element, we need to cancel or commit the
      //     operation now.
      HideInlineTableEditingUIInternal();
      NS_ASSERTION(
          !mInlineEditedCell,
          "HTMLEditor::HideInlineTableEditingUIInternal() failed, but ignored");
    }
  }
  if (mResizedObject) {
    const Element* const editingHost = mResizedObject->GetEditingHost();
    if (!IsObjectResizerEnabled() || !editingHost ||
        editingHost->IsContentEditablePlainTextOnly()) {
      // XXX If we're resizing something, we need to cancel or commit the
      //     operation now.
      DebugOnly<nsresult> rvIgnored = HideResizersInternal();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::HideResizersInternal() failed, but ignored");
      NS_ASSERTION(!mResizedObject,
                   "HTMLEditor::HideResizersInternal() failed, but ignored");
    }
  }
}

NS_IMETHODIMP HTMLEditor::CheckSelectionStateForAnonymousButtons() {
  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = RefreshEditingUI();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::RefereshEditingUI() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::RefreshEditingUI() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // First, we need to remove unnecessary editing UI now since some of them
  // may be disabled while them are visible.
  HideAnonymousEditingUIsIfUnnecessary();

  // early way out if all contextual UI extensions are disabled
  if (!IsObjectResizerEnabled() && !IsAbsolutePositionEditorEnabled() &&
      !IsInlineTableEditorEnabled()) {
    return NS_OK;
  }

  // Don't change selection state if we're moving.
  if (mIsMoving) {
    return NS_OK;
  }

  // let's get the containing element of the selection
  RefPtr<Element> selectionContainerElement = GetSelectionContainerElement();
  if (NS_WARN_IF(!selectionContainerElement)) {
    return NS_OK;
  }

  // If we're not in a document, don't try to add resizers
  if (!selectionContainerElement->IsInComposedDoc()) {
    return NS_OK;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (editingHost && editingHost->IsContentEditablePlainTextOnly()) {
    return NS_OK;
  }
  MOZ_ASSERT_IF(editingHost,
                editingHost == selectionContainerElement->GetEditingHost());

  // what's its tag?
  RefPtr<Element> focusElement = std::move(selectionContainerElement);
  nsAtom* focusTagAtom = focusElement->NodeInfo()->NameAtom();

  RefPtr<Element> absPosElement;
  if (IsAbsolutePositionEditorEnabled()) {
    // Absolute Positioning support is enabled, is the selection contained
    // in an absolutely positioned element ?
    absPosElement = GetAbsolutelyPositionedSelectionContainer();
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
  }

  RefPtr<Element> cellElement;
  if (IsObjectResizerEnabled() || IsInlineTableEditorEnabled()) {
    // Resizing or Inline Table Editing is enabled, we need to check if the
    // selection is contained in a table cell
    cellElement = GetInclusiveAncestorByTagNameAtSelection(*nsGkAtoms::td);
  }

  if (IsObjectResizerEnabled() && cellElement) {
    // we are here because Resizing is enabled AND selection is contained in
    // a cell

    // get the enclosing table
    if (nsGkAtoms::img != focusTagAtom) {
      // the element container of the selection is not an image, so we'll show
      // the resizers around the table
      // XXX There may be a bug.  cellElement may be not in <table> in invalid
      //     tree.  So, perhaps, GetClosestAncestorTableElement() returns
      //     nullptr, we should not set focusTagAtom to nsGkAtoms::table.
      focusElement =
          HTMLEditUtils::GetClosestAncestorTableElement(*cellElement);
      focusTagAtom = nsGkAtoms::table;
    }
  }

  // we allow resizers only around images, tables, and absolutely positioned
  // elements. If we don't have image/table, let's look at the latter case.
  if (nsGkAtoms::img != focusTagAtom && nsGkAtoms::table != focusTagAtom) {
    focusElement = absPosElement;
  }

  // at this point, focusElement  contains the element for Resizing,
  //                cellElement   contains the element for InlineTableEditing
  //                absPosElement contains the element for Positioning

  // TODO: Since we've already stopped supporting the DOM mutation events.
  // So, we may not need to verity the result below.

  if (IsAbsolutePositionEditorEnabled() && mAbsolutelyPositionedObject &&
      absPosElement != mAbsolutelyPositionedObject) {
    HideGrabberInternal();
    NS_ASSERTION(!mAbsolutelyPositionedObject,
                 "HTMLEditor::HideGrabberInternal() failed, but ignored");
  }

  if (IsObjectResizerEnabled() && mResizedObject &&
      mResizedObject != focusElement) {
    // Perhaps, even if HideResizersInternal() failed, we should try to hide
    // inline table editing UI.  However, it returns error only when we cannot
    // do anything.  So, it's okay for now.
    nsresult rv = HideResizersInternal();
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::HideResizersInternal() failed");
      return rv;
    }
    NS_ASSERTION(!mResizedObject,
                 "HTMLEditor::HideResizersInternal() failed, but ignored");
  }

  if (IsInlineTableEditorEnabled() && mInlineEditedCell &&
      mInlineEditedCell != cellElement) {
    HideInlineTableEditingUIInternal();
    NS_ASSERTION(
        !mInlineEditedCell,
        "HTMLEditor::HideInlineTableEditingUIInternal failed, but ignored");
  }

  // now, let's display all contextual UI for good
  if (IsObjectResizerEnabled() && focusElement &&
      HTMLEditUtils::IsSimplyEditableNode(*focusElement) &&
      focusElement != editingHost) {
    if (nsGkAtoms::img == focusTagAtom) {
      mResizedObjectIsAnImage = true;
    }
    if (mResizedObject) {
      nsresult rv = RefreshResizersInternal();
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::RefreshResizersInternal() failed");
        return rv;
      }
    } else {
      nsresult rv = ShowResizersInternal(*focusElement);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::ShowResizersInternal() failed");
        return rv;
      }
    }
  }

  if (IsAbsolutePositionEditorEnabled() && absPosElement &&
      HTMLEditUtils::IsSimplyEditableNode(*absPosElement) &&
      absPosElement != editingHost) {
    if (mAbsolutelyPositionedObject) {
      nsresult rv = RefreshGrabberInternal();
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::RefreshGrabberInternal() failed");
        return rv;
      }
    } else {
      nsresult rv = ShowGrabberInternal(*absPosElement);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::ShowGrabberInternal() failed");
        return rv;
      }
    }
  }

  // XXX Shouldn't we check whether the `<table>` element is editable or not?
  if (IsInlineTableEditorEnabled() && cellElement &&
      HTMLEditUtils::IsSimplyEditableNode(*cellElement) &&
      cellElement != editingHost) {
    if (mInlineEditedCell) {
      nsresult rv = RefreshInlineTableEditingUIInternal();
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::RefreshInlineTableEditingUIInternal() failed");
        return rv;
      }
    } else {
      nsresult rv = ShowInlineTableEditingUIInternal(*cellElement);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::ShowInlineTableEditingUIInternal() failed");
        return rv;
      }
    }
  }

  return NS_OK;
}

// Resizing and Absolute Positioning need to know everything about the
// containing box of the element: position, size, margins, borders
nsresult HTMLEditor::GetPositionAndDimensions(Element& aElement, int32_t& aX,
                                              int32_t& aY, int32_t& aW,
                                              int32_t& aH, int32_t& aBorderLeft,
                                              int32_t& aBorderTop,
                                              int32_t& aMarginLeft,
                                              int32_t& aMarginTop) {
  // Is the element positioned ? let's check the cheap way first...
  bool isPositioned = aElement.HasAttr(nsGkAtoms::_moz_abspos);
  if (!isPositioned) {
    // hmmm... the expensive way now...
    nsAutoString positionValue;
    DebugOnly<nsresult> rvIgnored = CSSEditUtils::GetComputedProperty(
        aElement, *nsGkAtoms::position, positionValue);
    if (NS_WARN_IF(Destroyed())) [[unlikely]] {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_WARN_IF(aElement.OwnerDoc() != GetDocument())) [[unlikely]] {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "CSSEditUtils::GetComputedProperty(nsGkAtoms::"
                         "position) failed, but ignored");
    isPositioned = positionValue.EqualsLiteral("absolute");
  }

  if (isPositioned) {
    // Yes, it is absolutely positioned
    mResizedObjectIsAbsolutelyPositioned = true;

    // Get the all the computed css styles attached to the element node
    RefPtr<nsComputedDOMStyle> computedDOMStyle =
        CSSEditUtils::GetComputedStyle(&aElement);
    if (NS_WARN_IF(Destroyed())) [[unlikely]] {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_WARN_IF(aElement.OwnerDoc() != GetDocument())) [[unlikely]] {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    if (NS_WARN_IF(!computedDOMStyle)) [[unlikely]] {
      return NS_ERROR_FAILURE;
    }

    aBorderLeft = GetCSSFloatValue(computedDOMStyle, "border-left-width"_ns);
    aBorderTop = GetCSSFloatValue(computedDOMStyle, "border-top-width"_ns);
    aMarginLeft = GetCSSFloatValue(computedDOMStyle, "margin-left"_ns);
    aMarginTop = GetCSSFloatValue(computedDOMStyle, "margin-top"_ns);

    aX = GetCSSFloatValue(computedDOMStyle, "left"_ns) + aMarginLeft +
         aBorderLeft;
    aY = GetCSSFloatValue(computedDOMStyle, "top"_ns) + aMarginTop + aBorderTop;
    aW = GetCSSFloatValue(computedDOMStyle, "width"_ns);
    aH = GetCSSFloatValue(computedDOMStyle, "height"_ns);
  } else {
    mResizedObjectIsAbsolutelyPositioned = false;
    RefPtr<nsGenericHTMLElement> htmlElement =
        nsGenericHTMLElement::FromNode(aElement);
    if (NS_WARN_IF(!htmlElement)) [[unlikely]] {
      return NS_ERROR_NULL_POINTER;
    }
    DebugOnly<nsresult> rvIgnored = GetElementOrigin(aElement, aX, aY);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "HTMLEditor::GetElementOrigin() failed, but ignored");

    aW = htmlElement->OffsetWidth();
    aH = htmlElement->OffsetHeight();

    aBorderLeft = 0;
    aBorderTop = 0;
    aMarginLeft = 0;
    aMarginTop = 0;
  }
  return NS_OK;
}

nsresult HTMLEditor::SetAnonymousElementPositionWithoutTransaction(
    nsStyledElement& aStyledElement, int32_t aX, int32_t aY) {
  nsresult rv;
  rv = CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(
      *this, aStyledElement, *nsGkAtoms::left, aX);
  if (rv == NS_ERROR_EDITOR_DESTROYED) {
    NS_WARNING(
        "CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(nsGkAtoms::left) "
        "destroyed the editor");
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(nsGkAtoms::left) "
      "failed, but ignored");
  rv = CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(
      *this, aStyledElement, *nsGkAtoms::top, aY);
  if (rv == NS_ERROR_EDITOR_DESTROYED) {
    NS_WARNING(
        "CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(nsGkAtoms::top) "
        "destroyed the editor");
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "CSSEditUtils::SetCSSPropertyPixelsWithoutTransaction(nsGkAtoms::top) "
      "failed, but ignored");
  return NS_OK;
}

}  // namespace mozilla
