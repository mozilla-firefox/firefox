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
#include "mozilla/dom/SVGMPathElement.h"
#include "mozilla/dom/SVGTextPathElement.h"
#include "mozilla/dom/SVGUseElement.h"
#include "mozilla/PresShell.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SVGClipPathFrame.h"
#include "mozilla/SVGGeometryFrame.h"
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

/*
 * This class contains URL and referrer information (referrer and referrer
 * policy).
 * We use it to pass to svg system instead of nsIURI. The object brings referrer
 * and referrer policy so we can send correct Referer headers.
 */
class SVGReference {
 public:
  SVGReference(const nsAString& aLocalRef, nsIURI* aBase,
               nsIReferrerInfo* aReferrerInfo)
      : mLocalRef(aLocalRef), mURI(aBase), mReferrerInfo(aReferrerInfo) {
    MOZ_ASSERT(nsContentUtils::IsLocalRefURL(mLocalRef));
  }

  SVGReference(const nsACString& aLocalRef, const URLExtraData& aExtraData)
      : mURI(aExtraData.BaseURI()), mReferrerInfo(aExtraData.ReferrerInfo()) {
    CopyUTF8toUTF16(aLocalRef, mLocalRef);
    MOZ_ASSERT(nsContentUtils::IsLocalRefURL(mLocalRef));
  }

  SVGReference(nsIURI* aURI, nsIReferrerInfo* aReferrerInfo)
      : mURI(aURI), mReferrerInfo(aReferrerInfo) {
    MOZ_ASSERT(aURI);
  }

  SVGReference(nsIURI* aURI, const URLExtraData& aExtraData)
      : mURI(aURI), mReferrerInfo(aExtraData.ReferrerInfo()) {
    MOZ_ASSERT(aURI);
  }

  NS_INLINE_DECL_REFCOUNTING(SVGReference)

  bool IsLocalRef() const { return !mLocalRef.IsEmpty(); }
  const nsAString& GetLocalRef() const { return mLocalRef; }

  nsIURI* GetURI() const { return mURI; }
  nsIReferrerInfo* GetReferrerInfo() const { return mReferrerInfo; }

  bool operator==(const SVGReference& aRHS) const;

 private:
  ~SVGReference() = default;

  nsString mLocalRef;
  // For local refs, this is the base URI that specified the URL. For non-local
  // refs, this is the whole URI. This is needed so that we can distinguish URIs
  // from an inner shadow tree and inherited ones.
  const nsCOMPtr<nsIURI> mURI;
  const nsCOMPtr<nsIReferrerInfo> mReferrerInfo;
};

bool SVGReference::operator==(const SVGReference& aRHS) const {
  if (mLocalRef != aRHS.mLocalRef) {
    return false;
  }
  bool uriEqual = false, referrerEqual = false;
  mURI->Equals(aRHS.mURI, &uriEqual);
  mReferrerInfo->Equals(aRHS.mReferrerInfo, &referrerEqual);
  return uriEqual && referrerEqual;
}

class SVGReferenceHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = const SVGReference*;
  using KeyTypePointer = const SVGReference*;

  explicit SVGReferenceHashKey(const SVGReference* aKey) noexcept : mKey(aKey) {
    MOZ_COUNT_CTOR(SVGReferenceHashKey);
  }
  SVGReferenceHashKey(SVGReferenceHashKey&& aToMove) noexcept
      : PLDHashEntryHdr(std::move(aToMove)), mKey(std::move(aToMove.mKey)) {
    MOZ_COUNT_CTOR(SVGReferenceHashKey);
  }
  MOZ_COUNTED_DTOR(SVGReferenceHashKey)

  const SVGReference* GetKey() const { return mKey; }

  bool KeyEquals(const SVGReference* aKey) const {
    if (!mKey) {
      return !aKey;
    }
    return *mKey == *aKey;
  }

  static const SVGReference* KeyToPointer(const SVGReference* aKey) {
    return aKey;
  }

  static PLDHashNumber HashKey(const SVGReference* aKey) {
    MOZ_ASSERT(aKey);

    nsAutoCString urlSpec, referrerSpec;
    // nsURIHashKey ignores GetSpec() failures, so we do too:
    Unused << aKey->GetURI()->GetSpec(urlSpec);
    return AddToHash(
        HashString(aKey->GetLocalRef()), HashString(urlSpec),
        static_cast<ReferrerInfo*>(aKey->GetReferrerInfo())->Hash());
  }

  enum { ALLOW_MEMMOVE = true };

 protected:
  RefPtr<const SVGReference> mKey;
};

