/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteAccessible.h"

#include "ARIAMap.h"
#include "CachedTableAccessible.h"
#include "Pivot.h"
#include "Relation.h"
#include "TextLeafRange.h"
#include "mozilla/a11y/CacheConstants.h"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/a11y/DocManager.h"
#include "mozilla/a11y/Platform.h"
#include "mozilla/a11y/RelationType.h"
#include "mozilla/a11y/TableAccessible.h"
#include "mozilla/a11y/TableCellAccessible.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/gfx/Matrix.h"
#include "nsAccUtils.h"
#include "nsAccessibilityService.h"
#include "nsFocusManager.h"
#include "nsTextEquivUtils.h"
#include "xpcAccessibleDocument.h"

#ifdef A11Y_LOG
#  include "Logging.h"
#  define VERIFY_CACHE(domain)                                 \
    if (logging::IsEnabled(logging::eCache)) {                 \
      (void)mDoc->SendVerifyCache(mID, domain, mCachedFields); \
    }
#else
#  define VERIFY_CACHE(domain) \
    do {                       \
    } while (0)

#endif

namespace mozilla {
namespace a11y {

// Domain sets we need commonly for functions in this file.
static constexpr uint64_t kNecessaryBoundsDomains =
    CacheDomain::Bounds | CacheDomain::TransformMatrix | CacheDomain::Style |
    CacheDomain::ScrollPosition | CacheDomain::APZ;
static constexpr uint64_t kNecessaryStateDomains =
    CacheDomain::State | CacheDomain::Viewport;

void RemoteAccessible::Shutdown() {
  MOZ_DIAGNOSTIC_ASSERT(!IsDoc());
  xpcAccessibleDocument* xpcDoc =
      GetAccService()->GetCachedXPCDocument(Document());
  if (xpcDoc) {
    xpcDoc->NotifyOfShutdown(static_cast<RemoteAccessible*>(this));
  }

  if (IsTable() || IsTableRow() || IsTableCell()) {
    CachedTableAccessible::Invalidate(this);
  }

  // Remove this acc's relation map from the doc's map of
  // reverse relations. Prune forward relations associated with this
  // acc's reverse relations. This also removes the acc's map of reverse
  // rels from the mDoc's mReverseRelations.
  PruneRelationsOnShutdown();

  // XXX Ideally  this wouldn't be necessary, but it seems OuterDoc
  // accessibles can be destroyed before the doc they own.
  uint32_t childCount = mChildren.Length();
  if (!IsOuterDoc()) {
    for (uint32_t idx = 0; idx < childCount; idx++) {
      RemoteAccessible* child = mChildren[idx].get();
      // Drop our reference before recursing so the refcount assertion below
      // reflects only mDoc's reference once we get to it.
      mChildren[idx] = nullptr;
      child->Shutdown();
    }
  } else {
    if (childCount > 1) {
      MOZ_CRASH("outer doc has too many documents!");
    } else if (childCount == 1) {
      mChildren[0]->AsDoc()->Unbind();
    }
  }

  mChildren.Clear();
  // mDoc's mAccessibles entry should be the only reference left at this
  // point; every other reference (our parent's mChildren, our own children's
  // mParent) should already have been released above or by our caller. If
  // this fires, something else is unexpectedly holding a strong reference to
  // this RemoteAccessible, which will leak it below rather than destroy it.
  MOZ_DIAGNOSTIC_ASSERT(int32_t(mRefCnt) == 1,
                        "RemoteAccessible has an unexpected extra reference "
                        "at shutdown");
  ProxyDestroyed(static_cast<RemoteAccessible*>(this));
  DocAccessibleParent* doc = mDoc;
  mDoc = nullptr;
  // This drops doc's reference to this RemoteAccessible. If nothing else
  // references it (e.g. another node's mChildren), it is destroyed now.
  doc->RemoveAccessible(static_cast<RemoteAccessible*>(this));
}

void RemoteAccessible::SetChildDoc(DocAccessibleParent* aChildDoc) {
  MOZ_ASSERT(aChildDoc);
  MOZ_ASSERT(mChildren.Length() == 0);
  mChildren.AppendElement(aChildDoc);
  aChildDoc->mIndexInParent = 0;
}

void RemoteAccessible::ClearChildDoc(DocAccessibleParent* aChildDoc) {
  MOZ_ASSERT(aChildDoc);
  // This is possible if we're replacing one document with another: Doc 1
  // has not had a chance to remove itself, but was already replaced by Doc 2
  // in SetChildDoc(). This could result in two subsequent calls to
  // ClearChildDoc() even though mChildren.Length() == 1.
  MOZ_ASSERT(mChildren.Length() <= 1);
  mChildren.RemoveElement(aChildDoc);
}

uint32_t RemoteAccessible::EmbeddedChildCount() {
  size_t count = 0, kids = mChildren.Length();
  for (size_t i = 0; i < kids; i++) {
    if (mChildren[i]->IsEmbeddedObject()) {
      count++;
    }
  }

  return count;
}

int32_t RemoteAccessible::IndexOfEmbeddedChild(Accessible* aChild) {
  size_t index = 0, kids = mChildren.Length();
  for (size_t i = 0; i < kids; i++) {
    if (mChildren[i]->IsEmbeddedObject()) {
      if (mChildren[i] == aChild) {
        return index;
      }

      index++;
    }
  }

  return -1;
}

Accessible* RemoteAccessible::EmbeddedChildAt(uint32_t aChildIdx) {
  size_t index = 0, kids = mChildren.Length();
  for (size_t i = 0; i < kids; i++) {
    if (!mChildren[i]->IsEmbeddedObject()) {
      continue;
    }

    if (index == aChildIdx) {
      return mChildren[i];
    }

    index++;
  }

  return nullptr;
}

LocalAccessible* RemoteAccessible::OuterDocOfRemoteBrowser() const {
  auto tab = mDoc->Manager();
  dom::Element* frame = tab->GetOwnerElement();
  NS_ASSERTION(frame, "why isn't the tab in a frame!");
  if (!frame) return nullptr;

  DocAccessible* chromeDoc = GetExistingDocAccessible(frame->OwnerDoc());

  return chromeDoc ? chromeDoc->GetAccessible(frame) : nullptr;
}

void RemoteAccessible::SetParent(RemoteAccessible* aParent) {
  MOZ_ASSERT(!IsDoc() || !AsDoc()->IsTopLevel(),
             "Top level doc should not have remote parent");
  MOZ_ASSERT(!IsDoc() || !aParent || !aParent->IsDoc(),
             "Doc can't be direct parent of another doc");
  MOZ_ASSERT(!IsDoc() || !aParent || aParent->IsOuterDoc(),
             "Doc's parent must be OuterDoc");
  mParent = aParent;
  if (!aParent) {
    mIndexInParent = -1;
  }
}

RemoteAccessible* RemoteAccessible::RemoteParent() const {
  MOZ_ASSERT(!IsDoc() || !AsDoc()->IsTopLevel() || !mParent,
             "Top level doc should not have RemoteParent");
  MOZ_ASSERT(!IsDoc() || !mParent || mParent->mDoc != mDoc,
             "Doc's parent should be in another doc");
  MOZ_ASSERT(!IsDoc() || !mParent || mParent->IsOuterDoc(),
             "Doc's parent should be in another doc");
  return mParent;
}

bool RemoteAccessible::ApplyCache(CacheUpdateType aUpdateType,
                                  AccAttributes* aFields) {
  if (!aFields) {
    MOZ_ASSERT_UNREACHABLE("ApplyCache called with aFields == null");
    return false;
  }
  if (aFields->HasAttribute(CacheKey::HyperTextOffsets) ||
      aFields->HasAttribute(CacheKey::GroupInfo)) {
    MOZ_ASSERT_UNREACHABLE("Received invalid cache key");
    return false;
  }

  const nsTArray<bool> relUpdatesNeeded = PreProcessRelations(aFields);
  if (auto maybeViewportCache =
          aFields->GetAttribute<nsTArray<uint64_t>>(CacheKey::Viewport)) {
    // Updating the viewport cache means the offscreen state of this
    // document's accessibles has changed. Update the HashSet we use for
    // checking offscreen state here.
    MOZ_ASSERT(IsDoc(),
               "Fetched the viewport cache from a non-doc accessible?");
    AsDoc()->mOnScreenAccessibles.Clear();
    for (auto id : *maybeViewportCache) {
      AsDoc()->mOnScreenAccessibles.Insert(id);
    }
#ifdef MOZ_WIDGET_COCOA
    // If our viewport cache has updated, we've seen at least one of the
    // following:
    // - bounds changed for an accessible
    // - APZ changed
    // - text bounds changed
    // - scroll position changed
    // - a transform changed
    // - the selected element in a selectable container changed
    // This means our focused acc may have moved.
    // XXX: Bug 1469779 probably wants this on windows too :)
    if (PlatformShouldTrackFocusedAccLocation()) {
      RemoteAccessible* focusedAcc = AsDoc()->GetFocusedAcc();
      // Sometimes we get a viewport update before the remote and local trees
      // are connected. If we try to proceed when that is the case, we'll be
      // unable to construct useful event data. To avoid trouble, check we can
      // reach the OuterDoc.
      if (focusedAcc && focusedAcc->OuterDocOfRemoteBrowser()) {
        LayoutDeviceIntRect bounds = focusedAcc->Bounds();
        if (Some(bounds) != AsDoc()->mFocusedAccBounds) {
          AsDoc()->mFocusedAccBounds = Some(bounds);
          PlatformFocusedAccLocationChanged(focusedAcc);
        }
      }
    }
#endif
  }

  if (aUpdateType == CacheUpdateType::Initial) {
    mCachedFields = aFields;
  } else {
    if (!mCachedFields) {
      // The fields cache can be uninitialized if there were no cache-worthy
      // fields in the initial cache push.
      // We don't do a simple assign because we don't want to store the
      // DeleteEntry entries.
      mCachedFields = new AccAttributes();
    }
    mCachedFields->Update(aFields);
  }

  if (IsText()) {
    RemoteAccessible* parent = RemoteParent();
    if (parent && parent->IsHyperText()) {
      parent->InvalidateCachedHyperTextOffsets();
    }
  }

  PostProcessRelations(relUpdatesNeeded);
  return true;
}

ENameValueFlag RemoteAccessible::Name(nsString& aName) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::NameAndDescription |
                                     CacheDomain::Text |
                                     CacheDomain::Relations) ||
      !mCachedFields) {
    aName.SetIsVoid(true);
    return eNameOK;
  }

  if (IsText()) {
    mCachedFields->GetAttribute(CacheKey::Text, aName);
    return eNameOK;
  }

  if (mCachedFields->GetAttribute(CacheKey::Name, aName)) {
    VERIFY_CACHE(CacheDomain::NameAndDescription);
    return eNameOK;
  }

  if (auto maybeAriaLabelIds = mCachedFields->GetAttribute<nsTArray<uint64_t>>(
          nsGkAtoms::aria_labelledby)) {
    RemoteAccIterator iter(*maybeAriaLabelIds, Document());
    nsTextEquivUtils::GetTextEquivFromAccIterable(this, &iter, aName);
    aName.CompressWhitespace();
  }

  if (!aName.IsEmpty()) {
    return eNameFromRelations;
  }

  if (auto accRelMapEntry = mDoc->mReverseRelations.Lookup(ID())) {
    nsTArray<uint64_t> relationCandidateIds;
    for (const auto& data : kRelationTypeAtoms) {
      if (data.mAtom != nsGkAtoms::_for || data.mValidTag != nsGkAtoms::label) {
        continue;
      }

      if (auto labelIds = accRelMapEntry.Data().Lookup(&data)) {
        RemoteAccIterator iter(*labelIds, Document());
        nsTextEquivUtils::GetTextEquivFromAccIterable(this, &iter, aName);
        aName.CompressWhitespace();
      }
    }
    aName.CompressWhitespace();
  }

  if (!aName.IsEmpty()) {
    return eNameFromRelations;
  }

  ArrayAccIterator iter(LegendsOrCaptions());
  nsTextEquivUtils::GetTextEquivFromAccIterable(this, &iter, aName);
  aName.CompressWhitespace();

  if (!aName.IsEmpty()) {
    return eNameFromRelations;
  }

  nsTextEquivUtils::GetNameFromSubtree(this, aName);
  if (!aName.IsEmpty()) {
    return eNameFromSubtree;
  }

  if (mCachedFields->GetAttribute(CacheKey::Tooltip, aName)) {
    VERIFY_CACHE(CacheDomain::NameAndDescription);
    return eNameFromTooltip;
  }

  if (mCachedFields->GetAttribute(CacheKey::CssAltContent, aName)) {
    VERIFY_CACHE(CacheDomain::NameAndDescription);
    return eNameOK;
  }

  MOZ_ASSERT(aName.IsEmpty());
  aName.SetIsVoid(true);
  return eNameOK;
}

