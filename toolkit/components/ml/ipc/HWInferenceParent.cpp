/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPtr.h"
#include "nsTHashSet.h"
#include "HWInferenceParent.h"
#include "HWInferenceManagerParent.h"
#include "mozilla/dom/Blob.h"
#include "mozilla/dom/BlobBinding.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/ipc/UtilityProcessParent.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/FileBlobImpl.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/Blob.h"
#include "mozilla/dom/BlobBinding.h"
#include "mozilla/ErrorNames.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "mozilla/ErrorResult.h"
#include "nsID.h"
#include "nsString.h"
#include "nsFmtString.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsComponentManagerUtils.h"
#include "nsIMLModelHub.h"
#include "nsIMLModelResolver.h"
#include "nsIObserverService.h"
#include "nsIWritablePropertyBag2.h"
#include "nsServiceManagerUtils.h"
#include "prio.h"
#include "private/pprio.h"

#ifdef XP_WIN
#  include <windows.h>
#endif

namespace mozilla::hwinference {

extern LazyLogModule gHWInferenceLog;
#define LOGE(...) MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Error, __VA_ARGS__)
#define LOGD(...) MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Debug, __VA_ARGS__)
#define LOGV(...) MOZ_LOG_FMT(gHWInferenceLog, LogLevel::Verbose, __VA_ARGS__)

StaticRefPtr<HWInferenceParent> HWInferenceParent::sInstance;

static StaticAutoPtr<nsTHashSet<nsCString>> sMockInstalledModels;

static nsCString MockModelKey(const nsACString& aModel,
                              const nsACString& aRevision,
                              const nsACString& aFilename) {
  return nsFmtCString("{}/{}/{}", aModel, aRevision, aFilename);
}

// Expands aId to a concrete ModelHub artifact via the resolver registered for
// aTask (contract id "@mozilla.org/ml/model-resolver;1?task=<task>"). Returns
// that resolver, so a caller needing more of it (e.g. AuthorizeDownload) does
// not have to look it up again, or nullptr if aTask has no resolver or aId is
// not one of its ids.
static already_AddRefed<nsIMLModelResolver> ResolveModelId(
    const nsACString& aTask, const nsACString& aId, nsCString& aEngine,
    nsCString& aModel, nsCString& aRevision, nsCString& aFilename) {
  nsFmtCString contractId("@mozilla.org/ml/model-resolver;1?task={}", aTask);
  nsCOMPtr<nsIMLModelResolver> resolver = do_GetService(contractId.get());
  if (!resolver) {
    LOGE("ResolveModelId - no resolver registered for task {}", aTask);
    return nullptr;
  }

  nsresult rv = resolver->Resolve(aId, aEngine, aModel, aRevision, aFilename);
  if (NS_FAILED(rv)) {
    LOGE("ResolveModelId - id {} not recognized for task {}", aId, aTask);
    return nullptr;
  }
  return resolver.forget();
}

// Reports a download's progress and its outcome: ModelHub always drives both
// together, and the completion side needs to emit a final progress
// notification, so this is a single object implementing both interfaces.
// Progress is broadcast as "ml-model-download-progress", tagged with the
// download's progress token so an observer can tell downloads apart.
class ModelDownloadCallbacks final
    : public nsIMLModelDownloadProgressCallback,
      public nsIMLModelDownloadCompletionCallback {
 public:
  NS_DECL_ISUPPORTS

  ModelDownloadCallbacks(const nsACString& aModel,
                         const nsAString& aProgressToken,
                         HWInferenceParent::InstallModelResolver&& aResolver)
      : mModel(aModel),
        mProgressToken(aProgressToken),
        mResolver(std::move(aResolver)) {}

  NS_IMETHOD OnProgress(int32_t aProgress, int64_t aCurrentLoaded,
                        int64_t aTotalLoaded, int64_t aTotal) override {
    LOGV("{} - model={} progress={}% current={} total loaded={} total={}",
         __func__, mModel, aProgress, aCurrentLoaded, aTotalLoaded, aTotal);
    // aProgress is already a whole percentage (0-100), reported for every
    // chunk received, which for a model that can be hundreds of megabytes is
    // far more often than it actually changes.
    if (aProgress == mLastNotifiedProgress) {
      return NS_OK;
    }
    mLastNotifiedProgress = aProgress;
    Notify(aProgress, aCurrentLoaded, aTotalLoaded, aTotal, false, true);
    return NS_OK;
  }

  NS_IMETHOD OnSuccess(const nsAString& aModel,
                       const nsAString& aRevision) override {
    LOGD("{} - model={} revision={}", __func__, NS_ConvertUTF16toUTF8(aModel),
         NS_ConvertUTF16toUTF8(aRevision));
    Notify(100, 0, 0, 0, true, true);
    mResolver(true);
    return NS_OK;
  }

  NS_IMETHOD OnError(const nsAString& aError) override {
    LOGE("{} - Error when downloading {}", __func__,
         NS_ConvertUTF16toUTF8(aError));
    Notify(0, 0, 0, 0, true, false);
    mResolver(false);
    return NS_OK;
  }

 private:
  ~ModelDownloadCallbacks() = default;

  void Notify(int32_t aProgress, int64_t aCurrentLoaded, int64_t aTotalLoaded,
              int64_t aTotal, bool aDone, bool aOk) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (!obs) {
      return;
    }

    nsCOMPtr<nsIWritablePropertyBag2> props =
        do_CreateInstance("@mozilla.org/hash-property-bag;1");
    props->SetPropertyAsAString(u"token"_ns, mProgressToken);
    props->SetPropertyAsInt32(u"progress"_ns, aProgress);
    props->SetPropertyAsInt64(u"currentLoaded"_ns, aCurrentLoaded);
    props->SetPropertyAsInt64(u"totalLoaded"_ns, aTotalLoaded);
    props->SetPropertyAsInt64(u"total"_ns, aTotal);
    props->SetPropertyAsBool(u"done"_ns, aDone);
    props->SetPropertyAsBool(u"ok"_ns, aOk);
    obs->NotifyObservers(props, "ml-model-download-progress", nullptr);
  }

