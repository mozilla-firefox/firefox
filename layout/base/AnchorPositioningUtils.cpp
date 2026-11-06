/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AnchorPositioningUtils.h"

#include "DisplayPortUtils.h"
#include "ScrollContainerFrame.h"
#include "mozilla/Maybe.h"
#include "mozilla/OverflowChangedTracker.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/DOMIntersectionObserver.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "nsCanvasFrame.h"
#include "nsContainerFrame.h"
#include "nsDisplayList.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsINode.h"
#include "nsLayoutUtils.h"
#include "nsPlaceholderFrame.h"
#include "nsStyleStruct.h"
#include "nsTArray.h"

namespace mozilla {

namespace {

bool IsScrolled(const nsIFrame* aFrame) {
  return aFrame->Style()->GetPseudoType() ==
         PseudoStyleType::MozScrolledContent;
}

bool DoTreeScopedPropertiesOfElementApplyToContent(
    const ScopedNameRef& aAnchorName, const nsIFrame* aReferencingFrame,
    const nsIFrame* aMaybeReferencedFrame) {
  const auto* referencingElement = aReferencingFrame->GetContent()->AsElement();

  const auto& referencingTreeScope =
      aReferencingFrame->StyleDisplay()->mAnchorName.scope;

  const auto* referencingShadowRoot =
      AnchorPositioningUtils::GetShadowRootForTreeScope(*referencingElement,
                                                        referencingTreeScope);

  const auto* maybeReferencedElement =
      aMaybeReferencedFrame->GetContent()->AsElement();
  const auto& maybeReferencedScope = aAnchorName.mTreeScope;

  const auto* maybeReferencedShadowRoot =
      AnchorPositioningUtils::GetShadowRootForTreeScope(*maybeReferencedElement,
                                                        maybeReferencedScope);
  const auto* currentShadowRoot = maybeReferencedShadowRoot;
  while (currentShadowRoot) {
    if (referencingShadowRoot == currentShadowRoot) {
      return true;
    }

    const auto* containingHost = currentShadowRoot->GetContainingShadowHost();
    if (!containingHost) {
      break;
    }
    currentShadowRoot = containingHost->GetContainingShadow();
  }

  // Original maybeReferencedShadowRoot, currentShadowRoot becomes eventually
  // null
  return !referencingShadowRoot && !maybeReferencedShadowRoot;
}

/**
 * Checks for the implementation of `anchor-scope`:
 * https://drafts.csswg.org/css-anchor-position-1/#anchor-scope
 *
 * TODO: Consider caching the ancestors, see bug 1986347
 */
bool IsAnchorInScopeForPositionedElement(const ScopedNameRef& aName,
                                         const nsIFrame* aPossibleAnchorFrame,
                                         const nsIFrame* aPositionedFrame) {
  // We don't need to look beyond positioned element's containing block.
  const auto* positionedContainingBlockContent =
      aPositionedFrame->GetParent()->GetContent();

  const auto* positionedElement = aPositionedFrame->GetContent()->AsElement();

  const auto& positionAnchorScope = aName.mTreeScope;

  const dom::ShadowRoot* positionAnchorShadowRoot =
      AnchorPositioningUtils::GetShadowRootForTreeScope(*positionedElement,
                                                        positionAnchorScope);

  auto getAnchorPosNearestScope =
      [&](const nsAtom* aName, const nsIFrame* aFrame,
          const dom::ShadowRoot* aShadowRoot) -> const nsIContent* {
    // We need to traverse the DOM, not the frame tree, since `anchor-scope`
    // may be present on elements with `display: contents` (in which case its
    // frame is in the `::before` list and won't be found by walking the frame
    // tree parent chain).
    for (nsIContent* cp = aFrame->GetContent();
         cp && cp != positionedContainingBlockContent;
         cp = cp->GetFlattenedTreeParentElementForStyle()) {
      const auto* anchorScope = [&]() -> const StyleScopedName* {
        const nsIFrame* f = nsLayoutUtils::GetStyleFrame(cp);
        if (MOZ_LIKELY(f)) {
          return &f->StyleDisplay()->mAnchorScope;
        }
        if (cp->AsElement()->IsDisplayContents()) {
          const auto* style =
              Servo_Element_GetMaybeOutOfDateStyle(cp->AsElement());
          MOZ_ASSERT(style);
          return &style->StyleDisplay()->mAnchorScope;
        }
        return nullptr;
      }();

      if (!anchorScope || anchorScope->value.IsEmpty()) {
        continue;
      }

      for (const StyleAtom& ident : anchorScope->value.AsSpan()) {
        if (aName == ident.AsAtom() || ident.AsAtom() == nsGkAtoms::all) {
          const dom::ShadowRoot* shadowRoot =
              Servo_GetShadowRootForScoped(cp->AsElement(), anchorScope->scope);
          if (shadowRoot == aShadowRoot) {
            return cp;
          }
        }
      }
    }
    return nullptr;
  };

  const auto& possibleAnchorName =
      aPossibleAnchorFrame->StyleDisplay()->mAnchorName;
  const dom::ShadowRoot* possibleAnchorShadowRoot =
      AnchorPositioningUtils::GetShadowRootForTreeScope(
          *aPossibleAnchorFrame->GetContent()->AsElement(),
          possibleAnchorName.scope);
  const auto* nearestScopeForAnchor = getAnchorPosNearestScope(
      aName.mName, aPossibleAnchorFrame, possibleAnchorShadowRoot);

  const auto* nearestScopeForPositioned = getAnchorPosNearestScope(
      aName.mName, aPositionedFrame, positionAnchorShadowRoot);
  if (!nearestScopeForAnchor) {
    // Anchor is not scoped and positioned element also should
    // not be gated by a scope.
    return !nearestScopeForPositioned ||
           aPossibleAnchorFrame->GetContent() == nearestScopeForPositioned;
  }

  // There may not be any other scopes between the positioned element
  // and the nearest scope of the anchor.
  return nearestScopeForAnchor == nearestScopeForPositioned;
};

bool IsFullyStyleableTreeAbidingOrNotPseudoElement(const nsIFrame* aFrame) {
  if (!aFrame->Style()->IsPseudoElement()) {
    return true;
  }

  const PseudoStyleType pseudoElementType = aFrame->Style()->GetPseudoType();

  // See https://www.w3.org/TR/css-pseudo-4/#treelike
  return pseudoElementType == PseudoStyleType::Before ||
         pseudoElementType == PseudoStyleType::After ||
         pseudoElementType == PseudoStyleType::Marker;
}

class TopLayerChecker {
  AutoTArray<dom::Element*, 1> mTopLayer;
  size_t mPositionedTopLayerIndex;

  static size_t GetIndex(const nsIFrame* aFrame,
                         const nsTArray<dom::Element*>& aTopLayers) {
    MOZ_ASSERT(aFrame);

    const nsIContent* frameContent = aFrame->GetContent();

    if (!frameContent) {
      return 0;
    }

    // Within the array returned by Document::GetTopLayer,
    // a higher index means the layer sits higher in the stack,
    // matching Document::GetTopLayerTop()’s top-to-bottom logic.
    // See https://drafts.csswg.org/css-position-4/#in-a-higher-top-layer

    for (size_t index = 0; index < aTopLayers.Length(); ++index) {
      const auto& topLayer = aTopLayers.ElementAt(index);
      if (nsContentUtils::ContentIsFlattenedTreeDescendantOfForStyle(
              /* aPossibleDescendant */ frameContent,
              /* aPossibleAncestor */ topLayer)) {
        return 1 + index;
      }
    }

    return 0;
  }

 public:
  size_t IndexFor(const nsIFrame* aFrame) const {
    return GetIndex(aFrame, mTopLayer);
  }

  explicit TopLayerChecker(const nsIFrame* aPositionedFrame)
      : mTopLayer{aPositionedFrame->GetContent()->OwnerDoc()->GetTopLayer()},
        mPositionedTopLayerIndex{GetIndex(aPositionedFrame, mTopLayer)} {}

