/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Geolocation.h"

#include "GeolocationIPCUtils.h"
#include "GeolocationSystem.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/CycleCollectedJSContext.h"  // for nsAutoMicroTask
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/GeolocationPositionError.h"
#include "mozilla/dom/GeolocationPositionErrorBinding.h"
#include "mozilla/glean/DomGeolocationMetrics.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_geo.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/EventStateManager.h"
#include "nsComponentManagerUtils.h"
#include "nsContentPermissionHelper.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "mozilla/dom/Document.h"
#include "nsINamed.h"
#include "nsIObserverService.h"
#include "nsIPromptService.h"
#include "nsIScriptError.h"
#include "nsPIDOMWindow.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

class nsIPrincipal;

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidLocationProvider.h"
#endif

#ifdef MOZ_ENABLE_DBUS
#  include "mozilla/WidgetUtilsGtk.h"
#  include "GeoclueLocationProvider.h"
#  include "PortalLocationProvider.h"
#endif

#ifdef MOZ_WIDGET_COCOA
#  include "CoreLocationLocationProvider.h"
#endif

#ifdef XP_WIN
#  include "WindowsLocationProvider.h"
#endif

// Some limit to the number of get or watch geolocation requests
// that a window can make.
#define MAX_GEO_REQUESTS_PER_WINDOW 1500

// This preference allows to override the "secure context" by
// default policy.
#define PREF_GEO_SECURITY_ALLOWINSECURE "geo.security.allowinsecure"

using mozilla::Unused;  // <snicker>
using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::geolocation;

mozilla::LazyLogModule gGeolocationLog("Geolocation");

class nsGeolocationRequest final : public ContentPermissionRequestBase,
                                   public nsIGeolocationUpdate,
                                   public SupportsWeakPtr {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIGEOLOCATIONUPDATE

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsGeolocationRequest,
                                           ContentPermissionRequestBase)

  nsGeolocationRequest(Geolocation* aLocator, GeoPositionCallback aCallback,
                       GeoPositionErrorCallback aErrorCallback,
                       UniquePtr<PositionOptions>&& aOptions,
                       nsIEventTarget* aMainThreadSerialEventTarget,
                       bool aWatchPositionRequest = false,
                       int32_t aWatchId = 0);

  // nsIContentPermissionRequest
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD Cancel(void) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD Allow(JS::Handle<JS::Value> choices) override;
  NS_IMETHOD GetTypes(nsIArray** aTypes) override;

  void Shutdown();

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY is OK here because we're always called from a
  // runnable.  Ideally nsIRunnable::Run and its overloads would just be
  // MOZ_CAN_RUN_SCRIPT and then we could be too...
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void SendLocation(nsIDOMGeoPosition* aLocation);
  bool WantsHighAccuracy() {
    return !mShutdown && mOptions && mOptions->mEnableHighAccuracy;
  }
  void SetTimeoutTimer();
  void StopTimeoutTimer();
  MOZ_CAN_RUN_SCRIPT
  void NotifyErrorAndShutdown(uint16_t);
  using ContentPermissionRequestBase::GetPrincipal;
  nsIPrincipal* GetPrincipal();

  bool IsWatch() { return mIsWatchPositionRequest; }
  int32_t WatchId() { return mWatchId; }

  void SetPromptBehavior(
      geolocation::SystemGeolocationPermissionBehavior aBehavior) {
    mBehavior = aBehavior;
  }

 private:
  virtual ~nsGeolocationRequest();

  class TimerCallbackHolder final : public nsITimerCallback, public nsINamed {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSITIMERCALLBACK

    explicit TimerCallbackHolder(nsGeolocationRequest* aRequest)
        : mRequest(aRequest) {}

    NS_IMETHOD GetName(nsACString& aName) override {
      aName.AssignLiteral("nsGeolocationRequest::TimerCallbackHolder");
      return NS_OK;
    }

   private:
    ~TimerCallbackHolder() = default;
    WeakPtr<nsGeolocationRequest> mRequest;
  };

  // Only called from a timer, so MOZ_CAN_RUN_SCRIPT_BOUNDARY ok for now.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Notify();

  bool mIsWatchPositionRequest;

  nsCOMPtr<nsITimer> mTimeoutTimer;
  GeoPositionCallback mCallback;
  GeoPositionErrorCallback mErrorCallback;
  UniquePtr<PositionOptions> mOptions;

  RefPtr<Geolocation> mLocator;

  int32_t mWatchId;
  bool mShutdown;
  bool mSeenAnySignal = false;
  nsCOMPtr<nsIEventTarget> mMainThreadSerialEventTarget;

  SystemGeolocationPermissionBehavior mBehavior;
};

static UniquePtr<PositionOptions> CreatePositionOptionsCopy(
    const PositionOptions& aOptions) {
  UniquePtr<PositionOptions> geoOptions = MakeUnique<PositionOptions>();

  geoOptions->mEnableHighAccuracy = aOptions.mEnableHighAccuracy;
  geoOptions->mMaximumAge = aOptions.mMaximumAge;
  geoOptions->mTimeout = aOptions.mTimeout;

  return geoOptions;
}

class RequestSendLocationEvent : public Runnable {
 public:
  RequestSendLocationEvent(nsIDOMGeoPosition* aPosition,
                           nsGeolocationRequest* aRequest)
      : mozilla::Runnable("RequestSendLocationEvent"),
        mPosition(aPosition),
        mRequest(aRequest) {}

  NS_IMETHOD Run() override {
    mRequest->SendLocation(mPosition);
    return NS_OK;
  }

 private:
  nsCOMPtr<nsIDOMGeoPosition> mPosition;
  RefPtr<nsGeolocationRequest> mRequest;
  RefPtr<Geolocation> mLocator;
};

////////////////////////////////////////////////////
// nsGeolocationRequest
////////////////////////////////////////////////////

static nsPIDOMWindowInner* ConvertWeakReferenceToWindow(
    nsIWeakReference* aWeakPtr) {
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(aWeakPtr);
  // This isn't usually safe, but here we're just extracting a raw pointer in
  // order to pass it to a base class constructor which will in turn convert it
  // into a strong pointer for us.
  nsPIDOMWindowInner* raw = window.get();
  return raw;
}

