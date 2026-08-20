/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AnchorPositioningUtils_h_
#define AnchorPositioningUtils_h_

#include "mozilla/Maybe.h"
#include "mozilla/WritingModes.h"
#include "nsRect.h"

class nsAtom;
class nsIFrame;

template <class T>
class nsTArray;

template <class T>
class CopyableTArray;

namespace mozilla {

namespace dom {
class ShadowRoot;
}

class nsDisplayListBuilder;

struct AnchorPosInfo {
  // Border-box of the anchor frame, offset against the positioned frame's
  // absolute containing block's padding box.
  nsRect mRect;
  // See `AnchorPosOffsetData::mCompensatesForScroll`.
  bool mCompensatesForScroll;
};

class DistanceToNearestScrollContainer {
 public:
  DistanceToNearestScrollContainer() = default;
  explicit DistanceToNearestScrollContainer(uint32_t aDistance)
      : mDistance{aDistance} {}

  bool Valid() const { return mDistance != kInvalid; }

  bool operator==(const DistanceToNearestScrollContainer&) const = default;

 private:
  // 0 is invalid - a frame itself cannot be its own nearest scroll container.
  static constexpr uint32_t kInvalid = 0;
  // Ancestor hops to the nearest scroll container. Note that scroll containers
  // between abspos/fixedpos frames and their containing blocks are irrelevant,
  // so the distance should be measured from the out-of-flow frame, not the
  // placeholder frame.
  uint32_t mDistance = kInvalid;
};

struct AnchorPosOffsetData {
  // Origin of the referenced anchor, w.r.t. containing block at the time of
  // resolution.
  nsPoint mOrigin;
  // Does this anchor's offset compensate for scroll?
  // https://drafts.csswg.org/css-anchor-position-1/#compensate-for-scroll
  bool mCompensatesForScroll = false;
  // Distance to this anchor's nearest scroll container.
  DistanceToNearestScrollContainer mDistanceToNearestScrollContainer;
};

class ScopedNameRef {
 public:
  ScopedNameRef(const nsAtom* aAtom, const StyleCascadeLevel& aTreeScope)
      : mName(aAtom), mTreeScope(aTreeScope) {}

  const nsAtom* mName = nullptr;
  StyleCascadeLevel mTreeScope = StyleCascadeLevel::Default();
};

class nsScopedNameRefHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = ScopedNameRef;
  using KeyTypePointer = const ScopedNameRef*;

  explicit nsScopedNameRefHashKey(const ScopedNameRef* aKey)
      : mAtom(aKey->mName), mTreeScope(aKey->mTreeScope) {
    MOZ_ASSERT(aKey);
    MOZ_ASSERT(aKey->mName);
  }
  nsScopedNameRefHashKey(const nsScopedNameRefHashKey& aOther) = delete;
  nsScopedNameRefHashKey(nsScopedNameRefHashKey&& aOther) = default;
  ~nsScopedNameRefHashKey() = default;

  KeyType GetKey() const { return ScopedNameRef(mAtom, mTreeScope); }
  bool KeyEquals(KeyTypePointer aKey) const {
    // This should work because a positioned element can't make two references
    // with the same name in different tree scopes. Further scope resolution is
    // hard to do here because the map does not have all the context.
    return aKey->mName == mAtom.get();
  }

  static KeyTypePointer KeyToPointer(const KeyType& aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    return MOZ_LIKELY(aKey && aKey->mName) ? aKey->mName->hash() : 0;
  }
  enum { ALLOW_MEMMOVE = true };

 private:
  RefPtr<const nsAtom> mAtom;
  StyleCascadeLevel mTreeScope;
};

// Resolved anchor positioning data.
struct AnchorPosResolutionData {
  // Size of the referenced anchor.
  nsSize mSize;
  // Offset resolution data. Nothing if the anchor did not resolve, or if the
  // anchor was only referred to by its size.
  Maybe<AnchorPosOffsetData> mOffsetData;
  StyleCascadeLevel mAnchorTreeScope;
};