  int32_t Compare(const nsIFrame* aPossibleAnchorFrame,
                  Maybe<size_t> aCachedAnchorTopLayerIndex) {
    const auto anchorTopLayerIndex = aCachedAnchorTopLayerIndex.valueOrFrom(
        [&]() { return GetIndex(aPossibleAnchorFrame, mTopLayer); });
    if (mPositionedTopLayerIndex == anchorTopLayerIndex) {
      return 0;
    }
    return anchorTopLayerIndex < mPositionedTopLayerIndex ? 1 : -1;
  }
};

bool IsInitialContainingBlock(const nsIFrame* aContainingBlock) {
  // Initial containing block: The containing block of the root element.
  // https://drafts.csswg.org/css-display-4/#initial-containing-block
  return aContainingBlock == aContainingBlock->PresShell()
                                 ->FrameConstructor()
                                 ->GetDocElementContainingBlock();
}

bool IsContainingBlockGeneratedByElement(const nsIFrame* aContainingBlock) {
  // 2.1. Containing Blocks of Positioned Boxes
  // https://www.w3.org/TR/css-position-3/#def-cb
  return !(!aContainingBlock || aContainingBlock->IsViewportFrame() ||
           IsInitialContainingBlock(aContainingBlock));
}

bool IsAnchorLaidOutStrictlyBeforeElement(
    const AnchorPosAnchorInfo& aPossibleAnchor,
    const nsIFrame* aPositionedFrame, uint32_t aPositionedFrameTreeDepth,
    const nsTArray<const nsIFrame*>& aPositionedFrameAncestors,
    TopLayerChecker& aTopLayerChecker,
    Maybe<size_t> aCachedAnchorTopLayerIndex) {
  // 1. positioned el is in a higher top layer than possible anchor,
  // see https://drafts.csswg.org/css-position-4/#in-a-higher-top-layer
  const auto topLayerResult = aTopLayerChecker.Compare(
      aPossibleAnchor.mAnchor, aCachedAnchorTopLayerIndex);

  if (topLayerResult != 0) {
    return topLayerResult > 0;
  }

  // Note: The containing block of an absolutely positioned element
  // is just the parent frame.
  const nsIFrame* positionedContainingBlock = aPositionedFrame->GetParent();
  // Note(dshin, bug 1985654): Spec strictly uses the term "containing block,"
  // corresponding to `GetContainingBlock()`. However, this leads to cases
  // where an anchor's non-inline containing block prevents it from being a
  // valid anchor for a absolutely positioned element (Which can explicitly
  // have inline elements as a containing block). Some WPT rely on inline
  // containing blocks as well.
  // See also: https://github.com/w3c/csswg-drafts/issues/12674
  const nsIFrame* anchorContainingBlock = aPossibleAnchor.mAnchor->GetParent();
  const auto* positionedContainingBlockFirstContinuation =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(
          positionedContainingBlock);

  // 2. Both elements are in the same top layer but have different
  // containing blocks and positioned el's containing block is an
  // ancestor of possible anchor's containing block in the containing
  // block chain, aka one of the following:
  if (nsLayoutUtils::FirstContinuationOrIBSplitSibling(anchorContainingBlock) !=
      positionedContainingBlockFirstContinuation) {
    // 2.1 positioned el's containing block is the viewport, and
    // possible anchor's containing block isn't.
    if (positionedContainingBlock->IsViewportFrame() &&
        !anchorContainingBlock->IsViewportFrame()) {
      return !nsLayoutUtils::IsProperAncestorFrame(aPositionedFrame,
                                                   aPossibleAnchor.mAnchor);
    }
    auto isLastContainingBlockOrderable = [&]() -> bool {
      const auto positionedContainingBlockFrameDepth =
          aPositionedFrameTreeDepth - 1;
      const nsIFrame* it = anchorContainingBlock;
      auto itFrameDepth = aPossibleAnchor.mFrameTreeDepth - 1;
      while (it) {
        const nsIFrame* parentContainingBlock = it->GetParent();
        if (!parentContainingBlock) {
          return false;
        }
        itFrameDepth--;
        if (itFrameDepth < positionedContainingBlockFrameDepth) {
          return false;
        }

        if (nsLayoutUtils::FirstContinuationOrIBSplitSibling(
                parentContainingBlock) ==
            positionedContainingBlockFirstContinuation) {
          return !it->IsAbsolutelyPositioned() ||
                 nsLayoutUtils::CompareTreePosition(it, aPositionedFrame,
                                                    aPositionedFrameAncestors,
                                                    nullptr) < 0;
        }

        it = parentContainingBlock;
      }

      return false;
    };

    // 2.2 positioned el's containing block is the initial containing
    // block, and possible anchor's containing block is generated by an
    // element, and the last containing block in possible anchor's containing
    // block chain before reaching positioned el's containing block is either
    // not absolutely positioned or precedes positioned el in the tree order,
    const bool isAnchorContainingBlockGenerated =
        IsContainingBlockGeneratedByElement(anchorContainingBlock);
    if (isAnchorContainingBlockGenerated &&
        IsInitialContainingBlock(positionedContainingBlock)) {
      return isLastContainingBlockOrderable();
    }

    // 2.3 both elements' containing blocks are generated by elements,
    // and positioned el's containing block is an ancestor in the flat
    // tree to that of possible anchor's containing block, and the last
    // containing block in possible anchor’s containing block chain before
    // reaching positioned el’s containing block is either not absolutely
    // positioned or precedes positioned el in the tree order.
    if (isAnchorContainingBlockGenerated &&
        IsContainingBlockGeneratedByElement(positionedContainingBlock)) {
      return isLastContainingBlockOrderable();
    }

    return false;
  }

  // 3. Both elements are in the same top layer and have the same
  // containing block, and are both absolutely positioned, and possible
  // anchor is earlier in flat tree order than positioned el.
  const bool isAnchorAbsolutelyPositioned =
      aPossibleAnchor.mAnchor->IsAbsolutelyPositioned();
  if (isAnchorAbsolutelyPositioned) {
    // We must have checked that the positioned element is absolutely
    // positioned by now.
    return nsLayoutUtils::CompareTreePosition(
               aPossibleAnchor.mAnchor, aPositionedFrame,
               aPositionedFrameAncestors, nullptr) < 0;
  }

  // 4. Both elements are in the same top layer and have the same
  // containing block, but possible anchor isn't absolutely positioned.
  return !isAnchorAbsolutelyPositioned;
}

/**
 * https://drafts.csswg.org/css-contain-2/#skips-its-contents
 */
bool IsPositionedElementAlsoSkippedWhenAnchorIsSkipped(
    const nsIFrame* aPossibleAnchorFrame, const nsIFrame* aPositionedFrame) {
  // If potential anchor is skipped and a root of a visibility subtree,
  // it can never be acceptable.
  if (aPossibleAnchorFrame->HidesContentForLayout()) {
    return false;
  }

  // If possible anchor is in the skipped contents of another element,
  // then positioned el shall be in the skipped contents of that same element.
  const nsIFrame* visibilityAncestor = aPossibleAnchorFrame->GetParent();
  while (visibilityAncestor) {
    // If anchor is skipped via auto or hidden, it cannot be acceptable,
    // be it a root or a non-root of a visibility subtree.
    if (visibilityAncestor->HidesContentForLayout()) {
      break;
    }

    visibilityAncestor = visibilityAncestor->GetParent();
  }

  // If positioned el is skipped and a root of a visibility subtree,
  // an anchor can never be acceptable.
  if (aPositionedFrame->HidesContentForLayout()) {
    return false;
  }

  const nsIFrame* ancestor = aPositionedFrame;
  while (ancestor) {
    if (ancestor->HidesContentForLayout()) {
      return ancestor == visibilityAncestor;
    }

    ancestor = ancestor->GetParent();
  }

  return true;
}

class LazyAncestorHolder {
  const nsIFrame* mFrame;
  AutoTArray<const nsIFrame*, 8> mAncestors;
  bool mFilled = false;

 public:
  const nsTArray<const nsIFrame*>& GetAncestors() {
    if (!mFilled) {
      nsLayoutUtils::FillAncestors(mFrame, nullptr, &mAncestors);
      mFilled = true;
    }
    return mAncestors;
  }

