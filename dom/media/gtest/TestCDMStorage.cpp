/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ChromiumCDMCallback.h"
#include "ChromiumCDMParent.h"
#include "GMPServiceParent.h"
#include "GMPTestMonitor.h"
#include "MediaResult.h"
#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsCRTGlue.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsExceptionHandler.h"
#include "nsIFile.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIPropertyBag2.h"
#include "nsNSSComponent.h"  //For EnsureNSSInitializedChromeOrContent
#include "nsThreadUtils.h"
#include "prenv.h"

using namespace mozilla;
using namespace mozilla::gmp;

static already_AddRefed<nsIThread> GetGMPThread() {
  RefPtr<GeckoMediaPluginService> service =
      GeckoMediaPluginService::GetGeckoMediaPluginService();
  nsCOMPtr<nsIThread> thread;
  EXPECT_NS_SUCCEEDED(service->GetThread(getter_AddRefs(thread)));
  return thread.forget();
}

/**
 * Enumerate files under |aPath| (non-recursive).
 */
template <typename T>
static nsresult EnumerateDir(nsIFile* aPath, T&& aDirIter) {
  nsCOMPtr<nsIDirectoryEnumerator> iter;
  nsresult rv = aPath->GetDirectoryEntries(getter_AddRefs(iter));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIFile> entry;
  while (NS_SUCCEEDED(iter->GetNextFile(getter_AddRefs(entry))) && entry) {
    aDirIter(entry);
  }
  return NS_OK;
}

/**
 * Enumerate files under $profileDir/gmp/$platform/gmp-fake/$aDir/
 * (non-recursive).
 */
template <typename T>
static nsresult EnumerateCDMStorageDir(const nsACString& aDir, T&& aDirIter) {
  RefPtr<GeckoMediaPluginServiceParent> service =
      GeckoMediaPluginServiceParent::GetSingleton();
  MOZ_RELEASE_ASSERT(service);

  // $profileDir/gmp/$platform/
  nsCOMPtr<nsIFile> path;
  nsresult rv = service->GetStorageDir(getter_AddRefs(path));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // $profileDir/gmp/$platform/gmp-fake/
  rv = path->Append(u"gmp-fake"_ns);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // $profileDir/gmp/$platform/gmp-fake/$aDir/
  rv = path->AppendNative(aDir);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return EnumerateDir(path, aDirIter);
}

class GMPShutdownObserver : public nsIRunnable, public nsIObserver {
 public:
  GMPShutdownObserver(already_AddRefed<nsIRunnable> aShutdownTask,
                      already_AddRefed<nsIRunnable> Continuation,
                      const nsACString& aNodeId)
      : mShutdownTask(aShutdownTask),
        mContinuation(Continuation),
        mNodeId(NS_ConvertUTF8toUTF16(aNodeId)) {}

  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD Run() override {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    EXPECT_TRUE(observerService);
    observerService->AddObserver(this, "gmp-shutdown", false);

    nsCOMPtr<nsIThread> thread(GetGMPThread());
    thread->Dispatch(mShutdownTask, NS_DISPATCH_NORMAL);
    return NS_OK;
  }

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aSomeData) override {
    if (!strcmp(aTopic, "gmp-shutdown") &&
        mNodeId.Equals(nsDependentString(aSomeData))) {
      nsCOMPtr<nsIObserverService> observerService =
          mozilla::services::GetObserverService();
      EXPECT_TRUE(observerService);
      observerService->RemoveObserver(this, "gmp-shutdown");
      nsCOMPtr<nsIThread> thread(GetGMPThread());
      thread->Dispatch(mContinuation, NS_DISPATCH_NORMAL);
    }
    return NS_OK;
  }

 private:
  virtual ~GMPShutdownObserver() = default;
  nsCOMPtr<nsIRunnable> mShutdownTask;
  nsCOMPtr<nsIRunnable> mContinuation;
  const nsString mNodeId;
};

NS_IMPL_ISUPPORTS(GMPShutdownObserver, nsIRunnable, nsIObserver)

class NotifyObserversTask : public Runnable {
 public:
  explicit NotifyObserversTask(const char* aTopic)
      : mozilla::Runnable("NotifyObserversTask"), mTopic(aTopic) {}
  NS_IMETHOD Run() override {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(nullptr, mTopic, nullptr);
    }
    return NS_OK;
  }
  const char* mTopic;
};

class ClearCDMStorageTask : public nsIRunnable, public nsIObserver {
 public:
  ClearCDMStorageTask(already_AddRefed<nsIRunnable> Continuation,
                      nsIThread* aTarget, PRTime aSince)
      : mContinuation(Continuation), mTarget(aTarget), mSince(aSince) {}

  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD Run() override {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    EXPECT_TRUE(observerService);
    observerService->AddObserver(this, "gmp-clear-storage-complete", false);
    if (observerService) {
      nsAutoString str;
      if (mSince >= 0) {
        str.AppendInt(static_cast<int64_t>(mSince));
      }
      observerService->NotifyObservers(nullptr, "browser:purge-session-history",
                                       str.Data());
    }
    return NS_OK;
  }

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aSomeData) override {
    if (!strcmp(aTopic, "gmp-clear-storage-complete")) {
      nsCOMPtr<nsIObserverService> observerService =
          mozilla::services::GetObserverService();
      EXPECT_TRUE(observerService);
      observerService->RemoveObserver(this, "gmp-clear-storage-complete");
      mTarget->Dispatch(mContinuation, NS_DISPATCH_NORMAL);
    }
    return NS_OK;
  }

 private:
  virtual ~ClearCDMStorageTask() = default;
  nsCOMPtr<nsIRunnable> mContinuation;
  nsCOMPtr<nsIThread> mTarget;
  const PRTime mSince;
};

NS_IMPL_ISUPPORTS(ClearCDMStorageTask, nsIRunnable, nsIObserver)

static void ClearCDMStorage(already_AddRefed<nsIRunnable> aContinuation,
                            nsIThread* aTarget, PRTime aSince = -1) {
  RefPtr<ClearCDMStorageTask> task(
      new ClearCDMStorageTask(std::move(aContinuation), aTarget, aSince));
  SchedulerGroup::Dispatch(task.forget());
}

static void SimulatePBModeExit() {
  NS_DispatchAndSpinEventLoopUntilComplete(
      "SimulatePBModeExit"_ns, GetMainThreadSerialEventTarget(),
      MakeAndAddRef<NotifyObserversTask>("last-pb-context-exited"));
  nsCOMPtr<nsIThread> thread(GetGMPThread());
  NS_DispatchAndSpinEventLoopUntilComplete(
      "ClearedPBContext"_ns, thread, NS_NewRunnableFunction(__func__, [] {}));
}

