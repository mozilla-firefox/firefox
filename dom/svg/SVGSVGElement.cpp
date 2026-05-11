/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGSVGElement.h"

#include "DOMSVGAngle.h"
#include "DOMSVGLength.h"
#include "DOMSVGNumber.h"
#include "DOMSVGPoint.h"
#include "ISVGSVGFrame.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/ISVGDisplayableFrame.h"
#include "mozilla/PresShell.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/SMILTimeContainer.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/DOMMatrix.h"
#include "mozilla/dom/SVGMatrix.h"
#include "mozilla/dom/SVGRect.h"
#include "mozilla/dom/SVGSVGElementBinding.h"
#include "mozilla/dom/SVGViewElement.h"
#include "nsFrameSelection.h"
#include "nsIFrame.h"

NS_IMPL_NS_NEW_SVG_ELEMENT_CHECK_PARSER(SVG)

using namespace mozilla::gfx;

namespace mozilla::dom {

using namespace SVGPreserveAspectRatio_Binding;

SVGEnumMapping SVGSVGElement::sZoomAndPanMap[] = {
    {nsGkAtoms::disable, SVGSVGElement_Binding::SVG_ZOOMANDPAN_DISABLE},
    {nsGkAtoms::magnify, SVGSVGElement_Binding::SVG_ZOOMANDPAN_MAGNIFY},
    {nullptr, 0}};

SVGElement::EnumInfo SVGSVGElement::sEnumInfo[1] = {
    {nsGkAtoms::zoomAndPan, sZoomAndPanMap,
     SVGSVGElement_Binding::SVG_ZOOMANDPAN_MAGNIFY}};

JSObject* SVGSVGElement::WrapNode(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return SVGSVGElement_Binding::Wrap(aCx, this, aGivenProto);
}

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_CYCLE_COLLECTION_CLASS(SVGSVGElement)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SVGSVGElement,
                                                SVGSVGElementBase)
  if (tmp->mTimedDocumentRoot) {
    tmp->mTimedDocumentRoot->Unlink();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SVGSVGElement,
                                                  SVGSVGElementBase)
  if (tmp->mTimedDocumentRoot) {
    tmp->mTimedDocumentRoot->Traverse(&cb);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SVGSVGElement)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(SVGSVGElement)
NS_INTERFACE_MAP_END_INHERITING(SVGSVGElementBase);

NS_IMPL_ADDREF_INHERITED(SVGSVGElement, SVGSVGElementBase)
NS_IMPL_RELEASE_INHERITED(SVGSVGElement, SVGSVGElementBase)

SVGView::SVGView() {
  mZoomAndPan.Init(SVGSVGElement::ZOOMANDPAN,
                   SVGSVGElement_Binding::SVG_ZOOMANDPAN_MAGNIFY);
  mViewBox.Init();
  mPreserveAspectRatio.Init();
}

//----------------------------------------------------------------------
// Implementation

SVGSVGElement::SVGSVGElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
    FromParser aFromParser)
    : SVGSVGElementBase(std::move(aNodeInfo)),
      mStartAnimationOnBindToTree(aFromParser == NOT_FROM_PARSER ||
                                  aFromParser == FROM_PARSER_FRAGMENT ||
                                  aFromParser == FROM_PARSER_XSLT) {}

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT_AND_PARSER(SVGSVGElement)

//----------------------------------------------------------------------
// nsIDOMSVGSVGElement methods:

