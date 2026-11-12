/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Data from presentational HTML attributes and style attributes */

#include "mozilla/AttributeStyles.h"

#include "mozilla/DeclarationBlock.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "nsHashKeys.h"
#include "nsLayoutUtils.h"

using namespace mozilla::dom;

namespace mozilla {

// -----------------------------------------------------------

AttributeStyles::AttributeStyles(Document* aDocument) : mDocument(aDocument) {
  MOZ_ASSERT(aDocument);
}

void AttributeStyles::SetOwningDocument(Document* aDocument) {
  mDocument = aDocument;  // not refcounted
}

void AttributeStyles::Reset() {
  mLinkColor.reset();
  mVisitedLinkColor.reset();
  mActiveLinkColor.reset();
}

void AttributeStyles::ImplLinkColorSetter(Maybe<nscolor>& aSlot,
                                          nscolor aValue) {
  MOZ_ASSERT(!ServoStyleSet::IsInServoTraversal());
  if (aSlot && *aSlot == aValue) {
    return;
  }

  aSlot = Some(aValue);

  // Now make sure we restyle any links that might need it.  This
  // shouldn't happen often, so just rebuilding everything is ok.
  if (Element* root = mDocument->GetRootElement()) {
    nsLayoutUtils::PostRestyleEvent(root, RestyleHint::RecascadeSubtree(),
                                    nsChangeHint(0));
  }
}

void AttributeStyles::SetLinkColor(nscolor aColor) {
  return ImplLinkColorSetter(mLinkColor, aColor);
}

void AttributeStyles::SetActiveLinkColor(nscolor aColor) {
  return ImplLinkColorSetter(mActiveLinkColor, aColor);
}

void AttributeStyles::SetVisitedLinkColor(nscolor aColor) {
  return ImplLinkColorSetter(mVisitedLinkColor, aColor);
}

size_t AttributeStyles::DOMSizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mCachedStyleAttrs;
  //
  // The following members are not measured:
  // - mDocument, because it's non-owning
  return n;
}

AttributeStyles::~AttributeStyles() {
  // We may go away before all of our cached style attributes do, so clean up
  // any that are left.
  for (auto iter = mCachedStyleAttrs.Iter(); !iter.Done(); iter.Next()) {
    MiscContainer*& value = iter.Data();

    // Ideally we'd just call MiscContainer::Evict, but we can't do that since
    // we're iterating the hashtable.
    if (value->mType == nsAttrValue::eCSSDeclaration) {
      DeclarationBlock* declaration = value->mValue.mCSSDeclaration;
      declaration->SetAttributeStyles(nullptr);
    } else {
      MOZ_ASSERT_UNREACHABLE("unexpected cached nsAttrValue type");
    }

    value->mValue.mCached = 0;
    iter.Remove();
  }
}

}  // namespace mozilla
