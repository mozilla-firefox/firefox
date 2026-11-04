/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPServiceParent_h_
#define GMPServiceParent_h_

#include "GMPService.h"
#include "GMPStorage.h"
#include "mozIGeckoMediaPluginChromeService.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/gmp/PGMPParent.h"
#include "mozilla/gmp/PGMPServiceParent.h"
#include "mozilla/media/MediaUtils.h"
#include "nsClassHashtable.h"
#include "nsIAsyncShutdown.h"
#include "nsNetUtil.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"
#include "nsThreadUtils.h"

template <class>
struct already_AddRefed;
using FlushFOGDataPromise = mozilla::dom::ContentParent::FlushFOGDataPromise;
using ContentParent = mozilla::dom::ContentParent;

namespace mozilla {
class OriginAttributesPattern;

namespace gmp {

class GMPParent;
class GMPServiceParent;

class GeckoMediaPluginServiceParent final
    : public GeckoMediaPluginService,
      public mozIGeckoMediaPluginChromeService {
 public:
  static already_AddRefed<GeckoMediaPluginServiceParent> GetSingleton();

  GeckoMediaPluginServiceParent();
  nsresult Init() override;

  NS_DECL_ISUPPORTS_INHERITED

  // mozIGeckoMediaPluginService
  NS_IMETHOD HasPluginForAPI(const nsACString& aAPI,
                             const nsTArray<nsCString>& aTags,
                             bool* aRetVal) override;
  NS_IMETHOD FindPluginDirectoryForAPI(const nsACString& aAPI,
                                       const nsTArray<nsCString>& aTags,
                                       nsIFile** aDirectory) override;
  NS_IMETHOD GetNodeId(const nsAString& aOrigin,
                       const nsAString& aTopLevelOrigin,
                       const nsAString& aGMPName,
                       UniquePtr<GetNodeIdCallback>&& aCallback) override;

  NS_DECL_MOZIGECKOMEDIAPLUGINCHROMESERVICE
  NS_DECL_NSIOBSERVER

  RefPtr<GenericNonExclusivePromise> EnsureInitialized();
  RefPtr<GenericPromise> AsyncAddPluginDirectory(const nsAString& aDirectory);

  // GMP thread access only
  bool IsShuttingDown();

  already_AddRefed<GMPStorage> GetMemoryStorageFor(const nsACString& aNodeId,
                                                   const nsAString& aGMPName);
  nsresult ForgetThisSiteNative(
      const nsAString& aSite, const mozilla::OriginAttributesPattern& aPattern);

  nsresult ForgetThisBaseDomainNative(const nsAString& aBaseDomain);

  // Notifies that some user of this class is created/destroyed.
  void ServiceUserCreated(GMPServiceParent* aServiceParent);
  void ServiceUserDestroyed(GMPServiceParent* aServiceParent);

  // If aContentProcess is specified, this will only update GMP caps in that
  // content process, otherwise will update all content processes.
  void UpdateContentProcessGMPCapabilities(
      ContentParent* aContentProcess = nullptr);

  void SendFlushFOGData(nsTArray<RefPtr<FlushFOGDataPromise>>& promises);

#if defined(XP_WIN)
  using GetUntrustedModulesDataPromise =
      PGMPParent::GetUntrustedModulesDataPromise;

  void SendGetUntrustedModulesData(
      nsTArray<RefPtr<GetUntrustedModulesDataPromise>>& promises);

  void SendUnblockUntrustedModulesThread();
#endif

  /*
   * ** Test-only Method **
   *
   * Trigger GMP-process test metric instrumentation.
   */
  RefPtr<PGMPParent::TestTriggerMetricsPromise> TestTriggerMetrics();

 private:
  friend class GMPServiceParent;
  class Observer;

  virtual ~GeckoMediaPluginServiceParent();

  void ClearTemporaryStorage();
  void ClearStorage();

  already_AddRefed<GMPParent> SelectPluginForAPI(
      const nsACString& aNodeId, const nsACString& aAPI,
      const nsTArray<nsCString>& aTags);

  already_AddRefed<GMPParent> FindPluginForAPIFrom(
      size_t aSearchStartIndex, const nsACString& aAPI,
      const nsTArray<nsCString>& aTags, size_t* aOutPluginIndex)
      MOZ_REQUIRES(mMutex);

  nsresult GetNodeId(const nsAString& aOrigin, const nsAString& aTopLevelOrigin,
                     const nsAString& aGMPName, nsACString& aOutId);

  void UnloadPlugins();
  void CrashPlugins();
  void NotifySyncShutdownComplete();

  void RemoveOnGMPThread(const nsAString& aDirectory,
                         const bool aDeleteFromDisk, const bool aCanDefer);

  struct DirectoryFilter {
    virtual bool operator()(nsIFile* aPath) = 0;
    ~DirectoryFilter() = default;
  };
  void ClearNodeIdAndPlugin(DirectoryFilter& aFilter);
  void ClearNodeIdAndPlugin(nsIFile* aPluginStorageDir,
                            DirectoryFilter& aFilter);
  void ForgetThisSiteOnGMPThread(
      const nsACString& aSite,
      const mozilla::OriginAttributesPattern& aPattern);
  void ForgetThisBaseDomainOnGMPThread(const nsACString& aBaseDomain);
  void ClearRecentHistoryOnGMPThread(PRTime aSince);
  void OnPreferenceChanged(mozilla::dom::Pref&& aPref);

  already_AddRefed<GMPParent> GetById(uint32_t aPluginId);

 protected:
  friend class GMPParent;
  void ReAddOnGMPThread(const RefPtr<GMPParent>& aOld);
  void PluginTerminated(const RefPtr<GMPParent>& aOld);
  void InitializePlugins(nsISerialEventTarget* GMPThread) override;
  RefPtr<GenericPromise> LoadFromEnvironment();
  RefPtr<GenericPromise> AddOnGMPThread(nsString aDirectory);

  RefPtr<GetGMPContentParentPromise> GetContentParent(
      GMPCrashHelper* aHelper, const NodeIdVariant& aNodeIdVariant,
      const nsACString& aAPI, const nsTArray<nsCString>& aTags) override;

 private:
  already_AddRefed<GMPParent> CreateGMPParent();

  // Creates a copy of aOriginal. Note that the caller is responsible for
  // adding this to GeckoMediaPluginServiceParent::mPlugins.
  already_AddRefed<GMPParent> ClonePlugin(const GMPParent* aOriginal);
  nsresult EnsurePluginsOnDiskScanned();
  nsresult InitStorage();

  // Get a string based node ID from a NodeIdVariant. This will
  // either fetch the internal string, or convert the internal NodeIdParts to a
  // string. The conversion process is fallible, so the return value should be
  // checked.
  nsresult GetNodeId(const NodeIdVariant& aNodeIdVariant, nsACString& aOutId);

  class PathRunnable : public Runnable {
   public:
    enum EOperation {
      REMOVE,
      REMOVE_AND_DELETE_FROM_DISK,
    };

    PathRunnable(GeckoMediaPluginServiceParent* aService,
                 const nsAString& aPath, EOperation aOperation,
                 bool aDefer = false)
        : Runnable("gmp::GeckoMediaPluginServiceParent::PathRunnable"),
          mService(aService),
          mPath(aPath),
          mOperation(aOperation),
          mDefer(aDefer) {}

    NS_DECL_NSIRUNNABLE

   private:
    RefPtr<GeckoMediaPluginServiceParent> mService;
    nsString mPath;
    EOperation mOperation;
    bool mDefer;
  };

  // Protected by mMutex from the base class.
  nsTArray<RefPtr<GMPParent>> mPlugins MOZ_GUARDED_BY(mMutex);

  template <typename T>
  class MainThreadOnly {
   public:
    MOZ_IMPLICIT MainThreadOnly(T aValue) : mValue(aValue) {}
    operator T&() {
      MOZ_ASSERT(NS_IsMainThread());
      return mValue;
    }

   private:
    T mValue;
  };

  MainThreadOnly<bool> mShuttingDown;
  MainThreadOnly<bool> mWaitingForPluginsSyncShutdown;

  nsTArray<nsString> mPluginsWaitingForDeletion;

  nsCOMPtr<nsIFile> mStorageBaseDir;

  // Hashes of (origin,topLevelOrigin) to the node id for
  // non-persistent sessions.
  nsClassHashtable<nsUint32HashKey, nsCString> mTempNodeIds;

  // Hashes node id to whether that node id is allowed to store data
  // persistently on disk.
  nsTHashMap<nsCStringHashKey, bool> mPersistentStorageAllowed;

  // Synchronization for barrier that ensures we've loaded GMPs from
  // MOZ_GMP_PATH before allowing GetContentParentFrom() to proceed.
  Monitor mInitPromiseMonitor;
  MozMonitoredPromiseHolder<GenericNonExclusivePromise> mInitPromise;
  bool mLoadPluginsFromDiskComplete;

  // Hashes nodeId to the hashtable of storage for that nodeId.
  nsRefPtrHashtable<nsCStringHashKey, GMPStorage> mTempGMPStorage;

  // Tracks how many IPC connections to GMPServices running in content
  // processes we have. When this is empty we can safely shut down.
  // Synchronized across thread via mMutex in base class.
  nsTArray<GMPServiceParent*> mServiceParents;

  uint32_t mDirectoriesAdded = 0;
  uint32_t mDirectoriesInProgress = 0;
};

nsresult WriteToFile(nsIFile* aPath, const nsACString& aFileName,
                     const nsACString& aData);
nsresult ReadSalt(nsIFile* aPath, nsACString& aOutData);
bool MatchOrigin(nsIFile* aPath, const nsACString& aSite,
                 const mozilla::OriginAttributesPattern& aPattern);
bool MatchBaseDomain(nsIFile* aPath, const nsACString& aBaseDomain);

/**
 * This class runs in the parent process, and manages the lifecycle of the GMP
 * process and brokering the creation of PGMPContent between the parent/content
 * processes and the GMP process.
 */
class GMPServiceParent final : public PGMPServiceParent {
 public:
  explicit GMPServiceParent(GeckoMediaPluginServiceParent* aService);