  nsCString mModel;
  nsString mProgressToken;
  HWInferenceParent::InstallModelResolver mResolver;
  int32_t mLastNotifiedProgress = -1;
};

NS_IMPL_ISUPPORTS(ModelDownloadCallbacks, nsIMLModelDownloadProgressCallback,
                  nsIMLModelDownloadCompletionCallback)

/* static */
RefPtr<HWInferenceParent> HWInferenceParent::GetSingleton() {
  AssertIsOnMainThread();

  // Evict an instance whose process is already gone. Its PHWInference channel
  // is separate from PUtilityProcess, so it keeps reporting CanSend() until
  // the peer actually dies and the channel errors, one main-thread dispatch
  // later. Handing it out in that window would make StartUtility take its
  // CanSend() fast path and resolve success on a doomed actor rather than
  // relaunching. Checked here rather than at teardown so it covers an
  // unexpected process death too, not just CleanShutdown.
  if (sInstance && sInstance->CanSend()) {
    RefPtr<ipc::UtilityProcessManager> upm =
        ipc::UtilityProcessManager::GetIfExists();
    if (!upm || !upm->Process(ipc::SandboxingKind::HW_INFERENCE)) {
      LOGD("{} - evicting stale instance", __func__);
      RefPtr<HWInferenceParent> stale = sInstance;
      sInstance = nullptr;
      // Synchronously runs ActorDestroy, so CanSend() is false on return.
      stale->Close();
    }
  }

  if (!sInstance) {
    sInstance = new HWInferenceParent();
    ClearOnShutdown(&sInstance);
  }
  return sInstance;
}

