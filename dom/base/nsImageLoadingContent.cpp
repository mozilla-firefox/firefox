/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A base class which implements nsIImageLoadingContent and can be
 * subclassed by various content nodes that want to provide image
 * loading functionality (eg <img>, <object>, etc).
 */

#include "nsImageLoadingContent.h"

#include "Orientation.h"
#include "imgIContainer.h"
#include "imgLoader.h"
#include "imgRequestProxy.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/PageloadEvent.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGImageFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/StaticPrefs_svg.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/ImageTextBinding.h"
#include "mozilla/dom/LargestContentfulPaint.h"
#include "mozilla/dom/PContent.h"  // For TextRecognitionResult
#include "mozilla/dom/PageLoadEventUtils.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ResponsiveImageSelector.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/widget/TextRecognition.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIChannel.h"
#include "nsIContent.h"
#include "nsIContentPolicy.h"
#include "nsIFrame.h"
#include "nsIScriptGlobalObject.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsImageFrame.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

#ifdef LoadImage
// Undefine LoadImage to prevent naming conflict with Windows.
#  undef LoadImage
#endif

using namespace mozilla;
using namespace mozilla::dom;

#ifdef DEBUG_chb
static void PrintReqURL(imgIRequest* req) {
  if (!req) {
    printf("(null req)\n");
    return;
  }

  nsCOMPtr<nsIURI> uri;
  req->GetURI(getter_AddRefs(uri));
  if (!uri) {
    printf("(null uri)\n");
    return;
  }

  nsAutoCString spec;
  uri->GetSpec(spec);
  printf("spec='%s'\n", spec.get());
}
#endif /* DEBUG_chb */

class ImageLoadTask : public MicroTaskRunnable {
 public:
  ImageLoadTask(nsImageLoadingContent* aElement, bool aAlwaysLoad,
                bool aUseUrgentStartForChannel)
      : mElement(aElement),
        mDocument(aElement->AsContent()->OwnerDoc()),
        mAlwaysLoad(aAlwaysLoad),
        mUseUrgentStartForChannel(aUseUrgentStartForChannel) {
    mDocument->BlockOnload();
  }

  void Run(AutoSlowOperation& aAso) override {
    if (mElement->RequireImageLoadData().mPendingImageLoadTask == this) {
      JSCallingLocation::AutoFallback fallback(&mCallingLocation);
      mElement->mUseUrgentStartForChannel = mUseUrgentStartForChannel;
      mElement->ClearImageLoadTask();
      mElement->LoadSelectedImage(mAlwaysLoad, /* aStopLazyLoading = */ false);
    }
    mDocument->UnblockOnload(false);
  }

  bool Suppressed() override {
    nsIGlobalObject* global = mElement->AsContent()->GetRelevantGlobal();
    return global && global->IsInSyncOperation();
  }

  bool AlwaysLoad() const { return mAlwaysLoad; }

 private:
  ~ImageLoadTask() = default;
  const RefPtr<nsImageLoadingContent> mElement;
  const RefPtr<dom::Document> mDocument;
  const JSCallingLocation mCallingLocation{JSCallingLocation::Get()};
  const bool mAlwaysLoad;
  // True if we want to set nsIClassOfService::UrgentStart to the channel to get
  // the response ASAP for better user responsiveness.
  const bool mUseUrgentStartForChannel;
};

nsImageLoadingContent::nsImageLoadingContent() = default;

void nsImageLoadingContent::CreateImageLoadData() {
  MOZ_ASSERT(!mImageLoadData);
  mImageLoadData = std::make_unique<ImageLoadData>();
  if (!nsContentUtils::GetImgLoaderForChannel(nullptr, nullptr)) {
    mLoadingEnabled = false;
  }
}

void nsImageLoadingContent::Destroy() {
  if (!mImageLoadData) {
    return;
  }
  // Cancel our requests so they won't hold stale refs to us
  // NB: Don't ask to discard the images here.
  RejectDecodePromises(NS_ERROR_DOM_IMAGE_INVALID_REQUEST);
  ClearCurrentRequest(NS_BINDING_ABORTED);
  ClearPendingRequest(NS_BINDING_ABORTED);
}

nsImageLoadingContent::~nsImageLoadingContent() {
  if (!mImageLoadData) {
    return;
  }
  MOZ_ASSERT(
      !mImageLoadData->mCurrentRequest && !mImageLoadData->mPendingRequest,
      "Destroy not called");
  MOZ_ASSERT(!mImageLoadData->mObserverList.mObserver &&
                 !mImageLoadData->mObserverList.mNext,
             "Observers still registered?");
  MOZ_ASSERT(mImageLoadData->mScriptedObservers.IsEmpty(),
             "Scripted observers still registered?");
  MOZ_ASSERT(mImageLoadData->mOutstandingDecodePromises == 0,
             "Decode promises still unfulfilled?");
  MOZ_ASSERT(mImageLoadData->mDecodePromises.IsEmpty(),
             "Decode promises still unfulfilled?");
}

void nsImageLoadingContent::QueueImageTask(
    nsIURI* aSrcURI, nsIPrincipal* aSrcTriggeringPrincipal, bool aForceAsync,
    bool aAlwaysLoad, bool aNotify) {
  // If loading is temporarily disabled, we don't want to queue tasks that may
  // then run when loading is re-enabled.
  // Roughly step 1 and 2.
  // FIXME(emilio): Would be great to do this more per-spec. We don't cancel
  // existing loads etc.
  if (!LoadingEnabled() || !GetOurOwnerDoc()->ShouldLoadImages()) {
    return;
  }

  // Ensure that we don't overwrite a previous load request that requires
  // a complete load to occur.
  const bool alwaysLoad = aAlwaysLoad || HasPendingAlwaysLoadImageTask();

  // Steps 5 and 7 (sync cache check for src).
  const bool shouldLoadSync = [&] {
    if (aForceAsync) {
      return false;
    }
    if (!aSrcURI) {
      // NOTE(emilio): we need to also do a sync check for empty / invalid src,
      // see https://github.com/whatwg/html/issues/2429
      // But do it sync only when there's a current request.
      return mImageLoadData && mImageLoadData->mCurrentRequest;
    }
    if (AsContent()->IsSVGElement()) {
      if (GetOurOwnerDoc()->IsBeingUsedAsImage()) {
        return true;
      }
      if (StaticPrefs::svg_image_element_force_sync_load()) {
        return true;
      }
    }
    return nsContentUtils::IsImageAvailable(
        AsContent(), aSrcURI, aSrcTriggeringPrincipal, GetCORSMode());
  }();

  if (shouldLoadSync) {
    if (!nsContentUtils::IsSafeToRunScript()) {
      // If not safe to run script, we should do the sync load task as soon as
      // possible instead. This prevents unsound state changes from frame
      // construction and such.
      void (nsImageLoadingContent::*fp)(nsIURI*, nsIPrincipal*, bool, bool,
                                        bool) =
          &nsImageLoadingContent::QueueImageTask;
      nsContentUtils::AddScriptRunner(
          NewRunnableMethod<nsIURI*, nsIPrincipal*, bool, bool, bool>(
              "nsImageLoadingContent::QueueImageTask", this, fp, aSrcURI,
              aSrcTriggeringPrincipal, aForceAsync, aAlwaysLoad,
              /* aNotify = */ true));
      return;
    }

    ClearImageLoadTask();
    LoadSelectedImage(alwaysLoad, mLazyLoading && aSrcURI);
    return;
  }

  if (mLazyLoading) {
    // This check is not in the spec, but it is just a performance optimization.
    // The reasoning for why it is sound is that we early-return from the image
    // task when lazy loading, and that StopLazyLoading makes us queue a new
    // task (which will implicitly cancel all the pre-existing tasks).
    return;
  }

  RefPtr task = new ImageLoadTask(this, alwaysLoad, mUseUrgentStartForChannel);
  EnsureImageLoadData().mPendingImageLoadTask = task;
  // We might have just become non-broken.
  UpdateImageState(aNotify);
  // The task checks this to determine if it was the last queued event, and so
  // earlier tasks are implicitly canceled.
  CycleCollectedJSContext::Get()->DispatchToMicroTask(task.forget());
}

void nsImageLoadingContent::ClearImageLoadTask() {
  if (mImageLoadData) {
    mImageLoadData->mPendingImageLoadTask = nullptr;
  }
}

/*
 * imgINotificationObserver impl
 */
void nsImageLoadingContent::Notify(imgIRequest* aRequest, int32_t aType,
                                   const nsIntRect* aData) {
  ImageLoadData& loadData = RequireImageLoadData();
  MOZ_ASSERT(aRequest, "no request?");
  MOZ_ASSERT(aRequest == loadData.mCurrentRequest ||
                 aRequest == loadData.mPendingRequest,
             "Forgot to cancel a previous request?");

  if (aType == imgINotificationObserver::IS_ANIMATED) {
    return OnImageIsAnimated(aRequest);
  }

  if (aType == imgINotificationObserver::UNLOCKED_DRAW) {
    return OnUnlockedDraw();
  }

  {
    // Calling Notify on observers can modify the list of observers so make
    // a local copy.
    AutoTArray<nsCOMPtr<imgINotificationObserver>, 2> observers;
    for (ImageObserver *observer = &loadData.mObserverList, *next; observer;
         observer = next) {
      next = observer->mNext;
      if (observer->mObserver) {
        observers.AppendElement(observer->mObserver);
      }
    }

    nsAutoScriptBlocker scriptBlocker;

    for (auto& observer : observers) {
      observer->Notify(aRequest, aType, aData);
    }
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    uint32_t reqStatus;
    aRequest->GetImageStatus(&reqStatus);
    /* triage STATUS_ERROR */
    if (reqStatus & imgIRequest::STATUS_ERROR) {
      nsresult errorCode = NS_OK;
      aRequest->GetImageErrorCode(&errorCode);

      /* Handle image not loading error because source was a tracking URL (or
       * fingerprinting, cryptomining, etc).
       * We make a note of this image node by including it in a dedicated
       * array of blocked tracking nodes under its parent document.
       */
      if (net::ChannelClassifierUtils::IsClassifierBlockingErrorCode(
              errorCode)) {
        Document* doc = GetOurOwnerDoc();
        doc->AddBlockedNodeByClassifier(AsContent());
      }
    }
    return OnLoadComplete(aRequest, reqStatus);
  }

  if ((aType == imgINotificationObserver::FRAME_COMPLETE ||
       aType == imgINotificationObserver::FRAME_UPDATE) &&
      loadData.mCurrentRequest == aRequest) {
    MaybeResolveDecodePromises();
  }

  if (aType == imgINotificationObserver::DECODE_COMPLETE) {
    nsCOMPtr<imgIContainer> container;
    aRequest->GetImage(getter_AddRefs(container));
    if (container) {
      container->PropagateUseCounters(GetOurOwnerDoc());
    }
    UpdateImageState(true);
  }
}

