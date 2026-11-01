/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AccEvent.h"
#include "AccGroupInfo.h"
#include "AccIterator.h"
#include "ApplicationAccessible.h"
#include "CachedTableAccessible.h"
#include "DocAccessible-inl.h"
#include "EmbeddedObjCollector.h"
#include "EventTree.h"
#include "HTMLElementAccessibles.h"
#include "HTMLSelectAccessible.h"
#include "HTMLTableAccessible.h"
#include "ImageAccessible.h"
#include "LocalAccessible-inl.h"
#include "NotificationController.h"
#include "OuterDocAccessible.h"
#include "Pivot.h"
#include "Relation.h"
#include "RootAccessible.h"
#include "States.h"
#include "TextLeafAccessible.h"
#include "TextLeafRange.h"
#include "TextRange.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/FocusModel.h"
#include "mozilla/PerfStats.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/a11y/AccAttributes.h"
#include "mozilla/a11y/DocAccessibleChild.h"
#include "mozilla/a11y/Platform.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "mozilla/dom/HTMLHeadingElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLLabelElement.h"
#include "mozilla/dom/KeyboardEventBinding.h"
#include "mozilla/dom/NodeList.h"
#include "mozilla/dom/PopoverData.h"
#include "mozilla/dom/TreeWalker.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/gfx/Matrix.h"
#include "nsAccUtils.h"
#include "nsAccessibilityService.h"
#include "nsAtom.h"
#include "nsComputedDOMStyle.h"
#include "nsContainerFrame.h"
#include "nsCoreUtils.h"
#include "nsDisplayList.h"
#include "nsEventShell.h"
#include "nsFocusManager.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIDOMXULButtonElement.h"
#include "nsIDOMXULSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDocShellTreeItem.h"
#include "nsIFormControl.h"
#include "nsIFrame.h"
#include "nsIMutationObserver.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsStyleStructInlines.h"
#include "nsTextEquivUtils.h"
#include "nsTextFrame.h"

using namespace mozilla;
using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible: nsISupports and cycle collection

NS_IMPL_CYCLE_COLLECTION_CLASS(LocalAccessible)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(LocalAccessible)
  tmp->Shutdown();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(LocalAccessible)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContent, mDoc)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(LocalAccessible)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(LocalAccessible)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, LocalAccessible)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(LocalAccessible)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(LocalAccessible, LastRelease())

LocalAccessible::LocalAccessible(nsIContent* aContent, DocAccessible* aDoc)
    : mContent(aContent),
      mDoc(aDoc),
      mParent(nullptr),
      mIndexInParent(-1),
      mFirstLineStart(-1),
      mStateFlags(0),
      mContextFlags(0),
      mReorderEventTarget(false),
      mShowEventTarget(false),
      mHideEventTarget(false),
      mIndexOfEmbeddedChild(-1),
      mGroupInfo(nullptr) {}

LocalAccessible::~LocalAccessible() {
  NS_ASSERTION(!mDoc, "LastRelease was never called!?!");
}

ENameValueFlag LocalAccessible::DirectName(nsString& aName) const {
  if (!HasOwnContent()) return eNameOK;

  ENameValueFlag nameFlag = ARIAName(aName);
  if (!aName.IsEmpty()) return nameFlag;

  nameFlag = NativeName(aName);
  aName.CompressWhitespace();

  return nameFlag;
}

ENameValueFlag LocalAccessible::Name(nsString& aName) const {
  aName.Truncate();

  ENameValueFlag nameFlag = DirectName(aName);
  if (!aName.IsEmpty()) return nameFlag;

  nsTextEquivUtils::GetNameFromSubtree(this, aName);
  if (!aName.IsEmpty()) return eNameFromSubtree;

  // In the end get the name from tooltip.
  if (Tooltip(aName)) {
    return eNameFromTooltip;
  }

  if (Maybe<nsString> cssAlt = nsCoreUtils::GetCssAltContent(mContent)) {
    // This covers two cases:
    // 1. The CSS content property replaces the content of an element with an
    // image plus alt text.
    // 2. Alt text is provided for a pseudo-element image.
    aName.Append(*cssAlt);
    return eNameOK;
  }

  aName.SetIsVoid(true);

  return eNameOK;
}

EDescriptionValueFlag LocalAccessible::Description(
    nsString& aDescription) const {
  // There are 4 conditions that make an accessible have no accDescription:
  // 1. it's a text node; or
  // 2. It has no ARIA describedby or description property
  // 3. it doesn't have an accName; or
  // 4. its title attribute already equals to its accName nsAutoString name;

  EDescriptionValueFlag descFlag = eDescriptionOK;
  aDescription.Truncate();

  if (!HasOwnContent() || mContent->IsText()) {
    return descFlag;
  }

  if (ARIADescription(aDescription)) {
    descFlag = eDescriptionFromARIA;
  }

  if (aDescription.IsEmpty()) {
    NativeDescription(aDescription);
    aDescription.CompressWhitespace();
  }

  if (aDescription.IsEmpty()) {
    Tooltip(aDescription);
  }

  if (!aDescription.IsEmpty()) {
    nsAutoString name;
    Name(name);
    // Don't expose a description if it is the same as the name.
    if (aDescription.Equals(name)) aDescription.Truncate();
  }

  return descFlag;
}

KeyBinding LocalAccessible::AccessKey() const {
  if (!HasOwnContent()) return KeyBinding();

  uint32_t key = nsCoreUtils::GetAccessKeyFor(mContent);
  if (!key && mContent->IsElement()) {
    LocalAccessible* label = nullptr;

    // Copy access key from label node.
    if (mContent->IsHTMLElement()) {
      // Unless it is labeled via an ancestor <label>, in which case that would
      // be redundant.
      HTMLLabelIterator iter(Document(), this,
                             HTMLLabelIterator::eSkipAncestorLabel);
      label = iter.Next();
    }
    if (!label) {
      XULLabelIterator iter(Document(), mContent);
      label = iter.Next();
    }

    if (label) key = nsCoreUtils::GetAccessKeyFor(label->GetContent());
  }

  if (!key) return KeyBinding();

  // Get modifier mask. Use ui.key.generalAccessKey (unless it is -1).
  switch (StaticPrefs::ui_key_generalAccessKey()) {
    case -1:
      break;
    case dom::KeyboardEvent_Binding::DOM_VK_SHIFT:
      return KeyBinding(key, KeyBinding::kShift);
    case dom::KeyboardEvent_Binding::DOM_VK_CONTROL:
      return KeyBinding(key, KeyBinding::kControl);
    case dom::KeyboardEvent_Binding::DOM_VK_ALT:
      return KeyBinding(key, KeyBinding::kAlt);
    case dom::KeyboardEvent_Binding::DOM_VK_META:
      return KeyBinding(key, KeyBinding::kMeta);
    default:
      return KeyBinding();
  }

  // Determine the access modifier used in this context.
  dom::Document* document = mContent->GetComposedDoc();
  if (!document) return KeyBinding();

  nsCOMPtr<nsIDocShellTreeItem> treeItem(document->GetDocShell());
  if (!treeItem) return KeyBinding();

  nsresult rv = NS_ERROR_FAILURE;
  int32_t modifierMask = 0;
  switch (treeItem->ItemType()) {
    case nsIDocShellTreeItem::typeChrome:
      modifierMask = StaticPrefs::ui_key_chromeAccess();
      rv = NS_OK;
      break;
    case nsIDocShellTreeItem::typeContent:
      modifierMask = StaticPrefs::ui_key_contentAccess();
      rv = NS_OK;
      break;
  }

  return NS_SUCCEEDED(rv) ? KeyBinding(key, modifierMask) : KeyBinding();
}

KeyBinding LocalAccessible::KeyboardShortcut() const { return KeyBinding(); }

uint64_t LocalAccessible::VisibilityState() const {
  if (IPCAccessibilityActive()) {
    // Visibility states must be calculated by RemoteAccessible, so there's no
    // point calculating them here.
    return 0;
  }
  nsIFrame* frame = GetFrame();
  if (!frame) {
    // Element having display:contents is considered visible semantically,
    // despite it doesn't have a visually visible box.
    if (nsCoreUtils::IsDisplayContents(mContent)) {
      return states::OFFSCREEN;
    }
    return states::INVISIBLE;
  }

  if (!frame->StyleVisibility()->IsVisible()) return states::INVISIBLE;

  // It's invisible if the presshell is hidden by a visibility:hidden element in
  // an ancestor document.
  if (frame->PresShell()->IsUnderHiddenEmbedderElement()) {
    return states::INVISIBLE;
  }

  // Offscreen state if the document's visibility state is not visible.
  if (Document()->IsHidden()) return states::OFFSCREEN;

  // Walk the parent frame chain to see if the frame is in background tab or
  // scrolled out.
  nsIFrame* curFrame = frame;
  do {
    if (nsMenuPopupFrame* popup = do_QueryFrame(curFrame)) {
      return popup->IsOpen() ? 0 : states::INVISIBLE;
    }

    if (curFrame->StyleUIReset()->mMozSubtreeHiddenOnlyVisually) {
      // Offscreen state for background tab content.
      return states::OFFSCREEN;
    }

    nsIFrame* parentFrame = curFrame->GetParent();
    // If contained by scrollable frame then check that at least 12 pixels
    // around the object is visible, otherwise the object is offscreen.
    const nscoord kMinPixels = nsPresContext::CSSPixelsToAppUnits(12);
    if (ScrollContainerFrame* scrollContainerFrame =
            do_QueryFrame(parentFrame)) {
      nsRect scrollPortRect = scrollContainerFrame->GetScrollPortRect();
      nsRect frameRect = nsLayoutUtils::TransformFrameRectToAncestor(
          frame, frame->GetRectRelativeToSelf(), parentFrame);
      if (!scrollPortRect.Contains(frameRect)) {
        scrollPortRect.Deflate(kMinPixels, kMinPixels);
        if (!scrollPortRect.Intersects(frameRect)) return states::OFFSCREEN;
      }
    }

    if (!parentFrame) {
      parentFrame = nsLayoutUtils::GetCrossDocParentFrameInProcess(curFrame);
      // Even if we couldn't find the parent frame, it might mean we are in an
      // out-of-process iframe, try to see if |frame| is scrolled out in an
      // scrollable frame in a cross-process ancestor document.
      if (!parentFrame &&
          nsLayoutUtils::FrameIsMostlyScrolledOutOfViewInCrossProcess(
              frame, kMinPixels)) {
        return states::OFFSCREEN;
      }
    }

    curFrame = parentFrame;
  } while (curFrame);

  // Zero area rects can occur in the first frame of a multi-frame text flow,
  // in which case the rendered text is not empty and the frame should not be
  // marked invisible.
  // XXX Can we just remove this check? Why do we need to mark empty
  // text invisible?
  if (frame->IsTextFrame() && !frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
      frame->GetRect().IsEmpty()) {
    nsIFrame::RenderedText text = frame->GetRenderedText(
        0, UINT32_MAX, nsIFrame::TextOffsetType::OffsetsInContentText,
        nsIFrame::TrailingWhitespace::DontTrim);
    if (text.mString.IsEmpty()) {
      return states::INVISIBLE;
    }
  }

  return 0;
}

uint64_t LocalAccessible::NativeState() const {
  uint64_t state = 0;

  if (!IsInDocument()) state |= states::STALE;

  if (HasOwnContent() && mContent->IsElement()) {
    dom::ElementState elementState = mContent->AsElement()->State();

    if (elementState.HasState(dom::ElementState::INVALID)) {
      state |= states::INVALID;
    }

    if (elementState.HasState(dom::ElementState::REQUIRED)) {
      state |= states::REQUIRED;
    }

    if (elementState.HasState(dom::ElementState::MODAL)) {
      state |= states::MODAL;
    }

    state |= NativeInteractiveState();
  }

  // Gather states::INVISIBLE and states::OFFSCREEN flags for this object.
  state |= VisibilityState();

  nsIFrame* frame = GetFrame();
  if (frame && frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    state |= states::FLOATING;
  }

  // Check if a XUL element has the popup attribute (an attached popup menu).
  if (HasOwnContent() && mContent->IsXULElement() &&
      mContent->AsElement()->HasAttr(nsGkAtoms::popup)) {
    state |= states::HASPOPUP;
  }

  // Bypass the link states specialization for non links.
  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
  if (!roleMapEntry || roleMapEntry->roleRule == kUseNativeRole ||
      roleMapEntry->role == roles::LINK) {
    state |= NativeLinkState();
  }

  return state;
}

uint64_t LocalAccessible::NativeInteractiveState() const {
  if (!mContent->IsElement()) return 0;

  if (NativelyUnavailable()) return states::UNAVAILABLE;

  nsIFrame* frame = GetFrame();
  auto flags = IsFocusableFlags(0);
  // If we're caching this remote document in the parent process, we
  // need to cache focusability irrespective of visibility. Otherwise,
  // if this document is invisible when it first loads, we'll cache that
  // all descendants are unfocusable and this won't get updated when the
  // document becomes visible. Even if we did get notified when the
  // document becomes visible, it would be wasteful to walk the entire
  // tree to figure out what is now focusable and push cache updates.
  // Although ignoring visibility means IsFocusable will return true for
  // visibility: hidden, etc., this isn't a problem because we don't include
  // those hidden elements in the a11y tree anyway.
  if (mDoc->IPCDoc()) {
    flags |= IsFocusableFlags::IgnoreVisibility;
  }
  if (frame && frame->IsFocusable(flags)) {
    return states::FOCUSABLE;
  }
  return 0;
}

uint64_t LocalAccessible::NativeLinkState() const { return 0; }

bool LocalAccessible::IsOnlyPlainContent() const {
  if (!IsPlainContent()) {
    return false;
  }

  uint32_t childCount = ChildCount();
  for (uint32_t i = 0; i < childCount; i++) {
    LocalAccessible* child = LocalChildAt(i);
    if (!child) {
      continue;
    }

    if (!child->IsOnlyPlainContent()) {
      return false;
    }
  }

  return true;
}

bool LocalAccessible::NativelyUnavailable() const {
  if (mContent->IsHTMLElement()) return mContent->AsElement()->IsDisabled();

  return mContent->IsElement() &&
         mContent->AsElement()->GetBoolAttr(nsGkAtoms::disabled);
}

Accessible* LocalAccessible::ChildAtPoint(int32_t aX, int32_t aY,
                                          EWhichChildAtPoint aWhichChild) {
  Accessible* child = LocalChildAtPoint(aX, aY, aWhichChild);
  if (aWhichChild != EWhichChildAtPoint::DirectChild && child &&
      child->IsOuterDoc()) {
    child = child->ChildAtPoint(aX, aY, aWhichChild);
  }

  return child;
}

LocalAccessible* LocalAccessible::LocalChildAtPoint(
    int32_t aX, int32_t aY, EWhichChildAtPoint aWhichChild) {
  // If we can't find the point in a child, we will return the fallback answer:
  // we return |this| if the point is within it, otherwise nullptr.
  LocalAccessible* fallbackAnswer = nullptr;
  LayoutDeviceIntRect rect = Bounds();
  if (rect.Contains(aX, aY)) fallbackAnswer = this;

  if (nsAccUtils::MustPrune(this)) {  // Do not dig any further
    return fallbackAnswer;
  }

  // Search an accessible at the given point starting from accessible document
  // because containing block (see CSS2) for out of flow element (for example,
  // absolutely positioned element) may be different from its DOM parent and
  // therefore accessible for containing block may be different from accessible
  // for DOM parent but GetFrameForPoint() should be called for containing block
  // to get an out of flow element.
  DocAccessible* accDocument = Document();
  NS_ENSURE_TRUE(accDocument, nullptr);

  nsIFrame* rootFrame = accDocument->GetFrame();
  NS_ENSURE_TRUE(rootFrame, nullptr);

  nsIFrame* startFrame = rootFrame;

  // Check whether the point is at popup content.
  nsIWidget* rootWidget = rootFrame->GetNearestWidget();
  NS_ENSURE_TRUE(rootWidget, nullptr);

  LayoutDeviceIntRect rootRect = rootWidget->GetScreenBounds();

  auto point = LayoutDeviceIntPoint(aX - rootRect.X(), aY - rootRect.Y());

  nsIFrame* popupFrame = nsLayoutUtils::GetPopupFrameForPoint(
      accDocument->PresContext()->GetRootPresContext(), rootWidget, point);
  if (popupFrame) {
    // If 'this' accessible is not inside the popup then ignore the popup when
    // searching an accessible at point.
    DocAccessible* popupDoc =
        GetAccService()->GetDocAccessible(popupFrame->GetContent()->OwnerDoc());
    LocalAccessible* popupAcc =
        popupDoc->GetAccessibleOrContainer(popupFrame->GetContent());
    LocalAccessible* popupChild = this;
    while (popupChild && !popupChild->IsDoc() && popupChild != popupAcc) {
      popupChild = popupChild->LocalParent();
    }

    if (popupChild == popupAcc) startFrame = popupFrame;
  }

  nsPresContext* presContext = startFrame->PresContext();
  nsRect screenRect = startFrame->GetScreenRectInAppUnits();
  nsPoint offset(presContext->DevPixelsToAppUnits(aX) - screenRect.X(),
                 presContext->DevPixelsToAppUnits(aY) - screenRect.Y());

  nsIFrame* foundFrame = nsLayoutUtils::GetFrameForPoint(
      RelativeTo{startFrame, ViewportType::Visual}, offset);

  nsIContent* content = nullptr;
  if (!foundFrame || !(content = foundFrame->GetContent())) {
    return fallbackAnswer;
  }

  // Get accessible for the node with the point or the first accessible in
  // the DOM parent chain.
  DocAccessible* contentDocAcc =
      GetAccService()->GetDocAccessible(content->OwnerDoc());

  // contentDocAcc in some circumstances can be nullptr. See bug 729861
  NS_ASSERTION(contentDocAcc, "could not get the document accessible");
  if (!contentDocAcc) return fallbackAnswer;

  LocalAccessible* accessible =
      contentDocAcc->GetAccessibleOrContainer(content);
  if (!accessible) return fallbackAnswer;

  // Hurray! We have an accessible for the frame that layout gave us.
  // Since DOM node of obtained accessible may be out of flow then we should
  // ensure obtained accessible is a child of this accessible.
  LocalAccessible* child = accessible;
  while (child != this) {
    LocalAccessible* parent = child->LocalParent();
    if (!parent) {
      // Reached the top of the hierarchy. These bounds were inside an
      // accessible that is not a descendant of this one.
      return fallbackAnswer;
    }

    // If we landed on a legitimate child of |this|, and we want the direct
    // child, return it here.
    if (parent == this && aWhichChild == EWhichChildAtPoint::DirectChild) {
      return child;
    }

    child = parent;
  }

  // Manually walk through accessible children and see if the are within this
  // point. Skip offscreen or invisible accessibles. This takes care of cases
  // where layout won't walk into things for us, such as image map areas and
  // sub documents (XXX: subdocuments should be handled by methods of
  // OuterDocAccessibles).
  uint32_t childCount = accessible->ChildCount();
  if (childCount == 1 && accessible->IsOuterDoc() &&
      accessible->FirstChild()->IsRemote()) {
    // No local children.
    return accessible;
  }
  for (uint32_t childIdx = 0; childIdx < childCount; childIdx++) {
    LocalAccessible* child = accessible->LocalChildAt(childIdx);

    LayoutDeviceIntRect childRect = child->Bounds();
    if (childRect.Contains(aX, aY) &&
        (child->State() & states::INVISIBLE) == 0) {
      if (aWhichChild == EWhichChildAtPoint::DeepestChild) {
        return child->LocalChildAtPoint(aX, aY,
                                        EWhichChildAtPoint::DeepestChild);
      }

      return child;
    }
  }

  return accessible;
}

nsIFrame* LocalAccessible::FindNearestAccessibleAncestorFrame() const {
  nsIFrame* frame = GetFrame();
  if (frame &&
      frame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
      nsLayoutUtils::IsReallyFixedPos(frame)) {
    return mDoc->PresShellPtr()->GetRootFrame();
  }

  if (IsDoc()) {
    // We bound documents by their own frame, which is their PresShell's root
    // frame. We cache the document offset elsewhere in BundleFieldsForCache
    // using the nsGkAtoms::crossorigin attribute.
    MOZ_ASSERT(frame, "DocAccessibles should always have a frame");
    return frame;
  }

  // Iterate through accessible's ancestors to find one with a frame.
  LocalAccessible* ancestor = mParent;
  while (ancestor) {
    if (nsIFrame* boundingFrame = ancestor->GetFrame()) {
      return boundingFrame;
    }
    ancestor = ancestor->LocalParent();
  }

  return frame ? nsLayoutUtils::GetContainingBlockForClientRect(frame)
               : nullptr;
}

nsRect LocalAccessible::ParentRelativeBounds() {
  nsIFrame* frame = GetFrame();
  if (frame && mContent) {
    nsIFrame* boundingFrame = FindNearestAccessibleAncestorFrame();
    nsRect result = nsLayoutUtils::GetAllInFlowRectsUnion(frame, boundingFrame);

    if (result.IsEmpty()) {
      // If we end up with a 0x0 rect from above (or one with negative
      // height/width) we should try using the ink overflow rect instead. If we
      // use this rect, our relative bounds will match the bounds of what
      // appears visually. We do this because some web authors (icloud.com for
      // example) employ things like 0x0 buttons with visual overflow. Without
      // this, such frames aren't navigable by screen readers.
      result = frame->InkOverflowRectRelativeToSelf();
      result.MoveBy(frame->GetOffsetTo(boundingFrame));
    }

    if (boundingFrame->GetRect().IsEmpty() ||
        nsLayoutUtils::GetNextContinuationOrIBSplitSibling(boundingFrame)) {
      // Constructing a bounding box across a frame that has an IB split means
      // the origin is likely be different from that of boundingFrame.
      // Descendants will need their parent-relative bounds adjusted
      // accordingly, since parent-relative bounds are constructed to the
      // bounding box of the entire element and not each individual IB split
      // frame. In the case that boundingFrame's rect is empty,
      // GetAllInFlowRectsUnion might exclude its origin. For example, if
      // boundingFrame is empty with an origin of (0, -840) but has a non-empty
      // ib-split-sibling with (0, 0), the union rect will originate at (0, 0).
      // This means the bounds returned for our parent Accessible might be
      // offset from boundingFrame's rect. Since result is currently relative to
      // boundingFrame's rect, we might need to adjust it to make it parent
      // relative.
      nsRect boundingUnion =
          nsLayoutUtils::GetAllInFlowRectsUnion(boundingFrame, boundingFrame);
      if (!boundingUnion.IsEmpty()) {
        // The origin of boundingUnion is relative to boundingFrame, meaning
        // when we call MoveBy on result with this value we're offsetting
        // `result` by the distance boundingFrame's origin was moved to
        // construct its bounding box.
        result.MoveBy(-boundingUnion.TopLeft());
      } else {
        // Since GetAllInFlowRectsUnion returned an empty rect on our parent
        // Accessible, we would have used the ink overflow rect. However,
        // GetAllInFlowRectsUnion calculates relative to the bounding frame's
        // main rect, not its ink overflow rect. We need to adjust for the ink
        // overflow offset to make our result parent relative.
        nsRect boundingOverflow =
            boundingFrame->InkOverflowRectRelativeToSelf();
        result.MoveBy(-boundingOverflow.TopLeft());
      }
    }

    if (frame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
        nsLayoutUtils::IsReallyFixedPos(frame)) {
      // If we're dealing with a fixed position frame, we've already made it
      // relative to the document which should have gotten rid of its scroll
      // offset.
      return result;
    }

    if (!IsDoc()) {
      if (ScrollContainerFrame* sf =
              boundingFrame == mDoc->GetFrame()
                  ? mDoc->PresShellPtr()->GetRootScrollContainerFrame()
                  : boundingFrame->GetScrollTargetFrame()) {
        // If boundingFrame has a scroll position, result is currently relative
        // to that. Instead, we want result to remain the same regardless of
        // scrolling. We then subtract the scroll position later when
        // calculating absolute bounds. We do this because we don't want to push
        // cache updates for the bounds of all descendants every time we scroll.
        // We don't need to do this for the document because document bounds are
        // always computed to be self-relative -- see
        // FindNearestAccessibleAncestorFrame. However, if boundingFrame is the
        // document's frame (e.g. mParent had no frame, and the document is the
        // nearest frame-having ancestor), we have to account for any scroll
        // offset between the doc and its content. This is tracked in the
        // PresShell's root scroll container frame.
        nsPoint scrollPos = sf->GetScrollPosition().ApplyResolution(
            mDoc->PresShellPtr()->GetResolution());
        result.MoveBy(scrollPos.x, scrollPos.y);
      }
    }

    return result;
  }

  return nsRect();
}

