/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A base class which implements nsIImageLoadingContent and can be
 * subclassed by various content nodes that want to provide image
 * loading functionality (eg <img>, <object>, etc).
 */

#ifndef nsImageLoadingContent_h_
#define nsImageLoadingContent_h_

#include "Units.h"
#include "imgINotificationObserver.h"
#include "mozilla/CORSMode.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/RustTypes.h"
#include "nsAttrValue.h"
#include "nsCOMPtr.h"
#include "nsIContentPolicy.h"
#include "nsIImageLoadingContent.h"
#include "nsIRequest.h"

class nsINode;
class nsIURI;
class nsPresContext;
class nsIContent;
class imgRequestProxy;
class ImageLoadTask;

namespace mozilla {
class AsyncEventDispatcher;
class ErrorResult;

namespace dom {
struct BindContext;
class Document;
class Element;
enum class FetchPriority : uint8_t;
}  // namespace dom
}  // namespace mozilla

#ifdef LoadImage
// Undefine LoadImage to prevent naming conflict with Windows.
#  undef LoadImage
#endif

class nsImageLoadingContent : public nsIImageLoadingContent {
 protected:
  friend class ImageLoadTask;
  template <typename T>
  using Maybe = mozilla::Maybe<T>;
  using Nothing = mozilla::Nothing;
  using OnNonvisible = mozilla::OnNonvisible;
  using Visibility = mozilla::Visibility;

  /* METHODS */
 public:
  nsImageLoadingContent();
  virtual ~nsImageLoadingContent();

  NS_DECL_IMGINOTIFICATIONOBSERVER
  NS_DECL_NSIIMAGELOADINGCONTENT

  // Web IDL binding methods.
  // Note that the XPCOM SetLoadingEnabled method is OK for Web IDL bindings
  // to use as well, since it does not throw when called via the Web IDL
  // bindings.

  bool LoadingEnabled() const { return mLoadingEnabled; }
  void AddObserver(imgINotificationObserver* aObserver);
  void RemoveObserver(imgINotificationObserver* aObserver);
  already_AddRefed<imgIRequest> GetRequest(int32_t aRequestType,
                                           mozilla::ErrorResult& aError);
  int32_t GetRequestType(imgIRequest* aRequest, mozilla::ErrorResult& aError);
  already_AddRefed<nsIURI> GetCurrentURI();
  already_AddRefed<nsIURI> GetCurrentRequestFinalURI();
  void ForceReload(bool aNotify, mozilla::ErrorResult& aError);

  mozilla::dom::Element* FindImageMap();
  static mozilla::dom::Element* FindImageMap(mozilla::dom::Element*);

  /**
   * Toggle whether or not to synchronously decode an image on draw.
   */
  void SetSyncDecodingHint(bool aHint);

  /**
   * Notify us that the document state has changed. Called by nsDocument so that
   * we may reject any promises which require the document to be active.
   */
  void NotifyOwnerDocumentActivityChanged();

  // Trigger text recognition for the current image request.
  already_AddRefed<mozilla::dom::Promise> RecognizeCurrentImageText(
      mozilla::ErrorResult&);

 protected:
  enum ImageLoadType {
    // Most normal image loads
    eImageLoadType_Normal,
    // From a <img srcset> or <picture> context. Affects type given to content
    // policy.
    eImageLoadType_Imageset
  };

  /**
   * LoadImage is called by subclasses when the appropriate
   * attributes (eg 'src' for <img> tags) change.  The string passed
   * in is the new uri string; this consolidates the code for getting
   * the charset, constructing URI objects, and any other incidentals
   * into this superclass.
   *
   * @param aNewURI the URI spec to be loaded (may be a relative URI)
   * @param aForce If true, make sure to load the URI.  If false, only
   *        load if the URI is different from the currently loaded URI.
   * @param aNotify If true, nsIDocumentObserver state change notifications
   *                will be sent as needed.
   * @param aImageLoadType The ImageLoadType for this request
   * @param aTriggeringPrincipal Optional parameter specifying the triggering
   *        principal to use for the image load
   */
  nsresult LoadImage(const nsAString& aNewURI, bool aForce, bool aNotify,
                     ImageLoadType aImageLoadType,
                     nsIPrincipal* aTriggeringPrincipal = nullptr);