bool nsImageLoadingContent::HasPendingAlwaysLoadImageTask() const {
  return mImageLoadData && mImageLoadData->mPendingImageLoadTask &&
         mImageLoadData->mPendingImageLoadTask->AlwaysLoad();
}

void nsImageLoadingContent::OnLoadComplete(imgIRequest* aRequest,
                                           uint32_t aImageStatus) {
  ImageLoadData& loadData = RequireImageLoadData();
  // XXXjdm This occurs when we have a pending request created, then another
  //       pending request replaces it before the first one is finished.
  //       This begs the question of what the correct behaviour is; we used
  //       to not have to care because we ran this code in OnStopDecode which
  //       wasn't called when the first request was cancelled. For now, I choose
  //       to punt when the given request doesn't appear to have terminated in
  //       an expected state.
  if (!(aImageStatus &
        (imgIRequest::STATUS_ERROR | imgIRequest::STATUS_LOAD_COMPLETE))) {
    return;
  }

  // If the pending request is loaded, switch to it.
  if (aRequest == loadData.mPendingRequest) {
    MakePendingRequestCurrent();
  }
  MOZ_ASSERT(aRequest == loadData.mCurrentRequest,
             "One way or another, we should be current by now");

  // Fire the appropriate DOM event.
  if (!(aImageStatus & imgIRequest::STATUS_ERROR)) {
    FireEvent(u"load"_ns);
  } else {
    FireEvent(u"error"_ns);
  }

  Element* element = AsContent()->AsElement();
  SVGObserverUtils::InvalidateDirectRenderingObservers(element);
  MaybeResolveDecodePromises();
  LargestContentfulPaint::MaybeProcessImageForElementTiming(
      loadData.mCurrentRequest, element);
  UpdateImageState(true);
}

void nsImageLoadingContent::OnUnlockedDraw() {
  // This notification is only sent for animated images. It's OK for
  // non-animated images to wait until the next frame visibility update to
  // become locked. (And that's preferable, since in the case of scrolling it
  // keeps memory usage minimal.)
  //
  // For animated images, though, we want to mark them visible right away so we
  // can call IncrementAnimationConsumers() on them and they'll start animating.

  nsIFrame* frame = GetOurPrimaryImageFrame();
  if (!frame) {
    return;
  }

  if (frame->GetVisibility() == Visibility::ApproximatelyVisible) {
    // This frame is already marked visible; there's nothing to do.
    return;
  }

  nsPresContext* presContext = frame->PresContext();
  if (!presContext) {
    return;
  }

  PresShell* presShell = presContext->GetPresShell();
  if (!presShell) {
    return;
  }

  presShell->EnsureFrameInApproximatelyVisibleList(frame);
}

void nsImageLoadingContent::OnImageIsAnimated(imgIRequest* aRequest) {
  ImageLoadData& loadData = RequireImageLoadData();
  bool* requestFlag = nullptr;
  if (aRequest == loadData.mCurrentRequest) {
    requestFlag = &loadData.mCurrentRequestRegistered;
  } else if (aRequest == loadData.mPendingRequest) {
    requestFlag = &loadData.mPendingRequestRegistered;
  } else {
    MOZ_ASSERT_UNREACHABLE("Which image is this?");
    return;
  }
  nsLayoutUtils::RegisterImageRequest(GetFramePresContext(), aRequest,
                                      requestFlag);
}

static bool IsOurImageFrame(nsIFrame* aFrame) {
  if (nsImageFrame* f = do_QueryFrame(aFrame)) {
    return f->IsForImageLoadingContent();
  }
  return aFrame->IsSVGImageFrame() || aFrame->IsSVGFEImageFrame();
}

nsIFrame* nsImageLoadingContent::GetOurPrimaryImageFrame() {
  nsIFrame* frame = AsContent()->GetPrimaryFrame();
  if (!frame || !IsOurImageFrame(frame)) {
    return nullptr;
  }
  return frame;
}

/*
 * nsIImageLoadingContent impl
 */

void nsImageLoadingContent::SetLoadingEnabled(bool aLoadingEnabled) {
  if (nsContentUtils::GetImgLoaderForChannel(nullptr, nullptr)) {
    mLoadingEnabled = aLoadingEnabled;
  }
}

nsresult nsImageLoadingContent::GetSyncDecodingHint(bool* aHint) {
  *aHint = mSyncDecodingHint;
  return NS_OK;
}

already_AddRefed<Promise> nsImageLoadingContent::QueueDecodeAsync(
    ErrorResult& aRv) {
  // XXX probably should use our global.
  Document* doc = GetOurOwnerDoc();
  RefPtr<Promise> promise = Promise::Create(doc->GetScopeObject(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  class QueueDecodeTask final : public MicroTaskRunnable {
   public:
    QueueDecodeTask(nsImageLoadingContent* aOwner, Promise* aPromise,
                    uint32_t aRequestGeneration)
        : mOwner(aOwner),
          mPromise(aPromise),
          mRequestGeneration(aRequestGeneration) {}

    void Run(AutoSlowOperation& aAso) override {
      mOwner->DecodeAsync(std::move(mPromise), mRequestGeneration);
    }

    bool Suppressed() override {
      // XXX Probably should use mOwner->GetRelevantGlobal()
      nsIGlobalObject* global = mOwner->GetOurOwnerDoc()->GetScopeObject();
      return global && global->IsInSyncOperation();
    }

   private:
    RefPtr<nsImageLoadingContent> mOwner;
    RefPtr<Promise> mPromise;
    uint32_t mRequestGeneration;
  };

  ImageLoadData& loadData = EnsureImageLoadData();
  if (++loadData.mOutstandingDecodePromises == 1) {
    MOZ_ASSERT(loadData.mDecodePromises.IsEmpty());
    doc->RegisterActivityObserver(AsContent()->AsElement());
  }

  auto task =
      MakeRefPtr<QueueDecodeTask>(this, promise, loadData.mRequestGeneration);
  CycleCollectedJSContext::Get()->DispatchToMicroTask(task.forget());
  return promise.forget();
}

void nsImageLoadingContent::DecodeAsync(RefPtr<Promise>&& aPromise,
                                        uint32_t aRequestGeneration) {
  // QueueDecodeAsync allocated the data before queueing us.
  ImageLoadData& loadData = RequireImageLoadData();
  MOZ_ASSERT(aPromise);
  MOZ_ASSERT(loadData.mOutstandingDecodePromises >
             loadData.mDecodePromises.Length());

  // The request may have gotten updated since the decode call was issued.
  if (aRequestGeneration != loadData.mRequestGeneration) {
    aPromise->MaybeReject(NS_ERROR_DOM_IMAGE_INVALID_REQUEST);
    // We never got placed in mDecodePromises, so we must
    // ensure we decrement the counter explicitly.
    --loadData.mOutstandingDecodePromises;
    MaybeDeregisterActivityObserver();
    return;
  }

  bool wasEmpty = loadData.mDecodePromises.IsEmpty();
  loadData.mDecodePromises.AppendElement(std::move(aPromise));
  if (wasEmpty) {
    MaybeResolveDecodePromises();
  }
}

void nsImageLoadingContent::MaybeResolveDecodePromises() {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  if (loadData.mDecodePromises.IsEmpty()) {
    return;
  }

  if (!loadData.mCurrentRequest) {
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_INVALID_REQUEST);
    return;
  }

  // Only can resolve if our document is the active document. If not we are
  // supposed to reject the promise, even if it was fulfilled successfully.
  if (!GetOurOwnerDoc()->IsCurrentActiveDocument()) {
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_INACTIVE_DOCUMENT);
    return;
  }

  // If any error occurred while decoding, we need to reject first.
  uint32_t status = imgIRequest::STATUS_NONE;
  loadData.mCurrentRequest->GetImageStatus(&status);
  if (status & imgIRequest::STATUS_ERROR) {
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_BROKEN);
    return;
  }

  // We need the size to bother with requesting a decode, as we are either
  // blocked on validation or metadata decoding.
  if (!(status & imgIRequest::STATUS_SIZE_AVAILABLE)) {
    return;
  }

  // Check the surface cache status and/or request decoding begin. We do this
  // before LOAD_COMPLETE because we want to start as soon as possible.
  uint32_t flags = imgIContainer::FLAG_HIGH_QUALITY_SCALING |
                   imgIContainer::FLAG_AVOID_REDECODE_FOR_SIZE;
  imgIContainer::DecodeResult decodeResult =
      loadData.mCurrentRequest->RequestDecodeWithResult(flags);
  if (decodeResult == imgIContainer::DECODE_REQUESTED) {
    return;
  }
  if (decodeResult == imgIContainer::DECODE_REQUEST_FAILED) {
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_BROKEN);
    return;
  }
  MOZ_ASSERT(decodeResult == imgIContainer::DECODE_SURFACE_AVAILABLE);

  // We can only fulfill the promises once we have all the data.
  if (!(status & imgIRequest::STATUS_LOAD_COMPLETE)) {
    return;
  }

  for (auto& promise : loadData.mDecodePromises) {
    promise->MaybeResolveWithUndefined();
  }

  MOZ_ASSERT(loadData.mOutstandingDecodePromises >=
             loadData.mDecodePromises.Length());
  loadData.mOutstandingDecodePromises -= loadData.mDecodePromises.Length();
  loadData.mDecodePromises.Clear();
  MaybeDeregisterActivityObserver();
}

