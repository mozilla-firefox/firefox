/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "URLQueryStringStripper.h"

#include "mozilla/Components.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/glean/AntitrackingMetrics.h"

#include "nsIEffectiveTLDService.h"
#include "nsISupportsImpl.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsUnicharUtils.h"
#include "nsURLHelper.h"
#include "nsNetUtil.h"
#include "mozilla/dom/StripOnShareRuleBinding.h"

namespace {

mozilla::StaticRefPtr<mozilla::URLQueryStringStripper> gQueryStringStripper;

static const char kQueryStrippingEnabledPref[] =
    "privacy.query_stripping.enabled";
static const char kQueryStrippingEnabledPBMPref[] =
    "privacy.query_stripping.enabled.pbmode";
static const char kQueryStrippingOnShareEnabledPref[] =
    "privacy.query_stripping.strip_on_share.enabled";

}  // namespace

namespace mozilla {

NS_IMPL_ISUPPORTS(URLQueryStringStripper, nsIObserver,
                  nsIURLQueryStringStripper, nsIURLQueryStrippingListObserver)

// static
already_AddRefed<URLQueryStringStripper>
URLQueryStringStripper::GetSingleton() {
  if (!gQueryStringStripper) {
    gQueryStringStripper = new URLQueryStringStripper();
    // Check initial pref state and enable service. We can pass nullptr, because
    // OnPrefChange doesn't rely on the args.
    URLQueryStringStripper::OnPrefChange(nullptr, nullptr);

    RunOnShutdown(
        [&] {
          DebugOnly<nsresult> rv = gQueryStringStripper->Shutdown();
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                               "URLQueryStringStripper::Shutdown failed");
          gQueryStringStripper = nullptr;
        },
        ShutdownPhase::XPCOMShutdown);
  }

  return do_AddRef(gQueryStringStripper);
}

URLQueryStringStripper::URLQueryStringStripper() {
  mIsInitialized = false;

  nsresult rv = Preferences::RegisterCallback(
      &URLQueryStringStripper::OnPrefChange, kQueryStrippingEnabledPBMPref);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = Preferences::RegisterCallback(&URLQueryStringStripper::OnPrefChange,
                                     kQueryStrippingEnabledPref);

  rv = Preferences::RegisterCallback(&URLQueryStringStripper::OnPrefChange,
                                     kQueryStrippingOnShareEnabledPref);
  NS_ENSURE_SUCCESS_VOID(rv);
}

NS_IMETHODIMP
URLQueryStringStripper::StripForCopyOrShare(nsIURI* aURI,
                                            nsIURI** strippedURI) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(strippedURI);
  int aStripCount = 0;

  nsresult rv = StripForCopyOrShareInternal(aURI, strippedURI, aStripCount,
                                            /* aDry = */ false,
                                            /* aStripNestedURIs = */ false);
  NS_ENSURE_SUCCESS(rv, rv);

  glean::contentblocking::strip_on_share_params_removed.AccumulateSingleSample(
      aStripCount);

  if (!aStripCount) {
    return NS_OK;
  }

  // To calculate difference in length of the URL
  // after stripping occurs for Telemetry
  nsAutoCString specOriginalURI;
  nsAutoCString specStrippedURI;

  rv = aURI->GetDisplaySpec(specOriginalURI);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(*strippedURI);

  rv = (*strippedURI)->GetDisplaySpec(specStrippedURI);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t lengthDiff = specOriginalURI.Length() - specStrippedURI.Length();
  glean::contentblocking::strip_on_share_length_decrease.AccumulateSingleSample(
      lengthDiff);

  return NS_OK;
}

NS_IMETHODIMP
URLQueryStringStripper::CanStripForShare(nsIURI* aURI, bool* aCanStrip) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(aCanStrip);

  *aCanStrip = false;
  int aStripCount = 0;
  nsresult rv =
      StripForCopyOrShareInternal(aURI, nullptr, aStripCount, /* aDry = */ true,
                                  /* aStripNestedURIs = */ false);
  NS_ENSURE_SUCCESS(rv, rv);

  *aCanStrip = aStripCount != 0;
  return NS_OK;
}

