/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLCanvasElement.h"

#include "ActiveLayerTracker.h"
#include "CanvasUtils.h"
#include "ClientWebGLContext.h"
#include "ImageEncoder.h"
#include "MediaTrackGraph.h"
#include "VRManagerChild.h"
#include "WindowRenderer.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "mozilla/Assertions.h"
#include "mozilla/Base64.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanvasCaptureMediaStream.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/GeneratePlaceholderCanvasData.h"
#include "mozilla/dom/HTMLCanvasElementBinding.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/OffscreenCanvas.h"
#include "mozilla/dom/OffscreenCanvasDisplayHelper.h"
#include "mozilla/dom/VideoStreamTrack.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/layers/CanvasRenderer.h"
#include "mozilla/layers/WebRenderCanvasRenderer.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/webgpu/CanvasContext.h"
#include "nsAttrValueInlines.h"
#include "nsContentUtils.h"
#include "nsDOMJSUtils.h"
#include "nsDisplayList.h"
#include "nsITimer.h"
#include "nsJSUtils.h"
#include "nsLayoutUtils.h"
#include "nsMathUtils.h"
#include "nsNetUtil.h"
#include "nsRefreshDriver.h"
#include "nsStreamUtils.h"

using namespace mozilla::layers;
using namespace mozilla::gfx;

NS_IMPL_NS_NEW_HTML_ELEMENT(Canvas)

namespace mozilla::dom {

class RequestedFrameRefreshObserver : public nsARefreshObserver {
  NS_INLINE_DECL_REFCOUNTING(RequestedFrameRefreshObserver, override)

 public:
  RequestedFrameRefreshObserver(HTMLCanvasElement* const aOwningElement,
                                nsRefreshDriver* aRefreshDriver,
                                bool aReturnPlaceholderData)
      : mRegistered(false),
        mWatching(false),
        mReturnPlaceholderData(aReturnPlaceholderData),
        mOwningElement(aOwningElement),
        mRefreshDriver(aRefreshDriver),
        mWatchManager(this, AbstractThread::MainThread()),
        mPendingThrottledCapture(false) {
    MOZ_ASSERT(mOwningElement);
  }

  static already_AddRefed<DataSourceSurface> CopySurface(
      const RefPtr<SourceSurface>& aSurface, bool aReturnPlaceholderData) {
    RefPtr<DataSourceSurface> data = aSurface->GetDataSurface();
    if (!data) {
      return nullptr;
    }

    DataSourceSurface::ScopedMap read(data, DataSourceSurface::READ);
    if (!read.IsMapped()) {
      return nullptr;
    }

    RefPtr<DataSourceSurface> copy = Factory::CreateDataSourceSurfaceWithStride(
        data->GetSize(), data->GetFormat(), read.GetStride());
    if (!copy) {
      return nullptr;
    }

    DataSourceSurface::ScopedMap write(copy, DataSourceSurface::WRITE);
    if (!write.IsMapped()) {
      return nullptr;
    }

    MOZ_ASSERT(read.GetStride() == write.GetStride());
    MOZ_ASSERT(data->GetSize() == copy->GetSize());
    MOZ_ASSERT(data->GetFormat() == copy->GetFormat());

    if (aReturnPlaceholderData) {
      auto size = write.GetStride() * copy->GetSize().height;
      auto* data = write.GetData();
      GeneratePlaceholderCanvasData(size, data);
    } else {
      memcpy(write.GetData(), read.GetData(),
             write.GetStride() * copy->GetSize().height);
    }

    return copy.forget();
  }

  void SetReturnPlaceholderData(bool aReturnPlaceholderData) {
    mReturnPlaceholderData = aReturnPlaceholderData;
  }

  void NotifyCaptureStateChange() {
    if (mPendingThrottledCapture) {
      return;
    }

    if (!mOwningElement) {
      return;
    }

    Watchable<FrameCaptureState>* captureState =
        mOwningElement->GetFrameCaptureState();
    if (!captureState) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: No capture state"_ns);
      return;
    }

    if (captureState->Ref() == FrameCaptureState::CLEAN) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: CLEAN"_ns);
      return;
    }

