/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef NSNOTIFYADDRLISTENER_H_
#define NSNOTIFYADDRLISTENER_H_

#include <windows.h>
#include <winsock2.h>
#include <iptypes.h>
#include "nsINetworkLinkService.h"
#include "nsIRunnable.h"
#include "nsIObserver.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsThreadPool.h"
#include "nsCOMPtr.h"
#include "mozilla/Atomics.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Mutex.h"
#include "mozilla/SHA1.h"
#include "mozilla/net/DNS.h"

class nsIThreadPool;

class nsNotifyAddrListener : public nsINetworkLinkService,
                             public nsIRunnable,
                             public nsIObserver {
  virtual ~nsNotifyAddrListener();

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSINETWORKLINKSERVICE
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIOBSERVER

  nsNotifyAddrListener();

  nsresult Init(void);
  void CheckLinkStatus(void);
  static void HashSortedNetworkIds(const std::vector<GUID> nwGUIDS,
                                   mozilla::SHA1Sum& sha1);

 protected:
  mozilla::Atomic<bool, mozilla::Relaxed> mLinkUp{true};
  mozilla::Atomic<bool, mozilla::Relaxed> mStatusKnown{false};
  bool mCheckAttempted{false};

  nsresult Shutdown(void);
  nsresult NotifyObservers(const char* aTopic, const char* aData);

  DWORD CheckAdaptersAddresses(void);

  // This threadpool only ever holds 1 thread. It is a threadpool and not a
  // regular thread so that we may call shutdownWithTimeout on it.
  nsCOMPtr<nsIThreadPool> mThread;

 private:
  // Returns the new timeout period for coalescing (or INFINITE)
  DWORD nextCoalesceWaitTime();

  // Called for every detected network change
  nsresult NetworkChanged();

  // Figure out the current network identification
  void calculateNetworkId(void);
  bool findMac(char* gateway);

  mozilla::Mutex mMutex{"nsNotifyAddrListener::mMutex"};
  nsCString mNetworkId MOZ_GUARDED_BY(mMutex);
  nsTArray<nsCString> mDnsSuffixList MOZ_GUARDED_BY(mMutex);
  nsTArray<mozilla::net::NetAddr> mDNSResolvers MOZ_GUARDED_BY(mMutex);

  HANDLE mCheckEvent{nullptr};

  // set true when mCheckEvent means shutdown
  mozilla::Atomic<bool> mShutdown{false};

  // Contains a set of flags that codify the reasons for which
  // the platform indicates DNS should be used instead of TRR.
  mozilla::Atomic<uint32_t, mozilla::Relaxed> mPlatformDNSIndications{
      NONE_DETECTED};

  // This is a checksum of various meta data for all network interfaces
  // considered UP at last check.
  ULONG mIPInterfaceChecksum{0};

  // start time of the checking
  mozilla::TimeStamp mStartTime;

  // Flag set while coalescing change events
  bool mCoalescingActive{false};

  // Time stamp for first event during coalescing
  mozilla::TimeStamp mChangeTime;
};

#endif /* NSNOTIFYADDRLISTENER_H_ */
