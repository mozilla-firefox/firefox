/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCoreUtils.h"

#include "AnchorPositioningUtils.h"
#include "XULTreeElement.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInternals.h"
#include "mozilla/dom/HTMLLabelElement.h"
#include "mozilla/dom/HTMLOptGroupElement.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/TouchEvent.h"
#include "nsAccUtils.h"
#include "nsAttrValue.h"
#include "nsComponentManagerUtils.h"
#include "nsGkAtoms.h"
#include "nsIAccessibleTypes.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "nsIFrame.h"
#include "nsIObserverService.h"
#include "nsISelectionController.h"
#include "nsISimpleEnumerator.h"
#include "nsPresContext.h"
#include "nsStyleStruct.h"
#include "nsTreeColumns.h"
#include "nsXULElement.h"

using namespace mozilla;

using mozilla::dom::DOMRect;
using mozilla::dom::Element;
using mozilla::dom::Selection;
using mozilla::dom::XULTreeElement;

using mozilla::a11y::nsAccUtils;

////////////////////////////////////////////////////////////////////////////////
// nsCoreUtils
////////////////////////////////////////////////////////////////////////////////

bool nsCoreUtils::IsLabelWithControl(nsIContent* aContent) {
  dom::HTMLLabelElement* label = dom::HTMLLabelElement::FromNode(aContent);
  if (label && label->GetLabeledElementInternal()) return true;

  return false;
}

bool nsCoreUtils::HasClickListener(nsIContent* aContent) {
  NS_ENSURE_TRUE(aContent, false);
  EventListenerManager* listenerManager =
      aContent->GetExistingListenerManager();

  return listenerManager &&
         (listenerManager->HasListenersFor(nsGkAtoms::onclick) ||
          listenerManager->HasListenersFor(nsGkAtoms::onmousedown) ||
          listenerManager->HasListenersFor(nsGkAtoms::onmouseup));
}

void nsCoreUtils::DispatchClickEvent(XULTreeElement* aTree, int32_t aRowIndex,
                                     nsTreeColumn* aColumn,
                                     const nsAString& aPseudoElt) {
  RefPtr<dom::Element> tcElm = aTree->GetTreeBody();
  if (!tcElm) return;

  Document* document = tcElm->GetUncomposedDoc();
  if (!document) return;

  RefPtr<PresShell> presShell = document->GetPresShell();
  if (!presShell) {
    return;
  }

  // Ensure row is visible.
  aTree->EnsureRowIsVisible(aRowIndex);

  // Calculate x and y coordinates.
  nsresult rv;
  nsIntRect rect =
      aTree->GetCoordsForCellItem(aRowIndex, aColumn, aPseudoElt, rv);
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<DOMRect> treeBodyRect = tcElm->GetBoundingClientRect();
  int32_t tcX = (int32_t)treeBodyRect->X();
  int32_t tcY = (int32_t)treeBodyRect->Y();

  // Dispatch mouse events.
  AutoWeakFrame tcFrame = tcElm->GetPrimaryFrame();
  nsIFrame* rootFrame = presShell->GetRootFrame();

  nsPoint offset;
  nsCOMPtr<nsIWidget> rootWidget = rootFrame->GetNearestWidget(offset);
  RefPtr<nsPresContext> presContext = presShell->GetPresContext();

  int32_t cnvdX = presContext->CSSPixelsToDevPixels(tcX + int32_t(rect.x) + 1) +
                  presContext->AppUnitsToDevPixels(offset.x);
  int32_t cnvdY = presContext->CSSPixelsToDevPixels(tcY + int32_t(rect.y) + 1) +
                  presContext->AppUnitsToDevPixels(offset.y);

  // This isn't needed once bug 1924790 is fixed.
  tcElm->OwnerDoc()->NotifyUserGestureActivation();

  // XUL is just desktop, so there is no real reason for senfing touch events.
  DispatchMouseEvent(eMouseDown, cnvdX, cnvdY, tcElm, tcFrame, presShell,
                     rootWidget);

  DispatchMouseEvent(eMouseUp, cnvdX, cnvdY, tcElm, tcFrame, presShell,
                     rootWidget);
}

