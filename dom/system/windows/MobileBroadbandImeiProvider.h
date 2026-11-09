/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef MobileBroadbandImeiProvider_h
#define MobileBroadbandImeiProvider_h

#include <functional>

#include "mozilla/AlreadyAddRefed.h"
#include "nsIImeiProvider.h"
#include "nsStringFwd.h"

struct IMbnInterfaceManager;

namespace mozilla::dom {

class MobileBroadbandImeiProvider final : public nsIImeiProvider {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIMEIPROVIDER

  // Returns the interface manager GetImei() queries, or null on failure.
  // Called in the MTA, off the main thread.
  using ManagerFactory =
      std::function<already_AddRefed<IMbnInterfaceManager>()>;

  MobileBroadbandImeiProvider();
  explicit MobileBroadbandImeiProvider(ManagerFactory aManagerFactory);
  static already_AddRefed<MobileBroadbandImeiProvider> GetInstance();

 private:
  ~MobileBroadbandImeiProvider() = default;

  static already_AddRefed<IMbnInterfaceManager> CreateManager();

  // Sets aImei to the device ID of the first mobile broadband interface of
  // aManager that has one. Returns NS_ERROR_NOT_IMPLEMENTED when there is
  // none. Must be called in the MTA.
  static nsresult GetImeiFromManager(IMbnInterfaceManager* aManager,
                                     nsAString& aImei);

  ManagerFactory mManagerFactory;
};

}  // namespace mozilla::dom

#endif /* MobileBroadbandImeiProvider_h */