EDescriptionValueFlag RemoteAccessible::Description(
    nsString& aDescription) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::NameAndDescription)) {
    return eDescriptionOK;
  }

  EDescriptionValueFlag descFlag = eDescriptionOK;

  if (mCachedFields) {
    auto cachedDescriptionFlag =
        mCachedFields->GetAttribute<int32_t>(CacheKey::DescriptionValueFlag);
    if (cachedDescriptionFlag) {
      descFlag = static_cast<EDescriptionValueFlag>(*cachedDescriptionFlag);
    }
    mCachedFields->GetAttribute(CacheKey::Description, aDescription);
    VERIFY_CACHE(CacheDomain::NameAndDescription);
  }

  return descFlag;
}

void RemoteAccessible::Value(nsString& aValue) const {
  if (mDoc->RequestDomainsIfInactive(
          CacheDomain::Value |    // CurValue, etc.
          CacheDomain::Actions |  // ActionAncestor (HasPrimaryAction)
          CacheDomain::State |    // GetSelectedItem
          CacheDomain::Viewport   // GetSelectedItem
          )) {
    return;
  }

  if (mCachedFields) {
    if (mCachedFields->HasAttribute(CacheKey::TextValue)) {
      mCachedFields->GetAttribute(CacheKey::TextValue, aValue);
      VERIFY_CACHE(CacheDomain::Value);
      return;
    }

    if (HasNumericValue()) {
      double checkValue = CurValue();
      if (!std::isnan(checkValue)) {
        aValue.AppendFloat(checkValue);
      }
      return;
    }

    const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
    // The value of a textbox is the text content from its subtree.
    if (IsTextField() ||
        (roleMapEntry && roleMapEntry->Is(nsGkAtoms::textbox)) ||
        (IsGeneric() && IsEditableRoot())) {
      TextLeafRange::FromAccessible(const_cast<RemoteAccessible*>(this))
          .GetFlattenedText(aValue);
      return;
    }

    if (IsCombobox()) {
      // For combo boxes, rely on selection state to determine the value.
      const Accessible* option =
          const_cast<RemoteAccessible*>(this)->GetSelectedItem(0);
      if (option) {
        option->Name(aValue);
      } else {
        // If no selected item, determine the value from descendant elements.
        nsTextEquivUtils::GetTextEquivFromSubtree(this, aValue);
      }
      return;
    }

    if (IsTextLeaf() || IsImage()) {
      if (const Accessible* actionAcc = ActionAncestor()) {
        if (const_cast<Accessible*>(actionAcc)->State() & states::LINKED) {
          // Text and image descendants of links expose the link URL as the
          // value.
          return actionAcc->Value(aValue);
        }
      }
    }
  }
}

double RemoteAccessible::CurValue() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Value)) {
    return UnspecifiedNaN<double>();
  }

  if (mCachedFields) {
    if (auto value =
            mCachedFields->GetAttribute<double>(CacheKey::NumericValue)) {
      VERIFY_CACHE(CacheDomain::Value);
      return *value;
    }
  }

  return UnspecifiedNaN<double>();
}

double RemoteAccessible::MinValue() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Value)) {
    return UnspecifiedNaN<double>();
  }

  if (mCachedFields) {
    if (auto min = mCachedFields->GetAttribute<double>(CacheKey::MinValue)) {
      VERIFY_CACHE(CacheDomain::Value);
      return *min;
    }
  }

  return UnspecifiedNaN<double>();
}

double RemoteAccessible::MaxValue() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Value)) {
    return UnspecifiedNaN<double>();
  }

  if (mCachedFields) {
    if (auto max = mCachedFields->GetAttribute<double>(CacheKey::MaxValue)) {
      VERIFY_CACHE(CacheDomain::Value);
      return *max;
    }
  }

  return UnspecifiedNaN<double>();
}

double RemoteAccessible::Step() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Value)) {
    return UnspecifiedNaN<double>();
  }

  if (mCachedFields) {
    if (auto step = mCachedFields->GetAttribute<double>(CacheKey::Step)) {
      VERIFY_CACHE(CacheDomain::Value);
      return *step;
    }
  }

  return UnspecifiedNaN<double>();
}

bool RemoteAccessible::SetCurValue(double aValue) {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Value |  // MinValue, MaxValue
                                     CacheDomain::State |  // State
                                     CacheDomain::Viewport  // State
                                     )) {
    return false;
  }

  if (!HasNumericValue() || IsProgress()) {
    return false;
  }

  const uint32_t kValueCannotChange = states::READONLY | states::UNAVAILABLE;
  if (State() & kValueCannotChange) {
    return false;
  }

  double checkValue = MinValue();
  if (!std::isnan(checkValue) && aValue < checkValue) {
    return false;
  }

  checkValue = MaxValue();
  if (!std::isnan(checkValue) && aValue > checkValue) {
    return false;
  }

  (void)mDoc->SendSetCurValue(mID, aValue);
  return true;
}

bool RemoteAccessible::ContainsPoint(int32_t aX, int32_t aY) {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::TextBounds |  // GetCachedTextLines
                            kNecessaryBoundsDomains,
                        this);

  if (!BoundsWithOffset(Nothing(), true).Contains(aX, aY)) {
    return false;
  }
  if (!IsTextLeaf()) {
    if (IsImage() || IsImageMap() || !HasChildren() ||
        RefPtr{DisplayStyle()} != nsGkAtoms::inlinevalue) {
      // This isn't an inline element that might contain text, so we don't need
      // to walk lines. It's enough that our rect contains the point.
      return true;
    }
    // Non-image inline elements with children can wrap across lines just like
    // text leaves; see below.
    // Walk the children, which will walk the lines of text in any text leaves.
    uint32_t count = ChildCount();
    for (uint32_t c = 0; c < count; ++c) {
      RemoteAccessible* child = RemoteChildAt(c);
      if (child->Role() == roles::TEXT_CONTAINER && child->IsClipped()) {
        // There is a clipped child. This is a candidate for fuzzy hit testing.
        // See RemoteAccessible::DoFuzzyHittesting.
        return true;
      }
      if (child->ContainsPoint(aX, aY)) {
        return true;
      }
    }
    // None of our descendants contain the point, so nor do we.
    return false;
  }
  // This is a text leaf. The text might wrap across lines, which means our
  // rect might cover a wider area than the actual text. For example, if the
  // text begins in the middle of the first line and wraps on to the second,
  // the rect will cover the start of the first line and the end of the second.
  auto lines = GetCachedTextLines();
  if (!lines) {
    // This means the text is empty or occupies a single line (but does not
    // begin the line). In that case, the Bounds check above is sufficient,
    // since there's only one rect.
    return true;
  }
  uint32_t length = lines->Length();
  MOZ_ASSERT(length > 0,
             "Line starts shouldn't be in cache if there aren't any");
  if (length == 0 || (length == 1 && (*lines)[0] == 0)) {
    // This means the text begins and occupies a single line. Again, the Bounds
    // check above is sufficient.
    return true;
  }
  // Walk the lines of the text. Even if this text doesn't start at the
  // beginning of a line (i.e. lines[0] > 0), we always want to consider its
  // first line.
  int32_t lineStart = 0;
  for (uint32_t index = 0; index <= length; ++index) {
    int32_t lineEnd;
    if (index < length) {
      int32_t nextLineStart = (*lines)[index];
      if (nextLineStart == 0) {
        // This Accessible starts at the beginning of a line. Here, we always
        // treat 0 as the first line start anyway.
        MOZ_ASSERT(index == 0);
        continue;
      }
      lineEnd = nextLineStart - 1;
    } else {
      // This is the last line.
      lineEnd = static_cast<int32_t>(nsAccUtils::TextLength(this)) - 1;
    }
    MOZ_ASSERT(lineEnd >= lineStart);
    nsRect lineRect = GetCachedCharRect(lineStart);
    if (lineEnd > lineStart) {
      nsRect lineEndRect = GetCachedCharRect(lineEnd);
      if (lineEndRect.IsEmpty() && lineEnd - 1 > lineStart) {
        // The line feed character at the end of a line in pre-formatted text
        // doesn't have a useful rect. Use the previous character. Otherwise,
        // lineRect won't span the line of text and we'll miss characters.
        lineEndRect = GetCachedCharRect(lineEnd - 1);
      }
      lineRect.UnionRect(lineRect, lineEndRect);
    }
    if (BoundsWithOffset(Some(lineRect), true).Contains(aX, aY)) {
      return true;
    }
    lineStart = lineEnd + 1;
  }
  return false;
}

RemoteAccessible* RemoteAccessible::DoFuzzyHittesting() {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::Bounds, this);

  uint32_t childCount = ChildCount();
  if (!childCount) {
    return nullptr;
  }
  // Check if this match has a clipped child.
  // This usually indicates invisible text, and we're
  // interested in returning the inner text content
  // even if it doesn't contain the point we're hittesting.
  RemoteAccessible* clippedContainer = nullptr;
  for (uint32_t i = 0; i < childCount; i++) {
    RemoteAccessible* child = RemoteChildAt(i);
    if (child->Role() == roles::TEXT_CONTAINER) {
      if (child->IsClipped()) {
        clippedContainer = child;
        break;
      }
    }
  }
  // If we found a clipped container, descend it in search of
  // meaningful text leaves. Ignore non-text-leaf/text-container
  // siblings.
  RemoteAccessible* container = clippedContainer;
  while (container) {
    RemoteAccessible* textLeaf = nullptr;
    bool continueSearch = false;
    childCount = container->ChildCount();
    for (uint32_t i = 0; i < childCount; i++) {
      RemoteAccessible* child = container->RemoteChildAt(i);
      if (child->Role() == roles::TEXT_CONTAINER) {
        container = child;
        continueSearch = true;
        break;
      }
      if (child->IsTextLeaf()) {
        textLeaf = child;
        // Don't break here -- it's possible a text container
        // exists as another sibling, and we should descend as
        // deep as possible.
      }
    }
    if (textLeaf) {
      return textLeaf;
    }
    if (!continueSearch) {
      // We didn't find anything useful in this set of siblings.
      // Don't keep searching
      break;
    }
  }
  return nullptr;
}

