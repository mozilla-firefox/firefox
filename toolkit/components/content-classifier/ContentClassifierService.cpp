/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentClassifierService.h"

#include "ContentClassifierFeatureUtils.h"
#include "ErrorList.h"
#include "ContentClassifierPrefMirror.h"
#include "mozilla/Logging.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "MainThreadUtils.h"
#include "nsDebug.h"
#include "mozilla/ContentClassifierEngine.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/Components.h"
#include "mozilla/MozPromise.h"
#include "mozilla/StaticPtr.h"
#include "nsIAsyncShutdown.h"
#include "nsIChannel.h"
#include "nsIClassifiedChannel.h"
#include "nsIObserverService.h"
#include "nsILoadInfo.h"
#include "nsIStreamLoader.h"
#include "nsIURI.h"
#include "nsIWritablePropertyBag2.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsContentUtils.h"
#include "nsIWebProgressListener.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashSet.h"
#include "nsThreadUtils.h"
#include "nsUrlClassifierDBService.h"

namespace mozilla {

static LazyLogModule gContentClassifierLog("ContentClassifier");

StaticRefPtr<ContentClassifierService> ContentClassifierService::sInstance;
bool ContentClassifierService::sEnabled = false;

namespace {

constexpr nsLiteralCString kTrackersListIds[] = {"disconnect-tracker-base"_ns};
constexpr nsLiteralCString kTrackersContentListIds[] = {
    "disconnect-tracker-content"_ns};
constexpr nsLiteralCString kSocialTrackersListIds[] = {"mozilla-social"_ns};
constexpr nsLiteralCString kFingerprintersListIds[] = {
    "disconnect-fingerprinters-base"_ns};
constexpr nsLiteralCString kEmailTrackersListIds[] = {
    "disconnect-email-base"_ns};
constexpr nsLiteralCString kCryptominersListIds[] = {
    "disconnect-cryptominer-base"_ns};
constexpr nsLiteralCString kMajorExceptionListIds[] = {
    "mozilla-major-exceptions"_ns};
constexpr nsLiteralCString kMinorExceptionListIds[] = {
    "mozilla-minor-exceptions"_ns};
constexpr nsLiteralCString kHarmfulAddonListIds[] = {
    "mozilla-harmful-addon"_ns};
constexpr nsLiteralCString kTestBlockListIds[] = {"test_block"_ns};
constexpr nsLiteralCString kTestAnnotateListIds[] = {"test_annotate"_ns};

constexpr ContentClassifierFeature kFeatures[] = {
    {"trackers"_ns, Span<const nsLiteralCString>(kTrackersListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING,
     nsIWebProgressListener::STATE_LOADED_LEVEL_1_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT,
     NS_ERROR_TRACKING_URI, false, true,
     Some(nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_CONTENT_ENABLED),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    // The annotation variant adds content-track-digest256, which mirrors
    // url-classifier's promotion to STATE_LOADED_LEVEL_2_TRACKING_CONTENT
    // when a content-track-* table matches.
    {"trackers-content"_ns,
     Span<const nsLiteralCString>(kTrackersContentListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING,
     nsIWebProgressListener::STATE_LOADED_LEVEL_2_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT,
     NS_ERROR_TRACKING_URI, false, true, Nothing(),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    {"social-trackers"_ns, Span<const nsLiteralCString>(kSocialTrackersListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_SOCIALTRACKING,
     nsIWebProgressListener::STATE_LOADED_SOCIALTRACKING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT,
     NS_ERROR_SOCIALTRACKING_URI, false, true,
     Some(nsIScopedPrefs::
              PRIVACY_TRACKINGPROTECTION_CONTENT_SOCIALTRACKING_ENABLED),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    {"fingerprinters"_ns, Span<const nsLiteralCString>(kFingerprintersListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_FINGERPRINTING,
     nsIWebProgressListener::STATE_LOADED_FINGERPRINTING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_FINGERPRINTING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_FINGERPRINTING_CONTENT,
     NS_ERROR_FINGERPRINTING_URI, false, true,
     Some(nsIScopedPrefs::
              PRIVACY_TRACKINGPROTECTION_CONTENT_FINGERPRINTING_ENABLED),
     &ContentClassifierFeatureUtils::ShouldClassifyNonPassiveForETP, nullptr},
    {"email-trackers"_ns, Span<const nsLiteralCString>(kEmailTrackersListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_EMAILTRACKING,
     nsIWebProgressListener::STATE_LOADED_EMAILTRACKING_LEVEL_1_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT,
     NS_ERROR_EMAILTRACKING_URI, false, true,
     Some(nsIScopedPrefs::
              PRIVACY_TRACKINGPROTECTION_CONTENT_EMAILTRACKING_ENABLED),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    {"cryptominers"_ns, Span<const nsLiteralCString>(kCryptominersListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_CRYPTOMINING,
     nsIWebProgressListener::STATE_LOADED_CRYPTOMINING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT,
     NS_ERROR_CRYPTOMINING_URI, false, true,
     Some(nsIScopedPrefs::
              PRIVACY_TRACKINGPROTECTION_CONTENT_CRYPTOMINING_ENABLED),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    {"harmful-addon"_ns, Span<const nsLiteralCString>(kHarmfulAddonListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING,
     0,  // mLoadedState: blocking-only feature, not annotated
     0,  // mReplacedState
     0,  // mAllowedState
     NS_ERROR_HARMFULADDON_URI, false, true, Nothing(),
     &ContentClassifierFeatureUtils::IsNonRecommendedAddonRequest,
     &ContentClassifierFeatureUtils::HarmfulAddonCancelChannelCallback},
    {"minor-exceptions"_ns,
     Span<const nsLiteralCString>(kMinorExceptionListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING, 0, 0, 0,
     NS_OK, true, true, Nothing(),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    {"major-exceptions"_ns,
     Span<const nsLiteralCString>(kMajorExceptionListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING, 0, 0, 0,
     NS_OK, true, true, Nothing(),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    // Test-only features. Their engines are built directly by the HTTP
    // test loader (driven by the *.test_list_urls prefs) and installed
    // into mEngines under these names. They behave like any other
    // feature once built and do not go through the RS client.
    {"test_block"_ns, Span<const nsLiteralCString>(kTestBlockListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING,
     nsIWebProgressListener::STATE_LOADED_LEVEL_1_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT,
     NS_ERROR_TRACKING_URI, false, false,
     Some(nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_CONTENT_TEST_ENABLED),
     &ContentClassifierFeatureUtils::ShouldClassifyForETP, nullptr},
    {"test_annotate"_ns, Span<const nsLiteralCString>(kTestAnnotateListIds),
     nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_TRACKING,
     nsIWebProgressListener::STATE_LOADED_LEVEL_1_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_REPLACED_TRACKING_CONTENT,
     nsIWebProgressListener::STATE_ALLOWED_TRACKING_CONTENT, NS_OK, false,
     false, Nothing(), &ContentClassifierFeatureUtils::ShouldClassifyForETP,
     nullptr},
};

// Prefs that name feature engines built into mEngines.
constexpr const char* kFeatureEnginesPrefs[] = {
    "privacy.trackingprotection.content.protection.engines",
    "privacy.trackingprotection.content.protection.engines.pbmode",
    "privacy.trackingprotection.content.annotation.engines",
    "privacy.trackingprotection.content.annotation.engines.pbmode",
};

bool HasAnyActiveRemoteSettingsFeatures() {
  nsTArray<nsCString> names;
  for (const char* pref : kFeatureEnginesPrefs) {
    names.Clear();
    EnginesPrefsSnapshot::AppendFeatureNamesFromPref(pref, names);
    for (const auto& name : names) {
      if (!name.Equals("test_block") && !name.Equals("test_annotate")) {
        return true;
      }
    }
  }
  return false;
}

void NotifyListsLoadedForTesting() {
  if (!StaticPrefs::privacy_trackingprotection_content_testing()) {
    return;
  }
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(
        nullptr, NS_CONTENT_CLASSIFIER_FILTER_LISTS_LOADED_TOPIC, nullptr);
  }
}

}  // namespace

NS_IMPL_ISUPPORTS(ContentClassifierProbeResult, nsIContentClassifierProbeResult)

NS_IMETHODIMP ContentClassifierProbeResult::GetFeatureName(
    nsACString& aFeatureName) {
  aFeatureName = mFeatureName;
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierProbeResult::GetMatched(bool* aMatched) {
  *aMatched = mMatched;
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierProbeResult::GetException(bool* aException) {
  *aException = mException;
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierProbeResult::GetImportant(bool* aImportant) {
  *aImportant = mImportant;
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierProbeResult::GetEngineResult(
    nsresult* aEngineResult) {
  *aEngineResult = mEngineResult;
  return NS_OK;
}

NS_IMPL_ISUPPORTS(ContentClassifierProbeReport, nsIContentClassifierProbeReport)

NS_IMETHODIMP ContentClassifierProbeReport::GetStatus(
    nsIContentClassifierService::ProbeStatus* aStatus) {
  *aStatus = mStatus;
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierProbeReport::GetResults(
    nsTArray<RefPtr<nsIContentClassifierProbeResult>>& aResults) {
  aResults = mResults.Clone();
  return NS_OK;
}

NS_IMPL_ISUPPORTS(ContentClassifierService, nsIAsyncShutdownBlocker,
                  nsIContentClassifierService, nsIMemoryReporter)

MOZ_DEFINE_MALLOC_SIZE_OF(ContentClassifierServiceMallocSizeOf)
#ifdef MOZ_MEMORY
MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF(
    ContentClassifierServiceMallocEnclosingSizeOf)
#else
// Without jemalloc, MOZ_DEFINE_MALLOC_ENCLOSING_SIZE_OF yields a function that
// returns 0 for everything. Passing null instead lets the engine fall back to
// estimates for the parts it can only reach through interior pointers.
static constexpr MallocSizeOf ContentClassifierServiceMallocEnclosingSizeOf =
    nullptr;
#endif

ContentClassifierService::ContentClassifierService()
    : mLock("ContentClassifierService::mLock"),
      mInitPhase(InitPhase::NotInited) {
  sEnabled =
      Preferences::GetBool(
          "privacy.trackingprotection.content.protection.enabled", false) ||
      Preferences::GetBool(
          "privacy.trackingprotection.content.annotation.enabled", false);
}

ContentClassifierService::~ContentClassifierService() = default;

NS_IMETHODIMP ContentClassifierService::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  // aAnonymize is ignored because the only variable path segment, the feature
  // name, is a compile-time literal from kFeatures rather than content data.
  struct EngineSizes {
    nsCString mFeatureName;
    ContentClassifierEngineSizes mSizes;
  };

  // Measure under the lock but report without it: the callback runs consumer
  // code, which must not run with the service lock held.
  nsTArray<EngineSizes> engines;
  {
    MutexAutoLock lock(mLock);
    engines.SetCapacity(mEngines.Count());
    for (const auto& entry : mEngines) {
      engines.AppendElement(
          EngineSizes{nsCString(entry.GetKey()),
                      entry.GetData()->SizeOfIncludingThis(
                          ContentClassifierServiceMallocSizeOf,
                          ContentClassifierServiceMallocEnclosingSizeOf)});
    }
  }

  // The paths are per-feature, so MOZ_COLLECT_REPORT is not usable here: it
  // needs a literal.
#define REPORT(_path, _amount, _desc)                                    \
  aHandleReport->Callback(""_ns, _path, KIND_HEAP, UNITS_BYTES, _amount, \
                          nsLiteralCString(_desc), aData)

  for (const auto& engine : engines) {
    REPORT(nsPrintfCString("explicit/content-classifier/engines/%s/objects",
                           engine.mFeatureName.get()),
           engine.mSizes.objects,
           "Memory used by the content classifier engine objects.");

    REPORT(
        nsPrintfCString("explicit/content-classifier/engines/%s/filter-rules",
                        engine.mFeatureName.get()),
        engine.mSizes.filter_rules,
        "Memory used by the parsed filter rules for this feature, held as one "
        "flatbuffer that the matching engine reads directly.");

    REPORT(
        nsPrintfCString("explicit/content-classifier/engines/%s/domain-hashes",
                        engine.mFeatureName.get()),
        engine.mSizes.domain_hashes,
        "Memory used by the index from domain hash to filter list position, "
        "rebuilt in memory each time this feature's rules are loaded.");

    REPORT(
        nsPrintfCString("explicit/content-classifier/engines/%s/regex-table",
                        engine.mFeatureName.get()),
        engine.mSizes.regex_table,
        "Memory used by the lookup table for the regexes compiled on demand "
        "while matching. Excludes the compiled regexes themselves, which the "
        "regex engine gives no way to measure.");

    REPORT(
        nsPrintfCString("explicit/content-classifier/engines/%s/enabled-tags",
                        engine.mFeatureName.get()),
        engine.mSizes.enabled_tags,
        "Memory used by the tag names enabled on this engine, which gate "
        "tagged filters.");

    REPORT(
        nsPrintfCString("explicit/content-classifier/engines/%s/cosmetic-cache",
                        engine.mFeatureName.get()),
        engine.mSizes.cosmetic_cache,
        "Memory owned by the cosmetic filter cache beyond the filter data it "
        "shares with the matcher. The rules it serves are reported under "
        "filter-rules, not here.");

    REPORT(nsPrintfCString("explicit/content-classifier/engines/%s/resources",
                           engine.mFeatureName.get()),
           engine.mSizes.resources,
           "Memory used by this engine's redirect and scriptlet resource "
           "backend. Excludes whatever the backend stores, which Firefox never "
           "populates.");
  }

#undef REPORT

  return NS_OK;
}

// static
bool ContentClassifierService::IsEnabled() {
  if (!sInstance) {
    return false;
  }

  return sEnabled;
}

// static
Span<const ContentClassifierFeature> ContentClassifierService::GetFeatures() {
  return Span<const ContentClassifierFeature>(kFeatures);
}

// static
Maybe<const ContentClassifierFeature&>
ContentClassifierService::GetFeatureByName(const nsACString& aName) {
  for (const auto& feature : kFeatures) {
    if (feature.mName.Equals(aName)) {
      return SomeRef(feature);
    }
  }
  return Nothing();
}

// static
bool ContentClassifierService::IsInitialized() {
  if (!sInstance) {
    return false;
  }

  MutexAutoLock lock(sInstance->mLock);
  return sInstance->mInitPhase == InitPhase::InitSucceeded;
}

// static
void ContentClassifierService::OnPrefChange(const char* aPref, void*) {
  MOZ_ASSERT(NS_IsMainThread());
  // Access sInstance directly rather than GetInstance(), because
  // GetInstance() returns nullptr when the feature is disabled, but we
  // need to handle enable/disable transitions here.
  RefPtr<ContentClassifierService> service = sInstance;
  if (!service) {
    return;
  }

  if (!IsInitialized()) {
    return;
  }

  bool wasEnabled = sEnabled;
  sEnabled =
      Preferences::GetBool(
          "privacy.trackingprotection.content.protection.enabled", false) ||
      Preferences::GetBool(
          "privacy.trackingprotection.content.annotation.enabled", false);

  // mRSClient is main-thread only (see header); the NS_IsMainThread
  // assert at the top of this function covers this read and the
  // subsequent Init/Shutdown calls.
  const bool hasRSClient = !!service->mRSClient;

  if (!wasEnabled && sEnabled && !hasRSClient) {
    // Feature just became enabled. Start the RS client if list names are set.
    if (HasAnyActiveRemoteSettingsFeatures()) {
      service->InitRSClient();
    }
    return;
  }

  if (wasEnabled && !sEnabled) {
    // Feature just became disabled. Tear down the RS client and engines.
    service->ShutdownRSClient();
    return;
  }

  // Feature enabled state unchanged. Handle individual pref changes.
  const nsDependentCString prefStr(aPref);
  const bool isFeatureSelectionPref =
      prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.protection.engines") ||
      prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.protection.engines.pbmode") ||
      prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.annotation.engines") ||
      prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.annotation.engines.pbmode") ||
      prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.testing.use_test_trackers_"
          "engines");

  if (isFeatureSelectionPref) {
    if (!sEnabled) {
      // The feature is disabled; nothing to rebuild or fetch. Enabling
      // will pick up the new pref.
      return;
    }
    // Active feature selection changed. Start RS client if needed; its
    // init will deliver onListsChanged notifications and trigger builds.
    if (!hasRSClient && HasAnyActiveRemoteSettingsFeatures()) {
      service->InitRSClient();
      return;
    }
    if (hasRSClient && !HasAnyActiveRemoteSettingsFeatures()) {
      service->ShutdownRSClient();
      return;
    }
    // Reshuffles into the per-mode arrays and picks up any newly-active
    // feature whose engine isn't built.
    service->ProcessListChanges({}, {});
    return;
  }

  // Redownload the test_* rule lists when the prefs controlling them are
  // updated
  nsTArray<nsCString> testEnginesToUpdate;
  if (prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.protection.test_list_urls")) {
    testEnginesToUpdate.AppendElement("test_block"_ns);
  }
  if (prefStr.EqualsLiteral(
          "privacy.trackingprotection.content.annotation.test_list_urls")) {
    testEnginesToUpdate.AppendElement("test_annotate"_ns);
  }

  if (!testEnginesToUpdate.IsEmpty()) {
    service->ProcessListChanges(testEnginesToUpdate, {});
    return;
  }

  // An .enabled pref changed but the combined enabled state didn't flip
  // (e.g. one was already true). Nothing to do - engines are already
  // populated via whichever path is active.
}

void ContentClassifierService::Init() {
  MOZ_ASSERT(XRE_IsParentProcess());
  AssertIsOnMainThread();

  {
    MutexAutoLock lock(mLock);

    if (mInitPhase != InitPhase::NotInited) {
      return;
    }

    MOZ_LOG(gContentClassifierLog, LogLevel::Info,
            ("ContentClassifierService::Init - initializing"));

    nsCOMPtr<nsIAsyncShutdownClient> shutdownBarrier =
        GetAsyncShutdownBarrier();
    if (!shutdownBarrier) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    bool closed;
    nsresult rv = shutdownBarrier->GetIsClosed(&closed);
    if (NS_FAILED(rv) || closed) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = shutdownBarrier->AddBlocker(
        this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__, u""_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.enabled"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.enabled"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }
    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.test_list_urls"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.test_list_urls"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.testing.use_test_trackers_engines"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.engines"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.engines"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.engines.pbmode"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = Preferences::RegisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.engines.pbmode"_ns);
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    rv = NS_CreateBackgroundTaskQueue("ContentClassifier",
                                      getter_AddRefs(mBuildThread));
    if (NS_FAILED(rv)) {
      mInitPhase = InitPhase::InitFailed;
      return;
    }

    mInitPhase = InitPhase::InitSucceeded;
  }

  // Weak: a strong registration would have the manager hold a reference,
  // keeping the service and the engines it owns alive past ClearOnShutdown.
  // The price is a bare pointer, which BlockShutdown removes.
  RegisterWeakMemoryReporter(this);

  // Lock released; safe to call into JS.
  // Only initialize the RS client if list_names prefs are set,
  // to avoid interfering with the test-only HTTP loading path.
  if (sEnabled && HasAnyActiveRemoteSettingsFeatures()) {
    InitRSClient();
  }

  ContentClassifierPrefMirror::Init();
}

void ContentClassifierService::InitRSClient() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mRSClient) {
    return;
  }

  MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Info,
              "InitRSClient - creating RS client");

  nsresult rv;
  mRSClient =
      do_GetService(NS_CONTENTCLASSIFIERREMOTESETTINGSCLIENT_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Error,
                "InitRSClient - failed to get RS client service: {:#x}",
                static_cast<uint32_t>(rv));
    return;
  }

  // The returned Promise is ignored: C++ doesn't need to await the
  // initial import. Callers that do (such as tests) observe the
  // NS_CONTENT_CLASSIFIER_FILTER_LISTS_LOADED_TOPIC notification.
  RefPtr<dom::Promise> unused;
  rv = mRSClient->Init(this, getter_AddRefs(unused));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Error,
                "InitRSClient - failed to init RS client: {:#x}",
                static_cast<uint32_t>(rv));
    mRSClient = nullptr;
    return;
  }
}

void ContentClassifierService::ShutdownRSClient() {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Info, "ShutdownRSClient");

  if (mRSClient) {
    // Release mRSClient before reacquiring mLock. The JS Shutdown()
    // implementation does not call back into us, but drop the strong
    // reference first to be defensive.
    nsCOMPtr<nsIContentClassifierRemoteSettingsClient> client =
        std::move(mRSClient);
    client->Shutdown();
  }

  MutexAutoLock lock(mLock);
  mEngines.Clear();
  mCancelEngines.Clear();
  mCancelEnginesPBM.Clear();
  mAnnotateEngines.Clear();
  mAnnotateEnginesPBM.Clear();
}

// static
already_AddRefed<ContentClassifierService>
ContentClassifierService::GetInstance() {
  if (!sInstance) {
    sInstance = new ContentClassifierService();
    ClearOnShutdown(&sInstance);
    sInstance->Init();
  }

  if (!IsInitialized() || !IsEnabled()) {
    return nullptr;
  }

  return do_AddRef(sInstance);
}

already_AddRefed<nsIAsyncShutdownClient>
ContentClassifierService::GetAsyncShutdownBarrier() const {
  nsCOMPtr<nsIAsyncShutdownService> svc = components::AsyncShutdown::Service();
  MOZ_RELEASE_ASSERT(svc);

  nsCOMPtr<nsIAsyncShutdownClient> client;
  nsresult rv = svc->GetProfileBeforeChange(getter_AddRefs(client));
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  MOZ_RELEASE_ASSERT(client);

  return client.forget();
}

NS_IMETHODIMP ContentClassifierService::BlockShutdown(
    nsIAsyncShutdownClient* aClient) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_LOG(gContentClassifierLog, LogLevel::Info,
          ("ContentClassifierService::BlockShutdown - shutting down"));

  // Flip the phase before anything else. Every build closure re-checks
  // mInitPhase under mLock before it touches state, so from here on a
  // queued closure cannot repopulate what ShutdownRSClient is about to
  // clear. Clearing mBuildThread also closes the dispatch window for any
  // subsequent UpdateFeatures call.
  {
    MutexAutoLock lock(mLock);
    mInitPhase = InitPhase::ShutdownStarted;
    mBuildThread = nullptr;
  }

  UnregisterWeakMemoryReporter(this);

  // ShutdownRSClient clears the filter list data and engines. It also
  // tears down the RS client if one was created (the HTTP-only test
  // path leaves mRSClient null).
  ShutdownRSClient();

  {
    MutexAutoLock lock(mLock);

    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.enabled"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.enabled"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.test_list_urls"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.test_list_urls"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.testing.use_test_trackers_engines"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.engines"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.engines"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.protection.engines.pbmode"_ns);
    Preferences::UnregisterCallback(
        &ContentClassifierService::OnPrefChange,
        "privacy.trackingprotection.content.annotation.engines.pbmode"_ns);

    content_classifier_teardown_domain_resolver();

    // Removal is synchronous on purpose. Nothing queued there needs to finish
    // before shutdown proceeds: closures bail at the mInitPhase check above.
    RemoveBlocker();
  }

  return NS_OK;
}

void ContentClassifierService::RemoveBlocker() {
  MOZ_ASSERT(NS_IsMainThread());
  mLock.AssertCurrentThreadOwns();
  nsCOMPtr<nsIAsyncShutdownClient> asc = GetAsyncShutdownBarrier();
  MOZ_ASSERT(asc);
  DebugOnly<nsresult> rv = asc->RemoveBlocker(this);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  mInitPhase = InitPhase::ShutdownEnded;
}

// Fold a single per-engine outcome into the aggregate. All matched engine
// results are appended so callers can attribute per-feature annotations,
// but the aggregate Status is promoted monotonically: a later
// non-Important Hit cannot demote an earlier Exception, and Important
// pins the status.
void ContentClassifierResult::Accumulate(
    ContentClassifierEngineResult aEngineResult) {
  Status engineStatus = Status::Miss;
  if (aEngineResult.Exception()) {
    engineStatus = aEngineResult.Important() ? Status::ImportantException
                                             : Status::Exception;
  } else if (aEngineResult.Matched()) {
    engineStatus =
        aEngineResult.Important() ? Status::ImportantHit : Status::Hit;
  }

  if (engineStatus > mStatus) {
    mStatus = engineStatus;
  }
  mEngineResults.AppendElement(std::move(aEngineResult));
}

ContentClassifierResult ContentClassifierService::ClassifyWithEngines(
    const nsTArray<RefPtr<ContentClassifierEngine>>& aEngines,
    const ContentClassifierRequest& aRequest, ClassifyMode aMode) {
  MOZ_ASSERT(!NS_IsMainThread());
  mLock.AssertCurrentThreadOwns();
  ContentClassifierResult result;
  if (mInitPhase != InitPhase::InitSucceeded) {
    MOZ_LOG(gContentClassifierLog, LogLevel::Warning,
            ("ClassifyWithEngines - service not initialized; returning Miss"));
    return result;
  }
  if (!aRequest.Valid()) {
    MOZ_LOG(gContentClassifierLog, LogLevel::Warning,
            ("ClassifyWithEngines - invalid request; returning Miss"));
    return result;
  }
  const bool annotate = aMode == ClassifyMode::Annotate;
  bool matchedSoFar = false;
  for (const auto& engine : aEngines) {
    if (engine->Feature().mRequestFilter &&
        !engine->Feature().mRequestFilter(aRequest, aMode)) {
      continue;
    }
    ContentClassifierEngineResult er = engine->CheckNetworkRequest(
        aRequest, annotate ? false : matchedSoFar,
        /* aMatchDocumentAsNetworkRequest */ annotate);
    result.Accumulate(er);
    const auto status = result.GetStatus();
    if (!annotate &&
        (status == ContentClassifierResult::Status::ImportantException ||
         status == ContentClassifierResult::Status::ImportantHit)) {
      break;
    }
    if (er.Matched() && !er.Exception()) {
      matchedSoFar = true;
    }
  }
  return result;
}

NS_IMETHODIMP ContentClassifierService::GetName(nsAString& aName) {
  aName.AssignLiteral("ContentClassifierService: Shutting down");
  return NS_OK;
}

static nsLiteralCString InitPhaseToString(InitPhase aPhase) {
  switch (aPhase) {
    case InitPhase::NotInited:
      return "not-inited"_ns;
    case InitPhase::InitSucceeded:
      return "init-succeeded"_ns;
    case InitPhase::InitFailed:
      return "init-failed"_ns;
    case InitPhase::ShutdownStarted:
      return "shutdown-started"_ns;
    case InitPhase::ShutdownEnded:
      return "shutdown-ended"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("unhandled InitPhase");
  return "unknown"_ns;
}

NS_IMETHODIMP ContentClassifierService::GetState(nsIPropertyBag** aState) {
  NS_ENSURE_ARG_POINTER(aState);
  *aState = nullptr;

  nsCOMPtr<nsIWritablePropertyBag2> bag =
      do_CreateInstance("@mozilla.org/hash-property-bag;1");
  NS_ENSURE_TRUE(bag, NS_ERROR_FAILURE);

  InitPhase phase;
  uint32_t engineCount;
  {
    MutexAutoLock lock(mLock);
    phase = mInitPhase;
    engineCount = mEngines.Count();
  }

  nsresult rv =
      bag->SetPropertyAsACString(u"phase"_ns, InitPhaseToString(phase));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = bag->SetPropertyAsUint32(u"engines"_ns, engineCount);
  NS_ENSURE_SUCCESS(rv, rv);

  bag.forget(aState);
  return NS_OK;
}

ContentClassifierResult ContentClassifierService::ClassifyForAnnotate(
    const ContentClassifierRequest& aRequest) {
  MutexAutoLock lock(mLock);
  const nsTArray<RefPtr<ContentClassifierEngine>>& engines =
      aRequest.PrivateBrowsing() ? mAnnotateEnginesPBM : mAnnotateEngines;
  ContentClassifierResult result =
      ClassifyWithEngines(engines, aRequest, ClassifyMode::Annotate);
  MOZ_LOG(gContentClassifierLog, LogLevel::Debug,
          ("ClassifyForAnnotate - url=%s hit=%d exception=%d",
           aRequest.Url().get(), result.Hit(), result.Exception()));
  return result;
}

ContentClassifierResult ContentClassifierService::ClassifyForCancel(
    const ContentClassifierRequest& aRequest) {
  MutexAutoLock lock(mLock);
  const nsTArray<RefPtr<ContentClassifierEngine>>& engines =
      aRequest.PrivateBrowsing() ? mCancelEnginesPBM : mCancelEngines;
  // ClassifyWithEngines bails out as soon as the aggregated status reaches
  // ImportantHit / ImportantException because either pins the outcome.
  ContentClassifierResult result =
      ClassifyWithEngines(engines, aRequest, ClassifyMode::Cancel);
  MOZ_LOG(gContentClassifierLog, LogLevel::Debug,
          ("ClassifyForCancel - url=%s hit=%d exception=%d",
           aRequest.Url().get(), result.Hit(), result.Exception()));
  return result;
}

void ContentClassifierService::MaybeAnnotateChannel(
    nsIChannel* aChannel, const ContentClassifierResult& aResult) {
  NS_ENSURE_TRUE_VOID(aChannel);
  if (!aResult.Hit()) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  aChannel->GetURI(getter_AddRefs(uri));

  for (const auto& engineResult : aResult.EngineResults()) {
    if (!engineResult.Matched() || engineResult.Exception()) {
      continue;
    }
    const ContentClassifierFeature& feature = engineResult.Feature();
    if (feature.mLoadedState == 0) {
      continue;
    }
    if (uri) {
      MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Debug,
                  "MaybeAnnotateChannel - url={} feature={}",
                  uri->GetSpecOrDefault(), feature.mName);
    }
    net::ChannelClassifierUtils::AnnotateChannel(
        aChannel, feature.mClassificationFlag, feature.mLoadedState);
  }
}

net::ChannelBlockDecision ContentClassifierService::MaybeCancelChannel(
    nsIChannel* aChannel, const ContentClassifierResult& aResult) {
  NS_ENSURE_TRUE(aChannel, net::ChannelBlockDecision::Allowed);
  if (!aResult.Hit()) {
    return net::ChannelBlockDecision::Allowed;
  }

  // Closest analogue to the URLClassifier's "first cancelling feature
  // wins" rule. Classification itself keeps evaluating all engines (so
  // exception rules can suppress a Hit), but at decision time we pick
  // the first matched feature whose definition carries a non-NS_OK
  // blocking error code. Iteration order is the order in
  // ClassifyWithEngines, which is the order of the engines pref.
  const ContentClassifierFeature* blockingFeature = nullptr;
  for (const auto& engineResult : aResult.EngineResults()) {
    if (!engineResult.Matched() || engineResult.Exception()) {
      continue;
    }
    const ContentClassifierFeature& feature = engineResult.Feature();
    if (feature.mBlockingErrorCode != NS_OK) {
      blockingFeature = &feature;
      break;
    }
  }
  if (!blockingFeature) {
    MOZ_LOG(gContentClassifierLog, LogLevel::Warning,
            ("MaybeCancelChannel - no matched feature carries a blocking error "
             "code; nothing to cancel"));
    return net::ChannelBlockDecision::Allowed;
  }

  nsCOMPtr<nsIURI> uri;
  aChannel->GetURI(getter_AddRefs(uri));
  if (uri) {
    MOZ_LOG(gContentClassifierLog, LogLevel::Debug,
            ("MaybeCancelChannel - url=%s", uri->GetSpecOrDefault().get()));
  }

  if (net::ChannelClassifierUtils::IsAllowListed(aChannel)) {
    return net::ChannelBlockDecision::Allowed;
  }

  // Check if the scoped pref overrides the blocking decision. test_block
  // standing in for trackers follows the trackers scoped pref.
  Maybe<nsIScopedPrefs::Pref> scopedPref =
      blockingFeature->mReferencedScopedPref;
  if (blockingFeature->mName.EqualsLiteral("test_block") &&
      EnginesPrefsSnapshot::UseTestTrackersEngines()) {
    scopedPref =
        Some(nsIScopedPrefs::PRIVACY_TRACKINGPROTECTION_CONTENT_ENABLED);
  }
  if (scopedPref.isSome() &&
      !ScopedPrefs::BoolPrefScoped(scopedPref.ref(), aChannel)) {
    return net::ChannelBlockDecision::Allowed;
  }

  net::ChannelBlockDecision decision = net::ChannelBlockDecision::Allowed;
  net::ChannelClassifierUtils::MaybeBlockChannel(
      aChannel, "content-classifier"_ns, "content-classifier-block"_ns,
      blockingFeature->mBlockingErrorCode, blockingFeature->mReplacedState,
      blockingFeature->mAllowedState, blockingFeature->mCancelChannelCallback,
      &decision);
  return decision;
}

// nsIContentClassifierService

NS_IMETHODIMP ContentClassifierService::OnListsChanged(
    const nsTArray<nsCString>& aUpdated, const nsTArray<nsCString>& aRemoved) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Debug,
              "OnListsChanged - updated={} removed={}", aUpdated.Length(),
              aRemoved.Length());

  {
    MutexAutoLock lock(mLock);
    if (mInitPhase != InitPhase::InitSucceeded) {
      return NS_ERROR_NOT_INITIALIZED;
    }
  }
  ProcessListChanges(aUpdated, aRemoved);
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierService::GetFeatureNames(
    nsTArray<nsCString>& aNames) {
  aNames.Clear();
  for (const auto& feature : GetFeatures()) {
    aNames.AppendElement(feature.mName);
  }
  return NS_OK;
}

static already_AddRefed<nsIContentClassifierProbeResult> MakeProbeResult(
    const ContentClassifierEngineResult& aEngineResult) {
  RefPtr<ContentClassifierProbeResult> result =
      new ContentClassifierProbeResult(
          aEngineResult.Feature().mName, aEngineResult.Matched(),
          aEngineResult.Exception(), aEngineResult.Important(),
          aEngineResult.EngineResult());
  return result.forget();
}

static void ProbeResultsToArray(
    const ContentClassifierResult& aResult,
    nsTArray<RefPtr<nsIContentClassifierProbeResult>>& aOut) {
  aOut.Clear();
  for (const auto& engineResult : aResult.EngineResults()) {
    aOut.AppendElement(MakeProbeResult(engineResult));
  }
}

static ContentClassifierRequest BuildRequestFromProbe(
    nsIContentClassifierProbeRequest* aRequest) {
  nsAutoCString url, sourceUrl, topWindowUrl, requestType;
  bool privateBrowsing = false;
  bool forceThirdPartyToTop = false;
  bool isNonRecommendedAddon = false;
  MOZ_ASSERT(aRequest);
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetUrl(url));
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetSourceUrl(sourceUrl));
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetTopWindowUrl(topWindowUrl));
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetRequestType(requestType));
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetPrivateBrowsing(&privateBrowsing));
  MOZ_ALWAYS_SUCCEEDS(aRequest->GetForceThirdPartyToTop(&forceThirdPartyToTop));
  MOZ_ALWAYS_SUCCEEDS(
      aRequest->GetIsNonRecommendedAddon(&isNonRecommendedAddon));
  return ContentClassifierRequest(url, sourceUrl, topWindowUrl, requestType,
                                  privateBrowsing, forceThirdPartyToTop,
                                  isNonRecommendedAddon);
}

static nsresult MakeProbePromise(JSContext* aCx, dom::Promise** aOutPromise) {
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_UNEXPECTED;
  }
  ErrorResult result;
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }
  promise.forget(aOutPromise);
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierService::ProbeBlocking(
    nsIContentClassifierProbeRequest* aRequest, JSContext* aCx,
    dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aRequest);
  NS_ENSURE_ARG_POINTER(aPromise);
  MOZ_ASSERT(NS_IsMainThread());
  if (!IsInitialized()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  ContentClassifierRequest request = BuildRequestFromProbe(aRequest);
  if (!request.Valid()) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<dom::Promise> promise;
  nsresult rv = MakeProbePromise(aCx, getter_AddRefs(promise));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "ContentClassifierService::ProbeBlocking promise", promise));

  nsIThread* backgroundThread = nsUrlClassifierDBService::BackgroundThread();
  if (backgroundThread) {
    rv = backgroundThread->Dispatch(
        NS_NewRunnableFunction(
            "ContentClassifierService::ProbeBlocking",
            [self = RefPtr{this}, request = std::move(request),
             promiseHolder]() {
              ContentClassifierResult result = self->ClassifyForCancel(request);
              nsTArray<RefPtr<nsIContentClassifierProbeResult>> results;
              ProbeResultsToArray(result, results);
              RefPtr<ContentClassifierProbeReport> report =
                  new ContentClassifierProbeReport(result.GetStatus(),
                                                   std::move(results));
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "ContentClassifierService::ProbeBlocking resolve",
                  [promiseHolder, report = std::move(report)]() {
                    promiseHolder->MaybeResolve(report);
                  }));
            }),
        NS_DISPATCH_NORMAL);
  } else {
    rv = NS_ERROR_NOT_INITIALIZED;
  }
  if (NS_FAILED(rv)) {
    promise->MaybeReject(rv);
  }
  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierService::ProbeAnnotate(
    nsIContentClassifierProbeRequest* aRequest, JSContext* aCx,
    dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aRequest);
  NS_ENSURE_ARG_POINTER(aPromise);
  MOZ_ASSERT(NS_IsMainThread());
  if (!IsInitialized()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  ContentClassifierRequest request = BuildRequestFromProbe(aRequest);
  if (!request.Valid()) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<dom::Promise> promise;
  nsresult rv = MakeProbePromise(aCx, getter_AddRefs(promise));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "ContentClassifierService::ProbeAnnotate promise", promise));

  nsIThread* backgroundThread = nsUrlClassifierDBService::BackgroundThread();
  if (backgroundThread) {
    rv = backgroundThread->Dispatch(
        NS_NewRunnableFunction(
            "ContentClassifierService::ProbeAnnotate",
            [self = RefPtr{this}, request = std::move(request),
             promiseHolder]() {
              ContentClassifierResult result =
                  self->ClassifyForAnnotate(request);
              nsTArray<RefPtr<nsIContentClassifierProbeResult>> results;
              ProbeResultsToArray(result, results);
              RefPtr<ContentClassifierProbeReport> report =
                  new ContentClassifierProbeReport(result.GetStatus(),
                                                   std::move(results));
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "ContentClassifierService::ProbeAnnotate resolve",
                  [promiseHolder, report = std::move(report)]() {
                    promiseHolder->MaybeResolve(report);
                  }));
            }),
        NS_DISPATCH_NORMAL);
  } else {
    rv = NS_ERROR_NOT_INITIALIZED;
  }
  if (NS_FAILED(rv)) {
    promise->MaybeReject(rv);
  }
  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP ContentClassifierService::ProbeFeature(
    const nsACString& aFeatureName, nsIContentClassifierProbeRequest* aRequest,
    JSContext* aCx, dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aRequest);
  NS_ENSURE_ARG_POINTER(aPromise);
  MOZ_ASSERT(NS_IsMainThread());
  if (!IsInitialized()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  if (GetFeatureByName(aFeatureName).isNothing()) {
    return NS_ERROR_INVALID_ARG;
  }
  ContentClassifierRequest request = BuildRequestFromProbe(aRequest);
  if (!request.Valid()) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<ContentClassifierEngine> engine;
  {
    MutexAutoLock lock(mLock);
    auto entry = mEngines.Lookup(nsCString(aFeatureName));
    if (!entry) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    engine = entry.Data();
  }

  RefPtr<dom::Promise> promise;
  nsresult rv = MakeProbePromise(aCx, getter_AddRefs(promise));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "ContentClassifierService::ProbeFeature promise", promise));

  nsIThread* backgroundThread = nsUrlClassifierDBService::BackgroundThread();
  if (backgroundThread) {
    rv = backgroundThread->Dispatch(
        NS_NewRunnableFunction(
            "ContentClassifierService::ProbeFeature",
            [self = RefPtr{this}, engine = std::move(engine),
             request = std::move(request), promiseHolder]() {
              // Matching is done under mLock, as every other engine consumer
              // does, so that an engine is never in use without it held.
              ContentClassifierEngineResult er = [&] {
                MutexAutoLock lock(self->mLock);
                return engine->CheckNetworkRequest(
                    request, /* aPreviouslyMatched */ false,
                    /* aMatchDocumentAsNetworkRequest */ false);
              }();
              nsCOMPtr<nsIContentClassifierProbeResult> probe =
                  MakeProbeResult(er);
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "ContentClassifierService::ProbeFeature resolve",
                  [promiseHolder, probe = std::move(probe)]() {
                    promiseHolder->MaybeResolve(probe);
                  }));
            }),
        NS_DISPATCH_NORMAL);
  } else {
    rv = NS_ERROR_NOT_INITIALIZED;
  }
  if (NS_FAILED(rv)) {
    promise->MaybeReject(rv);
  }
  promise.forget(aPromise);
  return NS_OK;
}