nsRect LocalAccessible::RelativeBounds(nsIFrame** aBoundingFrame) const {
  nsIFrame* frame = GetFrame();
  if (frame && mContent) {
    *aBoundingFrame = nsLayoutUtils::GetContainingBlockForClientRect(frame);
    nsRect unionRect = nsLayoutUtils::GetAllInFlowRectsUnion(
        frame, *aBoundingFrame,
        nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms);

    if (unionRect.IsEmpty()) {
      // If we end up with a 0x0 rect from above (or one with negative
      // height/width) we should try using the ink overflow rect instead. If we
      // use this rect, our relative bounds will match the bounds of what
      // appears visually. We do this because some web authors (icloud.com for
      // example) employ things like 0x0 buttons with visual overflow. Without
      // this, such frames aren't navigable by screen readers.
      nsRect overflow = frame->InkOverflowRectRelativeToSelf();
      nsLayoutUtils::TransformRect(frame, *aBoundingFrame, overflow);
      return overflow;
    }

    return unionRect;
  }

  return nsRect();
}

nsRect LocalAccessible::BoundsInAppUnits() const {
  nsIFrame* boundingFrame = nullptr;
  nsRect unionRectTwips = RelativeBounds(&boundingFrame);
  if (!boundingFrame) {
    if (nsCoreUtils::IsDisplayContents(mContent)) {
      nsRect result;
      for (uint32_t i = 0, n = ChildCount(); i < n; i++) {
        result.UnionRect(result, LocalChildAt(i)->BoundsInAppUnits());
      }
      return result;
    }
    return nsRect();
  }

  PresShell* presShell = mDoc->PresContext()->PresShell();

  // We need to inverse translate with the offset of the edge of the visual
  // viewport from top edge of the layout viewport.
  nsPoint viewportOffset = presShell->GetVisualViewportOffset() -
                           presShell->GetLayoutViewportOffset();
  unionRectTwips.MoveBy(-viewportOffset);

  // We need to take into account a non-1 resolution set on the presshell.
  // This happens with async pinch zooming. Here we scale the bounds before
  // adding the screen-relative offset.
  unionRectTwips.ScaleRoundOut(presShell->GetResolution());
  // We have the union of the rectangle, now we need to put it in absolute
  // screen coords.
  nsRect orgRectPixels = boundingFrame->GetScreenRectInAppUnits();
  unionRectTwips.MoveBy(orgRectPixels.X(), orgRectPixels.Y());

  return unionRectTwips;
}

LayoutDeviceIntRect LocalAccessible::Bounds() const {
  return LayoutDeviceIntRect::FromAppUnitsToNearest(
      BoundsInAppUnits(), mDoc->PresContext()->AppUnitsPerDevPixel());
}

void LocalAccessible::SetSelected(bool aSelect) {
  if (!HasOwnContent()) return;

  if (nsAccUtils::GetSelectableContainer(this, State()) && aSelect) {
    TakeFocus();
  }
}

void LocalAccessible::TakeSelection() {
  LocalAccessible* select = nsAccUtils::GetSelectableContainer(this, State());
  if (select) {
    if (select->State() & states::MULTISELECTABLE) select->UnselectAll();
    SetSelected(true);
  }
}

void LocalAccessible::TakeFocus() const {
  nsIFrame* frame = GetFrame();
  if (!frame) return;

  nsIContent* focusContent = mContent;

  // If the accessible focus is managed by container widget then focus the
  // widget and set the accessible as its current item.
  if (!frame->IsFocusable()) {
    LocalAccessible* widget = ContainerWidget();
    if (widget && widget->AreItemsOperable()) {
      nsIContent* widgetElm = widget->GetContent();
      nsIFrame* widgetFrame = widgetElm->GetPrimaryFrame();
      if (widgetFrame && widgetFrame->IsFocusable()) {
        focusContent = widgetElm;
        widget->SetCurrentItem(this);
      }
    }
  }

  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    dom::AutoHandlingUserInputStatePusher inputStatePusher(true);
    // XXXbz: Can we actually have a non-element content here?
    RefPtr<dom::Element> element = dom::Element::FromNodeOrNull(focusContent);
    fm->SetFocus(element, 0);
  }
}

void LocalAccessible::NameFromAssociatedXULLabel(DocAccessible* aDocument,
                                                 nsIContent* aElm,
                                                 nsString& aName) {
  LocalAccessible* label = nullptr;
  XULLabelIterator iter(aDocument, aElm);
  while ((label = iter.Next())) {
    // Check if label's value attribute is used
    label->Elm()->GetAttr(nsGkAtoms::value, aName);
    if (aName.IsEmpty()) {
      // If no value attribute, a non-empty label must contain
      // children that define its text -- possibly using HTML
      nsTextEquivUtils::AppendTextEquivFromContent(label, label->Elm(), &aName);
    }
  }
  aName.CompressWhitespace();
}

void LocalAccessible::XULElmName(DocAccessible* aDocument, nsIContent* aElm,
                                 nsString& aName) {
  /**
   * 3 main cases for XUL Controls to be labeled
   *   1 - control contains label="foo"
   *   2 - non-child label contains control="controlID"
   *        - label has either value="foo" or children
   *   3 - name from subtree; e.g. a child label element
   * Cases 1 and 2 are handled here.
   * Case 3 is handled by GetNameFromSubtree called in NativeName.
   * Once a label is found, the search is discontinued, so a control
   *  that has a label attribute as well as having a label external to
   *  the control that uses the control="controlID" syntax will use
   *  the label attribute for its Name.
   */

  // CASE #1 (via label attribute) -- great majority of the cases
  // Only do this if this is not a select control element, which uses label
  // attribute to indicate, which option is selected.
  nsCOMPtr<nsIDOMXULSelectControlElement> select =
      aElm->AsElement()->AsXULSelectControl();
  if (!select) {
    aElm->AsElement()->GetAttr(nsGkAtoms::label, aName);
  }

  // CASE #2 -- label as <label control="id" ... ></label>
  if (aName.IsEmpty()) {
    NameFromAssociatedXULLabel(aDocument, aElm, aName);
  }

  aName.CompressWhitespace();
}

nsresult LocalAccessible::HandleAccEvent(AccEvent* aEvent) {
  NS_ENSURE_ARG_POINTER(aEvent);

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString strEventType;
    GetAccService()->GetStringEventType(aEvent->GetEventType(), strEventType);
    nsAutoCString strMarker;
    strMarker.AppendLiteral("A11y Event - ");
    strMarker.Append(strEventType);
    PROFILER_MARKER_UNTYPED(strMarker, A11Y);
  }

  if (IPCAccessibilityActive() && Document()) {
    DocAccessibleChild* ipcDoc = mDoc->IPCDoc();
    // If ipcDoc is null, we can't fire the event to the client in the parent
    // process. This could happen for two reasons:
    // 1. There is no client in the parent process, so we didn't create a
    // DocAccessibleChild.
    // 2. A DocAccessibleChild would have been created, but we incorrectly fired
    // an event before that happened. In this case, we shouldn't have fired the
    // event in the first place, since this makes events inconsistent for local
    // and remote documents. To avoid this, don't call nsEventShell::FireEvent
    // on a DocAccessible for which HasLoadState(eTreeConstructed) is false.
    MOZ_ASSERT(mDoc->HasLoadState(DocAccessible::eTreeConstructed));
    if (ipcDoc) {
      uint64_t id = aEvent->GetAccessible()->ID();

      switch (aEvent->GetEventType()) {
        case nsIAccessibleEvent::EVENT_SHOW:
          ipcDoc->ShowEvent(downcast_accEvent(aEvent));
          break;

        case nsIAccessibleEvent::EVENT_HIDE:
          ipcDoc->PushMutationEventData(
              HideEventData{id, aEvent->IsFromUserInput()});
          break;

        case nsIAccessibleEvent::EVENT_INNER_REORDER:
        case nsIAccessibleEvent::EVENT_REORDER:
          if (IsTable()) {
            SendCache(CacheDomain::Table, CacheUpdateType::Update,
                      /*aAppendEventData*/ true);
          }

#if defined(XP_WIN)
          if (HasOwnContent() && mContent->IsMathMLElement()) {
            // For any change in a MathML subtree, update the innerHTML cache on
            // the root math element.
            for (LocalAccessible* acc = this; acc; acc = acc->LocalParent()) {
              if (acc->HasOwnContent() &&
                  acc->mContent->IsMathMLElement(nsGkAtoms::math)) {
                mDoc->QueueCacheUpdate(acc, CacheDomain::InnerHTML);
              }
            }
          }
#endif  // defined(XP_WIN)

          // reorder events on the application acc aren't necessary to tell the
          // parent about new top level documents.
          if (!aEvent->GetAccessible()->IsApplication()) {
            ipcDoc->PushMutationEventData(
                ReorderEventData{id, aEvent->GetEventType()});
          }
          break;
        case nsIAccessibleEvent::EVENT_STATE_CHANGE: {
          AccStateChangeEvent* event = downcast_accEvent(aEvent);
          ipcDoc->SendStateChangeEvent(id, event->GetState(),
                                       event->IsStateEnabled());
          break;
        }
        case nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED: {
          AccCaretMoveEvent* event = downcast_accEvent(aEvent);
          ipcDoc->SendCaretMoveEvent(
              id,
              DocAccessibleChild::GetCaretRectForIPCEvent(
                  aEvent->GetAccessible()),
              event->GetCaretOffset(), event->IsSelectionCollapsed(),
              event->IsAtEndOfLine(), event->GetGranularity(),
              event->IsFromUserInput(), /* aSuppressEvent */ false);
          break;
        }
        case nsIAccessibleEvent::EVENT_TEXT_INSERTED:
        case nsIAccessibleEvent::EVENT_TEXT_REMOVED: {
          AccTextChangeEvent* event = downcast_accEvent(aEvent);
          ipcDoc->PushMutationEventData(TextChangeEventData{
              id, event->ModifiedText(), event->GetStartOffset(),
              event->GetLength(), event->IsTextInserted(),
              event->IsFromUserInput()});
          break;
        }
        case nsIAccessibleEvent::EVENT_SELECTION:
        case nsIAccessibleEvent::EVENT_SELECTION_ADD:
        case nsIAccessibleEvent::EVENT_SELECTION_REMOVE: {
          AccSelChangeEvent* selEvent = downcast_accEvent(aEvent);
          ipcDoc->SendSelectionEvent(id, selEvent->Widget()->ID(),
                                     aEvent->GetEventType());
          break;
        }
        case nsIAccessibleEvent::EVENT_FOCUS:
          ipcDoc->SendFocusEvent(id,
                                 DocAccessibleChild::GetCaretRectForIPCEvent(
                                     aEvent->GetAccessible()));
          break;
        case nsIAccessibleEvent::EVENT_SCROLLING_END:
        case nsIAccessibleEvent::EVENT_SCROLLING: {
          AccScrollingEvent* scrollingEvent = downcast_accEvent(aEvent);
          ipcDoc->SendScrollingEvent(
              id, aEvent->GetEventType(), scrollingEvent->ScrollX(),
              scrollingEvent->ScrollY(), scrollingEvent->MaxScrollX(),
              scrollingEvent->MaxScrollY());
          break;
        }
        case nsIAccessibleEvent::EVENT_ANNOUNCEMENT: {
          AccAnnouncementEvent* announcementEvent = downcast_accEvent(aEvent);
          ipcDoc->SendAnnouncementEvent(id, announcementEvent->Announcement(),
                                        announcementEvent->Priority());
          break;
        }
        case nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED: {
          AccTextSelChangeEvent* textSelChangeEvent = downcast_accEvent(aEvent);
          AutoTArray<TextRange, 1> ranges;
          textSelChangeEvent->SelectionRanges(&ranges);
          nsTArray<TextRangeData> textRangeData(ranges.Length());
          for (size_t i = 0; i < ranges.Length(); i++) {
            const TextRange& range = ranges.ElementAt(i);
            LocalAccessible* start = range.StartContainer()->AsLocal();
            LocalAccessible* end = range.EndContainer()->AsLocal();
            textRangeData.AppendElement(TextRangeData(start->ID(), end->ID(),
                                                      range.StartOffset(),
                                                      range.EndOffset()));
          }
          ipcDoc->SendTextSelectionChangeEvent(id, textRangeData);
          break;
        }
        case nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE:
        case nsIAccessibleEvent::EVENT_NAME_CHANGE: {
          SendCache(CacheDomain::NameAndDescription, CacheUpdateType::Update);
          ipcDoc->SendEvent(id, aEvent->GetEventType());
          break;
        }
        case nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE:
        case nsIAccessibleEvent::EVENT_VALUE_CHANGE: {
          SendCache(CacheDomain::Value, CacheUpdateType::Update);
          ipcDoc->SendEvent(id, aEvent->GetEventType());
          break;
        }
        default:
          ipcDoc->SendEvent(id, aEvent->GetEventType());
      }
    }
  }

  if (nsCoreUtils::AccEventObserversExist()) {
    nsCoreUtils::DispatchAccEvent(MakeXPCEvent(aEvent));
  }

  if (IPCAccessibilityActive()) {
    return NS_OK;
  }

  if (IsDefunct()) {
    // This could happen if there is an XPCOM observer, since script might run
    // which mutates the tree.
    return NS_OK;
  }

  LocalAccessible* target = aEvent->GetAccessible();
  switch (aEvent->GetEventType()) {
    case nsIAccessibleEvent::EVENT_SHOW: {
      // Scope for PerfStats
      AUTO_PROFILER_MARKER_TEXT("a11y::PlatformShowHideEvent", A11Y, {}, ""_ns);
      PerfStats::AutoMetricRecording<
          PerfStats::Metric::A11Y_PlatformShowHideEvent>
          autoRecording;
      // WITHIN THIS SCOPE, DO NOT ADD CODE ABOVE THIS BLOCK:
      // THIS CODE IS MEASURING TIMINGS.
      PlatformShowHideEvent(target, target->LocalParent(), true,
                            aEvent->IsFromUserInput());
      break;
    }
    case nsIAccessibleEvent::EVENT_HIDE: {
      // Scope for PerfStats
      AUTO_PROFILER_MARKER_TEXT("a11y::PlatformShowHideEvent", A11Y, {}, ""_ns);
      PerfStats::AutoMetricRecording<
          PerfStats::Metric::A11Y_PlatformShowHideEvent>
          autoRecording;
      // WITHIN THIS SCOPE, DO NOT ADD CODE ABOVE THIS BLOCK:
      // THIS CODE IS MEASURING TIMINGS.
      PlatformShowHideEvent(target, target->LocalParent(), false,
                            aEvent->IsFromUserInput());
      break;
    }
    case nsIAccessibleEvent::EVENT_STATE_CHANGE: {
      AccStateChangeEvent* event = downcast_accEvent(aEvent);
      PlatformStateChangeEvent(target, event->GetState(),
                               event->IsStateEnabled());
      break;
    }
    case nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED: {
      AccCaretMoveEvent* event = downcast_accEvent(aEvent);
      PlatformCaretMoveEvent(target, event->GetCaretOffset(),
                             event->IsSelectionCollapsed(),
                             event->GetGranularity(), event->IsFromUserInput());
      break;
    }
    case nsIAccessibleEvent::EVENT_TEXT_INSERTED:
    case nsIAccessibleEvent::EVENT_TEXT_REMOVED: {
      AccTextChangeEvent* event = downcast_accEvent(aEvent);
      const nsString& text = event->ModifiedText();
      PlatformTextChangeEvent(target, text, event->GetStartOffset(),
                              event->GetLength(), event->IsTextInserted(),
                              event->IsFromUserInput());
      break;
    }
    case nsIAccessibleEvent::EVENT_SELECTION:
    case nsIAccessibleEvent::EVENT_SELECTION_ADD:
    case nsIAccessibleEvent::EVENT_SELECTION_REMOVE: {
      AccSelChangeEvent* selEvent = downcast_accEvent(aEvent);
      PlatformSelectionEvent(target, selEvent->Widget(),
                             aEvent->GetEventType());
      break;
    }
    case nsIAccessibleEvent::EVENT_FOCUS: {
      PlatformFocusEvent(target);
      break;
    }
#if defined(ANDROID)
    case nsIAccessibleEvent::EVENT_SCROLLING_END:
    case nsIAccessibleEvent::EVENT_SCROLLING: {
      AccScrollingEvent* scrollingEvent = downcast_accEvent(aEvent);
      PlatformScrollingEvent(
          target, aEvent->GetEventType(), scrollingEvent->ScrollX(),
          scrollingEvent->ScrollY(), scrollingEvent->MaxScrollX(),
          scrollingEvent->MaxScrollY());
      break;
    }
#endif  // defined(ANDROID)
    case nsIAccessibleEvent::EVENT_ANNOUNCEMENT: {
      AccAnnouncementEvent* announcementEvent = downcast_accEvent(aEvent);
      PlatformAnnouncementEvent(target, announcementEvent->Announcement(),
                                announcementEvent->Priority());
      break;
    }
#if defined(MOZ_WIDGET_COCOA)
    case nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED: {
      AccTextSelChangeEvent* textSelChangeEvent = downcast_accEvent(aEvent);
      AutoTArray<TextRange, 1> ranges;
      textSelChangeEvent->SelectionRanges(&ranges);
      PlatformTextSelectionChangeEvent(target, ranges);
      break;
    }
#endif  // defined(MOZ_WIDGET_COCOA)
    default:
      PlatformEvent(target, aEvent->GetEventType());
  }

  return NS_OK;
}

already_AddRefed<AccAttributes> LocalAccessible::Attributes() {
  RefPtr<AccAttributes> attributes = NativeAttributes();
  if (!HasOwnContent() || !mContent->IsElement()) return attributes.forget();

  // 'xml-roles' attribute coming from ARIA.
  nsString xmlRoles;
  if (nsAccUtils::GetARIAAttr(mContent->AsElement(), nsGkAtoms::role,
                              xmlRoles) &&
      !xmlRoles.IsEmpty()) {
    attributes->SetAttribute(nsGkAtoms::xmlroles, std::move(xmlRoles));
  } else if (nsAtom* landmark = LandmarkRole()) {
    // 'xml-roles' attribute for landmark.
    attributes->SetAttribute(nsGkAtoms::xmlroles, landmark);
  }

  // Expose object attributes from ARIA attributes.
  aria::AttrIterator attribIter(mContent);
  while (attribIter.Next()) {
    if (attribIter.AttrName() == nsGkAtoms::aria_placeholder &&
        attributes->HasAttribute(nsGkAtoms::placeholder)) {
      // If there is an HTML placeholder attribute exposed by
      // HTMLTextFieldAccessible::NativeAttributes, don't expose
      // aria-placeholder.
      continue;
    }
    attribIter.ExposeAttr(attributes);
  }

  if (HasCustomActions()) {
    attributes->SetAttribute(nsGkAtoms::hasActions, true);
  }

  // If there is no aria-live attribute then expose default value of 'live'
  // object attribute used for ARIA role of this accessible.
  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
  if (roleMapEntry) {
    if (roleMapEntry->Is(nsGkAtoms::searchbox)) {
      attributes->SetAttribute(nsGkAtoms::textInputType, nsGkAtoms::search);
    }

    if (!attributes->HasAttribute(nsGkAtoms::aria_live)) {
      nsString live;
      if (nsAccUtils::GetLiveAttrValue(roleMapEntry->liveAttRule, live)) {
        attributes->SetAttribute(nsGkAtoms::aria_live, std::move(live));
      }
    }
  }

  nsString detailsFrom;
  AssociatedElementsIterator iter(mDoc, Elm(), nsGkAtoms::aria_details);
  if (iter.Next()) {
    detailsFrom.AssignLiteral("aria-details");
  } else if (GetCommandForDetailsRelation()) {
    detailsFrom.AssignLiteral("command-for");
  } else if (GetPopoverTargetDetailsRelation()) {
    detailsFrom.AssignLiteral("popover-target");
  } else if (GetAnchorPositionTargetDetailsRelation()) {
    detailsFrom.AssignLiteral("css-anchor");
  }

  if (!detailsFrom.IsEmpty()) {
    attributes->SetAttribute(nsGkAtoms::details_from, std::move(detailsFrom));
  }

  return attributes.forget();
}