Accessible* RemoteAccessible::ChildAtPoint(
    int32_t aX, int32_t aY, LocalAccessible::EWhichChildAtPoint aWhichChild) {
  if (mDoc->RequestDomainsIfInactive(
          kNecessaryBoundsDomains |
          CacheDomain::TextBounds  // GetCachedTextLines (via ContainsPoint)
          )) {
    return nullptr;
  }

  // Elements that are partially on-screen should have their bounds masked by
  // their containing scroll area so hittesting yields results that are
  // consistent with the content's visual representation. Pass this value to
  // bounds calculation functions to indicate that we're hittesting.
  const bool hitTesting = true;

  if (IsOuterDoc() && aWhichChild == EWhichChildAtPoint::DirectChild) {
    // This is an iframe, which is as deep as the viewport cache goes. The
    // caller wants a direct child, which can only be the embedded document.
    if (BoundsWithOffset(Nothing(), hitTesting).Contains(aX, aY)) {
      return RemoteFirstChild();
    }
    return nullptr;
  }

  RemoteAccessible* lastMatch = nullptr;
  // If `this` is a document, use its viewport cache instead of
  // the cache of its parent document.
  if (DocAccessibleParent* doc = IsDoc() ? AsDoc() : mDoc) {
    if (!doc->mCachedFields) {
      // A client call might arrive after we've constructed doc but before we
      // get a cache push for it.
      return nullptr;
    }
    if (auto maybeViewportCache =
            doc->mCachedFields->GetAttribute<nsTArray<uint64_t>>(
                CacheKey::Viewport)) {
      // The retrieved viewport cache contains acc IDs in hittesting order.
      // That is, items earlier in the list have z-indexes that are larger than
      // those later in the list. If you were to build a tree by z-index, where
      // chilren have larger z indices than their parents, iterating this list
      // is essentially a postorder tree traversal.
      const nsTArray<uint64_t>& viewportCache = *maybeViewportCache;

      for (auto id : viewportCache) {
        RemoteAccessible* acc = doc->GetAccessible(id);
        if (!acc) {
          // This can happen if the acc died in between
          // pushing the viewport cache and doing this hittest
          continue;
        }

        if (acc->IsOuterDoc() &&
            aWhichChild == EWhichChildAtPoint::DeepestChild &&
            acc->BoundsWithOffset(Nothing(), hitTesting).Contains(aX, aY)) {
          // acc is an iframe, which is as deep as the viewport cache goes. This
          // iframe contains the requested point.
          RemoteAccessible* innerDoc = acc->RemoteFirstChild();
          if (innerDoc) {
            MOZ_ASSERT(innerDoc->IsDoc());
            // Search the embedded document's viewport cache so we return the
            // deepest descendant in that embedded document.
            Accessible* deepestAcc = innerDoc->ChildAtPoint(
                aX, aY, EWhichChildAtPoint::DeepestChild);
            MOZ_ASSERT(!deepestAcc || deepestAcc->IsRemote());
            lastMatch = deepestAcc ? deepestAcc->AsRemote() : nullptr;
            break;
          }
          // If there is no embedded document, the iframe itself is the deepest
          // descendant.
          lastMatch = acc;
          break;
        }

        if (acc == this) {
          MOZ_ASSERT(!acc->IsOuterDoc());
          // Even though we're searching from the doc's cache
          // this call shouldn't pass the boundary defined by
          // the acc this call originated on. If we hit `this`,
          // return our most recent match.
          if (!lastMatch &&
              BoundsWithOffset(Nothing(), hitTesting).Contains(aX, aY)) {
            // If we haven't found a match, but `this` contains the point we're
            // looking for, set it as our temp last match so we can
            // (potentially) do fuzzy hittesting on it below.
            lastMatch = acc;
          }
          break;
        }

        if (acc->ContainsPoint(aX, aY)) {
          // Because our rects are in hittesting order, the
          // first match we encounter is guaranteed to be the
          // deepest match.
          lastMatch = acc;
          break;
        }
      }
      if (lastMatch) {
        RemoteAccessible* fuzzyMatch = lastMatch->DoFuzzyHittesting();
        lastMatch = fuzzyMatch ? fuzzyMatch : lastMatch;
      }
    }
  }

  if (aWhichChild == EWhichChildAtPoint::DirectChild && lastMatch) {
    // lastMatch is the deepest match. Walk up to the direct child of this.
    RemoteAccessible* parent = lastMatch->RemoteParent();
    for (;;) {
      if (parent == this) {
        break;
      }
      if (!parent || parent->IsDoc()) {
        // `this` is not an ancestor of lastMatch. Ignore lastMatch.
        lastMatch = nullptr;
        break;
      }
      lastMatch = parent;
      parent = parent->RemoteParent();
    }
  } else if (aWhichChild == EWhichChildAtPoint::DeepestChild && lastMatch &&
             !IsDoc() && !IsAncestorOf(lastMatch)) {
    // If we end up with a match that is not in the ancestor chain
    // of the accessible this call originated on, we should ignore it.
    // This can happen when the aX, aY given are outside `this`.
    lastMatch = nullptr;
  }

  if (!lastMatch && BoundsWithOffset(Nothing(), hitTesting).Contains(aX, aY)) {
    // Even though the hit target isn't inside `this`, the point is still
    // within our bounds, so fall back to `this`.
    return this;
  }

  return lastMatch;
}

Maybe<nsRect> RemoteAccessible::RetrieveCachedBounds() const {
  if (!mCachedFields) {
    return Nothing();
  }

  ASSERT_DOMAINS_ACTIVE(CacheDomain::Bounds, this);
  Maybe<const UniquePtr<nsRect>&> maybeRect =
      mCachedFields->GetAttribute<UniquePtr<nsRect>>(
          CacheKey::ParentRelativeBounds);
  if (maybeRect) {
    return Some(*(*maybeRect));
  }

  return Nothing();
}

void RemoteAccessible::ApplyCrossDocOffset(nsRect& aBounds) const {
  if (!IsDoc()) {
    // We should only apply cross-doc offsets to documents. If we're anything
    // else, return early here.
    return;
  }

  RemoteAccessible* parentAcc = RemoteParent();
  if (!parentAcc || !parentAcc->IsOuterDoc()) {
    return;
  }

  ASSERT_DOMAINS_ACTIVE(CacheDomain::Bounds, this);
  Maybe<const nsTArray<int32_t>&> maybeOffset =
      parentAcc->mCachedFields->GetAttribute<nsTArray<int32_t>>(
          CacheKey::CrossDocOffset);
  if (!maybeOffset) {
    return;
  }

  MOZ_ASSERT(maybeOffset->Length() == 2);
  const nsTArray<int32_t>& offset = *maybeOffset;
  // Our retrieved value is in app units, so we don't need to do any
  // unit conversion here.
  aBounds.MoveBy(offset[0], offset[1]);
}

bool RemoteAccessible::ApplyTransform(nsRect& aCumulativeBounds) const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::TransformMatrix, this);

  // First, attempt to retrieve the transform from the cache.
  Maybe<const UniquePtr<gfx::Matrix4x4>&> maybeTransform =
      mCachedFields->GetAttribute<UniquePtr<gfx::Matrix4x4>>(
          CacheKey::TransformMatrix);
  if (!maybeTransform) {
    return false;
  }

  auto mtxInPixels = gfx::Matrix4x4Typed<CSSPixel, CSSPixel>::FromUnknownMatrix(
      *(*maybeTransform));

  // Our matrix is in CSS Pixels, so we need our rect to be in CSS
  // Pixels too. Convert before applying.
  auto boundsInPixels = CSSRect::FromAppUnits(aCumulativeBounds);
  boundsInPixels = mtxInPixels.TransformBounds(boundsInPixels);
  aCumulativeBounds = CSSRect::ToAppUnits(boundsInPixels);

  return true;
}

bool RemoteAccessible::ApplyScrollOffset(nsRect& aBounds,
                                         float aResolution) const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::ScrollPosition, this);
  Maybe<const nsTArray<int32_t>&> maybeScrollPosition =
      mCachedFields->GetAttribute<nsTArray<int32_t>>(CacheKey::ScrollPosition);

  if (!maybeScrollPosition || maybeScrollPosition->Length() != 2) {
    return false;
  }
  // Our retrieved value is in app units, so we don't need to do any
  // unit conversion here.
  const nsTArray<int32_t>& scrollPosition = *maybeScrollPosition;

  // Scroll position is an inverse representation of scroll offset (since the
  // further the scroll bar moves down the page, the further the page content
  // moves up/closer to the origin).
  nsPoint scrollOffset(-scrollPosition[0], -scrollPosition[1]);

  aBounds.MoveBy(scrollOffset.x * aResolution * aResolution,
                 scrollOffset.y * aResolution * aResolution);

  // Return true here even if the scroll offset was 0,0 because the RV is used
  // as a scroll container indicator. Non-scroll containers won't have cached
  // scroll position.
  return true;
}

void RemoteAccessible::ApplyVisualViewportOffset(nsRect& aBounds) const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::APZ, this);
  MOZ_ASSERT(IsDoc(), "Attempting to get visual viewport data from non-doc?");
  Maybe<const nsTArray<int32_t>&> maybeViewportOffset =
      mCachedFields->GetAttribute<nsTArray<int32_t>>(
          CacheKey::VisualViewportOffset);

  if (!maybeViewportOffset || maybeViewportOffset->Length() != 2) {
    return;
  }
  // Our retrieved value is in app units, so we don't need to do any
  // unit conversion here.
  const nsTArray<int32_t>& viewportOffset = *maybeViewportOffset;

  // Like scroll position, this offset is an inverse representation: the
  // further the visual viewport moves, the further the page content
  // moves up/closer to the origin
  aBounds.MoveBy(-viewportOffset[0], -viewportOffset[1]);
}

nsRect RemoteAccessible::BoundsInAppUnits() const {
  if (mDoc->RequestDomainsIfInactive(kNecessaryBoundsDomains)) {
    return {};
  }
  if (dom::CanonicalBrowsingContext* cbc = mDoc->GetBrowsingContext()->Top()) {
    if (dom::BrowserParent* bp = cbc->GetBrowserParent()) {
      DocAccessibleParent* topDoc = bp->GetTopLevelDocAccessible();
      if (topDoc && topDoc->mCachedFields) {
        auto appUnitsPerDevPixel = topDoc->mCachedFields->GetAttribute<int32_t>(
            CacheKey::AppUnitsPerDevPixel);
        MOZ_ASSERT(appUnitsPerDevPixel);
        return LayoutDeviceIntRect::ToAppUnits(Bounds(), *appUnitsPerDevPixel);
      }
    }
  }
  return LayoutDeviceIntRect::ToAppUnits(Bounds(), AppUnitsPerCSSPixel());
}

bool RemoteAccessible::IsFixedPos() const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::Style, this);
  MOZ_ASSERT(mCachedFields);
  if (auto maybePosition =
          mCachedFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::CssPosition)) {
    return *maybePosition == nsGkAtoms::fixed;
  }

  return false;
}

bool RemoteAccessible::IsOverflowHidden() const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::Style, this);
  MOZ_ASSERT(mCachedFields);
  if (auto maybeOverflow =
          mCachedFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::CSSOverflow)) {
    return *maybeOverflow == nsGkAtoms::hidden;
  }

  return false;
}

bool RemoteAccessible::IsClipped() const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::Bounds, this);
  MOZ_ASSERT(mCachedFields);
  if (mCachedFields->GetAttribute<bool>(CacheKey::IsClipped)) {
    return true;
  }

  return false;
}

LayoutDeviceIntRect RemoteAccessible::ComputeBoundsFromContent() const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::Style, this);
  MOZ_ASSERT(mCachedFields);

  LayoutDeviceIntRect result;
  for (uint32_t i = 0, n = ChildCount(); i < n; i++) {
    if (Accessible* child = ChildAt(i); child && child->IsRemote()) {
      result =
          result.Union(child->AsRemote()->BoundsWithOffset(Nothing(), false));
    }
  }
  return result;
}