  // Our refcounting is thread safe, and when our refcount drops to zero
  // we dispatch an event to the main thread to delete the GMPServiceParent.
  // Note that this means it's safe for references to this object to be
  // released on a non main thread, but the destructor will always run on
  // the main thread.

  // Mark AddRef and Release as `final`, as they overload pure virtual
  // implementations in PGMPServiceParent.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD(
      GMPServiceParent, final);

  ipc::IPCResult RecvGetGMPNodeId(const nsAString& aOrigin,
                                  const nsAString& aTopLevelOrigin,
                                  const nsAString& aGMPName,
                                  GetGMPNodeIdResolver&& aResolve) override;

  static bool Create(Endpoint<PGMPServiceParent>&& aGMPService);

  ipc::IPCResult RecvLaunchGMP(const NodeIdVariant& aNodeIdVariant,
                               const nsACString& aAPI,
                               nsTArray<nsCString>&& aTags,
                               nsTArray<ProcessId>&& aAlreadyBridgedTo,
                               LaunchGMPResolver&& aResolve) override;

  // Releases the profile-before-change shutdown blocker. Called from
  // GeckoMediaPluginServiceParent::UnloadPlugins(). Must be called with
  // mService->mMutex held (asserted); safe to null the UniquePtr from any
  // thread because ~ShutdownBlockingTicketImpl posts RemoveBlocker to the
  // main thread internally.
  void BeginShutdown();

 private:
  ~GMPServiceParent();

  const RefPtr<GeckoMediaPluginServiceParent> mService;

  // Ticket that holds a blocker on the profile-before-change barrier.
  // Released when this actor is destroyed, or proactively from BeginShutdown()
  // so that profile-before-change isn't blocked waiting for the PGMPService
  // channel to close at xpcom-shutdown-threads.
  UniquePtr<media::ShutdownBlockingTicket> mShutdownBlocker;
};

}  // namespace gmp
}  // namespace mozilla

#endif  // GMPServiceParent_h_
