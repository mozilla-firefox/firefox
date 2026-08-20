/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_FOG_h
#define mozilla_FOG_h

#include "nsIFOG.h"
#include "nsIObserver.h"
#include "nsIMemoryReporter.h"
#include "mozilla/Atomics.h"

namespace mozilla {
class FOG final : public nsIFOG, public nsIObserver, public nsIMemoryReporter {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIFOG
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIMEMORYREPORTER

 public:
  FOG() = default;
  static already_AddRefed<FOG> GetSingleton();
  void InitMemoryReporter();

  /**
   * Sets the application_id to initialize Glean with.
   * Does not take precedence over `nsIFOG.initializeFOG`'s `aAppIdOverride`.
   * Only has effect if called before `nsIFOG.initializeFOG`.
   * Parent-process only.
   */
  static void SetApplicationID(const nsACString& aAppId);

 private:
  ~FOG() = default;
  static bool ApplyInterestingServerKnobs();
  void Shutdown();

  mozilla::Atomic<bool> mIsShutdown;
};

};  // namespace mozilla

#endif  // mozilla_FOG_h
