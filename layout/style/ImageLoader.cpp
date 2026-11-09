/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A class that handles style system image loads (other image loads are handled
 * by the nodes in the content tree).
 */

#include "mozilla/css/ImageLoader.h"

#include "Image.h"
#include "imgIContainer.h"
#include "imgINotificationObserver.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/LargestContentfulPaint.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "nsCanvasFrame.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsIFrameInlines.h"
#include "nsIReflowCallback.h"
#include "nsLayoutUtils.h"
#include "nsTHashSet.h"

using namespace mozilla::dom;

namespace mozilla::css {

// This is a singleton observer which looks in the `GlobalRequestTable` to look
// at which loaders to notify.
struct GlobalImageObserver final : public imgINotificationObserver {
  NS_DECL_ISUPPORTS
  NS_DECL_IMGINOTIFICATIONOBSERVER

  GlobalImageObserver() = default;

 private:
  virtual ~GlobalImageObserver() = default;
};

NS_IMPL_ADDREF(GlobalImageObserver)
NS_IMPL_RELEASE(GlobalImageObserver)

NS_INTERFACE_MAP_BEGIN(GlobalImageObserver)
  NS_INTERFACE_MAP_ENTRY(imgINotificationObserver)
NS_INTERFACE_MAP_END

// Data associated with every started load.
struct ImageTableEntry {
  // Set of all ImageLoaders that have registered this URL and care for updates
  // for it.
  nsTHashSet<ImageLoader*> mImageLoaders;