static already_AddRefed<SVGReference> ResolveURLUsingLocalRef(
    const StyleComputedUrl& aURL) {
  if (aURL.IsLocalRef()) {
    return MakeAndAddRef<SVGReference>(aURL.SpecifiedSerialization(),
                                       aURL.ExtraData());
  }

  nsCOMPtr<nsIURI> uri = aURL.GetURI();
  if (!uri) {
    return nullptr;
  }

  return do_AddRef(new SVGReference(uri, aURL.ExtraData()));
}

static already_AddRefed<SVGReference> ResolveURLUsingLocalRef(
    nsIContent* aContent, const nsAString& aURL) {
  // We want to resolve the URL against any <use> element shadow tree's source
  // document. We are assuming that the URL was specified directly on mFrame's
  // content (because this ResolveURLUsingLocalRef overload is used for href=""
  // attributes and not CSS URL values), so there is no need to check whether
  // the URL was specified / inherited from outside the shadow tree.
  nsIURI* base = nullptr;
  const Encoding* encoding = nullptr;
  if (SVGUseElement* use = aContent->GetContainingSVGUseShadowHost()) {
    base = use->GetSourceDocURI();
    encoding = use->GetSourceDocCharacterSet();
  }

  // There's no clear refererer policy spec about non-CSS SVG resource
  // references Bug 1415044 to investigate which referrer we should use
  nsIReferrerInfo* referrerInfo =
      aContent->OwnerDoc()->ReferrerInfoForInternalCSSAndSVGResources();

  if (nsContentUtils::IsLocalRefURL(aURL)) {
    return MakeAndAddRef<SVGReference>(aURL, base, referrerInfo);
  }

  if (!base) {
    base = aContent->OwnerDoc()->GetDocumentURI();
    encoding = aContent->OwnerDoc()->GetDocumentCharacterSet();
  }

  nsCOMPtr<nsIURI> uri;
  Unused << NS_NewURI(getter_AddRefs(uri), aURL, WrapNotNull(encoding), base);
  if (!uri) {
    return nullptr;
  }
  return do_AddRef(new SVGReference(uri, referrerInfo));
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

void SVGRenderingObserver::ContentWillBeRemoved(
    nsIContent* aChild, const BatchRemovalState* aState) {
  if (aState && !aState->mIsFirst) {
    return;
  }
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
      SVGReference* aReference, Element* aObservingElement,
      bool aReferenceImage,
      uint32_t aCallbacks = kAttributeChanged | kContentAppended |
                            kContentInserted | kContentWillBeRemoved,
      TargetIsValidCallback aTargetIsValidCallback = nullptr);

  void Traverse(nsCycleCollectionTraversalCallback* aCB);

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
      if (observed->OwnerDoc() == mObservingElement->OwnerDoc() &&
          nsContentUtils::ContentIsHostIncludingDescendantOf(mObservingElement,
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
  RefPtr<Element> mObservingElement;
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
    SVGReference* aReference, Element* aObservingElement, bool aReferenceImage,
    uint32_t aCallbacks, TargetIsValidCallback aTargetIsValidCallback)
    : SVGRenderingObserver(aCallbacks),
      mObservedElementTracker(this),
      mObservingElement(aObservingElement),
      mTargetIsValidCallback(aTargetIsValidCallback) {
  // Start watching the target element
  if (aReference) {
    if (aReference->IsLocalRef()) {
      mObservedElementTracker.ResetToLocalFragmentID(
          *aObservingElement, aReference->GetLocalRef(), aReference->GetURI(),
          aReference->GetReferrerInfo(), aReferenceImage);
    } else {
      mObservedElementTracker.ResetToURIWithFragmentID(
          *aObservingElement, aReference->GetURI(),
          aReference->GetReferrerInfo(), aReferenceImage);
    }
  } else {
    mObservedElementTracker.Unlink();
  }

  TargetChanged();
  StartObserving();
}

void SVGIDRenderingObserver::Traverse(nsCycleCollectionTraversalCallback* aCB) {
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCB, "mObservingElement");
  aCB->NoteXPCOMChild(mObservingElement);
  mObservedElementTracker.Traverse(aCB);
}

void SVGIDRenderingObserver::OnRenderingChange() {
  if (mObservedElementTracker.get() && mInObserverSet) {
    SVGObserverUtils::RemoveRenderingObserver(mObservedElementTracker.get(),
                                              this);
    mInObserverSet = false;
  }
}

// Convenience function to return aFrame->GetContent() as an Element* if the
// content pointer is non-null (or just return nullptr otherwise).
// (AsElement itself isn't callable on null pointers.)
static Element* GetFrameContentAsElement(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "Expecting a non-null frame");
  auto* content = aFrame->GetContent();
  if (content) {
    return content->AsElement();
  }
  return nullptr;
}