    if (!mRefreshDriver) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: no refresh driver"_ns);
      return;
    }

    if (!mRefreshDriver->IsThrottled()) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: not throttled"_ns);
      return;
    }

    TimeStamp now = TimeStamp::Now();
    TimeStamp next =
        mLastCaptureTime.IsNull()
            ? now
            : mLastCaptureTime + TimeDuration::FromMilliseconds(
                                     nsRefreshDriver::DefaultInterval());
    if (mLastCaptureTime.IsNull() || next <= now) {
      AUTO_PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                                "CaptureFrame direct while throttled"_ns);
      CaptureFrame(now);
      return;
    }

    nsCString str;
    if (profiler_thread_is_being_profiled_for_markers()) {
      str.AppendPrintf("Delaying CaptureFrame by %.2fms",
                       (next - now).ToMilliseconds());
    }
    AUTO_PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {}, str);

    mPendingThrottledCapture = true;
    AbstractThread::MainThread()->DelayedDispatch(
        NS_NewRunnableFunction(
            __func__,
            [this, self = RefPtr<RequestedFrameRefreshObserver>(this), next] {
              mPendingThrottledCapture = false;
              AUTO_PROFILER_MARKER_TEXT(
                  "Canvas CaptureStream", MEDIA_RT, {},
                  "CaptureFrame after delay while throttled"_ns);
              CaptureFrame(next);
            }),
        // next >= now, so this is a guard for (next - now) flooring to 0.
        std::max<uint32_t>(
            1, static_cast<uint32_t>((next - now).ToMilliseconds())));
  }

  void WillRefresh(TimeStamp aTime) override {
    AUTO_PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                              "CaptureFrame by refresh driver"_ns);

    CaptureFrame(aTime);
  }

  void CaptureFrame(TimeStamp aTime) {
    MOZ_ASSERT(NS_IsMainThread());

    if (!mOwningElement) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: no owning element"_ns);
      return;
    }

    if (mOwningElement->IsWriteOnly()) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: write only"_ns);
      return;
    }

    if (auto* captureStateWatchable = mOwningElement->GetFrameCaptureState();
        captureStateWatchable &&
        *captureStateWatchable == FrameCaptureState::CLEAN) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: CLEAN"_ns);
      return;
    }

    // Mark the context already now, since if the frame capture state is DIRTY
    // and we catch an early return below (not marking it CLEAN), the next draw
    // will not trigger a capture state change from the
    // Watchable<FrameCaptureState>.
    mOwningElement->MarkContextCleanForFrameCapture();

    mOwningElement->ProcessDestroyedFrameListeners();

    if (!mOwningElement->IsFrameCaptureRequested(aTime)) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: no capture requested"_ns);
      return;
    }

    if (mPendingCapturePromise.Exists()) {
      PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                           "Abort: pending capture"_ns);
      return;
    }

    {
      AUTO_PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                                "GetSnapshot"_ns);

      mOwningElement->GetSurfaceSnapshotAsync()
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              [self = RefPtr{this},
               time = aTime](RefPtr<gfx::SourceSurface> aSurface) {
                MOZ_ASSERT(aSurface);
                self->mPendingCapturePromise.Complete();
                self->GetSurfaceSnapshotAsyncSuccess(aSurface, time);
              },
              [self = RefPtr{this}](nsresult aRv) {
                NS_WARNING("Failed to get Snapshot");
                self->mPendingCapturePromise.Complete();
              })
          ->Track(mPendingCapturePromise);
    }
  }

  void GetSurfaceSnapshotAsyncSuccess(RefPtr<gfx::SourceSurface> aSurface,
                                      TimeStamp aTime) {
    RefPtr<DataSourceSurface> copy;
    {
      AUTO_PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                                "CopySurface"_ns);
      copy = CopySurface(aSurface, mReturnPlaceholderData);
      if (!copy) {
        PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {},
                             "Abort: copy failed"_ns);
        return;
      }
    }

    nsCString str;
    if (profiler_thread_is_being_profiled_for_markers()) {
      TimeDuration sinceLast =
          aTime - (mLastCaptureTime.IsNull() ? aTime : mLastCaptureTime);
      str.AppendPrintf("Forwarding captured frame %.2fms after last",
                       sinceLast.ToMilliseconds());
    }
    AUTO_PROFILER_MARKER_TEXT("Canvas CaptureStream", MEDIA_RT, {}, str);

    if (!mLastCaptureTime.IsNull() && aTime <= mLastCaptureTime) {
      aTime = mLastCaptureTime + TimeDuration::FromMilliseconds(1);
    }
    mLastCaptureTime = aTime;

    mOwningElement->SetFrameCapture(copy.forget(), aTime);
  }

  void DetachFromRefreshDriver() {
    MOZ_ASSERT(mOwningElement);
    MOZ_ASSERT(mRefreshDriver);

    Unregister();
    mRefreshDriver = nullptr;
    mWatchManager.Shutdown();
    mPendingCapturePromise.DisconnectIfExists();
  }

  bool IsRegisteredAndWatching() { return mRegistered && mWatching; }

  void Register() {
    if (!mRegistered) {
      MOZ_ASSERT(mRefreshDriver);
      if (mRefreshDriver) {
        mRefreshDriver->AddRefreshObserver(this, FlushType::Display,
                                           "Canvas frame capture listeners");
        mRegistered = true;
      }
    }

    if (mWatching) {
      return;
    }

    if (!mOwningElement) {
      return;
    }

    if (Watchable<FrameCaptureState>* captureState =
            mOwningElement->GetFrameCaptureState()) {
      mWatchManager.Watch(
          *captureState,
          &RequestedFrameRefreshObserver::NotifyCaptureStateChange);
      mWatching = true;
    }
  }

  void Unregister() {
    if (mRegistered) {
      MOZ_ASSERT(mRefreshDriver);
      if (mRefreshDriver) {
        mRefreshDriver->RemoveRefreshObserver(this, FlushType::Display);
        mRegistered = false;
      }
    }

    if (!mWatching) {
      return;
    }

    if (!mOwningElement) {
      return;
    }

    if (Watchable<FrameCaptureState>* captureState =
            mOwningElement->GetFrameCaptureState()) {
      mWatchManager.Unwatch(
          *captureState,
          &RequestedFrameRefreshObserver::NotifyCaptureStateChange);
      mWatching = false;
    }
  }

 private:
  virtual ~RequestedFrameRefreshObserver() {
    MOZ_ASSERT(!mRefreshDriver);
    MOZ_ASSERT(!mRegistered);
    MOZ_ASSERT(!mWatching);
  }

  bool mRegistered;
  bool mWatching;
  bool mReturnPlaceholderData;
  const WeakPtr<HTMLCanvasElement> mOwningElement;
  RefPtr<nsRefreshDriver> mRefreshDriver;
  WatchManager<RequestedFrameRefreshObserver> mWatchManager;
  TimeStamp mLastCaptureTime;
  bool mPendingThrottledCapture;
  MozPromiseRequestHolder<HTMLCanvasElement::SurfaceSnapshotPromise>
      mPendingCapturePromise;
};

// ---------------------------------------------------------------------------

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(HTMLCanvasPrintState, mCanvas, mContext,
                                      mCallback)

HTMLCanvasPrintState::HTMLCanvasPrintState(
    HTMLCanvasElement* aCanvas, nsICanvasRenderingContextInternal* aContext,
    nsITimerCallback* aCallback)
    : mIsDone(false),
      mPendingNotify(false),
      mCanvas(aCanvas),
      mContext(aContext),
      mCallback(aCallback) {}

HTMLCanvasPrintState::~HTMLCanvasPrintState() = default;

HTMLCanvasElement* HTMLCanvasPrintState::GetParentObject() {
  if (auto* original = mCanvas->GetOriginalCanvas()) {
    return original;
  }
  return mCanvas;
}

/* virtual */
JSObject* HTMLCanvasPrintState::WrapObject(JSContext* aCx,
                                           JS::Handle<JSObject*> aGivenProto) {
  return MozCanvasPrintState_Binding::Wrap(aCx, this, aGivenProto);
}

nsISupports* HTMLCanvasPrintState::Context() const { return mContext; }

void HTMLCanvasPrintState::Done() {
  if (!mPendingNotify && !mIsDone) {
    // The canvas needs to be invalidated for printing reftests on linux to
    // work.
    if (mCanvas) {
      mCanvas->InvalidateCanvas();
    }
    RefPtr<nsRunnableMethod<HTMLCanvasPrintState>> doneEvent =
        NewRunnableMethod("dom::HTMLCanvasPrintState::NotifyDone", this,
                          &HTMLCanvasPrintState::NotifyDone);
    if (NS_SUCCEEDED(NS_DispatchToCurrentThread(doneEvent))) {
      mPendingNotify = true;
    }
  }
}

void HTMLCanvasPrintState::NotifyDone() {
  mIsDone = true;
  mPendingNotify = false;
  if (mCallback) {
    mCallback->Notify(nullptr);
  }
}

// ---------------------------------------------------------------------------

HTMLCanvasElementObserver::HTMLCanvasElementObserver(
    HTMLCanvasElement* aElement)
    : mElement(aElement) {
  RegisterObserverEvents();
}

HTMLCanvasElementObserver::~HTMLCanvasElementObserver() { Destroy(); }

void HTMLCanvasElementObserver::Destroy() {
  UnregisterObserverEvents();
  mElement = nullptr;
}

