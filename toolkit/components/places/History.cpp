/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Attributes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/MemoryReporting.h"

#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/BrowserChild.h"
#include "nsXULAppAPI.h"

#include "ConcurrentConnection.h"
#include "History.h"
#include "nsNavHistory.h"
#include "nsNavBookmarks.h"
#include "Helpers.h"
#include "PlaceInfo.h"
#include "VisitInfo.h"
#include "nsPlacesMacros.h"
#include "NotifyRankingChanged.h"

#include "mozilla/storage.h"
#include "mozIStorageBindingParamsArray.h"
#include "mozIStorageResultSet.h"
#include "mozIStorageRow.h"
#include "mozilla/dom/Link.h"
#include "nsDocShellCID.h"
#include "mozilla/Components.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsThreadUtils.h"
#include "nsNetUtil.h"
#include "nsIWidget.h"
#include "nsIXPConnect.h"
#include "nsIXULRuntime.h"
#include "nsContentUtils.h"  // for nsAutoScriptBlocker
#include "nsJSUtils.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsPrintfCString.h"
#include "nsTHashtable.h"
#include "jsapi.h"
#include "js/Array.h"  // JS::GetArrayLength, JS::IsArrayObject, JS::NewArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineElement, JS_GetElement, JS_GetProperty
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_places.h"
#include "mozilla/dom/ContentProcessMessageManager.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/PlacesObservers.h"
#include "mozilla/dom/PlacesVisit.h"
#include "mozilla/dom/PlacesVisitTitle.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/glean/PlacesMetrics.h"
#include "mozilla/places/INativePlacesEventCallback.h"

#include "nsIBrowserWindowTracker.h"
#include "nsImportModule.h"
#include "mozilla/StaticPrefs_browser.h"

using namespace mozilla::dom;
using namespace mozilla::ipc;

namespace mozilla::places {

////////////////////////////////////////////////////////////////////////////////
//// Global Defines

// Observer event fired after a visit has been registered in the DB.
#define URI_VISIT_SAVED "uri-visit-saved"

#define DESTINATIONFILEURI_ANNO "downloads/destinationFileURI"_ns

////////////////////////////////////////////////////////////////////////////////
//// VisitData

struct VisitData {
  VisitData()
      : placeId(0),
        visitId(0),
        hidden(true),
        typed(false),
        transitionType(UINT32_MAX),
        visitTime(0),
        frecency(-1),
        lastVisitId(0),
        lastVisitTime(0),
        visitCount(0),
        referrerVisitId(0),
        titleChanged(false),
        isUnrecoverableError(false),
        useFrecencyRedirectBonus(false),
        source(nsINavHistoryService::VISIT_SOURCE_ORGANIC),
        triggeringPlaceId(0),
        triggeringSponsoredURLVisitTimeMS(0),
        bookmarked(false),
        userContextId(0) {
    guid.SetIsVoid(true);
    title.SetIsVoid(true);
    baseDomain.SetIsVoid(true);
    triggeringSearchEngine.SetIsVoid(true);
    triggeringSponsoredURL.SetIsVoid(true);
    triggeringSponsoredURLBaseDomain.SetIsVoid(true);
  }

  explicit VisitData(nsIURI* aURI, nsIURI* aReferrer = nullptr)
      : placeId(0),
        visitId(0),
        hidden(true),
        typed(false),
        transitionType(UINT32_MAX),
        visitTime(0),
        frecency(-1),
        lastVisitId(0),
        lastVisitTime(0),
        visitCount(0),
        referrerVisitId(0),
        titleChanged(false),
        isUnrecoverableError(false),
        useFrecencyRedirectBonus(false),
        source(nsINavHistoryService::VISIT_SOURCE_ORGANIC),
        triggeringPlaceId(0),
        triggeringSponsoredURLVisitTimeMS(0),
        bookmarked(false),
        userContextId(0) {
    MOZ_ASSERT(aURI);
    if (aURI) {
      (void)aURI->GetSpec(spec);
      (void)GetReversedHostname(aURI, revHost);
    }
    if (aReferrer) {
      (void)aReferrer->GetSpec(referrerSpec);
    }
    guid.SetIsVoid(true);
    title.SetIsVoid(true);
    baseDomain.SetIsVoid(true);
    triggeringSearchEngine.SetIsVoid(true);
    triggeringSponsoredURL.SetIsVoid(true);
    triggeringSponsoredURLBaseDomain.SetIsVoid(true);
  }

  /**
   * Sets the transition type of the visit, as well as if it was typed.
   *
   * @param aTransitionType
   *        The transition type constant to set.  Must be one of the
   *        TRANSITION_ constants on nsINavHistoryService.
   */
  void SetTransitionType(uint32_t aTransitionType) {
    typed = aTransitionType == nsINavHistoryService::TRANSITION_TYPED;
    transitionType = aTransitionType;
  }

  int64_t placeId;
  nsCString guid;
  int64_t visitId;
  nsCString spec;
  nsCString baseDomain;
  nsString revHost;
  bool hidden;
  bool typed;
  uint32_t transitionType;
  PRTime visitTime;
  int32_t frecency;
  int64_t lastVisitId;
  PRTime lastVisitTime;
  uint32_t visitCount;

  /**
   * Stores the title.  If this is empty (IsEmpty() returns true), then the
   * title should be removed from the Place.  If the title is void (IsVoid()
   * returns true), then no title has been set on this object, and titleChanged
   * should remain false.
   */
  nsString title;

  nsCString referrerSpec;
  int64_t referrerVisitId;

  // TODO bug 626836 hook up hidden and typed change tracking too!
  bool titleChanged;

  // Indicates whether the visit ended up in an unrecoverable error.
  bool isUnrecoverableError;

  // Whether to override the visit type bonus with a redirect bonus when
  // calculating frecency on the most recent visit.
  bool useFrecencyRedirectBonus;

  uint16_t source;
  nsCString triggeringSearchEngine;
  int64_t triggeringPlaceId;
  nsCString triggeringSponsoredURL;
  nsCString triggeringSponsoredURLBaseDomain;
  int64_t triggeringSponsoredURLVisitTimeMS;
  bool bookmarked;
  uint32_t userContextId;
};

////////////////////////////////////////////////////////////////////////////////
//// Anonymous Helpers

namespace {

/**
 * Convert the given js value to a js array.
 *
 * @param [in] aValue
 *        the JS value to convert.
 * @param [in] aCtx
 *        The JSContext for aValue.
 * @param [out] _array
 *        the JS array.
 * @param [out] _arrayLength
 *        _array's length.
 */
nsresult GetJSArrayFromJSValue(JS::Handle<JS::Value> aValue, JSContext* aCtx,
                               JS::MutableHandle<JSObject*> _array,
                               uint32_t* _arrayLength) {
  if (aValue.isObjectOrNull()) {
    JS::Rooted<JSObject*> val(aCtx, aValue.toObjectOrNull());
    bool isArray;
    if (!JS::IsArrayObject(aCtx, val, &isArray)) {
      return NS_ERROR_UNEXPECTED;
    }
    if (isArray) {
      _array.set(val);
      (void)JS::GetArrayLength(aCtx, _array, _arrayLength);
      NS_ENSURE_ARG(*_arrayLength > 0);
      return NS_OK;
    }
  }

  // Build a temporary array to store this one item so the code below can
  // just loop.
  *_arrayLength = 1;
  _array.set(JS::NewArrayObject(aCtx, 0));
  NS_ENSURE_TRUE(_array, NS_ERROR_OUT_OF_MEMORY);

  bool rc = JS_DefineElement(aCtx, _array, 0, aValue, 0);
  NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  return NS_OK;
}

/**
 * Attemps to convert a given js value to a nsIURI object.
 * @param aCtx
 *        The JSContext for aValue.
 * @param aValue
 *        The JS value to convert.
 * @return the nsIURI object, or null if aValue is not a nsIURI object.
 */
already_AddRefed<nsIURI> GetJSValueAsURI(JSContext* aCtx,
                                         const JS::Value& aValue) {
  if (!aValue.isPrimitive()) {
    nsCOMPtr<nsIXPConnect> xpc = nsIXPConnect::XPConnect();

    nsCOMPtr<nsIXPConnectWrappedNative> wrappedObj;
    JS::Rooted<JSObject*> obj(aCtx, aValue.toObjectOrNull());
    nsresult rv =
        xpc->GetWrappedNativeOfJSObject(aCtx, obj, getter_AddRefs(wrappedObj));
    NS_ENSURE_SUCCESS(rv, nullptr);
    nsCOMPtr<nsIURI> uri = do_QueryInterface(wrappedObj->Native());
    return uri.forget();
  }
  return nullptr;
}

/**
 * Obtains an nsIURI from the "uri" property of a JSObject.
 *
 * @param aCtx
 *        The JSContext for aObject.
 * @param aObject
 *        The JSObject to get the URI from.
 * @param aProperty
 *        The name of the property to get the URI from.
 * @return the URI if it exists.
 */
already_AddRefed<nsIURI> GetURIFromJSObject(JSContext* aCtx,
                                            JS::Handle<JSObject*> aObject,
                                            const char* aProperty) {
  JS::Rooted<JS::Value> uriVal(aCtx);
  bool rc = JS_GetProperty(aCtx, aObject, aProperty, &uriVal);
  NS_ENSURE_TRUE(rc, nullptr);
  return GetJSValueAsURI(aCtx, uriVal);
}

/**
 * Attemps to convert a JS value to a string.
 * @param aCtx
 *        The JSContext for aObject.
 * @param aValue
 *        The JS value to convert.
 * @param _string
 *        The string to populate with the value, or set it to void.
 */
void GetJSValueAsString(JSContext* aCtx, const JS::Value& aValue,
                        nsString& _string) {
  if (aValue.isUndefined() || !(aValue.isNull() || aValue.isString())) {
    _string.SetIsVoid(true);
    return;
  }

  // |null| in JS maps to the empty string.
  if (aValue.isNull()) {
    _string.Truncate();
    return;
  }

  if (!AssignJSString(aCtx, _string, aValue.toString())) {
    _string.SetIsVoid(true);
  }
}

/**
 * Obtains the specified property of a JSObject.
 *
 * @param aCtx
 *        The JSContext for aObject.
 * @param aObject
 *        The JSObject to get the string from.
 * @param aProperty
 *        The property to get the value from.
 * @param _string
 *        The string to populate with the value, or set it to void.
 */
void GetStringFromJSObject(JSContext* aCtx, JS::Handle<JSObject*> aObject,
                           const char* aProperty, nsString& _string) {
  JS::Rooted<JS::Value> val(aCtx);
  bool rc = JS_GetProperty(aCtx, aObject, aProperty, &val);
  if (!rc) {
    _string.SetIsVoid(true);
    return;
  }
  GetJSValueAsString(aCtx, val, _string);
}

/**
 * Obtains the specified property of a JSObject.
 *
 * @param aCtx
 *        The JSContext for aObject.
 * @param aObject
 *        The JSObject to get the int from.
 * @param aProperty
 *        The property to get the value from.
 * @param _int
 *        The integer to populate with the value on success.
 */
template <typename IntType>
nsresult GetIntFromJSObject(JSContext* aCtx, JS::Handle<JSObject*> aObject,
                            const char* aProperty, IntType* _int) {
  JS::Rooted<JS::Value> value(aCtx);
  bool rc = JS_GetProperty(aCtx, aObject, aProperty, &value);
  NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  if (value.isUndefined()) {
    return NS_ERROR_INVALID_ARG;
  }
  NS_ENSURE_ARG(value.isPrimitive());
  NS_ENSURE_ARG(value.isNumber());

  int64_t num;
  NS_ENSURE_ARG(mozilla::NumberEqualsInt64(value.toNumber(), &num));
  CheckedInt<IntType> checked(num);
  NS_ENSURE_ARG(checked.isValid());

  *_int = checked.value();
  return NS_OK;
}

/**
 * Obtains the specified property of a JSObject.
 *
 * @pre aArray must be an Array object.
 *
 * @param aCtx
 *        The JSContext for aArray.
 * @param aArray
 *        The JSObject to get the object from.
 * @param aIndex
 *        The index to get the object from.
 * @param objOut
 *        Set to the JSObject pointer on success.
 */
nsresult GetJSObjectFromArray(JSContext* aCtx, JS::Handle<JSObject*> aArray,
                              uint32_t aIndex,
                              JS::MutableHandle<JSObject*> objOut) {
  JS::Rooted<JS::Value> value(aCtx);
  bool rc = JS_GetElement(aCtx, aArray, aIndex, &value);
  NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  NS_ENSURE_ARG(!value.isPrimitive());
  objOut.set(&value.toObject());
  return NS_OK;
}

}  // namespace

class HistoryClearedListener final : public INativePlacesEventCallback {
 public:
  NS_INLINE_DECL_REFCOUNTING(HistoryClearedListener, override)

