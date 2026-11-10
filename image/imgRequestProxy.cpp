/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "imgRequestProxy.h"

#include <utility>

#include "Image.h"
#include "ImageLogging.h"
#include "ImageOps.h"
#include "ImageTypes.h"
#include "imgINotificationObserver.h"
#include "imgLoader.h"
#include "mozilla/dom/DocGroup.h"  // for DocGroup
#include "mozilla/dom/Document.h"
#include "nsCRTGlue.h"
#include "nsError.h"

using namespace mozilla;
using namespace mozilla::image;

// The split of imgRequestProxy and imgRequestProxyStatic means that
// certain overridden functions need to be usable in the destructor.
// Since virtual functions can't be used in that way, this class
// provides a behavioural trait for each class to use instead.
class ProxyBehaviour {
 public:
  virtual ~ProxyBehaviour() = default;

  virtual already_AddRefed<mozilla::image::Image> GetImage() const = 0;
  virtual bool HasImage() const = 0;
  virtual already_AddRefed<ProgressTracker> GetProgressTracker() const = 0;
  virtual imgRequest* GetOwner() const = 0;
  virtual void SetOwner(imgRequest* aOwner) = 0;
};

class RequestBehaviour : public ProxyBehaviour {
 public:
  RequestBehaviour() : mOwner(nullptr), mOwnerHasImage(false) {}

  already_AddRefed<mozilla::image::Image> GetImage() const override;
  bool HasImage() const override;
  already_AddRefed<ProgressTracker> GetProgressTracker() const override;

  imgRequest* GetOwner() const override { return mOwner; }

  void SetOwner(imgRequest* aOwner) override {
    mOwner = aOwner;

    if (mOwner) {
      RefPtr<ProgressTracker> ownerProgressTracker = GetProgressTracker();
      mOwnerHasImage = ownerProgressTracker && ownerProgressTracker->HasImage();
    } else {
      mOwnerHasImage = false;
    }
  }

 private:
  // We maintain the following invariant:
  // The proxy is registered at most with a single imgRequest as an observer,
  // and whenever it is, mOwner points to that object. This helps ensure that
  // imgRequestProxy::~imgRequestProxy unregisters the proxy as an observer
  // from whatever request it was registered with (if any). This, in turn,
  // means that imgRequest::mObservers will not have any stale pointers in it.
  RefPtr<imgRequest> mOwner;

  bool mOwnerHasImage;
};

already_AddRefed<mozilla::image::Image> RequestBehaviour::GetImage() const {
  if (!mOwnerHasImage) {
    return nullptr;
  }
  RefPtr<ProgressTracker> progressTracker = GetProgressTracker();
  return progressTracker->GetImage();
}

already_AddRefed<ProgressTracker> RequestBehaviour::GetProgressTracker() const {
  // NOTE: It's possible that our mOwner has an Image that it didn't notify
  // us about, if we were Canceled before its Image was constructed.
  // (Canceling removes us as an observer, so mOwner has no way to notify us).
  // That's why this method uses mOwner->GetProgressTracker() instead of just
  // mOwner->mProgressTracker -- we might have a null mImage and yet have an
  // mOwner with a non-null mImage (and a null mProgressTracker pointer).
  return mOwner->GetProgressTracker();
}

NS_IMPL_ADDREF(imgRequestProxy)
NS_IMPL_RELEASE(imgRequestProxy)

NS_INTERFACE_MAP_BEGIN(imgRequestProxy)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, PreloaderBase)
  NS_INTERFACE_MAP_ENTRY(imgIRequest)
  NS_INTERFACE_MAP_ENTRY(nsIRequest)
  NS_INTERFACE_MAP_ENTRY(nsISupportsPriority)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(imgRequestProxy)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsITimedChannel, TimedChannel() != nullptr)
NS_INTERFACE_MAP_END

imgRequestProxy::imgRequestProxy()
    : mBehaviour(new RequestBehaviour),
      mURI(nullptr),
      mListener(nullptr),
      mLoadFlags(nsIRequest::LOAD_NORMAL),
      mLockCount(0),
      mAnimationConsumers(0),
      mCancelable(true),
      mCanceled(false),
      mIsInLoadGroup(false),
      mForceDispatchLoadGroup(false),
      mListenerIsStrongRef(false),
      mDecodeRequested(false),
      mPendingNotify(false),
      mValidating(false) {
  /* member initializers and constructor code */
  LOG_FUNC(gImgLog, "imgRequestProxy::imgRequestProxy");
}

imgRequestProxy::~imgRequestProxy() {
  /* destructor code */
  MOZ_ASSERT(!mListener, "Someone forgot to properly cancel this request!");
  MOZ_RELEASE_ASSERT(!mLockCount, "Someone forgot to unlock on time?");

  ClearAnimationConsumers();

  // Explicitly set mListener to null to ensure that the RemoveProxy
  // call below can't send |this| to an arbitrary listener while |this|
  // is being destroyed.  This is all belt-and-suspenders in view of the
  // above assert.
  NullOutListener();

  /* Call RemoveProxy with a successful status.  This will keep the
     channel, if still downloading data, from being canceled if 'this' is
     the last observer.  This allows the image to continue to download and
     be cached even if no one is using it currently.
  */
  mCanceled = true;
  RemoveFromOwner(NS_OK);

  RemoveFromLoadGroup();
  LOG_FUNC(gImgLog, "imgRequestProxy::~imgRequestProxy");
}