  /**
   * ImageState is called by subclasses that are computing their content state.
   * The return value will have the ElementState::BROKEN bit set as needed.
   *
   * Note that this state assumes that this node is "trying" to be an
   * image (so for example complete lack of attempt to load an image will lead
   * to ElementState::BROKEN being set).  Subclasses that are not "trying" to
   * be an image (eg an HTML <input> of type other than "image") should just
   * not call this method when computing their intrinsic state.
   */
  mozilla::dom::ElementState ImageState() const;

  /**
   * LoadImage is called by subclasses when the appropriate
   * attributes (eg 'src' for <img> tags) change. If callers have an
   * URI object already available, they should use this method.
   *
   * @param aNewURI the URI to be loaded
   * @param aForce If true, make sure to load the URI.  If false, only
   *        load if the URI is different from the currently loaded URI.
   * @param aNotify If true, nsIDocumentObserver state change notifications
   *                will be sent as needed.
   * @param aImageLoadType The ImageLoadType for this request
   * @param aDocument Optional parameter giving the document this node is in.
   *        This is purely a performance optimization.
   * @param aLoadFlags Optional parameter specifying load flags to use for
   *        the image load
   * @param aTriggeringPrincipal Optional parameter specifying the triggering
   *        principal to use for the image load
   */
  nsresult LoadImage(nsIURI* aNewURI, bool aForce, bool aNotify,
                     ImageLoadType aImageLoadType, nsLoadFlags aLoadFlags,
                     mozilla::dom::Document* aDocument = nullptr,
                     nsIPrincipal* aTriggeringPrincipal = nullptr);

  nsresult LoadImage(nsIURI* aNewURI, bool aForce, bool aNotify,
                     ImageLoadType aImageLoadType,
                     nsIPrincipal* aTriggeringPrincipal) {
    return LoadImage(aNewURI, aForce, aNotify, aImageLoadType, LoadFlags(),
                     nullptr, aTriggeringPrincipal);
  }

  /**
   * helpers to get the document for this content (from the nodeinfo
   * and such).  Not named GetOwnerDoc/GetCurrentDoc to prevent ambiguous
   * method names in subclasses
   *
   * @return the document we belong to
   */
  mozilla::dom::Document* GetOurOwnerDoc();
  mozilla::dom::Document* GetOurCurrentDoc();

  /**
   * Helper function to get the frame associated with this content. Not named
   * GetPrimaryFrame to prevent ambiguous method names in subclasses.
   *
   * @return The frame we own, or nullptr if it doesn't exist, or isn't
   * associated with any of our requests.
   */
  nsIFrame* GetOurPrimaryImageFrame();

  /**
   * Helper function to get the PresContext associated with this content's
   * frame. Not named GetPresContext to prevent ambiguous method names in
   * subclasses.
   *
   * @return The nsPresContext associated with our frame, or nullptr if either
   *         the frame doesn't exist, or the frame's prescontext doesn't exist.
   */
  nsPresContext* GetFramePresContext();

  /**
   * CancelImageRequests is called by subclasses when they want to
   * cancel all image requests (for example when the subclass is
   * somehow not an image anymore).
   */
  void CancelImageRequests(bool aNotify);

  /**
   * Derived classes of nsImageLoadingContent MUST call Destroy from their
   * destructor, or earlier.  It does things that cannot be done in
   * ~nsImageLoadingContent because they rely on being able to QueryInterface to
   * other derived classes, which cannot happen once the derived class
   * destructor has started calling the base class destructors.
   */
  void Destroy();

  /**
   * Returns the CORS mode that will be used for all future image loads. The
   * default implementation returns CORS_NONE unconditionally.
   */
  virtual mozilla::CORSMode GetCORSMode();

  // Subclasses are *required* to call BindToTree/UnbindFromTree.
  void BindToTree(mozilla::dom::BindContext&, nsINode& aParent);
  void UnbindFromTree();

  void OnLoadComplete(imgIRequest* aRequest, uint32_t aImageStatus);
  void OnUnlockedDraw();
  void OnImageIsAnimated(imgIRequest* aRequest);

  bool HasPendingAlwaysLoadImageTask() const;

  // The nsContentPolicyType we would use for this ImageLoadType
  static nsContentPolicyType PolicyTypeForLoad(ImageLoadType aImageLoadType);

  void AsyncEventRunning(mozilla::AsyncEventDispatcher* aEvent);

  // Get ourselves as an nsIContent*.  Not const because some of the callers
  // want a non-const nsIContent.
  virtual nsIContent* AsContent() = 0;