  // The amount of style values that are sharing this image.
  uint32_t mSharedCount = 1;
};

using GlobalRequestTable =
    nsClassHashtable<nsRefPtrHashKey<imgIRequest>, ImageTableEntry>;

// A table of all loads, keyed by their id mapping them to the set of
// ImageLoaders they have been registered in, and recording their "canonical"
// image request.
//
// We use the load id as the key since we can only access sImages on the
// main thread, but LoadData objects might be destroyed from other threads,
// and we don't want to leave dangling pointers around.
static StaticAutoPtr<GlobalRequestTable> sImages;
static StaticRefPtr<GlobalImageObserver> sImageObserver;

/* static */
void ImageLoader::Init() {
  sImages = new GlobalRequestTable();
  sImageObserver = MakeRefPtr<GlobalImageObserver>();
}

/* static */
void ImageLoader::Shutdown() {
  for (const auto& entry : *sImages) {
    imgIRequest* imgRequest = entry.GetKey();
    // All the images we put in sImages are imgRequestProxy, see LoadImage, but
    // it's non-trivial to make the hash table to use that without changing a
    // lot of other code.
    auto* req = static_cast<imgRequestProxy*>(imgRequest);
    req->SetCancelable(true);
    req->CancelAndForgetObserver(NS_BINDING_ABORTED);
  }

  sImages = nullptr;
  sImageObserver = nullptr;
}

void ImageLoader::DropDocumentReference() {
  MOZ_ASSERT(NS_IsMainThread());

  // It's okay if GetPresContext returns null here (due to the presshell pointer
  // on the document being null) as that means the presshell has already
  // been destroyed, and it also calls ClearFrames when it is destroyed.
  ClearFrames(GetPresContext());

  mDocument = nullptr;
}

// Arrays of requests and frames are sorted by their pointer address,
// for faster lookup.
template <typename Elem, typename Item,
          typename Comparator = nsDefaultComparator<Elem, Item>>
static size_t GetMaybeSortedIndex(const nsTArray<Elem>& aArray,
                                  const Item& aItem, bool* aFound,
                                  Comparator aComparator = Comparator()) {
  size_t index = aArray.IndexOfFirstElementGt(aItem, aComparator);
  *aFound = index > 0 && aComparator.Equals(aItem, aArray.ElementAt(index - 1));
  return index;
}

// Returns true if an async decode is triggered for aRequest, and thus we will
// get an OnFrameComplete callback for this request eventually.
static bool TriggerAsyncDecodeAtIntrinsicSize(imgIRequest* aRequest) {
  uint32_t status = 0;
  // Don't block onload if we've already got a frame complete status
  // (since in that case the image is already loaded), or if we get an
  // error status (since then we know the image won't ever load).
  if (NS_SUCCEEDED(aRequest->GetImageStatus(&status))) {
    if (status & imgIRequest::STATUS_FRAME_COMPLETE) {
      // Already decoded, no need to do it again.
      return false;
    }
    if (status & imgIRequest::STATUS_ERROR) {
      // Already errored, this would be useless.
      return false;
    }
  }

  // We want to request decode in such a way that avoids triggering sync decode.
  // First, we attempt to convert the aRequest into a imgIContainer. If that
  // succeeds, then aRequest has an image and we can request decoding for size
  // at zero size, the size will be ignored because we don't pass the
  // FLAG_HIGH_QUALITY_SCALING flag and an async decode (because we didn't pass
  // any sync decoding flags) at the intrinsic size will be requested. If the
  // conversion to imgIContainer is unsuccessful, then that means aRequest
  // doesn't have an image yet, which means we can safely call StartDecoding()
  // on it without triggering any synchronous work.
  nsCOMPtr<imgIContainer> imgContainer;
  aRequest->GetImage(getter_AddRefs(imgContainer));
  if (imgContainer) {
    imgContainer->RequestDecodeForSize(gfx::IntSize(0, 0),
                                       imgIContainer::DECODE_FLAGS_DEFAULT);
  } else {
    // It's safe to call StartDecoding directly, since it can't
    // trigger synchronous decode without an image. Flags are ignored.
    aRequest->StartDecoding(imgIContainer::FLAG_NONE);
  }
  return true;
}

void ImageLoader::AssociateRequestToFrame(imgIRequest* aRequest,
                                          nsIFrame* aFrame, Flags aFlags) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!(aFlags & Flags::IsBlockingLoadEvent),
             "Shouldn't be used in the public API");

  {
    nsCOMPtr<imgINotificationObserver> observer;
    aRequest->GetNotificationObserver(getter_AddRefs(observer));
    if (!observer) {
      // The request has already been canceled, so ignore it. This is ok because
      // we're not going to get any more notifications from a canceled request.
      return;
    }
    MOZ_ASSERT(observer == sImageObserver);
  }

  auto* const frameSet =
      mRequestToFrameMap
          .LookupOrInsertWith(
              aRequest,
              [&] {
                mDocument->TrackImage(aRequest);

                if (auto entry = sImages->Lookup(aRequest)) {
                  DebugOnly<bool> inserted =
                      entry.Data()->mImageLoaders.EnsureInserted(this);
                  MOZ_ASSERT(inserted);
                } else {
                  MOZ_ASSERT_UNREACHABLE(
                      "Shouldn't be associating images not in sImages");
                }

                if (nsPresContext* presContext = GetPresContext()) {
                  nsLayoutUtils::RegisterImageRequestIfAnimated(
                      presContext, aRequest, nullptr);
                }
                return std::make_unique<FrameSet>();
              })
          .get();

  auto* const requestSet =
      mFrameToRequestMap
          .LookupOrInsertWith(aFrame,
                              [=]() {
                                aFrame->SetHasImageRequest(true);
                                return std::make_unique<RequestSet>();
                              })
          .get();

  // Add frame to the frameSet, and handle any special processing the
  // frame might require.
  FrameWithFlags fwf(aFrame);
  FrameWithFlags* fwfToModify = &fwf;

  // See if the frameSet already has this frame.
  bool found;
  uint32_t i =
      GetMaybeSortedIndex(*frameSet, fwf, &found, FrameOnlyComparator());
  if (found) {
    // We're already tracking this frame, so prepare to modify the
    // existing FrameWithFlags object.
    fwfToModify = &frameSet->ElementAt(i - 1);
  }

  // Check if the frame requires special processing.
  if (aFlags & Flags::RequiresReflowOnSizeAvailable) {
    MOZ_ASSERT(!(aFlags &
                 Flags::RequiresReflowOnFirstFrameCompleteAndLoadEventBlocking),
               "These two are exclusive");
    fwfToModify->mFlags |= Flags::RequiresReflowOnSizeAvailable;
  }

  if (aFlags & Flags::RequiresReflowOnFirstFrameCompleteAndLoadEventBlocking) {
    fwfToModify->mFlags |=
        Flags::RequiresReflowOnFirstFrameCompleteAndLoadEventBlocking;

    // If we weren't already blocking onload, do that now.
    if (!(fwfToModify->mFlags & Flags::IsBlockingLoadEvent)) {
      if (TriggerAsyncDecodeAtIntrinsicSize(aRequest)) {
        // If there's no error, and the image has not loaded yet, so we can
        // block onload.
        fwfToModify->mFlags |= Flags::IsBlockingLoadEvent;

        // Block document onload until we either remove the frame in
        // RemoveRequestToFrameMapping or onLoadComplete, or complete a reflow.
        mDocument->BlockOnload();
      }
    }
  }

  // Do some sanity checking to ensure that we only add to one mapping
  // iff we also add to the other mapping.
  DebugOnly<bool> didAddToFrameSet(false);
  DebugOnly<bool> didAddToRequestSet(false);

  // If we weren't already tracking this frame, add it to the frameSet.
  if (!found) {
    frameSet->InsertElementAt(i, fwf);
    didAddToFrameSet = true;
  }

  // Add request to the request set if it wasn't already there.
  i = GetMaybeSortedIndex(*requestSet, aRequest, &found);
  if (!found) {
    requestSet->InsertElementAt(i, aRequest);
    didAddToRequestSet = true;
  }

  MOZ_ASSERT(didAddToFrameSet == didAddToRequestSet,
             "We should only add to one map iff we also add to the other map.");
}

