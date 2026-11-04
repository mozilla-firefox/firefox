/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GMPServiceChild_h_
#define GMPServiceChild_h_

#include "GMPService.h"
#include "MediaResult.h"
#include "base/process.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/gmp/PGMPServiceChild.h"
#include "mozilla/media/MediaUtils.h"
#include "nsRefPtrHashtable.h"

namespace mozilla::gmp {

class GMPContentParent;
class GMPContentParentCloseBlocker;
class GMPServiceChild;

class GeckoMediaPluginServiceChild : public GeckoMediaPluginService {
  friend class GMPServiceChild;

 public:
  static already_AddRefed<GeckoMediaPluginServiceChild> GetSingleton();
  nsresult Init() override;

  NS_DECL_ISUPPORTS_INHERITED

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

  NS_DECL_NSIOBSERVER

  void SetServiceChild(RefPtr<GMPServiceChild>&& aServiceChild);

  void RemoveGMPContentParent(GMPContentParent* aGMPContentParent);

  static void UpdateGMPCapabilities(
      nsTArray<mozilla::dom::GMPCapabilityData>&& aCapabilities);

  // Returns whether the GMP capabilities received from the parent process
  // support aAPI with aTags. This does not need the GMP service to exist.
  static bool HasCapabilityForAPI(const nsACString& aAPI,
                                  const nsTArray<nsCString>& aTags);

  void BeginShutdown();

 protected:
  void InitializePlugins(nsISerialEventTarget*) override {
    // Nothing to do here.
  }

  RefPtr<GetGMPContentParentPromise> GetContentParent(
      GMPCrashHelper* aHelper, const NodeIdVariant& aNodeIdVariant,
      const nsACString& aAPI, const nsTArray<nsCString>& aTags) override;

 private:
  friend class OpenPGMPServiceChild;

  ~GeckoMediaPluginServiceChild() override;

  typedef MozPromise<GMPServiceChild*, MediaResult, /* IsExclusive = */ true>
      GetServiceChildPromise;
  RefPtr<GetServiceChildPromise> GetServiceChild();

  nsTArray<MozPromiseHolder<GetServiceChildPromise>> mGetServiceChildPromises;
  RefPtr<GMPServiceChild> mServiceChild;

  // Shutdown blocker management. A shutdown blocker is used to ensure that
  // we do not shutdown until all content parents are removed from
  // GMPServiceChild::mContentParents.
  //
  // The following rules let us shutdown block (and thus we shouldn't
  // violate them):
  // - We will only call GetContentParent if mShuttingDownOnGMPThread is false.
  // - mShuttingDownOnGMPThread will become true once profile teardown is
  //   observed in the parent process (and once GMPServiceChild receives a
  //   message from GMPServiceParent) or if we block shutdown (which implies
  //   we're in shutdown).
  // - If we currently have mPendingGetContentParents > 0 or parents in
  //   GMPServiceChild::mContentParents we should block shutdown so such
  //   parents can be cleanly shutdown.
  // therefore
  // - We can block shutdown at xpcom-will-shutdown until our content parents
  //   are handled.
  // - Because once mShuttingDownOnGMPThread is true we cannot add new content
  //   parents, we know that when mShuttingDownOnGMPThread && all content
  //   parents are handled we'll never add more and it's safe to stop blocking
  //   shutdown.
  // this relies on
  // - Once we're shutting down, we need to ensure all content parents are
  //   shutdown and removed. Failure to do so will result in a shutdown stall.

  // Note that at the time of writing there are significant differences in how
  // xpcom shutdown is handled in release and non-release builds. For example,
  // in release builds content processes are exited early, so xpcom shutdown
  // is not observed (and not blocked by blockers). This is important to keep
  // in mind when testing the shutdown blocking machinery (you won't see most
  // of it be invoked in release).

  // All of these members should only be used on the GMP thread unless
  // otherwise noted!

  // Add a shutdown blocker. Main thread only. Should only be called once when
  // we init the service.
  nsresult AddShutdownBlocker();
  // Remove a shutdown blocker. Should be called once at most and only when
  // mShuttingDownOnGMPThread. Prefer RemoveShutdownBlockerIfNeeded unless
  // absolutely certain you need to call this directly.
  void RemoveShutdownBlocker();
  // Remove shutdown blocker if the following conditions are met:
  // - mShuttingDownOnGMPThread.
  // - !mServiceChild->HaveContentParents.
  // - mPendingGetContentParents == 0.
  // - mShutdownBlockerHasBeenAdded.
  void RemoveShutdownBlockerIfNeeded();

  // Ticket that controls the shutdown blocker.
  UniquePtr<media::ShutdownBlockingTicket> mShutdownBlocker;
  // The number of GetContentParent calls that have not yet been resolved or
  // rejected. We use this value to help determine if we need to block
  // shutdown. Should only be used on GMP thread to avoid races.
  uint32_t mPendingGetContentParents = 0;
  // End shutdown blocker management.
};

/**
 * This class runs in the content process, and allows the content process to
 * request an IPC connection to the desired GMP process.
 */
class GMPServiceChild : public PGMPServiceChild {
 public:
  // Mark AddRef and Release as `final`, as they overload pure virtual
  // implementations in PGMPServiceChild.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GMPServiceChild, final)

  explicit GMPServiceChild(GeckoMediaPluginServiceChild* aService)
      : mService(aService) {}

  already_AddRefed<GMPContentParent> GetBridgedGMPContentParent(
      ProcessId aOtherPid, ipc::Endpoint<PGMPContentParent>&& endpoint);

  void RemoveGMPContentParent(GMPContentParent* aGMPContentParent);

  bool HasAlreadyBridgedTo(base::ProcessId aPid) const;

  void GetAndBlockAlreadyBridgedTo(
      nsTArray<ProcessId>& aAlreadyBridgedTo,
      nsTArray<RefPtr<GMPContentParentCloseBlocker>>& aBlockers);

  static bool Create(Endpoint<PGMPServiceChild>&& aGMPService);

  ipc::IPCResult RecvBeginShutdown() override;

  bool HaveContentParents() const;

  GeckoMediaPluginServiceChild* Service() const { return mService; }

 private:
  ~GMPServiceChild() = default;

  nsRefPtrHashtable<nsUint64HashKey, GMPContentParent> mContentParents;

  // Raw pointer to our owning service. The service owns us via a RefPtr in
  // mServiceChild, so it strictly outlives us.
  GeckoMediaPluginServiceChild* const mService;
};

}  // namespace mozilla::gmp

#endif  // GMPServiceChild_h_