void nsImageLoadingContent::RejectDecodePromises(nsresult aStatus) {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  if (loadData.mDecodePromises.IsEmpty()) {
    return;
  }

  for (auto& promise : loadData.mDecodePromises) {
    promise->MaybeReject(aStatus);
  }

  MOZ_ASSERT(loadData.mOutstandingDecodePromises >=
             loadData.mDecodePromises.Length());
  loadData.mOutstandingDecodePromises -= loadData.mDecodePromises.Length();
  loadData.mDecodePromises.Clear();
  MaybeDeregisterActivityObserver();
}

void nsImageLoadingContent::MaybeAgeRequestGeneration(nsIURI* aNewURI) {
  ImageLoadData& loadData = RequireImageLoadData();
  MOZ_ASSERT(loadData.mCurrentRequest);

  // If the current request is about to change, we need to verify if the new
  // URI matches the existing current request's URI. If it doesn't, we need to
  // reject any outstanding promises due to the current request mutating as per
  // step 2.2 of the decode API requirements.
  //
  // https://html.spec.whatwg.org/multipage/embedded-content.html#dom-img-decode
  if (aNewURI) {
    nsCOMPtr<nsIURI> currentURI;
    loadData.mCurrentRequest->GetURI(getter_AddRefs(currentURI));

    bool equal = false;
    if (NS_SUCCEEDED(aNewURI->Equals(currentURI, &equal)) && equal) {
      return;
    }
  }

  ++loadData.mRequestGeneration;
  RejectDecodePromises(NS_ERROR_DOM_IMAGE_INVALID_REQUEST);
}

void nsImageLoadingContent::MaybeDeregisterActivityObserver() {
  if (!mImageLoadData) {
    return;
  }
  if (mImageLoadData->mOutstandingDecodePromises == 0) {
    MOZ_ASSERT(mImageLoadData->mDecodePromises.IsEmpty());
    GetOurOwnerDoc()->UnregisterActivityObserver(AsContent()->AsElement());
  }
}

void nsImageLoadingContent::SetSyncDecodingHint(bool aHint) {
  if (mSyncDecodingHint == aHint) {
    return;
  }

  mSyncDecodingHint = aHint;
  MaybeForceSyncDecoding(/* aPrepareNextRequest */ false);
}

void nsImageLoadingContent::MaybeForceSyncDecoding(
    bool aPrepareNextRequest, nsIFrame* aFrame /* = nullptr */) {
  // GetOurPrimaryImageFrame() might not return the frame during frame init.
  nsIFrame* frame = aFrame ? aFrame : GetOurPrimaryImageFrame();
  if (!frame) {
    return;
  }

  bool forceSync = mSyncDecodingHint;
  if (!forceSync && aPrepareNextRequest) {
    // Detect JavaScript-based animations created by changing the |src|
    // attribute on a timer.
    TimeStamp now = TimeStamp::Now();
    TimeDuration threshold = TimeDuration::FromMilliseconds(
        StaticPrefs::image_infer_src_animation_threshold_ms());

    // If the length of time between request changes is less than the threshold,
    // then force sync decoding to eliminate flicker from the animation.
    ImageLoadData& loadData = EnsureImageLoadData();
    forceSync = (now - loadData.mMostRecentRequestChange < threshold);
    loadData.mMostRecentRequestChange = now;
  }

  if (nsImageFrame* imageFrame = do_QueryFrame(frame)) {
    imageFrame->SetForceSyncDecoding(forceSync);
  } else if (SVGImageFrame* svgImageFrame = do_QueryFrame(frame)) {
    svgImageFrame->SetForceSyncDecoding(forceSync);
  }
}

static void ReplayImageStatus(imgIRequest* aRequest,
                              imgINotificationObserver* aObserver) {
  if (!aRequest) {
    return;
  }

  // Notifications can cancel the request being replayed.
  nsCOMPtr<imgIRequest> kungFuDeathGrip(aRequest);

  uint32_t status = 0;
  nsresult rv = aRequest->GetImageStatus(&status);
  if (NS_FAILED(rv)) {
    return;
  }

  if (status & imgIRequest::STATUS_SIZE_AVAILABLE) {
    aObserver->Notify(aRequest, imgINotificationObserver::SIZE_AVAILABLE,
                      nullptr);
  }
  if (status & imgIRequest::STATUS_FRAME_COMPLETE) {
    aObserver->Notify(aRequest, imgINotificationObserver::FRAME_COMPLETE,
                      nullptr);
  }
  if (status & imgIRequest::STATUS_HAS_TRANSPARENCY) {
    aObserver->Notify(aRequest, imgINotificationObserver::HAS_TRANSPARENCY,
                      nullptr);
  }
  if (status & imgIRequest::STATUS_IS_ANIMATED) {
    aObserver->Notify(aRequest, imgINotificationObserver::IS_ANIMATED, nullptr);
  }
  if (status & imgIRequest::STATUS_DECODE_COMPLETE) {
    aObserver->Notify(aRequest, imgINotificationObserver::DECODE_COMPLETE,
                      nullptr);
  }
  if (status & imgIRequest::STATUS_LOAD_COMPLETE) {
    aObserver->Notify(aRequest, imgINotificationObserver::LOAD_COMPLETE,
                      nullptr);
  }
}

void nsImageLoadingContent::AddNativeObserver(
    imgINotificationObserver* aObserver) {
  if (NS_WARN_IF(!aObserver)) {
    return;
  }

  RefPtr<nsImageLoadingContent> kungFuDeathGrip(this);
  nsCOMPtr<imgINotificationObserver> observerGrip(aObserver);
  nsAutoScriptBlocker scriptBlocker;

  ImageLoadData& loadData = EnsureImageLoadData();

  if (!loadData.mObserverList.mObserver) {
    // Don't touch the linking of the list!
    loadData.mObserverList.mObserver = aObserver;

    ReplayImageStatus(loadData.mCurrentRequest, aObserver);
    ReplayImageStatus(loadData.mPendingRequest, aObserver);

    return;
  }

  // otherwise we have to create a new entry

  ImageObserver* observer = &loadData.mObserverList;
  while (observer->mNext) {
    observer = observer->mNext;
  }

  observer->mNext = new ImageObserver(aObserver);
  ReplayImageStatus(loadData.mCurrentRequest, aObserver);
  ReplayImageStatus(loadData.mPendingRequest, aObserver);
}

void nsImageLoadingContent::RemoveNativeObserver(
    imgINotificationObserver* aObserver) {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  if (NS_WARN_IF(!aObserver)) {
    return;
  }

  if (loadData.mObserverList.mObserver == aObserver) {
    loadData.mObserverList.mObserver = nullptr;
    // Don't touch the linking of the list!
    return;
  }

  // otherwise have to find it and splice it out
  ImageObserver* observer = &loadData.mObserverList;
  while (observer->mNext && observer->mNext->mObserver != aObserver) {
    observer = observer->mNext;
  }

  // At this point, we are pointing to the list element whose mNext is
  // the right observer (assuming of course that mNext is not null)
  if (observer->mNext) {
    // splice it out
    ImageObserver* oldObserver = observer->mNext;
    observer->mNext = oldObserver->mNext;
    oldObserver->mNext = nullptr;  // so we don't destroy them all
    delete oldObserver;
  }
#ifdef DEBUG
  else {
    NS_WARNING("Asked to remove nonexistent observer");
  }
#endif
}

void nsImageLoadingContent::AddObserver(imgINotificationObserver* aObserver) {
  if (NS_WARN_IF(!aObserver)) {
    return;
  }

  ImageLoadData& loadData = EnsureImageLoadData();
  RefPtr<imgRequestProxy> currentReq;
  if (loadData.mCurrentRequest) {
    // Scripted observers may not belong to the same document as us, so when we
    // create the imgRequestProxy, we shouldn't use any. This allows the request
    // to dispatch notifications from the correct scheduler group.
    nsresult rv = loadData.mCurrentRequest->Clone(aObserver, nullptr,
                                                  getter_AddRefs(currentReq));
    if (NS_FAILED(rv)) {
      return;
    }
  }

  RefPtr<imgRequestProxy> pendingReq;
  if (loadData.mPendingRequest) {
    // See above for why we don't use the loading document.
    nsresult rv = loadData.mPendingRequest->Clone(aObserver, nullptr,
                                                  getter_AddRefs(pendingReq));
    if (NS_FAILED(rv)) {
      loadData.mCurrentRequest->CancelAndForgetObserver(NS_BINDING_ABORTED);
      return;
    }
  }

  loadData.mScriptedObservers.AppendElement(new ScriptedImageObserver(
      aObserver, std::move(currentReq), std::move(pendingReq)));
}