already_AddRefed<DOMSVGAnimatedLength> SVGSVGElement::X() {
  return mLengthAttributes[ATTR_X].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGSVGElement::Y() {
  return mLengthAttributes[ATTR_Y].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGSVGElement::Width() {
  return mLengthAttributes[ATTR_WIDTH].ToDOMAnimatedLength(this);
}

already_AddRefed<DOMSVGAnimatedLength> SVGSVGElement::Height() {
  return mLengthAttributes[ATTR_HEIGHT].ToDOMAnimatedLength(this);
}

bool SVGSVGElement::UseCurrentView() const {
  return mSVGView || !mCurrentViewID.IsVoid();
}

SVGAnimatedTransformList* SVGSVGElement::GetViewTransformList() const {
  if (mSVGView && mSVGView->mTransforms) {
    return mSVGView->mTransforms.get();
  }
  return nullptr;
}

float SVGSVGElement::CurrentScale() const { return mCurrentScale; }

#define CURRENT_SCALE_MAX 16.0f
#define CURRENT_SCALE_MIN 0.0625f

void SVGSVGElement::SetCurrentScale(float aCurrentScale) {
  // Prevent bizarre behaviour and maxing out of CPU and memory by clamping
  aCurrentScale =
      std::clamp(aCurrentScale, CURRENT_SCALE_MIN, CURRENT_SCALE_MAX);

  if (aCurrentScale == mCurrentScale) {
    return;
  }
  mCurrentScale = aCurrentScale;

  if (IsRootSVGSVGElement()) {
    InvalidateTransformNotifyFrame();
  }
}

already_AddRefed<DOMSVGPoint> SVGSVGElement::CurrentTranslate() {
  return DOMSVGPoint::GetTranslateTearOff(&mCurrentTranslate, this);
}

uint32_t SVGSVGElement::SuspendRedraw(uint32_t max_wait_milliseconds) {
  // suspendRedraw is a no-op in Mozilla, so it doesn't matter what
  // we return
  return 1;
}

void SVGSVGElement::UnsuspendRedraw(uint32_t suspend_handle_id) {
  // no-op
}

void SVGSVGElement::UnsuspendRedrawAll() {
  // no-op
}

void SVGSVGElement::ForceRedraw() {
  // no-op
}

void SVGSVGElement::PauseAnimations() {
  if (mTimedDocumentRoot) {
    mTimedDocumentRoot->Pause(SMILTimeContainer::PauseType::Script);
  }
  // else we're not the outermost <svg> or not bound to a tree, so silently fail
}

static SMILTime SecondsToSMILTime(float aSeconds) {
  double milliseconds = double(aSeconds) * PR_MSEC_PER_SEC;
  // Round to nearest whole number before converting, to avoid precision
  // errors
  return SVGUtils::ClampToInt64(NS_round(milliseconds));
}

void SVGSVGElement::PauseAnimationsAt(float aSeconds) {
  if (mTimedDocumentRoot) {
    mTimedDocumentRoot->PauseAt(SecondsToSMILTime(aSeconds));
  }
  // else we're not the outermost <svg> or not bound to a tree, so silently fail
}

void SVGSVGElement::UnpauseAnimations() {
  if (mTimedDocumentRoot) {
    mTimedDocumentRoot->Resume(SMILTimeContainer::PauseType::Script);
  }
  // else we're not the outermost <svg> or not bound to a tree, so silently fail
}

bool SVGSVGElement::AnimationsPaused() {
  SMILTimeContainer* root = GetTimedDocumentRoot();
  return root && root->IsPausedByType(SMILTimeContainer::PauseType::Script);
}

float SVGSVGElement::GetCurrentTimeAsFloat() {
  SMILTimeContainer* root = GetTimedDocumentRoot();
  if (root) {
    double fCurrentTimeMs = double(root->GetCurrentTimeAsSMILTime());
    return (float)(fCurrentTimeMs / PR_MSEC_PER_SEC);
  }
  return 0.f;
}

void SVGSVGElement::SetCurrentTime(float seconds) {
  if (!mTimedDocumentRoot) {
    // we're not the outermost <svg> or not bound to a tree, so silently fail
    return;
  }
  // Make sure the timegraph is up-to-date
  if (auto* currentDoc = GetComposedDoc()) {
    currentDoc->FlushPendingNotifications(FlushType::Style);
  }
  if (!mTimedDocumentRoot) {
    return;
  }
  FlushAnimations();
  mTimedDocumentRoot->SetCurrentTime(SecondsToSMILTime(seconds));
  AnimationNeedsResample();
  // Trigger synchronous sample now, to:
  //  - Make sure we get an up-to-date paint after this method
  //  - re-enable event firing (it got disabled during seeking, and it
  //  doesn't get re-enabled until the first sample after the seek -- so
  //  let's make that happen now.)
  FlushAnimations();
}

void SVGSVGElement::DeselectAll() {
  if (Document* doc = GetComposedDoc()) {
    if (RefPtr<PresShell> presShell = doc->GetPresShell()) {
      if (RefPtr<Selection> docSel =
              presShell->GetCurrentSelection(SelectionType::eNormal)) {
        docSel->RemoveAllRanges(IgnoreErrors());
      }
    }
  }
}

already_AddRefed<DOMSVGNumber> SVGSVGElement::CreateSVGNumber() {
  return MakeAndAddRef<DOMSVGNumber>(this);
}

already_AddRefed<DOMSVGLength> SVGSVGElement::CreateSVGLength() {
  return MakeAndAddRef<DOMSVGLength>();
}

already_AddRefed<DOMSVGAngle> SVGSVGElement::CreateSVGAngle() {
  return MakeAndAddRef<DOMSVGAngle>(this);
}

already_AddRefed<DOMSVGPoint> SVGSVGElement::CreateSVGPoint() {
  return MakeAndAddRef<DOMSVGPoint>(Point(0, 0));
}

already_AddRefed<SVGMatrix> SVGSVGElement::CreateSVGMatrix() {
  return MakeAndAddRef<SVGMatrix>();
}

already_AddRefed<SVGRect> SVGSVGElement::CreateSVGRect() {
  return MakeAndAddRef<SVGRect>(this);
}

already_AddRefed<DOMSVGTransform> SVGSVGElement::CreateSVGTransform() {
  return MakeAndAddRef<DOMSVGTransform>();
}

already_AddRefed<DOMSVGTransform> SVGSVGElement::CreateSVGTransformFromMatrix(
    const DOMMatrix2DInit& matrix, ErrorResult& rv) {
  return MakeAndAddRef<DOMSVGTransform>(matrix, rv);
}

void SVGSVGElement::DidChangeTranslate() {
  if (Document* doc = GetUncomposedDoc()) {
    RefPtr<PresShell> presShell = doc->GetPresShell();
    // now dispatch the appropriate event if we are the root element
    if (presShell && IsRootSVGSVGElement()) {
      nsEventStatus status = nsEventStatus_eIgnore;
      WidgetEvent svgScrollEvent(true, eSVGScroll);
      presShell->HandleDOMEventWithTarget(this, &svgScrollEvent, &status);
      InvalidateTransformNotifyFrame();
    }
  }
}

//----------------------------------------------------------------------
// SVGZoomAndPanValues
uint16_t SVGSVGElement::ZoomAndPan() const {
  return mEnumAttributes[ZOOMANDPAN].GetAnimValue();
}

void SVGSVGElement::SetZoomAndPan(uint16_t aZoomAndPan, ErrorResult& rv) {
  if (aZoomAndPan == SVGSVGElement_Binding::SVG_ZOOMANDPAN_DISABLE ||
      aZoomAndPan == SVGSVGElement_Binding::SVG_ZOOMANDPAN_MAGNIFY) {
    ErrorResult nestedRv;
    mEnumAttributes[ZOOMANDPAN].SetBaseValue(aZoomAndPan, this, nestedRv);
    MOZ_ASSERT(!nestedRv.Failed(),
               "We already validated our aZoomAndPan value!");
    return;
  }

  rv.ThrowRangeError<MSG_INVALID_ZOOMANDPAN_VALUE_ERROR>();
}

//----------------------------------------------------------------------
SMILTimeContainer* SVGSVGElement::GetTimedDocumentRoot() {
  if (mTimedDocumentRoot) {
    return mTimedDocumentRoot.get();
  }

  // We must not be the outermost <svg> element, try to find it
  SVGSVGElement* outerSVGElement = SVGContentUtils::GetOuterSVGElement(this);

  if (outerSVGElement) {
    return outerSVGElement->GetTimedDocumentRoot();
  }
  // invalid structure
  return nullptr;
}
//----------------------------------------------------------------------
// SVGElement
nsresult SVGSVGElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  SMILAnimationController* smilController = nullptr;

  if (Document* doc = aContext.GetComposedDoc()) {
    if ((smilController = doc->GetAnimationController())) {
      // SMIL is enabled in this document
      if (WillBeOutermostSVG(aParent)) {
        // We'll be the outermost <svg> element.  We'll need a time container.
        if (!mTimedDocumentRoot) {
          mTimedDocumentRoot = std::make_unique<SMILTimeContainer>();
        }
      } else {
        // We're a child of some other <svg> element, so we don't need our own
        // time container. However, we need to make sure that we'll get a
        // kick-start if we get promoted to be outermost later on.
        mTimedDocumentRoot = nullptr;
        mStartAnimationOnBindToTree = true;
      }
    }
  }

  nsresult rv = SVGGraphicsElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mTimedDocumentRoot && smilController) {
    rv = mTimedDocumentRoot->SetParent(smilController);
    if (mStartAnimationOnBindToTree) {
      mTimedDocumentRoot->Begin();
      mStartAnimationOnBindToTree = false;
    }
  }

  return rv;
}