// Parses a byte buffer of adblock-format filter list text into rules.
// Tolerates both LF and CRLF line endings; skips empty lines.
static void ParseFilterListRules(const nsTArray<uint8_t>& aData,
                                 nsTArray<nsCString>& aRules) {
  nsDependentCSubstring content(reinterpret_cast<const char*>(aData.Elements()),
                                aData.Length());
  for (const auto& line : content.Split('\n')) {
    nsCString rule(line);
    // Trim trailing CR for CRLF line endings.
    if (!rule.IsEmpty() && rule.Last() == '\r') {
      rule.Truncate(rule.Length() - 1);
    }
    if (!rule.IsEmpty()) {
      aRules.AppendElement(std::move(rule));
    }
  }
}

// MozPromise resolved with the parsed rules for one feature's list IDs,
// rejected with the first nsresult error we encountered. Used to fan in
// the per-list getListBytes calls before building the engine.
using ListBytesPromise = MozPromise<nsTArray<uint8_t>, nsresult,
                                    /* IsExclusive = */ true>;

// Convert the JS Promise returned by getListBytes into a MozPromise
// resolved with the raw bytes (Uint8Array contents).
static RefPtr<ListBytesPromise> FetchListBytesFromRemoteSettings(
    nsIContentClassifierRemoteSettingsClient* aClient,
    const nsACString& aName) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<dom::Promise> jsPromise;
  nsresult rv = aClient->GetListBytes(aName, getter_AddRefs(jsPromise));
  if (NS_FAILED(rv) || !jsPromise) {
    return ListBytesPromise::CreateAndReject(
        NS_FAILED(rv) ? rv : NS_ERROR_FAILURE, __func__);
  }

  RefPtr<ListBytesPromise::Private> result =
      new ListBytesPromise::Private(__func__);
  jsPromise->AddCallbacksWithCycleCollectedArgs(
      [result](JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult&) {
        if (!aValue.isObject()) {
          result->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        JS::Rooted<JSObject*> jsObj(aCx, &aValue.toObject());
        dom::Uint8Array arr;
        if (!arr.Init(jsObj)) {
          result->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        nsTArray<uint8_t> bytes;
        if (!arr.AppendDataTo(bytes)) {
          result->Reject(NS_ERROR_OUT_OF_MEMORY, __func__);
          return;
        }
        result->Resolve(std::move(bytes), __func__);
      },
      [result](JSContext*, JS::Handle<JS::Value>, ErrorResult&) {
        result->Reject(NS_ERROR_FAILURE, __func__);
      });
  return result;
}

nsresult BuildEngineFromRules(const ContentClassifierFeature& aFeature,
                              const nsTArray<nsCString>& aRules,
                              RefPtr<ContentClassifierEngine>& aEngineOutput) {
  if (aRules.IsEmpty()) {
    MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Info,
                "BuildEngineFromRules - no rules for feature \"{}\"; ",
                aFeature.mName);
    aEngineOutput = nullptr;
    return NS_OK;
  }
  aEngineOutput = new ContentClassifierEngine(aFeature);
  nsresult rv = aEngineOutput->InitFromRules(aRules);
  if (NS_FAILED(rv)) {
    MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Warning,
                "BuildEngineFromRules - InitFromRules failed for feature "
                "\"{}\": {:#x}",
                aFeature.mName, static_cast<uint32_t>(rv));
    aEngineOutput = nullptr;
  }
  return rv;
}