nsresult imgRequestProxy::Init(imgRequest* aOwner, nsILoadGroup* aLoadGroup,
                               nsIURI* aURI,
                               imgINotificationObserver* aObserver) {
  MOZ_ASSERT(!GetOwner() && !mListener,
             "imgRequestProxy is already initialized");

  LOG_SCOPE_WITH_PARAM(gImgLog, "imgRequestProxy::Init", "request", aOwner);

  MOZ_ASSERT(mAnimationConsumers == 0, "Cannot have animation before Init");

  mBehaviour->SetOwner(aOwner);
  mListener = aObserver;
  // Make sure to addref mListener before the AddToOwner call below, since
  // that call might well want to release it if the imgRequest has
  // already seen OnStopRequest.
  if (mListener) {
    mListenerIsStrongRef = true;
    NS_ADDREF(mListener);
  }
  mLoadGroup = aLoadGroup;
  mURI = aURI;

  // Note: AddToOwner won't send all the On* notifications immediately
  AddToOwner();

  return NS_OK;
}

nsresult imgRequestProxy::ChangeOwner(imgRequest* aNewOwner) {
  MOZ_ASSERT(GetOwner(), "Cannot ChangeOwner on a proxy without an owner!");

  if (mCanceled) {
    // Ensure that this proxy has received all notifications to date
    // before we clean it up when removing it from the old owner below.
    SyncNotifyListener();
  }

  // If we're holding locks, unlock the old image.
  // Note that UnlockImage decrements mLockCount each time it's called.
  uint32_t oldLockCount = mLockCount;
  while (mLockCount) {
    UnlockImage();
  }

  // If we're holding animation requests, undo them.
  uint32_t oldAnimationConsumers = mAnimationConsumers;
  ClearAnimationConsumers();

  GetOwner()->RemoveProxy(this, NS_OK);

  mBehaviour->SetOwner(aNewOwner);
  MOZ_ASSERT(!GetValidator(), "New owner cannot be validating!");

  // If we were locked, apply the locks here
  for (uint32_t i = 0; i < oldLockCount; i++) {
    LockImage();
  }

  // If we had animation requests, restore them here. Note that we
  // do this *after* RemoveProxy, which clears out animation consumers
  // (see bug 601723).
  for (uint32_t i = 0; i < oldAnimationConsumers; i++) {
    IncrementAnimationConsumers();
  }

  AddToOwner();
  return NS_OK;
}

NS_IMETHODIMP imgRequestProxy::GetTriggeringPrincipal(
    nsIPrincipal** aTriggeringPrincipal) {
  MOZ_ASSERT(GetOwner());
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      GetOwner()->GetTriggeringPrincipal();
  triggeringPrincipal.forget(aTriggeringPrincipal);
  return NS_OK;
}

void imgRequestProxy::MarkValidating() {
  MOZ_ASSERT(GetValidator());
  mValidating = true;
}

void imgRequestProxy::ClearValidating() {
  MOZ_ASSERT(mValidating);
  MOZ_ASSERT(!GetValidator());
  mValidating = false;

  // If we'd previously requested a synchronous decode, request a decode on the
  // new image.
  if (mDecodeRequested) {
    mDecodeRequested = false;
    StartDecoding(imgIContainer::FLAG_NONE);
  }
}

bool imgRequestProxy::HasDecodedPixels() {
  if (IsValidating()) {
    return false;
  }

  RefPtr<Image> image = GetImage();
  if (image) {
    return image->HasDecodedPixels();
  }

  return false;
}

nsresult imgRequestProxy::DispatchWithTargetIfAvailable(
    already_AddRefed<nsIRunnable> aEvent) {
  LOG_FUNC(gImgLog, "imgRequestProxy::DispatchWithTargetIfAvailable");
  return NS_DispatchToMainThread(
      CreateRenderBlockingRunnable(std::move(aEvent)));
}

void imgRequestProxy::AddToOwner() {
  imgRequest* owner = GetOwner();
  if (!owner) {
    return;
  }

  owner->AddProxy(this);
}

void imgRequestProxy::RemoveFromOwner(nsresult aStatus) {
  imgRequest* owner = GetOwner();
  if (owner) {
    if (mValidating) {
      imgCacheValidator* validator = owner->GetValidator();
      MOZ_ASSERT(validator);
      validator->RemoveProxy(this);
      mValidating = false;
    }

    owner->RemoveProxy(this, aStatus);
  }
}

void imgRequestProxy::AddToLoadGroup() {
  NS_ASSERTION(!mIsInLoadGroup, "Whaa, we're already in the loadgroup!");
  MOZ_ASSERT(!mForceDispatchLoadGroup);

  /* While in theory there could be a dispatch outstanding to remove this
     request from the load group, in practice we only add to the load group
     (when previously not in a load group) at initialization. */
  if (!mIsInLoadGroup && mLoadGroup) {
    LOG_FUNC(gImgLog, "imgRequestProxy::AddToLoadGroup");
    mLoadGroup->AddRequest(this, nullptr);
    mIsInLoadGroup = true;
  }
}