  HistoryClearedListener() {
    AutoTArray<PlacesEventType, 1> events;
    events.AppendElement(PlacesEventType::History_cleared);
    PlacesObservers::AddListener(events, this);
  }

  void HandlePlacesEvent(const PlacesEventSequence& aEvents) override {
    for (const auto& event : aEvents) {
      if (event->Type() == PlacesEventType::History_cleared) {
        // The History service owns this listener, so it is still alive.
        History* history = History::GetService();
        // Reloads of recently visited URIs are not stored, so forget the
        // visits that were just cleared.
        history->ClearRecentlyVisitedURIs();
        history->OnHistoryCleared();
        return;
      }
    }
  }

 private:
  ~HistoryClearedListener() = default;
};

class VisitedQuery final : public PendingStatementCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  struct VisitedUrlInfo {
    VisitedUrlInfo(nsCOMPtr<nsIURI>&& aURI,
                   History::ContentParentSet&& aContentParentSet)
        : mURI(std::move(aURI)),
          mContentParentSet(std::move(aContentParentSet)) {}
    nsCOMPtr<nsIURI> mURI;
    bool mIsVisited{};
    History::ContentParentSet mContentParentSet;
  };

  using PendingVisitedQueries =
      nsTHashMap<nsURIHashKey, History::ContentParentSet>;

  using VisitedUrlsToContentParentSet =
      nsTHashMap<nsCStringHashKey, VisitedUrlInfo>;

  static nsresult Start(PendingVisitedQueries&& aURIsToContentParentSet) {
    MOZ_ASSERT(XRE_IsParentProcess());
    MOZ_ASSERT(NS_IsMainThread());

    History* history = History::GetService();
    NS_ENSURE_STATE(history);

    VisitedUrlsToContentParentSet urls(aURIsToContentParentSet.Count());
    for (auto& origEntry : aURIsToContentParentSet) {
      auto& data = *origEntry.GetModifiableData();
      nsCOMPtr<nsIURI> URI = origEntry.GetKey();
      nsAutoCString spec;
      if (NS_SUCCEEDED(URI->GetSpec(spec))) {
        VisitedUrlInfo info = VisitedUrlInfo(std::move(URI), std::move(data));
        urls.InsertOrUpdate(spec, std::move(info));
      }
    }
    RefPtr<VisitedQuery> query = new VisitedQuery(history, std::move(urls));
    return history->QueueVisitedStatement(std::move(query));
  }

  static nsresult Start(nsIURI* aURI,
                        mozIVisitedStatusCallback* aCallback = nullptr) {
    MOZ_ASSERT(aURI, "Null URI");
    MOZ_ASSERT(XRE_IsParentProcess());
    MOZ_ASSERT(NS_IsMainThread());

    nsMainThreadPtrHandle<mozIVisitedStatusCallback> callback(
        new nsMainThreadPtrHolder<mozIVisitedStatusCallback>(
            "mozIVisitedStatusCallback", aCallback));

    History* history = History::GetService();
    NS_ENSURE_STATE(history);
    VisitedUrlsToContentParentSet urls;
    nsAutoCString spec;
    if (NS_SUCCEEDED(aURI->GetSpec(spec))) {
      nsCOMPtr<nsIURI> uri = aURI;
      VisitedUrlInfo info =
          VisitedUrlInfo(std::move(uri), History::ContentParentSet());
      urls.InsertOrUpdate(spec, std::move(info));
    }
    RefPtr<VisitedQuery> query =
        new VisitedQuery(history, std::move(urls), callback);
    return history->QueueVisitedStatement(std::move(query));
  }

  NS_IMETHOD HandleResult(mozIStorageResultSet* aResultSet) override {
    nsCOMPtr<mozIStorageRow> row;
    while (NS_SUCCEEDED(aResultSet->GetNextRow(getter_AddRefs(row))) && row) {
      nsAutoCString spec;
      nsresult rv = row->GetUTF8String(0, spec);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        // Ignore this error and continue updating others.
        continue;
      }
      if (auto entry = mUrlsToContentParentSet.Lookup(spec)) {
        entry.Data().mIsVisited = !!row->AsInt64(1);
      }
    }
    return NS_OK;
  }

  NS_IMETHOD
  HandleError(mozIStorageError* aError) override {
    // We'll assume unvisited, that is the default value.
    return NS_OK;
  }

  NS_IMETHOD HandleCompletion(uint16_t aReason) override {
    if (aReason != mozIStorageStatementCallback::REASON_FINISHED) {
      return NS_OK;
    }
    History* history = History::GetService();
    if (history && history->HistoryClearedCount() != mHistoryClearedCount) {
      // History was cleared while this query ran, its results may be stale.
      for (auto& entry : mUrlsToContentParentSet) {
        entry.GetModifiableData()->mIsVisited = false;
      }
      RefPtr<VisitedQuery> query = new VisitedQuery(
          history, std::move(mUrlsToContentParentSet), mCallback);
      (void)history->QueueVisitedStatement(std::move(query));
      return NS_OK;
    }
    NotifyVisitedStatus();
    return NS_OK;
  }

  nsresult BindParams(mozIStorageBindingParamsArray* aParamsArray) override {
    NS_ENSURE_ARG(aParamsArray);
    nsTArray<nsCString> urls =
        ToTArray<nsTArray<nsCString>>(mUrlsToContentParentSet.Keys());
    nsCOMPtr<mozIStorageBindingParams> params;
    nsresult rv = aParamsArray->NewBindingParams(getter_AddRefs(params));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = params->BindArrayOfUTF8StringsByIndex(0, urls);
    NS_ENSURE_SUCCESS(rv, rv);
    return aParamsArray->AddParams(params);
  }

  void NotifyVisitedStatus() {
    // If an external handling callback is provided, just notify through it.
    if (mCallback) {
      MOZ_DIAGNOSTIC_ASSERT(
          mUrlsToContentParentSet.Count() == 1,
          "Should only have 1 URI when a callback is provided");
      const auto iter = mUrlsToContentParentSet.ConstIter();
      if (!iter.Done()) {
        const VisitedUrlInfo& info = iter.Data();
        mCallback->IsVisited(info.mURI, info.mIsVisited);
      }
      return;
    }

    if (History* history = History::GetService()) {
      for (const auto& entry : mUrlsToContentParentSet) {
        const VisitedUrlInfo& info = entry.GetData();
        auto status = info.mIsVisited ? IHistory::VisitedStatus::Visited
                                      : IHistory::VisitedStatus::Unvisited;
        history->NotifyVisited(info.mURI, status, &info.mContentParentSet);
      }
    }
  }

 private:
  VisitedQuery(History* aHistory,
               VisitedUrlsToContentParentSet&& aUrlsToContentParentSet,
               const nsMainThreadPtrHandle<mozIVisitedStatusCallback>&
                   aCallback = nullptr)
      : mUrlsToContentParentSet(std::move(aUrlsToContentParentSet)),
        mCallback(aCallback),
        mHistoryClearedCount(aHistory->HistoryClearedCount()) {}

  ~VisitedQuery() = default;

  VisitedUrlsToContentParentSet mUrlsToContentParentSet;
  nsMainThreadPtrHandle<mozIVisitedStatusCallback> mCallback;
  uint32_t mHistoryClearedCount;
};

NS_IMPL_ISUPPORTS_INHERITED0(VisitedQuery, PendingStatementCallback)

/**
 * Notifies observers about a visit or an array of visits.
 */
class NotifyManyVisitsObservers : public Runnable {
 public:
  explicit NotifyManyVisitsObservers(const VisitData& aPlace)
      : Runnable("places::NotifyManyVisitsObservers"),
        mPlaces({aPlace}),
        mHistory(History::GetService()) {}

  explicit NotifyManyVisitsObservers(nsTArray<VisitData>&& aPlaces)
      : Runnable("places::NotifyManyVisitsObservers"),
        mPlaces(std::move(aPlaces)),
        mHistory(History::GetService()) {}

  nsresult NotifyVisit(nsNavHistory* aNavHistory,
                       nsCOMPtr<nsIObserverService>& aObsService, PRTime aNow,
                       nsIURI* aURI, const VisitData& aPlace) {
    if (aObsService) {
      DebugOnly<nsresult> rv =
          aObsService->NotifyObservers(aURI, URI_VISIT_SAVED, nullptr);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Could not notify observers");
    }

    if (aNow - aPlace.visitTime < RECENTLY_VISITED_URIS_MAX_AGE) {
      mHistory->AppendToRecentlyVisitedURIs(aURI, aPlace.hidden);
    }
    mHistory->NotifyVisited(aURI, IHistory::VisitedStatus::Visited);

    aNavHistory->UpdateDaysOfHistory(aPlace.visitTime);

    return NS_OK;
  }

