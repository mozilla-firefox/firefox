/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "ScopedPrefs.h"

#include "nsIURI.h"
#include "nsIEffectiveTLDService.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/Components.h"
#include "mozilla/ContentClassifierService.h"
#include "mozilla/Logging.h"
#include "mozilla/net/UrlClassifierCommon.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIChannel.h"

static mozilla::LazyLogModule gScopedPrefsLog("ScopedPrefs");

namespace mozilla {

// ===========
// ScopedPrefs
// ===========

NS_IMPL_ISUPPORTS(ScopedPrefs, nsIScopedPrefs);

// static
bool ScopedPrefs::BoolPrefScoped(const nsIScopedPrefs::Pref aPref,
                                 nsIChannel* aChannel) {
  NS_ENSURE_TRUE(aChannel, false);
  bool value = false;
  if (NS_SUCCEEDED(GetBoolPrefScopedInternal(aPref, aChannel, &value))) {
    return value;
  }
  DebugOnly<nsresult> rv =
      GetBoolPrefFallback(aPref, NS_UsePrivateBrowsing(aChannel), &value);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  return value;
}

// static
nsresult ScopedPrefs::GetBoolPrefScopedInternal(
    const nsIScopedPrefs::Pref aPref, nsIChannel* aChannel, bool* aValue) {
  NS_ENSURE_ARG_POINTER(aChannel);
  NS_ENSURE_ARG_POINTER(aValue);

  // retrieve ScopedPref object for this channel
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  RefPtr<dom::BrowsingContext> bc;
  nsresult rv = loadInfo->GetBrowsingContext(getter_AddRefs(bc));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetBoolPrefScopedInternal: Failed to get BrowsingContext"));
    return rv;
  }
  if (!bc) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetBoolPrefScopedInternal: BrowsingContext is null"));
    return NS_ERROR_FAILURE;
  }
  RefPtr<dom::CanonicalBrowsingContext> cbc = bc->Canonical();
  nsCOMPtr<nsIScopedPrefs> scopedPrefs = cbc->GetScopedPrefs();
  if (!scopedPrefs) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetBoolPrefScopedInternal: ScopedPrefs is null"));
    return NS_ERROR_FAILURE;
  }
  MOZ_TRY(scopedPrefs->GetBoolPrefScoped(aPref, bc, aValue));
  return NS_OK;
}

// static
nsresult ScopedPrefs::GetBoolPrefFallback(const nsIScopedPrefs::Pref aPref,
                                          bool aIsPrivate, bool* aValue) {
  switch (aPref) {
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_ENABLED:
      // In general the `ScopedPrefs` implementation should stay generic.
      // However, we are inheriting the behavior of the tracking-protection pref
      // differentiate between private and non-private mode.
      *aValue = AntiTrackingUtils::IsTrackingProtectionEnabled(aIsPrivate);
      break;
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_CRYPTOMINING_ENABLED:
      *aValue = StaticPrefs::privacy_trackingprotection_cryptomining_enabled();
      break;
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_FINGERPRINTING_ENABLED:
      *aValue =
          StaticPrefs::privacy_trackingprotection_fingerprinting_enabled();
      break;
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_SOCIALTRACKING_ENABLED:
      *aValue =
          StaticPrefs::privacy_trackingprotection_socialtracking_enabled();
      break;
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_EMAILTRACKING_ENABLED:
      *aValue =
          StaticPrefs::privacy_trackingprotection_emailtracking_enabled() ||
          (aIsPrivate &&
           StaticPrefs::
               privacy_trackingprotection_emailtracking_pbmode_enabled());
      break;
    // The content-classifier features gate independently from the
    // URLClassifier: each falls back to whether its blocking feature is active.
    // ContentClassifierService owns the engines prefs that decide this.
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_CONTENT_ENABLED:
      *aValue = ContentClassifierService::IsBlockingFeatureActive("trackers"_ns,
                                                                  aIsPrivate);
      break;
    case nsIScopedPrefs::
        PRIVACY_TRACKINGPROTECTION_CONTENT_CRYPTOMINING_ENABLED:
      *aValue = ContentClassifierService::IsBlockingFeatureActive(
          "cryptominers"_ns, aIsPrivate);
      break;
    case nsIScopedPrefs::
        PRIVACY_TRACKINGPROTECTION_CONTENT_FINGERPRINTING_ENABLED:
      *aValue = ContentClassifierService::IsBlockingFeatureActive(
          "fingerprinters"_ns, aIsPrivate);
      break;
    case nsIScopedPrefs::
        PRIVACY_TRACKINGPROTECTION_CONTENT_SOCIALTRACKING_ENABLED:
      *aValue = ContentClassifierService::IsBlockingFeatureActive(
          "social-trackers"_ns, aIsPrivate);
      break;
    case nsIScopedPrefs::
        PRIVACY_TRACKINGPROTECTION_CONTENT_EMAILTRACKING_ENABLED:
      *aValue = ContentClassifierService::IsBlockingFeatureActive(
          "email-trackers"_ns, aIsPrivate);
      break;
    case nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_CONTENT_TEST_ENABLED:
      *aValue = ContentClassifierService::IsBlockingFeatureActive(
          "test_block"_ns, aIsPrivate);
      break;
    case nsIScopedPrefs::NUM_SCOPED_BOOL_PREFS:
      // not a valid value
      return NS_ERROR_FAILURE;
  }
  MOZ_LOG(gScopedPrefsLog, LogLevel::Debug,
          ("GetBoolPrefFallback: pref=%d, isPrivate=%d, value=%d", aPref,
           aIsPrivate, *aValue));
  return NS_OK;
}

