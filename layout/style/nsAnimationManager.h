/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsAnimationManager_h_
#define nsAnimationManager_h_

#include "AnimationCommon.h"
#include "mozilla/Keyframe.h"
#include "mozilla/Span.h"
#include "mozilla/dom/CSSAnimation.h"
#include "nsISupportsImpl.h"
#include "nsTHashSet.h"

class ServoCSSAnimationBuilder;

struct nsStyleUIReset;

namespace mozilla {
class ComputedStyle;

struct NonOwningAnimationTarget;
struct PseudoStyleRequest;

} /* namespace mozilla */

class nsAnimationManager final
    : public mozilla::CommonAnimationManager<mozilla::dom::CSSAnimation> {
 public:
  using TimelineNamesToAnimationMap =
      nsTHashMap<RefPtr<const nsAtom>,
                 nsTArray<RefPtr<mozilla::dom::CSSAnimation>>>;

  explicit nsAnimationManager(nsPresContext* aPresContext)
      : mozilla::CommonAnimationManager<mozilla::dom::CSSAnimation>(
            aPresContext) {}

  typedef mozilla::AnimationCollection<mozilla::dom::CSSAnimation>
      CSSAnimationCollection;
  typedef nsTArray<RefPtr<mozilla::dom::CSSAnimation>>
      OwningCSSAnimationPtrArray;

  ~nsAnimationManager() override = default;

  /**
   * This function does the same thing as the above UpdateAnimations()
   * but with servo's computed values.
   */
  void UpdateAnimations(mozilla::dom::Element* aElement,
                        const mozilla::PseudoStyleRequest& aPseudoRequest,
                        const mozilla::ComputedStyle* aComputedValues);

  void RemoveNamedTimelineAnimation(const nsAtom* aName,
                                    mozilla::dom::CSSAnimation* aAnimation);

  void UpdateDeferredTimelineChanges();
  /* Request that animations referring to the given timeline names (or to any
   * named timeline) look their timeline up again, because some element
   * started/stopped providing or scoping a timeline of that name. The lookups
   * run in FlushPendingNamedTimelineUpdates(), once the style traversal has
   * registered all of its timelines. Note that we only care about the name,
   * because we cannot early-reject name changes based on the cascade level of
   * the scoped name; we need to do the actual lookup. */
  void MarkNamedTimelineAnimationForUpdate(const nsAtom* aName);
  void MarkNamedTimelineAnimationsForUpdate(
      mozilla::Span<const RefPtr<const nsAtom>> aNames);
  void MarkAllNamedTimelineAnimationsForUpdate() {
    mAllNamedTimelineAnimationsPending = true;
  }
  bool HasPendingNamedTimelineUpdates() const {
    return mAllNamedTimelineAnimationsPending ||
           !mTimelineNamesPendingUpdate.IsEmpty();
  }
  void FlushPendingNamedTimelineUpdates();

  // Utility function to walk through |aIter| to find the Keyframe with
  // matching offset and timing function but stopping as soon as the offset
  // differs from |aOffset| (i.e. it assumes a sorted iterator for double
  // offsets). For TimelineRangeOffset, see the comments in the loop.
  //
  // If a matching Keyframe is found,
  //   Returns true and sets |aIndex| to the index of the matching Keyframe
  //   within |aIter|.
  //
  // If no matching Keyframe is found,
  //   Returns false and sets |aIndex| to the index in the iterator of the
  //   first Keyframe with an offset differing to |aOffset| or, if the end
  //   of the iterator is reached, sets |aIndex| to the index after the last
  //   Keyframe.
  template <class IterType>
  static bool FindMatchingKeyframe(
      IterType&& aIter, const mozilla::Keyframe::OffsetType& aOffset,
      const mozilla::StyleComputedTimingFunction& aTimingFunctionToMatch,
      mozilla::dom::CompositeOperationOrAuto aCompositionToMatch,
      size_t& aIndex) {
    aIndex = 0;
    for (mozilla::Keyframe& keyframe : aIter) {
      if (keyframe.mOffset.value() != aOffset) {
        // There is an assumption that we handle keyframes with double offsets
        // first, and they are in sorted order. So when we are searching for a
        // double offset and the offset is different from the current
        // |keyframe|, it must be a non-existing Keyframe.
        if (aOffset.IsPercentageOffset()) {
          break;
        }

        // There is an assumption that we handle keyframes with
        // TimelineRangeOffset after we insert all the keyframes with double
        // offsets, and the Keyframe with TimelineRangeOffset should be put
        // after the Keyframes with double offsets. In this case, we search for
        // the TimelineRangeOffset in the reversed order of Keyframes.
        // Therefore, if current |keyframe| is is double offset, |offset| must
        // not exist.
        if (keyframe.mOffset->IsPercentageOffset()) {
          break;
        }
        ++aIndex;
        continue;
      }

      const bool matches = [&] {
        if (keyframe.mComposite != aCompositionToMatch) {
          return false;
        }
        return keyframe.mTimingFunction
                   ? *keyframe.mTimingFunction == aTimingFunctionToMatch
                   : aTimingFunctionToMatch.IsLinearKeyword();
      }();
      if (matches) {
        return true;
      }
      ++aIndex;
    }
    return false;
  }

  bool AnimationMayBeReferenced(nsAtom* aName) const {
    return mMaybeReferencedAnimations.Contains(aName);
  }

 private:
  // This includes all animation names referenced regardless of whether a
  // corresponding `@keyframes` rule is available.
  //
  // It may contain names which are no longer referenced, but it should always
  // contain names which are currently referenced, so that it is usable for
  // style invalidation.
  nsTHashSet<RefPtr<nsAtom>> mMaybeReferencedAnimations;
  // Animations that refer to a timeline by name. This is necessary for
  // invalidating such animations, because the timeline referred to by
  // that name for the animation target may change.
  // Note that only scroll and view timelines can be named.
  // Also note that we represent `animation-timeline: none` as a named
  // timeline with an empty name, which is not tracked in this hashmap.
  TimelineNamesToAnimationMap mAnimationsWithNamedTimeline;
  // Animations with updates to named timeline that may result in a null or
  // inactive timeline attachment.
  nsTHashSet<RefPtr<mozilla::dom::CSSAnimation>> mAnimationsWithDeferredUpdate;
  // Timeline names whose animations need a new lookup, see
  // MarkNamedTimelineAnimationsForUpdate().
  nsTHashSet<RefPtr<const nsAtom>> mTimelineNamesPendingUpdate;
  bool mAllNamedTimelineAnimationsPending = false;

  void UpdateNamedTimelineAnimations(
      const nsTHashSet<RefPtr<const nsAtom>>& aChanged);
  void UpdateAllNamedTimelineAnimations();

  void DoUpdateAnimations(const mozilla::NonOwningAnimationTarget& aTarget,
                          const nsStyleUIReset& aStyle,
                          ServoCSSAnimationBuilder& aBuilder);
};

#endif /* !defined(nsAnimationManager_h_) */
