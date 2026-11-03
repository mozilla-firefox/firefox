/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMIntersectionObserver.h"

#include "NonCustomCSSPropertyId.h"
#include "Units.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "nsCCUncollectableMarker.h"
#include "nsContainerFrame.h"
#include "nsContentUtils.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsRefreshDriver.h"

namespace mozilla::dom {

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMIntersectionObserverEntry)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMIntersectionObserverEntry)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMIntersectionObserverEntry)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DOMIntersectionObserverEntry, mOwner,
                                      mRootBounds, mBoundingClientRect,
                                      mIntersectionRect, mTarget)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DOMIntersectionObserver)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(DOMIntersectionObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(DOMIntersectionObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DOMIntersectionObserver)

NS_IMPL_CYCLE_COLLECTION_CLASS(DOMIntersectionObserver)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(DOMIntersectionObserver)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DOMIntersectionObserver)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->Disconnect();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocument)
  if (tmp->mCallback.is<RefPtr<dom::IntersectionCallback>>()) {
    ImplCycleCollectionUnlink(
        tmp->mCallback.as<RefPtr<dom::IntersectionCallback>>());
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRoot)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mQueuedEntries)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DOMIntersectionObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument)
  if (tmp->mCallback.is<RefPtr<dom::IntersectionCallback>>()) {
    ImplCycleCollectionTraverse(
        cb, tmp->mCallback.as<RefPtr<dom::IntersectionCallback>>(), "mCallback",
        0);
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRoot)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mQueuedEntries)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

DOMIntersectionObserver::DOMIntersectionObserver(
    already_AddRefed<nsPIDOMWindowInner> aOwner, dom::IntersectionCallback& aCb)
    : mOwner(aOwner),
      mDocument(mOwner->GetExtantDoc()),
      mCallback(RefPtr<dom::IntersectionCallback>(&aCb)) {
  NodeOwnedObjectList<DOMIntersectionObserver>::Add(this);
}

bool DOMIntersectionObserver::HasOwnerInCCGenerationDocument() const {
  for (const Element* target : mObservationTargets) {
    if (nsCCUncollectableMarker::IsInCCGenerationDocument(*target)) {
      return true;
    }
  }
  return false;
}

void DOMIntersectionObserver::MarkForCC() {
  if (dom::IntersectionCallback* callback =
          mCallback.as<RefPtr<dom::IntersectionCallback>>()) {
    callback->MarkForCC();
  }
}

already_AddRefed<DOMIntersectionObserver> DOMIntersectionObserver::Constructor(
    const GlobalObject& aGlobal, dom::IntersectionCallback& aCb,
    ErrorResult& aRv) {
  return Constructor(aGlobal, aCb, IntersectionObserverInit(), aRv);
}

// https://w3c.github.io/IntersectionObserver/#initialize-new-intersection-observer
already_AddRefed<DOMIntersectionObserver> DOMIntersectionObserver::Constructor(
    const GlobalObject& aGlobal, dom::IntersectionCallback& aCb,
    const IntersectionObserverInit& aOptions, ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // 1. Let this be a new IntersectionObserver object
  // 2. Set this’s internal [[callback]] slot to callback.
  RefPtr<DOMIntersectionObserver> observer =
      new DOMIntersectionObserver(window.forget(), aCb);

  if (!aOptions.mRoot.IsNull()) {
    if (aOptions.mRoot.Value().IsElement()) {
      observer->mRoot = aOptions.mRoot.Value().GetAsElement();
    } else {
      MOZ_ASSERT(aOptions.mRoot.Value().IsDocument());
      observer->mRoot = aOptions.mRoot.Value().GetAsDocument();
    }
  }

  // 3. Attempt to parse a margin from options.rootMargin. If a list is
  // returned, set this’s internal [[rootMargin]] slot to that. Otherwise, throw
  // a SyntaxError exception.
  if (!observer->SetRootMargin(aOptions.mRootMargin)) {
    aRv.ThrowSyntaxError("rootMargin must be specified in pixels or percent.");
    return nullptr;
  }

  // 4. Attempt to parse a margin from options.scrollMargin. If a list is
  // returned, set this’s internal [[scrollMargin]] slot to that. Otherwise,
  // throw a SyntaxError exception.
  if (!observer->SetScrollMargin(aOptions.mScrollMargin)) {
    aRv.ThrowSyntaxError(
        "scrollMargin must be specified in pixels or percent.");
    return nullptr;
  }

  // 5. Let thresholds be a list equal to options.threshold.
  if (aOptions.mThreshold.IsDoubleSequence()) {
    const Sequence<double>& thresholds =
        aOptions.mThreshold.GetAsDoubleSequence();
    observer->mThresholds.SetCapacity(thresholds.Length());

    // 6. If any value in thresholds is less than 0.0 or greater than 1.0, throw
    // a RangeError exception.
    for (const auto& thresh : thresholds) {
      if (thresh < 0.0 || thresh > 1.0) {
        aRv.ThrowRangeError<dom::MSG_THRESHOLD_RANGE_ERROR>();
        return nullptr;
      }
      observer->mThresholds.AppendElement(thresh);
    }

    // 7. Sort thresholds in ascending order.
    observer->mThresholds.Sort();

    // 8. If thresholds is empty, append 0 to thresholds.
    if (observer->mThresholds.IsEmpty()) {
      observer->mThresholds.AppendElement(0.0);
    }
  } else {
    double thresh = aOptions.mThreshold.GetAsDouble();
    if (thresh < 0.0 || thresh > 1.0) {
      aRv.ThrowRangeError<dom::MSG_THRESHOLD_RANGE_ERROR>();
      return nullptr;
    }
    observer->mThresholds.AppendElement(thresh);
  }

  // 9. The thresholds attribute getter will return this sorted thresholds list.
  // (This is implicit given `observer->mThresholds`)

  // 10. Let delay be the value of options.delay.
  // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1896900

  // 11. If options.trackVisibility is true and delay is less than 100, set
  // delay to 100.
  // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1896900

  // 12. Set this’s internal [[delay]] slot to options.delay to delay.
  // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1896900

  // 13. Set this’s internal [[trackVisibility]] slot to
  // options.trackVisibility.
  // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1896900

  // 14. Return this.
  return observer.forget();
}

