/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/CacheOpParent.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/dom/cache/AutoUtils.h"
#include "mozilla/dom/cache/CacheStreamControlParent.h"
#include "mozilla/dom/cache/ManagerId.h"
#include "mozilla/dom/cache/ReadStream.h"
#include "mozilla/dom/cache/SavedTypes.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/InputStreamUtils.h"

namespace mozilla::dom::cache {

using mozilla::ipc::PBackgroundParent;

CacheOpParent::CacheOpParent(const WeakRefParentType& aIpcManager,
                             const CacheOpArgs& aOpArgs, CacheId aCacheId,
                             Namespace aNamespace)
    : mIpcManager(aIpcManager),
      mCacheId(aCacheId),
      mNamespace(aNamespace),
      mOpArgs(aOpArgs) {
  MOZ_DIAGNOSTIC_ASSERT(mIpcManager.isSome());
}

CacheOpParent::~CacheOpParent() { NS_ASSERT_OWNINGTHREAD(CacheOpParent); }

void CacheOpParent::Execute(const SafeRefPtr<ManagerId>& aManagerId) {
  NS_ASSERT_OWNINGTHREAD(CacheOpParent);
  MOZ_DIAGNOSTIC_ASSERT(!mManager);
  MOZ_DIAGNOSTIC_ASSERT(!mVerifier);

  auto managerOrErr = cache::Manager::AcquireCreateIfNonExistent(aManagerId);
  if (NS_WARN_IF(managerOrErr.isErr())) {
    (void)Send__delete__(this, CopyableErrorResult(managerOrErr.unwrapErr()),
                         void_t());
    return;
  }

  Execute(managerOrErr.unwrap());
}

void CacheOpParent::Execute(SafeRefPtr<cache::Manager> aManager) {
  NS_ASSERT_OWNINGTHREAD(CacheOpParent);
  MOZ_DIAGNOSTIC_ASSERT(!mManager);
  MOZ_DIAGNOSTIC_ASSERT(!mVerifier);

  mManager = std::move(aManager);

  // Handle put op
  if (mOpArgs.type() == CacheOpArgs::TCachePutAllArgs) {
    MOZ_DIAGNOSTIC_ASSERT(mCacheId != INVALID_CACHE_ID);

    const CachePutAllArgs& args = mOpArgs.get_CachePutAllArgs();
    const nsTArray<CacheRequestResponse>& list = args.requestResponseList();

    AutoTArray<nsCOMPtr<nsIInputStream>, 256> requestStreamList;
    AutoTArray<nsCOMPtr<nsIInputStream>, 256> responseStreamList;

    for (uint32_t i = 0; i < list.Length(); ++i) {
      requestStreamList.AppendElement(
          DeserializeCacheStream(list[i].request().body()));
      responseStreamList.AppendElement(
          DeserializeCacheStream(list[i].response().body()));
    }

    mManager->ExecutePutAll(this, mCacheId, args.requestResponseList(),
                            requestStreamList, responseStreamList);
    return;
  }

  // Handle all other cache ops
  if (mCacheId != INVALID_CACHE_ID) {
    MOZ_DIAGNOSTIC_ASSERT(mNamespace == INVALID_NAMESPACE);
    mManager->ExecuteCacheOp(this, mCacheId, mOpArgs);
    return;
  }

  // Handle all storage ops
  MOZ_DIAGNOSTIC_ASSERT(mNamespace != INVALID_NAMESPACE);
  mManager->ExecuteStorageOp(this, mNamespace, mOpArgs);
}

void CacheOpParent::WaitForVerification(PrincipalVerifier* aVerifier) {
  NS_ASSERT_OWNINGTHREAD(CacheOpParent);
  MOZ_DIAGNOSTIC_ASSERT(!mManager);
  MOZ_DIAGNOSTIC_ASSERT(!mVerifier);

  mVerifier = aVerifier;
  mVerifier->AddListener(*this);
}

void CacheOpParent::ActorDestroy(ActorDestroyReason aReason) {
  NS_ASSERT_OWNINGTHREAD(CacheOpParent);

  if (mVerifier) {
    mVerifier->RemoveListener(*this);
    mVerifier = nullptr;
  }

  if (mManager) {
    mManager->RemoveListener(this);
    mManager = nullptr;
  }

  mIpcManager.destroy();
}

void CacheOpParent::OnPrincipalVerified(
    nsresult aRv, const SafeRefPtr<ManagerId>& aManagerId) {
  NS_ASSERT_OWNINGTHREAD(CacheOpParent);

  mVerifier->RemoveListener(*this);
  mVerifier = nullptr;

  if (NS_WARN_IF(NS_FAILED(aRv))) {
    (void)Send__delete__(this, CopyableErrorResult(aRv), void_t());
    return;
  }

  Execute(aManagerId);
}

void CacheOpParent::OnOpComplete(ErrorResult&& aRv,
                                 const CacheOpResult& aResult,
                                 CacheId aOpenedCacheId,
                                 const Maybe<StreamInfo>& aStreamInfo) {
  NS_ASSERT_OWNINGTHREAD(CacheOpParent);
  MOZ_DIAGNOSTIC_ASSERT(mIpcManager.isSome());
  MOZ_DIAGNOSTIC_ASSERT(mManager);

  // Never send an op-specific result if we have an error.  Instead, send
  // void_t() to ensure that we don't leak actors on the child side.
  if (NS_WARN_IF(aRv.Failed())) {
    (void)Send__delete__(this, CopyableErrorResult(std::move(aRv)), void_t());
    return;
  }

  if (aStreamInfo.isSome()) {
    ProcessCrossOriginResourcePolicyHeader(aRv,
                                           aStreamInfo->mSavedResponseList);
    if (NS_WARN_IF(aRv.Failed())) {
      (void)Send__delete__(this, CopyableErrorResult(std::move(aRv)), void_t());
      return;
    }
  }

  uint32_t entryCount =
      std::max(1lu, aStreamInfo ? static_cast<unsigned long>(std::max(
                                      aStreamInfo->mSavedResponseList.Length(),
                                      aStreamInfo->mSavedRequestList.Length()))
                                : 0lu);

  // The result must contain the appropriate type at this point.  It may
  // or may not contain the additional result data yet.  For types that
  // do not need special processing, it should already be set.  If the
  // result requires actor-specific operations, then we do that below.
  // If the type and data types don't match, then we will trigger an
  // assertion in AutoParentOpResult::Add().
  AutoParentOpResult result(mIpcManager.ref(), aResult, entryCount);

  if (aOpenedCacheId != INVALID_CACHE_ID) {
    result.Add(aOpenedCacheId, mManager.clonePtr());
  }

  if (aStreamInfo) {
    const auto& streamInfo = *aStreamInfo;

    for (const auto& savedResponse : streamInfo.mSavedResponseList) {
      result.Add(savedResponse, streamInfo.mStreamList);
    }

    for (const auto& savedRequest : streamInfo.mSavedRequestList) {
      result.Add(savedRequest, streamInfo.mStreamList);
    }
  }

  (void)Send__delete__(this, CopyableErrorResult(std::move(aRv)),
                       result.SendAsOpResult());
}

already_AddRefed<nsIInputStream> CacheOpParent::DeserializeCacheStream(
    const Maybe<CacheReadStream>& aMaybeStream) {
  if (aMaybeStream.isNothing()) {
    return nullptr;
  }

  nsCOMPtr<nsIInputStream> stream;
  const CacheReadStream& readStream = aMaybeStream.ref();

  // Option 1: One of our own ReadStreams was passed back to us with a stream
  //           control actor.
  if (readStream.control()) {
    MOZ_ASSERT(readStream.control().IsParent());
    auto actor =
        static_cast<CacheStreamControlParent*>(readStream.control().AsParent());
    // Make sure the stream control is coming from the same Manager/origin
    MOZ_ASSERT(actor && actor->GetManager() == mManager.unsafeGetRawPtr());
    if (!actor || actor->GetManager() != mManager.unsafeGetRawPtr())
        [[unlikely]] {
      return nullptr;
    }
    stream = ReadStream::Create(readStream);
    if (stream) {
      return stream.forget();
    }
  }

  // Option 2: A stream was serialized using normal methods or passed
  //           as a DataPipe.  Use the standard method for extracting the
  //           resulting stream.
  return DeserializeIPCStream(readStream.stream());
}

void CacheOpParent::ProcessCrossOriginResourcePolicyHeader(
    ErrorResult& aRv, const nsTArray<SavedResponse>& aResponses) {
  if (!StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy()) {
    return;
  }
  // Only checking for match/matchAll.
  nsILoadInfo::CrossOriginEmbedderPolicy loadingCOEP =
      nsILoadInfo::EMBEDDER_POLICY_NULL;
  Maybe<mozilla::ipc::PrincipalInfo> principalInfo;
  switch (mOpArgs.type()) {
    case CacheOpArgs::TCacheMatchArgs: {
      const auto& request = mOpArgs.get_CacheMatchArgs().request();
      loadingCOEP = request.loadingEmbedderPolicy();
      principalInfo = request.principalInfo();
      break;
    }
    case CacheOpArgs::TCacheMatchAllArgs: {
      if (mOpArgs.get_CacheMatchAllArgs().maybeRequest().isSome()) {
        const auto& request =
            mOpArgs.get_CacheMatchAllArgs().maybeRequest().ref();
        loadingCOEP = request.loadingEmbedderPolicy();
        principalInfo = request.principalInfo();
      }
      break;
    }
    default: {
      return;
    }
  }

  // skip checking if the request has no principal for same-origin/same-site
  // checking.
  if (principalInfo.isNothing() ||
      principalInfo.ref().type() !=
          mozilla::ipc::PrincipalInfo::TContentPrincipalInfo) {
    return;
  }
  const mozilla::ipc::ContentPrincipalInfo& contentPrincipalInfo =
      principalInfo.ref().get_ContentPrincipalInfo();

  for (auto it = aResponses.cbegin(); it != aResponses.cend(); ++it) {
    if (it->mValue.type() != ResponseType::Opaque &&
        it->mValue.type() != ResponseType::Opaqueredirect) {
      continue;
    }

    const auto& headers = it->mValue.headers();
    const RequestCredentials credentials = it->mValue.credentials();
    const auto corpHeaderIt =
        std::find_if(headers.cbegin(), headers.cend(), [](const auto& header) {
          return header.name().EqualsLiteral("Cross-Origin-Resource-Policy");
        });

    // According to https://github.com/w3c/ServiceWorker/issues/1490, the cache
    // response is expected with CORP header, otherwise, throw the type error.
    // Note that this is different with the CORP checking for fetch metioned in
    // https://wicg.github.io/cross-origin-embedder-policy/#corp-check.
    // For fetch, if the response has no CORP header, "same-origin" checking
    // will be performed.
    if (corpHeaderIt == headers.cend() &&
        loadingCOEP == nsILoadInfo::EMBEDDER_POLICY_REQUIRE_CORP) {
      aRv.ThrowTypeError("Response is expected with CORP header.");
      return;
    }

    // Skip the case if the response has no principal for same-origin/same-site
    // checking.
    if (it->mValue.principalInfo().isNothing() ||
        it->mValue.principalInfo().ref().type() !=
            mozilla::ipc::PrincipalInfo::TContentPrincipalInfo) {
      continue;
    }

    const mozilla::ipc::ContentPrincipalInfo& responseContentPrincipalInfo =
        it->mValue.principalInfo().ref().get_ContentPrincipalInfo();

    nsCString corp =
        corpHeaderIt == headers.cend() ? EmptyCString() : corpHeaderIt->value();

    if (corp.IsEmpty()) {
      if (loadingCOEP == nsILoadInfo::EMBEDDER_POLICY_CREDENTIALLESS) {
        // This means the request of this request doesn't have
        // credentials, so it's safe for us to return.
        if (credentials == RequestCredentials::Omit) {
          return;
        }
        corp = "same-origin";
      }
    }

    if (corp.EqualsLiteral("same-origin")) {
      if (responseContentPrincipalInfo != contentPrincipalInfo) {
        aRv.ThrowTypeError("Response is expected from same origin.");
        return;
      }
    } else if (corp.EqualsLiteral("same-site")) {
      if (!responseContentPrincipalInfo.baseDomain().Equals(
              contentPrincipalInfo.baseDomain())) {
        aRv.ThrowTypeError("Response is expected from same site.");
        return;
      }
    }
  }
}

}  // namespace mozilla::dom::cache
