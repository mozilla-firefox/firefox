/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGDocumentWrapper.h"

#include "mozilla/Components.h"
#include "mozilla/PresShell.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/Animation.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/SVGDocument.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "nsComponentManagerUtils.h"
#include "nsICategoryManager.h"
#include "nsIChannel.h"
#include "nsIDocumentLoaderFactory.h"
#include "nsIDocumentViewer.h"
#include "nsIHttpChannel.h"
#include "nsIObserverService.h"
#include "nsIParser.h"
#include "nsIRequest.h"
#include "nsIStreamListener.h"
#include "nsIXMLContentSink.h"
#include "nsMimeTypes.h"
#include "nsNetCID.h"
#include "nsRefreshDriver.h"

namespace mozilla {

using namespace dom;
using namespace gfx;

namespace image {

NS_IMPL_ISUPPORTS(SVGDocumentWrapper, nsIStreamListener, nsIRequestObserver,
                  nsIObserver, nsISupportsWeakReference)

SVGDocumentWrapper::SVGDocumentWrapper()
    : mIgnoreInvalidation(false),
      mRegisteredForXPCOMShutdown(false),
      mIsDrawing(false) {}

SVGDocumentWrapper::~SVGDocumentWrapper() {
  DestroyViewer();
  if (mRegisteredForXPCOMShutdown) {
    UnregisterForXPCOMShutdown();
  }
}

void SVGDocumentWrapper::DestroyViewer() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mViewer) {
    mViewer->GetDocument()->OnPageHide(false, nullptr);
    mViewer->Close();
    mViewer->Destroy();
    mViewer = nullptr;
  }
}

nsIFrame* SVGDocumentWrapper::GetRootLayoutFrame() const {
  Element* rootElem = GetSVGRootElement();
  return rootElem ? rootElem->GetPrimaryFrame() : nullptr;
}

void SVGDocumentWrapper::UpdateViewportBounds(const CSSSize& aViewportSize) {
  MOZ_ASSERT(!mIgnoreInvalidation, "shouldn't be reentrant");
  mIgnoreInvalidation = true;

  // We want to make sure our (maybe fractional) viewport gets used as-is.
  // However, the document viewer deals with whole device pixels.
  //
  // Ceil our doc viewer bounds (to avoid any potential clipping), and set the
  // layout viewport explicitly as well.
  const auto intSize =
      LayoutDeviceIntSize::Ceil(aViewportSize.width, aViewportSize.height);
  const nsSize maybeFractionalSize = CSSPixel::ToAppUnits(aViewportSize);

  LayoutDeviceIntRect currentBounds;
  mViewer->GetBounds(currentBounds);

  bool changed = false;
  if (currentBounds.Size() != intSize) {
    mViewer->SetBounds(LayoutDeviceIntRect(LayoutDeviceIntPoint(), intSize));
    changed = true;
  }

  if (RefPtr ps = GetPresShell();
      ps && ps->GetLayoutViewportSize() != maybeFractionalSize) {
    ps->SetLayoutViewportSize(maybeFractionalSize, /* aDelay = */ false);
    changed = true;
  }

  // If the bounds have changed, we need to do a layout flush.
  if (changed) {
    FlushLayout();
  }

  mIgnoreInvalidation = false;
}

void SVGDocumentWrapper::FlushImageTransformInvalidation() {
  MOZ_ASSERT(!mIgnoreInvalidation, "shouldn't be reentrant");

  SVGSVGElement* svgElem = GetSVGRootElement();
  if (!svgElem) {
    return;
  }

  mIgnoreInvalidation = true;
  svgElem->FlushImageTransformInvalidation();
  FlushLayout();
  mIgnoreInvalidation = false;
}

bool SVGDocumentWrapper::IsAnimated() const {
  // Can be called for animated images during shutdown, after we've
  // already Observe()'d XPCOM shutdown and cleared out our mViewer pointer.
  if (!mViewer) {
    return false;
  }

  Document* doc = mViewer->GetDocument();
  if (!doc) {
    return false;
  }
  if (doc->Timeline()->HasAnimations()) {
    // CSS animations (technically HasAnimations() also checks for CSS
    // transitions and Web animations but since SVG-as-an-image doesn't run
    // script they will never run in the document that we wrap).
    return true;
  }
  if (doc->HasAnimationController() &&
      doc->GetAnimationController()->HasRegisteredAnimations()) {
    // SMIL animations
    return true;
  }
  return false;
}

void SVGDocumentWrapper::StartAnimation() {
  // Can be called for animated images during shutdown, after we've
  // already Observe()'d XPCOM shutdown and cleared out our mViewer pointer.
  if (!mViewer) {
    return;
  }

  Document* doc = mViewer->GetDocument();
  if (doc) {
    SMILAnimationController* controller = doc->GetAnimationController();
    if (controller) {
      controller->Resume(SMILTimeContainer::PauseType::Image);
    }
    doc->SetImageAnimationState(true);
  }
}

