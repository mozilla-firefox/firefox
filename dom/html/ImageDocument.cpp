/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageDocument.h"

#include <algorithm>

#include "DocumentInlines.h"
#include "ImageBlocker.h"
#include "imgIContainer.h"
#include "imgINotificationObserver.h"
#include "imgIRequest.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/ImageDocumentBinding.h"
#include "mozilla/dom/MouseEvent.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDOMCSSDeclaration.h"
#include "nsDOMTokenList.h"
#include "nsDocShell.h"
#include "nsError.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIChannel.h"
#include "nsIContentPolicy.h"
#include "nsIDOMEventListener.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsIFrame.h"
#include "nsIImageLoadingContent.h"
#include "nsObjectLoadingContent.h"
#include "nsPIDOMWindow.h"
#include "nsPresContext.h"
#include "nsRect.h"
#include "nsThreadUtils.h"
#include "nsURILoader.h"

namespace mozilla::dom {

class ImageListener : public MediaDocumentStreamListener {
 public:
  // NS_DECL_NSIREQUESTOBSERVER
  // We only implement OnStartRequest; OnStopRequest is
  // implemented by MediaDocumentStreamListener
  NS_IMETHOD OnStartRequest(nsIRequest* aRequest) override;

  explicit ImageListener(ImageDocument* aDocument);
  virtual ~ImageListener();
};

ImageListener::ImageListener(ImageDocument* aDocument)
    : MediaDocumentStreamListener(aDocument) {}

ImageListener::~ImageListener() = default;

NS_IMETHODIMP
ImageListener::OnStartRequest(nsIRequest* request) {
  RefPtr<ImageDocument> imgDoc = static_cast<ImageDocument*>(mDocument.get());
  NS_ENSURE_TRUE(imgDoc, NS_ERROR_FAILURE);

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  if (!channel) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsPIDOMWindowOuter> domWindow = imgDoc->GetWindow();
  NS_ENSURE_TRUE(domWindow, NS_ERROR_UNEXPECTED);

  // This is an image being loaded as a document, so it's not going to be
  // detected by the ImageBlocker. However we don't want to call
  // NS_CheckContentLoadPolicy (with an TYPE_INTERNAL_IMAGE) here, as it would
  // e.g. make this image load be detectable by CSP.
  nsCOMPtr<nsIURI> channelURI;
  channel->GetURI(getter_AddRefs(channelURI));
  if (image::ImageBlocker::ShouldBlock(channelURI)) {
    request->Cancel(NS_ERROR_CONTENT_BLOCKED);
    return NS_OK;
  }

  if (!imgDoc->mObservingImageLoader) {
    NS_ENSURE_TRUE(imgDoc->mImageContent, NS_ERROR_UNEXPECTED);
    imgDoc->mObservingImageLoader = true;
    imgDoc->mImageContent->AddNativeObserver(imgDoc);
    // Image status that may have gotten replayed, in AddNativeObserver may run
    // script and destroy the document.
    NS_ENSURE_TRUE(mDocument, NS_ERROR_FAILURE);
    NS_ENSURE_TRUE(imgDoc->mImageContent, NS_ERROR_UNEXPECTED);
    // If script adopted the image into another document, its adopting steps
    // queued its own load there, so the channel isn't needed.
    if (imgDoc->mImageContent->OwnerDoc() == imgDoc) {
      imgDoc->mImageContent->LoadImageWithChannel(channel,
                                                  getter_AddRefs(mNextStream));
    }
  }

  return MediaDocumentStreamListener::OnStartRequest(request);
}

ImageDocument::ImageDocument()
    : mVisibleWidth(0.0),
      mVisibleHeight(0.0),
      mImageWidth(0),
      mImageHeight(0),
      mImageIsResized(false),
      mShouldResize(false),
      mFirstResize(false),
      mObservingImageLoader(false),
      mTitleUpdateInProgress(false),
      mHasCustomTitle(false),
      mIsInObjectOrEmbed(false),
      mOriginalZoomLevel(1.0),
      mOriginalResolution(1.0) {}

ImageDocument::~ImageDocument() = default;

NS_IMPL_CYCLE_COLLECTION_INHERITED(ImageDocument, MediaDocument, mImageContent)

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(ImageDocument, MediaDocument,
                                             imgINotificationObserver,
                                             nsIDOMEventListener)

nsresult ImageDocument::Init(nsIPrincipal* aPrincipal,
                             nsIPrincipal* aPartitionedPrincipal) {
  nsresult rv = MediaDocument::Init(aPrincipal, aPartitionedPrincipal);
  NS_ENSURE_SUCCESS(rv, rv);

  mShouldResize = StaticPrefs::browser_enable_automatic_image_resizing();
  mFirstResize = true;

  return NS_OK;
}

JSObject* ImageDocument::WrapNode(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return ImageDocument_Binding::Wrap(aCx, this, aGivenProto);
}

nsresult ImageDocument::StartDocumentLoad(
    const char* aCommand, nsIChannel* aChannel, nsILoadGroup* aLoadGroup,
    nsISupports* aContainer, nsIStreamListener** aDocListener, bool aReset) {
  nsresult rv = MediaDocument::StartDocumentLoad(
      aCommand, aChannel, aLoadGroup, aContainer, aDocListener, aReset);
  if (NS_FAILED(rv)) {
    return rv;
  }

  mOriginalZoomLevel = IsSiteSpecific() ? 1.0 : GetZoomLevel();
  CheckFullZoom();
  mOriginalResolution = GetResolution();

  if (BrowsingContext* context = GetBrowsingContext()) {
    mIsInObjectOrEmbed = context->IsEmbedderTypeObjectOrEmbed();
  }

  NS_ASSERTION(aDocListener, "null aDocListener");
  *aDocListener = new ImageListener(this);
  NS_ADDREF(*aDocListener);

  return NS_OK;
}

void ImageDocument::Destroy() {
  if (RefPtr<HTMLImageElement> img = std::move(mImageContent)) {
    // Remove our event listener from the image content.
    img->RemoveEventListener(u"load"_ns, this, false);
    img->RemoveEventListener(u"click"_ns, this, false);

    // Break reference cycle with mImageContent, if we have one
    if (mObservingImageLoader) {
      img->RemoveNativeObserver(this);
    }
  }

  MediaDocument::Destroy();
}

void ImageDocument::SetScriptGlobalObject(
    nsIScriptGlobalObject* aScriptGlobalObject) {
  // If the script global object is changing, we need to unhook our event
  // listeners on the window.
  nsCOMPtr<EventTarget> target;
  if (mScriptGlobalObject && aScriptGlobalObject != mScriptGlobalObject) {
    target = do_QueryInterface(mScriptGlobalObject);
    target->RemoveEventListener(u"resize"_ns, this, false);
    target->RemoveEventListener(u"keypress"_ns, this, false);
  }

  // Set the script global object on the superclass before doing
  // anything that might require it....
  MediaDocument::SetScriptGlobalObject(aScriptGlobalObject);

  if (aScriptGlobalObject) {
    if (!InitialSetupHasBeenDone()) {
      MOZ_ASSERT(!GetRootElement(), "Where did the root element come from?");
      // Create synthetic document
#ifdef DEBUG
      nsresult rv =
#endif
          CreateSyntheticDocument();
      NS_ASSERTION(NS_SUCCEEDED(rv), "failed to create synthetic document");

      target = mImageContent;
      target->AddEventListener(u"load"_ns, this, false);
      target->AddEventListener(u"click"_ns, this, false);
    }

    target = do_QueryInterface(aScriptGlobalObject);
    target->AddEventListener(u"resize"_ns, this, false);
    target->AddEventListener(u"keypress"_ns, this, false);

    if (!InitialSetupHasBeenDone()) {
      LinkStylesheet(u"resource://content-accessible/ImageDocument.css"_ns);
      if (!nsContentUtils::IsChildOfSameType(this)) {
        LinkStylesheet(nsLiteralString(
            u"resource://content-accessible/TopLevelImageDocument.css"));
      }
      InitialSetupDone();
    }
  }
}

void ImageDocument::OnPageShow(bool aPersisted,
                               EventTarget* aDispatchStartTarget,
                               bool aOnlySystemGroup) {
  if (aPersisted) {
    mOriginalZoomLevel = IsSiteSpecific() ? 1.0 : GetZoomLevel();
    CheckFullZoom();
    mOriginalResolution = GetResolution();
  }
  RefPtr<ImageDocument> kungFuDeathGrip(this);
  UpdateSizeFromLayout();

  MediaDocument::OnPageShow(aPersisted, aDispatchStartTarget, aOnlySystemGroup);
}

void ImageDocument::ShrinkToFit() {
  if (!mImageContent) {
    return;
  }
  if (GetZoomLevel() != mOriginalZoomLevel && mImageIsResized &&
      !nsContentUtils::IsChildOfSameType(this)) {
    // If we're zoomed, so that we don't maintain the invariant that
    // mImageIsResized if and only if its displayed width/height fit in
    // mVisibleWidth/mVisibleHeight, then we may need to switch to/from the
    // overflowingVertical class here, because our viewport size may have
    // changed and we don't plan to adjust the image size to compensate.  Since
    // mImageIsResized it has a "height" attribute set, and we can just get the
    // displayed image height by getting .height on the HTMLImageElement.
    //
    // Hold strong ref, because Height() can run script.
    RefPtr<HTMLImageElement> img = mImageContent;
    uint32_t imageHeight = img->Height();
    nsDOMTokenList* classList = img->ClassList();
    if (imageHeight > mVisibleHeight) {
      classList->Add(u"overflowingVertical"_ns, IgnoreErrors());
    } else {
      classList->Remove(u"overflowingVertical"_ns, IgnoreErrors());
    }
    return;
  }
  if (GetResolution() != mOriginalResolution && mImageIsResized) {
    // Don't resize if resolution has changed, e.g., through pinch-zooming on
    // Android.
    return;
  }

  // Keep image content alive while changing the attributes.
  RefPtr<HTMLImageElement> image = mImageContent;

  uint32_t newWidth = std::max(1, NSToCoordFloor(GetRatio() * mImageWidth));
  uint32_t newHeight = std::max(1, NSToCoordFloor(GetRatio() * mImageHeight));
  image->SetWidth(newWidth, IgnoreErrors());
  image->SetHeight(newHeight, IgnoreErrors());

  // The view might have been scrolled when zooming in, scroll back to the
  // origin now that we're showing a shrunk-to-window version.
  ScrollImageTo(0, 0);

  if (!mImageContent) {
    // ScrollImageTo flush destroyed our content.
    return;
  }

  SetModeClass(eShrinkToFit);

  mImageIsResized = true;

  UpdateTitleAndCharset();
}

void ImageDocument::ScrollImageTo(int32_t aX, int32_t aY) {
  RefPtr<PresShell> presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  ScrollContainerFrame* sf = presShell->GetRootScrollContainerFrame();
  if (!sf) {
    return;
  }

  float ratio = GetRatio();
  // Don't try to scroll image if the document is not visible (mVisibleWidth or
  // mVisibleHeight is zero).
  if (ratio <= 0.0) {
    return;
  }
  nsRect portRect = sf->GetScrollPortRect();
  sf->ScrollTo(
      nsPoint(
          nsPresContext::CSSPixelsToAppUnits(aX / ratio) - portRect.width / 2,
          nsPresContext::CSSPixelsToAppUnits(aY / ratio) - portRect.height / 2),
      ScrollMode::Instant);
}

void ImageDocument::RestoreImage() {
  if (!mImageContent) {
    return;
  }
  // Keep image content alive while changing the attributes.
  RefPtr<HTMLImageElement> imageContent = mImageContent;
  imageContent->UnsetAttr(kNameSpaceID_None, nsGkAtoms::width, true);
  imageContent->UnsetAttr(kNameSpaceID_None, nsGkAtoms::height, true);

  if (mIsInObjectOrEmbed) {
    SetModeClass(eIsInObjectOrEmbed);
  } else if (ImageIsOverflowing()) {
    if (!ImageIsOverflowingVertically()) {
      SetModeClass(eOverflowingHorizontalOnly);
    } else {
      SetModeClass(eOverflowingVertical);
    }
  } else {
    SetModeClass(eNone);
  }

  mImageIsResized = false;

  UpdateTitleAndCharset();
}

void ImageDocument::NotifyPossibleTitleChange(bool aBoundTitleElement) {
  if (!mHasCustomTitle && !mTitleUpdateInProgress) {
    mHasCustomTitle = true;
  }

  Document::NotifyPossibleTitleChange(aBoundTitleElement);
}

void ImageDocument::Notify(imgIRequest* aRequest, int32_t aType,
                           const nsIntRect* aData) {
  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    nsCOMPtr<imgIContainer> image;
    aRequest->GetImage(getter_AddRefs(image));
    return OnSizeAvailable(aRequest, image);
  }

