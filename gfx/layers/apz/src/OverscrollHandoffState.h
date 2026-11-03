/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_OverscrollHandoffChain_h
#define mozilla_layers_OverscrollHandoffChain_h

#include <vector>

#include "APZUtils.h"                    // for CancelAnimationFlags
#include "Units.h"                       // for ScreenPoint
#include "mozilla/RefPtr.h"              // for RefPtr
#include "mozilla/layers/LayersTypes.h"  // for Layer::ScrollDirection
#include "nsISupportsImpl.h"  // for NS_INLINE_DECL_THREADSAFE_REFCOUNTING

namespace mozilla {

class InputData;

namespace layers {

class AsyncPanZoomController;

/**
 * This class represents the chain of APZCs along which overscroll is handed
 * off. It is created by APZCTreeManager by starting from an initial APZC which
 * is the target for input events, and following the scroll parent ID links
 * (often but not always corresponding to parent pointers in the APZC tree).
 */
class OverscrollHandoffChain {
 protected:
  // Reference-counted classes cannot have public destructors.
  ~OverscrollHandoffChain() = default;

 public:
  // Threadsafe so that the controller and sampler threads can both maintain
  // nsRefPtrs to the same handoff chain.
  // Mutable so that we can pass around the class by
  // RefPtr<const OverscrollHandoffChain> and thus enforce that, once built,
  // the chain is not modified.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(OverscrollHandoffChain)

  /*
   * Methods for building the handoff chain.
   * These should be used only by
   * AsyncPanZoomController::BuildOverscrollHandoffChain().
   */
  void Add(AsyncPanZoomController* aApzc);
  void SortByScrollPriority();

  /*
   * Methods for accessing the handoff chain.
   */
  uint32_t Length() const { return mChain.size(); }
  const RefPtr<AsyncPanZoomController>& GetApzcAtIndex(uint32_t aIndex) const;
  // Returns Length() if |aApzc| is not on this chain.
  uint32_t IndexOf(const AsyncPanZoomController* aApzc) const;

  /*
   * Convenience methods for performing operations on APZCs in the chain.
   */

  // Flush repaints all the way up the chain.
  void FlushRepaints() const;

  // Cancel animations all the way up the chain.
  void CancelAnimations(CancelAnimationFlags aFlags = Default) const;

  // Clear overscroll all the way up the chain.
  void ClearOverscroll() const;

  // Clear the handed-off gesture flag all the way up the chain, to be called
  // when the gesture driving the handoff ends.
  void ClearScrolledByHandedOffGesture() const;

  // Snap back the APZC that is overscrolled on the subset of the chain from
  // |aStart| onwards, if any.
  void SnapBackOverscrolledApzc(const AsyncPanZoomController* aStart) const;

  // Similar to above SnapbackOverscrolledApzc but for pan gestures with
  // momentum events, this function doesn't end up calling each APZC's
  // ScrollSnap.
  // |aVelocity| is the initial velocity of |aStart|.
  void SnapBackOverscrolledApzcForMomentum(
      const AsyncPanZoomController* aStart,
      const ParentLayerPoint& aVelocity) const;

  // Determine whether the given APZC, or any APZC further in the chain,
  // has room to be panned.
  bool CanBePanned(const AsyncPanZoomController* aApzc) const;

  // Determine whether the given APZC, or any APZC further in the chain,
  // can scroll in the given direction.
  bool CanScrollInDirection(const AsyncPanZoomController* aApzc,
                            ScrollDirection aDirection) const;

  // Determine whether any APZC along this handoff chain is overscrolled.
  bool HasOverscrolledApzc() const;

  // Determine whether any APZC along this handoff chain has been flung fast.
  bool HasFastFlungApzc() const;

  // Determine whether any APZC along this handoff chain is autoscroll.
  bool HasAutoscrollApzc() const;

  // Determine whether any APZC along this handoff chain has an animation
  // running.
  bool HasAnimatingApzc() const;