// Data required for an anchor positioned frame, including:
// * If valid anchors are found,
// * Cached offset/size resolution, if resolution was valid,
// * Compensating for scroll [1]
// * Default scroll shift [2]
//
// [1]: https://drafts.csswg.org/css-anchor-position-1/#compensate-for-scroll
// [2]: https://drafts.csswg.org/css-anchor-position-1/#default-scroll-shift
class AnchorPosReferenceData {
 private:
  using ResolutionMap =
      nsBaseHashtable<nsScopedNameRefHashKey,
                      mozilla::Maybe<AnchorPosResolutionData>,
                      mozilla::Maybe<AnchorPosResolutionData>>;

 public:
  // Backup data for attempting a different `@position-try` style, when
  // the default anchor remains the same.
  // These entries correspond 1:1 to that of `AnchorPosReferenceData`.
  struct PositionTryBackup {
    mozilla::PhysicalAxes mCompensatingForScroll;
    nsPoint mDefaultScrollShift;
    nsRect mAdjustedContainingBlock;
    SideBits mScrollCompensatedSides;
    nsMargin mInsets;
  };
  using Value = mozilla::Maybe<AnchorPosResolutionData>;

  AnchorPosReferenceData() = default;
  AnchorPosReferenceData(const AnchorPosReferenceData&) = delete;
  AnchorPosReferenceData(AnchorPosReferenceData&&) = default;

  AnchorPosReferenceData& operator=(const AnchorPosReferenceData&) = delete;
  AnchorPosReferenceData& operator=(AnchorPosReferenceData&&) = default;

  struct Result {
    bool mAlreadyResolved;
    Value* mEntry;
  };

  Result InsertOrModify(const ScopedNameRef& aKey, bool aNeedOffset);
  const Value* Lookup(const ScopedNameRef& aKey) const;

  bool IsEmpty() const { return mMap.IsEmpty(); }

  ResolutionMap::const_iterator begin() const { return mMap.cbegin(); }
  ResolutionMap::const_iterator end() const { return mMap.cend(); }

  void AdjustCompensatingForScroll(const mozilla::PhysicalAxes& aAxes) {
    mCompensatingForScroll += aAxes;
  }

  mozilla::PhysicalAxes CompensatingForScrollAxes() const {
    return mCompensatingForScroll;
  }

  PositionTryBackup TryPositionWithSameDefaultAnchor() {
    auto compensatingForScroll = std::exchange(mCompensatingForScroll, {});
    auto defaultScrollShift = std::exchange(mDefaultScrollShift, {});
    auto adjustedContainingBlock = std::exchange(mAdjustedContainingBlock, {});
    auto containingBlockSidesAttachedToAnchor =
        std::exchange(mScrollCompensatedSides, SideBits::eNone);
    auto insets = std::exchange(mInsets, nsMargin{});
    return {compensatingForScroll, defaultScrollShift, adjustedContainingBlock,
            containingBlockSidesAttachedToAnchor, insets};
  }

  void UndoTryPositionWithSameDefaultAnchor(PositionTryBackup&& aBackup) {
    mCompensatingForScroll = aBackup.mCompensatingForScroll;
    mDefaultScrollShift = aBackup.mDefaultScrollShift;
    mAdjustedContainingBlock = aBackup.mAdjustedContainingBlock;
    mScrollCompensatedSides = aBackup.mScrollCompensatedSides;
    mInsets = aBackup.mInsets;
  }

  // Distance from the default anchor to the nearest scroll container.
  DistanceToNearestScrollContainer mDistanceToDefaultScrollContainer;
  // https://drafts.csswg.org/css-anchor-position-1/#default-scroll-shift
  nsPoint mDefaultScrollShift;
  // Rect of the original containg block.
  nsRect mOriginalContainingBlockRect;
  // Adjusted containing block, by position-area or grid, as per
  // https://drafts.csswg.org/css-position/#original-cb
  // TODO(dshin, bug 2004596): "or" should be "and/or."
  nsRect mAdjustedContainingBlock;
  // TODO(dshin, bug 1987962): Remembered scroll offset
  // https://drafts.csswg.org/css-anchor-position-1/#remembered-scroll-offset
  // Name of the default used anchor. Not necessarily positioned frame's
  // style, because of fallbacks.
  RefPtr<const nsAtom> mDefaultAnchorName;
  // Flag indicating which sides of the containing block attach to the
  // scroll-compensated anchor. Whenever a scroll-compensated anchor scrolls, it
  // effectively moves around w.r.t. its absolute containing block. This
  // effectively changes the size of the containing block. For example, given:
  //
  // * Absolute containing block of 50px height,
  // * Scroller, under the abs CB, with the scrolled content height of 100px,
  // * Anchor element, under the scroller, of 30px height, and
  // * Positioned element of 30px height, attached to anchor at the bottom.
  //
  // The positioned element would overflow the abs CB, until the scroller moves
  // down by 10px. We address this by defining sides of the CB that scrolls
  // with the anchor, so that whenever we carry out an overflow check, we move
  // those sides by the scroll offset, while pinning the rest of the sides to
  // the original containing block.
  SideBits mScrollCompensatedSides = SideBits::eNone;
  // Resolved insets for this positioned element. Modifies the adjusted &
  // scrolled containing block.
  nsMargin mInsets;