void nsCoreUtils::DispatchMouseEvent(EventMessage aMessage, int32_t aX,
                                     int32_t aY, nsIContent* aContent,
                                     nsIFrame* aFrame, PresShell* aPresShell,
                                     nsIWidget* aRootWidget) {
  MOZ_ASSERT(!IsPointerEventMessage(aMessage));
  WidgetMouseEvent event(true, aMessage, aRootWidget, WidgetMouseEvent::eReal);

  event.mRefPoint = LayoutDeviceIntPoint(aX, aY);

  event.mClickCount = 1;
  event.mButton = MouseButton::ePrimary;
  event.mInputSource = dom::MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;

  nsEventStatus status = nsEventStatus_eIgnore;
  aPresShell->HandleEventWithTarget(&event, aFrame, aContent, &status);
}

void nsCoreUtils::DispatchTouchEvent(EventMessage aMessage, int32_t aX,
                                     int32_t aY, nsIContent* aContent,
                                     nsIFrame* aFrame, PresShell* aPresShell,
                                     nsIWidget* aRootWidget) {
  nsIDocShell* docShell = nullptr;
  if (aPresShell->GetDocument()) {
    docShell = aPresShell->GetDocument()->GetDocShell();
  }
  if (!dom::TouchEvent::PrefEnabled(docShell)) {
    return;
  }

  WidgetTouchEvent event(true, aMessage, aRootWidget);

  // XXX: Touch has an identifier of -1 to hint that it is synthesized.
  auto t = MakeRefPtr<dom::Touch>(-1, LayoutDeviceIntPoint(aX, aY),
                                  LayoutDeviceIntPoint(1, 1), 0.0f, 1.0f);
  t->SetTouchTarget(aContent);
  event.mTouches.AppendElement(t);
  nsEventStatus status = nsEventStatus_eIgnore;
  aPresShell->HandleEventWithTarget(&event, aFrame, aContent, &status);
}

uint32_t nsCoreUtils::GetAccessKeyFor(nsIContent* aContent) {
  // Accesskeys are registered by @accesskey attribute only. At first check
  // whether it is presented on the given element to avoid the slow
  // EventStateManager::GetRegisteredAccessKey() method.
  if (!aContent->IsElement() || !aContent->AsElement()->HasAttr(
                                    kNameSpaceID_None, nsGkAtoms::accesskey)) {
    return 0;
  }

  nsPresContext* presContext = aContent->OwnerDoc()->GetPresContext();
  if (!presContext) return 0;

  EventStateManager* esm = presContext->EventStateManager();
  if (!esm) return 0;

  return esm->GetRegisteredAccessKey(aContent->AsElement());
}

nsIContent* nsCoreUtils::GetDOMElementFor(nsIContent* aContent) {
  if (aContent->IsElement()) return aContent;

  if (aContent->IsText()) return aContent->GetFlattenedTreeParent();

  return nullptr;
}

nsINode* nsCoreUtils::GetDOMNodeFromDOMPoint(nsINode* aNode, uint32_t aOffset) {
  if (aNode && aNode->IsElement()) {
    if (aNode->IsTextControlElement()) {
      // Offsets in text controls refer to the control itself.
      // TODO(bug 2017248): Return the anonymous text node itself. This is
      // currently not a problem because the caret code is managed by
      // HyperTextAccessible, but would be a problem if this was rewritten to
      // use TextLeafPoint.
      return aNode;
    }

    uint32_t childCount = aNode->GetChildCount();
    NS_ASSERTION(aOffset <= childCount, "Wrong offset of the DOM point!");

    // The offset can be after last child of container node that means DOM point
    // is placed immediately after the last child. In this case use the DOM node
    // from the given DOM point is used as result node.
    if (aOffset != childCount) return aNode->GetChildAt_Deprecated(aOffset);
  }

  return aNode;
}