void SVGDocumentWrapper::StopAnimation() {
  // Can be called for animated images during shutdown, after we've
  // already Observe()'d XPCOM shutdown and cleared out our mViewer pointer.
  if (!mViewer) {
    return;
  }

  if (Document* doc = mViewer->GetDocument()) {
    SMILAnimationController* controller = doc->GetAnimationController();
    if (controller) {
      controller->Pause(SMILTimeContainer::PauseType::Image);
    }
    doc->SetImageAnimationState(false);
  }
}

void SVGDocumentWrapper::ResetAnimation() {
  SVGSVGElement* svgElem = GetSVGRootElement();
  if (!svgElem) {
    return;
  }

  svgElem->SetCurrentTime(0.0f);
}

float SVGDocumentWrapper::GetCurrentTimeAsFloat() const {
  SVGSVGElement* svgElem = GetSVGRootElement();
  return svgElem ? svgElem->GetCurrentTimeAsFloat() : 0.0f;
}

void SVGDocumentWrapper::SetCurrentTime(float aTime) {
  SVGSVGElement* svgElem = GetSVGRootElement();
  if (svgElem && svgElem->GetCurrentTimeAsFloat() != aTime) {
    svgElem->SetCurrentTime(aTime);
  }
}

void SVGDocumentWrapper::TickRefreshDriver() {
  if (RefPtr<PresShell> presShell = mViewer->GetPresShell()) {
    if (RefPtr<nsPresContext> presContext = presShell->GetPresContext()) {
      if (RefPtr<nsRefreshDriver> driver = presContext->RefreshDriver()) {
        driver->DoTick();
      }
    }
  }
}

/** nsIStreamListener methods **/

NS_IMETHODIMP
SVGDocumentWrapper::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* inStr,
                                    uint64_t sourceOffset, uint32_t count) {
  nsCOMPtr<nsIStreamListener> listener = mListener;
  return listener->OnDataAvailable(aRequest, inStr, sourceOffset, count);
}

/** nsIRequestObserver methods **/

NS_IMETHODIMP
SVGDocumentWrapper::OnStartRequest(nsIRequest* aRequest) {
  nsresult rv = SetupViewer(aRequest, getter_AddRefs(mViewer),
                            getter_AddRefs(mLoadGroup));

  nsCOMPtr<nsIStreamListener> listener = mListener;
  if (NS_SUCCEEDED(rv) && NS_SUCCEEDED(listener->OnStartRequest(aRequest))) {
    mViewer->GetDocument()->SetIsBeingUsedAsImage();
    StopAnimation();  // otherwise animations start automatically in helper doc

    rv = mViewer->Init(nullptr, LayoutDeviceIntRect(), nullptr);
    if (NS_SUCCEEDED(rv)) {
      rv = mViewer->Open();
    }
  }
  return rv;
}

NS_IMETHODIMP
SVGDocumentWrapper::OnStopRequest(nsIRequest* aRequest, nsresult status) {
  if (nsCOMPtr<nsIStreamListener> listener = mListener) {
    listener->OnStopRequest(aRequest, status);
    mListener = nullptr;
  }

  return NS_OK;
}

/** nsIObserver Methods **/
NS_IMETHODIMP
SVGDocumentWrapper::Observe(nsISupports* aSubject, const char* aTopic,
                            const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    // Sever ties from rendering observers to helper-doc's root SVG node
    SVGSVGElement* svgElem = GetSVGRootElement();
    if (svgElem) {
      SVGObserverUtils::RemoveAllRenderingObservers(svgElem);
    }

    // Clean up at XPCOM shutdown time.
    DestroyViewer();
    if (mListener) {
      mListener = nullptr;
    }
    if (mLoadGroup) {
      mLoadGroup = nullptr;
    }

    // Turn off "registered" flag, or else we'll try to unregister when we die.
    // (No need for that now, and the try would fail anyway -- it's too late.)
    mRegisteredForXPCOMShutdown = false;
  } else {
    NS_ERROR("Unexpected observer topic.");
  }
  return NS_OK;
}

/** Private helper methods **/