already_AddRefed<AccAttributes> LocalAccessible::NativeAttributes() {
  auto attributes = MakeRefPtr<AccAttributes>();

  // We support values, so expose the string value as well, via the valuetext
  // object attribute. We test for the value interface because we don't want
  // to expose traditional Value() information such as URL's on links and
  // documents, or text in an input.
  if (HasNumericValue()) {
    nsString valuetext;
    Value(valuetext);
    attributes->SetAttribute(nsGkAtoms::aria_valuetext, std::move(valuetext));
  }

  // Expose checkable object attribute if the accessible has checkable state
  if (State() & states::CHECKABLE) {
    attributes->SetAttribute(nsGkAtoms::checkable, true);
  }

  // Expose 'explicit-name' attribute.
  nsAutoString name;
  if (Name(name) != eNameFromSubtree && !name.IsVoid()) {
    attributes->SetAttribute(nsGkAtoms::explicit_name, true);
  }

  bool hierarchical = false;
  uint32_t itemCount = AccGroupInfo::TotalItemCount(this, &hierarchical);
  if (itemCount) {
    attributes->SetAttribute(nsGkAtoms::child_item_count,
                             static_cast<int32_t>(itemCount));
  }

  if (hierarchical) {
    attributes->SetAttribute(nsGkAtoms::tree, true);
  }

  // If the accessible doesn't have own content (such as list item bullet or
  // xul tree item) then don't calculate content based attributes.
  if (!HasOwnContent()) return attributes.forget();

  // Get container-foo computed live region properties based on the closest
  // container with the live region attribute. Inner nodes override outer nodes
  // within the same document. The inner nodes can be used to override live
  // region behavior on more general outer nodes.
  nsAccUtils::SetLiveContainerAttributes(attributes, this);

  if (!mContent->IsElement()) return attributes.forget();

  nsString id;
  if (nsCoreUtils::GetID(mContent, id)) {
    attributes->SetAttribute(nsGkAtoms::id, std::move(id));
  }

  // Expose class because it may have useful microformat information.
  nsString _class;
  if (mContent->AsElement()->GetAttr(nsGkAtoms::_class, _class)) {
    attributes->SetAttribute(nsGkAtoms::_class, std::move(_class));
  }

  // Expose tag.
  attributes->SetAttribute(nsGkAtoms::tag, mContent->NodeInfo()->NameAtom());

  if (auto htmlElement = nsGenericHTMLElement::FromNode(mContent)) {
    // Expose draggable object attribute.
    if (htmlElement->Draggable()) {
      attributes->SetAttribute(nsGkAtoms::draggable, true);
    }
    nsString popover;
    htmlElement->GetPopover(popover);
    if (!popover.IsEmpty()) {
      attributes->SetAttribute(nsGkAtoms::ispopup, std::move(popover));
    }
  }

  // Don't calculate CSS-based object attributes when:
  // 1. There is no frame (e.g. the accessible is unattached from the tree).
  // 2. This is an image map area. CSS is irrelevant here. Furthermore, we won't
  // be able to get the computed style if the map is unslotted in a shadow host.
  nsIFrame* f = mContent->GetPrimaryFrame();
  if (!f || mContent->IsHTMLElement(nsGkAtoms::area)) {
    return attributes.forget();
  }

  // Expose 'display' attribute.
  if (RefPtr<nsAtom> display = DisplayStyle()) {
    attributes->SetAttribute(nsGkAtoms::display, display);
  }

  const ComputedStyle& style = *f->Style();
  auto Atomize = [&](NonCustomCSSPropertyId aId) -> RefPtr<nsAtom> {
    nsAutoCString value;
    style.GetComputedPropertyValue(aId, value);
    return NS_Atomize(value);
  };

  // Expose 'text-align' attribute.
  attributes->SetAttribute(nsGkAtoms::textAlign,
                           Atomize(eCSSProperty_text_align));

  // Expose 'text-indent' attribute.
  attributes->SetAttribute(nsGkAtoms::textIndent,
                           Atomize(eCSSProperty_text_indent));

  auto GetMargin = [&](mozilla::Side aSide) -> CSSCoord {
    // This is here only to guarantee that we do the same as getComputedStyle
    // does, so that we don't hit precision errors in tests.
    const auto margin =
        f->StyleMargin()->GetMargin(aSide, AnchorPosResolutionParams::From(f));
    if (margin->ConvertsToLength()) {
      return margin->AsLengthPercentage().ToLengthInCSSPixels();
    }

    nscoord coordVal = f->GetUsedMargin().Side(aSide);
    return CSSPixel::FromAppUnits(coordVal);
  };

  // Expose 'margin-left' attribute.
  attributes->SetAttribute(nsGkAtoms::marginLeft, GetMargin(eSideLeft));

  // Expose 'margin-right' attribute.
  attributes->SetAttribute(nsGkAtoms::marginRight, GetMargin(eSideRight));

  // Expose 'margin-top' attribute.
  attributes->SetAttribute(nsGkAtoms::marginTop, GetMargin(eSideTop));

  // Expose 'margin-bottom' attribute.
  attributes->SetAttribute(nsGkAtoms::marginBottom, GetMargin(eSideBottom));

  // Expose data-at-shortcutkeys attribute for web applications and virtual
  // cursors. Currently mostly used by JAWS.
  nsString atShortcutKeys;
  if (mContent->AsElement()->GetAttr(
          kNameSpaceID_None, nsGkAtoms::dataAtShortcutkeys, atShortcutKeys)) {
    attributes->SetAttribute(nsGkAtoms::dataAtShortcutkeys,
                             std::move(atShortcutKeys));
  }

  return attributes.forget();
}

bool LocalAccessible::AttributeChangesState(nsAtom* aAttribute) {
  return aAttribute == nsGkAtoms::aria_disabled ||
         // The HTML element disabled state gets handled in
         // DocAccessible::ElementStateChanged. This matches
         // LocalAccessible::NativelyUnavailable.
         (aAttribute == nsGkAtoms::disabled && !mContent->IsHTMLElement()) ||
         aAttribute == nsGkAtoms::tabindex ||
         aAttribute == nsGkAtoms::aria_required ||
         aAttribute == nsGkAtoms::aria_invalid ||
         aAttribute == nsGkAtoms::aria_expanded ||
         aAttribute == nsGkAtoms::aria_checked ||
         (aAttribute == nsGkAtoms::aria_pressed && IsButton()) ||
         aAttribute == nsGkAtoms::aria_readonly ||
         aAttribute == nsGkAtoms::aria_current ||
         aAttribute == nsGkAtoms::aria_haspopup ||
         aAttribute == nsGkAtoms::aria_busy ||
         aAttribute == nsGkAtoms::aria_multiline ||
         aAttribute == nsGkAtoms::aria_multiselectable ||
         // We track this for focusable state update
         aAttribute == nsGkAtoms::commandfor ||
         aAttribute == nsGkAtoms::contenteditable ||
         aAttribute == nsGkAtoms::popovertarget;
}

void LocalAccessible::DOMAttributeChanged(int32_t aNameSpaceID,
                                          nsAtom* aAttribute,
                                          AttrModType aModType,
                                          const nsAttrValue* aOldValue,
                                          uint64_t aOldState) {
  // Fire accessible event after short timer, because we need to wait for
  // DOM attribute & resulting layout to actually change. Otherwise,
  // assistive technology will retrieve the wrong state/value/selection info.

  // XXX todo
  // We still need to handle special HTML cases here
  // For example, if an <img>'s usemap attribute is modified
  // Otherwise it may just be a state change, for example an object changing
  // its visibility
  //
  // XXX todo: report aria state changes for "undefined" literal value changes
  // filed as bug 472142
  //
  // XXX todo:  invalidate accessible when aria state changes affect exposed
  // role filed as bug 472143

  if (AttributeChangesState(aAttribute)) {
    uint64_t currState = State();
    uint64_t diffState = currState ^ aOldState;
    if (diffState) {
      for (uint64_t state = 1; state <= states::LAST_ENTRY; state <<= 1) {
        if (diffState & state) {
          auto stateChangeEvent =
              MakeRefPtr<AccStateChangeEvent>(this, state, (currState & state));
          mDoc->FireDelayedEvent(stateChangeEvent);
        }
      }
    }
  }

  if (aAttribute == nsGkAtoms::_class) {
    mDoc->QueueCacheUpdate(this, CacheDomain::DOMNodeIDAndClass);
    return;
  }

  // When a details object has its open attribute changed
  // we should fire a state-change event on the accessible of
  // its main summary
  if (aAttribute == nsGkAtoms::open) {
    // FromDetails checks if the given accessible belongs to
    // a details frame and also locates the accessible of its
    // main summary.
    if (HTMLSummaryAccessible* summaryAccessible =
            HTMLSummaryAccessible::FromDetails(this)) {
      auto expandedChangeEvent =
          MakeRefPtr<AccStateChangeEvent>(summaryAccessible, states::EXPANDED);
      mDoc->FireDelayedEvent(expandedChangeEvent);
      return;
    }
  }

  // Check for namespaced ARIA attribute
  if (aNameSpaceID == kNameSpaceID_None) {
    // Check for hyphenated aria-foo property?
    if (StringBeginsWith(nsDependentAtomString(aAttribute), u"aria-"_ns)) {
      uint8_t attrFlags = aria::AttrCharacteristicsFor(aAttribute);
      if (!(attrFlags & ATTR_BYPASSOBJ)) {
        mDoc->QueueCacheUpdate(this, CacheDomain::ARIA);
        // For aria attributes like drag and drop changes we fire a generic
        // attribute change event; at least until native API comes up with a
        // more meaningful event.
        auto event = MakeRefPtr<AccObjectAttrChangedEvent>(this, aAttribute);
        mDoc->FireDelayedEvent(event);
      }
    }
  }

  if (aAttribute == nsGkAtoms::aria_actions && IsAdditionOrRemoval(aModType)) {
    // We only care about the presence of aria-actions, not its value.
    mDoc->QueueCacheUpdate(this, CacheDomain::ARIA);
    auto event =
        MakeRefPtr<AccObjectAttrChangedEvent>(this, nsGkAtoms::hasActions);
    mDoc->FireDelayedEvent(event);
  }

  dom::Element* elm = Elm();

  if (HasNumericValue() &&
      (aAttribute == nsGkAtoms::aria_valuemax ||
       aAttribute == nsGkAtoms::aria_valuemin || aAttribute == nsGkAtoms::min ||
       aAttribute == nsGkAtoms::max || aAttribute == nsGkAtoms::step)) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Value);
    return;
  }

  // Fire text value change event whenever aria-valuetext is changed.
  if (aAttribute == nsGkAtoms::aria_valuetext) {
    mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE, this);
    return;
  }

  if (aAttribute == nsGkAtoms::aria_valuenow) {
    if (!nsAccUtils::HasARIAAttr(elm, nsGkAtoms::aria_valuetext) ||
        nsAccUtils::ARIAAttrValueIs(elm, nsGkAtoms::aria_valuetext,
                                    nsGkAtoms::_empty, eCaseMatters)) {
      // Fire numeric value change event when aria-valuenow is changed and
      // aria-valuetext is empty
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_VALUE_CHANGE, this);
    } else {
      // We need to update the cache here since we won't get an event if
      // aria-valuenow is shadowed by aria-valuetext.
      mDoc->QueueCacheUpdate(this, CacheDomain::Value);
    }
    return;
  }

  if (aAttribute == nsGkAtoms::aria_owns) {
    mDoc->Controller()->ScheduleRelocation(this);
  }

  // Fire name change and description change events.
  if (aAttribute == nsGkAtoms::aria_label || aAttribute == nsGkAtoms::label) {
    // A valid aria-labelledby would take precedence over an aria-label or a xul
    // label attribute. So if that relation exists the name won't change.
    AssociatedElementsIterator iter(mDoc, elm, nsGkAtoms::aria_labelledby);
    if (!iter.NextElem()) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
    }
    return;
  }

  if (aAttribute == nsGkAtoms::aria_description) {
    // A valid aria-describedby would take precedence so an aria-description
    // change won't change the description.
    AssociatedElementsIterator iter(mDoc, elm, nsGkAtoms::aria_describedby);
    if (!iter.NextElem()) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE,
                             this);
    }
    return;
  }

  if (aAttribute == nsGkAtoms::aria_describedby) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
    mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE, this);
    if (IsAdditionOrModification(aModType)) {
      // The subtrees of the new aria-describedby targets might be used to
      // compute the description for this. Therefore, we need to set
      // the eHasDescriptionDependent flag on all Accessibles in these subtrees.
      AssociatedElementsIterator iter(mDoc, elm, nsGkAtoms::aria_describedby);
      while (LocalAccessible* target = iter.Next()) {
        target->ModifySubtreeContextFlags(eHasDescriptionDependent, true);
      }
    }
    return;
  }

  if (aAttribute == nsGkAtoms::aria_labelledby) {
    // We only queue cache updates for explicit relations. Implicit, reverse
    // relations are handled in ApplyCache and stored in a map on the remote
    // document itself.
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
    mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
    if (IsAdditionOrModification(aModType)) {
      // The subtrees of the new aria-labelledby targets might be used to
      // compute the name for this. Therefore, we need to set
      // the eHasNameDependent flag on all Accessibles in these subtrees.
      AssociatedElementsIterator iter(mDoc, elm, nsGkAtoms::aria_labelledby);
      while (LocalAccessible* target = iter.Next()) {
        target->ModifySubtreeContextFlags(eHasNameDependent, true);
      }
    }
    return;
  }

  if ((aAttribute == nsGkAtoms::aria_expanded ||
       aAttribute == nsGkAtoms::href) &&
      IsAdditionOrRemoval(aModType)) {
    // The presence of aria-expanded adds an expand/collapse action.
    mDoc->QueueCacheUpdate(this, CacheDomain::Actions);
  }

  if (aAttribute == nsGkAtoms::href || aAttribute == nsGkAtoms::src) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Value);
  }

  if (aAttribute == nsGkAtoms::aria_details) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
    // If an aria-details attribute is added or removed from an anchored
    // accessible, it will change the validity of its anchor's relation.
    mDoc->RefreshAnchorRelationCacheForTarget(this);
  }

  if (aAttribute == nsGkAtoms::aria_controls ||
      aAttribute == nsGkAtoms::aria_flowto ||
      aAttribute == nsGkAtoms::aria_errormessage ||
      aAttribute == nsGkAtoms::aria_actions) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
  }

  if (aAttribute == nsGkAtoms::popovertarget) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
    return;
  }

  if (aAttribute == nsGkAtoms::commandfor) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
    return;
  }

  if (aAttribute == nsGkAtoms::alt &&
      !nsAccUtils::HasARIAAttr(elm, nsGkAtoms::aria_label) &&
      !elm->HasAttr(nsGkAtoms::aria_labelledby)) {
    mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
    return;
  }

  if (aAttribute == nsGkAtoms::title) {
    nsAutoString name;
    if (Name(name) == eNameFromTooltip || name.IsVoid()) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
      return;
    }

    if (!elm->HasAttr(nsGkAtoms::aria_describedby)) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE,
                             this);
    }

    return;
  }

  // ARIA or XUL selection
  if ((mContent->IsXULElement() && aAttribute == nsGkAtoms::selected) ||
      aAttribute == nsGkAtoms::aria_selected) {
    if (aAttribute == nsGkAtoms::aria_selected) {
      const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
      if (!roleMapEntry && IsHTMLTableCell() && Role() == roles::GRID_CELL) {
        // This is a <td> inside a role="grid"; see the similar case in
        // ApplyARIAState.
        roleMapEntry = aria::GetRoleMap(nsGkAtoms::gridcell);
      }
      if (roleMapEntry && roleMapEntry->IsSelectableIfDefined()) {
        // For these roles, whether aria-selected is "defined" (present and
        // neither empty nor "undefined") determines the selectable state.
        // For other selectable roles (option, tab, treeitem, etc.), the
        // selectable state doesn't depend on aria-selected's definedness, so
        // don't fire this there.
        const bool wasDefined =
            aOldValue && !aOldValue->IsEmptyString() &&
            !aOldValue->Equals(nsGkAtoms::_undefined, eCaseMatters);
        const bool isDefined =
            nsAccUtils::HasDefinedARIAToken(elm, nsGkAtoms::aria_selected);
        if (wasDefined != isDefined) {
          auto stateChangeEvent = MakeRefPtr<AccStateChangeEvent>(
              this, states::SELECTABLE, isDefined);
          mDoc->FireDelayedEvent(stateChangeEvent);
        }
      }
    }

    LocalAccessible* widget = nsAccUtils::GetSelectableContainer(this, State());
    if (widget) {
      AccSelChangeEvent::SelChangeType selChangeType;
      if (aAttribute == nsGkAtoms::selected) {
        // The condition above guarantees a XUL element here. In XUL, `selected`
        // is a boolean attribute whose mere presence indicates selection,
        // unlike aria-selected which is "true"/"false".
        selChangeType = elm->HasAttr(nsGkAtoms::selected)
                            ? AccSelChangeEvent::eSelectionAdd
                            : AccSelChangeEvent::eSelectionRemove;
      } else {
        selChangeType = nsAccUtils::ARIAAttrValueIs(
                            elm, aAttribute, nsGkAtoms::_true, eCaseMatters)
                            ? AccSelChangeEvent::eSelectionAdd
                            : AccSelChangeEvent::eSelectionRemove;
      }

      auto event = MakeRefPtr<AccSelChangeEvent>(widget, this, selChangeType);
      mDoc->FireDelayedEvent(event);
      if (aAttribute == nsGkAtoms::aria_selected) {
        mDoc->QueueCacheUpdate(this, CacheDomain::State);
      }
    }

    return;
  }

  if (aAttribute == nsGkAtoms::aria_level ||
      aAttribute == nsGkAtoms::aria_setsize ||
      aAttribute == nsGkAtoms::aria_posinset) {
    mDoc->QueueCacheUpdate(this, CacheDomain::GroupInfo);
    return;
  }

  if (aAttribute == nsGkAtoms::accesskey) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Actions);
  }

  if (aAttribute == nsGkAtoms::name &&
      (mContent && mContent->IsHTMLElement(nsGkAtoms::a))) {
    // If an anchor's name changed, it's possible a LINKS_TO relation
    // also changed. Push a cache update for Relations.
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
  }
}

void LocalAccessible::ARIAGroupPosition(int32_t* aLevel, int32_t* aSetSize,
                                        int32_t* aPosInSet) const {
  if (!mContent) {
    return;
  }

  if (aLevel) {
    nsCoreUtils::GetUIntAttr(mContent, nsGkAtoms::aria_level, aLevel);
  }
  if (aSetSize) {
    nsCoreUtils::GetUIntAttr(mContent, nsGkAtoms::aria_setsize, aSetSize);
  }
  if (aPosInSet) {
    nsCoreUtils::GetUIntAttr(mContent, nsGkAtoms::aria_posinset, aPosInSet);
  }
}

uint64_t LocalAccessible::ExplicitState() const {
  if (IsDefunct()) return states::DEFUNCT;

  uint64_t state = NativeState();
  // Apply ARIA states to be sure accessible states will be overridden.
  ApplyARIAState(&state);

  if (!(state & states::UNAVAILABLE)) {
    // If the object is a current item of container widget then mark it as
    // ACTIVE. This allows screen reader virtual buffer modes to know which
    // descendant is the current one that would get focus if the user navigates
    // to the container widget.
    LocalAccessible* widget = ContainerWidget();
    if (widget && widget->CurrentItem() == this) state |= states::ACTIVE;
  }

  return state;
}

uint64_t LocalAccessible::State() {
  uint64_t state = ExplicitState();

  ApplyImplicitState(state);
  return state;
}

void LocalAccessible::ApplyARIAState(uint64_t* aState) const {
  if (!mContent->IsElement()) return;

  dom::Element* element = mContent->AsElement();

  // Test for universal states first
  *aState |= aria::UniversalStatesFor(element);

  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
  if (!roleMapEntry && IsHTMLTableCell() && Role() == roles::GRID_CELL) {
    // This is a <td> inside a role="grid", so it gets an implicit role of
    // GRID_CELL in ARIATransformRole. However, because it's implicit, we
    // don't have a role map entry, and without that, we can't apply ARIA states
    // below. Therefore, we get the role map entry here.
    roleMapEntry = aria::GetRoleMap(nsGkAtoms::gridcell);
    MOZ_ASSERT(roleMapEntry, "Should have role map entry for gridcell");
  }
  if (roleMapEntry) {
    // We only force the readonly bit off if we have a real mapping for the aria
    // role. This preserves the ability for screen readers to use readonly
    // (primarily on the document) as the hint for creating a virtual buffer.
    // Avoid doing this on text fields, since the readonly state is useful
    // there.
    if (roleMapEntry->role != roles::NOTHING && !IsTextField())
      *aState &= ~states::READONLY;

    if (mContent->HasID()) {
      // If has a role & ID and aria-activedescendant on the container, assume
      // focusable.
      const LocalAccessible* ancestor = this;
      while ((ancestor = ancestor->LocalParent()) && !ancestor->IsDoc()) {
        dom::Element* el = ancestor->Elm();
        if (el && el->HasAttr(nsGkAtoms::aria_activedescendant)) {
          *aState |= states::FOCUSABLE;
          break;
        }
      }
    }
  }

  if (!(*aState & states::FOCUSABLE)) {
    // Sometimes, we use aria-activedescendant targeting something which isn't
    // actually a descendant. This is technically a spec violation, but it's a
    // useful hack which makes certain things much easier. For example, we use
    // this for "fake focus" for multi select browser tabs and Quantumbar
    // autocomplete suggestions.
    // In these cases, the aria-activedescendant code above won't make the
    // active item focusable. It doesn't make sense for something to have
    // focus when it isn't focusable, so fix that here.
    if (FocusMgr()->IsActiveItem(this)) {
      *aState |= states::FOCUSABLE;
    }
  }

  if (!roleMapEntry) {
    if (nsStaticAtom* ariaRole = ComputedARIARole()) {
      const nsRoleMapEntry* computedRoleMapEntry = aria::GetRoleMap(ariaRole);
      aria::MapToStateIfInRoleMapEntry(computedRoleMapEntry,
                                       aria::eARIAExpanded, element, aState);
      aria::MapToStateIfInRoleMapEntry(computedRoleMapEntry, aria::eARIAPressed,
                                       element, aState);
    }
  }

  if (!IsTextField() && IsEditableRoot()) {
    // HTML text fields will have their own multi/single line calcuation in
    // NativeState.
    aria::MapToState(aria::eARIAMultilineByDefault, element, aState);
  }

  if (!roleMapEntry) return;

  *aState |= roleMapEntry->state;

  if (aria::MapToState(roleMapEntry->attributeMap1, element, aState) &&
      aria::MapToState(roleMapEntry->attributeMap2, element, aState) &&
      aria::MapToState(roleMapEntry->attributeMap3, element, aState)) {
    aria::MapToState(roleMapEntry->attributeMap4, element, aState);
  }

  // ARIA gridcell inherits readonly state from the grid until it's overridden.
  if ((roleMapEntry->Is(nsGkAtoms::gridcell) ||
       roleMapEntry->Is(nsGkAtoms::columnheader) ||
       roleMapEntry->Is(nsGkAtoms::rowheader)) &&
      // Don't recurse infinitely for an authoring error like
      // <table role="gridcell">. Without this check, we'd call TableFor(this)
      // below, which would return this.
      !IsTable() &&
      !nsAccUtils::HasDefinedARIAToken(mContent, nsGkAtoms::aria_readonly)) {
    if (const LocalAccessible* grid = nsAccUtils::TableFor(this)) {
      uint64_t gridState = 0;
      grid->ApplyARIAState(&gridState);
      *aState |= gridState & states::READONLY;
    }
  }
}

void LocalAccessible::Value(nsString& aValue) const {
  if (HasNumericValue()) {
    // aria-valuenow is a number, and aria-valuetext is the optional text
    // equivalent. For the string value, we will try the optional text
    // equivalent first.
    if (!mContent->IsElement()) {
      return;
    }

    if (!nsAccUtils::GetARIAAttr(mContent->AsElement(),
                                 nsGkAtoms::aria_valuetext, aValue)) {
      if (!NativeHasNumericValue()) {
        double checkValue = CurValue();
        if (!std::isnan(checkValue)) {
          aValue.AppendFloat(checkValue);
        }
      }
    }
    return;
  }

  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();

  // The value of a textbox is the text content from its subtree.
  if (IsTextField() || (roleMapEntry && roleMapEntry->Is(nsGkAtoms::textbox)) ||
      (IsGeneric() && IsEditableRoot())) {
    TextLeafRange::FromAccessible(const_cast<LocalAccessible*>(this))
        .GetFlattenedText(aValue);
    return;
  }

  // Value of combobox is a text of current or selected item.
  if (roleMapEntry && roleMapEntry->Is(nsGkAtoms::combobox)) {
    LocalAccessible* option = CurrentItem();
    if (!option) {
      uint32_t childCount = ChildCount();
      for (uint32_t idx = 0; idx < childCount; idx++) {
        LocalAccessible* child = mChildren.ElementAt(idx);
        if (child->IsListControl()) {
          Accessible* acc = child->GetSelectedItem(0);
          option = acc ? acc->AsLocal() : nullptr;
          break;
        }
      }
    }

    // If there's a selected item, get the value from it. Otherwise, determine
    // the value from descendant elements.
    nsTextEquivUtils::GetTextEquivFromSubtree(option ? option : this, aValue);
  }
}