  explicit LazyAncestorHolder(const nsIFrame* aFrame) : mFrame(aFrame) {}
};

bool IsAcceptableAnchorElement(
    const AnchorPosAnchorInfo& aPossibleAnchor, const ScopedNameRef* aName,
    const nsIFrame* aPositionedFrame, uint32_t aPositionedFrameTreeDepth,
    LazyAncestorHolder& aPositionedFrameAncestorHolder,
    TopLayerChecker& aTopLayerChecker,
    Maybe<size_t> aCachedAnchorTopLayerIndex) {
  MOZ_ASSERT(aPossibleAnchor.mAnchor);
  MOZ_ASSERT(aPositionedFrame);

  // An element possible anchor is an acceptable anchor element for an
  // absolutely positioned element positioned el if all of the following are
  // true:
  // - possible anchor is either an element or a fully styleable
  // tree-abiding pseudo-element.
  // - possible anchor is in scope for positioned el, per the effects of
  // anchor-scope on positioned el or its ancestors.
  // - possible anchor is laid out strictly before positioned el
  //
  // Note: Frames having an anchor name contain elements.
  // The phrase "element or a fully styleable tree-abiding pseudo-element"
  // used by the spec is taken to mean
  // "either not a pseudo-element or a pseudo-element of a specific kind".
  if (!IsFullyStyleableTreeAbidingOrNotPseudoElement(aPossibleAnchor.mAnchor)) {
    return false;
  }
  if (!IsAnchorLaidOutStrictlyBeforeElement(
          aPossibleAnchor, aPositionedFrame, aPositionedFrameTreeDepth,
          aPositionedFrameAncestorHolder.GetAncestors(), aTopLayerChecker,
          aCachedAnchorTopLayerIndex)) {
    return false;
  }
  if (aName && !IsAnchorInScopeForPositionedElement(
                   *aName, aPossibleAnchor.mAnchor, aPositionedFrame)) {
    return false;
  }
  if (!IsPositionedElementAlsoSkippedWhenAnchorIsSkipped(
          aPossibleAnchor.mAnchor, aPositionedFrame)) {
    return false;
  }
  return true;
}

}  // namespace

AnchorPosAnchorInfo::AnchorPosAnchorInfo(nsIFrame* aAnchor)
    : mAnchor{aAnchor}, mFrameTreeDepth{aAnchor->GetDepthInFrameTree()} {}

AnchorPosReferenceData::Result AnchorPosReferenceData::InsertOrModify(
    const ScopedNameRef& aKey, const bool aNeedOffset) {
  MOZ_ASSERT(aKey.mName);
  bool exists = true;
  auto* result = &mMap.LookupOrInsertWith(aKey, [&exists]() {
    exists = false;
    return Nothing{};
  });

  if (!exists) {
    return {false, result};
  }

  // We tried to resolve before.
  if (result->isNothing()) {
    // We know this reference is invalid.
    return {true, result};
  }
  // Previous resolution found a valid anchor.
  if (!aNeedOffset) {
    // Size is guaranteed to be populated on resolution.
    return {true, result};
  }

  // Previous resolution may have been for size only, in which case another
  // anchor resolution is still required.
  return {result->ref().mOffsetData.isSome(), result};
}

const AnchorPosReferenceData::Value* AnchorPosReferenceData::Lookup(
    const ScopedNameRef& aKey) const {
  return mMap.Lookup(aKey).DataPtrOrNull();
}

void AnchorPosReferenceData::RememberScrollOffset() {
  if (mDefaultAnchorName) {
    mRememberedScrollOffset = Some(RememberedScrollOffset{
        mDefaultAnchorName, mAnchorTreeScope, AppliedScrollShift()});
    mDefaultScrollShift = nsPoint();
  }
}

void AnchorPosReferenceData::MaybeRememberScrollOffset() {
  if (!mRememberedScrollOffset) {
    RememberScrollOffset();
  }
}

void AnchorPosReferenceData::
    ForgetRememberedScrollOffsetIfDefaultAnchorChanged() {
  if (mRememberedScrollOffset &&
      (mRememberedScrollOffset->mAnchorName != mDefaultAnchorName ||
       mRememberedScrollOffset->mAnchorTreeScope != mAnchorTreeScope)) {
    // Per spec, "if abspos has a default anchor element, it always calculates a
    // remembered scroll offset for it, even if abspos doesn't actually have an
    // anchor reference to it." Even though changing the default anchor is not
    // an anchor recalculation point, we clear the old remembered scroll offset
    // so that we'll remember one for the new default anchor.
    // https://drafts.csswg.org/css-anchor-position-1/#anchor-recalculation-point
    mRememberedScrollOffset.reset();
  }
}

nsPoint AnchorPosReferenceData::AppliedScrollShift() const {
  return mRememberedScrollOffset
             ? mRememberedScrollOffset->mOffset + mDefaultScrollShift
             : mDefaultScrollShift;
}

void AnchorPosReferenceData::Reset() {
  auto rememberedScrollOffset = std::move(mRememberedScrollOffset);
  *this = AnchorPosReferenceData();
  mRememberedScrollOffset = std::move(rememberedScrollOffset);
}

AnchorPosDefaultAnchorCache::AnchorPosDefaultAnchorCache(
    const nsIFrame* aAnchor, const nsIFrame* aScrollContainer)
    : mAnchor{aAnchor}, mScrollContainer{aScrollContainer} {
  MOZ_ASSERT_IF(
      aAnchor,
      nsLayoutUtils::GetNearestScrollContainerFrame(
          const_cast<nsContainerFrame*>(aAnchor->GetParent()),
          nsLayoutUtils::SCROLLABLE_SAME_DOC |
              nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN) == mScrollContainer);
}

nsIFrame* AnchorPositioningUtils::FindFirstAcceptableAnchor(
    const ScopedNameRef& aName, const nsIFrame* aPositionedFrame,
    const nsTArray<AnchorPosAnchorInfo>& aPossibleAnchorFrames,
    uint32_t aPositionedFrameTreeDepth, nsTArray<size_t>* aTopLayerIndexCache) {
  LazyAncestorHolder positionedFrameAncestorHolder(aPositionedFrame);
  TopLayerChecker topLayerHolder{aPositionedFrame};

  for (auto it = aPossibleAnchorFrames.rbegin();
       it != aPossibleAnchorFrames.rend(); ++it) {
    nsIFrame* possibleAnchorFrame = it->mAnchor;
    const auto anchorTopLayerIndex = [&]() -> Maybe<size_t> {
      // Instead of pushing to front, work with the index cache in the reverse
      // order.
      const auto i = std::distance(it, aPossibleAnchorFrames.rend());
      MOZ_ASSERT(i >= 0);
      if (!aTopLayerIndexCache) {
        return Nothing{};
      }
      if (static_cast<size_t>(i) >= aTopLayerIndexCache->Length()) {
        const auto topLayer = topLayerHolder.IndexFor(possibleAnchorFrame);
        aTopLayerIndexCache->AppendElement(topLayer);
        return Some(topLayer);
      }

      return Some((*aTopLayerIndexCache)[i]);
    }();
    if (!DoTreeScopedPropertiesOfElementApplyToContent(
            aName, possibleAnchorFrame, aPositionedFrame)) {
      // Skip anchors in different shadow trees.
      continue;
    }

    MOZ_ASSERT(aPositionedFrame->GetContent()->OwnerDoc() ==
                   possibleAnchorFrame->GetContent()->OwnerDoc(),
               "Anchor and positiond frames in different documents?");
    // Check if the possible anchor is an acceptable anchor element.
    if (IsAcceptableAnchorElement(*it, &aName, aPositionedFrame,
                                  aPositionedFrameTreeDepth,
                                  positionedFrameAncestorHolder, topLayerHolder,
                                  anchorTopLayerIndex)) {
      return possibleAnchorFrame;
    }
  }

  // If we reach here, we didn't find any acceptable anchor.
  return nullptr;
}

static const nsIFrame* GetAnchorOf(const nsIFrame* aPositioned,
                                   const ScopedNameRef& aAnchorName,
                                   uint32_t aFrameTreeDepth) {
  const auto* presShell = aPositioned->PresShell();
  MOZ_ASSERT(presShell, "No PresShell for frame?");
  return presShell->GetAnchorPosAnchor(aAnchorName, aPositioned,
                                       aFrameTreeDepth);
}

Maybe<nsRect> AnchorPositioningUtils::GetAnchorPosRect(
    const nsIFrame* aAbsoluteContainingBlock, const nsIFrame* aAnchor,
    bool aCBRectIsValid) {
  auto rect = [&]() -> Maybe<nsRect> {
    if (aCBRectIsValid) {
      return Some(ReassembleAnchorRect(aAnchor, aAbsoluteContainingBlock));
    }

    // Ok, containing block doesn't have its rect fully resolved. Compute the
    // anchor rect in aAbsoluteContainingBlock's coordinate space.
    // TODO(dshin): This wouldn't handle anchor in a previous top layer.
    if (!nsLayoutUtils::IsProperAncestorFrameConsideringContinuations(
            aAbsoluteContainingBlock, aAnchor)) {
      return Nothing{};
    }
    return Some(GetCombinedFragmentRects(aAnchor, aAbsoluteContainingBlock,
                                         UnionFragments::All,
                                         ApplyTransform::Yes)
                    .mRect);
  }();
  return rect.map([&](const nsRect& aRect) {
    // We need to position the border box of the anchor within the abspos
    // containing block's size - So the rectangle's size (i.e. Anchor size)
    // stays the same, while "the outer rectangle" (i.e. The abspos cb size)
    // "shrinks" by shifting the position.
    const auto border = aAbsoluteContainingBlock->GetUsedBorder();
    const nsPoint borderTopLeft{border.left, border.top};
    const auto rect = aRect - borderTopLeft;
    return rect;
  });
}

Maybe<AnchorPosInfo> AnchorPositioningUtils::ResolveAnchorPosRect(
    const nsIFrame* aPositioned, const nsIFrame* aAbsoluteContainingBlock,
    const ScopedNameRef& aAnchorName, bool aCBRectIsValid,
    AnchorPosResolutionCache* aResolutionCache) {
  if (!aPositioned) {
    return Nothing{};
  }

  if (!aPositioned->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    return Nothing{};
  }

  MOZ_ASSERT(aPositioned->GetParent() == aAbsoluteContainingBlock);

  const auto anchorName = GetUsedAnchorName(aPositioned, aAnchorName);
  if (!anchorName) {
    return Nothing{};
  }

  Maybe<AnchorPosResolutionData>* entry = nullptr;
  if (aResolutionCache) {
    const auto result =
        aResolutionCache->mReferenceData->InsertOrModify(*anchorName, true);
    if (result.mAlreadyResolved) {
      MOZ_ASSERT(result.mEntry, "Entry exists but null?");
      return result.mEntry->map([&](const AnchorPosResolutionData& aData) {
        MOZ_ASSERT(aData.mOffsetData, "Missing anchor offset resolution.");
        const auto& offsetData = aData.mOffsetData.ref();
        return AnchorPosInfo{nsRect{offsetData.mOrigin, aData.mSize},
                             offsetData.mCompensatesForScroll};
      });
    }
    entry = result.mEntry;
  }
  uint32_t frameTreeDepth =
      !aResolutionCache || !aResolutionCache->mReferenceData
          ? aPositioned->GetDepthInFrameTree()
          : aResolutionCache->mReferenceData->mFrameTreeDepth;

  const auto* anchor = GetAnchorOf(aPositioned, *anchorName, frameTreeDepth);
  if (!anchor) {
    // If we have a cached entry, just check that it resolved to nothing last
    // time as well.
    MOZ_ASSERT_IF(entry, entry->isNothing());
    return Nothing{};
  }

  const auto result =
      GetAnchorPosRect(aAbsoluteContainingBlock, anchor, aCBRectIsValid);
  return result.map([&](const nsRect& aRect) {
    bool compensatesForScroll = false;
    DistanceToNearestScrollContainer distanceToNearestScrollContainer;
    if (aResolutionCache) {
      MOZ_ASSERT(entry);
      // Update the cache.
      compensatesForScroll = [&]() {
        auto& defaultAnchorCache = aResolutionCache->mDefaultAnchorCache;
        if (!aAnchorName.mName) {
          // Explicitly resolved default anchor for the first time - populate
          // the cache.
          defaultAnchorCache.mAnchor = anchor;
          const auto [scrollContainer, distance] =
              AnchorPositioningUtils::GetNearestScrollFrame(anchor);
          distanceToNearestScrollContainer = distance;
          defaultAnchorCache.mScrollContainer = scrollContainer;
          aResolutionCache->mReferenceData->mDistanceToDefaultScrollContainer =
              distance;
          aResolutionCache->mReferenceData->mDefaultAnchorName =
              anchorName->mName;
          aResolutionCache->mReferenceData->mAnchorTreeScope =
              anchorName->mTreeScope;
          // This is the default anchor, so scroll compensated by definition.
          return true;
        }
        if (defaultAnchorCache.mAnchor == anchor) {
          // This is referring to the default anchor, so scroll compensated by
          // definition.
          return true;
        }
        const auto [scrollContainer, distance] =
            AnchorPositioningUtils::GetNearestScrollFrame(anchor);
        distanceToNearestScrollContainer = distance;
        return scrollContainer ==
               aResolutionCache->mDefaultAnchorCache.mScrollContainer;
      }();
      // If a partially resolved entry exists, make sure that it matches what we
      // have now.
      MOZ_ASSERT_IF(*entry, entry->ref().mSize == aRect.Size());
      *entry = Some(AnchorPosResolutionData{
          aRect.Size(),
          Some(AnchorPosOffsetData{aRect.TopLeft(), compensatesForScroll,
                                   distanceToNearestScrollContainer}),
          aAnchorName.mTreeScope});
    }
    return AnchorPosInfo{aRect, compensatesForScroll};
  });
}

Maybe<nsSize> AnchorPositioningUtils::ResolveAnchorPosSize(
    const nsIFrame* aPositioned, const nsIFrame* aAbsoluteContainingBlock,
    const ScopedNameRef& aAnchorName,
    AnchorPosResolutionCache* aResolutionCache) {
  auto anchorName = GetUsedAnchorName(aPositioned, aAnchorName);
  if (!anchorName) {
    return Nothing{};
  }
  Maybe<AnchorPosResolutionData>* entry = nullptr;
  auto* referencedAnchors =
      aResolutionCache ? aResolutionCache->mReferenceData : nullptr;
  if (referencedAnchors) {
    const auto result = referencedAnchors->InsertOrModify(*anchorName, false);
    if (result.mAlreadyResolved) {
      MOZ_ASSERT(result.mEntry, "Entry exists but null?");
      return result.mEntry->map(
          [](const AnchorPosResolutionData& aData) { return aData.mSize; });
    }
    entry = result.mEntry;
  }
  uint32_t frameTreeDepth = referencedAnchors
                                ? referencedAnchors->mFrameTreeDepth
                                : aPositioned->GetDepthInFrameTree();
  const auto* anchor = GetAnchorOf(aPositioned, *anchorName, frameTreeDepth);
  if (!anchor) {
    return Nothing{};
  }
  const auto size =
      GetCombinedFragmentRects(anchor, aAbsoluteContainingBlock,
                               UnionFragments::All, ApplyTransform::Yes)
          .mRect.Size();
  if (entry) {
    *entry =
        Some(AnchorPosResolutionData{size, Nothing{}, aAnchorName.mTreeScope});
  }
  return Some(size);
}

/**
 * Returns an equivalent StylePositionArea that contains:
 * [
 *   [ left | center | right | span-left | span-right | span-all]
 *   [ top | center | bottom | span-top | span-bottom | span-all]
 * ]
 */
static StylePositionArea ToPhysicalPositionArea(StylePositionArea aPosArea,
                                                WritingMode aCbWM,
                                                WritingMode aPosWM) {
  StyleWritingMode cbwm{aCbWM.GetBits()};
  StyleWritingMode wm{aPosWM.GetBits()};
  Servo_PhysicalizePositionArea(&aPosArea, &cbwm, &wm);
  return aPosArea;
}

StylePositionArea AnchorPositioningUtils::PhysicalizePositionArea(
    StylePositionArea aPosArea, const nsIFrame* aPositioned) {
  return ToPhysicalPositionArea(aPosArea,
                                aPositioned->GetParent()->GetWritingMode(),
                                aPositioned->GetWritingMode());
}

nsRect AnchorPositioningUtils::AdjustAbsoluteContainingBlockRectForPositionArea(
    const nsRect& aAnchorRect, const nsRect& aCBRect, WritingMode aPositionedWM,
    WritingMode aCBWM, const StylePositionArea& aPosArea,
    StylePositionArea* aOutResolvedArea) {
  // Get the boundaries of 3x3 grid in CB's frame space. The edges of the
  // default anchor box are clamped to the bounds of the CB, even if that
  // results in zero width/height cells.
  //
  //          ltrEdges[0]  ltrEdges[1]  ltrEdges[2]  ltrEdges[3]
  //              |            |            |            |
  // ttbEdges[0]  +------------+------------+------------+
  //              |            |            |            |
  // ttbEdges[1]  +------------+------------+------------+
  //              |            |            |            |
  // ttbEdges[2]  +------------+------------+------------+
  //              |            |            |            |
  // ttbEdges[3]  +------------+------------+------------+

  const nsRect gridRect = aCBRect.Union(aAnchorRect);
  nscoord ltrEdges[4] = {gridRect.x, aAnchorRect.x,
                         aAnchorRect.x + aAnchorRect.width,
                         gridRect.x + gridRect.width};
  nscoord ttbEdges[4] = {gridRect.y, aAnchorRect.y,
                         aAnchorRect.y + aAnchorRect.height,
                         gridRect.y + gridRect.height};
  ltrEdges[1] = std::clamp(ltrEdges[1], ltrEdges[0], ltrEdges[3]);
  ltrEdges[2] = std::clamp(ltrEdges[2], ltrEdges[0], ltrEdges[3]);
  ttbEdges[1] = std::clamp(ttbEdges[1], ttbEdges[0], ttbEdges[3]);
  ttbEdges[2] = std::clamp(ttbEdges[2], ttbEdges[0], ttbEdges[3]);

  nsRect res = gridRect;

  // PositionArea, resolved to only contain Left/Right/Top/Bottom values.
  StylePositionArea posArea =
      ToPhysicalPositionArea(aPosArea, aCBWM, aPositionedWM);
  *aOutResolvedArea = posArea;

  nscoord right = ltrEdges[3];
  if (posArea.first == StylePositionAreaKeyword::Left) {
    right = ltrEdges[1];
  } else if (posArea.first == StylePositionAreaKeyword::SpanLeft) {
    right = ltrEdges[2];
  } else if (posArea.first == StylePositionAreaKeyword::Center) {
    res.x = ltrEdges[1];
    right = ltrEdges[2];
  } else if (posArea.first == StylePositionAreaKeyword::SpanRight) {
    res.x = ltrEdges[1];
  } else if (posArea.first == StylePositionAreaKeyword::Right) {
    res.x = ltrEdges[2];
  } else if (posArea.first == StylePositionAreaKeyword::SpanAll) {
    // no adjustment
  } else {
    MOZ_ASSERT_UNREACHABLE("Bad value from ToPhysicalPositionArea");
  }
  res.width = right - res.x;

  nscoord bottom = ttbEdges[3];
  if (posArea.second == StylePositionAreaKeyword::Top) {
    bottom = ttbEdges[1];
  } else if (posArea.second == StylePositionAreaKeyword::SpanTop) {
    bottom = ttbEdges[2];
  } else if (posArea.second == StylePositionAreaKeyword::Center) {
    res.y = ttbEdges[1];
    bottom = ttbEdges[2];
  } else if (posArea.second == StylePositionAreaKeyword::SpanBottom) {
    res.y = ttbEdges[1];
  } else if (posArea.second == StylePositionAreaKeyword::Bottom) {
    res.y = ttbEdges[2];
  } else if (posArea.second == StylePositionAreaKeyword::SpanAll) {
    // no adjustment
  } else {
    MOZ_ASSERT_UNREACHABLE("Bad value from ToPhysicalPositionArea");
  }
  res.height = bottom - res.y;

  return res;
}

AnchorPositioningUtils::NearestScrollFrameInfo
AnchorPositioningUtils::GetNearestScrollFrame(const nsIFrame* aFrame) {
  if (!aFrame) {
    return {nullptr, {}};
  }
  uint32_t distance = 1;
  // `GetNearestScrollContainerFrame` will return the incoming frame if it's a
  // scroll frame, so nudge to parent.
  for (const nsIFrame* f = aFrame->GetParent(); f; f = f->GetParent()) {
    if (f->IsScrollContainerOrSubclass()) {
      return {f, DistanceToNearestScrollContainer{distance}};
    }
    distance++;
  }
  return {nullptr, {}};
}

nsPoint AnchorPositioningUtils::GetScrollOffsetFor(
    PhysicalAxes aAxes, const nsIFrame* aPositioned,
    const AnchorPosDefaultAnchorCache& aDefaultAnchorCache) {
  MOZ_ASSERT(aPositioned);
  const nsIFrame* anchor = aDefaultAnchorCache.mAnchor;
  if (!anchor || aAxes.isEmpty()) {
    return nsPoint{};
  }

  // aPositioned was placed against the anchor's scroll-ignored position, so the
  // scroll offset is how far the anchor has since scrolled relative to the
  // absolute containing block.
  const auto* absCB = aPositioned->GetParent();
  const nsPoint offset = anchor->GetScrollOffsetTo(absCB);
  return nsPoint(aAxes.contains(PhysicalAxis::Horizontal) ? offset.x : 0,
                 aAxes.contains(PhysicalAxis::Vertical) ? offset.y : 0);
}

// Out of line to avoid having to include AnchorPosReferenceData from nsIFrame.h
void DeleteAnchorPosReferenceData(AnchorPosReferenceData* aData) {
  delete aData;
}

void DeleteLastSuccessfulPositionData(LastSuccessfulPositionData* aData) {
  delete aData;
}

Maybe<ScopedNameRef> AnchorPositioningUtils::GetUsedAnchorName(
    const nsIFrame* aPositioned, const ScopedNameRef& aAnchorName) {
  if (aAnchorName.mName && !aAnchorName.mName->IsEmpty()) {
    return Some(aAnchorName);
  }

  const auto* stylePosition = aPositioned->StylePosition();
  if (!stylePosition->CanHaveDefaultAnchor()) {
    return Nothing{};
  }

  const auto& defaultAnchor = stylePosition->mPositionAnchor;
  if (defaultAnchor.value.IsIdent()) {
    return Some(ScopedNameRef(defaultAnchor.value.AsIdent().AsAtom(),
                              defaultAnchor.scope));
  }

  MOZ_ASSERT(defaultAnchor.value.IsNormal() || defaultAnchor.value.IsAuto());

  if (aPositioned->Style()->IsPseudoElement()) {
    return Some(ScopedNameRef(nsGkAtoms::AnchorPosImplicitAnchor,
                              StyleCascadeLevel::Default()));
  }

  if (const nsIContent* content = aPositioned->GetContent()) {
    if (const auto* element = nsGenericHTMLElement::FromNode(content)) {
      if (element->GetPopoverAttributeState() !=
          dom::PopoverAttributeState::None) {
        return Some(ScopedNameRef(nsGkAtoms::AnchorPosImplicitAnchor,
                                  StyleCascadeLevel::Default()));
      }
    }
  }

  return Nothing{};
}

static std::pair<nsIContent*, AnchorPositioningUtils::ImplicitAnchorKind>
GetImplicitAnchorContent(const nsIFrame* aFrame) {
  const auto* element = dom::Element::FromNodeOrNull(aFrame->GetContent());
  if (!element) [[unlikely]] {
    return {};
  }
  if (const auto* popoverData = element->GetPopoverData()) [[unlikely]] {
    if (RefPtr invoker = popoverData->GetInvoker()) {
      return {invoker.get(),
              AnchorPositioningUtils::ImplicitAnchorKind::Popover};
    }
  }
  if (!aFrame->Style()->IsPseudoElement()) {
    return {};
  }
  return {element->GetClosestNativeAnonymousSubtreeRootParentOrHost(),
          AnchorPositioningUtils::ImplicitAnchorKind::PseudoElement};
}

auto AnchorPositioningUtils::GetAnchorPosImplicitAnchor(const nsIFrame* aFrame)
    -> ImplicitAnchorResult {
  auto [implicitAnchor, kind] = GetImplicitAnchorContent(aFrame);
  if (!implicitAnchor) {
    return {};
  }
  auto* anchorFrame = implicitAnchor->GetPrimaryFrame();
  if (!anchorFrame) {
    return {};
  }
  LazyAncestorHolder ancestorHolder(aFrame);
  TopLayerChecker topLayerHolder{aFrame};
  if (!IsAcceptableAnchorElement(AnchorPosAnchorInfo{anchorFrame},
                                 /* aName = */ nullptr, aFrame,
                                 aFrame->GetDepthInFrameTree(), ancestorHolder,
                                 topLayerHolder, Nothing{})) {
    return {};
  }
  return {anchorFrame, kind};
}

AnchorPositioningUtils::ContainingBlockInfo
AnchorPositioningUtils::ContainingBlockInfo::ExplicitCBFrameSize(
    const nsRect& aContainingBlockRect) {
  // TODO(dshin, bug 1989292): Ideally, this takes both local containing rect +
  // scrollable containing rect, and one is picked here.
  return ContainingBlockInfo{aContainingBlockRect};
}

AnchorPositioningUtils::ContainingBlockInfo
AnchorPositioningUtils::ContainingBlockInfo::UseCBFrameSize(
    const nsIFrame* aPositioned) {
  // TODO(dshin, bug 1989292): This just gets local containing block.
  const auto* cb = aPositioned->GetParent();
  MOZ_ASSERT(cb);
  if (IsScrolled(cb)) {
    cb = aPositioned->GetParent();
  }
  return ContainingBlockInfo{cb->GetPaddingRectRelativeToSelf()};
}

bool AnchorPositioningUtils::FitsInContainingBlock(
    const nsIFrame* aPositioned, const AnchorPosReferenceData& aReferenceData) {
  MOZ_ASSERT(aPositioned->FirstInFlow()->GetProperty(
                 nsIFrame::AnchorPosReferences()) == &aReferenceData);

  const nsPoint scrollShift = aReferenceData.AppliedScrollShift();
  const auto scrollCompensatedSides = aReferenceData.mScrollCompensatedSides;
  nsSize checkSize = [&]() {
    const auto& adjustedCB = aReferenceData.mAdjustedContainingBlock;
    if (scrollShift == nsPoint{} || scrollCompensatedSides == SideBits::eNone) {
      return adjustedCB.Size();
    }

    // We now know that this frame's anchor has moved in relation to
    // the original containing block, and that at least one side of our
    // IMCB is attached to it.

    // Scroll shift the adjusted containing block.
    const auto shifted = aReferenceData.mAdjustedContainingBlock - scrollShift;
    const auto& originalCB = aReferenceData.mOriginalContainingBlockRect;

    // Now, move edges that are not attached to the anchors and pin it
    // to the original containing block.
    const nsPoint pt{
        scrollCompensatedSides & SideBits::eLeft ? shifted.X() : originalCB.X(),
        scrollCompensatedSides & SideBits::eTop ? shifted.Y() : originalCB.Y()};
    const nsPoint ptMost{
        scrollCompensatedSides & SideBits::eRight ? shifted.XMost()
                                                  : originalCB.XMost(),
        scrollCompensatedSides & SideBits::eBottom ? shifted.YMost()
                                                   : originalCB.YMost()};

    return nsSize{ptMost.x - pt.x, ptMost.y - pt.y};
  }();

  // Finally, reduce by inset.
  checkSize -= nsSize{aReferenceData.mInsets.LeftRight(),
                      aReferenceData.mInsets.TopBottom()};

  return aPositioned->GetMarginRectRelativeToSelf().Size() <= checkSize;
}

nsIFrame* AnchorPositioningUtils::GetAnchorThatFrameScrollsWith(
    nsIFrame* aFrame, nsDisplayListBuilder* aBuilder,
    bool aSkipAsserts /* = false */) {
#ifdef DEBUG
  if (!aSkipAsserts) {
    MOZ_ASSERT(!aBuilder || aBuilder->IsPaintingToWindow());
    MOZ_ASSERT_IF(!aBuilder, aFrame->PresContext()->LayoutPhaseCount(
                                 nsLayoutPhase::DisplayListBuilding) == 0);
  }
#endif

  if (!StaticPrefs::apz_async_scroll_css_anchor_pos_AtStartup()) {
    return nullptr;
  }
  PhysicalAxes axes = aFrame->GetAnchorPosCompensatingForScroll();
  if (axes.isEmpty()) {
    return nullptr;
  }

  const auto* pos = aFrame->StylePosition();
  if (!pos->mPositionAnchor.value.IsIdent()) {
    return nullptr;
  }

  const nsAtom* defaultAnchorName =
      pos->mPositionAnchor.value.AsIdent().AsAtom();
  StyleCascadeLevel anchorTreeScope = pos->mPositionAnchor.scope;
  nsIFrame* anchor =
      const_cast<nsIFrame*>(aFrame->PresShell()->GetAnchorPosAnchor(
          {defaultAnchorName, anchorTreeScope}, aFrame,
          aFrame->GetDepthInFrameTree()));
  // TODO Bug 1997026 We need to update the anchor finding code so this can't
  // happen. For now we just detect it and reject it.
  if (anchor && !nsLayoutUtils::IsProperAncestorFrameConsideringContinuations(
                    aFrame->GetParent(), anchor)) {
    return nullptr;
  }
  if (!aBuilder) {
    return anchor;
  }
  // TODO for now ShouldAsyncScrollWithAnchor will return false if we are
  // compensating in only one axis and there is a scroll frame between the
  // anchor and the positioned's containing block that can scroll in the "wrong"
  // axis so that we don't async scroll in the wrong axis because ASRs/APZ only
  // support scrolling in both axes. This is not fully spec compliant, bug
  // 1988034 tracks this.
  return DisplayPortUtils::ShouldAsyncScrollWithAnchor(aFrame, anchor, aBuilder,
                                                       axes)
             ? anchor
             : nullptr;
}

using AffectedAnchor = AnchorPosDefaultAnchorCache;
using AppliedShifts = nsTHashMap<nsIFrame*, nsPoint>;
struct ScrollShifts {
  nsPoint mScrollCompensatedDelta;
  nsPoint mChainedDelta;

