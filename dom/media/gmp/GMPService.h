/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPService_h_
#define GMPService_h_

#include "GMPContentParent.h"
#include "GMPCrashHelper.h"
#include "gmp-video-codec.h"
#include "mozIGeckoMediaPluginService.h"
#include "mozilla/Atomics.h"
#include "mozilla/MozPromise.h"
#include "mozilla/gmp/GMPTypes.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsIObserver.h"
#include "nsString.h"
#include "nsTArray.h"

class nsIAsyncShutdownClient;
class nsIRunnable;
class nsISerialEventTarget;
class nsIThread;

template <class>
struct already_AddRefed;

namespace mozilla {

class GMPCrashHelper;
class MediaResult;

extern LogModule* GetGMPLog();
extern LogModule* GetGMPLibraryLog();
extern GMPLogLevel GetGMPLibraryLogLevel();

namespace gmp {

using GetGMPContentParentPromise =
    MozPromise<RefPtr<GMPContentParentCloseBlocker>, MediaResult,
               /* IsExclusive = */ true>;
using GetCDMParentPromise = MozPromise<RefPtr<ChromiumCDMParent>, MediaResult,
                                       /* IsExclusive = */ true>;

class GeckoMediaPluginService : public mozIGeckoMediaPluginService,
                                public nsIObserver {
 public:
  static already_AddRefed<GeckoMediaPluginService> GetGeckoMediaPluginService();

  virtual nsresult Init();

  NS_DECL_THREADSAFE_ISUPPORTS

  RefPtr<GetCDMParentPromise> GetCDM(const NodeIdParts& aNodeIdParts,
                                     const nsACString& aKeySystem,
                                     GMPCrashHelper* aHelper);

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
  RefPtr<GetGMPContentParentPromise> GetContentParentForTest();
#endif

  // mozIGeckoMediaPluginService
  NS_IMETHOD GetThread(nsIThread** aThread) override MOZ_EXCLUDES(mMutex);
  nsresult GetThreadLocked(nsIThread** aThread) MOZ_REQUIRES(mMutex);
  NS_IMETHOD GetGMPVideoDecoder(
      GMPCrashHelper* aHelper, nsTArray<nsCString>* aTags,
      const nsACString& aNodeId,
      UniquePtr<GetGMPVideoDecoderCallback>&& aCallback) override;
  NS_IMETHOD GetGMPVideoEncoder(
      GMPCrashHelper* aHelper, nsTArray<nsCString>* aTags,
      const nsACString& aNodeId,
      UniquePtr<GetGMPVideoEncoderCallback>&& aCallback) override;

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD RunPluginCrashCallbacks(
      uint32_t aPluginId, const nsACString& aPluginName) override;

  already_AddRefed<nsISerialEventTarget> GetGMPThread();

  void ConnectCrashHelper(uint32_t aPluginId, GMPCrashHelper* aHelper);
  void DisconnectCrashHelper(GMPCrashHelper* aHelper);

  bool XPCOMWillShutdownReceived() const { return mXPCOMWillShutdown; }

 protected:
  GeckoMediaPluginService();
  virtual ~GeckoMediaPluginService();

  void AssertOnGMPThread() {
#ifdef DEBUG
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mGMPThread->IsOnCurrentThread());
#endif
  }

  virtual void InitializePlugins(nsISerialEventTarget* aGMPThread) = 0;

  virtual RefPtr<GetGMPContentParentPromise> GetContentParent(
      GMPCrashHelper* aHelper, const NodeIdVariant& aNodeIdVariant,
      const nsACString& aAPI, const nsTArray<nsCString>& aTags) = 0;

  nsresult GMPDispatch(nsIRunnable* event, nsIEventTarget::DispatchFlags flags =
                                               NS_DISPATCH_NORMAL);
  nsresult GMPDispatch(
      already_AddRefed<nsIRunnable> event,
      nsIEventTarget::DispatchFlags flags = NS_DISPATCH_NORMAL);
  void ShutdownGMPThread();

  static nsCOMPtr<nsIAsyncShutdownClient> GetShutdownBarrier();

  Mutex mMutex;  // Protects mGMPThread, mPluginCrashHelpers,
                 // mGMPThreadShutdown and some members in
                 // derived classes.

  const nsCOMPtr<nsISerialEventTarget> mMainThread;

  nsCOMPtr<nsIThread> mGMPThread MOZ_GUARDED_BY(mMutex);
  bool mGMPThreadShutdown MOZ_GUARDED_BY(mMutex);
  bool mShuttingDownOnGMPThread;
  Atomic<bool> mXPCOMWillShutdown;

  nsClassHashtable<nsUint32HashKey, nsTArray<RefPtr<GMPCrashHelper>>>
      mPluginCrashHelpers MOZ_GUARDED_BY(mMutex);
};

}  // namespace gmp
}  // namespace mozilla

#endif  // GMPService_h_