bool nsCoreUtils::IsAncestorOf(nsINode* aPossibleAncestorNode,
                               nsINode* aPossibleDescendantNode,
                               nsINode* aRootNode) {
  NS_ENSURE_TRUE(aPossibleAncestorNode && aPossibleDescendantNode, false);

  nsINode* parentNode = aPossibleDescendantNode;
  while ((parentNode = parentNode->GetParentNode()) &&
         parentNode != aRootNode) {
    if (parentNode == aPossibleAncestorNode) return true;
  }

  return false;
}

nsresult nsCoreUtils::ScrollSubstringTo(nsIFrame* aFrame,
                                        mozilla::dom::Range* aRange,
                                        uint32_t aScrollType) {
  AxisScrollParams vertical, horizontal;
  ConvertScrollTypeToPercents(aScrollType, &vertical, &horizontal);

  return ScrollSubstringTo(aFrame, aRange, vertical, horizontal);
}

nsresult nsCoreUtils::ScrollSubstringTo(nsIFrame* aFrame,
                                        mozilla::dom::Range* aRange,
                                        AxisScrollParams aVertical,
                                        AxisScrollParams aHorizontal) {
  if (!aFrame || !aRange) {
    return NS_ERROR_FAILURE;
  }

  const RefPtr<dom::Selection> selection = [&]() -> dom::Selection* {
    nsISelectionController* const selCon = aFrame->GetSelectionController();
    NS_ENSURE_TRUE(selCon, nullptr);
    return selCon->GetSelection(
        nsISelectionController::SELECTION_ACCESSIBILITY);
  }();
  if (MOZ_UNLIKELY(!selection)) {
    return NS_ERROR_FAILURE;
  }

  selection->RemoveAllRanges(IgnoreErrors());
  selection->AddRangeAndSelectFramesAndNotifyListeners(*aRange, IgnoreErrors());

  selection->ScrollIntoView(nsISelectionController::SELECTION_ANCHOR_REGION,
                            aVertical, aHorizontal, ScrollFlags::None,
                            SelectionScrollMode::SyncNoFlush);

  selection->CollapseToStart(IgnoreErrors());

  return NS_OK;
}

void nsCoreUtils::ScrollFrameToPoint(nsIFrame* aScrollContainerFrame,
                                     nsIFrame* aFrame,
                                     const LayoutDeviceIntPoint& aPoint) {
  ScrollContainerFrame* scrollContainerFrame =
      do_QueryFrame(aScrollContainerFrame);
  if (!scrollContainerFrame) {
    return;
  }

  nsPoint point = LayoutDeviceIntPoint::ToAppUnits(
      aPoint, aFrame->PresContext()->AppUnitsPerDevPixel());
  nsRect frameRect = aFrame->GetScreenRectInAppUnits();
  nsPoint deltaPoint = point - frameRect.TopLeft();

  nsPoint scrollPoint = scrollContainerFrame->GetScrollPosition();
  scrollPoint -= deltaPoint;

  scrollContainerFrame->ScrollTo(scrollPoint, ScrollMode::Instant);
}