void nsImageLoadingContent::RemoveObserver(
    imgINotificationObserver* aObserver) {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  if (NS_WARN_IF(!aObserver)) {
    return;
  }

  if (NS_WARN_IF(loadData.mScriptedObservers.IsEmpty())) {
    return;
  }

  RefPtr<ScriptedImageObserver> observer;
  auto i = loadData.mScriptedObservers.Length();
  do {
    --i;
    if (loadData.mScriptedObservers[i]->mObserver == aObserver) {
      observer = std::move(loadData.mScriptedObservers[i]);
      loadData.mScriptedObservers.RemoveElementAt(i);
      break;
    }
  } while (i > 0);

  if (NS_WARN_IF(!observer)) {
    return;
  }

  // If the cancel causes a mutation, it will be harmless, because we have
  // already removed the observer from the list.
  observer->CancelRequests();
}

void nsImageLoadingContent::ClearScriptedRequests(int32_t aRequestType,
                                                  nsresult aReason) {
  if (!mImageLoadData ||
      MOZ_LIKELY(mImageLoadData->mScriptedObservers.IsEmpty())) {
    return;
  }

  nsTArray<RefPtr<ScriptedImageObserver>> observers(
      mImageLoadData->mScriptedObservers.Clone());
  auto i = observers.Length();
  do {
    --i;

    RefPtr<imgRequestProxy> req;
    switch (aRequestType) {
      case CURRENT_REQUEST:
        req = std::move(observers[i]->mCurrentRequest);
        break;
      case PENDING_REQUEST:
        req = std::move(observers[i]->mPendingRequest);
        break;
      default:
        NS_ERROR("Unknown request type");
        return;
    }

    if (req) {
      req->CancelAndForgetObserver(aReason);
    }
  } while (i > 0);
}

void nsImageLoadingContent::CloneScriptedRequests(imgRequestProxy* aRequest) {
  MOZ_ASSERT(aRequest);

  if (!mImageLoadData ||
      MOZ_LIKELY(mImageLoadData->mScriptedObservers.IsEmpty())) {
    return;
  }

  ImageLoadData& loadData = *mImageLoadData;
  bool current;
  if (aRequest == loadData.mCurrentRequest) {
    current = true;
  } else if (aRequest == loadData.mPendingRequest) {
    current = false;
  } else {
    MOZ_ASSERT_UNREACHABLE("Unknown request type");
    return;
  }

  nsTArray<RefPtr<ScriptedImageObserver>> observers(
      loadData.mScriptedObservers.Clone());
  auto i = observers.Length();
  do {
    --i;

    ScriptedImageObserver* observer = observers[i];
    RefPtr<imgRequestProxy>& req =
        current ? observer->mCurrentRequest : observer->mPendingRequest;
    if (NS_WARN_IF(req)) {
      MOZ_ASSERT_UNREACHABLE("Should have cancelled original request");
      req->CancelAndForgetObserver(NS_BINDING_ABORTED);
      req = nullptr;
    }

    nsresult rv =
        aRequest->Clone(observer->mObserver, nullptr, getter_AddRefs(req));
    (void)NS_WARN_IF(NS_FAILED(rv));
  } while (i > 0);
}

void nsImageLoadingContent::MakePendingScriptedRequestsCurrent() {
  if (!mImageLoadData ||
      MOZ_LIKELY(mImageLoadData->mScriptedObservers.IsEmpty())) {
    return;
  }

  nsTArray<RefPtr<ScriptedImageObserver>> observers(
      mImageLoadData->mScriptedObservers.Clone());
  auto i = observers.Length();
  do {
    --i;

    ScriptedImageObserver* observer = observers[i];
    if (observer->mCurrentRequest) {
      observer->mCurrentRequest->CancelAndForgetObserver(NS_BINDING_ABORTED);
    }
    observer->mCurrentRequest = std::move(observer->mPendingRequest);
  } while (i > 0);
}

already_AddRefed<imgIRequest> nsImageLoadingContent::GetRequest(
    int32_t aRequestType, ErrorResult& aError) {
  nsCOMPtr<imgIRequest> request;
  switch (aRequestType) {
    case CURRENT_REQUEST:
      if (mImageLoadData) {
        request = mImageLoadData->mCurrentRequest;
      }
      break;
    case PENDING_REQUEST:
      if (mImageLoadData) {
        request = mImageLoadData->mPendingRequest;
      }
      break;
    default:
      NS_ERROR("Unknown request type");
      aError.Throw(NS_ERROR_UNEXPECTED);
  }

  return request.forget();
}

NS_IMETHODIMP
nsImageLoadingContent::GetRequest(int32_t aRequestType,
                                  imgIRequest** aRequest) {
  NS_ENSURE_ARG_POINTER(aRequest);

  ErrorResult result;
  *aRequest = GetRequest(aRequestType, result).take();

  return result.StealNSResult();
}

NS_IMETHODIMP_(void)
nsImageLoadingContent::FrameCreated(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "aFrame is null");
  MOZ_ASSERT(IsOurImageFrame(aFrame));

  MaybeForceSyncDecoding(/* aPrepareNextRequest */ false, aFrame);
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  TrackImage(loadData.mCurrentRequest, aFrame);
  TrackImage(loadData.mPendingRequest, aFrame);

  // We need to make sure that our image request is registered, if it should
  // be registered.
  nsPresContext* presContext = aFrame->PresContext();
  if (loadData.mCurrentRequest) {
    nsLayoutUtils::RegisterImageRequestIfAnimated(
        presContext, loadData.mCurrentRequest,
        &loadData.mCurrentRequestRegistered);
  }

  if (loadData.mPendingRequest) {
    nsLayoutUtils::RegisterImageRequestIfAnimated(
        presContext, loadData.mPendingRequest,
        &loadData.mPendingRequestRegistered);
  }
}

NS_IMETHODIMP_(void)
nsImageLoadingContent::FrameDestroyed(nsIFrame* aFrame) {
  NS_ASSERTION(aFrame, "aFrame is null");

  // We need to make sure that our image request is deregistered.
  nsPresContext* presContext = GetFramePresContext();
  if (mImageLoadData) {
    ImageLoadData& loadData = *mImageLoadData;
    if (loadData.mCurrentRequest) {
      nsLayoutUtils::DeregisterImageRequest(
          presContext, loadData.mCurrentRequest,
          &loadData.mCurrentRequestRegistered);
    }

    if (loadData.mPendingRequest) {
      nsLayoutUtils::DeregisterImageRequest(
          presContext, loadData.mPendingRequest,
          &loadData.mPendingRequestRegistered);
    }

    UntrackImage(loadData.mCurrentRequest);
    UntrackImage(loadData.mPendingRequest);
  }

  PresShell* presShell = presContext ? presContext->GetPresShell() : nullptr;
  if (presShell) {
    presShell->RemoveFrameFromApproximatelyVisibleList(aFrame);
  }
}

/* static */
nsContentPolicyType nsImageLoadingContent::PolicyTypeForLoad(
    ImageLoadType aImageLoadType) {
  if (aImageLoadType == eImageLoadType_Imageset) {
    return nsIContentPolicy::TYPE_IMAGESET;
  }

  MOZ_ASSERT(aImageLoadType == eImageLoadType_Normal,
             "Unknown ImageLoadType type in PolicyTypeForLoad");
  return nsIContentPolicy::TYPE_INTERNAL_IMAGE;
}

int32_t nsImageLoadingContent::GetRequestType(imgIRequest* aRequest,
                                              ErrorResult& aError) {
  if (mImageLoadData) {
    if (aRequest == mImageLoadData->mCurrentRequest) {
      return CURRENT_REQUEST;
    }

    if (aRequest == mImageLoadData->mPendingRequest) {
      return PENDING_REQUEST;
    }
  }

  NS_ERROR("Unknown request");
  aError.Throw(NS_ERROR_UNEXPECTED);
  return UNKNOWN_REQUEST;
}

NS_IMETHODIMP
nsImageLoadingContent::GetRequestType(imgIRequest* aRequest,
                                      int32_t* aRequestType) {
  MOZ_ASSERT(aRequestType, "Null out param");

  ErrorResult result;
  *aRequestType = GetRequestType(aRequest, result);
  return result.StealNSResult();
}

already_AddRefed<nsIURI> nsImageLoadingContent::GetCurrentURI() {
  nsCOMPtr<nsIURI> uri;
  if (mImageLoadData) {
    if (mImageLoadData->mCurrentRequest) {
      mImageLoadData->mCurrentRequest->GetURI(getter_AddRefs(uri));
    } else {
      uri = mImageLoadData->mCurrentURI;
    }
  }

  return uri.forget();
}

NS_IMETHODIMP
nsImageLoadingContent::GetCurrentURI(nsIURI** aURI) {
  NS_ENSURE_ARG_POINTER(aURI);
  *aURI = GetCurrentURI().take();
  return NS_OK;
}

already_AddRefed<nsIURI> nsImageLoadingContent::GetCurrentRequestFinalURI() {
  nsCOMPtr<nsIURI> uri;
  if (mImageLoadData && mImageLoadData->mCurrentRequest) {
    mImageLoadData->mCurrentRequest->GetFinalURI(getter_AddRefs(uri));
  }
  return uri.forget();
}