double LocalAccessible::MaxValue() const {
  double checkValue = AttrNumericValue(nsGkAtoms::aria_valuemax);
  if (std::isnan(checkValue) && !NativeHasNumericValue()) {
    // aria-valuemax isn't present and this element doesn't natively provide a
    // maximum value. Use the ARIA default.
    const nsRoleMapEntry* roleMap = ARIARoleMap();
    if (roleMap && roleMap->role == roles::SPINBUTTON) {
      return UnspecifiedNaN<double>();
    }
    return 100;
  }
  return checkValue;
}

double LocalAccessible::MinValue() const {
  double checkValue = AttrNumericValue(nsGkAtoms::aria_valuemin);
  if (std::isnan(checkValue) && !NativeHasNumericValue()) {
    // aria-valuemin isn't present and this element doesn't natively provide a
    // minimum value. Use the ARIA default.
    const nsRoleMapEntry* roleMap = ARIARoleMap();
    if (roleMap && roleMap->role == roles::SPINBUTTON) {
      return UnspecifiedNaN<double>();
    }
    return 0;
  }
  return checkValue;
}

double LocalAccessible::Step() const {
  return UnspecifiedNaN<double>();  // no mimimum increment (step) in ARIA.
}

double LocalAccessible::CurValue() const {
  double checkValue = AttrNumericValue(nsGkAtoms::aria_valuenow);
  if (std::isnan(checkValue) && !NativeHasNumericValue()) {
    // aria-valuenow isn't present and this element doesn't natively provide a
    // current value. Use the ARIA default.
    const nsRoleMapEntry* roleMap = ARIARoleMap();
    if (roleMap && roleMap->role == roles::SPINBUTTON) {
      return UnspecifiedNaN<double>();
    }
    double minValue = MinValue();
    return minValue + ((MaxValue() - minValue) / 2);
  }

  return checkValue;
}

bool LocalAccessible::SetCurValue(double /*aValue*/) { return false; }

role LocalAccessible::NativeRole() const { return roles::NOTHING; }

uint8_t LocalAccessible::ActionCount() const {
  return HasPrimaryAction() || ActionAncestor() ? 1 : 0;
}

void LocalAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  aName.Truncate();

  if (aIndex != 0) return;

  uint32_t actionRule = GetActionRule();

  switch (actionRule) {
    case eActivateAction:
      aName.AssignLiteral("activate");
      return;

    case eClickAction:
      aName.AssignLiteral("click");
      return;

    case ePressAction:
      aName.AssignLiteral("press");
      return;

    case eCheckUncheckAction: {
      uint64_t state = State();
      if (state & states::CHECKED) {
        aName.AssignLiteral("uncheck");
      } else if (state & states::MIXED) {
        aName.AssignLiteral("cycle");
      } else {
        aName.AssignLiteral("check");
      }
      return;
    }

    case eJumpAction:
      aName.AssignLiteral("jump");
      return;

    case eOpenCloseAction:
      if (State() & states::EXPANDED) {
        aName.AssignLiteral("close");
      } else {
        aName.AssignLiteral("open");
      }
      return;

    case eSelectAction:
      aName.AssignLiteral("select");
      return;

    case eSwitchAction:
      aName.AssignLiteral("switch");
      return;

    case eSortAction:
      aName.AssignLiteral("sort");
      return;

    case eExpandAction:
      if (State() & states::EXPANDED) {
        aName.AssignLiteral("collapse");
      } else {
        aName.AssignLiteral("expand");
      }
      return;
  }

  if (ActionAncestor()) {
    aName.AssignLiteral("clickAncestor");
    return;
  }
}

bool LocalAccessible::DoAction(uint8_t aIndex) const {
  if (aIndex != 0) return false;

  if (HasPrimaryAction() || ActionAncestor()) {
    DoCommand();
    return true;
  }

  return false;
}

bool LocalAccessible::HasPrimaryAction() const {
  return GetActionRule() != eNoAction;
}

nsIContent* LocalAccessible::GetAtomicRegion() const {
  nsIContent* loopContent = mContent;
  nsAutoString atomic;
  while (loopContent &&
         (!loopContent->IsElement() ||
          !nsAccUtils::GetARIAAttr(loopContent->AsElement(),
                                   nsGkAtoms::aria_atomic, atomic))) {
    loopContent = loopContent->GetParent();
  }

  return atomic.EqualsLiteral("true") ? loopContent : nullptr;
}

static bool IsHintPopover(nsIContent* aContent) {
  auto* htmlEl = nsGenericHTMLElement::FromNode(aContent);
  return htmlEl &&
         htmlEl->GetPopoverAttributeState() == dom::PopoverAttributeState::Hint;
}

static bool IsOpenHintPopover(nsIContent* aContent) {
  auto* htmlEl = nsGenericHTMLElement::FromNode(aContent);
  return htmlEl && htmlEl->PopoverOpen() &&
         htmlEl->GetPopoverAttributeState() == dom::PopoverAttributeState::Hint;
}

// Returns true if targetEl is:
//  - a non hint popover (these should always use details, not described by).
//  - a hint popover that is not open (no relation should be established).
//  - a hint popover with "rich" content (for non-rich content, desdcribed by
//    should be established instead).
static bool ShouldHintPopoverEstablishDetailsRelation(
    dom::Element* aTargetEl, LocalAccessible* aTargetAcc) {
  if (!IsHintPopover(aTargetEl)) {
    return true;
  }
  if (!IsOpenHintPopover(aTargetEl)) {
    return false;
  }
  return !aTargetAcc->IsOnlyPlainContent();
}

static bool ShouldHintPopoverEstablishDescribedByRelation(
    dom::Element* aTargetEl, LocalAccessible* aTargetAcc) {
  return IsOpenHintPopover(aTargetEl) && aTargetAcc->IsOnlyPlainContent();
}

LocalAccessible* LocalAccessible::GetCommandForDetailsRelation() const {
  dom::Element* targetEl = mContent->GetEffectiveCommandForElement();
  if (!targetEl) {
    return nullptr;
  }
  LocalAccessible* targetAcc = mDoc->GetAccessible(targetEl);
  if (!targetAcc) {
    return nullptr;
  }
  // Relations on Command/CommandFor should only be for ShowPopover &
  // TogglePopover commands.
  if (const nsAttrValue* actionVal = Elm()->GetParsedAttr(nsGkAtoms::command)) {
    if (actionVal && actionVal->Type() != nsAttrValue::eEnum) {
      return nullptr;
    }
    auto command =
        static_cast<dom::Element::Command>(actionVal->GetEnumValue());
    if (command != dom::Element::Command::ShowPopover &&
        command != dom::Element::Command::TogglePopover) {
      return nullptr;
    }
  }
  if (targetAcc->NextSibling() == this || targetAcc->PrevSibling() == this) {
    return nullptr;
  }
  if (!ShouldHintPopoverEstablishDetailsRelation(targetEl, targetAcc)) {
    return nullptr;
  }
  return targetAcc;
}

LocalAccessible* LocalAccessible::GetPopoverTargetDetailsRelation() const {
  dom::Element* targetEl = mContent->GetEffectivePopoverTargetElement();
  if (!targetEl) {
    return nullptr;
  }
  LocalAccessible* targetAcc = mDoc->GetAccessible(targetEl);
  if (!targetAcc) {
    return nullptr;
  }
  // Even if the popovertarget is valid, there are a few cases where we must not
  // expose it via the details relation.
  if (const nsAttrValue* actionVal =
          Elm()->GetParsedAttr(nsGkAtoms::popovertargetaction)) {
    if (static_cast<PopoverTargetAction>(actionVal->GetEnumValue()) ==
        PopoverTargetAction::Hide) {
      return nullptr;
    }
  }
  if (targetAcc->NextSibling() == this || targetAcc->PrevSibling() == this) {
    return nullptr;
  }
  if (!ShouldHintPopoverEstablishDetailsRelation(targetEl, targetAcc)) {
    return nullptr;
  }
  return targetAcc;
}

LocalAccessible* LocalAccessible::GetAnchorPositionTargetDetailsRelation()
    const {
  if (!StaticPrefs::accessibility_anchorPositionedAsDetails_enabled()) {
    return nullptr;
  }
  nsIFrame* positionedFrame = nsCoreUtils::GetPositionedFrameForAnchor(
      mDoc->PresShellPtr(), GetFrame());
  if (!positionedFrame) {
    return nullptr;
  }

  if (!nsCoreUtils::GetAnchorForPositionedFrame(mDoc->PresShellPtr(),
                                                positionedFrame)) {
    // There is no reciprocal, 1:1, anchor for this positioned frame.
    return nullptr;
  }

  LocalAccessible* targetAcc =
      mDoc->GetAccessible(positionedFrame->GetContent());

  if (!targetAcc) {
    return nullptr;
  }

  if (targetAcc->Role() == roles::TOOLTIP) {
    // A tooltip is never a valid target for details relation.
    return nullptr;
  }

  AssociatedElementsIterator describedby(mDoc, GetContent(),
                                         nsGkAtoms::aria_describedby);
  while (LocalAccessible* target = describedby.Next()) {
    if (target == targetAcc) {
      // An explicit description relation exists, so we don't want to create a
      // details relation.
      return nullptr;
    }
  }

  AssociatedElementsIterator labelledby(mDoc, GetContent(),
                                        nsGkAtoms::aria_labelledby);
  while (LocalAccessible* target = labelledby.Next()) {
    if (target == targetAcc) {
      // An explicit label relation exists, so we don't want to create a details
      // relation.
      return nullptr;
    }
  }

  dom::Element* anchorEl = targetAcc->Elm();
  if (anchorEl && anchorEl->HasAttr(nsGkAtoms::aria_details)) {
    // If the anchor has an explicit aria-details attribute, then we don't want
    // to create a details relation.
    return nullptr;
  }

  dom::Element* targetEl = Elm();
  if (targetEl && targetEl->HasAttr(nsGkAtoms::aria_details)) {
    // If the target has an explicit aria-details attribute, then we don't want
    // to create a details relation.
    return nullptr;
  }

  return targetAcc;
}

LocalAccessible* LocalAccessible::GetPopoverTargetDescribedByRelation() const {
  dom::Element* targetEl = mContent->GetEffectivePopoverTargetElement();
  if (!targetEl) {
    return nullptr;
  }
  LocalAccessible* targetAcc = mDoc->GetAccessible(targetEl);
  if (!targetAcc) {
    return nullptr;
  }
  if (const nsAttrValue* actionVal =
          Elm()->GetParsedAttr(nsGkAtoms::popovertargetaction)) {
    if (static_cast<PopoverTargetAction>(actionVal->GetEnumValue()) ==
        PopoverTargetAction::Hide) {
      return nullptr;
    }
  }
  if (ShouldHintPopoverEstablishDescribedByRelation(targetEl, targetAcc)) {
    return targetAcc;
  }
  return nullptr;
}

LocalAccessible* LocalAccessible::GetCommandForDescribedByRelation() const {
  dom::Element* targetEl = mContent->GetEffectiveCommandForElement();
  if (!targetEl) {
    return nullptr;
  }
  LocalAccessible* targetAcc = mDoc->GetAccessible(targetEl);
  if (!targetAcc) {
    return nullptr;
  }
  if (const nsAttrValue* actionVal = Elm()->GetParsedAttr(nsGkAtoms::command)) {
    if (actionVal && actionVal->Type() != nsAttrValue::eEnum) {
      return nullptr;
    }
    auto command =
        static_cast<dom::Element::Command>(actionVal->GetEnumValue());
    if (command != dom::Element::Command::ShowPopover &&
        command != dom::Element::Command::TogglePopover) {
      return nullptr;
    }
  }
  if (ShouldHintPopoverEstablishDescribedByRelation(targetEl, targetAcc)) {
    return targetAcc;
  }
  return nullptr;
}

Relation LocalAccessible::RelationByType(RelationType aType) const {
  if (!HasOwnContent()) return Relation();

  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();

  // Relationships are defined on the same content node that the role would be
  // defined on.
  switch (aType) {
    case RelationType::LABELLED_BY: {
      Relation rel(new AssociatedElementsIterator(mDoc, mContent,
                                                  nsGkAtoms::aria_labelledby));
      if (mContent->IsHTMLElement()) {
        rel.AppendIter(new HTMLLabelIterator(mDoc, this));
      }
      rel.AppendIter(new XULLabelIterator(Document(), mContent));

      return rel;
    }

    case RelationType::LABEL_FOR: {
      Relation rel(new RelatedAccIterator(Document(), mContent,
                                          nsGkAtoms::aria_labelledby));
      if (mContent->IsXULElement(nsGkAtoms::label)) {
        rel.AppendIter(
            new AssociatedElementsIterator(mDoc, mContent, nsGkAtoms::control));
      }

      return rel;
    }

    case RelationType::DESCRIBED_BY: {
      Relation rel(new AssociatedElementsIterator(mDoc, mContent,
                                                  nsGkAtoms::aria_describedby));
      if (mContent->IsXULElement()) {
        rel.AppendIter(new XULDescriptionIterator(Document(), mContent));
      }
      if (LocalAccessible* target = GetCommandForDescribedByRelation()) {
        rel.AppendTarget(target);
      }
      if (LocalAccessible* target = GetPopoverTargetDescribedByRelation()) {
        rel.AppendTarget(target);
      }

      return rel;
    }

    case RelationType::DESCRIPTION_FOR: {
      Relation rel(new RelatedAccIterator(Document(), mContent,
                                          nsGkAtoms::aria_describedby));

      // This affectively adds an optional control attribute to xul:description,
      // which only affects accessibility, by allowing the description to be
      // tied to a control.
      if (mContent->IsXULElement(nsGkAtoms::description)) {
        rel.AppendIter(
            new AssociatedElementsIterator(mDoc, mContent, nsGkAtoms::control));
      }

      RelatedAccIterator popoverInvokers(mDoc, mContent,
                                         nsGkAtoms::popovertarget);
      while (Accessible* invoker = popoverInvokers.Next()) {
        if (invoker->AsLocal()->GetPopoverTargetDescribedByRelation()) {
          MOZ_ASSERT(
              invoker->AsLocal()->GetPopoverTargetDescribedByRelation() ==
              this);
          rel.AppendTarget(invoker);
        }
      }
      RelatedAccIterator commandInvokers(mDoc, mContent, nsGkAtoms::commandfor);
      while (Accessible* invoker = commandInvokers.Next()) {
        if (invoker->AsLocal()->GetCommandForDescribedByRelation()) {
          MOZ_ASSERT(invoker->AsLocal()->GetCommandForDescribedByRelation() ==
                     this);
          rel.AppendTarget(invoker);
        }
      }

      return rel;
    }

    case RelationType::NODE_CHILD_OF: {
      Relation rel;
      // This is an ARIA tree or treegrid that doesn't use owns, so we need to
      // get the parent the hard way.
      if (roleMapEntry && (roleMapEntry->role == roles::OUTLINEITEM ||
                           roleMapEntry->role == roles::LISTITEM ||
                           roleMapEntry->role == roles::ROW)) {
        AccGroupInfo* groupInfo =
            const_cast<LocalAccessible*>(this)->GetOrCreateGroupInfo();
        if (groupInfo) {
          Accessible* parent = groupInfo->ConceptualParent();
          if (parent) {
            MOZ_ASSERT(parent->IsLocal());
            rel.AppendTarget(parent->AsLocal());
          }
        }
      }

      // If this is an OOP iframe document, we can't support NODE_CHILD_OF
      // here, since the iframe resides in a different process. This is fine
      // because the client will then request the parent instead, which will be
      // correctly handled by platform code.
      if (XRE_IsContentProcess() && IsRoot()) {
        dom::Document* doc =
            const_cast<LocalAccessible*>(this)->AsDoc()->DocumentNode();
        dom::BrowsingContext* bc = doc->GetBrowsingContext();
        MOZ_ASSERT(bc);
        if (!bc->Top()->IsInProcess()) {
          return rel;
        }
      }

      // If accessible is in its own Window, or is the root of a document,
      // then we should provide NODE_CHILD_OF relation so that MSAA clients
      // can easily get to true parent instead of getting to oleacc's
      // ROLE_WINDOW accessible which will prevent us from going up further
      // (because it is system generated and has no idea about the hierarchy
      // above it).
      if (nsIFrame* frame = GetFrame()) {
        ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(frame);
        if (scrollContainerFrame || frame->GetOwnWidget() ||
            !frame->GetParent()) {
          rel.AppendTarget(LocalParent());
        }
      }

      return rel;
    }

    case RelationType::NODE_PARENT_OF: {
      // ARIA tree or treegrid can do the hierarchy by @aria-level, ARIA trees
      // also can be organized by groups.
      if (roleMapEntry && (roleMapEntry->role == roles::OUTLINEITEM ||
                           roleMapEntry->role == roles::LISTITEM ||
                           roleMapEntry->role == roles::ROW ||
                           roleMapEntry->role == roles::OUTLINE ||
                           roleMapEntry->role == roles::LIST ||
                           roleMapEntry->role == roles::TREE_TABLE)) {
        return Relation(new ItemIterator(this));
      }

      return Relation();
    }

    case RelationType::CONTROLLED_BY: {
      Relation rel(new RelatedAccIterator(Document(), mContent,
                                          nsGkAtoms::aria_controls));

      RelatedAccIterator owners(Document(), mContent, nsGkAtoms::aria_owns);
      if (LocalAccessible* owner = owners.Next()) {
        if (nsAccUtils::IsEditableARIACombobox(owner)) {
          MOZ_ASSERT(!IsRelocated(),
                     "Child is not relocated to editable combobox");
          rel.AppendTarget(owner);
        }
      }

      return rel;
    }
    case RelationType::CONTROLLER_FOR: {
      Relation rel(new AssociatedElementsIterator(mDoc, mContent,
                                                  nsGkAtoms::aria_controls));
      rel.AppendIter(new HTMLOutputIterator(Document(), mContent));
      if (nsAccUtils::IsEditableARIACombobox(this)) {
        AssociatedElementsIterator iter(mDoc, mContent, nsGkAtoms::aria_owns);
        while (Accessible* owned_child = iter.Next()) {
          MOZ_ASSERT(!owned_child->AsLocal()->IsRelocated());
          rel.AppendTarget(owned_child->AsLocal());
        }
      }
      return rel;
    }

    case RelationType::FLOWS_TO:
      return Relation(new AssociatedElementsIterator(mDoc, mContent,
                                                     nsGkAtoms::aria_flowto));

    case RelationType::FLOWS_FROM:
      return Relation(
          new RelatedAccIterator(Document(), mContent, nsGkAtoms::aria_flowto));

    case RelationType::MEMBER_OF: {
      if (Role() == roles::RADIOBUTTON) {
        /* If we see a radio button role here, we're dealing with an aria
         * radio button (because input=radio buttons are
         * HTMLRadioButtonAccessibles) */
        Relation rel = Relation();
        LocalAccessible* currParent = LocalParent();
        while (currParent && currParent->Role() != roles::RADIO_GROUP) {
          currParent = currParent->LocalParent();
        }

        if (currParent && currParent->Role() == roles::RADIO_GROUP) {
          /* If we found a radiogroup parent, search for all
           * roles::RADIOBUTTON children and add them to our relation.
           * This search will include the radio button this method
           * was called from, which is expected. */
          Pivot p = Pivot(currParent);
          PivotRoleRule rule(roles::RADIOBUTTON);
          Accessible* match = p.Next(currParent, rule);
          while (match) {
            MOZ_ASSERT(match->IsLocal(),
                       "We shouldn't find any remote accs while building our "
                       "relation!");
            rel.AppendTarget(match->AsLocal());
            match = p.Next(match, rule);
          }
        }

        /* By webkit's standard, aria radio buttons do not get grouped
         * if they lack a group parent, so we return an empty
         * relation here if the above check fails. */

        return rel;
      }

      return Relation(mDoc, GetAtomicRegion());
    }

    case RelationType::LINKS_TO: {
      Relation rel = Relation();
      if (Role() == roles::LINK) {
        dom::HTMLAnchorElement* anchor =
            dom::HTMLAnchorElement::FromNode(mContent);
        if (!anchor) {
          return rel;
        }
        // If this node is an anchor element, query its hash to find the
        // target.
        nsAutoCString hash;
        anchor->GetHash(hash);
        if (hash.IsEmpty()) {
          return rel;
        }

        // GetHash returns an ID or name with a leading '#', trim it so we can
        // search the doc by ID or name alone.
        NS_ConvertUTF8toUTF16 hash16(Substring(hash, 1));
        if (dom::Element* elm = mContent->OwnerDoc()->GetElementById(hash16)) {
          rel.AppendTarget(mDoc->GetAccessibleOrContainer(elm));
        } else if (RefPtr<dom::NodeList> list =
                       mContent->OwnerDoc()->GetElementsByName(hash16)) {
          // Loop through the named nodes looking for the first anchor
          uint32_t length = list->Length();
          for (uint32_t i = 0; i < length; i++) {
            nsIContent* node = list->Item(i);
            if (node->IsHTMLElement(nsGkAtoms::a)) {
              rel.AppendTarget(mDoc->GetAccessibleOrContainer(node));
              break;
            }
          }
        }
      }

      return rel;
    }

    case RelationType::SUBWINDOW_OF:
    case RelationType::EMBEDS:
    case RelationType::EMBEDDED_BY:
    case RelationType::POPUP_FOR:
    case RelationType::PARENT_WINDOW_OF:
      return Relation();

    case RelationType::DEFAULT_BUTTON: {
      if (mContent->IsHTMLElement()) {
        // HTML form controls implements nsIFormControl interface.
        if (auto* control = nsIFormControl::FromNode(mContent)) {
          if (dom::HTMLFormElement* form = control->GetFormInternal()) {
            return Relation(mDoc, form->GetDefaultSubmitElement());
          }
        }
      } else {
        // In XUL, use first <button default="true" .../> in the document
        dom::Document* doc = mContent->OwnerDoc();
        nsIContent* buttonEl = nullptr;
        if (doc->AllowXULXBL()) {
          RefPtr<dom::HTMLCollection> possibleDefaultButtons =
              doc->GetElementsByAttribute(u"default"_ns, u"true"_ns);
          if (possibleDefaultButtons) {
            uint32_t length = possibleDefaultButtons->Length();
            // Check for button in list of default="true" elements
            for (uint32_t count = 0; count < length && !buttonEl; count++) {
              nsIContent* item = possibleDefaultButtons->Item(count);
              RefPtr<nsIDOMXULButtonElement> button =
                  item->IsElement() ? item->AsElement()->AsXULButton()
                                    : nullptr;
              if (button) {
                buttonEl = item;
              }
            }
          }
          return Relation(mDoc, buttonEl);
        }
      }
      return Relation();
    }

    case RelationType::CONTAINING_DOCUMENT:
      return Relation(mDoc);

    case RelationType::CONTAINING_TAB_PANE: {
      nsCOMPtr<nsIDocShell> docShell = nsCoreUtils::GetDocShellFor(GetNode());
      if (docShell) {
        // Walk up the parent chain without crossing the boundary at which item
        // types change, preventing us from walking up out of tab content.
        nsCOMPtr<nsIDocShellTreeItem> root;
        docShell->GetInProcessSameTypeRootTreeItem(getter_AddRefs(root));
        if (root) {
          // If the item type is typeContent, we assume we are in browser tab
          // content. Note, this includes content such as about:addons,
          // for consistency.
          if (root->ItemType() == nsIDocShellTreeItem::typeContent) {
            return Relation(nsAccUtils::GetDocAccessibleFor(root));
          }
        }
      }
      return Relation();
    }

    case RelationType::CONTAINING_APPLICATION:
      return Relation(ApplicationAcc());

    case RelationType::DETAILS: {
      if (mContent->IsElement() &&
          nsAccUtils::HasARIAAttr(mContent->AsElement(),
                                  nsGkAtoms::aria_details)) {
        return Relation(new AssociatedElementsIterator(
            mDoc, mContent, nsGkAtoms::aria_details));
      }
      if (LocalAccessible* target = GetCommandForDetailsRelation()) {
        return Relation(target);
      }
      if (LocalAccessible* target = GetPopoverTargetDetailsRelation()) {
        return Relation(target);
      }
      if (LocalAccessible* target = GetAnchorPositionTargetDetailsRelation()) {
        if (nsAccUtils::IsValidDetailsTargetForAnchor(target, this)) {
          return Relation(target);
        }
      }

      return Relation();
    }

    case RelationType::DETAILS_FOR: {
      Relation rel(
          new RelatedAccIterator(mDoc, mContent, nsGkAtoms::aria_details));
      RelatedAccIterator popover_invokers(mDoc, mContent,
                                          nsGkAtoms::popovertarget);
      while (Accessible* invoker = popover_invokers.Next()) {
        // We should only expose DETAILS_FOR if DETAILS was exposed on the
        // invoker. However, DETAILS exposure on popover invokers is
        // conditional.
        if (invoker->AsLocal()->GetPopoverTargetDetailsRelation()) {
          MOZ_ASSERT(invoker->AsLocal()->GetPopoverTargetDetailsRelation() ==
                     this);
          rel.AppendTarget(invoker);
        }
      }
      RelatedAccIterator command_invokers(mDoc, mContent,
                                          nsGkAtoms::commandfor);
      while (Accessible* invoker = command_invokers.Next()) {
        // We should only expose DETAILS_FOR if DETAILS was exposed on the
        // invoker. However, DETAILS exposure on popover invokers is
        // conditional.
        if (invoker->AsLocal()->GetCommandForDetailsRelation()) {
          MOZ_ASSERT(invoker->AsLocal()->GetCommandForDetailsRelation() ==
                     this);
          rel.AppendTarget(invoker);
        }
      }

      // Check early if the accessible is a tooltip. If so, it can never be a
      // valid target for an anchor's details relation.
      if (StaticPrefs::accessibility_anchorPositionedAsDetails_enabled() &&
          Role() != roles::TOOLTIP) {
        if (const nsIFrame* anchorFrame =
                nsCoreUtils::GetAnchorForPositionedFrame(mDoc->PresShellPtr(),
                                                         GetFrame())) {
          LocalAccessible* anchorAcc =
              mDoc->GetAccessible(anchorFrame->GetContent());
          if (anchorAcc &&
              anchorAcc->GetAnchorPositionTargetDetailsRelation() == this &&
              nsAccUtils::IsValidDetailsTargetForAnchor(this, anchorAcc)) {
            rel.AppendTarget(anchorAcc);
          }
        }
      }

      return rel;
    }

    case RelationType::ERRORMSG:
      if (!(ExplicitState() & states::INVALID)) {
        return Relation();
      }
      return Relation(new AssociatedElementsIterator(
          mDoc, mContent, nsGkAtoms::aria_errormessage));

    case RelationType::ERRORMSG_FOR:
      return Relation(
          new RelatedAccIterator(mDoc, mContent, nsGkAtoms::aria_errormessage));

    case RelationType::ACTION:
      return Relation(new AssociatedElementsIterator(mDoc, mContent,
                                                     nsGkAtoms::aria_actions));

    case RelationType::ACTION_FOR:
      return Relation(
          new RelatedAccIterator(mDoc, mContent, nsGkAtoms::aria_actions));

    default:
      return Relation();
  }
}