class TestGetNodeIdCallback : public GetNodeIdCallback {
 public:
  TestGetNodeIdCallback(nsCString& aNodeId, nsresult& aResult)
      : mNodeId(aNodeId), mResult(aResult) {}

  void Done(nsresult aResult, const nsACString& aNodeId) {
    mResult = aResult;
    mNodeId = aNodeId;
  }

 private:
  nsCString& mNodeId;
  nsresult& mResult;
};

static NodeIdParts GetNodeIdParts(const nsAString& aOrigin,
                                  const nsAString& aTopLevelOrigin,
                                  const nsAString& aGmpName, bool aInPBMode) {
  OriginAttributes attrs;
  attrs.mPrivateBrowsingId = aInPBMode ? 1 : 0;

  nsAutoCString suffix;
  attrs.CreateSuffix(suffix);

  nsAutoString origin;
  origin.Assign(aOrigin);
  origin.Append(NS_ConvertUTF8toUTF16(suffix));

  nsAutoString topLevelOrigin;
  topLevelOrigin.Assign(aTopLevelOrigin);
  topLevelOrigin.Append(NS_ConvertUTF8toUTF16(suffix));
  return NodeIdParts{origin, topLevelOrigin, nsString(aGmpName)};
}

static nsCString GetNodeId(const nsAString& aOrigin,
                           const nsAString& aTopLevelOrigin, bool aInPBMode) {
  RefPtr<GeckoMediaPluginServiceParent> service =
      GeckoMediaPluginServiceParent::GetSingleton();
  EXPECT_TRUE(service);
  nsCString nodeId;
  nsresult result;
  UniquePtr<GetNodeIdCallback> callback(
      new TestGetNodeIdCallback(nodeId, result));

  OriginAttributes attrs;
  attrs.mPrivateBrowsingId = aInPBMode ? 1 : 0;

  nsAutoCString suffix;
  attrs.CreateSuffix(suffix);

  nsAutoString origin;
  origin.Assign(aOrigin);
  origin.Append(NS_ConvertUTF8toUTF16(suffix));

  nsAutoString topLevelOrigin;
  topLevelOrigin.Assign(aTopLevelOrigin);
  topLevelOrigin.Append(NS_ConvertUTF8toUTF16(suffix));

  // We rely on the fact that the GetNodeId implementation for
  // GeckoMediaPluginServiceParent is synchronous.
  nsresult rv = service->GetNodeId(origin, topLevelOrigin, u"gmp-fake"_ns,
                                   std::move(callback));
  EXPECT_TRUE(NS_SUCCEEDED(rv) && NS_SUCCEEDED(result));
  return nodeId;
}

static bool IsCDMStorageIsEmpty() {
  RefPtr<GeckoMediaPluginServiceParent> service =
      GeckoMediaPluginServiceParent::GetSingleton();
  MOZ_RELEASE_ASSERT(service);
  nsCOMPtr<nsIFile> storage;
  nsresult rv = service->GetStorageDir(getter_AddRefs(storage));
  EXPECT_NS_SUCCEEDED(rv);
  bool exists = false;
  if (storage) {
    storage->Exists(&exists);
  }
  return !exists;
}

static void AssertIsOnGMPThread() {
  RefPtr<GeckoMediaPluginService> service =
      GeckoMediaPluginService::GetGeckoMediaPluginService();
  MOZ_RELEASE_ASSERT(service);
  nsCOMPtr<nsIThread> thread;
  service->GetThread(getter_AddRefs(thread));
  MOZ_RELEASE_ASSERT(thread);
  nsCOMPtr<nsIThread> currentThread;
  nsresult rv = NS_GetCurrentThread(getter_AddRefs(currentThread));
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  MOZ_RELEASE_ASSERT(currentThread == thread);
}