class SVGRenderingObserverProperty : public SVGIDRenderingObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGRenderingObserverProperty(
      SVGReference* aReference, nsIFrame* aFrame, bool aReferenceImage,
      uint32_t aCallbacks = kAttributeChanged | kContentAppended |
                            kContentInserted | kContentWillBeRemoved,
      TargetIsValidCallback aTargetIsValidCallback = nullptr)
      : SVGIDRenderingObserver(aReference, GetFrameContentAsElement(aFrame),
                               aReferenceImage, aCallbacks,
                               aTargetIsValidCallback),
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

static bool IsSVGGeometryElement(const Element& aObserved) {
  return aObserved.IsSVGGeometryElement();
}

class SVGTextPathObserver final : public SVGRenderingObserverProperty {
 public:
  SVGTextPathObserver(SVGReference* aReference, nsIFrame* aFrame,
                      bool aReferenceImage)
      : SVGRenderingObserverProperty(aReference, aFrame, aReferenceImage,
                                     kAttributeChanged, IsSVGGeometryElement) {}

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

  MOZ_ASSERT(frame->IsSVGFrame() || frame->IsInSVGTextSubtree(),
             "SVG frame expected");

  MOZ_ASSERT(frame->GetContent()->IsSVGElement(nsGkAtoms::textPath),
             "expected frame for a <textPath> element");

  auto* text = static_cast<SVGTextFrame*>(
      nsLayoutUtils::GetClosestFrameOfType(frame, LayoutFrameType::SVGText));
  MOZ_ASSERT(text, "expected to find an ancestor SVGTextFrame");
  if (text) {
    text->AddStateBits(NS_STATE_SVG_POSITIONING_DIRTY);

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

class SVGMPathObserver final : public SVGIDRenderingObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGMPathObserver(SVGReference* aReference, SVGMPathElement* aElement)
      : SVGIDRenderingObserver(aReference, aElement,
                               /* aReferenceImage = */ false, kAttributeChanged,
                               IsSVGGeometryElement) {}

 protected:
  virtual ~SVGMPathObserver() = default;  // non-public

  void OnRenderingChange() override;
};

NS_IMPL_ISUPPORTS(SVGMPathObserver, nsIMutationObserver)

void SVGMPathObserver::OnRenderingChange() {
  SVGIDRenderingObserver::OnRenderingChange();

  if (!mTargetIsValid) {
    return;
  }

  auto* element = static_cast<SVGMPathElement*>(mObservingElement.get());
  element->NotifyParentOfMpathChange();
}

class SVGMarkerObserver final : public SVGRenderingObserverProperty {
 public:
  SVGMarkerObserver(SVGReference* aReference, nsIFrame* aFrame,
                    bool aReferenceImage)
      : SVGRenderingObserverProperty(aReference, aFrame, aReferenceImage,
                                     kAttributeChanged | kContentAppended |
                                         kContentInserted |
                                         kContentWillBeRemoved) {}

 protected:
  void OnRenderingChange() override;
};

void SVGMarkerObserver::OnRenderingChange() {
  SVGRenderingObserverProperty::OnRenderingChange();

  nsIFrame* frame = mFrameReference.Get();
  if (!frame) {
    return;
  }

  MOZ_ASSERT(frame->IsSVGFrame(), "SVG frame expected");

  // Don't need to request ReflowFrame if we're being reflowed.
  // Because mRect for SVG frames includes the bounds of any markers
  // (see the comment for nsIFrame::GetRect), the referencing frame must be
  // reflowed for any marker changes.
  if (!frame->HasAnyStateBits(NS_FRAME_IN_REFLOW)) {
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
  SVGPaintingProperty(SVGReference* aReference, nsIFrame* aFrame,
                      bool aReferenceImage)
      : SVGRenderingObserverProperty(aReference, aFrame, aReferenceImage) {}

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
  SVGMozElementObserver(SVGReference* aReference, nsIFrame* aFrame)
      : SVGPaintingProperty(aReference, aFrame, /* aReferenceImage = */ true) {}

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
  SVGFilterObserver(SVGReference* aReference, Element* aObservingElement,
                    SVGFilterObserverList* aFilterChainObserver)
      : SVGIDRenderingObserver(aReference, aObservingElement, false,
                               kAttributeChanged | kContentAppended |
                                   kContentInserted | kContentWillBeRemoved,
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
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mObservingElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(SVGFilterObserver)
  tmp->StopObserving();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObservedElementTracker);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObservingElement)
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
                        Element* aFilteredElement,
                        nsIFrame* aFilteredFrame = nullptr);