void imgRequestProxy::RemoveFromLoadGroup() {
  if (!mIsInLoadGroup || !mLoadGroup) {
    return;
  }

  /* Sometimes we may not be able to remove ourselves from the load group in
     the current context. This is because our listeners are not re-entrant (e.g.
     we are in the middle of CancelAndForgetObserver or SyncClone). */
  if (mForceDispatchLoadGroup) {
    LOG_FUNC(gImgLog, "imgRequestProxy::RemoveFromLoadGroup -- dispatch");

    /* We take away the load group from the request temporarily; this prevents
       additional dispatches via RemoveFromLoadGroup occurring, as well as
       MoveToBackgroundInLoadGroup from removing and readding. This is safe
       because we know that once we get here, blocking the load group at all is
       unnecessary. */
    mIsInLoadGroup = false;
    nsCOMPtr<nsILoadGroup> loadGroup = std::move(mLoadGroup);
    RefPtr<imgRequestProxy> self(this);
    DispatchWithTargetIfAvailable(NS_NewRunnableFunction(
        "imgRequestProxy::RemoveFromLoadGroup", [self, loadGroup]() -> void {
          loadGroup->RemoveRequest(self, nullptr, NS_OK);
        }));
    return;
  }

  LOG_FUNC(gImgLog, "imgRequestProxy::RemoveFromLoadGroup");

  /* calling RemoveFromLoadGroup may cause the document to finish
     loading, which could result in our death.  We need to make sure
     that we stay alive long enough to fight another battle... at
     least until we exit this function. */
  nsCOMPtr<imgIRequest> kungFuDeathGrip(this);
  mLoadGroup->RemoveRequest(this, nullptr, NS_OK);
  mLoadGroup = nullptr;
  mIsInLoadGroup = false;
}

void imgRequestProxy::MoveToBackgroundInLoadGroup() {
  /* Even if we are still in the load group, we may have taken away the load
     group reference itself because we are in the process of leaving the group.
     In that case, there is no need to background the request. */
  if (!mLoadGroup) {
    return;
  }

  /* There is no need to dispatch if we only need to add ourselves to the load
     group without removal. It is the removal which causes the problematic
     callbacks (see RemoveFromLoadGroup). */
  if (mIsInLoadGroup && mForceDispatchLoadGroup) {
    LOG_FUNC(gImgLog,
             "imgRequestProxy::MoveToBackgroundInLoadGroup -- dispatch");

    RefPtr<imgRequestProxy> self(this);
    DispatchWithTargetIfAvailable(NS_NewRunnableFunction(
        "imgRequestProxy::MoveToBackgroundInLoadGroup",
        [self]() -> void { self->MoveToBackgroundInLoadGroup(); }));
    return;
  }

  LOG_FUNC(gImgLog, "imgRequestProxy::MoveToBackgroundInLoadGroup");
  nsCOMPtr<imgIRequest> kungFuDeathGrip(this);
  if (mIsInLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, NS_OK);
  }

  mLoadFlags |= nsIRequest::LOAD_BACKGROUND;
  mLoadGroup->AddRequest(this, nullptr);
}

/**  nsIRequest / imgIRequest methods **/