  virtual mozilla::dom::FetchPriority GetFetchPriorityForImage() const;

  /**
   * Get the natural size of the current request, as defined here:
   * https://html.spec.whatwg.org/multipage/images.html#preferred-density-corrected-dimensions
   *
   * By default, we return the density-corrected natural size, though we skip
   * density-correction if DoDensityCorrection::No is passed.
   */
  enum class DoDensityCorrection : bool { No, Yes };
  mozilla::CSSIntSize NaturalSize(
      DoDensityCorrection = DoDensityCorrection::Yes);

  /**
   * Get width and height of the current request, using given image request if
   * attributes are unset.
   */
  MOZ_CAN_RUN_SCRIPT mozilla::CSSIntSize GetWidthHeightForImage();

  /**
   * Create a promise and queue a microtask which will ensure the current
   * request (after any pending loads are applied) has requested a full decode.
   * The promise is fulfilled once the request has a fully decoded surface that
   * is available for drawing, or an error condition occurrs (e.g. broken image,
   * current request is updated, etc).
   *
   * https://html.spec.whatwg.org/multipage/embedded-content.html#dom-img-decode
   */
  already_AddRefed<mozilla::dom::Promise> QueueDecodeAsync(
      mozilla::ErrorResult& aRv);

  enum class ImageDecodingType : uint8_t {
    Auto,
    Async,
    Sync,
  };

  static constexpr nsAttrValue::EnumTableEntry kDecodingTable[] = {
      {"auto", nsImageLoadingContent::ImageDecodingType::Auto},
      {"async", nsImageLoadingContent::ImageDecodingType::Async},
      {"sync", nsImageLoadingContent::ImageDecodingType::Sync},
  };
  static constexpr const nsAttrValue::EnumTableEntry* kDecodingTableDefault =
      &nsImageLoadingContent::kDecodingTable[0];

 private:
  /**
   * Enqueue and/or fulfill a promise created by QueueDecodeAsync.
   */
  void DecodeAsync(RefPtr<mozilla::dom::Promise>&& aPromise,
                   uint32_t aRequestGeneration);

  /**
   * Attempt to resolve all queued promises based on the state of the current
   * request. If the current request does not yet have all of the encoded data,
   * or the decoding has not yet completed, it will return without changing the
   * promise states.
   */
  void MaybeResolveDecodePromises();

  /**
   * Reject all queued promises with the given status.
   */
  void RejectDecodePromises(nsresult aStatus);

  /**
   * Age the generation counter if we have a new current request with a
   * different URI. If the generation counter is aged, then all queued promises
   * will also be rejected.
   */
  void MaybeAgeRequestGeneration(nsIURI* aNewURI);

  /**
   * Deregister as an observer for the owner document's activity notifications
   * if we have no outstanding decode promises.
   */
  void MaybeDeregisterActivityObserver();

  /**
   * Struct used to manage the native image observers.
   */
  struct ImageObserver {
    explicit ImageObserver(imgINotificationObserver* aObserver);
    ~ImageObserver();

    nsCOMPtr<imgINotificationObserver> mObserver;
    ImageObserver* mNext;
  };

  /**
   * Struct used to manage the scripted/XPCOM image observers.
   */
  class ScriptedImageObserver final {
   public:
    NS_INLINE_DECL_REFCOUNTING(ScriptedImageObserver)

    ScriptedImageObserver(imgINotificationObserver* aObserver,
                          RefPtr<imgRequestProxy>&& aCurrentRequest,
                          RefPtr<imgRequestProxy>&& aPendingRequest);
    bool CancelRequests();

    nsCOMPtr<imgINotificationObserver> mObserver;
    RefPtr<imgRequestProxy> mCurrentRequest;
    RefPtr<imgRequestProxy> mPendingRequest;

   private:
    ~ScriptedImageObserver();
  };

  /**
   * Method to fire an event once we know what's going on with the image load.
   *
   * @param aEventType "load", or "error" depending on how things went
   * @param aIsCancelable true if event is cancelable.
   */
  nsresult FireEvent(const nsAString& aEventType, bool aIsCancelable = false);

  /**
   * Method to cancel and null-out pending event if they exist.
   */
  void CancelPendingEvent();

 protected:
  /**
   * UpdateImageState recomputes the current state of this image loading
   * content and updates what ImageState() returns accordingly.  It will also
   * fire a ContentStatesChanged() notification as needed if aNotify is true.
   */
  void UpdateImageState(bool aNotify);