LayoutDeviceIntRect RemoteAccessible::BoundsWithOffset(
    Maybe<nsRect> aOffset, bool aBoundsAreForHittesting) const {
  if (mDoc->RequestDomainsIfInactive(kNecessaryBoundsDomains)) {
    return LayoutDeviceIntRect{};
  }

  Maybe<nsRect> maybeBounds = RetrieveCachedBounds();
  if (maybeBounds) {
    if (maybeBounds->IsEmpty() && aOffset.isNothing()) {
      RefPtr<nsAtom> display = DisplayStyle();
      RefPtr<nsAtom> contentsAtom = NS_Atomize("contents");
      if (display == contentsAtom) {
        // If we have a display:contents acc, we'll end up
        // with a 0x0 rect from our normal bounds computation below.
        // Try to compute bounds from child accs instead.
        return ComputeBoundsFromContent();
      }
    }

    nsRect bounds = *maybeBounds;
    // maybeBounds is parent-relative. However, the transform matrix we cache
    // (if any) is meant to operate on self-relative rects. Therefore, make
    // bounds self-relative until after we transform.
    bounds.MoveTo(0, 0);
    const DocAccessibleParent* topDoc = IsDoc() ? AsDoc() : nullptr;

    if (aOffset.isSome()) {
      // The rect we've passed in is in app units, so no conversion needed.
      const nsRect& internalRect = *aOffset;
      bounds.SetRectX(bounds.x + internalRect.x, internalRect.width);
      bounds.SetRectY(bounds.y + internalRect.y, internalRect.height);
    }

    (void)ApplyTransform(bounds);
    // Now apply the parent-relative offset.
    bounds.MoveBy(maybeBounds->TopLeft());

    ApplyCrossDocOffset(bounds);

    Maybe<float> res =
        mDoc->mCachedFields->GetAttribute<float>(CacheKey::Resolution);
    MOZ_ASSERT(res, "No cached document resolution found.");
    const float resolution = res.valueOr(1.0f);

    LayoutDeviceIntRect devPxBounds;
    const Accessible* acc = Parent();
    bool encounteredFixedContainer = IsFixedPos();
    while (acc && acc->IsRemote()) {
      // Return early if we're hit testing and our cumulative bounds are empty,
      // since walking the ancestor chain won't produce any hits.
      if (aBoundsAreForHittesting && bounds.IsEmpty()) {
        return LayoutDeviceIntRect{};
      }

      RemoteAccessible* remoteAcc = const_cast<Accessible*>(acc)->AsRemote();

      if (Maybe<nsRect> maybeRemoteBounds = remoteAcc->RetrieveCachedBounds()) {
        nsRect remoteBounds = *maybeRemoteBounds;
        // We need to take into account a non-1 resolution set on the
        // presshell. This happens with async pinch zooming, among other
        // things. We can't reliably query this value in the parent process,
        // so we retrieve it from the document's cache.
        if (remoteAcc->IsDoc()) {
          // Apply our visual viewport offset, which is non-zero when
          // pinch zoom has been applied. Do this before we scale by
          // resolution as this offset is unscaled.
          remoteAcc->ApplyVisualViewportOffset(bounds);
          // Apply the document's resolution to the bounds we've gathered
          // thus far. We do this before applying the document's offset
          // because document accs should not have their bounds scaled by
          // their own resolution. They should be scaled by the resolution
          // of their containing document (if any).
          Maybe<float> res =
              remoteAcc->AsDoc()->mCachedFields->GetAttribute<float>(
                  CacheKey::Resolution);
          MOZ_ASSERT(res, "No cached document resolution found.");
          bounds.ScaleRoundOut(res.valueOr(1.0f));

          topDoc = remoteAcc->AsDoc();
        }

        // We don't account for the document offset of iframes when
        // computing parent-relative bounds. Instead, we store this value
        // separately on all iframes and apply it here. See the comments in
        // LocalAccessible::BundleFieldsForCache where we set the
        // nsGkAtoms::crossorigin attribute.
        remoteAcc->ApplyCrossDocOffset(remoteBounds);
        if (!encounteredFixedContainer) {
          // Apply scroll offset, if applicable. Only the contents of an
          // element are affected by its scroll offset, which is why this call
          // happens in this loop instead of both inside and outside of
          // the loop (like ApplyTransform).
          // Never apply scroll offsets past a fixed container.
          const bool hasScrollArea =
              remoteAcc->ApplyScrollOffset(bounds, resolution);

          // If we are hit testing and the Accessible has a scroll area, ensure
          // that the bounds we've calculated so far are constrained to the
          // bounds of the scroll area. Without this, we'll "hit" the off-screen
          // portions of accs that are are partially (but not fully) within the
          // scroll area. This is also a problem for accs with overflow:hidden;
          if (aBoundsAreForHittesting &&
              (hasScrollArea || remoteAcc->IsOverflowHidden())) {
            nsRect selfRelativeVisibleBounds(0, 0, remoteBounds.width,
                                             remoteBounds.height);
            bounds = bounds.SafeIntersect(selfRelativeVisibleBounds);
          }
        }
        if (remoteAcc->IsDoc()) {
          // Fixed elements are document relative, so if we've hit a
          // document we're now subject to that document's styling
          // (including scroll offsets that operate on it).
          // This ordering is important, we don't want to apply scroll
          // offsets on this doc's content.
          encounteredFixedContainer = false;
        }
        if (!encounteredFixedContainer) {
          // The transform matrix we cache (if any) is meant to operate on
          // self-relative rects. Therefore, we must apply the transform before
          // we make bounds parent-relative.
          (void)remoteAcc->ApplyTransform(bounds);
          // Regardless of whether this is a doc, we should offset `bounds`
          // by the bounds retrieved here. This is how we build screen
          // coordinates from relative coordinates.
          bounds.MoveBy(remoteBounds.X(), remoteBounds.Y());
        }

        if (remoteAcc->IsFixedPos()) {
          encounteredFixedContainer = true;
        }
        // we can't just break here if we're scroll suppressed because we still
        // need to find the top doc.
      }
      acc = acc->Parent();
    }

    MOZ_ASSERT(topDoc);
    if (topDoc) {
      // We use the top documents app-units-per-dev-pixel even though
      // theoretically nested docs can have different values. Practically,
      // that isn't likely since we only offer zoom controls for the top
      // document and all subdocuments inherit from it.
      auto appUnitsPerDevPixel = topDoc->mCachedFields->GetAttribute<int32_t>(
          CacheKey::AppUnitsPerDevPixel);
      MOZ_ASSERT(appUnitsPerDevPixel);
      if (appUnitsPerDevPixel) {
        // Convert our existing `bounds` rect from app units to dev pixels
        devPxBounds = LayoutDeviceIntRect::FromAppUnitsToNearest(
            bounds, *appUnitsPerDevPixel);
      }
    }

#if !defined(ANDROID)
    // This block is not thread safe because it queries a LocalAccessible.
    // It is also not needed in Android since the only local accessible is
    // the outer doc browser that has an offset of 0.
    // acc could be null if the OuterDocAccessible died before the top level
    // DocAccessibleParent.
    if (LocalAccessible* localAcc =
            acc ? const_cast<Accessible*>(acc)->AsLocal() : nullptr) {
      // LocalAccessible::Bounds returns screen-relative bounds in
      // dev pixels.
      LayoutDeviceIntRect localBounds = localAcc->Bounds();

      // The root document will always have an APZ resolution of 1,
      // so we don't factor in its scale here. We also don't scale
      // by GetFullZoom because LocalAccessible::Bounds already does
      // that.
      devPxBounds.MoveBy(localBounds.X(), localBounds.Y());
    }
#endif

    return devPxBounds;
  }

  return LayoutDeviceIntRect();
}

LayoutDeviceIntRect RemoteAccessible::Bounds() const {
  if (mDoc->RequestDomainsIfInactive(kNecessaryBoundsDomains)) {
    return {};
  }
  return BoundsWithOffset(Nothing());
}