  // Run this using a script runner because HAS_TRANSPARENCY notifications can
  // come during painting and this will trigger invalidation.
  if (aType == imgINotificationObserver::HAS_TRANSPARENCY) {
    nsCOMPtr<nsIRunnable> runnable =
        NewRunnableMethod("dom::ImageDocument::OnHasTransparency", this,
                          &ImageDocument::OnHasTransparency);
    nsContentUtils::AddScriptRunner(runnable);
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    uint32_t reqStatus;
    aRequest->GetImageStatus(&reqStatus);
    nsresult status =
        reqStatus & imgIRequest::STATUS_ERROR ? NS_ERROR_FAILURE : NS_OK;
    return OnLoadComplete(aRequest, status);
  }
}

void ImageDocument::OnHasTransparency() {
  if (!mImageContent || nsContentUtils::IsChildOfSameType(this)) {
    return;
  }

  nsDOMTokenList* classList = mImageContent->ClassList();
  classList->Add(u"transparent"_ns, IgnoreErrors());
}

void ImageDocument::SetModeClass(eModeClasses mode) {
  RefPtr<nsDOMTokenList> classList = mImageContent->ClassList();

  if (mode == eShrinkToFit) {
    classList->Add(u"shrinkToFit"_ns, IgnoreErrors());
  } else {
    classList->Remove(u"shrinkToFit"_ns, IgnoreErrors());
  }

  if (mode == eOverflowingVertical) {
    classList->Add(u"overflowingVertical"_ns, IgnoreErrors());
  } else {
    classList->Remove(u"overflowingVertical"_ns, IgnoreErrors());
  }

  if (mode == eOverflowingHorizontalOnly) {
    classList->Add(u"overflowingHorizontalOnly"_ns, IgnoreErrors());
  } else {
    classList->Remove(u"overflowingHorizontalOnly"_ns, IgnoreErrors());
  }

  if (mode == eIsInObjectOrEmbed) {
    classList->Add(u"isInObjectOrEmbed"_ns, IgnoreErrors());
  }
}

