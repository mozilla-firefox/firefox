/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedScriptCache.h"

#include <memory>

#include "ScriptLoadHandler.h"  // ScriptLoadHandler
#include "ScriptLoader.h"       // ScriptLoader
#include "ScriptTrace.h"        // TRACE_FOR_TEST
#include "js/RootingAPI.h"      // JS::MutableHandle
#include "js/Value.h"           // JS::Value
#include "js/experimental/CompileScript.h"  // JS::FrontendContext, JS::NewFrontendContext, JS::DestroyFrontendContext
#include "js/experimental/JSStencil.h"  // JS::GetScriptSourceText
#include "mozilla/HalTypes.h"           // hal::CONTENT_PROCESS_ID_*
#include "mozilla/Maybe.h"              // Maybe, Some, Nothing
#include "mozilla/TaskController.h"     // TaskController, Task
#include "mozilla/dom/ContentChild.h"   // dom::ContentChild
#include "mozilla/dom/ContentParent.h"  // dom::ContentParent
#include "mozilla/glean/DomMetrics.h"   // mozilla::glean::dom::*
#include "nsIMemoryReporter.h"  // nsIMemoryReporter, MOZ_DEFINE_MALLOC_SIZE_OF, RegisterWeakMemoryReporter, UnregisterWeakMemoryReporter, MOZ_COLLECT_REPORT, KIND_HEAP, UNITS_BYTES
#include "nsIPrefBranch.h"   // nsIPrefBranch, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID
#include "nsIPrefService.h"  // NS_PREFSERVICE_CONTRACTID
#include "nsIPrincipal.h"    // nsIPrincipal
#include "nsIPropertyBag2.h"  // nsIPropertyBag2
#include "nsISupportsImpl.h"  // NS_IMPL_ISUPPORTS
#include "nsStringFwd.h"      // nsACString