// static
nsresult ScopedPrefs::GetTopSite(nsIChannel* aChannel, nsACString& aOutSite) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv =
      net::UrlClassifierCommon::GetTopWindowURI(aChannel, getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: Failed to get top window URI"));
    return rv;
  }
  nsCOMPtr<nsIEffectiveTLDService> etld = components::EffectiveTLD::Service();
  if (!etld) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: EffectiveTLDService not available"));
    return NS_ERROR_NOT_AVAILABLE;
  }
  rv = etld->GetSite(uri, aOutSite);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: Failed to get site from URI"));
    return rv;
  }
  return NS_OK;
}

// static
nsresult ScopedPrefs::GetTopSite(dom::BrowsingContext* aBc,
                                 nsACString& aOutSite) {
  NS_ENSURE_ARG_POINTER(aBc);
  dom::CanonicalBrowsingContext* top = aBc->Canonical()->Top();
  dom::WindowGlobalParent* wgp = top->GetCurrentWindowGlobal();
  if (!wgp) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: WindowGlobalParent is null"));
    return NS_ERROR_FAILURE;
  }
  nsIURI* uri = wgp->GetDocumentURI();
  if (!uri) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: Document URI is null"));
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIEffectiveTLDService> etld = components::EffectiveTLD::Service();
  if (!etld) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: EffectiveTLDService not available"));
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsresult rv = etld->GetSite(uri, aOutSite);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gScopedPrefsLog, LogLevel::Warning,
            ("GetTopSite: Failed to get site from URI"));
    return rv;
  }
  return NS_OK;
}

// --------------
// nsIScopedPrefs
// --------------

nsresult ScopedPrefs::GetBoolPrefScoped(const nsIScopedPrefs::Pref pref,
                                        dom::BrowsingContext* aBc,
                                        bool* aValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aBc);
  if (pref >= nsIScopedPrefs::NUM_SCOPED_BOOL_PREFS) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!mBoolPrefValue[pref].IsEmpty()) {
    nsAutoCString site;
    if (NS_SUCCEEDED(ScopedPrefs::GetTopSite(aBc, site))) {
      if (auto siteConfig = mBoolPrefValue[pref].Lookup(site)) {
        *aValue = siteConfig.Data();
        MOZ_LOG(gScopedPrefsLog, LogLevel::Debug,
                ("GetBoolPrefScoped: pref=%d, site=%s, value=%d", pref,
                 site.get(), *aValue));
        return NS_OK;
      }
    }
  }
  bool isPrivate = false;
  aBc->GetUsePrivateBrowsing(&isPrivate);

  // fallback to global preferences, when there is no override
  return GetBoolPrefFallback(pref, isPrivate, aValue);
}

nsresult ScopedPrefs::SetBoolPrefScoped(const nsIScopedPrefs::Pref pref,
                                        dom::BrowsingContext* aBc,
                                        bool aValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aBc);
  if (pref >= nsIScopedPrefs::NUM_SCOPED_BOOL_PREFS) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoCString site;
  if (NS_WARN_IF(NS_FAILED(ScopedPrefs::GetTopSite(aBc, site)))) {
    return NS_ERROR_FAILURE;
  }
  mBoolPrefValue[pref].InsertOrUpdate(site, aValue);
  MOZ_LOG(gScopedPrefsLog, LogLevel::Debug,
          ("SetBoolPrefScoped: pref=%d, site=%s, value=%d", pref, site.get(),
           aValue));
  return NS_OK;
}

nsresult ScopedPrefs::ClearScoped() {
  for (auto& i : mBoolPrefValue) {
    i.Clear();
  }
  return NS_OK;
}

nsresult ScopedPrefs::ClearScopedPref(const nsIScopedPrefs::Pref pref) {
  MOZ_LOG(gScopedPrefsLog, LogLevel::Debug, ("ClearScopedPref: pref=%i", pref));
  if (pref >= nsIScopedPrefs::NUM_SCOPED_BOOL_PREFS) {
    return NS_ERROR_INVALID_ARG;
  }
  mBoolPrefValue[pref].Clear();
  return NS_OK;
}

nsresult ScopedPrefs::ClearScopedByHost(const nsACString& aHost) {
  MOZ_LOG(gScopedPrefsLog, LogLevel::Debug,
          ("ClearScopedByHost: host=%s", PromiseFlatCString(aHost).get()));
  for (auto& pref : mBoolPrefValue) {
    pref.Remove(aHost);
  }
  return NS_OK;
}

nsresult ScopedPrefs::ClearScopedPrefByHost(const nsIScopedPrefs::Pref pref,
                                            const nsACString& aHost) {
  MOZ_LOG(gScopedPrefsLog, LogLevel::Debug,
          ("ClearScopedPrefByHost: pref=%d, host=%s", pref,
           PromiseFlatCString(aHost).get()));
  if (pref >= nsIScopedPrefs::NUM_SCOPED_BOOL_PREFS) {
    return NS_ERROR_INVALID_ARG;
  }
  mBoolPrefValue[pref].Remove(aHost);
  return NS_OK;
}

}  // namespace mozilla
