/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGObserverUtils.h"

// Keep others in (case-insensitive) order:
#include "mozilla/css/ImageLoader.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/SVGGeometryElement.h"
#include "mozilla/dom/SVGTextPathElement.h"
#include "mozilla/dom/SVGUseElement.h"
#include "mozilla/PresShell.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SVGClipPathFrame.h"
#include "mozilla/SVGMaskFrame.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/SVGUtils.h"
#include "nsCSSFrameConstructor.h"
#include "nsCycleCollectionParticipant.h"
#include "nsHashKeys.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsInterfaceHashtable.h"
#include "nsIReflowCallback.h"
#include "nsISupportsImpl.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "nsTHashtable.h"
#include "nsURIHashKey.h"
#include "SVGFilterFrame.h"
#include "SVGMarkerFrame.h"
#include "SVGPaintServerFrame.h"

using namespace mozilla::dom;

namespace mozilla {

bool URLAndReferrerInfo::operator==(const URLAndReferrerInfo& aRHS) const {
  bool uriEqual = false, referrerEqual = false;
  this->mURI->Equals(aRHS.mURI, &uriEqual);
  this->mReferrerInfo->Equals(aRHS.mReferrerInfo, &referrerEqual);

  return uriEqual && referrerEqual;
}

class URLAndReferrerInfoHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = const URLAndReferrerInfo*;
  using KeyTypePointer = const URLAndReferrerInfo*;

  explicit URLAndReferrerInfoHashKey(const URLAndReferrerInfo* aKey) noexcept
      : mKey(aKey) {
    MOZ_COUNT_CTOR(URLAndReferrerInfoHashKey);
  }
  URLAndReferrerInfoHashKey(URLAndReferrerInfoHashKey&& aToMove) noexcept
      : PLDHashEntryHdr(std::move(aToMove)), mKey(std::move(aToMove.mKey)) {
    MOZ_COUNT_CTOR(URLAndReferrerInfoHashKey);
  }
  MOZ_COUNTED_DTOR(URLAndReferrerInfoHashKey)

  const URLAndReferrerInfo* GetKey() const { return mKey; }

  bool KeyEquals(const URLAndReferrerInfo* aKey) const {
    if (!mKey) {
      return !aKey;
    }
    return *mKey == *aKey;
  }

  static const URLAndReferrerInfo* KeyToPointer(
      const URLAndReferrerInfo* aKey) {
    return aKey;
  }

  static PLDHashNumber HashKey(const URLAndReferrerInfo* aKey) {
    if (!aKey) {
      // If the key is null, return hash for empty string.
      return HashString(""_ns);
    }
    nsAutoCString urlSpec, referrerSpec;
    // nsURIHashKey ignores GetSpec() failures, so we do too:
    Unused << aKey->GetURI()->GetSpec(urlSpec);
    return AddToHash(
        HashString(urlSpec),
        static_cast<ReferrerInfo*>(aKey->GetReferrerInfo())->Hash());
  }

  enum { ALLOW_MEMMOVE = true };

 protected:
  RefPtr<const URLAndReferrerInfo> mKey;
};

static already_AddRefed<URLAndReferrerInfo> ResolveURLUsingLocalRef(
    nsIFrame* aFrame, const StyleComputedImageUrl& aURL) {
  MOZ_ASSERT(aFrame);

  nsCOMPtr<nsIURI> uri = aURL.GetURI();

  if (aURL.IsLocalRef()) {
    uri = SVGObserverUtils::GetBaseURLForLocalRef(aFrame->GetContent(), uri);
    uri = aURL.ResolveLocalRef(uri);
  }

  if (!uri) {
    return nullptr;
  }

  RefPtr<URLAndReferrerInfo> info =
      new URLAndReferrerInfo(uri, aURL.ExtraData());
  return info.forget();
}

static already_AddRefed<URLAndReferrerInfo> ResolveURLUsingLocalRef(
    nsIFrame* aFrame, const nsAString& aURL, nsIReferrerInfo* aReferrerInfo) {
  MOZ_ASSERT(aFrame);

  nsIContent* content = aFrame->GetContent();

  // Like SVGObserverUtils::GetBaseURLForLocalRef, we want to resolve the
  // URL against any <use> element shadow tree's source document.
  //
  // Unlike GetBaseURLForLocalRef, we are assuming that the URL was specified
  // directly on mFrame's content (because this ResolveURLUsingLocalRef
  // overload is used for href="" attributes and not CSS URL values), so there
  // is no need to check whether the URL was specified / inherited from
  // outside the shadow tree.
  nsIURI* base = nullptr;
  const Encoding* encoding = nullptr;
  if (SVGUseElement* use = content->GetContainingSVGUseShadowHost()) {
    base = use->GetSourceDocURI();
    encoding = use->GetSourceDocCharacterSet();
  }

  if (!base) {
    base = content->OwnerDoc()->GetDocumentURI();
    encoding = content->OwnerDoc()->GetDocumentCharacterSet();
  }

  nsCOMPtr<nsIURI> uri;
  Unused << NS_NewURI(getter_AddRefs(uri), aURL, WrapNotNull(encoding), base);

  if (!uri) {
    return nullptr;
  }

  RefPtr<URLAndReferrerInfo> info = new URLAndReferrerInfo(uri, aReferrerInfo);
  return info.forget();
}

class SVGFilterObserverList;

/**
 * A class used as a member of the "observer" classes below to help them
 * avoid dereferencing their frame during presshell teardown when their frame
 * may have been destroyed (leaving their pointer to their frame dangling).
 *
 * When a presshell is torn down, the properties for each frame may not be
 * deleted until after the frames are destroyed.  "Observer" objects (attached
 * as frame properties) must therefore check whether the presshell is being
 * torn down before using their pointer to their frame.
 *
 * mFramePresShell may be null, but when mFrame is non-null, mFramePresShell
 * is guaranteed to be non-null, too.
 */
struct SVGFrameReferenceFromProperty {
  explicit SVGFrameReferenceFromProperty(nsIFrame* aFrame)
      : mFrame(aFrame), mFramePresShell(aFrame->PresShell()) {}

  // Clear our reference to the frame.
  void Detach() {
    mFrame = nullptr;
    mFramePresShell = nullptr;
  }

  // null if the frame has become invalid
  nsIFrame* Get() {
    if (mFramePresShell && mFramePresShell->IsDestroying()) {
      Detach();  // mFrame is no longer valid.
    }
    return mFrame;
  }

 private:
  // The frame that our property is attached to (may be null).
  nsIFrame* mFrame;
  PresShell* mFramePresShell;
};

void SVGRenderingObserver::StartObserving() {
  Element* target = GetReferencedElementWithoutObserving();
  if (target) {
    target->AddMutationObserver(this);
  }
}

void SVGRenderingObserver::StopObserving() {
  Element* target = GetReferencedElementWithoutObserving();

  if (target) {
    target->RemoveMutationObserver(this);
    if (mInObserverSet) {
      SVGObserverUtils::RemoveRenderingObserver(target, this);
      mInObserverSet = false;
    }
  }
  NS_ASSERTION(!mInObserverSet, "still in an observer set?");
}

Element* SVGRenderingObserver::GetAndObserveReferencedElement() {
#ifdef DEBUG
  DebugObserverSet();
#endif
  Element* referencedElement = GetReferencedElementWithoutObserving();
  if (referencedElement && !mInObserverSet) {
    SVGObserverUtils::AddRenderingObserver(referencedElement, this);
    mInObserverSet = true;
  }
  return referencedElement;
}

nsIFrame* SVGRenderingObserver::GetAndObserveReferencedFrame() {
  Element* referencedElement = GetAndObserveReferencedElement();
  return referencedElement ? referencedElement->GetPrimaryFrame() : nullptr;
}

nsIFrame* SVGRenderingObserver::GetAndObserveReferencedFrame(
    LayoutFrameType aFrameType, bool* aOK) {
  nsIFrame* frame = GetAndObserveReferencedFrame();
  if (frame) {
    if (frame->Type() == aFrameType) {
      return frame;
    }
    if (aOK) {
      *aOK = false;
    }
  }
  return nullptr;
}

void SVGRenderingObserver::OnNonDOMMutationRenderingChange() {
  mInObserverSet = false;
  OnRenderingChange();
}