  StyleCascadeLevel mAnchorTreeScope = StyleCascadeLevel::Default();

 private:
  ResolutionMap mMap;
  // Axes we need to compensate for scroll [1] in.
  // [1]: https://drafts.csswg.org/css-anchor-position-1/#compensate-for-scroll
  mozilla::PhysicalAxes mCompensatingForScroll;
};

struct LastSuccessfulPositionData {
  // The style + index of our last reflow.
  RefPtr<const ComputedStyle> mLastStyle;
  Maybe<uint32_t> mLastIndex;
  // The "recorded" index that we start looking fallbacks from.
  // https://drafts.csswg.org/css-anchor-position/#last-successful-recording
  Maybe<uint32_t> mRecordedIndex;
  // Whether we tried all fallbacks or not.
  bool mTriedAllFallbacks = false;
};

struct StylePositionArea;
class WritingMode;

struct AnchorPosDefaultAnchorCache {
  // Default anchor element's corresponding frame.
  const nsIFrame* mAnchor = nullptr;
  // Scroll container for the default anchor.
  const nsIFrame* mScrollContainer = nullptr;

  AnchorPosDefaultAnchorCache() = default;
  AnchorPosDefaultAnchorCache(const nsIFrame* aAnchor,
                              const nsIFrame* aScrollContainer);
};

// Cache data used by anchor resolution. To be populated on abspos reflow,
// whenever the frame makes any anchor reference.
struct AnchorPosResolutionCache {
  // Storage for referenced anchors. Designed to be long-lived (i.e. beyond
  // a reflow cycle).
  AnchorPosReferenceData* mReferenceData = nullptr;
  // Cached data for default anchor resolution. Designed to be short-lived,
  // so it can contain e.g. frame pointers.
  AnchorPosDefaultAnchorCache mDefaultAnchorCache;

  // Backup data for attempting a different `@position-try` style, when
  // the default anchor remains the same.
  using PositionTryBackup = AnchorPosReferenceData::PositionTryBackup;
  PositionTryBackup TryPositionWithSameDefaultAnchor() {
    return mReferenceData->TryPositionWithSameDefaultAnchor();
  }
  void UndoTryPositionWithSameDefaultAnchor(PositionTryBackup&& aBackup) {
    mReferenceData->UndoTryPositionWithSameDefaultAnchor(std::move(aBackup));
  }

  // Backup data for attempting a different `@position-try` style, when
  // the default anchor changes.
  using PositionTryFullBackup =
      std::pair<AnchorPosReferenceData, AnchorPosDefaultAnchorCache>;
  PositionTryFullBackup TryPositionWithDifferentDefaultAnchor() {
    auto referenceData = std::move(*mReferenceData);
    *mReferenceData = {};
    return std::make_pair(
        std::move(referenceData),
        std::exchange(mDefaultAnchorCache, AnchorPosDefaultAnchorCache{}));
  }
  void UndoTryPositionWithDifferentDefaultAnchor(
      PositionTryFullBackup&& aBackup) {
    *mReferenceData = std::move(aBackup.first);
    std::exchange(mDefaultAnchorCache, aBackup.second);
  }
};

enum class StylePositionTryFallbacksTryTacticKeyword : uint8_t;
using StylePositionTryFallbacksTryTactic =
    CopyableTArray<StylePositionTryFallbacksTryTacticKeyword>;

