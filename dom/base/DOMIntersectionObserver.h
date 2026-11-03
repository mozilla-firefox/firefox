/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOMIntersectionObserver_h
#define DOMIntersectionObserver_h

#include "mozilla/Attributes.h"
#include "mozilla/DoublyLinkedList.h"
#include "mozilla/LinkedList.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/Variant.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/IntersectionObserverBinding.h"
#include "mozilla/dom/NodeOwnedObjectList.h"
#include "nsDOMNavigationTiming.h"
#include "nsTArray.h"
#include "nsTHashSet.h"

namespace mozilla::dom {

class DOMIntersectionObserver;

class DOMIntersectionObserverEntry final : public nsISupports,
                                           public nsWrapperCache {
  ~DOMIntersectionObserverEntry() = default;

 public:
  DOMIntersectionObserverEntry(nsISupports* aOwner, DOMHighResTimeStamp aTime,
                               RefPtr<DOMRect> aRootBounds,
                               RefPtr<DOMRect> aBoundingClientRect,
                               RefPtr<DOMRect> aIntersectionRect,
                               bool aIsIntersecting, Element* aTarget,
                               double aIntersectionRatio)
      : mOwner(aOwner),
        mTime(aTime),
        mRootBounds(std::move(aRootBounds)),
        mBoundingClientRect(std::move(aBoundingClientRect)),
        mIntersectionRect(std::move(aIntersectionRect)),
        mIsIntersecting(aIsIntersecting),
        mTarget(aTarget),
        mIntersectionRatio(aIntersectionRatio) {}
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(DOMIntersectionObserverEntry)

  nsISupports* GetParentObject() const { return mOwner; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return IntersectionObserverEntry_Binding::Wrap(aCx, this, aGivenProto);
  }

  DOMHighResTimeStamp Time() const { return mTime; }

  DOMRect* GetRootBounds() { return mRootBounds; }

  DOMRect* BoundingClientRect() { return mBoundingClientRect; }

  DOMRect* IntersectionRect() { return mIntersectionRect; }

  bool IsIntersecting() const { return mIsIntersecting; }

  double IntersectionRatio() const { return mIntersectionRatio; }

  Element* Target() { return mTarget; }

 protected:
  nsCOMPtr<nsISupports> mOwner;
  DOMHighResTimeStamp mTime;
  RefPtr<DOMRect> mRootBounds;
  RefPtr<DOMRect> mBoundingClientRect;
  RefPtr<DOMRect> mIntersectionRect;
  bool mIsIntersecting;
  RefPtr<Element> mTarget;
  double mIntersectionRatio;
};

#define NS_DOM_INTERSECTION_OBSERVER_IID \
  {0x8570a575, 0xe303, 0x4d18, {0xb6, 0xb1, 0x4d, 0x2b, 0x49, 0xd8, 0xef, 0x94}}

using IntersectionObserverMargin = StyleRect<LengthPercentage>;

// An input suitable to compute intersections with multiple targets.
struct IntersectionInput {
  // Whether the root is implicit (null, originally).
  const bool mIsImplicitRoot = false;
  // The computed root node. For the implicit root, this will be the in-process
  // root document we can compute coordinates against (along with the remote
  // document visible rect if appropriate).
  const nsINode* mRootNode = nullptr;
  nsIFrame* mRootFrame = nullptr;
  // The rect of mRootFrame in client coordinates.
  nsRect mRootRect;
  // The root margin computed against the root rect.
  nsMargin mRootMargin;
  // The scroll margin computed against the root rect.
  IntersectionObserverMargin mScrollMargin;
  // If this is in an OOP iframe, the visible rect of the OOP frame.
  Maybe<nsRect> mRemoteDocumentVisibleRect;
};

struct IntersectionOutput {
  const bool mIsSimilarOrigin;
  const nsRect mRootBounds;
  const nsRect mTargetRect;
  const Maybe<nsRect> mIntersectionRect;
  // See aPreservesAxisAlignedRectangles of
  // nsLayoutUtils::TransformFrameRectToAncestor().
  // https://searchfox.org/firefox-main/rev/e2cbda2dd0f622553b5c825f319832db4863f6a4/layout/base/nsLayoutUtils.h#829-830
  const bool mPreservesAxisAlignedRectangles;