class CDMStorageTest {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CDMStorageTest)

  void DoTest(void (CDMStorageTest::*aTestMethod)()) {
    EnsureNSSInitializedChromeOrContent();
    nsCOMPtr<nsIThread> thread(GetGMPThread());
    ClearCDMStorage(
        NewRunnableMethod("CDMStorageTest::DoTest", this, aTestMethod), thread);
    AwaitFinished();
  }

  CDMStorageTest() : mMonitor("CDMStorageTest"), mFinished(false) {}

  void Update(const nsCString& aMessage) {
    nsTArray<uint8_t> msg;
    msg.AppendElements(aMessage.get(), aMessage.Length());
    mCDM->UpdateSession("fake-session-id"_ns, 1, msg);
  }

  void TestGetNodeId() {
    AssertIsOnGMPThread();

    EXPECT_TRUE(IsCDMStorageIsEmpty());

    const nsString origin1 = u"http://example1.com"_ns;
    const nsString origin2 = u"http://example2.org"_ns;

    nsCString PBnodeId1 = GetNodeId(origin1, origin2, true);
    nsCString PBnodeId2 = GetNodeId(origin1, origin2, true);

    // Node ids for the same origins should be the same in PB mode.
    EXPECT_TRUE(PBnodeId1.Equals(PBnodeId2));

    nsCString PBnodeId3 = GetNodeId(origin2, origin1, true);

    // Node ids with origin and top level origin swapped should be different.
    EXPECT_TRUE(!PBnodeId3.Equals(PBnodeId1));

    // Getting node ids in PB mode should not result in the node id being
    // stored.
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    nsCString nodeId1 = GetNodeId(origin1, origin2, false);
    nsCString nodeId2 = GetNodeId(origin1, origin2, false);

    // NodeIds for the same origin pair in non-pb mode should be the same.
    EXPECT_TRUE(nodeId1.Equals(nodeId2));

    // Node ids for a given origin pair should be different for the PB origins
    // should be the same in PB mode.
    EXPECT_TRUE(!PBnodeId1.Equals(nodeId1));
    EXPECT_TRUE(!PBnodeId2.Equals(nodeId2));

    nsCOMPtr<nsIThread> thread(GetGMPThread());
    ClearCDMStorage(NewRunnableMethod<nsCString>(
                        "CDMStorageTest::TestGetNodeId_Continuation", this,
                        &CDMStorageTest::TestGetNodeId_Continuation, nodeId1),
                    thread);
  }

  void TestGetNodeId_Continuation(nsCString aNodeId1) {
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    // Once we clear storage, the node ids generated for the same origin-pair
    // should be different.
    const nsString origin1 = u"http://example1.com"_ns;
    const nsString origin2 = u"http://example2.org"_ns;
    nsCString nodeId3 = GetNodeId(origin1, origin2, false);
    EXPECT_TRUE(!aNodeId1.Equals(nodeId3));

    SetFinished();
  }

  void CreateDecryptor(const nsAString& aOrigin,
                       const nsAString& aTopLevelOrigin, bool aInPBMode,
                       const nsCString& aUpdate) {
    nsTArray<nsCString> updates;
    updates.AppendElement(aUpdate);
    CreateDecryptor(aOrigin, aTopLevelOrigin, aInPBMode, std::move(updates));
  }

  void CreateDecryptor(const nsAString& aOrigin,
                       const nsAString& aTopLevelOrigin, bool aInPBMode,
                       nsTArray<nsCString>&& aUpdates) {
    CreateDecryptor(
        GetNodeIdParts(aOrigin, aTopLevelOrigin, u"gmp-fake"_ns, aInPBMode),
        std::move(aUpdates));
  }

  void CreateDecryptor(const NodeIdParts& aNodeId,
                       nsTArray<nsCString>&& aUpdates) {
    RefPtr<GeckoMediaPluginService> service =
        GeckoMediaPluginService::GetGeckoMediaPluginService();
    EXPECT_TRUE(service);

    nsCString keySystem{"fake"_ns};

    RefPtr<CDMStorageTest> self = this;
    RefPtr<gmp::GetCDMParentPromise> promise =
        service->GetCDM(aNodeId, keySystem, nullptr);
    nsCOMPtr<nsISerialEventTarget> thread = GetGMPThread();
    promise->Then(
        thread, __func__,
        [self, updates = std::move(aUpdates),
         thread](RefPtr<gmp::ChromiumCDMParent> cdm) mutable {
          self->mCDM = cdm;
          EXPECT_TRUE(!!self->mCDM);
          self->mCallback.reset(new CallbackProxy(self));
          nsCString failureReason;
          self->mCDM
              ->Init(self->mCallback.get(), false, true,
                     GetMainThreadSerialEventTarget())
              ->Then(
                  thread, __func__,
                  [self, updates = std::move(updates)] {
                    for (const auto& update : updates) {
                      self->Update(update);
                    }
                  },
                  [](MediaResult rv) { EXPECT_TRUE(false); });
        },
        [](MediaResult rv) { EXPECT_TRUE(false); });
  }

  void TestBasicStorage() {
    AssertIsOnGMPThread();
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    RefPtr<GeckoMediaPluginService> service =
        GeckoMediaPluginService::GetGeckoMediaPluginService();

    // Send a message to the fake GMP for it to run its own tests internally.
    // It sends us a "test-storage complete" message when its passed, or
    // some other message if its tests fail.
    Expect("test-storage complete"_ns,
           NewRunnableMethod("CDMStorageTest::SetFinished", this,
                             &CDMStorageTest::SetFinished));

    CreateDecryptor(u"http://example1.com"_ns, u"http://example2.com"_ns, false,
                    "test-storage"_ns);
  }

  /**
   * 1. Generate storage data for some sites.
   * 2. Forget about one of the sites.
   * 3. Check if the storage data for the forgotten site are erased correctly.
   * 4. Check if the storage data for other sites remain unchanged.
   */
  void TestForgetThisSite() {
    AssertIsOnGMPThread();
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    // Generate storage data for some site.
    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestForgetThisSite_AnotherSite", this,
        &CDMStorageTest::TestForgetThisSite_AnotherSite);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://example1.com"_ns, u"http://example2.com"_ns, false,
                    "test-storage"_ns);
  }

  void TestForgetThisSite_AnotherSite() {
    Shutdown();

    // Generate storage data for another site.
    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestForgetThisSite_CollectSiteInfo", this,
        &CDMStorageTest::TestForgetThisSite_CollectSiteInfo);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://example3.com"_ns, u"http://example4.com"_ns, false,
                    "test-storage"_ns);
  }

  struct NodeInfo {
    explicit NodeInfo(const nsACString& aSite,
                      const mozilla::OriginAttributesPattern& aPattern)
        : siteToForget(aSite), mPattern(aPattern) {}
    nsCString siteToForget;
    mozilla::OriginAttributesPattern mPattern;
    nsTArray<nsCString> mExpectedRemainingNodeIds;
  };

  class NodeIdCollector {
   public:
    explicit NodeIdCollector(NodeInfo* aInfo) : mNodeInfo(aInfo) {}
    void operator()(nsIFile* aFile) {
      nsCString salt;
      nsresult rv = ReadSalt(aFile, salt);
      ASSERT_NS_SUCCEEDED(rv);
      if (!MatchOrigin(aFile, mNodeInfo->siteToForget, mNodeInfo->mPattern)) {
        mNodeInfo->mExpectedRemainingNodeIds.AppendElement(salt);
      }
    }

   private:
    NodeInfo* mNodeInfo;
  };

  void TestForgetThisSite_CollectSiteInfo() {
    mozilla::OriginAttributesPattern pattern;

    UniquePtr<NodeInfo> siteInfo(
        new NodeInfo("http://example1.com"_ns, pattern));
    // Collect nodeIds that are expected to remain for later comparison.
    EnumerateCDMStorageDir("id"_ns, NodeIdCollector(siteInfo.get()));
    // Invoke "Forget this site" on the main thread.
    SchedulerGroup::Dispatch(NewRunnableMethod<UniquePtr<NodeInfo>&&>(
        "CDMStorageTest::TestForgetThisSite_Forget", this,
        &CDMStorageTest::TestForgetThisSite_Forget, std::move(siteInfo)));
  }

  void TestForgetThisSite_Forget(UniquePtr<NodeInfo>&& aSiteInfo) {
    RefPtr<GeckoMediaPluginServiceParent> service =
        GeckoMediaPluginServiceParent::GetSingleton();
    service->ForgetThisSiteNative(
        NS_ConvertUTF8toUTF16(aSiteInfo->siteToForget), aSiteInfo->mPattern);

    nsCOMPtr<nsIThread> thread;
    service->GetThread(getter_AddRefs(thread));

    nsCOMPtr<nsIRunnable> r = NewRunnableMethod<UniquePtr<NodeInfo>&&>(
        "CDMStorageTest::TestForgetThisSite_Verify", this,
        &CDMStorageTest::TestForgetThisSite_Verify, std::move(aSiteInfo));
    thread->Dispatch(r, NS_DISPATCH_NORMAL);

    nsCOMPtr<nsIRunnable> f = NewRunnableMethod(
        "CDMStorageTest::SetFinished", this, &CDMStorageTest::SetFinished);
    thread->Dispatch(f, NS_DISPATCH_NORMAL);
  }

  class NodeIdVerifier {
   public:
    explicit NodeIdVerifier(const NodeInfo* aInfo)
        : mNodeInfo(aInfo),
          mExpectedRemainingNodeIds(aInfo->mExpectedRemainingNodeIds.Clone()) {}
    void operator()(nsIFile* aFile) {
      nsCString salt;
      nsresult rv = ReadSalt(aFile, salt);
      ASSERT_NS_SUCCEEDED(rv);
      // Shouldn't match the origin if we clear correctly.
      EXPECT_FALSE(
          MatchOrigin(aFile, mNodeInfo->siteToForget, mNodeInfo->mPattern))
          << "Found files persisted that match against a site that should "
             "have been removed!";
      // Check if remaining nodeIDs are as expected.
      EXPECT_TRUE(mExpectedRemainingNodeIds.RemoveElement(salt))
          << "Failed to remove salt from expected remaining node ids. This "
             "indicates storage that should be forgotten is still persisted!";
    }
    ~NodeIdVerifier() {
      EXPECT_TRUE(mExpectedRemainingNodeIds.IsEmpty())
          << "Some expected remaining node ids were not checked against. This "
             "indicates that data we expected to find in storage was missing!";
    }

   private:
    const NodeInfo* mNodeInfo;
    nsTArray<nsCString> mExpectedRemainingNodeIds;
  };

  class StorageVerifier {
   public:
    explicit StorageVerifier(const NodeInfo* aInfo)
        : mExpectedRemainingNodeIds(aInfo->mExpectedRemainingNodeIds.Clone()) {}
    void operator()(nsIFile* aFile) {
      nsCString salt;
      nsresult rv = aFile->GetNativeLeafName(salt);
      ASSERT_NS_SUCCEEDED(rv);
      EXPECT_TRUE(mExpectedRemainingNodeIds.RemoveElement(salt))
          << "Failed to remove salt from expected remaining node ids. This "
             "indicates storage that should be forgotten is still persisted!";
    }
    ~StorageVerifier() {
      EXPECT_TRUE(mExpectedRemainingNodeIds.IsEmpty())
          << "Some expected remaining node ids were not checked against. This "
             "indicates that data we expected to find in storage was missing!";
    }

   private:
    nsTArray<nsCString> mExpectedRemainingNodeIds;
  };

  void TestForgetThisSite_Verify(UniquePtr<NodeInfo>&& aSiteInfo) {
    nsresult rv =
        EnumerateCDMStorageDir("id"_ns, NodeIdVerifier(aSiteInfo.get()));
    EXPECT_NS_SUCCEEDED(rv);

    rv = EnumerateCDMStorageDir("storage"_ns, StorageVerifier(aSiteInfo.get()));
    EXPECT_NS_SUCCEEDED(rv);
  }

  /**
   * 1. Generate storage data for some sites.
   * 2. Forget about base domain example1.com
   * 3. Check if the storage data for the forgotten site are erased correctly.
   * 4. Check if the storage data for other sites remain unchanged.
   */
  void TestForgetThisBaseDomain() {
    AssertIsOnGMPThread();
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    // Generate storage data for some site.
    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestForgetThisBaseDomain_SecondSite", this,
        &CDMStorageTest::TestForgetThisBaseDomain_SecondSite);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://media.example1.com"_ns,
                    u"http://tld.example2.com"_ns, false, "test-storage"_ns);
  }

  void TestForgetThisBaseDomain_SecondSite() {
    Shutdown();

    // Generate storage data for another site.
    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestForgetThisBaseDomain_ThirdSite", this,
        &CDMStorageTest::TestForgetThisBaseDomain_ThirdSite);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://media.somewhereelse.com"_ns,
                    u"http://home.example1.com"_ns, false, "test-storage"_ns);
  }

  void TestForgetThisBaseDomain_ThirdSite() {
    Shutdown();

    // Generate storage data for another site.
    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestForgetThisBaseDomain_CollectSiteInfo", this,
        &CDMStorageTest::TestForgetThisBaseDomain_CollectSiteInfo);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://media.example3.com"_ns,
                    u"http://tld.long-example1.com"_ns, false,
                    "test-storage"_ns);
  }

  struct BaseDomainNodeInfo {
    explicit BaseDomainNodeInfo(const nsACString& aBaseDomain)
        : baseDomainToForget(aBaseDomain) {}
    nsCString baseDomainToForget;

    nsTArray<nsCString> mExpectedRemainingNodeIds;
  };

  class BaseDomainNodeIdCollector {
   public:
    explicit BaseDomainNodeIdCollector(BaseDomainNodeInfo* aInfo)
        : mNodeInfo(aInfo) {}
    void operator()(nsIFile* aFile) {
      nsCString salt;
      nsresult rv = ReadSalt(aFile, salt);
      ASSERT_NS_SUCCEEDED(rv);
      if (!MatchBaseDomain(aFile, mNodeInfo->baseDomainToForget)) {
        mNodeInfo->mExpectedRemainingNodeIds.AppendElement(salt);
      }
    }

   private:
    BaseDomainNodeInfo* mNodeInfo;
  };

  void TestForgetThisBaseDomain_CollectSiteInfo() {
    UniquePtr<BaseDomainNodeInfo> siteInfo(
        new BaseDomainNodeInfo("example1.com"_ns));
    // Collect nodeIds that are expected to remain for later comparison.
    EnumerateCDMStorageDir("id"_ns, BaseDomainNodeIdCollector(siteInfo.get()));
    // Invoke "ForgetThisBaseDomain" on the main thread.
    SchedulerGroup::Dispatch(NewRunnableMethod<UniquePtr<BaseDomainNodeInfo>&&>(
        "CDMStorageTest::TestForgetThisBaseDomain_Forget", this,
        &CDMStorageTest::TestForgetThisBaseDomain_Forget, std::move(siteInfo)));
  }

  void TestForgetThisBaseDomain_Forget(
      UniquePtr<BaseDomainNodeInfo>&& aSiteInfo) {
    RefPtr<GeckoMediaPluginServiceParent> service =
        GeckoMediaPluginServiceParent::GetSingleton();
    service->ForgetThisBaseDomain(
        NS_ConvertUTF8toUTF16(aSiteInfo->baseDomainToForget));

    nsCOMPtr<nsIThread> thread;
    service->GetThread(getter_AddRefs(thread));

    nsCOMPtr<nsIRunnable> r =
        NewRunnableMethod<UniquePtr<BaseDomainNodeInfo>&&>(
            "CDMStorageTest::TestForgetThisBaseDomain_Verify", this,
            &CDMStorageTest::TestForgetThisBaseDomain_Verify,
            std::move(aSiteInfo));
    thread->Dispatch(r, NS_DISPATCH_NORMAL);

    nsCOMPtr<nsIRunnable> f = NewRunnableMethod(
        "CDMStorageTest::SetFinished", this, &CDMStorageTest::SetFinished);
    thread->Dispatch(f, NS_DISPATCH_NORMAL);
  }

  class BaseDomainNodeIdVerifier {
   public:
    explicit BaseDomainNodeIdVerifier(const BaseDomainNodeInfo* aInfo)
        : mNodeInfo(aInfo),
          mExpectedRemainingNodeIds(aInfo->mExpectedRemainingNodeIds.Clone()) {}
    void operator()(nsIFile* aFile) {
      nsCString salt;
      nsresult rv = ReadSalt(aFile, salt);
      ASSERT_NS_SUCCEEDED(rv);
      // Shouldn't match the origin if we clear correctly.
      EXPECT_FALSE(MatchBaseDomain(aFile, mNodeInfo->baseDomainToForget))
          << "Found files persisted that match against a domain that should "
             "have been removed!";
      // Check if remaining nodeIDs are as expected.
      EXPECT_TRUE(mExpectedRemainingNodeIds.RemoveElement(salt))
          << "Failed to remove salt from expected remaining node ids. This "
             "indicates storage that should be forgotten is still persisted!";
    }
    ~BaseDomainNodeIdVerifier() {
      EXPECT_TRUE(mExpectedRemainingNodeIds.IsEmpty())
          << "Some expected remaining node ids were not checked against. This "
             "indicates that data we expected to find in storage was missing!";
    }

   private:
    const BaseDomainNodeInfo* mNodeInfo;
    nsTArray<nsCString> mExpectedRemainingNodeIds;
  };

  class BaseDomainStorageVerifier {
   public:
    explicit BaseDomainStorageVerifier(const BaseDomainNodeInfo* aInfo)
        : mExpectedRemainingNodeIds(aInfo->mExpectedRemainingNodeIds.Clone()) {}
    void operator()(nsIFile* aFile) {
      nsCString salt;
      nsresult rv = aFile->GetNativeLeafName(salt);
      ASSERT_NS_SUCCEEDED(rv);
      EXPECT_TRUE(mExpectedRemainingNodeIds.RemoveElement(salt))
          << "Failed to remove salt from expected remaining node ids. This "
             "indicates storage that should be forgotten is still persisted!";
      ;
    }
    ~BaseDomainStorageVerifier() {
      EXPECT_TRUE(mExpectedRemainingNodeIds.IsEmpty())
          << "Some expected remaining node ids were not checked against. This "
             "indicates that data we expected to find in storage was missing!";
      ;
    }

   private:
    nsTArray<nsCString> mExpectedRemainingNodeIds;
  };

  void TestForgetThisBaseDomain_Verify(
      UniquePtr<BaseDomainNodeInfo>&& aSiteInfo) {
    nsresult rv = EnumerateCDMStorageDir(
        "id"_ns, BaseDomainNodeIdVerifier(aSiteInfo.get()));
    EXPECT_NS_SUCCEEDED(rv);

    rv = EnumerateCDMStorageDir("storage"_ns,
                                BaseDomainStorageVerifier(aSiteInfo.get()));
    EXPECT_NS_SUCCEEDED(rv);
  }

  /**
   * 1. Generate some storage data.
   * 2. Find the max mtime |t| in $profileDir/gmp/$platform/gmp-fake/id/.
   * 3. Pass |t| to clear recent history.
   * 4. Check if all directories in $profileDir/gmp/$platform/gmp-fake/id/ and
   *    $profileDir/gmp/$platform/gmp-fake/storage are removed.
   */
  void TestClearRecentHistory1() {
    AssertIsOnGMPThread();
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    // Generate storage data for some site.
    nsCOMPtr<nsIRunnable> r =
        NewRunnableMethod("CDMStorageTest::TestClearRecentHistory1_Clear", this,
                          &CDMStorageTest::TestClearRecentHistory1_Clear);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://example1.com"_ns, u"http://example2.com"_ns, false,
                    "test-storage"_ns);
  }

  /**
   * 1. Generate some storage data.
   * 2. Find the max mtime |t| in $profileDir/gmp/$platform/gmp-fake/storage/.
   * 3. Pass |t| to clear recent history.
   * 4. Check if all directories in $profileDir/gmp/$platform/gmp-fake/id/ and
   *    $profileDir/gmp/$platform/gmp-fake/storage are removed.
   */
  void TestClearRecentHistory2() {
    AssertIsOnGMPThread();
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    // Generate storage data for some site.
    nsCOMPtr<nsIRunnable> r =
        NewRunnableMethod("CDMStorageTest::TestClearRecentHistory2_Clear", this,
                          &CDMStorageTest::TestClearRecentHistory2_Clear);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://example1.com"_ns, u"http://example2.com"_ns, false,
                    "test-storage"_ns);
  }

  /**
   * 1. Generate some storage data.
   * 2. Find the max mtime |t| in $profileDir/gmp/$platform/gmp-fake/storage/.
   * 3. Pass |t+1| to clear recent history.
   * 4. Check if all directories in $profileDir/gmp/$platform/gmp-fake/id/ and
   *    $profileDir/gmp/$platform/gmp-fake/storage remain unchanged.
   */
  void TestClearRecentHistory3() {
    AssertIsOnGMPThread();
    EXPECT_TRUE(IsCDMStorageIsEmpty());

    // Generate storage data for some site.
    nsCOMPtr<nsIRunnable> r =
        NewRunnableMethod("CDMStorageTest::TestClearRecentHistory3_Clear", this,
                          &CDMStorageTest::TestClearRecentHistory3_Clear);
    Expect("test-storage complete"_ns, r.forget());

    CreateDecryptor(u"http://example1.com"_ns, u"http://example2.com"_ns, false,
                    "test-storage"_ns);
  }

  class MaxMTimeFinder {
   public:
    MaxMTimeFinder() : mMaxTime(0) {}
    void operator()(nsIFile* aFile) {
      PRTime lastModified;
      nsresult rv = aFile->GetLastModifiedTime(&lastModified);
      if (NS_SUCCEEDED(rv) && lastModified > mMaxTime) {
        mMaxTime = lastModified;
      }
      EnumerateDir(aFile, *this);
    }
    PRTime GetResult() const { return mMaxTime; }

   private:
    PRTime mMaxTime;
  };

  void TestClearRecentHistory1_Clear() {
    MaxMTimeFinder f;
    nsresult rv = EnumerateCDMStorageDir("id"_ns, f);
    EXPECT_NS_SUCCEEDED(rv);

    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestClearRecentHistory_CheckEmpty", this,
        &CDMStorageTest::TestClearRecentHistory_CheckEmpty);
    nsCOMPtr<nsIThread> t(GetGMPThread());
    ClearCDMStorage(r.forget(), t, f.GetResult());
  }

  void TestClearRecentHistory2_Clear() {
    MaxMTimeFinder f;
    nsresult rv = EnumerateCDMStorageDir("storage"_ns, f);
    EXPECT_NS_SUCCEEDED(rv);

    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestClearRecentHistory_CheckEmpty", this,
        &CDMStorageTest::TestClearRecentHistory_CheckEmpty);
    nsCOMPtr<nsIThread> t(GetGMPThread());
    ClearCDMStorage(r.forget(), t, f.GetResult());
  }

  void TestClearRecentHistory3_Clear() {
    MaxMTimeFinder f;
    nsresult rv = EnumerateCDMStorageDir("storage"_ns, f);
    EXPECT_NS_SUCCEEDED(rv);

    nsCOMPtr<nsIRunnable> r = NewRunnableMethod(
        "CDMStorageTest::TestClearRecentHistory_CheckNonEmpty", this,
        &CDMStorageTest::TestClearRecentHistory_CheckNonEmpty);
    nsCOMPtr<nsIThread> t(GetGMPThread());
    ClearCDMStorage(r.forget(), t, f.GetResult() + 1);
  }

  class FileCounter {
   public:
    FileCounter() : mCount(0) {}
    void operator()(nsIFile* aFile) { ++mCount; }
    int GetCount() const { return mCount; }

   private:
    int mCount;
  };

  void TestClearRecentHistory_CheckEmpty() {
    FileCounter c1;
    nsresult rv = EnumerateCDMStorageDir("id"_ns, c1);
    EXPECT_NS_SUCCEEDED(rv);
    // There should be no files under $profileDir/gmp/$platform/gmp-fake/id/
    EXPECT_EQ(c1.GetCount(), 0);

    FileCounter c2;
    rv = EnumerateCDMStorageDir("storage"_ns, c2);
    EXPECT_NS_SUCCEEDED(rv);
    // There should be no files under
    // $profileDir/gmp/$platform/gmp-fake/storage/
    EXPECT_EQ(c2.GetCount(), 0);

    SetFinished();
  }

  void TestClearRecentHistory_CheckNonEmpty() {
    FileCounter c1;
    nsresult rv = EnumerateCDMStorageDir("id"_ns, c1);
    EXPECT_NS_SUCCEEDED(rv);
    // There should be one directory under
    // $profileDir/gmp/$platform/gmp-fake/id/
    EXPECT_EQ(c1.GetCount(), 1);

    FileCounter c2;
    rv = EnumerateCDMStorageDir("storage"_ns, c2);
    EXPECT_NS_SUCCEEDED(rv);
    // There should be one directory under
    // $profileDir/gmp/$platform/gmp-fake/storage/
    EXPECT_EQ(c2.GetCount(), 1);

    SetFinished();
  }

  void TestCrossOriginStorage() {
    EXPECT_TRUE(!mCDM);

    // Send the decryptor the message "store recordid $time"
    // Wait for the decrytor to send us "stored recordid $time"
    auto t = time(nullptr);
    nsCString response("stored crossOriginTestRecordId ");
    response.AppendInt((int64_t)t);
    Expect(
        response,
        NewRunnableMethod(
            "CDMStorageTest::TestCrossOriginStorage_RecordStoredContinuation",
            this,
            &CDMStorageTest::TestCrossOriginStorage_RecordStoredContinuation));

    nsCString update("store crossOriginTestRecordId ");
    update.AppendInt((int64_t)t);

    // Open decryptor on one, origin, write a record, and test that that
    // record can't be read on another origin.
    CreateDecryptor(u"http://example3.com"_ns, u"http://example4.com"_ns, false,
                    update);
  }

  void TestCrossOriginStorage_RecordStoredContinuation() {
    // Close the old decryptor, and create a new one on a different origin,
    // and try to read the record.
    Shutdown();

    Expect(nsLiteralCString(
               "retrieve crossOriginTestRecordId succeeded (length 0 bytes)"),
           NewRunnableMethod("CDMStorageTest::SetFinished", this,
                             &CDMStorageTest::SetFinished));

    CreateDecryptor(u"http://example5.com"_ns, u"http://example6.com"_ns, false,
                    "retrieve crossOriginTestRecordId"_ns);
  }

  void TestPBStorage() {
    // Send the decryptor the message "store recordid $time"
    // Wait for the decrytor to send us "stored recordid $time"
    nsCString response("stored pbdata test-pb-data");
    Expect(response,
           NewRunnableMethod(
               "CDMStorageTest::TestPBStorage_RecordStoredContinuation", this,
               &CDMStorageTest::TestPBStorage_RecordStoredContinuation));

    // Open decryptor on one, origin, write a record, close decryptor,
    // open another, and test that record can be read, close decryptor,
    // then send pb-last-context-closed notification, then open decryptor
    // and check that it can't read that data; it should have been purged.
    CreateDecryptor(u"http://pb1.com"_ns, u"http://pb2.com"_ns, true,
                    "store pbdata test-pb-data"_ns);
  }

  void TestPBStorage_RecordStoredContinuation() {
    Shutdown();

    Expect(
        "retrieve pbdata succeeded (length 12 bytes)"_ns,
        NewRunnableMethod(
            "CDMStorageTest::TestPBStorage_RecordRetrievedContinuation", this,
            &CDMStorageTest::TestPBStorage_RecordRetrievedContinuation));

    CreateDecryptor(u"http://pb1.com"_ns, u"http://pb2.com"_ns, true,
                    "retrieve pbdata"_ns);
  }

  void TestPBStorage_RecordRetrievedContinuation() {
    Shutdown();
    SimulatePBModeExit();

    Expect("retrieve pbdata succeeded (length 0 bytes)"_ns,
           NewRunnableMethod("CDMStorageTest::SetFinished", this,
                             &CDMStorageTest::SetFinished));

    CreateDecryptor(u"http://pb1.com"_ns, u"http://pb2.com"_ns, true,
                    "retrieve pbdata"_ns);
  }

  void TestCrash() {
    AssertIsOnGMPThread();
    CreateDecryptor(u"http://example1.com"_ns, u"http://example2.com"_ns, false,
                    "crash"_ns);
  }

