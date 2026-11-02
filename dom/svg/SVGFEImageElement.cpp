/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGFEImageElement.h"

#include "imgIContainer.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/SVGFEImageElementBinding.h"
#include "mozilla/dom/SVGFilterElement.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/gfx/2D.h"
#include "nsContentUtils.h"
#include "nsIURIWithSizeOf.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(FEImage)

using namespace mozilla::gfx;

namespace mozilla::dom {

JSObject* SVGFEImageElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return SVGFEImageElement_Binding::Wrap(aCx, this, aGivenProto);
}

SVGElement::StringInfo SVGFEImageElement::sStringInfo[3] = {
    {nsGkAtoms::result, kNameSpaceID_None, true},
    {nsGkAtoms::href, kNameSpaceID_None, true},
    {nsGkAtoms::href, kNameSpaceID_XLink, true}};

// Cycle collection magic -- based on SVGUseElement
NS_IMPL_CYCLE_COLLECTION_CLASS(SVGFEImageElement)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(SVGFEImageElement,
                                                SVGFEImageElementBase)
  tmp->mImageContentObserver = nullptr;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(SVGFEImageElement,
                                                  SVGFEImageElementBase)
  SVGObserverUtils::TraverseFEImageObserver(tmp, &cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(SVGFEImageElement,
                                             SVGFEImageElementBase,
                                             imgINotificationObserver,
                                             nsIImageLoadingContent)

//----------------------------------------------------------------------
// Implementation

SVGFEImageElement::SVGFEImageElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : SVGFEImageElementBase(std::move(aNodeInfo)) {
  // We start out broken
  AddStatesSilently(ElementState::BROKEN);
}

SVGFEImageElement::~SVGFEImageElement() { nsImageLoadingContent::Destroy(); }

//----------------------------------------------------------------------

void SVGFEImageElement::UpdateSrcURI() {
  nsAutoString href;
  HrefAsString(href);

  mImageContentObserver = nullptr;
  mSrcURI = nullptr;
  if (!href.IsEmpty()) {
    StringToURI(href, OwnerDoc(), getter_AddRefs(mSrcURI));
  }
}

void SVGFEImageElement::LoadSelectedImage(bool aAlwaysLoad,
                                          bool aStopLazyLoading) {
  // Make sure we don't get in a recursive death-spiral
  bool isEqual;
  if (mSrcURI && NS_SUCCEEDED(mSrcURI->Equals(GetBaseURI(), &isEqual)) &&
      isEqual) {
    // Image URI matches our URI exactly! Bail out.
    return;
  }

  const bool kNotify = true;

  if (SVGObserverUtils::GetAndObserveFEImageContent(this)) {
    // We have a local target, don't try to load an image.
    CancelImageRequests(kNotify);
    return;
  }

  nsresult rv = NS_ERROR_FAILURE;
  if (mSrcURI || (mStringAttributes[HREF].IsExplicitlySet() ||
                  mStringAttributes[XLINK_HREF].IsExplicitlySet())) {
    rv = LoadImage(mSrcURI, /* aForce = */ true, kNotify, eImageLoadType_Normal,
                   LoadFlags(), OwnerDoc());
  }

  if (NS_FAILED(rv)) {
    CancelImageRequests(kNotify);
  }
}

//----------------------------------------------------------------------
// EventTarget methods:

void SVGFEImageElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  nsImageLoadingContent::AsyncEventRunning(aEvent);
}

//----------------------------------------------------------------------
// nsIContent methods:

bool SVGFEImageElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                       const nsAString& aValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::crossorigin) {
      ParseCORSValue(aValue, aResult);
      return true;
    }
    if (aAttribute == nsGkAtoms::fetchpriority) {
      ParseFetchPriority(aValue, aResult);
      return true;
    }
    if (aAttribute == nsGkAtoms::referrerpolicy) {
      return ParseReferrerAttribute(aValue, aResult);
    }
  }
  return SVGFEImageElementBase::ParseAttribute(
      aNamespaceID, aAttribute, aValue, aMaybeScriptedPrincipal, aResult);
}

void SVGFEImageElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aSubjectPrincipal,
                                     bool aNotify) {
  bool forceReload = false;
  if (aName == nsGkAtoms::href && (aNamespaceID == kNameSpaceID_XLink ||
                                   aNamespaceID == kNameSpaceID_None)) {
    if (aNamespaceID == kNameSpaceID_XLink &&
        mStringAttributes[HREF].IsExplicitlySet()) {
      // href overrides xlink:href
      return;
    }
    UpdateSrcURI();
    forceReload = true;
  } else if (aNamespaceID == kNameSpaceID_None &&
             aName == nsGkAtoms::crossorigin) {
    forceReload = GetCORSMode() != AttrValueToCORSMode(aOldValue);
  }

  if (forceReload) {
    QueueImageTask(mSrcURI, /* aAlwaysLoad = */ true, aNotify);
  }

  return SVGFEImageElementBase::AfterSetAttr(
      aNamespaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

nsresult SVGFEImageElement::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  nsresult rv = SVGFEImageElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  nsImageLoadingContent::BindToTree(aContext, aParent);

  if (aContext.InComposedDoc()) {
    aContext.OwnerDoc().SetUseCounter(eUseCounter_custom_feImage);
  }

  return rv;
}

void SVGFEImageElement::UnbindFromTree(UnbindContext& aContext) {
  mImageContentObserver = nullptr;
  nsImageLoadingContent::UnbindFromTree();
  SVGFEImageElementBase::UnbindFromTree(aContext);
}

void SVGFEImageElement::DestroyContent() {
  ClearImageLoadTask();

  nsImageLoadingContent::Destroy();
  SVGFEImageElementBase::DestroyContent();
}

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGFEImageElement)

void SVGFEImageElement::NodeInfoChanged(Document* aOldDoc) {
  SVGFEImageElementBase::NodeInfoChanged(aOldDoc);

  // Reparse the URI if needed. Note that we can't check whether we already have
  // a parsed URI, because it might be null even if we have a valid href
  // attribute, if we tried to parse with a different base.
  UpdateSrcURI();

  QueueImageTask(mSrcURI, /* aAlwaysLoad = */ true, /* aNotify = */ false);
}

//----------------------------------------------------------------------
//  nsImageLoadingContent methods:

CORSMode SVGFEImageElement::GetCORSMode() {
  return AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));
}

void SVGFEImageElement::GetFetchPriority(nsAString& aFetchPriority) const {
  GetEnumAttr(nsGkAtoms::fetchpriority, kFetchPriorityAttributeValueAuto,
              aFetchPriority);
}

//----------------------------------------------------------------------
// nsIDOMSVGFEImageElement methods

FilterPrimitiveDescription SVGFEImageElement::GetPrimitiveDescription(
    SVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
    const nsTArray<bool>& aInputsAreTainted,
    nsTArray<RefPtr<SourceSurface>>& aInputImages) {
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return FilterPrimitiveDescription();
  }

  nsCOMPtr<imgIRequest> currentRequest;
  GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
             getter_AddRefs(currentRequest));

  nsCOMPtr<imgIContainer> imageContainer;
  if (currentRequest) {
    currentRequest->GetImage(getter_AddRefs(imageContainer));
  }

  RefPtr<SourceSurface> image;
  nsIntSize nativeSize;
  if (imageContainer) {
    CSSIntSize size = NaturalSize(DoDensityCorrection::No);
    nativeSize.width = size.width;
    nativeSize.height = size.height;
    uint32_t flags =
        imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY;
    image = imageContainer->GetFrameAtSize(nativeSize,
                                           imgIContainer::FRAME_CURRENT, flags);
  }

  if (!image) {
    return FilterPrimitiveDescription();
  }

  Matrix viewBoxTM = SVGContentUtils::GetViewBoxTransform(
      aFilterSubregion.width, aFilterSubregion.height, 0, 0, nativeSize.width,
      nativeSize.height, mPreserveAspectRatio);
  Matrix TM = viewBoxTM;
  TM.PostTranslate(aFilterSubregion.x, aFilterSubregion.y);

  SamplingFilter samplingFilter =
      nsLayoutUtils::GetSamplingFilterForFrame(frame);

  ImageAttributes atts;
  atts.mFilter = (uint32_t)samplingFilter;
  atts.mTransform = TM;

  // Append the image to aInputImages and store its index in the description.
  size_t imageIndex = aInputImages.Length();
  aInputImages.AppendElement(image);
  atts.mInputIndex = (uint32_t)imageIndex;
  return FilterPrimitiveDescription(AsVariant(std::move(atts)));
}

bool SVGFEImageElement::AttributeAffectsRendering(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute) const {
  // nsGkAtoms::href is deliberately omitted as the frame has special
  // handling to load the image
  return SVGFEImageElementBase::AttributeAffectsRendering(aNameSpaceID,
                                                          aAttribute) ||
         (aNameSpaceID == kNameSpaceID_None &&
          aAttribute == nsGkAtoms::preserveAspectRatio);
}

bool SVGFEImageElement::OutputIsTainted(const nsTArray<bool>& aInputsAreTainted,
                                        nsIPrincipal* aReferencePrincipal) {
  nsresult rv;
  nsCOMPtr<imgIRequest> currentRequest;
  GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
             getter_AddRefs(currentRequest));

  if (!currentRequest) {
    return false;
  }

  nsCOMPtr<nsIPrincipal> principal;
  rv = currentRequest->GetImagePrincipal(getter_AddRefs(principal));
  if (NS_FAILED(rv) || !principal) {
    return true;
  }

  // If CORS was used to load the image, the page is allowed to read from it.
  if (nsLayoutUtils::ImageRequestUsesCORS(currentRequest)) {
    return false;
  }

  if (aReferencePrincipal->Subsumes(principal)) {
    // The page is allowed to read from the image.
    return false;
  }

  return true;
}