// This method is largely cribbed from
// nsExternalResourceMap::PendingLoad::SetupViewer.
nsresult SVGDocumentWrapper::SetupViewer(nsIRequest* aRequest,
                                         nsIDocumentViewer** aViewer,
                                         nsILoadGroup** aLoadGroup) {
  nsCOMPtr<nsIChannel> chan(do_QueryInterface(aRequest));
  NS_ENSURE_TRUE(chan, NS_ERROR_UNEXPECTED);

  // Check for HTTP error page
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aRequest));
  if (httpChannel) {
    bool requestSucceeded;
    if (NS_FAILED(httpChannel->GetRequestSucceeded(&requestSucceeded)) ||
        !requestSucceeded) {
      return NS_ERROR_FAILURE;
    }
  }

  // Give this document its own loadgroup
  nsCOMPtr<nsILoadGroup> loadGroup;
  chan->GetLoadGroup(getter_AddRefs(loadGroup));

  nsCOMPtr<nsILoadGroup> newLoadGroup =
      do_CreateInstance(NS_LOADGROUP_CONTRACTID);
  NS_ENSURE_TRUE(newLoadGroup, NS_ERROR_OUT_OF_MEMORY);
  newLoadGroup->SetLoadGroup(loadGroup);

  nsCOMPtr<nsIDocumentLoaderFactory> docLoaderFactory =
      nsContentUtils::FindInternalDocumentViewer(
          nsLiteralCString(IMAGE_SVG_XML));
  NS_ENSURE_TRUE(docLoaderFactory, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIDocumentViewer> viewer;
  nsCOMPtr<nsIStreamListener> listener;
  nsresult rv = docLoaderFactory->CreateInstance(
      "external-resource", chan, newLoadGroup, nsLiteralCString(IMAGE_SVG_XML),
      nullptr, nullptr, getter_AddRefs(listener), getter_AddRefs(viewer));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(viewer, NS_ERROR_UNEXPECTED);

  // Create a navigation time object and pass it to the SVG document through
  // the viewer.
  // The timeline(DocumentTimeline, used in CSS animation) of this SVG
  // document needs this navigation timing object for time computation, such
  // as to calculate current time stamp based on the start time of navigation
  // time object.
  //
  // For a root document, DocShell would do these sort of things
  // automatically. Since there is no DocShell for this wrapped SVG document,
  // we must set it up manually.
  auto timing = MakeRefPtr<nsDOMNavigationTiming>(nullptr);
  timing->NotifyNavigationStart(
      nsDOMNavigationTiming::DocShellState::eInactive);
  viewer->SetNavigationTiming(timing);

  nsCOMPtr<nsIParser> parser = do_QueryInterface(listener);
  NS_ENSURE_TRUE(parser, NS_ERROR_UNEXPECTED);

  // XML-only, because this is for SVG content
  nsCOMPtr<nsIContentSink> sink = parser->GetContentSink();
  NS_ENSURE_TRUE(sink, NS_ERROR_UNEXPECTED);

  listener.swap(mListener);
  viewer.forget(aViewer);
  newLoadGroup.forget(aLoadGroup);

  RegisterForXPCOMShutdown();
  return NS_OK;
}

void SVGDocumentWrapper::RegisterForXPCOMShutdown() {
  MOZ_ASSERT(!mRegisteredForXPCOMShutdown, "re-registering for XPCOM shutdown");
  // Listen for xpcom-shutdown so that we can drop references to our
  // helper-document at that point. (Otherwise, we won't get cleaned up
  // until imgLoader::Shutdown, which can happen after the JAR service
  // and RDF service have been unregistered.)
  nsresult rv;
  nsCOMPtr<nsIObserverService> obsSvc = components::Observer::Service(&rv);
  if (NS_FAILED(rv) || NS_FAILED(obsSvc->AddObserver(
                           this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true))) {
    NS_WARNING("Failed to register as observer of XPCOM shutdown");
  } else {
    mRegisteredForXPCOMShutdown = true;
  }
}

void SVGDocumentWrapper::UnregisterForXPCOMShutdown() {
  MOZ_ASSERT(mRegisteredForXPCOMShutdown,
             "unregistering for XPCOM shutdown w/out being registered");

  nsresult rv;
  nsCOMPtr<nsIObserverService> obsSvc = components::Observer::Service(&rv);
  if (NS_FAILED(rv) ||
      NS_FAILED(obsSvc->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID))) {
    NS_WARNING("Failed to unregister as observer of XPCOM shutdown");
  } else {
    mRegisteredForXPCOMShutdown = false;
  }
}

void SVGDocumentWrapper::FlushLayout() {
  if (SVGDocument* doc = GetDocument()) {
    doc->FlushPendingNotifications(FlushType::Layout);
  }
}

SVGDocument* SVGDocumentWrapper::GetDocument() const {
  if (!mViewer) {
    return nullptr;
  }
  Document* doc = mViewer->GetDocument();
  if (!doc) {
    return nullptr;
  }
  return doc->AsSVGDocument();
}

SVGSVGElement* SVGDocumentWrapper::GetSVGRootElement() const {
  if (!mViewer) {
    return nullptr;  // Can happen during destruction
  }

  Document* doc = mViewer->GetDocument();
  return doc ? doc->GetSVGRootElement() : nullptr;
}

}  // namespace image
}  // namespace mozilla
