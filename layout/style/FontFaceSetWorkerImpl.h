/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FontFaceSetWorkerImpl_h
#define mozilla_dom_FontFaceSetWorkerImpl_h

#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/FontFaceSetImpl.h"

namespace mozilla::dom {
class ThreadSafeWorkerRef;
class WorkerFontVisibilityProvider;
class WorkerPrivate;

class FontFaceSetWorkerImpl final : public FontFaceSetImpl {
  NS_DECL_ISUPPORTS_INHERITED

 public:
  explicit FontFaceSetWorkerImpl(FontFaceSet* aOwner);

  bool Initialize(WorkerPrivate* aWorkerPrivate);
  void Destroy() override;

  bool IsOnOwningThread() override;
#ifdef DEBUG
  void AssertIsOnOwningThread() override;
#endif
  void DispatchToOwningThread(const char* aName,
                              std::function<void()>&& aFunc) override;

  already_AddRefed<URLExtraData> GetURLExtraData() override;

  void FlushUserFontSet() override;

  // gfxUserFontSet

  already_AddRefed<gfxUserFontFamily> LookupFamily(
      const nsACString& aName) const override;

  nsresult StartLoad(gfxUserFontEntry* aUserFontEntry,
                     uint32_t aSrcIndex) override;

  bool IsFontLoadAllowed(const gfxFontFaceSrc&) override;

  FontVisibilityProvider* GetFontVisibilityProvider() const override;

 private:
  ~FontFaceSetWorkerImpl() override;

  void InitializeOnMainThread();

  uint64_t GetInnerWindowID() override;

  nsresult CreateChannelForSyncLoadFontData(
      nsIChannel** aOutChannel, gfxUserFontEntry* aFontToLoad,
      const gfxFontFaceSrc* aFontFaceSrc) override;

  TimeStamp GetNavigationStartTimeStamp() override;

  RefPtr<ThreadSafeWorkerRef> mWorkerRef MOZ_GUARDED_BY(mMutex);

  RefPtr<URLExtraData> mURLExtraData MOZ_GUARDED_BY(mMutex);

  // Font loads happen on the main thread, but the required ClientInfo is only
  // accessible on the worker thread.
  Maybe<ClientInfo> mClientInfo MOZ_GUARDED_BY(mMutex);

  // It's important that this outlives the worker shutdown.
  RefPtr<WorkerFontVisibilityProvider> mFontVisibilityProvider
      MOZ_GUARDED_BY(mMutex);
};

}  // namespace mozilla::dom

#endif  // !defined(mozilla_dom_FontFaceSetWorkerImpl_h)