static void LazyLoadCallback(
    const Sequence<OwningNonNull<DOMIntersectionObserverEntry>>& aEntries) {
  for (const auto& entry : aEntries) {
    Element* target = entry->Target();
    if (entry->IsIntersecting()) {
      if (auto* image = HTMLImageElement::FromNode(target)) {
        image->StopLazyLoading(HTMLImageElement::StartLoad::Yes);
      } else if (auto* iframe = HTMLIFrameElement::FromNode(target)) {
        iframe->StopLazyLoading(HTMLIFrameElement::TriggerLoad::Yes);
      } else if (auto* media = HTMLMediaElement::FromNode(target)) {
        media->StopLazyLoading(HTMLMediaElement::StartLoad::Yes);
      } else {
        MOZ_ASSERT_UNREACHABLE(
            "Only <img>, <iframe>, <video>, and <audio> should be observed by "
            "lazy load observer");
      }
    }
  }
}

static LengthPercentage PrefMargin(float aValue, bool aIsPercentage) {
  return aIsPercentage ? LengthPercentage::FromPercentage(aValue / 100.0f)
                       : LengthPercentage::FromPixels(aValue);
}

static IntersectionObserverMargin LazyLoadingMargin() {
  IntersectionObserverMargin margin;
#define SET_MARGIN(side_, side_lower_)                      \
  margin.Get(eSide##side_) = PrefMargin(                    \
      StaticPrefs::dom_lazy_loading_margin_##side_lower_(), \
      StaticPrefs::dom_lazy_loading_margin_##side_lower_##_percentage());
  SET_MARGIN(Top, top);
  SET_MARGIN(Right, right);
  SET_MARGIN(Bottom, bottom);
  SET_MARGIN(Left, left);
#undef SET_MARGIN
  return margin;
}

DOMIntersectionObserver::DOMIntersectionObserver(Document& aDocument,
                                                 NativeCallback aCallback)
    : mOwner(aDocument.GetInnerWindow()),
      mDocument(&aDocument),
      mCallback(aCallback) {}

already_AddRefed<DOMIntersectionObserver>
DOMIntersectionObserver::CreateLazyLoadObserver(Document& aDocument) {
  RefPtr<DOMIntersectionObserver> observer =
      new DOMIntersectionObserver(aDocument, LazyLoadCallback);
  observer->mThresholds.AppendElement(0.0f);
  auto* margin = StaticPrefs::dom_lazy_loading_margin_is_scroll()
                     ? &observer->mScrollMargin
                     : &observer->mRootMargin;
  *margin = LazyLoadingMargin();
  return observer.forget();
}

bool DOMIntersectionObserver::SetRootMargin(const nsACString& aString) {
  return Servo_IntersectionObserverMargin_Parse(&aString, &mRootMargin);
}

bool DOMIntersectionObserver::SetScrollMargin(const nsACString& aString) {
  return Servo_IntersectionObserverMargin_Parse(&aString, &mScrollMargin);
}

nsISupports* DOMIntersectionObserver::GetParentObject() const { return mOwner; }

void DOMIntersectionObserver::GetRootMargin(nsACString& aRetVal) {
  Servo_IntersectionObserverMargin_ToString(&mRootMargin, &aRetVal);
}

void DOMIntersectionObserver::GetScrollMargin(nsACString& aRetVal) {
  Servo_IntersectionObserverMargin_ToString(&mScrollMargin, &aRetVal);
}

void DOMIntersectionObserver::GetThresholds(nsTArray<double>& aRetVal) {
  aRetVal = mThresholds.Clone();
}

bool DOMIntersectionObserver::Observes(Element& aTarget) const {
  return mObservationTargetMap.Contains(&aTarget);
}

// https://w3c.github.io/IntersectionObserver/#observe-target-element
void DOMIntersectionObserver::Observe(Element& aTarget) {
  // 1. If target is in observer’s internal [[ObservationTargets]] slot, return.
  bool wasPresent =
      mObservationTargetMap.WithEntryHandle(&aTarget, [](auto handle) {
        if (handle.HasEntry()) {
          return true;
        }
        handle.Insert(Uninitialized);
        return false;
      });
  if (wasPresent) {
    return;
  }

  // 2. Let intersectionObserverRegistration be an
  // IntersectionObserverRegistration record with an observer property set to
  // observer, a previousThresholdIndex property set to -1, a
  // previousIsIntersecting property set to false, and a previousIsVisible
  // property set to false.
  // 3. Append intersectionObserverRegistration to target’s internal
  // [[RegisteredIntersectionObservers]] slot.
  // 4. Add target to observer’s internal [[ObservationTargets]] slot.
  aTarget.BindObject(this, [](nsISupports* aObserver, nsINode* aTarget) {
    static_cast<DOMIntersectionObserver*>(aObserver)->UnlinkTarget(
        *aTarget->AsElement());
  });
  mObservationTargets.AppendElement(&aTarget);

  MOZ_ASSERT(mObservationTargets.Length() == mObservationTargetMap.Count());

  Connect();
  if (mDocument) {
    if (nsPresContext* pc = mDocument->GetPresContext()) {
      pc->RefreshDriver()->EnsureIntersectionObservationsUpdateHappens();
    }
  }
}

// https://w3c.github.io/IntersectionObserver/#unobserve-target-element
void DOMIntersectionObserver::Unobserve(Element& aTarget) {
  // 1. Remove the IntersectionObserverRegistration record whose observer
  // property is equal to this from target’s internal
  // [[RegisteredIntersectionObservers]] slot, if present.
  if (!mObservationTargetMap.Remove(&aTarget)) {
    return;
  }

  // 2. Remove target from this’s internal [[ObservationTargets]] slot, if
  // present
  mObservationTargets.RemoveElement(&aTarget);

  aTarget.UnbindObject(this);

  MOZ_ASSERT(mObservationTargets.Length() == mObservationTargetMap.Count());

  if (mObservationTargets.IsEmpty()) {
    Disconnect();
  }
}

// Inner step of
// https://w3c.github.io/IntersectionObserver/#dom-intersectionobserver-disconnect
void DOMIntersectionObserver::UnlinkTarget(Element& aTarget) {
  // 1. Remove the IntersectionObserverRegistration record whose observer
  // property is equal to this from target’s internal
  // [[RegisteredIntersectionObservers]] slot.
  // 2. Remove target from this’s internal [[ObservationTargets]] slot.
  mObservationTargets.RemoveElement(&aTarget);
  mObservationTargetMap.Remove(&aTarget);

  if (mObservationTargets.IsEmpty()) {
    Disconnect();
  }
}

void DOMIntersectionObserver::Connect() {
  if (mConnected) {
    return;
  }

  mConnected = true;
  if (mDocument) {
    mDocument->AddIntersectionObserver(*this);
  }
}

// https://w3c.github.io/IntersectionObserver/#dom-intersectionobserver-disconnect
void DOMIntersectionObserver::Disconnect() {
  if (!mConnected) {
    return;
  }

  mConnected = false;
  // 1. For each target in this’s internal [[ObservationTargets]] slot:
  for (Element* target : mObservationTargets) {
    // 2. Remove the IntersectionObserverRegistration record whose observer
    // property is equal to this from target’s internal
    // [[RegisteredIntersectionObservers]] slot.
    // 3. Remove target from this’s internal [[ObservationTargets]] slot.
    target->UnbindObject(this);
  }

  mObservationTargets.Clear();
  mObservationTargetMap.Clear();
  if (mDocument) {
    mDocument->RemoveIntersectionObserver(*this);
  }
}

// https://w3c.github.io/IntersectionObserver/#dom-intersectionobserver-takerecords
void DOMIntersectionObserver::TakeRecords(
    nsTArray<RefPtr<DOMIntersectionObserverEntry>>& aRetVal) {
  aRetVal = std::move(mQueuedEntries);
}

enum class BrowsingContextOrigin { Similar, Different };

// NOTE(emilio): Checking docgroup as per discussion in:
// https://github.com/w3c/IntersectionObserver/issues/161
static BrowsingContextOrigin SimilarOrigin(const nsIContent& aTarget,
                                           const nsINode* aRoot) {
  if (!aRoot) {
    return BrowsingContextOrigin::Different;
  }
  return aTarget.OwnerDoc()->GetDocGroup() == aRoot->OwnerDoc()->GetDocGroup()
             ? BrowsingContextOrigin::Similar
             : BrowsingContextOrigin::Different;
}

// NOTE: This returns nullptr if |aDocument| is in another process from the top
// level content document.
static const Document* GetTopLevelContentDocumentInThisProcess(
    const Document& aDocument) {
  auto* wc = aDocument.GetTopLevelWindowContext();
  return wc ? wc->GetExtantDoc() : nullptr;
}

static nsMargin ResolveMargin(const IntersectionObserverMargin& aMargin,
                              const nsSize& aPercentBasis) {
  nsMargin margin;
  for (const auto side : mozilla::AllPhysicalSides()) {
    nscoord basis = side == eSideTop || side == eSideBottom
                        ? aPercentBasis.Height()
                        : aPercentBasis.Width();
    margin.Side(side) = aMargin.Get(side).Resolve(
        basis, static_cast<nscoord (*)(float)>(NSToCoordRoundWithClamp));
  }
  return margin;
}

// https://w3c.github.io/IntersectionObserver/#compute-the-intersection
//
// TODO(emilio): Proof of this being equivalent to the spec welcome, seems
// reasonably close.
//
// Also, it's unclear to me why the spec talks about browsing context while
// discarding observations of targets of different documents.
//
// Both aRootBounds and the return value are relative to
// nsLayoutUtils::GetContainingBlockForClientRect(aRoot).
//
// In case of out-of-process document, aRemoteDocumentVisibleRect is a rectangle
// in the out-of-process document's coordinate system.
static Maybe<nsRect> ComputeTheIntersection(
    nsIFrame* aTarget, const nsRect& aTargetRectRelativeToTarget,
    nsIFrame* aRoot, const nsRect& aRootBounds,
    const IntersectionObserverMargin& aScrollMargin,
    const Maybe<nsRect>& aRemoteDocumentVisibleRect,
    DOMIntersectionObserver::IsForProximityToViewport aIsForProximityToViewport,
    bool* aPreservesAxisAlignedRectangles) {
  nsIFrame* target = aTarget;
  // 1. Let intersectionRect be the result of running the
  // getBoundingClientRect() algorithm on the target.
  //
  // `intersectionRect` is kept relative to `target` during the loop.
  auto inflowRect = aTargetRectRelativeToTarget;
  Maybe<nsRect> intersectionRect = Some(inflowRect);

  // 2. Let container be the containing block of the target.
  // (We go through the parent chain and only look at scroll frames)
  //
  // FIXME(emilio): Spec uses containing blocks, we use scroll frames, but we
  // only apply overflow-clipping, not clip-path, so it's ~fine. We do need to
  // apply clip-path.
  //
  // 3. While container is not the intersection root:
  nsIFrame* containerFrame =
      nsLayoutUtils::GetCrossDocParentFrameInProcess(target);
  while (containerFrame && containerFrame != aRoot) {
    // FIXME(emilio): What about other scroll frames that inherit from
    // ScrollContainerFrame but have a different type, like nsListControlFrame?
    // This looks bogus in that case, but different bug.
    if (ScrollContainerFrame* scrollContainerFrame =
            do_QueryFrame(containerFrame)) {
      if (containerFrame->GetParent() == aRoot && !aRoot->GetParent()) {
        // This is subtle: if we're computing the intersection against the
        // viewport (the root frame), and this is its scroll frame, we really
        // want to skip this intersection (because we want to account for the
        // root margin, which is already in aRootBounds).
        break;
      }
      nsRect subFrameRect =
          scrollContainerFrame->GetScrollPortRectAccountingForDynamicToolbar();

      // 3.1 If container is the document of a nested browsing context, update
      // intersectionRect by clipping to the viewport of the document, and
      // update container to be the browsing context container of container.
      // XXX: Handled below by aRemoteDocumentVisibleRect & walking
      // CrossDocParentFrame.

      // 3.2 Map intersectionRect to the coordinate space of container.
      bool preservesAxisAlignedRectangles = false;
      nsRect intersectionRectRelativeToContainer =
          nsLayoutUtils::TransformFrameRectToAncestor(
              target, intersectionRect.value(), containerFrame,
              &preservesAxisAlignedRectangles);
      if (aPreservesAxisAlignedRectangles) {
        *aPreservesAxisAlignedRectangles |= preservesAxisAlignedRectangles;
      }

      // 3.3 If container is a scroll container, apply the
      // IntersectionObserver’s [[scrollMargin]] to the container’s clip rect as
      // described in apply scroll margin to a scrollport.
      subFrameRect.Inflate(ResolveMargin(aScrollMargin, subFrameRect.Size()));

      intersectionRect =
          intersectionRectRelativeToContainer.EdgeInclusiveIntersection(
              subFrameRect);
      if (!intersectionRect) {
        return Nothing();
      }
      target = containerFrame;
    } else {
      const auto& disp = *containerFrame->StyleDisplay();
      auto clipAxes = containerFrame->ShouldApplyOverflowClipping(&disp);
      // 3.4 If container has a content clip or a css clip-path property, update
      // intersectionRect by applying container’s clip.

      // 3.4 TODO: Apply clip-path.
      if (!clipAxes.isEmpty()) {
        // 3.2 Map intersectionRect to the coordinate space of container.
        bool preservesAxisAlignedRectangles = false;
        const nsRect intersectionRectRelativeToContainer =
            nsLayoutUtils::TransformFrameRectToAncestor(
                target, intersectionRect.value(), containerFrame,
                &preservesAxisAlignedRectangles);
        if (aPreservesAxisAlignedRectangles) {
          *aPreservesAxisAlignedRectangles |= preservesAxisAlignedRectangles;
        }
        const nsRect clipRect = OverflowAreas::GetOverflowClipRect(
            intersectionRectRelativeToContainer,
            containerFrame->GetRectRelativeToSelf(), clipAxes,
            containerFrame->OverflowClipMargin(clipAxes));
        intersectionRect =
            intersectionRectRelativeToContainer.EdgeInclusiveIntersection(
                clipRect);
        if (!intersectionRect) {
          return Nothing();
        }
        target = containerFrame;
      }
    }
    // Preserve the popup's own clip, but skip ancestor clips since popups
    // can extend outside their embedding document's viewport.
    if (containerFrame->IsMenuPopupFrame()) {
      break;
    }
    // 3.5 If container is the root element of a browsing context, update
    // container to be the browsing context’s document; otherwise, update
    // container to be the containing block of container.
    containerFrame =
        nsLayoutUtils::GetCrossDocParentFrameInProcess(containerFrame);
  }
  MOZ_ASSERT(intersectionRect);

  // 4. Map intersectionRect to the coordinate space of the intersection root.
  bool preservesAxisAlignedRectangles = false;
  nsRect intersectionRectRelativeToRoot =
      nsLayoutUtils::TransformFrameRectToAncestor(
          target, intersectionRect.value(),
          nsLayoutUtils::GetContainingBlockForClientRect(aRoot),
          &preservesAxisAlignedRectangles);
  if (aPreservesAxisAlignedRectangles) {
    *aPreservesAxisAlignedRectangles |= preservesAxisAlignedRectangles;
  }

  // 5.Update intersectionRect by intersecting it with the root intersection
  // rectangle.
  //
  // In out-of-process iframes we need to take an intersection with the remote
  // document visible rect which was already clipped by ancestor document's
  // viewports.
  if (aRemoteDocumentVisibleRect) {
    MOZ_ASSERT(aRoot->PresContext()->IsRootContentDocumentInProcess() &&
               !aRoot->PresContext()->IsRootContentDocumentCrossProcess());

    intersectionRect = intersectionRectRelativeToRoot.EdgeInclusiveIntersection(
        *aRemoteDocumentVisibleRect);
  } else if (aTarget->HasAnyStateBits(NS_FRAME_IN_POPUP)) {
    // Popups don't get clipped to the viewport, so avoid applying the root
    // intersection rect, see bug 1991410.
    intersectionRect = Some(intersectionRectRelativeToRoot);
  } else {
    intersectionRect =
        intersectionRectRelativeToRoot.EdgeInclusiveIntersection(aRootBounds);
  }

  if (intersectionRect.isNothing()) {
    return Nothing();
  }
  // 6. Map intersectionRect to the coordinate space of the viewport of the
  // Document containing the target.
  //
  // FIXME(emilio): I think this may not be correct if the root is explicit
  // and in the same document, since then the rectangle may not be relative to
  // the viewport already (but it's in the same document).
  nsRect rect = intersectionRect.value();
  if (aTarget->PresContext() != aRoot->PresContext()) {
    if (nsIFrame* rootScrollContainerFrame =
            aTarget->PresShell()->GetRootScrollContainerFrame()) {
      nsLayoutUtils::TransformRect(aRoot, rootScrollContainerFrame, rect);
    }
  }

  // 7. Return intersectionRect.
  return Some(rect);
}

struct OopIframeMetrics {
  nsIFrame* mInProcessRootFrame = nullptr;
  nsRect mInProcessRootRect;
  nsRect mRemoteDocumentVisibleRect;
};

static Maybe<OopIframeMetrics> GetOopIframeMetrics(
    const Document& aDocument, const Document* aRootDocument) {
  const Document* rootDoc =
      nsContentUtils::GetInProcessSubtreeRootDocument(&aDocument);
  MOZ_ASSERT(rootDoc);

  if (rootDoc->IsTopLevelContentDocument()) {
    return Nothing();
  }

  if (aRootDocument &&
      rootDoc ==
          nsContentUtils::GetInProcessSubtreeRootDocument(aRootDocument)) {
    // aRootDoc, if non-null, is either the implicit root
    // (top-level-content-document) or a same-origin document passed explicitly.
    //
    // In the former case, we should've returned above if there are no iframes
    // in between. This condition handles the explicit, same-origin root
    // document, when both are embedded in an OOP iframe.
    return Nothing();
  }

  PresShell* rootPresShell = rootDoc->GetPresShell();
  if (!rootPresShell || rootPresShell->IsDestroying()) {
    return Some(OopIframeMetrics{});
  }

  nsIFrame* inProcessRootFrame = rootPresShell->GetRootFrame();
  if (!inProcessRootFrame) {
    return Some(OopIframeMetrics{});
  }

  BrowserChild* browserChild = BrowserChild::GetFrom(rootDoc->GetDocShell());
  if (!browserChild) {
    return Some(OopIframeMetrics{});
  }

  if (MOZ_UNLIKELY(NS_WARN_IF(browserChild->IsTopLevel()))) {
    // FIXME(bug 1772083): This can be hit with popups, e.g. in
    // html/browsers/the-window-object/open-close/no_window_open_when_term_nesting_level_nonzero.window.html
    // temporarily while opening a new popup (on the about:blank doc).
    // MOZ_ASSERT_UNREACHABLE("Top level BrowserChild but non-top level doc?");
    return Nothing();
  }

  nsRect inProcessRootRect;
  if (ScrollContainerFrame* rootScrollContainerFrame =
          rootPresShell->GetRootScrollContainerFrame()) {
    inProcessRootRect = rootScrollContainerFrame
                            ->GetScrollPortRectAccountingForDynamicToolbar();
  }

  Maybe<LayoutDeviceRect> remoteDocumentVisibleRect =
      browserChild->GetTopLevelViewportVisibleRectInSelfCoords();
  if (!remoteDocumentVisibleRect) {
    return Some(OopIframeMetrics{});
  }

  return Some(OopIframeMetrics{
      inProcessRootFrame,
      inProcessRootRect,
      LayoutDeviceRect::ToAppUnits(
          *remoteDocumentVisibleRect,
          rootPresShell->GetPresContext()->AppUnitsPerDevPixel()),
  });
}

IntersectionInput DOMIntersectionObserver::ComputeInputForIframeThrottling(
    const Document& aEmbedderDocument) {
  auto margin = LazyLoadingMargin();
  // TODO: Consider not using exactly the same parameters as lazy-load?
  const bool useScroll = StaticPrefs::dom_lazy_loading_margin_is_scroll();
  return ComputeInput(aEmbedderDocument, /* aRoot = */ nullptr,
                      /* aRootMargin = */ useScroll ? nullptr : &margin,
                      /* aScrollMargin = */ useScroll ? &margin : nullptr);
}

// https://w3c.github.io/IntersectionObserver/#update-intersection-observations-algo
// step 2.1
IntersectionInput DOMIntersectionObserver::ComputeInput(
    const Document& aDocument, const nsINode* aRoot,
    const IntersectionObserverMargin* aRootMargin,
    const IntersectionObserverMargin* aScrollMargin) {
  // 1 - Let rootBounds be observer's root intersection rectangle.
  //  ... but since the intersection rectangle depends on the target, we defer
  //      the inflation until later.
  // NOTE: |rootRect| and |rootFrame| will be root in the same process. In
  // out-of-process iframes, they are NOT root ones of the top level content
  // document.
  nsRect rootRect;
  nsIFrame* rootFrame = nullptr;
  const nsINode* root = aRoot;
  const bool isImplicitRoot = !aRoot;
  Maybe<nsRect> remoteDocumentVisibleRect;
  if (aRoot && aRoot->IsElement()) {
    if ((rootFrame = aRoot->AsElement()->GetPrimaryFrame())) {
      nsIFrame* containingBlock =
          nsLayoutUtils::GetContainingBlockForClientRect(rootFrame);
      if (ScrollContainerFrame* scrollContainerFrame =
              do_QueryFrame(rootFrame)) {
        // rootRect should be the content rect of rootFrame, not including the
        // scrollbars.
        rootRect = nsLayoutUtils::TransformFrameRectToAncestor(
            rootFrame,
            scrollContainerFrame
                ->GetScrollPortRectAccountingForDynamicToolbar(),
            containingBlock);
      } else {
        // rootRect should be the border rect of rootFrame.
        rootRect = nsLayoutUtils::GetAllInFlowRectsUnion(
            rootFrame, containingBlock,
            nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms);
      }
    }
  } else {
    MOZ_ASSERT(!aRoot || aRoot->IsDocument());
    const Document* rootDocument =
        aRoot ? aRoot->AsDocument()
              : GetTopLevelContentDocumentInThisProcess(aDocument);
    root = rootDocument;

    if (rootDocument) {
      // We're in the same process as the root document, though note that there
      // could be an out-of-process iframe in between us and the root. Grab the
      // root frame and the root rect.
      //
      // Note that the root rect is always good (we assume no DPI changes in
      // between the two documents, and we don't need to convert coordinates).
      //
      // The root frame however we may need to tweak in the block below, if
      // there's any OOP iframe in between `rootDocument` and `aDocument`, to
      // handle the OOP iframe positions.
      if (PresShell* presShell = rootDocument->GetPresShell()) {
        rootFrame = presShell->GetRootFrame();
        // We use the root scroll container frame's scroll port to account the
        // scrollbars in rootRect, if needed.
        if (ScrollContainerFrame* rootScrollContainerFrame =
                presShell->GetRootScrollContainerFrame()) {
          rootRect = rootScrollContainerFrame
                         ->GetScrollPortRectAccountingForDynamicToolbar();
        } else if (rootFrame) {
          rootRect = rootFrame->GetRectRelativeToSelf();
        }
      }
    }

    if (Maybe<OopIframeMetrics> metrics =
            GetOopIframeMetrics(aDocument, rootDocument)) {
      rootFrame = metrics->mInProcessRootFrame;
      if (!rootDocument) {
        rootRect = metrics->mInProcessRootRect;
      }
      remoteDocumentVisibleRect = Some(metrics->mRemoteDocumentVisibleRect);
    }
  }

  nsMargin rootMargin;  // This root margin is NOT applied in `implicit root`
                        // case, e.g. in out-of-process iframes.
  if (aRootMargin) {
    rootMargin = ResolveMargin(*aRootMargin, rootRect.Size());
  }

  return {isImplicitRoot,
          root,
          rootFrame,
          rootRect,
          rootMargin,
          aScrollMargin ? *aScrollMargin : IntersectionObserverMargin(),
          remoteDocumentVisibleRect};
}

IntersectionOutput DOMIntersectionObserver::Intersect(
    const IntersectionInput& aInput, const Element& aTarget, BoxToUse aBoxToUse,
    IsForProximityToViewport aIsForProximityToViewport) {
  nsIFrame* targetFrame = aTarget.GetPrimaryFrame();
  if (!targetFrame) {
    return {SimilarOrigin(aTarget, aInput.mRootNode) ==
            BrowsingContextOrigin::Similar};
  }
  return Intersect(aInput, targetFrame, aBoxToUse, aIsForProximityToViewport);
}

// https://w3c.github.io/IntersectionObserver/#update-intersection-observations-algo
// (steps 2.1 - 2.5)
IntersectionOutput DOMIntersectionObserver::Intersect(
    const IntersectionInput& aInput, nsIFrame* aTargetFrame, BoxToUse aBoxToUse,
    IsForProximityToViewport aIsForProximityToViewport) {
  MOZ_ASSERT(aTargetFrame);

  const nsIContent* target = aTargetFrame->GetContent();
  const bool isSimilarOrigin =
      target && SimilarOrigin(*target, aInput.mRootNode) ==
                    BrowsingContextOrigin::Similar;
  if (!aInput.mRootFrame) {
    return {isSimilarOrigin};
  }

  // "From the perspective of an IntersectionObserver, the skipped contents
  // of an element are never intersecting the intersection root. This is
  // true even if both the root and the target elements are in the skipped
  // contents."
  // https://drafts.csswg.org/css-contain/#cv-notes
  //
  // Skip the intersection if the element is hidden, unless this is the
  // specifically to determine the proximity to the viewport for
  // `content-visibility: auto` elements.
  if (aIsForProximityToViewport == IsForProximityToViewport::No &&
      aTargetFrame->IsHiddenByContentVisibilityOnAnyAncestor()) {
    return {isSimilarOrigin};
  }

  // 2.2. If the intersection root is not the implicit root, and target is
  // not in the same Document as the intersection root, skip to step 11.
  if (!aInput.mIsImplicitRoot &&
      aInput.mRootNode->OwnerDoc() != target->OwnerDoc()) {
    return {isSimilarOrigin};
  }

  // 2.3. If the intersection root is an element and target is not a descendant
  // of the intersection root in the containing block chain, skip to step 11.
  //
  // NOTE(emilio): We also do this if target is the implicit root, pending
  // clarification in
  // https://github.com/w3c/IntersectionObserver/issues/456.
  if (aInput.mRootFrame == aTargetFrame ||
      !nsLayoutUtils::IsAncestorFrameCrossDocInProcessConsideringContinuations(
          aInput.mRootFrame, aTargetFrame)) {
    return {isSimilarOrigin};
  }

  nsRect rootBounds = aInput.mRootRect;
  if (isSimilarOrigin) {
    rootBounds.Inflate(aInput.mRootMargin);

    // Implicit roots should apply the scrollMargin as well:
    if (aInput.mIsImplicitRoot) {
      rootBounds.Inflate(
          ResolveMargin(aInput.mScrollMargin, aInput.mRootRect.Size()));
    }
  }

  // 2.4. Set targetRect to the DOMRectReadOnly obtained by running the
  // getBoundingClientRect() algorithm on target. We compute the box relative to
  // self first, then transform.
  nsLayoutUtils::GetAllInFlowRectsFlags flags{
      nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms};
  if (aBoxToUse == BoxToUse::Content) {
    flags += nsLayoutUtils::GetAllInFlowRectsFlag::UseContentBox;
  }
  nsRect targetRectRelativeToTarget =
      nsLayoutUtils::GetAllInFlowRectsUnion(aTargetFrame, aTargetFrame, flags);

  if (aBoxToUse == BoxToUse::OverflowClip) {
    const auto& disp = *aTargetFrame->StyleDisplay();
    auto clipAxes = aTargetFrame->ShouldApplyOverflowClipping(&disp);
    if (!clipAxes.isEmpty()) {
      targetRectRelativeToTarget = OverflowAreas::GetOverflowClipRect(
          targetRectRelativeToTarget, targetRectRelativeToTarget, clipAxes,
          aTargetFrame->OverflowClipMargin(clipAxes,
                                           /* aAllowNegative = */ false));
    }
  }

  auto targetRect = nsLayoutUtils::TransformFrameRectToAncestor(
      aTargetFrame, targetRectRelativeToTarget,
      nsLayoutUtils::GetContainingBlockForClientRect(aTargetFrame));

  // For content-visibility, we need to observe the overflow clip edge,
  // https://drafts.csswg.org/css-contain-2/#close-to-the-viewport
  MOZ_ASSERT_IF(aIsForProximityToViewport == IsForProximityToViewport::Yes,
                aBoxToUse == BoxToUse::OverflowClip);

  // 2.5. Let intersectionRect be the result of running the compute the
  // intersection algorithm on target and observer’s intersection root.
  bool preservesAxisAlignedRectangles = false;
  Maybe<nsRect> intersectionRect = ComputeTheIntersection(
      aTargetFrame, targetRectRelativeToTarget, aInput.mRootFrame, rootBounds,
      aInput.mScrollMargin, aInput.mRemoteDocumentVisibleRect,
      aIsForProximityToViewport, &preservesAxisAlignedRectangles);

  return {isSimilarOrigin, rootBounds, targetRect, intersectionRect,
          preservesAxisAlignedRectangles};
}

IntersectionOutput DOMIntersectionObserver::Intersect(
    const IntersectionInput& aInput, const nsRect& aTargetRect) {
  nsRect rootBounds = aInput.mRootRect;
  rootBounds.Inflate(aInput.mRootMargin);
  auto intersectionRect =
      aInput.mRootRect.EdgeInclusiveIntersection(aTargetRect);
  if (intersectionRect && aInput.mRemoteDocumentVisibleRect) {
    intersectionRect = intersectionRect->EdgeInclusiveIntersection(
        *aInput.mRemoteDocumentVisibleRect);
  }
  return {true, rootBounds, aTargetRect, intersectionRect,
          /* mPreserverAxisAlignedRectangles= */ false};
}

// https://w3c.github.io/IntersectionObserver/#update-intersection-observations-algo
// (step 2)
void DOMIntersectionObserver::Update(Document& aDocument,
                                     DOMHighResTimeStamp time) {
  auto input = ComputeInput(aDocument, mRoot, &mRootMargin, &mScrollMargin);

  // 2. For each target in observer’s internal [[ObservationTargets]] slot,
  // processed in the same order that observe() was called on each target:
  for (Element* target : mObservationTargets) {
    // 2.1 - 2.4.
    IntersectionOutput output = Intersect(input, *target);

    // 2.5. Let targetArea be targetRect’s area.
    int64_t targetArea = (int64_t)output.mTargetRect.Width() *
                         (int64_t)output.mTargetRect.Height();

    // 2.6. Let intersectionArea be intersectionRect’s area.
    int64_t intersectionArea =
        !output.mIntersectionRect
            ? 0
            : (int64_t)output.mIntersectionRect->Width() *
                  (int64_t)output.mIntersectionRect->Height();

    // 2.7. Let isIntersecting be true if targetRect and rootBounds intersect or
    // are edge-adjacent, even if the intersection has zero area (because
    // rootBounds or targetRect have zero area); otherwise, let isIntersecting
    // be false.
    const bool isIntersecting = output.Intersects();

    // 2.8. If targetArea is non-zero, let intersectionRatio be intersectionArea
    // divided by targetArea. Otherwise, let intersectionRatio be 1 if
    // isIntersecting is true, or 0 if isIntersecting is false.
    double intersectionRatio;
    if (targetArea > 0.0) {
      intersectionRatio =
          std::min((double)intersectionArea / (double)targetArea, 1.0);
    } else {
      intersectionRatio = isIntersecting ? 1.0 : 0.0;
    }

    // 2.9 Let thresholdIndex be the index of the first entry in
    // observer.thresholds whose value is greater than intersectionRatio, or the
    // length of observer.thresholds if intersectionRatio is greater than or
    // equal to the last entry in observer.thresholds.
    int32_t thresholdIndex = -1;

    // If not intersecting, we can just shortcut, as we know that the thresholds
    // are always between 0 and 1.
    if (isIntersecting) {
      thresholdIndex = mThresholds.IndexOfFirstElementGt(intersectionRatio);
      if (thresholdIndex == 0) {
        // Per the spec, we should leave threshold at 0 and distinguish between
        // "less than all thresholds and intersecting" and "not intersecting"
        // (queuing observer entries as both cases come to pass). However,
        // neither Chrome nor the WPT tests expect this behavior, so treat these
        // two cases as one.
        //
        // See https://github.com/w3c/IntersectionObserver/issues/432 about
        // this.
        thresholdIndex = -1;
      }
    }

    // Steps 2.10 - 2.15.
    bool updated = false;
    if (auto entry = mObservationTargetMap.Lookup(target)) {
      updated = entry.Data() != thresholdIndex;
      entry.Data() = thresholdIndex;
    } else {
      MOZ_ASSERT_UNREACHABLE("Target not properly registered?");
    }

    if (updated) {
      // See https://github.com/w3c/IntersectionObserver/issues/432 about
      // why we use thresholdIndex > 0 rather than isIntersecting for the
      // entry's isIntersecting value.
      QueueIntersectionObserverEntry(
          target, time,
          output.mIsSimilarOrigin ? Some(output.mRootBounds) : Nothing(),
          output.mTargetRect, output.mIntersectionRect, thresholdIndex > 0,
          intersectionRatio);
    }
  }
}

void DOMIntersectionObserver::QueueIntersectionObserverEntry(
    Element* aTarget, DOMHighResTimeStamp time, const Maybe<nsRect>& aRootRect,
    const nsRect& aTargetRect, const Maybe<nsRect>& aIntersectionRect,
    bool aIsIntersecting, double aIntersectionRatio) {
  RefPtr<DOMRect> rootBounds;
  if (aRootRect.isSome()) {
    rootBounds = new DOMRect(mOwner);
    rootBounds->SetLayoutRect(aRootRect.value());
  }
  RefPtr<DOMRect> boundingClientRect = new DOMRect(mOwner);
  boundingClientRect->SetLayoutRect(aTargetRect);
  RefPtr<DOMRect> intersectionRect = new DOMRect(mOwner);
  if (aIntersectionRect.isSome()) {
    intersectionRect->SetLayoutRect(aIntersectionRect.value());
  }
  RefPtr<DOMIntersectionObserverEntry> entry = new DOMIntersectionObserverEntry(
      mOwner, time, rootBounds.forget(), boundingClientRect.forget(),
      intersectionRect.forget(), aIsIntersecting, aTarget, aIntersectionRatio);
  mQueuedEntries.AppendElement(entry.forget());
}

void DOMIntersectionObserver::Notify() {
  if (!mQueuedEntries.Length()) {
    return;
  }
  Sequence<OwningNonNull<DOMIntersectionObserverEntry>> entries;
  if (entries.SetCapacity(mQueuedEntries.Length(), mozilla::fallible)) {
    for (size_t i = 0; i < mQueuedEntries.Length(); ++i) {
      RefPtr<DOMIntersectionObserverEntry> next = mQueuedEntries[i];
      *entries.AppendElement(mozilla::fallible) = next;
    }
  }
  mQueuedEntries.Clear();

  if (mCallback.is<RefPtr<dom::IntersectionCallback>>()) {
    RefPtr<dom::IntersectionCallback> callback(
        mCallback.as<RefPtr<dom::IntersectionCallback>>());
    callback->Call(this, entries, *this);
  } else {
    mCallback.as<NativeCallback>()(entries);
  }
}

}  // namespace mozilla::dom