void LocalAccessible::GetNativeInterface(void** aNativeAccessible) {}

void LocalAccessible::DoCommand(uint32_t aActionIndex) const {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "LocalAccessible::DispatchClickEvent",
      [aActionIndex, acc = RefPtr{this}]() MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        acc->DispatchClickEvent(aActionIndex);
      }));
}

void LocalAccessible::DispatchClickEvent(uint32_t /*aActionIndex*/) const {
  if (IsDefunct()) return;
  MOZ_ASSERT(mContent);

  RefPtr<PresShell> presShell = mDoc->PresShellPtr();

  // Scroll into view.
  presShell->ScrollContentIntoView(mContent, AxisScrollParams(),
                                   AxisScrollParams(),
                                   ScrollFlags::ScrollOverflowHidden);

  AutoWeakFrame frame = GetFrame();
  if (!frame) {
    return;
  }

  // We use RelativeBounds rather than querying the frame directly because of
  // special cases like image map areas which don't have their own frame.
  // RelativeBounds overrides handle these special cases.
  nsIFrame* boundingFrame = nullptr;
  nsRect rect = RelativeBounds(&boundingFrame);
  MOZ_ASSERT(boundingFrame);

  // Compute x and y coordinates in dev pixels relative to the widget.
  nsPoint offsetToWidget;
  nsCOMPtr<nsIWidget> widget = boundingFrame->GetNearestWidget(offsetToWidget);
  if (!widget) {
    return;
  }

  rect += offsetToWidget;
  RefPtr<nsPresContext> presContext = presShell->GetPresContext();
  int32_t x = presContext->AppUnitsToDevPixels(rect.x + rect.width / 2);
  int32_t y = presContext->AppUnitsToDevPixels(rect.y + rect.height / 2);

  // Simulate a touch interaction by dispatching touch events with mouse events.
  // Even though we calculated x and y using the bounding frame, we must use the
  // primary frame for the event. In most cases, these two frames will be
  // different, but they are the same for special cases such as image map areas
  // which don't have their own frame.
  nsCoreUtils::DispatchTouchEvent(eTouchStart, x, y, mContent, frame, presShell,
                                  widget);

  // This isn't needed once bug 1924790 is fixed.
  mContent->OwnerDoc()->NotifyUserGestureActivation();

  nsCoreUtils::DispatchMouseEvent(eMouseDown, x, y, mContent, frame, presShell,
                                  widget);
  nsCoreUtils::DispatchTouchEvent(eTouchEnd, x, y, mContent, frame, presShell,
                                  widget);
  nsCoreUtils::DispatchMouseEvent(eMouseUp, x, y, mContent, frame, presShell,
                                  widget);
}

void LocalAccessible::ScrollToPoint(uint32_t aCoordinateType, int32_t aX,
                                    int32_t aY) {
  nsIFrame* frame = GetFrame();
  if (!frame) return;

  LayoutDeviceIntPoint coords =
      nsAccUtils::ConvertToScreenCoords(aX, aY, aCoordinateType, this);

  nsIFrame* parentFrame = frame;
  while ((parentFrame = parentFrame->GetParent())) {
    nsCoreUtils::ScrollFrameToPoint(parentFrame, frame, coords);
  }
}

bool LocalAccessible::IsScrollable() const {
  const auto [scrollPosition, scrollRange] = mDoc->ComputeScrollData(this);
  return scrollRange.width > 0 || scrollRange.height > 0;
}

bool LocalAccessible::IsPopover() const {
  dom::Element* el = Elm();
  return el && el->IsHTMLElement() && el->HasAttr(nsGkAtoms::popover);
}

bool LocalAccessible::IsEditable() const {
  dom::Element* el = Elm();
  return el && el->State().HasState(dom::ElementState::READWRITE);
}

void LocalAccessible::AppendTextTo(nsAString& aText, uint32_t aStartOffset,
                                   uint32_t aLength) {
  // Return text representation of non-text accessible within hypertext
  // accessible. Text accessible overrides this method to return enclosed text.
  if (aStartOffset != 0 || aLength == 0) return;

  MOZ_ASSERT(mParent,
             "Called on accessible unbound from tree. Result can be wrong.");
  nsIFrame* frame = GetFrame();
  // We handle something becoming display: none async, which means we won't have
  // a frame when we're queuing text removed events. Thus, it's important that
  // we produce text here even if there's no frame. Otherwise, we won't fire a
  // text removed event at all, which might leave client caches (e.g. NVDA
  // virtual buffers) with dead nodes.
  if (IsHTMLBr() || (frame && frame->IsBrFrame())) {
    aText += kForcedNewLineChar;
  } else if (mParent && nsAccUtils::MustPrune(mParent)) {
    // Expose the embedded object accessible as imaginary embedded object
    // character if its parent hypertext accessible doesn't expose children to
    // AT.
    aText += kImaginaryEmbeddedObjectChar;
  } else {
    aText += kEmbeddedObjectChar;
  }
}

void LocalAccessible::Shutdown() {
  // Mark the accessible as defunct, invalidate the child count and pointers to
  // other accessibles, also make sure none of its children point to this
  // parent
  mStateFlags |= eIsDefunct;

  int32_t childCount = mChildren.Length();
  for (int32_t childIdx = 0; childIdx < childCount; childIdx++) {
    mChildren.ElementAt(childIdx)->UnbindFromParent();
  }
  mChildren.Clear();

  mEmbeddedObjCollector = nullptr;

  if (mParent) mParent->RemoveChild(this);

  mContent = nullptr;
  mDoc = nullptr;
  if (SelectionMgr() && SelectionMgr()->AccessibleWithCaret(nullptr) == this) {
    SelectionMgr()->ResetCaretOffset();
  }
}

// LocalAccessible protected
ENameValueFlag LocalAccessible::ARIAName(nsString& aName) const {
  // 'slot' elements should ignore aria-label and aria-labelledby.
  if (mContent->IsHTMLElement(nsGkAtoms::slot)) {
    return eNameOK;
  }
  // aria-labelledby now takes precedence over aria-label
  bool notSimpleRelation = nsTextEquivUtils::GetTextEquivFromIDRefs(
      this, nsGkAtoms::aria_labelledby, aName);
  aName.CompressWhitespace();

  if (!aName.IsEmpty()) {
    return notSimpleRelation ? eNameOK : eNameFromRelations;
  }

  if (mContent->IsElement() &&
      nsAccUtils::GetARIAAttr(mContent->AsElement(), nsGkAtoms::aria_label,
                              aName)) {
    aName.CompressWhitespace();
  }

  return eNameOK;
}

// LocalAccessible protected
bool LocalAccessible::ARIADescription(nsString& aDescription) const {
  // aria-describedby takes precedence over aria-description
  nsTextEquivUtils::GetTextEquivFromIDRefs(this, nsGkAtoms::aria_describedby,
                                           aDescription);
  aDescription.CompressWhitespace();

  if (aDescription.IsEmpty() && mContent->IsElement() &&
      nsAccUtils::GetARIAAttr(mContent->AsElement(),
                              nsGkAtoms::aria_description, aDescription)) {
    aDescription.CompressWhitespace();
  }

  return !aDescription.IsEmpty();
}

// LocalAccessible protected
bool LocalAccessible::Tooltip(nsString& aTooltip) const {
  if (!HasOwnContent()) {
    return false;
  }

  if (mContent->IsHTMLElement()) {
    mContent->AsElement()->GetAttr(nsGkAtoms::title, aTooltip);
    aTooltip.CompressWhitespace();
    return !aTooltip.IsEmpty();
  } else if (mContent->IsXULElement()) {
    mContent->AsElement()->GetAttr(nsGkAtoms::tooltiptext, aTooltip);
    aTooltip.CompressWhitespace();
    return !aTooltip.IsEmpty();
  } else if (mContent->IsSVGElement()) {
    // If user agents need to choose among multiple 'desc' or 'title'
    // elements for processing, the user agent shall choose the first one.
    for (nsIContent* childElm = mContent->GetFirstChild(); childElm;
         childElm = childElm->GetNextSibling()) {
      if (childElm->IsSVGElement(nsGkAtoms::desc)) {
        nsTextEquivUtils::AppendTextEquivFromContent(this, childElm, &aTooltip);
        aTooltip.CompressWhitespace();
        return !aTooltip.IsEmpty();
      }
    }
  }
  return false;
}

// LocalAccessible protected
ENameValueFlag LocalAccessible::NativeName(nsString& aName) const {
  if (mContent->IsHTMLElement()) {
    LocalAccessible* label = nullptr;
    HTMLLabelIterator iter(Document(), this);
    bool notSimpleRelation = false;
    while ((label = iter.Next())) {
      notSimpleRelation |= nsTextEquivUtils::AppendTextEquivFromContent(
          this, label->GetContent(), &aName);
      aName.CompressWhitespace();
    }

    if (!aName.IsEmpty()) {
      return notSimpleRelation ? eNameOK : eNameFromRelations;
    }

    NameFromAssociatedXULLabel(mDoc, mContent, aName);
    if (!aName.IsEmpty()) {
      return eNameOK;
    }

    // We return eNameFromSubtree here to indicate that if there is no native
    // name we will calculate the name from the subtree next. This is useful for
    // noting where the name will come from in cases like name change
    // notifications.
    return eNameFromSubtree;
  }

  if (mContent->IsXULElement()) {
    XULElmName(mDoc, mContent, aName);
    if (!aName.IsEmpty()) return eNameOK;

    // We return eNameFromSubtree here to indicate that if there is no native
    // name we will calculate the name from the subtree next. See above.
    return eNameFromSubtree;
  }

  if (mContent->IsSVGElement()) {
    // If user agents need to choose among multiple 'desc' or 'title'
    // elements for processing, the user agent shall choose the first one.
    for (nsIContent* childElm = mContent->GetFirstChild(); childElm;
         childElm = childElm->GetNextSibling()) {
      if (childElm->IsSVGElement(nsGkAtoms::title)) {
        nsTextEquivUtils::AppendTextEquivFromContent(this, childElm, &aName);
        return eNameOK;
      }
    }
  }

  return eNameOK;
}

// LocalAccessible protected
void LocalAccessible::NativeDescription(nsString& aDescription) const {
  bool isXUL = mContent->IsXULElement();
  if (isXUL) {
    // Try XUL <description control="[id]">description text</description>
    XULDescriptionIterator iter(Document(), mContent);
    LocalAccessible* descr = nullptr;
    while ((descr = iter.Next())) {
      nsTextEquivUtils::AppendTextEquivFromContent(this, descr->GetContent(),
                                                   &aDescription);
    }
  }
}

// LocalAccessible protected
void LocalAccessible::BindToParent(LocalAccessible* aParent,
                                   uint32_t aIndexInParent) {
  MOZ_ASSERT(aParent, "This method isn't used to set null parent");
  MOZ_ASSERT(!mParent, "The child was expected to be moved");

#ifdef A11Y_LOG
  if (mParent) {
    logging::TreeInfo("BindToParent: stealing accessible", 0, "old parent",
                      mParent, "new parent", aParent, "child", this, nullptr);
  }
#endif

  mParent = aParent;
  mIndexInParent = aIndexInParent;

  if (mParent->HasNameDependent() || mParent->IsXULListItem() ||
      RelationByType(RelationType::LABEL_FOR).Next() ||
      nsTextEquivUtils::HasNameRule(mParent, eNameFromSubtreeRule)) {
    mContextFlags |= eHasNameDependent;
  } else {
    mContextFlags &= ~eHasNameDependent;
  }
  if (mParent->HasDescriptionDependent() ||
      RelationByType(RelationType::DESCRIPTION_FOR).Next()) {
    mContextFlags |= eHasDescriptionDependent;
  } else {
    mContextFlags &= ~eHasDescriptionDependent;
  }
  if (mParent->HasValueDependent() ||
      nsAccUtils::ShouldFireValueChangeForDescendantChanges(mParent)) {
    mContextFlags |= eHasValueDependent;
  } else {
    mContextFlags &= ~eHasValueDependent;
  }

  // Add name/description dependent flags for dependent content once
  // a name/description provider is added to doc.
  Relation rel = RelationByType(RelationType::LABELLED_BY);
  LocalAccessible* relTarget = nullptr;
  while ((relTarget = rel.LocalNext())) {
    if (!relTarget->HasNameDependent()) {
      relTarget->ModifySubtreeContextFlags(eHasNameDependent, true);
    }
  }

  rel = RelationByType(RelationType::DESCRIBED_BY);
  while ((relTarget = rel.LocalNext())) {
    if (!relTarget->HasDescriptionDependent()) {
      relTarget->ModifySubtreeContextFlags(eHasDescriptionDependent, true);
    }
  }

  if (mParent->IsAlert() || mParent->IsInsideAlert()) {
    mContextFlags |= eInsideAlert;
  }

  if (IsTableRow() || IsTableCell()) {
    CachedTableAccessible::Invalidate(this);
  }

  if (mContent && mContent->IsElement()) {
    // If this element is an open hint popover, queue cache updates for all its
    // invokers so they can recompute relations now that this popover and its
    // descendants are in the accessibility tree.
    if (IsOpenHintPopover(mContent)) {
      mDoc->QueueCacheUpdateForPopoverInvokers(mContent->AsElement());
    }

    // If this element is being bound to a parent that is an open hint popover,
    // queue cache updates for that popover's invokers. This handles the case
    // where interactive descendants are added to the hint popover after it's
    // already been shown.
    if (aParent->Elm() && IsOpenHintPopover(aParent->Elm())) {
      mDoc->QueueCacheUpdateForPopoverInvokers(aParent->Elm());
    }
  }
}

// LocalAccessible protected
void LocalAccessible::UnbindFromParent() {
  // We do this here to handle document shutdown and an Accessible being moved.
  // We do this for subtree removal in DocAccessible::UncacheChildrenInSubtree.
  if (IsTableRow() || IsTable() || IsTableCell()) {
    CachedTableAccessible::Invalidate(this);
  }

  // If this element is a hint popover being hidden, queue cache updates for all
  // its invokers so they can clear the relations.
  if (mContent && mContent->IsElement() && IsHintPopover(mContent)) {
    mDoc->QueueCacheUpdateForPopoverInvokers(mContent->AsElement());
  }

  mParent = nullptr;
  mIndexInParent = -1;
  mIndexOfEmbeddedChild = -1;

  delete mGroupInfo;
  mGroupInfo = nullptr;
  mContextFlags &= ~eHasNameDependent & ~eHasValueDependent & ~eInsideAlert;
}

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible public methods

RootAccessible* LocalAccessible::RootAccessible() const {
  nsCOMPtr<nsIDocShell> docShell = nsCoreUtils::GetDocShellFor(GetNode());
  NS_ASSERTION(docShell, "No docshell for mContent");
  if (!docShell) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShellTreeItem> root;
  docShell->GetInProcessRootTreeItem(getter_AddRefs(root));
  NS_ASSERTION(root, "No root content tree item");
  if (!root) {
    return nullptr;
  }

  DocAccessible* docAcc = nsAccUtils::GetDocAccessibleFor(root);
  return docAcc ? docAcc->AsRoot() : nullptr;
}

nsIFrame* LocalAccessible::GetFrame() const {
  return mContent ? nsLayoutUtils::GetPrimaryOrAreaImageFrame(mContent)
                  : nullptr;
}

nsINode* LocalAccessible::GetNode() const { return mContent; }

dom::Element* LocalAccessible::Elm() const {
  return dom::Element::FromNodeOrNull(mContent);
}

void LocalAccessible::Language(nsAString& aLanguage) {
  aLanguage.Truncate();

  if (!mDoc) return;

  nsCoreUtils::GetLanguageFor(mContent, nullptr, aLanguage);
  if (aLanguage.IsEmpty()) {  // Nothing found, so use document's language
    mDoc->DocumentNode()->GetHeaderData(nsGkAtoms::headerContentLanguage,
                                        aLanguage);
  }
}

bool LocalAccessible::InsertChildAt(uint32_t aIndex, LocalAccessible* aChild) {
  if (!aChild) return false;

  if (aIndex == mChildren.Length()) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mChildren.AppendElement(aChild);
  } else {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mChildren.InsertElementAt(aIndex, aChild);

    MOZ_ASSERT(mStateFlags & eKidsMutating, "Illicit children change");

    for (uint32_t idx = aIndex + 1; idx < mChildren.Length(); idx++) {
      mChildren[idx]->mIndexInParent = idx;
    }
  }

  if (aChild->IsText()) {
    mStateFlags |= eHasTextKids;
  }

  aChild->BindToParent(this, aIndex);
  return true;
}

bool LocalAccessible::RemoveChild(LocalAccessible* aChild) {
  MOZ_DIAGNOSTIC_ASSERT(aChild, "No child was given");
  MOZ_DIAGNOSTIC_ASSERT(aChild->mParent, "No parent");
  MOZ_DIAGNOSTIC_ASSERT(aChild->mParent == this, "Wrong parent");
  MOZ_DIAGNOSTIC_ASSERT(aChild->mIndexInParent != -1,
                        "Unbound child was given");
  MOZ_DIAGNOSTIC_ASSERT((mStateFlags & eKidsMutating) || aChild->IsDefunct() ||
                            aChild->IsDoc() || IsApplication(),
                        "Illicit children change");

  int32_t index = static_cast<uint32_t>(aChild->mIndexInParent);
  if (mChildren.SafeElementAt(index) != aChild) {
    MOZ_ASSERT_UNREACHABLE("A wrong child index");
    index = mChildren.IndexOf(aChild);
    if (index == -1) {
      MOZ_ASSERT_UNREACHABLE("No child was found");
      return false;
    }
  }

  aChild->UnbindFromParent();
  mChildren.RemoveElementAt(index);

  for (uint32_t idx = index; idx < mChildren.Length(); idx++) {
    mChildren[idx]->mIndexInParent = idx;
  }

  return true;
}