nsGeolocationRequest::nsGeolocationRequest(
    Geolocation* aLocator, GeoPositionCallback aCallback,
    GeoPositionErrorCallback aErrorCallback,
    UniquePtr<PositionOptions>&& aOptions,
    nsIEventTarget* aMainThreadSerialEventTarget, bool aWatchPositionRequest,
    int32_t aWatchId)
    : ContentPermissionRequestBase(
          aLocator->GetPrincipal(),
          ConvertWeakReferenceToWindow(aLocator->GetOwner()), "geo"_ns,
          "geolocation"_ns),
      mIsWatchPositionRequest(aWatchPositionRequest),
      mCallback(std::move(aCallback)),
      mErrorCallback(std::move(aErrorCallback)),
      mOptions(std::move(aOptions)),
      mLocator(aLocator),
      mWatchId(aWatchId),
      mShutdown(false),
      mMainThreadSerialEventTarget(aMainThreadSerialEventTarget),
      mBehavior(SystemGeolocationPermissionBehavior::NoPrompt) {}

nsGeolocationRequest::~nsGeolocationRequest() { StopTimeoutTimer(); }

NS_IMPL_QUERY_INTERFACE_CYCLE_COLLECTION_INHERITED(nsGeolocationRequest,
                                                   ContentPermissionRequestBase,
                                                   nsIGeolocationUpdate)

NS_IMPL_ADDREF_INHERITED(nsGeolocationRequest, ContentPermissionRequestBase)
NS_IMPL_RELEASE_INHERITED(nsGeolocationRequest, ContentPermissionRequestBase)
NS_IMPL_CYCLE_COLLECTION_WEAK_PTR_INHERITED(nsGeolocationRequest,
                                            ContentPermissionRequestBase,
                                            mCallback, mErrorCallback, mLocator)

void nsGeolocationRequest::Notify() {
  MOZ_LOG(gGeolocationLog, LogLevel::Debug, ("nsGeolocationRequest::Notify"));
  NotifyErrorAndShutdown(GeolocationPositionError_Binding::TIMEOUT);
}

void nsGeolocationRequest::NotifyErrorAndShutdown(uint16_t aErrorCode) {
  MOZ_LOG(gGeolocationLog, LogLevel::Debug,
          ("nsGeolocationRequest::NotifyErrorAndShutdown with error code: %u",
           aErrorCode));
  MOZ_ASSERT(!mShutdown, "timeout after shutdown");
  if (!mIsWatchPositionRequest) {
    Shutdown();
    mLocator->RemoveRequest(this);
  }

  NotifyError(aErrorCode);
}

NS_IMETHODIMP
nsGeolocationRequest::Cancel() {
  if (mLocator->ClearPendingRequest(this)) {
    return NS_OK;
  }

  NotifyError(GeolocationPositionError_Binding::PERMISSION_DENIED);
  return NS_OK;
}

/**
 * When the promise for the cancel dialog is resolved or rejected, we should
 * stop waiting for permission.  If it was granted then the
 * SystemGeolocationPermissionRequest should already be resolved, so we do
 * nothing.  Otherwise, we were either cancelled or got an error, so we cancel
 * the SystemGeolocationPermissionRequest.
 */
class CancelSystemGeolocationPermissionRequest : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  explicit CancelSystemGeolocationPermissionRequest(
      SystemGeolocationPermissionRequest* aRequest)
      : mRequest(aRequest) {}

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    mRequest->Stop();
  }

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    mRequest->Stop();
  }

 private:
  ~CancelSystemGeolocationPermissionRequest() = default;
  RefPtr<SystemGeolocationPermissionRequest> mRequest;
};

NS_IMPL_ISUPPORTS0(CancelSystemGeolocationPermissionRequest)

/* static */
void Geolocation::ReallowWithSystemPermissionOrCancel(
    BrowsingContext* aBrowsingContext,
    geolocation::ParentRequestResolver&& aResolver) {
  // Make sure we don't return without responding to the geolocation request.
  auto denyPermissionOnError =
      MakeScopeExit([&aResolver]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        aResolver(GeolocationPermissionStatus::Error);
      });

  NS_ENSURE_TRUE_VOID(aBrowsingContext);

  nsCOMPtr<nsIStringBundle> bundle;
  nsCOMPtr<nsIStringBundleService> sbs =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID);
  NS_ENSURE_TRUE_VOID(sbs);

  sbs->CreateBundle("chrome://browser/locale/browser.properties",
                    getter_AddRefs(bundle));
  NS_ENSURE_TRUE_VOID(bundle);

  nsAutoString title;
  nsresult rv =
      bundle->GetStringFromName("geolocation.systemSettingsTitle", title);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsAutoString brandName;
  rv = nsContentUtils::GetLocalizedString(nsContentUtils::eBRAND_PROPERTIES,
                                          "brandShortName", brandName);
  NS_ENSURE_SUCCESS_VOID(rv);
  AutoTArray<nsString, 1> formatParams;
  formatParams.AppendElement(brandName);
  nsAutoString message;
  rv = bundle->FormatStringFromName("geolocation.systemSettingsMessage",
                                    formatParams, message);
  NS_ENSURE_SUCCESS_VOID(rv);

  // We MUST do this because aResolver is moved below.
  denyPermissionOnError.release();

  RefPtr<SystemGeolocationPermissionRequest> permissionRequest =
      geolocation::RequestLocationPermissionFromUser(aBrowsingContext,
                                                     std::move(aResolver));
  NS_ENSURE_TRUE_VOID(permissionRequest);

  auto cancelRequestOnError = MakeScopeExit([&]() {
    // Stop waiting for the system permission and just leave it up to the user.
    permissionRequest->Stop();
  });

  nsCOMPtr<nsIPromptService> promptSvc =
      do_GetService("@mozilla.org/prompter;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  // The dialog should include a cancel button if Gecko is prompting the user
  // for system permission.  It should have no buttons if the OS will be
  // doing the prompting.
  bool geckoWillPrompt =
      GetLocationOSPermission() ==
      geolocation::SystemGeolocationPermissionBehavior::GeckoWillPromptUser;
  // This combination of flags removes all buttons and adds a spinner to the
  // title.
  const auto kSpinnerNoButtonFlags =
      nsIPromptService::BUTTON_NONE | nsIPromptService::SHOW_SPINNER;
  // This combination of flags indicates there is only one button labeled
  // "Cancel".
  const auto kCancelButtonFlags =
      nsIPromptService::BUTTON_TITLE_CANCEL * nsIPromptService::BUTTON_POS_0;
  RefPtr<mozilla::dom::Promise> tabBlockingDialogPromise;
  rv = promptSvc->AsyncConfirmEx(
      aBrowsingContext, nsIPromptService::MODAL_TYPE_TAB, title.get(),
      message.get(),
      geckoWillPrompt ? kCancelButtonFlags : kSpinnerNoButtonFlags, nullptr,
      nullptr, nullptr, nullptr, false, JS::UndefinedHandleValue,
      getter_AddRefs(tabBlockingDialogPromise));
  NS_ENSURE_SUCCESS_VOID(rv);
  MOZ_ASSERT(tabBlockingDialogPromise);

  // If the tab blocking dialog promise is resolved or rejected then the dialog
  // is no longer visible so we should stop waiting for permission, whether it
  // was granted or not.
  tabBlockingDialogPromise->AppendNativeHandler(
      new CancelSystemGeolocationPermissionRequest(permissionRequest));

  cancelRequestOnError.release();
}

