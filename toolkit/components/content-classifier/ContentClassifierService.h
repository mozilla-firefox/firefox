/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ContentClassifierService_h
#define mozilla_ContentClassifierService_h

#include <cstdint>
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/MozPromise.h"
#include "mozilla/ScopedPrefs.h"
#include "mozilla/Span.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ThreadSafety.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "nsIAsyncShutdown.h"
#include "nsIChannel.h"
#include "nsIClassifiedChannel.h"
#include "nsIContentClassifierService.h"
#include "nsIContentClassifierRemoteSettingsClient.h"
#include "nsIMemoryReporter.h"
#include "nsISupportsImpl.h"
#include "nsLiteralString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsCharSeparatedTokenizer.h"

#include "mozilla/ContentClassifierEngine.h"

class nsISerialEventTarget;

namespace mozilla {

enum class ClassifyMode { Annotate, Cancel };

struct ContentClassifierFeature {
  // Feature identifier used by prefs and lookups.
  nsLiteralCString mName;

  // RemoteSettings record names whose attachments are merged into a
  // single ContentClassifierEngine for this feature.
  Span<const nsLiteralCString> mListIds;

  // nsIClassifiedChannel::ClassificationFlags value set on the channel
  // on an annotation match (consumed via UrlClassifierCommon::Annotate-
  // Channel -> SetClassificationFlagsHelper).
  nsIClassifiedChannel::ClassificationFlags mClassificationFlag;

  // nsIWebProgressListener::STATE_LOADED_* value logged in the content
  // blocking log on an annotation match. The corresponding STATE_BLOCKED_*
  // value for a cancellation match is derived from mBlockingErrorCode by
  // UrlClassifierCommon::SetBlockedContent. Set to 0 for features that
  // only annotate the channel and never emit a content blocking log
  // entry (mirrors UrlClassifierCommon::AnnotateChannelWithoutNotifying
  // behavior).
  uint32_t mLoadedState;

  // nsIWebProgressListener::STATE_REPLACED_* value logged in the content
  // blocking log on a blocking match that ends up replaced.
  uint32_t mReplacedState;

  // nsIWebProgressListener::STATE_ALLOWED_* value logged in the content
  // blocking log on a blocking match that ends up allowed.
  uint32_t mAllowedState;

  // NS_ERROR_*_URI value passed to UrlClassifierCommon::SetBlockedContent
  // on a cancellation match. SetBlockedContent uses this to set both the
  // load-info blocking reason and (via GetClassifierBlockingEventCode)
  // the STATE_BLOCKED_* content blocking log entry. Set to NS_OK for
  // features that have no blocking variant in url-classifier; consumers
  // must check this before attempting to use the feature in blocking
  // mode.
  nsresult mBlockingErrorCode;

  // True if this feature only contains exception/allowlist rules. Used to
  // sanity-check that such features are listed after the features they may
  // except in the engines pref.
  bool mExceptionOnly;

  // If true, the engine is called with the top-window site as the source
  // site (and third-party-to-top-window as the third-party bit) instead of
  // the loading frame's site. Used by features that expect to key on the top
  // level, rather than the requesting frame- specifically all rules emulating
  // the old behavior.
  bool mUseTopWindowAsSource;

  // The scoped pref that gates this feature's blocking decision. When the
  // scoped pref is set to false, blocking is suppressed for the channel's site.
  // Only consulted on the blocking path.
  Maybe<nsIScopedPrefs::Pref> mReferencedScopedPref;

  // Optional predicate evaluated before classification. When non-null, the
  // feature's engine is skipped for any request where this returns false for
  // the given mode. Allows features to restrict classification to a request
  // subset without needing per-request engine variants.
  bool (*mRequestFilter)(const ContentClassifierRequest&, ClassifyMode);

  // Optional callback invoked just before a matched channel is cancelled, after
  // SetBlockedContent and before aChannel->Cancel(). Null for most features.
  void (*mCancelChannelCallback)(nsIChannel*);
};

enum class InitPhase {
  NotInited,
  InitSucceeded,
  InitFailed,
  ShutdownStarted,
  ShutdownEnded
};

// Aggregated outcome across the engines consulted for one classification.
// Records every engine result that contributed (a match or an exception),
// so the caller can attribute the channel-side annotation / block to the
// right feature definitions.
class ContentClassifierResult {
 public:
  // Aggregation ordering follows the numeric
  // value of nsIContentClassifierService::ProbeStatus: higher values
  // supersede lower ones in Accumulate, so any Exception promotes the
  // aggregate over a Hit, and an Important variant pins the status against
  // later non-Important results.
  using Status = nsIContentClassifierService::ProbeStatus;

