/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocumentOrShadowRoot.h"
#include "mozilla/AnimationComparator.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/dom/AnimatableBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/StyleSheetList.h"
#include "nsTHashtable.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsIFormControl.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsWindowSizes.h"

namespace mozilla::dom {

DocumentOrShadowRoot::DocumentOrShadowRoot(ShadowRoot* aShadowRoot)
    : mAsNode(aShadowRoot), mKind(Kind::ShadowRoot) {
  MOZ_ASSERT(mAsNode);
}

DocumentOrShadowRoot::DocumentOrShadowRoot(Document* aDoc)
    : mAsNode(aDoc), mKind(Kind::Document) {
  MOZ_ASSERT(mAsNode);
}

void DocumentOrShadowRoot::AddSizeOfOwnedSheetArrayExcludingThis(
    nsWindowSizes& aSizes, const nsTArray<RefPtr<StyleSheet>>& aSheets) const {
  size_t n = 0;
  n += aSheets.ShallowSizeOfExcludingThis(aSizes.mState.mMallocSizeOf);
  for (StyleSheet* sheet : aSheets) {
    if (!sheet->GetAssociatedDocumentOrShadowRoot()) {
      // Avoid over-reporting shared sheets.
      continue;
    }
    n += sheet->SizeOfIncludingThis(aSizes.mState.mMallocSizeOf);
  }

  if (mKind == Kind::ShadowRoot) {
    aSizes.mLayoutShadowDomStyleSheetsSize += n;
  } else {
    aSizes.mLayoutStyleSheetsSize += n;
  }
}

void DocumentOrShadowRoot::AddSizeOfExcludingThis(nsWindowSizes& aSizes) const {
  AddSizeOfOwnedSheetArrayExcludingThis(aSizes, mStyleSheets);
  aSizes.mDOMSizes.mDOMOtherSize +=
      mIdentifierMap.SizeOfExcludingThis(aSizes.mState.mMallocSizeOf);
}

DocumentOrShadowRoot::~DocumentOrShadowRoot() {
  for (StyleSheet* sheet : mStyleSheets) {
    sheet->ClearAssociatedDocumentOrShadowRoot();
  }
}

StyleSheetList* DocumentOrShadowRoot::StyleSheets() {
  if (!mDOMStyleSheets) {
    mDOMStyleSheets = new StyleSheetList(*this);
  }
  return mDOMStyleSheets;
}

void DocumentOrShadowRoot::InsertSheetAt(size_t aIndex, StyleSheet& aSheet) {
  aSheet.SetAssociatedDocumentOrShadowRoot(this);
  mStyleSheets.InsertElementAt(aIndex, &aSheet);
}

void DocumentOrShadowRoot::RemoveStyleSheet(StyleSheet& aSheet) {
  auto index = mStyleSheets.IndexOf(&aSheet);
  if (index == mStyleSheets.NoIndex) {
    // We should only hit this case if we are unlinking
    // in which case mStyleSheets should be cleared.
    MOZ_ASSERT(mKind != Kind::Document ||
               AsNode().AsDocument()->InUnlinkOrDeletion());
    MOZ_ASSERT(mStyleSheets.IsEmpty());
    return;
  }
  RefPtr<StyleSheet> sheet = std::move(mStyleSheets[index]);
  mStyleSheets.RemoveElementAt(index);
  RemoveSheetFromStylesIfApplicable(*sheet);
  sheet->ClearAssociatedDocumentOrShadowRoot();
  AsNode().OwnerDoc()->PostStyleSheetRemovedEvent(aSheet);
}

void DocumentOrShadowRoot::RemoveSheetFromStylesIfApplicable(
    StyleSheet& aSheet) {
  if (!aSheet.IsApplicable()) {
    return;
  }
  if (mKind == Kind::Document) {
    AsNode().AsDocument()->RemoveStyleSheetFromStyleSets(aSheet);
  } else {
    MOZ_ASSERT(AsNode().IsShadowRoot());
    static_cast<ShadowRoot&>(AsNode()).RemoveSheetFromStyles(aSheet);
  }
}

// https://drafts.csswg.org/cssom/#dom-documentorshadowroot-adoptedstylesheets
void DocumentOrShadowRoot::OnSetAdoptedStyleSheets(StyleSheet& aSheet,
                                                   uint32_t aIndex,
                                                   ErrorResult& aRv) {
  Document& doc = *AsNode().OwnerDoc();
  // 1. If value’s constructed flag is not set, or its constructor document is
  // not equal to this DocumentOrShadowRoot's node document, throw a
  // "NotAllowedError" DOMException.

  if (!StaticPrefs::
          dom_webcomponents_lift_adoptedstylesheets_restriction_enabled()) {
    if (!aSheet.IsConstructed()) {
      return aRv.ThrowNotAllowedError(
          "Adopted style sheet must be created through the Constructable "
          "StyleSheets API");
    }

    if (!aSheet.ConstructorDocumentMatches(doc)) {
      return aRv.ThrowNotAllowedError(
          "Adopted style sheet's constructor document must match the "
          "document or shadow root's node document");
    }
  }

  auto* shadow = ShadowRoot::FromNode(AsNode());
  MOZ_ASSERT((mKind == Kind::ShadowRoot) == !!shadow);

  auto existingIndex = mAdoptedStyleSheets.LastIndexOf(&aSheet);
  // Ensure it's in the backing array at the right index.
  mAdoptedStyleSheets.InsertElementAt(aIndex, &aSheet);
  if (existingIndex == mAdoptedStyleSheets.NoIndex) {
    // common case: we're not already adopting this sheet.
    aSheet.AddAdopter(*this);
  } else if (existingIndex < aIndex) {
    // We're inserting an already-adopted stylesheet in a later position, so
    // this one should take precedent and we should remove the old one.
    RemoveSheetFromStylesIfApplicable(aSheet);
  } else {
    // The sheet is already at a position later than or equal to the current
    // one, and is already adopted by us, we have nothing to do here other than
    // adding to the current list.
    return;
  }

  if (aSheet.IsApplicable()) {
    if (mKind == Kind::Document) {
      doc.AddStyleSheetToStyleSets(aSheet);
    } else {
      shadow->InsertSheetIntoAuthorData(aIndex, aSheet, mAdoptedStyleSheets);
    }
  }
}

void DocumentOrShadowRoot::OnDeleteAdoptedStyleSheets(StyleSheet& aSheet,
                                                      uint32_t aIndex,
                                                      ErrorResult&) {
  MOZ_ASSERT(mAdoptedStyleSheets.ElementAt(aIndex) == &aSheet);
  mAdoptedStyleSheets.RemoveElementAt(aIndex);
  auto existingIndex = mAdoptedStyleSheets.LastIndexOf(&aSheet);
  if (existingIndex != mAdoptedStyleSheets.NoIndex && existingIndex >= aIndex) {
    // The sheet is still adopted by us and was already later from the one we're
    // removing, so nothing to do.
    return;
  }

  RemoveSheetFromStylesIfApplicable(aSheet);
  if (existingIndex == mAdoptedStyleSheets.NoIndex) {
    // The sheet is no longer adopted by us.
    aSheet.RemoveAdopter(*this);
  } else if (aSheet.IsApplicable()) {
    // We need to re-insert the sheet at the right (pre-existing) index.
    nsINode& node = AsNode();
    if (mKind == Kind::Document) {
      node.AsDocument()->AddStyleSheetToStyleSets(aSheet);
    } else {
      ShadowRoot::FromNode(node)->InsertSheetIntoAuthorData(
          existingIndex, aSheet, mAdoptedStyleSheets);
    }
  }
}

void DocumentOrShadowRoot::ClearAdoptedStyleSheets() {
  auto* shadow = ShadowRoot::FromNode(AsNode());
  auto* doc = shadow ? nullptr : AsNode().AsDocument();
  MOZ_ASSERT(shadow || doc);
  IgnoredErrorResult rv;
  while (!mAdoptedStyleSheets.IsEmpty()) {
    if (shadow) {
      ShadowRoot_Binding::AdoptedStyleSheetsHelpers::RemoveLastElement(shadow,
                                                                       rv);
    } else {
      Document_Binding::AdoptedStyleSheetsHelpers::RemoveLastElement(doc, rv);
    }
    MOZ_DIAGNOSTIC_ASSERT(!rv.Failed(), "Removal doesn't fail");
  }
}

void DocumentOrShadowRoot::CloneAdoptedSheetsFrom(
    const DocumentOrShadowRoot& aSource) {
  if (aSource.mAdoptedStyleSheets.IsEmpty()) {
    return;
  }

  Document& ownerDoc = *AsNode().OwnerDoc();
  const Document& sourceDoc = *aSource.AsNode().OwnerDoc();
  auto* clonedSheetMap = static_cast<Document::AdoptedStyleSheetCloneCache*>(
      sourceDoc.GetProperty(nsGkAtoms::adoptedsheetclones));
  MOZ_ASSERT(clonedSheetMap);

  // We don't need to care about the reflector (AdoptedStyleSheetsHelpers and
  // so) because this is only used for static documents.
  for (const StyleSheet* sheet : aSource.mAdoptedStyleSheets) {
    RefPtr<StyleSheet> clone = clonedSheetMap->LookupOrInsertWith(
        sheet, [&] { return sheet->CloneAdoptedSheet(ownerDoc); });
    MOZ_ASSERT(clone);
    MOZ_DIAGNOSTIC_ASSERT(clone->ConstructorDocumentMatches(ownerDoc));
    ErrorResult rv;
    OnSetAdoptedStyleSheets(*clone, mAdoptedStyleSheets.Length(), rv);
    MOZ_ASSERT(!rv.Failed());
  }
}

Element* DocumentOrShadowRoot::GetElementById(
    const nsAString& aElementId) const {
  if (MOZ_UNLIKELY(aElementId.IsEmpty())) {
    ReportEmptyGetElementByIdArg();
    return nullptr;
  }

  if (IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aElementId)) {
    return entry->GetIdElement();
  }