  const nsTArray<RefPtr<SVGFilterObserver>>& GetObservers() const override {
    return mObservers;
  }

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(SVGFilterObserverList,
                                           ISVGFilterObserverList)

  virtual void OnRenderingChange(Element* aObservingElement) = 0;

 protected:
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

  if (!mTargetIsValid) {
    return;
  }

  if (mFilterObserverList) {
    mFilterObserverList->OnRenderingChange(mObservingElement);
  }
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
                                             Element* aFilteredElement,
                                             nsIFrame* aFilteredFrame) {
  for (const auto& filter : aFilters) {
    if (!filter.IsUrl()) {
      continue;
    }

    const auto& url = filter.AsUrl();

    // aFilteredFrame can be null if this filter belongs to a
    // CanvasRenderingContext2D.
    RefPtr<SVGReference> filterURL = ResolveURLUsingLocalRef(url);
    auto observer =
        MakeRefPtr<SVGFilterObserver>(filterURL, aFilteredElement, this);
    mObservers.AppendElement(std::move(observer));
  }
}

SVGFilterObserverList::~SVGFilterObserverList() { DetachObservers(); }

class SVGFilterObserverListForCSSProp final : public SVGFilterObserverList {
 public:
  SVGFilterObserverListForCSSProp(Span<const StyleFilter> aFilters,
                                  nsIFrame* aFilteredFrame)
      : SVGFilterObserverList(aFilters,
                              GetFrameContentAsElement(aFilteredFrame),
                              aFilteredFrame) {}

 protected:
  void OnRenderingChange(Element* aObservingElement) override;
};

void SVGFilterObserverListForCSSProp::OnRenderingChange(
    Element* aObservingElement) {
  nsIFrame* frame = aObservingElement->GetPrimaryFrame();
  if (!frame) {
    return;
  }
  // Repaint asynchronously in case the filter frame is being torn down
  auto changeHint = nsChangeHint_RepaintFrame;

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
      aObservingElement, RestyleHint{0}, changeHint);
}

class SVGFilterObserverListForCanvasContext final
    : public SVGFilterObserverList {
 public:
  SVGFilterObserverListForCanvasContext(CanvasRenderingContext2D* aContext,
                                        Element* aCanvasElement,
                                        Span<const StyleFilter> aFilters)
      : SVGFilterObserverList(aFilters, aCanvasElement), mContext(aContext) {}

  void OnRenderingChange(Element* aObservingElement) override;
  void Detach() override { mContext = nullptr; }

 private:
  CanvasRenderingContext2D* mContext;
};

void SVGFilterObserverListForCanvasContext::OnRenderingChange(
    Element* aObservingElement) {
  if (!mContext) {
    NS_WARNING(
        "GFX: This should never be called without a context, except during "
        "cycle collection (when Detach has been called)");
    return;
  }
  // Refresh the cached FilterDescription in mContext->CurrentState().filter.
  // If this filter is not at the top of the state stack, we'll refresh the
  // wrong filter, but that's ok, because we'll refresh the right filter
  // when we pop the state stack in CanvasRenderingContext2D::Restore().
  //
  // We don't need to flush, we're called by layout.
  RefPtr<CanvasRenderingContext2D> kungFuDeathGrip(mContext);
  kungFuDeathGrip->UpdateFilter(/* aFlushIfNeeded = */ false);
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
    const StyleComputedUrl* data =
        svgReset->mMask.mLayers[i].mImage.GetImageRequestURLValue();
    RefPtr<SVGReference> maskUri;
    if (data) {
      maskUri = ResolveURLUsingLocalRef(*data);
    }

    bool hasRef = false;
    if (maskUri) {
      if (maskUri->IsLocalRef()) {
        hasRef = true;
      } else {
        maskUri->GetURI()->GetHasRef(&hasRef);
      }
    }

    // According to maskUri, SVGPaintingProperty's ctor may trigger an external
    // SVG resource download, so we should pass maskUri in only if maskUri has a
    // chance pointing to an SVG mask resource.
    //
    // And, an URL may refer to an SVG mask resource if it consists of a
    // fragment.
    auto prop = MakeRefPtr<SVGPaintingProperty>(
        hasRef ? maskUri.get() : nullptr, aFrame, false);
    mProperties.AppendElement(std::move(prop));
  }
}