  ContentClassifierResult() = default;

  ContentClassifierResult(ContentClassifierResult&&) = default;
  ContentClassifierResult& operator=(ContentClassifierResult&&) = default;
  ContentClassifierResult(const ContentClassifierResult&) = delete;
  ContentClassifierResult& operator=(const ContentClassifierResult&) = delete;

  Status GetStatus() const { return mStatus; }

  bool Hit() const {
    return mStatus == Status::Hit || mStatus == Status::ImportantHit;
  }
  bool Exception() const {
    return mStatus == Status::Exception ||
           mStatus == Status::ImportantException;
  }
  bool Important() const {
    return mStatus == Status::ImportantHit ||
           mStatus == Status::ImportantException;
  }

  const nsTArray<ContentClassifierEngineResult>& EngineResults() const {
    return mEngineResults;
  }

  void Accumulate(ContentClassifierEngineResult aEngineResult);

 private:
  Status mStatus = Status::Miss;
  nsTArray<ContentClassifierEngineResult> mEngineResults;
};

// Snapshot of the four engines prefs (cancel/annotate x normal/PBM)
// captured on the main thread. UpdateFeatures threads this snapshot
// through the main-thread fetch step, the build-thread engine build,
// and the lock-holding install/populate/prune step so every stage in
// the rebuild pipeline sees a consistent view of the pref state, even
// if a later pref change races the rebuild.
struct EnginesPrefsSnapshot {
  EnginesPrefsSnapshot() {
    AppendFeatureNamesFromPref(
        "privacy.trackingprotection.content.protection.engines", mCancel);
    AppendFeatureNamesFromPref(
        "privacy.trackingprotection.content.protection.engines.pbmode",
        mCancelPBM);
    AppendFeatureNamesFromPref(
        "privacy.trackingprotection.content.annotation.engines", mAnnotate);
    AppendFeatureNamesFromPref(
        "privacy.trackingprotection.content.annotation.engines.pbmode",
        mAnnotatePBM);
  }

  // Whether test_block and test_annotate stand in for the trackers engine.
  static bool UseTestTrackersEngines() {
    return Preferences::GetBool(
        "privacy.trackingprotection.content.testing.use_test_trackers_engines",
        false);
  }

  // Appends the feature names listed in the engines pref |aPref|. With
  // UseTestTrackersEngines(), the trackers engine is replaced by test_block in
  // the protection engines prefs and by test_annotate in the annotation ones.
  static void AppendFeatureNamesFromPref(const char* aPref,
                                         nsTArray<nsCString>& aOut) {
    nsAutoCString value;
    Preferences::GetCString(aPref, value);
    const bool useTestEngines = UseTestTrackersEngines();
    const bool isProtectionPref =
        !strcmp(aPref,
                "privacy.trackingprotection.content.protection.engines") ||
        !strcmp(aPref,
                "privacy.trackingprotection.content.protection.engines.pbmode");
    for (const auto& part : value.Split(',')) {
      nsAutoCString name(part);
      name.Trim("\b\t\r\n ");
      if (useTestEngines && name.EqualsLiteral("trackers")) {
        name = isProtectionPref ? "test_block"_ns : "test_annotate"_ns;
      }
      if (!name.IsEmpty() && !aOut.Contains(name)) {
        aOut.AppendElement(name);
      }
    }
  }