namespace mozilla::dom {

ScriptHashKey::ScriptHashKey(
    ScriptLoader* aLoader, const JS::loader::ScriptLoadRequest* aRequest,
    mozilla::dom::ReferrerPolicy aReferrerPolicy,
    const JS::loader::ScriptFetchOptions* aFetchOptions,
    const nsCOMPtr<nsIURI> aURI)
    : PLDHashEntryHdr(),
      mURI(aURI),
      mPartitionPrincipal(aLoader->PartitionedPrincipal()),
      mLoaderPrincipal(aLoader->LoaderPrincipal()),
      mKind(aRequest->mKind),
      mCORSMode(aFetchOptions->mCORSMode),
      mReferrerPolicy(aReferrerPolicy) {
  if (mKind == JS::loader::ScriptKind::eClassic) {
    mClassicScriptHintEncoding = aRequest->mClassicScriptHintEncoding;
  }

  MOZ_COUNT_CTOR(ScriptHashKey);
}

ScriptHashKey::ScriptHashKey(const ScriptLoadData& aLoadData)
    : ScriptHashKey(aLoadData.CacheKey()) {}

bool ScriptHashKey::KeyEquals(const ScriptHashKey& aKey) const {
  {
    bool eq;
    if (NS_FAILED(mURI->Equals(aKey.mURI, &eq)) || !eq) {
      return false;
    }
  }

  if (!mPartitionPrincipal->Equals(aKey.mPartitionPrincipal)) {
    return false;
  }

  // NOTE: mLoaderPrincipal is only for the SharedSubResourceCache logic,
  //       not for comparison here.

  if (mKind != aKey.mKind) {
    return false;
  }

  if (mCORSMode != aKey.mCORSMode) {
    return false;
  }

  if (mReferrerPolicy != aKey.mReferrerPolicy) {
    return false;
  }

  // NOTE: module always use UTF-8.
  if (mKind == JS::loader::ScriptKind::eClassic) {
    if (mClassicScriptHintEncoding != aKey.mClassicScriptHintEncoding) {
      return false;
    }
  }

  return true;
}

// The separator after the encoding, inside the stringified hash key.
// The '/' character doesn't appear in the known encoding names.
static constexpr char KeyEncodingSeparator = '/';

void ScriptHashKey::ToStringForLookup(nsACString& aResult) {
  aResult.Truncate();

  aResult.AppendLiteral("SharedScriptCache:");
  switch (mKind) {
    case JS::loader::ScriptKind::eClassic:
      aResult.Append('c');
      break;
    case JS::loader::ScriptKind::eModule:
      aResult.Append('m');
      break;
    case JS::loader::ScriptKind::eEvent:
      aResult.Append('e');
      break;
    case JS::loader::ScriptKind::eImportMap:
      aResult.Append('i');
      break;
    case JS::loader::ScriptKind::eSpeculationRules:
      aResult.Append('s');
      break;
  }

  switch (mCORSMode) {
    case CORS_NONE:
      aResult.Append('n');
      break;
    case CORS_ANONYMOUS:
      aResult.Append('a');
      break;
    case CORS_USE_CREDENTIALS:
      aResult.Append('c');
      break;
  }

  switch (mReferrerPolicy) {
    case ReferrerPolicy::_empty:
      aResult.Append('_');
      break;
    case ReferrerPolicy::No_referrer:
      aResult.Append('n');
      break;
    case ReferrerPolicy::No_referrer_when_downgrade:
      aResult.Append('d');
      break;
    case ReferrerPolicy::Origin:
      aResult.Append('o');
      break;
    case ReferrerPolicy::Origin_when_cross_origin:
      aResult.Append('c');
      break;
    case ReferrerPolicy::Unsafe_url:
      aResult.Append('u');
      break;
    case ReferrerPolicy::Same_origin:
      aResult.Append('s');
      break;
    case ReferrerPolicy::Strict_origin:
      aResult.Append('S');
      break;
    case ReferrerPolicy::Strict_origin_when_cross_origin:
      aResult.Append('C');
      break;
  }

  if (mClassicScriptHintEncoding) {
    nsAutoCString name;
    mClassicScriptHintEncoding->Name(name);
    aResult.Append(name);
  }
  aResult.Append(KeyEncodingSeparator);

  nsAutoCString partitionPrincipal;
  BasePrincipal::Cast(mPartitionPrincipal)->ToJSON(partitionPrincipal);
  aResult.Append(partitionPrincipal);
}

/* static */
Maybe<ScriptHashKey> ScriptHashKey::FromStringsForLookup(
    const nsACString& aKey, const nsACString& aURI) {
  if (aKey.Length() < 22) {
    return Nothing();
  }

  if (Substring(aKey, 0, 18) != "SharedScriptCache:") {
    return Nothing();
  }

  JS::loader::ScriptKind kind;
  char kindChar = aKey[18];
  if (kindChar == 'c') {
    kind = JS::loader::ScriptKind::eClassic;
  } else if (kindChar == 'm') {
    kind = JS::loader::ScriptKind::eModule;
  } else if (kindChar == 'e') {
    kind = JS::loader::ScriptKind::eEvent;
  } else if (kindChar == 'i') {
    kind = JS::loader::ScriptKind::eImportMap;
  } else if (kindChar == 's') {
    kind = JS::loader::ScriptKind::eSpeculationRules;
  } else {
    return Nothing();
  }

  CORSMode corsMode;
  char corsModeChar = aKey[19];
  if (corsModeChar == 'n') {
    corsMode = CORS_NONE;
  } else if (corsModeChar == 'a') {
    corsMode = CORS_ANONYMOUS;
  } else if (corsModeChar == 'c') {
    corsMode = CORS_USE_CREDENTIALS;
  } else {
    return Nothing();
  }

  mozilla::dom::ReferrerPolicy referrerPolicy;
  char referrerPolicyChar = aKey[20];
  if (referrerPolicyChar == '_') {
    referrerPolicy = ReferrerPolicy::_empty;
  } else if (referrerPolicyChar == 'n') {
    referrerPolicy = ReferrerPolicy::No_referrer;
  } else if (referrerPolicyChar == 'd') {
    referrerPolicy = ReferrerPolicy::No_referrer_when_downgrade;
  } else if (referrerPolicyChar == 'o') {
    referrerPolicy = ReferrerPolicy::Origin;
  } else if (referrerPolicyChar == 'c') {
    referrerPolicy = ReferrerPolicy::Origin_when_cross_origin;
  } else if (referrerPolicyChar == 'u') {
    referrerPolicy = ReferrerPolicy::Unsafe_url;
  } else if (referrerPolicyChar == 's') {
    referrerPolicy = ReferrerPolicy::Same_origin;
  } else if (referrerPolicyChar == 'S') {
    referrerPolicy = ReferrerPolicy::Strict_origin;
  } else if (referrerPolicyChar == 'C') {
    referrerPolicy = ReferrerPolicy::Strict_origin_when_cross_origin;
  } else {
    return Nothing();
  }

  static constexpr int32_t EncodingStartPos = 21;

  int32_t sep = aKey.FindChar(KeyEncodingSeparator, EncodingStartPos);
  if (sep == kNotFound) {
    return Nothing();
  }

  nsCOMPtr<nsIPrincipal> partitionPrincipal =
      BasePrincipal::FromJSON(Substring(aKey, sep + 1));
  if (!partitionPrincipal) {
    return Nothing();
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURI);
  if (NS_FAILED(rv)) {
    return Nothing();
  }

  NS_ConvertUTF8toUTF16 classicScriptHintCharset(
      Substring(aKey, EncodingStartPos, sep - EncodingStartPos));
  const Encoding* classicScriptHintEncoding = nullptr;
  if (!classicScriptHintCharset.IsEmpty()) {
    classicScriptHintEncoding = Encoding::ForLabel(classicScriptHintCharset);
  }

  return Some(ScriptHashKey(uri, partitionPrincipal, kind, corsMode,
                            referrerPolicy, classicScriptHintEncoding));
}

NS_IMPL_ISUPPORTS(ScriptLoadData, nsISupports)

ScriptLoadData::ScriptLoadData(ScriptLoader* aLoader,
                               JS::loader::ScriptLoadRequest* aRequest,
                               CacheExpirationTime aExpirationTime,
                               JS::loader::LoadedScript* aLoadedScript)
    : mExpirationTime(aExpirationTime),
      mLoader(aLoader),
      mKey(aLoader, aRequest, aRequest->ReferrerPolicy(),
           aRequest->FetchOptions(), aLoadedScript->GetURI()),
      mLoadedScript(aLoadedScript),
      mNetworkMetadata(aRequest->mNetworkMetadata) {}

NS_IMPL_ISUPPORTS(SharedScriptCache, nsIMemoryReporter, nsIObserver)

MOZ_DEFINE_MALLOC_SIZE_OF(SharedScriptCacheMallocSizeOf)

SharedScriptCache::SharedScriptCache() = default;

void SharedScriptCache::Init() {
  RegisterWeakMemoryReporter(this);

  // URL classification (tracking protection etc) are handled inside
  // nsHttpChannel.
  // The cache reflects the policy for whether to block or not, and once
  // the policy is modified, we should discard the cache, to avoid running
  // a cached script which is supposed to be blocked.
  auto ClearCache = [](const char*, void*) { Clear(); };
  Preferences::RegisterPrefixCallback(ClearCache, "urlclassifier.");
  Preferences::RegisterCallback(ClearCache,
                                "privacy.trackingprotection.enabled");
}

SharedScriptCache::~SharedScriptCache() {
  UnregisterWeakMemoryReporter(this);
  ClearDiskCacheTimer();
}

bool SharedScriptCache::ShouldIgnoreMemoryPressure() {
  // During the automated testing, we need to ignore the memory pressure,
  // in order to get the deterministic result.
  return !StaticPrefs::
      dom_script_loader_experimental_navigation_cache_check_memory_pressure();
}

void SharedScriptCache::ClearInProcessForMemoryPressure() {
  for (auto iter = mComplete.Iter(); !iter.Done(); iter.Next()) {
    iter.Data().mResource->InvalidateCachedStencil();
  }

  SharedSubResourceCache::ClearInProcessForMemoryPressure();
}

void SharedScriptCache::LoadCompleted(SharedScriptCache* aCache,
                                      ScriptLoadData& aData) {}

NS_IMETHODIMP
SharedScriptCache::CollectReports(nsIHandleReportCallback* aHandleReport,
                                  nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT("explicit/js-non-window/cache", KIND_HEAP, UNITS_BYTES,
                     SharedScriptCacheMallocSizeOf(this) +
                         SizeOfExcludingThis(SharedScriptCacheMallocSizeOf),
                     "Memory used for SharedScriptCache to share script "
                     "across documents");
  return NS_OK;
}

/* static */
void SharedScriptCache::Clear(const Maybe<bool>& aChrome,
                              const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                              const Maybe<nsCString>& aSchemelessSite,
                              const Maybe<OriginAttributesPattern>& aPattern,
                              const Maybe<nsCString>& aURL) {
  using ContentParent = dom::ContentParent;

  if (XRE_IsParentProcess()) {
    for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
      (void)cp->SendClearScriptCache(aChrome, aPrincipal, aSchemelessSite,
                                     aPattern, aURL);
    }
  }