nsresult ContentClassifierService::InstallEngine(
    const nsACString& aFeatureName, RefPtr<ContentClassifierEngine>&& aEngine) {
  mLock.AssertCurrentThreadOwns();
  if (!aEngine) {
    mEngines.Remove(nsCString(aFeatureName));
  } else {
    MOZ_ASSERT(aEngine->Feature().mName.Equals(aFeatureName));
    mEngines.InsertOrUpdate(nsCString(aFeatureName), std::move(aEngine));
  }
  return NS_OK;
}

void ContentClassifierService::PopulateAllActiveEnginesFromPreferenceSnapshot(
    const EnginesPrefsSnapshot& aPreferenceSnapshot) {
  mLock.AssertCurrentThreadOwns();
  PopulateActiveEngineListFromFeatureNames(aPreferenceSnapshot.mCancel,
                                           mCancelEngines);
  PopulateActiveEngineListFromFeatureNames(aPreferenceSnapshot.mCancelPBM,
                                           mCancelEnginesPBM);
  PopulateActiveEngineListFromFeatureNames(aPreferenceSnapshot.mAnnotate,
                                           mAnnotateEngines);
  PopulateActiveEngineListFromFeatureNames(aPreferenceSnapshot.mAnnotatePBM,
                                           mAnnotateEnginesPBM);
}