NS_IMETHODIMP
nsGeolocationRequest::Allow(JS::Handle<JS::Value> aChoices) {
  MOZ_ASSERT(aChoices.isUndefined());

  if (mLocator->ClearPendingRequest(this)) {
    return NS_OK;
  }

  if (mBehavior != SystemGeolocationPermissionBehavior::NoPrompt) {
    // Asynchronously present the system dialog or open system preferences
    // (RequestGeolocationPermissionFromUser will know which to do), and wait
    // for the permission to change or the request to be canceled.  If the
    // permission is (maybe) granted then it will call Allow again.  It actually
    // will also re-call Allow if the permission is denied, in order to get the
    // "denied permission" behavior.
    mBehavior = SystemGeolocationPermissionBehavior::NoPrompt;
    RefPtr<BrowsingContext> browsingContext = mWindow->GetBrowsingContext();
    if (ContentChild* cc = ContentChild::GetSingleton()) {
      cc->SendRequestGeolocationPermissionFromUser(
          browsingContext,
          [self = RefPtr{this}](GeolocationPermissionStatus aResult)
              MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                self->Allow(JS::UndefinedHandleValue);
              },
          [self = RefPtr{this}](mozilla::ipc::ResponseRejectReason aReason)
              MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                self->Allow(JS::UndefinedHandleValue);
              });
      return NS_OK;
    }

    Geolocation::ReallowWithSystemPermissionOrCancel(
        browsingContext,
        [self = RefPtr{this}](GeolocationPermissionStatus aResult)
            MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
              self->Allow(JS::UndefinedHandleValue);
            });
    return NS_OK;
  }

  RefPtr<nsGeolocationService> gs = nsGeolocationService::GetGeolocationService(
      mLocator->GetBrowsingContext());
  bool canUseCache = false;
  CachedPositionAndAccuracy lastPosition = gs->GetCachedPosition();
  if (lastPosition.position) {
    EpochTimeStamp cachedPositionTime_ms;
    lastPosition.position->GetTimestamp(&cachedPositionTime_ms);
    // check to see if we can use a cached value
    // if the user has specified a maximumAge, return a cached value.
    if (mOptions && mOptions->mMaximumAge > 0) {
      uint32_t maximumAge_ms = mOptions->mMaximumAge;
      bool isCachedWithinRequestedAccuracy =
          WantsHighAccuracy() <= lastPosition.isHighAccuracy;
      bool isCachedWithinRequestedTime =
          EpochTimeStamp(PR_Now() / PR_USEC_PER_MSEC - maximumAge_ms) <=
          cachedPositionTime_ms;
      canUseCache =
          isCachedWithinRequestedAccuracy && isCachedWithinRequestedTime;
    }
  }

  // Enforce using cache in case the geolocation override server is set,
  // since this server can return only cached values.
  if (!canUseCache && gs != nsGeolocationService::sService.get()) {
    canUseCache = true;
  }

  gs->UpdateAccuracy(WantsHighAccuracy());
  if (canUseCache) {
    // okay, we can return a cached position
    // getCurrentPosition requests serviced by the cache
    // will now be owned by the RequestSendLocationEvent
    Update(lastPosition.position);

    // After Update is called, getCurrentPosition finishes its job.
    if (!mIsWatchPositionRequest) {
      return NS_OK;
    }

  } else {
    // if it is not a watch request and timeout is 0,
    // invoke the errorCallback (if present) with TIMEOUT code
    if (mOptions && mOptions->mTimeout == 0 && !mIsWatchPositionRequest) {
      NotifyError(GeolocationPositionError_Binding::TIMEOUT);
      return NS_OK;
    }
  }

  // Non-cached location request
  bool allowedRequest = mIsWatchPositionRequest || !canUseCache;
  if (allowedRequest) {
    // let the locator know we're pending
    // we will now be owned by the locator
    mLocator->NotifyAllowedRequest(this);
  }

  // Kick off the geo device, if it isn't already running
  nsresult rv = gs->StartDevice();

  if (NS_FAILED(rv)) {
    if (allowedRequest) {
      mLocator->RemoveRequest(this);
    }
    // Location provider error
    NotifyError(GeolocationPositionError_Binding::POSITION_UNAVAILABLE);
    return NS_OK;
  }

  SetTimeoutTimer();

  return NS_OK;
}

void nsGeolocationRequest::SetTimeoutTimer() {
  MOZ_ASSERT(!mShutdown, "set timeout after shutdown");

  StopTimeoutTimer();

  if (mOptions && mOptions->mTimeout != 0 && mOptions->mTimeout != 0x7fffffff) {
    RefPtr<TimerCallbackHolder> holder = new TimerCallbackHolder(this);
    NS_NewTimerWithCallback(getter_AddRefs(mTimeoutTimer), holder,
                            mOptions->mTimeout, nsITimer::TYPE_ONE_SHOT);
  }
}

void nsGeolocationRequest::StopTimeoutTimer() {
  if (mTimeoutTimer) {
    mTimeoutTimer->Cancel();
    mTimeoutTimer = nullptr;
  }
}