void HTMLCanvasElementObserver::RegisterObserverEvents() {
  if (!mElement) {
    return;
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  MOZ_ASSERT(observerService);

  if (observerService) {
    observerService->AddObserver(this, "memory-pressure", false);
    observerService->AddObserver(this, "canvas-device-reset", false);
  }
}

void HTMLCanvasElementObserver::UnregisterObserverEvents() {
  if (!mElement) {
    return;
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  // Do not assert on observerService here. This might be triggered by
  // the cycle collector at a late enough time, that XPCOM services are
  // no longer available. See bug 1029504.
  if (observerService) {
    observerService->RemoveObserver(this, "memory-pressure");
    observerService->RemoveObserver(this, "canvas-device-reset");
  }
}

NS_IMETHODIMP
HTMLCanvasElementObserver::Observe(nsISupports*, const char* aTopic,
                                   const char16_t*) {
  if (!mElement) {
    return NS_OK;
  }

  if (strcmp(aTopic, "memory-pressure") == 0) {
    mElement->OnMemoryPressure();
  } else if (strcmp(aTopic, "canvas-device-reset") == 0) {
    mElement->OnDeviceReset();
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS(HTMLCanvasElementObserver, nsIObserver)

// ---------------------------------------------------------------------------

HTMLCanvasElement::HTMLCanvasElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)),
      mResetLayer(true),
      mMaybeModified(false),
      mWriteOnly(false) {}

HTMLCanvasElement::~HTMLCanvasElement() { Destroy(); }

void HTMLCanvasElement::Destroy() {
  if (mOffscreenDisplay) {
    UnregisterActivityObserver();
    mOffscreenDisplay->DestroyElement();
    mOffscreenDisplay = nullptr;
    mImageContainer = nullptr;
  }

  if (mContextObserver) {
    mContextObserver->Destroy();
    mContextObserver = nullptr;
  }

  ResetPrintCallback();
  if (mRequestedFrameRefreshObserver) {
    mRequestedFrameRefreshObserver->DetachFromRefreshDriver();
    mRequestedFrameRefreshObserver = nullptr;
  }
}

void HTMLCanvasElement::DestroyContent() {
  // The document is going away, so release the 2D context's buffer now instead
  // of at GC time, as accelerated canvases hold limited compositor resources.
  if (!mOffscreenCanvas && mCurrentContext) {
    mCurrentContext->OnWindowDestroy();
  }
  nsGenericHTMLElement::DestroyContent();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLCanvasElement)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLCanvasElement,
                                                nsGenericHTMLElement)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCurrentContext, mPrintCallback, mPrintState,
                                  mOriginalCanvas, mOffscreenCanvas)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLCanvasElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCurrentContext, mPrintCallback,
                                    mPrintState, mOriginalCanvas,
                                    mOffscreenCanvas)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(HTMLCanvasElement,
                                               nsGenericHTMLElement)

NS_IMPL_ELEMENT_CLONE(HTMLCanvasElement)

/* virtual */
JSObject* HTMLCanvasElement::WrapNode(JSContext* aCx,
                                      JS::Handle<JSObject*> aGivenProto) {
  return HTMLCanvasElement_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<nsICanvasRenderingContextInternal>
HTMLCanvasElement::CreateContext(CanvasContextType aContextType) {
  // Note that the compositor backend will be LAYERS_NONE if there is no widget.
  RefPtr<nsICanvasRenderingContextInternal> ret =
      CreateContextHelper(aContextType, GetCompositorBackendType());
  if (NS_WARN_IF(!ret)) {
    return nullptr;
  }

  // Add Observer for webgl canvas.
  if (aContextType == CanvasContextType::WebGL1 ||
      aContextType == CanvasContextType::WebGL2 ||
      aContextType == CanvasContextType::Canvas2D) {
    if (!mContextObserver) {
      mContextObserver = new HTMLCanvasElementObserver(this);
    }
  }

  ret->SetCanvasElement(this);
  return ret.forget();
}

void HTMLCanvasElement::AfterUpdateContext() {
  // If we have a mRequestedFrameRefreshObserver that wasn't fully registered,
  // retry that now.
  if (mRequestedFrameRefreshObserver.get() &&
      !mRequestedFrameRefreshObserver->IsRegisteredAndWatching()) {
    mRequestedFrameRefreshObserver->Register();
  }
}

CSSIntSize HTMLCanvasElement::GetWidthHeight() {
  CSSIntSize size = kFallbackIntrinsicSizeInPixels;
  const nsAttrValue* value;

  if ((value = GetParsedAttr(nsGkAtoms::width)) &&
      value->Type() == nsAttrValue::eInteger) {
    size.width = value->GetIntegerValue();
  }

  if ((value = GetParsedAttr(nsGkAtoms::height)) &&
      value->Type() == nsAttrValue::eInteger) {
    size.height = value->GetIntegerValue();
  }

  MOZ_ASSERT(size.width >= 0 && size.height >= 0,
             "we should've required <canvas> width/height attrs to be "
             "unsigned (non-negative) values");

  return size;
}

void HTMLCanvasElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                     const nsAttrValue* aValue,
                                     const nsAttrValue* aOldValue,
                                     nsIPrincipal* aSubjectPrincipal,
                                     bool aNotify) {
  AfterMaybeChangeAttr(aNamespaceID, aName, aNotify);

  return nsGenericHTMLElement::AfterSetAttr(
      aNamespaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

void HTMLCanvasElement::OnAttrSetButNotChanged(
    int32_t aNamespaceID, nsAtom* aName, const nsAttrValueOrString& aValue,
    bool aNotify) {
  AfterMaybeChangeAttr(aNamespaceID, aName, aNotify);

  return nsGenericHTMLElement::OnAttrSetButNotChanged(aNamespaceID, aName,
                                                      aValue, aNotify);
}

void HTMLCanvasElement::AfterMaybeChangeAttr(int32_t aNamespaceID,
                                             nsAtom* aName, bool aNotify) {
  if (mCurrentContext && aNamespaceID == kNameSpaceID_None &&
      (aName == nsGkAtoms::width || aName == nsGkAtoms::height ||
       aName == nsGkAtoms::moz_opaque)) {
    UpdateContext();
  }
}

void HTMLCanvasElement::HandlePrintCallback(nsPresContext* aPresContext) {
  // Only call the print callback here if 1) we're in a print testing mode or
  // print preview mode, 2) the canvas has a print callback and 3) the callback
  // hasn't already been called. For real printing the callback is handled in
  // nsPageSequenceFrame::PrePrintNextSheet.
  if ((aPresContext->Type() == nsPresContext::eContext_PageLayout ||
       aPresContext->Type() == nsPresContext::eContext_PrintPreview) &&
      !mPrintState && GetMozPrintCallback()) {
    DispatchPrintCallback(nullptr);
  }
}

nsresult HTMLCanvasElement::DispatchPrintCallback(nsITimerCallback* aCallback) {
  // For print reftests the context may not be initialized yet, so get a context
  // so mCurrentContext is set.
  if (!mCurrentContext) {
    nsresult rv;
    nsCOMPtr<nsISupports> context;
    rv = GetContext(u"2d"_ns, getter_AddRefs(context));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  mPrintState = new HTMLCanvasPrintState(this, mCurrentContext, aCallback);

  RefPtr<nsRunnableMethod<HTMLCanvasElement>> renderEvent =
      NewRunnableMethod<RefPtr<HTMLCanvasPrintState>>(
          "dom::HTMLCanvasElement::CallPrintCallback", this,
          &HTMLCanvasElement::CallPrintCallback, mPrintState);
  return OwnerDoc()->Dispatch(renderEvent.forget());
}

void HTMLCanvasElement::CallPrintCallback(
    RefPtr<HTMLCanvasPrintState> aPrintState) {
  AUTO_PROFILER_MARKER_TEXT("HTMLCanvasElement Printing", LAYOUT_Printing, {},
                            "HTMLCanvasElement::CallPrintCallback"_ns);
  MOZ_ASSERT(aPrintState,
             "Our caller should always infallibly allocate a print state, "
             "and give us a strong ref, before dispatching us");
  if (mPrintState != aPrintState) {
    // The PrintState has been cleared (and perhaps replaced with a fresh one),
    // e.g. due to the canvas frame being reconstructed. This dispatched call
    // (associated with a now-abandoned PrintState) is no longer needed.
    return;
  }
  RefPtr<PrintCallback> callback = GetMozPrintCallback();
  // Note: aPrintState is a strong reference on the stack, so it'll stay alive
  // no matter what JS runs in the callback here.
  callback->Call(*aPrintState);
}

void HTMLCanvasElement::ResetPrintCallback() {
  if (mPrintState) {
    mPrintState = nullptr;
  }
}

bool HTMLCanvasElement::IsPrintCallbackDone() {
  if (mPrintState == nullptr) {
    return true;
  }

  return mPrintState->mIsDone;
}

HTMLCanvasElement* HTMLCanvasElement::GetOriginalCanvas() {
  return mOriginalCanvas ? mOriginalCanvas.get() : this;
}

nsresult HTMLCanvasElement::CopyInnerTo(HTMLCanvasElement* aDest) {
  nsresult rv = nsGenericHTMLElement::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);
  Document* destDoc = aDest->OwnerDoc();
  if (destDoc->IsStaticDocument()) {
    // The Firefox print preview code can create a static clone from an
    // existing static clone, so we may not be the original 'canvas' element.
    aDest->mOriginalCanvas = GetOriginalCanvas();

    if (GetMozPrintCallback()) {
      destDoc->SetHasPrintCallbacks();
    }

    // We make sure that the canvas is not zero sized since that would cause
    // the DrawImage call below to return an error, which would cause printing
    // to fail.
    CSSIntSize size = GetWidthHeight();
    if (size.height > 0 && size.width > 0) {
      nsCOMPtr<nsISupports> cxt;
      aDest->GetContext(u"2d"_ns, getter_AddRefs(cxt));
      RefPtr<CanvasRenderingContext2D> context2d =
          static_cast<CanvasRenderingContext2D*>(cxt.get());
      if (context2d && !mPrintCallback) {
        CanvasImageSource source;
        source.SetAsHTMLCanvasElement() = this;
        ErrorResult err;
        context2d->DrawImage(source, 0.0, 0.0, err);
        rv = err.StealNSResult();
      }
    }
  }
  return rv;
}

nsChangeHint HTMLCanvasElement::GetAttributeChangeHint(
    const nsAtom* aAttribute, AttrModType aModType) const {
  nsChangeHint retval =
      nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);
  if (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height) {
    retval |= NS_STYLE_HINT_REFLOW;
  } else if (aAttribute == nsGkAtoms::moz_opaque) {
    retval |= NS_STYLE_HINT_VISUAL;
  }
  return retval;
}

void HTMLCanvasElement::MapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  MapAspectRatioInto(aBuilder);
  MapCommonAttributesInto(aBuilder);
}