void SVGRenderingObserver::NotifyEvictedFromRenderingObserverSet() {
  mInObserverSet = false;  // We've been removed from rendering-obs. set.
  StopObserving();         // Stop observing mutations too.
}

void SVGRenderingObserver::AttributeChanged(dom::Element* aElement,
                                            int32_t aNameSpaceID,
                                            nsAtom* aAttribute,
                                            int32_t aModType,
                                            const nsAttrValue* aOldValue) {
  if (aElement->IsInNativeAnonymousSubtree()) {
    // Don't observe attribute changes in native-anonymous subtrees like
    // scrollbars.
    return;
  }

  // An attribute belonging to the element that we are observing *or one of its
  // descendants* has changed.
  //
  // In the case of observing a gradient element, say, we want to know if any
  // of its 'stop' element children change, but we don't actually want to do
  // anything for changes to SMIL element children, for example. Maybe it's not
  // worth having logic to optimize for that, but in most cases it could be a
  // small check?
  //
  // XXXjwatt: do we really want to blindly break the link between our
  // observers and ourselves for all attribute changes? For non-ID changes
  // surely that is unnecessary.

  OnRenderingChange();
}

void SVGRenderingObserver::ContentAppended(nsIContent* aFirstNewContent) {
  OnRenderingChange();
}

void SVGRenderingObserver::ContentInserted(nsIContent* aChild) {
  OnRenderingChange();
}

void SVGRenderingObserver::ContentRemoved(nsIContent* aChild,
                                          nsIContent* aPreviousSibling) {
  OnRenderingChange();
}

/**
 * SVG elements reference supporting resources by element ID. We need to
 * track when those resources change and when the document changes in ways
 * that affect which element is referenced by a given ID (e.g., when
 * element IDs change). The code here is responsible for that.
 *
 * When a frame references a supporting resource, we create a property
 * object derived from SVGIDRenderingObserver to manage the relationship. The
 * property object is attached to the referencing frame.
 */
class SVGIDRenderingObserver : public SVGRenderingObserver {
 public:
  // Callback for checking if the element being observed is valid for this
  // observer. Note that this may be called during construction, before the
  // deriving class is fully constructed.
  using TargetIsValidCallback = bool (*)(const Element&);
  SVGIDRenderingObserver(
      URLAndReferrerInfo* aURI, nsIContent* aObservingContent,
      bool aReferenceImage,
      TargetIsValidCallback aTargetIsValidCallback = nullptr);

 protected:
  virtual ~SVGIDRenderingObserver() {
    // This needs to call our GetReferencedElementWithoutObserving override,
    // so must be called here rather than in our base class's dtor.
    StopObserving();
  }

  void TargetChanged() {
    mTargetIsValid = ([this] {
      Element* observed = mObservedElementTracker.get();
      if (!observed) {
        return false;
      }
      // If the content is observing an ancestor, then return the target is not
      // valid.
      //
      // TODO(emilio): Should we allow content observing its own descendants?
      // That seems potentially-bad as well.
      if (observed->OwnerDoc() == mObservingContent->OwnerDoc() &&
          nsContentUtils::ContentIsHostIncludingDescendantOf(mObservingContent,
                                                             observed)) {
        return false;
      }
      if (mTargetIsValidCallback) {
        return mTargetIsValidCallback(*observed);
      }
      return true;
    }());
  }

  Element* GetReferencedElementWithoutObserving() final {
    return mTargetIsValid ? mObservedElementTracker.get() : nullptr;
  }

  void OnRenderingChange() override;

  /**
   * Helper that provides a reference to the element with the ID that our
   * observer wants to observe, and that will invalidate our observer if the
   * element that that ID identifies changes to a different element (or none).
   */
  class ElementTracker final : public IDTracker {
   public:
    explicit ElementTracker(SVGIDRenderingObserver* aOwningObserver)
        : mOwningObserver(aOwningObserver) {}

   protected:
    void ElementChanged(Element* aFrom, Element* aTo) override {
      // Call OnRenderingChange() before the target changes, so that
      // mIsTargetValid reflects the right state.
      mOwningObserver->OnRenderingChange();
      mOwningObserver->StopObserving();
      IDTracker::ElementChanged(aFrom, aTo);
      mOwningObserver->TargetChanged();
      mOwningObserver->StartObserving();
      // And same after the target changes, for the same reason.
      mOwningObserver->OnRenderingChange();
    }
    /**
     * Override IsPersistent because we want to keep tracking the element
     * for the ID even when it changes.
     */
    bool IsPersistent() override { return true; }

   private:
    SVGIDRenderingObserver* mOwningObserver;
  };

  ElementTracker mObservedElementTracker;
  RefPtr<Element> mObservingContent;
  bool mTargetIsValid = false;
  TargetIsValidCallback mTargetIsValidCallback;
};

/**
 * Note that in the current setup there are two separate observer lists.
 *
 * In SVGIDRenderingObserver's ctor, the new object adds itself to the
 * mutation observer list maintained by the referenced element. In this way the
 * SVGIDRenderingObserver is notified if there are any attribute or content
 * tree changes to the element or any of its *descendants*.
 *
 * In SVGIDRenderingObserver::GetAndObserveReferencedElement() the
 * SVGIDRenderingObserver object also adds itself to an
 * SVGRenderingObserverSet object belonging to the referenced
 * element.
 *
 * XXX: it would be nice to have a clear and concise executive summary of the
 * benefits/necessity of maintaining a second observer list.
 */
SVGIDRenderingObserver::SVGIDRenderingObserver(
    URLAndReferrerInfo* aURI, nsIContent* aObservingContent,
    bool aReferenceImage, TargetIsValidCallback aTargetIsValidCallback)
    : mObservedElementTracker(this),
      mObservingContent(aObservingContent->AsElement()),
      mTargetIsValidCallback(aTargetIsValidCallback) {
  // Start watching the target element
  nsIURI* uri = nullptr;
  nsIReferrerInfo* referrerInfo = nullptr;
  if (aURI) {
    uri = aURI->GetURI();
    referrerInfo = aURI->GetReferrerInfo();
  }

  mObservedElementTracker.ResetToURIFragmentID(
      aObservingContent, uri, referrerInfo, true, aReferenceImage);
  TargetChanged();
  StartObserving();
}

void SVGIDRenderingObserver::OnRenderingChange() {
  if (mObservedElementTracker.get() && mInObserverSet) {
    SVGObserverUtils::RemoveRenderingObserver(mObservedElementTracker.get(),
                                              this);
    mInObserverSet = false;
  }
}

class SVGRenderingObserverProperty : public SVGIDRenderingObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGRenderingObserverProperty(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                               bool aReferenceImage)
      : SVGIDRenderingObserver(aURI, aFrame->GetContent(), aReferenceImage),
        mFrameReference(aFrame) {}

 protected:
  virtual ~SVGRenderingObserverProperty() = default;  // non-public

  void OnRenderingChange() override;

  SVGFrameReferenceFromProperty mFrameReference;
};

NS_IMPL_ISUPPORTS(SVGRenderingObserverProperty, nsIMutationObserver)

void SVGRenderingObserverProperty::OnRenderingChange() {
  SVGIDRenderingObserver::OnRenderingChange();

  if (!mTargetIsValid) {
    return;
  }

  nsIFrame* frame = mFrameReference.Get();

  if (frame && frame->HasAllStateBits(NS_FRAME_SVG_LAYOUT)) {
    // We need to notify anything that is observing the referencing frame or
    // any of its ancestors that the referencing frame has been invalidated.
    // Since walking the parent chain checking for observers is expensive we
    // do that using a change hint (multiple change hints of the same type are
    // coalesced).
    nsLayoutUtils::PostRestyleEvent(frame->GetContent()->AsElement(),
                                    RestyleHint{0},
                                    nsChangeHint_InvalidateRenderingObservers);
  }
}

class SVGTextPathObserver final : public SVGRenderingObserverProperty {
 public:
  SVGTextPathObserver(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                      bool aReferenceImage)
      : SVGRenderingObserverProperty(aURI, aFrame, aReferenceImage) {}

 protected:
  void OnRenderingChange() override;
};