NS_IMETHODIMP
imgRequestProxy::GetName(nsACString& aName) {
  aName.Truncate();

  if (mURI) {
    mURI->GetSpec(aName);
  }

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::IsPending(bool* /*_retval*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
imgRequestProxy::GetStatus(nsresult* /*aStatus*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP imgRequestProxy::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP imgRequestProxy::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP imgRequestProxy::CancelWithReason(nsresult aStatus,
                                                const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

void imgRequestProxy::SetCancelable(bool aCancelable) {
  MOZ_ASSERT(NS_IsMainThread());
  mCancelable = aCancelable;
}

NS_IMETHODIMP
imgRequestProxy::Cancel(nsresult status) {
  if (mCanceled) {
    return NS_ERROR_FAILURE;
  }

  // Non-cancelable requests, like CSS images, are still in the load group, so
  // canceling the load group tries to cancel them too.
  if (!mCancelable) {
    return NS_ERROR_FAILURE;
  }

  LOG_SCOPE(gImgLog, "imgRequestProxy::Cancel");

  mCanceled = true;

  nsCOMPtr<nsIRunnable> ev = new imgCancelRunnable(this, status);
  return DispatchWithTargetIfAvailable(ev.forget());
}

void imgRequestProxy::DoCancel(nsresult status) {
  RemoveFromOwner(status);
  RemoveFromLoadGroup();
  NullOutListener();
}

NS_IMETHODIMP
imgRequestProxy::CancelAndForgetObserver(nsresult aStatus) {
  // If mCanceled is true but mListener is non-null, that means
  // someone called Cancel() on us but the imgCancelRunnable is still
  // pending.  We still need to null out mListener before returning
  // from this function in this case.  That means we want to do the
  // RemoveProxy call right now, because we need to deliver the
  // onStopRequest.
  if (mCanceled && !mListener) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(!mCancelable)) {
    MOZ_ASSERT(mCancelable,
               "Shouldn't try to cancel non-cancelable requests via "
               "CancelAndForgetObserver");
    return NS_ERROR_FAILURE;
  }

  LOG_SCOPE(gImgLog, "imgRequestProxy::CancelAndForgetObserver");

  mCanceled = true;
  mForceDispatchLoadGroup = true;
  RemoveFromOwner(aStatus);
  RemoveFromLoadGroup();
  mForceDispatchLoadGroup = false;

  NullOutListener();

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::StartDecoding(uint32_t aFlags) {
  // Flag this, so we know to request after validation if pending.
  if (IsValidating()) {
    mDecodeRequested = true;
    return NS_OK;
  }

  RefPtr<Image> image = GetImage();
  if (image) {
    return image->StartDecoding(aFlags);
  }

  if (GetOwner()) {
    GetOwner()->StartDecoding();
  }

  return NS_OK;
}

bool imgRequestProxy::StartDecodingWithResult(uint32_t aFlags) {
  // Flag this, so we know to request after validation if pending.
  if (IsValidating()) {
    mDecodeRequested = true;
    return false;
  }

  RefPtr<Image> image = GetImage();
  if (image) {
    return image->StartDecodingWithResult(aFlags);
  }

  if (GetOwner()) {
    GetOwner()->StartDecoding();
  }

  return false;
}

imgIContainer::DecodeResult imgRequestProxy::RequestDecodeWithResult(
    uint32_t aFlags) {
  if (IsValidating()) {
    mDecodeRequested = true;
    return imgIContainer::DECODE_REQUESTED;
  }

  RefPtr<Image> image = GetImage();
  if (image) {
    return image->RequestDecodeWithResult(aFlags);
  }

  if (GetOwner()) {
    GetOwner()->StartDecoding();
  }

  return imgIContainer::DECODE_REQUESTED;
}

NS_IMETHODIMP
imgRequestProxy::GetHasAnimationConsumers(bool* aIsLocked) {
  *aIsLocked = !!mAnimationConsumers;
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::LockImage() {
  mLockCount++;
  RefPtr<Image> image =
      GetOwner() && GetOwner()->ImageAvailable() ? GetImage() : nullptr;
  if (image) {
    return image->LockImage();
  }
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::UnlockImage() {
  MOZ_ASSERT(mLockCount > 0, "calling unlock but no locks!");

  mLockCount--;
  RefPtr<Image> image =
      GetOwner() && GetOwner()->ImageAvailable() ? GetImage() : nullptr;
  if (image) {
    return image->UnlockImage();
  }
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::RequestDiscard() {
  RefPtr<Image> image = GetImage();
  if (image) {
    return image->RequestDiscard();
  }
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::IncrementAnimationConsumers() {
  mAnimationConsumers++;
  RefPtr<Image> image =
      GetOwner() && GetOwner()->ImageAvailable() ? GetImage() : nullptr;
  if (image) {
    image->IncrementAnimationConsumers();
  }
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::DecrementAnimationConsumers() {
  // We may get here if some responsible code called Increment,
  // then called us, but we have meanwhile called ClearAnimationConsumers
  // because we needed to get rid of them earlier (see
  // imgRequest::RemoveProxy), and hence have nothing left to
  // decrement. (In such a case we got rid of the animation consumers
  // early, but not the observer.)
  if (mAnimationConsumers > 0) {
    mAnimationConsumers--;
    RefPtr<Image> image =
        GetOwner() && GetOwner()->ImageAvailable() ? GetImage() : nullptr;
    if (image) {
      image->DecrementAnimationConsumers();
    }
  }
  return NS_OK;
}

void imgRequestProxy::ClearAnimationConsumers() {
  while (mAnimationConsumers > 0) {
    DecrementAnimationConsumers();
  }
}

NS_IMETHODIMP
imgRequestProxy::Suspend() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
imgRequestProxy::Resume() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
imgRequestProxy::GetLoadGroup(nsILoadGroup** loadGroup) {
  NS_IF_ADDREF(*loadGroup = mLoadGroup.get());
  return NS_OK;
}
NS_IMETHODIMP
imgRequestProxy::SetLoadGroup(nsILoadGroup* loadGroup) {
  if (loadGroup != mLoadGroup) {
    MOZ_ASSERT_UNREACHABLE("Switching load groups is unsupported!");
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetLoadFlags(nsLoadFlags* flags) {
  *flags = mLoadFlags;
  return NS_OK;
}
NS_IMETHODIMP
imgRequestProxy::SetLoadFlags(nsLoadFlags flags) {
  mLoadFlags = flags;
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  return GetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
imgRequestProxy::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  return SetTRRModeImpl(aTRRMode);
}

/**  imgIRequest methods **/

NS_IMETHODIMP
imgRequestProxy::GetImage(imgIContainer** aImage) {
  NS_ENSURE_TRUE(aImage, NS_ERROR_NULL_POINTER);
  // It's possible that our owner has an image but hasn't notified us of it -
  // that'll happen if we get Canceled before the owner instantiates its image
  // (because Canceling unregisters us as a listener on mOwner). If we're
  // in that situation, just grab the image off of mOwner.
  RefPtr<Image> image = GetImage();
  nsCOMPtr<imgIContainer> imageToReturn;
  if (image) {
    imageToReturn = image;
  }
  if (!imageToReturn && GetOwner()) {
    imageToReturn = GetOwner()->GetImage();
  }
  if (!imageToReturn) {
    return NS_ERROR_FAILURE;
  }

  imageToReturn.swap(*aImage);

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetProviderId(uint32_t* aId) {
  NS_ENSURE_TRUE(aId, NS_ERROR_NULL_POINTER);

  nsCOMPtr<imgIContainer> image;
  nsresult rv = GetImage(getter_AddRefs(image));
  if (NS_SUCCEEDED(rv)) {
    *aId = image->GetProviderId();
  } else {
    *aId = 0;
  }

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetImageStatus(uint32_t* aStatus) {
  if (IsValidating()) {
    // We are currently validating the image, and so our status could revert if
    // we discard the cache. We should also be deferring notifications, such
    // that the caller will be notified when validation completes. Rather than
    // risk misleading the caller, return nothing.
    *aStatus = imgIRequest::STATUS_NONE;
  } else {
    RefPtr<ProgressTracker> progressTracker = GetProgressTracker();
    *aStatus = progressTracker->GetImageStatus();
  }

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetImageErrorCode(nsresult* aStatus) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  *aStatus = GetOwner()->GetImageErrorCode();

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetURI(nsIURI** aURI) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be on main thread to convert URI");
  nsCOMPtr<nsIURI> uri = mURI;
  uri.forget(aURI);
  return NS_OK;
}

nsresult imgRequestProxy::GetFinalURI(nsIURI** aURI) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  return GetOwner()->GetFinalURI(aURI);
}

NS_IMETHODIMP
imgRequestProxy::GetNotificationObserver(imgINotificationObserver** aObserver) {
  *aObserver = mListener;
  NS_IF_ADDREF(*aObserver);
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetMimeType(char** aMimeType) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  const char* type = GetOwner()->GetMimeType();
  if (!type) {
    return NS_ERROR_FAILURE;
  }

  *aMimeType = NS_xstrdup(type);

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetFileName(nsACString& aFileName) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  GetOwner()->GetFileName(aFileName);
  return NS_OK;
}

already_AddRefed<imgRequestProxy> imgRequestProxy::NewClonedProxy() {
  return mozilla::MakeAndAddRef<imgRequestProxy>();
}

NS_IMETHODIMP
imgRequestProxy::Clone(imgINotificationObserver* aObserver,
                       imgIRequest** aClone) {
  nsresult result;
  imgRequestProxy* proxy;
  result = PerformClone(aObserver, nullptr, /* aSyncNotify */ true, &proxy);
  *aClone = proxy;
  return result;
}

nsresult imgRequestProxy::SyncClone(imgINotificationObserver* aObserver,
                                    Document* aLoadingDocument,
                                    imgRequestProxy** aClone) {
  return PerformClone(aObserver, aLoadingDocument,
                      /* aSyncNotify */ true, aClone);
}

nsresult imgRequestProxy::Clone(imgINotificationObserver* aObserver,
                                Document* aLoadingDocument,
                                imgRequestProxy** aClone) {
  return PerformClone(aObserver, aLoadingDocument,
                      /* aSyncNotify */ false, aClone);
}

nsresult imgRequestProxy::PerformClone(imgINotificationObserver* aObserver,
                                       Document* aLoadingDocument,
                                       bool aSyncNotify,
                                       imgRequestProxy** aClone) {
  MOZ_ASSERT(aClone, "Null out param");

  LOG_SCOPE(gImgLog, "imgRequestProxy::Clone");

  *aClone = nullptr;
  RefPtr<imgRequestProxy> clone = NewClonedProxy();

  nsCOMPtr<nsILoadGroup> loadGroup;
  if (aLoadingDocument) {
    loadGroup = aLoadingDocument->GetDocumentLoadGroup();
  }

  // It is important to call |SetLoadFlags()| before calling |Init()| because
  // |Init()| adds the request to the loadgroup.
  // When a request is added to a loadgroup, its load flags are merged
  // with the load flags of the loadgroup.
  // XXXldb That's not true anymore.  Stuff from imgLoader adds the
  // request to the loadgroup.
  clone->SetLoadFlags(mLoadFlags);
  nsresult rv = clone->Init(mBehaviour->GetOwner(), loadGroup, mURI, aObserver);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Assign to *aClone before calling Notify so that if the caller expects to
  // only be notified for requests it's already holding pointers to it won't be
  // surprised.
  NS_ADDREF(*aClone = clone);

  imgCacheValidator* validator = GetValidator();
  if (validator) {
    // Note that if we have a validator, we don't want to issue notifications at
    // here because we want to defer until that completes. AddProxy will add us
    // to the load group; we cannot avoid that in this case, because we don't
    // know when the validation will complete, and if it will cause us to
    // discard our cached state anyways. We are probably already blocked by the
    // original LoadImage(WithChannel) request in any event.
    clone->MarkValidating();
    validator->AddProxy(clone);
  } else {
    // We only want to add the request to the load group of the owning document
    // if it is still in progress. Some callers cannot handle a supurious load
    // group removal (e.g. print preview) so we must be careful. On the other
    // hand, if after cloning, the original request proxy is cancelled /
    // destroyed, we need to ensure that any clones still block the load group
    // if it is incomplete.
    bool addToLoadGroup = mIsInLoadGroup;
    if (!addToLoadGroup) {
      RefPtr<ProgressTracker> tracker = clone->GetProgressTracker();
      addToLoadGroup =
          tracker && !(tracker->GetProgress() & FLAG_LOAD_COMPLETE);
    }

    if (addToLoadGroup) {
      clone->AddToLoadGroup();
    }

    if (aSyncNotify) {
      // This is wrong!!! We need to notify asynchronously, but there's code
      // that assumes that we don't. This will be fixed in bug 580466. Note that
      // if we have a validator, we won't issue notifications anyways because
      // they are deferred, so there is no point in requesting.
      clone->mForceDispatchLoadGroup = true;
      clone->SyncNotifyListener();
      clone->mForceDispatchLoadGroup = false;
    } else {
      // Without a validator, we can request asynchronous notifications
      // immediately. If there was a validator, this would override the deferral
      // and that would be incorrect.
      clone->NotifyListener();
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetImagePrincipal(nsIPrincipal** aPrincipal) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> principal = GetOwner()->GetPrincipal();
  principal.forget(aPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetHadCrossOriginRedirects(bool* aHadCrossOriginRedirects) {
  *aHadCrossOriginRedirects =
      GetOwner() ? GetOwner()->HadCrossOriginRedirects() : false;
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetMultipart(bool* aMultipart) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  *aMultipart = GetOwner()->GetMultipart();
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetCORSMode(int32_t* aCorsMode) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  *aCorsMode = GetOwner()->GetCORSMode();
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::GetReferrerInfo(nsIReferrerInfo** aReferrerInfo) {
  if (!GetOwner()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIReferrerInfo> referrerInfo = GetOwner()->GetReferrerInfo();
  referrerInfo.forget(aReferrerInfo);
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::BoostPriority(uint32_t aCategory) {
  NS_ENSURE_STATE(GetOwner() && !mCanceled);
  GetOwner()->BoostPriority(aCategory);
  return NS_OK;
}

/** nsISupportsPriority methods **/

NS_IMETHODIMP
imgRequestProxy::GetPriority(int32_t* priority) {
  NS_ENSURE_STATE(GetOwner());
  *priority = GetOwner()->Priority();
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::SetPriority(int32_t priority) {
  NS_ENSURE_STATE(GetOwner() && !mCanceled);
  GetOwner()->AdjustPriority(this, priority - GetOwner()->Priority());
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxy::AdjustPriority(int32_t priority) {
  // We don't require |!mCanceled| here. This may be called even if we're
  // cancelled, because it's invoked as part of the process of removing an image
  // from the load group.
  NS_ENSURE_STATE(GetOwner());
  GetOwner()->AdjustPriority(this, priority);
  return NS_OK;
}

static const char* NotificationTypeToString(int32_t aType) {
  switch (aType) {
    case imgINotificationObserver::SIZE_AVAILABLE:
      return "SIZE_AVAILABLE";
    case imgINotificationObserver::FRAME_UPDATE:
      return "FRAME_UPDATE";
    case imgINotificationObserver::FRAME_COMPLETE:
      return "FRAME_COMPLETE";
    case imgINotificationObserver::LOAD_COMPLETE:
      return "LOAD_COMPLETE";
    case imgINotificationObserver::DECODE_COMPLETE:
      return "DECODE_COMPLETE";
    case imgINotificationObserver::DISCARD:
      return "DISCARD";
    case imgINotificationObserver::UNLOCKED_DRAW:
      return "UNLOCKED_DRAW";
    case imgINotificationObserver::IS_ANIMATED:
      return "IS_ANIMATED";
    case imgINotificationObserver::HAS_TRANSPARENCY:
      return "HAS_TRANSPARENCY";
    default:
      MOZ_ASSERT_UNREACHABLE("Notification list should be exhaustive");
      return "(unknown notification)";
  }
}

void imgRequestProxy::Notify(int32_t aType,
                             const mozilla::gfx::IntRect* aRect) {
  MOZ_ASSERT(aType != imgINotificationObserver::LOAD_COMPLETE,
             "Should call OnLoadComplete");

  LOG_FUNC_WITH_PARAM(gImgLog, "imgRequestProxy::Notify", "type",
                      NotificationTypeToString(aType));

  if (!mListener || mCanceled) {
    return;
  }

  // Make sure the listener stays alive while we notify.
  nsCOMPtr<imgINotificationObserver> listener(mListener);

  listener->Notify(this, aType, aRect);
}

void imgRequestProxy::OnLoadComplete(bool aLastPart) {
  LOG_FUNC_WITH_PARAM(gImgLog, "imgRequestProxy::OnLoadComplete", "uri", mURI);

  // There's all sorts of stuff here that could kill us (the OnStopRequest call
  // on the listener, the removal from the loadgroup, the release of the
  // listener, etc).  Don't let them do it.
  RefPtr<imgRequestProxy> self(this);

  if (mListener && !mCanceled) {
    // Hold a ref to the listener while we call it, just in case.
    nsCOMPtr<imgINotificationObserver> listener(mListener);
    listener->Notify(this, imgINotificationObserver::LOAD_COMPLETE, nullptr);
  }

  // If we're expecting more data from a multipart channel, re-add ourself
  // to the loadgroup so that the document doesn't lose track of the load.
  // If the request is already a background request and there's more data
  // coming, we can just leave the request in the loadgroup as-is.
  if (aLastPart || (mLoadFlags & nsIRequest::LOAD_BACKGROUND) == 0) {
    if (aLastPart) {
      RemoveFromLoadGroup();

      nsresult errorCode = NS_OK;
      // if the load is cross origin without CORS, or the CORS access is
      // rejected, always fire load event to avoid leaking site information for
      // <link rel=preload>.
      // XXXedgar, currently we don't do the same thing for <img>.
      imgRequest* request = GetOwner();
      if (!request || !(request->IsDeniedCrossSiteCORSRequest() ||
                        request->IsCrossSiteNoCORSRequest())) {
        uint32_t status = imgIRequest::STATUS_NONE;
        GetImageStatus(&status);
        if (status & imgIRequest::STATUS_ERROR) {
          errorCode = NS_ERROR_FAILURE;
        }
      }
      NotifyStop(errorCode);
    } else {
      // More data is coming, so change the request to be a background request
      // and put it back in the loadgroup.
      MoveToBackgroundInLoadGroup();
    }
  }

  if (mListenerIsStrongRef && aLastPart) {
    MOZ_ASSERT(mListener, "How did that happen?");
    // Drop our strong ref to the listener now that we're done with
    // everything.  Note that this can cancel us and other fun things
    // like that.  Don't add anything in this method after this point.
    imgINotificationObserver* obs = mListener;
    mListenerIsStrongRef = false;
    NS_RELEASE(obs);
  }
}

void imgRequestProxy::NullOutListener() {
  // If we have animation consumers, then they don't matter anymore
  if (mListener) {
    ClearAnimationConsumers();
  }

  if (mListenerIsStrongRef) {
    // Releasing could do weird reentery stuff, so just play it super-safe
    nsCOMPtr<imgINotificationObserver> obs;
    obs.swap(mListener);
    mListenerIsStrongRef = false;
  } else {
    mListener = nullptr;
  }
}

NS_IMETHODIMP
imgRequestProxy::GetStaticRequest(imgIRequest** aReturn) {
  RefPtr<imgRequestProxy> proxy =
      GetStaticRequest(static_cast<Document*>(nullptr));
  if (proxy != this) {
    RefPtr<Image> image = GetImage();
    if (image && image->HasError()) {
      // image/test/unit/test_async_notification_404.js needs this, but ideally
      // this special case can be removed from the scripted codepath.
      return NS_ERROR_FAILURE;
    }
  }
  proxy.forget(aReturn);
  return NS_OK;
}

already_AddRefed<imgRequestProxy> imgRequestProxy::GetStaticRequest(
    Document* aLoadingDocument) {
  MOZ_DIAGNOSTIC_ASSERT(!aLoadingDocument ||
                        aLoadingDocument->IsStaticDocument());
  RefPtr<Image> image = GetImage();

  bool animated;
  if (!image || (NS_SUCCEEDED(image->GetAnimated(&animated)) && !animated)) {
    // Early exit - we're not animated, so we don't have to do anything.
    return do_AddRef(this);
  }

  // We are animated. We need to create a frozen version of this image.
  RefPtr<Image> frozenImage = ImageOps::Freeze(image);

  // Create a static imgRequestProxy with our new extracted frame.
  nsCOMPtr<nsIPrincipal> currentPrincipal;
  GetImagePrincipal(getter_AddRefs(currentPrincipal));
  bool hadCrossOriginRedirects = true;
  GetHadCrossOriginRedirects(&hadCrossOriginRedirects);
  nsCOMPtr<nsIPrincipal> triggeringPrincipal = GetTriggeringPrincipal();
  auto req = MakeRefPtr<imgRequestProxyStatic>(frozenImage, currentPrincipal,
                                               triggeringPrincipal,
                                               hadCrossOriginRedirects);
  req->Init(nullptr, nullptr, mURI, nullptr);

  return req.forget();
}

void imgRequestProxy::NotifyListener() {
  // It would be nice to notify the observer directly in the status tracker
  // instead of through the proxy, but there are several places we do extra
  // processing when we receive notifications (like OnStopRequest()), and we
  // need to check mCanceled everywhere too.

  RefPtr<ProgressTracker> progressTracker = GetProgressTracker();
  if (GetOwner()) {
    // Send the notifications to our listener asynchronously.
    progressTracker->Notify(this);
  } else {
    // We don't have an imgRequest, so we can only notify the clone of our
    // current state, but we still have to do that asynchronously.
    MOZ_ASSERT(HasImage(), "if we have no imgRequest, we should have an Image");
    progressTracker->NotifyCurrentState(this);
  }
}

void imgRequestProxy::SyncNotifyListener() {
  // It would be nice to notify the observer directly in the status tracker
  // instead of through the proxy, but there are several places we do extra
  // processing when we receive notifications (like OnStopRequest()), and we
  // need to check mCanceled everywhere too.

  RefPtr<ProgressTracker> progressTracker = GetProgressTracker();
  progressTracker->SyncNotify(this);
}

void imgRequestProxy::SetHasImage() {
  RefPtr<ProgressTracker> progressTracker = GetProgressTracker();
  MOZ_ASSERT(progressTracker);
  RefPtr<Image> image = progressTracker->GetImage();
  MOZ_ASSERT(image);

  // Force any private status related to the owner to reflect
  // the presence of an image;
  mBehaviour->SetOwner(mBehaviour->GetOwner());

  // Apply any locks we have
  for (uint32_t i = 0; i < mLockCount; ++i) {
    image->LockImage();
  }

  // Apply any animation consumers we have
  for (uint32_t i = 0; i < mAnimationConsumers; i++) {
    image->IncrementAnimationConsumers();
  }
}

already_AddRefed<ProgressTracker> imgRequestProxy::GetProgressTracker() const {
  return mBehaviour->GetProgressTracker();
}

already_AddRefed<mozilla::image::Image> imgRequestProxy::GetImage() const {
  return mBehaviour->GetImage();
}

bool RequestBehaviour::HasImage() const {
  if (!mOwnerHasImage) {
    return false;
  }
  RefPtr<ProgressTracker> progressTracker = GetProgressTracker();
  return progressTracker ? progressTracker->HasImage() : false;
}

bool imgRequestProxy::HasImage() const { return mBehaviour->HasImage(); }

imgRequest* imgRequestProxy::GetOwner() const { return mBehaviour->GetOwner(); }

imgCacheValidator* imgRequestProxy::GetValidator() const {
  imgRequest* owner = GetOwner();
  if (!owner) {
    return nullptr;
  }
  return owner->GetValidator();
}

nsITimedChannel* imgRequestProxy::TimedChannel() {
  if (!GetOwner()) {
    return nullptr;
  }
  return GetOwner()->GetTimedChannel();
}

////////////////// imgRequestProxyStatic methods

class StaticBehaviour : public ProxyBehaviour {
 public:
  explicit StaticBehaviour(mozilla::image::Image* aImage) : mImage(aImage) {}

  already_AddRefed<mozilla::image::Image> GetImage() const override {
    RefPtr<mozilla::image::Image> image = mImage;
    return image.forget();
  }

  bool HasImage() const override { return mImage; }

  already_AddRefed<ProgressTracker> GetProgressTracker() const override {
    return mImage->GetProgressTracker();
  }

  imgRequest* GetOwner() const override { return nullptr; }

  void SetOwner(imgRequest* aOwner) override {
    MOZ_ASSERT(!aOwner,
               "We shouldn't be giving static requests a non-null owner.");
  }

 private:
  // Our image. We have to hold a strong reference here, because that's normally
  // the job of the underlying request.
  RefPtr<mozilla::image::Image> mImage;
};

imgRequestProxyStatic::imgRequestProxyStatic(mozilla::image::Image* aImage,
                                             nsIPrincipal* aImagePrincipal,
                                             nsIPrincipal* aTriggeringPrincipal,
                                             bool aHadCrossOriginRedirects)
    : mImagePrincipal(aImagePrincipal),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mHadCrossOriginRedirects(aHadCrossOriginRedirects) {
  mBehaviour = std::make_unique<StaticBehaviour>(aImage);
}

NS_IMETHODIMP
imgRequestProxyStatic::GetImagePrincipal(nsIPrincipal** aPrincipal) {
  if (!mImagePrincipal) {
    return NS_ERROR_FAILURE;
  }
  NS_ADDREF(*aPrincipal = mImagePrincipal);
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxyStatic::GetTriggeringPrincipal(nsIPrincipal** aPrincipal) {
  NS_IF_ADDREF(*aPrincipal = mTriggeringPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
imgRequestProxyStatic::GetHadCrossOriginRedirects(
    bool* aHadCrossOriginRedirects) {
  *aHadCrossOriginRedirects = mHadCrossOriginRedirects;
  return NS_OK;
}

already_AddRefed<imgRequestProxy> imgRequestProxyStatic::NewClonedProxy() {
  nsCOMPtr<nsIPrincipal> currentPrincipal;
  GetImagePrincipal(getter_AddRefs(currentPrincipal));
  nsCOMPtr<nsIPrincipal> triggeringPrincipal;
  GetTriggeringPrincipal(getter_AddRefs(triggeringPrincipal));
  bool hadCrossOriginRedirects = true;
  GetHadCrossOriginRedirects(&hadCrossOriginRedirects);
  RefPtr<mozilla::image::Image> image = GetImage();
  return mozilla::MakeAndAddRef<imgRequestProxyStatic>(
      image, currentPrincipal, triggeringPrincipal, hadCrossOriginRedirects);
}