  return nullptr;
}

Element* DocumentOrShadowRoot::GetElementById(nsAtom* aElementId) const {
  if (MOZ_UNLIKELY(aElementId == nsGkAtoms::_empty)) {
    ReportEmptyGetElementByIdArg();
    return nullptr;
  }

  if (IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aElementId)) {
    return entry->GetIdElement();
  }

  return nullptr;
}

already_AddRefed<nsContentList> DocumentOrShadowRoot::GetElementsByTagNameNS(
    const nsAString& aNamespaceURI, const nsAString& aLocalName) {
  ErrorResult rv;
  RefPtr<nsContentList> list =
      GetElementsByTagNameNS(aNamespaceURI, aLocalName, rv);
  if (rv.Failed()) {
    return nullptr;
  }
  return list.forget();
}

already_AddRefed<nsContentList> DocumentOrShadowRoot::GetElementsByTagNameNS(
    const nsAString& aNamespaceURI, const nsAString& aLocalName,
    ErrorResult& aResult) {
  int32_t nameSpaceId = kNameSpaceID_Wildcard;

  if (!aNamespaceURI.EqualsLiteral("*")) {
    aResult = nsNameSpaceManager::GetInstance()->RegisterNameSpace(
        aNamespaceURI, nameSpaceId);
    if (aResult.Failed()) {
      return nullptr;
    }
  }

  NS_ASSERTION(nameSpaceId != kNameSpaceID_Unknown, "Unexpected namespace ID!");
  return NS_GetContentList(&AsNode(), nameSpaceId, aLocalName);
}