void ImageLoader::RemoveRequestToFrameMapping(imgIRequest* aRequest,
                                              nsIFrame* aFrame) {
#ifdef DEBUG
  {
    nsCOMPtr<imgINotificationObserver> observer;
    aRequest->GetNotificationObserver(getter_AddRefs(observer));
    MOZ_ASSERT(!observer || observer == sImageObserver);
  }
#endif

  if (auto entry = mRequestToFrameMap.Lookup(aRequest)) {
    const auto& frameSet = entry.Data();
    MOZ_ASSERT(frameSet, "This should never be null");

    // Before we remove aFrame from the frameSet, unblock onload if needed.
    bool found;
    uint32_t i = GetMaybeSortedIndex(*frameSet, FrameWithFlags(aFrame), &found,
                                     FrameOnlyComparator());
    if (found) {
      UnblockOnloadIfNeeded(frameSet->ElementAt(i - 1));
      frameSet->RemoveElementAtUnsafe(i - 1);
    }

    if (frameSet->IsEmpty()) {
      DeregisterImageRequest(aRequest, GetPresContext());
      entry.Remove();
    }
  }
}

void ImageLoader::DeregisterImageRequest(imgIRequest* aRequest,
                                         nsPresContext* aPresContext) {
  mDocument->UntrackImage(aRequest);

  if (auto entry = sImages->Lookup(aRequest)) {
    entry.Data()->mImageLoaders.EnsureRemoved(this);
  }

  if (aPresContext) {
    nsLayoutUtils::DeregisterImageRequest(aPresContext, aRequest, nullptr);
  }
}

void ImageLoader::RemoveFrameToRequestMapping(imgIRequest* aRequest,
                                              nsIFrame* aFrame) {
  if (auto entry = mFrameToRequestMap.Lookup(aFrame)) {
    const auto& requestSet = entry.Data();
    MOZ_ASSERT(requestSet, "This should never be null");
    requestSet->RemoveElementSorted(aRequest);
    if (requestSet->IsEmpty()) {
      aFrame->SetHasImageRequest(false);
      entry.Remove();
    }
  }
}

void ImageLoader::DisassociateRequestFromFrame(imgIRequest* aRequest,
                                               nsIFrame* aFrame) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aFrame->HasImageRequest(), "why call me?");

  RemoveRequestToFrameMapping(aRequest, aFrame);
  RemoveFrameToRequestMapping(aRequest, aFrame);
}

void ImageLoader::DropRequestsForFrame(nsIFrame* aFrame) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aFrame->HasImageRequest(), "why call me?");

  UniquePtr<RequestSet> requestSet;
  mFrameToRequestMap.Remove(aFrame, &requestSet);
  aFrame->SetHasImageRequest(false);
  if (MOZ_UNLIKELY(!requestSet)) {
    MOZ_ASSERT_UNREACHABLE("HasImageRequest was lying");
    return;
  }
  for (imgIRequest* request : *requestSet) {
    RemoveRequestToFrameMapping(request, aFrame);
  }
}