NS_IMETHODIMP
URLQueryStringStripper::Strip(nsIURI* aURI, bool aIsPBM, nsIURI** aOutput,
                              uint32_t* aStripCount) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(aOutput);
  NS_ENSURE_ARG_POINTER(aStripCount);

  *aStripCount = 0;

  if (aIsPBM) {
    if (!StaticPrefs::privacy_query_stripping_enabled_pbmode()) {
      return NS_OK;
    }
  } else {
    if (!StaticPrefs::privacy_query_stripping_enabled()) {
      return NS_OK;
    }
  }

  if (CheckAllowList(aURI)) {
    return NS_OK;
  }

  return StripQueryString(aURI, aOutput, aStripCount);
}

// static
void URLQueryStringStripper::OnPrefChange(const char* aPref, void* aData) {
  MOZ_ASSERT(gQueryStringStripper);

  bool prefEnablesComponent =
      StaticPrefs::privacy_query_stripping_enabled() ||
      StaticPrefs::privacy_query_stripping_enabled_pbmode() ||
      StaticPrefs::privacy_query_stripping_strip_on_share_enabled();

  nsresult rv;
  if (prefEnablesComponent) {
    rv = gQueryStringStripper->Init();
  } else {
    rv = gQueryStringStripper->Shutdown();
  }
  NS_ENSURE_SUCCESS_VOID(rv);
}

nsresult URLQueryStringStripper::Init() {
  nsresult rv;
  if (mIsInitialized) {
    rv = gQueryStringStripper->ManageObservers();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }
  mIsInitialized = true;

  mListService = do_GetService("@mozilla.org/query-stripping-list-service;1");
  NS_ENSURE_TRUE(mListService, NS_ERROR_FAILURE);
  rv = gQueryStringStripper->ManageObservers();
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

// (Un)registers a QPS/Strip-on-share observer according to the QPS prefs states
// and the strip-on-share pref state. This is called whenever one of the three
// prefs changes, to ensure that we are not observing one of the lists although
// the corresponding feature is not turned on.
nsresult URLQueryStringStripper::ManageObservers() {
  MOZ_ASSERT(mListService);
  nsresult rv;
  // Register QPS observer.
  // We are not listening to QPS but the feature is on, register a listener.
  if (!mObservingQPS) {
    if (StaticPrefs::privacy_query_stripping_enabled() ||
        StaticPrefs::privacy_query_stripping_enabled_pbmode()) {
      rv = mListService->RegisterAndRunObserver(gQueryStringStripper);
      NS_ENSURE_SUCCESS(rv, rv);
      mObservingQPS = true;
    }
  } else {
    // Unregister QPS observer.
    // We are listening to QPS but the feature is off, unregister.
    if (!StaticPrefs::privacy_query_stripping_enabled() &&
        !StaticPrefs::privacy_query_stripping_enabled_pbmode()) {
      // Clean up QPS lists.
      mList.Clear();
      mAllowList.Clear();
      rv = mListService->UnregisterObserver(this);
      NS_ENSURE_SUCCESS(rv, rv);
      mObservingQPS = false;
    }
  }

  // Register Strip on Share observer.
  // We are not listening to strip-on-share but the feature is on, register an
  // Observer.
  if (!mObservingStripOnShare) {
    if (StaticPrefs::privacy_query_stripping_strip_on_share_enabled()) {
      rv = mListService->RegisterAndRunObserverStripOnShare(
          gQueryStringStripper);
      NS_ENSURE_SUCCESS(rv, rv);
      mObservingStripOnShare = true;
    }
  } else {
    // Unregister Strip on Share observer.
    // We are listening to strip-on-share but the feature is off, unregister.
    if (!StaticPrefs::privacy_query_stripping_strip_on_share_enabled()) {
      // Clean up strip-on-share list
      mStripOnShareGlobal.reset();
      mStripOnShareOriginMap.Clear();
      mStripOnShareSchemelessSiteMap.Clear();
      rv = mListService->UnregisterStripOnShareObserver(this);
      NS_ENSURE_SUCCESS(rv, rv);
      mObservingStripOnShare = false;
    }
  }
  return NS_OK;
}

nsresult URLQueryStringStripper::Shutdown() {
  if (!mIsInitialized) {
    return NS_OK;
  }
  nsresult rv = gQueryStringStripper->ManageObservers();
  NS_ENSURE_SUCCESS(rv, rv);
  mIsInitialized = false;
  mListService = nullptr;
  return NS_OK;
}

nsresult URLQueryStringStripper::StripQueryString(nsIURI* aURI,
                                                  nsIURI** aOutput,
                                                  uint32_t* aStripCount) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(aOutput);
  NS_ENSURE_ARG_POINTER(aStripCount);

  *aStripCount = 0;

  nsCOMPtr<nsIURI> uri(aURI);

  nsAutoCString query;
  nsresult rv = aURI->GetQuery(query);
  NS_ENSURE_SUCCESS(rv, rv);

  // We don't need to do anything if there is no query string.
  if (query.IsEmpty()) {
    return NS_OK;
  }

  URLParams params;

  URLParams::Parse(query, false, [&](nsCString&& name, nsCString&& value) {
    nsAutoCString lowerCaseName;
    ToLowerCase(name, lowerCaseName);

    if (mList.Contains(lowerCaseName)) {
      *aStripCount += 1;

      // Count how often a specific query param is stripped. For privacy reasons
      // this will only count query params listed in the Histogram definition.
      // Calls for any other query params will be discarded.
      nsAutoCString telemetryLabel("param_");
      telemetryLabel.Append(lowerCaseName);
      glean::contentblocking::query_stripping_count_by_param.Get(telemetryLabel)
          .Add();

      return true;
    }

    params.Append(name, value);
    return true;
  });

  // Return if there is no parameter has been stripped.
  if (!*aStripCount) {
    return NS_OK;
  }

  nsAutoCString newQuery;
  params.Serialize(newQuery, false);

  (void)NS_MutateURI(uri).SetQuery(newQuery).Finalize(aOutput);
  return NS_OK;
}