already_AddRefed<nsContentList> DocumentOrShadowRoot::GetElementsByClassName(
    const nsAString& aClasses) {
  return nsContentUtils::GetElementsByClassName(&AsNode(), aClasses);
}

nsINode* DocumentOrShadowRoot::Retarget(nsINode* aNode) const {
  for (nsINode* cur = aNode; cur; cur = cur->GetContainingShadowHost()) {
    if (cur->SubtreeRoot() == &AsNode()) {
      return cur;
    }
  }
  return nullptr;
}

Element* DocumentOrShadowRoot::GetRetargetedFocusedElement() {
  auto* content = AsNode().OwnerDoc()->GetUnretargetedFocusedContent();
  if (!content) {
    return nullptr;
  }
  if (nsINode* retarget = Retarget(content)) {
    return retarget->AsElement();
  }
  return nullptr;
}

Element* DocumentOrShadowRoot::GetPointerLockElement() {
  nsCOMPtr<Element> pointerLockedElement =
      PointerLockManager::GetLockedElement();
  return Element::FromNodeOrNull(Retarget(pointerLockedElement));
}

Element* DocumentOrShadowRoot::GetFullscreenElement() const {
  if (!AsNode().IsInComposedDoc()) {
    return nullptr;
  }

  Element* element = AsNode().OwnerDoc()->GetUnretargetedFullscreenElement();
  NS_ASSERTION(!element || element->State().HasState(ElementState::FULLSCREEN),
               "Fullscreen element should have fullscreen styles applied");
  return Element::FromNodeOrNull(Retarget(element));
}