void SVGMaskObserverList::ResolveImage(uint32_t aIndex) {
  const nsStyleSVGReset* svgReset = mFrame->StyleSVGReset();
  MOZ_ASSERT(aIndex < svgReset->mMask.mImageCount);

  const auto& image = svgReset->mMask.mLayers[aIndex].mImage;
  if (image.IsResolved()) {
    return;
  }
  MOZ_ASSERT(image.IsImageRequestType());
  Document* doc = mFrame->PresContext()->Document();
  const_cast<StyleImage&>(image).ResolveImage(*doc, nullptr);
  if (imgRequestProxy* req = image.GetImageRequest()) {
    // FIXME(emilio): What disassociates this request?
    doc->StyleImageLoader()->AssociateRequestToFrame(req, mFrame);
  }
}

/**
 * Used for gradient-to-gradient, pattern-to-pattern and filter-to-filter
 * references to "template" elements (specified via the 'href' attributes).
 */
class SVGTemplateElementObserver : public SVGIDRenderingObserver {
 public:
  NS_DECL_ISUPPORTS

  SVGTemplateElementObserver(SVGReference* aReference, nsIFrame* aFrame,
                             bool aReferenceImage)
      : SVGIDRenderingObserver(aReference, GetFrameContentAsElement(aFrame),
                               aReferenceImage,
                               kAttributeChanged | kContentAppended |
                                   kContentInserted | kContentWillBeRemoved),
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
    SVGObserverUtils::InvalidateRenderingObservers(frame);
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

  ~SVGRenderingObserverSet() { MOZ_COUNT_DTOR(SVGRenderingObserverSet); }

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