void SVGSVGElement::UnbindFromTree(UnbindContext& aContext) {
  if (mTimedDocumentRoot) {
    mTimedDocumentRoot->SetParent(nullptr);
  }

  SVGGraphicsElement::UnbindFromTree(aContext);
}

void SVGSVGElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  if (aVisitor.mEvent->mMessage == eSVGLoad) {
    if (mTimedDocumentRoot) {
      mTimedDocumentRoot->Begin();
      // Set 'resample needed' flag, so that if any script calls a DOM method
      // that requires up-to-date animations before our first sample callback,
      // we'll force a synchronous sample.
      AnimationNeedsResample();
    }
  }
  SVGSVGElementBase::GetEventTargetParent(aVisitor);
}

bool SVGSVGElement::IsEventAttributeNameInternal(nsAtom* aName) {
  /* The events in EventNameType_SVGSVG are for events that are only
     applicable to outermost 'svg' elements. We don't check if we're an outer
     'svg' element in case we're not inserted into the document yet, but since
     the target of the events in question will always be the outermost 'svg'
     element, this shouldn't cause any real problems.
  */
  return nsContentUtils::IsEventAttributeName(
      aName, (EventNameType_SVGGraphic | EventNameType_SVGSVG));
}

LengthPercentage SVGSVGElement::GetIntrinsicWidthOrHeight(int aAttr) {
  MOZ_ASSERT(aAttr == ATTR_WIDTH || aAttr == ATTR_HEIGHT);

  int otherAttr = (aAttr == ATTR_HEIGHT) ? ATTR_WIDTH : ATTR_HEIGHT;

  if (!mLengthAttributes[aAttr].IsExplicitlySet() &&
      mLengthAttributes[otherAttr].IsExplicitlySet()) {
    const auto& viewBox = GetViewBoxInternal();
    if (viewBox.HasRect()) {
      auto aspectRatio = AspectRatio::FromSize(viewBox.GetAnimValue().width,
                                               viewBox.GetAnimValue().height);
      if (aAttr == ATTR_HEIGHT && aspectRatio) {
        aspectRatio = aspectRatio.Inverted();
      }
      if (aspectRatio) {
        if (mLengthAttributes[otherAttr].IsPercentage()) {
          float rawSize = aspectRatio.ApplyToFloat(
              mLengthAttributes[otherAttr].GetAnimValInSpecifiedUnits());
          return LengthPercentage::FromPercentage(rawSize);
        }

        float rawSize = aspectRatio.ApplyToFloat(
            mLengthAttributes[otherAttr].GetAnimValueWithZoom(this));
        return LengthPercentage::FromPixels(rawSize);
      }
    }
  }

  if (mLengthAttributes[aAttr].IsPercentage()) {
    float rawSize = mLengthAttributes[aAttr].GetAnimValInSpecifiedUnits();
    return LengthPercentage::FromPercentage(rawSize);
  }

  // Passing |this| as a SVGViewportElement* invokes the variant of GetAnimValue
  // that uses the passed argument as the context, but that's fine since we
  // know the length isn't a percentage so the context won't be used (and we
  // need to pass the element to be able to resolve em/ex units).
  float rawSize = mLengthAttributes[aAttr].GetAnimValueWithZoom(this);
  return LengthPercentage::FromPixels(rawSize);
}