  /**
   * Method to create an nsIURI object from the given string (will
   * handle getting the right charset, base, etc).  You MUST pass in a
   * non-null document to this function.
   *
   * @param aSpec the string spec (from an HTML attribute, eg)
   * @param aDocument the document we belong to
   * @return the URI we want to be loading
   */
  nsresult StringToURI(const nsAString& aSpec,
                       mozilla::dom::Document* aDocument, nsIURI** aURI);

  /**
   * Prepare and returns a reference to the "next request". If there's already
   * a _usable_ current request (one with SIZE_AVAILABLE), this request is
   * "pending" until it becomes usable. Otherwise, this becomes the current
   * request.
   *
   * @param aImageLoadType The ImageLoadType for this request
   * @param aNewURI The uri that we're going to load
   */
  RefPtr<imgRequestProxy>& PrepareNextRequest(ImageLoadType, nsIURI* aNewURI);

  /**
   * Returns a COMPtr reference to the current/pending image requests, cleaning
   * up and canceling anything that was there before. Note that if you just want
   * to get rid of one of the requests, you should call
   * Clear*Request(NS_BINDING_ABORTED) instead.
   *
   * @param aImageLoadType The ImageLoadType for this request
   * @param aNewURI The uri that we're going to load
   */
  RefPtr<imgRequestProxy>& PrepareCurrentRequest(ImageLoadType,
                                                 nsIURI* aNewURI);
  RefPtr<imgRequestProxy>& PreparePendingRequest(ImageLoadType);

  /**
   * Switch our pending request to be our current request.
   * mPendingRequest must be non-null!
   */
  void MakePendingRequestCurrent();

  /**
   * Cancels and nulls-out the "current" and "pending" requests if they exist.
   *
   * @param aNonvisibleAction An action to take if the image is no longer
   *                          visible as a result; see |UntrackImage|.
   */
  void ClearCurrentRequest(
      nsresult aReason,
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());
  void ClearPendingRequest(
      nsresult aReason,
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());

  /**
   * Static helper method to tell us if we have the size of a request. The
   * image may be null.
   */
  static bool HaveSize(imgIRequest* aImage);

  /**
   * Adds/Removes a given imgIRequest from our document's tracker.
   *
   * No-op if aImage is null.
   *
   * @param aFrame If called from FrameCreated the frame passed to FrameCreated.
   *               This is our frame, but at the time of the FrameCreated call
   *               our primary frame pointer hasn't been set yet, so this is
   *               only way to get our frame.
   *
   * @param aNonvisibleAction A requested action if the frame has become
   *                          nonvisible. If Nothing(), no action is
   *                          requested. If DISCARD_IMAGES is specified, the
   *                          frame is requested to ask any images it's
   *                          associated with to discard their surfaces if
   *                          possible.
   */
  void TrackImage(imgIRequest* aImage, nsIFrame* aFrame = nullptr);
  void UntrackImage(imgIRequest* aImage,
                    const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());

  nsLoadFlags LoadFlags();

 private:
  /**
   * Clones the given "current" or "pending" request for each scripted observer.
   */
  void CloneScriptedRequests(imgRequestProxy* aRequest);

  /**
   * Cancels and nulls-out the "current" or "pending" requests if they exist
   * for each scripted observer.
   */
  void ClearScriptedRequests(int32_t aRequestType, nsresult aReason);

  /**
   * Moves the "pending" request into the "current" request for each scripted
   * observer. If there is an existing "current" request, it will cancel it
   * first.
   */
  void MakePendingScriptedRequestsCurrent();

  /**
   * Depending on the configured decoding hint, and/or how recently we updated
   * the image request, force or stop the frame from decoding the image
   * synchronously when it is drawn.
   * @param aPrepareNextRequest True if this is when updating the image request.
   * @param aFrame If called from FrameCreated the frame passed to FrameCreated.
   *               This is our frame, but at the time of the FrameCreated call
   *               our primary frame pointer hasn't been set yet, so this is
   *               only way to get our frame.
   */
  void MaybeForceSyncDecoding(bool aPrepareNextRequest,
                              nsIFrame* aFrame = nullptr);

 protected:
  void QueueImageTask(nsIURI* aURI, nsIPrincipal* aSrcTriggeringPrincipal,
                      bool aForceAsync, bool aAlwaysLoad, bool aNotify);
  void QueueImageTask(nsIURI* aURI, bool aAlwaysLoad, bool aNotify) {
    QueueImageTask(aURI, nullptr, false, aAlwaysLoad, aNotify);
  }

