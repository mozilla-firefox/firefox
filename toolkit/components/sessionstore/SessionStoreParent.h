/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SessionStoreParent_h
#define mozilla_dom_SessionStoreParent_h

#include "mozilla/dom/BrowserSessionStore.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/PSessionStoreParent.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/SessionStoreScrollData.h"

namespace mozilla::dom {
class BrowserParent;

class SessionStoreParent final : public PSessionStoreParent {
 public:
  SessionStoreParent(CanonicalBrowsingContext* aBrowsingContext,
                     BrowserSessionStore* aSessionStore);

  void FlushAllSessionStoreChildren(const std::function<void()>& aDone);

  void FinalFlushAllSessionStoreChildren(const std::function<void()>& aDone);

  /**
   * Sends data to be stored and instructions to the session store to
   * potentially collect data in the parent.
   */
  mozilla::ipc::IPCResult RecvSessionStoreUpdate(
      const Maybe<nsCString>& aDocShellCaps, const Maybe<bool>& aPrivatedMode,
      const MaybeSessionStoreZoom& aZoom, const bool aNeedCollectSHistory,
      const uint32_t& aEpoch);

  mozilla::ipc::IPCResult RecvIncrementalSessionStoreUpdate(
      const MaybeDiscarded<BrowsingContext>& aBrowsingContext,
      const Maybe<FormData>& aFormData, const Maybe<nsPoint>& aScrollPosition,
      uint32_t aEpoch);

  mozilla::ipc::IPCResult RecvResetSessionStore(
      const MaybeDiscarded<BrowsingContext>& aBrowsingContext, uint32_t aEpoch);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(SessionStoreParent)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(SessionStoreParent)

 protected:
  friend class SessionStoreChild;
  void SessionStoreUpdate(const Maybe<nsCString>& aDocShellCaps,
                          const Maybe<bool>& aPrivatedMode,
                          const MaybeSessionStoreZoom& aZoom,
                          const bool aNeedCollectSHistory,
                          const uint32_t& aEpoch);

  void IncrementalSessionStoreUpdate(
      const MaybeDiscarded<BrowsingContext>& aBrowsingContext,
      const Maybe<FormData>& aFormData, const Maybe<nsPoint>& aScrollPosition,
      uint32_t aEpoch);

  void ResetSessionStore(
      const MaybeDiscarded<BrowsingContext>& aBrowsingContext, uint32_t aEpoch);

 private:
  ~SessionStoreParent() = default;

  bool IsAllowedUpdateTarget(CanonicalBrowsingContext* aBrowsingContext);

  already_AddRefed<SessionStoreParent::FlushTabStatePromise>
  FlushSessionStore();

  bool mHasNewFormData = false;
  bool mHasNewScrollPosition = false;

  RefPtr<CanonicalBrowsingContext> mBrowsingContext;
  RefPtr<BrowserSessionStore> mSessionStore;
};
}  // namespace mozilla::dom

#endif  // mozilla_dom_SessionStoreParent_h