void ContentClassifierService::PopulateActiveEngineListFromFeatureNames(
    const nsTArray<nsCString>& aFeatureNames,
    nsTArray<RefPtr<ContentClassifierEngine>>& aEngineList) {
  mLock.AssertCurrentThreadOwns();
  aEngineList.Clear();
  bool sawExceptionOnly = false;
  for (const auto& name : aFeatureNames) {
    auto entry = mEngines.Lookup(name);
    if (entry) {
      RefPtr<ContentClassifierEngine> engine = entry.Data();
      MOZ_ASSERT(engine);
      if (sawExceptionOnly && !engine->Feature().mExceptionOnly) {
        MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Warning,
                    "PopulateActiveEngineListFromFeatureNames - pref lists "
                    "non-exception feature \"{}\" after an exception-only "
                    "feature; matched_rule state will not reach it",
                    name);
      }
      if (engine->Feature().mExceptionOnly) {
        sawExceptionOnly = true;
      }
      aEngineList.AppendElement(engine);
    }
  }
}

nsTHashSet<nsCString> ContentClassifierService::ActiveFeatureNames(
    const EnginesPrefsSnapshot& aPreferenceSnapshot) {
  nsTHashSet<nsCString> names;
  for (const auto& name : aPreferenceSnapshot.mCancel) {
    names.Insert(name);
  }
  for (const auto& name : aPreferenceSnapshot.mCancelPBM) {
    names.Insert(name);
  }
  for (const auto& name : aPreferenceSnapshot.mAnnotate) {
    names.Insert(name);
  }
  for (const auto& name : aPreferenceSnapshot.mAnnotatePBM) {
    names.Insert(name);
  }
  return names;
}

