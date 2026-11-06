/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MockNetworkLayerController_h_
#define MockNetworkLayerController_h_

#include "mozilla/RWLock.h"
#include "mozilla/net/DNS.h"
#include "nsIMockNetworkLayerController.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"

namespace mozilla::net {

bool FindNetAddrOverride(const NetAddr& aInput, NetAddr& aOutput);
bool FindBlockedTCPConnect(const NetAddr& aInput);
bool FindPausedTCPConnect(const NetAddr& aInput);
bool FindBlockedUDPAddr(const NetAddr& aInput);
bool FindFailedUDPAddr(const NetAddr& aInput);
bool FindWouldBlockUDPAddr(const NetAddr& aInput);

class MockNetworkLayerController : public nsIMockNetworkLayerController {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIMOCKNETWORKLAYERCONTROLLER

  MockNetworkLayerController() = default;

  static already_AddRefed<nsIMockNetworkLayerController> GetSingleton();

 private:
  virtual ~MockNetworkLayerController() = default;
  mozilla::RWLock mLock{"MockNetworkLayerController::mLock"};

  nsTHashMap<nsCStringHashKey, NetAddr> mNetAddrOverrides MOZ_GUARDED_BY(mLock);
  nsTHashSet<nsCStringHashKey> mBlockedTCPConnects MOZ_GUARDED_BY(mLock);
  nsTHashSet<nsCStringHashKey> mPausedTCPConnects MOZ_GUARDED_BY(mLock);
  nsTHashSet<nsCStringHashKey> mBlockedUDPAddresses MOZ_GUARDED_BY(mLock);
  nsTHashSet<nsCStringHashKey> mFailedUDPAddresses MOZ_GUARDED_BY(mLock);
  nsTHashSet<nsCStringHashKey> mWouldBlockUDPAddresses MOZ_GUARDED_BY(mLock);

  friend bool FindNetAddrOverride(const NetAddr& aInput, NetAddr& aOutput);
  friend bool FindBlockedTCPConnect(const NetAddr& aInput);
  friend bool FindPausedTCPConnect(const NetAddr& aInput);
  friend bool FindBlockedUDPAddr(const NetAddr& aInput);
  friend bool FindFailedUDPAddr(const NetAddr& aInput);
  friend bool FindWouldBlockUDPAddr(const NetAddr& aInput);
};

}  // namespace mozilla::net

#endif  // MockNetworkLayerController_h_