#if defined(XP_WIN)
  void TestOutputProtection() {
    Shutdown();

    Expect("OP tests completed"_ns,
           NewRunnableMethod("CDMStorageTest::SetFinished", this,
                             &CDMStorageTest::SetFinished));

    CreateDecryptor(u"http://example15.com"_ns, u"http://example16.com"_ns,
                    false, "test-op-apis"_ns);
  }
#endif

  void TestLongRecordNames() {
    constexpr auto longRecordName =
        "A_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "very_very_very_very_very_very_very_very_very_"
        "very_very_very_very_very_very_"
        "long_record_name"_ns;

    constexpr auto data = "Just_some_arbitrary_data."_ns;

    MOZ_RELEASE_ASSERT(longRecordName.Length() < GMP_MAX_RECORD_NAME_SIZE);
    MOZ_RELEASE_ASSERT(longRecordName.Length() > 260);  // Windows MAX_PATH

    nsCString response("stored ");
    response.Append(longRecordName);
    response.AppendLiteral(" ");
    response.Append(data);
    Expect(response, NewRunnableMethod("CDMStorageTest::SetFinished", this,
                                       &CDMStorageTest::SetFinished));

    nsCString update("store ");
    update.Append(longRecordName);
    update.AppendLiteral(" ");
    update.Append(data);
    CreateDecryptor(u"http://fuz.com"_ns, u"http://baz.com"_ns, false, update);
  }

  void Expect(const nsCString& aMessage,
              already_AddRefed<nsIRunnable> aContinuation) {
    mExpected.AppendElement(
        ExpectedMessage(aMessage, std::move(aContinuation)));
  }

  void AwaitFinished() {
    mozilla::SpinEventLoopUntil("CDMStorageTest::AwaitFinished"_ns,
                                [&]() -> bool { return mFinished; });
    mFinished = false;
  }

  void ShutdownThen(already_AddRefed<nsIRunnable> aContinuation) {
    EXPECT_TRUE(!!mCDM);
    if (!mCDM) {
      return;
    }
    EXPECT_FALSE(mNodeId.IsEmpty());
    RefPtr<GMPShutdownObserver> task(new GMPShutdownObserver(
        NewRunnableMethod("CDMStorageTest::Shutdown", this,
                          &CDMStorageTest::Shutdown),
        std::move(aContinuation), mNodeId));
    SchedulerGroup::Dispatch(task.forget());
  }

  void Shutdown() {
    if (mCDM) {
      mCDM->Shutdown();
      mCDM = nullptr;
      mNodeId.Truncate();
    }
  }

  void Dummy() {}

  void SetFinished() {
    mFinished = true;
    Shutdown();
    nsCOMPtr<nsIRunnable> task = NewRunnableMethod(
        "CDMStorageTest::Dummy", this, &CDMStorageTest::Dummy);
    SchedulerGroup::Dispatch(task.forget());
  }

  void SessionMessage(const nsACString& aSessionId,
                      cdm::MessageType aMessageType,
                      const nsTArray<uint8_t>& aMessage) {
    MonitorAutoLock mon(mMonitor);

    nsCString msg((const char*)aMessage.Elements(), aMessage.Length());
    EXPECT_TRUE(mExpected.Length() > 0);
    bool matches = mExpected[0].mMessage.Equals(msg);
    EXPECT_STREQ(mExpected[0].mMessage.get(), msg.get());
    if (mExpected.Length() > 0 && matches) {
      nsCOMPtr<nsIRunnable> continuation = mExpected[0].mContinuation;
      mExpected.RemoveElementAt(0);
      if (continuation) {
        NS_DispatchToCurrentThread(continuation);
      }
    }
  }

  void Terminated() {
    if (mCDM) {
      mCDM->Shutdown();
      mCDM = nullptr;
    }
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "CDMStorageTest::Terminated",
        [self = RefPtr{this}] { self->mTerminated = true; }));
  }

  bool mTerminated = false;

 private:
  ~CDMStorageTest() = default;

  struct ExpectedMessage {
    ExpectedMessage(const nsCString& aMessage,
                    already_AddRefed<nsIRunnable> aContinuation)
        : mMessage(aMessage), mContinuation(aContinuation) {}
    nsCString mMessage;
    nsCOMPtr<nsIRunnable> mContinuation;
  };

  nsTArray<ExpectedMessage> mExpected;

  RefPtr<gmp::ChromiumCDMParent> mCDM;
  Monitor mMonitor MOZ_UNANNOTATED;
  Atomic<bool> mFinished;
  nsCString mNodeId;

  class CallbackProxy : public ChromiumCDMCallback {
   public:
    explicit CallbackProxy(CDMStorageTest* aRunner) : mRunner(aRunner) {}

    void SetSessionId(uint32_t aPromiseId,
                      const nsCString& aSessionId) override {}

    void ResolveLoadSessionPromise(uint32_t aPromiseId,
                                   bool aSuccessful) override {}

    void ResolvePromiseWithKeyStatus(uint32_t aPromiseId,
                                     cdm::KeyStatus aKeyStatus) override {}

    void ResolvePromise(uint32_t aPromiseId) override {}

    void RejectPromise(uint32_t aPromiseId, ErrorResult&& aError,
                       const nsCString& aErrorMessage) override {}

    void SessionMessage(const nsACString& aSessionId,
                        cdm::MessageType aMessageType,
                        nsTArray<uint8_t>&& aMessage) override {
      mRunner->SessionMessage(aSessionId, aMessageType, std::move(aMessage));
    }

    void SessionKeysChange(
        const nsCString& aSessionId,
        nsTArray<mozilla::gmp::CDMKeyInformation>&& aKeysInfo) override {}

    void ExpirationChange(const nsCString& aSessionId,
                          double aSecondsSinceEpoch) override {}

    void SessionClosed(const nsCString& aSessionId) override {}

    void QueryOutputProtectionStatus() override {}

    void Terminated() override { mRunner->Terminated(); }

    void Shutdown() override { mRunner->Shutdown(); }

   private:
    // Warning: Weak ref.
    CDMStorageTest* mRunner;
  };

  UniquePtr<CallbackProxy> mCallback;
};  // class CDMStorageTest