nsMapRuleToAttributesFunc HTMLCanvasElement::GetAttributeMappingFunction()
    const {
  return &MapAttributesIntoRule;
}

bool HTMLCanvasElement::IsNoNamespaceAttrMapped(
    const nsAtom* aAttribute) const {
  static const MappedAttributeEntry attributes[] = {
      {nsGkAtoms::width}, {nsGkAtoms::height}, {nullptr}};
  static const MappedAttributeEntry* const map[] = {attributes,
                                                    sCommonAttributeMap};
  return FindAttributeDependence(aAttribute, map);
}

bool HTMLCanvasElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                       const nsAString& aValue,
                                       nsIPrincipal* aMaybeScriptedPrincipal,
                                       nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height)) {
    return aResult.ParseNonNegativeIntValue(aValue);
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

void HTMLCanvasElement::ToDataURL(JSContext* aCx, const nsAString& aType,
                                  JS::Handle<JS::Value> aParams,
                                  nsAString& aDataURL,
                                  nsIPrincipal& aSubjectPrincipal,
                                  ErrorResult& aRv) {
  bool recheckCanRead = mOffscreenDisplay && mOffscreenDisplay->HasWorkerRef();

  if (!CallerCanRead(aSubjectPrincipal)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsString dataURL;
  nsresult rv = ToDataURLImpl(aCx, aSubjectPrincipal, aType, aParams, dataURL);
  if (recheckCanRead && !CallerCanRead(aSubjectPrincipal)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (NS_FAILED(rv)) {
    aDataURL.Assign(u"data:,"_ns);
    return;
  }

  aDataURL = std::move(dataURL);
}

void HTMLCanvasElement::SetMozPrintCallback(PrintCallback* aCallback) {
  mPrintCallback = aCallback;
}

PrintCallback* HTMLCanvasElement::GetMozPrintCallback() const {
  if (mOriginalCanvas) {
    return mOriginalCanvas->GetMozPrintCallback();
  }
  return mPrintCallback;
}

static uint32_t sCaptureSourceId = 0;
class CanvasCaptureTrackSource : public MediaStreamTrackSource {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(CanvasCaptureTrackSource,
                                           MediaStreamTrackSource)

  CanvasCaptureTrackSource(nsIPrincipal* aPrincipal,
                           CanvasCaptureMediaStream* aCaptureStream)
      : MediaStreamTrackSource(
            aPrincipal, nsString(),
            TrackingId(TrackingId::Source::Canvas, sCaptureSourceId++,
                       TrackingId::TrackAcrossProcesses::Yes)),
        mCaptureStream(aCaptureStream) {}

  MediaSourceEnum GetMediaSource() const override {
    return MediaSourceEnum::Other;
  }

  bool HasAlpha() const override {
    if (!mCaptureStream || !mCaptureStream->Canvas()) {
      // In cycle-collection
      return false;
    }
    return !mCaptureStream->Canvas()->GetIsOpaque();
  }

  void GetSettings(dom::MediaTrackSettings& aResult) override {
    aResult.mWidth.Construct(mCaptureStream->Canvas()->Width());
    aResult.mHeight.Construct(mCaptureStream->Canvas()->Height());
  }

  void Stop() override {
    if (!mCaptureStream) {
      return;
    }

    mCaptureStream->StopCapture();
  }

  void Disable() override {}

  void Enable() override {}

 private:
  virtual ~CanvasCaptureTrackSource() = default;

  RefPtr<CanvasCaptureMediaStream> mCaptureStream;
};

NS_IMPL_ADDREF_INHERITED(CanvasCaptureTrackSource, MediaStreamTrackSource)
NS_IMPL_RELEASE_INHERITED(CanvasCaptureTrackSource, MediaStreamTrackSource)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CanvasCaptureTrackSource)
NS_INTERFACE_MAP_END_INHERITING(MediaStreamTrackSource)
NS_IMPL_CYCLE_COLLECTION_INHERITED(CanvasCaptureTrackSource,
                                   MediaStreamTrackSource, mCaptureStream)

already_AddRefed<CanvasCaptureMediaStream> HTMLCanvasElement::CaptureStream(
    const Optional<double>& aFrameRate, nsIPrincipal& aSubjectPrincipal,
    ErrorResult& aRv) {
  if (IsWriteOnly()) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
  if (!window) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Check if we transferred the OffscreenCanvas to a DOM worker. This is not
  // defined by the spec yet, so it is better to fail now than implement
  // something not compliant:
  // https://github.com/w3c/mediacapture-fromelement/issues/65
  // https://github.com/w3c/mediacapture-extensions/pull/26
  // https://github.com/web-platform-tests/wpt/issues/21102
  if (mOffscreenDisplay &&
      NS_WARN_IF(!mOffscreenDisplay->CanElementCaptureStream())) {
    aRv.ThrowNotSupportedError(
        "Capture stream not supported when OffscreenCanvas transferred to "
        "worker");
    return nullptr;
  }

  auto stream = MakeRefPtr<CanvasCaptureMediaStream>(window, this);

  nsCOMPtr<nsIPrincipal> principal = NodePrincipal();
  nsresult rv = stream->Init(aFrameRate, principal);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return nullptr;
  }

  RefPtr<MediaStreamTrack> track =
      new VideoStreamTrack(window, stream->GetSourceStream(),
                           new CanvasCaptureTrackSource(principal, stream));
  stream->AddTrackInternal(track);

  // Check site-specific permission and display prompt if appropriate.
  // If no permission, arrange for the frame capture listener to return
  // all-white, opaque image data.
  CanvasUtils::ImageExtraction extractionBehaviour =
      CanvasUtils::ImageExtractionResult(
          this, nsContentUtils::GetCurrentJSContext(), &aSubjectPrincipal);

  rv = RegisterFrameCaptureListener(
      stream->FrameCaptureListener(),
      extractionBehaviour == CanvasUtils::ImageExtraction::Placeholder);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return nullptr;
  }

  return stream.forget();
}