//----------------------------------------------------------------------
// public helpers:

LengthPercentage SVGSVGElement::GetIntrinsicWidth() {
  return GetIntrinsicWidthOrHeight(ATTR_WIDTH);
}

LengthPercentage SVGSVGElement::GetIntrinsicHeight() {
  return GetIntrinsicWidthOrHeight(ATTR_HEIGHT);
}

void SVGSVGElement::FlushImageTransformInvalidation() {
  MOZ_ASSERT(!GetParent(), "Should only be called on root node");
  MOZ_ASSERT(OwnerDoc()->IsBeingUsedAsImage(),
             "Should only be called on image documents");

  if (mImageNeedsTransformInvalidation) {
    InvalidateTransformNotifyFrame();
    mImageNeedsTransformInvalidation = false;
  }
}

//----------------------------------------------------------------------
// implementation helpers

bool SVGSVGElement::WillBeOutermostSVG(nsINode& aParent) const {
  nsINode* parent = &aParent;
  while (parent && parent->IsSVGElement()) {
    if (parent->IsSVGElement(nsGkAtoms::foreignObject)) {
      // SVG in a foreignObject must have its own <svg> (SVGOuterSVGFrame).
      return false;
    }
    if (parent->IsSVGElement(nsGkAtoms::svg)) {
      return false;
    }
    parent = parent->GetParentOrShadowHostNode();
  }

  return true;
}

