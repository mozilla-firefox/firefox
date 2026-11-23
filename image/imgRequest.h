/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_imgRequest_h
#define mozilla_image_imgRequest_h

#include "ImageCacheKey.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIChannelEventSink.h"
#include "nsIInterfaceRequestor.h"
#include "nsIPrincipal.h"
#include "nsIStreamListener.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsProxyRelease.h"
#include "nsString.h"

class imgCacheValidator;
class imgLoader;
class imgRequestProxy;
class imgCacheEntry;
class nsIProperties;
class nsIRequest;
class nsITimedChannel;
class nsIURI;
class nsIReferrerInfo;

namespace mozilla {
enum CORSMode : uint8_t;
namespace dom {
class CacheablePerformanceTimingData;
}  // namespace dom
namespace image {
class Image;
class ProgressTracker;
}  // namespace image
}  // namespace mozilla

struct NewPartResult;

class imgRequest final : public nsIThreadRetargetableStreamListener,
                         public nsIChannelEventSink,
                         public nsIInterfaceRequestor,
                         public nsIAsyncVerifyRedirectCallback {
  typedef mozilla::image::Image Image;
  typedef mozilla::image::ImageCacheKey ImageCacheKey;
  typedef mozilla::image::ProgressTracker ProgressTracker;
  typedef mozilla::dom::ReferrerPolicy ReferrerPolicy;

 public:
  imgRequest(imgLoader* aLoader, const ImageCacheKey& aCacheKey);

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIASYNCVERIFYREDIRECTCALLBACK

  [[nodiscard]] nsresult Init(nsIURI* aURI, nsIURI* aFinalURI,
                              bool aHadInsecureRedirect, nsIRequest* aRequest,
                              nsIChannel* aChannel, imgCacheEntry* aCacheEntry,
                              mozilla::dom::Document* aLoadingDocument,
                              nsIPrincipal* aTriggeringPrincipal,
                              mozilla::CORSMode aCORSMode,
                              nsIReferrerInfo* aReferrerInfo);

  void ClearLoader();

  // Callers must call imgRequestProxy::Notify later.
  void AddProxy(imgRequestProxy* proxy);

  // Whether a given document is allowed to reuse this request without any
  // revalidation.
  bool CanReuseWithoutValidation(mozilla::dom::Document*) const;

  nsresult RemoveProxy(imgRequestProxy* proxy, nsresult aStatus);

  // Cancel, but also ensure that all work done in Init() is undone. Call this
  // only when the channel has failed to open, and so calling Cancel() on it
  // won't be sufficient.
  void CancelAndAbort(nsresult aStatus);

  // Called or dispatched by cancel for main thread only execution.
  void ContinueCancel(nsresult aStatus);

  // Called or dispatched by EvictFromCache for main thread only execution.
  void ContinueEvict();

  // Request that we start decoding the image as soon as data becomes available.
  void StartDecoding();

  uint64_t InnerWindowID() const;
  void SetInnerWindowID(uint64_t aInnerWindowId);

  // Set the cache validation information (expiry time, whether we must
  // validate, etc) on the cache entry based on the request information.
  // If this function is called multiple times, the most strict value wins.
  static void SetCacheValidation(imgCacheEntry* aEntry, nsIRequest* aRequest,
                                 bool aForceTouch = false);

  bool GetMultipart() const;

  // Returns whether we went through an insecure (non-HTTPS) redirect at some
  // point during loading. This does not consider the final URI.
  bool HadInsecureRedirect() const;

  // The CORS mode for which we loaded this image.
  mozilla::CORSMode GetCORSMode() const { return mCORSMode; }

  // The ReferrerInfo in effect when loading this image.
  nsIReferrerInfo* GetReferrerInfo() const { return mReferrerInfo; }

  // The principal for the document that loaded this image. Used when trying to
  // validate a CORS image load.
  already_AddRefed<nsIPrincipal> GetTriggeringPrincipal() const;

  // Return the ProgressTracker associated with this imgRequest. It may live
  // in |mProgressTracker| or in |mImage.mProgressTracker|, depending on whether
  // mImage has been instantiated yet.
  already_AddRefed<ProgressTracker> GetProgressTracker() const;

  /// Returns the Image associated with this imgRequest, if it's ready.
  already_AddRefed<Image> GetImage() const;

  // Get the current principal of the image. No AddRefing.
  inline nsIPrincipal* GetPrincipal() const { return mPrincipal.get(); }

  /// Get the ImageCacheKey associated with this request.
  const ImageCacheKey& CacheKey() const { return mCacheKey; }

  // Resize the cache entry to 0 if it exists
  void ResetCacheEntry();

  // OK to use on any thread.
  nsresult GetURI(nsIURI** aURI);
  nsresult GetFinalURI(nsIURI** aURI);
  bool IsChrome() const;
  bool IsData() const;

  nsresult GetImageErrorCode(void);

  /// Returns a non-owning pointer to this imgRequest's MIME type.
  const char* GetMimeType() const { return mContentType.get(); }

  int64_t GetContentLength() const { return mContentLength; }

  void GetFileName(nsACString& aFileName);

  /// @return the priority of the underlying network request, or
  /// PRIORITY_NORMAL if it doesn't support nsISupportsPriority.
  int32_t Priority() const;

  /// Adjust the priority of the underlying network request by @aDelta on behalf
  /// of @aProxy.
  void AdjustPriority(imgRequestProxy* aProxy, int32_t aDelta);

  void BoostPriority(uint32_t aCategory);

  /// Returns a weak pointer to the underlying request.
  nsIRequest* GetRequest() const { return mRequest; }

  nsITimedChannel* GetTimedChannel() const { return mTimedChannel; }

  /// The timing data of the completed load, used to report resource timing
  /// entries for documents that reuse this request from the cache.
  const mozilla::dom::CacheablePerformanceTimingData* GetPerfData() const {
    return mPerfData.get();
  }

  /// Returns true if any redirect in the chain that loaded this image was
  /// cross-origin.
  bool HadCrossOriginRedirects() const;

  imgCacheValidator* GetValidator() const { return mValidator; }
  void SetValidator(imgCacheValidator* aValidator) { mValidator = aValidator; }

  void* LoadId() const { return mLoadId; }
  void SetLoadId(void* aLoadId) { mLoadId = aLoadId; }

  /// Reset the cache entry after we've dropped our reference to it. Used by
  /// imgLoader when our cache entry is re-requested after we've dropped our
  /// reference to it.
  void SetCacheEntry(imgCacheEntry* aEntry);

  /// Returns whether we've got a reference to the cache entry.
  bool HasCacheEntry() const;

  /// Set whether this request is stored in the cache. If it isn't, regardless
  /// of whether this request has a non-null mCacheEntry, this imgRequest won't
  /// try to update or modify the image cache.
  void SetIsInCache(bool aCacheable);

  void EvictFromCache();
  void RemoveFromCache();

  // Sets properties for this image; will dispatch to main thread if needed.
  void SetProperties(const nsACString& aContentType,
                     const nsACString& aContentDisposition);

  nsIProperties* Properties() const { return mProperties; }

  bool HasConsumers() const;

  bool ImageAvailable() const;

  bool IsDeniedCrossSiteCORSRequest() const {
    return mIsDeniedCrossSiteCORSRequest;
  }

  bool IsCrossSiteNoCORSRequest() const { return mIsCrossSiteNoCORSRequest; }

  bool ShouldReportRenderTimeForLCP() const {
    return mShouldReportRenderTimeForLCP;
  }

 private:
  friend class FinishPreparingForNewPartRunnable;

  virtual ~imgRequest();

  void FinishPreparingForNewPart(const NewPartResult& aResult);

  void UpdateShouldReportRenderTimeForLCP();

  void Cancel(nsresult aStatus);

  // Update the cache entry size based on the image container.
  void UpdateCacheEntrySize();

  /// Returns true if StartDecoding() was called.
  bool IsDecodeRequested() const;

  void AdjustPriorityInternal(int32_t aDelta);

  // Weak reference to parent loader; this request cannot outlive its owner.
  imgLoader* mLoader;
  nsCOMPtr<nsIRequest> mRequest;
  // The original URI we were loaded with. This is the same as the URI we are
  // keyed on in the cache. We store a string here to avoid off main thread
  // refcounting issues with nsStandardURL.
  nsCOMPtr<nsIURI> mURI;
  // The URI of the resource we ended up loading after all redirects, etc.
  nsCOMPtr<nsIURI> mFinalURI;
  // The principal which triggered the load of this image. Generally either
  // the principal of the document the image is being loaded into, or of the
  // stylesheet which specified the image to load. Used when validating for
  // CORS.
  nsCOMPtr<nsIPrincipal> mTriggeringPrincipal;
  // The principal of this image.
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIProperties> mProperties;
  nsCOMPtr<nsIChannel> mChannel;
  nsCOMPtr<nsIInterfaceRequestor> mPrevChannelSink;

  nsCOMPtr<nsITimedChannel> mTimedChannel;

  mozilla::UniquePtr<mozilla::dom::CacheablePerformanceTimingData> mPerfData;

  nsCString mContentType;
  int64_t mContentLength = 0;

  /* we hold on to this to this so long as we have observers */
  RefPtr<imgCacheEntry> mCacheEntry;

  /// The key under which this imgRequest is stored in the image cache.
  ImageCacheKey mCacheKey;

  void* mLoadId;

  /// Raw pointer to the first proxy that was added to this imgRequest. Use only
  /// pointer comparisons; there's no guarantee this will remain valid.
  void* mFirstProxy;

  imgCacheValidator* mValidator;
  nsCOMPtr<nsIAsyncVerifyRedirectCallback> mRedirectCallback;
  nsCOMPtr<nsIChannel> mNewRedirectChannel;

  // The ReferrerInfo used for this image.
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;

  nsresult mImageErrorCode;

  // The categories of prioritization strategy that have been requested.
  uint32_t mBoostCategoriesRequested = 0;

  // The CORS mode (defined in imgIRequest) this image was loaded with. By
  // default, CORS_NONE.
  mozilla::CORSMode mCORSMode;

  // If we've called OnImageAvailable.
  bool mImageAvailable;
  bool mIsDeniedCrossSiteCORSRequest;
  bool mIsCrossSiteNoCORSRequest;

  bool mShouldReportRenderTimeForLCP;
  // True if any redirect in the chain that loaded this image was cross-origin.
  bool mHadCrossOriginRedirects = false;
  // SVGs can't be OffMainThread for example
  bool mOffMainThreadData = false;

  mutable mozilla::Mutex mMutex;

  // Member variables protected by mMutex. Note that *all* flags in our bitfield
  // are protected by mMutex; if you're adding a new flag that isn'protected, it
  // must not be a part of this bitfield.
  RefPtr<ProgressTracker> mProgressTracker MOZ_GUARDED_BY(mMutex);
  RefPtr<Image> mImage MOZ_GUARDED_BY(mMutex);
  bool mIsMultiPartChannel : 1 MOZ_GUARDED_BY(mMutex);
  bool mIsInCache : 1 MOZ_GUARDED_BY(mMutex);
  bool mDecodeRequested : 1 MOZ_GUARDED_BY(mMutex);
  bool mNewPartPending : 1 MOZ_GUARDED_BY(mMutex);
  bool mHadInsecureRedirect : 1 MOZ_GUARDED_BY(mMutex);
  // The ID of the inner window origin, used for error reporting.
  uint64_t mInnerWindowId MOZ_GUARDED_BY(mMutex);
};

#endif  // mozilla_image_imgRequest_h
