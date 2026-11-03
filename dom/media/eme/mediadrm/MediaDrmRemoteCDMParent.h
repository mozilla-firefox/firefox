/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MediaDrmRemoteCDMParent_h_
#define MediaDrmRemoteCDMParent_h_

#include <map>

#include "media/NdkMediaCodec.h"
#include "media/NdkMediaCrypto.h"
#include "media/NdkMediaDrm.h"
#include "media/NdkMediaError.h"
#include "mozilla/Mutex.h"
#include "mozilla/RemoteCDMParent.h"
#include "mozilla/StaticPtr.h"

namespace mozilla {

class MediaDrmCrypto final {
  template <typename T, typename... Args>
  friend RefPtr<T> MakeRefPtr(Args&&...);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDrmCrypto);

 public:
  AMediaCrypto* GetNdkCrypto() const { return mCrypto; }

 private:
  explicit MediaDrmCrypto(AMediaCrypto* aCrypto) : mCrypto(aCrypto) {}

  ~MediaDrmCrypto();

  AMediaCrypto* mCrypto;
};

class MediaDrmCryptoInfo final {
  template <typename T, typename... Args>
  friend already_AddRefed<T> MakeAndAddRef(Args&&...);

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaDrmCryptoInfo);

 public:
  AMediaCodecCryptoInfo* GetNdkCryptoInfo() const { return mCryptoInfo; }

 private:
  explicit MediaDrmCryptoInfo(AMediaCodecCryptoInfo* aCryptoInfo)
      : mCryptoInfo(aCryptoInfo) {}

  ~MediaDrmCryptoInfo();

  AMediaCodecCryptoInfo* mCryptoInfo;
};

class MediaDrmRemoteCDMParent final : public RemoteCDMParent {
  friend class MediaDrmCrypto;
  friend class MediaDrmCryptoInfo;

 public:
  explicit MediaDrmRemoteCDMParent(const nsAString& aKeySystem);

  already_AddRefed<MediaDrmCrypto> GetCrypto() const {
    MutexAutoLock lock(mMutex);
    return do_AddRef(mCrypto);
  }

  bool HasCrypto() const {
    MutexAutoLock lock(mMutex);
    return !!mCrypto;
  }

  already_AddRefed<MediaDrmCryptoInfo> CreateCryptoInfo(MediaRawData* aSample);

  // PRemoteCDMParent
  mozilla::ipc::IPCResult RecvInit(const RemoteCDMInitRequestIPDL& aRequest,
                                   InitResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvCreateSession(
      RemoteCDMCreateSessionRequestIPDL&& aRequest,
      CreateSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvLoadSession(
      const RemoteCDMLoadSessionRequestIPDL& aRequest,
      LoadSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvUpdateSession(
      const RemoteCDMUpdateSessionRequestIPDL& aRequest,
      UpdateSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvRemoveSession(
      const nsString& aSessionId, RemoveSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvCloseSession(
      const nsString& aSessionId, CloseSessionResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvSetServerCertificate(
      mozilla::Span<uint8_t const> aCertificate,
      SetServerCertificateResolver&& aResolver) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

  static void UnprovisionMediaDrmOrigins(
      const nsTArray<nsCString>& aOriginKeys);

 private:
  virtual ~MediaDrmRemoteCDMParent();

  static constexpr uint8_t CLEARKEY_UUID[] = {
      0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9,
      0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e};

  static constexpr uint8_t WIDEVINE_UUID[] = {
      0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
      0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed};

  static void InitializeStatics();

  static void HandleEventCb(AMediaDrm* aMediaDrm,
                            const AMediaDrmSessionId* aSessionId,
                            AMediaDrmEventType aEventType, int aExtra,
                            const uint8_t* aData, size_t aDataSize);

  static void HandleExpirationUpdateCb(AMediaDrm* aDrm,
                                       const AMediaDrmSessionId* aSessionId,
                                       int64_t aExpiryTimeInMS);

  static void HandleKeysChangeCb(AMediaDrm* aDrm,
                                 const AMediaDrmSessionId* aSessionId,
                                 const AMediaDrmKeyStatus* aKeyStatus,
                                 size_t aNumKeys, bool aHasNewUsableKey);

  void HandleEvent(nsString&& aSessionId, AMediaDrmEventType aEventType,
                   int aExtra, nsTArray<uint8_t>&& aData,
                   bool aProvisioned = false);

  void HandleExpirationUpdate(nsString&& aSessionId, int64_t aExpiryTimeInMS);

  void HandleKeysChange(nsString&& aSessionId, bool aHasNewUsableKey,
                        nsTArray<CDMKeyInfo>&& aKeyInfo);

  using InternalPromise =
      MozPromise<bool, MediaResult, /* IsExclusive */ false>;

  // If aProvisioned is true, provisioning was just attempted, so a further not
  // provisioned status is an error rather than a reason to provision again.
  RefPtr<InternalPromise> EnsureHasAMediaCrypto(bool aProvisioned = false);
  RefPtr<InternalPromise> EnsureProvisioned();

  void DoCreateSession(RemoteCDMCreateSessionRequestIPDL&& aRequest,
                       CreateSessionResolver&& aResolver, bool aProvisioned);

  void Destroy();

  using DrmCallbackMap = std::map<AMediaDrm*, MediaDrmRemoteCDMParent*>;
  static StaticAutoPtr<DrmCallbackMap> sCbMap;

  struct SessionEntry {
    AMediaDrmSessionId id;
    nsAutoCString mimeType;
  };

  std::map<nsString, SessionEntry> mSessions;
  MozPromiseHolder<InternalPromise> mProvisionPromise;

  mutable Mutex mMutex;
  RefPtr<MediaDrmCrypto> mCrypto MOZ_GUARDED_BY(mMutex);

  // Points to a static constexpr buffer set in the constructor.
  const uint8_t* mUuid = nullptr;

  // Allocated in RecvInit, freed in ActorDestroy/Destroy.
  AMediaDrm* mDrm = nullptr;

  AMediaDrmSessionId mCryptoSessionId;
};

}  // namespace mozilla

#endif  // MediaDrmRemoteCDMParent_h_