  bool Intersects() const { return mIntersectionRect.isSome(); }
};

class DOMIntersectionObserver final
    : public nsISupports,
      public nsWrapperCache,
      public LinkedListElement<DOMIntersectionObserver>,
      public DoublyLinkedListElement<DOMIntersectionObserver> {
  ~DOMIntersectionObserver() {
    NodeOwnedObjectList<DOMIntersectionObserver>::Remove(this);
    Disconnect();
  }

  using NativeCallback = void (*)(
      const Sequence<OwningNonNull<DOMIntersectionObserverEntry>>& aEntries);
  DOMIntersectionObserver(Document&, NativeCallback);

 public:
  DOMIntersectionObserver(already_AddRefed<nsPIDOMWindowInner> aOwner,
                          dom::IntersectionCallback& aCb);
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMIntersectionObserver)
  NS_INLINE_DECL_STATIC_IID(NS_DOM_INTERSECTION_OBSERVER_IID)

  static already_AddRefed<DOMIntersectionObserver> Constructor(
      const GlobalObject&, dom::IntersectionCallback&, ErrorResult&);
  static already_AddRefed<DOMIntersectionObserver> Constructor(
      const GlobalObject&, dom::IntersectionCallback&,
      const IntersectionObserverInit&, ErrorResult&);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return IntersectionObserver_Binding::Wrap(aCx, this, aGivenProto);
  }

  nsISupports* GetParentObject() const;

  nsINode* GetRoot() const { return mRoot; }

  void GetRootMargin(nsACString&);
  bool SetRootMargin(const nsACString&);

  void GetScrollMargin(nsACString&);
  bool SetScrollMargin(const nsACString&);

  void GetThresholds(nsTArray<double>& aRetVal);
  void Observe(Element& aTarget);
  void Unobserve(Element& aTarget);
  [[nodiscard]] bool Observes(Element& aTarget) const;

  void UnlinkTarget(Element& aTarget);
  void Disconnect();

  bool HasOwnerInCCGenerationDocument() const;
  void MarkForCC();

  void TakeRecords(nsTArray<RefPtr<DOMIntersectionObserverEntry>>& aRetVal);

  static IntersectionInput ComputeInputForIframeThrottling(const Document&);
  static IntersectionInput ComputeInput(
      const Document& aDocument, const nsINode* aRoot,
      const StyleRect<LengthPercentage>* aRootMargin,
      const StyleRect<LengthPercentage>* aScrollMargin);

  enum class IsForProximityToViewport : bool { No, Yes };
  enum class BoxToUse : uint8_t {
    Content,
    Border,
    OverflowClip,
  };
  static IntersectionOutput Intersect(
      const IntersectionInput&, const Element&, BoxToUse = BoxToUse::Border,
      IsForProximityToViewport = IsForProximityToViewport::No);
  static IntersectionOutput Intersect(
      const IntersectionInput&, nsIFrame*, BoxToUse = BoxToUse::Border,
      IsForProximityToViewport = IsForProximityToViewport::No);
  // Intersects with a given rect, already relative to the root frame.
  static IntersectionOutput Intersect(const IntersectionInput&, const nsRect&);

  void Update(Document& aDocument, DOMHighResTimeStamp time);
  MOZ_CAN_RUN_SCRIPT void Notify();

  static already_AddRefed<DOMIntersectionObserver> CreateLazyLoadObserver(
      Document&);

  static Maybe<nsRect> EdgeInclusiveIntersection(const nsRect& aRect,
                                                 const nsRect& aOtherRect);

 protected:
  void Connect();
  void QueueIntersectionObserverEntry(Element* aTarget,
                                      DOMHighResTimeStamp time,
                                      const Maybe<nsRect>& aRootRect,
                                      const nsRect& aTargetRect,
                                      const Maybe<nsRect>& aIntersectionRect,
                                      bool aIsIntersecting,
                                      double aIntersectionRatio);

  nsCOMPtr<nsPIDOMWindowInner> mOwner;
  RefPtr<Document> mDocument;
  Variant<RefPtr<dom::IntersectionCallback>, NativeCallback> mCallback;
  RefPtr<nsINode> mRoot;
  StyleRect<LengthPercentage> mRootMargin;
  StyleRect<LengthPercentage> mScrollMargin;
  AutoTArray<double, 1> mThresholds;

  // These hold raw pointers which are explicitly cleared by UnlinkTarget().
  //
  // We keep a set and an array because we need ordered access, but also
  // constant time lookup.
  nsTArray<Element*> mObservationTargets;

  // Value can be:
  //   -2:   Makes sure next calculated threshold always differs, leading to a
  //         notification task being scheduled.
  //   -1:   Non-intersecting.
  //   >= 0: Intersecting, valid index of aObserver->mThresholds.
  enum ObservationState : int32_t { Uninitialized = -2, NotIntersecting = -1 };
  nsTHashMap<Element*, int32_t> mObservationTargetMap;

  nsTArray<RefPtr<DOMIntersectionObserverEntry>> mQueuedEntries;
  bool mConnected = false;
};

}  // namespace mozilla::dom

#endif