void SVGTextPathObserver::OnRenderingChange() {
  SVGRenderingObserverProperty::OnRenderingChange();

  if (!mTargetIsValid) {
    return;
  }

  nsIFrame* frame = mFrameReference.Get();
  if (!frame) {
    return;
  }

  MOZ_ASSERT(
      frame->IsFrameOfType(nsIFrame::eSVG) || frame->IsInSVGTextSubtree(),
      "SVG frame expected");

  MOZ_ASSERT(frame->GetContent()->IsSVGElement(nsGkAtoms::textPath),
             "expected frame for a <textPath> element");

  auto* text = static_cast<SVGTextFrame*>(
      nsLayoutUtils::GetClosestFrameOfType(frame, LayoutFrameType::SVGText));
  MOZ_ASSERT(text, "expected to find an ancestor SVGTextFrame");
  if (text) {
    text->AddStateBits(NS_STATE_SVG_POSITIONING_DIRTY);

    if (text->HasAnyStateBits(NS_STATE_SVG_TEXT_IN_REFLOW |
                              NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY)) {
      return;
    }

    if (SVGUtils::AnyOuterSVGIsCallingReflowSVG(text)) {
      text->AddStateBits(NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN);
      if (text->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
        text->ReflowSVGNonDisplayText();
      } else {
        text->ReflowSVG();
      }
    } else {
      text->ScheduleReflowSVG();
    }
  }
}

class SVGMarkerObserver final : public SVGRenderingObserverProperty {
 public:
  SVGMarkerObserver(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                    bool aReferenceImage)
      : SVGRenderingObserverProperty(aURI, aFrame, aReferenceImage) {}

 protected:
  void OnRenderingChange() override;
};

void SVGMarkerObserver::OnRenderingChange() {
  SVGRenderingObserverProperty::OnRenderingChange();

  nsIFrame* frame = mFrameReference.Get();
  if (!frame) {
    return;
  }

  MOZ_ASSERT(frame->IsFrameOfType(nsIFrame::eSVG), "SVG frame expected");

  // Don't need to request ReflowFrame if we're being reflowed.
  // Because mRect for SVG frames includes the bounds of any markers
  // (see the comment for nsIFrame::GetRect), the referencing frame must be
  // reflowed for any marker changes.
  if (!SVGUtils::OuterSVGIsCallingReflowSVG(frame)) {
    // XXXjwatt: We need to unify SVG into standard reflow so we can just use
    // nsChangeHint_NeedReflow | nsChangeHint_NeedDirtyReflow here.
    // XXXSDL KILL THIS!!!
    SVGUtils::ScheduleReflowSVG(frame);
  }
  frame->PresContext()->RestyleManager()->PostRestyleEvent(
      frame->GetContent()->AsElement(), RestyleHint{0},
      nsChangeHint_RepaintFrame);
}

class SVGPaintingProperty : public SVGRenderingObserverProperty {
 public:
  SVGPaintingProperty(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                      bool aReferenceImage)
      : SVGRenderingObserverProperty(aURI, aFrame, aReferenceImage) {}

 protected:
  void OnRenderingChange() override;
};

void SVGPaintingProperty::OnRenderingChange() {
  SVGRenderingObserverProperty::OnRenderingChange();

  nsIFrame* frame = mFrameReference.Get();
  if (!frame) {
    return;
  }

  if (frame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    frame->InvalidateFrameSubtree();
  } else {
    for (nsIFrame* f = frame; f;
         f = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(f)) {
      f->InvalidateFrame();
    }
  }
}

// Observer for -moz-element(#element). Note that the observed element does not
// have to be an SVG element.
class SVGMozElementObserver final : public SVGPaintingProperty {
 public:
  SVGMozElementObserver(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                        bool aReferenceImage)
      : SVGPaintingProperty(aURI, aFrame, aReferenceImage) {}

  // We only return true here because GetAndObserveBackgroundImage uses us
  // to implement observing of arbitrary elements (including HTML elements)
  // that may require us to repaint if the referenced element is reflowed.
  // Bug 1496065 has been filed to remove that support though.
  bool ObservesReflow() override { return true; }
};

/**
 * For content with `background-clip: text`.
 *
 * This observer is unusual in that the observing frame and observed frame are
 * the same frame.  This is because the observing frame is observing for reflow
 * of its descendant text nodes, since such reflows may not result in the
 * frame's nsDisplayBackground changing.  In other words, Display List Based
 * Invalidation may not invalidate the frame's background, so we need this
 * observer to make sure that happens.
 *
 * XXX: It's questionable whether we should even be [ab]using the SVG observer
 * mechanism for `background-clip:text`.  Since we know that the observed frame
 * is the frame we need to invalidate, we could just check the computed style
 * in the (one) place where we pass INVALIDATE_REFLOW and invalidate there...
 */
class BackgroundClipRenderingObserver : public SVGRenderingObserver {
 public:
  explicit BackgroundClipRenderingObserver(nsIFrame* aFrame) : mFrame(aFrame) {}

  NS_DECL_ISUPPORTS

 private:
  // We do not call StopObserving() since the observing and observed element
  // are the same element (and because we could crash - see bug 1556441).
  virtual ~BackgroundClipRenderingObserver() = default;

  Element* GetReferencedElementWithoutObserving() final {
    return mFrame->GetContent()->AsElement();
  }

  void OnRenderingChange() final;

  /**
   * Observing for mutations is not enough.  A new font loading and applying
   * to the text content could cause it to reflow, and we need to invalidate
   * for that.
   */
  bool ObservesReflow() final { return true; }

  // The observer and observee!
  nsIFrame* mFrame;
};

NS_IMPL_ISUPPORTS(BackgroundClipRenderingObserver, nsIMutationObserver)

void BackgroundClipRenderingObserver::OnRenderingChange() {
  for (nsIFrame* f = mFrame; f;
       f = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(f)) {
    f->InvalidateFrame();
  }
}

static bool IsSVGFilterElement(const Element& aObserved) {
  return aObserved.IsSVGElement(nsGkAtoms::filter);
}

/**
 * In a filter chain, there can be multiple SVG reference filters.
 * e.g. filter: url(#svg-filter-1) blur(10px) url(#svg-filter-2);
 *
 * This class keeps track of one SVG reference filter in a filter chain.
 * e.g. url(#svg-filter-1)
 *
 * It fires invalidations when the SVG filter element's id changes or when
 * the SVG filter element's content changes.
 *
 * The SVGFilterObserverList class manages a list of SVGFilterObservers.
 */
class SVGFilterObserver final : public SVGIDRenderingObserver {
 public:
  SVGFilterObserver(URLAndReferrerInfo* aURI, nsIContent* aObservingContent,
                    SVGFilterObserverList* aFilterChainObserver)
      : SVGIDRenderingObserver(aURI, aObservingContent, false,
                               IsSVGFilterElement),
        mFilterObserverList(aFilterChainObserver) {}

  void DetachFromChainObserver() { mFilterObserverList = nullptr; }

  /**
   * @return the filter frame, or null if there is no filter frame
   */
  SVGFilterFrame* GetAndObserveFilterFrame();

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(SVGFilterObserver)

  // SVGIDRenderingObserver
  void OnRenderingChange() override;

 protected:
  virtual ~SVGFilterObserver() = default;  // non-public

  SVGFilterObserverList* mFilterObserverList;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(SVGFilterObserver)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SVGFilterObserver)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SVGFilterObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(SVGFilterObserver)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(SVGFilterObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mObservedElementTracker)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mObservingContent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(SVGFilterObserver)
  tmp->StopObserving();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObservedElementTracker);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObservingContent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

SVGFilterFrame* SVGFilterObserver::GetAndObserveFilterFrame() {
  return static_cast<SVGFilterFrame*>(
      GetAndObserveReferencedFrame(LayoutFrameType::SVGFilter, nullptr));
}

NS_IMPL_CYCLE_COLLECTION(ISVGFilterObserverList)