void nsCoreUtils::ConvertScrollTypeToPercents(uint32_t aScrollType,
                                              AxisScrollParams* aVertical,
                                              AxisScrollParams* aHorizontal) {
  WhereToScroll whereY, whereX;
  WhenToScroll whenY, whenX;
  switch (aScrollType) {
    case nsIAccessibleScrollType::SCROLL_TYPE_TOP_LEFT:
      whereY = WhereToScroll::Start;
      whenY = WhenToScroll::Always;
      whereX = WhereToScroll::Start;
      whenX = WhenToScroll::Always;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_BOTTOM_RIGHT:
      whereY = WhereToScroll::End;
      whenY = WhenToScroll::Always;
      whereX = WhereToScroll::End;
      whenX = WhenToScroll::Always;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_TOP_EDGE:
      whereY = WhereToScroll::Start;
      whenY = WhenToScroll::Always;
      whereX = WhereToScroll::Nearest;
      whenX = WhenToScroll::IfNotFullyVisible;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_BOTTOM_EDGE:
      whereY = WhereToScroll::End;
      whenY = WhenToScroll::Always;
      whereX = WhereToScroll::Nearest;
      whenX = WhenToScroll::IfNotFullyVisible;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_LEFT_EDGE:
      whereY = WhereToScroll::Nearest;
      whenY = WhenToScroll::IfNotFullyVisible;
      whereX = WhereToScroll::Start;
      whenX = WhenToScroll::Always;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_RIGHT_EDGE:
      whereY = WhereToScroll::Nearest;
      whenY = WhenToScroll::IfNotFullyVisible;
      whereX = WhereToScroll::End;
      whenX = WhenToScroll::Always;
      break;
    default:
      whereY = WhereToScroll::Center;
      whenY = WhenToScroll::IfNotFullyVisible;
      whereX = WhereToScroll::Center;
      whenX = WhenToScroll::IfNotFullyVisible;
  }
  *aVertical = AxisScrollParams(whereY, whenY);
  *aHorizontal = AxisScrollParams(whereX, whenX);
}

already_AddRefed<nsIDocShell> nsCoreUtils::GetDocShellFor(nsINode* aNode) {
  if (!aNode) return nullptr;

  nsCOMPtr<nsIDocShell> docShell = aNode->OwnerDoc()->GetDocShell();
  return docShell.forget();
}

bool nsCoreUtils::IsRootDocument(Document* aDocument) {
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem = aDocument->GetDocShell();
  NS_ASSERTION(docShellTreeItem, "No document shell for document!");

  nsCOMPtr<nsIDocShellTreeItem> parentTreeItem;
  docShellTreeItem->GetInProcessParent(getter_AddRefs(parentTreeItem));

  return !parentTreeItem;
}

bool nsCoreUtils::IsTopLevelContentDocInProcess(Document* aDocumentNode) {
  mozilla::dom::BrowsingContext* bc = aDocumentNode->GetBrowsingContext();
  return bc->IsContent() && (
                                // Tab document.
                                bc->IsTop() ||
                                // Out-of-process iframe.
                                !bc->GetParent()->IsInProcess());
}

bool nsCoreUtils::IsErrorPage(Document* aDocument) {
  nsIURI* uri = aDocument->GetDocumentURI();
  if (!uri->SchemeIs("about")) {
    return false;
  }

  nsAutoCString path;
  uri->GetPathQueryRef(path);

  constexpr auto neterror = "neterror"_ns;
  constexpr auto certerror = "certerror"_ns;

  return StringBeginsWith(path, neterror) || StringBeginsWith(path, certerror);
}

PresShell* nsCoreUtils::GetPresShellFor(nsINode* aNode) {
  return aNode->OwnerDoc()->GetPresShell();
}

bool nsCoreUtils::GetID(nsIContent* aContent, nsAString& aID) {
  return aContent->IsElement() &&
         aContent->AsElement()->GetAttr(nsGkAtoms::id, aID);
}

bool nsCoreUtils::GetUIntAttr(nsIContent* aContent, nsAtom* aAttr,
                              int32_t* aUInt) {
  if (!aContent->IsElement()) {
    return false;
  }
  return GetUIntAttrValue(nsAccUtils::GetARIAAttr(aContent->AsElement(), aAttr),
                          aUInt);
}

bool nsCoreUtils::GetUIntAttrValue(const nsAttrValue* aVal, int32_t* aUInt) {
  if (!aVal) {
    return false;
  }
  nsAutoString value;
  aVal->ToString(value);
  if (!value.IsEmpty()) {
    nsresult error = NS_OK;
    int32_t integer = value.ToInteger(&error);
    if (NS_SUCCEEDED(error) && integer > 0) {
      *aUInt = integer;
      return true;
    }
  }

  return false;
}