bool URLQueryStringStripper::CheckAllowList(nsIURI* aURI) {
  MOZ_ASSERT(aURI);

  // Get the site(eTLD+1) from the URI.
  nsAutoCString baseDomain;
  nsCOMPtr<nsIEffectiveTLDService> tldService =
      mozilla::components::EffectiveTLD::Service();
  nsresult rv = tldService->GetBaseDomain(aURI, 0, baseDomain);
  if (rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
      rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
    return false;
  }
  NS_ENSURE_SUCCESS(rv, false);

  return mAllowList.Contains(baseDomain);
}

void URLQueryStringStripper::PopulateStripList(const nsACString& aList) {
  mList.Clear();

  for (const nsACString& item : aList.Split(' ')) {
    mList.Insert(item);
  }
}

void URLQueryStringStripper::PopulateAllowList(const nsACString& aList) {
  mAllowList.Clear();

  for (const nsACString& item : aList.Split(',')) {
    mAllowList.Insert(item);
  }
}

NS_IMETHODIMP
URLQueryStringStripper::OnQueryStrippingListUpdate(
    const nsACString& aStripList, const nsACString& aAllowList) {
  PopulateStripList(aStripList);
  PopulateAllowList(aAllowList);
  return NS_OK;
}

NS_IMETHODIMP
URLQueryStringStripper::OnStripOnShareUpdate(const nsTArray<nsString>& aArgs,
                                             JSContext* aCx) {
  mStripOnShareOriginMap.Clear();
  mStripOnShareOriginMap.Clear();
  mStripOnShareGlobal.reset();

  for (const auto& ruleString : aArgs) {
    dom::StripRule rule;
    if (NS_WARN_IF(!rule.Init(ruleString))) {
      // Skipping malformed rules
      continue;
    }
    if (rule.mIsGlobal) {
      mStripOnShareGlobal = Some(rule);
    } else {
      for (const auto& origin : rule.mOrigins) {
        mStripOnShareOriginMap.InsertOrUpdate(origin, rule);
      }
      for (const auto& schemelessSite : rule.mSchemelessSites) {
        printf_stderr("Adding schemeless site: %s\n", schemelessSite.get());
        mStripOnShareSchemelessSiteMap.InsertOrUpdate(schemelessSite, rule);
      }
    }
  }

  return NS_OK;
}
// static
NS_IMETHODIMP
URLQueryStringStripper::TestGetStripList(nsACString& aStripList) {
  aStripList.Truncate();

  StringJoinAppend(
      aStripList, " "_ns, mList,
      [](auto& aResult, const auto& aValue) { aResult.Append(aValue); });
  return NS_OK;
}

/* nsIObserver */
NS_IMETHODIMP
URLQueryStringStripper::Observe(nsISupports*, const char* aTopic,
                                const char16_t*) {
  // Since this class is created at profile-after-change by the Category
  // Manager, it's expected to implement nsIObserver; however, we have nothing
  // interesting to do here.
  MOZ_ASSERT(strcmp(aTopic, "profile-after-change") == 0);

  return NS_OK;
}

