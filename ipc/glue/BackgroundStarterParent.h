/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_BackgroundStarterParent_h
#define mozilla_ipc_BackgroundStarterParent_h

#include "mozilla/ipc/PBackgroundStarterParent.h"
#include "mozilla/dom/ContentParent.h"
#include "nsISupportsImpl.h"

namespace mozilla::ipc {

class BackgroundStarterParent final : public PBackgroundStarterParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING_WITH_DELETE_ON_MAIN_THREAD(
      BackgroundStarterParent, override)

  // Implemented in BackgroundImpl.cpp
  BackgroundStarterParent(mozilla::dom::ThreadsafeContentParentHandle* aContent,
                          bool aCrossProcess);

 private:
  friend class PBackgroundStarterParent;
  ~BackgroundStarterParent() = default;

  // Implemented in BackgroundImpl.cpp
  IPCResult RecvInitBackground(Endpoint<PBackgroundParent>&& aEndpoint);

  const bool mCrossProcess;

  const RefPtr<mozilla::dom::ThreadsafeContentParentHandle> mContent;
};

}  // namespace mozilla::ipc

#endif  // mozilla_ipc_BackgroundStarterParent_h