void ImageDocument::OnSizeAvailable(imgIRequest* aRequest,
                                    imgIContainer* aImage) {
  int32_t oldWidth = mImageWidth;
  int32_t oldHeight = mImageHeight;

  // Styles have not yet been applied, so we don't know the final size. For now,
  // default to the image's intrinsic size.
  aImage->GetWidth(&mImageWidth);
  aImage->GetHeight(&mImageHeight);

  // Multipart images send size available for each part; ignore them if it
  // doesn't change our size. (We may not even support changing size in
  // multipart images in the future.)
  if (oldWidth == mImageWidth && oldHeight == mImageHeight) {
    return;
  }

  nsCOMPtr<nsIRunnable> runnable =
      NewRunnableMethod("dom::ImageDocument::DefaultCheckOverflowing", this,
                        &ImageDocument::DefaultCheckOverflowing);
  nsContentUtils::AddScriptRunner(runnable);
  UpdateTitleAndCharset();
}

void ImageDocument::OnLoadComplete(imgIRequest* aRequest, nsresult aStatus) {
  UpdateTitleAndCharset();

  // mImageContent can be null if the document is already destroyed
  if (NS_FAILED(aStatus) && mImageContent) {
    nsAutoCString src;
    mDocumentURI->GetSpec(src);
    AutoTArray<nsString, 1> formatString;
    CopyUTF8toUTF16(src, *formatString.AppendElement());
    nsAutoString errorMsg;
    FormatStringFromName("InvalidImage", formatString, errorMsg);

    mImageContent->SetAttr(kNameSpaceID_None, nsGkAtoms::alt, errorMsg, true,
                           IsKnownNewAttr::No);
  }

  MaybeSendResultToEmbedder(aStatus);
}