void nsCoreUtils::GetLanguageFor(nsIContent* aContent, nsIContent* aRootContent,
                                 nsAString& aLanguage) {
  aLanguage.Truncate();

  nsIContent* walkUp = aContent;
  while (walkUp && walkUp != aRootContent &&
         (!walkUp->IsElement() ||
          !walkUp->AsElement()->GetAttr(nsGkAtoms::lang, aLanguage))) {
    walkUp = walkUp->GetParent();
  }
}

XULTreeElement* nsCoreUtils::GetTree(nsIContent* aContent) {
  // Find DOMNode's parents recursively until reach the <tree> tag
  nsIContent* currentContent = aContent;
  while (currentContent) {
    if (currentContent->NodeInfo()->Equals(nsGkAtoms::tree, kNameSpaceID_XUL)) {
      return XULTreeElement::FromNode(currentContent);
    }
    currentContent = currentContent->GetFlattenedTreeParent();
  }

  return nullptr;
}

already_AddRefed<nsTreeColumn> nsCoreUtils::GetFirstSensibleColumn(
    XULTreeElement* aTree, FlushType aFlushType) {
  if (!aTree) {
    return nullptr;
  }

  RefPtr<nsTreeColumns> cols = aTree->GetColumns(aFlushType);
  if (!cols) {
    return nullptr;
  }

  RefPtr<nsTreeColumn> column = cols->GetFirstColumn();
  if (column && IsColumnHidden(column)) return GetNextSensibleColumn(column);

  return column.forget();
}

uint32_t nsCoreUtils::GetSensibleColumnCount(XULTreeElement* aTree) {
  uint32_t count = 0;
  if (!aTree) {
    return count;
  }

  RefPtr<nsTreeColumns> cols = aTree->GetColumns();
  if (!cols) {
    return count;
  }

  nsTreeColumn* column = cols->GetFirstColumn();

  while (column) {
    if (!IsColumnHidden(column)) count++;

    column = column->GetNext();
  }

  return count;
}

already_AddRefed<nsTreeColumn> nsCoreUtils::GetSensibleColumnAt(
    XULTreeElement* aTree, uint32_t aIndex) {
  if (!aTree) {
    return nullptr;
  }

  uint32_t idx = aIndex;

  nsCOMPtr<nsTreeColumn> column = GetFirstSensibleColumn(aTree);
  while (column) {
    if (idx == 0) return column.forget();

    idx--;
    column = GetNextSensibleColumn(column);
  }

  return nullptr;
}

already_AddRefed<nsTreeColumn> nsCoreUtils::GetNextSensibleColumn(
    nsTreeColumn* aColumn) {
  if (!aColumn) {
    return nullptr;
  }

  RefPtr<nsTreeColumn> nextColumn = aColumn->GetNext();

  while (nextColumn && IsColumnHidden(nextColumn)) {
    nextColumn = nextColumn->GetNext();
  }

  return nextColumn.forget();
}

already_AddRefed<nsTreeColumn> nsCoreUtils::GetPreviousSensibleColumn(
    nsTreeColumn* aColumn) {
  if (!aColumn) {
    return nullptr;
  }

  RefPtr<nsTreeColumn> prevColumn = aColumn->GetPrevious();

  while (prevColumn && IsColumnHidden(prevColumn)) {
    prevColumn = prevColumn->GetPrevious();
  }

  return prevColumn.forget();
}

bool nsCoreUtils::IsColumnHidden(nsTreeColumn* aColumn) {
  if (!aColumn) {
    return false;
  }

  Element* element = aColumn->Element();
  return element->GetBoolAttr(nsGkAtoms::hidden);
}