void ImageLoader::SetAnimationMode(uint16_t aMode) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ASSERTION(aMode == imgIContainer::kNormalAnimMode ||
                   aMode == imgIContainer::kDontAnimMode ||
                   aMode == imgIContainer::kLoopOnceAnimMode,
               "Wrong Animation Mode is being set!");

  for (nsISupports* key : mRequestToFrameMap.Keys()) {
    auto* request = static_cast<imgIRequest*>(key);

#ifdef DEBUG
    {
      nsCOMPtr<imgIRequest> debugRequest = request;
      NS_ASSERTION(debugRequest == request, "This is bad");
    }
#endif

    nsCOMPtr<imgIContainer> container;
    request->GetImage(getter_AddRefs(container));
    if (!container) {
      continue;
    }

    // This can fail if the image is in error, and we don't care.
    container->SetAnimationMode(aMode);
  }
}

void ImageLoader::ClearFrames(nsPresContext* aPresContext) {
  MOZ_ASSERT(NS_IsMainThread());

  for (const auto& key : mRequestToFrameMap.Keys()) {
    auto* request = static_cast<imgIRequest*>(key);

#ifdef DEBUG
    {
      nsCOMPtr<imgIRequest> debugRequest = request;
      NS_ASSERTION(debugRequest == request, "This is bad");
    }
#endif

    DeregisterImageRequest(request, aPresContext);
  }

  mRequestToFrameMap.Clear();
  mFrameToRequestMap.Clear();
}

static CORSMode EffectiveCorsMode(nsIURI* aURI,
                                  const StyleComputedUrl& aImage) {
  MOZ_ASSERT(aURI);
  StyleCorsMode mode = aImage.CorsMode();
  if (mode == StyleCorsMode::None) {
    return CORSMode::CORS_NONE;
  }
  MOZ_ASSERT(mode == StyleCorsMode::Anonymous);
  if (aURI->SchemeIs("resource")) {
    return CORSMode::CORS_NONE;
  }
  return CORSMode::CORS_ANONYMOUS;
}

/* static */
already_AddRefed<imgRequestProxy> ImageLoader::LoadImage(
    const StyleComputedUrl& aImage, Document& aDocument) {
  MOZ_ASSERT(NS_IsMainThread());
  nsIURI* uri = aImage.GetURI();
  if (!uri) {
    return nullptr;
  }

  if (aImage.HasRef()) {
    bool isEqualExceptRef = false;
    nsIURI* docURI = aDocument.GetDocumentURI();
    if (NS_SUCCEEDED(uri->EqualsExceptRef(docURI, &isEqualExceptRef)) &&
        isEqualExceptRef) {
      // Prevent loading an internal resource.
      return nullptr;
    }
  }

  int32_t loadFlags =
      nsIRequest::LOAD_NORMAL |
      nsContentUtils::CORSModeToLoadImageFlags(EffectiveCorsMode(uri, aImage));

  const URLExtraData& data = aImage.ExtraData();

  const nsLiteralString initiatorType =
      aImage.Initiator() == StyleUrlInitiator::Body ? u"body"_ns : u"css"_ns;

  RefPtr<imgRequestProxy> request;
  nsresult rv = nsContentUtils::LoadImage(
      uri, &aDocument, &aDocument, data.Principal(), 0, data.ReferrerInfo(),
      sImageObserver, loadFlags, initiatorType, getter_AddRefs(request));

  if (NS_FAILED(rv) || !request) {
    return nullptr;
  }

  // This image could be shared across documents, so its load cannot be
  // canceled, see bug 1800979.
  request->SetCancelable(false);
  sImages->GetOrInsertNew(request);
  return request.forget();
}

void ImageLoader::UnloadImage(imgRequestProxy* aImage) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aImage);

  if (MOZ_UNLIKELY(!sImages)) {
    return;  // Shutdown() takes care of it.
  }

  auto lookup = sImages->Lookup(aImage);
  MOZ_DIAGNOSTIC_ASSERT(lookup, "Unregistered image?");
  if (MOZ_UNLIKELY(!lookup)) {
    return;
  }

  if (MOZ_UNLIKELY(--lookup.Data()->mSharedCount)) {
    // Someone else still cares about this image.
    return;
  }

  // Now we want to really cancel the request.
  aImage->SetCancelable(true);
  aImage->CancelAndForgetObserver(NS_BINDING_ABORTED);
  MOZ_DIAGNOSTIC_ASSERT(lookup.Data()->mImageLoaders.IsEmpty(),
                        "Shouldn't be keeping references to any loader "
                        "by now");
  lookup.Remove();
}