NS_IMETHODIMP
ImageDocument::HandleEvent(Event* aEvent) {
  nsAutoString eventType;
  aEvent->GetType(eventType);
  if (eventType.EqualsLiteral("resize")) {
    CheckOverflowing(false);
    CheckFullZoom();
  } else if (eventType.EqualsLiteral("click") &&
             StaticPrefs::browser_enable_click_image_resizing() &&
             !mIsInObjectOrEmbed) {
    ResetZoomLevel();
    mShouldResize = true;
    if (mImageIsResized) {
      int32_t x = 0, y = 0;
      MouseEvent* event = aEvent->AsMouseEvent();
      if (event) {
        RefPtr<HTMLImageElement> img = mImageContent;
        x = event->ClientX() - img->OffsetLeft();
        y = event->ClientY() - img->OffsetTop();
      }
      mShouldResize = false;
      RestoreImage();
      FlushPendingNotifications(FlushType::Layout);
      ScrollImageTo(x, y);
    } else if (ImageIsOverflowing()) {
      ShrinkToFit();
    }
  } else if (eventType.EqualsLiteral("load")) {
    UpdateSizeFromLayout();
  }

  return NS_OK;
}

void ImageDocument::UpdateSizeFromLayout() {
  // Pull an updated size from the content frame to account for any size
  // change due to CSS properties like |image-orientation|.
  if (!mImageContent) {
    return;
  }

  // Need strong ref, because GetPrimaryFrame can run script.
  RefPtr<HTMLImageElement> imageContent = mImageContent;
  nsIFrame* contentFrame = imageContent->GetPrimaryFrame(FlushType::Frames);
  if (!contentFrame) {
    return;
  }

  nsIntSize oldSize(mImageWidth, mImageHeight);
  IntrinsicSize newSize = contentFrame->GetIntrinsicSize();

  if (newSize.width) {
    mImageWidth = nsPresContext::AppUnitsToFloatCSSPixels(*newSize.width);
  }
  if (newSize.height) {
    mImageHeight = nsPresContext::AppUnitsToFloatCSSPixels(*newSize.height);
  }

  // Ensure that our information about overflow is up-to-date if needed.
  if (mImageWidth != oldSize.width || mImageHeight != oldSize.height) {
    CheckOverflowing(false);
  }
}