NS_IMPL_CYCLE_COLLECTING_ADDREF(ISVGFilterObserverList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ISVGFilterObserverList)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ISVGFilterObserverList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/**
 * This class manages a list of SVGFilterObservers, which correspond to
 * reference to SVG filters in a list of filters in a given 'filter' property.
 * e.g. filter: url(#svg-filter-1) blur(10px) url(#svg-filter-2);
 *
 * In the above example, the SVGFilterObserverList will manage two
 * SVGFilterObservers, one for each of the references to SVG filters.  CSS
 * filters like "blur(10px)" don't reference filter elements, so they don't
 * need an SVGFilterObserver.  The style system invalidates changes to CSS
 * filters.
 *
 * FIXME(emilio): Why do we need this as opposed to the individual observers we
 * create in the constructor?
 */
class SVGFilterObserverList : public ISVGFilterObserverList {
 public:
  SVGFilterObserverList(Span<const StyleFilter> aFilters,
                        nsIContent* aFilteredElement,
                        nsIFrame* aFilteredFrame = nullptr);

  const nsTArray<RefPtr<SVGFilterObserver>>& GetObservers() const override {
    return mObservers;
  }

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(SVGFilterObserverList,
                                           ISVGFilterObserverList)

  virtual void OnRenderingChange() = 0;

 protected:
  SVGFilterObserverList(const SVGFilterObserverList& aOther)
      : mObservers(aOther.mObservers.Clone()) {}
  virtual ~SVGFilterObserverList();

  void DetachObservers() {
    for (auto& observer : mObservers) {
      observer->DetachFromChainObserver();
    }
  }

  nsTArray<RefPtr<SVGFilterObserver>> mObservers;
};

void SVGFilterObserver::OnRenderingChange() {
  SVGIDRenderingObserver::OnRenderingChange();

  if (mFilterObserverList) {
    mFilterObserverList->OnRenderingChange();
  }

  if (!mTargetIsValid) {
    return;
  }

  nsIFrame* frame = mObservingContent->GetPrimaryFrame();
  if (!frame) {
    return;
  }

  // Repaint asynchronously in case the filter frame is being torn down
  nsChangeHint changeHint = nsChangeHint(nsChangeHint_RepaintFrame);

  // Since we don't call SVGRenderingObserverProperty::
  // OnRenderingChange, we have to add this bit ourselves.
  if (frame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    // Changes should propagate out to things that might be observing
    // the referencing frame or its ancestors.
    changeHint |= nsChangeHint_InvalidateRenderingObservers;
  }

  // Don't need to request UpdateOverflow if we're being reflowed.
  if (!frame->HasAnyStateBits(NS_FRAME_IN_REFLOW)) {
    changeHint |= nsChangeHint_UpdateOverflow;
  }
  frame->PresContext()->RestyleManager()->PostRestyleEvent(
      mObservingContent, RestyleHint{0}, changeHint);
}

NS_IMPL_ADDREF_INHERITED(SVGFilterObserverList, ISVGFilterObserverList)
NS_IMPL_RELEASE_INHERITED(SVGFilterObserverList, ISVGFilterObserverList)

NS_IMPL_CYCLE_COLLECTION_CLASS(SVGFilterObserverList)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SVGFilterObserverList,
                                                  ISVGFilterObserverList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mObservers)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SVGFilterObserverList,
                                                ISVGFilterObserverList)
  tmp->DetachObservers();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObservers)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SVGFilterObserverList)
  NS_INTERFACE_MAP_ENTRY(ISVGFilterObserverList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

SVGFilterObserverList::SVGFilterObserverList(Span<const StyleFilter> aFilters,
                                             nsIContent* aFilteredElement,
                                             nsIFrame* aFilteredFrame) {
  for (const auto& filter : aFilters) {
    if (!filter.IsUrl()) {
      continue;
    }

    const auto& url = filter.AsUrl();

    // aFilteredFrame can be null if this filter belongs to a
    // CanvasRenderingContext2D.
    RefPtr<URLAndReferrerInfo> filterURL;
    if (aFilteredFrame) {
      filterURL = ResolveURLUsingLocalRef(aFilteredFrame, url);
    } else {
      nsCOMPtr<nsIURI> resolvedURI = url.ResolveLocalRef(aFilteredElement);
      if (resolvedURI) {
        filterURL = new URLAndReferrerInfo(resolvedURI, url.ExtraData());
      }
    }

    RefPtr<SVGFilterObserver> observer =
        new SVGFilterObserver(filterURL, aFilteredElement, this);
    mObservers.AppendElement(observer);
  }
}

SVGFilterObserverList::~SVGFilterObserverList() { DetachObservers(); }

class SVGFilterObserverListForCSSProp final : public SVGFilterObserverList {
 public:
  SVGFilterObserverListForCSSProp(Span<const StyleFilter> aFilters,
                                  nsIFrame* aFilteredFrame)
      : SVGFilterObserverList(aFilters, aFilteredFrame->GetContent(),
                              aFilteredFrame) {}

  ISVGFilterObserverList* Clone() const override {
    return new SVGFilterObserverListForCSSProp(*this);
  }

 protected:
  SVGFilterObserverListForCSSProp(const SVGFilterObserverListForCSSProp& aOther)
      : SVGFilterObserverList(aOther), mInvalidating(aOther.mInvalidating) {}
  void OnRenderingChange() override;
  bool mInvalidating = false;
};

void SVGFilterObserverListForCSSProp::OnRenderingChange() {
  if (mInvalidating) {
    return;
  }
  AutoRestore<bool> guard(mInvalidating);
  mInvalidating = true;
  for (auto& observer : mObservers) {
    observer->OnRenderingChange();
  }
}

class SVGFilterObserverListForCanvasContext final
    : public SVGFilterObserverList {
 public:
  SVGFilterObserverListForCanvasContext(CanvasRenderingContext2D* aContext,
                                        Element* aCanvasElement,
                                        Span<const StyleFilter> aFilters)
      : SVGFilterObserverList(aFilters, aCanvasElement), mContext(aContext) {}

  void OnRenderingChange() override;
  void SetIsActive(bool aActive) override { mActive = aActive; }
  ISVGFilterObserverList* Clone() const override {
    return new SVGFilterObserverListForCanvasContext(*this);
  }

 private:
  SVGFilterObserverListForCanvasContext(
      const SVGFilterObserverListForCanvasContext& aOther)
      : SVGFilterObserverList(aOther),
        mContext(aOther.mContext),
        mActive(aOther.mActive) {}

  CanvasRenderingContext2D* mContext;
  bool mActive = true;
};

void SVGFilterObserverListForCanvasContext::OnRenderingChange() {
  if (!mActive) {
    return;
  }
  // Refresh the cached FilterDescription in mContext->CurrentState().filter.
  // If this filter is not at the top of the state stack, we'll refresh the
  // wrong filter, but that's ok, because we'll refresh the right filter
  // when we pop the state stack in CanvasRenderingContext2D::Restore().
  RefPtr<CanvasRenderingContext2D> kungFuDeathGrip(mContext);
  kungFuDeathGrip->UpdateFilter();
}

class SVGMaskObserverList final : public nsISupports {
 public:
  explicit SVGMaskObserverList(nsIFrame* aFrame);

  // nsISupports
  NS_DECL_ISUPPORTS

  const nsTArray<RefPtr<SVGPaintingProperty>>& GetObservers() const {
    return mProperties;
  }

  void ResolveImage(uint32_t aIndex);

 private:
  virtual ~SVGMaskObserverList() = default;  // non-public
  nsTArray<RefPtr<SVGPaintingProperty>> mProperties;
  nsIFrame* mFrame;
};

NS_IMPL_ISUPPORTS(SVGMaskObserverList, nsISupports)

SVGMaskObserverList::SVGMaskObserverList(nsIFrame* aFrame) : mFrame(aFrame) {
  const nsStyleSVGReset* svgReset = aFrame->StyleSVGReset();

  for (uint32_t i = 0; i < svgReset->mMask.mImageCount; i++) {
    const StyleComputedImageUrl* data =
        svgReset->mMask.mLayers[i].mImage.GetImageRequestURLValue();
    RefPtr<URLAndReferrerInfo> maskUri;
    if (data) {
      maskUri = ResolveURLUsingLocalRef(aFrame, *data);
    }

    bool hasRef = false;
    if (maskUri) {
      maskUri->GetURI()->GetHasRef(&hasRef);
    }

    // Accrording to maskUri, SVGPaintingProperty's ctor may trigger an
    // external SVG resource download, so we should pass maskUri in only if
    // maskUri has a chance pointing to an SVG mask resource.
    //
    // And, an URL may refer to an SVG mask resource if it consists of
    // a fragment.
    SVGPaintingProperty* prop = new SVGPaintingProperty(
        hasRef ? maskUri.get() : nullptr, aFrame, false);
    mProperties.AppendElement(prop);
  }
}

void SVGMaskObserverList::ResolveImage(uint32_t aIndex) {
  const nsStyleSVGReset* svgReset = mFrame->StyleSVGReset();
  MOZ_ASSERT(aIndex < svgReset->mMask.mImageCount);

  auto& image = const_cast<StyleImage&>(svgReset->mMask.mLayers[aIndex].mImage);
  if (image.IsResolved()) {
    return;
  }
  MOZ_ASSERT(image.IsImageRequestType());
  Document* doc = mFrame->PresContext()->Document();
  image.ResolveImage(*doc, nullptr);
  if (imgRequestProxy* req = image.GetImageRequest()) {
    // FIXME(emilio): What disassociates this request?
    doc->StyleImageLoader()->AssociateRequestToFrame(req, mFrame);
  }
}

/**
 * Used for gradient-to-gradient, pattern-to-pattern and filter-to-filter
 * references to "template" elements (specified via the 'href' attributes).
 *
 * This is a special class for the case where we know we only want to call
 * InvalidateDirectRenderingObservers (as opposed to
 * InvalidateRenderingObservers).
 *
 * TODO(jwatt): If we added a new NS_FRAME_RENDERING_OBSERVER_CONTAINER state
 * bit to clipPath, filter, gradients, marker, mask, pattern and symbol, and
 * could have InvalidateRenderingObservers stop on reaching such an element,
 * then we would no longer need this class (not to mention improving perf by
 * significantly cutting down on ancestor traversal).
 */
class SVGTemplateElementObserver : public SVGIDRenderingObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGTemplateElementObserver(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                             bool aReferenceImage)
      : SVGIDRenderingObserver(aURI, aFrame->GetContent(), aReferenceImage),
        mFrameReference(aFrame) {}

 protected:
  virtual ~SVGTemplateElementObserver() = default;  // non-public

  void OnRenderingChange() override;

  SVGFrameReferenceFromProperty mFrameReference;
};