  if (sSingleton) {
    sSingleton->ClearInProcess(aChrome, aPrincipal, aSchemelessSite, aPattern,
                               aURL);
  }
}

/* static */
void SharedScriptCache::Invalidate() {
  using ContentParent = dom::ContentParent;

  if (XRE_IsParentProcess()) {
    for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
      (void)cp->SendInvalidateScriptCache();
    }
  }

  if (sSingleton) {
    sSingleton->InvalidateInProcess();
  }
  TRACE_FOR_TEST_0("memorycache:invalidate");
}

/* static */
bool SharedScriptCache::GetCachedScriptSource(
    JSContext* aCx, const nsACString& aKey, const nsACString& aURI,
    JS::MutableHandle<JS::Value> aRetval) {
  if (!sSingleton) {
    aRetval.setUndefined();
    return true;
  }

  Maybe<ScriptHashKey> maybeKey =
      ScriptHashKey::FromStringsForLookup(aKey, aURI);
  if (!maybeKey) {
    aRetval.setUndefined();
    return true;
  }

  JS::Stencil* stencil = nullptr;
  if (auto lookup = sSingleton->mComplete.Lookup(*maybeKey)) {
    JS::loader::LoadedScript* loadedScript = lookup.Data().mResource;
    if (loadedScript->IsInvalidatedCachedStencil()) {
      aRetval.setUndefined();
      return true;
    }

    // NOTE: We don't check the SRIMetadata here, because this is not a
    //       request from <script> element.
    stencil = loadedScript->GetCachedStencil();
  } else {
    aRetval.setUndefined();
    return true;
  }

  if (!JS::GetScriptSourceText(aCx, stencil, aRetval)) {
    return false;
  }

  return true;
}