void SVGSVGElement::DidChangeSVGView() {
  InvalidateTransformNotifyFrame();
  // We map the SVGView transform as the transform css property, so need to
  // schedule attribute mapping.
  if (!IsPendingMappedAttributeEvaluation() &&
      mAttrs.MarkAsPendingPresAttributeEvaluation()) {
    OwnerDoc()->ScheduleForPresAttrEvaluation(this);
  }
}

void SVGSVGElement::InvalidateTransformNotifyFrame() {
  // might fail this check if we've failed conditional processing
  if (ISVGSVGFrame* svgframe = do_QueryFrame(GetPrimaryFrame())) {
    svgframe->NotifyViewportOrTransformChanged(
        ISVGDisplayableFrame::ChangeFlag::TransformChanged);
  }
}

SVGElement::EnumAttributesInfo SVGSVGElement::GetEnumInfo() {
  return EnumAttributesInfo(mEnumAttributes, sEnumInfo, std::size(sEnumInfo));
}

void SVGSVGElement::SetImageOverridePreserveAspectRatio(
    const SVGPreserveAspectRatio& aPAR) {
  MOZ_ASSERT(OwnerDoc()->IsBeingUsedAsImage(),
             "should only override preserveAspectRatio in images");

  bool hasViewBox = HasViewBox();
  if (!hasViewBox && ShouldSynthesizeViewBox()) {
    // My non-<svg:image> clients will have been painting me with a synthesized
    // viewBox, but my <svg:image> client that's about to paint me now does NOT
    // want that.  Need to tell ourselves to flush our transform.
    mImageNeedsTransformInvalidation = true;
  }

  if (!hasViewBox) {
    return;  // preserveAspectRatio irrelevant (only matters if we have viewBox)
  }

  if (SetPreserveAspectRatioProperty(aPAR)) {
    mImageNeedsTransformInvalidation = true;
  }
}

void SVGSVGElement::ClearImageOverridePreserveAspectRatio() {
  MOZ_ASSERT(OwnerDoc()->IsBeingUsedAsImage(),
             "should only override image preserveAspectRatio in images");

  if (!HasViewBox() && ShouldSynthesizeViewBox()) {
    // My non-<svg:image> clients will want to paint me with a synthesized
    // viewBox, but my <svg:image> client that just painted me did NOT
    // use that.  Need to tell ourselves to flush our transform.
    mImageNeedsTransformInvalidation = true;
  }

  if (ClearPreserveAspectRatioProperty()) {
    mImageNeedsTransformInvalidation = true;
  }
}