//----------------------------------------------------------------------
// SVGElement methods

already_AddRefed<DOMSVGAnimatedString> SVGFEImageElement::Href() {
  return mStringAttributes[HREF].IsExplicitlySet() ||
                 !mStringAttributes[XLINK_HREF].IsExplicitlySet()
             ? mStringAttributes[HREF].ToDOMAnimatedString(this)
             : mStringAttributes[XLINK_HREF].ToDOMAnimatedString(this);
}

already_AddRefed<DOMSVGAnimatedPreserveAspectRatio>
SVGFEImageElement::PreserveAspectRatio() {
  return mPreserveAspectRatio.ToDOMAnimatedPreserveAspectRatio(this);
}

SVGAnimatedPreserveAspectRatio*
SVGFEImageElement::GetAnimatedPreserveAspectRatio() {
  return &mPreserveAspectRatio;
}

SVGElement::StringAttributesInfo SVGFEImageElement::GetStringInfo() {
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              std::size(sStringInfo));
}

//----------------------------------------------------------------------
// nsIImageLoadingContent methods
NS_IMETHODIMP_(void)
SVGFEImageElement::FrameCreated(nsIFrame* aFrame) {
  nsImageLoadingContent::FrameCreated(aFrame);

  auto mode = aFrame->PresContext()->ImageAnimationMode();
  if (mode == mImageAnimationMode) {
    return;
  }

  mImageAnimationMode = mode;

  if (!mImageLoadData) {
    return;
  }

  if (mImageLoadData->mPendingRequest) {
    nsCOMPtr<imgIContainer> container;
    mImageLoadData->mPendingRequest->GetImage(getter_AddRefs(container));
    if (container) {
      container->SetAnimationMode(mode);
    }
  }

  if (mImageLoadData->mCurrentRequest) {
    nsCOMPtr<imgIContainer> container;
    mImageLoadData->mCurrentRequest->GetImage(getter_AddRefs(container));
    if (container) {
      container->SetAnimationMode(mode);
    }
  }
}

//----------------------------------------------------------------------
// imgINotificationObserver methods

void SVGFEImageElement::Notify(imgIRequest* aRequest, int32_t aType,
                               const nsIntRect* aData) {
  nsImageLoadingContent::Notify(aRequest, aType, aData);

  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    // Request a decode
    nsCOMPtr<imgIContainer> container;
    aRequest->GetImage(getter_AddRefs(container));
    MOZ_ASSERT(container, "who sent the notification then?");
    container->StartDecoding(imgIContainer::FLAG_NONE);
    container->SetAnimationMode(mImageAnimationMode);
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE ||
      aType == imgINotificationObserver::FRAME_UPDATE ||
      aType == imgINotificationObserver::SIZE_AVAILABLE) {
    if (auto* filter = SVGFilterElement::FromNodeOrNull(GetParent())) {
      SVGObserverUtils::InvalidateDirectRenderingObservers(filter);
    }
  }
}

void SVGFEImageElement::DidAnimateAttribute(int32_t aNameSpaceID,
                                            nsAtom* aAttribute) {
  if ((aNameSpaceID == kNameSpaceID_None ||
       aNameSpaceID == kNameSpaceID_XLink) &&
      aAttribute == nsGkAtoms::href) {
    UpdateSrcURI();
    QueueImageTask(mSrcURI, /* aAlwaysLoad = */ true, /* aNotify */ true);
  }
  SVGFEImageElementBase::DidAnimateAttribute(aNameSpaceID, aAttribute);
}

//----------------------------------------------------------------------
// Public helper methods

void SVGFEImageElement::HrefAsString(nsAString& aHref) {
  if (mStringAttributes[HREF].IsExplicitlySet()) {
    mStringAttributes[HREF].GetBaseValue(aHref, this);
  } else {
    mStringAttributes[XLINK_HREF].GetBaseValue(aHref, this);
  }
}

void SVGFEImageElement::NotifyImageContentChanged() {
  // We don't support rendering fragments yet (bug 455986)
}

void SVGFEImageElement::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                               size_t* aNodeSize) const {
  SVGElement::AddSizeOfExcludingThis(aSizes, aNodeSize);

  // It is okay to include the size of mSrcURI here even though it might have
  // strong references from elsewhere because the URI was created for this
  // object, in nsImageLoadingContent::StringToURI(). Only objects that created
  // their own URI will call nsIURIWithSizeOf::SizeOfIncludingThis().
  if (mSrcURI) {
    *aNodeSize += SizeOfIncludingThisIfURIWithSizeOf(
        mSrcURI, aSizes.mState.mMallocSizeOf);
  }
}

}  // namespace mozilla::dom