  nsPoint Sum() const { return mChainedDelta + mScrollCompensatedDelta; }
};

static ScrollShifts FindScrollCompensatedAnchorShift(
    const PresShell* aPresShell, const nsIFrame* aPositioned,
    const AnchorPosReferenceData& aReferenceData,
    const AppliedShifts& aAppliedShifts,
    AnchorPosAnchorTopLayerIndexCache& aTopLayerIndexCache) {
  MOZ_ASSERT(aPositioned->IsAbsolutelyPositioned(),
             "Anchor positioned frame is not absolutely positioned?");
  const auto* defaultAnchorName = aReferenceData.mDefaultAnchorName.get();
  if (!defaultAnchorName) {
    return {};
  }
  const StyleCascadeLevel& anchorTreeScope = aReferenceData.mAnchorTreeScope;
  auto* defaultAnchor = aPresShell->GetAnchorPosAnchor(
      {defaultAnchorName, anchorTreeScope}, aPositioned,
      aReferenceData.mFrameTreeDepth, &aTopLayerIndexCache);
  if (!defaultAnchor) {
    return {};
  }
  // HACK(dshin, Bug 1999954): This is a workaround. While we try to lay out
  // against the scroll-ignored position of an anchor, chain anchored frames
  // end up containing scroll offset in their position. For now, walk the chain
  // to account for those deltas too.
  const nsPoint chainedDelta = [&]() -> nsPoint {
    if (!defaultAnchor->StylePosition()->CanHaveDefaultAnchor()) {
      return {};
    }
    const auto* referenceData =
        defaultAnchor->GetProperty(nsIFrame::AnchorPosReferences());
    if (!referenceData) {
      return {};
    }
    if (auto delta = aAppliedShifts.Lookup(defaultAnchor)) {
      // If we've gone through this anchor already, grab the delta we've
      // applied already (if any), since otherwise
      // FindScrollCompensatedAnchorShift will end up being zero anyways.
      return *delta;
    }
    return FindScrollCompensatedAnchorShift(aPresShell, defaultAnchor,
                                            *referenceData, aAppliedShifts,
                                            aTopLayerIndexCache)
        .Sum();
  }();

  const nsPoint scrollCompensatedDelta = [&]() -> nsPoint {
    const auto compensatingForScroll =
        aReferenceData.CompensatingForScrollAxes();
    if (compensatingForScroll.isEmpty()) {
      return {};
    }
    const auto* scrollContainer =
        AnchorPositioningUtils::GetNearestScrollFrame(defaultAnchor)
            .mScrollContainer;
    if (!scrollContainer) {
      return nsPoint();
    }
    const auto offset = AnchorPositioningUtils::GetScrollOffsetFor(
        compensatingForScroll, aPositioned,
        AffectedAnchor{defaultAnchor, scrollContainer});
    return offset - aReferenceData.AppliedScrollShift();
  }();
  return {scrollCompensatedDelta, chainedDelta};
}

// https://drafts.csswg.org/css-anchor-position-1/#default-scroll-shift
static void UpdateScrollShift(
    PresShell* aPresShell, nsIFrame* aPositioned,
    AnchorPosReferenceData& aReferenceData, OverflowChangedTracker& aOct,
    AppliedShifts& aAppliedShifts,
    AnchorPosAnchorTopLayerIndexCache& aTopLayerIndexCache) {
  const auto scrollShifts =
      FindScrollCompensatedAnchorShift(aPresShell, aPositioned, aReferenceData,
                                       aAppliedShifts, aTopLayerIndexCache);
  auto delta = scrollShifts.Sum();
  if (delta == nsPoint()) {
    aReferenceData.MaybeRememberScrollOffset();
    return;
  }
  aAppliedShifts.InsertOrUpdate(aPositioned, delta);
  // APZ-handled scrolling may skip scheduling of paint for the relevant
  // scroll container - We need to ensure that we schedule a paint for this
  // positioned frame. Could theoretically do this when deciding to skip
  // painting in `ScrollContainerFrame::ScrollToImpl`, that'd be conditional
  // on finding a dependent anchor anyway, we should be as specific as
  // possible as to what gets scheduled to paint.
  aPositioned->SchedulePaint();
  if (!aReferenceData.CompensatingForScrollAxes().isEmpty()) {
    aReferenceData.mDefaultScrollShift += scrollShifts.mScrollCompensatedDelta;
  }
  aReferenceData.MaybeRememberScrollOffset();
#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->NotifyAnchorPositionedScrollUpdate(aPresShell, aPositioned);
  }
#endif
  // NOTE(emilio): It might be tempting to call MarkPositionedFrameForReflow(),
  // but we don't want to trigger a full reflow as a response to scrolling, and
  // it seems to match other browsers and test expectations, see bug 1950251.
  aPositioned->SetPosition(aPositioned->GetPosition() - delta);
  // Moving aPositioned changes only its parent's overflow areas, not its own.
  aOct.AddFrame(aPositioned->GetParent(),
                OverflowChangedTracker::CHILDREN_CHANGED);
}