static nsresult CreateTestDirectory(nsCOMPtr<nsIFile>& aOut) {
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(aOut));
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCString dirName;
  dirName.SetLength(32);
  NS_MakeRandomString(dirName.BeginWriting(), 32);
  aOut->Append(NS_ConvertUTF8toUTF16(dirName));
  rv = aOut->Create(nsIFile::DIRECTORY_TYPE, 0755);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return NS_OK;
}

void TestMatchBaseDomain_MatchOrigin() {
  nsCOMPtr<nsIFile> testDir;
  nsresult rv = CreateTestDirectory(testDir);
  EXPECT_NS_SUCCEEDED(rv);

  rv = WriteToFile(testDir, "origin"_ns,
                   "https://video.subdomain.removeme.github.io"_ns);
  EXPECT_NS_SUCCEEDED(rv);
  rv = WriteToFile(testDir, "topLevelOrigin"_ns,
                   "https://embedder.example.com"_ns);
  EXPECT_NS_SUCCEEDED(rv);
  bool result = MatchBaseDomain(testDir, "removeme.github.io"_ns);
  EXPECT_TRUE(result);
  testDir->Remove(true);
}

void TestMatchBaseDomain_MatchTLD() {
  nsCOMPtr<nsIFile> testDir;
  nsresult rv = CreateTestDirectory(testDir);
  EXPECT_NS_SUCCEEDED(rv);

  rv = WriteToFile(testDir, "origin"_ns,
                   "https://video.example.com^userContextId=4"_ns);
  EXPECT_NS_SUCCEEDED(rv);
  rv = WriteToFile(testDir, "topLevelOrigin"_ns,
                   "https://evil.web.megacorp.co.uk^privateBrowsingId=1"_ns);
  EXPECT_NS_SUCCEEDED(rv);
  bool result = MatchBaseDomain(testDir, "megacorp.co.uk"_ns);
  EXPECT_TRUE(result);
  testDir->Remove(true);
}