void ImageLoader::NoteSharedLoad(imgRequestProxy* aImage) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aImage);

  auto lookup = sImages->Lookup(aImage);
  MOZ_DIAGNOSTIC_ASSERT(lookup, "Unregistered image?");
  if (MOZ_UNLIKELY(!lookup)) {
    return;
  }

  lookup.Data()->mSharedCount++;
}

nsPresContext* ImageLoader::GetPresContext() {
  if (!mDocument) {
    return nullptr;
  }

  return mDocument->GetPresContext();
}

static bool IsRenderNoImages(uint32_t aDisplayItemKey) {
  DisplayItemType type = GetDisplayItemTypeFromKey(aDisplayItemKey);
  uint8_t flags = GetDisplayItemFlagsForType(type);
  return flags & TYPE_RENDERS_NO_IMAGES;
}

static void InvalidateImages(nsIFrame* aFrame, imgIRequest* aRequest,
                             bool aForcePaint) {
  if (!aFrame->StyleVisibility()->IsVisible()) {
    return;
  }

  if (aFrame->IsTablePart()) {
    // Tables don't necessarily build border/background display items
    // for the individual table part frames, so IterateRetainedDataFor
    // might not find the right display item.
    return aFrame->InvalidateFrame();
  }

  if (aFrame->ShouldPropagateRepaintsToRoot()) {
    if (auto* canvas = aFrame->PresShell()->GetCanvasFrame()) {
      // Try to invalidate the canvas too, in the probable case the background
      // was propagated to it.
      InvalidateImages(canvas, aRequest, aForcePaint);
    }
  }

  bool invalidateFrame = aForcePaint;

  if (auto userDataTable =
          aFrame->GetProperty(layers::WebRenderUserDataProperty::Key())) {
    for (RefPtr<layers::WebRenderUserData> data : userDataTable->Values()) {
      switch (data->GetType()) {
        case layers::WebRenderUserData::UserDataType::eFallback:
          if (!IsRenderNoImages(data->GetDisplayItemKey())) {
            static_cast<layers::WebRenderFallbackData*>(data.get())
                ->SetInvalid(true);
          }
          // XXX: handle Blob data
          invalidateFrame = true;
          break;
        case layers::WebRenderUserData::UserDataType::eMask:
          static_cast<layers::WebRenderMaskData*>(data.get())->Invalidate();
          invalidateFrame = true;
          break;
        case layers::WebRenderUserData::UserDataType::eImageProvider:
          if (static_cast<layers::WebRenderImageProviderData*>(data.get())
                  ->Invalidate(aRequest->GetProviderId())) {
            break;
          }
          [[fallthrough]];
        default:
          invalidateFrame = true;
          break;
      }
    }
  }
#ifdef XP_MACOSX
  else if (aFrame->HasAnyStateBits(NS_FRAME_IN_POPUP)) {
    // On macOS popups are painted with fallback rendering so they don't have
    // webrender user data to tell us if the frame was painted last time, so we
    // just have to invalidate always. Bug 1754796 tracks making popups on macOS
    // use webrender. (On other platforms tooltips type popups are still
    // rendered with fallback, but we don't expect them to have images.)
    invalidateFrame = true;
  }
#endif

  // Update ancestor rendering observers (-moz-element etc)
  //
  // NOTE: We need to do this even if invalidateFrame is false, see bug 1114526.
  {
    nsIFrame* f = aFrame;
    while (f && !f->HasAnyStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT)) {
      SVGObserverUtils::InvalidateDirectRenderingObservers(f);
      f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f);
    }
  }

  if (invalidateFrame) {
    aFrame->SchedulePaint();
  }
}

void ImageLoader::UnblockOnloadIfNeeded(FrameWithFlags& aFwf) {
  if (aFwf.mFlags & Flags::IsBlockingLoadEvent) {
    mDocument->UnblockOnload(false);
    aFwf.mFlags &= ~Flags::IsBlockingLoadEvent;
  }
}