static bool TriggerFallbackReflow(PresShell* aPresShell, nsIFrame* aPositioned,
                                  AnchorPosReferenceData& aReferencedAnchors,
                                  OverflowChangedTracker& aOct,
                                  bool aEvaluateAllFallbacksIfNeeded) {
  auto totalFallbacks =
      aPositioned->StylePosition()->mPositionTryFallbacks.value._0.Length();
  if (!totalFallbacks) {
    // No fallbacks specified.
    return false;
  }

  const bool positionedFitsInCB = AnchorPositioningUtils::FitsInContainingBlock(
      aPositioned, aReferencedAnchors);
  auto* lastSuccessfulPosition =
      aPositioned->GetProperty(nsIFrame::LastSuccessfulPositionFallback());

  const bool needsRetry = [&] {
    if (positionedFitsInCB) {
      return false;
    }
    // TODO(bug 1987964): Try to only do this when the scroll offset changes?
    if (aEvaluateAllFallbacksIfNeeded) {
      return true;
    }
    return lastSuccessfulPosition && lastSuccessfulPosition->mLastIndex &&
           !lastSuccessfulPosition->mTriedAllFallbacks;
  }();

  if (!needsRetry) {
    // Record our last successful fallback.
    if (lastSuccessfulPosition) {
      if (lastSuccessfulPosition->mLastIndex !=
          lastSuccessfulPosition->mRecordedIndex) {
        // Changing position fallbacks is an anchor recalculation point, so
        // remember the scroll offset for the new fallback, and update the
        // parent's overflow areas.
        aReferencedAnchors.RememberScrollOffset();
        aOct.AddFrame(aPositioned->GetParent(),
                      OverflowChangedTracker::CHILDREN_CHANGED);
      }
      if (lastSuccessfulPosition->mLastIndex) {
        lastSuccessfulPosition->mRecordedIndex =
            lastSuccessfulPosition->mLastIndex;
      } else {
        aPositioned->RemoveProperty(nsIFrame::LastSuccessfulPositionFallback());
      }
    }
    return false;
  }
  // We'll be back, no need to record the last fallback.
  aPresShell->MarkPositionedFrameForReflow(aPositioned);
  return true;
}