nsresult HTMLCanvasElement::ExtractData(JSContext* aCx,
                                        nsIPrincipal& aSubjectPrincipal,
                                        nsAString& aType,
                                        const nsAString& aOptions,
                                        nsIInputStream** aStream) {
  // Check site-specific permission and display prompt if appropriate.
  // If no permission, return all-white, opaque image data.
  CanvasUtils::ImageExtraction extractionBehaviour =
      CanvasUtils::ImageExtractionResult(this, aCx, &aSubjectPrincipal);

  if (extractionBehaviour != CanvasUtils::ImageExtraction::Placeholder) {
    auto size = GetWidthHeight();
    auto usage = CanvasUsage::CreateUsage(false, GetCurrentContextType(),
                                          CanvasExtractionAPI::ToDataURL, size,
                                          GetCurrentContext());
    OwnerDoc()->RecordCanvasUsage(usage);
  }

  nsCString randomizationKey = VoidCString();
  if (extractionBehaviour == CanvasUtils::ImageExtraction::EfficientRandomize) {
    nsRFPService::GetFingerprintingRandomizationKeyAsString(
        GetCookieJarSettings(), randomizationKey);
  }

  return ImageEncoder::ExtractData(aType, aOptions, GetSize(),
                                   extractionBehaviour, randomizationKey,
                                   mCurrentContext, mOffscreenDisplay, aStream);
}

nsresult HTMLCanvasElement::ToDataURLImpl(JSContext* aCx,
                                          nsIPrincipal& aSubjectPrincipal,
                                          const nsAString& aMimeType,
                                          const JS::Value& aEncoderOptions,
                                          nsAString& aDataURL) {
  CSSIntSize size = GetWidthHeight();
  if (size.height == 0 || size.width == 0) {
    aDataURL = u"data:,"_ns;
    return NS_OK;
  }

  nsAutoString type;
  nsContentUtils::ASCIIToLower(aMimeType, type);

  nsAutoString params;
  bool usingCustomParseOptions;
  nsresult rv =
      ParseParams(aCx, type, aEncoderOptions, params, &usingCustomParseOptions);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIInputStream> stream;
  rv =
      ExtractData(aCx, aSubjectPrincipal, type, params, getter_AddRefs(stream));

  // If there are unrecognized custom parse options, we should fall back to
  // the default values for the encoder without any options at all.
  if (rv == NS_ERROR_INVALID_ARG && usingCustomParseOptions) {
    rv = ExtractData(aCx, aSubjectPrincipal, type, u""_ns,
                     getter_AddRefs(stream));
  }

  NS_ENSURE_SUCCESS(rv, rv);

  // build data URL string
  aDataURL = u"data:"_ns + type + u";base64,"_ns;

  uint64_t count;
  rv = stream->Available(&count);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(count <= UINT32_MAX, NS_ERROR_FILE_TOO_BIG);

  return Base64EncodeInputStream(stream, aDataURL, (uint32_t)count,
                                 aDataURL.Length());
}

UniquePtr<uint8_t[]> HTMLCanvasElement::GetImageBuffer(
    CanvasUtils::ImageExtraction aExtractionBehavior, int32_t* aOutFormat,
    gfx::IntSize* aOutImageSize) {
  if (mCurrentContext) {
    return mCurrentContext->GetImageBuffer(aExtractionBehavior, aOutFormat,
                                           aOutImageSize);
  }
  if (mOffscreenDisplay) {
    return mOffscreenDisplay->GetImageBuffer(aExtractionBehavior, aOutFormat,
                                             aOutImageSize);
  }
  return nullptr;
}