void ImageDocument::UpdateRemoteStyle(StyleImageRendering aImageRendering) {
  if (!mImageContent) {
    return;
  }

  // Using ScriptRunner to avoid doing DOM mutation at an unexpected time.
  if (!nsContentUtils::IsSafeToRunScript()) {
    return nsContentUtils::AddScriptRunner(
        NewRunnableMethod<StyleImageRendering>(
            "UpdateRemoteStyle", this, &ImageDocument::UpdateRemoteStyle,
            aImageRendering));
  }

  nsCOMPtr<nsDOMCSSDeclaration> style = mImageContent->Style();
  switch (aImageRendering) {
    case StyleImageRendering::Auto:
    case StyleImageRendering::Smooth:
    case StyleImageRendering::Optimizequality:
      style->SetProperty("image-rendering"_ns, "auto"_ns, ""_ns,
                         IgnoreErrors());
      break;
    case StyleImageRendering::Optimizespeed:
    case StyleImageRendering::Pixelated:
      style->SetProperty("image-rendering"_ns, "pixelated"_ns, ""_ns,
                         IgnoreErrors());
      break;
    case StyleImageRendering::CrispEdges:
      style->SetProperty("image-rendering"_ns, "crisp-edges"_ns, ""_ns,
                         IgnoreErrors());
      break;
  }
}