void SharedScriptCache::InvalidateInProcess() {
  for (auto iter = mComplete.Iter(); !iter.Done(); iter.Next()) {
    if (!iter.Data().mResource->HasCacheEntryId()) {
      iter.Remove();
    } else {
      iter.Data().mResource->SetDirty();
    }
  }
}

/* static */
void SharedScriptCache::PrepareForLastCC() {
  if (sSingleton) {
    sSingleton->mComplete.Clear();
    sSingleton->mPending.Clear();
    sSingleton->mLoading.Clear();
  }
}

static bool ShouldSave(JS::loader::LoadedScript* aLoadedScript,
                       JS::Stencil* aStencil,
                       ScriptLoader::DiskCacheStrategy aStrategy) {
  if (!aLoadedScript->HasDiskCacheReference()) {
    return false;
  }

  if (!aLoadedScript->HasSRI()) {
    return false;
  }

  if (aStrategy.mHasSourceLengthMin) {
    size_t len = JS::GetScriptSourceLength(aStencil);
    if (len < aStrategy.mSourceLengthMin) {
      return false;
    }
  }

  if (aStrategy.mHasFetchCountMin) {
    if (aLoadedScript->mFetchCount < aStrategy.mFetchCountMin) {
      return false;
    }
  }

  return true;
}

bool SharedScriptCache::MaybeScheduleUpdateDiskCache() {
  auto strategy = ScriptLoader::GetDiskCacheStrategy();
  if (strategy.mIsDisabled) {
    return false;
  }

  bool hasSaveable = false;
  for (auto iter = mComplete.Iter(); !iter.Done(); iter.Next()) {
    JS::loader::LoadedScript* loadedScript = iter.Data().mResource;
    if (loadedScript->IsInvalidatedCachedStencil()) {
      continue;
    }

    JS::Stencil* stencil = loadedScript->GetCachedStencil();
    if (ShouldSave(loadedScript, stencil, strategy)) {
      hasSaveable = true;
      break;
    }
  }

  if (!hasSaveable) {
    return false;
  }

  SetDiskCacheTimer();
  return true;
}