  void AddPlaceForNotify(const VisitData& aPlace,
                         Sequence<OwningNonNull<PlacesEvent>>& aEvents) {
    if (aPlace.transitionType == nsINavHistoryService::TRANSITION_EMBED) {
      return;
    }

    RefPtr<PlacesVisit> visitEvent = new PlacesVisit();
    visitEvent->mVisitId = aPlace.visitId;
    visitEvent->mUrl.Assign(NS_ConvertUTF8toUTF16(aPlace.spec));
    visitEvent->mVisitTime = aPlace.visitTime / 1000;
    visitEvent->mReferringVisitId = aPlace.referrerVisitId;
    visitEvent->mTransitionType = aPlace.transitionType;
    visitEvent->mPageGuid.Assign(aPlace.guid);
    visitEvent->mFrecency = aPlace.frecency;
    visitEvent->mHidden = aPlace.hidden;
    visitEvent->mVisitCount = aPlace.visitCount + 1;  // Add current visit
    visitEvent->mTypedCount = static_cast<uint32_t>(aPlace.typed);
    visitEvent->mLastKnownTitle.Assign(aPlace.title);
    visitEvent->mUserContextId = aPlace.userContextId;

    bool success = !!aEvents.AppendElement(visitEvent.forget(), fallible);
    MOZ_RELEASE_ASSERT(success);

    if (aPlace.titleChanged) {
      RefPtr<PlacesVisitTitle> titleEvent = new PlacesVisitTitle();
      titleEvent->mUrl.Assign(NS_ConvertUTF8toUTF16(aPlace.spec));
      titleEvent->mPageGuid.Assign(aPlace.guid);
      titleEvent->mTitle.Assign(aPlace.title);
      bool success = !!aEvents.AppendElement(titleEvent.forget(), fallible);
      MOZ_RELEASE_ASSERT(success);
    }
  }

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is marked
  // MOZ_CAN_RUN_SCRIPT.  See bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread(), "This should be called on the main thread");

    // We are in the main thread, no need to lock.
    if (mHistory->IsShuttingDown()) {
      // If we are shutting down, we cannot notify the observers.
      return NS_OK;
    }

    nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
    if (!navHistory) {
      NS_WARNING(
          "Trying to notify visits observers but cannot get the history "
          "service!");
      return NS_OK;
    }

    nsCOMPtr<nsIObserverService> obsService =
        mozilla::services::GetObserverService();

    Sequence<OwningNonNull<PlacesEvent>> events;
    PRTime now = PR_Now();
    for (uint32_t i = 0; i < mPlaces.Length(); ++i) {
      nsCOMPtr<nsIURI> uri;
      if (NS_WARN_IF(
              NS_FAILED(NS_NewURI(getter_AddRefs(uri), mPlaces[i].spec)))) {
        return NS_ERROR_UNEXPECTED;
      }
      AddPlaceForNotify(mPlaces[i], events);

      nsresult rv = NotifyVisit(navHistory, obsService, now, uri, mPlaces[i]);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (events.Length() > 0) {
      PlacesObservers::NotifyListeners(events);
    }

    return NS_OK;
  }

 private:
  AutoTArray<VisitData, 1> mPlaces;
  RefPtr<History> mHistory;
};

/**
 * Notifies observers about a pages title changing.
 */
class NotifyTitleObservers : public Runnable {
 public:
  /**
   * Notifies observers on the main thread.
   *
   * @param aSpec
   *        The spec of the URI to notify about.
   * @param aTitle
   *        The new title to notify about.
   */
  NotifyTitleObservers(const nsCString& aSpec, const nsString& aTitle,
                       const nsCString& aGUID)
      : Runnable("places::NotifyTitleObservers"),
        mSpec(aSpec),
        mTitle(aTitle),
        mGUID(aGUID) {}

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is marked
  // MOZ_CAN_RUN_SCRIPT.  See bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread(), "This should be called on the main thread");

    RefPtr<PlacesVisitTitle> titleEvent = new PlacesVisitTitle();
    titleEvent->mUrl.Assign(NS_ConvertUTF8toUTF16(mSpec));
    titleEvent->mPageGuid.Assign(mGUID);
    titleEvent->mTitle.Assign(mTitle);

    Sequence<OwningNonNull<PlacesEvent>> events;
    bool success = !!events.AppendElement(titleEvent.forget(), fallible);
    MOZ_RELEASE_ASSERT(success);

    PlacesObservers::NotifyListeners(events);

    return NS_OK;
  }

 private:
  const nsCString mSpec;
  const nsString mTitle;
  const nsCString mGUID;
};

/**
 * Helper class for methods which notify their callers through the
 * mozIVisitInfoCallback interface.
 */
class NotifyPlaceInfoCallback : public Runnable {
 public:
  NotifyPlaceInfoCallback(
      const nsMainThreadPtrHandle<mozIVisitInfoCallback>& aCallback,
      const VisitData& aPlace, bool aIsSingleVisit, nsresult aResult)
      : Runnable("places::NotifyPlaceInfoCallback"),
        mCallback(aCallback),
        mPlace(aPlace),
        mResult(aResult),
        mIsSingleVisit(aIsSingleVisit) {
    MOZ_ASSERT(aCallback, "Must pass a non-null callback!");
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread(), "This should be called on the main thread");

    bool hasValidURIs = true;
    nsCOMPtr<nsIURI> referrerURI;
    if (!mPlace.referrerSpec.IsEmpty()) {
      hasValidURIs = !NS_WARN_IF(NS_FAILED(
          NS_NewURI(getter_AddRefs(referrerURI), mPlace.referrerSpec)));
    }

    nsCOMPtr<nsIURI> uri;
    hasValidURIs =
        hasValidURIs &&
        !NS_WARN_IF(NS_FAILED(NS_NewURI(getter_AddRefs(uri), mPlace.spec)));

    nsCOMPtr<mozIPlaceInfo> place;
    if (mIsSingleVisit) {
      nsCOMPtr<mozIVisitInfo> visit =
          new VisitInfo(mPlace.visitId, mPlace.visitTime, mPlace.transitionType,
                        referrerURI.forget());
      PlaceInfo::VisitsArray visits;
      (void)visits.AppendElement(visit);

      // The frecency isn't exposed because it may not reflect the updated value
      // in the case of InsertVisitedURIs.
      place = new PlaceInfo(mPlace.placeId, mPlace.guid, uri.forget(),
                            mPlace.title, -1, visits);
    } else {
      // Same as above.
      place = new PlaceInfo(mPlace.placeId, mPlace.guid, uri.forget(),
                            mPlace.title, -1);
    }

    if (NS_SUCCEEDED(mResult) && hasValidURIs) {
      (void)mCallback->HandleResult(place);
    } else {
      (void)mCallback->HandleError(mResult, place);
    }

    return NS_OK;
  }

 private:
  nsMainThreadPtrHandle<mozIVisitInfoCallback> mCallback;
  VisitData mPlace;
  const nsresult mResult;
  bool mIsSingleVisit;
};

/**
 * Notifies a callback object when the operation is complete.
 */
class NotifyCompletion : public Runnable {
 public:
  explicit NotifyCompletion(
      const nsMainThreadPtrHandle<mozIVisitInfoCallback>& aCallback,
      uint32_t aUpdatedCount = 0)
      : Runnable("places::NotifyCompletion"),
        mCallback(aCallback),
        mUpdatedCount(aUpdatedCount) {
    MOZ_ASSERT(aCallback, "Must pass a non-null callback!");
  }

  NS_IMETHOD Run() override {
    if (NS_IsMainThread()) {
      (void)mCallback->HandleCompletion(mUpdatedCount);
    } else {
      (void)NS_DispatchToMainThread(this);
    }
    return NS_OK;
  }

 private:
  nsMainThreadPtrHandle<mozIVisitInfoCallback> mCallback;
  uint32_t mUpdatedCount;
};

/**
 * Checks to see if we can add aURI to history, and dispatches an error to
 * aCallback (if provided) if we cannot.
 *
 * @param aURI
 *        The URI to check.
 * @param [optional] aGUID
 *        The guid of the URI to check.  This is passed back to the callback.
 * @param [optional] aCallback
 *        The callback to notify if the URI cannot be added to history.
 * @return true if the URI can be added to history, false otherwise.
 */
bool CanAddURI(nsIURI* aURI, const nsCString& aGUID = ""_ns,
               mozIVisitInfoCallback* aCallback = nullptr) {
  MOZ_ASSERT(NS_IsMainThread());
  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(navHistory, false);

  bool canAdd;
  nsresult rv = navHistory->CanAddURI(aURI, &canAdd);
  if (NS_SUCCEEDED(rv) && canAdd) {
    return true;
  };

  // We cannot add the URI.  Notify the callback, if we were given one.
  if (aCallback) {
    VisitData place(aURI);
    place.guid = aGUID;
    nsMainThreadPtrHandle<mozIVisitInfoCallback> callback(
        new nsMainThreadPtrHolder<mozIVisitInfoCallback>(
            "mozIVisitInfoCallback", aCallback));
    nsCOMPtr<nsIRunnable> event = new NotifyPlaceInfoCallback(
        callback, place, true, NS_ERROR_INVALID_ARG);
    (void)NS_DispatchToMainThread(event);
  }

  return false;
}

/**
 * Adds a visit to the database.
 */
class InsertVisitedURIs final : public Runnable {
 public:
  /**
   * Adds a visit to the database asynchronously.
   *
   * @param aConnection
   *        The database connection to use for these operations.
   * @param aPlaces
   *        The locations to record visits.
   * @param [optional] aCallback
   *        The callback to notify about the visit.
   */
  static nsresult Start(mozIStorageConnection* aConnection,
                        nsTArray<VisitData>&& aPlaces,
                        mozIVisitInfoCallback* aCallback = nullptr,
                        uint32_t aInitialUpdatedCount = 0) {
    MOZ_ASSERT(NS_IsMainThread(), "This should be called on the main thread");
    MOZ_ASSERT(aPlaces.Length() > 0, "Must pass a non-empty array!");

    // Make sure nsNavHistory service is up before proceeding:
    nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
    MOZ_ASSERT(navHistory, "Could not get nsNavHistory?!");
    if (!navHistory) {
      return NS_ERROR_FAILURE;
    }

    nsMainThreadPtrHandle<mozIVisitInfoCallback> callback(
        new nsMainThreadPtrHolder<mozIVisitInfoCallback>(
            "mozIVisitInfoCallback", aCallback));
    bool ignoreErrors = false, ignoreResults = false;
    if (aCallback) {
      // We ignore errors from either of these methods in case old JS consumers
      // don't implement them (in which case they will get error/result
      // notifications as normal).
      (void)aCallback->GetIgnoreErrors(&ignoreErrors);
      (void)aCallback->GetIgnoreResults(&ignoreResults);
    }
    RefPtr<InsertVisitedURIs> event = new InsertVisitedURIs(
        aConnection, std::move(aPlaces), callback, ignoreErrors, ignoreResults,
        aInitialUpdatedCount);

    // Get the target thread, and then start the work!
    nsCOMPtr<nsIEventTarget> target = do_GetInterface(aConnection);
    NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
    nsresult rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(!NS_IsMainThread(),
               "This should not be called on the main thread");

    // The inner run method may bail out at any point, so we ensure we do
    // whatever we can and then notify the main thread we're done.
    nsresult rv = InnerRun();

    if (!!mCallback) {
      NS_DispatchToMainThread(
          new NotifyCompletion(mCallback, mSuccessfulUpdatedCount));
    }
    return rv;
  }