void nsCoreUtils::ScrollTo(PresShell* aPresShell, nsIContent* aContent,
                           uint32_t aScrollType) {
  AxisScrollParams vertical, horizontal;
  ConvertScrollTypeToPercents(aScrollType, &vertical, &horizontal);
  aPresShell->ScrollContentIntoView(aContent, vertical, horizontal,
                                    ScrollFlags::ScrollOverflowHidden);
}

bool nsCoreUtils::IsHTMLTableHeader(nsIContent* aContent) {
  return aContent->NodeInfo()->Equals(nsGkAtoms::th) ||
         (aContent->IsElement() &&
          aContent->AsElement()->HasAttr(nsGkAtoms::scope));
}

bool nsCoreUtils::IsWhitespaceString(const nsAString& aString) {
  nsAString::const_char_iterator iterBegin, iterEnd;

  aString.BeginReading(iterBegin);
  aString.EndReading(iterEnd);

  while (iterBegin != iterEnd && IsWhitespace(*iterBegin)) ++iterBegin;

  return iterBegin == iterEnd;
}

bool nsCoreUtils::AccEventObserversExist() {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  NS_ENSURE_TRUE(obsService, false);

  nsCOMPtr<nsISimpleEnumerator> observers;
  obsService->EnumerateObservers(NS_ACCESSIBLE_EVENT_TOPIC,
                                 getter_AddRefs(observers));
  NS_ENSURE_TRUE(observers, false);

  bool hasObservers = false;
  observers->HasMoreElements(&hasObservers);

  return hasObservers;
}

void nsCoreUtils::DispatchAccEvent(RefPtr<nsIAccessibleEvent> event) {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(obsService);

  obsService->NotifyObservers(event, NS_ACCESSIBLE_EVENT_TOPIC, nullptr);
}

bool nsCoreUtils::IsDisplayContents(nsIContent* aContent) {
  auto* element = Element::FromNodeOrNull(aContent);
  return element && element->IsDisplayContents();
}

bool nsCoreUtils::CanCreateAccessibleWithoutFrame(nsIContent* aContent,
                                                  bool* aIsSubtreeHidden) {
  auto* element = Element::FromNodeOrNull(aContent);
  if (!element) {
    return false;
  }
  // <option> and <optgroup> can create an accessible for comboboxes, if our
  // select can also create an accessible (even if they're display: none)
  if (auto* option = dom::HTMLOptionElement::FromNode(element)) {
    if (auto* select = option->GetSelect(); select && select->IsCombobox()) {
      element = select;
    }
  } else if (auto* optgroup = dom::HTMLOptGroupElement::FromNode(element)) {
    if (auto* select = optgroup->GetSelect(); select && select->IsCombobox()) {
      element = select;
    }
  } else if (element->GetPseudoElementType() == PseudoStyleType::Picker) {
    if (auto* select =
            dom::HTMLSelectElement::FromNode(element->GetFlattenedTreeParent());
        select && select->IsCombobox()) {
      element = select;
    }
  }

  // If we aren't display: contents or option/optgroup we can't create an
  // accessible without frame. Our select combobox code relies on the latter.
  // Note that we need to check primary frame explicitly for the <select> case
  // above.
  if (!element->GetPrimaryFrame() && !element->IsDisplayContents()) {
    // Nothing in aContent's subtree can ever render in this case, regardless of
    // any ancestor.
    if (aIsSubtreeHidden) {
      *aIsSubtreeHidden = true;
    }
    return false;
  }

  // Even if we're display: contents or optgroups, we might not be able to
  // create an accessible if we're in a content-visibility: hidden, visibility:
  // hidden or inert subtree.
  //
  // To check that, find the closest ancestor element with a frame.
  for (nsIContent* c :
       element->InclusiveFlatTreeAncestorsOfType<nsIContent>()) {
    if (nsIFrame* f = c->GetPrimaryFrame()) {
      if (f->HidesContent(nsIFrame::IncludeContentVisibility::Hidden) ||
          f->IsHiddenByContentVisibilityOnAnyAncestor(
              nsIFrame::IncludeContentVisibility::Hidden)) {
        if (aIsSubtreeHidden) {
          // A descendant can't override content-visibility: hidden, so there's
          // no point walking into the subtree.
          *aIsSubtreeHidden = true;
        }
        return false;
      }
      if (!f->StyleVisibility()->IsVisible() || f->StyleUI()->IsInert()) {
        // Unlike content-visibility, a descendant might still be exposed
        // despite this (e.g. an open modal <dialog> which is not inert), so
        // leave aIsSubtreeHidden alone and let the caller keep walking.
        return false;
      }
      break;
    }
  }

  return true;
}