void nsGeolocationRequest::SendLocation(nsIDOMGeoPosition* aPosition) {
  if (mShutdown) {
    // Ignore SendLocationEvents issued before we were cleared.
    return;
  }

  if (mOptions && mOptions->mMaximumAge > 0) {
    EpochTimeStamp positionTime_ms;
    aPosition->GetTimestamp(&positionTime_ms);
    const uint32_t maximumAge_ms = mOptions->mMaximumAge;
    const bool isTooOld = EpochTimeStamp(PR_Now() / PR_USEC_PER_MSEC -
                                         maximumAge_ms) > positionTime_ms;
    if (isTooOld) {
      return;
    }
  }

  RefPtr<mozilla::dom::GeolocationPosition> wrapped;

  if (aPosition) {
    nsCOMPtr<nsIDOMGeoPositionCoords> coords;
    aPosition->GetCoords(getter_AddRefs(coords));
    if (coords) {
      wrapped = new mozilla::dom::GeolocationPosition(ToSupports(mLocator),
                                                      aPosition);
    }
  }

  if (!wrapped) {
    NotifyError(GeolocationPositionError_Binding::POSITION_UNAVAILABLE);
    return;
  }

  if (mIsWatchPositionRequest) {
    // The initial signal for watch request is observed, after this we do not
    // set a timer as the geolocation device is free to not send extra position
    // if the device is simply not moving.
    //
    // See also https://w3c.github.io/geolocation/#dfn-request-a-position
    // Step 7.5.1: "Wait for a significant change of geographic position. What
    // constitutes a significant change of geographic position is left to the
    // implementation."
    StopTimeoutTimer();
  } else {
    // Cancel timer and position updates in case the position
    // callback spins the event loop
    Shutdown();
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  obs->NotifyObservers(wrapped, "geolocation-position-events",
                       u"location-updated");

  nsAutoMicroTask mt;
  if (mCallback.HasWebIDLCallback()) {
    RefPtr<PositionCallback> callback = mCallback.GetWebIDLCallback();

    MOZ_ASSERT(callback);
    callback->Call(*wrapped);
  } else {
    nsIDOMGeoPositionCallback* callback = mCallback.GetXPCOMCallback();
    MOZ_ASSERT(callback);
    callback->HandleEvent(aPosition);
  }

  MOZ_ASSERT(mShutdown || mIsWatchPositionRequest,
             "non-shutdown getCurrentPosition request after callback!");
}

nsIPrincipal* nsGeolocationRequest::GetPrincipal() {
  if (!mLocator) {
    return nullptr;
  }
  return mLocator->GetPrincipal();
}

NS_IMETHODIMP
nsGeolocationRequest::Update(nsIDOMGeoPosition* aPosition) {
  if (!mSeenAnySignal) {
    mSeenAnySignal = true;
    glean::geolocation::request_result
        .EnumGet(glean::geolocation::RequestResultLabel::eSuccess)
        .Add();
  }
  nsCOMPtr<nsIRunnable> ev = new RequestSendLocationEvent(aPosition, this);
  mMainThreadSerialEventTarget->Dispatch(ev.forget());
  return NS_OK;
}

static glean::geolocation::RequestResultLabel MapErrorToLabel(
    uint16_t aErrorCode) {
  using Label = glean::geolocation::RequestResultLabel;
  switch (aErrorCode) {
    case GeolocationPositionError_Binding::PERMISSION_DENIED:
      return Label::ePermissionDenied;
    case GeolocationPositionError_Binding::POSITION_UNAVAILABLE:
      return Label::ePositionUnavailable;
    case GeolocationPositionError_Binding::TIMEOUT:
      return Label::eTimeout;
    default:
      MOZ_CRASH("Unknown geolocation error label");
      return Label::ePositionUnavailable;
  }
}

NS_IMETHODIMP
nsGeolocationRequest::NotifyError(uint16_t aErrorCode) {
  MOZ_LOG(
      gGeolocationLog, LogLevel::Debug,
      ("nsGeolocationRequest::NotifyError with error code: %u", aErrorCode));
  MOZ_ASSERT(NS_IsMainThread());
  if (!mSeenAnySignal) {
    mSeenAnySignal = true;
    glean::geolocation::request_result.EnumGet(MapErrorToLabel(aErrorCode))
        .Add();
  }
  RefPtr<GeolocationPositionError> positionError =
      new GeolocationPositionError(mLocator, static_cast<int16_t>(aErrorCode));
  positionError->NotifyCallback(mErrorCallback);
  return NS_OK;
}

void nsGeolocationRequest::Shutdown() {
  MOZ_ASSERT(!mShutdown, "request shutdown twice");
  mShutdown = true;

  StopTimeoutTimer();

  // If there are no other high accuracy requests, the geolocation service will
  // notify the provider to switch to the default accuracy.
  if (mOptions && mOptions->mEnableHighAccuracy) {
    RefPtr<nsGeolocationService> gs =
        nsGeolocationService::GetGeolocationService(
            mLocator->GetBrowsingContext());
    if (gs) {
      gs->UpdateAccuracy();
    }
  }
}

NS_IMETHODIMP
nsGeolocationRequest::GetTypes(nsIArray** aTypes) {
  AutoTArray<nsString, 2> options;

  switch (mBehavior) {
    case SystemGeolocationPermissionBehavior::SystemWillPromptUser:
      options.AppendElement(u"sysdlg"_ns);
      break;
    case SystemGeolocationPermissionBehavior::GeckoWillPromptUser:
      options.AppendElement(u"syssetting"_ns);
      break;
    default:
      break;
  }
  return nsContentPermissionUtils::CreatePermissionArray(mType, options,
                                                         aTypes);
}

////////////////////////////////////////////////////
// nsGeolocationRequest::TimerCallbackHolder
////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS(nsGeolocationRequest::TimerCallbackHolder, nsITimerCallback,
                  nsINamed)

NS_IMETHODIMP
nsGeolocationRequest::TimerCallbackHolder::Notify(nsITimer*) {
  if (mRequest && mRequest->mLocator) {
    RefPtr<nsGeolocationRequest> request(mRequest);
    request->Notify();
  }

  return NS_OK;
}

////////////////////////////////////////////////////
// nsGeolocationService
////////////////////////////////////////////////////
NS_INTERFACE_MAP_BEGIN(nsGeolocationService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIGeolocationUpdate)
  NS_INTERFACE_MAP_ENTRY(nsIGeolocationUpdate)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(nsGeolocationService)
NS_IMPL_RELEASE(nsGeolocationService)

nsresult nsGeolocationService::Init() {
  if (!StaticPrefs::geo_enabled()) {
    return NS_ERROR_FAILURE;
  }

  if (XRE_IsContentProcess()) {
    return NS_OK;
  }

  // geolocation service can be enabled -> now register observer
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return NS_ERROR_FAILURE;
  }

  obs->AddObserver(this, "xpcom-shutdown", false);

#ifdef MOZ_WIDGET_ANDROID
  mProvider = new AndroidLocationProvider();