  void ClearImageLoadTask();

  virtual void LoadSelectedImage(bool aAlwaysLoad, bool aStopLazyLoading) = 0;

  /**
   * Lazily allocated data that is needed once an image load is initiated.
   */
  struct ImageLoadData {
    RefPtr<mozilla::AsyncEventDispatcher> mPendingEvent;

    RefPtr<ImageLoadTask> mPendingImageLoadTask;

    RefPtr<imgRequestProxy> mCurrentRequest;
    RefPtr<imgRequestProxy> mPendingRequest;

    /**
     * Typically we will have only one observer (our frame in the screen
     * prescontext), so we want to only make space for one and to
     * heap-allocate anything past that (saves memory and malloc churn
     * in the common case).  The storage is a linked list, we just
     * happen to actually hold the first observer instead of a pointer
     * to it.
     */
    ImageObserver mObserverList{nullptr};

    /**
     * Typically we will have no scripted observers, as this is only used by
     * chrome, legacy extensions, and some mochitests. An empty array reserves
     * minimal memory.
     */
    nsTArray<RefPtr<ScriptedImageObserver>> mScriptedObservers;

    // If the image was blocked or if there was an error loading, it's nice to
    // still keep track of what the URI was despite not having an imgIRequest.
    // We only maintain this in those situations (in the common case, this is
    // always null).
    nsCOMPtr<nsIURI> mCurrentURI;

    mozilla::TimeStamp mMostRecentRequestChange =
        mozilla::TimeStamp::ProcessCreation();

    /**
     * Promises created by QueueDecodeAsync that are still waiting to be
     * fulfilled by the image being fully decoded.
     */
    nsTArray<RefPtr<mozilla::dom::Promise>> mDecodePromises;

    /**
     * Total number of outstanding decode promises, including those stored in
     * mDecodePromises and those embedded in runnables waiting to be enqueued.
     * This is used to determine whether we need to register as an observer for
     * document activity notifications.
     */
    size_t mOutstandingDecodePromises = 0;

    /**
     * An incrementing counter representing the current request generation;
     * Each time mCurrentRequest is modified with a different URI, this will
     * be incremented. Each QueueDecodeAsync call will cache the generation
     * of the current request so that when it is processed, it knows if it
     * should have rejected because the request changed.
     */
    uint32_t mRequestGeneration = 0;

    // Flags to indicate whether each of the current and pending requests are
    // registered with the refresh driver.
    bool mCurrentRequestRegistered = false;
    bool mPendingRequestRegistered = false;

    uint8_t mCurrentRequestFlags = 0;
    uint8_t mPendingRequestFlags = 0;
  };

  void CreateImageLoadData();

  ImageLoadData& EnsureImageLoadData() {
    if (!mImageLoadData) {
      CreateImageLoadData();
    }
    return *mImageLoadData;
  }

  // For paths that can only run once a load was initiated.
  ImageLoadData& RequireImageLoadData() {
    MOZ_DIAGNOSTIC_ASSERT(mImageLoadData);
    return *mImageLoadData;
  }

  mozilla::UniquePtr<ImageLoadData> mImageLoadData;

  bool mLoadingEnabled : 1 = true;
  /**
   * Flag to indicate whether the channel should be mark as urgent-start.
   * It should be set in *Element and passed to nsContentUtils::LoadImage.
   * True if we want to set nsIClassOfService::UrgentStart to the channel to
   * get the response ASAP for better user responsiveness.
   */
  bool mUseUrgentStartForChannel : 1 = false;

  // Represents the image is deferred loading until this element gets visible.
  bool mLazyLoading : 1 = false;

  // If true, force frames to synchronously decode images on draw.
  bool mSyncDecodingHint : 1 = false;

  // Whether we're in the doc responsive content set (HTMLImageElement only).
  bool mInDocResponsiveContent : 1 = false;

  // Whether the document's mAutoSizeImageObserver is observing this
  // (HTMLImageElement only).
  bool mObservingResize : 1 = false;

 private:
  enum {
    // Set if the request is currently tracked with the document.
    REQUEST_IS_TRACKED = 1 << 0,
    // Set if this is an imageset request, such as from <img srcset> or
    // <picture>
    REQUEST_IS_IMAGESET = 1 << 1,
  };
};

#endif  // nsImageLoadingContent_h_