nsresult ImageDocument::CreateSyntheticDocument() {
  // Synthesize an html document that refers to the image
  nsresult rv = MediaDocument::CreateSyntheticDocument();
  NS_ENSURE_SUCCESS(rv, rv);

  // Add the image element
  RefPtr<Element> body = GetBodyElement();
  if (!body) {
    NS_WARNING("no body on image document!");
    return NS_ERROR_FAILURE;
  }

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  nodeInfo = mNodeInfoManager->GetNodeInfo(
      nsGkAtoms::img, nullptr, kNameSpaceID_XHTML, nsINode::ELEMENT_NODE);

  RefPtr<Element> image = NS_NewHTMLImageElement(nodeInfo.forget());
  mImageContent = HTMLImageElement::FromNodeOrNull(image);
  if (!mImageContent) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsAutoCString src;
  mDocumentURI->GetSpec(src);

  NS_ConvertUTF8toUTF16 srcString(src);
  // Make sure not to start the image load from here...
  mImageContent->SetLoadingEnabled(false);
  mImageContent->SetAttr(kNameSpaceID_None, nsGkAtoms::src, srcString, false,
                         IsKnownNewAttr::Yes);
  mImageContent->SetAttr(kNameSpaceID_None, nsGkAtoms::alt, srcString, false,
                         IsKnownNewAttr::Yes);

  if (mIsInObjectOrEmbed) {
    SetModeClass(eIsInObjectOrEmbed);
  }

  body->AppendChildTo(mImageContent, false, IgnoreErrors());
  mImageContent->SetLoadingEnabled(true);

  return NS_OK;
}

void ImageDocument::DefaultCheckOverflowing() {
  CheckOverflowing(StaticPrefs::browser_enable_automatic_image_resizing());
}

nsresult ImageDocument::CheckOverflowing(bool changeState) {
  const bool imageWasOverflowing = ImageIsOverflowing();
  const bool imageWasOverflowingVertically = ImageIsOverflowingVertically();

  {
    nsPresContext* context = GetPresContext();
    if (!context) {
      return NS_OK;
    }

    nsRect visibleArea = context->GetVisibleArea();

    mVisibleWidth = nsPresContext::AppUnitsToFloatCSSPixels(visibleArea.width);
    mVisibleHeight =
        nsPresContext::AppUnitsToFloatCSSPixels(visibleArea.height);
  }

  const bool windowBecameBigEnough =
      imageWasOverflowing && !ImageIsOverflowing();
  const bool verticalOverflowChanged =
      imageWasOverflowingVertically != ImageIsOverflowingVertically();

  if (changeState || mShouldResize || mFirstResize || windowBecameBigEnough ||
      verticalOverflowChanged) {
    if (mIsInObjectOrEmbed) {
      SetModeClass(eIsInObjectOrEmbed);
    } else if (ImageIsOverflowing() && (changeState || mShouldResize)) {
      ShrinkToFit();
    } else if (mImageIsResized || mFirstResize || windowBecameBigEnough) {
      RestoreImage();
    } else if (!mImageIsResized && verticalOverflowChanged) {
      if (ImageIsOverflowingVertically()) {
        SetModeClass(eOverflowingVertical);
      } else {
        SetModeClass(eOverflowingHorizontalOnly);
      }
    }
  }
  mFirstResize = false;
  return NS_OK;
}