  // Find the APZC in this handoff chain that |aInput| should be applied to.
  // The chain is scanned from the innermost APZC outwards for the first one
  // that can be scrolled by |aInput|. If none can, and |aIncludeOverscroll| is
  // Yes, the APZCs the scan reached are examined again, this time from the
  // outermost inwards, for the first one that can show an overscroll effect
  // instead. That second pass currently only runs for pan gestures.
  // |aOutAllowedScrollDirections| is populated with the overscroll-behavior
  // handoff restrictions of the APZCs that precede the returned one: the
  // caller rebuilds the handoff chain from the returned APZC, so those APZCs
  // drop out of it and can no longer enforce their own restrictions when the
  // scroll is dispatched. Nothing drops out of the chain when nullptr is
  // returned, so both directions are reported in that case.
  enum class IncludeOverscroll : bool { No, Yes };
  RefPtr<AsyncPanZoomController> FindScrollTarget(
      const InputData& aInput, ScrollDirections* aOutAllowedScrollDirections,
      IncludeOverscroll aIncludeOverscroll = IncludeOverscroll::Yes) const;

  // Return a pair of true and the root content APZC if all non-root APZCs in
  // this handoff chain starting from |aApzc| are not able to scroll downwards
  // (i.e. there is no room to scroll downwards in each APZC respectively) and
  // there is any contents covered by the dynamic toolbar, otherwise return a
  // pair of false and nullptr.
  std::tuple<bool, const AsyncPanZoomController*>
  ScrollingDownWillMoveDynamicToolbar(
      const AsyncPanZoomController* aApzc) const;

  bool ScrollingUpWillTriggerPullToRefresh(
      const AsyncPanZoomController* aApzc) const;

  SideBits GetSwipableDirections(const AsyncPanZoomController& aTarget) const;

 private:
  std::vector<RefPtr<AsyncPanZoomController>> mChain;

  typedef void (AsyncPanZoomController::*APZCMethod)();
  typedef bool (AsyncPanZoomController::*APZCPredicate)() const;
  void ForEachApzc(APZCMethod aMethod) const;
  bool AnyApzc(APZCPredicate aPredicate) const;
};

/**
 * This class groups the state maintained during overscroll handoff.
 */
struct OverscrollHandoffState {
  OverscrollHandoffState(const OverscrollHandoffChain& aChain,
                         const ScreenPoint& aPanDistance,
                         ScrollSource aScrollSource)
      : mChain(aChain),
        mChainIndex(0),
        mPanDistance(aPanDistance),
        mScrollSource(aScrollSource) {}

  bool IsScrolledByHandedOffGesture() const;

  // The chain of APZCs along which we hand off scroll.
  // This is const to indicate that the chain does not change over the
  // course of handoff.
  const OverscrollHandoffChain& mChain;

  // The index of the APZC in the chain that we are currently giving scroll to.
  // This is non-const to indicate that this changes over the course of handoff.
  uint32_t mChainIndex;

  // The total distance since touch-start of the pan that triggered the
  // handoff. This is const to indicate that it does not change over the
  // course of handoff.
  // The x/y components of this are non-negative.
  const ScreenPoint mPanDistance;

  ScrollSource mScrollSource;

  // The total amount of actual movement that this scroll caused, including
  // scrolling and changes to overscroll. This starts at zero and is accumulated
  // over the course of the handoff.
  ScreenPoint mTotalMovement;
};

/*
 * This class groups the state maintained during fling handoff.
 */
struct FlingHandoffState {
  // The velocity of the fling being handed off.
  ParentLayerPoint mVelocity;

  // The chain of APZCs along which we hand off the fling.
  // Unlike in OverscrollHandoffState, this is stored by RefPtr because
  // otherwise it may not stay alive for the entire handoff.
  RefPtr<const OverscrollHandoffChain> mChain;

  // The time duration between the touch start and the touch move that started
  // the pan gesture which triggered this fling. In other words, the time it
  // took for the finger to move enough to cross the touch slop threshold.
  // Nothing if this fling was not immediately caused by a touch pan.
  Maybe<TimeDuration> mTouchStartRestingTime;

  // The slowest panning velocity encountered during the pan that triggered this
  // fling.
  ParentLayerCoord mMinPanVelocity;

  // Whether handoff has happened by this point, or we're still process
  // the original fling.
  bool mIsHandoff;

  // The single APZC that was scrolled by the pan that started this fling.
  // The fling is only allowed to scroll this APZC, too.
  // Used only if immediate scroll handoff is disallowed.
  RefPtr<const AsyncPanZoomController> mScrolledApzc;
};

}  // namespace layers
}  // namespace mozilla

#endif /* mozilla_layers_OverscrollHandoffChain_h */