// static
bool ContentClassifierService::IsBlockingFeatureActive(
    const nsACString& aFeatureName, bool aIsPrivate) {
  if (!StaticPrefs::privacy_trackingprotection_content_protection_enabled()) {
    return false;
  }

  // The feature is active only if it is listed in the protection engines pref,
  // where test_block may stand in for trackers.
  nsAutoCString featureName(aFeatureName);
  if (featureName.EqualsLiteral("trackers") &&
      EnginesPrefsSnapshot::UseTestTrackersEngines()) {
    featureName.AssignLiteral("test_block");
  }
  nsTArray<nsCString> names;
  EnginesPrefsSnapshot::AppendFeatureNamesFromPref(
      aIsPrivate
          ? "privacy.trackingprotection.content.protection.engines.pbmode"
          : "privacy.trackingprotection.content.protection.engines",
      names);
  return names.Contains(featureName);
}

void ContentClassifierService::PruneInactiveEngines(
    const EnginesPrefsSnapshot& aPreferenceSnapshot) {
  mLock.AssertCurrentThreadOwns();
  nsTHashSet<nsCString> activeFeatureNames =
      ActiveFeatureNames(aPreferenceSnapshot);
  for (auto iter = mEngines.Iter(); !iter.Done(); iter.Next()) {
    if (!activeFeatureNames.Contains(iter.Key())) {
      MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Debug,
                  "PruneInactiveEngines - dropping engine for \"{}\"",
                  iter.Key());
      iter.Remove();
    }
  }
  for (auto iter = mFeatureVersions.Iter(); !iter.Done(); iter.Next()) {
    if (!activeFeatureNames.Contains(iter.Key())) {
      iter.Remove();
    }
  }
}