#endif

#ifdef MOZ_WIDGET_GTK
#  ifdef MOZ_ENABLE_DBUS
  if (!mProvider && widget::ShouldUsePortal(widget::PortalKind::Location)) {
    mProvider = new PortalLocationProvider();
    MOZ_LOG(gGeolocationLog, LogLevel::Debug,
            ("Selected PortalLocationProvider"));
    glean::geolocation::linux_provider
        .EnumGet(glean::geolocation::LinuxProviderLabel::ePortal)
        .Set(true);
  }

  if (!mProvider && StaticPrefs::geo_provider_use_geoclue()) {
    nsCOMPtr<nsIGeolocationProvider> gcProvider = new GeoclueLocationProvider();
    MOZ_LOG(gGeolocationLog, LogLevel::Debug,
            ("Checking GeoclueLocationProvider"));
    // The Startup() method will only succeed if Geoclue is available on D-Bus
    if (NS_SUCCEEDED(gcProvider->Startup())) {
      gcProvider->Shutdown();
      mProvider = std::move(gcProvider);
      MOZ_LOG(gGeolocationLog, LogLevel::Debug,
              ("Selected GeoclueLocationProvider"));
      glean::geolocation::linux_provider
          .EnumGet(glean::geolocation::LinuxProviderLabel::eGeoclue)
          .Set(true);
    }
  }
#  endif
#endif

#ifdef MOZ_WIDGET_COCOA
  if (Preferences::GetBool("geo.provider.use_corelocation", true)) {
    mProvider = new CoreLocationLocationProvider();
  }
#endif

#ifdef XP_WIN
  if (Preferences::GetBool("geo.provider.ms-windows-location", false)) {
    mProvider = new WindowsLocationProvider();
  }
#endif

  if (Preferences::GetBool("geo.provider.use_mls", false)) {
    mProvider = do_CreateInstance("@mozilla.org/geolocation/mls-provider;1");
  }

  // Override platform-specific providers with the default (network)
  // provider while testing. Our tests are currently not meant to exercise
  // the provider, and some tests rely on the network provider being used.
  // "geo.provider.testing" is always set for all plain and browser chrome
  // mochitests, and also for xpcshell tests.
  if (!mProvider || Preferences::GetBool("geo.provider.testing", false)) {
    nsCOMPtr<nsIGeolocationProvider> geoTestProvider =
        do_GetService(NS_GEOLOCATION_PROVIDER_CONTRACTID);

    if (geoTestProvider) {
      mProvider = geoTestProvider;
    }
  }

  return NS_OK;
}

nsGeolocationService::~nsGeolocationService() = default;