void SharedScriptCache::SetDiskCacheTimer() {
  if (mDiskCacheTimer) {
    mRetryDiskCacheTimer = true;
    return;
  }

  auto result = NS_NewTimerWithFuncCallback(
      [](nsITimer*, void* aClosure) {
        auto* self = static_cast<SharedScriptCache*>(aClosure);
        self->OnDiskCacheTimer();
      },
      this, StaticPrefs::dom_script_loader_disk_cache_delay_ms(),
      nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY,
      "SharedScriptCache::DiskCacheTimer"_ns);

  if (result.isErr()) {
    return;
  }

  mDiskCacheTimer = result.unwrap();
}

void SharedScriptCache::ClearDiskCacheTimer() {
  if (!mDiskCacheTimer) {
    return;
  }

  mDiskCacheTimer->Cancel();
  mDiskCacheTimer = nullptr;
}

void SharedScriptCache::OnDiskCacheTimer() {
  mDiskCacheTimer = nullptr;
  if (mRetryDiskCacheTimer) {
    mRetryDiskCacheTimer = false;
    SetDiskCacheTimer();
    return;
  }

  UpdateDiskCache();
}

class ScriptEncodeAndCompressionTask : public mozilla::Task {
 public:
  ScriptEncodeAndCompressionTask()
      : Task(Kind::OffMainThreadOnly, EventQueuePriority::Idle) {}
  virtual ~ScriptEncodeAndCompressionTask() = default;

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("ScriptEncodeAndCompressionTask");
    return true;
  }
#endif

  TaskResult Run() override {
    SharedScriptCache::Get()->EncodeAndCompress();
    return TaskResult::Complete;
  }
};

class ScriptSaveTask : public mozilla::Task {
 public:
  ScriptSaveTask() : Task(Kind::MainThreadOnly, EventQueuePriority::Idle) {}
  virtual ~ScriptSaveTask() = default;

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("ScriptSaveTask");
    return true;
  }
#endif

  TaskResult Run() override {
    SharedScriptCache::Get()->SaveToDiskCache();
    return TaskResult::Complete;
  }
};

void SharedScriptCache::UpdateDiskCache() {
  auto strategy = ScriptLoader::GetDiskCacheStrategy();
  if (strategy.mIsDisabled) {
    return;
  }

  mozilla::MutexAutoLock lock(mEncodeMutex);

  if (!mEncodeItems.empty()) {
    return;
  }

  for (auto iter = mComplete.Iter(); !iter.Done(); iter.Next()) {
    JS::loader::LoadedScript* loadedScript = iter.Data().mResource;
    if (loadedScript->IsInvalidatedCachedStencil()) {
      continue;
    }

    RefPtr<JS::Stencil> stencil = loadedScript->GetCachedStencil();
    if (!ShouldSave(loadedScript, stencil, strategy)) {
      continue;
    }

    if (!mEncodeItems.emplaceBack(stencil, std::move(loadedScript->SRI()),
                                  loadedScript)) {
      continue;
    }
  }

  if (mEncodeItems.empty()) {
    return;
  }

  RefPtr<ScriptEncodeAndCompressionTask> encodeTask =
      new ScriptEncodeAndCompressionTask();
  RefPtr<ScriptSaveTask> saveTask = new ScriptSaveTask();
  saveTask->AddDependency(encodeTask);

  TaskController::Get()->AddTask(encodeTask.forget());
  TaskController::Get()->AddTask(saveTask.forget());
}

void SharedScriptCache::EncodeAndCompress() {
  JS::FrontendContext* fc = JS::NewFrontendContext();
  if (!fc) {
    return;
  }

  mozilla::MutexAutoLock lock(mEncodeMutex);

  for (auto& item : mEncodeItems) {
    if (!ScriptLoader::EncodeAndCompress(fc, item.mLoadedScript, item.mStencil,
                                         item.mSRI, item.mCompressed)) {
      item.mCompressed.clear();
    }
  }

  JS::DestroyFrontendContext(fc);
}

void SharedScriptCache::SaveToDiskCache() {
  MOZ_ASSERT(NS_IsMainThread());

  mozilla::MutexAutoLock lock(mEncodeMutex);

  for (const auto& item : mEncodeItems) {
    if (item.mCompressed.empty()) {
      item.mLoadedScript->DropDiskCacheReference();
      item.mLoadedScript->DropSRIOrSRIAndSerializedStencil();
      TRACE_FOR_TEST(item.mLoadedScript, "diskcache:failed");
      continue;
    }

    if (!ScriptLoader::SaveToDiskCache(item.mLoadedScript, item.mCompressed)) {
      item.mLoadedScript->DropDiskCacheReference();
      item.mLoadedScript->DropSRIOrSRIAndSerializedStencil();
      TRACE_FOR_TEST(item.mLoadedScript, "diskcache:failed");
    }

    item.mLoadedScript->DropDiskCacheReference();
    item.mLoadedScript->DropSRIOrSRIAndSerializedStencil();
    TRACE_FOR_TEST(item.mLoadedScript, "diskcache:saved");
  }

  mEncodeItems.clear();
}