void ContentClassifierService::ProcessListChanges(
    const nsTArray<nsCString>& aUpdated, const nsTArray<nsCString>& aRemoved) {
  MOZ_ASSERT(NS_IsMainThread());

  EnginesPrefsSnapshot snapshot;
  nsTHashSet<nsCString> activeNames = ActiveFeatureNames(snapshot);

  nsTArray<const ContentClassifierFeature*> toUpdate;
  nsTArray<uint64_t> featureVersions;
  uint64_t generation;
  {
    MutexAutoLock lock(mLock);
    for (const auto& feature : GetFeatures()) {
      if (!activeNames.Contains(nsCString(feature.mName))) {
        continue;
      }
      bool affected = !mEngines.Contains(nsCString(feature.mName));
      if (!affected) {
        for (const auto& listId : feature.mListIds) {
          if (aUpdated.Contains(listId) || aRemoved.Contains(listId)) {
            affected = true;
            break;
          }
        }
      }
      if (affected) {
        toUpdate.AppendElement(&feature);
      }
    }

    // Bump the global generation and (where applicable) per-feature
    // versions under the lock. The build closure compares each captured
    // version against the current one before installing, so a later
    // ProcessListChanges call for the same feature can't be overwritten by
    // an earlier in-flight rebuild. The global generation gates
    // Populate / Prune / Notify so an older closure's stale snapshot
    // can't clobber the active-engine lists set up by a newer call.
    generation = ++mUpdateGeneration;
    featureVersions.SetCapacity(toUpdate.Length());
    for (const auto* feature : toUpdate) {
      uint64_t& v = mFeatureVersions.LookupOrInsert(feature->mName);
      featureVersions.AppendElement(++v);
    }

    // Let the engines that are already built follow the new prefs right away,
    // as url-classifier applies a pref change to the next classification. The
    // rebuild below refreshes the lists again once its fetches settle.
    PopulateAllActiveEnginesFromPreferenceSnapshot(snapshot);
  }

  UpdateFeatures(std::move(toUpdate), std::move(featureVersions), generation,
                 std::move(snapshot));
}