  nsTArray<nsCString> mCancel;
  nsTArray<nsCString> mCancelPBM;
  nsTArray<nsCString> mAnnotate;
  nsTArray<nsCString> mAnnotatePBM;
};

class ContentClassifierProbeResult final
    : public nsIContentClassifierProbeResult {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICONTENTCLASSIFIERPROBERESULT

  ContentClassifierProbeResult(const nsACString& aFeatureName, bool aMatched,
                               bool aException, bool aImportant,
                               nsresult aEngineResult)
      : mFeatureName(aFeatureName),
        mMatched(aMatched),
        mException(aException),
        mImportant(aImportant),
        mEngineResult(aEngineResult) {}

 private:
  ~ContentClassifierProbeResult() = default;

  nsCString mFeatureName;
  bool mMatched;
  bool mException;
  bool mImportant;
  nsresult mEngineResult;
};

class ContentClassifierProbeReport final
    : public nsIContentClassifierProbeReport {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICONTENTCLASSIFIERPROBEREPORT

  ContentClassifierProbeReport(
      nsIContentClassifierService::ProbeStatus aStatus,
      nsTArray<RefPtr<nsIContentClassifierProbeResult>>&& aResults)
      : mStatus(aStatus), mResults(std::move(aResults)) {}

 private:
  ~ContentClassifierProbeReport() = default;

  nsIContentClassifierService::ProbeStatus mStatus;
  nsTArray<RefPtr<nsIContentClassifierProbeResult>> mResults;
};

class ContentClassifierService final : public nsIAsyncShutdownBlocker,
                                       public nsIContentClassifierService,
                                       public nsIMemoryReporter {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIASYNCSHUTDOWNBLOCKER
  NS_DECL_NSICONTENTCLASSIFIERSERVICE
  NS_DECL_NSIMEMORYREPORTER

  static already_AddRefed<ContentClassifierService> GetInstance();

  static bool IsEnabled();
  static bool IsInitialized();

  // Returns the static table of content classifier features. The table
  // is the single source of truth for how filter list IDs are grouped
  // into engines and how matches are reported.
  static Span<const ContentClassifierFeature> GetFeatures();

  // Returns the feature with the given name, or Nothing() if no such
  // feature exists in the static table.
  static Maybe<const ContentClassifierFeature&> GetFeatureByName(
      const nsACString& aName);

  // Returns whether the named blocking feature is currently active.
  static bool IsBlockingFeatureActive(const nsACString& aFeatureName,
                                      bool aIsPrivate);

  ContentClassifierResult ClassifyForCancel(
      const ContentClassifierRequest& aRequest);
  ContentClassifierResult ClassifyForAnnotate(
      const ContentClassifierRequest& aRequest);

  [[nodiscard]] net::ChannelBlockDecision MaybeCancelChannel(
      nsIChannel* aChannel, const ContentClassifierResult& aResult);
  void MaybeAnnotateChannel(nsIChannel* aChannel,
                            const ContentClassifierResult& aResult);

 private:
  ContentClassifierService();
  ~ContentClassifierService();

  void Init();
  static void OnPrefChange(const char* aPref, void* aData);
  void InitRSClient();
  void ShutdownRSClient();
  void RemoveBlocker();
  already_AddRefed<nsIAsyncShutdownClient> GetAsyncShutdownBarrier() const;

  // Cancel threads matched_rule across engines so trailing exception engines
  // see an earlier hit. Annotate evaluates every engine in isolation so
  // MaybeAnnotateChannel can attribute matches to every feature whose rules
  // fired, and also classifies top-level documents. Which requests a feature
  // considers in each mode is decided by its mRequestFilter.
  ContentClassifierResult ClassifyWithEngines(
      const nsTArray<RefPtr<ContentClassifierEngine>>& aEngines,
      const ContentClassifierRequest& aRequest, ClassifyMode aMode)
      MOZ_REQUIRES(mLock);

  // Take a fresh pref snapshot, decide which active features need to be
  // (re)built — either because they have no engine yet, or because one of
  // their mListIds appears in aUpdated/aRemoved — and hand the result to
  // UpdateFeatures. Main thread only.
  void ProcessListChanges(const nsTArray<nsCString>& aUpdated,
                          const nsTArray<nsCString>& aRemoved);

  // This rebuilds the given features,
  // according to that preference snapshot. This also means that features that
  // aren't referenced by the current pref snapshot are going to have their
  // engine destroyed. aFeatureVersions holds the version of each entry in
  // aFeatures and aGeneration the update generation, both already bumped
  // by ProcessListChanges.
  void UpdateFeatures(nsTArray<const ContentClassifierFeature*> aFeatures,
                      nsTArray<uint64_t> aFeatureVersions, uint64_t aGeneration,
                      EnginesPrefsSnapshot aPreferenceSnapshot);

  // Put the given engine into the authoritative map. Doesn't update references
  // to this feature's engine elsewhere. See
  // PopulateAllActiveEnginesFromPreferenceSnapshot.
  nsresult InstallEngine(const nsACString& aFeatureName,
                         RefPtr<ContentClassifierEngine>&& aEngine)
      MOZ_REQUIRES(mLock);

  // Get the pointers in each of the arrays used in classification to point to
  // the latest version in the authoritative mEngines map.
  void PopulateAllActiveEnginesFromPreferenceSnapshot(
      const EnginesPrefsSnapshot& aPreferenceSnapshot) MOZ_REQUIRES(mLock);

  // Helper for PopulateAllActiveEnginesFromPreferenceSnapshot.
  void PopulateActiveEngineListFromFeatureNames(
      const nsTArray<nsCString>& aFeatureNames,
      nsTArray<RefPtr<ContentClassifierEngine>>& aEngineList)
      MOZ_REQUIRES(mLock);

  // Remove engines from mEngines that aren't being used by any of the arrays
  // used in classification.
  void PruneInactiveEngines(const EnginesPrefsSnapshot& aPreferenceSnapshot)
      MOZ_REQUIRES(mLock);

  // Get a set of which features are being used in classification
  nsTHashSet<nsCString> ActiveFeatureNames(
      const EnginesPrefsSnapshot& aPreferenceSnapshot);

  // Helper type to grab a list of rules to be built into an engine
  using EngineRulesPromise = MozPromise<nsTArray<nsCString>, nsresult,
                                        /* IsExclusive = */ true>;
  // Grab the list of rules for a given feature. Hits remote settings, unless it
  // is a test_* feature, then it delegates to FetchEngineDataForTestFeature.
  RefPtr<EngineRulesPromise> FetchEngineDataForFeature(
      const ContentClassifierFeature& aFeature);

  // Fetch and parse the resourses for a test_* feature, returning a list of
  // rules to build
  RefPtr<EngineRulesPromise> FetchEngineDataForTestFeature(
      const ContentClassifierFeature& aFeature);

  static StaticRefPtr<ContentClassifierService> sInstance;
  static bool sEnabled;

  mozilla::Mutex mLock MOZ_UNANNOTATED;
  InitPhase mInitPhase MOZ_GUARDED_BY(mLock);

  // Feature-keyed engines built from the new engines/engines.pbmode prefs.
  // Each feature's engine is constructed once from the union of rules in
  // its mListIds.
  nsTHashMap<nsCStringHashKey, RefPtr<ContentClassifierEngine>> mEngines
      MOZ_GUARDED_BY(mLock);

  // Per-feature monotonic version counter, bumped each time
  // ProcessListChanges decides to rebuild that feature's engine. The
  // version is captured into the build closure at dispatch time;
  // when the closure later runs it skips installation for any feature
  // whose recorded version no longer matches the current one (i.e. a
  // newer rebuild has been issued since), so two racing rebuilds
  // can't write each other's stale results.
  nsTHashMap<nsCStringHashKey, uint64_t> mFeatureVersions MOZ_GUARDED_BY(mLock);

  // Global update generation. Bumped under mLock on every
  // ProcessListChanges call (including the empty-features path), under the
  // same lock that decides which features to rebuild. Each
  // closure captures its generation at dispatch time and only runs
  // Populate / Prune / Notify when its generation equals mUpdateGeneration
  // — i.e., no newer ProcessListChanges has been issued. This prevents an older
  // closure with a stale snapshot from clobbering active-engine lists
  // populated by a newer call's snapshot.
  uint64_t mUpdateGeneration MOZ_GUARDED_BY(mLock) = 0;

  // Engines to consult at classify time, split by phase (Cancel/Annotate)
  // and PBM-ness. Refreshed alongside mEngines whenever the engines
  // selection changes.
  nsTArray<RefPtr<ContentClassifierEngine>> mCancelEngines
      MOZ_GUARDED_BY(mLock);
  nsTArray<RefPtr<ContentClassifierEngine>> mCancelEnginesPBM
      MOZ_GUARDED_BY(mLock);
  nsTArray<RefPtr<ContentClassifierEngine>> mAnnotateEngines
      MOZ_GUARDED_BY(mLock);
  nsTArray<RefPtr<ContentClassifierEngine>> mAnnotateEnginesPBM
      MOZ_GUARDED_BY(mLock);

  // RemoteSettings client for fetching filter lists. All reads and
  // writes must happen on the main thread; each call site asserts.
  nsCOMPtr<nsIContentClassifierRemoteSettingsClient> mRSClient;

  // Serial background task queue used for the CPU-heavy half of engine
  // rebuilds (BuildEngineFromRules) plus the lock-holding install /
  // populate / prune phase. Created in Init(); cleared in BlockShutdown.
  // Shutdown does not wait for it to drain: closures already on it are no-ops
  // once mInitPhase leaves InitSucceeded.
  nsCOMPtr<nsISerialEventTarget> mBuildThread;
};

}  // namespace mozilla

#endif  // mozilla_ContentClassifierService_h