void TestMatchBaseDomain_NoMatch() {
  nsCOMPtr<nsIFile> testDir;
  nsresult rv = CreateTestDirectory(testDir);
  EXPECT_NS_SUCCEEDED(rv);

  rv = WriteToFile(testDir, "origin"_ns,
                   "https://video.example.com^userContextId=4"_ns);
  EXPECT_NS_SUCCEEDED(rv);
  rv = WriteToFile(testDir, "topLevelOrigin"_ns,
                   "https://evil.web.megacorp.co.uk^privateBrowsingId=1"_ns);
  EXPECT_NS_SUCCEEDED(rv);
  bool result = MatchBaseDomain(testDir, "longer-example.com"_ns);
  EXPECT_FALSE(result);
  testDir->Remove(true);
}

TEST(GeckoMediaPlugins, MatchBaseDomain_MatchOrigin)
{
  TestMatchBaseDomain_MatchOrigin();
}

TEST(GeckoMediaPlugins, MatchBaseDomain_MatchTLD)
{
  TestMatchBaseDomain_MatchTLD();
}

TEST(GeckoMediaPlugins, MatchBaseDomain_NoMatch)
{
  TestMatchBaseDomain_NoMatch();
}

TEST(GeckoMediaPlugins, CDMStorageGetNodeId)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestGetNodeId);
}