void SharedScriptCache::OnEntryInserted() { mEntryInserted++; }

void SharedScriptCache::OnEntryEverHit() { mEntryEverHit++; }

void SharedScriptCache::UpdateEverHitTelemetry() {
  if (mEntryInserted == 0) {
    return;
  }

  uint32_t rate = mEntryEverHit * 100 / mEntryInserted;
  if (rate == mLastEverHitRatio) {
    return;
  }
  mLastEverHitRatio = rate;

  if (XRE_IsParentProcess()) {
    if (!EnsureEverHitMap()) {
      return;
    }
    (void)mEverHitMap->put(hal::CONTENT_PROCESS_ID_MAIN, rate);
    return;
  }

  if (!XRE_IsContentProcess()) {
    return;
  }
  auto* cc = ContentChild::GetSingleton();
  if (!cc) {
    return;
  }

  uint64_t childId = cc->GetID();
  cc->SendUpdateScriptCacheEverHitTelemetry(childId, rate);
}

/* static */
void SharedScriptCache::RecvUpdateEverHitTelemetry(const uint64_t& aChildId,
                                                   const uint32_t& aRate) {
  MOZ_ASSERT(XRE_IsParentProcess());

  SharedScriptCache* self = SharedScriptCache::Get();
  if (!self) {
    return;
  }
  if (!self->EnsureEverHitMap()) {
    return;
  }
  (void)self->mEverHitMap->put(aChildId, aRate);
}

bool SharedScriptCache::EnsureEverHitMap() {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (mPreparedEverHitMap) {
    return !!mEverHitMap;
  }
  mPreparedEverHitMap = true;

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (!os) {
    return false;
  }
  os->AddObserver(this, "ipc:content-shutdown", /* ownsWeak= */ false);
  os->AddObserver(this, "profile-before-change", /* ownsWeak= */ false);

  mEverHitMap = std::make_unique<EverHitMapType>();
  return !!mEverHitMap;
}

// When a content process gets shutdown, accumulate the latest cache-hit ratio
// to the telemetry, and remove the entry from the map, to avoid keeping the
// data unnecessarily longer.
void SharedScriptCache::OnContentShutdown(nsISupports* aSubject) {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!mEverHitMap) {
    return;
  }

  nsCOMPtr<nsIPropertyBag2> props = do_QueryInterface(aSubject);
  if (!props) {
    return;
  }

  uint64_t childID = hal::CONTENT_PROCESS_ID_UNKNOWN;
  props->GetPropertyAsUint64(u"childID"_ns, &childID);
  if (childID == hal::CONTENT_PROCESS_ID_UNKNOWN) {
    return;
  }

  auto p = mEverHitMap->lookup(childID);
  if (!p) {
    return;
  }
  AccumulateEverHitTelemetry(p->value());
  mEverHitMap->remove(p);
}

// When the parent process is getting shutdown, accumulate the latest cache-hit
// ratio of all processes to the telemetry.
//
// We perform this at ShutdownPhase::AppShutdown phase.
// Glean submits the telemetry at ShutdownPhase::AppShutdownTelemetry, which
// is after the ShutdownPhase::AppShutdown phase.
void SharedScriptCache::OnProfileBeforeChange() {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!mEverHitMap) {
    return;
  }

  for (auto iter = mEverHitMap->iter(); !iter.done(); iter.next()) {
    AccumulateEverHitTelemetry(iter.get().value());
  }
  mEverHitMap->clear();
  mEverHitMap.reset();
}

void SharedScriptCache::AccumulateEverHitTelemetry(uint32_t aRate) {
  MOZ_ASSERT(XRE_IsParentProcess());

  using namespace mozilla::glean::dom;

  // Skip this function if we are not running telemetry.
  if (!mozilla::Telemetry::CanRecordExtended()) {
    return;
  }

  script_memory_cache_ever_hit.AccumulateSingleSample(aRate);
}

}  // namespace mozilla::dom