void ImageLoader::UnblockOnloadIfNeeded(nsIFrame* aFrame,
                                        imgIRequest* aRequest) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aRequest);

  FrameSet* frameSet = mRequestToFrameMap.Get(aRequest);
  if (!frameSet) {
    return;
  }

  size_t i =
      frameSet->BinaryIndexOf(FrameWithFlags(aFrame), FrameOnlyComparator());
  if (i != FrameSet::NoIndex) {
    UnblockOnloadIfNeeded(frameSet->ElementAt(i));
  }
}

// This callback is used to unblock document onload after a reflow
// triggered from an image load.
struct ImageLoader::ImageReflowCallback final : public nsIReflowCallback {
  RefPtr<ImageLoader> mLoader;
  WeakFrame mFrame;
  nsCOMPtr<imgIRequest> const mRequest;

  ImageReflowCallback(ImageLoader* aLoader, nsIFrame* aFrame,
                      imgIRequest* aRequest)
      : mLoader(aLoader), mFrame(aFrame), mRequest(aRequest) {}

  bool ReflowFinished() override;
  void ReflowCallbackCanceled() override;
};

bool ImageLoader::ImageReflowCallback::ReflowFinished() {
  // Check that the frame is still valid. If it isn't, then onload was
  // unblocked when the frame was removed from the FrameSet in
  // RemoveRequestToFrameMapping.
  if (mFrame.IsAlive()) {
    mLoader->UnblockOnloadIfNeeded(mFrame, mRequest);
  }

  // Get rid of this callback object.
  delete this;

  // We don't need to trigger layout.
  return false;
}

void ImageLoader::ImageReflowCallback::ReflowCallbackCanceled() {
  // Check that the frame is still valid. If it isn't, then onload was
  // unblocked when the frame was removed from the FrameSet in
  // RemoveRequestToFrameMapping.
  if (mFrame.IsAlive()) {
    mLoader->UnblockOnloadIfNeeded(mFrame, mRequest);
  }

  // Get rid of this callback object.
  delete this;
}

void GlobalImageObserver::Notify(imgIRequest* aRequest, int32_t aType,
                                 const nsIntRect* aData) {
  auto entry = sImages->Lookup(aRequest);
  MOZ_DIAGNOSTIC_ASSERT(entry);
  if (MOZ_UNLIKELY(!entry)) {
    return;
  }

  const auto loadersToNotify =
      ToTArray<nsTArray<RefPtr<ImageLoader>>>(entry.Data()->mImageLoaders);
  for (const auto& loader : loadersToNotify) {
    loader->Notify(aRequest, aType, aData);
  }
}

void ImageLoader::Notify(imgIRequest* aRequest, int32_t aType,
                         const nsIntRect* aData) {
  nsCString uriString;
  if (profiler_is_active()) {
    nsCOMPtr<nsIURI> uri;
    aRequest->GetFinalURI(getter_AddRefs(uri));
    if (uri) {
      uri->GetSpec(uriString);
    }
  }

  AUTO_PROFILER_LABEL_DYNAMIC_CSTR("ImageLoader::Notify", OTHER,
                                   uriString.get());

  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    nsCOMPtr<imgIContainer> image;
    aRequest->GetImage(getter_AddRefs(image));
    return OnSizeAvailable(aRequest, image);
  }

  if (aType == imgINotificationObserver::IS_ANIMATED) {
    return OnImageIsAnimated(aRequest);
  }

  if (aType == imgINotificationObserver::FRAME_COMPLETE) {
    return OnFrameComplete(aRequest);
  }

  if (aType == imgINotificationObserver::FRAME_UPDATE) {
    return OnFrameUpdate(aRequest);
  }

  if (aType == imgINotificationObserver::DECODE_COMPLETE) {
    nsCOMPtr<imgIContainer> image;
    aRequest->GetImage(getter_AddRefs(image));
    if (image && mDocument) {
      image->PropagateUseCounters(mDocument);
    }
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    return OnLoadComplete(aRequest);
  }
}