  // We've moved all the observers from mObservers, effectively
  // evicting them so we need to notify all observers of eviction
  // before we process any rendering changes. In short, don't
  // try to merge these loops.
  for (const auto& observer : observers) {
    observer->NotifyEvictedFromRenderingObserverSet();
  }
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
      obs->NotifyEvictedFromRenderingObserverSet();
    }
  }

  for (const auto& observer : observers) {
    observer->OnNonDOMMutationRenderingChange();
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
    nsInterfaceHashtable<SVGReferenceHashKey, nsIMutationObserver>;

using PaintingPropertyDescriptor =
    const FramePropertyDescriptor<SVGPaintingProperty>*;

static void DestroyFilterProperty(SVGFilterObserverListForCSSProp* aProp) {
  aProp->Release();
}

NS_DECLARE_FRAME_PROPERTY_RELEASABLE(HrefToTemplateProperty,
                                     SVGTemplateElementObserver)
NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(BackdropFilterProperty,
                                    SVGFilterObserverListForCSSProp,
                                    DestroyFilterProperty)
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
NS_DECLARE_FRAME_PROPERTY_RELEASABLE(OffsetPathProperty,
                                     SVGRenderingObserverProperty)

template <class T>
static T* GetEffectProperty(SVGReference* aReference, nsIFrame* aFrame,
                            const FramePropertyDescriptor<T>* aProperty) {
  if (!aReference) {
    return nullptr;
  }

  bool found;
  T* prop = aFrame->GetProperty(aProperty, &found);
  if (found) {
    MOZ_ASSERT(prop, "this property should only store non-null values");
    return prop;
  }
  prop = new T(aReference, aFrame, false);
  NS_ADDREF(prop);
  aFrame->AddProperty(aProperty, prop);
  return prop;
}

static SVGPaintingProperty* GetPaintingProperty(
    SVGReference* aReference, nsIFrame* aFrame,
    const FramePropertyDescriptor<SVGPaintingProperty>* aProperty) {
  return GetEffectProperty(aReference, aFrame, aProperty);
}

static already_AddRefed<SVGReference> GetMarkerURI(
    nsIFrame* aFrame, const StyleUrlOrNone nsStyleSVG::* aMarker) {
  const StyleUrlOrNone& url = aFrame->StyleSVG()->*aMarker;
  if (url.IsNone()) {
    return nullptr;
  }
  return ResolveURLUsingLocalRef(url.AsUrl());
}

bool SVGObserverUtils::GetAndObserveMarkers(nsIFrame* aMarkedFrame,
                                            SVGMarkerFrame* (*aFrames)[3]) {
  MOZ_ASSERT(!aMarkedFrame->GetPrevContinuation() &&
                 aMarkedFrame->IsSVGGeometryFrame() &&
                 static_cast<SVGGeometryElement*>(aMarkedFrame->GetContent())
                     ->IsMarkable(),
             "Bad frame");

  bool foundMarker = false;
  RefPtr<SVGReference> markerURL;
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
template <typename P>
static SVGFilterObserverListForCSSProp* GetOrCreateFilterObserverListForCSS(
    nsIFrame* aFrame, bool aHasFilters,
    FrameProperties::Descriptor<P> aProperty,
    Span<const StyleFilter> aFilters) {
  if (!aHasFilters) {
    return nullptr;
  }

  bool found;
  SVGFilterObserverListForCSSProp* observers =
      aFrame->GetProperty(aProperty, &found);
  if (found) {
    MOZ_ASSERT(observers, "this property should only store non-null values");
    return observers;
  }
  observers = new SVGFilterObserverListForCSSProp(aFilters, aFrame);
  NS_ADDREF(observers);
  aFrame->AddProperty(aProperty, observers);
  return observers;
}

static SVGFilterObserverListForCSSProp* GetOrCreateFilterObserverListForCSS(
    nsIFrame* aFrame, StyleFilterType aStyleFilterType) {
  MOZ_ASSERT(!aFrame->GetPrevContinuation(), "Require first continuation");

  const nsStyleEffects* effects = aFrame->StyleEffects();

  return aStyleFilterType == StyleFilterType::BackdropFilter
             ? GetOrCreateFilterObserverListForCSS(
                   aFrame, effects->HasBackdropFilters(),
                   BackdropFilterProperty(), effects->mBackdropFilters.AsSpan())
             : GetOrCreateFilterObserverListForCSS(
                   aFrame, effects->HasFilters(), FilterProperty(),
                   effects->mFilters.AsSpan());
}

static SVGObserverUtils::ReferenceState GetAndObserveFilters(
    ISVGFilterObserverList* aObserverList,
    nsTArray<SVGFilterFrame*>* aFilterFrames) {
  if (!aObserverList) {
    return SVGObserverUtils::eHasNoRefs;
  }

  const nsTArray<RefPtr<SVGFilterObserver>>& observers =
      aObserverList->GetObservers();
  if (observers.IsEmpty()) {
    return SVGObserverUtils::eHasNoRefs;
  }

  for (const auto& observer : observers) {
    SVGFilterFrame* filter = observer->GetAndObserveFilterFrame();
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
    nsIFrame* aFilteredFrame, nsTArray<SVGFilterFrame*>* aFilterFrames,
    StyleFilterType aStyleFilterType) {
  SVGFilterObserverListForCSSProp* observerList =
      GetOrCreateFilterObserverListForCSS(aFilteredFrame, aStyleFilterType);
  return mozilla::GetAndObserveFilters(observerList, aFilterFrames);
}

SVGObserverUtils::ReferenceState SVGObserverUtils::GetAndObserveFilters(
    ISVGFilterObserverList* aObserverList,
    nsTArray<SVGFilterFrame*>* aFilterFrames) {
  return mozilla::GetAndObserveFilters(aObserverList, aFilterFrames);
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
  RefPtr<SVGReference> pathURI = ResolveURLUsingLocalRef(url);
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
  if (!frameTypeOK) {
    return eHasRefsSomeInvalid;
  }
  if (aClipPathFrame) {
    *aClipPathFrame = frame;
  }
  return frame ? eHasRefsAllValid : eHasNoRefs;
}

static SVGRenderingObserverProperty* GetOrCreateGeometryObserver(
    nsIFrame* aFrame) {
  // Now only offset-path property uses this. See MotionPathUtils.cpp.
  const nsStyleDisplay* disp = aFrame->StyleDisplay();
  if (!disp->mOffsetPath.IsUrl()) {
    return nullptr;
  }
  const auto& url = disp->mOffsetPath.AsUrl();
  RefPtr<SVGReference> pathURI = ResolveURLUsingLocalRef(url);
  return GetEffectProperty(pathURI, aFrame, OffsetPathProperty());
}

SVGGeometryElement* SVGObserverUtils::GetAndObserveGeometry(nsIFrame* aFrame) {
  SVGRenderingObserverProperty* observers = GetOrCreateGeometryObserver(aFrame);
  if (!observers) {
    return nullptr;
  }

  bool frameTypeOK = true;
  SVGGeometryFrame* frame =
      do_QueryFrame(observers->GetAndObserveReferencedFrame(
          LayoutFrameType::SVGGeometry, &frameTypeOK));
  if (!frameTypeOK || !frame) {
    return nullptr;
  }

  return static_cast<dom::SVGGeometryElement*>(frame->GetContent());
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

    RefPtr<SVGReference> target = ResolveURLUsingLocalRef(content, href);

    property =
        GetEffectProperty(target, aTextPathFrame, HrefAsTextPathProperty());
    if (!property) {
      return nullptr;
    }
  }

  return SVGGeometryElement::FromNodeOrNull(
      property->GetAndObserveReferencedElement());
}

SVGGeometryElement* SVGObserverUtils::GetAndObserveMPathsPath(
    SVGMPathElement* aSVGMPathElement) {
  if (!aSVGMPathElement->mMPathObserver) {
    nsAutoString href;
    aSVGMPathElement->HrefAsString(href);
    if (href.IsEmpty()) {
      return nullptr;  // no URL
    }

    RefPtr<SVGReference> target =
        ResolveURLUsingLocalRef(aSVGMPathElement, href);

    aSVGMPathElement->mMPathObserver =
        new SVGMPathObserver(target, aSVGMPathElement);
  }

  return SVGGeometryElement::FromNodeOrNull(
      static_cast<SVGMPathObserver*>(aSVGMPathElement->mMPathObserver.get())
          ->GetAndObserveReferencedElement());
}

void SVGObserverUtils::TraverseMPathObserver(
    SVGMPathElement* aSVGMPathElement,
    nsCycleCollectionTraversalCallback* aCB) {
  if (aSVGMPathElement->mMPathObserver) {
    static_cast<SVGMPathObserver*>(aSVGMPathElement->mMPathObserver.get())
        ->Traverse(aCB);
  }
}

void SVGObserverUtils::InitiateResourceDocLoads(nsIFrame* aFrame) {
  // We create observer objects and attach them to aFrame, but we do not
  // make aFrame start observing the referenced frames.
  Unused << GetOrCreateFilterObserverListForCSS(
      aFrame, StyleFilterType::BackdropFilter);
  Unused << GetOrCreateFilterObserverListForCSS(aFrame,
                                                StyleFilterType::Filter);
  Unused << GetOrCreateClipPathObserver(aFrame);
  Unused << GetOrCreateGeometryObserver(aFrame);
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

    RefPtr<SVGReference> info =
        ResolveURLUsingLocalRef(aFrame->GetContent(), href);

    observer = GetEffectProperty(info, aFrame, HrefToTemplateProperty());
  }

  return observer ? observer->GetAndObserveReferencedFrame() : nullptr;
}

void SVGObserverUtils::RemoveTemplateObserver(nsIFrame* aFrame) {
  aFrame->RemoveProperty(HrefToTemplateProperty());
}

Element* SVGObserverUtils::GetAndObserveBackgroundImage(nsIFrame* aFrame,
                                                        const nsAtom* aHref) {
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
  auto url = MakeRefPtr<SVGReference>(targetURI, referrerInfo);

  return static_cast<SVGMozElementObserver*>(
             hashtable
                 ->LookupOrInsertWith(
                     url,
                     [&] {
                       return MakeRefPtr<SVGMozElementObserver>(url, aFrame);
                     })
                 .get())
      ->GetAndObserveReferencedElement();
}

Element* SVGObserverUtils::GetAndObserveBackgroundClip(nsIFrame* aFrame) {
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
    nsIFrame* aPaintedFrame, StyleSVGPaint nsStyleSVG::* aPaint) {
  // If we're looking at a frame within SVG text, then we need to look up
  // to find the right frame to get the painting property off.  We should at
  // least look up past a text frame, and if the text frame's parent is the
  // anonymous block frame, then we look up to its parent (the SVGTextFrame).
  nsIFrame* paintedFrame = aPaintedFrame;
  if (paintedFrame->IsInSVGTextSubtree()) {
    paintedFrame = paintedFrame->GetParent();
    nsIFrame* grandparent = paintedFrame->GetParent();
    if (grandparent && grandparent->IsSVGTextFrame()) {
      paintedFrame = grandparent;
    }
  }

  const nsStyleSVG* svgStyle = paintedFrame->StyleSVG();
  if (!(svgStyle->*aPaint).kind.IsPaintServer()) {
    return nullptr;
  }

  RefPtr<SVGReference> paintServerURL =
      ResolveURLUsingLocalRef((svgStyle->*aPaint).kind.AsPaintServer());

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
  NS_ASSERTION(!aFrame->GetContent() || aFrame->GetContent()->IsElement(),
               "aFrame's content (if non-null) should be an element");

  aFrame->RemoveProperty(BackdropFilterProperty());
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
  GetOrCreateFilterObserverListForCSS(aFrame, StyleFilterType::BackdropFilter);
  GetOrCreateFilterObserverListForCSS(aFrame, StyleFilterType::Filter);

  if (aFrame->IsSVGGeometryFrame() &&
      static_cast<SVGGeometryElement*>(aFrame->GetContent())->IsMarkable()) {
    // Set marker properties here to avoid reference loops
    RefPtr<SVGReference> markerURL =
        GetMarkerURI(aFrame, &nsStyleSVG::mMarkerStart);
    GetEffectProperty(markerURL, aFrame, MarkerStartProperty());
    markerURL = GetMarkerURI(aFrame, &nsStyleSVG::mMarkerMid);
    GetEffectProperty(markerURL, aFrame, MarkerMidProperty());
    markerURL = GetMarkerURI(aFrame, &nsStyleSVG::mMarkerEnd);
    GetEffectProperty(markerURL, aFrame, MarkerEndProperty());
  }
}

bool SVGObserverUtils::SelfOrAncestorHasRenderingObservers(
    const nsIFrame* aFrame) {
  nsIContent* content = aFrame->GetContent();
  while (content) {
    if (content->HasDirectRenderingObservers()) {
      return true;
    }
    const auto* frame = content->GetPrimaryFrame();
    if (frame && frame->IsSVGRenderingObserverContainer()) {
      break;
    }
    content = content->GetFlattenedTreeParent();
  }
  return false;
}

void SVGObserverUtils::AddRenderingObserver(Element* aElement,
                                            SVGRenderingObserver* aObserver) {
  SVGRenderingObserverSet* observers = GetObserverSet(aElement);
  if (!observers) {
    observers = new SVGRenderingObserverSet();
    // When we call cloneAndAdopt we keep the property. If the referenced
    // element doesn't exist in the new document then the observer set and
    // observers will be removed by ElementTracker::ElementChanged when we
    // get the ChangeNotification.
    aElement->SetProperty(nsGkAtoms::renderingobserverset, observers,
                          nsINode::DeleteProperty<SVGRenderingObserverSet>,
                          /* aTransfer = */ true);
  }
  aElement->SetHasDirectRenderingObservers(true);
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
      aElement->RemoveProperty(nsGkAtoms::renderingobserverset);
      aElement->SetHasDirectRenderingObservers(false);
    }
  }
}