bool nsCoreUtils::IsDocumentVisibleConsideringInProcessAncestors(
    const Document* aDocument) {
  const Document* parent = aDocument;
  do {
    if (!parent->IsVisible()) {
      return false;
    }
  } while ((parent = parent->GetInProcessParentDocument()));
  return true;
}

bool nsCoreUtils::IsDescendantOfAnyShadowIncludingAncestor(
    nsINode* aDescendant, nsINode* aStartAncestor) {
  const nsINode* descRoot = aDescendant->SubtreeRoot();
  nsINode* ancRoot = aStartAncestor->SubtreeRoot();
  for (;;) {
    if (ancRoot == descRoot) {
      return true;
    }
    auto* shadow = mozilla::dom::ShadowRoot::FromNode(ancRoot);
    if (!shadow || !shadow->GetHost()) {
      break;
    }
    ancRoot = shadow->GetHost()->SubtreeRoot();
  }
  return false;
}

Element* nsCoreUtils::GetAriaActiveDescendantElement(Element* aElement) {
  if (Element* activeDescendant = aElement->GetAriaActiveDescendantElement()) {
    return activeDescendant;
  }

  if (auto* element = nsGenericHTMLElement::FromNode(aElement)) {
    if (auto* internals = element->GetInternals()) {
      return internals->GetAriaActiveDescendantElement();
    }
  }

  return nullptr;
}

bool nsCoreUtils::IsTrimmedWhitespaceBeforeHardLineBreak(nsIFrame* aFrame) {
  if (!aFrame->GetRect().IsEmpty() ||
      !aFrame->HasAnyStateBits(TEXT_END_OF_LINE)) {
    return false;
  }
  // Normally, accessibility calls nsIFrame::GetRenderedText with
  // TrailingWhitespace::NoTrim. Using TrailingWhitespace::Trim instead trims 0
  // width whitespace before a hard line break, resulting in an empty string if
  // that is all the frame contains. Note that TrailingWhitespace::Trim does
  // *not* trim whitespace before a soft line break (wrapped line).
  nsIFrame::RenderedText text = aFrame->GetRenderedText(
      0, UINT32_MAX, nsIFrame::TextOffsetType::OffsetsInContentText,
      nsIFrame::TrailingWhitespace::Trim);
  return text.mString.IsEmpty();
}

