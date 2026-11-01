/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLImageMapAccessible.h"

#include "ARIAMap.h"
#include "EventTree.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/dom/HTMLAreaElement.h"
#include "nsCoreUtils.h"
#include "nsIFrame.h"
#include "nsImageFrame.h"
#include "nsImageMap.h"
#include "nsLayoutUtils.h"

using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// HTMLImageMapAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLImageMapAccessible::HTMLImageMapAccessible(nsIContent* aContent,
                                               DocAccessible* aDoc)
    : ImageAccessible(aContent, aDoc) {
  mType = eImageMapType;

  UpdateChildAreas(false);
}

////////////////////////////////////////////////////////////////////////////////
// HTMLImageMapAccessible: LocalAccessible public

role HTMLImageMapAccessible::NativeRole() const { return roles::IMAGE_MAP; }

////////////////////////////////////////////////////////////////////////////////
// HTMLImageMapAccessible: public

void HTMLImageMapAccessible::UpdateChildAreas(bool aDoFireEvents) {
  if (!mContent || !mContent->GetPrimaryFrame()) {
    return;
  }
  nsImageFrame* imageFrame = do_QueryFrame(mContent->GetPrimaryFrame());

  // If image map is not initialized yet then we trigger one time more later.
  nsImageMap* imageMapObj = imageFrame->GetExistingImageMap();
  if (!imageMapObj) return;

  TreeMutation mt(this, TreeMutation::kNoEvents & !aDoFireEvents);

  // Remove areas that are not a valid part of the image map anymore.
  for (int32_t childIdx = mChildren.Length() - 1; childIdx >= 0; childIdx--) {
    LocalAccessible* area = mChildren.ElementAt(childIdx);
    if (nsLayoutUtils::GetPrimaryOrAreaImageFrame(area->GetContent())) {
      continue;
    }

    mt.BeforeRemoval(area);
    RemoveChild(area);
  }

  // Insert new areas into the tree.
  uint32_t areaElmCount = imageMapObj->AreaCount();
  for (uint32_t idx = 0; idx < areaElmCount; idx++) {
    nsIContent* areaContent = imageMapObj->GetAreaAt(idx);
    LocalAccessible* area = mChildren.SafeElementAt(idx);
    if (!area || area->GetContent() != areaContent) {
      auto area = MakeRefPtr<HTMLAreaAccessible>(areaContent, mDoc);
      mDoc->BindToDocument(area, aria::GetRoleMap(areaContent->AsElement()));

      if (!InsertChildAt(idx, area)) {
        mDoc->UnbindFromDocument(area);
        break;
      }

      mt.AfterInsertion(area);
    }
  }

  mt.Done();
}

LocalAccessible* HTMLImageMapAccessible::GetChildAccessibleFor(
    const nsINode* aNode) const {
  uint32_t length = mChildren.Length();
  for (uint32_t i = 0; i < length; i++) {
    LocalAccessible* area = mChildren[i];
    if (area->GetContent() == aNode) return area;
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLAreaAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLAreaAccessible::HTMLAreaAccessible(nsIContent* aContent,
                                       DocAccessible* aDoc)
    : HTMLLinkAccessible(aContent, aDoc) {
  // Make HTML area DOM element not accessible. HTML image map accessible
  // manages its tree itself.
  mStateFlags |= eNotNodeMapEntry;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLAreaAccessible: LocalAccessible

role HTMLAreaAccessible::NativeRole() const {
  // A link element without an href attribute and without a click listener
  // should be reported as a generic.
  if (mContent->IsElement()) {
    dom::Element* element = mContent->AsElement();
    if (!element->HasAttr(nsGkAtoms::href) &&
        !nsCoreUtils::HasClickListener(element)) {
      return roles::TEXT;
    }
  }
  return HTMLLinkAccessible::NativeRole();
}

ENameValueFlag HTMLAreaAccessible::NativeName(nsString& aName) const {
  ENameValueFlag nameFlag = LocalAccessible::NativeName(aName);
  if (!aName.IsEmpty()) return nameFlag;

  mContent->AsElement()->GetAttr(nsGkAtoms::alt, aName);
  return eNameOK;
}

EDescriptionValueFlag HTMLAreaAccessible::Description(
    nsString& aDescription) const {
  aDescription.Truncate();

  // Still to do - follow IE's standard here
  RefPtr<dom::HTMLAreaElement> area =
      dom::HTMLAreaElement::FromNodeOrNull(mContent);
  if (area) area->GetShape(aDescription);

  return eDescriptionOK;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLAreaAccessible: LocalAccessible public

LocalAccessible* HTMLAreaAccessible::LocalChildAtPoint(
    int32_t /*aX*/, int32_t /*aY*/, EWhichChildAtPoint /*aWhichChild*/) {
  // Don't walk into area accessibles.
  return this;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLImageMapAccessible: HyperLinkAccessible

uint32_t HTMLAreaAccessible::StartOffset() {
  // Image map accessible is not hypertext accessible therefore
  // StartOffset/EndOffset implementations of LocalAccessible doesn't work here.
  // We return index in parent because image map contains area links only which
  // are embedded objects.
  // XXX: image map should be a hypertext accessible.
  return IndexInParent();
}

uint32_t HTMLAreaAccessible::EndOffset() { return IndexInParent() + 1; }

nsRect HTMLAreaAccessible::RelativeBounds(nsIFrame** aBoundingFrame) const {
  nsIFrame* frame = GetFrame();
  if (!frame) return nsRect();

  nsImageFrame* imageFrame = do_QueryFrame(frame);
  nsImageMap* map = imageFrame->GetImageMap();

  nsRect bounds;
  nsresult rv = map->GetBoundsForAreaContent(mContent, bounds);

  if (NS_FAILED(rv)) return nsRect();

  // XXX Areas are screwy; they return their rects as a pair of points, one pair
  // stored into the width and height.
  *aBoundingFrame = frame;
  bounds.SizeTo(bounds.Width() - bounds.X(), bounds.Height() - bounds.Y());
  return bounds;
}

nsRect HTMLAreaAccessible::ParentRelativeBounds() {
  nsIFrame* boundingFrame = nullptr;
  nsRect relativeBoundsRect = RelativeBounds(&boundingFrame);
  if (MOZ_UNLIKELY(!boundingFrame)) {
    // Area is not attached to an image map?
    return nsRect();
  }

  // The relative bounds returned above are relative to this area's
  // image map, which is technically already "parent relative".
  // Because area elements are `display:none` to layout, they can't
  // have transforms or other styling applied directly, and so we
  // don't apply any additional transforms here. Any transform
  // at the image map layer will be taken care of when computing bounds
  // in the parent process.
  return relativeBoundsRect;
}