TEST(GeckoMediaPlugins, CDMStorageBasic)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestBasicStorage);
}

TEST(GeckoMediaPlugins, CDMStorageForgetThisSite)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestForgetThisSite);
}

TEST(GeckoMediaPlugins, CDMStorageForgetThisBaseDomain)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestForgetThisBaseDomain);
}

TEST(GeckoMediaPlugins, CDMStorageClearRecentHistory1)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestClearRecentHistory1);
}

TEST(GeckoMediaPlugins, CDMStorageClearRecentHistory2)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestClearRecentHistory2);
}

TEST(GeckoMediaPlugins, CDMStorageClearRecentHistory3)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestClearRecentHistory3);
}

TEST(GeckoMediaPlugins, CDMStorageCrossOrigin)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestCrossOriginStorage);
}

TEST(GeckoMediaPlugins, CDMStoragePrivateBrowsing)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestPBStorage);
}

TEST(GeckoMediaPlugins, CDMStorageLongRecordNames)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestLongRecordNames);
}

#if defined(XP_WIN)
TEST(GeckoMediaPlugins, GMPOutputProtection)
{
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  runner->DoTest(&CDMStorageTest::TestOutputProtection);
}
#endif  // defined(XP_WIN)

class GMPCrashObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override {
    nsCOMPtr<nsIPropertyBag2> props = do_QueryInterface(aSubject);
    props->GetPropertyAsAString(u"pluginDumpID"_ns, mDumpID);
    mCrashed = true;
    return NS_OK;
  }

  nsString mDumpID;
  bool mCrashed = false;

 private:
  ~GMPCrashObserver() = default;
};