NS_IMETHODIMP
nsGeolocationService::Observe(nsISupports* aSubject, const char* aTopic,
                              const char16_t* aData) {
  if (!strcmp("xpcom-shutdown", aTopic)) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, "xpcom-shutdown");
    }

    for (uint32_t i = 0; i < mGeolocators.Length(); i++) {
      mGeolocators[i]->Shutdown();
    }
    StopDevice();

    return NS_OK;
  }

  if (!strcmp("timer-callback", aTopic)) {
    // decide if we can close down the service.
    for (uint32_t i = 0; i < mGeolocators.Length(); i++)
      if (mGeolocators[i]->HasActiveCallbacks()) {
        SetDisconnectTimer();
        return NS_OK;
      }

    // okay to close up.
    StopDevice();
    Update(nullptr);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsGeolocationService::Update(nsIDOMGeoPosition* aSomewhere) {
  if (aSomewhere) {
    SetCachedPosition(aSomewhere);
  }

  for (uint32_t i = 0; i < mGeolocators.Length(); i++) {
    mGeolocators[i]->Update(aSomewhere);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsGeolocationService::NotifyError(uint16_t aErrorCode) {
  MOZ_LOG(
      gGeolocationLog, LogLevel::Debug,
      ("nsGeolocationService::NotifyError with error code: %u", aErrorCode));
  // nsTArray doesn't have a constructors that takes a different-type TArray.
  nsTArray<RefPtr<Geolocation>> geolocators;
  geolocators.AppendElements(mGeolocators);
  for (uint32_t i = 0; i < geolocators.Length(); i++) {
    // MOZ_KnownLive because the stack array above keeps it alive.
    MOZ_KnownLive(geolocators[i])->NotifyError(aErrorCode);
  }
  return NS_OK;
}

void nsGeolocationService::SetCachedPosition(nsIDOMGeoPosition* aPosition) {
  mLastPosition.position = aPosition;
  mLastPosition.isHighAccuracy = mHigherAccuracy;
}

CachedPositionAndAccuracy nsGeolocationService::GetCachedPosition() {
  return mLastPosition;
}

nsresult nsGeolocationService::StartDevice() {
  if (!StaticPrefs::geo_enabled()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // We do not want to keep the geolocation devices online
  // indefinitely.
  // Close them down after a reasonable period of inactivivity.
  SetDisconnectTimer();

  if (XRE_IsContentProcess()) {
    ContentChild* cpc = ContentChild::GetSingleton();
    cpc->SendAddGeolocationListener(HighAccuracyRequested());
    return NS_OK;
  }

  // Start them up!
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return NS_ERROR_FAILURE;
  }

  if (!mProvider) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;

  if (NS_FAILED(rv = mProvider->Startup()) ||
      NS_FAILED(rv = mProvider->Watch(this))) {
    NotifyError(GeolocationPositionError_Binding::POSITION_UNAVAILABLE);
    return rv;
  }

  obs->NotifyObservers(mProvider, "geolocation-device-events", u"starting");

  return NS_OK;
}

void nsGeolocationService::SetDisconnectTimer() {
  if (!mDisconnectTimer) {
    mDisconnectTimer = NS_NewTimer();
  } else {
    mDisconnectTimer->Cancel();
  }

  mDisconnectTimer->Init(this, StaticPrefs::geo_timeout(),
                         nsITimer::TYPE_ONE_SHOT);
}

bool nsGeolocationService::HighAccuracyRequested() {
  for (uint32_t i = 0; i < mGeolocators.Length(); i++) {
    if (mGeolocators[i]->HighAccuracyRequested()) {
      return true;
    }
  }

  return false;
}

void nsGeolocationService::UpdateAccuracy(bool aForceHigh) {
  bool highRequired = aForceHigh || HighAccuracyRequested();

  if (XRE_IsContentProcess()) {
    ContentChild* cpc = ContentChild::GetSingleton();
    if (cpc->IsAlive()) {
      cpc->SendSetGeolocationHigherAccuracy(highRequired);
    }

    return;
  }

  mProvider->SetHighAccuracy(!mHigherAccuracy && highRequired);
  mHigherAccuracy = highRequired;
}

void nsGeolocationService::StopDevice() {
  if (mDisconnectTimer) {
    mDisconnectTimer->Cancel();
    mDisconnectTimer = nullptr;
  }

  if (XRE_IsContentProcess()) {
    ContentChild* cpc = ContentChild::GetSingleton();
    cpc->SendRemoveGeolocationListener();

    return;  // bail early
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return;
  }

  if (!mProvider) {
    return;
  }

  mHigherAccuracy = false;

  mProvider->Shutdown();
  obs->NotifyObservers(mProvider, "geolocation-device-events", u"shutdown");
}

StaticRefPtr<nsGeolocationService> nsGeolocationService::sService;

already_AddRefed<nsGeolocationService>
nsGeolocationService::GetGeolocationService(
    mozilla::dom::BrowsingContext* aBrowsingContext) {
  RefPtr<nsGeolocationService> result;
  if (aBrowsingContext) {
    result = aBrowsingContext->GetGeolocationServiceOverride();

    if (result) {
      return result.forget();
    }
  }
  if (nsGeolocationService::sService) {
    result = nsGeolocationService::sService;

    return result.forget();
  }

  result = new nsGeolocationService();
  if (NS_FAILED(result->Init())) {
    return nullptr;
  }

  ClearOnShutdown(&nsGeolocationService::sService);
  nsGeolocationService::sService = result;
  return result.forget();
}

void nsGeolocationService::AddLocator(Geolocation* aLocator) {
  mGeolocators.AppendElement(aLocator);
}

void nsGeolocationService::RemoveLocator(Geolocation* aLocator) {
  mGeolocators.RemoveElement(aLocator);
}

void nsGeolocationService::MoveLocators(nsGeolocationService* aService) {
  for (uint32_t i = 0; i < mGeolocators.Length(); i++) {
    aService->AddLocator(mGeolocators[i]);
  }
}

////////////////////////////////////////////////////
// Geolocation
////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Geolocation)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIGeolocationUpdate)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Geolocation)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Geolocation)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(Geolocation, mPendingCallbacks,
                                      mWatchingCallbacks, mPendingRequests)

Geolocation::Geolocation()
    : mProtocolType(ProtocolType::OTHER), mLastWatchId(1) {}

Geolocation::~Geolocation() {
  if (mService) {
    Shutdown();
  }
}

StaticRefPtr<Geolocation> Geolocation::sNonWindowSingleton;

already_AddRefed<Geolocation> Geolocation::NonWindowSingleton() {
  if (sNonWindowSingleton) {
    return do_AddRef(sNonWindowSingleton);
  }

  RefPtr<Geolocation> result = new Geolocation();
  DebugOnly<nsresult> rv = result->Init();
  MOZ_ASSERT(NS_SUCCEEDED(rv), "How can this fail?");

  ClearOnShutdown(&sNonWindowSingleton);
  sNonWindowSingleton = result;
  return result.forget();
}

nsresult Geolocation::Init(nsPIDOMWindowInner* aContentDom) {
  nsCOMPtr<Document> doc = aContentDom ? aContentDom->GetExtantDoc() : nullptr;

  // Remember the window
  if (aContentDom) {
    mOwner = do_GetWeakReference(aContentDom);
    if (!mOwner) {
      return NS_ERROR_FAILURE;
    }

    // Grab the principal of the document
    if (!doc) {
      return NS_ERROR_FAILURE;
    }

    mPrincipal = doc->NodePrincipal();
    // Store the protocol to send via telemetry later.
    if (mPrincipal->SchemeIs("http")) {
      mProtocolType = ProtocolType::HTTP;
    } else if (mPrincipal->SchemeIs("https")) {
      mProtocolType = ProtocolType::HTTPS;
    }
  }

  mBrowsingContext =
      doc ? RefPtr<BrowsingContext>(doc->GetBrowsingContext()) : nullptr;

  // If no aContentDom was passed into us, we are being used
  // by chrome/c++ and have no mOwner, no mPrincipal, and no need
  // to prompt.
  mService = nsGeolocationService::GetGeolocationService(mBrowsingContext);

  if (mService) {
    mService->AddLocator(this);
  }

  return NS_OK;
}

void Geolocation::Shutdown() {
  // Release all callbacks
  mPendingCallbacks.Clear();
  mWatchingCallbacks.Clear();

  if (mService) {
    mService->RemoveLocator(this);
    mService->UpdateAccuracy();
  }

  mService = nullptr;
  mPrincipal = nullptr;
}

nsPIDOMWindowInner* Geolocation::GetParentObject() const {
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(mOwner);
  return window.get();
}

bool Geolocation::HasActiveCallbacks() {
  return mPendingCallbacks.Length() || mWatchingCallbacks.Length();
}

bool Geolocation::HighAccuracyRequested() {
  for (uint32_t i = 0; i < mWatchingCallbacks.Length(); i++) {
    if (mWatchingCallbacks[i]->WantsHighAccuracy()) {
      return true;
    }
  }

  for (uint32_t i = 0; i < mPendingCallbacks.Length(); i++) {
    if (mPendingCallbacks[i]->WantsHighAccuracy()) {
      return true;
    }
  }

  return false;
}

void Geolocation::RemoveRequest(nsGeolocationRequest* aRequest) {
  bool requestWasKnown = (mPendingCallbacks.RemoveElement(aRequest) !=
                          mWatchingCallbacks.RemoveElement(aRequest));

  Unused << requestWasKnown;
}

NS_IMETHODIMP
Geolocation::Update(nsIDOMGeoPosition* aSomewhere) {
  if (!WindowOwnerStillExists()) {
    Shutdown();
    return NS_OK;
  }

  // Don't update position if window is not fully active or the document is
  // hidden. We keep the pending callaback and watchers waiting for the next
  // update.
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(this->GetOwner());
  if (window) {
    nsCOMPtr<Document> document = window->GetDoc();
    bool isHidden = document && document->Hidden();
    if (isHidden || !window->IsFullyActive()) {
      return NS_OK;
    }
  }

  if (aSomewhere) {
    nsCOMPtr<nsIDOMGeoPositionCoords> coords;
    aSomewhere->GetCoords(getter_AddRefs(coords));
    if (coords) {
      double accuracy = -1;
      coords->GetAccuracy(&accuracy);
      glean::geolocation::accuracy.AccumulateSingleSample(
          static_cast<uint64_t>(accuracy));
    }
  }

  for (uint32_t i = mPendingCallbacks.Length(); i > 0; i--) {
    mPendingCallbacks[i - 1]->Update(aSomewhere);
    RemoveRequest(mPendingCallbacks[i - 1]);
  }

  // notify everyone that is watching
  for (uint32_t i = 0; i < mWatchingCallbacks.Length(); i++) {
    mWatchingCallbacks[i]->Update(aSomewhere);
  }

  return NS_OK;
}

NS_IMETHODIMP
Geolocation::NotifyError(uint16_t aErrorCode) {
  MOZ_LOG(gGeolocationLog, LogLevel::Debug,
          ("Geolocation::NotifyError with error code: %u", aErrorCode));
  if (!WindowOwnerStillExists()) {
    Shutdown();
    return NS_OK;
  }

  for (uint32_t i = mPendingCallbacks.Length(); i > 0; i--) {
    RefPtr<nsGeolocationRequest> request = mPendingCallbacks[i - 1];
    request->NotifyErrorAndShutdown(aErrorCode);
    // NotifyErrorAndShutdown() removes the request from the array
  }

  // notify everyone that is watching
  for (uint32_t i = 0; i < mWatchingCallbacks.Length(); i++) {
    RefPtr<nsGeolocationRequest> request = mWatchingCallbacks[i];
    request->NotifyErrorAndShutdown(aErrorCode);
  }

  return NS_OK;
}

bool Geolocation::IsFullyActiveOrChrome() {
  // For regular content window, only allow this proceed if the window is "fully
  // active".
  if (nsPIDOMWindowInner* window = this->GetParentObject()) {
    return window->IsFullyActive();
  }
  // Calls coming from chrome code don't have window, so we can proceed.
  return true;
}

bool Geolocation::IsAlreadyCleared(nsGeolocationRequest* aRequest) {
  for (uint32_t i = 0, length = mClearedWatchIDs.Length(); i < length; ++i) {
    if (mClearedWatchIDs[i] == aRequest->WatchId()) {
      return true;
    }
  }

  return false;
}

bool Geolocation::ShouldBlockInsecureRequests() const {
  if (Preferences::GetBool(PREF_GEO_SECURITY_ALLOWINSECURE, false)) {
    return false;
  }

  nsCOMPtr<nsPIDOMWindowInner> win = do_QueryReferent(mOwner);
  if (!win) {
    return false;
  }

  nsCOMPtr<Document> doc = win->GetDoc();
  if (!doc) {
    return false;
  }

  if (!nsGlobalWindowInner::Cast(win)->IsSecureContext()) {
    nsContentUtils::ReportToConsole(nsIScriptError::errorFlag, "DOM"_ns, doc,
                                    nsContentUtils::eDOM_PROPERTIES,
                                    "GeolocationInsecureRequestIsForbidden");
    return true;
  }

  return false;
}

bool Geolocation::ClearPendingRequest(nsGeolocationRequest* aRequest) {
  if (aRequest->IsWatch() && this->IsAlreadyCleared(aRequest)) {
    this->NotifyAllowedRequest(aRequest);
    this->ClearWatch(aRequest->WatchId());
    return true;
  }

  return false;
}

void Geolocation::GetCurrentPosition(PositionCallback& aCallback,
                                     PositionErrorCallback* aErrorCallback,
                                     const PositionOptions& aOptions,
                                     CallerType aCallerType, ErrorResult& aRv) {
  nsresult rv = GetCurrentPosition(
      GeoPositionCallback(&aCallback), GeoPositionErrorCallback(aErrorCallback),
      CreatePositionOptionsCopy(aOptions), aCallerType);

  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
}

nsresult Geolocation::GetCurrentPosition(GeoPositionCallback callback,
                                         GeoPositionErrorCallback errorCallback,
                                         UniquePtr<PositionOptions>&& options,
                                         CallerType aCallerType) {
  if (!IsFullyActiveOrChrome()) {
    RefPtr<GeolocationPositionError> positionError =
        new GeolocationPositionError(
            this, GeolocationPositionError_Binding::POSITION_UNAVAILABLE);
    positionError->NotifyCallback(errorCallback);
    return NS_OK;
  }

  if (mPendingCallbacks.Length() > MAX_GEO_REQUESTS_PER_WINDOW) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // After this we hand over ownership of options to our nsGeolocationRequest.

  nsIEventTarget* target = GetMainThreadSerialEventTarget();
  RefPtr<nsGeolocationRequest> request = new nsGeolocationRequest(
      this, std::move(callback), std::move(errorCallback), std::move(options),
      target);

  if (!StaticPrefs::geo_enabled() || ShouldBlockInsecureRequests() ||
      !request->CheckPermissionDelegate()) {
    request->RequestDelayedTask(target,
                                nsGeolocationRequest::DelayedTaskType::Deny);
    return NS_OK;
  }

  if (!mOwner && aCallerType != CallerType::System) {
    return NS_ERROR_FAILURE;
  }

  if (mOwner) {
    RequestIfPermitted(request);
    return NS_OK;
  }

  if (aCallerType != CallerType::System) {
    return NS_ERROR_FAILURE;
  }

  request->RequestDelayedTask(target,
                              nsGeolocationRequest::DelayedTaskType::Allow);

  return NS_OK;
}

int32_t Geolocation::WatchPosition(PositionCallback& aCallback,
                                   PositionErrorCallback* aErrorCallback,
                                   const PositionOptions& aOptions,
                                   CallerType aCallerType, ErrorResult& aRv) {
  return WatchPosition(GeoPositionCallback(&aCallback),
                       GeoPositionErrorCallback(aErrorCallback),
                       CreatePositionOptionsCopy(aOptions), aCallerType, aRv);
}

int32_t Geolocation::WatchPosition(
    nsIDOMGeoPositionCallback* aCallback,
    nsIDOMGeoPositionErrorCallback* aErrorCallback,
    UniquePtr<PositionOptions>&& aOptions) {
  MOZ_ASSERT(aCallback);

  return WatchPosition(GeoPositionCallback(aCallback),
                       GeoPositionErrorCallback(aErrorCallback),
                       std::move(aOptions), CallerType::System, IgnoreErrors());
}

// On errors we return 0 because that's not a valid watch id and will
// get ignored in clearWatch.
int32_t Geolocation::WatchPosition(GeoPositionCallback aCallback,
                                   GeoPositionErrorCallback aErrorCallback,
                                   UniquePtr<PositionOptions>&& aOptions,
                                   CallerType aCallerType, ErrorResult& aRv) {
  if (!IsFullyActiveOrChrome()) {
    RefPtr<GeolocationPositionError> positionError =
        new GeolocationPositionError(
            this, GeolocationPositionError_Binding::POSITION_UNAVAILABLE);
    positionError->NotifyCallback(aErrorCallback);
    return 0;
  }

  if (mWatchingCallbacks.Length() > MAX_GEO_REQUESTS_PER_WINDOW) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return 0;
  }

  // The watch ID:
  int32_t watchId = mLastWatchId++;

  nsIEventTarget* target = GetMainThreadSerialEventTarget();
  RefPtr<nsGeolocationRequest> request = new nsGeolocationRequest(
      this, std::move(aCallback), std::move(aErrorCallback),
      std::move(aOptions), target, true, watchId);

  if (!StaticPrefs::geo_enabled() || ShouldBlockInsecureRequests() ||
      !request->CheckPermissionDelegate()) {
    request->RequestDelayedTask(target,
                                nsGeolocationRequest::DelayedTaskType::Deny);
    return watchId;
  }

  if (!mOwner && aCallerType != CallerType::System) {
    aRv.Throw(NS_ERROR_FAILURE);
    return 0;
  }

  if (mOwner) {
    RequestIfPermitted(request);
    return watchId;
  }

  if (aCallerType != CallerType::System) {
    aRv.Throw(NS_ERROR_FAILURE);
    return 0;
  }

  request->Allow(JS::UndefinedHandleValue);
  return watchId;
}

void Geolocation::ClearWatch(int32_t aWatchId) {
  if (aWatchId < 1) {
    return;
  }

  if (!mClearedWatchIDs.Contains(aWatchId)) {
    mClearedWatchIDs.AppendElement(aWatchId);
  }

  for (uint32_t i = 0, length = mWatchingCallbacks.Length(); i < length; ++i) {
    if (mWatchingCallbacks[i]->WatchId() == aWatchId) {
      mWatchingCallbacks[i]->Shutdown();
      RemoveRequest(mWatchingCallbacks[i]);
      mClearedWatchIDs.RemoveElement(aWatchId);
      break;
    }
  }

  // make sure we also search through the pending requests lists for
  // watches to clear...
  for (uint32_t i = 0, length = mPendingRequests.Length(); i < length; ++i) {
    if (mPendingRequests[i]->IsWatch() &&
        (mPendingRequests[i]->WatchId() == aWatchId)) {
      mPendingRequests[i]->Shutdown();
      mPendingRequests.RemoveElementAt(i);
      break;
    }
  }
}

bool Geolocation::WindowOwnerStillExists() {
  // an owner was never set when Geolocation
  // was created, which means that this object
  // is being used without a window.
  if (mOwner == nullptr) {
    return true;
  }

  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(mOwner);

  if (window) {
    nsPIDOMWindowOuter* outer = window->GetOuterWindow();
    if (!outer || outer->GetCurrentInnerWindow() != window || outer->Closed()) {
      return false;
    }
  }

  return true;
}

void Geolocation::NotifyAllowedRequest(nsGeolocationRequest* aRequest) {
  if (aRequest->IsWatch()) {
    mWatchingCallbacks.AppendElement(aRequest);
  } else {
    mPendingCallbacks.AppendElement(aRequest);
  }
}

/* static */ bool Geolocation::RegisterRequestWithPrompt(
    nsGeolocationRequest* request) {
  nsIEventTarget* target = GetMainThreadSerialEventTarget();
  ContentPermissionRequestBase::PromptResult pr = request->CheckPromptPrefs();
  if (pr == ContentPermissionRequestBase::PromptResult::Granted) {
    request->RequestDelayedTask(target,
                                nsGeolocationRequest::DelayedTaskType::Allow);
    return true;
  }
  if (pr == ContentPermissionRequestBase::PromptResult::Denied) {
    request->RequestDelayedTask(target,
                                nsGeolocationRequest::DelayedTaskType::Deny);
    return true;
  }

  request->RequestDelayedTask(target,
                              nsGeolocationRequest::DelayedTaskType::Request);
  return true;
}

/* static */ geolocation::SystemGeolocationPermissionBehavior
Geolocation::GetLocationOSPermission() {
  auto permission = geolocation::GetGeolocationPermissionBehavior();

  if (!StaticPrefs::geo_prompt_open_system_prefs() &&
      permission == geolocation::SystemGeolocationPermissionBehavior::
                        GeckoWillPromptUser) {
    return geolocation::SystemGeolocationPermissionBehavior::NoPrompt;
  }
  return permission;
}

void Geolocation::RequestIfPermitted(nsGeolocationRequest* request) {
  auto getPermission = [request = RefPtr{request}](auto aPermission) {
    switch (aPermission) {
      case geolocation::SystemGeolocationPermissionBehavior::
          SystemWillPromptUser:
      case geolocation::SystemGeolocationPermissionBehavior::
          GeckoWillPromptUser:
        request->SetPromptBehavior(aPermission);
        break;
      case geolocation::SystemGeolocationPermissionBehavior::NoPrompt:
        // Either location access is already permitted by OS or the system
        // permission UX is not available for this platform.  Do nothing.
        break;
      default:
        MOZ_ASSERT_UNREACHABLE(
            "unexpected GeolocationPermissionBehavior value");
        break;
    }
    RegisterRequestWithPrompt(request);
  };

  if (auto* contentChild = ContentChild::GetSingleton()) {
    contentChild->SendGetSystemGeolocationPermissionBehavior(
        std::move(getPermission),
        [request =
             RefPtr{request}](mozilla::ipc::ResponseRejectReason aReason) {
          NS_WARNING("Error sending GetSystemGeolocationPermissionBehavior");
          // We still need to run the location request, even if we don't
          // have permission.
          RegisterRequestWithPrompt(request);
        });
  } else {
    MOZ_ASSERT(XRE_IsParentProcess());
    getPermission(GetLocationOSPermission());
  }
}

JSObject* Geolocation::WrapObject(JSContext* aCtx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return Geolocation_Binding::Wrap(aCtx, this, aGivenProto);
}
