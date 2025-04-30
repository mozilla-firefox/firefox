/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_AbstractRange_h
#define mozilla_dom_AbstractRange_h

#include <cstdint>
#include <ostream>
#include "ErrorList.h"
#include "js/RootingAPI.h"
#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupports.h"
#include "nsWrapperCache.h"

class JSObject;
class nsIContent;
class nsINode;
class nsRange;
struct JSContext;

namespace mozilla::dom {
class Document;
class Selection;
class StaticRange;

enum class AllowRangeCrossShadowBoundary : bool { No, Yes };

class AbstractRange : public nsISupports,
                      public nsWrapperCache,
                      // For linking together selection-associated ranges.
                      public mozilla::LinkedListElement<AbstractRange> {
  using AllowRangeCrossShadowBoundary =
      mozilla::dom::AllowRangeCrossShadowBoundary;

 protected:
  explicit AbstractRange(nsINode* aNode, bool aIsDynamicRange);
  virtual ~AbstractRange();

 public:
  enum class IsUnlinking : bool { No, Yes };

  AbstractRange() = delete;
  explicit AbstractRange(const AbstractRange& aOther) = delete;

  /**
   * Called when the process is shutting down.
   */
  static void Shutdown();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(AbstractRange)

  /**
   * All of the MayCrossShadowBoundary* methods are used to get the boundary
   * endpoints that cross shadow boundaries. They would return
   * the same value as the non-MayCrossShadowBoundary* methods if the range
   * boundaries don't cross shadow boundaries.
   */
  const RangeBoundary& StartRef() const { return mStart; }
  const RangeBoundary& MayCrossShadowBoundaryStartRef() const;

  const RangeBoundary& EndRef() const { return mEnd; }
  const RangeBoundary& MayCrossShadowBoundaryEndRef() const;

  nsIContent* GetChildAtStartOffset() const {
    return mStart.GetChildAtOffset();
  }
  nsIContent* GetMayCrossShadowBoundaryChildAtStartOffset() const;

  nsIContent* GetChildAtEndOffset() const { return mEnd.GetChildAtOffset(); }
  nsIContent* GetMayCrossShadowBoundaryChildAtEndOffset() const;

  bool IsPositioned() const { return mIsPositioned; }
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  nsINode* GetClosestCommonInclusiveAncestor(
      AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary =
          AllowRangeCrossShadowBoundary::No) const;

  // WebIDL

  // If Range is created from JS, it's initialized with Document.createRange()
  // and it collaps the range to start of the Document.  Therefore, the
  // following WebIDL methods are called only when `mIsPositioned` is true.
  // So, it does not make sense to take `ErrorResult` as their parameter
  // since its destruction cost may appear in profile.  If you create range
  // object from C++ and needs to check whether it's positioned, should call
  // `IsPositioned()` directly.

  nsINode* GetStartContainer() const { return mStart.GetContainer(); }
  nsINode* GetMayCrossShadowBoundaryStartContainer() const;

  nsINode* GetEndContainer() const { return mEnd.GetContainer(); }
  nsINode* GetMayCrossShadowBoundaryEndContainer() const;

  /**
   * Return GetStartContainer() and GetEndContainer() if this is positioned.
   */
  [[nodiscard]] bool IsPositionedAndSameContainer() const {
    return MOZ_LIKELY(mIsPositioned) &&
           mStart.GetContainer() == mEnd.GetContainer();
  }
  /**
   * Return GetMayCrossShadowBoundaryStartContainer() and
   * GetMayCrossShadowBoundaryEndContainer() if this is positioned.
   */
  [[nodiscard]] bool IsPositionedAndSameContainerMayCrossShadowBoundary()
      const {
    return MOZ_LIKELY(mIsPositioned) &&
           GetMayCrossShadowBoundaryStartContainer() ==
               GetMayCrossShadowBoundaryEndContainer();
  }

  bool MayCrossShadowBoundary() const;

  Document* GetComposedDocOfContainers() const {
    return mStart.GetComposedDoc();
  }

  // FYI: Returns 0 if it's not positioned.
  uint32_t StartOffset() const {
    return static_cast<uint32_t>(
        *mStart.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets));
  }
  uint32_t MayCrossShadowBoundaryStartOffset() const;