void ContentClassifierService::UpdateFeatures(
    nsTArray<const ContentClassifierFeature*> aFeatures,
    nsTArray<uint64_t> aFeatureVersions, uint64_t aGeneration,
    EnginesPrefsSnapshot aPreferenceSnapshot) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aFeatures.Length() == aFeatureVersions.Length());

#ifdef DEBUG
  // Ensure the features to update are all active in the snapshot.
  nsTHashSet<nsCString> activeNames = ActiveFeatureNames(aPreferenceSnapshot);
  for (const auto* feature : aFeatures) {
    MOZ_ASSERT(activeNames.Contains(nsCString(feature->mName)),
               "UpdateFeatures expects only features active in the snapshot");
  }
#endif

  nsTArray<RefPtr<EngineRulesPromise>> fetches;
  for (const auto* feature : aFeatures) {
    fetches.AppendElement(FetchEngineDataForFeature(*feature));
  }

  // mBuildThread is non-null iff mInitPhase == InitSucceeded. A null
  // value here means we're either pre-Init, init failed, or shutdown
  // has already extracted the queue; in all of those cases the rebuild
  // has nothing useful to do.
  nsCOMPtr<nsISerialEventTarget> buildThread = mBuildThread;
  if (!buildThread) {
    return;
  }

  RefPtr<ContentClassifierService> self = this;

  if (aFeatures.IsEmpty()) {
    // No fetches needed; still refresh the active lists and prune in
    // case the snapshot changed. Run the lock-holding work on the
    // build thread so it never blocks the main thread. The Notify is
    // dispatched only when this is still the latest UpdateFeatures
    // call — otherwise a newer call's closure will fire it.
    buildThread->Dispatch(NS_NewRunnableFunction(
        "ContentClassifierService::UpdateFeaturesNoFetch",
        [self, snapshot = std::move(aPreferenceSnapshot),
         generation = aGeneration]() {
          {
            MutexAutoLock lock(self->mLock);
            if (self->mInitPhase != InitPhase::InitSucceeded) {
              return;
            }
            if (self->mUpdateGeneration != generation) {
              return;
            }
            self->PopulateAllActiveEnginesFromPreferenceSnapshot(snapshot);
            self->PruneInactiveEngines(snapshot);
          }
          NS_DispatchToMainThread(NS_NewRunnableFunction(
              "ContentClassifierService::NotifyListsLoaded",
              [self]() { NotifyListsLoadedForTesting(); }));
        }));
    return;
  }

  EngineRulesPromise::AllSettled(GetMainThreadSerialEventTarget(), fetches)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, features = std::move(aFeatures),
           featureVersions = std::move(aFeatureVersions),
           snapshot = std::move(aPreferenceSnapshot),
           buildThread = std::move(buildThread), generation = aGeneration](
              EngineRulesPromise::AllSettledPromiseType::ResolveOrRejectValue&&
                  aValue) mutable {
            MOZ_ASSERT(NS_IsMainThread());

            // Collect per-feature rule arrays out of the settled promises;
            // defer the expensive parsing / InitFromRules to mBuildThread.
            nsTArray<nsTArray<nsCString>> perFeatureRules;
            perFeatureRules.SetLength(features.Length());
            if (aValue.IsResolve()) {
              auto& settled = aValue.ResolveValue();
              MOZ_ASSERT(settled.Length() == features.Length());
              for (size_t i = 0; i < settled.Length(); ++i) {
                if (settled[i].IsReject()) {
                  MOZ_LOG_FMT(
                      gContentClassifierLog, LogLevel::Warning,
                      "UpdateFeatures - fetch rejected for feature \"{}\"",
                      features[i]->mName);
                  continue;
                }
                perFeatureRules[i] = std::move(settled[i].ResolveValue());
              }
            }

            buildThread->Dispatch(NS_NewRunnableFunction(
                "ContentClassifierService::UpdateFeaturesBuild",
                [self, features = std::move(features),
                 featureVersions = std::move(featureVersions),
                 perFeatureRules = std::move(perFeatureRules),
                 snapshot = std::move(snapshot), generation]() mutable {
                  MOZ_ASSERT(!NS_IsMainThread());

                  // Build engines outside the lock; InitFromRules can be
                  // expensive. A null engine — from a fetch reject, build
                  // failure, or empty rules — clobbers any existing entry
                  // for that feature via InstallEngine.
                  nsTArray<RefPtr<ContentClassifierEngine>> builtEngines;
                  builtEngines.SetLength(features.Length());
                  for (size_t i = 0; i < features.Length(); ++i) {
                    if (perFeatureRules[i].IsEmpty()) {
                      continue;
                    }
                    RefPtr<ContentClassifierEngine> engine;
                    if (NS_FAILED(BuildEngineFromRules(
                            *features[i], perFeatureRules[i], engine))) {
                      continue;
                    }
                    builtEngines[i] = std::move(engine);
                  }

                  bool didFullWork = false;
                  {
                    MutexAutoLock lock(self->mLock);
                    if (self->mInitPhase != InitPhase::InitSucceeded) {
                      // Shutdown raced us; drop everything we built.
                      return;
                    }
                    // Install non-stale engines (per-feature versioning).
                    for (size_t i = 0; i < builtEngines.Length(); ++i) {
                      uint64_t current =
                          self->mFeatureVersions.Get(features[i]->mName);
                      if (current != featureVersions[i]) {
                        MOZ_LOG_FMT(
                            gContentClassifierLog, LogLevel::Debug,
                            "UpdateFeatures - skipping stale install for "
                            "feature \"{}\" (have v{}, current v{})",
                            features[i]->mName, featureVersions[i], current);
                        continue;
                      }
                      self->InstallEngine(features[i]->mName,
                                          std::move(builtEngines[i]));
                    }
                    // Only run Populate / Prune (and the Notify below)
                    // when this is still the latest UpdateFeatures call.
                    // A newer call's closure will run those itself with
                    // its own (more recent) snapshot.
                    if (self->mUpdateGeneration == generation) {
                      self->PopulateAllActiveEnginesFromPreferenceSnapshot(
                          snapshot);
                      self->PruneInactiveEngines(snapshot);
                      didFullWork = true;
                    }
                  }

                  if (didFullWork) {
                    NS_DispatchToMainThread(NS_NewRunnableFunction(
                        "ContentClassifierService::NotifyListsLoaded",
                        [self]() { NotifyListsLoadedForTesting(); }));
                  }
                }));
          });
}