NS_IMETHODIMP
nsImageLoadingContent::LoadImageWithChannel(nsIChannel* aChannel,
                                            nsIStreamListener** aListener) {
  imgLoader* loader =
      nsContentUtils::GetImgLoaderForChannel(aChannel, GetOurOwnerDoc());
  if (!loader) {
    return NS_ERROR_NULL_POINTER;
  }

  nsCOMPtr<Document> doc = GetOurOwnerDoc();
  if (!doc) {
    // Don't bother
    *aListener = nullptr;
    return NS_OK;
  }

  // XXX what should we do with content policies here, if anything?
  // Shouldn't that be done before the start of the load?
  // XXX what about shouldProcess?

  // Our state might change. Watch it.
  auto updateStateOnExit = MakeScopeExit([&] { UpdateImageState(true); });
  // Do the load.
  nsCOMPtr<nsIURI> uri;
  aChannel->GetOriginalURI(getter_AddRefs(uri));
  ImageLoadData& loadData = EnsureImageLoadData();
  RefPtr<imgRequestProxy>& req = PrepareNextRequest(eImageLoadType_Normal, uri);
  nsresult rv = loader->LoadImageWithChannel(aChannel, this, doc, aListener,
                                             getter_AddRefs(req));
  if (NS_SUCCEEDED(rv)) {
    CloneScriptedRequests(req);
    TrackImage(req);
    return NS_OK;
  }

  MOZ_ASSERT(!req, "Shouldn't have non-null request here");
  // If we don't have a current URI, we might as well store this URI so people
  // know what we tried (and failed) to load.
  if (!loadData.mCurrentRequest) {
    aChannel->GetURI(getter_AddRefs(loadData.mCurrentURI));
  }

  FireEvent(u"error"_ns);
  return rv;
}

void nsImageLoadingContent::ForceReload(bool aNotify, ErrorResult& aError) {
  nsCOMPtr<nsIURI> currentURI;
  GetCurrentURI(getter_AddRefs(currentURI));
  if (!currentURI) {
    aError.Throw(NS_ERROR_NOT_AVAILABLE);
    return;
  }

  // We keep this flag around along with the old URI even for failed requests
  // without a live request object
  ImageLoadType loadType =
      (RequireImageLoadData().mCurrentRequestFlags & REQUEST_IS_IMAGESET)
          ? eImageLoadType_Imageset
          : eImageLoadType_Normal;
  nsresult rv = LoadImage(currentURI, true, aNotify, loadType,
                          nsIRequest::VALIDATE_ALWAYS | LoadFlags());
  if (NS_FAILED(rv)) {
    aError.Throw(rv);
  }
}

/*
 * Non-interface methods
 */

nsresult nsImageLoadingContent::LoadImage(const nsAString& aNewURI, bool aForce,
                                          bool aNotify,
                                          ImageLoadType aImageLoadType,
                                          nsIPrincipal* aTriggeringPrincipal) {
  // First, get a document (needed for security checks and the like)
  Document* doc = GetOurOwnerDoc();
  if (!doc) {
    // No reason to bother, I think...
    return NS_OK;
  }

  // Parse the URI string to get image URI
  nsCOMPtr<nsIURI> imageURI;
  if (!aNewURI.IsEmpty()) {
    (void)StringToURI(aNewURI, doc, getter_AddRefs(imageURI));
  }

  return LoadImage(imageURI, aForce, aNotify, aImageLoadType, LoadFlags(), doc,
                   aTriggeringPrincipal);
}

nsresult nsImageLoadingContent::LoadImage(nsIURI* aNewURI, bool aForce,
                                          bool aNotify,
                                          ImageLoadType aImageLoadType,
                                          nsLoadFlags aLoadFlags,
                                          Document* aDocument,
                                          nsIPrincipal* aTriggeringPrincipal) {
  // Pending load/error events need to be canceled in some situations. This
  // is not documented in the spec, but can cause site compat problems if not
  // done. See bug 1309461 and https://github.com/whatwg/html/issues/1872.
  CancelPendingEvent();

  if (!aNewURI) {
    // Cancel image requests and then fire only error event per spec.
    CancelImageRequests(aNotify);
    if (aImageLoadType == eImageLoadType_Normal) {
      // Mark error event as cancelable only for src="" case, since only this
      // error causes site compat problem (bug 1308069) for now.
      FireEvent(u"error"_ns, true);
    }
    return NS_OK;
  }

  if (!mLoadingEnabled) {
    // XXX Why fire an error here? seems like the callers to SetLoadingEnabled
    // don't want/need it.
    FireEvent(u"error"_ns);
    return NS_OK;
  }

  NS_ASSERTION(!aDocument || aDocument == GetOurOwnerDoc(),
               "Bogus document passed in");
  // First, get a document (needed for security checks and the like)
  if (!aDocument) {
    aDocument = GetOurOwnerDoc();
    if (!aDocument) {
      // No reason to bother, I think...
      return NS_OK;
    }
  }

  // Data documents, or documents from DOMParser shouldn't perform image
  // loading.
  //
  // FIXME(emilio): Shouldn't this check be part of
  // Document::ShouldLoadImages()? Or alternatively check ShouldLoadImages here
  // instead? (It seems we only check ShouldLoadImages in HTMLImageElement,
  // which seems wrong...)
  if (aDocument->IsLoadedAsData() && !aDocument->IsStaticDocument()) {
    // Clear our pending request if we do have one.
    ClearPendingRequest(NS_BINDING_ABORTED, Some(OnNonvisible::DiscardImages));

    FireEvent(u"error"_ns);
    return NS_OK;
  }

  // URI equality check.
  //
  // We skip the equality check if we don't have a current image, since in that
  // case we really do want to try loading again.
  if (!aForce && mImageLoadData && mImageLoadData->mCurrentRequest) {
    nsCOMPtr<nsIURI> currentURI;
    GetCurrentURI(getter_AddRefs(currentURI));
    bool equal;
    if (currentURI && NS_SUCCEEDED(currentURI->Equals(aNewURI, &equal)) &&
        equal) {
      // Nothing to do here.
      return NS_OK;
    }
  }

  // From this point on, our image state could change. Watch it.
  auto updateStateOnExit = MakeScopeExit([&] { UpdateImageState(aNotify); });

  // Sanity check.
  //
  // We use the principal of aDocument to avoid having to QI |this| an extra
  // time. It should always be the same as the principal of this node.
  Element* element = AsContent()->AsElement();
  MOZ_ASSERT(element->NodePrincipal() == aDocument->NodePrincipal(),
             "Principal mismatch?");

  nsLoadFlags loadFlags =
      aLoadFlags | nsContentUtils::CORSModeToLoadImageFlags(GetCORSMode());

  ImageLoadData& loadData = EnsureImageLoadData();
  RefPtr<imgRequestProxy>& req = PrepareNextRequest(aImageLoadType, aNewURI);

  nsCOMPtr<nsIPrincipal> triggeringPrincipal;
  bool result = nsContentUtils::QueryTriggeringPrincipal(
      element, aTriggeringPrincipal, getter_AddRefs(triggeringPrincipal));

  // If result is true, which means this node has specified
  // 'triggeringprincipal' attribute on it, so we use favicon as the policy
  // type.
  nsContentPolicyType policyType =
      result ? nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON
             : PolicyTypeForLoad(aImageLoadType);

  auto referrerInfo = MakeRefPtr<ReferrerInfo>(*element);
  auto fetchPriority = GetFetchPriorityForImage();
  nsresult rv = nsContentUtils::LoadImage(
      aNewURI, element, aDocument, triggeringPrincipal, 0, referrerInfo, this,
      loadFlags, element->LocalName(), getter_AddRefs(req), policyType,
      mUseUrgentStartForChannel, /* aLinkPreload */ false,
      /* aEarlyHintPreloaderId */ 0, fetchPriority);

  if (fetchPriority != FetchPriority::Auto) {
    aDocument->SetPageloadEventFeature(
        performance::pageload_event::DocumentFeature::FETCH_PRIORITY_IMAGES);
  }

  // Reset the flag to avoid loading from XPCOM or somewhere again else without
  // initiated by user interaction.
  mUseUrgentStartForChannel = false;

  // Tell the document to forget about the image preload, if any, for
  // this URI, now that we might have another imgRequestProxy for it.
  // That way if we get canceled later the image load won't continue.
  aDocument->ForgetImagePreload(aNewURI);

  if (NS_SUCCEEDED(rv)) {
    // Based on performance testing unsuppressing painting soon after the page
    // has gotten an image may improve visual metrics.
    if (Document* doc = element->GetComposedDoc()) {
      if (PresShell* shell = doc->GetPresShell()) {
        shell->TryUnsuppressPaintingSoon();
      }
    }

    CloneScriptedRequests(req);
    TrackImage(req);

    // Handle cases when we just ended up with a request but it's already done.
    // In that situation we have to synchronously switch that request to being
    // the current request, because websites depend on that behavior.
    {
      uint32_t loadStatus;
      if (NS_SUCCEEDED(req->GetImageStatus(&loadStatus)) &&
          (loadStatus & imgIRequest::STATUS_LOAD_COMPLETE)) {
        if (req == loadData.mPendingRequest) {
          MakePendingRequestCurrent();
        }
        MOZ_ASSERT(loadData.mCurrentRequest,
                   "How could we not have a current request here?");

        if (nsImageFrame* f = do_QueryFrame(GetOurPrimaryImageFrame())) {
          f->NotifyNewCurrentRequest(loadData.mCurrentRequest);
        }
      }
    }
  } else {
    MOZ_ASSERT(!req, "Shouldn't have non-null request here");
    // If we don't have a current URI, we might as well store this URI so people
    // know what we tried (and failed) to load.
    if (!loadData.mCurrentRequest) {
      loadData.mCurrentURI = aNewURI;
    }

    FireEvent(u"error"_ns);
  }

  return NS_OK;
}