void HWInferenceParent::ActorDestroy(ActorDestroyReason aReason) {
  LOGD("{}", __func__);
  // Only clear ourselves: a late ActorDestroy from a superseded instance must
  // not evict the replacement created after it.
  if (sInstance == this) {
    sInstance = nullptr;
  }
}

nsresult HWInferenceParent::BindToUtilityProcess(
    const RefPtr<ipc::UtilityProcessParent>& aUtilityParent) {
  LOGD("{}", __func__);
  Endpoint<hwinference::PHWInferenceParent> parentEnd;
  Endpoint<hwinference::PHWInferenceChild> childEnd;
  MOZ_ALWAYS_SUCCEEDS(PHWInference::CreateEndpoints(
      ipc::EndpointProcInfo::Current(), aUtilityParent->OtherEndpointProcInfo(),
      &parentEnd, &childEnd));

  LOGD("Sending StartHWInferenceService to utility process");
  if (!aUtilityParent->SendStartHWInferenceService(std::move(childEnd))) {
    LOGE("Failed to send StartHWInferenceService");
    MOZ_ASSERT(false, "StartHWInference service failure");
    return NS_ERROR_FAILURE;
  }

  LOGD("StartHWInferenceService sent successfully, binding parent endpoint");
  MOZ_ALWAYS_TRUE(parentEnd.Bind(this));
  return NS_OK;
}

mozilla::ipc::IPCResult HWInferenceParent::RecvIsModelAvailable(
    nsCString&& aTask, nsCString&& aId, IsModelAvailableResolver&& aResolver) {
  LOGD("{}: task={} id={}", __func__, aTask, aId);

  nsCString engine, model, revision, filename;
  nsCOMPtr<nsIMLModelResolver> resolver =
      ResolveModelId(aTask, aId, engine, model, revision, filename);
  if (!resolver) {
    aResolver(false);
    return IPC_OK();
  }

  if (StaticPrefs::browser_ml_modelHub_testing()) {
    bool available =
        sMockInstalledModels &&
        sMockInstalledModels->Contains(MockModelKey(model, revision, filename));
    LOGD("{} - testing mock: available={}", __func__, available);
    aResolver(available);
    return IPC_OK();
  }

  nsCOMPtr<nsIMLModelHub> modelHubService =
      do_GetService("@mozilla.org/ml-modelhub;1");

  if (!modelHubService) {
    LOGE("{} - Failed to get ModelHub XPCOM service", __func__);
    aResolver(false);
    return IPC_OK();
  }

  RefPtr<dom::Promise> promise;
  nsresult rv = modelHubService->IsModelAvailable(
      engine, model, revision, filename, getter_AddRefs(promise));

  if (NS_FAILED(rv) || !promise) {
    LOGE("{}  ERROR: ModelHub call failed with nsresult={}", __func__, rv);
    aResolver(false);
    return IPC_OK();
  }

  (void)promise->AddCallbacksWithCycleCollectedArgs(
      [aResolver](JSContext* aCx, JS::Handle<JS::Value> aArg,
                  ErrorResult& aRv) { aResolver(JS::ToBoolean(aArg)); },
      [aResolver](JSContext* aCx, JS::Handle<JS::Value> aArg,
                  ErrorResult& aRv) { aResolver(false); });

  return IPC_OK();
}