  nsresult InnerRun() {
    MOZ_ASSERT(!NS_IsMainThread());
    // Prevent Shutdown() from proceeding while this is running.
    MutexAutoLock lockedScope(mHistory->mBlockShutdownMutex);
    // Check if we were already shutting down.
    if (mHistory->IsShuttingDown()) {
      return NS_OK;
    }

    mozStorageTransaction transaction(
        mDBConn, false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

    // XXX Handle the error, bug 1696133.
    (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

    const VisitData* lastFetchedPlace = nullptr;
    uint32_t lastFetchedVisitCount = 0;
    bool shouldChunkNotifications = mPlaces.Length() > NOTIFY_VISITS_CHUNK_SIZE;
    nsTArray<VisitData> notificationChunk;
    if (shouldChunkNotifications) {
      notificationChunk.SetCapacity(NOTIFY_VISITS_CHUNK_SIZE);
    }

    // This is an optimization for frecency updating, if all the entries point
    // to the same URL (inserting multiple visits for the same url), then we can
    // update frecency once at the end of the loop. Otherwise, if there's
    // multiple pages, we'll delay frecency recalculation to a later time.
    bool shouldUpdateFrecency = false;

    for (nsTArray<VisitData>::size_type i = 0; i < mPlaces.Length(); i++) {
      VisitData& place = mPlaces.ElementAt(i);

      if (i == 0) {
        // isUnrecoverableError can only be defined when this is invoked by
        // VisitURI, to insert a single visit. When it's defined, the page
        // will be hidden, thus it's not worth updating.
        shouldUpdateFrecency = !place.isUnrecoverableError;
      } else if (shouldUpdateFrecency &&
                 (!place.spec.Equals(mPlaces.ElementAt(i - 1).spec))) {
        // We have multiple entries with different URLs, delay recalculation.
        // A SQL trigger will set recalc_frecency automatically when a visit
        // is added.
        shouldUpdateFrecency = false;
      }
      // Fetching from the database can overwrite this information, so save it
      // apart.
      bool typed = place.typed;
      bool hidden = place.hidden;

      // We can avoid a database lookup if it's the same place as the last
      // visit we added.
      bool known =
          lastFetchedPlace && lastFetchedPlace->spec.Equals(place.spec);
      if (!known) {
        nsresult rv = mHistory->FetchPageInfo(place, &known);
        if (NS_FAILED(rv)) {
          if (!!mCallback && !mIgnoreErrors) {
            nsCOMPtr<nsIRunnable> event =
                new NotifyPlaceInfoCallback(mCallback, place, true, rv);
            return NS_DispatchToMainThread(event);
          }
          return NS_OK;
        }
        lastFetchedPlace = &mPlaces.ElementAt(i);
        lastFetchedVisitCount = lastFetchedPlace->visitCount;
      } else {
        // Copy over the data from the already known place.
        place.placeId = lastFetchedPlace->placeId;
        place.guid = lastFetchedPlace->guid;
        place.lastVisitId = lastFetchedPlace->visitId;
        place.lastVisitTime = lastFetchedPlace->visitTime;
        if (!place.title.IsVoid()) {
          place.titleChanged = !lastFetchedPlace->title.Equals(place.title);
        }
        place.frecency = lastFetchedPlace->frecency;
        // Add one visit for the previous loop.
        place.visitCount = ++lastFetchedVisitCount;
      }

      // If any transition is typed, ensure the page is marked as typed.
      if (typed != lastFetchedPlace->typed) {
        place.typed = true;
      }

      // If any transition is visible, ensure the page is marked as visible.
      if (hidden != lastFetchedPlace->hidden) {
        place.hidden = false;
      }

      FetchReferrerInfo(place);
      UpdateVisitSource(place, mHistory);

      nsresult rv = DoDatabaseInserts(known, place);
      if (!!mCallback) {
        // Check if consumers wanted to be notified about success/failure,
        // depending on whether this action succeeded or not.
        if ((NS_SUCCEEDED(rv) && !mIgnoreResults) ||
            (NS_FAILED(rv) && !mIgnoreErrors)) {
          nsCOMPtr<nsIRunnable> event =
              new NotifyPlaceInfoCallback(mCallback, place, true, rv);
          nsresult rv2 = NS_DispatchToMainThread(event);
          NS_ENSURE_SUCCESS(rv2, rv2);
        }
      }
      NS_ENSURE_SUCCESS(rv, rv);

      if (shouldChunkNotifications) {
        int32_t numRemaining = (int32_t)(mPlaces.Length() - (i + 1));
        notificationChunk.AppendElement(place);
        if (notificationChunk.Length() == NOTIFY_VISITS_CHUNK_SIZE ||
            numRemaining == 0) {
          nsCOMPtr<nsIRunnable> event =
              new NotifyManyVisitsObservers(std::move(notificationChunk));
          rv = NS_DispatchToMainThread(event);
          NS_ENSURE_SUCCESS(rv, rv);

          int32_t nextCapacity =
              std::min(NOTIFY_VISITS_CHUNK_SIZE, numRemaining);
          notificationChunk.SetCapacity(nextCapacity);
        }
      }

      // If we get here, we must have been successful adding/updating this
      // visit/place, so update the count:
      mSuccessfulUpdatedCount++;
    }

    if (shouldUpdateFrecency) {
      VisitData& place = mPlaces.ElementAt(0);
      if (NS_SUCCEEDED(UpdateFrecency(
              place.placeId,
              place.useFrecencyRedirectBonus && mPlaces.Length() == 1))) {
        // Notifying a new visit should be sufficient to know that frecency
        // changed, but since historically we notified a frecency change, for
        // now we'll continue doing it, and re-evaluate in the future.
        NS_DispatchToMainThread(new NotifyRankingChanged());
      }
    }

    nsresult rv = transaction.Commit();
    NS_ENSURE_SUCCESS(rv, rv);

    // If we don't need to chunk the notifications, just notify using the
    // original mPlaces array.
    if (!shouldChunkNotifications) {
      nsCOMPtr<nsIRunnable> event =
          new NotifyManyVisitsObservers(std::move(mPlaces));
      rv = NS_DispatchToMainThread(event);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

 private:
  InsertVisitedURIs(
      mozIStorageConnection* aConnection, nsTArray<VisitData>&& aPlaces,
      const nsMainThreadPtrHandle<mozIVisitInfoCallback>& aCallback,
      bool aIgnoreErrors, bool aIgnoreResults, uint32_t aInitialUpdatedCount)
      : Runnable("places::InsertVisitedURIs"),
        mDBConn(aConnection),
        mPlaces(std::move(aPlaces)),
        mCallback(aCallback),
        mIgnoreErrors(aIgnoreErrors),
        mIgnoreResults(aIgnoreResults),
        mSuccessfulUpdatedCount(aInitialUpdatedCount),
        mHistory(History::GetService()) {
    MOZ_ASSERT(NS_IsMainThread(), "This should be called on the main thread");

#ifdef DEBUG
    for (nsTArray<VisitData>::size_type i = 0; i < mPlaces.Length(); i++) {
      nsCOMPtr<nsIURI> uri;
      MOZ_ALWAYS_SUCCEEDS((NS_NewURI(getter_AddRefs(uri), mPlaces[i].spec)));
      MOZ_ASSERT(CanAddURI(uri),
                 "Passed a VisitData with a URI we cannot add to history!");
    }
#endif
  }

  /**
   * Inserts or updates the entry in moz_places for this visit, adds the visit,
   * and updates the frecency of the place.
   *
   * @param {boolean} aKnown
   *        True if we already have an entry for this place in moz_places, false
   *        otherwise.
   * @param {VisitData} aPlace
   *        The place we are adding a visit for.
   */
  nsresult DoDatabaseInserts(bool aKnown, VisitData& aPlace) {
    MOZ_ASSERT(!NS_IsMainThread(),
               "This should not be called on the main thread");

    // If the page was in moz_places, we need to update the entry.
    nsresult rv;
    if (aKnown) {
      rv = mHistory->UpdatePlace(aPlace);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    // Otherwise, the page was not in moz_places, so now we have to add it.
    else {
      rv = mHistory->InsertPlace(aPlace);
      NS_ENSURE_SUCCESS(rv, rv);
      aPlace.placeId = nsNavHistory::sLastInsertedPlaceId;
    }
    MOZ_ASSERT(aPlace.placeId > 0);

    rv = AddVisit(aPlace);
    NS_ENSURE_SUCCESS(rv, rv);

    // Adding a visit sets the recalculation flags through a trigger, but for
    // error pages we don't want that, because recalculation considers this a
    // normal successful visit. In the future we should store the error state
    // along with the visit, so that recalculation can do a better job and
    // we can remove this workaround update. See Bug 1842008.
    if (aPlace.isUnrecoverableError) {
      nsCOMPtr<mozIStorageStatement> stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET recalc_frecency = 0, recalc_alt_frecency = 0 "
          "WHERE id = :page_id");
      NS_ENSURE_STATE(stmt);
      mozStorageStatementScoper scoper(stmt);
      rv = stmt->BindInt64ByName("page_id"_ns, aPlace.placeId);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  /**
   * Fetches information about a referrer for aPlace if it was a recent
   * visit or not.
   *
   * @param aPlace
   *        The VisitData for the visit we will eventually add.
   *
   */
  void FetchReferrerInfo(VisitData& aPlace) {
    if (aPlace.referrerSpec.IsEmpty()) {
      return;
    }

    VisitData referrer;
    referrer.spec = aPlace.referrerSpec;
    // If the referrer is the same as the page, we don't need to fetch it.
    if (aPlace.referrerSpec.Equals(aPlace.spec)) {
      referrer = aPlace;
      // The page last visit id is also the referrer visit id.
      aPlace.referrerVisitId = aPlace.lastVisitId;
    } else {
      bool exists = false;
      if (NS_SUCCEEDED(mHistory->FetchPageInfo(referrer, &exists)) && exists) {
        // Copy the referrer last visit id.
        aPlace.referrerVisitId = referrer.lastVisitId;
      }
    }

    // Check if the page has effectively been visited recently, otherwise
    // discard the referrer info.
    if (!aPlace.referrerVisitId || !referrer.lastVisitTime ||
        aPlace.visitTime - referrer.lastVisitTime > RECENT_EVENT_THRESHOLD) {
      // We will not be using the referrer data.
      aPlace.referrerSpec.Truncate();
      aPlace.referrerVisitId = 0;
    }
  }

  /**
   * Adds a visit for _place and updates it with the right visit id.
   *
   * @param _place
   *        The VisitData for the place we need to know visit information about.
   */
  nsresult AddVisit(VisitData& _place) {
    MOZ_ASSERT(_place.placeId > 0);

    nsresult rv;
    nsCOMPtr<mozIStorageStatement> stmt;
    stmt = mHistory->GetStatement(
        "INSERT INTO moz_historyvisits "
        "(from_visit, place_id, visit_date, visit_type, session, source, "
        "triggeringPlaceId, userContextId) "
        "VALUES (:from_visit, :page_id, :visit_date, :visit_type, 0, :source, "
        ":triggeringPlaceId, :userContextId) ");
    NS_ENSURE_STATE(stmt);
    mozStorageStatementScoper scoper(stmt);

    rv = stmt->BindInt64ByName("page_id"_ns, _place.placeId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt64ByName("from_visit"_ns, _place.referrerVisitId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt64ByName("visit_date"_ns, _place.visitTime);
    NS_ENSURE_SUCCESS(rv, rv);
    uint32_t transitionType = _place.transitionType;
    MOZ_ASSERT(transitionType >= nsINavHistoryService::TRANSITION_LINK &&
                   transitionType <= nsINavHistoryService::TRANSITION_RELOAD,
               "Invalid transition type!");
    rv = stmt->BindInt32ByName("visit_type"_ns, (int32_t)transitionType);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt32ByName("source"_ns, _place.source);
    NS_ENSURE_SUCCESS(rv, rv);
    if (_place.triggeringPlaceId != 0) {
      rv = stmt->BindInt64ByName("triggeringPlaceId"_ns,
                                 _place.triggeringPlaceId);
    } else {
      rv = stmt->BindNullByName("triggeringPlaceId"_ns);
    }
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt64ByName("userContextId"_ns, _place.userContextId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = stmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    _place.visitId = nsNavHistory::sLastInsertedVisitId;
    MOZ_ASSERT(_place.visitId > 0);

    return NS_OK;
  }

  /**
   * Updates the frecency, and possibly the hidden-ness of aPlace.
   *
   * @param aPlace
   *        The VisitData for the place we want to update.
   */
  nsresult UpdateFrecency(const int64_t aPlaceId, bool aIsRedirect) {
    nsresult rv;
    {  // First, set our frecency to the proper value.
      nsCOMPtr<mozIStorageStatement> stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET frecency = CALCULATE_FRECENCY(:page_id, :redirect) "
          "WHERE id = :page_id");
      NS_ENSURE_STATE(stmt);
      mozStorageStatementScoper scoper(stmt);

      rv = stmt->BindInt64ByName("page_id"_ns, aPlaceId);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->BindInt32ByName("redirect"_ns, aIsRedirect);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (StaticPrefs::
            places_frecency_pages_alternative_featureGate_AtStartup()) {
      nsCOMPtr<mozIStorageStatement> stmt = mHistory->GetStatement(
          "UPDATE moz_places "
          "SET alt_frecency = CALCULATE_ALT_FRECENCY(id, :redirect), "
          "recalc_alt_frecency = 0 "
          "WHERE id = :page_id");
      NS_ENSURE_STATE(stmt);
      mozStorageStatementScoper scoper(stmt);
      rv = stmt->BindInt64ByName("page_id"_ns, aPlaceId);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->BindInt32ByName("redirect"_ns, aIsRedirect);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return NS_OK;
  }

  nsresult UpdateVisitSource(VisitData& aPlace, History* aHistory) {
    if (aPlace.bookmarked) {
      aPlace.source = nsINavHistoryService::VISIT_SOURCE_BOOKMARKED;
    } else if (!aPlace.triggeringSearchEngine.IsEmpty()) {
      aPlace.source = nsINavHistoryService::VISIT_SOURCE_SEARCHED;
    } else {
      aPlace.source = nsINavHistoryService::VISIT_SOURCE_ORGANIC;
    }

    if (aPlace.triggeringSponsoredURL.IsEmpty()) {
      // No triggeringSponsoredURL.
      return NS_OK;
    }

    if ((aPlace.visitTime -
         aPlace.triggeringSponsoredURLVisitTimeMS * PR_USEC_PER_MSEC) >
        StaticPrefs::browser_places_sponsoredSession_timeoutSecs() *
            PR_USEC_PER_SEC) {
      // Sponsored session timeout.
      return NS_OK;
    }

    if (aPlace.spec.Equals(aPlace.triggeringSponsoredURL)) {
      // This place is the triggeringSponsoredURL.
      aPlace.source = nsINavHistoryService::VISIT_SOURCE_SPONSORED;
      return NS_OK;
    }

    if (!aPlace.baseDomain.Equals(aPlace.triggeringSponsoredURLBaseDomain)) {
      // The base domain is not same.
      return NS_OK;
    }

    nsCOMPtr<mozIStorageStatement> stmt;
    stmt = aHistory->GetStatement(
        "SELECT id FROM moz_places h "
        "WHERE url_hash = hash(:url) AND url = :url");
    NS_ENSURE_STATE(stmt);
    nsresult rv =
        URIBinder::Bind(stmt, "url"_ns, aPlace.triggeringSponsoredURL);
    NS_ENSURE_SUCCESS(rv, rv);

    mozStorageStatementScoper scoper(stmt);

    bool exists;
    rv = stmt->ExecuteStep(&exists);
    NS_ENSURE_SUCCESS(rv, rv);

    if (exists) {
      rv = stmt->GetInt64(0, &aPlace.triggeringPlaceId);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      glean::places::sponsored_visit_no_triggering_url.Add(1);
    }

    aPlace.source = nsINavHistoryService::VISIT_SOURCE_SPONSORED;

    return NS_OK;
  }

  mozIStorageConnection* mDBConn;

  nsTArray<VisitData> mPlaces;

  nsMainThreadPtrHandle<mozIVisitInfoCallback> mCallback;

  bool mIgnoreErrors;

  bool mIgnoreResults;

  uint32_t mSuccessfulUpdatedCount;

  /**
   * Strong reference to the History object because we do not want it to
   * disappear out from under us.
   */
  RefPtr<History> mHistory;
};

/**
 * Sets the page title for a page in moz_places (if necessary).
 */
class SetPageTitle : public Runnable {
 public:
  /**
   * Sets a pages title in the database asynchronously.
   *
   * @param aConnection
   *        The database connection to use for this operation.
   * @param aURI
   *        The URI to set the page title on.
   * @param aTitle
   *        The title to set for the page, if the page exists.
   */
  static nsresult Start(mozIStorageConnection* aConnection, nsIURI* aURI,
                        const nsAString& aTitle) {
    MOZ_ASSERT(NS_IsMainThread(), "This should be called on the main thread");
    MOZ_ASSERT(aURI, "Must pass a non-null URI object!");

    nsCString spec;
    nsresult rv = aURI->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);

    RefPtr<SetPageTitle> event = new SetPageTitle(spec, aTitle);

    // Get the target thread, and then start the work!
    nsCOMPtr<nsIEventTarget> target = do_GetInterface(aConnection);
    NS_ENSURE_TRUE(target, NS_ERROR_UNEXPECTED);
    rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(!NS_IsMainThread(),
               "This should not be called on the main thread");

    // First, see if the page exists in the database (we'll need its id later).
    bool exists;
    nsresult rv = mHistory->FetchPageInfo(mPlace, &exists);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!exists || !mPlace.titleChanged) {
      // We have no record of this page, or we have no title change, so there
      // is no need to do any further work.
      return NS_OK;
    }

    MOZ_ASSERT(mPlace.placeId > 0, "We somehow have an invalid place id here!");

    // Now we can update our database record.
    nsCOMPtr<mozIStorageStatement> stmt = mHistory->GetStatement(
        "UPDATE moz_places "
        "SET title = :page_title "
        "WHERE id = :page_id ");
    NS_ENSURE_STATE(stmt);

    {
      mozStorageStatementScoper scoper(stmt);
      rv = stmt->BindInt64ByName("page_id"_ns, mPlace.placeId);
      NS_ENSURE_SUCCESS(rv, rv);
      // Empty strings should clear the title, just like
      // nsNavHistory::SetPageTitle.
      if (mPlace.title.IsEmpty()) {
        rv = stmt->BindNullByName("page_title"_ns);
      } else {
        rv = stmt->BindStringByName("page_title"_ns,
                                    StringHead(mPlace.title, TITLE_LENGTH_MAX));
      }
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    nsCOMPtr<nsIRunnable> event =
        new NotifyTitleObservers(mPlace.spec, mPlace.title, mPlace.guid);
    rv = NS_DispatchToMainThread(event);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

 private:
  SetPageTitle(const nsCString& aSpec, const nsAString& aTitle)
      : Runnable("places::SetPageTitle"), mHistory(History::GetService()) {
    mPlace.spec = aSpec;
    mPlace.title = aTitle;
  }

  VisitData mPlace;

  /**
   * Strong reference to the History object because we do not want it to
   * disappear out from under us.
   */
  RefPtr<History> mHistory;
};

/**
 * Stores an embed visit, and notifies observers.
 *
 * @param aPlace
 *        The VisitData of the visit to store as an embed visit.
 * @param [optional] aCallback
 *        The mozIVisitInfoCallback to notify, if provided.
 *
 * FIXME(emilio, bug 1595484): We should get rid of EMBED visits completely.
 */
void NotifyEmbedVisit(VisitData& aPlace,
                      mozIVisitInfoCallback* aCallback = nullptr) {
  MOZ_ASSERT(aPlace.transitionType == nsINavHistoryService::TRANSITION_EMBED,
             "Must only pass TRANSITION_EMBED visits to this!");
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread!");

  nsCOMPtr<nsIURI> uri;
  if (NS_WARN_IF(NS_FAILED(NS_NewURI(getter_AddRefs(uri), aPlace.spec)))) {
    return;
  }

  if (!!aCallback) {
    nsMainThreadPtrHandle<mozIVisitInfoCallback> callback(
        new nsMainThreadPtrHolder<mozIVisitInfoCallback>(
            "mozIVisitInfoCallback", aCallback));
    bool ignoreResults = false;
    (void)aCallback->GetIgnoreResults(&ignoreResults);
    if (!ignoreResults) {
      nsCOMPtr<nsIRunnable> event =
          new NotifyPlaceInfoCallback(callback, aPlace, true, NS_OK);
      (void)NS_DispatchToMainThread(event);
    }
  }

  nsCOMPtr<nsIRunnable> event = new NotifyManyVisitsObservers(aPlace);
  (void)NS_DispatchToMainThread(event);
}

void NotifyOriginRestrictedVisit(nsIURI* aURI) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread!");

  nsCOMPtr<nsIRunnable> event = new NotifyManyVisitsObservers(VisitData(aURI));
  (void)NS_DispatchToMainThread(event);
}

void NotifyVisitIfHavingUserPass(nsIURI* aURI) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread!");

  bool hasUserPass;
  if (NS_SUCCEEDED(aURI->GetHasUserPass(&hasUserPass)) && hasUserPass) {
    nsCOMPtr<nsIRunnable> event =
        new NotifyManyVisitsObservers(VisitData(aURI));
    (void)NS_DispatchToMainThread(event);
  }
}

////////////////////////////////////////////////////////////////////////////////
//// History

History* History::gService = nullptr;

History::History()
    : mShuttingDown(false),
      mShuttingDownMutex("History::mShuttingDownMutex"),
      mBlockShutdownMutex("History::mBlockShutdownMutex"),
      mRecentlyVisitedURIs(RECENTLY_VISITED_URIS_SIZE) {
  NS_ASSERTION(!gService, "Ruh-roh!  This service has already been created!");
  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsIProperties> dirsvc = components::Directory::Service();
    bool haveProfile = false;
    MOZ_RELEASE_ASSERT(
        dirsvc &&
            NS_SUCCEEDED(
                dirsvc->Has(NS_APP_USER_PROFILE_50_DIR, &haveProfile)) &&
            haveProfile,
        "Can't construct history service if there is no profile.");
  }
  gService = this;

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  NS_WARNING_ASSERTION(os, "Observer service was not found!");
  if (os) {
    (void)os->AddObserver(this, TOPIC_PLACES_SHUTDOWN, false);
  }

  if (XRE_IsParentProcess()) {
    mHistoryClearedListener = new HistoryClearedListener();
  }
}

History::~History() {
  UnregisterWeakMemoryReporter(this);

  MOZ_ASSERT(gService == this);
  gService = nullptr;
}

void History::InitMemoryReporter() { RegisterWeakMemoryReporter(this); }

nsresult History::QueueVisitedStatement(RefPtr<VisitedQuery>&& aQuery) {
  MOZ_ASSERT(NS_IsMainThread());
  if (IsShuttingDown()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  auto conn = ConcurrentConnection::GetInstance();
  if (conn.isSome()) {
    conn.value()->Queue(
        "WITH urls (url, url_hash) AS ( "
        "  SELECT value, hash(value) FROM carray(?1) "
        ") "
        "SELECT url, last_visit_date NOTNULL "
        "FROM moz_places "
        "JOIN urls USING(url_hash, url) "_ns,
        aQuery);
  }
  return NS_OK;
}

nsresult History::InsertPlace(VisitData& aPlace) {
  MOZ_ASSERT(aPlace.placeId == 0, "should not have a valid place id!");
  MOZ_ASSERT(!NS_IsMainThread(), "must be called off of the main thread!");

  nsCOMPtr<mozIStorageStatement> stmt = GetStatement(
      "INSERT INTO moz_places "
      "(url, url_hash, title, rev_host, hidden, typed, frecency, guid) "
      "VALUES (:url, hash(:url), :title, :rev_host, :hidden, :typed, "
      ":frecency, :guid) ");
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindStringByName("rev_host"_ns, aPlace.revHost);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = URIBinder::Bind(stmt, "url"_ns, aPlace.spec);
  NS_ENSURE_SUCCESS(rv, rv);
  nsString title = aPlace.title;
  // Empty strings should have no title, just like nsNavHistory::SetPageTitle.
  if (title.IsEmpty()) {
    rv = stmt->BindNullByName("title"_ns);
  } else {
    title.Assign(StringHead(aPlace.title, TITLE_LENGTH_MAX));
    rv = stmt->BindStringByName("title"_ns, title);
  }
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName("typed"_ns, aPlace.typed);
  NS_ENSURE_SUCCESS(rv, rv);
  // When inserting a page for a first visit that should not appear in
  // autocomplete, for example an error page, use a zero frecency.
  int32_t frecency = aPlace.isUnrecoverableError ? 0 : aPlace.frecency;
  rv = stmt->BindInt32ByName("frecency"_ns, frecency);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName("hidden"_ns, aPlace.hidden);
  NS_ENSURE_SUCCESS(rv, rv);
  if (aPlace.guid.IsVoid()) {
    rv = GenerateGUID(aPlace.guid);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = stmt->BindUTF8StringByName("guid"_ns, aPlace.guid);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult History::UpdatePlace(const VisitData& aPlace) {
  MOZ_ASSERT(!NS_IsMainThread(), "must be called off of the main thread!");
  MOZ_ASSERT(aPlace.placeId > 0, "must have a valid place id!");
  MOZ_ASSERT(!aPlace.guid.IsVoid(), "must have a guid!");

  nsCOMPtr<mozIStorageStatement> stmt;
  bool titleIsVoid = aPlace.title.IsVoid();
  if (titleIsVoid) {
    // Don't change the title.
    stmt = GetStatement(
        "UPDATE moz_places "
        "SET hidden = :hidden, "
        "typed = :typed, "
        "guid = :guid "
        "WHERE id = :page_id ");
  } else {
    stmt = GetStatement(
        "UPDATE moz_places "
        "SET title = :title, "
        "hidden = :hidden, "
        "typed = :typed, "
        "guid = :guid "
        "WHERE id = :page_id ");
  }
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv;
  if (!titleIsVoid) {
    // An empty string clears the title.
    if (aPlace.title.IsEmpty()) {
      rv = stmt->BindNullByName("title"_ns);
    } else {
      rv = stmt->BindStringByName("title"_ns,
                                  StringHead(aPlace.title, TITLE_LENGTH_MAX));
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = stmt->BindInt32ByName("typed"_ns, aPlace.typed);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt32ByName("hidden"_ns, aPlace.hidden);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindUTF8StringByName("guid"_ns, aPlace.guid);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName("page_id"_ns, aPlace.placeId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult History::FetchPageInfo(VisitData& _place, bool* _exists) {
  MOZ_ASSERT(!_place.spec.IsEmpty() || !_place.guid.IsEmpty(),
             "must have either a non-empty spec or guid!");
  MOZ_ASSERT(!NS_IsMainThread(), "must be called off of the main thread!");

  nsresult rv;

  // URI takes precedence.
  nsCOMPtr<mozIStorageStatement> stmt;
  bool selectByURI = !_place.spec.IsEmpty();
  if (selectByURI) {
    stmt = GetStatement(
        "SELECT guid, id, title, hidden, typed, frecency, visit_count, "
        "last_visit_date, "
        "(SELECT id FROM moz_historyvisits "
        "WHERE place_id = h.id AND visit_date = h.last_visit_date) AS "
        "last_visit_id, "
        "EXISTS(SELECT 1 FROM moz_bookmarks WHERE fk = h.id) AS bookmarked "
        "FROM moz_places h "
        "WHERE url_hash = hash(:page_url) AND url = :page_url ");
    NS_ENSURE_STATE(stmt);

    rv = URIBinder::Bind(stmt, "page_url"_ns, _place.spec);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    stmt = GetStatement(
        "SELECT url, id, title, hidden, typed, frecency, visit_count, "
        "last_visit_date, "
        "(SELECT id FROM moz_historyvisits "
        "WHERE place_id = h.id AND visit_date = h.last_visit_date) AS "
        "last_visit_id, "
        "EXISTS(SELECT 1 FROM moz_bookmarks WHERE fk = h.id) AS bookmarked "
        "FROM moz_places h "
        "WHERE guid = :guid ");
    NS_ENSURE_STATE(stmt);

    rv = stmt->BindUTF8StringByName("guid"_ns, _place.guid);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mozStorageStatementScoper scoper(stmt);

  rv = stmt->ExecuteStep(_exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!*_exists) {
    return NS_OK;
  }

  if (selectByURI) {
    if (_place.guid.IsEmpty()) {
      rv = stmt->GetUTF8String(0, _place.guid);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    nsAutoCString spec;
    rv = stmt->GetUTF8String(0, spec);
    NS_ENSURE_SUCCESS(rv, rv);
    _place.spec = std::move(spec);
  }

  rv = stmt->GetInt64(1, &_place.placeId);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString title;
  rv = stmt->GetString(2, title);
  NS_ENSURE_SUCCESS(rv, rv);

  // If the title we were given was void, that means we did not bother to set
  // it to anything.  As a result, ignore the fact that we may have changed the
  // title (because we don't want to, that would be empty), and set the title
  // to what is currently stored in the datbase.
  if (_place.title.IsVoid()) {
    _place.title = std::move(title);
  }
  // Otherwise, just indicate if the title has changed.
  else {
    _place.titleChanged = !(_place.title.Equals(title)) &&
                          !(_place.title.IsEmpty() && title.IsVoid());
  }

  int32_t hidden;
  rv = stmt->GetInt32(3, &hidden);
  NS_ENSURE_SUCCESS(rv, rv);
  _place.hidden = !!hidden;

  int32_t typed;
  rv = stmt->GetInt32(4, &typed);
  NS_ENSURE_SUCCESS(rv, rv);
  _place.typed = !!typed;

  rv = stmt->GetInt32(5, &_place.frecency);
  NS_ENSURE_SUCCESS(rv, rv);
  int32_t visitCount;
  rv = stmt->GetInt32(6, &visitCount);
  NS_ENSURE_SUCCESS(rv, rv);
  _place.visitCount = visitCount;
  rv = stmt->GetInt64(7, &_place.lastVisitTime);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->GetInt64(8, &_place.lastVisitId);
  NS_ENSURE_SUCCESS(rv, rv);
  int32_t bookmarked;
  rv = stmt->GetInt32(9, &bookmarked);
  NS_ENSURE_SUCCESS(rv, rv);
  _place.bookmarked = bookmarked == 1;

  return NS_OK;
}

MOZ_DEFINE_MALLOC_SIZE_OF(HistoryMallocSizeOf)

NS_IMETHODIMP
History::CollectReports(nsIHandleReportCallback* aHandleReport,
                        nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT(
      "explicit/history-links-hashtable", KIND_HEAP, UNITS_BYTES,
      SizeOfIncludingThis(HistoryMallocSizeOf),
      "Memory used by the hashtable that records changes to the visited state "
      "of links.");

  return NS_OK;
}

size_t History::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  size_t size = aMallocSizeOf(this);
  size += mTrackedURIs.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mTrackedURIs.Values()) {
    size += entry.SizeOfExcludingThis(aMallocSizeOf);
  }
  return size;
}

/* static */
History* History::GetService() {
  if (gService) {
    return gService;
  }

  nsCOMPtr<IHistory> service = components::History::Service();
  if (service) {
    NS_ASSERTION(gService, "Our constructor was not run?!");
  }

  return gService;
}

/* static */
already_AddRefed<History> History::GetSingleton() {
  if (!gService) {
    RefPtr<History> svc = new History();
    MOZ_ASSERT(gService == svc.get());
    svc->InitMemoryReporter();
    return svc.forget();
  }

  return do_AddRef(gService);
}

mozIStorageConnection* History::GetDBConn() {
  MOZ_ASSERT(NS_IsMainThread());
  if (IsShuttingDown()) {
    return nullptr;
  }
  if (!mDB) {
    mDB = Database::GetDatabase();
    NS_ENSURE_TRUE(mDB, nullptr);
    // This must happen on the main-thread, so when we try to use the connection
    // later it's initialized.
    mDB->EnsureConnection();
    NS_ENSURE_TRUE(mDB, nullptr);
  }
  return mDB->MainConn();
}

const mozIStorageConnection* History::GetConstDBConn() {
  MOZ_ASSERT(!NS_IsMainThread());
  {
    MOZ_ASSERT(mDB || IsShuttingDown());
    if (IsShuttingDown() || !mDB) {
      return nullptr;
    }
  }
  return mDB->MainConn();
}

void History::UpdateOriginFloodingRestriction(nsACString& aOrigin) {
  MOZ_ASSERT(NS_IsMainThread());

  TimeStamp latestInputTimeStamp = UserActivation::LatestUserInputStart();
  if (latestInputTimeStamp.IsNull()) {
    // Probably just after browser initialization.
    return;
  }

  TimeStamp now = TimeStamp::Now();

  // Remove expired flooding resrictions.
  for (auto iter = mOriginFloodingRestrictions.Iter(); !iter.Done();
       iter.Next()) {
    if ((now - iter.Data().mLastVisitTimeStamp).ToSeconds() >
        iter.Data().mExpireIntervalSeconds) {
      iter.Remove();
    }
  }

  // If not long enough time elapsed sincer last user interaction, this visit
  // can be stored.
  if ((now - latestInputTimeStamp).ToSeconds() <
      StaticPrefs::
          places_history_floodingPrevention_maxSecondsFromLastUserInteraction()) {
    mOriginFloodingRestrictions.Remove(aOrigin);
    return;
  }

  // Otherwise, update the flooding restriction for the origin.
  auto restriction = mOriginFloodingRestrictions.Lookup(aOrigin);
  if (restriction) {
    if (restriction->mAllowedVisitCount) {
      // Count down to 0. If 0, the origin should be restricted.
      restriction->mAllowedVisitCount -= 1;
    } else {
      // Since the origin is marked as restricted make its expiration time
      // longer.
      restriction->mExpireIntervalSeconds *= 2;
    }
  } else {
    // Initialize OriginFloodingRestriction to store the origin restriction.
    mOriginFloodingRestrictions.InsertOrUpdate(
        aOrigin,
        OriginFloodingRestriction{
            now,
            StaticPrefs::
                places_history_floodingPrevention_restrictionExpireSeconds(),
            StaticPrefs::places_history_floodingPrevention_restrictionCount()});
  }
}

bool History::IsRestrictedOrigin(nsACString& aOrigin) {
  auto restriction = mOriginFloodingRestrictions.Lookup(aOrigin);
  // If the count is 0, need to restrict the origin.
  return restriction && !restriction->mAllowedVisitCount;
}

void History::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lockedScope(mBlockShutdownMutex);
  {
    MutexAutoLock lockedScope(mShuttingDownMutex);
    MOZ_ASSERT(!mShuttingDown && "Shutdown was called more than once!");
    mShuttingDown = true;
  }
}

void History::AppendToRecentlyVisitedURIs(nsIURI* aURI, bool aHidden) {
  PRTime now = PR_Now();

  mRecentlyVisitedURIs.InsertOrUpdate(aURI, RecentURIVisit{now, aHidden});

  // Remove entries older than RECENTLY_VISITED_URIS_MAX_AGE.
  for (auto iter = mRecentlyVisitedURIs.Iter(); !iter.Done(); iter.Next()) {
    if ((now - iter.Data().mTime) > RECENTLY_VISITED_URIS_MAX_AGE) {
      iter.Remove();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
//// IHistory

NS_IMETHODIMP
History::VisitURI(nsIWidget* aWidget, nsIURI* aURI, nsIURI* aLastVisitedURI,
                  uint32_t aFlags, uint64_t aBrowserId,
                  uint32_t aUserContextId) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aURI);

  if (IsShuttingDown()) {
    return NS_OK;
  }

  if (XRE_IsContentProcess()) {
    if (!BaseHistory::CanStore(aURI)) {
      return NS_OK;
    }

    NS_ENSURE_ARG(aWidget);
    BrowserChild* browserChild = aWidget->GetOwningBrowserChild();
    NS_ENSURE_TRUE(browserChild, NS_ERROR_FAILURE);
    (void)browserChild->SendVisitURI(aURI, aLastVisitedURI, aFlags, aBrowserId);
    return NS_OK;
  }

  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(navHistory, NS_ERROR_OUT_OF_MEMORY);

  // Silently return if URI is something we shouldn't add to DB.
  bool canAdd;
  nsresult rv = navHistory->CanAddURI(aURI, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    return NS_OK;
  }

  bool reload = false;
  if (aLastVisitedURI) {
    rv = aURI->Equals(aLastVisitedURI, &reload);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Assigns a type to the edge in the visit linked list. Each type will be
  // considered differently when weighting the frecency of a location.
  uint32_t recentFlags = navHistory->GetRecentFlags(aURI);
  bool isFollowedLink = recentFlags & nsNavHistory::RECENT_ACTIVATED;

  // Embed visits should never be added to the database, and the same is valid
  // for redirects across frames.
  // For the above reasoning non-toplevel transitions are handled at first.
  // if the visit is toplevel or a non-toplevel followed link, then it can be
  // handled as usual and stored on disk.

  uint32_t transitionType = nsINavHistoryService::TRANSITION_LINK;

  if (!(aFlags & IHistory::TOP_LEVEL) && !isFollowedLink) {
    // A frame redirected to a new site without user interaction.
    transitionType = nsINavHistoryService::TRANSITION_EMBED;
  } else if (aFlags & IHistory::REDIRECT_TEMPORARY) {
    transitionType = nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY;
  } else if (aFlags & IHistory::REDIRECT_PERMANENT) {
    transitionType = nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT;
  } else if (reload) {
    transitionType = nsINavHistoryService::TRANSITION_RELOAD;
  } else if ((recentFlags & nsNavHistory::RECENT_TYPED) &&
             !(aFlags & IHistory::UNRECOVERABLE_ERROR)) {
    // Don't mark error pages as typed, even if they were actually typed by
    // the user.  This is useful to limit their score in autocomplete.
    transitionType = nsINavHistoryService::TRANSITION_TYPED;
  } else if (recentFlags & nsNavHistory::RECENT_BOOKMARKED) {
    transitionType = nsINavHistoryService::TRANSITION_BOOKMARK;
  } else if (!(aFlags & IHistory::TOP_LEVEL) && isFollowedLink) {
    // User activated a link in a frame.
    transitionType = nsINavHistoryService::TRANSITION_FRAMED_LINK;
  }

  bool isRedirect = aFlags & IHistory::REDIRECT_SOURCE;
  bool isHidden = GetHiddenState(isRedirect, transitionType);

  // Do not save a reloaded uri if we have visited the same URI recently.
  if (reload) {
    auto entry = mRecentlyVisitedURIs.Lookup(aURI);
    // Check if the entry exists and is younger than
    // RECENTLY_VISITED_URIS_MAX_AGE.
    if (entry && (PR_Now() - entry->mTime) < RECENTLY_VISITED_URIS_MAX_AGE) {
      bool wasHidden = entry->mHidden;
      // Regardless of whether we store the visit or not, we must update the
      // stored visit time.
      AppendToRecentlyVisitedURIs(aURI, isHidden);
      // We always want to store an unhidden visit, if the previous visits were
      // hidden, because otherwise the page may not appear in the history UI.
      // This can happen for example at a page redirecting to itself.
      if (!wasHidden || isHidden) {
        // We can skip this visit.
        return NS_OK;
      }
    }
  }

  // Never store the URL having userpass to database.
  nsCOMPtr<nsIURI> visitedURI = GetExposableURI(aURI);
  nsCOMPtr<nsIURI> lastVisitedURI;
  if (aLastVisitedURI) {
    lastVisitedURI = GetExposableURI(aLastVisitedURI);
  }

  nsTArray<VisitData> placeArray(1);
  placeArray.AppendElement(VisitData(visitedURI, lastVisitedURI));
  VisitData& place = placeArray.ElementAt(0);
  NS_ENSURE_FALSE(place.spec.IsEmpty(), NS_ERROR_INVALID_ARG);

  place.visitTime = PR_Now();
  place.SetTransitionType(transitionType);
  place.hidden = isHidden;
  place.userContextId = aUserContextId;

  if (isRedirect) {
    place.useFrecencyRedirectBonus =
        (aFlags & (IHistory::REDIRECT_SOURCE_PERMANENT |
                   IHistory::REDIRECT_SOURCE_UPGRADED)) ||
        transitionType != nsINavHistoryService::TRANSITION_TYPED;
  }

  // Error pages should never be autocompleted.
  place.isUnrecoverableError = aFlags & IHistory::UNRECOVERABLE_ERROR;

  // EMBED visits should not go through the database.
  // They exist only to keep track of isVisited status during the session.
  if (place.transitionType == nsINavHistoryService::TRANSITION_EMBED) {
    NotifyEmbedVisit(place);
    return NS_OK;
  }

  if (StaticPrefs::places_history_floodingPrevention_enabled()) {
    // If the origin is restricted, make isVisited status available during the
    // session but not stored in the database.
    nsAutoCString origin;
    (void)visitedURI->GetHost(origin);
    if (StringBeginsWith(origin, "www."_ns)) {
      origin.Cut(0, 4);
    }

    UpdateOriginFloodingRestriction(origin);

    if (IsRestrictedOrigin(origin)) {
      NotifyOriginRestrictedVisit(visitedURI);
      NotifyVisitIfHavingUserPass(aURI);
      return NS_OK;
    }
  }

  nsCOMPtr<nsIBrowserWindowTracker> bwt =
      do_ImportESModule("resource:///modules/BrowserWindowTracker.sys.mjs",
                        "BrowserWindowTracker", &rv);
  if (NS_SUCCEEDED(rv)) {
    // Only if it is running on Firefox, continue to process the followings.
    nsCOMPtr<nsISupports> browser;
    rv = bwt->GetBrowserById(aBrowserId, getter_AddRefs(browser));
    NS_ENSURE_SUCCESS(rv, rv);
    if (browser) {
      RefPtr<Element> browserElement = static_cast<Element*>(browser.get());

      nsAutoString triggeringSearchEngineURL;
      browserElement->GetAttribute(u"triggeringSearchEngineURL"_ns,
                                   triggeringSearchEngineURL);
      if (!triggeringSearchEngineURL.IsEmpty() &&
          place.spec.Equals(NS_ConvertUTF16toUTF8(triggeringSearchEngineURL))) {
        nsAutoString triggeringSearchEngine;
        browserElement->GetAttribute(u"triggeringSearchEngine"_ns,
                                     triggeringSearchEngine);
        place.triggeringSearchEngine.Assign(
            NS_ConvertUTF16toUTF8(triggeringSearchEngine));
      }

      nsAutoString triggeringSponsoredURL;
      browserElement->GetAttribute(u"triggeringSponsoredURL"_ns,
                                   triggeringSponsoredURL);
      if (!triggeringSponsoredURL.IsEmpty()) {
        place.triggeringSponsoredURL.Assign(
            NS_ConvertUTF16toUTF8(triggeringSponsoredURL));

        nsAutoString triggeringSponsoredURLVisitTimeMS;
        browserElement->GetAttribute(u"triggeringSponsoredURLVisitTimeMS"_ns,
                                     triggeringSponsoredURLVisitTimeMS);
        place.triggeringSponsoredURLVisitTimeMS =
            triggeringSponsoredURLVisitTimeMS.ToInteger64(&rv);
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<nsIURI> sponsoredURL;
        rv = NS_NewURI(getter_AddRefs(sponsoredURL),
                       place.triggeringSponsoredURL);
        bool sameDomain = false;
        if (!NS_WARN_IF(NS_FAILED(rv))) {
          // Get base domains here since nsIEffectiveTLDService referred in
          // DomainNameFromURI should access on main thread.
          nsCString visitedBaseDomain, sponsoredBaseDomain;
          navHistory->DomainNameFromURI(visitedURI, visitedBaseDomain);
          navHistory->DomainNameFromURI(sponsoredURL, sponsoredBaseDomain);
          sameDomain = visitedBaseDomain.Equals(sponsoredBaseDomain);
        }
        if (!sameDomain) {
          place.triggeringSponsoredURL.Truncate();
        }
      }
    }
  }

  mozIStorageConnection* dbConn = GetDBConn();
  NS_ENSURE_STATE(dbConn);

  rv = InsertVisitedURIs::Start(dbConn, std::move(placeArray));
  NS_ENSURE_SUCCESS(rv, rv);

  // URIs with a userpass component are not stored in the database for security
  // reasons, we store the exposable URI version of them instead.
  // The original link pointing at the URI with userpass must still be marked as
  // visited, to properly react to the user interaction, so we notify a visit.
  // The visited status is not going to survive a reload of course, though the
  // alternative of marking any userpass URI as visited if the exposable URI is
  // visited also feels wrong from the user point of view.
  NotifyVisitIfHavingUserPass(aURI);

  return NS_OK;
}

NS_IMETHODIMP
History::SetURITitle(nsIURI* aURI, const nsAString& aTitle) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aURI);

  if (IsShuttingDown()) {
    return NS_OK;
  }

  if (XRE_IsContentProcess()) {
    auto* cpc = dom::ContentChild::GetSingleton();
    MOZ_ASSERT(cpc, "Content Protocol is NULL!");
    (void)cpc->SendSetURITitle(aURI, PromiseFlatString(aTitle));
    return NS_OK;
  }

  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();

  // At first, it seems like nav history should always be available here, no
  // matter what.
  //
  // nsNavHistory fails to register as a service if there is no profile in
  // place (for instance, if user is choosing a profile).
  //
  // Maybe the correct thing to do is to not register this service if no
  // profile has been selected?
  //
  NS_ENSURE_TRUE(navHistory, NS_ERROR_FAILURE);

  nsCOMPtr<nsIURI> uri = GetExposableURI(aURI);
  bool canAdd;
  nsresult rv = navHistory->CanAddURI(uri, &canAdd);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!canAdd) {
    return NS_OK;
  }

  mozIStorageConnection* dbConn = GetDBConn();
  NS_ENSURE_STATE(dbConn);

  return SetPageTitle::Start(dbConn, uri, aTitle);
}

////////////////////////////////////////////////////////////////////////////////
//// mozIAsyncHistory

NS_IMETHODIMP
History::UpdatePlaces(JS::Handle<JS::Value> aPlaceInfos,
                      mozIVisitInfoCallback* aCallback, JSContext* aCtx) {
  NS_ENSURE_TRUE(NS_IsMainThread(), NS_ERROR_UNEXPECTED);
  NS_ENSURE_TRUE(!aPlaceInfos.isPrimitive(), NS_ERROR_INVALID_ARG);

  uint32_t infosLength;
  JS::Rooted<JSObject*> infos(aCtx);
  nsresult rv = GetJSArrayFromJSValue(aPlaceInfos, aCtx, &infos, &infosLength);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t initialUpdatedCount = 0;

  nsTArray<VisitData> visitData;
  for (uint32_t i = 0; i < infosLength; i++) {
    JS::Rooted<JSObject*> info(aCtx);
    nsresult rv = GetJSObjectFromArray(aCtx, infos, i, &info);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIURI> uri = GetURIFromJSObject(aCtx, info, "uri");
    if (uri) {
      uri = GetExposableURI(uri);
    }

    nsCString guid;
    {
      nsString fatGUID;
      GetStringFromJSObject(aCtx, info, "guid", fatGUID);
      if (fatGUID.IsVoid()) {
        guid.SetIsVoid(true);
      } else {
        CopyUTF16toUTF8(fatGUID, guid);
      }
    }

    // Make sure that any uri we are given can be added to history, and if not,
    // skip it (CanAddURI will notify our callback for us).
    if (uri && !CanAddURI(uri, guid, aCallback)) {
      continue;
    }

    // We must have at least one of uri or guid.
    NS_ENSURE_ARG(uri || !guid.IsVoid());

    // If we were given a guid, make sure it is valid.
    bool isValidGUID = IsValidGUID(guid);
    NS_ENSURE_ARG(guid.IsVoid() || isValidGUID);

    nsString title;
    GetStringFromJSObject(aCtx, info, "title", title);

    JS::Rooted<JSObject*> visits(aCtx, nullptr);
    {
      JS::Rooted<JS::Value> visitsVal(aCtx);
      bool rc = JS_GetProperty(aCtx, info, "visits", &visitsVal);
      NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
      if (!visitsVal.isPrimitive()) {
        visits = visitsVal.toObjectOrNull();
        bool isArray;
        if (!JS::IsArrayObject(aCtx, visits, &isArray)) {
          return NS_ERROR_UNEXPECTED;
        }
        if (!isArray) {
          return NS_ERROR_INVALID_ARG;
        }
      }
    }
    NS_ENSURE_ARG(visits);

    uint32_t visitsLength = 0;
    if (visits) {
      (void)JS::GetArrayLength(aCtx, visits, &visitsLength);
    }
    NS_ENSURE_ARG(visitsLength > 0);

    // Check each visit, and build our array of VisitData objects.
    visitData.SetCapacity(visitData.Length() + visitsLength);
    for (uint32_t j = 0; j < visitsLength; j++) {
      JS::Rooted<JSObject*> visit(aCtx);
      rv = GetJSObjectFromArray(aCtx, visits, j, &visit);
      NS_ENSURE_SUCCESS(rv, rv);

      VisitData& data = *visitData.AppendElement(VisitData(uri));
      if (!title.IsEmpty()) {
        data.title = title;
      } else if (!title.IsVoid()) {
        // Setting data.title to an empty string wouldn't make it non-void.
        data.title.SetIsVoid(false);
      }
      data.guid = guid;

      // We must have a date and a transaction type!
      rv = GetIntFromJSObject(aCtx, visit, "visitDate", &data.visitTime);
      NS_ENSURE_SUCCESS(rv, rv);
      // visitDate should be in microseconds. It's easy to do the wrong thing
      // and pass milliseconds to updatePlaces, so we lazily check for that.
      // While it's not easily distinguishable, since both are integers, we can
      // check if the value is very far in the past, and assume it's probably
      // a mistake.
      if (data.visitTime < (PR_Now() / 1000)) {
#ifdef DEBUG
        nsCOMPtr<nsIXPConnect> xpc = nsIXPConnect::XPConnect();
        (void)xpc->DebugDumpJSStack(false, false, false);
        MOZ_CRASH("invalid time format passed to updatePlaces");
#endif
        return NS_ERROR_INVALID_ARG;
      }
      uint32_t transitionType = 0;
      rv = GetIntFromJSObject(aCtx, visit, "transitionType", &transitionType);
      NS_ENSURE_SUCCESS(rv, rv);
      NS_ENSURE_ARG_RANGE(transitionType, nsINavHistoryService::TRANSITION_LINK,
                          nsINavHistoryService::TRANSITION_RELOAD);
      data.SetTransitionType(transitionType);
      data.hidden = GetHiddenState(false, transitionType);

      // If the visit is an embed visit, we do not actually add it to the
      // database.
      if (transitionType == nsINavHistoryService::TRANSITION_EMBED) {
        NotifyEmbedVisit(data, aCallback);
        visitData.RemoveLastElement();
        initialUpdatedCount++;
        continue;
      }

      // The referrer is optional.
      nsCOMPtr<nsIURI> referrer =
          GetURIFromJSObject(aCtx, visit, "referrerURI");
      if (referrer) {
        (void)referrer->GetSpec(data.referrerSpec);
      }

      JS::Rooted<JS::Value> userContextIdVal(aCtx);
      if (!JS_GetProperty(aCtx, visit, "userContextId", &userContextIdVal)) {
        return NS_ERROR_UNEXPECTED;
      }
      if (!userContextIdVal.isUndefined()) {
        rv = GetIntFromJSObject(aCtx, visit, "userContextId",
                                &data.userContextId);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  mozIStorageConnection* dbConn = GetDBConn();
  NS_ENSURE_STATE(dbConn);

  nsMainThreadPtrHandle<mozIVisitInfoCallback> callback(
      new nsMainThreadPtrHolder<mozIVisitInfoCallback>("mozIVisitInfoCallback",
                                                       aCallback));

  // It is possible that all of the visits we were passed were dissallowed by
  // CanAddURI, which isn't an error.  If we have no visits to add, however,
  // we should not call InsertVisitedURIs::Start.
  if (visitData.Length()) {
    nsresult rv = InsertVisitedURIs::Start(dbConn, std::move(visitData),
                                           callback, initialUpdatedCount);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (aCallback) {
    // Be sure to notify that all of our operations are complete.  This
    // is dispatched to the background thread first and redirected to the
    // main thread from there to make sure that all database notifications
    // and all embed or canAddURI notifications have finished.

    // Note: if we're inserting anything, it's the responsibility of
    // InsertVisitedURIs to call the completion callback, as here we won't
    // know how yet many items we will successfully insert/update.
    nsCOMPtr<nsIEventTarget> backgroundThread = do_GetInterface(dbConn);
    NS_ENSURE_TRUE(backgroundThread, NS_ERROR_UNEXPECTED);
    nsCOMPtr<nsIRunnable> event =
        new NotifyCompletion(callback, initialUpdatedCount);
    return backgroundThread->Dispatch(event, NS_DISPATCH_NORMAL);
  }

  return NS_OK;
}

NS_IMETHODIMP
History::IsURIVisited(nsIURI* aURI, mozIVisitedStatusCallback* aCallback) {
  NS_ENSURE_STATE(NS_IsMainThread());
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aCallback);

  return VisitedQuery::Start(aURI, aCallback);
}

NS_IMETHODIMP
History::ClearCache() {
  mRecentlyVisitedURIs.Clear();
  mOriginFloodingRestrictions.Clear();
  return NS_OK;
}

void History::StartPendingVisitedQueries(PendingVisitedQueries&& aQueries) {
  if (XRE_IsContentProcess()) {
    auto* cpc = dom::ContentChild::GetSingleton();
    MOZ_ASSERT(cpc, "Content Protocol is NULL!");

    // Fairly arbitrary limit on the number of URLs we send at a time, to avoid
    // going over the IPC message size limit... Note that this is imperfect (we
    // could have very long URIs), so this is a best-effort kind of thing. See
    // bug 1775265.
    constexpr size_t kBatchLimit = 4000;

    nsTArray<RefPtr<nsIURI>> uris(aQueries.Count());
    for (const auto& entry : aQueries) {
      uris.AppendElement(entry.GetKey());
      MOZ_ASSERT(entry.GetData().IsEmpty(),
                 "Child process shouldn't have parent requests");
      if (uris.Length() == kBatchLimit) {
        (void)cpc->SendStartVisitedQueries(uris);
        uris.ClearAndRetainStorage();
      }
    }

    if (!uris.IsEmpty()) {
      (void)cpc->SendStartVisitedQueries(uris);
    }
  } else {
    VisitedQuery::Start(std::move(aQueries));
  }
}

////////////////////////////////////////////////////////////////////////////////
//// nsIObserver

NS_IMETHODIMP
History::Observe(nsISupports* aSubject, const char* aTopic,
                 const char16_t* aData) {
  if (strcmp(aTopic, TOPIC_PLACES_SHUTDOWN) == 0) {
    Shutdown();

    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      (void)os->RemoveObserver(this, TOPIC_PLACES_SHUTDOWN);
    }
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsISupports

NS_IMPL_ISUPPORTS(History, IHistory, mozIAsyncHistory, nsIObserver,
                  nsIMemoryReporter)

}  // namespace mozilla::places