/**
 * AnchorPositioningUtils is a namespace class used for various anchor
 * positioning helper functions that are useful in multiple places.
 * The goal is to avoid code duplication and to avoid having too
 * many helpers in nsLayoutUtils.
 */
struct AnchorPositioningUtils {
  /**
   * Finds the first acceptable frame from the list of possible anchor frames
   * following https://drafts.csswg.org/css-anchor-position-1/#target
   */
  static nsIFrame* FindFirstAcceptableAnchor(
      const ScopedNameRef& aName, const nsIFrame* aPositionedFrame,
      const nsTArray<nsIFrame*>& aPossibleAnchorFrames);

  static Maybe<nsRect> GetAnchorPosRect(
      const nsIFrame* aAbsoluteContainingBlock, const nsIFrame* aAnchor,
      bool aCBRectIsValid);

  static Maybe<AnchorPosInfo> ResolveAnchorPosRect(
      const nsIFrame* aPositioned, const nsIFrame* aAbsoluteContainingBlock,
      const ScopedNameRef& aAnchorName, bool aCBRectIsValid,
      AnchorPosResolutionCache* aResolutionCache);

  static Maybe<nsSize> ResolveAnchorPosSize(
      const nsIFrame* aPositioned, const nsIFrame* aAbsoluteContainingBlock,
      const ScopedNameRef& aAnchorName,
      AnchorPosResolutionCache* aResolutionCache);

  /**
   * Adjust the containing block rect for the 'position-area' property.
   * https://drafts.csswg.org/css-anchor-position-1/#position-area
   */
  static nsRect AdjustAbsoluteContainingBlockRectForPositionArea(
      const nsRect& aAnchorRect, const nsRect& aCBRect,
      WritingMode aPositionedWM, WritingMode aCBWM,
      const StylePositionArea& aPosArea, StylePositionArea* aOutResolvedArea);

  /**
   * Gets the used anchor name for an anchor positioned frame.
   *
   * @param aPositioned The anchor positioned frame.
   * @param aAnchorName The anchor name specified in the anchor function,
   *   or nullptr if not specified.
   *
   * If `aAnchorName` is not specified, then this function will return the
   * default anchor name, if the `position-anchor` property specified one.
   * Otherwise it will return `nsGkAtoms::AnchorPosImplicitAnchor` if the
   * element has an implicit anchor, or a nullptr.
   */
  static Maybe<ScopedNameRef> GetUsedAnchorName(
      const nsIFrame* aPositioned, const ScopedNameRef& aAnchorName);

  /**
   * Get the implicit anchor of the frame.
   *
   * @param aFrame The anchor positioned frame.
   *
   * For pseudo-elements, this returns the parent frame of the originating
   * element. For popovers, this returns the primary frame of the invoker. In
   * all other cases, returns null.
   */
  enum class ImplicitAnchorKind : uint8_t { None, Popover, PseudoElement };
  struct ImplicitAnchorResult {
    nsIFrame* mAnchorFrame = nullptr;
    ImplicitAnchorKind mKind = ImplicitAnchorKind::None;
  };
  static ImplicitAnchorResult GetAnchorPosImplicitAnchor(
      const nsIFrame* aFrame);

  struct NearestScrollFrameInfo {
    const nsIFrame* mScrollContainer = nullptr;
    DistanceToNearestScrollContainer mDistance;
  };
  static NearestScrollFrameInfo GetNearestScrollFrame(const nsIFrame* aFrame);

  static nsPoint GetScrollOffsetFor(
      PhysicalAxes aAxes, const nsIFrame* aPositioned,
      const AnchorPosDefaultAnchorCache& aDefaultAnchorCache);

  struct ContainingBlockInfo {
    // Provide an explicit containing block size, for during reflow when
    // its `mRect` has not yet been set.
    static ContainingBlockInfo ExplicitCBFrameSize(
        const nsRect& aContainingBlockRect);
    // Provide the positioned frame, to query  its containing block rect.
    static ContainingBlockInfo UseCBFrameSize(const nsIFrame* aPositioned);

    nsRect GetContainingBlockRect() const { return mRect; }

   private:
    explicit ContainingBlockInfo(const nsRect& aRect) : mRect{aRect} {}
    nsRect mRect;
  };