static bool AnchorIsEffectivelyHidden(nsIFrame* aAnchor) {
  if (!aAnchor->StyleVisibility()->IsVisible()) {
    return true;
  }
  for (auto* anchor = aAnchor; anchor; anchor = anchor->GetParent()) {
    if (anchor->HasAnyStateBits(NS_FRAME_POSITION_VISIBILITY_HIDDEN)) {
      return true;
    }
  }
  return false;
}

static bool ComputePositionVisibility(
    PresShell* aPresShell, nsIFrame* aPositioned,
    AnchorPosReferenceData& aReferencedAnchors,
    AnchorPosAnchorTopLayerIndexCache& aTopLayerIndexCache) {
  auto vis = aPositioned->StylePosition()->mPositionVisibility;
  if (vis & StylePositionVisibility::ALWAYS) {
    MOZ_ASSERT(vis == StylePositionVisibility::ALWAYS,
               "always can't be combined");
    return true;
  }
  if (vis & StylePositionVisibility::ANCHOR_VALID) {
    for (const auto& ref : aReferencedAnchors) {
      if (ref.GetData().isNothing()) {
        return false;
      }
    }
  }
  if (vis & StylePositionVisibility::NO_OVERFLOW) {
    const bool positionedFitsInCB =
        AnchorPositioningUtils::FitsInContainingBlock(aPositioned,
                                                      aReferencedAnchors);
    if (!positionedFitsInCB) {
      return false;
    }
  }
  if (vis & StylePositionVisibility::ANCHOR_VISIBLE) {
    const auto* defaultAnchorName = aReferencedAnchors.mDefaultAnchorName.get();
    auto anchorTreeScope = aReferencedAnchors.mAnchorTreeScope;
    if (defaultAnchorName) {
      auto* defaultAnchor = aPresShell->GetAnchorPosAnchor(
          {defaultAnchorName, anchorTreeScope}, aPositioned,
          aReferencedAnchors.mFrameTreeDepth, &aTopLayerIndexCache);
      if (defaultAnchor && AnchorIsEffectivelyHidden(defaultAnchor)) {
        return false;
      }
      auto* containingBlock = nsLayoutUtils::FirstContinuationOrIBSplitSibling(
          aPositioned->GetParent());
      // If both are in the same cb the expectation is that this doesn't apply
      // because there are no intervening clips. I think that's broken, see
      // https://github.com/w3c/csswg-drafts/issues/13176
      if (defaultAnchor && nsLayoutUtils::FirstContinuationOrIBSplitSibling(
                               defaultAnchor->GetParent()) != containingBlock) {
        // Initially, get containingBlock's rect in intersectionRoot's
        // coordinate space.
        auto* intersectionRoot = containingBlock;
        nsRect rootRect;
        if (IsScrolled(containingBlock)) {
          intersectionRoot = containingBlock->GetParent();
          ScrollContainerFrame* sc = do_QueryFrame(intersectionRoot);
          rootRect = sc->GetScrollPortRectAccountingForDynamicToolbar();
        } else {
          rootRect = nsLayoutUtils::GetAllInFlowRectsUnion(
              containingBlock, intersectionRoot,
              nsLayoutUtils::GetAllInFlowRectsFlag::UseInkOverflowAsBox);
        }
        // Then, transform it to the root frame's coordinate space.
        rootRect = nsLayoutUtils::TransformFrameRectToAncestor(
            intersectionRoot, rootRect,
            nsLayoutUtils::GetContainingBlockForClientRect(intersectionRoot));

        const auto* doc = aPositioned->PresContext()->Document();
        const nsINode* root =
            intersectionRoot->GetContent()
                ? static_cast<nsINode*>(intersectionRoot->GetContent())
                : doc;
        const auto input = dom::IntersectionInput{
            .mIsImplicitRoot = false,
            .mRootNode = root,
            .mRootFrame = intersectionRoot,
            .mRootRect = rootRect,
            .mRootMargin = {},
            .mScrollMargin = {},
            .mRemoteDocumentVisibleRect = {},
        };
        const auto output =
            dom::DOMIntersectionObserver::Intersect(input, defaultAnchor);
        // NOTE(emilio): It is a bit weird to also check that mIntersectionRect
        // is non-empty, see https://github.com/w3c/csswg-drafts/issues/13176.
        if (!output.Intersects() || (output.mIntersectionRect->IsEmpty() &&
                                     !defaultAnchor->GetRect().IsEmpty())) {
          return false;
        }
      }
    }
  }
  return true;
}

