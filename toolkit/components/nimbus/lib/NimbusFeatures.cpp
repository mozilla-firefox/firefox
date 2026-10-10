/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/browser/NimbusFeatures.h"
#include "mozilla/browser/NimbusFeatureManifest.h"
#include "mozilla/dom/NimbusBinding.h"
#include "mozilla/Try.h"
#include "mozilla/glean/NimbusMetrics.h"

namespace mozilla {

constinit static nsTHashSet<nsCString> sExposureFeatureSet;

void NimbusFeatures::GetPrefName(const nsACString& branchPrefix,
                                 const nsACString& aFeatureId,
                                 const nsACString& aVariable,
                                 nsACString& aPref) {
  nsAutoCString featureAndVariable;
  featureAndVariable.Append(aFeatureId);
  if (!aVariable.IsEmpty()) {
    featureAndVariable.Append(".");
    featureAndVariable.Append(aVariable);
  }
  aPref.Truncate();
  aPref.Append(branchPrefix);
  aPref.Append(featureAndVariable);
}

/**
 * Returns the variable value configured via experiment or rollout.
 * If a fallback pref is defined in the FeatureManifest and it
 * has a user value set this takes precedence over remote configurations.
 */
bool NimbusFeatures::GetBool(const nsACString& aFeatureId,
                             const nsACString& aVariable, bool aDefault) {
  nsAutoCString experimentPref;
  GetPrefName(kSyncDataPrefBranch, aFeatureId, aVariable, experimentPref);
  if (Preferences::HasUserValue(experimentPref.get())) {
    return Preferences::GetBool(experimentPref.get(), aDefault);
  }

  nsAutoCString rolloutPref;
  GetPrefName(kSyncRolloutsPrefBranch, aFeatureId, aVariable, rolloutPref);
  if (Preferences::HasUserValue(rolloutPref.get())) {
    return Preferences::GetBool(rolloutPref.get(), aDefault);
  }

  auto prefName = GetNimbusFallbackPrefName(aFeatureId, aVariable);
  if (prefName.isSome()) {
    return Preferences::GetBool(prefName->get(), aDefault);
  }
  return aDefault;
}

/**
 * Returns the variable value configured via experiment or rollout.
 * If a fallback pref is defined in the FeatureManifest and it
 * has a user value set this takes precedence over remote configurations.
 */
int NimbusFeatures::GetInt(const nsACString& aFeatureId,
                           const nsACString& aVariable, int aDefault) {
  nsAutoCString experimentPref;
  GetPrefName(kSyncDataPrefBranch, aFeatureId, aVariable, experimentPref);
  if (Preferences::HasUserValue(experimentPref.get())) {
    return Preferences::GetInt(experimentPref.get(), aDefault);
  }

  nsAutoCString rolloutPref;
  GetPrefName(kSyncRolloutsPrefBranch, aFeatureId, aVariable, rolloutPref);
  if (Preferences::HasUserValue(rolloutPref.get())) {
    return Preferences::GetInt(rolloutPref.get(), aDefault);
  }

  auto prefName = GetNimbusFallbackPrefName(aFeatureId, aVariable);
  if (prefName.isSome()) {
    return Preferences::GetInt(prefName->get(), aDefault);
  }
  return aDefault;
}

nsresult NimbusFeatures::OnUpdate(const nsACString& aFeatureId,
                                  const nsACString& aVariable,
                                  PrefChangedFunc aUserCallback,
                                  void* aUserData) {
  nsAutoCString experimentPref;
  nsAutoCString rolloutPref;
  GetPrefName(kSyncDataPrefBranch, aFeatureId, aVariable, experimentPref);
  GetPrefName(kSyncRolloutsPrefBranch, aFeatureId, aVariable, rolloutPref);
  nsresult rv =
      Preferences::RegisterCallback(aUserCallback, experimentPref, aUserData);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = Preferences::RegisterCallback(aUserCallback, rolloutPref, aUserData);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult NimbusFeatures::OffUpdate(const nsACString& aFeatureId,
                                   const nsACString& aVariable,
                                   PrefChangedFunc aUserCallback,
                                   void* aUserData) {
  nsAutoCString experimentPref;
  nsAutoCString rolloutPref;
  GetPrefName(kSyncDataPrefBranch, aFeatureId, aVariable, experimentPref);
  GetPrefName(kSyncRolloutsPrefBranch, aFeatureId, aVariable, rolloutPref);
  nsresult rv =
      Preferences::UnregisterCallback(aUserCallback, experimentPref, aUserData);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = Preferences::UnregisterCallback(aUserCallback, rolloutPref, aUserData);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

/**
 * Attempt to read Nimbus preference to determine experiment and branch slug.
 * Nimbus will store a pref with experiment metadata in the following format:
 * {
 *   slug: "experiment slug",
 *   branch: { slug: "branch slug" },
 *   ...
 * }
 * The naming convention for preference names is:
 * `nimbus.syncdatastore.<feature_id>`
 * These values are used to send `exposure` telemetry pings.
 */
nsresult NimbusFeatures::GetExperimentSlug(const nsACString& aFeatureId,
                                           nsACString& aExperimentSlug,
                                           nsACString& aBranchSlug) {
  aExperimentSlug.Truncate();
  aBranchSlug.Truncate();

  nsAutoCString prefName;
  GetPrefName(kSyncDataPrefBranch, aFeatureId, EmptyCString(), prefName);

  nsAutoString prefValue;
  MOZ_TRY(Preferences::GetString(prefName.get(), prefValue));
  if (prefValue.IsEmpty()) {
    return NS_ERROR_UNEXPECTED;
  }

  dom::CachedNimbusExperimentMetadata meta;
  if (!meta.Init(prefValue)) {
    return NS_ERROR_UNEXPECTED;
  }

  aExperimentSlug.Assign(std::move(meta.mSlug));
  aBranchSlug.Assign(std::move(meta.mBranch.mSlug));

  return NS_OK;
}

/**
 * Sends an exposure event for aFeatureId when enrolled in an experiment.
 * By default attempt to send once per function call. For some usecases it might
 * be useful to send only once, in which case set the optional aOnce to `true`.
 */
nsresult NimbusFeatures::RecordExposureEvent(const nsACString& aFeatureId,
                                             const bool aOnce) {
  nsAutoCString featureName(aFeatureId);
  if (!sExposureFeatureSet.EnsureInserted(featureName) && aOnce) {
    // We already sent (or tried to send) an exposure ping for this featureId
    return NS_ERROR_ABORT;
  }
  nsAutoCString slugName;
  nsAutoCString branchName;
  MOZ_TRY(GetExperimentSlug(aFeatureId, slugName, branchName));
  if (slugName.IsEmpty() || branchName.IsEmpty()) {
    // Failed getting experiment metadata or not enrolled in an experiment for
    // this featureId
    return NS_ERROR_UNEXPECTED;
  }
  glean::normandy::expose_nimbus_experiment.Record(
      Some(glean::normandy::ExposeNimbusExperimentExtra{
          .branchslug = Some(branchName),
          .featureid = Some(featureName),
          .value = Some(slugName),
      }));
  glean::nimbus_events::exposure.Record(
      Some(glean::nimbus_events::ExposureExtra{
          .branch = Some(branchName),
          .experiment = Some(slugName),
          .featureId = Some(featureName),
      }));

  return NS_OK;
}

}  // namespace mozilla