NS_IMPL_ISUPPORTS(SVGTemplateElementObserver, nsIMutationObserver)

void SVGTemplateElementObserver::OnRenderingChange() {
  SVGIDRenderingObserver::OnRenderingChange();

  if (nsIFrame* frame = mFrameReference.Get()) {
    // We know that we don't need to walk the parent chain notifying rendering
    // observers since changes to a gradient etc. do not affect ancestor
    // elements.  So we only invalidate *direct* rendering observers here.
    // Since we don't need to walk the parent chain, we don't need to worry
    // about coalescing multiple invalidations by using a change hint as we do
    // in SVGRenderingObserverProperty::OnRenderingChange.
    SVGObserverUtils::InvalidateDirectRenderingObservers(frame);
  }
}

/**
 * An instance of this class is stored on an observed frame (as a frame
 * property) whenever the frame has active rendering observers.  It is used to
 * store pointers to the SVGRenderingObserver instances belonging to any
 * observing frames, allowing invalidations from the observed frame to be sent
 * to all observing frames.
 *
 * SVGRenderingObserver instances that are added are not strongly referenced,
 * so they must remove themselves before they die.
 *
 * This class is "single-shot", which is to say that when something about the
 * observed element changes, InvalidateAll() clears our hashtable of
 * SVGRenderingObservers.  SVGRenderingObserver objects will be added back
 * again if/when the observing frame looks up our observed frame to use it.
 *
 * XXXjwatt: is this the best thing to do nowadays?  Back when that mechanism
 * landed in bug 330498 we had two pass, recursive invalidation up the frame
 * tree, and I think reference loops were a problem.  Nowadays maybe a flag
 * on the SVGRenderingObserver objects to coalesce invalidations may work
 * better?
 *
 * InvalidateAll must be called before this object is destroyed, i.e.
 * before the referenced frame is destroyed. This should normally happen
 * via SVGContainerFrame::RemoveFrame, since only frames in the frame
 * tree should be referenced.
 */
class SVGRenderingObserverSet {
 public:
  SVGRenderingObserverSet() : mObservers(4) {
    MOZ_COUNT_CTOR(SVGRenderingObserverSet);
  }

  ~SVGRenderingObserverSet() {
    InvalidateAll();
    MOZ_COUNT_DTOR(SVGRenderingObserverSet);
  }

  void Add(SVGRenderingObserver* aObserver) { mObservers.Insert(aObserver); }
  void Remove(SVGRenderingObserver* aObserver) { mObservers.Remove(aObserver); }
#ifdef DEBUG
  bool Contains(SVGRenderingObserver* aObserver) {
    return mObservers.Contains(aObserver);
  }
#endif
  bool IsEmpty() { return mObservers.IsEmpty(); }

  /**
   * Drop all our observers, and notify them that we have changed and dropped
   * our reference to them.
   */
  void InvalidateAll();

  /**
   * Drop all observers that observe reflow, and notify them that we have
   * changed and dropped our reference to them.
   */
  void InvalidateAllForReflow();

  /**
   * Drop all our observers, and notify them that we have dropped our reference
   * to them.
   */
  void RemoveAll();

 private:
  nsTHashSet<SVGRenderingObserver*> mObservers;
};

void SVGRenderingObserverSet::InvalidateAll() {
  if (mObservers.IsEmpty()) {
    return;
  }

  const auto observers = std::move(mObservers);

  for (const auto& observer : observers) {
    observer->OnNonDOMMutationRenderingChange();
  }
}

void SVGRenderingObserverSet::InvalidateAllForReflow() {
  if (mObservers.IsEmpty()) {
    return;
  }

  AutoTArray<SVGRenderingObserver*, 10> observers;

  for (auto it = mObservers.cbegin(), end = mObservers.cend(); it != end;
       ++it) {
    SVGRenderingObserver* obs = *it;
    if (obs->ObservesReflow()) {
      observers.AppendElement(obs);
      mObservers.Remove(it);
    }
  }

  for (uint32_t i = 0; i < observers.Length(); ++i) {
    observers[i]->OnNonDOMMutationRenderingChange();
  }
}

void SVGRenderingObserverSet::RemoveAll() {
  const auto observers = std::move(mObservers);

  // Our list is now cleared.  We need to notify the observers we've removed,
  // so they can update their state & remove themselves as mutation-observers.
  for (const auto& observer : observers) {
    observer->NotifyEvictedFromRenderingObserverSet();
  }
}

static SVGRenderingObserverSet* GetObserverSet(Element* aElement) {
  return static_cast<SVGRenderingObserverSet*>(
      aElement->GetProperty(nsGkAtoms::renderingobserverset));
}

#ifdef DEBUG
// Defined down here because we need SVGRenderingObserverSet's definition.
void SVGRenderingObserver::DebugObserverSet() {
  Element* referencedElement = GetReferencedElementWithoutObserving();
  if (referencedElement) {
    SVGRenderingObserverSet* observers = GetObserverSet(referencedElement);
    bool inObserverSet = observers && observers->Contains(this);
    MOZ_ASSERT(inObserverSet == mInObserverSet,
               "failed to track whether we're in our referenced element's "
               "observer set!");
  } else {
    MOZ_ASSERT(!mInObserverSet, "In whose observer set are we, then?");
  }
}
#endif

using URIObserverHashtable =
    nsInterfaceHashtable<URLAndReferrerInfoHashKey, nsIMutationObserver>;

using PaintingPropertyDescriptor =
    const FramePropertyDescriptor<SVGPaintingProperty>*;

static void DestroyFilterProperty(SVGFilterObserverListForCSSProp* aProp) {
  aProp->Release();
}

NS_DECLARE_FRAME_PROPERTY_RELEASABLE(HrefToTemplateProperty,
                                     SVGTemplateElementObserver)
NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(FilterProperty,
                                    SVGFilterObserverListForCSSProp,
                                    DestroyFilterProperty)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(MaskProperty, SVGMaskObserverList)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(ClipPathProperty, SVGPaintingProperty)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(MarkerStartProperty, SVGMarkerObserver)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(MarkerMidProperty, SVGMarkerObserver)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(MarkerEndProperty, SVGMarkerObserver)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(FillProperty, SVGPaintingProperty)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(StrokeProperty, SVGPaintingProperty)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(HrefAsTextPathProperty,
                                     SVGTextPathObserver)