bool AnchorPositioningUtils::TriggerLayoutOnOverflow(PresShell* aPresShell,
                                                     bool aFirstIteration) {
  bool didLayoutPositionedItems = false;

  OverflowChangedTracker oct;
  AppliedShifts appliedShifts;
  AnchorPosAnchorTopLayerIndexCache topLayerCache;
  for (auto* positioned : aPresShell->GetAnchorPosPositioned()) {
    AnchorPosReferenceData* referencedAnchors =
        positioned->GetProperty(nsIFrame::AnchorPosReferences());
    if (NS_WARN_IF(!referencedAnchors)) {
      continue;
    }

    if (aFirstIteration) {
      UpdateScrollShift(aPresShell, positioned, *referencedAnchors, oct,
                        appliedShifts, topLayerCache);
    }

    if (TriggerFallbackReflow(aPresShell, positioned, *referencedAnchors, oct,
                              aFirstIteration)) {
      didLayoutPositionedItems = true;
    }

    if (didLayoutPositionedItems) {
      // We'll come back to evaluate position-visibility later.
      continue;
    }
    const bool shouldBeVisible = ComputePositionVisibility(
        aPresShell, positioned, *referencedAnchors, topLayerCache);
    const bool isVisible =
        !positioned->HasAnyStateBits(NS_FRAME_POSITION_VISIBILITY_HIDDEN);
    if (shouldBeVisible != isVisible) {
      positioned->AddOrRemoveStateBits(NS_FRAME_POSITION_VISIBILITY_HIDDEN,
                                       !shouldBeVisible);
      positioned->InvalidateFrameSubtree();
    }
  }
  oct.Flush();
  return didLayoutPositionedItems;
}

const nsIFrame* AnchorPositioningUtils::GetMatchingContainingBlock(
    const nsIFrame* aAnchor, const nsIFrame* aContainingBlock) {
  MOZ_ASSERT(nsLayoutUtils::IsProperAncestorFrameConsideringContinuations(
      aContainingBlock, aAnchor));

  const auto* firstCont =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(aContainingBlock);
  const auto* lastCont =
      nsLayoutUtils::LastContinuationOrIBSplitSibling(aContainingBlock);
  if (firstCont == lastCont ||
      nsLayoutUtils::IsProperAncestorFrame(aContainingBlock, aAnchor)) {
    // aContainingBlock has no continuations nor IB-split siblings, or
    // aContainingBlock itself is already a proper ancestor.
    return aContainingBlock;
  }

  for (const auto* f = firstCont; f;
       f = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(f)) {
    if (nsLayoutUtils::IsProperAncestorFrame(f, aAnchor)) {
      return f;
    }
  }
  return nullptr;
}

static nsSize InkOverflowSize(const nsIFrame* aFrame) {
  return aFrame->InkOverflowRectRelativeToSelf().Size();
}

static nscoord BSizeFromPhysicalSize(const nsSize& aSize,
                                     WritingMode aWritingMode) {
  return LogicalSize{aWritingMode, aSize}.BSize(aWritingMode);
}