void HTMLCanvasElement::ToBlob(JSContext* aCx, BlobCallback& aCallback,
                               const nsAString& aType,
                               JS::Handle<JS::Value> aParams,
                               nsIPrincipal& aSubjectPrincipal,
                               ErrorResult& aRv) {
  bool recheckCanRead = mOffscreenDisplay && mOffscreenDisplay->HasWorkerRef();

  if (!CallerCanRead(aSubjectPrincipal)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsCOMPtr<nsIGlobalObject> global = OwnerDoc()->GetScopeObject();
  MOZ_ASSERT(global);

  CSSIntSize elemSize = GetWidthHeight();
  if (elemSize.width == 0 || elemSize.height == 0) {
    // According to spec, blob should return null if either its horizontal
    // dimension or its vertical dimension is zero. See link below.
    // https://html.spec.whatwg.org/multipage/scripting.html#dom-canvas-toblob
    OwnerDoc()->Dispatch(NewRunnableMethod<Blob*, const char*>(
        "dom::HTMLCanvasElement::ToBlob", &aCallback,
        static_cast<void (BlobCallback::*)(Blob*, const char*)>(
            &BlobCallback::Call),
        nullptr, nullptr));
    return;
  }

  // Check site-specific permission and display prompt if appropriate.
  // If no permission, return all-white, opaque image data.
  CanvasUtils::ImageExtraction extractionBehaviour =
      CanvasUtils::ImageExtractionResult(this, aCx, &aSubjectPrincipal);

  // Encoder callback when encoding is complete.
  class EncodeCallback : public EncodeCompleteCallback {
   public:
    EncodeCallback(nsIGlobalObject* aGlobal, BlobCallback* aCallback,
                   OffscreenCanvasDisplayHelper* aOffscreenDisplay,
                   nsIPrincipal* aSubjectPrincipal)
        : mGlobal(aGlobal),
          mBlobCallback(aCallback),
          mOffscreenDisplay(aOffscreenDisplay),
          mSubjectPrincipal(aSubjectPrincipal) {}

    // This is called on main thread.
    MOZ_CAN_RUN_SCRIPT
    nsresult ReceiveBlobImpl(already_AddRefed<BlobImpl> aBlobImpl) override {
      MOZ_ASSERT(NS_IsMainThread());

      RefPtr<BlobImpl> blobImpl = aBlobImpl;

      RefPtr<Blob> blob;

      if (blobImpl && (!mOffscreenDisplay ||
                       mOffscreenDisplay->CallerCanRead(*mSubjectPrincipal))) {
        blob = Blob::Create(mGlobal, blobImpl);
      }

      RefPtr<BlobCallback> callback(std::move(mBlobCallback));
      ErrorResult rv;

      callback->Call(blob, rv);

      mGlobal = nullptr;
      MOZ_ASSERT(!mBlobCallback);

      return rv.StealNSResult();
    }

    bool CanBeDeletedOnAnyThread() override {
      // EncodeCallback is used from the main thread only.
      return false;
    }

    nsCOMPtr<nsIGlobalObject> mGlobal;
    RefPtr<BlobCallback> mBlobCallback;
    RefPtr<OffscreenCanvasDisplayHelper> mOffscreenDisplay;
    RefPtr<nsIPrincipal> mSubjectPrincipal;
  };

  RefPtr<EncodeCompleteCallback> callback = new EncodeCallback(
      global, &aCallback, recheckCanRead ? mOffscreenDisplay.get() : nullptr,
      recheckCanRead ? &aSubjectPrincipal : nullptr);

  auto usage = CanvasUsage::CreateUsage(false, GetCurrentContextType(),
                                        CanvasExtractionAPI::ToBlob,
                                        GetWidthHeight(), GetCurrentContext());
  if (extractionBehaviour != CanvasUtils::ImageExtraction::Placeholder) {
    OwnerDoc()->RecordCanvasUsage(usage);
  }

  CanvasRenderingContextHelper::ToBlob(aCx, callback, aType, aParams,
                                       extractionBehaviour, aRv);
}

OffscreenCanvas* HTMLCanvasElement::TransferControlToOffscreen(
    ErrorResult& aRv) {
  if (mCurrentContext || mOffscreenCanvas) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  MOZ_ASSERT(!mOffscreenDisplay);

  nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();
  if (!win) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  LayersBackend backend = LayersBackend::LAYERS_NONE;
  if (nsIWidget* docWidget = nsContentUtils::WidgetForDocument(OwnerDoc())) {
    if (WindowRenderer* renderer = docWidget->GetWindowRenderer()) {
      backend = renderer->GetCompositorBackendType();
    }
  }

  CSSIntSize sz = GetWidthHeight();
  mOffscreenDisplay =
      MakeRefPtr<OffscreenCanvasDisplayHelper>(this, sz.width, sz.height);
  RegisterActivityObserver();
  NotifyOwnerDocumentActivityChanged();
  mOffscreenCanvas = new OffscreenCanvas(win->AsGlobal(), sz.width, sz.height,
                                         backend, do_AddRef(mOffscreenDisplay),
                                         FragmentOrElement::GetLang());
  if (mWriteOnly) {
    mOffscreenCanvas->SetWriteOnly(mExpandedReader);
  }

  if (!mContextObserver) {
    mContextObserver = new HTMLCanvasElementObserver(this);
  }

  return mOffscreenCanvas;
}

nsresult HTMLCanvasElement::GetContext(const nsAString& aContextId,
                                       nsISupports** aContext) {
  ErrorResult rv;
  mMaybeModified = true;  // For FirstContentfulPaint
  *aContext = GetContext(nullptr, aContextId, JS::NullHandleValue, rv).take();
  return rv.StealNSResult();
}

already_AddRefed<nsISupports> HTMLCanvasElement::GetContext(
    JSContext* aCx, const nsAString& aContextId,
    JS::Handle<JS::Value> aContextOptions, ErrorResult& aRv) {
  CanvasContextType contextType;
  if (!CanvasUtils::GetCanvasContextType(aContextId, &contextType)) {
    return nullptr;
  }
  CanvasRenderingContextOptions options;
  if (!options.Init(aCx, contextType, aContextOptions, aRv)) {
    return nullptr;
  }
  if (mOffscreenCanvas) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }
  mMaybeModified = true;  // For FirstContentfulPaint
  if (!CanvasRenderingContextHelper::GetOrCreateContext(contextType, options,
                                                        aRv)) {
    return nullptr;
  }
  return do_AddRef(mCurrentContext);
}

CSSIntSize HTMLCanvasElement::GetSize() { return GetWidthHeight(); }

bool HTMLCanvasElement::IsWriteOnly() const {
  if (mOffscreenDisplay && mOffscreenDisplay->IsWriteOnly()) {
    return true;
  }
  return mWriteOnly;
}

void HTMLCanvasElement::SetWriteOnly(
    nsIPrincipal* aExpandedReader /* = nullptr */) {
  mExpandedReader = aExpandedReader;
  mWriteOnly = true;
  if (mOffscreenCanvas) {
    mOffscreenCanvas->SetWriteOnly(aExpandedReader);
  }
}