mozilla::ipc::IPCResult HWInferenceParent::RecvIsModelInstalled(
    nsCString&& aTask, nsCString&& aId, IsModelInstalledResolver&& aResolver) {
  LOGD("{}: task={} id={}", __func__, aTask, aId);

  nsCString engine, model, revision, filename;
  nsCOMPtr<nsIMLModelResolver> resolver =
      ResolveModelId(aTask, aId, engine, model, revision, filename);
  if (!resolver) {
    aResolver(false);
    return IPC_OK();
  }

  if (StaticPrefs::browser_ml_modelHub_testing()) {
    bool installed =
        sMockInstalledModels &&
        sMockInstalledModels->Contains(MockModelKey(model, revision, filename));
    LOGD("{} - testing mock: installed={}", __func__, installed);
    aResolver(installed);
    return IPC_OK();
  }

  nsCOMPtr<nsIMLModelHub> modelHubService =
      do_GetService("@mozilla.org/ml-modelhub;1");

  if (!modelHubService) {
    LOGE("{} - Failed to get ModelHub XPCOM service", __func__);
    aResolver(false);
    return IPC_OK();
  }

  RefPtr<dom::Promise> promise;
  nsresult rv = modelHubService->IsModelInstalled(
      engine, model, revision, filename, getter_AddRefs(promise));

  if (NS_FAILED(rv) || !promise) {
    LOGE("{}  ERROR: ModelHub call failed with nsresult={}", __func__, rv);
    aResolver(false);
    return IPC_OK();
  }

  (void)promise->AddCallbacksWithCycleCollectedArgs(
      [aResolver](JSContext* aCx, JS::Handle<JS::Value> aArg,
                  ErrorResult& aRv) { aResolver(JS::ToBoolean(aArg)); },
      [aResolver](JSContext* aCx, JS::Handle<JS::Value> aArg,
                  ErrorResult& aRv) { aResolver(false); });

  return IPC_OK();
}

// Performs the actual model download (or testing-mock install) and resolves
// aResolver with the outcome. Honors the testing mock. Only reached after the
// task's resolver has authorized the download.
static void PerformModelInstall(
    const nsCString& aEngine, const nsCString& aTask, const nsCString& aModel,
    const nsCString& aRevision, const nsCString& aFilename,
    const nsString& aProgressToken,
    HWInferenceParent::InstallModelResolver&& aResolver) {
  if (StaticPrefs::browser_ml_modelHub_testing()) {
    if (!sMockInstalledModels) {
      sMockInstalledModels = new nsTHashSet<nsCString>();
      ClearOnShutdown(&sMockInstalledModels);
    }
    sMockInstalledModels->Insert(MockModelKey(aModel, aRevision, aFilename));
    LOGD("PerformModelInstall - testing mock: installed {}",
         MockModelKey(aModel, aRevision, aFilename));
    aResolver(true);
    return;
  }

  nsCOMPtr<nsIMLModelHub> modelHubService =
      do_GetService("@mozilla.org/ml-modelhub;1");

  if (!modelHubService) {
    LOGE("PerformModelInstall - Failed to get ModelHub XPCOM service");
    aResolver(false);
    return;
  }

  nsTArray<nsCString> files;
  files.AppendElement(aFilename);

  auto callbacks = MakeRefPtr<ModelDownloadCallbacks>(aModel, aProgressToken,
                                                      std::move(aResolver));

  nsString downloadSessionId;
  nsresult rv = modelHubService->DownloadModel(
      aEngine, aTask, aModel, aRevision, files, aProgressToken, callbacks,
      callbacks, downloadSessionId);

  if (NS_FAILED(rv) || downloadSessionId.IsEmpty()) {
    LOGE(
        "PerformModelInstall - ERROR: ModelHub DownloadModel call failed "
        "with nsresult={}",
        rv);
    callbacks->OnError(u"Failed to start download"_ns);
    return;
  }
  LOGD(
      "PerformModelInstall - download started successfully with session ID: {}",
      NS_ConvertUTF16toUTF8(downloadSessionId));
}