Maybe<nsString> nsCoreUtils::GetCssAltContent(nsIContent* aContent) {
  if (!aContent) {
    return Nothing();
  }

  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (!frame) {
    return Nothing();
  }
  Element* pseudoElement = nullptr;
  // Check if this is for a pseudo-element.
  if (IsPseudoElement(aContent)) {
    // If there are children, we want to expose the alt text on those instead,
    // so ignore it for the pseudo-element itself. The exception is ::marker,
    // since a list bullet is a single Accessible for the ::marker element and
    // its children are never exposed.
    if (aContent->HasChildren() &&
        !aContent->IsGeneratedContentContainerForMarker()) {
      return Nothing();
    }
    // No children only happens when there is alt text with an empty content
    // string; e.g. content: "" / "alt"
    // In this case, we need to expose the alt text on the pseudo-element
    // itself.
    pseudoElement = aContent->AsElement();
  } else if (aContent->IsInNativeAnonymousSubtree()) {
    if (!frame->IsReplaced()) {
      return Nothing();
    }
    Element* parent = aContent->GetParentElement();
    if (parent && IsPseudoElement(parent)) {
      // aContent is a child of a pseudo-element.
      pseudoElement = parent;
      // We need the frame from the pseudo-element to get the content style.
      frame = parent->GetPrimaryFrame();
      if (!frame) {
        return Nothing();
      }
    }
  }
  if (pseudoElement) {
    if (!pseudoElement->GetParentElement()) {
      return Nothing();
    }
  } else if (!aContent->IsElement() || !frame->IsReplaced()) {
    // CSS alt content doesn't apply because this is neither a pseudo-element
    // nor an element which has its content replaced using CSS content.
    return Nothing();
  }
  Span<const StyleContentItem> items = frame->StyleContent()->AltContentItems();
  if (items.IsEmpty()) {
    return Nothing();
  }

  nsString text;
  // There can be multiple alt text items.
  for (const auto& item : items) {
    if (item.IsString()) {
      AppendUTF8toUTF16(item.AsString().AsString(), text);
    }
  }
  return Some(std::move(text));
}

const nsIFrame* nsCoreUtils::GetAnchorForPositionedFrame(
    const PresShell* aPresShell, const nsIFrame* aPositionedFrame) {
  if (!aPositionedFrame ||
      !aPositionedFrame->Style()->HasAnchorPosReference()) {
    return nullptr;
  }

  ScopedNameRef anchorName{nullptr, StyleCascadeLevel::Default()};
  AnchorPosReferenceData* referencedAnchors =
      aPositionedFrame->GetProperty(nsIFrame::AnchorPosReferences());

  if (!referencedAnchors) {
    return nullptr;
  }

  for (const auto& entry : *referencedAnchors) {
    if (entry.GetData().isNothing()) {
      continue;
    }

    const auto& anchorKey = entry.GetKey();
    if (anchorName.mName && anchorKey.mName != anchorName.mName) {
      // Multiple anchors referenced.
      return nullptr;
    }

    anchorName = anchorKey;
  }

  return anchorName.mName ? aPresShell->GetAnchorPosAnchor(
                                anchorName, aPositionedFrame,
                                referencedAnchors->mFrameTreeDepth)
                          : nullptr;
}

nsIFrame* nsCoreUtils::GetPositionedFrameForAnchor(
    const PresShell* aPresShell, const nsIFrame* aAnchorFrame) {
  if (!aAnchorFrame) {
    return nullptr;
  }

  nsIFrame* positionedFrame = nullptr;
  const auto* styleDisp = aAnchorFrame->StyleDisplay();
  if (styleDisp->HasAnchorName()) {
    auto treeScope = styleDisp->mAnchorName.scope;
    for (auto& name : styleDisp->mAnchorName.AsSpan()) {
      for (nsIFrame* frame : aPresShell->GetAnchorPosPositioned()) {
        // Bug 1990069: We need to iterate over all positioned frames in doc and
        // check their referenced anchors because we don't store reverse mapping
        // from anchor to positioned frame.
        const auto* referencedAnchors =
            frame->GetProperty(nsIFrame::AnchorPosReferences());
        if (!referencedAnchors) {
          // Depending on where we are in the reflow, this property may or may
          // not be set. If it isn't set, a future reflow will set it, so we can
          // just skip this frame for now.
          continue;
        }
        const ScopedNameRef nameRef(name.AsAtom(), treeScope);
        const auto* data = referencedAnchors->Lookup(nameRef);
        if (data && *data && data->ref().mOffsetData) {
          if (aAnchorFrame ==
              aPresShell->GetAnchorPosAnchor(
                  nameRef, frame, referencedAnchors->mFrameTreeDepth)) {
            if (positionedFrame) {
              // Multiple positioned frames reference this anchor.
              return nullptr;
            }
            positionedFrame = frame;
          }
        }
      }
    }
  }

  return positionedFrame;
}