bool URLQueryStringStripper::ShouldStripParam(const nsACString& aHost,
                                              const nsACString& aSchemelessSite,
                                              const nsACString& aName) {
  nsAutoCString lowerCaseName;
  ToLowerCase(aName, lowerCaseName);
  const auto matches = [&lowerCaseName](const dom::StripRule& aRule) {
    return aRule.mQueryParams.Contains(lowerCaseName);
  };
  // Look through the global rules.
  if (mStripOnShareGlobal.isSome() && matches(mStripOnShareGlobal.ref())) {
    return true;
  }
  // Check for site specific rules.
  if (auto entry = mStripOnShareOriginMap.Lookup(aHost);
      entry && matches(entry.Data())) {
    return true;
  }
  if (auto entry = mStripOnShareSchemelessSiteMap.Lookup(aSchemelessSite);
      entry && matches(entry.Data())) {
    return true;
  }
  // no rule covering
  return false;
}

int URLQueryStringStripper::TryStripValue(const nsACString& aHost,
                                          nsACString& aValue, bool aDry) {
  nsresult rv;

  nsAutoCString decodeValue;
  URLParams::DecodeString(aValue, decodeValue);

  nsCOMPtr<nsIURI> nestedURI;
  rv = NS_NewURI(getter_AddRefs(nestedURI), decodeValue);

  if (NS_FAILED(rv)) {
    return 0;
  }

  int stripCount = 0;
  // recurse down
  nsCOMPtr<nsIURI> strippedNestedURI;
  rv = StripForCopyOrShareInternal(nestedURI, getter_AddRefs(strippedNestedURI),
                                   stripCount, aDry,
                                   /* aStripNestedURIs = */ true);

  if (NS_SUCCEEDED(rv) && stripCount != 0) {
    if (aDry) {
      return 1;
    }
    MOZ_ASSERT(strippedNestedURI,
               "URL must be returned if stripCount != 0 in non-dry mode");
    nsAutoCString nestedURIString;
    rv = strippedNestedURI->GetSpec(nestedURIString);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return 0;
    }

    // Overwrite aValue with URL with stripped query parameters
    aValue.Truncate();
    URLParams::SerializeString(nestedURIString, aValue);
    return stripCount;
  }
  return 0;
}

nsresult URLQueryStringStripper::StripForCopyOrShareInternal(
    nsIURI* aURI, nsIURI** aStrippedURI, int& aStripCount, bool aDry,
    bool aStripNestedURIs) {
  if (!StaticPrefs::privacy_query_stripping_strip_on_share_enabled()) {
    aStripCount = 0;
    return NS_OK;
  }

  nsAutoCString query;
  nsresult rv = aURI->GetQuery(query);
  NS_ENSURE_SUCCESS(rv, rv);

  // We don't need to do anything if there is no query string.
  if (query.IsEmpty()) {
    return NS_OK;
  }

  nsAutoCString host;
  rv = aURI->GetHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  const nsCOMPtr<nsIEffectiveTLDService> eTLDService =
      mozilla::components::EffectiveTLD::Service(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString schemelessSite;
  rv = eTLDService->GetSchemelessSite(aURI, schemelessSite);
  NS_ENSURE_SUCCESS(rv, rv);

  URLParams params;

  URLParams::Parse(query, false, [&](nsCString&& aName, nsCString&& aValue) {
    if (ShouldStripParam(host, schemelessSite, aName)) {
      aStripCount++;
      // If we found a query param to strip in dry mode, skip iterating over the
      // remaining ones (we return greedily). Otherwise don't add the param to
      // the new list and continue with the next one.
      return !aDry;
    }

    // Only if it is top layer of the recursion then it checks if the value of
    // the query parameter is a valid URI if not then it gets added back to the
    // query, if it is then it gets passed back into this method but with the
    // recursive stripping flag set to true
    if (!aStripNestedURIs) {
      aStripCount += TryStripValue(host, aValue, aDry);
    }
    if (aDry) {
      return aStripCount == 0;
    }

    params.Append(aName, aValue);
    return true;
  });

  // Returns null for aStrippedURI if no query params have been stripped
  // or in dry mode.
  if (!aStripCount || aDry || !aStrippedURI) {
    return NS_OK;
  }

  nsAutoCString newQuery;
  params.Serialize(newQuery, false);
  return NS_MutateURI(aURI).SetQuery(newQuery).Finalize(aStrippedURI);
}
}  // namespace mozilla