bool HTMLCanvasElement::CallerCanRead(nsIPrincipal& aPrincipal) const {
  if (mOffscreenDisplay && !mOffscreenDisplay->CallerCanRead(aPrincipal)) {
    return false;
  }

  if (!mWriteOnly) {
    return true;
  }

  // If mExpandedReader is set, this canvas was tainted only by
  // mExpandedReader's resources. So allow reading if the subject
  // principal subsumes mExpandedReader.
  if (mExpandedReader && aPrincipal.Subsumes(mExpandedReader)) {
    return true;
  }

  return nsContentUtils::PrincipalHasPermission(aPrincipal,
                                                nsGkAtoms::all_urlsPermission);
}

void HTMLCanvasElement::SetWidth(uint32_t aWidth, ErrorResult& aRv) {
  if (mOffscreenCanvas) {
    aRv.ThrowInvalidStateError(
        "Cannot set width of placeholder canvas transferred to "
        "OffscreenCanvas.");
    return;
  }

  SetUnsignedIntAttr(nsGkAtoms::width, aWidth, kFallbackIntrinsicWidthInPixels,
                     aRv);
}

void HTMLCanvasElement::SetHeight(uint32_t aHeight, ErrorResult& aRv) {
  if (mOffscreenCanvas) {
    aRv.ThrowInvalidStateError(
        "Cannot set height of placeholder canvas transferred to "
        "OffscreenCanvas.");
    return;
  }

  SetUnsignedIntAttr(nsGkAtoms::height, aHeight,
                     kFallbackIntrinsicHeightInPixels, aRv);
}

void HTMLCanvasElement::SetSize(const nsIntSize& aSize, ErrorResult& aRv) {
  if (mOffscreenCanvas) {
    aRv.ThrowInvalidStateError(
        "Cannot set width of placeholder canvas transferred to "
        "OffscreenCanvas.");
    return;
  }

  if (NS_WARN_IF(aSize.IsEmpty())) {
    aRv.ThrowRangeError("Canvas size is empty, must be non-empty.");
    return;
  }

  SetUnsignedIntAttr(nsGkAtoms::width, aSize.width,
                     kFallbackIntrinsicWidthInPixels, aRv);
  MOZ_ASSERT(!aRv.Failed());
  SetUnsignedIntAttr(nsGkAtoms::height, aSize.height,
                     kFallbackIntrinsicHeightInPixels, aRv);
  MOZ_ASSERT(!aRv.Failed());
}

void HTMLCanvasElement::FlushOffscreenCanvas() {
  if (mOffscreenDisplay) {
    mOffscreenDisplay->FlushForDisplay();
  }
}

void HTMLCanvasElement::NotifyOwnerDocumentActivityChanged() {
  if (!mOffscreenDisplay) {
    return;
  }

  BrowsingContext* bc = OwnerDoc()->GetBrowsingContext();
  mOffscreenDisplay->SetPresentationEnabled(bc && bc->IsActive());
}

void HTMLCanvasElement::NodeInfoChanged(Document* aOldDoc) {
  nsGenericHTMLElement::NodeInfoChanged(aOldDoc);

  // The activity observer registration itself is moved over by nsINode::Adopt.
  NotifyOwnerDocumentActivityChanged();
}

void HTMLCanvasElement::InvalidateCanvasPlaceholder(uint32_t aWidth,
                                                    uint32_t aHeight) {
  ErrorResult rv;
  SetUnsignedIntAttr(nsGkAtoms::width, aWidth, kFallbackIntrinsicWidthInPixels,
                     rv);
  MOZ_ASSERT(!rv.Failed());
  SetUnsignedIntAttr(nsGkAtoms::height, aHeight,
                     kFallbackIntrinsicHeightInPixels, rv);
  MOZ_ASSERT(!rv.Failed());
}

static bool InvalidateCanvasData(nsIFrame* aFrame, uint32_t aKey) {
  RefPtr data = GetWebRenderUserData<WebRenderCanvasData>(aFrame, aKey);
  if (!data) {
    return false;
  }
  CanvasRenderer* renderer = data->GetCanvasRenderer();
  if (!renderer) {
    return false;
  }
  renderer->SetDirty();
  aFrame->SchedulePaint(nsIFrame::PAINT_COMPOSITE_ONLY);
  return true;
}

void HTMLCanvasElement::InvalidateCanvasContent(const gfx::Rect* damageRect) {
  // Cache the current ImageContainer to avoid contention on the mutex.
  if (mOffscreenDisplay) {
    mImageContainer = mOffscreenDisplay->GetImageContainer();
  }

  // We don't need to flush anything here; if there's no frame or if
  // we plan to reframe we don't need to invalidate it anyway.
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return;
  }

  // When using layers-free WebRender, we cannot invalidate the layer (because
  // there isn't one). Instead, we mark the CanvasRenderer dirty and scheduling
  // an empty transaction which is effectively equivalent.
  bool invalidated = false;
  for (auto* item : frame->DisplayItems()) {
    if (item->GetType() == DisplayItemType::TYPE_CANVAS) {
      invalidated |= InvalidateCanvasData(frame, item->GetPerFrameKey());
    }
  }
  invalidated =
      invalidated ||
      InvalidateCanvasData(frame, uint32_t(DisplayItemType::TYPE_CANVAS));
  if (!invalidated) {
    if (damageRect) {
      CSSIntSize size = GetWidthHeight();
      if (size.width != 0 && size.height != 0) {
        gfx::IntRect invalRect = gfx::IntRect::Truncate(*damageRect);
        frame->InvalidateLayer(DisplayItemType::TYPE_CANVAS, &invalRect);
      }
    } else {
      frame->InvalidateLayer(DisplayItemType::TYPE_CANVAS);
    }

    // This path is taken in two situations:
    // 1) WebRender is enabled and has not yet processed a display list.
    // 2) WebRender is disabled and layer invalidation failed.
    // In both cases, schedule a full paint to properly update canvas.
    frame->SchedulePaint(nsIFrame::PAINT_DEFAULT, false);
  }

  /*
   * Treat canvas invalidations as animation activity for JS. Frequently
   * invalidating a canvas will feed into heuristics and cause JIT code to be
   * kept around longer, for smoother animations.
   */
  if (nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow()) {
    if (JSObject* obj = win->AsGlobal()->GetGlobalJSObject()) {
      js::NotifyAnimationActivity(obj);
    }
  }
}

void HTMLCanvasElement::InvalidateCanvas() {
  // We don't need to flush anything here; if there's no frame or if
  // we plan to reframe we don't need to invalidate it anyway.
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) return;

  frame->InvalidateFrame();
}

bool HTMLCanvasElement::GetIsOpaque() {
  if (mCurrentContext) {
    return mCurrentContext->GetIsOpaque();
  }

  return GetOpaqueAttr();
}

bool HTMLCanvasElement::GetOpaqueAttr() {
  return HasAttr(nsGkAtoms::moz_opaque);
}

CanvasContextType HTMLCanvasElement::GetCurrentContextType() {
  if (mCurrentContextType == CanvasContextType::NoContext &&
      mOffscreenDisplay) {
    mCurrentContextType = mOffscreenDisplay->GetContextType();
  }
  return mCurrentContextType;
}