Relation RemoteAccessible::RelationByType(RelationType aType) const {
  if (mDoc->RequestDomainsIfInactive(
          CacheDomain::Relations |          // relations info, DOMName attribute
          CacheDomain::Value |              // Value
          CacheDomain::DOMNodeIDAndClass |  // DOMNodeID
          CacheDomain::GroupInfo            // GetOrCreateGroupInfo
          )) {
    return Relation();
  }

  // We are able to handle some relations completely in the
  // parent process, without the help of the cache. Those
  // relations are enumerated here. Other relations, whose
  // types are stored in kRelationTypeAtoms, are processed
  // below using the cache.
  if (aType == RelationType::CONTAINING_TAB_PANE) {
    if (dom::CanonicalBrowsingContext* cbc = mDoc->GetBrowsingContext()) {
      if (dom::CanonicalBrowsingContext* topCbc = cbc->Top()) {
        if (dom::BrowserParent* bp = topCbc->GetBrowserParent()) {
          return Relation(bp->GetTopLevelDocAccessible());
        }
      }
    }
    return Relation();
  }

  if (aType == RelationType::LINKS_TO && Role() == roles::LINK) {
    Pivot p = Pivot(mDoc);
    nsString href;
    Value(href);
    int32_t i = href.FindChar('#');
    int32_t len = static_cast<int32_t>(href.Length());
    if (i != -1 && i < (len - 1)) {
      nsDependentSubstring anchorName = Substring(href, i + 1, len);
      MustPruneSameDocRule rule;
      Accessible* nameMatch = nullptr;
      for (Accessible* match = p.Next(mDoc, rule); match;
           match = p.Next(match, rule)) {
        nsString currID;
        match->DOMNodeID(currID);
        MOZ_ASSERT(match->IsRemote());
        if (anchorName.Equals(currID)) {
          return Relation(match->AsRemote());
        }
        if (!nameMatch) {
          nsString currName = match->AsRemote()->GetCachedHTMLNameAttribute();
          if (match->TagName() == nsGkAtoms::a && anchorName.Equals(currName)) {
            // If we find an element with a matching ID, we should return
            // that, but if we don't we should return the first anchor with
            // a matching name. To avoid doing two traversals, store the first
            // name match here.
            nameMatch = match;
          }
        }
      }
      return nameMatch ? Relation(nameMatch->AsRemote()) : Relation();
    }

    return Relation();
  }

  // Handle ARIA tree, treegrid parent/child relations. Each of these cases
  // relies on cached group info. To find the parent of an accessible, use the
  // unified conceptual parent.
  if (aType == RelationType::NODE_CHILD_OF) {
    const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
    if (roleMapEntry && (roleMapEntry->role == roles::OUTLINEITEM ||
                         roleMapEntry->role == roles::LISTITEM ||
                         roleMapEntry->role == roles::ROW)) {
      if (const AccGroupInfo* groupInfo =
              const_cast<RemoteAccessible*>(this)->GetOrCreateGroupInfo()) {
        return Relation(groupInfo->ConceptualParent());
      }
    }
    return Relation();
  }

  // To find the children of a parent, provide an iterator through its items.
  if (aType == RelationType::NODE_PARENT_OF) {
    const nsRoleMapEntry* roleMapEntry = ARIARoleMap();
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

  if (aType == RelationType::MEMBER_OF) {
    Relation rel = Relation();
    // HTML radio buttons with cached names should be grouped.
    if (IsHTMLRadioButton()) {
      nsString name = GetCachedHTMLNameAttribute();
      if (name.IsEmpty()) {
        return rel;
      }

      RemoteAccessible* ancestor = RemoteParent();
      while (ancestor && ancestor->Role() != roles::FORM && ancestor != mDoc) {
        ancestor = ancestor->RemoteParent();
      }
      if (ancestor) {
        // Sometimes we end up with an unparented acc here, potentially
        // because the acc is being moved. See bug 1807639.
        // Pivot expects to be created with a non-null mRoot.
        Pivot p = Pivot(ancestor);
        PivotRadioNameRule rule(name);
        Accessible* match = p.Next(ancestor, rule);
        while (match) {
          rel.AppendTarget(match->AsRemote());
          match = p.Next(match, rule);
        }
      }
      return rel;
    }

    if (IsARIARole(nsGkAtoms::radio)) {
      // ARIA radio buttons should be grouped by their radio group
      // parent, if one exists.
      RemoteAccessible* currParent = RemoteParent();
      while (currParent && currParent->Role() != roles::RADIO_GROUP) {
        currParent = currParent->RemoteParent();
      }

      if (currParent && currParent->Role() == roles::RADIO_GROUP) {
        // If we found a radiogroup parent, search for all
        // roles::RADIOBUTTON children and add them to our relation.
        // This search will include the radio button this method
        // was called from, which is expected.
        Pivot p = Pivot(currParent);
        PivotRoleRule rule(roles::RADIOBUTTON);
        Accessible* match = p.Next(currParent, rule);
        while (match) {
          MOZ_ASSERT(match->IsRemote(),
                     "We should only be traversing the remote tree.");
          rel.AppendTarget(match->AsRemote());
          match = p.Next(match, rule);
        }
      }
    }
    // By webkit's standard, aria radio buttons do not get grouped
    // if they lack a group parent, so we return an empty
    // relation here if the above check fails.
    return rel;
  }

  Relation rel;
  if (!mCachedFields) {
    return rel;
  }

  auto GetDirectRelationFromCache =
      [](const RemoteAccessible* aAcc,
         RelationType aRelType) -> Maybe<const nsTArray<uint64_t>&> {
    for (const auto& data : kRelationTypeAtoms) {
      if (data.mType != aRelType ||
          (data.mValidTag && aAcc->TagName() != data.mValidTag)) {
        continue;
      }

      if (auto maybeIds = aAcc->mCachedFields->GetAttribute<nsTArray<uint64_t>>(
              data.mAtom)) {
        if (data.mAtom == nsGkAtoms::target) {
          if (!maybeIds->IsEmpty() &&
              !nsAccUtils::IsValidDetailsTargetForAnchor(
                  aAcc->mDoc->GetAccessible(maybeIds->ElementAt(0)), aAcc)) {
            continue;
          }
        }

        // For popovertarget/commandfor, check the PopoverInvokerIsDetails flag
        // to determine whether this should be a DETAILS or DESCRIBED_BY
        // relation.
        if (data.mAtom == nsGkAtoms::popovertarget ||
            data.mAtom == nsGkAtoms::commandfor) {
          auto isDetails = aAcc->mCachedFields->GetAttribute<bool>(
              CacheKey::PopoverInvokerIsDetails);
          bool isDetailsValue = isDetails.isSome() && *isDetails;
          bool wantDetails = aRelType == RelationType::DETAILS;
          if (isDetailsValue != wantDetails) {
            continue;
          }
        }

        // Relations can have several cached attributes in order of precedence,
        // if one is found we use it.
        return maybeIds;
      }
    }

    return Nothing();
  };

  if (auto maybeIds = GetDirectRelationFromCache(this, aType)) {
    rel.AppendIter(new RemoteAccIterator(*maybeIds, Document()));
  }

  if (auto accRelMapEntry = mDoc->mReverseRelations.Lookup(ID())) {
    // A list of candidate IDs that might have a relation of type aType
    // pointing to us. We keep a list so we don't evaluate or add the same
    // target more than once.
    nsTArray<uint64_t> relationCandidateIds;
    for (const auto& data : kRelationTypeAtoms) {
      if (data.mReverseType != aType) {
        continue;
      }

      auto reverseIdsEntry = accRelMapEntry.Data().Lookup(&data);
      if (!reverseIdsEntry) {
        continue;
      }

      for (auto id : *reverseIdsEntry) {
        if (relationCandidateIds.Contains(id)) {
          // If multiple attributes point to the same target, only
          // include it once. Our assumption is that there is a 1:1
          // relationship between relation and reverse relation *types*.
          continue;
        }
        relationCandidateIds.AppendElement(id);

        RemoteAccessible* relatedAcc = mDoc->GetAccessible(id);
        if (!relatedAcc) {
          continue;
        }

        if (auto maybeIds =
                GetDirectRelationFromCache(relatedAcc, data.mType)) {
          if (maybeIds->Contains(ID())) {
            // The candidate target has the forward relation type pointing
            // to us, so we can add it as a target.
            rel.AppendTarget(relatedAcc);
          }
        }
      }
    }
  }

  // We handle these relations here rather than before cached relations because
  // the cached relations need to take precedence. For example, a <figure> with
  // both aria-labelledby and a <figcaption> must return two LABELLED_BY
  // targets: the aria-labelledby and then the <figcaption>.
  if (aType == RelationType::LABELLED_BY) {
    rel.AppendIter(new ArrayAccIterator(LegendsOrCaptions()));
  } else if (aType == RelationType::LABEL_FOR) {
    if (RemoteAccessible* labelTarget = LegendOrCaptionFor()) {
      rel.AppendTarget(labelTarget);
    }
  }

  return rel;
}

nsTArray<Accessible*> RemoteAccessible::LegendsOrCaptions() const {
  nsTArray<Accessible*> children;
  auto AddChildWithTag = [this, &children](nsAtom* aTarget) {
    uint32_t count = ChildCount();
    for (uint32_t c = 0; c < count; ++c) {
      Accessible* child = ChildAt(c);
      MOZ_ASSERT(child);
      if (child->TagName() == aTarget) {
        children.AppendElement(child);
      }
    }
  };

  auto tag = TagName();
  if (tag == nsGkAtoms::figure) {
    AddChildWithTag(nsGkAtoms::figcaption);
  } else if (tag == nsGkAtoms::fieldset) {
    AddChildWithTag(nsGkAtoms::legend);
  } else if (tag == nsGkAtoms::table) {
    AddChildWithTag(nsGkAtoms::caption);
  }

  return children;
}

RemoteAccessible* RemoteAccessible::LegendOrCaptionFor() const {
  auto tag = TagName();
  if (tag == nsGkAtoms::figcaption) {
    if (RemoteAccessible* parent = RemoteParent()) {
      if (parent->TagName() == nsGkAtoms::figure) {
        return parent;
      }
    }
  } else if (tag == nsGkAtoms::legend) {
    if (RemoteAccessible* parent = RemoteParent()) {
      if (parent->TagName() == nsGkAtoms::fieldset) {
        return parent;
      }
    }
  } else if (tag == nsGkAtoms::caption) {
    if (RemoteAccessible* parent = RemoteParent()) {
      if (parent->TagName() == nsGkAtoms::table) {
        return parent;
      }
    }
  }

  return nullptr;
}

void RemoteAccessible::AppendTextTo(nsAString& aText, uint32_t aStartOffset,
                                    uint32_t aLength) {
  if (IsText()) {
    if (mDoc->RequestDomainsIfInactive(CacheDomain::Text)) {
      return;
    }
    if (mCachedFields) {
      if (auto text = mCachedFields->GetAttribute<nsString>(CacheKey::Text)) {
        aText.Append(Substring(*text, aStartOffset, aLength));
      }
      VERIFY_CACHE(CacheDomain::Text);
    }
    return;
  }

  if (aStartOffset != 0 || aLength == 0) {
    return;
  }

  if (IsHTMLBr()) {
    aText += kForcedNewLineChar;
  } else if (RemoteParent() && nsAccUtils::MustPrune(RemoteParent())) {
    // Expose the embedded object accessible as imaginary embedded object
    // character if its parent hypertext accessible doesn't expose children to
    // AT.
    aText += kImaginaryEmbeddedObjectChar;
  } else {
    aText += kEmbeddedObjectChar;
  }
}

nsTArray<bool> RemoteAccessible::PreProcessRelations(AccAttributes* aFields) {
  if (!mDoc->DomainsAreActive(CacheDomain::Relations)) {
    return {};
  }
  nsTArray<bool> updateTracker(std::size(kRelationTypeAtoms));
  for (auto const& data : kRelationTypeAtoms) {
    if (data.mValidTag) {
      // The relation we're currently processing only applies to particular
      // elements. Check to see if we're one of them.
      nsAtom* tag = TagName();
      if (!tag) {
        // TagName() returns null on an initial cache push -- check aFields
        // for a tag name instead.
        if (auto maybeTag =
                aFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::TagName)) {
          tag = *maybeTag;
        }
      }
      MOZ_ASSERT(
          tag || IsTextLeaf() || IsDoc(),
          "Could not fetch tag via TagName() or from initial cache push!");
      if (tag != data.mValidTag) {
        // If this rel doesn't apply to us, do no pre-processing. Also,
        // note in our updateTracker that we should do no post-processing.
        updateTracker.AppendElement(false);
        continue;
      }
    }

    nsStaticAtom* const relAtom = data.mAtom;
    auto newRelationTargets =
        aFields->GetAttribute<nsTArray<uint64_t>>(relAtom);
    bool shouldAddNewImplicitRels =
        newRelationTargets && newRelationTargets->Length();

    // Remove existing implicit relations if we need to perform an update, or
    // if we've received a DeleteEntry(). Only do this if mCachedFields is
    // initialized. If mCachedFields is not initialized, we still need to
    // construct the update array so we correctly handle reverse rels in
    // PostProcessRelations.
    if ((shouldAddNewImplicitRels ||
         aFields->GetAttribute<DeleteEntry>(relAtom)) &&
        mCachedFields) {
      ASSERT_DOMAINS_ACTIVE(CacheDomain::Relations, this);
      if (auto maybeOldIDs =
              mCachedFields->GetAttribute<nsTArray<uint64_t>>(relAtom)) {
        for (uint64_t id : *maybeOldIDs) {
          // For each target, fetch its reverse relation map
          // We need to call `Lookup` here instead of `LookupOrInsert` because
          // it's possible the ID we're querying is from an acc that has since
          // been Shutdown(), and so has intentionally removed its reverse rels
          // from the doc's reverse rel cache.
          if (auto reverseRels = Document()->mReverseRelations.Lookup(id)) {
            // Then fetch its reverse relation's ID list. This should be safe
            // to do via LookupOrInsert because by the time we've gotten here,
            // we know the acc and `this` are still alive in the doc. If we hit
            // the following assert, we don't have parity on implicit/explicit
            // rels and something is wrong.
            nsTArray<uint64_t>& reverseRelIDs =
                reverseRels->LookupOrInsert(&data);
            //  There might be other reverse relations stored for this acc, so
            //  remove our ID instead of deleting the array entirely.
            DebugOnly<bool> removed = reverseRelIDs.RemoveElement(ID());
            MOZ_ASSERT(removed, "Can't find old reverse relation");
          }
        }
      }
    }

    updateTracker.AppendElement(shouldAddNewImplicitRels);
  }

  return updateTracker;
}

void RemoteAccessible::PostProcessRelations(const nsTArray<bool>& aToUpdate) {
  if (!mDoc->DomainsAreActive(CacheDomain::Relations)) {
    return;
  }
  size_t updateCount = aToUpdate.Length();
  MOZ_ASSERT(updateCount == std::size(kRelationTypeAtoms),
             "Did not note update status for every relation type!");
  for (size_t i = 0; i < updateCount; i++) {
    if (aToUpdate.ElementAt(i)) {
      // Since kRelationTypeAtoms was used to generate aToUpdate, we
      // know the ith entry of aToUpdate corresponds to the relation type in
      // the ith entry of kRelationTypeAtoms. Fetch the related data here.
      auto const& data = kRelationTypeAtoms[i];

      const nsTArray<uint64_t>& newIDs =
          *mCachedFields->GetAttribute<nsTArray<uint64_t>>(data.mAtom);
      for (uint64_t id : newIDs) {
        auto& relations = Document()->mReverseRelations.LookupOrInsert(id);
        nsTArray<uint64_t>& ids = relations.LookupOrInsert(&data);
        ids.AppendElement(ID());
      }
    }
  }
}

void RemoteAccessible::PruneRelationsOnShutdown() {
  auto reverseRels = mDoc->mReverseRelations.Lookup(ID());
  if (!reverseRels) {
    return;
  }
  for (auto const& data : kRelationTypeAtoms) {
    // Fetch the list of targets for this reverse relation
    auto reverseTargetList = reverseRels->Lookup(&data);
    if (!reverseTargetList) {
      continue;
    }
    for (uint64_t id : *reverseTargetList) {
      // For each target, retrieve its corresponding forward relation target
      // list
      RemoteAccessible* affectedAcc = mDoc->GetAccessible(id);
      if (!affectedAcc) {
        // It's possible the affect acc also shut down, in which case
        // we don't have anything to update.
        continue;
      }
      if (auto forwardTargetList =
              affectedAcc->mCachedFields
                  ->GetMutableAttribute<nsTArray<uint64_t>>(data.mAtom)) {
        forwardTargetList->RemoveElement(ID());
        if (!forwardTargetList->Length()) {
          // The ID we removed was the only thing in the list, so remove the
          // entry from the cache entirely -- don't leave an empty array.
          affectedAcc->mCachedFields->Remove(data.mAtom);
        }
      }
    }
  }
  // Remove this ID from the document's map of reverse relations.
  reverseRels.Remove();
}