namespace {

using FrameForPointOption = nsLayoutUtils::FrameForPointOption;
using FrameForPointOptions = nsLayoutUtils::FrameForPointOptions;

// Whether only one node or multiple nodes is requested.
enum class Multiple {
  No,
  Yes,
};

// Whether we should flush layout or not.
enum class FlushLayout {
  No,
  Yes,
};

enum class PerformRetargeting {
  No,
  Yes,
};

template <typename NodeOrElement>
NodeOrElement* CastTo(nsINode*);

template <>
Element* CastTo<Element>(nsINode* aNode) {
  return aNode->AsElement();
}

template <>
nsINode* CastTo<nsINode>(nsINode* aNode) {
  return aNode;
}

template <typename NodeOrElement>
static void QueryNodesFromRect(DocumentOrShadowRoot& aRoot, const nsRect& aRect,
                               FrameForPointOptions aOptions,
                               FlushLayout aShouldFlushLayout,
                               Multiple aMultiple, ViewportType aViewportType,
                               PerformRetargeting aPerformRetargeting,
                               nsTArray<RefPtr<NodeOrElement>>& aNodes) {
  static_assert(std::is_same<nsINode, NodeOrElement>::value ||
                    std::is_same<Element, NodeOrElement>::value,
                "Should returning nodes or elements");

  constexpr bool returningElements =
      std::is_same<Element, NodeOrElement>::value;
  const bool retargeting = aPerformRetargeting == PerformRetargeting::Yes;

  nsCOMPtr<Document> doc = aRoot.AsNode().OwnerDoc();

  // Make sure the layout information we get is up-to-date, and
  // ensure we get a root frame (for everything but XUL)
  if (aShouldFlushLayout == FlushLayout::Yes) {
    doc->FlushPendingNotifications(FlushType::Layout);
  }

  PresShell* presShell = doc->GetPresShell();
  if (!presShell) {
    return;
  }

  nsIFrame* rootFrame = presShell->GetRootFrame();
  // XUL docs, unlike HTML, have no frame tree until everything's done loading
  if (!rootFrame) {
    return;  // return null to premature XUL callers as a reminder to wait
  }

  aOptions.mBits += FrameForPointOption::IgnorePaintSuppression;
  aOptions.mBits += FrameForPointOption::IgnoreCrossDoc;

  AutoTArray<nsIFrame*, 8> frames;
  nsLayoutUtils::GetFramesForArea({rootFrame, aViewportType}, aRect, frames,
                                  aOptions);

  for (nsIFrame* frame : frames) {
    nsINode* node = doc->GetContentInThisDocument(frame);
    while (node && node->IsInNativeAnonymousSubtree()) {
      nsIContent* root = node->GetClosestNativeAnonymousSubtreeRoot();
      MOZ_ASSERT(root, "content is connected");
      MOZ_ASSERT(root->IsRootOfNativeAnonymousSubtree(), "wat");
      if (root == &aRoot.AsNode()) {
        // If we're in the anonymous subtree root we care about, don't retarget.
        break;
      }
      node = root->GetParentOrShadowHostNode();
    }

    if (!node) {
      continue;
    }

    if (returningElements && !node->IsElement()) {
      // If this helper is called via ElementsFromPoint, we need to make sure
      // our frame is an element. Otherwise return whatever the top frame is
      // even if it isn't the top-painted element.
      // SVG 'text' element's SVGTextFrame doesn't respond to hit-testing, so
      // if 'content' is a child of such an element then we need to manually
      // defer to the parent here.
      if (aMultiple == Multiple::Yes && !frame->IsInSVGTextSubtree()) {
        continue;
      }

      node = node->GetParent();
      if (ShadowRoot* shadow = ShadowRoot::FromNodeOrNull(node)) {
        node = shadow->Host();
      }
    }

    // XXXsmaug There is plenty of unspec'ed behavior here
    //         https://github.com/w3c/webcomponents/issues/735
    //         https://github.com/w3c/webcomponents/issues/736
    if (retargeting) {
      node = aRoot.Retarget(node);
    }

    if (node && node != aNodes.SafeLastElement(nullptr)) {
      aNodes.AppendElement(CastTo<NodeOrElement>(node));
      if (aMultiple == Multiple::No) {
        return;
      }
    }
  }
}

template <typename NodeOrElement>
static void QueryNodesFromPoint(DocumentOrShadowRoot& aRoot, float aX, float aY,
                                FrameForPointOptions aOptions,
                                FlushLayout aShouldFlushLayout,
                                Multiple aMultiple, ViewportType aViewportType,
                                PerformRetargeting aPerformRetargeting,
                                nsTArray<RefPtr<NodeOrElement>>& aNodes) {
  // As per the spec, we return null if either coord is negative.
  if (!aOptions.mBits.contains(FrameForPointOption::IgnoreRootScrollFrame) &&
      (aX < 0 || aY < 0)) {
    return;
  }

  nscoord x = nsPresContext::CSSPixelsToAppUnits(aX);
  nscoord y = nsPresContext::CSSPixelsToAppUnits(aY);
  nsPoint pt(x, y);
  QueryNodesFromRect(aRoot, nsRect(pt, nsSize(1, 1)), aOptions,
                     aShouldFlushLayout, aMultiple, aViewportType,
                     aPerformRetargeting, aNodes);
}

}  // namespace