// Bridges nsIMLModelResolver::AuthorizeDownload's decision back to the pending
// InstallModel: on allow it performs the download, on deny it resolves the
// install false.
class ModelDownloadAuthorizationCallback final
    : public nsIMLModelDownloadAuthorizationCallback {
 public:
  NS_DECL_ISUPPORTS

  ModelDownloadAuthorizationCallback(
      const nsACString& aEngine, const nsACString& aTask,
      const nsACString& aModel, const nsACString& aRevision,
      const nsACString& aFilename, const nsAString& aProgressToken,
      HWInferenceParent::InstallModelResolver&& aResolver)
      : mEngine(aEngine),
        mTask(aTask),
        mModel(aModel),
        mRevision(aRevision),
        mFilename(aFilename),
        mProgressToken(aProgressToken),
        mResolver(std::move(aResolver)) {}

  NS_IMETHOD Resolve(bool aAllow) override {
    if (!mResolver) {
      return NS_OK;
    }
    auto resolver = std::move(mResolver);
    mResolver = nullptr;
    if (!aAllow) {
      LOGD("ModelDownloadAuthorizationCallback - download of {} not authorized",
           mModel);
      resolver(false);
      return NS_OK;
    }
    PerformModelInstall(mEngine, mTask, mModel, mRevision, mFilename,
                        mProgressToken, std::move(resolver));
    return NS_OK;
  }

 private:
  ~ModelDownloadAuthorizationCallback() {
    if (mResolver) {
      mResolver(false);
    }
  }

  nsCString mEngine;
  nsCString mTask;
  nsCString mModel;
  nsCString mRevision;
  nsCString mFilename;
  nsString mProgressToken;
  HWInferenceParent::InstallModelResolver mResolver;
};

NS_IMPL_ISUPPORTS(ModelDownloadAuthorizationCallback,
                  nsIMLModelDownloadAuthorizationCallback)

ipc::IPCResult HWInferenceParent::RecvInstallModel(
    nsCString&& aTask, nsCString&& aId, uint64_t aInnerWindowId,
    const dom::ContentParentId& aContentId, InstallModelResolver&& aResolver) {
  LOGD("{} task={} id={}", __func__, aTask, aId);

  nsCString engine, model, revision, filename;
  nsCOMPtr<nsIMLModelResolver> resolver =
      ResolveModelId(aTask, aId, engine, model, revision, filename);
  if (!resolver) {
    aResolver(false);
    return IPC_OK();
  }

  // A privileged-chrome request sends a 0 content id: nothing to validate,
  // and the resolver gets a null window.
  RefPtr<dom::WindowGlobalParent> window =
      dom::WindowGlobalParent::GetByInnerWindowId(aInnerWindowId);
  if (aContentId != 0 && (!window || window->ContentParentId() != aContentId)) {
    LOGE("{} - window {} not owned by requester {}", __func__, aInnerWindowId,
         uint64_t(aContentId));
    aResolver(false);
    return IPC_OK();
  }

  // The mock lives only here, so install and availability agree on what has
  // been "downloaded". Outside testing, the resolver skips installed models.
  if (StaticPrefs::browser_ml_modelHub_testing() && sMockInstalledModels &&
      sMockInstalledModels->Contains(MockModelKey(model, revision, filename))) {
    LOGD("{} - testing mock: already installed", __func__);
    aResolver(true);
    return IPC_OK();
  }

  // Tags this download's "ml-model-download-progress" notifications, and is
  // what a consumer hands back to stop it. Created here so that nothing
  // outside the parent gets to choose it.
  nsString progressToken;
  AppendUTF8toUTF16(
      nsDependentCString(nsIDToCString(nsID::GenerateUUID()).get()),
      progressToken);

  auto callback = MakeRefPtr<ModelDownloadAuthorizationCallback>(
      engine, aTask, model, revision, filename, progressToken,
      std::move(aResolver));
  resolver->AuthorizeDownload(model, revision, filename, window, progressToken,
                              callback);
  return IPC_OK();
}

