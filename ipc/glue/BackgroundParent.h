/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_backgroundparent_h_
#define mozilla_ipc_backgroundparent_h_

#include "base/process.h"
#include "mozilla/Maybe.h"
#include "mozilla/Tainting.h"
#include "mozilla/dom/ContentParent.h"
#include "nsStringFwd.h"
#include "nsTArrayForwardDeclare.h"

#ifdef DEBUG
#  include "nsXULAppAPI.h"
#endif

template <class>
struct already_AddRefed;

namespace mozilla {

namespace dom {

class BlobImpl;
class ContentParent;

}  // namespace dom

namespace ipc {

class BackgroundStarterParent;
class PBackgroundParent;
class PBackgroundStarterParent;

template <class PFooSide>
class Endpoint;

// This class is not designed for public consumption beyond the few static
// member functions.
class BackgroundParent final {
  friend class mozilla::ipc::BackgroundStarterParent;
  friend class mozilla::dom::ContentParent;

  using ProcessId = base::ProcessId;
  using BlobImpl = mozilla::dom::BlobImpl;
  using ContentParent = mozilla::dom::ContentParent;
  using LoadedOriginSet = mozilla::dom::LoadedOriginSet;
  using ThreadsafeContentParentHandle =
      mozilla::dom::ThreadsafeContentParentHandle;

 public:
  // Get the nsISerialEventTarget used to handle messages from BackgroundParent
  // actors, if it is running.
  //
  // This function may only be called on the background or main thread.
  static already_AddRefed<nsISerialEventTarget> GetBackgroundThread();

  // This function allows the caller to determine if the given parent actor
  // corresponds to a child actor from another process or a child actor from a
  // different thread in the same process.
  // This function may only be called on the background thread.
  static bool IsOtherProcessActor(PBackgroundParent* aBackgroundActor);

  // This function returns a handle to the ContentParent associated with the
  // parent actor if the parent actor corresponds to a child actor from another
  // content process. If the parent actor corresponds to a child actor from a
  // different thread in the same process then this function returns null.
  //
  // This function may only be called on the background thread.
  static ThreadsafeContentParentHandle* GetContentParentHandle(
      PBackgroundParent* aBackgroundActor);

  // Return the loaded-origin state associated with a content process actor, or
  // null for an in-process actor.
  //
  // This function may only be called on the background thread.
  static LoadedOriginSet* GetLoadedOrigins(PBackgroundParent* aBackgroundActor);

  static uint64_t GetChildID(PBackgroundParent* aBackgroundActor);

  static mozilla::dom::RemoteType GetRemoteType(
      PBackgroundParent* aBackgroundActor);

  // Whenever receiving a Principal we need to validate that Principal case
  // by case. The options can customize the behaviour of the checks.
  // See ContentParent::ValidatePrincipal for an analog.
  static bool ValidatePrincipal(
      PBackgroundParent* aBackgroundActor, nsIPrincipal* aPrincipal,
      const EnumSet<dom::ValidatePrincipalOptions>& aOptions = {});

  // Validates a tainted principal received over IPC. Returns the untainted
  // principal on success, or Nothing() if validation failed.
  template <typename T>
  static Maybe<T> ValidatePrincipal(
      PBackgroundParent* aBackgroundActor, Tainted<T>& aPrincipal,
      const EnumSet<dom::ValidatePrincipalOptions>& aOptions = {}) {
    if (!MOZ_IS_VALID(aPrincipal, ValidatePrincipal(aBackgroundActor,
                                                    aPrincipal, aOptions))) {
      return Nothing();
    }
    return Some(MOZ_NO_VALIDATE(aPrincipal, "Validated above"));
  }

  static bool ValidatePrincipalInfo(
      PBackgroundParent* aBackgroundActor, const PrincipalInfo& aPrincipalInfo,
      const EnumSet<dom::ValidatePrincipalOptions>& aOptions = {});

  static void KillHardAsync(PBackgroundParent* aBackgroundActor,
                            const nsACString& aReason);

 private:
  // Only called by ContentParent for cross-process actors.
  static bool AllocStarter(ContentParent* aContent,
                           Endpoint<PBackgroundStarterParent>&& aEndpoint);
};

// Implemented in BackgroundImpl.cpp.
bool IsOnBackgroundThread();

#ifdef DEBUG

// Implemented in BackgroundImpl.cpp.
void AssertIsOnBackgroundThread();

#else

inline void AssertIsOnBackgroundThread() {}

#endif  // DEBUG

inline void AssertIsInMainProcess() { MOZ_ASSERT(XRE_IsParentProcess()); }

}  // namespace ipc
}  // namespace mozilla

#endif  // mozilla_ipc_backgroundparent_h_