NS_DECLARE_FRAME_PROPERTY_DELETABLE(BackgroundImageProperty,
                                    URIObserverHashtable)
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(BackgroundClipObserverProperty,
                                     BackgroundClipRenderingObserver)

template <class T>
static T* GetEffectProperty(URLAndReferrerInfo* aURI, nsIFrame* aFrame,
                            const FramePropertyDescriptor<T>* aProperty) {
  MOZ_ASSERT(!aFrame->GetPrevContinuation(), "Require first continuation");
  if (!aURI) {
    return nullptr;
  }

  bool found;
  T* prop = aFrame->GetProperty(aProperty, &found);
  if (found) {
    MOZ_ASSERT(prop, "this property should only store non-null values");
    return prop;
  }
  prop = new T(aURI, aFrame, false);
  NS_ADDREF(prop);
  aFrame->AddProperty(aProperty, prop);
  return prop;
}

static SVGPaintingProperty* GetPaintingProperty(
    URLAndReferrerInfo* aURI, nsIFrame* aFrame,
    const FramePropertyDescriptor<SVGPaintingProperty>* aProperty) {
  return GetEffectProperty(aURI, aFrame, aProperty);
}

static already_AddRefed<URLAndReferrerInfo> GetMarkerURI(
    nsIFrame* aFrame, const StyleUrlOrNone nsStyleSVG::*aMarker) {
  const StyleUrlOrNone& url = aFrame->StyleSVG()->*aMarker;
  if (url.IsNone()) {
    return nullptr;
  }
  return ResolveURLUsingLocalRef(aFrame, url.AsUrl());
}

