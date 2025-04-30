/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PartitioningExceptionList.h"

#include "AntiTrackingLog.h"
#include "nsContentUtils.h"
#include "nsServiceManagerUtils.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPtr.h"

namespace mozilla {

namespace {

static constexpr std::array<nsLiteralCString, 2> kSupportedSchemes = {
    {"https://"_ns, "http://"_ns}};

StaticRefPtr<PartitioningExceptionList> gPartitioningExceptionList;

}  // namespace

NS_IMPL_ISUPPORTS(PartitioningExceptionList,
                  nsIPartitioningExceptionListObserver)

bool PartitioningExceptionList::Check(const nsACString& aFirstPartyOrigin,
                                      const nsACString& aThirdPartyOrigin) {
  if (!StaticPrefs::privacy_antitracking_enableWebcompat()) {
    LOG(("Partition exception list disabled via pref"));
    return false;
  }

  if (aFirstPartyOrigin.IsEmpty() || aFirstPartyOrigin == "null" ||
      aThirdPartyOrigin.IsEmpty() || aThirdPartyOrigin == "null") {
    return false;
  }

  LOG(("Check partitioning exception list for url %s and %s",
       PromiseFlatCString(aFirstPartyOrigin).get(),
       PromiseFlatCString(aThirdPartyOrigin).get()));

  for (PartitionExceptionListEntry& entry : GetOrCreate()->mExceptionList) {
    if (OriginMatchesPattern(aFirstPartyOrigin, entry.mFirstParty) &&
        OriginMatchesPattern(aThirdPartyOrigin, entry.mThirdParty)) {
      LOG(("Found partitioning exception list entry for %s and %s",
           PromiseFlatCString(aFirstPartyOrigin).get(),
           PromiseFlatCString(aThirdPartyOrigin).get()));

      return true;
    }
  }

  return false;
}

PartitioningExceptionList* PartitioningExceptionList::GetOrCreate() {
  if (!gPartitioningExceptionList) {
    gPartitioningExceptionList = new PartitioningExceptionList();
    gPartitioningExceptionList->Init();

    RunOnShutdown([&] {
      gPartitioningExceptionList->Shutdown();
      gPartitioningExceptionList = nullptr;
    });
  }

  return gPartitioningExceptionList;
}

nsresult PartitioningExceptionList::Init() {
  mService =
      do_GetService("@mozilla.org/partitioning/exception-list-service;1");
  if (NS_WARN_IF(!mService)) {
    return NS_ERROR_FAILURE;
  }

  mService->RegisterAndRunExceptionListObserver(this);
  return NS_OK;
}

void PartitioningExceptionList::Shutdown() {
  if (mService) {
    mService->UnregisterExceptionListObserver(this);
    mService = nullptr;
  }

  mExceptionList.Clear();
}

NS_IMETHODIMP
PartitioningExceptionList::OnExceptionListUpdate(const nsACString& aList) {
  mExceptionList.Clear();

  nsresult rv;
  for (const nsACString& item : aList.Split(';')) {
    auto origins = item.Split(',');
    auto originsIt = origins.begin();

    if (originsIt == origins.end()) {
      LOG(("Ignoring empty exception entry"));
      continue;
    }

    PartitionExceptionListEntry entry;

    rv = GetExceptionListPattern(*originsIt, entry.mFirstParty);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    ++originsIt;

    if (originsIt == origins.end()) {
      LOG(("Ignoring incomplete exception entry"));
      continue;
    }

    rv = GetExceptionListPattern(*originsIt, entry.mThirdParty);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    if (entry.mFirstParty.mSuffix == "*" && entry.mThirdParty.mSuffix == "*") {
      LOG(("Ignoring *,* exception entry"));
      continue;
    }

    LOG(("onExceptionListUpdate: %s%s - %s%s", entry.mFirstParty.mScheme.get(),
         entry.mFirstParty.mSuffix.get(), entry.mThirdParty.mScheme.get(),
         entry.mThirdParty.mSuffix.get()));

    mExceptionList.AppendElement(entry);
  }

  return NS_OK;
}

nsresult PartitioningExceptionList::GetSchemeFromOrigin(
    const nsACString& aOrigin, nsACString& aScheme,
    nsACString& aOriginNoScheme) {
  NS_ENSURE_FALSE(aOrigin.IsEmpty(), NS_ERROR_INVALID_ARG);

  for (const auto& scheme : kSupportedSchemes) {
    if (aOrigin.Length() <= scheme.Length() ||
        !StringBeginsWith(aOrigin, scheme)) {
      continue;
    }
    aScheme = Substring(aOrigin, 0, scheme.Length());
    aOriginNoScheme = Substring(aOrigin, scheme.Length());
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

bool PartitioningExceptionList::OriginMatchesPattern(
    const nsACString& aOrigin, const PartitionExceptionListPattern& aPattern) {
  if (NS_WARN_IF(aOrigin.IsEmpty())) {
    return false;
  }

  if (aPattern.mSuffix == "*") {
    return true;
  }

  nsAutoCString scheme, originNoScheme;
  nsresult rv = GetSchemeFromOrigin(aOrigin, scheme, originNoScheme);
  if (NS_FAILED(rv)) {
    return false;
  }

  // Always strict match scheme.
  if (scheme != aPattern.mScheme) {
    return false;
  }

  if (!aPattern.mIsWildCard) {
    // aPattern is not a wildcard, match strict.
    return originNoScheme == aPattern.mSuffix;
  }

  // For wildcard patterns, check if origin suffix matches pattern suffix.
  return StringEndsWith(originNoScheme, aPattern.mSuffix);
}

// Parses a string with an origin or an origin-pattern into a
// PartitionExceptionListPattern.
nsresult PartitioningExceptionList::GetExceptionListPattern(
    const nsACString& aOriginPattern, PartitionExceptionListPattern& aPattern) {
  NS_ENSURE_FALSE(aOriginPattern.IsEmpty(), NS_ERROR_INVALID_ARG);

  if (aOriginPattern == "*") {
    aPattern.mIsWildCard = true;
    aPattern.mSuffix = "*";

    return NS_OK;
  }

  nsAutoCString originPatternNoScheme;
  nsresult rv = GetSchemeFromOrigin(aOriginPattern, aPattern.mScheme,
                                    originPatternNoScheme);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (StringBeginsWith(originPatternNoScheme, "*"_ns)) {
    NS_ENSURE_TRUE(originPatternNoScheme.Length() > 2, NS_ERROR_INVALID_ARG);

    aPattern.mIsWildCard = true;
    aPattern.mSuffix = Substring(originPatternNoScheme, 1);

    return NS_OK;
  }

  aPattern.mIsWildCard = false;
  aPattern.mSuffix = originPatternNoScheme;

  return NS_OK;
}

}  // namespace mozilla