static nsresult BlobJSObjectToFileDescriptor(JSContext* aCx,
                                             JS::Handle<JS::Value> aValue,
                                             ipc::FileDescriptor* aDesc) {
  if (!aValue.isObject()) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<dom::Blob> blob;
  nsresult rv = UNWRAP_OBJECT(Blob, &aValue.toObject(), blob);
  if (NS_FAILED(rv)) {
    LOGE("BlobJSObjectToFileDescriptor - ERROR: Failed to unwrap Blob: {}", rv);
    return rv;
  }

  ErrorResult errorResult;
  nsCOMPtr<nsIInputStream> stream;
  blob->CreateInputStream(getter_AddRefs(stream), errorResult);
  if (errorResult.Failed()) {
    LOGE(
        "BlobJSObjectToFileDescriptor - ERROR: Failed to create input stream "
        "from blob");
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIFileMetadata> fileMetadata = do_QueryInterface(stream);
  if (!fileMetadata) {
    LOGE(
        "BlobJSObjectToFileDescriptor - ERROR: Stream doesn't support "
        "nsIFileMetadata");
    return NS_ERROR_UNEXPECTED;
  }

  PRFileDesc* fileDesc;
  nsresult getRv = fileMetadata->GetFileDescriptor(&fileDesc);
  if (NS_FAILED(getRv)) {
    LOGE("BlobJSObjectToFileDescriptor - ERROR: GetFileDescriptor failed: {}",
         getRv);
    return getRv;
  }

  ipc::FileDescriptor fd(ipc::FileDescriptor::PlatformHandleType(
      PR_FileDesc2NativeHandle(fileDesc)));
  if (!fd.IsValid()) {
    LOGE("BlobJSObjectToFileDescriptor - ERROR: Failed to get native handle");
    return NS_ERROR_UNEXPECTED;
  }

  *aDesc = std::move(fd);
  return NS_OK;
}

ipc::IPCResult HWInferenceParent::RecvGetModelFile(
    nsCString&& aTask, nsCString&& aId, GetModelFileResolver&& aResolver) {
  LOGD("{} task={} id={}", __func__, aTask.get(), aId.get());

  nsCString engine, model, revision, filename;
  nsCOMPtr<nsIMLModelResolver> resolver =
      ResolveModelId(aTask, aId, engine, model, revision, filename);
  if (!resolver) {
    GetModelError error;
    error.errorCode() = NS_ERROR_NOT_AVAILABLE;
    aResolver(GetModelFileResult(error));
    return IPC_OK();
  }

  nsCOMPtr<nsIMLModelHub> modelHubService =
      do_GetService("@mozilla.org/ml-modelhub;1");

  if (!modelHubService) {
    LOGE("{} - ERROR: Failed to get ModelHub XPCOM service", __func__);
    GetModelError error;
    error.errorCode() = NS_ERROR_FAILURE;
    aResolver(GetModelFileResult(error));
    return IPC_OK();
  }

  RefPtr<dom::Promise> promise;
  nsresult rv = modelHubService->GetModelBlob(
      engine, aTask, model, revision, filename, getter_AddRefs(promise));

  if (NS_FAILED(rv)) {
    LOGE("{} - ERROR: GetModelBlob call failed with rv={}", __func__, rv);
    GetModelError error;
    error.errorCode() = rv;
    aResolver(GetModelFileResult(error));
    return IPC_OK();
  }

  promise->AddCallbacksWithCycleCollectedArgs(
      [aResolver](JSContext* aCx, JS::Handle<JS::Value> aValue,
                  ErrorResult& aRv) {
        GetModelFileSuccess success;
        nsresult rv = BlobJSObjectToFileDescriptor(aCx, aValue, &success.fd());
        if (NS_FAILED(rv)) {
          aResolver(GetModelFileResult(GetModelError(rv)));
          return;
        }
        MOZ_ASSERT(success.fd().IsValid());
        aResolver(GetModelFileResult(std::move(success)));
      },
      [aResolver](JSContext* aCx, JS::Handle<JS::Value> aValue,
                  ErrorResult& aRv) {
        LOGE("RecvGetModelFile - ERROR: promise rejected in RecvGetModelFile");
        GetModelError error;
        error.errorCode() = NS_ERROR_FAILURE;
        aResolver(GetModelFileResult(error));
      });

  return IPC_OK();
}

}  // namespace mozilla::hwinference

#undef LOGD
#undef LOGV
#undef LOGE