Element* DocumentOrShadowRoot::ElementFromPoint(float aX, float aY) {
  return ElementFromPointHelper(aX, aY, false, true, ViewportType::Layout);
}

void DocumentOrShadowRoot::ElementsFromPoint(
    float aX, float aY, nsTArray<RefPtr<Element>>& aElements) {
  QueryNodesFromPoint(*this, aX, aY, {}, FlushLayout::Yes, Multiple::Yes,
                      ViewportType::Layout, PerformRetargeting::Yes, aElements);
}

void DocumentOrShadowRoot::NodesFromPoint(float aX, float aY,
                                          nsTArray<RefPtr<nsINode>>& aNodes) {
  QueryNodesFromPoint(*this, aX, aY, {}, FlushLayout::Yes, Multiple::Yes,
                      ViewportType::Layout, PerformRetargeting::Yes, aNodes);
}

nsINode* DocumentOrShadowRoot::NodeFromPoint(float aX, float aY) {
  AutoTArray<RefPtr<nsINode>, 1> nodes;
  QueryNodesFromPoint(*this, aX, aY, {}, FlushLayout::Yes, Multiple::No,
                      ViewportType::Layout, PerformRetargeting::Yes, nodes);
  return nodes.SafeElementAt(0);
}

Element* DocumentOrShadowRoot::ElementFromPointHelper(
    float aX, float aY, bool aIgnoreRootScrollFrame, bool aFlushLayout,
    ViewportType aViewportType, bool aPerformRetargeting) {
  EnumSet<FrameForPointOption> options;
  if (aIgnoreRootScrollFrame) {
    options += FrameForPointOption::IgnoreRootScrollFrame;
  }

  auto flush = aFlushLayout ? FlushLayout::Yes : FlushLayout::No;
  auto performRetargeting =
      aPerformRetargeting ? PerformRetargeting::Yes : PerformRetargeting::No;

  AutoTArray<RefPtr<Element>, 1> elements;
  QueryNodesFromPoint(*this, aX, aY, options, flush, Multiple::No,
                      aViewportType, performRetargeting, elements);
  return elements.SafeElementAt(0);
}