void LocalAccessible::RelocateChild(uint32_t aNewIndex,
                                    LocalAccessible* aChild) {
  MOZ_DIAGNOSTIC_ASSERT(aChild, "No child was given");
  MOZ_DIAGNOSTIC_ASSERT(aChild->mParent == this,
                        "A child from different subtree was given");
  MOZ_DIAGNOSTIC_ASSERT(aChild->mIndexInParent != -1,
                        "Unbound child was given");
  MOZ_DIAGNOSTIC_ASSERT(
      aChild->mParent->LocalChildAt(aChild->mIndexInParent) == aChild,
      "Wrong index in parent");
  MOZ_DIAGNOSTIC_ASSERT(
      static_cast<uint32_t>(aChild->mIndexInParent) != aNewIndex,
      "No move, same index");
  MOZ_DIAGNOSTIC_ASSERT(aNewIndex <= mChildren.Length(),
                        "Wrong new index was given");

  // Don't queue events while we're still building the initial tree.
  if (mDoc->HasLoadState(DocAccessible::eTreeConstructed)) {
    auto hideEvent = MakeRefPtr<AccHideEvent>(aChild, false);
    if (mDoc->Controller()->QueueMutationEvent(hideEvent)) {
      aChild->SetHideEventTarget(true);
    }
  }

  mEmbeddedObjCollector = nullptr;
  mChildren.RemoveElementAt(aChild->mIndexInParent);

  uint32_t startIdx = aNewIndex, endIdx = aChild->mIndexInParent;

  // If the child is moved after its current position.
  if (static_cast<uint32_t>(aChild->mIndexInParent) < aNewIndex) {
    startIdx = aChild->mIndexInParent;
    if (aNewIndex == mChildren.Length() + 1) {
      // The child is moved to the end.
      mChildren.AppendElement(aChild);
      endIdx = mChildren.Length() - 1;
    } else {
      mChildren.InsertElementAt(aNewIndex - 1, aChild);
      endIdx = aNewIndex;
    }
  } else {
    // The child is moved prior its current position.
    mChildren.InsertElementAt(aNewIndex, aChild);
  }

  for (uint32_t idx = startIdx; idx <= endIdx; idx++) {
    mChildren[idx]->mIndexInParent = idx;
    mChildren[idx]->mIndexOfEmbeddedChild = -1;
  }

  for (uint32_t idx = 0; idx < mChildren.Length(); idx++) {
    mChildren[idx]->mStateFlags |= eGroupInfoDirty;
  }

  if (mDoc->HasLoadState(DocAccessible::eTreeConstructed)) {
    auto showEvent = MakeRefPtr<AccShowEvent>(aChild);
    DebugOnly<bool> added = mDoc->Controller()->QueueMutationEvent(showEvent);
    MOZ_ASSERT(added);
    aChild->SetShowEventTarget(true);
  }
}

LocalAccessible* LocalAccessible::LocalChildAt(uint32_t aIndex) const {
  LocalAccessible* child = mChildren.SafeElementAt(aIndex, nullptr);
  if (!child) return nullptr;

#ifdef DEBUG
  LocalAccessible* realParent = child->mParent;
  NS_ASSERTION(!realParent || realParent == this,
               "Two accessibles have the same first child accessible!");
#endif

  return child;
}

uint32_t LocalAccessible::ChildCount() const { return mChildren.Length(); }

int32_t LocalAccessible::IndexInParent() const { return mIndexInParent; }

uint32_t LocalAccessible::EmbeddedChildCount() {
  if (mStateFlags & eHasTextKids) {
    if (!mEmbeddedObjCollector) {
      mEmbeddedObjCollector.reset(new EmbeddedObjCollector(this));
    }
    return mEmbeddedObjCollector->Count();
  }

  return ChildCount();
}

Accessible* LocalAccessible::EmbeddedChildAt(uint32_t aIndex) {
  if (mStateFlags & eHasTextKids) {
    if (!mEmbeddedObjCollector) {
      mEmbeddedObjCollector.reset(new EmbeddedObjCollector(this));
    }
    return mEmbeddedObjCollector->GetAccessibleAt(aIndex);
  }

  return ChildAt(aIndex);
}

int32_t LocalAccessible::IndexOfEmbeddedChild(Accessible* aChild) {
  MOZ_ASSERT(aChild->IsLocal());
  if (mStateFlags & eHasTextKids) {
    if (!mEmbeddedObjCollector) {
      mEmbeddedObjCollector.reset(new EmbeddedObjCollector(this));
    }
    return mEmbeddedObjCollector->GetIndexAt(aChild->AsLocal());
  }

  return GetIndexOf(aChild->AsLocal());
}

////////////////////////////////////////////////////////////////////////////////
// HyperLinkAccessible methods

bool LocalAccessible::IsLink() const {
  // Every embedded accessible within hypertext accessible implements
  // hyperlink interface.
  return mParent && mParent->IsHyperText() && !IsText();
}

////////////////////////////////////////////////////////////////////////////////
// SelectAccessible

void LocalAccessible::SelectedItems(nsTArray<Accessible*>* aItems) {
  AccIterator iter(this, filters::GetSelected);
  LocalAccessible* selected = nullptr;
  while ((selected = iter.Next())) aItems->AppendElement(selected);
}

uint32_t LocalAccessible::SelectedItemCount() {
  uint32_t count = 0;
  AccIterator iter(this, filters::GetSelected);
  LocalAccessible* selected = nullptr;
  while ((selected = iter.Next())) ++count;

  return count;
}

Accessible* LocalAccessible::GetSelectedItem(uint32_t aIndex) {
  AccIterator iter(this, filters::GetSelected);
  LocalAccessible* selected = nullptr;

  uint32_t index = 0;
  while ((selected = iter.Next()) && index < aIndex) index++;

  return selected;
}

bool LocalAccessible::IsItemSelected(uint32_t aIndex) {
  uint32_t index = 0;
  AccIterator iter(this, filters::GetSelectable);
  LocalAccessible* selected = nullptr;
  while ((selected = iter.Next()) && index < aIndex) index++;

  return selected && selected->State() & states::SELECTED;
}

bool LocalAccessible::AddItemToSelection(uint32_t aIndex) {
  uint32_t index = 0;
  AccIterator iter(this, filters::GetSelectable);
  LocalAccessible* selected = nullptr;
  while ((selected = iter.Next()) && index < aIndex) index++;

  if (selected) selected->SetSelected(true);

  return static_cast<bool>(selected);
}

bool LocalAccessible::RemoveItemFromSelection(uint32_t aIndex) {
  uint32_t index = 0;
  AccIterator iter(this, filters::GetSelectable);
  LocalAccessible* selected = nullptr;
  while ((selected = iter.Next()) && index < aIndex) index++;

  if (selected) selected->SetSelected(false);

  return static_cast<bool>(selected);
}

bool LocalAccessible::SelectAll() {
  bool success = false;
  LocalAccessible* selectable = nullptr;

  AccIterator iter(this, filters::GetSelectable);
  while ((selectable = iter.Next())) {
    success = true;
    selectable->SetSelected(true);
  }
  return success;
}

bool LocalAccessible::UnselectAll() {
  bool success = false;
  LocalAccessible* selected = nullptr;

  AccIterator iter(this, filters::GetSelected);
  while ((selected = iter.Next())) {
    success = true;
    selected->SetSelected(false);
  }
  return success;
}

////////////////////////////////////////////////////////////////////////////////
// Widgets

bool LocalAccessible::IsWidget() const { return false; }

bool LocalAccessible::IsActiveWidget() const {
  if (FocusMgr()->HasDOMFocus(mContent)) return true;

  // If text entry of combobox widget has a focus then the combobox widget is
  // active.
  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
  if (roleMapEntry && roleMapEntry->Is(nsGkAtoms::combobox)) {
    uint32_t childCount = ChildCount();
    for (uint32_t idx = 0; idx < childCount; idx++) {
      LocalAccessible* child = mChildren.ElementAt(idx);
      if (child->Role() == roles::ENTRY) {
        return FocusMgr()->HasDOMFocus(child->GetContent());
      }
    }
  }

  return false;
}

bool LocalAccessible::AreItemsOperable() const {
  return HasOwnContent() && mContent->IsElement() &&
         mContent->AsElement()->HasAttr(nsGkAtoms::aria_activedescendant);
}

LocalAccessible* LocalAccessible::CurrentItem() const {
  // Check for aria-activedescendant, which changes which element has focus.
  // For activedescendant, the ARIA spec does not require that the user agent
  // checks whether pointed node is actually a DOM descendant of the element
  // with the aria-activedescendant attribute.
  if (HasOwnContent() && mContent->IsElement()) {
    if (dom::Element* activeDescendantElm =
            nsCoreUtils::GetAriaActiveDescendantElement(
                mContent->AsElement())) {
      if (mContent->IsInclusiveDescendantOf(activeDescendantElm)) {
        // Don't want a cyclical descendant relationship. That would be bad.
        return nullptr;
      }

      DocAccessible* document = Document();
      if (document) return document->GetAccessible(activeDescendantElm);
    }
  }
  return nullptr;
}

void LocalAccessible::SetCurrentItem(const LocalAccessible* aItem) {}

LocalAccessible* LocalAccessible::ContainerWidget() const {
  if (HasARIARole() && mContent->HasID()) {
    for (LocalAccessible* parent = LocalParent(); parent;
         parent = parent->LocalParent()) {
      nsIContent* parentContent = parent->GetContent();
      if (parentContent && parentContent->IsElement() &&
          nsCoreUtils::GetAriaActiveDescendantElement(
              parentContent->AsElement())) {
        return parent;
      }

      // Don't cross DOM document boundaries.
      if (parent->IsDoc()) break;
    }
  }
  return nullptr;
}

bool LocalAccessible::IsActiveDescendant(LocalAccessible** aWidget) const {
  RelatedAccIterator widgets(mDoc, mContent, nsGkAtoms::aria_activedescendant);
  if (LocalAccessible* widget = widgets.Next()) {
    if (aWidget) {
      *aWidget = widget;
    }
    return true;
  }

  return false;
}

void LocalAccessible::Announce(const nsAString& aAnnouncement,
                               uint16_t aPriority) {
  // Don't fire announcement events if the document is in the background (not
  // active).
  if (!mDoc) {
    return;
  }
  dom::Document* docNode = mDoc->DocumentNode();
  if (!docNode) {
    return;
  }
  dom::BrowsingContext* bc = docNode->GetBrowsingContext();
  if (!bc || !bc->IsActive()) {
    return;
  }
  auto event = MakeRefPtr<AccAnnouncementEvent>(this, aAnnouncement, aPriority);
  nsEventShell::FireEvent(event);
}

////////////////////////////////////////////////////////////////////////////////
// LocalAccessible protected methods

void LocalAccessible::LastRelease() {
  // First cleanup if needed...
  if (mDoc) {
    Shutdown();
    NS_ASSERTION(!mDoc,
                 "A Shutdown() impl forgot to call its parent's Shutdown?");
  }
  // ... then die.
  delete this;
}

LocalAccessible* LocalAccessible::GetSiblingAtOffset(int32_t aOffset,
                                                     nsresult* aError) const {
  if (!mParent || mIndexInParent == -1) {
    if (aError) *aError = NS_ERROR_UNEXPECTED;

    return nullptr;
  }

  if (aError &&
      mIndexInParent + aOffset >= static_cast<int32_t>(mParent->ChildCount())) {
    *aError = NS_OK;  // fail peacefully
    return nullptr;
  }

  LocalAccessible* child = mParent->LocalChildAt(mIndexInParent + aOffset);
  if (aError && !child) *aError = NS_ERROR_UNEXPECTED;

  return child;
}

void LocalAccessible::ModifySubtreeContextFlags(uint32_t aContextFlags,
                                                bool aAdd) {
  Pivot pivot(this);
  LocalAccInSameDocRule rule;
  for (Accessible* anchor = this; anchor; anchor = pivot.Next(anchor, rule)) {
    MOZ_ASSERT(anchor->IsLocal());
    LocalAccessible* acc = anchor->AsLocal();
    if (aAdd) {
      acc->mContextFlags |= aContextFlags;
    } else {
      acc->mContextFlags &= ~aContextFlags;
    }
  }
}

double LocalAccessible::AttrNumericValue(nsAtom* aAttr) const {
  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
  if (!roleMapEntry || roleMapEntry->valueRule == eNoValue) {
    return UnspecifiedNaN<double>();
  }

  nsAutoString attrValue;
  if (!mContent->IsElement() ||
      !nsAccUtils::GetARIAAttr(mContent->AsElement(), aAttr, attrValue)) {
    return UnspecifiedNaN<double>();
  }

  nsresult error = NS_OK;
  double value = attrValue.ToDouble(&error);
  return NS_FAILED(error) ? UnspecifiedNaN<double>() : value;
}

uint32_t LocalAccessible::GetActionRule() const {
  if (!HasOwnContent() || (InteractiveState() & states::UNAVAILABLE)) {
    return eNoAction;
  }

  // Return "click" action on elements that have an attached popup menu.
  if (mContent->IsXULElement()) {
    if (mContent->AsElement()->HasAttr(nsGkAtoms::popup)) {
      return eClickAction;
    }
  }

  // Has registered 'click' event handler.
  bool isOnclick = nsCoreUtils::HasClickListener(mContent);

  if (isOnclick) return eClickAction;

  // Get an action based on ARIA role.
  const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
  if (roleMapEntry && roleMapEntry->actionRule != eNoAction) {
    return roleMapEntry->actionRule;
  }

  // Get an action based on ARIA attribute.
  if (nsAccUtils::HasDefinedARIAToken(mContent, nsGkAtoms::aria_expanded)) {
    return eExpandAction;
  }

  return eNoAction;
}

AccGroupInfo* LocalAccessible::GetGroupInfo() const {
  if (mGroupInfo && !(mStateFlags & eGroupInfoDirty)) {
    return mGroupInfo;
  }

  return nullptr;
}

AccGroupInfo* LocalAccessible::GetOrCreateGroupInfo() {
  if (mGroupInfo) {
    if (mStateFlags & eGroupInfoDirty) {
      mGroupInfo->Update();
      mStateFlags &= ~eGroupInfoDirty;
    }

    return mGroupInfo;
  }

  mGroupInfo = AccGroupInfo::CreateGroupInfo(this);
  mStateFlags &= ~eGroupInfoDirty;
  return mGroupInfo;
}

void LocalAccessible::SendCache(uint64_t aCacheDomain,
                                CacheUpdateType aUpdateType,
                                bool aAppendEventData) {
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_SendCache>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  if (!IPCAccessibilityActive() || !Document()) {
    return;
  }

  // Only send cache updates for domains that are active.
  const uint64_t domainsToSend = mDoc->EffectiveCacheDomains() & aCacheDomain;

  // Avoid sending cache updates if we have no domains to update.
  if (domainsToSend == CacheDomain::None) {
    return;
  }

  DocAccessibleChild* ipcDoc = mDoc->IPCDoc();
  if (!ipcDoc) {
    // This means DocAccessible::DoInitialUpdate hasn't been called yet, which
    // means the a11y tree hasn't been built yet. Therefore, this should only
    // be possible if this is a DocAccessible.
    MOZ_ASSERT(IsDoc(), "Called on a non-DocAccessible but IPCDoc is null");
    return;
  }

  RefPtr<AccAttributes> fields =
      BundleFieldsForCache(domainsToSend, aUpdateType);
  if (!fields->Count()) {
    return;
  }
  nsTArray<CacheData> data;
  data.AppendElement(CacheData(ID(), fields));
  if (aAppendEventData) {
    ipcDoc->PushMutationEventData(
        CacheEventData{std::move(aUpdateType), std::move(data)});
  } else {
    ipcDoc->SendCache(aUpdateType, data);
  }

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString updateTypeStr;
    if (aUpdateType == CacheUpdateType::Initial) {
      updateTypeStr = "Initial";
    } else if (aUpdateType == CacheUpdateType::Update) {
      updateTypeStr = "Update";
    } else {
      updateTypeStr = "Other";
    }
    PROFILER_MARKER_TEXT("LocalAccessible::SendCache", A11Y, {}, updateTypeStr);
  }
}

