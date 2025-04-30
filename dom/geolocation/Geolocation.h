/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Geolocation_h
#define mozilla_dom_Geolocation_h

// Microsoft's API Name hackery sucks
#undef CreateEvent

#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsITimer.h"
#include "nsIObserver.h"
#include "nsIWeakReferenceUtils.h"
#include "nsWrapperCache.h"

#include "nsCycleCollectionParticipant.h"

#include "GeolocationPosition.h"
#include "GeolocationCoordinates.h"
#include "nsIDOMGeoPosition.h"
#include "nsIDOMGeoPositionCallback.h"
#include "nsIDOMGeoPositionErrorCallback.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/GeolocationBinding.h"
#include "mozilla/dom/CallbackObject.h"
#include "GeolocationSystem.h"

#include "nsIGeolocationProvider.h"
#include "mozilla/Attributes.h"

class nsGeolocationService;
class nsGeolocationRequest;

namespace mozilla::dom {
class Geolocation;
using GeoPositionCallback =
    CallbackObjectHolder<PositionCallback, nsIDOMGeoPositionCallback>;
using GeoPositionErrorCallback =
    CallbackObjectHolder<PositionErrorCallback, nsIDOMGeoPositionErrorCallback>;
namespace geolocation {
enum class LocationOSPermission;
}
}  // namespace mozilla::dom

struct CachedPositionAndAccuracy {
  nsCOMPtr<nsIDOMGeoPosition> position;
  bool isHighAccuracy;
};

/**
 * Singleton that manages the geolocation provider
 */
class nsGeolocationService final : public nsIGeolocationUpdate,
                                   public nsIObserver {
 public:
  static already_AddRefed<nsGeolocationService> GetGeolocationService(
      mozilla::dom::BrowsingContext* browsingContext = nullptr);
  static mozilla::StaticRefPtr<nsGeolocationService> sService;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIGEOLOCATIONUPDATE
  NS_DECL_NSIOBSERVER

  nsGeolocationService() = default;

  nsresult Init();

  // Management of the Geolocation objects
  void AddLocator(mozilla::dom::Geolocation* aLocator);
  void RemoveLocator(mozilla::dom::Geolocation* aLocator);

  // Move locators from service override to the original service.
  void MoveLocators(nsGeolocationService* aService);

  void SetCachedPosition(nsIDOMGeoPosition* aPosition);
  CachedPositionAndAccuracy GetCachedPosition();

  // Find and startup a geolocation device (gps, nmea, etc.)
  MOZ_CAN_RUN_SCRIPT nsresult StartDevice();

  // Stop the started geolocation device (gps, nmea, etc.)
  void StopDevice();

  // create, or reinitialize the callback timer
  void SetDisconnectTimer();

  // Update the accuracy and notify the provider if changed
  void UpdateAccuracy(bool aForceHigh = false);
  bool HighAccuracyRequested();

 private:
  ~nsGeolocationService();

  // Disconnect timer.  When this timer expires, it clears all pending callbacks
  // and closes down the provider, unless we are watching a point, and in that
  // case, we disable the disconnect timer.
  nsCOMPtr<nsITimer> mDisconnectTimer;

  // The object providing geo location information to us.
  nsCOMPtr<nsIGeolocationProvider> mProvider;

  // mGeolocators are not owned here.  Their constructor
  // adds them to this list, and their destructor removes
  // them from this list.
  nsTArray<mozilla::dom::Geolocation*> mGeolocators;

  // This is the last geo position that we have seen.
  CachedPositionAndAccuracy mLastPosition;

  // Current state of requests for higher accuracy
  bool mHigherAccuracy = false;
};