void DocumentOrShadowRoot::NodesFromRect(float aX, float aY, float aTopSize,
                                         float aRightSize, float aBottomSize,
                                         float aLeftSize,
                                         bool aIgnoreRootScrollFrame,
                                         bool aFlushLayout, bool aOnlyVisible,
                                         float aVisibleThreshold,
                                         nsTArray<RefPtr<nsINode>>& aReturn) {
  // Following the same behavior of elementFromPoint,
  // we don't return anything if either coord is negative
  if (!aIgnoreRootScrollFrame && (aX < 0 || aY < 0)) {
    return;
  }

  nscoord x = nsPresContext::CSSPixelsToAppUnits(aX - aLeftSize);
  nscoord y = nsPresContext::CSSPixelsToAppUnits(aY - aTopSize);
  nscoord w = nsPresContext::CSSPixelsToAppUnits(aLeftSize + aRightSize) + 1;
  nscoord h = nsPresContext::CSSPixelsToAppUnits(aTopSize + aBottomSize) + 1;

  nsRect rect(x, y, w, h);

  FrameForPointOptions options;
  if (aIgnoreRootScrollFrame) {
    options.mBits += FrameForPointOption::IgnoreRootScrollFrame;
  }
  if (aOnlyVisible) {
    options.mBits += FrameForPointOption::OnlyVisible;
    options.mVisibleThreshold = aVisibleThreshold;
  }

  auto flush = aFlushLayout ? FlushLayout::Yes : FlushLayout::No;
  QueryNodesFromRect(*this, rect, options, flush, Multiple::Yes,
                     ViewportType::Layout, PerformRetargeting::No, aReturn);
}

Element* DocumentOrShadowRoot::AddIDTargetObserver(nsAtom* aID,
                                                   IDTargetObserver aObserver,
                                                   void* aData,
                                                   bool aForImage) {
  nsDependentAtomString id(aID);

  if (!CheckGetElementByIdArg(id)) {
    return nullptr;
  }

  IdentifierMapEntry* entry = mIdentifierMap.PutEntry(aID);
  NS_ENSURE_TRUE(entry, nullptr);

  entry->AddContentChangeCallback(aObserver, aData, aForImage);
  return aForImage ? entry->GetImageIdElement() : entry->GetIdElement();
}

void DocumentOrShadowRoot::RemoveIDTargetObserver(nsAtom* aID,
                                                  IDTargetObserver aObserver,
                                                  void* aData, bool aForImage) {
  nsDependentAtomString id(aID);

  if (!CheckGetElementByIdArg(id)) {
    return;
  }

  IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aID);
  if (!entry) {
    return;
  }

  entry->RemoveContentChangeCallback(aObserver, aData, aForImage);
}

Element* DocumentOrShadowRoot::LookupImageElement(nsAtom* aId) {
  if (aId->IsEmpty()) {
    return nullptr;
  }

  IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aId);
  return entry ? entry->GetImageIdElement() : nullptr;
}

void DocumentOrShadowRoot::ReportEmptyGetElementByIdArg() const {
  nsContentUtils::ReportEmptyGetElementByIdArg(AsNode().OwnerDoc());
}

void DocumentOrShadowRoot::GetAnimations(
    nsTArray<RefPtr<Animation>>& aAnimations) {
  // As with Element::GetAnimations we initially flush style here.
  // This should ensure that there are no subsequent changes to the tree
  // structure while iterating over the children below.
  if (Document* doc = AsNode().GetComposedDoc()) {
    doc->FlushPendingNotifications(
        ChangesToFlush(FlushType::Style, /* aFlushAnimations = */ false,
                       /* aUpdateRelevancy = */ false));
  }

  GetAnimationsOptions options;
  options.mSubtree = true;

  for (RefPtr<nsIContent> child = AsNode().GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (RefPtr<Element> element = Element::FromNode(child)) {
      nsTArray<RefPtr<Animation>> result;
      element->GetAnimationsWithoutFlush(options, result);
      aAnimations.AppendElements(std::move(result));
    }
  }

  aAnimations.Sort(AnimationPtrComparator<RefPtr<Animation>>());
}

struct SheetTreeOrderComparator {
  nsINode* mNode = nullptr;
  mutable nsContentUtils::NodeIndexCache mCache;

  int operator()(StyleSheet* aSheet) const {
    auto* sheetNode = aSheet->GetOwnerNode();
    MOZ_ASSERT(sheetNode != mNode, "Sheet already in the list?");
    if (!sheetNode) {
      // We go after all the link headers.
      return 1;
    }
    return nsContentUtils::CompareTreePosition<TreeKind::DOM>(mNode, sheetNode,
                                                              nullptr, &mCache);
  }
};

