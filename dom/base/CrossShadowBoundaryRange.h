/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CrossShadowBoundaryRange_h
#define mozilla_dom_CrossShadowBoundaryRange_h

#include "mozilla/RangeBoundary.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/dom/AbstractRange.h"
#include "mozilla/dom/StaticRange.h"
#include "nsTArray.h"

namespace mozilla {
class ErrorResult;

namespace dom {

/**
 * CrossShadowBoundaryRange inherits StaticRange but the boundaries may cross
 * the shadow DOM boundaries. This won't exposed to the web.
 */
class CrossShadowBoundaryRange final : public StaticRange,
                                       public nsStubMutationObserver {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_IMETHODIMP_(void) DeleteCycleCollectable(void) override;
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
      CrossShadowBoundaryRange, StaticRange)

  CrossShadowBoundaryRange() = delete;
  explicit CrossShadowBoundaryRange(const StaticRange& aOther) = delete;

  template <typename SPT, typename SRT, typename EPT, typename ERT>
  static already_AddRefed<CrossShadowBoundaryRange> Create(
      const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
      const RangeBoundaryBase<EPT, ERT>& aEndBoundary, nsRange* aOwner);

  void NotifyNodeBecomesShadowHost(nsINode* aNode) {
    if (aNode == mStart.GetContainer()) {
      mStart.NotifyParentBecomesShadowHost();
    }

    if (aNode == mEnd.GetContainer()) {
      mEnd.NotifyParentBecomesShadowHost();
    }
  }

  nsINode* GetCommonAncestor() const { return mCommonAncestor; }

  // Recomputes mCommonAncestor from the current mStart/mEnd and moves the
  // mutation-observer registration to it if it changed. Called from
  // StaticRange::DoSetRange after the boundaries are updated, so it runs for
  // every boundary change rather than only on initial creation.
  void UpdateCommonAncestor();

  /**
   * Return true if the boundaries are in the same range root, i.e., in the
   * same uncomposed document or in the same shadow container.
   */
  [[nodiscard]] bool IsPositionedInSameRangeRoot() const {
    return IsPositioned() &&
           RangeUtils::ComputeRootNode(mStart.GetContainer()) ==
               RangeUtils::ComputeRootNode(mEnd.GetContainer());
  }

  // CrossShadowBoundaryRange should have a very limited usage.
  nsresult SetStartAndEnd(nsINode* aStartContainer, uint32_t aStartOffset,
                          nsINode* aEndContainer, uint32_t aEndOffset) = delete;

  template <typename SPT, typename SRT, typename EPT, typename ERT>
  nsresult SetStartAndEnd(const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
                          const RangeBoundaryBase<EPT, ERT>& aEndBoundary) {
    return StaticRange::SetStartAndEnd(aStartBoundary, aEndBoundary);
  }

  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
  NS_DECL_NSIMUTATIONOBSERVER_PARENTCHAINCHANGED

 private:
  explicit CrossShadowBoundaryRange(nsINode* aNode, nsRange* aOwner)
      : StaticRange(aNode, StaticRange::MutationObserved::Yes,
                    TreeKind::FlatForSelection),
        mOwner(aOwner) {}
  virtual ~CrossShadowBoundaryRange() = default;

  void ResetToReuse();

  // This is either NULL if this CrossShadowBoundaryRange has been
  // reset by Release() or the closest common shadow-including ancestor
  // of mStart and mEnd.
  nsCOMPtr<nsINode> mCommonAncestor;

  static nsTArray<RefPtr<CrossShadowBoundaryRange>>* sCachedRanges;

  friend class AbstractRange;

  // nsRange owns CrossShadowBoundaryRange; it always outlives
  // CrossShadowBoundaryRange, so it's safe to use raw pointer here.
  nsRange* mOwner;
};

inline CrossShadowBoundaryRange* StaticRange::AsCrossShadowBoundaryRange() {
  MOZ_ASSERT(IsCrossShadowBoundaryRange());
  return static_cast<CrossShadowBoundaryRange*>(this);
}

}  // namespace dom
}  // namespace mozilla

#endif  // #ifndef mozilla_dom_CrossShadowBoundaryRange_h