namespace mozilla::dom {

/**
 * Can return a geolocation info
 */
class Geolocation final : public nsIGeolocationUpdate, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Geolocation)

  NS_DECL_NSIGEOLOCATIONUPDATE

  Geolocation();

  nsresult Init(nsPIDOMWindowInner* aContentDom = nullptr);

  nsPIDOMWindowInner* GetParentObject() const;
  virtual JSObject* WrapObject(JSContext* aCtx,
                               JS::Handle<JSObject*> aGivenProto) override;

  MOZ_CAN_RUN_SCRIPT
  int32_t WatchPosition(PositionCallback& aCallback,
                        PositionErrorCallback* aErrorCallback,
                        const PositionOptions& aOptions, CallerType aCallerType,
                        ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT
  void GetCurrentPosition(PositionCallback& aCallback,
                          PositionErrorCallback* aErrorCallback,
                          const PositionOptions& aOptions,
                          CallerType aCallerType, ErrorResult& aRv);
  void ClearWatch(int32_t aWatchId);

  // A WatchPosition for C++ use. Returns 0 if we failed to actually watch.
  MOZ_CAN_RUN_SCRIPT
  int32_t WatchPosition(nsIDOMGeoPositionCallback* aCallback,
                        nsIDOMGeoPositionErrorCallback* aErrorCallback,
                        UniquePtr<PositionOptions>&& aOptions);

  // Returns true if any of the callbacks are repeating
  bool HasActiveCallbacks();

  // Register an allowed request
  void NotifyAllowedRequest(nsGeolocationRequest* aRequest);

  // Remove request from all callbacks arrays
  void RemoveRequest(nsGeolocationRequest* request);

  // Check if there is already ClearWatch called for current
  // request & clear if yes
  bool ClearPendingRequest(nsGeolocationRequest* aRequest);

  // Shutting down.
  void Shutdown();

  // Getter for the browsing context that this Geolocation was loaded for
  mozilla::dom::BrowsingContext* GetBrowsingContext() {
    return mBrowsingContext;
  }

  // Getter for the principal that this Geolocation was loaded from
  nsIPrincipal* GetPrincipal() { return mPrincipal; }

  // Getter for the window that this Geolocation is owned by
  nsIWeakReference* GetOwner() { return mOwner; }

  // Check to see if the window still exists
  bool WindowOwnerStillExists();

  // Check to see if any active request requires high accuracy
  bool HighAccuracyRequested();

  // Get the singleton non-window Geolocation instance.  This never returns
  // null.
  static already_AddRefed<Geolocation> NonWindowSingleton();

  static geolocation::SystemGeolocationPermissionBehavior
  GetLocationOSPermission();

  static MOZ_CAN_RUN_SCRIPT void ReallowWithSystemPermissionOrCancel(
      BrowsingContext* aBrowsingContext,
      geolocation::ParentRequestResolver&& aResolver);

 private:
  ~Geolocation();

  MOZ_CAN_RUN_SCRIPT
  nsresult GetCurrentPosition(GeoPositionCallback aCallback,
                              GeoPositionErrorCallback aErrorCallback,
                              UniquePtr<PositionOptions>&& aOptions,
                              CallerType aCallerType);

  MOZ_CAN_RUN_SCRIPT
  int32_t WatchPosition(GeoPositionCallback aCallback,
                        GeoPositionErrorCallback aErrorCallback,
                        UniquePtr<PositionOptions>&& aOptions,
                        CallerType aCallerType, ErrorResult& aRv);

  static bool RegisterRequestWithPrompt(nsGeolocationRequest* request);

  // Check if clearWatch is already called
  bool IsAlreadyCleared(nsGeolocationRequest* aRequest);

  // Returns whether the Geolocation object should block requests
  // within a context that is not secure.
  bool ShouldBlockInsecureRequests() const;

  // Checks if the request is in a content window that is fully active, or the
  // request is coming from a chrome window.
  bool IsFullyActiveOrChrome();

  // Initates the asynchronous process of filling the request.
  static void RequestIfPermitted(nsGeolocationRequest* request);

  // Two callback arrays.  The first |mPendingCallbacks| holds objects for only
  // one callback and then they are released/removed from the array.  The second
  // |mWatchingCallbacks| holds objects until the object is explicitly removed
  // or there is a page change. All requests held by either array are active,
  // that is, they have been allowed and expect to be fulfilled.

  nsTArray<RefPtr<nsGeolocationRequest> > mPendingCallbacks;
  nsTArray<RefPtr<nsGeolocationRequest> > mWatchingCallbacks;

  // window that this was created for.  Weak reference.
  nsWeakPtr mOwner;

  // where the content was loaded from
  nsCOMPtr<nsIPrincipal> mPrincipal;
  RefPtr<mozilla::dom::BrowsingContext> mBrowsingContext;

  // the protocols we want to measure
  enum class ProtocolType : uint8_t { OTHER, HTTP, HTTPS };

  // the protocol used to load the content
  ProtocolType mProtocolType;

  // owning back pointer.
  RefPtr<nsGeolocationService> mService;
  // owning back pointer for service override.
  RefPtr<nsGeolocationService> mServiceOverride;

  // Watch ID
  uint32_t mLastWatchId;

  // Pending requests are used when the service is not ready
  nsTArray<RefPtr<nsGeolocationRequest> > mPendingRequests;

  // Array containing already cleared watch IDs
  nsTArray<int32_t> mClearedWatchIDs;

  // Our cached non-window singleton.
  static mozilla::StaticRefPtr<Geolocation> sNonWindowSingleton;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_Geolocation_h */