void ImageLoader::OnSizeAvailable(imgIRequest* aRequest,
                                  imgIContainer* aImage) {
  nsPresContext* presContext = GetPresContext();
  if (!presContext) {
    return;
  }

  aImage->SetAnimationMode(presContext->ImageAnimationMode());

  FrameSet* frameSet = mRequestToFrameMap.Get(aRequest);
  if (!frameSet) {
    return;
  }

  for (const FrameWithFlags& fwf : *frameSet) {
    if (fwf.mFlags & Flags::RequiresReflowOnSizeAvailable) {
      fwf.mFrame->PresShell()->FrameNeedsReflow(
          fwf.mFrame, IntrinsicDirty::FrameAncestorsAndDescendants,
          NS_FRAME_IS_DIRTY);
    }
  }
}

void ImageLoader::OnImageIsAnimated(imgIRequest* aRequest) {
  if (!mDocument) {
    return;
  }

  FrameSet* frameSet = mRequestToFrameMap.Get(aRequest);
  if (!frameSet) {
    return;
  }

  // Register with the refresh driver now that we are aware that
  // we are animated.
  nsPresContext* presContext = GetPresContext();
  if (presContext) {
    nsLayoutUtils::RegisterImageRequest(presContext, aRequest, nullptr);
  }
}

void ImageLoader::OnFrameComplete(imgIRequest* aRequest) {
  ImageFrameChanged(aRequest, /* aFirstFrame = */ true);
}

void ImageLoader::OnFrameUpdate(imgIRequest* aRequest) {
  ImageFrameChanged(aRequest, /* aFirstFrame = */ false);
}

void ImageLoader::ImageFrameChanged(imgIRequest* aRequest, bool aFirstFrame) {
  if (!mDocument) {
    return;
  }

  FrameSet* frameSet = mRequestToFrameMap.Get(aRequest);
  if (!frameSet) {
    return;
  }

  for (FrameWithFlags& fwf : *frameSet) {
    // Since we just finished decoding a frame, we always want to paint, in
    // case we're now able to paint an image that we couldn't paint before
    // (and hence that we don't have retained data for).
    const bool forceRepaint = aFirstFrame;
    InvalidateImages(fwf.mFrame, aRequest, forceRepaint);
    if (!aFirstFrame) {
      // We don't reflow / try to unblock onload for subsequent frame updates.
      continue;
    }
    if (fwf.mFlags &
        Flags::RequiresReflowOnFirstFrameCompleteAndLoadEventBlocking) {
      // Tell the container of the frame to reflow because the image request
      // has finished decoding its first frame.
      // FIXME(emilio): Why requesting reflow on the _parent_?
      nsIFrame* parent = fwf.mFrame->GetInFlowParent();
      parent->PresShell()->FrameNeedsReflow(
          parent, IntrinsicDirty::FrameAncestorsAndDescendants,
          NS_FRAME_IS_DIRTY);
      // If we need to also potentially unblock onload, do it once reflow is
      // done, with a reflow callback.
      if (fwf.mFlags & Flags::IsBlockingLoadEvent) {
        auto* unblocker = new ImageReflowCallback(this, fwf.mFrame, aRequest);
        parent->PresShell()->PostReflowCallback(unblocker);
      }
    }
  }
}

void ImageLoader::OnLoadComplete(imgIRequest* aRequest) {
  if (!mDocument) {
    return;
  }

  uint32_t status = 0;
  if (NS_FAILED(aRequest->GetImageStatus(&status))) {
    return;
  }

  FrameSet* frameSet = mRequestToFrameMap.Get(aRequest);
  if (!frameSet) {
    return;
  }

  for (FrameWithFlags& fwf : *frameSet) {
    if (status & imgIRequest::STATUS_ERROR) {
      // Check if aRequest has an error state. If it does, we need to unblock
      // Document onload for all the frames associated with this request that
      // have blocked onload. This is what happens in a CORS mode violation, and
      // may happen during other network events.
      UnblockOnloadIfNeeded(fwf);
    }
    nsIFrame* frame = fwf.mFrame;
    if (frame->StyleVisibility()->IsVisible()) {
      frame->SchedulePaint();
    }

    if (StaticPrefs::dom_enable_largest_contentful_paint()) {
      LargestContentfulPaint::MaybeProcessImageForElementTiming(
          static_cast<imgRequestProxy*>(aRequest),
          frame->GetContent()->AsElement());
    }
  }
}
}  // namespace mozilla::css