uint32_t RemoteAccessible::GetCachedTextLength() {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Text)) {
    return 0;
  }
  MOZ_ASSERT(!HasChildren());
  if (!mCachedFields) {
    return 0;
  }
  VERIFY_CACHE(CacheDomain::Text);
  auto text = mCachedFields->GetAttribute<nsString>(CacheKey::Text);
  if (!text) {
    return 0;
  }
  return text->Length();
}

Maybe<const nsTArray<int32_t>&> RemoteAccessible::GetCachedTextLines() {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::TextBounds)) {
    return Nothing();
  }

  MOZ_ASSERT(!HasChildren());
  if (!mCachedFields) {
    return Nothing();
  }
  VERIFY_CACHE(CacheDomain::TextBounds);
  return mCachedFields->GetAttribute<nsTArray<int32_t>>(
      CacheKey::TextLineStarts);
}

nsRect RemoteAccessible::GetCachedCharRect(int32_t aOffset) {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::TextBounds, this);
  MOZ_ASSERT(IsText());
  if (!mCachedFields) {
    return nsRect();
  }

  if (Maybe<const nsTArray<int32_t>&> maybeCharData =
          mCachedFields->GetAttribute<nsTArray<int32_t>>(
              CacheKey::TextBounds)) {
    const nsTArray<int32_t>& charData = *maybeCharData;
    const int32_t index = aOffset * kNumbersInRect;
    if (index < static_cast<int32_t>(charData.Length())) {
      return nsRect(charData[index], charData[index + 1], charData[index + 2],
                    charData[index + 3]);
    }
    // It is valid for a client to call this with an offset 1 after the last
    // character because of the insertion point at the end of text boxes.
    MOZ_ASSERT(index == static_cast<int32_t>(charData.Length()));
  }

  return nsRect();
}

void RemoteAccessible::DOMNodeID(nsString& aID) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::DOMNodeIDAndClass)) {
    return;
  }
  if (mCachedFields) {
    mCachedFields->GetAttribute(CacheKey::DOMNodeID, aID);
    VERIFY_CACHE(CacheDomain::DOMNodeIDAndClass);
  }
}

void RemoteAccessible::DOMNodeClass(nsString& aClass) const {
  if (mCachedFields) {
    mCachedFields->GetAttribute(CacheKey::DOMNodeClass, aClass);
    VERIFY_CACHE(CacheDomain::DOMNodeIDAndClass);
  }
}

int32_t RemoteAccessible::HeadingLevel() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::GroupInfo)) {
    return 0;
  }
  if (mCachedFields) {
    if (auto maybeLevel =
            mCachedFields->GetAttribute<int32_t>(CacheKey::HeadingLevel)) {
      VERIFY_CACHE(CacheDomain::GroupInfo);
      return *maybeLevel;
    }
  }
  return 0;
}

void RemoteAccessible::ScrollToPoint(uint32_t aScrollType, int32_t aX,
                                     int32_t aY) {
  (void)mDoc->SendScrollToPoint(mID, aScrollType, aX, aY);
}

bool RemoteAccessible::IsScrollable() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ScrollPosition)) {
    return false;
  }
  return mCachedFields && mCachedFields->HasAttribute(CacheKey::ScrollPosition);
}

bool RemoteAccessible::IsPopover() const {
  return mCachedFields && mCachedFields->HasAttribute(CacheKey::PopupType);
}

bool RemoteAccessible::IsEditable() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::State)) {
    return false;
  }

  if (mCachedFields) {
    if (auto rawState =
            mCachedFields->GetAttribute<uint64_t>(CacheKey::State)) {
      VERIFY_CACHE(CacheDomain::State);
      return (*rawState & states::EDITABLE) != 0;
    }
  }

  return false;
}

int32_t RemoteAccessible::ValueRegion() const {
  MOZ_ASSERT(TagName() == nsGkAtoms::meter,
             "Accessing value region on non-meter element?");
  if (mCachedFields) {
    if (auto region =
            mCachedFields->GetAttribute<int32_t>(CacheKey::ValueRegion)) {
      return *region;
    }
  }
  // Expose sub-optimal (but not critical) as the value region, as a fallback.
  return 0;
}

void RemoteAccessible::ScrollSubstringToPoint(int32_t aStartOffset,
                                              int32_t aEndOffset,
                                              uint32_t aCoordinateType,
                                              int32_t aX, int32_t aY) {
  (void)mDoc->SendScrollSubstringToPoint(mID, aStartOffset, aEndOffset,
                                         aCoordinateType, aX, aY);
}

const AccAttributes* RemoteAccessible::GetCachedTextAttributes() {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Text)) {
    return nullptr;
  }
  MOZ_ASSERT(IsText() || IsHyperText());
  if (mCachedFields) {
    auto attrs = mCachedFields->GetAttributeWeakPtr<AccAttributes>(
        CacheKey::TextAttributes);
    VERIFY_CACHE(CacheDomain::Text);
    return attrs;
  }
  return nullptr;
}

std::pair<LayoutDeviceIntRect, nsIWidget*> RemoteAccessible::GetCaretRect() {
  nsIWidget* widget = nullptr;
  LocalAccessible* outerDoc = OuterDocOfRemoteBrowser();
  if (outerDoc) {
    widget = nsContentUtils::WidgetForContent(outerDoc->GetContent());
  }

  return {mDoc->GetCachedCaretRect(), widget};
}

already_AddRefed<AccAttributes> RemoteAccessible::DefaultTextAttributes() {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Text)) {
    return nullptr;
  }

  auto result = MakeRefPtr<AccAttributes>();
  for (RemoteAccessible* parent = this; parent;
       parent = parent->RemoteParent()) {
    if (!parent->IsHyperText()) {
      // We are only interested in hypertext nodes for defaults, not in text
      // leafs or non hypertext nodes.
      continue;
    }

    if (const AccAttributes* parentAttrs = parent->GetCachedTextAttributes()) {
      // Update our text attributes with any parent entries we don't have.
      parentAttrs->CopyTo(result, true);
    }
  }

  return result.forget();
}

const AccAttributes* RemoteAccessible::GetCachedARIAAttributes() const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::ARIA, this);
  if (mCachedFields) {
    auto attrs = mCachedFields->GetAttributeWeakPtr<AccAttributes>(
        CacheKey::ARIAAttributes);
    VERIFY_CACHE(CacheDomain::ARIA);
    return attrs;
  }
  return nullptr;
}

nsString RemoteAccessible::GetCachedHTMLNameAttribute() const {
  ASSERT_DOMAINS_ACTIVE(CacheDomain::Relations, this);
  if (mCachedFields) {
    if (auto maybeName =
            mCachedFields->GetAttribute<nsString>(CacheKey::DOMName)) {
      return *maybeName;
    }
  }
  return nsString();
}

uint64_t RemoteAccessible::State() {
  if (mDoc->RequestDomainsIfInactive(
          CacheDomain::State |   // State attributes
          CacheDomain::Style |   // for Opacity (via ApplyImplicitState)
          CacheDomain::Viewport  // necessary to build mOnScreenAccessibles
          )) {
    return 0;
  }
  uint64_t state = 0;
  if (mCachedFields) {
    if (auto rawState =
            mCachedFields->GetAttribute<uint64_t>(CacheKey::State)) {
      VERIFY_CACHE(CacheDomain::State);
      state = *rawState;
    }

    ApplyImplicitState(state);

    auto* cbc = mDoc->GetBrowsingContext();
    if (cbc && !cbc->IsActive()) {
      // If our browsing context is _not_ active, we're in a background tab
      // and inherently offscreen.
      state |= states::OFFSCREEN;
    } else {
      // If we're in an active browsing context, there are a few scenarios we
      // need to address:
      // - We are an iframe document in the visual viewport
      // - We are an iframe document out of the visual viewport
      // - We are non-iframe content in the visual viewport
      // - We are non-iframe content out of the visual viewport
      // We assume top level tab docs are on screen if their BC is active, so
      // we don't need additional handling for them here.
      if (!mDoc->IsTopLevel()) {
        // Here we handle iframes and iframe content.
        // We use an iframe's outer doc's position in the embedding document's
        // viewport to determine if the iframe has been scrolled offscreen.
        Accessible* docParent = mDoc->Parent();
        // In rare cases, we might not have an outer doc yet. Return if that's
        // the case.
        if (NS_WARN_IF(!docParent || !docParent->IsRemote())) {
          return state;
        }

        RemoteAccessible* outerDoc = docParent->AsRemote();
        DocAccessibleParent* embeddingDocument = outerDoc->Document();
        if (embeddingDocument &&
            !embeddingDocument->mOnScreenAccessibles.Contains(outerDoc->ID())) {
          // Our embedding document's viewport cache doesn't contain the ID of
          // our outer doc, so this iframe (and any of its content) is
          // offscreen.
          state |= states::OFFSCREEN;
        } else if (this != mDoc && !mDoc->mOnScreenAccessibles.Contains(ID())) {
          // Our embedding document's viewport cache contains the ID of our
          // outer doc, but the iframe's viewport cache doesn't contain our ID.
          // We are offscreen.
          state |= states::OFFSCREEN;
        }
      } else if (this != mDoc && !mDoc->mOnScreenAccessibles.Contains(ID())) {
        // We are top level tab content (but not a top level tab doc).
        // If our tab doc's viewport cache doesn't contain our ID, we're
        // offscreen.
        state |= states::OFFSCREEN;
      }
    }
  }

  return state;
}