bool SVGObserverUtils::GetAndObserveMarkers(nsIFrame* aMarkedFrame,
                                            SVGMarkerFrame* (*aFrames)[3]) {
  MOZ_ASSERT(!aMarkedFrame->GetPrevContinuation() &&
                 aMarkedFrame->IsSVGGeometryFrame() &&
                 static_cast<SVGGeometryElement*>(aMarkedFrame->GetContent())
                     ->IsMarkable(),
             "Bad frame");

  bool foundMarker = false;
  RefPtr<URLAndReferrerInfo> markerURL;
  SVGMarkerObserver* observer;
  nsIFrame* marker;

#define GET_MARKER(type)                                                    \
  markerURL = GetMarkerURI(aMarkedFrame, &nsStyleSVG::mMarker##type);       \
  observer =                                                                \
      GetEffectProperty(markerURL, aMarkedFrame, Marker##type##Property()); \
  marker = observer ? observer->GetAndObserveReferencedFrame(               \
                          LayoutFrameType::SVGMarker, nullptr)              \
                    : nullptr;                                              \
  foundMarker = foundMarker || bool(marker);                                \
  (*aFrames)[SVGMark::e##type] = static_cast<SVGMarkerFrame*>(marker);

  GET_MARKER(Start)
  GET_MARKER(Mid)
  GET_MARKER(End)

#undef GET_MARKER

  return foundMarker;
}

// Note that the returned list will be empty in the case of a 'filter' property
// that only specifies CSS filter functions (no url()'s to SVG filters).
static SVGFilterObserverListForCSSProp* GetOrCreateFilterObserverListForCSS(
    nsIFrame* aFrame) {
  MOZ_ASSERT(!aFrame->GetPrevContinuation(), "Require first continuation");

  const nsStyleEffects* effects = aFrame->StyleEffects();
  if (!effects->HasFilters()) {
    return nullptr;
  }

  bool found;
  SVGFilterObserverListForCSSProp* observers =
      aFrame->GetProperty(FilterProperty(), &found);
  if (found) {
    MOZ_ASSERT(observers, "this property should only store non-null values");
    return observers;
  }
  observers =
      new SVGFilterObserverListForCSSProp(effects->mFilters.AsSpan(), aFrame);
  NS_ADDREF(observers);
  aFrame->AddProperty(FilterProperty(), observers);
  return observers;
}

static SVGObserverUtils::ReferenceState GetAndObserveFilters(
    SVGFilterObserverListForCSSProp* aObserverList,
    nsTArray<SVGFilterFrame*>* aFilterFrames) {
  if (!aObserverList) {
    return SVGObserverUtils::eHasNoRefs;
  }

  const nsTArray<RefPtr<SVGFilterObserver>>& observers =
      aObserverList->GetObservers();
  if (observers.IsEmpty()) {
    return SVGObserverUtils::eHasNoRefs;
  }

  for (uint32_t i = 0; i < observers.Length(); i++) {
    SVGFilterFrame* filter = observers[i]->GetAndObserveFilterFrame();
    if (!filter) {
      if (aFilterFrames) {
        aFilterFrames->Clear();
      }
      return SVGObserverUtils::eHasRefsSomeInvalid;
    }
    if (aFilterFrames) {
      aFilterFrames->AppendElement(filter);
    }
  }

  return SVGObserverUtils::eHasRefsAllValid;
}

SVGObserverUtils::ReferenceState SVGObserverUtils::GetAndObserveFilters(
    nsIFrame* aFilteredFrame, nsTArray<SVGFilterFrame*>* aFilterFrames) {
  SVGFilterObserverListForCSSProp* observerList =
      GetOrCreateFilterObserverListForCSS(aFilteredFrame);
  return mozilla::GetAndObserveFilters(observerList, aFilterFrames);
}

SVGObserverUtils::ReferenceState SVGObserverUtils::GetFiltersIfObserving(
    nsIFrame* aFilteredFrame, nsTArray<SVGFilterFrame*>* aFilterFrames) {
  SVGFilterObserverListForCSSProp* observerList =
      aFilteredFrame->GetProperty(FilterProperty());
  return mozilla::GetAndObserveFilters(observerList, aFilterFrames);
}

already_AddRefed<ISVGFilterObserverList>
SVGObserverUtils::ObserveFiltersForCanvasContext(
    CanvasRenderingContext2D* aContext, Element* aCanvasElement,
    const Span<const StyleFilter> aFilters) {
  return do_AddRef(new SVGFilterObserverListForCanvasContext(
      aContext, aCanvasElement, aFilters));
}

static SVGPaintingProperty* GetOrCreateClipPathObserver(
    nsIFrame* aClippedFrame) {
  MOZ_ASSERT(!aClippedFrame->GetPrevContinuation(),
             "Require first continuation");

  const nsStyleSVGReset* svgStyleReset = aClippedFrame->StyleSVGReset();
  if (!svgStyleReset->mClipPath.IsUrl()) {
    return nullptr;
  }
  const auto& url = svgStyleReset->mClipPath.AsUrl();
  RefPtr<URLAndReferrerInfo> pathURI =
      ResolveURLUsingLocalRef(aClippedFrame, url);
  return GetPaintingProperty(pathURI, aClippedFrame, ClipPathProperty());
}

SVGObserverUtils::ReferenceState SVGObserverUtils::GetAndObserveClipPath(
    nsIFrame* aClippedFrame, SVGClipPathFrame** aClipPathFrame) {
  if (aClipPathFrame) {
    *aClipPathFrame = nullptr;
  }
  SVGPaintingProperty* observers = GetOrCreateClipPathObserver(aClippedFrame);
  if (!observers) {
    return eHasNoRefs;
  }
  bool frameTypeOK = true;
  SVGClipPathFrame* frame =
      static_cast<SVGClipPathFrame*>(observers->GetAndObserveReferencedFrame(
          LayoutFrameType::SVGClipPath, &frameTypeOK));
  // Note that, unlike for filters, a reference to an ID that doesn't exist
  // is not invalid for clip-path or mask.
  if (!frameTypeOK || (frame && !frame->IsValid())) {
    return eHasRefsSomeInvalid;
  }
  if (aClipPathFrame) {
    *aClipPathFrame = frame;
  }
  return frame ? eHasRefsAllValid : eHasNoRefs;
}

static SVGMaskObserverList* GetOrCreateMaskObserverList(
    nsIFrame* aMaskedFrame) {
  MOZ_ASSERT(!aMaskedFrame->GetPrevContinuation(),
             "Require first continuation");

  const nsStyleSVGReset* style = aMaskedFrame->StyleSVGReset();
  if (!style->HasMask()) {
    return nullptr;
  }

  MOZ_ASSERT(style->mMask.mImageCount > 0);

  bool found;
  SVGMaskObserverList* prop = aMaskedFrame->GetProperty(MaskProperty(), &found);
  if (found) {
    MOZ_ASSERT(prop, "this property should only store non-null values");
    return prop;
  }
  prop = new SVGMaskObserverList(aMaskedFrame);
  NS_ADDREF(prop);
  aMaskedFrame->AddProperty(MaskProperty(), prop);
  return prop;
}

SVGObserverUtils::ReferenceState SVGObserverUtils::GetAndObserveMasks(
    nsIFrame* aMaskedFrame, nsTArray<SVGMaskFrame*>* aMaskFrames) {
  SVGMaskObserverList* observerList = GetOrCreateMaskObserverList(aMaskedFrame);
  if (!observerList) {
    return eHasNoRefs;
  }

  const nsTArray<RefPtr<SVGPaintingProperty>>& observers =
      observerList->GetObservers();
  if (observers.IsEmpty()) {
    return eHasNoRefs;
  }

  ReferenceState state = eHasRefsAllValid;

  for (size_t i = 0; i < observers.Length(); i++) {
    bool frameTypeOK = true;
    SVGMaskFrame* maskFrame =
        static_cast<SVGMaskFrame*>(observers[i]->GetAndObserveReferencedFrame(
            LayoutFrameType::SVGMask, &frameTypeOK));
    MOZ_ASSERT(!maskFrame || frameTypeOK);
    // XXXjwatt: this looks fishy
    if (!frameTypeOK) {
      // We can not find the specific SVG mask resource in the downloaded SVG
      // document. There are two possibilities:
      // 1. The given resource id is invalid.
      // 2. The given resource id refers to a viewbox.
      //
      // Hand it over to the style image.
      observerList->ResolveImage(i);
      state = eHasRefsSomeInvalid;
    }
    if (aMaskFrames) {
      aMaskFrames->AppendElement(maskFrame);
    }
  }

  return state;
}

SVGGeometryElement* SVGObserverUtils::GetAndObserveTextPathsPath(
    nsIFrame* aTextPathFrame) {
  // Continuations can come and go during reflow, and we don't need to observe
  // the referenced element more than once for a given node.
  aTextPathFrame = aTextPathFrame->FirstContinuation();

  SVGTextPathObserver* property =
      aTextPathFrame->GetProperty(HrefAsTextPathProperty());

  if (!property) {
    nsIContent* content = aTextPathFrame->GetContent();
    nsAutoString href;
    static_cast<SVGTextPathElement*>(content)->HrefAsString(href);
    if (href.IsEmpty()) {
      return nullptr;  // no URL
    }

    // There's no clear refererer policy spec about non-CSS SVG resource
    // references Bug 1415044 to investigate which referrer we should use
    nsIReferrerInfo* referrerInfo =
        content->OwnerDoc()->ReferrerInfoForInternalCSSAndSVGResources();
    RefPtr<URLAndReferrerInfo> target =
        ResolveURLUsingLocalRef(aTextPathFrame, href, referrerInfo);

    property =
        GetEffectProperty(target, aTextPathFrame, HrefAsTextPathProperty());
    if (!property) {
      return nullptr;
    }
  }

  return SVGGeometryElement::FromNodeOrNull(
      property->GetAndObserveReferencedElement());
}

void SVGObserverUtils::InitiateResourceDocLoads(nsIFrame* aFrame) {
  // We create observer objects and attach them to aFrame, but we do not
  // make aFrame start observing the referenced frames.
  Unused << GetOrCreateFilterObserverListForCSS(aFrame);
  Unused << GetOrCreateClipPathObserver(aFrame);
  Unused << GetOrCreateMaskObserverList(aFrame);
}

void SVGObserverUtils::RemoveTextPathObserver(nsIFrame* aTextPathFrame) {
  aTextPathFrame->RemoveProperty(HrefAsTextPathProperty());
}

nsIFrame* SVGObserverUtils::GetAndObserveTemplate(
    nsIFrame* aFrame, HrefToTemplateCallback aGetHref) {
  SVGTemplateElementObserver* observer =
      aFrame->GetProperty(HrefToTemplateProperty());

  if (!observer) {
    nsAutoString href;
    aGetHref(href);
    if (href.IsEmpty()) {
      return nullptr;  // no URL
    }

    // Convert href to an nsIURI
    nsIContent* content = aFrame->GetContent();

    nsCOMPtr<nsIURI> baseURI = content->GetBaseURI();
    if (nsContentUtils::IsLocalRefURL(href)) {
      baseURI = GetBaseURLForLocalRef(content, baseURI);
    }
    nsCOMPtr<nsIURI> targetURI;
    nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(targetURI), href, content->GetUncomposedDoc(), baseURI);

    // There's no clear refererer policy spec about non-CSS SVG resource
    // references.  Bug 1415044 to investigate which referrer we should use.
    nsIReferrerInfo* referrerInfo =
        content->OwnerDoc()->ReferrerInfoForInternalCSSAndSVGResources();
    RefPtr<URLAndReferrerInfo> target =
        new URLAndReferrerInfo(targetURI, referrerInfo);

    observer = GetEffectProperty(target, aFrame, HrefToTemplateProperty());
  }

  return observer ? observer->GetAndObserveReferencedFrame() : nullptr;
}

void SVGObserverUtils::RemoveTemplateObserver(nsIFrame* aFrame) {
  aFrame->RemoveProperty(HrefToTemplateProperty());
}

Element* SVGObserverUtils::GetAndObserveBackgroundImage(nsIFrame* aFrame,
                                                        const nsAtom* aHref) {
  // Continuations can come and go during reflow, and we don't need to observe
  // the referenced element more than once for a given node.
  aFrame = aFrame->FirstContinuation();
  bool found;
  URIObserverHashtable* hashtable =
      aFrame->GetProperty(BackgroundImageProperty(), &found);
  if (!found) {
    hashtable = new URIObserverHashtable();
    aFrame->AddProperty(BackgroundImageProperty(), hashtable);
  } else {
    MOZ_ASSERT(hashtable, "this property should only store non-null values");
  }

  nsAutoString elementId = u"#"_ns + nsDependentAtomString(aHref);
  nsCOMPtr<nsIURI> targetURI;
  nsContentUtils::NewURIWithDocumentCharset(
      getter_AddRefs(targetURI), elementId,
      aFrame->GetContent()->GetUncomposedDoc(),
      aFrame->GetContent()->GetBaseURI());
  nsIReferrerInfo* referrerInfo =
      aFrame->GetContent()
          ->OwnerDoc()
          ->ReferrerInfoForInternalCSSAndSVGResources();
  RefPtr<URLAndReferrerInfo> url =
      new URLAndReferrerInfo(targetURI, referrerInfo);

  return static_cast<SVGMozElementObserver*>(
             hashtable
                 ->LookupOrInsertWith(
                     url,
                     [&] {
                       return MakeRefPtr<SVGMozElementObserver>(
                           url, aFrame,
                           /* aWatchImage */ true);
                     })
                 .get())
      ->GetAndObserveReferencedElement();
}

Element* SVGObserverUtils::GetAndObserveBackgroundClip(nsIFrame* aFrame) {
  // Continuations can come and go during reflow, and we don't need to observe
  // the referenced element more than once for a given node.
  aFrame = aFrame->FirstContinuation();
  bool found;
  BackgroundClipRenderingObserver* obs =
      aFrame->GetProperty(BackgroundClipObserverProperty(), &found);
  if (!found) {
    obs = new BackgroundClipRenderingObserver(aFrame);
    NS_ADDREF(obs);
    aFrame->AddProperty(BackgroundClipObserverProperty(), obs);
  }

  return obs->GetAndObserveReferencedElement();
}

SVGPaintServerFrame* SVGObserverUtils::GetAndObservePaintServer(
    nsIFrame* aPaintedFrame, StyleSVGPaint nsStyleSVG::*aPaint) {
  // If we're looking at a frame within SVG text, then we need to look up
  // to find the right frame to get the painting property off.  We should at
  // least look up past a text frame, and if the text frame's parent is the
  // anonymous block frame, then we look up to its parent (the SVGTextFrame).
  nsIFrame* paintedFrame = aPaintedFrame;
  if (paintedFrame->IsInSVGTextSubtree()) {
    // Continuations can come and go during reflow, and we don't need to
    // observe the referenced element more than once for a given node.
    paintedFrame = paintedFrame->GetParent()->FirstContinuation();
    nsIFrame* grandparent = paintedFrame->GetParent()->FirstContinuation();
    if (grandparent && grandparent->IsSVGTextFrame()) {
      paintedFrame = grandparent;
    }
  }

  const nsStyleSVG* svgStyle = paintedFrame->StyleSVG();
  if (!(svgStyle->*aPaint).kind.IsPaintServer()) {
    return nullptr;
  }

  RefPtr<URLAndReferrerInfo> paintServerURL = ResolveURLUsingLocalRef(
      paintedFrame, (svgStyle->*aPaint).kind.AsPaintServer());

  MOZ_ASSERT(aPaint == &nsStyleSVG::mFill || aPaint == &nsStyleSVG::mStroke);
  PaintingPropertyDescriptor propDesc =
      (aPaint == &nsStyleSVG::mFill) ? FillProperty() : StrokeProperty();
  if (auto* property =
          GetPaintingProperty(paintServerURL, paintedFrame, propDesc)) {
    return do_QueryFrame(property->GetAndObserveReferencedFrame());
  }
  return nullptr;
}

void SVGObserverUtils::UpdateEffects(nsIFrame* aFrame) {
  NS_ASSERTION(aFrame->GetContent()->IsElement(),
               "aFrame's content should be an element");

  aFrame->RemoveProperty(FilterProperty());
  aFrame->RemoveProperty(MaskProperty());
  aFrame->RemoveProperty(ClipPathProperty());
  aFrame->RemoveProperty(MarkerStartProperty());
  aFrame->RemoveProperty(MarkerMidProperty());
  aFrame->RemoveProperty(MarkerEndProperty());
  aFrame->RemoveProperty(FillProperty());
  aFrame->RemoveProperty(StrokeProperty());
  aFrame->RemoveProperty(BackgroundImageProperty());

  // Ensure that the filter is repainted correctly
  // We can't do that in OnRenderingChange as the referenced frame may
  // not be valid
  GetOrCreateFilterObserverListForCSS(aFrame);

  if (aFrame->IsSVGGeometryFrame() &&
      static_cast<SVGGeometryElement*>(aFrame->GetContent())->IsMarkable()) {
    // Set marker properties here to avoid reference loops
    RefPtr<URLAndReferrerInfo> markerURL =
        GetMarkerURI(aFrame, &nsStyleSVG::mMarkerStart);
    GetEffectProperty(markerURL, aFrame, MarkerStartProperty());
    markerURL = GetMarkerURI(aFrame, &nsStyleSVG::mMarkerMid);
    GetEffectProperty(markerURL, aFrame, MarkerMidProperty());
    markerURL = GetMarkerURI(aFrame, &nsStyleSVG::mMarkerEnd);
    GetEffectProperty(markerURL, aFrame, MarkerEndProperty());
  }
}

void SVGObserverUtils::AddRenderingObserver(Element* aElement,
                                            SVGRenderingObserver* aObserver) {
  SVGRenderingObserverSet* observers = GetObserverSet(aElement);
  if (!observers) {
    observers = new SVGRenderingObserverSet();
    aElement->SetProperty(nsGkAtoms::renderingobserverset, observers,
                          nsINode::DeleteProperty<SVGRenderingObserverSet>);
  }
  aElement->SetHasRenderingObservers(true);
  observers->Add(aObserver);
}

void SVGObserverUtils::RemoveRenderingObserver(
    Element* aElement, SVGRenderingObserver* aObserver) {
  SVGRenderingObserverSet* observers = GetObserverSet(aElement);
  if (observers) {
    NS_ASSERTION(observers->Contains(aObserver),
                 "removing observer from an element we're not observing?");
    observers->Remove(aObserver);
    if (observers->IsEmpty()) {
      aElement->SetHasRenderingObservers(false);
    }
  }
}

void SVGObserverUtils::RemoveAllRenderingObservers(Element* aElement) {
  SVGRenderingObserverSet* observers = GetObserverSet(aElement);
  if (observers) {
    observers->RemoveAll();
    aElement->SetHasRenderingObservers(false);
  }
}

void SVGObserverUtils::InvalidateRenderingObservers(nsIFrame* aFrame) {
  NS_ASSERTION(!aFrame->GetPrevContinuation(),
               "aFrame must be first continuation");

  auto* element = Element::FromNodeOrNull(aFrame->GetContent());
  if (!element) {
    return;
  }

  // If the rendering has changed, the bounds may well have changed too:
  aFrame->RemoveProperty(SVGUtils::ObjectBoundingBoxProperty());

  if (auto* observers = GetObserverSet(element)) {
    observers->InvalidateAll();
    return;
  }

  // Check ancestor SVG containers. The root frame cannot be of type
  // eSVGContainer so we don't have to check f for null here.
  for (nsIFrame* f = aFrame->GetParent();
       f->IsFrameOfType(nsIFrame::eSVGContainer); f = f->GetParent()) {
    if (auto* element = Element::FromNode(f->GetContent())) {
      if (auto* observers = GetObserverSet(element)) {
        observers->InvalidateAll();
        return;
      }
    }
  }
}

void SVGObserverUtils::InvalidateDirectRenderingObservers(
    Element* aElement, uint32_t aFlags /* = 0 */) {
  if (nsIFrame* frame = aElement->GetPrimaryFrame()) {
    // If the rendering has changed, the bounds may well have changed too:
    frame->RemoveProperty(SVGUtils::ObjectBoundingBoxProperty());
  }

  if (aElement->HasRenderingObservers()) {
    SVGRenderingObserverSet* observers = GetObserverSet(aElement);
    if (observers) {
      if (aFlags & INVALIDATE_REFLOW) {
        observers->InvalidateAllForReflow();
      } else {
        observers->InvalidateAll();
      }
    }
  }
}

void SVGObserverUtils::InvalidateDirectRenderingObservers(
    nsIFrame* aFrame, uint32_t aFlags /* = 0 */) {
  if (auto* element = Element::FromNodeOrNull(aFrame->GetContent())) {
    InvalidateDirectRenderingObservers(element, aFlags);
  }
}

already_AddRefed<nsIURI> SVGObserverUtils::GetBaseURLForLocalRef(
    nsIContent* content, nsIURI* aDocURI) {
  MOZ_ASSERT(content);

  // Content is in a shadow tree.  If this URL was specified in the subtree
  // referenced by the <use>, element, and that subtree came from a separate
  // resource document, then we want the fragment-only URL to resolve to an
  // element from the resource document.  Otherwise, the URL was specified
  // somewhere in the document with the <use> element, and we want the
  // fragment-only URL to resolve to an element in that document.
  if (SVGUseElement* use = content->GetContainingSVGUseShadowHost()) {
    if (nsIURI* originalURI = use->GetSourceDocURI()) {
      bool isEqualsExceptRef = false;
      aDocURI->EqualsExceptRef(originalURI, &isEqualsExceptRef);
      if (isEqualsExceptRef) {
        return do_AddRef(originalURI);
      }
    }
  }

  // For a local-reference URL, resolve that fragment against the current
  // document that relative URLs are resolved against.
  return do_AddRef(content->OwnerDoc()->GetDocumentURI());
}

already_AddRefed<URLAndReferrerInfo> SVGObserverUtils::GetFilterURI(
    nsIFrame* aFrame, const StyleFilter& aFilter) {
  MOZ_ASSERT(!aFrame->StyleEffects()->mFilters.IsEmpty() ||
             !aFrame->StyleEffects()->mBackdropFilters.IsEmpty() ||
             !aFrame->GetContent()->GetParent());
  MOZ_ASSERT(aFilter.IsUrl());
  return ResolveURLUsingLocalRef(aFrame, aFilter.AsUrl());
}

}  // namespace mozilla