  static bool FitsInContainingBlock(const nsIFrame* aPositioned,
                                    const AnchorPosReferenceData&);

  /**
   * If aFrame is positioned using CSS anchor positioning, and it scrolls with
   * its anchor this function returns the anchor. Otherwise null.
   * Note that this function has different behaviour if it called during paint
   * (ie aBuilder not null) or not during painting (aBuilder null).
   */
  static nsIFrame* GetAnchorThatFrameScrollsWith(nsIFrame* aFrame,
                                                 nsDisplayListBuilder* aBuilder,
                                                 bool aSkipAsserts = false);

  // Trigger a layout for positioned items that are currently overflowing their
  // abs-cb and that have available fallbacks to try.
  static bool TriggerLayoutOnOverflow(PresShell*, bool aFirstIteration);

  static StylePositionArea PhysicalizePositionArea(StylePositionArea aPosArea,
                                                   const nsIFrame* aPositioned);

  /**
   * When an anchor is split across fragmentainers such as multiple columns or
   * pages, this function reconstructs what its unfragmented bounding rect would
   * be by walking through the containing block's continuations and stacking all
   * the anchor's fragment rects vertically in the containing block's block-axis
   * direction. The returned rect is relative to the containing block's
   * first-continuation. During reflow, we simply cache the unfragmented anchor
   * rect as the resolution cache is populated, and use it, so this isn't
   * required. However, we need to be able to recompute the anchor out-of-reflow
   * to see if we need to trigger reflow.
   */
  static nsRect ReassembleAnchorRect(const nsIFrame* aAnchor,
                                     const nsIFrame* aContainingBlock);

  /**
   * Return the continuation (or IB-split sibling) of aContainingBlock that is a
   * proper ancestor of aAnchor, which may not be aContainingBlock itself when
   * the containing block is fragmented. Return nullptr if no continuation of
   * aContainingBlock contains aAnchor.
   */
  static const nsIFrame* GetMatchingContainingBlock(
      const nsIFrame* aAnchor, const nsIFrame* aContainingBlock);

  struct CombinedFragments {
    // Previous continuation, if exists, that got skipped due to being on a
    // different page, or a different containing block continuation.
    const nsIFrame* mSkippedPrevContinuation = nullptr;
    // Same as above, but next continuation.
    const nsIFrame* mSkippedNextContinuation = nullptr;
    // The overall frame rect formed by unioning the frame's fragment rects.
    nsRect mRect;
  };
  enum class UnionFragments : bool {
    // Union every fragment of the frame.
    All,
    // Union only the fragments under the given containing block continuation.
    SameContainingBlockOnly,
  };

  enum class ApplyTransform : bool {
    // The returned rect is untransformed. The caller is not expected to apply
    // transforms to it afterwards.
    No,
    // The returned rect takes transforms between aFrame and aContainingBlock
    // into account, if any.
    Yes,
  };
  /**
   * Get the union of the rects of aFrame and its continuations (but not if the
   * context is paginated and they're on a different page, as it doesn't make
   * sense to "merge" their rects in that case).
   *
   * @param aFrame The target frame whose combined fragments are wanted.
   * @param aContainingBlock The frame whose coordinate space the result is
   * expressed in.
   * @param aUnionFragments Which of aFrame's fragments to union: all of them,
   * or only those under aContainingBlock. SameContainingBlockOnly requires
   * aContainingBlock to be a proper ancestor of aFrame.
   * @param aApplyTransform Whether the returned mRect takes transforms between
   * aFrame and aContainingBlock into account per
   * https://drafts.csswg.org/css-anchor-position-1/#determining. Transforms are
   * applied only when aContainingBlock is a proper ancestor of aFrame.
   *
   */
  static CombinedFragments GetCombinedFragmentRects(
      const nsIFrame* aFrame, const nsIFrame* aContainingBlock,
      UnionFragments aUnionFragments, ApplyTransform aApplyTransform);

  // Helper to get shadow root for a property's tree scope
  static const dom::ShadowRoot* GetShadowRootForTreeScope(
      const dom::Element& aElement, const StyleCascadeLevel& aTreeScope);
};

}  // namespace mozilla

#endif  // AnchorPositioningUtils_h_