already_AddRefed<AccAttributes> RemoteAccessible::Attributes() {
  auto attributes = MakeRefPtr<AccAttributes>();
  if (mDoc->RequestDomainsIfInactive(
          CacheDomain::ARIA |                // GetCachedARIAAttributes
          CacheDomain::NameAndDescription |  // Name
          CacheDomain::Text |                // Name
          CacheDomain::Value |               // Value
          CacheDomain::Actions |             // Value
          CacheDomain::Style |               // DisplayStyle
          CacheDomain::GroupInfo |           // GroupPosition
          CacheDomain::State |               // State
          CacheDomain::Viewport |            // State
          CacheDomain::Table |               // TableIsProbablyForLayout
          CacheDomain::DOMNodeIDAndClass |   // DOMNodeID
          CacheDomain::Relations)) {
    return attributes.forget();
  }

  nsAccessibilityService* accService = GetAccService();
  if (!accService) {
    // The service can be shut down before RemoteAccessibles. If it is shut
    // down, we can't calculate some attributes. We're about to die anyway.
    return attributes.forget();
  }

  if (mCachedFields) {
    // We use GetAttribute instead of GetAttributeRefPtr because we need
    // nsAtom, not const nsAtom.
    if (auto tag =
            mCachedFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::TagName)) {
      attributes->SetAttribute(nsGkAtoms::tag, *tag);
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

    if (auto inputType =
            mCachedFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::InputType)) {
      attributes->SetAttribute(nsGkAtoms::textInputType, *inputType);
    }

    if (RefPtr<nsAtom> display = DisplayStyle()) {
      attributes->SetAttribute(nsGkAtoms::display, display);
    }

    if (TableCellAccessible* cell = AsTableCell()) {
      TableAccessible* table = cell->Table();
      uint32_t row = cell->RowIdx();
      uint32_t col = cell->ColIdx();
      int32_t cellIdx = table->CellIndexAt(row, col);
      if (cellIdx != -1) {
        attributes->SetAttribute(nsGkAtoms::tableCellIndex, cellIdx);
      }
    }

    if (bool layoutGuess = TableIsProbablyForLayout()) {
      attributes->SetAttribute(nsGkAtoms::layout_guess, layoutGuess);
    }

    accService->MarkupAttributes(this, attributes);

    const nsRoleMapEntry* roleMap = ARIARoleMap();
    nsAutoString role;
    mCachedFields->GetAttribute(CacheKey::ARIARole, role);
    if (role.IsEmpty()) {
      if (roleMap && roleMap->roleAtom != nsGkAtoms::_empty) {
        // Single, known role.
        attributes->SetAttribute(nsGkAtoms::xmlroles, roleMap->roleAtom);
      } else if (nsAtom* landmark = LandmarkRole()) {
        // Landmark role from markup; e.g. HTML <main>.
        attributes->SetAttribute(nsGkAtoms::xmlroles, landmark);
      }
    } else {
      // Unknown role or multiple roles.
      attributes->SetAttribute(nsGkAtoms::xmlroles, std::move(role));
    }

    if (roleMap) {
      nsAutoString live;
      if (nsAccUtils::GetLiveAttrValue(roleMap->liveAttRule, live)) {
        attributes->SetAttribute(nsGkAtoms::aria_live, std::move(live));
      }
    }

    if (auto ariaAttrs = GetCachedARIAAttributes()) {
      ariaAttrs->CopyTo(attributes);
    }

    nsAccUtils::SetLiveContainerAttributes(attributes, this);

    nsString id;
    DOMNodeID(id);
    if (!id.IsEmpty()) {
      attributes->SetAttribute(nsGkAtoms::id, std::move(id));
    }

    nsString className;
    DOMNodeClass(className);
    if (!className.IsEmpty()) {
      attributes->SetAttribute(nsGkAtoms::_class, std::move(className));
    }

    if (IsImage()) {
      nsString src;
      mCachedFields->GetAttribute(CacheKey::SrcURL, src);
      if (!src.IsEmpty()) {
        attributes->SetAttribute(nsGkAtoms::src, std::move(src));
      }
    }

    if (IsTextField()) {
      nsString placeholder;
      mCachedFields->GetAttribute(CacheKey::HTMLPlaceholder, placeholder);
      if (!placeholder.IsEmpty()) {
        attributes->SetAttribute(nsGkAtoms::placeholder,
                                 std::move(placeholder));
        attributes->Remove(nsGkAtoms::aria_placeholder);
      }
    }

    nsString popupType;
    mCachedFields->GetAttribute(CacheKey::PopupType, popupType);
    if (!popupType.IsEmpty()) {
      attributes->SetAttribute(nsGkAtoms::ispopup, std::move(popupType));
    }

    if (HasCustomActions()) {
      attributes->SetAttribute(nsGkAtoms::hasActions, true);
    }

    nsString detailsFrom;
    if (mCachedFields->HasAttribute(nsGkAtoms::aria_details)) {
      detailsFrom.AssignLiteral("aria-details");
    } else if (mCachedFields->HasAttribute(nsGkAtoms::commandfor)) {
      // Only expose details_from if this is actually a DETAILS relation
      auto isDetails =
          mCachedFields->GetAttribute<bool>(CacheKey::PopoverInvokerIsDetails);
      if (isDetails && *isDetails) {
        detailsFrom.AssignLiteral("command-for");
      }
    } else if (mCachedFields->HasAttribute(nsGkAtoms::popovertarget)) {
      // Only expose details_from if this is actually a DETAILS relation
      auto isDetails =
          mCachedFields->GetAttribute<bool>(CacheKey::PopoverInvokerIsDetails);
      if (isDetails && *isDetails) {
        detailsFrom.AssignLiteral("popover-target");
      }
    } else if (mCachedFields->HasAttribute(nsGkAtoms::target)) {
      detailsFrom.AssignLiteral("css-anchor");
    }

    if (!detailsFrom.IsEmpty()) {
      attributes->SetAttribute(nsGkAtoms::details_from, std::move(detailsFrom));
    }
  }

  nsAutoString name;
  if (Name(name) != eNameFromSubtree && !name.IsVoid()) {
    attributes->SetAttribute(nsGkAtoms::explicit_name, true);
  }

  // Expose the string value via the valuetext attribute. We test for the value
  // interface because we don't want to expose traditional Value() information
  // such as URLs on links and documents, or text in an input.
  // XXX This is only needed for ATK, since other APIs have native ways to
  // retrieve value text. We should probably move this into ATK specific code.
  // For now, we do this because LocalAccessible does it.
  if (HasNumericValue()) {
    nsString valuetext;
    Value(valuetext);
    attributes->SetAttribute(nsGkAtoms::aria_valuetext, std::move(valuetext));
  }

  return attributes.forget();
}

nsAtom* RemoteAccessible::TagName() const {
  if (mCachedFields) {
    if (auto tag =
            mCachedFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::TagName)) {
      return *tag;
    }
  }

  return nullptr;
}

already_AddRefed<nsAtom> RemoteAccessible::DisplayStyle() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Style)) {
    return nullptr;
  }
  if (mCachedFields) {
    if (auto display =
            mCachedFields->GetAttribute<RefPtr<nsAtom>>(CacheKey::CSSDisplay)) {
      RefPtr<nsAtom> result = *display;
      return result.forget();
    }
  }
  return nullptr;
}

float RemoteAccessible::Opacity() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Style)) {
    return 1.0f;
  }

  if (mCachedFields) {
    if (auto opacity = mCachedFields->GetAttribute<float>(CacheKey::Opacity)) {
      return *opacity;
    }
  }

  return 1.0f;
}

WritingMode RemoteAccessible::GetWritingMode() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Style)) {
    return WritingMode();
  }

  if (mCachedFields) {
    if (auto wm =
            mCachedFields->GetAttribute<WritingMode>(CacheKey::WritingMode)) {
      return *wm;
    }
  }

  return WritingMode();
}

void RemoteAccessible::LiveRegionAttributes(nsAString* aLive,
                                            nsAString* aRelevant,
                                            Maybe<bool>* aAtomic,
                                            nsAString* aBusy) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ARIA)) {
    return;
  }
  if (!mCachedFields) {
    return;
  }
  auto attrs = GetCachedARIAAttributes();
  if (!attrs) {
    return;
  }
  if (aLive) {
    attrs->GetAttribute(nsGkAtoms::aria_live, *aLive);
  }
  if (aRelevant) {
    attrs->GetAttribute(nsGkAtoms::aria_relevant, *aRelevant);
  }
  if (aAtomic) {
    if (auto value =
            attrs->GetAttribute<RefPtr<nsAtom>>(nsGkAtoms::aria_atomic)) {
      *aAtomic = Some(*value == nsGkAtoms::_true);
    }
  }
  if (aBusy) {
    attrs->GetAttribute(nsGkAtoms::aria_busy, *aBusy);
  }
}

Maybe<bool> RemoteAccessible::ARIASelected() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::State)) {
    return Nothing();
  }

  if (mCachedFields) {
    return mCachedFields->GetAttribute<bool>(CacheKey::ARIASelected);
  }
  return Nothing();
}

nsAtom* RemoteAccessible::GetPrimaryAction() const {
  if (mCachedFields) {
    ASSERT_DOMAINS_ACTIVE(CacheDomain::Actions, this);
    if (auto action = mCachedFields->GetAttribute<RefPtr<nsAtom>>(
            CacheKey::PrimaryAction)) {
      return *action;
    }
  }

  return nullptr;
}

uint8_t RemoteAccessible::ActionCount() const {
  uint8_t actionCount = 0;
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Actions)) {
    return actionCount;
  }
  if (mCachedFields) {
    if (HasPrimaryAction() || ActionAncestor()) {
      actionCount++;
    }

    if (mCachedFields->HasAttribute(CacheKey::HasLongdesc)) {
      actionCount++;
    }
    VERIFY_CACHE(CacheDomain::Actions);
  }

  return actionCount;
}

void RemoteAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Actions)) {
    return;
  }

  if (mCachedFields) {
    aName.Truncate();
    nsAtom* action = GetPrimaryAction();
    bool hasActionAncestor = !action && ActionAncestor();

    switch (aIndex) {
      case 0:
        if (action) {
          action->ToString(aName);
        } else if (hasActionAncestor) {
          aName.AssignLiteral("clickAncestor");
        } else if (mCachedFields->HasAttribute(CacheKey::HasLongdesc)) {
          aName.AssignLiteral("showlongdesc");
        }
        break;
      case 1:
        if ((action || hasActionAncestor) &&
            mCachedFields->HasAttribute(CacheKey::HasLongdesc)) {
          aName.AssignLiteral("showlongdesc");
        }
        break;
      default:
        break;
    }
  }
  VERIFY_CACHE(CacheDomain::Actions);
}

bool RemoteAccessible::DoAction(uint8_t aIndex) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Actions)) {
    return false;
  }

  if (ActionCount() < aIndex + 1) {
    return false;
  }

  (void)mDoc->SendDoActionAsync(mID, aIndex);
  return true;
}

KeyBinding RemoteAccessible::AccessKey() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Actions)) {
    return {};
  }

  if (mCachedFields) {
    if (auto value =
            mCachedFields->GetAttribute<uint64_t>(CacheKey::AccessKey)) {
      return KeyBinding(*value);
    }
  }
  return KeyBinding();
}

void RemoteAccessible::SelectionRanges(nsTArray<TextRange>* aRanges) const {
  Document()->SelectionRanges(aRanges);
}

bool RemoteAccessible::RemoveFromSelection(int32_t aSelectionNum) {
  MOZ_ASSERT(IsHyperText());
  if (SelectionCount() <= aSelectionNum) {
    return false;
  }

  (void)mDoc->SendRemoveTextSelection(mID, aSelectionNum);

  return true;
}

void RemoteAccessible::ARIAGroupPosition(int32_t* aLevel, int32_t* aSetSize,
                                         int32_t* aPosInSet) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::GroupInfo)) {
    return;
  }

  if (!mCachedFields) {
    return;
  }

  if (aLevel) {
    if (auto level =
            mCachedFields->GetAttribute<int32_t>(nsGkAtoms::aria_level)) {
      *aLevel = *level;
    }
  }
  if (aSetSize) {
    if (auto setsize =
            mCachedFields->GetAttribute<int32_t>(nsGkAtoms::aria_setsize)) {
      *aSetSize = *setsize;
    }
  }
  if (aPosInSet) {
    if (auto posinset =
            mCachedFields->GetAttribute<int32_t>(nsGkAtoms::aria_posinset)) {
      *aPosInSet = *posinset;
    }
  }
}

AccGroupInfo* RemoteAccessible::GetGroupInfo() const {
  // Interpret a call to GetGroupInfo as a signal that the AT will want group
  // info information. CacheKey::GroupInfo is not in CacheDomain::GroupInfo, so
  // this isn't strictly necessary, but is likely helpful.
  if (mDoc->RequestDomainsIfInactive(CacheDomain::GroupInfo)) {
    return nullptr;
  }

  if (!mCachedFields) {
    return nullptr;
  }

  if (auto groupInfo = mCachedFields->GetAttribute<UniquePtr<AccGroupInfo>>(
          CacheKey::GroupInfo)) {
    return groupInfo->get();
  }

  return nullptr;
}

AccGroupInfo* RemoteAccessible::GetOrCreateGroupInfo() {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::GroupInfo)) {
    return nullptr;
  }

  AccGroupInfo* groupInfo = GetGroupInfo();
  if (groupInfo) {
    return groupInfo;
  }

  groupInfo = AccGroupInfo::CreateGroupInfo(this);
  if (groupInfo) {
    if (!mCachedFields) {
      mCachedFields = new AccAttributes();
    }

    mCachedFields->SetAttribute(CacheKey::GroupInfo, groupInfo);
  }

  return groupInfo;
}

void RemoteAccessible::InvalidateGroupInfo() {
  if (mCachedFields) {
    mCachedFields->Remove(CacheKey::GroupInfo);
  }
}

void RemoteAccessible::GetPositionAndSetSize(int32_t* aPosInSet,
                                             int32_t* aSetSize) {
  // Note: Required domains come from requirements of RelationByType.
  if (mDoc->RequestDomainsIfInactive(
          CacheDomain::Relations | CacheDomain::Value |
          CacheDomain::DOMNodeIDAndClass | CacheDomain::GroupInfo)) {
    return;
  }

  if (IsHTMLRadioButton()) {
    *aSetSize = 0;
    Relation rel = RelationByType(RelationType::MEMBER_OF);
    while (Accessible* radio = rel.Next()) {
      ++*aSetSize;
      if (radio == this) {
        *aPosInSet = *aSetSize;
      }
    }
    return;
  }

  Accessible::GetPositionAndSetSize(aPosInSet, aSetSize);
}