  // FYI: Returns 0 if it's not positioned.
  uint32_t EndOffset() const {
    return static_cast<uint32_t>(
        *mEnd.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets));
  }
  uint32_t MayCrossShadowBoundaryEndOffset() const;

  bool Collapsed() const {
    return !mIsPositioned || (mStart.GetContainer() == mEnd.GetContainer() &&
                              StartOffset() == EndOffset());
  }

  bool AreNormalRangeAndCrossShadowBoundaryRangeCollapsed() const;

  nsINode* GetParentObject() const;
  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  bool HasEqualBoundaries(const AbstractRange& aOther) const {
    return (mStart == aOther.mStart) && (mEnd == aOther.mEnd);
  }
  bool IsDynamicRange() const { return mIsDynamicRange; }
  bool IsStaticRange() const { return !mIsDynamicRange; }
  inline nsRange* AsDynamicRange();
  inline const nsRange* AsDynamicRange() const;
  inline StaticRange* AsStaticRange();
  inline const StaticRange* AsStaticRange() const;

  /**
   * Return true if this range is part of a Selection object
   * and isn't detached.
   */
  bool IsInAnySelection() const { return !mSelections.IsEmpty(); }

  MOZ_CAN_RUN_SCRIPT void RegisterSelection(
      mozilla::dom::Selection& aSelection);

  void UnregisterSelection(const mozilla::dom::Selection& aSelection,
                           IsUnlinking aIsUnlinking = IsUnlinking::No);

  /**
   * Returns a list of all Selections the range is associated with.
   */
  const nsTArray<WeakPtr<Selection>>& GetSelections() const;

  /**
   * Return true if this range is in |aSelection|.
   */
  bool IsInSelection(const mozilla::dom::Selection& aSelection) const;

  /**
   * Return true if aRoot is a UA shadow root.
   */
  static bool IsRootUAWidget(const nsINode* aRoot);

 protected:
  template <typename SPT, typename SRT, typename EPT, typename ERT,
            typename RangeType>
  static nsresult SetStartAndEndInternal(
      const RangeBoundaryBase<SPT, SRT>& aStartBoundary,
      const RangeBoundaryBase<EPT, ERT>& aEndBoundary, RangeType* aRange);

  template <class RangeType>
  static bool MaybeCacheToReuse(RangeType& aInstance);

  void Init(nsINode* aNode);

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const AbstractRange& aRange) {
    if (aRange.Collapsed()) {
      aStream << "{ mStart=mEnd=" << aRange.mStart;
    } else {
      aStream << "{ mStart=" << aRange.mStart << ", mEnd=" << aRange.mEnd;
    }
    return aStream << ", mIsGenerated="
                   << (aRange.mIsGenerated ? "true" : "false")
                   << ", mCalledByJS="
                   << (aRange.mIsPositioned ? "true" : "false")
                   << ", mIsDynamicRange="
                   << (aRange.mIsDynamicRange ? "true" : "false") << " }";
  }

  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  void RegisterClosestCommonInclusiveAncestor(nsINode* aNode);
  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor
   */
  void UnregisterClosestCommonInclusiveAncestor(
      IsUnlinking aIsUnlinking = IsUnlinking::No);

  void UpdateCommonAncestorIfNecessary();

  static void MarkDescendants(const nsINode& aNode);
  static void UnmarkDescendants(const nsINode& aNode);

 private:
  void ClearForReuse();

 protected:
  RefPtr<Document> mOwner;
  RangeBoundary mStart;
  RangeBoundary mEnd;

  // A Range can be part of multiple |Selection|s. This is a very rare use case.
  AutoTArray<WeakPtr<Selection>, 1> mSelections;
  // mRegisteredClosestCommonInclusiveAncestor is only non-null when the range
  // IsInAnySelection().
  nsCOMPtr<nsINode> mRegisteredClosestCommonInclusiveAncestor;

  // `true` if `mStart` and `mEnd` are set for StaticRange or set and valid
  // for nsRange.
  bool mIsPositioned;

  // Used by nsRange, but this should have this for minimizing the size.
  bool mIsGenerated;
  // Used by nsRange, but this should have this for minimizing the size.
  bool mCalledByJS;

  // true if this is an `nsRange` object.
  const bool mIsDynamicRange;

  static bool sHasShutDown;
};

}  // namespace mozilla::dom

#endif  // #ifndef mozilla_dom_AbstractRange_h