already_AddRefed<Image> HTMLCanvasElement::GetAsImage() {
  if (mOffscreenDisplay) {
    return mOffscreenDisplay->GetAsImage();
  }

  if (mCurrentContext) {
    return mCurrentContext->GetAsImage();
  }

  return nullptr;
}

bool HTMLCanvasElement::UpdateWebRenderCanvasData(
    nsDisplayListBuilder* aBuilder, WebRenderCanvasData* aCanvasData) {
  MOZ_ASSERT(!mOffscreenDisplay);

  if (mCurrentContext) {
    return mCurrentContext->UpdateWebRenderCanvasData(aBuilder, aCanvasData);
  }

  // Clear CanvasRenderer of WebRenderCanvasData
  aCanvasData->ClearCanvasRenderer();
  return false;
}

bool HTMLCanvasElement::InitializeCanvasRenderer(nsDisplayListBuilder* aBuilder,
                                                 CanvasRenderer* aRenderer) {
  MOZ_ASSERT(!mOffscreenDisplay);

  if (mCurrentContext) {
    return mCurrentContext->InitializeCanvasRenderer(aBuilder, aRenderer);
  }

  return false;
}

void HTMLCanvasElement::MarkContextClean() {
  if (!mCurrentContext) return;

  mCurrentContext->MarkContextClean();
}

void HTMLCanvasElement::MarkContextCleanForFrameCapture() {
  if (!mCurrentContext) return;

  mCurrentContext->MarkContextCleanForFrameCapture();
}

Watchable<FrameCaptureState>* HTMLCanvasElement::GetFrameCaptureState() {
  if (!mCurrentContext) {
    return nullptr;
  }
  return mCurrentContext->GetFrameCaptureState();
}

nsresult HTMLCanvasElement::RegisterFrameCaptureListener(
    FrameCaptureListener* aListener, bool aReturnPlaceholderData) {
  WeakPtr<FrameCaptureListener> listener = aListener;

  if (mRequestedFrameListeners.Contains(listener)) {
    return NS_OK;
  }

  if (!mRequestedFrameRefreshObserver) {
    PresShell* shell = nsContentUtils::FindPresShellForDocument(OwnerDoc());
    if (NS_WARN_IF(!shell)) {
      return NS_ERROR_FAILURE;
    }

    nsPresContext* context = shell->GetPresContext();
    if (NS_WARN_IF(!context)) {
      return NS_ERROR_FAILURE;
    }

    context = context->GetRootPresContext();
    if (NS_WARN_IF(!context)) {
      return NS_ERROR_FAILURE;
    }

    nsRefreshDriver* driver = context->RefreshDriver();
    MOZ_ASSERT(driver);

    mRequestedFrameRefreshObserver =
        new RequestedFrameRefreshObserver(this, driver, aReturnPlaceholderData);
  } else {
    mRequestedFrameRefreshObserver->SetReturnPlaceholderData(
        aReturnPlaceholderData);
  }

  mRequestedFrameListeners.AppendElement(listener);
  mRequestedFrameRefreshObserver->Register();
  return NS_OK;
}

bool HTMLCanvasElement::IsFrameCaptureRequested(const TimeStamp& aTime) const {
  for (const WeakPtr<FrameCaptureListener>& listener :
       mRequestedFrameListeners) {
    if (!listener) {
      continue;
    }

    if (listener->FrameCaptureRequested(aTime)) {
      return true;
    }
  }
  return false;
}

void HTMLCanvasElement::ProcessDestroyedFrameListeners() {
  // Remove destroyed listeners from the list.
  mRequestedFrameListeners.RemoveElementsBy(
      [](const auto& weakListener) { return !weakListener; });

  if (mRequestedFrameListeners.IsEmpty()) {
    mRequestedFrameRefreshObserver->Unregister();
  }
}

void HTMLCanvasElement::SetFrameCapture(
    already_AddRefed<SourceSurface> aSurface, const TimeStamp& aTime) {
  RefPtr<SourceSurface> surface = aSurface;
  RefPtr<SourceSurfaceImage> image =
      new SourceSurfaceImage(surface->GetSize(), surface);

  for (const WeakPtr<FrameCaptureListener>& listener :
       mRequestedFrameListeners) {
    if (!listener) {
      continue;
    }

    RefPtr<Image> imageRefCopy = image.get();
    listener->NewFrame(imageRefCopy.forget(), aTime);
  }
}

already_AddRefed<SourceSurface> HTMLCanvasElement::GetSurfaceSnapshot(
    gfxAlphaType* const aOutAlphaType, DrawTarget* aTarget) {
  if (mCurrentContext) {
    return mCurrentContext->GetOptimizedSnapshot(aTarget, aOutAlphaType);
  } else if (mOffscreenDisplay) {
    return mOffscreenDisplay->GetSurfaceSnapshot();
  }
  return nullptr;
}

RefPtr<HTMLCanvasElement::SurfaceSnapshotPromise>
HTMLCanvasElement::GetSurfaceSnapshotAsync() {
  if (mCurrentContext && mCurrentContext->SupportAsyncSnapshot()) {
    return mCurrentContext->GetSurfaceSnapshotAsync();
  }

  RefPtr<gfx::SourceSurface> surface = GetSurfaceSnapshot();
  if (!surface) {
    return SurfaceSnapshotPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  return SurfaceSnapshotPromise::CreateAndResolve(std::move(surface), __func__);
}

layers::LayersBackend HTMLCanvasElement::GetCompositorBackendType() const {
  nsIWidget* docWidget = nsContentUtils::WidgetForDocument(OwnerDoc());
  if (docWidget) {
    WindowRenderer* renderer = docWidget->GetWindowRenderer();
    if (renderer) {
      return renderer->GetCompositorBackendType();
    }
  }

  return LayersBackend::LAYERS_NONE;
}

void HTMLCanvasElement::OnMemoryPressure() {
  // FIXME(aosmond): We need to implement memory pressure handling for
  // OffscreenCanvas when it is on worker threads. See bug 1746260.

  if (mCurrentContext) {
    mCurrentContext->OnMemoryPressure();
  }
}

void HTMLCanvasElement::OnDeviceReset() {
  if (!mOffscreenCanvas && mCurrentContext) {
    mCurrentContext->ResetBitmap();
  }
}

ClientWebGLContext* HTMLCanvasElement::GetWebGLContext() {
  if (GetCurrentContextType() != CanvasContextType::WebGL1 &&
      GetCurrentContextType() != CanvasContextType::WebGL2) {
    return nullptr;
  }

  return static_cast<ClientWebGLContext*>(GetCurrentContext());
}

webgpu::CanvasContext* HTMLCanvasElement::GetWebGPUContext() {
  if (GetCurrentContextType() != CanvasContextType::WebGPU) {
    return nullptr;
  }

  return static_cast<webgpu::CanvasContext*>(GetCurrentContext());
}

}  // namespace mozilla::dom