size_t DocumentOrShadowRoot::FindSheetInsertionPointInTree(
    const StyleSheet& aSheet) const {
  MOZ_ASSERT(!aSheet.IsConstructed());
  nsINode* owningNode = aSheet.GetOwnerNode();
  // We should always have an owning node unless Link: headers are at play.
  MOZ_ASSERT_IF(!owningNode, AsNode().IsDocument());
  MOZ_ASSERT_IF(owningNode, owningNode->SubtreeRoot() == &AsNode());
  if (mStyleSheets.IsEmpty()) {
    return 0;
  }

  if (!owningNode) {
    // We come from a link header, our position is after all the other link
    // headers, but before any link-owned node.
    size_t i = 0;
    for (const auto& sheet : mStyleSheets) {
      if (sheet->GetOwnerNode()) {
        break;
      }
      i++;
    }
    return i;
  }

  SheetTreeOrderComparator cmp{owningNode};
  if (cmp(mStyleSheets.LastElement()) > 0) {
    // Optimize for append.
    return mStyleSheets.Length();
  }
  size_t idx;
  BinarySearchIf(mStyleSheets, 0, mStyleSheets.Length(), cmp, &idx);
  return idx;
}

size_t DocumentOrShadowRoot::StyleOrderIndexOfSheet(
    const StyleSheet& aSheet) const {
  if (aSheet.IsConstructed()) {
    // NOTE: constructable sheets can have duplicates, so we need to start
    // looking from behind.
    size_t index = mAdoptedStyleSheets.LastIndexOf(&aSheet);
    return index == mAdoptedStyleSheets.NoIndex ? index : index + SheetCount();
  }
  return mStyleSheets.LastIndexOf(&aSheet);
}

void DocumentOrShadowRoot::TraverseSheetRefInStylesIfApplicable(
    StyleSheet& aSheet, nsCycleCollectionTraversalCallback& cb) {
  if (!aSheet.IsApplicable()) {
    return;
  }
  if (mKind == Kind::ShadowRoot) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mServoStyles->sheets[i]");
    cb.NoteXPCOMChild(&aSheet);
  } else if (AsNode().AsDocument()->StyleSetFilled()) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(
        cb, "mStyleSet->mRawSet.stylist.stylesheets.<origin>[i]");
    cb.NoteXPCOMChild(&aSheet);
  }
}

void DocumentOrShadowRoot::TraverseStyleSheets(
    nsTArray<RefPtr<StyleSheet>>& aSheets, const char* aEdgeName,
    nsCycleCollectionTraversalCallback& cb) {
  MOZ_ASSERT(aEdgeName);
  MOZ_ASSERT(&aSheets != &mAdoptedStyleSheets);
  for (StyleSheet* sheet : aSheets) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, aEdgeName);
    cb.NoteXPCOMChild(sheet);
    TraverseSheetRefInStylesIfApplicable(*sheet, cb);
  }
}

void DocumentOrShadowRoot::Traverse(DocumentOrShadowRoot* tmp,
                                    nsCycleCollectionTraversalCallback& cb) {
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDOMStyleSheets)
  tmp->TraverseStyleSheets(tmp->mStyleSheets, "mStyleSheets[i]", cb);

  tmp->EnumerateUniqueAdoptedStyleSheetsBackToFront([&](StyleSheet& aSheet) {
    tmp->TraverseSheetRefInStylesIfApplicable(aSheet, cb);
  });
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAdoptedStyleSheets);

  for (auto iter = tmp->mIdentifierMap.Iter(); !iter.Done(); iter.Next()) {
    iter.Get()->Traverse(&cb);
  }
}

void DocumentOrShadowRoot::UnlinkStyleSheets(
    nsTArray<RefPtr<StyleSheet>>& aSheets) {
  MOZ_ASSERT(&aSheets != &mAdoptedStyleSheets);
  for (StyleSheet* sheet : aSheets) {
    sheet->ClearAssociatedDocumentOrShadowRoot();
    RemoveSheetFromStylesIfApplicable(*sheet);
  }
  aSheets.Clear();
}

void DocumentOrShadowRoot::Unlink(DocumentOrShadowRoot* tmp) {
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDOMStyleSheets);
  tmp->UnlinkStyleSheets(tmp->mStyleSheets);
  tmp->EnumerateUniqueAdoptedStyleSheetsBackToFront([&](StyleSheet& aSheet) {
    aSheet.RemoveAdopter(*tmp);
    tmp->RemoveSheetFromStylesIfApplicable(aSheet);
  });
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAdoptedStyleSheets);
  tmp->mIdentifierMap.Clear();
}

}  // namespace mozilla::dom