auto AnchorPositioningUtils::GetCombinedFragmentRects(
    const nsIFrame* aFrame, const nsIFrame* aContainingBlock,
    UnionFragments aUnionFragments, ApplyTransform aApplyTransform)
    -> CombinedFragments {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aContainingBlock);
  MOZ_ASSERT(aUnionFragments == UnionFragments::All ||
                 nsLayoutUtils::IsProperAncestorFrame(aContainingBlock, aFrame),
             "aContainingBlock must be a proper ancestor of aFrame when using "
             "UnionFragments::SameContainingBlockOnly!");

  bool isPaginated = aFrame->PresContext()->IsPaginated();

  const bool applyTransform =
      StaticPrefs::layout_css_anchor_positioning_follows_transforms_enabled() &&
      aApplyTransform == ApplyTransform::Yes &&
      nsLayoutUtils::IsProperAncestorFrame(aContainingBlock, aFrame) &&
      nsLayoutUtils::IsTransformed(aFrame, aContainingBlock);

  // Lazy getter for aFrame's page-frame ancestor, if any.
  Maybe<const nsIFrame*> maybePageFrame;
  auto currPageFrame = [=, &maybePageFrame]() -> const nsIFrame* {
    MOZ_ASSERT(isPaginated);
    if (!maybePageFrame) {
      maybePageFrame.emplace(nsLayoutUtils::GetPageFrame(aFrame));
    }
    return maybePageFrame.ref();
  };

  // A continuation is considered "on the same page" if the context is not
  // paginated, or if it has the same page-frame ancestor.
  auto onSamePage = [=](const nsIFrame* aContinuation) -> bool {
    return !isPaginated ||
           nsLayoutUtils::GetPageFrame(aContinuation) == currPageFrame();
  };

  auto inSameCBFragment = [&](const nsIFrame* aContinuation) {
    // When applying transforms, also restrict to continuations under
    // aContainingBlock. A continuation in a different CB continuation cannot be
    // transformed into aContainingBlock's coordinate space.
    if (aUnionFragments == UnionFragments::SameContainingBlockOnly ||
        applyTransform) {
      return nsLayoutUtils::IsProperAncestorFrame(aContainingBlock,
                                                  aContinuation);
    }
    return true;
  };

  auto GetRectInContainingBlockSpace = [&](const nsIFrame* aContinuation) {
    if (applyTransform) {
      return nsLayoutUtils::TransformFrameRectToAncestor(
          aContinuation, aContinuation->GetRectRelativeToSelf(),
          aContainingBlock, nullptr, nullptr,
          TransformMatrixFlag::IgnoreScrolling);
    }
    return aContinuation->GetRectRelativeToSelf() +
           aContinuation->GetOffsetToIgnoringScrollingAndSticky(
               aContainingBlock);
  };

  // Collect rects from our continuations and IB-split siblings (limited to
  // those that are on the same page if the context is paginated).
  nsRect rect = GetRectInContainingBlockSpace(aFrame);
  const auto* next = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aFrame);
  for (; next && onSamePage(next) && inSameCBFragment(next);
       next = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(next)) {
    rect = rect.Union(GetRectInContainingBlockSpace(next));
  }
  const auto* prev = nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(aFrame);
  for (; prev && onSamePage(prev) && inSameCBFragment(prev);
       prev = nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(prev)) {
    rect = rect.Union(GetRectInContainingBlockSpace(prev));
  }

  return CombinedFragments{prev, next, rect};
}

nsRect AnchorPositioningUtils::ReassembleAnchorRect(
    const nsIFrame* aAnchor, const nsIFrame* aContainingBlock) {
  const nsIFrame* matchingCB =
      GetMatchingContainingBlock(aAnchor, aContainingBlock);
  if (!matchingCB) {
    MOZ_ASSERT_UNREACHABLE("No matching containing block?");
    return nsRect{};
  }
  // Union fragments of the anchor within this containing block.
  auto fragRect = GetCombinedFragmentRects(
      aAnchor, matchingCB, UnionFragments::SameContainingBlockOnly,
      ApplyTransform::Yes);
  // This anchor is contained within this CB fragment, or the containing block
  // is inline.
  // TODO(dshin, bug 2014554): Handle inline containing blocks properly. Inline
  // CBs may continue over multiple lines, e.g. when an inline frame has a
  // `<br>`. In this case, stacking of containing blocks should take line height
  // into account.
  if ((!fragRect.mSkippedPrevContinuation &&
       !fragRect.mSkippedNextContinuation) ||
      matchingCB->IsInlineOutside()) {
    // fragRect.mRect is in matching containing block's coordinate space.
    // Translate the rect back to aContainingBlock's coordinate space.
    return fragRect.mRect +
           matchingCB->GetOffsetToIgnoringScrollingAndSticky(aContainingBlock);
  }
  // Ok, we need to reassemble the unfragmented size and position of the anchor,
  // by stacking up the containing block in block direction.
  // TODO(TYLin, Bug 2063761): the rects need to take transforms into account.
  fragRect = GetCombinedFragmentRects(aAnchor, matchingCB,
                                      UnionFragments::SameContainingBlockOnly,
                                      ApplyTransform::No);
  const auto cbwm = matchingCB->GetWritingMode();
  // Note the use of ink overflow, since the anchor may overflow it.
  const auto cbSize = InkOverflowSize(matchingCB);
  LogicalRect unfragmentedAnchorRect{cbwm, fragRect.mRect, cbSize};
  LogicalSize relevantCbSize{cbwm, cbSize};

  const auto* prev = fragRect.mSkippedPrevContinuation;
  const auto* prevCb = matchingCB->GetPrevContinuation();
  while (prev) {
    MOZ_ASSERT(unfragmentedAnchorRect.BStart(cbwm) == 0,
               "Prev continuation exists but this continuation didn't hit "
               "block-start?");
    MOZ_ASSERT(nsLayoutUtils::IsProperAncestorFrame(prevCb, prev));

    const auto r = GetCombinedFragmentRects(
        prev, prevCb, UnionFragments::SameContainingBlockOnly,
        ApplyTransform::No);
    const auto inkOverflowSize = InkOverflowSize(prevCb);
    const auto prevCBBSize = BSizeFromPhysicalSize(inkOverflowSize, cbwm);

    relevantCbSize.BSize(cbwm) += prevCBBSize;
    LogicalRect rect{cbwm, r.mRect, inkOverflowSize};
    MOZ_ASSERT(rect.BEnd(cbwm) == prevCBBSize,
               "Prev contination doesn't end at block-end?");

    // Use the previous continuation's rect as a base, using its origin, and
    // extending its inline/block size
    unfragmentedAnchorRect = LogicalRect{
        cbwm, rect.Origin(cbwm),
        LogicalSize{
            cbwm,
            std::max(unfragmentedAnchorRect.ISize(cbwm), rect.ISize(cbwm)),
            unfragmentedAnchorRect.BSize(cbwm) + rect.BSize(cbwm)}};

    prev = r.mSkippedPrevContinuation;
    prevCb = prevCb->GetPrevContinuation();
  }

  // We need to get through the rest of previous continuations here, since we
  // need block-start offset of the anchor.
  while (prevCb) {
    const auto prevCbBOffset =
        BSizeFromPhysicalSize(InkOverflowSize(prevCb), cbwm);
    relevantCbSize.BSize(cbwm) += prevCbBOffset;
    unfragmentedAnchorRect.MoveBy(cbwm, LogicalPoint{cbwm, 0, prevCbBOffset});

    prevCb = prevCb->GetPrevContinuation();
  }

  // Assemble fragments in the next block flow fragment.
  const auto* next = fragRect.mSkippedNextContinuation;
  const auto* nextCb = matchingCB->GetNextContinuation();
  while (next) {
    MOZ_ASSERT(
        unfragmentedAnchorRect.BEnd(cbwm) == relevantCbSize.BSize(cbwm),
        "Next continuation exists this continuation didn't hit block-end?");
    MOZ_ASSERT(nsLayoutUtils::IsProperAncestorFrame(nextCb, next));
    const auto r = GetCombinedFragmentRects(
        next, nextCb, UnionFragments::SameContainingBlockOnly,
        ApplyTransform::No);

    const auto inkOverflowSize = InkOverflowSize(nextCb);
    relevantCbSize.BSize(cbwm) += BSizeFromPhysicalSize(inkOverflowSize, cbwm);
    LogicalRect rect{cbwm, r.mRect, inkOverflowSize};
    MOZ_ASSERT(rect.BStart(cbwm) == 0,
               "Next continuation doesn't start at block-start?");

    // Use the current combined anchor rect as a base, keeping its origin,
    // extending its inline/block size.
    unfragmentedAnchorRect = LogicalRect{
        cbwm, unfragmentedAnchorRect.Origin(cbwm),
        LogicalSize{
            cbwm,
            std::max(unfragmentedAnchorRect.ISize(cbwm), rect.ISize(cbwm)),
            unfragmentedAnchorRect.BSize(cbwm) + rect.BSize(cbwm)}};

    next = r.mSkippedNextContinuation;
    nextCb = nextCb->GetNextContinuation();
  }

  // Don't need to run through `nextCb` since reassembled anchor rect is fully
  // constrained by the start side.

  return unfragmentedAnchorRect.GetPhysicalRect(
      cbwm, relevantCbSize.GetPhysicalSize(cbwm));
}

const dom::ShadowRoot* AnchorPositioningUtils::GetShadowRootForTreeScope(
    const dom::Element& aElement, const StyleCascadeLevel& aTreeScope) {
  return Servo_GetShadowRootForScoped(&aElement, aTreeScope);
}

}  // namespace mozilla