NS_IMPL_ISUPPORTS(GMPCrashObserver, nsIObserver)

TEST(GeckoMediaPlugins, CDMCrashMinidump)
{
  if (!CrashReporter::GetEnabled()) {
    GTEST_SKIP() << "The crash reporter is disabled";
  }

  // Keep the minidump out of the pending directory.
  const char* noReport = PR_GetEnv("MOZ_CRASHREPORTER_NO_REPORT");
  PR_SetEnv("MOZ_CRASHREPORTER_NO_REPORT=1");
  auto restoreNoReport = MakeScopeExit([&] {
    if (!noReport || !*noReport) {
      PR_SetEnv("MOZ_CRASHREPORTER_NO_REPORT=");
    }
  });

  // Write the minidump to a throwaway directory rather than removing it, as the
  // crash service keeps processing it after we're notified of the crash.
  nsAutoString originalMinidumpPath;
  ASSERT_TRUE(CrashReporter::GetMinidumpPath(originalMinidumpPath));
  nsCOMPtr<nsIFile> minidumpDir;
  ASSERT_NS_SUCCEEDED(
      NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(minidumpDir)));
  ASSERT_NS_SUCCEEDED(minidumpDir->Append(u"gmp-crash-minidumps"_ns));
  ASSERT_NS_SUCCEEDED(minidumpDir->CreateUnique(nsIFile::DIRECTORY_TYPE, 0700));
  nsAutoString minidumpPath;
  ASSERT_NS_SUCCEEDED(minidumpDir->GetPath(minidumpPath));
  ASSERT_NS_SUCCEEDED(CrashReporter::SetMinidumpPath(minidumpPath));
  auto restoreMinidumpPath = MakeScopeExit(
      [&] { CrashReporter::SetMinidumpPath(originalMinidumpPath); });

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  RefPtr<GMPCrashObserver> observer = new GMPCrashObserver();
  obs->AddObserver(observer, "gmp-plugin-crash", false);

  EnsureNSSInitializedChromeOrContent();
  RefPtr<CDMStorageTest> runner = new CDMStorageTest();
  nsCOMPtr<nsIThread> thread(GetGMPThread());
  thread->Dispatch(NewRunnableMethod("CDMStorageTest::TestCrash", runner,
                                     &CDMStorageTest::TestCrash));
  SpinEventLoopUntil("CDMCrashMinidump"_ns, [&]() -> bool {
    return observer->mCrashed && runner->mTerminated;
  });
  obs->RemoveObserver(observer, "gmp-plugin-crash");

  ASSERT_FALSE(observer->mDumpID.IsEmpty())
  << "No minidump was written";
  nsCOMPtr<nsIFile> minidump;
  ASSERT_TRUE(CrashReporter::GetMinidumpForID(observer->mDumpID,
                                              getter_AddRefs(minidump)));
  int64_t size = 0;
  EXPECT_NS_SUCCEEDED(minidump->GetFileSize(&size));
  EXPECT_GT(size, 0);
}