already_AddRefed<AccAttributes> LocalAccessible::BundleFieldsForCache(
    uint64_t aCacheDomain, CacheUpdateType aUpdateType,
    uint64_t aInitialDomains) {
  MOZ_ASSERT((~aCacheDomain & aInitialDomains) == CacheDomain::None,
             "Initial domain pushes without domains requested!");
  auto fields = MakeRefPtr<AccAttributes>();

  if (aUpdateType == CacheUpdateType::Initial) {
    aInitialDomains = CacheDomain::All;
  }
  // Pass a single cache domain in to query whether this is the initial push for
  // this domain.
  auto IsInitialPush = [aInitialDomains](uint64_t aCacheDomain) {
    return (aCacheDomain & aInitialDomains) == aCacheDomain;
  };
  auto IsUpdatePush = [aInitialDomains](uint64_t aCacheDomain) {
    return (aCacheDomain & aInitialDomains) == CacheDomain::None;
  };

  // Caching name for text leaf Accessibles is redundant, since their name is
  // always their text. Text gets handled below.
  if (aCacheDomain & CacheDomain::NameAndDescription && !IsText()) {
    nsString name;
    ENameValueFlag nameFlag = DirectName(name);

    if (IsTextField()) {
      MOZ_ASSERT(mContent);
      nsString placeholder;
      // Only cache the placeholder separately if it isn't used as the name.
      if (Elm()->GetAttr(nsGkAtoms::placeholder, placeholder) &&
          name != placeholder) {
        fields->SetAttribute(CacheKey::HTMLPlaceholder, std::move(placeholder));
      } else if (IsUpdatePush(CacheDomain::NameAndDescription)) {
        fields->SetAttribute(CacheKey::HTMLPlaceholder, DeleteEntry());
      }
    }

    if (nameFlag != eNameFromRelations && !name.IsEmpty()) {
      fields->SetAttribute(CacheKey::Name, std::move(name));
    } else if (IsUpdatePush(CacheDomain::NameAndDescription)) {
      fields->SetAttribute(CacheKey::Name, DeleteEntry());
    }

    nsString tooltip;
    if (Tooltip(tooltip)) {
      fields->SetAttribute(CacheKey::Tooltip, std::move(tooltip));
    } else if (IsUpdatePush(CacheDomain::NameAndDescription)) {
      fields->SetAttribute(CacheKey::Tooltip, DeleteEntry());
    }

    if (Maybe<nsString> cssAlt = nsCoreUtils::GetCssAltContent(mContent)) {
      fields->SetAttribute(CacheKey::CssAltContent, std::move(*cssAlt));
    } else if (IsUpdatePush(CacheDomain::NameAndDescription)) {
      fields->SetAttribute(CacheKey::CssAltContent, DeleteEntry());
    }

    nsString description;
    int32_t descFlag = Description(description);
    if (!description.IsEmpty()) {
      fields->SetAttribute(CacheKey::Description, std::move(description));
    } else if (IsUpdatePush(CacheDomain::NameAndDescription)) {
      fields->SetAttribute(CacheKey::Description, DeleteEntry());
    }

    if (descFlag != eDescriptionOK) {
      fields->SetAttribute(CacheKey::DescriptionValueFlag, descFlag);
    } else if (IsUpdatePush(CacheDomain::NameAndDescription)) {
      fields->SetAttribute(CacheKey::DescriptionValueFlag, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::Value) {
    // We cache the text value in 4 cases:
    // 1. Accessible is an HTML input type that holds a number.
    // 2. Accessible has a numeric value and an aria-valuetext.
    // 3. Accessible is a link, in which case value is the target URL.
    // 4. Accessible is an HTML input type that holds a color.
    // ... for all other cases we divine the value remotely.
    bool cacheValueText = false;
    if (HasNumericValue()) {
      fields->SetAttribute(CacheKey::NumericValue, CurValue());
      fields->SetAttribute(CacheKey::MaxValue, MaxValue());
      fields->SetAttribute(CacheKey::MinValue, MinValue());
      fields->SetAttribute(CacheKey::Step, Step());
      cacheValueText = NativeHasNumericValue() ||
                       (mContent->IsElement() &&
                        nsAccUtils::HasARIAAttr(mContent->AsElement(),
                                                nsGkAtoms::aria_valuetext));
    } else if (IsHTMLLink()) {
      cacheValueText = true;
    } else if (auto* input = dom::HTMLInputElement::FromNodeOrNull(mContent)) {
      cacheValueText = input->IsInputColor();
    }

    if (cacheValueText) {
      nsString value;
      Value(value);
      if (!value.IsEmpty()) {
        fields->SetAttribute(CacheKey::TextValue, std::move(value));
      } else if (IsUpdatePush(CacheDomain::Value)) {
        fields->SetAttribute(CacheKey::TextValue, DeleteEntry());
      }
    }

    if (IsImage()) {
      // Cache the src of images. This is used by some clients to help remediate
      // inaccessible images.
      MOZ_ASSERT(mContent, "Image must have mContent");
      nsString src;
      mContent->AsElement()->GetAttr(nsGkAtoms::src, src);
      if (!src.IsEmpty()) {
        fields->SetAttribute(CacheKey::SrcURL, std::move(src));
      } else if (IsUpdatePush(CacheDomain::Value)) {
        fields->SetAttribute(CacheKey::SrcURL, DeleteEntry());
      }
    }

    if (TagName() == nsGkAtoms::meter) {
      // We should only cache value region for HTML meter elements. A meter
      // should always have a value region, so this attribute should never
      // be empty (i.e. there is no DeleteEntry() clause here).
      HTMLMeterAccessible* meter = static_cast<HTMLMeterAccessible*>(this);
      fields->SetAttribute(CacheKey::ValueRegion, meter->ValueRegion());
    }
  }

  if (aCacheDomain & CacheDomain::Viewport && IsDoc()) {
    // Construct the viewport cache for this document. This cache domain will
    // only be requested after we finish painting.
    DocAccessible* doc = AsDoc();
    PresShell* presShell = doc->PresShellPtr();

    if (nsIFrame* rootFrame = presShell->GetRootFrame()) {
      nsTArray<nsIFrame*> frames;
      ScrollContainerFrame* sf = presShell->GetRootScrollContainerFrame();
      nsRect scrollPort = sf ? sf->GetScrollPortRect() : rootFrame->GetRect();

      nsLayoutUtils::GetFramesForArea(
          RelativeTo{rootFrame}, scrollPort, frames,
          {{// We don't add the ::OnlyVisible option here, because
            // it means we always consider frames with pointer-events: none.
            // See usage of HitTestIsForVisibility in nsDisplayList::HitTest.
            // This flag ensures the display lists are built, even if
            // the page hasn't finished loading.
            nsLayoutUtils::FrameForPointOption::IgnorePaintSuppression,
            // Each doc should have its own viewport cache, so we can
            // ignore cross-doc content as an optimization.
            nsLayoutUtils::FrameForPointOption::IgnoreCrossDoc}});

      nsTHashSet<LocalAccessible*> inViewAccs;
      nsTArray<uint64_t> viewportCache(frames.Length());
      // Layout considers table rows fully occluded by their containing cells.
      // This means they don't have their own display list items, and they won't
      // show up in the list returned from GetFramesForArea. To prevent table
      // rows from appearing offscreen, we manually add any rows for which we
      // have on-screen cells.
      LocalAccessible* prevParentRow = nullptr;
      for (nsIFrame* frame : frames) {
        if (frame->IsInlineFrame() && !frame->IsPrimaryFrame()) {
          // This is a line other than the first line in an inline element. Even
          // though there are multiple frames for this element (one per line),
          // there is only a single Accessible with bounds encompassing all the
          // frames. We don't have any additional information about the
          // individual continuation frames in our cache. Thus, we don't want
          // this Accessible to appear before leaves on other lines which are
          // later in the `frames` array. Otherwise, when hit testing, this
          // Accessible will match instead of those leaves. We will add this
          // Accessible when we get to its primary frame later.
          continue;
        }
        nsIContent* content = frame->GetContent();
        if (!content) {
          continue;
        }

        LocalAccessible* acc = doc->GetAccessible(content);
        // The document should always be present at the end of the list, so
        // including it is unnecessary and wasteful. We skip the document here
        // and handle it as a fallback when hit testing.
        if (!acc || acc == mDoc) {
          continue;
        }

        if (acc->IsTextLeaf() && nsAccUtils::MustPrune(acc->LocalParent())) {
          acc = acc->LocalParent();
        }
        if (acc->IsTableCell()) {
          LocalAccessible* parent = acc->LocalParent();
          if (parent && parent->IsTableRow() && parent != prevParentRow) {
            // If we've entered a new row since the last cell we saw, add the
            // previous parent row to our viewport cache here to maintain
            // hittesting order. Keep track of the current parent row.
            if (prevParentRow && inViewAccs.EnsureInserted(prevParentRow)) {
              viewportCache.AppendElement(prevParentRow->ID());
            }
            prevParentRow = parent;
          }
        } else if (acc->IsTable()) {
          // If we've encountered a table, we know we've already
          // handled all of this table's content (because we're traversing
          // in hittesting order). Add our table's final row to the viewport
          // cache before adding the table itself. Reset our marker for the next
          // table.
          if (prevParentRow && inViewAccs.EnsureInserted(prevParentRow)) {
            viewportCache.AppendElement(prevParentRow->ID());
          }
          prevParentRow = nullptr;
        } else if (acc->IsImageMap()) {
          // Layout doesn't walk image maps, so we do that
          // manually here. We do this before adding the map itself
          // so the children come earlier in the hittesting order.
          for (uint32_t i = 0; i < acc->ChildCount(); i++) {
            LocalAccessible* child = acc->LocalChildAt(i);
            MOZ_ASSERT(child);
            if (inViewAccs.EnsureInserted(child)) {
              MOZ_ASSERT(!child->IsDoc());
              viewportCache.AppendElement(child->ID());
            }
          }
        } else if (acc->IsHTMLCombobox()) {
          // Layout doesn't consider combobox lists (or their
          // currently selected items) to be onscreen, but we do.
          // Add those things manually here.
          HTMLComboboxAccessible* combobox =
              static_cast<HTMLComboboxAccessible*>(acc);
          HTMLComboboxListAccessible* list = combobox->List();
          LocalAccessible* currItem = combobox->SelectedOption();
          // Preserve hittesting order by adding the item, then
          // the list, and finally the combobox itself.
          if (currItem && inViewAccs.EnsureInserted(currItem)) {
            viewportCache.AppendElement(currItem->ID());
          }
          if (list && inViewAccs.EnsureInserted(list)) {
            viewportCache.AppendElement(list->ID());
          }
        }

        if (inViewAccs.EnsureInserted(acc)) {
          MOZ_ASSERT(!acc->IsDoc());
          viewportCache.AppendElement(acc->ID());
        }
      }

      // Always send the viewport cache, even if we have no accessibles
      // in it. We don't want to send a delete entry because the viewport
      // cache _does_ exist, it is simply representing an empty screen.
      fields->SetAttribute(CacheKey::Viewport, std::move(viewportCache));
    }
  }

  if (aCacheDomain & CacheDomain::APZ && IsDoc()) {
    PresShell* presShell = AsDoc()->PresShellPtr();
    MOZ_ASSERT(presShell, "Can't get APZ factor for null presShell");
    nsPoint viewportOffset =
        presShell->GetVisualViewportOffsetRelativeToLayoutViewport();
    if (viewportOffset.x || viewportOffset.y) {
      nsTArray<int32_t> offsetArray(2);
      offsetArray.AppendElement(viewportOffset.x);
      offsetArray.AppendElement(viewportOffset.y);
      fields->SetAttribute(CacheKey::VisualViewportOffset,
                           std::move(offsetArray));
    } else if (IsUpdatePush(CacheDomain::APZ)) {
      fields->SetAttribute(CacheKey::VisualViewportOffset, DeleteEntry());
    }
  }

  bool boundsChanged = false;
  nsIFrame* frame = GetFrame();
  if (aCacheDomain & CacheDomain::Bounds) {
    nsRect newBoundsRect = ParentRelativeBounds();

    // 1. Layout might notify us of a possible bounds change when the bounds
    // haven't really changed. Therefore, we cache the last  bounds we sent
    // and don't send an update if they haven't changed.
    // 2. For an initial cache push, we ignore 1)  and always send the bounds.
    // This handles the case where this LocalAccessible was moved (but not
    // re-created). In that case, we will have cached bounds, but we currently
    // do an initial cache push.
    MOZ_ASSERT(IsInitialPush(CacheDomain::Bounds) || mBounds.isSome(),
               "Incremental cache push but mBounds is not set!");

    if (OuterDocAccessible* doc = AsOuterDoc()) {
      if (nsIFrame* docFrame = doc->GetFrame()) {
        const nsMargin& newOffset = docFrame->GetUsedBorderAndPadding();
        Maybe<nsMargin> currOffset = doc->GetCrossDocOffset();
        if (!currOffset || *currOffset != newOffset) {
          // OOP iframe docs can't compute their position within their
          // cross-proc parent, so we have to manually cache that offset
          // on the parent (outer doc) itself. For simplicity and consistency,
          // we do this here for both OOP and in-process iframes. For in-process
          // iframes, this also avoids the need to push a cache update for the
          // embedded document when the iframe changes its padding, gets
          // re-created, etc. Similar to bounds, we maintain a local cache and a
          // remote cache to avoid sending redundant updates.
          doc->SetCrossDocOffset(newOffset);
          nsTArray<int32_t> offsetArray(2);
          offsetArray.AppendElement(newOffset.Side(eSideLeft));  // X offset
          offsetArray.AppendElement(newOffset.Side(eSideTop));   // Y offset
          fields->SetAttribute(CacheKey::CrossDocOffset,
                               std::move(offsetArray));
        }
      }
    }

    // mBounds should never be Nothing, but sometimes it is (see Bug 1922691).
    // We null check mBounds here to avoid a crash while we figure out how this
    // can happen.
    boundsChanged = IsInitialPush(CacheDomain::Bounds) || !mBounds ||
                    !newBoundsRect.IsEqualEdges(mBounds.value());
    if (boundsChanged) {
      mBounds = Some(newBoundsRect);

      auto ptr = std::make_unique<nsRect>(newBoundsRect);
      fields->SetAttribute(CacheKey::ParentRelativeBounds, std::move(ptr));
    }

    if (frame && frame->ScrollableOverflowRect().IsEmpty()) {
      fields->SetAttribute(CacheKey::IsClipped, true);
    } else if (IsUpdatePush(CacheDomain::Bounds)) {
      fields->SetAttribute(CacheKey::IsClipped, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::Text) {
    if (!HasChildren()) {
      // We only cache text and line offsets on leaf Accessibles.
      // Only text Accessibles can have actual text.
      if (IsText()) {
        nsString text;
        AppendTextTo(text);
        fields->SetAttribute(CacheKey::Text, std::move(text));
        TextLeafPoint point(this, 0);
        RefPtr<AccAttributes> attrs = point.GetTextAttributesLocalAcc(
            /* aIncludeDefaults */ false);
        if (attrs->Count()) {
          fields->SetAttribute(CacheKey::TextAttributes, std::move(attrs));
        } else if (IsUpdatePush(CacheDomain::Text)) {
          fields->SetAttribute(CacheKey::TextAttributes, DeleteEntry());
        }
      }
    }
    if (HyperTextAccessible* ht = AsHyperText()) {
      RefPtr<AccAttributes> attrs = ht->DefaultTextAttributes();
      LocalAccessible* parent = LocalParent();
      if (HyperTextAccessible* htParent =
              parent ? parent->AsHyperText() : nullptr) {
        if (RefPtr<AccAttributes> parentAttrs =
                htParent->DefaultTextAttributes()) {
          // Discard any entries that our parent already has.
          attrs->RemoveIdentical(parentAttrs);
        }
      }
      if (attrs->Count()) {
        fields->SetAttribute(CacheKey::TextAttributes, std::move(attrs));
      } else if (IsUpdatePush(CacheDomain::Text)) {
        fields->SetAttribute(CacheKey::TextAttributes, DeleteEntry());
      }
    } else if (!IsText()) {
      // Language is normally cached in text attributes, but Accessibles that
      // aren't HyperText or Text (e.g. <img>, <input type="radio">) don't have
      // text attributes. The Text domain isn't a great fit, but the kinds of
      // clients (e.g. screen readers) that care about language are likely to
      // care about text as well.
      nsString language;
      Language(language);
      if (!language.IsEmpty()) {
        fields->SetAttribute(CacheKey::Language, std::move(language));
      } else if (IsUpdatePush(CacheDomain::Text)) {
        fields->SetAttribute(CacheKey::Language, DeleteEntry());
      }
    }
  }

  // If text changes, we must also update text offset attributes.
  if (aCacheDomain & (CacheDomain::TextOffsetAttributes | CacheDomain::Text) &&
      IsTextLeaf()) {
    auto offsetAttrs = TextLeafPoint::GetTextOffsetAttributes(this);
    if (!offsetAttrs.IsEmpty()) {
      fields->SetAttribute(CacheKey::TextOffsetAttributes,
                           std::move(offsetAttrs));
    } else if (IsUpdatePush(CacheDomain::TextOffsetAttributes) ||
               IsUpdatePush(CacheDomain::Text)) {
      fields->SetAttribute(CacheKey::TextOffsetAttributes, DeleteEntry());
    }
  }

  if (aCacheDomain & (CacheDomain::TextBounds) && !HasChildren()) {
    // We cache line start offsets for both text and non-text leaf Accessibles
    // because non-text leaf Accessibles can still start a line.
    TextLeafPoint lineStart =
        TextLeafPoint(this, 0).FindNextLineStartSameLocalAcc(
            /* aIncludeOrigin */ true);
    int32_t lineStartOffset = lineStart ? lineStart.mOffset : -1;
    // We push line starts and text bounds in two cases:
    // 1. TextBounds is pushed initially.
    // 2. CacheDomain::Bounds was requested (indicating that the frame was
    // reflowed) but the bounds  didn't actually change. This can happen when
    // the spanned text is non-rectangular. For example, an Accessible might
    // cover two characters on one line and a single character on another line.
    // An insertion in a previous text node might cause it to shift such that it
    // now covers a single character on the first line and two characters on the
    // second line. Its bounding rect will be the same both before and after the
    // insertion. In this case, we use the first line start to determine whether
    // there was a change. This should be safe because the text didn't change in
    // this Accessible, so if the first line start doesn't shift, none of them
    // should shift.
    if (IsInitialPush(CacheDomain::TextBounds) ||
        aCacheDomain & CacheDomain::Text || boundsChanged ||
        mFirstLineStart != lineStartOffset) {
      mFirstLineStart = lineStartOffset;
      nsTArray<int32_t> lineStarts;
      for (; lineStart;
           lineStart = lineStart.FindNextLineStartSameLocalAcc(false)) {
        lineStarts.AppendElement(lineStart.mOffset);
      }
      if (!lineStarts.IsEmpty()) {
        fields->SetAttribute(CacheKey::TextLineStarts, std::move(lineStarts));
      } else if (IsUpdatePush(CacheDomain::TextBounds)) {
        fields->SetAttribute(CacheKey::TextLineStarts, DeleteEntry());
      }

      if (frame && frame->IsTextFrame()) {
        if (nsTextFrame* currTextFrame = do_QueryFrame(frame)) {
          nsTArray<int32_t> charData(nsAccUtils::TextLength(this) *
                                     kNumbersInRect);
          // Continuation offsets are calculated relative to the primary frame.
          // However, the acc's bounds are calculated using
          // GetAllInFlowRectsUnion. For wrapped text which starts part way
          // through a line, this might mean the top left of the acc is
          // different to the top left of the primary frame. This also happens
          // when the primary frame is empty (e.g. a blank line at the start of
          // pre-formatted text), since the union rect will exclude the origin
          // in that case. Calculate the offset from the acc's rect to the
          // primary frame's rect.
          nsRect accOffset =
              nsLayoutUtils::GetAllInFlowRectsUnion(frame, frame);
          while (currTextFrame) {
            nsPoint contOffset = currTextFrame->GetOffsetTo(frame);
            contOffset -= accOffset.TopLeft();
            int32_t length = currTextFrame->GetContentLength();
            nsTArray<nsRect> charBounds(length);
            currTextFrame->GetCharacterRectsInRange(
                currTextFrame->GetContentOffset(), length, charBounds);
            for (nsRect& charRect : charBounds) {
              if (charRect.width == 0 &&
                  !currTextFrame->StyleText()->WhiteSpaceIsSignificant()) {
                // GetCharacterRectsInRange gives us one rect per content
                // offset. However, TextLeafAccessibles use rendered offsets;
                // e.g. they might exclude some content white space. If we get
                // a 0 width rect and it's white space, skip this rect, since
                // this character isn't in the rendered text. We do have
                // a way to convert between content and rendered offsets, but
                // doing this for every character is expensive.
                const char16_t contentChar =
                    mContent->GetCharacterDataBuffer()->CharAt(
                        charData.Length() / kNumbersInRect);
                if (contentChar == u' ' || contentChar == u'\t' ||
                    contentChar == u'\n') {
                  continue;
                }
              }
              // We expect each char rect to be relative to the text leaf
              // acc this text lives in. Unfortunately, GetCharacterRectsInRange
              // returns rects relative to their continuation. Add the
              // continuation's relative position here to make our final
              // rect relative to the text leaf acc.
              charRect.MoveBy(contOffset);
              charData.AppendElement(charRect.x);
              charData.AppendElement(charRect.y);
              charData.AppendElement(charRect.width);
              charData.AppendElement(charRect.height);
            }
            currTextFrame = currTextFrame->GetNextContinuation();
          }
          if (charData.Length()) {
            fields->SetAttribute(CacheKey::TextBounds, std::move(charData));
          }
        }
      }
    }
  }

  if (aCacheDomain & CacheDomain::TransformMatrix) {
    bool transformed = false;
    if (frame && frame->IsTransformed()) {
      // This matrix is only valid when applied to CSSPixel points/rects
      // in the coordinate space of `frame`.
      gfx::Matrix4x4 mtx = nsDisplayTransform::GetResultingTransformMatrix(
          frame, nsPoint(0, 0), AppUnitsPerCSSPixel(),
          nsDisplayTransform::INCLUDE_PERSPECTIVE);
      // We might get back the identity matrix. This can happen if there is no
      // actual transform. For example, if an element has
      // will-change: transform, nsIFrame::IsTransformed will return true, but
      // this doesn't necessarily mean there is a transform right now.
      // Applying the identity matrix is effectively a no-op, so there's no
      // point caching it.
      transformed = !mtx.IsIdentity();
      if (transformed) {
        auto ptr = std::make_unique<gfx::Matrix4x4>(mtx);
        fields->SetAttribute(CacheKey::TransformMatrix, std::move(ptr));
      }
    }
    if (!transformed && IsUpdatePush(CacheDomain::TransformMatrix)) {
      // Otherwise, if we're bundling a transform update but this
      // frame isn't transformed (or doesn't exist), we need
      // to send a DeleteEntry() to remove any
      // transform that was previously cached for this frame.
      fields->SetAttribute(CacheKey::TransformMatrix, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::ScrollPosition && frame) {
    // We request these values unscaled when caching because the scaled values
    // have resolution multiplied in. We can encounter a race condition when
    // using APZ where the resolution is not propogated back to content in time
    // for it to be multipled into the scroll position calculation. Even if we
    // end up with the correct resolution cached in parent, our final bounds
    // will be incorrect. Instead of scaling here, we scale in parent with our
    // cached resolution so any incorrectness will be consistent and dependent
    // on a single cache update (Resolution) instead of two (Resolution and
    // ScrollPosition).
    const auto [scrollPosition, scrollRange] =
        mDoc->ComputeScrollData(this, /* aShouldScaleByResolution */ false);
    if (scrollRange.width || scrollRange.height) {
      // If the scroll range is 0 by 0, this acc is not scrollable. We
      // can't simply check scrollPosition != 0, since it's valid for scrollable
      // frames to have a (0, 0) position. We also can't check IsEmpty or
      // ZeroArea because frames with only one scrollable dimension will return
      // a height/width of zero for the non-scrollable dimension, yielding zero
      // area even if the width/height for the scrollable dimension is nonzero.
      // We also cache (0, 0) for accs with overflow:auto or overflow:scroll,
      // even if the content is not currently large enough to be scrollable
      // right now -- these accs have a non-zero scroll range.
      nsTArray<int32_t> positionArr(2);
      positionArr.AppendElement(scrollPosition.x);
      positionArr.AppendElement(scrollPosition.y);
      fields->SetAttribute(CacheKey::ScrollPosition, std::move(positionArr));
    } else if (IsUpdatePush(CacheDomain::ScrollPosition)) {
      fields->SetAttribute(CacheKey::ScrollPosition, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::DOMNodeIDAndClass && mContent) {
    nsAtom* id = mContent->GetID();
    if (id) {
      fields->SetAttribute(CacheKey::DOMNodeID, id);
    } else if (IsUpdatePush(CacheDomain::DOMNodeIDAndClass)) {
      fields->SetAttribute(CacheKey::DOMNodeID, DeleteEntry());
    }

    if (dom::Element* el = Elm()) {
      nsTArray<RefPtr<nsAtom>> classes;
      if (const nsAttrValue* attr = el->GetClasses()) {
        for (uint32_t i = 0; i < attr->GetAtomCount(); i++) {
          classes.AppendElement(attr->AtomAt(i));
        }
      }
      if (!classes.IsEmpty()) {
        fields->SetAttribute(CacheKey::DOMNodeClass, std::move(classes));
      } else if (IsUpdatePush(CacheDomain::DOMNodeIDAndClass)) {
        fields->SetAttribute(CacheKey::DOMNodeClass, DeleteEntry());
      }
    }
  }

  // State is only included in the initial push. Thereafter, cached state is
  // updated via events.
  if (aCacheDomain & CacheDomain::State) {
    if (IsInitialPush(CacheDomain::State)) {
      // Most states are updated using state change events, so we only send
      // these for the initial cache push.
      uint64_t state = ExplicitState();
      // Exclude states which must be calculated by RemoteAccessible.
      state &= ~kRemoteCalculatedStates;
      fields->SetAttribute(CacheKey::State, state);
    }
    // If aria-selected isn't specified, there may be no SELECTED state.
    // However, aria-selected can be implicit in some cases when an item is
    // focused. We don't want to do this if aria-selected is explicitly
    // set to "false", so we need to differentiate between false and unset.
    if (auto ariaSelected = ARIASelected()) {
      fields->SetAttribute(CacheKey::ARIASelected, *ariaSelected);
    } else if (IsUpdatePush(CacheDomain::State)) {
      fields->SetAttribute(CacheKey::ARIASelected, DeleteEntry());  // Unset.
    }
  }

  if (aCacheDomain & CacheDomain::GroupInfo && mContent) {
    for (nsAtom* attr : {nsGkAtoms::aria_level, nsGkAtoms::aria_setsize,
                         nsGkAtoms::aria_posinset}) {
      int32_t value = 0;
      if (nsCoreUtils::GetUIntAttr(mContent, attr, &value)) {
        fields->SetAttribute(attr, value);
      } else if (IsUpdatePush(CacheDomain::GroupInfo)) {
        fields->SetAttribute(attr, DeleteEntry());
      }
    }

    int32_t headingLevel = HeadingLevel();
    if (headingLevel > 0) {
      fields->SetAttribute(CacheKey::HeadingLevel, headingLevel);
    } else if (IsUpdatePush(CacheDomain::GroupInfo)) {
      fields->SetAttribute(CacheKey::HeadingLevel, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::Actions) {
    if (HasPrimaryAction()) {
      // Here we cache the primary action.
      nsAutoString actionName;
      ActionNameAt(0, actionName);
      RefPtr<nsAtom> actionAtom = NS_Atomize(actionName);
      fields->SetAttribute(CacheKey::PrimaryAction, actionAtom);
    } else if (IsUpdatePush(CacheDomain::Actions)) {
      fields->SetAttribute(CacheKey::PrimaryAction, DeleteEntry());
    }

    if (ImageAccessible* imgAcc = AsImage()) {
      // Here we cache the showlongdesc action.
      if (imgAcc->HasLongDesc()) {
        fields->SetAttribute(CacheKey::HasLongdesc, true);
      } else if (IsUpdatePush(CacheDomain::Actions)) {
        fields->SetAttribute(CacheKey::HasLongdesc, DeleteEntry());
      }
    }

    KeyBinding accessKey = AccessKey();
    if (!accessKey.IsEmpty()) {
      fields->SetAttribute(CacheKey::AccessKey, accessKey.Serialize());
    } else if (IsUpdatePush(CacheDomain::Actions)) {
      fields->SetAttribute(CacheKey::AccessKey, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::Style) {
    if (RefPtr<nsAtom> display = DisplayStyle()) {
      fields->SetAttribute(CacheKey::CSSDisplay, display);
    }

    float opacity = Opacity();
    if (opacity != 1.0f) {
      fields->SetAttribute(CacheKey::Opacity, opacity);
    } else if (IsUpdatePush(CacheDomain::Style)) {
      fields->SetAttribute(CacheKey::Opacity, DeleteEntry());
    }

    WritingMode wm = GetWritingMode();
    if (wm.GetBits()) {
      fields->SetAttribute(CacheKey::WritingMode, wm);
    } else if (IsUpdatePush(CacheDomain::Style)) {
      fields->SetAttribute(CacheKey::WritingMode, DeleteEntry());
    }

    if (frame &&
        frame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
        nsLayoutUtils::IsReallyFixedPos(frame)) {
      fields->SetAttribute(CacheKey::CssPosition, nsGkAtoms::fixed);
    } else if (IsUpdatePush(CacheDomain::Style)) {
      fields->SetAttribute(CacheKey::CssPosition, DeleteEntry());
    }

    if (frame) {
      nsAutoCString overflow;
      frame->Style()->GetComputedPropertyValue(eCSSProperty_overflow, overflow);
      RefPtr<nsAtom> overflowAtom = NS_Atomize(overflow);
      if (overflowAtom == nsGkAtoms::hidden) {
        fields->SetAttribute(CacheKey::CSSOverflow, nsGkAtoms::hidden);
      } else if (IsUpdatePush(CacheDomain::Style)) {
        fields->SetAttribute(CacheKey::CSSOverflow, DeleteEntry());
      }
    }
  }

  if (aCacheDomain & CacheDomain::Table) {
    if (auto* table = HTMLTableAccessible::GetFrom(this)) {
      if (table->IsProbablyLayoutTable()) {
        fields->SetAttribute(CacheKey::TableLayoutGuess, true);
      } else if (IsUpdatePush(CacheDomain::Table)) {
        fields->SetAttribute(CacheKey::TableLayoutGuess, DeleteEntry());
      }
    } else if (auto* cell = HTMLTableCellAccessible::GetFrom(this)) {
      // For HTML table cells, we must use the HTMLTableCellAccessible
      // GetRow/ColExtent methods rather than using the DOM attributes directly.
      // This is because of things like rowspan="0" which depend on knowing
      // about thead, tbody, etc., which is info we don't have in the a11y tree.
      int32_t value = static_cast<int32_t>(cell->RowExtent());
      MOZ_ASSERT(value > 0);
      if (value > 1) {
        fields->SetAttribute(CacheKey::RowSpan, value);
      } else if (IsUpdatePush(CacheDomain::Table)) {
        fields->SetAttribute(CacheKey::RowSpan, DeleteEntry());
      }
      value = static_cast<int32_t>(cell->ColExtent());
      MOZ_ASSERT(value > 0);
      if (value > 1) {
        fields->SetAttribute(CacheKey::ColSpan, value);
      } else if (IsUpdatePush(CacheDomain::Table)) {
        fields->SetAttribute(CacheKey::ColSpan, DeleteEntry());
      }
      if (mContent->AsElement()->HasAttr(nsGkAtoms::headers)) {
        nsTArray<uint64_t> headers;
        AssociatedElementsIterator iter(mDoc, mContent, nsGkAtoms::headers);
        while (LocalAccessible* cell = iter.Next()) {
          if (cell->IsTableCell()) {
            headers.AppendElement(cell->ID());
          }
        }
        fields->SetAttribute(CacheKey::CellHeaders, std::move(headers));
      } else if (IsUpdatePush(CacheDomain::Table)) {
        fields->SetAttribute(CacheKey::CellHeaders, DeleteEntry());
      }
    }
  }

  if (aCacheDomain & CacheDomain::ARIA && mContent && mContent->IsElement()) {
    // We use a nested AccAttributes to make cache updates simpler. Rather than
    // managing individual removals, we just replace or remove the entire set of
    // ARIA attributes.
    RefPtr<AccAttributes> ariaAttrs;
    aria::AttrIterator attrIt(mContent);
    while (attrIt.Next()) {
      if (!ariaAttrs) {
        ariaAttrs = MakeRefPtr<AccAttributes>();
      }
      attrIt.ExposeAttr(ariaAttrs);
    }
    if (ariaAttrs) {
      fields->SetAttribute(CacheKey::ARIAAttributes, std::move(ariaAttrs));
    } else if (IsUpdatePush(CacheDomain::ARIA)) {
      fields->SetAttribute(CacheKey::ARIAAttributes, DeleteEntry());
    }

    if (HasCustomActions()) {
      fields->SetAttribute(CacheKey::HasActions, true);
    } else if (IsUpdatePush(CacheDomain::ARIA)) {
      fields->SetAttribute(CacheKey::HasActions, DeleteEntry());
    }
  }

  if (aCacheDomain & CacheDomain::Relations && mContent) {
    if (IsHTMLRadioButton() ||
        (mContent->IsElement() &&
         mContent->AsElement()->IsHTMLElement(nsGkAtoms::a))) {
      // HTML radio buttons with the same name should be grouped
      // and returned together when their MEMBER_OF relation is
      // requested. Computing LINKS_TO also requires we cache `name` on
      // anchor elements.
      nsString name;
      mContent->AsElement()->GetAttr(nsGkAtoms::name, name);
      if (!name.IsEmpty()) {
        fields->SetAttribute(CacheKey::DOMName, std::move(name));
      } else if (IsUpdatePush(CacheDomain::Relations)) {
        // It's possible we used to have a name and it's since been
        // removed. Send a delete entry.
        fields->SetAttribute(CacheKey::DOMName, DeleteEntry());
      }
    }

    // Track whether aria-details/aria-describedby have targets, so that
    // commandfor/popovertarget can respect their precedence.
    bool hasAriaDetails = false;
    bool hasAriaDescribedby = false;

    for (auto const& data : kRelationTypeAtoms) {
      nsTArray<uint64_t> ids;
      nsStaticAtom* const relAtom = data.mAtom;
      if (data.mValidTag && !mContent->IsHTMLElement(data.mValidTag)) {
        continue;
      }

      Relation rel;
      if (data.mType == RelationType::LABEL_FOR) {
        // Labels are a special case -- we need to validate that the target of
        // their `for` attribute is in fact labelable. DOM checks this when we
        // call GetLabeledElementInternal(). If a label contains an element we
        // will return it here.
        if (dom::HTMLLabelElement* labelEl =
                dom::HTMLLabelElement::FromNode(mContent)) {
          rel.AppendTarget(mDoc, labelEl->GetLabeledElementInternal());
        }
      } else if (data.mType == RelationType::DETAILS) {
        if (relAtom == nsGkAtoms::aria_details) {
          rel.AppendIter(
              new AssociatedElementsIterator(mDoc, mContent, relAtom));
        } else if (relAtom == nsGkAtoms::commandfor) {
          // aria-details takes precedence; skip if it has targets.
          if (!hasAriaDetails) {
            if (LocalAccessible* target = GetCommandForDetailsRelation()) {
              rel.AppendTarget(target);
              fields->SetAttribute(CacheKey::PopoverInvokerIsDetails, true);
            }
          }
        } else if (relAtom == nsGkAtoms::popovertarget) {
          // aria-details takes precedence; skip if it has targets.
          if (!hasAriaDetails) {
            if (LocalAccessible* target = GetPopoverTargetDetailsRelation()) {
              rel.AppendTarget(target);
              fields->SetAttribute(CacheKey::PopoverInvokerIsDetails, true);
            }
          }
        } else if (relAtom == nsGkAtoms::target) {
          if (LocalAccessible* target =
                  GetAnchorPositionTargetDetailsRelation()) {
            rel.AppendTarget(target);
          }
        } else {
          MOZ_ASSERT_UNREACHABLE("Unknown details relAtom");
        }
      } else if (data.mType == RelationType::DESCRIBED_BY) {
        if (relAtom == nsGkAtoms::aria_describedby) {
          rel.AppendIter(
              new AssociatedElementsIterator(mDoc, mContent, relAtom));
        } else if (relAtom == nsGkAtoms::commandfor) {
          // aria-describedby takes precedence; skip if it has targets.
          if (!hasAriaDescribedby) {
            if (LocalAccessible* target = GetCommandForDescribedByRelation()) {
              rel.AppendTarget(target);
            }
          }
        } else if (relAtom == nsGkAtoms::popovertarget) {
          // aria-describedby takes precedence; skip if it has targets.
          if (!hasAriaDescribedby) {
            if (LocalAccessible* target =
                    GetPopoverTargetDescribedByRelation()) {
              rel.AppendTarget(target);
            }
          }
        }
      } else if (data.mType == RelationType::CONTROLLER_FOR) {
        // We need to use RelationByType for controls because it might include
        // failed aria-owned relocations or it may be an output element.
        // Nothing exposes an implicit reverse controls relation, so using
        // RelationByType here is fine.
        rel = RelationByType(data.mType);
      } else {
        // We use an AssociatedElementsIterator here instead of calling
        // RelationByType directly because we only want to cache explicit
        // relations. Implicit relations (e.g. LABEL_FOR exposed on the target
        // of aria-labelledby) will be computed and stored separately in the
        // parent process.
        rel.AppendIter(new AssociatedElementsIterator(mDoc, mContent, relAtom));
      }

      while (LocalAccessible* acc = rel.LocalNext()) {
        ids.AppendElement(acc->ID());
      }
      if (ids.Length()) {
        fields->SetAttribute(relAtom, std::move(ids));
        // Track that aria-details/aria-describedby have targets for precedence.
        if (relAtom == nsGkAtoms::aria_details) {
          hasAriaDetails = true;
        } else if (relAtom == nsGkAtoms::aria_describedby) {
          hasAriaDescribedby = true;
        }
      } else if (IsUpdatePush(CacheDomain::Relations) &&
                 !fields->HasAttribute(relAtom)) {
        // Only send DeleteEntry if we haven't already cached a value for this
        // atom in an earlier iteration. This handles atoms that appear multiple
        // times in kRelationTypeAtoms (e.g., popovertarget/commandfor appear
        // for both DETAILS and DESCRIBED_BY).
        fields->SetAttribute(relAtom, DeleteEntry());
      }
    }
  }

#if defined(XP_WIN)
  if (aCacheDomain & CacheDomain::InnerHTML && HasOwnContent() &&
      mContent->IsMathMLElement(nsGkAtoms::math)) {
    nsString innerHTML;
    mContent->AsElement()->GetInnerHTML(innerHTML, IgnoreErrors());
    fields->SetAttribute(CacheKey::InnerHTML, std::move(innerHTML));
  }
#endif  // defined(XP_WIN)

  if (aUpdateType == CacheUpdateType::Initial) {
    // Add fields which never change and thus only need to be included in the
    // initial cache push.
    if (mContent && mContent->IsElement()) {
      fields->SetAttribute(CacheKey::TagName, mContent->NodeInfo()->NameAtom());

      dom::Element* el = mContent->AsElement();
      if (IsTextField() || IsDateTimeField()) {
        // Cache text input types. Accessible is recreated if this changes,
        // so it is considered immutable.
        if (const nsAttrValue* attr = el->GetParsedAttr(nsGkAtoms::type)) {
          RefPtr<nsAtom> inputType = attr->GetAsAtom();
          if (inputType) {
            fields->SetAttribute(CacheKey::InputType, inputType);
          }
        }
      }

      // Changing the role attribute currently re-creates the Accessible, so
      // it's immutable in the cache.
      if (const nsRoleMapEntry* roleMap = ARIARoleMap()) {
        // Most of the time, the role attribute is a single, known role. We
        // already send the map index, so we don't need to double up.
        if (!nsAccUtils::ARIAAttrValueIs(el, nsGkAtoms::role, roleMap->roleAtom,
                                         eIgnoreCase)) {
          // Multiple roles or unknown roles are rare, so just send them as a
          // string.
          nsAutoString role;
          nsAccUtils::GetARIAAttr(el, nsGkAtoms::role, role);
          fields->SetAttribute(CacheKey::ARIARole, std::move(role));
        }
      }

      if (auto* htmlEl = nsGenericHTMLElement::FromNode(mContent)) {
        // Changing popover recreates the Accessible, so it's immutable in the
        // cache.
        nsAutoString popover;
        htmlEl->GetPopover(popover);
        if (!popover.IsEmpty()) {
          fields->SetAttribute(CacheKey::PopupType,
                               RefPtr{NS_Atomize(popover)});
        }
      }
    }

    if (frame) {
      // Note our frame's current computed style so we can track style changes
      // later on.
      mOldComputedStyle = frame->Style();
      if (frame->IsTransformed()) {
        mStateFlags |= eOldFrameHasValidTransformStyle;
      } else {
        mStateFlags &= ~eOldFrameHasValidTransformStyle;
      }
    }

    if (IsDoc()) {
      if (PresShell* presShell = AsDoc()->PresShellPtr()) {
        // Send the initial resolution of the document. When this changes, we
        // will ne notified via nsAS::NotifyOfResolutionChange
        float resolution = presShell->GetResolution();
        fields->SetAttribute(CacheKey::Resolution, resolution);
        int32_t appUnitsPerDevPixel =
            presShell->GetPresContext()->AppUnitsPerDevPixel();
        fields->SetAttribute(CacheKey::AppUnitsPerDevPixel,
                             appUnitsPerDevPixel);
      }

      nsString mimeType;
      AsDoc()->MimeType(mimeType);
      fields->SetAttribute(CacheKey::MimeType, std::move(mimeType));
    }
  }

  if ((aCacheDomain & (CacheDomain::Text | CacheDomain::ScrollPosition |
                       CacheDomain::APZ) ||
       boundsChanged) &&
      mDoc) {
    mDoc->SetViewportCacheDirty(true);
  }

  return fields.forget();
}

void LocalAccessible::MaybeQueueCacheUpdateForStyleChanges() {
  // mOldComputedStyle might be null if the initial cache hasn't been sent yet.
  // In that case, there is nothing to do here.
  if (!IPCAccessibilityActive() || !mOldComputedStyle) {
    return;
  }

  if (nsIFrame* frame = GetFrame()) {
    const ComputedStyle* newStyle = frame->Style();

    const auto overflowProps =
        nsCSSPropertyIDSet({eCSSProperty_overflow_x, eCSSProperty_overflow_y});

    for (NonCustomCSSPropertyId overflowProp : overflowProps) {
      nsAutoCString oldOverflow, newOverflow;
      mOldComputedStyle->GetComputedPropertyValue(overflowProp, oldOverflow);
      newStyle->GetComputedPropertyValue(overflowProp, newOverflow);

      if (oldOverflow != newOverflow) {
        if (oldOverflow.Equals("hidden"_ns) ||
            newOverflow.Equals("hidden"_ns)) {
          mDoc->QueueCacheUpdate(this, CacheDomain::Style);
        }
        if (oldOverflow.Equals("auto"_ns) || newOverflow.Equals("auto"_ns) ||
            oldOverflow.Equals("scroll"_ns) ||
            newOverflow.Equals("scroll"_ns)) {
          // We cache a (0,0) scroll position for frames that have overflow
          // styling which means they _could_ become scrollable, even if the
          // content within them doesn't currently scroll.
          mDoc->QueueCacheUpdate(this, CacheDomain::ScrollPosition);
        }
      } else {
        ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(frame);
        if (!scrollContainerFrame && (newOverflow.Equals("auto"_ns) ||
                                      newOverflow.Equals("scroll"_ns))) {
          // A document's body element can lose its scroll frame if the root
          // element (eg. <html>) is restyled to overflow scroll/auto. In that
          // case we will not get any useful notifications for the body element
          // except for a reframe to a non-scrolling frame.
          mDoc->QueueCacheUpdate(this, CacheDomain::ScrollPosition);
        }
      }
    }

    nsAutoCString oldDisplay, newDisplay;
    mOldComputedStyle->GetComputedPropertyValue(eCSSProperty_display,
                                                oldDisplay);
    newStyle->GetComputedPropertyValue(eCSSProperty_display, newDisplay);

    nsAutoCString oldOpacity, newOpacity;
    mOldComputedStyle->GetComputedPropertyValue(eCSSProperty_opacity,
                                                oldOpacity);
    newStyle->GetComputedPropertyValue(eCSSProperty_opacity, newOpacity);

    if (oldDisplay != newDisplay || oldOpacity != newOpacity) {
      // CacheDomain::Style covers both display and opacity, so if
      // either property has changed, send an update for the entire domain.
      mDoc->QueueCacheUpdate(this, CacheDomain::Style);
    }

    if (mOldComputedStyle->StyleDisplay()->mAnchorName !=
        newStyle->StyleDisplay()->mAnchorName) {
      // Anchor name changes can affect the result of
      // details relations.
      mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
    }

    if (mOldComputedStyle->MaybeAnchorPosReferencesDiffer(newStyle)) {
      // Refresh the cache for details on current target (ie. the old style)
      mDoc->RefreshAnchorRelationCacheForTarget(this);
      // Refresh the cache for details on new target asynchronously after the
      // next layout tick for new style.
      mDoc->Controller()->ScheduleNotification<DocAccessible>(
          mDoc, &DocAccessible::RefreshAnchorRelationCacheForTarget, this);
    }

    nsAutoCString oldPosition, newPosition;
    mOldComputedStyle->GetComputedPropertyValue(eCSSProperty_position,
                                                oldPosition);
    newStyle->GetComputedPropertyValue(eCSSProperty_position, newPosition);

    if (oldPosition != newPosition) {
      RefPtr<nsAtom> oldAtom = NS_Atomize(oldPosition);
      RefPtr<nsAtom> newAtom = NS_Atomize(newPosition);
      if (oldAtom == nsGkAtoms::fixed || newAtom == nsGkAtoms::fixed) {
        mDoc->QueueCacheUpdate(this, CacheDomain::Style);
      }
    }

    bool newHasValidTransformStyle =
        newStyle->StyleDisplay()->HasTransform(frame);
    bool oldHasValidTransformStyle =
        (mStateFlags & eOldFrameHasValidTransformStyle) != 0;

    // We should send a transform update if we're adding or
    // removing transform styling altogether.
    bool sendTransformUpdate =
        newHasValidTransformStyle || oldHasValidTransformStyle;

    if (newHasValidTransformStyle && oldHasValidTransformStyle) {
      // If we continue to have transform styling, verify
      // our transform has actually changed.
      nsChangeHint transformHint =
          newStyle->StyleDisplay()->CalcTransformPropertyDifference(
              *mOldComputedStyle->StyleDisplay());
      // If this hint exists, it implies we found a property difference
      sendTransformUpdate = !!transformHint;
    }

    if (sendTransformUpdate) {
      // If our transform matrix has changed, it's possible our
      // viewport cache has also changed.
      mDoc->SetViewportCacheDirty(true);
      // Queuing a cache update for the TransformMatrix domain doesn't
      // necessarily mean we'll send the matrix itself, we may
      // send a DeleteEntry() instead. See BundleFieldsForCache for
      // more information.
      mDoc->QueueCacheUpdate(this, CacheDomain::TransformMatrix);
    }

    mOldComputedStyle = newStyle;
    if (newHasValidTransformStyle) {
      mStateFlags |= eOldFrameHasValidTransformStyle;
    } else {
      mStateFlags &= ~eOldFrameHasValidTransformStyle;
    }
  }
}

nsAtom* LocalAccessible::TagName() const {
  return mContent && mContent->IsElement() ? mContent->NodeInfo()->NameAtom()
                                           : nullptr;
}

already_AddRefed<nsAtom> LocalAccessible::DisplayStyle() const {
  dom::Element* elm = Elm();
  if (!elm) {
    return nullptr;
  }
  if (elm->IsHTMLElement(nsGkAtoms::area)) {
    // This is an image map area. CSS is irrelevant here.
    return nullptr;
  }
  static const dom::Element::AttrValuesArray presentationRoles[] = {
      nsGkAtoms::none, nsGkAtoms::presentation, nullptr};
  if (nsAccUtils::FindARIAAttrValueIn(elm, nsGkAtoms::role, presentationRoles,
                                      eIgnoreCase) != AttrArray::ATTR_MISSING &&
      IsGeneric()) {
    // This Accessible has been marked presentational, but we forced a generic
    // Accessible for some reason; e.g. CSS transform. Don't expose display in
    // this case, as the author might be explicitly trying to avoid said
    // exposure.
    return nullptr;
  }
  RefPtr<const ComputedStyle> style =
      nsComputedDOMStyle::GetComputedStyleNoFlush(elm);
  if (!style) {
    // The element is not styled, maybe not in the flat tree?
    return nullptr;
  }
  nsAutoCString value;
  style->GetComputedPropertyValue(eCSSProperty_display, value);
  return NS_Atomize(value);
}

float LocalAccessible::Opacity() const {
  if (nsIFrame* frame = GetFrame()) {
    return frame->StyleEffects()->mOpacity;
  }

  return 1.0f;
}

WritingMode LocalAccessible::GetWritingMode() const {
  if (nsIFrame* frame = GetFrame()) {
    return WritingMode(frame->Style());
  }

  return WritingMode();
}

void LocalAccessible::DOMNodeID(nsString& aID) const {
  aID.Truncate();
  if (mContent) {
    if (nsAtom* id = mContent->GetID()) {
      id->ToString(aID);
    }
  }
}

void LocalAccessible::DOMNodeClass(nsString& aClass) const {
  aClass.Truncate();
  if (auto* el = dom::Element::FromNodeOrNull(mContent)) {
    el->GetClassName(aClass);
  }
}

int32_t LocalAccessible::HeadingLevel() const {
  if (auto* el = dom::HTMLHeadingElement::FromNodeOrNull(mContent)) {
    return static_cast<int32_t>(el->ComputedLevel());
  }
  return 0;
}

void LocalAccessible::LiveRegionAttributes(nsAString* aLive,
                                           nsAString* aRelevant,
                                           Maybe<bool>* aAtomic,
                                           nsAString* aBusy) const {
  dom::Element* el = Elm();
  if (!el) {
    return;
  }
  if (aLive) {
    nsAccUtils::GetARIAAttr(el, nsGkAtoms::aria_live, *aLive);
  }
  if (aRelevant) {
    nsAccUtils::GetARIAAttr(el, nsGkAtoms::aria_relevant, *aRelevant);
  }
  if (aAtomic) {
    // XXX We ignore aria-atomic="false", but this probably doesn't conform to
    // the spec.
    if (nsAccUtils::ARIAAttrValueIs(el, nsGkAtoms::aria_atomic,
                                    nsGkAtoms::_true, eCaseMatters)) {
      *aAtomic = Some(true);
    }
  }
  if (aBusy) {
    nsAccUtils::GetARIAAttr(el, nsGkAtoms::aria_busy, *aBusy);
  }
}

Maybe<bool> LocalAccessible::ARIASelected() const {
  if (dom::Element* el = Elm()) {
    nsStaticAtom* atom =
        nsAccUtils::NormalizeARIAToken(el, nsGkAtoms::aria_selected);
    if (atom == nsGkAtoms::_true) {
      return Some(true);
    }
    if (atom == nsGkAtoms::_false) {
      return Some(false);
    }
  }
  return Nothing();
}

void LocalAccessible::StaticAsserts() const {
  static_assert(
      eLastStateFlag <= (1 << kStateFlagsBits) - 1,
      "LocalAccessible::mStateFlags was oversized by eLastStateFlag!");
  static_assert(
      eLastContextFlag <= (1 << kContextFlagsBits) - 1,
      "LocalAccessible::mContextFlags was oversized by eLastContextFlag!");
}

TableAccessible* LocalAccessible::AsTable() {
  if (IsTable() && !IsCustomTable()) {
    return CachedTableAccessible::GetFrom(this);
  }
  return nullptr;
}

TableCellAccessible* LocalAccessible::AsTableCell() {
  if (IsTableCell()) {
    return CachedTableCellAccessible::GetFrom(this);
  }
  return nullptr;
}

Maybe<int32_t> LocalAccessible::GetIntARIAAttr(nsAtom* aAttrName) const {
  if (mContent) {
    int32_t val;
    if (nsCoreUtils::GetUIntAttr(mContent, aAttrName, &val)) {
      return Some(val);
    }
    // XXX Handle attributes that allow -1; e.g. aria-row/colcount.
  }
  return Nothing();
}

bool LocalAccessible::GetStringARIAAttr(nsAtom* aAttrName,
                                        nsAString& aAttrValue) const {
  if (dom::Element* elm = Elm()) {
    return nsAccUtils::GetARIAAttr(elm, aAttrName, aAttrValue);
  }
  return false;
}

bool LocalAccessible::ARIAAttrValueIs(nsAtom* aAttrName,
                                      nsAtom* aAttrValue) const {
  if (dom::Element* elm = Elm()) {
    return nsAccUtils::ARIAAttrValueIs(elm, aAttrName, aAttrValue,
                                       eCaseMatters);
  }
  return false;
}

bool LocalAccessible::HasARIAAttr(nsAtom* aAttrName) const {
  return mContent ? nsAccUtils::HasDefinedARIAToken(mContent, aAttrName)
                  : false;
}

bool LocalAccessible::HasCustomActions() const {
  dom::Element* el = Elm();
  return el && nsAccUtils::HasARIAAttr(el, nsGkAtoms::aria_actions);
}