void ImageDocument::UpdateTitleAndCharset() {
  if (mHasCustomTitle) {
    return;
  }

  AutoRestore<bool> restore(mTitleUpdateInProgress);
  mTitleUpdateInProgress = true;

  nsAutoCString typeStr;
  nsCOMPtr<imgIRequest> imageRequest;
  if (mImageContent) {
    mImageContent->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                              getter_AddRefs(imageRequest));
  }

  if (imageRequest) {
    nsCString mimeType;
    imageRequest->GetMimeType(getter_Copies(mimeType));
    ToUpperCase(mimeType);
    nsCString::const_iterator start, end;
    mimeType.BeginReading(start);
    mimeType.EndReading(end);
    nsCString::const_iterator iter = end;
    if (FindInReadable("IMAGE/"_ns, start, iter) && iter != end) {
      // strip out "X-" if any
      if (*iter == 'X') {
        ++iter;
        if (iter != end && *iter == '-') {
          ++iter;
          if (iter == end) {
            // looks like "IMAGE/X-" is the type??  Bail out of here.
            mimeType.BeginReading(iter);
          }
        } else {
          --iter;
        }
      }
      typeStr = Substring(iter, end);
    } else {
      typeStr = mimeType;
    }
  }

  nsAutoString status;
  if (mImageIsResized) {
    AutoTArray<nsString, 1> formatString;
    formatString.AppendElement()->AppendInt(NSToCoordFloor(GetRatio() * 100));

    FormatStringFromName("ScaledImage", formatString, status);
  }

  static const char* const formatNames[4] = {
      "ImageTitleWithNeitherDimensionsNorFile",
      "ImageTitleWithoutDimensions",
      "ImageTitleWithDimensions2",
      "ImageTitleWithDimensions2AndFile",
  };

  MediaDocument::UpdateTitleAndCharset(typeStr, mChannel, formatNames,
                                       mImageWidth, mImageHeight, status);
}

bool ImageDocument::IsSiteSpecific() {
  return !ShouldResistFingerprinting(RFPTarget::SiteSpecificZoom) &&
         StaticPrefs::browser_zoom_siteSpecific();
}

void ImageDocument::ResetZoomLevel() {
  if (nsContentUtils::IsChildOfSameType(this)) {
    return;
  }

  if (RefPtr<BrowsingContext> bc = GetBrowsingContext()) {
    // Resetting the zoom level on a discarded browsing context has no effect.
    (void)bc->SetFullZoom(mOriginalZoomLevel);
  }
}

float ImageDocument::GetZoomLevel() {
  if (BrowsingContext* bc = GetBrowsingContext()) {
    return bc->FullZoom();
  }
  return mOriginalZoomLevel;
}

void ImageDocument::CheckFullZoom() {
  RefPtr<nsDOMTokenList> classList =
      mImageContent ? mImageContent->ClassList() : nullptr;

  if (!classList) {
    return;
  }

  classList->Toggle(u"fullZoomOut"_ns,
                    dom::Optional<bool>(GetZoomLevel() > mOriginalZoomLevel),
                    IgnoreErrors());
  classList->Toggle(u"fullZoomIn"_ns,
                    dom::Optional<bool>(GetZoomLevel() < mOriginalZoomLevel),
                    IgnoreErrors());
}

float ImageDocument::GetResolution() {
  if (PresShell* presShell = GetPresShell()) {
    return presShell->GetResolution();
  }
  return mOriginalResolution;
}

void ImageDocument::MaybeSendResultToEmbedder(nsresult aResult) {
  if (!mIsInObjectOrEmbed) {
    return;
  }

  BrowsingContext* context = GetBrowsingContext();

  if (!context) {
    return;
  }

  if (context->GetParent() && context->GetParent()->IsInProcess()) {
    if (Element* embedder = context->GetEmbedderElement()) {
      if (nsCOMPtr<nsIObjectLoadingContent> objectLoadingContent =
              do_QueryInterface(embedder)) {
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "nsObjectLoadingContent::SubdocumentImageLoadComplete",
            [objectLoadingContent, aResult]() {
              static_cast<nsObjectLoadingContent*>(objectLoadingContent.get())
                  ->SubdocumentImageLoadComplete(aResult);
            }));
      }
      return;
    }
  }

  if (BrowserChild* browserChild =
          BrowserChild::GetFrom(context->GetDocShell())) {
    browserChild->SendImageLoadComplete(aResult);
  }
}
}  // namespace mozilla::dom

nsresult NS_NewImageDocument(mozilla::dom::Document** aResult,
                             nsIPrincipal* aPrincipal,
                             nsIPrincipal* aPartitionedPrincipal) {
  auto* doc = new mozilla::dom::ImageDocument();
  NS_ADDREF(doc);

  nsresult rv = doc->Init(aPrincipal, aPartitionedPrincipal);
  if (NS_FAILED(rv)) {
    NS_RELEASE(doc);
  }

  *aResult = doc;

  return rv;
}