class FilterListLoader final : public nsIStreamLoaderObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit FilterListLoader(nsTArray<nsCString>* aRules) : mRules(aRules) {}

  NS_IMETHOD
  OnStreamComplete(nsIStreamLoader* aLoader, nsISupports* aCtxt,
                   nsresult aStatus, uint32_t aResultLength,
                   const uint8_t* aResult) override {
    MOZ_ASSERT(NS_IsMainThread());

    NS_ENSURE_SUCCESS(aStatus, aStatus);
    if (NS_FAILED(aStatus)) {
      MOZ_LOG(gContentClassifierLog, LogLevel::Debug,
              ("FilterListLoader::OnStreamComplete - failed with status 0x%x",
               static_cast<uint32_t>(aStatus)));
      mPromiseHolder.RejectIfExists(aStatus, __func__);
      return aStatus;
    }

    nsAutoCString content(reinterpret_cast<const char*>(aResult),
                          aResultLength);

    for (const auto& line : content.Split('\n')) {
      if (!line.IsEmpty()) {
        mRules->AppendElement(line);
      }
    }

    MOZ_LOG(gContentClassifierLog, LogLevel::Debug,
            ("FilterListLoader::OnStreamComplete - loaded %zu rules",
             mRules->Length()));

    mPromiseHolder.ResolveIfExists(true, __func__);

    return NS_OK;
  }

  RefPtr<GenericPromise> Load(const nsACString& aURL) {
    MOZ_ASSERT(NS_IsMainThread());

    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), aURL);
    NS_ENSURE_SUCCESS(rv, GenericPromise::CreateAndReject(rv, __func__));

    nsCOMPtr<nsIChannel> channel;
    uint32_t loadFlags = nsIChannel::LOAD_BYPASS_URL_CLASSIFIER;
    rv = NS_NewChannel(getter_AddRefs(channel), uri,
                       nsContentUtils::GetSystemPrincipal(),
                       nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                       nsIContentPolicy::TYPE_OTHER,
                       nullptr,  // nsICookieJarSettings
                       nullptr,  // aPerformanceStorage
                       nullptr,  // aLoadGroup
                       nullptr,  // aInterfaceRequestor
                       loadFlags);
    NS_ENSURE_SUCCESS(rv, GenericPromise::CreateAndReject(rv, __func__));

    nsCOMPtr<nsIStreamLoader> loader;
    rv = NS_NewStreamLoader(getter_AddRefs(loader), this);
    NS_ENSURE_SUCCESS(rv, GenericPromise::CreateAndReject(rv, __func__));

    rv = channel->AsyncOpen(loader);
    NS_ENSURE_SUCCESS(rv, GenericPromise::CreateAndReject(rv, __func__));

    return mPromiseHolder.Ensure(__func__);
  }

 private:
  ~FilterListLoader() {
    mPromiseHolder.RejectIfExists(NS_ERROR_ABORT, __func__);
  }

  nsTArray<nsCString>* mRules;
  MozPromiseHolder<GenericPromise> mPromiseHolder;
};

NS_IMPL_ISUPPORTS(FilterListLoader, nsIStreamLoaderObserver)

RefPtr<ContentClassifierService::EngineRulesPromise>
ContentClassifierService::FetchEngineDataForFeature(
    const ContentClassifierFeature& aFeature) {
  if (aFeature.mName.Equals("test_block") ||
      aFeature.mName.Equals("test_annotate")) {
    return FetchEngineDataForTestFeature(aFeature);
  }

  if (!mRSClient) {
    return EngineRulesPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                               __func__);
  }

  nsTArray<RefPtr<ListBytesPromise>> fetches;
  nsTArray<nsCString> listIdsInOrder;
  for (const auto& listId : aFeature.mListIds) {
    listIdsInOrder.AppendElement(listId);
    fetches.AppendElement(FetchListBytesFromRemoteSettings(mRSClient, listId));
  }

  if (fetches.IsEmpty()) {
    return EngineRulesPromise::CreateAndResolve(nsTArray<nsCString>{},
                                                __func__);
  }

  RefPtr<EngineRulesPromise::Private> result =
      new EngineRulesPromise::Private(__func__);
  RefPtr<ContentClassifierService> self = this;
  ListBytesPromise::AllSettled(GetMainThreadSerialEventTarget(), fetches)
      ->Then(GetMainThreadSerialEventTarget(), __func__,
             [self, result, feature = &aFeature,
              listIdsInOrder = std::move(listIdsInOrder)](
                 const ListBytesPromise::AllSettledPromiseType::
                     ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Error,
                             "FetchEngineDataForFeature - failed to fetch "
                             "list bytes for feature \"{}\"",
                             feature->mName);
                 result->Reject(NS_ERROR_NOT_AVAILABLE, __func__);
                 return;
               }

               const auto& fetchPromises = aValue.ResolveValue();
               nsTArray<nsCString> rules;
               for (size_t i = 0; i < fetchPromises.Length(); ++i) {
                 if (fetchPromises[i].IsReject()) {
                   MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Warning,
                               "FetchEngineDataForFeature - list \"{}\" for "
                               "feature \"{}\" rejected",
                               listIdsInOrder[i], feature->mName);
                   continue;
                 }
                 const auto& fetchResult = fetchPromises[i].ResolveValue();
                 if (fetchResult.IsEmpty()) {
                   MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Warning,
                               "FetchEngineDataForFeature - list \"{}\" for "
                               "feature \"{}\" returned no bytes",
                               listIdsInOrder[i], feature->mName);
                   continue;
                 }
                 ParseFilterListRules(fetchResult, rules);
               }
               result->Resolve(std::move(rules), __func__);
             });
  return result;
}

RefPtr<ContentClassifierService::EngineRulesPromise>
ContentClassifierService::FetchEngineDataForTestFeature(
    const ContentClassifierFeature& aFeature) {
  MOZ_LOG_FMT(gContentClassifierLog, LogLevel::Debug,
              "FetchEngineDataForTestFeature - loading filter lists for "
              "feature \"{}\"",
              aFeature.mName);

  nsAutoCString testListURLPref;
  if (aFeature.mName.Equals("test_annotate")) {
    Preferences::GetCString(
        "privacy.trackingprotection.content.annotation.test_list_urls",
        testListURLPref);

  } else if (aFeature.mName.Equals("test_block")) {
    Preferences::GetCString(
        "privacy.trackingprotection.content.protection.test_list_urls",
        testListURLPref);

  } else {
    MOZ_LOG(gContentClassifierLog, LogLevel::Warning,
            ("FetchEngineDataForTestFeature - incorrect feature name"));
    return EngineRulesPromise::CreateAndResolve(nsTArray<nsCString>{},
                                                __func__);
  }

  nsTArray<nsCString> listURLS;
  for (const nsACString& url : testListURLPref.Split('|')) {
    if (!url.IsEmpty()) {
      listURLS.AppendElement(url);
      MOZ_LOG(gContentClassifierLog, LogLevel::Debug,
              ("FetchEngineDataForTestFeature - test list URL: %s",
               nsAutoCString(url).get()));
    }
  }

  nsTArray<RefPtr<GenericPromise>> promises;
  nsTArray<nsTArray<nsCString>> filterRules;
  filterRules.SetLength(listURLS.Length());

  for (size_t i = 0; i < listURLS.Length(); ++i) {
    RefPtr<FilterListLoader> loader = new FilterListLoader(&filterRules[i]);
    promises.AppendElement(loader->Load(listURLS[i]));
  }

  RefPtr<EngineRulesPromise::Private> result =
      new EngineRulesPromise::Private(__func__);

  GenericPromise::AllSettled(GetMainThreadSerialEventTarget(), promises)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr{this}, result, filterRules = std::move(filterRules)](
              const GenericPromise::AllSettledPromiseType::ResolveOrRejectValue&
                  aResults) {
            nsTArray<nsCString> rules;
            for (const auto& fromUrl : filterRules) {
              rules.AppendElements(fromUrl);
            }
            result->Resolve(std::move(rules), __func__);
          });
  return result;
}

}  // namespace mozilla