already_AddRefed<Promise> nsImageLoadingContent::RecognizeCurrentImageText(
    ErrorResult& aRv) {
  using widget::TextRecognition;

  if (!mImageLoadData || !mImageLoadData->mCurrentRequest) {
    aRv.ThrowInvalidStateError("No current request");
    return nullptr;
  }
  nsCOMPtr<imgIContainer> image;
  mImageLoadData->mCurrentRequest->GetImage(getter_AddRefs(image));
  if (!image) {
    aRv.ThrowInvalidStateError("No image");
    return nullptr;
  }

  // XXX Probably should use this->GetRelevantGlobal()
  RefPtr<Promise> domPromise =
      Promise::Create(GetOurOwnerDoc()->GetScopeObject(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // The list of ISO 639-1 language tags to pass to the text recognition API.
  AutoTArray<nsCString, 4> languages;
  {
    // The document's locale should be the top language to use. Parse the BCP 47
    // locale and extract the ISO 639-1 language tag. e.g. "en-US" -> "en".
    nsAutoCString elementLanguage;
    nsAtom* imgLanguage = AsContent()->GetLang();
    intl::Locale locale;
    if (imgLanguage) {
      imgLanguage->ToUTF8String(elementLanguage);
      auto result = intl::LocaleParser::TryParse(elementLanguage, locale);
      if (result.isOk()) {
        languages.AppendElement(locale.Language().Span());
      }
    }
  }

  {
    // The app locales should also be included after the document's locales.
    // Extract the language tag like above.
    nsTArray<nsCString> appLocales;
    intl::LocaleService::GetInstance()->GetAppLocalesAsBCP47(appLocales);

    for (const auto& localeString : appLocales) {
      intl::Locale locale;
      auto result = intl::LocaleParser::TryParse(localeString, locale);
      if (result.isErr()) {
        NS_WARNING("Could not parse an app locale string, ignoring it.");
        continue;
      }
      languages.AppendElement(locale.Language().Span());
    }
  }

  TextRecognition::FindText(*image, languages)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [weak = RefPtr{do_GetWeakReference(this)},
           request = RefPtr{mImageLoadData->mCurrentRequest}, domPromise](
              TextRecognition::NativePromise::ResolveOrRejectValue&& aValue) {
            if (aValue.IsReject()) {
              domPromise->MaybeRejectWithNotSupportedError(
                  aValue.RejectValue());
              return;
            }
            RefPtr<nsIImageLoadingContent> iilc = do_QueryReferent(weak.get());
            if (!iilc) {
              domPromise->MaybeRejectWithInvalidStateError(
                  "Element was dead when we got the results");
              return;
            }
            auto* ilc = static_cast<nsImageLoadingContent*>(iilc.get());
            if (ilc->RequireImageLoadData().mCurrentRequest != request) {
              domPromise->MaybeRejectWithInvalidStateError(
                  "Request not current");
              return;
            }
            auto& textRecognitionResult = aValue.ResolveValue();
            Element* el = ilc->AsContent()->AsElement();

            nsTArray<ImageText> imageTexts(
                textRecognitionResult.quads().Length());
            // XXX shouldn't this be GetRelevantGlobal? But it's privileged code
            // so seems probably fine.
            nsIGlobalObject* global = el->GetDocumentGlobal();

            for (const auto& quad : textRecognitionResult.quads()) {
              NotNull<ImageText*> imageText = imageTexts.AppendElement();

              // Note: These points are not actually CSSPixels, but a
              // DOMQuad is a conveniently similar structure that can store
              // these values.
              CSSPoint points[4];
              points[0] = CSSPoint(quad.points()[0].x, quad.points()[0].y);
              points[1] = CSSPoint(quad.points()[1].x, quad.points()[1].y);
              points[2] = CSSPoint(quad.points()[2].x, quad.points()[2].y);
              points[3] = CSSPoint(quad.points()[3].x, quad.points()[3].y);

              imageText->mQuad = new DOMQuad(global, points);
              imageText->mConfidence = quad.confidence();
              imageText->mString = quad.string();
            }
            domPromise->MaybeResolve(std::move(imageTexts));
          });
  return domPromise.forget();
}

CSSIntSize nsImageLoadingContent::NaturalSize(
    DoDensityCorrection aDensityCorrection) {
  // This function implements the image.naturalWidth and image.naturalHeight
  // APIs, specced here:
  // https://html.spec.whatwg.org/multipage/embedded-content.html#dom-img-naturalwidth-dev

  // Trivial case from the spec: "return [...] 0 if the image is not available"
  // (our early-return statements here return a zero-filled CSSIntSize).
  if (!mImageLoadData || !mImageLoadData->mCurrentRequest) {
    return {};
  }

  nsCOMPtr<imgIContainer> image;
  mImageLoadData->mCurrentRequest->GetImage(getter_AddRefs(image));
  if (!image) {
    return {};
  }

  mozilla::image::ImageIntrinsicSize intrinsicSize;
  nsresult rv = image->GetIntrinsicSize(&intrinsicSize);
  if (NS_FAILED(rv)) {
    return {};
  }

  // If we get here, we have enough information about the image to return its
  // "density-corrected natural width and height", which is specced here:
  // https://html.spec.whatwg.org/multipage/images.html#density-corrected-intrinsic-width-and-height
  //
  // At a high level, here's what happens:
  // * If the image has an intrinsic size in both axes, we density-correct it
  //   and return it.
  // * Otherwise: use the (non-density-corrected) fallback size to fill in
  //   whichever component is missing -- though if we can transfer the size
  //   through the aspect ratio, we do that instead (and density-correct it).

  // * If we *only* have an intrinsic aspect ratio, then we use the fallback
  //   width, and we transfer it through the aspect ratio to produce a suitable
  //   fallback height. (And neither component gets density-corrected since
  //   they're both fallbacks.)
  CSSIntSize size;  // defaults to 0,0

  // Use intrinsicSize.mWidth, if it exists; otherwise, use fallback width.
  bool isUsingFallbackWidth = !intrinsicSize.mWidth;
  size.width = isUsingFallbackWidth ? kFallbackIntrinsicWidthInPixels
                                    : CSSIntCoord(*intrinsicSize.mWidth);

  // Use intrinsicSize.mHeight, if it exists; otherwise, use fallback height.
  bool isUsingFallbackHeight = !intrinsicSize.mHeight;
  size.height = isUsingFallbackHeight ? kFallbackIntrinsicHeightInPixels
                                      : CSSIntCoord(*intrinsicSize.mHeight);

  AspectRatio ratio = image->GetIntrinsicRatio();
  if (ratio) {
    if (isUsingFallbackHeight) {
      // If we're using the fallback height and we have an aspect ratio, we
      // instead compute our height from the width & ratio.  (Note that the
      // width we use here might be the fallback width, and that's fine. If
      // so, we just keep treating the resulting height as a fallback, i.e.
      // we won't density-correct it.)
      size.height = ratio.Inverted().ApplyTo(size.width);
      isUsingFallbackHeight = isUsingFallbackWidth;
    } else if (isUsingFallbackWidth) {
      // We're tentatively using the fallback width, but we have an
      // aspect-ratio and a (non-fallback) height.  So we don't use the
      // fallback width after all; instead, we compute a width from the height
      // & ratio.  (And we don't treat the result as fallback, because it's
      // computed from a non-fallback height.)
      size.width = ratio.ApplyTo(size.height);
      isUsingFallbackWidth = false;
    }
  }

  ImageResolution resolution = image->GetResolution();
  if (aDensityCorrection == DoDensityCorrection::Yes) {
    // NOTE(emilio): What we implement here matches the image-set() spec, but
    // it's unclear whether this is the right thing to do, see
    // https://github.com/whatwg/html/pull/5574#issuecomment-826335244.
    if (auto* image = HTMLImageElement::FromNode(AsContent())) {
      if (auto* sel = image->GetResponsiveImageSelector()) {
        float density = sel->GetSelectedImageDensity();
        MOZ_ASSERT(density >= 0.0);
        resolution.ScaleBy(density);
      }
    }
  }

  if (!isUsingFallbackWidth) {
    resolution.ApplyXTo(size.width);
  }
  if (!isUsingFallbackHeight) {
    resolution.ApplyYTo(size.height);
  }
  return size;
}

CSSIntSize nsImageLoadingContent::GetWidthHeightForImage() {
  // Determine the value for .width and .height getters. The spec text for this
  // lives here:
  // https://html.spec.whatwg.org/multipage/embedded-content.html#img-dimensions
  // It mostly defers to a "determine the dimensions" algorithm which lives
  // here:
  // https://html.spec.whatwg.org/multipage/embedded-content-other.html#determine-dimensions
  //
  // Quotes below are from that "determine the dimensions" spec text.

  // "If element is being rendered, then return the width and height of
  // element's content box, in CSS pixels."
  Element* element = AsContent()->AsElement();
  if (nsIFrame* frame = element->GetPrimaryFrame(FlushType::Layout)) {
    return CSSIntSize::FromAppUnitsRounded(frame->GetContentRect().Size());
  }

  // "...set width to naturalDimensions's width and height to
  // naturalDimensions's height" [in the tentative return value]
  CSSIntSize size = NaturalSize(DoDensityCorrection::Yes);

  // "If element has a width attribute: [attempt to parse and use that value]"
  const nsAttrValue* value;
  if ((value = element->GetParsedAttr(nsGkAtoms::width)) &&
      value->Type() == nsAttrValue::eInteger) {
    size.width = value->GetIntegerValue();
  }

  // "If element has a height attribute: [attempt to parse and use that value]"
  if ((value = element->GetParsedAttr(nsGkAtoms::height)) &&
      value->Type() == nsAttrValue::eInteger) {
    size.height = value->GetIntegerValue();
  }

  NS_ASSERTION(size.width >= 0, "negative width");
  NS_ASSERTION(size.height >= 0, "negative height");
  return size;
}

void nsImageLoadingContent::UpdateImageState(bool aNotify) {
  Element* thisElement = AsContent()->AsElement();
  const bool isBroken = [&] {
    if (mLazyLoading ||
        (mImageLoadData && mImageLoadData->mPendingImageLoadTask)) {
      return false;
    }
    if (!mImageLoadData || !mImageLoadData->mCurrentRequest) {
      return true;
    }
    uint32_t currentLoadStatus;
    nsresult rv =
        mImageLoadData->mCurrentRequest->GetImageStatus(&currentLoadStatus);
    return NS_FAILED(rv) || currentLoadStatus & imgIRequest::STATUS_ERROR;
  }();
  thisElement->SetStates(ElementState::BROKEN, isBroken, aNotify);
  if (isBroken) {
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_BROKEN);
  }
}