void SVGObserverUtils::RemoveAllRenderingObservers(Element* aElement) {
  SVGRenderingObserverSet* observers = GetObserverSet(aElement);
  if (observers) {
    observers->RemoveAll();
    aElement->RemoveProperty(nsGkAtoms::renderingobserverset);
    aElement->SetHasDirectRenderingObservers(false);
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

  if (aFrame->IsSVGRenderingObserverContainer()) {
    return;
  }

  // Check ancestor SVG containers. The root frame cannot be of type
  // eSVGContainer so we don't have to check f for null here.
  for (nsIFrame* f = aFrame->GetParent(); f->IsSVGContainerFrame();
       f = f->GetParent()) {
    if (auto* element = Element::FromNode(f->GetContent())) {
      if (auto* observers = GetObserverSet(element)) {
        observers->InvalidateAll();
        return;
      }
    }
    if (f->IsSVGRenderingObserverContainer()) {
      return;
    }
  }
}

void SVGObserverUtils::InvalidateDirectRenderingObservers(
    Element* aElement, uint32_t aFlags /* = 0 */) {
  if (!(aFlags & INVALIDATE_DESTROY)) {
    if (nsIFrame* frame = aElement->GetPrimaryFrame()) {
      // If the rendering has changed, the bounds may well have changed too:
      frame->RemoveProperty(SVGUtils::ObjectBoundingBoxProperty());
    }
  }

  if (aElement->HasDirectRenderingObservers()) {
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

}  // namespace mozilla