bool RemoteAccessible::HasPrimaryAction() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Actions)) {
    return false;
  }
  return mCachedFields && mCachedFields->HasAttribute(CacheKey::PrimaryAction);
}

void RemoteAccessible::TakeFocus() const {
  (void)mDoc->SendTakeFocus(mID);
  auto* bp = mDoc->Manager();
  MOZ_ASSERT(bp);
  if (nsFocusManager::GetFocusedElementStatic() == bp->GetOwnerElement()) {
    // This remote document tree is already focused. We don't need to do
    // anything else.
    return;
  }
  // Otherwise, we need to focus the <browser> or <iframe> element embedding the
  // remote document in the parent process. If `this` is in an OOP iframe, we
  // first need to focus the embedder iframe (and any ancestor OOP iframes). If
  // the parent process embedder element were already focused, that would happen
  // automatically, but it isn't. We can't simply focus the parent process
  // embedder element before calling mDoc->SendTakeFocus because that would
  // cause the remote document to restore focus to the last focused element,
  // which we don't want.
  DocAccessibleParent* embeddedDoc = mDoc;
  Accessible* embedder = mDoc->Parent();
  while (embedder) {
    MOZ_ASSERT(embedder->IsOuterDoc());
    RemoteAccessible* embedderRemote = embedder->AsRemote();
    if (!embedderRemote) {
      // This is the element in the parent process which embeds the remote
      // document.
      embedder->TakeFocus();
      break;
    }
    // This is a remote <iframe>.
    if (embeddedDoc->IsTopLevelInContentProcess()) {
      // We only need to focus OOP iframes because these are where we cross
      // process boundaries.
      (void)embedderRemote->mDoc->SendTakeFocus(embedderRemote->mID);
    }
    embeddedDoc = embedderRemote->mDoc;
    embedder = embeddedDoc->Parent();
  }
}

void RemoteAccessible::ScrollTo(uint32_t aHow) const {
  (void)mDoc->SendScrollTo(mID, aHow);
}

////////////////////////////////////////////////////////////////////////////////
// SelectAccessible

void RemoteAccessible::SelectedItems(nsTArray<Accessible*>* aItems) {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return;
  }
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTED);
  for (Accessible* selected = p.First(rule); selected;
       selected = p.Next(selected, rule)) {
    aItems->AppendElement(selected);
  }
}

uint32_t RemoteAccessible::SelectedItemCount() {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return 0;
  }
  uint32_t count = 0;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTED);
  for (Accessible* selected = p.First(rule); selected;
       selected = p.Next(selected, rule)) {
    count++;
  }

  return count;
}

Accessible* RemoteAccessible::GetSelectedItem(uint32_t aIndex) {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return nullptr;
  }
  uint32_t index = 0;
  Accessible* selected = nullptr;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTED);
  for (selected = p.First(rule); selected && index < aIndex;
       selected = p.Next(selected, rule)) {
    index++;
  }

  return selected;
}

bool RemoteAccessible::IsItemSelected(uint32_t aIndex) {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return false;
  }
  uint32_t index = 0;
  Accessible* selectable = nullptr;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTABLE);
  for (selectable = p.First(rule); selectable && index < aIndex;
       selectable = p.Next(selectable, rule)) {
    index++;
  }

  return selectable && selectable->State() & states::SELECTED;
}

bool RemoteAccessible::AddItemToSelection(uint32_t aIndex) {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return false;
  }
  uint32_t index = 0;
  Accessible* selectable = nullptr;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTABLE);
  for (selectable = p.First(rule); selectable && index < aIndex;
       selectable = p.Next(selectable, rule)) {
    index++;
  }

  if (selectable) selectable->SetSelected(true);

  return static_cast<bool>(selectable);
}

bool RemoteAccessible::RemoveItemFromSelection(uint32_t aIndex) {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return false;
  }
  uint32_t index = 0;
  Accessible* selectable = nullptr;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTABLE);
  for (selectable = p.First(rule); selectable && index < aIndex;
       selectable = p.Next(selectable, rule)) {
    index++;
  }

  if (selectable) selectable->SetSelected(false);

  return static_cast<bool>(selectable);
}

bool RemoteAccessible::SelectAll() {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return false;
  }
  if ((State() & states::MULTISELECTABLE) == 0) {
    return false;
  }

  bool success = false;
  Accessible* selectable = nullptr;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTABLE);
  for (selectable = p.First(rule); selectable;
       selectable = p.Next(selectable, rule)) {
    success = true;
    selectable->SetSelected(true);
  }
  return success;
}

bool RemoteAccessible::UnselectAll() {
  if (mDoc->RequestDomainsIfInactive(kNecessaryStateDomains)) {
    return false;
  }
  if ((State() & states::MULTISELECTABLE) == 0) {
    return false;
  }

  bool success = false;
  Accessible* selectable = nullptr;
  Pivot p = Pivot(this);
  PivotStateRule rule(states::SELECTABLE);
  for (selectable = p.First(rule); selectable;
       selectable = p.Next(selectable, rule)) {
    success = true;
    selectable->SetSelected(false);
  }
  return success;
}

void RemoteAccessible::TakeSelection() { (void)mDoc->SendTakeSelection(mID); }

void RemoteAccessible::SetSelected(bool aSelect) {
  (void)mDoc->SendSetSelected(mID, aSelect);
}

TableAccessible* RemoteAccessible::AsTable() {
  if (IsTable()) {
    return CachedTableAccessible::GetFrom(this);
  }
  return nullptr;
}

TableCellAccessible* RemoteAccessible::AsTableCell() {
  if (IsTableCell()) {
    return CachedTableCellAccessible::GetFrom(this);
  }
  return nullptr;
}

bool RemoteAccessible::TableIsProbablyForLayout() {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Table)) {
    return false;
  }
  if (mCachedFields) {
    if (auto layoutGuess =
            mCachedFields->GetAttribute<bool>(CacheKey::TableLayoutGuess)) {
      return *layoutGuess;
    }
  }
  return false;
}

nsTArray<int32_t>& RemoteAccessible::GetCachedHyperTextOffsets() {
  if (mCachedFields) {
    if (auto offsets = mCachedFields->GetMutableAttribute<nsTArray<int32_t>>(
            CacheKey::HyperTextOffsets)) {
      return *offsets;
    }
  }
  nsTArray<int32_t> newOffsets;
  if (!mCachedFields) {
    mCachedFields = new AccAttributes();
  }
  mCachedFields->SetAttribute(CacheKey::HyperTextOffsets,
                              std::move(newOffsets));
  return *mCachedFields->GetMutableAttribute<nsTArray<int32_t>>(
      CacheKey::HyperTextOffsets);
}

Maybe<int32_t> RemoteAccessible::GetIntARIAAttr(nsAtom* aAttrName) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ARIA)) {
    return Nothing();
  }
  if (auto attrs = GetCachedARIAAttributes()) {
    if (auto val = attrs->GetAttribute<int32_t>(aAttrName)) {
      return val;
    }
  }
  return Nothing();
}

bool RemoteAccessible::GetStringARIAAttr(nsAtom* aAttrName,
                                         nsAString& aAttrValue) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ARIA)) {
    return false;
  }

  if (aAttrName == nsGkAtoms::role) {
    if (mCachedFields->GetAttribute(CacheKey::ARIARole, aAttrValue)) {
      // Unknown, or multiple roles.
      return true;
    }

    if (const nsRoleMapEntry* roleMap = ARIARoleMap()) {
      if (roleMap->roleAtom != nsGkAtoms::_empty) {
        // Non-empty rolemap, stringify it and return true.
        roleMap->roleAtom->ToString(aAttrValue);
        return true;
      }
    }
  }

  if (auto attrs = GetCachedARIAAttributes()) {
    return attrs->GetAttribute(aAttrName, aAttrValue);
  }

  return false;
}

bool RemoteAccessible::ARIAAttrValueIs(nsAtom* aAttrName,
                                       nsAtom* aAttrValue) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ARIA)) {
    return false;
  }

  if (aAttrName == nsGkAtoms::role) {
    nsAutoString roleStr;
    if (mCachedFields->GetAttribute(CacheKey::ARIARole, roleStr)) {
      return aAttrValue->Equals(roleStr);
    }

    if (const nsRoleMapEntry* roleMap = ARIARoleMap()) {
      return roleMap->roleAtom == aAttrValue;
    }
  }

  if (auto attrs = GetCachedARIAAttributes()) {
    if (auto val = attrs->GetAttribute<RefPtr<nsAtom>>(aAttrName)) {
      return *val == aAttrValue;
    }
  }

  return false;
}

bool RemoteAccessible::HasARIAAttr(nsAtom* aAttrName) const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ARIA)) {
    return false;
  }

  if (aAttrName == nsGkAtoms::role) {
    if (const nsRoleMapEntry* roleMap = ARIARoleMap()) {
      if (roleMap->roleAtom != nsGkAtoms::_empty) {
        return true;
      }
    }

    return mCachedFields->HasAttribute(CacheKey::ARIARole);
  }

  if (auto attrs = GetCachedARIAAttributes()) {
    return attrs->HasAttribute(aAttrName);
  }

  return false;
}

void RemoteAccessible::Language(nsAString& aLocale) {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::Text)) {
    return;
  }
  auto GetLanguage = [&aLocale](RemoteAccessible* aAcc) {
    if (aAcc->IsHyperText() || aAcc->IsText()) {
      if (const AccAttributes* attrs = aAcc->GetCachedTextAttributes()) {
        if (attrs->GetAttribute(nsGkAtoms::language, aLocale)) {
          return true;
        }
      }
    } else if (aAcc->mCachedFields) {
      if (aAcc->mCachedFields->GetAttribute(CacheKey::Language, aLocale)) {
        return true;
      }
    }

    return false;
  };

  for (RemoteAccessible* parent = this; parent;
       parent = parent->RemoteParent()) {
    // Climb up the tree to find where the nearest language attribute is.
    if (GetLanguage(parent)) {
      return;
    }
  }
}

void RemoteAccessible::ReplaceText(const nsAString& aText) {
  (void)mDoc->SendReplaceText(mID, aText);
}

void RemoteAccessible::InsertText(const nsAString& aText, int32_t aPosition) {
  (void)mDoc->SendInsertText(mID, aText, aPosition);
}

void RemoteAccessible::CopyText(int32_t aStartPos, int32_t aEndPos) {
  (void)mDoc->SendCopyText(mID, aStartPos, aEndPos);
}

void RemoteAccessible::CutText(int32_t aStartPos, int32_t aEndPos) {
  (void)mDoc->SendCutText(mID, aStartPos, aEndPos);
}

void RemoteAccessible::DeleteText(int32_t aStartPos, int32_t aEndPos) {
  (void)mDoc->SendDeleteText(mID, aStartPos, aEndPos);
}

void RemoteAccessible::PasteText(int32_t aPosition) {
  (void)mDoc->SendPasteText(mID, aPosition);
}

bool RemoteAccessible::HasCustomActions() const {
  if (mDoc->RequestDomainsIfInactive(CacheDomain::ARIA) || !mCachedFields) {
    return false;
  }
  auto hasActions = mCachedFields->GetAttribute<bool>(CacheKey::HasActions);
  return hasActions && *hasActions;
}

size_t RemoteAccessible::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

size_t RemoteAccessible::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) {
  size_t size = 0;

  // Count attributes.
  if (mCachedFields) {
    size += mCachedFields->SizeOfIncludingThis(aMallocSizeOf);
  }

  // We don't recurse into mChildren because they're already counted in their
  // document's mAccessibles.
  size += mChildren.ShallowSizeOfExcludingThis(aMallocSizeOf);

  return size;
}

}  // namespace a11y
}  // namespace mozilla