void nsImageLoadingContent::CancelImageRequests(bool aNotify) {
  RejectDecodePromises(NS_ERROR_DOM_IMAGE_INVALID_REQUEST);
  ClearPendingRequest(NS_BINDING_ABORTED, Some(OnNonvisible::DiscardImages));
  ClearCurrentRequest(NS_BINDING_ABORTED, Some(OnNonvisible::DiscardImages));
  UpdateImageState(aNotify);
}

Document* nsImageLoadingContent::GetOurOwnerDoc() {
  return AsContent()->OwnerDoc();
}

Document* nsImageLoadingContent::GetOurCurrentDoc() {
  return AsContent()->GetComposedDoc();
}

nsPresContext* nsImageLoadingContent::GetFramePresContext() {
  nsIFrame* frame = GetOurPrimaryImageFrame();
  if (!frame) {
    return nullptr;
  }
  return frame->PresContext();
}

nsresult nsImageLoadingContent::StringToURI(const nsAString& aSpec,
                                            Document* aDocument,
                                            nsIURI** aURI) {
  MOZ_ASSERT(aDocument, "Must have a document");
  MOZ_ASSERT(aURI, "Null out param");

  // (1) Get the base URI
  nsIContent* thisContent = AsContent();
  nsIURI* baseURL = thisContent->GetBaseURI();

  // (2) Get the charset
  auto encoding = aDocument->GetDocumentCharacterSet();

  // (3) Construct the silly thing
  return NS_NewURI(aURI, aSpec, encoding, baseURL);
}

nsresult nsImageLoadingContent::FireEvent(const nsAString& aEventType,
                                          bool aIsCancelable) {
  if (nsContentUtils::DocumentInactiveForImageLoads(GetOurOwnerDoc())) {
    // Don't bother to fire any events, especially error events.
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_INACTIVE_DOCUMENT);
    return NS_OK;
  }

  // We have to fire the event asynchronously so that we won't go into infinite
  // loops in cases when onLoad handlers reset the src and the new src is in
  // cache.

  nsCOMPtr<nsINode> thisNode = AsContent();

  RefPtr<AsyncEventDispatcher> loadBlockingAsyncDispatcher =
      new LoadBlockingAsyncEventDispatcher(thisNode, aEventType, CanBubble::eNo,
                                           ChromeOnlyDispatch::eNo);
  loadBlockingAsyncDispatcher->PostDOMEvent();

  if (aIsCancelable) {
    EnsureImageLoadData().mPendingEvent = loadBlockingAsyncDispatcher;
  }

  return NS_OK;
}

void nsImageLoadingContent::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  if (mImageLoadData && mImageLoadData->mPendingEvent == aEvent) {
    mImageLoadData->mPendingEvent = nullptr;
  }
}

void nsImageLoadingContent::CancelPendingEvent() {
  if (mImageLoadData && mImageLoadData->mPendingEvent) {
    mImageLoadData->mPendingEvent->Cancel();
    mImageLoadData->mPendingEvent = nullptr;
  }
}

RefPtr<imgRequestProxy>& nsImageLoadingContent::PrepareNextRequest(
    ImageLoadType aImageLoadType, nsIURI* aNewURI) {
  ImageLoadData& loadData = EnsureImageLoadData();
  MaybeForceSyncDecoding(/* aPrepareNextRequest */ true);

  // We only want to cancel the existing current request if size is not
  // available. bz says the web depends on this behavior.
  // Otherwise, we get rid of any half-baked request that might be sitting there
  // and make this one current.
  return HaveSize(loadData.mCurrentRequest)
             ? PreparePendingRequest(aImageLoadType)
             : PrepareCurrentRequest(aImageLoadType, aNewURI);
}

RefPtr<imgRequestProxy>& nsImageLoadingContent::PrepareCurrentRequest(
    ImageLoadType aImageLoadType, nsIURI* aNewURI) {
  ImageLoadData& loadData = RequireImageLoadData();
  if (loadData.mCurrentRequest) {
    MaybeAgeRequestGeneration(aNewURI);
  }
  // Get rid of anything that was there previously.
  ClearCurrentRequest(NS_BINDING_ABORTED, Some(OnNonvisible::DiscardImages));

  if (aImageLoadType == eImageLoadType_Imageset) {
    loadData.mCurrentRequestFlags |= REQUEST_IS_IMAGESET;
  }

  // Return a reference.
  return loadData.mCurrentRequest;
}

RefPtr<imgRequestProxy>& nsImageLoadingContent::PreparePendingRequest(
    ImageLoadType aImageLoadType) {
  ImageLoadData& loadData = RequireImageLoadData();
  // Get rid of anything that was there previously.
  ClearPendingRequest(NS_BINDING_ABORTED, Some(OnNonvisible::DiscardImages));

  if (aImageLoadType == eImageLoadType_Imageset) {
    loadData.mPendingRequestFlags |= REQUEST_IS_IMAGESET;
  }

  // Return a reference.
  return loadData.mPendingRequest;
}

namespace {

class ImageRequestAutoLock {
 public:
  explicit ImageRequestAutoLock(imgIRequest* aRequest) : mRequest(aRequest) {
    if (mRequest) {
      mRequest->LockImage();
    }
  }

  ~ImageRequestAutoLock() {
    if (mRequest) {
      mRequest->UnlockImage();
    }
  }

 private:
  nsCOMPtr<imgIRequest> mRequest;
};

}  // namespace

void nsImageLoadingContent::MakePendingRequestCurrent() {
  ImageLoadData& loadData = RequireImageLoadData();
  MOZ_ASSERT(loadData.mPendingRequest);

  // If we have a pending request, we know that there is an existing current
  // request with size information. If the pending request is for a different
  // URI, then we need to reject any outstanding promises.
  nsCOMPtr<nsIURI> uri;
  loadData.mPendingRequest->GetURI(getter_AddRefs(uri));

  // Lock mCurrentRequest for the duration of this method.  We
  // do this because PrepareCurrentRequest() might unlock
  // mCurrentRequest.  If mCurrentRequest and
  // mPendingRequest are both requests for the same image,
  // unlocking mCurrentRequest before we lock
  // mPendingRequest can cause the lock count to go to 0 and
  // the image to be discarded!
  ImageRequestAutoLock autoLock(loadData.mCurrentRequest);

  ImageLoadType loadType = (loadData.mPendingRequestFlags & REQUEST_IS_IMAGESET)
                               ? eImageLoadType_Imageset
                               : eImageLoadType_Normal;

  PrepareCurrentRequest(loadType, uri) = loadData.mPendingRequest;
  MakePendingScriptedRequestsCurrent();
  loadData.mPendingRequest = nullptr;
  loadData.mCurrentRequestFlags = loadData.mPendingRequestFlags;
  loadData.mPendingRequestFlags = 0;
  loadData.mCurrentRequestRegistered = loadData.mPendingRequestRegistered;
  loadData.mPendingRequestRegistered = false;
}

void nsImageLoadingContent::ClearCurrentRequest(
    nsresult aReason, const Maybe<OnNonvisible>& aNonvisibleAction) {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  if (!loadData.mCurrentRequest) {
    // Even if we didn't have a current request, we might have been keeping
    // a URI and flags as a placeholder for a failed load. Clear that now.
    loadData.mCurrentURI = nullptr;
    loadData.mCurrentRequestFlags = 0;
    return;
  }
  MOZ_ASSERT(!loadData.mCurrentURI,
             "Shouldn't have both mCurrentRequest and "
             "mCurrentURI!");

  // Deregister this image from the refresh driver so it no longer receives
  // notifications.
  nsLayoutUtils::DeregisterImageRequest(GetFramePresContext(),
                                        loadData.mCurrentRequest,
                                        &loadData.mCurrentRequestRegistered);

  // Clean up the request.
  UntrackImage(loadData.mCurrentRequest, aNonvisibleAction);
  ClearScriptedRequests(CURRENT_REQUEST, aReason);
  loadData.mCurrentRequest->CancelAndForgetObserver(aReason);
  loadData.mCurrentRequest = nullptr;
  loadData.mCurrentRequestFlags = 0;
}

void nsImageLoadingContent::ClearPendingRequest(
    nsresult aReason, const Maybe<OnNonvisible>& aNonvisibleAction) {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  if (!loadData.mPendingRequest) return;

  // Deregister this image from the refresh driver so it no longer receives
  // notifications.
  nsLayoutUtils::DeregisterImageRequest(GetFramePresContext(),
                                        loadData.mPendingRequest,
                                        &loadData.mPendingRequestRegistered);

  UntrackImage(loadData.mPendingRequest, aNonvisibleAction);
  ClearScriptedRequests(PENDING_REQUEST, aReason);
  loadData.mPendingRequest->CancelAndForgetObserver(aReason);
  loadData.mPendingRequest = nullptr;
  loadData.mPendingRequestFlags = 0;
}

bool nsImageLoadingContent::HaveSize(imgIRequest* aImage) {
  // Handle the null case
  if (!aImage) return false;

  // Query the image
  uint32_t status;
  nsresult rv = aImage->GetImageStatus(&status);
  return (NS_SUCCEEDED(rv) && (status & imgIRequest::STATUS_SIZE_AVAILABLE));
}

void nsImageLoadingContent::NotifyOwnerDocumentActivityChanged() {
  if (!GetOurOwnerDoc()->IsCurrentActiveDocument()) {
    RejectDecodePromises(NS_ERROR_DOM_IMAGE_INACTIVE_DOCUMENT);
  }
}