bool SVGSVGElement::SetPreserveAspectRatioProperty(
    const SVGPreserveAspectRatio& aPAR) {
  SVGPreserveAspectRatio* pAROverridePtr = new SVGPreserveAspectRatio(aPAR);
  nsresult rv =
      SetProperty(nsGkAtoms::overridePreserveAspectRatio, pAROverridePtr,
                  nsINode::DeleteProperty<SVGPreserveAspectRatio>, true);
  MOZ_ASSERT(rv != NS_PROPTABLE_PROP_OVERWRITTEN,
             "Setting override value when it's already set...?");

  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    // property-insertion failed (e.g. OOM in property-table code)
    delete pAROverridePtr;
    return false;
  }
  return true;
}

const SVGPreserveAspectRatio* SVGSVGElement::GetPreserveAspectRatioProperty()
    const {
  void* valPtr = GetProperty(nsGkAtoms::overridePreserveAspectRatio);
  if (valPtr) {
    return static_cast<SVGPreserveAspectRatio*>(valPtr);
  }
  return nullptr;
}

bool SVGSVGElement::ClearPreserveAspectRatioProperty() {
  void* valPtr = TakeProperty(nsGkAtoms::overridePreserveAspectRatio);
  bool didHaveProperty = !!valPtr;
  delete static_cast<SVGPreserveAspectRatio*>(valPtr);
  return didHaveProperty;
}

SVGPreserveAspectRatio SVGSVGElement::GetPreserveAspectRatioWithOverride()
    const {
  Document* doc = GetUncomposedDoc();
  if (doc && doc->IsBeingUsedAsImage()) {
    const SVGPreserveAspectRatio* pAROverridePtr =
        GetPreserveAspectRatioProperty();
    if (pAROverridePtr) {
      return *pAROverridePtr;
    }
  }

  SVGViewElement* viewElement = GetCurrentViewElement();

  // This check is equivalent to "!HasViewBox() &&
  // ShouldSynthesizeViewBox()". We're just holding onto the viewElement that
  // HasViewBox() would look up, so that we don't have to look it up again
  // later.
  if (!((viewElement && viewElement->mViewBox.HasRect()) ||
        (mSVGView && mSVGView->mViewBox.HasRect()) || mViewBox.HasRect()) &&
      ShouldSynthesizeViewBox()) {
    // If we're synthesizing a viewBox, use preserveAspectRatio="none";
    return SVGPreserveAspectRatio(SVG_PRESERVEASPECTRATIO_NONE,
                                  SVG_MEETORSLICE_SLICE);
  }

  if (viewElement && viewElement->mPreserveAspectRatio.IsExplicitlySet()) {
    return viewElement->mPreserveAspectRatio.GetAnimValue();
  }
  if (mSVGView && mSVGView->mPreserveAspectRatio.IsExplicitlySet()) {
    return mSVGView->mPreserveAspectRatio.GetAnimValue();
  }
  return mPreserveAspectRatio.GetAnimValue();
}

SVGViewElement* SVGSVGElement::GetCurrentViewElement() const {
  if (!mCurrentViewID.IsVoid()) {
    // XXXsmaug It is unclear how this should work in case we're in Shadow DOM.
    Document* doc = GetUncomposedDoc();
    if (doc) {
      Element* element = doc->GetElementById(mCurrentViewID);
      return SVGViewElement::FromNodeOrNull(element);
    }
  }
  return nullptr;
}

const SVGAnimatedViewBox& SVGSVGElement::GetViewBoxInternal() const {
  SVGViewElement* viewElement = GetCurrentViewElement();

  if (viewElement && viewElement->mViewBox.HasRect()) {
    return viewElement->mViewBox;
  }
  if (mSVGView && mSVGView->mViewBox.HasRect()) {
    return mSVGView->mViewBox;
  }

  return mViewBox;
}

}  // namespace mozilla::dom