void nsImageLoadingContent::BindToTree(BindContext& aContext,
                                       nsINode& aParent) {
  // We may be getting connected, if so our image should be tracked,
  if (mImageLoadData && aContext.InComposedDoc()) {
    TrackImage(mImageLoadData->mCurrentRequest);
    TrackImage(mImageLoadData->mPendingRequest);
  }
}

void nsImageLoadingContent::UnbindFromTree() {
  if (!mImageLoadData) {
    return;
  }
  // We may be leaving the document, so if our image is tracked, untrack it.
  nsCOMPtr<Document> doc = GetOurCurrentDoc();
  if (!doc) {
    return;
  }

  UntrackImage(mImageLoadData->mCurrentRequest);
  UntrackImage(mImageLoadData->mPendingRequest);
}

void nsImageLoadingContent::OnVisibilityChange(
    Visibility aNewVisibility, const Maybe<OnNonvisible>& aNonvisibleAction) {
  if (!mImageLoadData) {
    return;
  }
  ImageLoadData& loadData = *mImageLoadData;
  switch (aNewVisibility) {
    case Visibility::ApproximatelyVisible:
      TrackImage(loadData.mCurrentRequest);
      TrackImage(loadData.mPendingRequest);
      break;

    case Visibility::ApproximatelyNonVisible:
      UntrackImage(loadData.mCurrentRequest, aNonvisibleAction);
      UntrackImage(loadData.mPendingRequest, aNonvisibleAction);
      break;

    case Visibility::Untracked:
      MOZ_ASSERT_UNREACHABLE("Shouldn't notify for untracked visibility");
      break;
  }
}

void nsImageLoadingContent::TrackImage(imgIRequest* aImage,
                                       nsIFrame* aFrame /*= nullptr */) {
  if (!aImage) return;

  ImageLoadData& loadData = RequireImageLoadData();
  MOZ_ASSERT(
      aImage == loadData.mCurrentRequest || aImage == loadData.mPendingRequest,
      "Why haven't we heard of this request?");

  Document* doc = GetOurCurrentDoc();
  if (!doc) {
    return;
  }

  if (!aFrame) {
    aFrame = GetOurPrimaryImageFrame();
  }

  /* This line is deceptively simple. It hides a lot of subtlety. Before we
   * create an nsImageFrame we call nsImageFrame::ShouldCreateImageFrameFor
   * to determine if we should create an nsImageFrame or create a frame based
   * on the display of the element (ie inline, block, etc). Inline, block, etc
   * frames don't register for visibility tracking so they will return UNTRACKED
   * from GetVisibility(). So this line is choosing to mark such images as
   * visible. Once the image loads we will get an nsImageFrame and the proper
   * visibility. This is a pitfall of tracking the visibility on the frames
   * instead of the content node.
   */
  if (!aFrame ||
      aFrame->GetVisibility() == Visibility::ApproximatelyNonVisible) {
    return;
  }

  if (aImage == loadData.mCurrentRequest &&
      !(loadData.mCurrentRequestFlags & REQUEST_IS_TRACKED)) {
    loadData.mCurrentRequestFlags |= REQUEST_IS_TRACKED;
    doc->TrackImage(loadData.mCurrentRequest);
  }
  if (aImage == loadData.mPendingRequest &&
      !(loadData.mPendingRequestFlags & REQUEST_IS_TRACKED)) {
    loadData.mPendingRequestFlags |= REQUEST_IS_TRACKED;
    doc->TrackImage(loadData.mPendingRequest);
  }
}

void nsImageLoadingContent::UntrackImage(
    imgIRequest* aImage, const Maybe<OnNonvisible>& aNonvisibleAction
    /* = Nothing() */) {
  if (!aImage) return;

  ImageLoadData& loadData = RequireImageLoadData();
  MOZ_ASSERT(
      aImage == loadData.mCurrentRequest || aImage == loadData.mPendingRequest,
      "Why haven't we heard of this request?");

  // We may not be in the document.  If we outlived our document that's fine,
  // because the document empties out the tracker and unlocks all locked images
  // on destruction.  But if we were never in the document we may need to force
  // discarding the image here, since this is the only chance we have.
  Document* doc = GetOurCurrentDoc();
  if (aImage == loadData.mCurrentRequest) {
    if (doc && (loadData.mCurrentRequestFlags & REQUEST_IS_TRACKED)) {
      loadData.mCurrentRequestFlags &= ~REQUEST_IS_TRACKED;
      doc->UntrackImage(loadData.mCurrentRequest,
                        aNonvisibleAction == Some(OnNonvisible::DiscardImages)
                            ? Document::RequestDiscard::Yes
                            : Document::RequestDiscard::No);
    } else if (aNonvisibleAction == Some(OnNonvisible::DiscardImages)) {
      // If we're not in the document we may still need to be discarded.
      aImage->RequestDiscard();
    }
  }
  if (aImage == loadData.mPendingRequest) {
    if (doc && (loadData.mPendingRequestFlags & REQUEST_IS_TRACKED)) {
      loadData.mPendingRequestFlags &= ~REQUEST_IS_TRACKED;
      doc->UntrackImage(loadData.mPendingRequest,
                        aNonvisibleAction == Some(OnNonvisible::DiscardImages)
                            ? Document::RequestDiscard::Yes
                            : Document::RequestDiscard::No);
    } else if (aNonvisibleAction == Some(OnNonvisible::DiscardImages)) {
      // If we're not in the document we may still need to be discarded.
      aImage->RequestDiscard();
    }
  }
}

CORSMode nsImageLoadingContent::GetCORSMode() { return CORS_NONE; }

nsImageLoadingContent::ImageObserver::ImageObserver(
    imgINotificationObserver* aObserver)
    : mObserver(aObserver), mNext(nullptr) {
  MOZ_COUNT_CTOR(ImageObserver);
}

nsImageLoadingContent::ImageObserver::~ImageObserver() {
  MOZ_COUNT_DTOR(ImageObserver);
  NS_CONTENT_DELETE_LIST_MEMBER(ImageObserver, this, mNext);
}

nsImageLoadingContent::ScriptedImageObserver::ScriptedImageObserver(
    imgINotificationObserver* aObserver,
    RefPtr<imgRequestProxy>&& aCurrentRequest,
    RefPtr<imgRequestProxy>&& aPendingRequest)
    : mObserver(aObserver),
      mCurrentRequest(aCurrentRequest),
      mPendingRequest(aPendingRequest) {}

nsImageLoadingContent::ScriptedImageObserver::~ScriptedImageObserver() {
  // We should have cancelled any requests before getting released.
  DebugOnly<bool> cancel = CancelRequests();
  MOZ_ASSERT(!cancel, "Still have requests in ~ScriptedImageObserver!");
}

bool nsImageLoadingContent::ScriptedImageObserver::CancelRequests() {
  bool cancelled = false;
  if (mCurrentRequest) {
    mCurrentRequest->CancelAndForgetObserver(NS_BINDING_ABORTED);
    mCurrentRequest = nullptr;
    cancelled = true;
  }
  if (mPendingRequest) {
    mPendingRequest->CancelAndForgetObserver(NS_BINDING_ABORTED);
    mPendingRequest = nullptr;
    cancelled = true;
  }
  return cancelled;
}

Element* nsImageLoadingContent::FindImageMap() {
  return FindImageMap(AsContent()->AsElement());
}

/* static */ Element* nsImageLoadingContent::FindImageMap(Element* aElement) {
  nsAutoString useMap;
  aElement->GetAttr(nsGkAtoms::usemap, useMap);
  if (useMap.IsEmpty()) {
    return nullptr;
  }

  nsAString::const_iterator start, end;
  useMap.BeginReading(start);
  useMap.EndReading(end);

  int32_t hash = useMap.FindChar('#');
  if (hash < 0) {
    return nullptr;
  }
  // useMap contains a '#', set start to point right after the '#'
  start.advance(hash + 1);

  if (start == end) {
    return nullptr;  // useMap == "#"
  }

  RefPtr<ContentList> imageMapList;
  if (aElement->IsInUncomposedDoc()) {
    // Optimize the common case and use document level image map.
    imageMapList = aElement->OwnerDoc()->ImageMapList();
  } else {
    // Per HTML spec image map should be searched in the element's scope,
    // so using SubtreeRoot() here.
    // Because this is a temporary list, we don't need to make it live.
    imageMapList =
        new ContentList(aElement->SubtreeRoot(), kNameSpaceID_XHTML,
                        nsGkAtoms::map, nsGkAtoms::map, true, /* deep */
                        false /* live */);
  }

  nsAutoString mapName(Substring(start, end));

  const uint32_t n = imageMapList->Length();
  for (uint32_t i = 0; i < n; ++i) {
    nsIContent* map = imageMapList->Item(i);
    if (map->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, mapName,
                                      eCaseMatters) ||
        map->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name,
                                      mapName, eCaseMatters)) {
      return map->AsElement();
    }
  }

  return nullptr;
}

nsLoadFlags nsImageLoadingContent::LoadFlags() {
  auto* image = HTMLImageElement::FromNode(AsContent());
  if (image && image->OwnerDoc()->IsScriptEnabled() &&
      !image->OwnerDoc()->IsStaticDocument() &&
      image->LoadingState() == Element::Loading::Lazy) {
    // Note that LOAD_BACKGROUND is not about priority of the load, but about
    // whether it blocks the load event (by bypassing the loadgroup).
    return nsIRequest::LOAD_BACKGROUND;
  }
  return nsIRequest::LOAD_NORMAL;
}

FetchPriority nsImageLoadingContent::GetFetchPriorityForImage() const {
  return FetchPriority::Auto;
}
