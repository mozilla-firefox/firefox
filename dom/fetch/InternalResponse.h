/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_InternalResponse_h
#define mozilla_dom_InternalResponse_h

#include "mozilla/UniquePtr.h"
#include "mozilla/dom/ChannelInfo.h"
#include "mozilla/dom/FetchTypes.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/ResponseBinding.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "nsICacheInfoChannel.h"
#include "nsIInputStream.h"
#include "nsISupportsImpl.h"
#include "nsProxyRelease.h"

namespace mozilla {
namespace ipc {
class PBackgroundChild;
class PBackgroundParent;
class PrincipalInfo;
}  // namespace ipc

namespace dom {

class ChildToParentInternalResponse;
class InternalHeaders;
class ParentToChildInternalResponse;
class ParentToParentInternalResponse;

class InternalResponse final : public AtomicSafeRefCounted<InternalResponse> {
  friend class FetchDriver;

 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(InternalResponse)

  InternalResponse(
      uint16_t aStatus, const nsACString& aStatusText,
      RequestCredentials aCredentialsMode = RequestCredentials::Omit);

  static SafeRefPtr<InternalResponse> FromIPC(
      const ParentToChildInternalResponse& aIPCResponse);

  static SafeRefPtr<InternalResponse> FromIPC(
      const ParentToParentInternalResponse& aIPCResponse);

  void ToChildToParentInternalResponse(
      ChildToParentInternalResponse* aIPCResponse);

  ParentToParentInternalResponse ToParentToParentInternalResponse();

  ParentToChildInternalResponse ToParentToChildInternalResponse();

  enum CloneType {
    eCloneInputStream,
    eDontCloneInputStream,
  };

  SafeRefPtr<InternalResponse> Clone(CloneType aCloneType);

  static SafeRefPtr<InternalResponse> NetworkError(nsresult aRv) {
    MOZ_DIAGNOSTIC_ASSERT(NS_FAILED(aRv));
    SafeRefPtr<InternalResponse> response =
        MakeSafeRefPtr<InternalResponse>(0, ""_ns);
    ErrorResult result;
    response->Headers()->SetGuard(HeadersGuardEnum::Immutable, result);
    MOZ_ASSERT(!result.Failed());
    response->mType = ResponseType::Error;
    response->mErrorCode = aRv;
    return response;
  }

  SafeRefPtr<InternalResponse> OpaqueResponse();

  SafeRefPtr<InternalResponse> OpaqueRedirectResponse();

  SafeRefPtr<InternalResponse> BasicResponse();

  SafeRefPtr<InternalResponse> CORSResponse();

  ResponseType Type() const {
    MOZ_ASSERT_IF(mType == ResponseType::Error, !mWrappedResponse);
    MOZ_ASSERT_IF(mType == ResponseType::Default, !mWrappedResponse);
    MOZ_ASSERT_IF(mType == ResponseType::Basic, mWrappedResponse);
    MOZ_ASSERT_IF(mType == ResponseType::Cors, mWrappedResponse);
    MOZ_ASSERT_IF(mType == ResponseType::Opaque, mWrappedResponse);
    MOZ_ASSERT_IF(mType == ResponseType::Opaqueredirect, mWrappedResponse);
    return mType;
  }

  bool IsError() const { return Type() == ResponseType::Error; }
  // GetUrl should return last fetch URL in response's url list and null if
  // response's url list is the empty list.
  nsIURI* GetURL() const {
    // Empty urlList when response is a synthetic response.
    if (mURLList.IsEmpty()) {
      return nullptr;
    }
    return mURLList.LastElement();
  }
  const nsTArray<NotNull<RefPtr<nsIURI>>>& GetURLList() const {
    return mURLList;
  }
  nsIURI* GetUnfilteredURL() const {
    if (mWrappedResponse) {
      return mWrappedResponse->GetURL();
    }
    return GetURL();
  }
  const nsTArray<NotNull<RefPtr<nsIURI>>>& GetUnfilteredURLList() const {
    if (mWrappedResponse) {
      return mWrappedResponse->GetURLList();
    }

    return GetURLList();
  }

  void SetURLList(const nsTArray<NotNull<RefPtr<nsIURI>>>& aURLList) {
    mURLList.Assign(aURLList);

#ifdef DEBUG
    for (auto& url : mURLList) {
      bool hasRef = false;
      url->GetHasRef(&hasRef);
      MOZ_ASSERT(!hasRef);
    }
#endif
  }

  uint16_t GetStatus() const { return mStatus; }

  uint16_t GetUnfilteredStatus() const {
    if (mWrappedResponse) {
      return mWrappedResponse->GetStatus();
    }

    return GetStatus();
  }

  const nsCString& GetStatusText() const { return mStatusText; }

  const nsCString& GetUnfilteredStatusText() const {
    if (mWrappedResponse) {
      return mWrappedResponse->GetStatusText();
    }

    return GetStatusText();
  }

  InternalHeaders* Headers() { return mHeaders; }

  InternalHeaders* UnfilteredHeaders() {
    if (mWrappedResponse) {
      return mWrappedResponse->Headers();
    };

    return Headers();
  }

  void GetUnfilteredBody(nsIInputStream** aStream,
                         int64_t* aBodySize = nullptr) {
    if (mWrappedResponse) {
      MOZ_ASSERT(!mBody);
      return mWrappedResponse->GetBody(aStream, aBodySize);
    }
    nsCOMPtr<nsIInputStream> stream = mBody;
    stream.forget(aStream);
    if (aBodySize) {
      *aBodySize = mBodySize;
    }
  }

  void GetBody(nsIInputStream** aStream, int64_t* aBodySize = nullptr) {
    if (Type() == ResponseType::Opaque ||
        Type() == ResponseType::Opaqueredirect) {
      *aStream = nullptr;
      if (aBodySize) {
        *aBodySize = UNKNOWN_BODY_SIZE;
      }
      return;
    }

    GetUnfilteredBody(aStream, aBodySize);
  }

  void SetBodyBlobImpl(BlobImpl* aBlobImpl) { mBodyBlobImpl = aBlobImpl; }

  BlobImpl* BodyBlobImpl() const {
    if (mWrappedResponse) {
      return mWrappedResponse->BodyBlobImpl();
    }
    return mBodyBlobImpl;
  }

  void SetBodyLocalPath(nsAString& aLocalPath) { mBodyLocalPath = aLocalPath; }

  const nsAString& BodyLocalPath() const {
    if (mWrappedResponse) {
      return mWrappedResponse->BodyLocalPath();
    }
    return mBodyLocalPath;
  }

  void SetBody(nsIInputStream* aBody, int64_t aBodySize) {
    if (mWrappedResponse) {
      return mWrappedResponse->SetBody(aBody, aBodySize);
    }
    // A request's body may not be reset once set.
    MOZ_ASSERT(!mBody);
    MOZ_ASSERT(mBodySize == UNKNOWN_BODY_SIZE);
    // Check arguments.
    MOZ_ASSERT(aBodySize == UNKNOWN_BODY_SIZE || aBodySize >= 0);
    // If body is not given, then size must be unknown.
    MOZ_ASSERT_IF(!aBody, aBodySize == UNKNOWN_BODY_SIZE);

    mBody = aBody;
    mBodySize = aBodySize;
  }

  uint32_t GetPaddingInfo();

  nsresult GeneratePaddingInfo();

  int64_t GetPaddingSize();

  void SetPaddingSize(int64_t aPaddingSize);

  void SetAlternativeDataType(const nsACString& aAltDataType) {
    if (mWrappedResponse) {
      return mWrappedResponse->SetAlternativeDataType(aAltDataType);
    }

    MOZ_DIAGNOSTIC_ASSERT(mAlternativeDataType.IsEmpty());

    mAlternativeDataType.Assign(aAltDataType);
  }

  const nsCString& GetAlternativeDataType() {
    if (mWrappedResponse) {
      return mWrappedResponse->GetAlternativeDataType();
    }

    return mAlternativeDataType;
  }

  void SetAlternativeBody(nsIInputStream* aAlternativeBody) {
    if (mWrappedResponse) {
      return mWrappedResponse->SetAlternativeBody(aAlternativeBody);
    }
    // A request's body may not be reset once set.
    MOZ_DIAGNOSTIC_ASSERT(!mAlternativeBody);

    mAlternativeBody = aAlternativeBody;
  }

  already_AddRefed<nsIInputStream> TakeAlternativeBody() {
    if (mWrappedResponse) {
      return mWrappedResponse->TakeAlternativeBody();
    }

    if (!mAlternativeBody) {
      return nullptr;
    }

    // cleanup the non-alternative body here.
    // Once alternative data is used, the real body is no need anymore.
    mBody = nullptr;
    mBodySize = UNKNOWN_BODY_SIZE;
    return mAlternativeBody.forget();
  }

  void SetCacheInfoChannel(
      const nsMainThreadPtrHandle<nsICacheInfoChannel>& aCacheInfoChannel) {
    if (mWrappedResponse) {
      return mWrappedResponse->SetCacheInfoChannel(aCacheInfoChannel);
    }
    MOZ_ASSERT(!mCacheInfoChannel);
    mCacheInfoChannel = aCacheInfoChannel;
  }

  nsMainThreadPtrHandle<nsICacheInfoChannel> TakeCacheInfoChannel() {
    if (mWrappedResponse) {
      return mWrappedResponse->TakeCacheInfoChannel();
    }
    nsMainThreadPtrHandle<nsICacheInfoChannel> rtn = mCacheInfoChannel;
    mCacheInfoChannel = nullptr;
    return rtn;
  }

  bool HasCacheInfoChannel() const {
    if (mWrappedResponse) {
      return !!mWrappedResponse->HasCacheInfoChannel();
    }
    return !!mCacheInfoChannel;
  }

  bool HasBeenCloned() const { return mCloned; }

  void SetSerializeAsLazy(bool aAllow) { mSerializeAsLazy = aAllow; }
  bool CanSerializeAsLazy() const { return mSerializeAsLazy; }

  void InitChannelInfo(nsIChannel* aChannel) {
    mChannelInfo.InitFromChannel(aChannel);
  }

  void InitChannelInfo(nsITransportSecurityInfo* aSecurityInfo) {
    mChannelInfo.InitFromTransportSecurityInfo(aSecurityInfo);
  }

  void InitChannelInfo(const ChannelInfo& aChannelInfo) {
    mChannelInfo = aChannelInfo;
  }

  const ChannelInfo& GetChannelInfo() const { return mChannelInfo; }

  const UniquePtr<mozilla::ipc::PrincipalInfo>& GetPrincipalInfo() const {
    return mPrincipalInfo;
  }

  bool IsRedirected() const { return mURLList.Length() > 1; }

  nsresult GetErrorCode() const { return mErrorCode; }

  // Takes ownership of the principal info.
  void SetPrincipalInfo(UniquePtr<mozilla::ipc::PrincipalInfo> aPrincipalInfo);

  LoadTainting GetTainting() const;

  SafeRefPtr<InternalResponse> Unfiltered();

  InternalResponseMetadata GetMetadata();

  RequestCredentials GetCredentialsMode() const {
    if (mWrappedResponse) {
      return mWrappedResponse->GetCredentialsMode();
    }
    return mCredentialsMode;
  }

  ~InternalResponse();

  explicit InternalResponse(const InternalResponse& aOther) = delete;
  InternalResponse& operator=(const InternalResponse&) = delete;

 private:
  // Returns an instance of InternalResponse which is a copy of this
  // InternalResponse, except headers, body and wrapped response (if any) which
  // are left uninitialized. Used for cloning and filtering.
  SafeRefPtr<InternalResponse> CreateIncompleteCopy();

  template <typename T>
  static SafeRefPtr<InternalResponse> FromIPCTemplate(const T& aIPCResponse);

  ResponseType mType;
  // A response has an associated url list (a list of zero or more fetch URLs).
  // Unless stated otherwise, it is the empty list. The current url is the last
  // element in mURLlist
  nsTArray<NotNull<RefPtr<nsIURI>>> mURLList;
  const uint16_t mStatus;
  const nsCString mStatusText;
  RefPtr<InternalHeaders> mHeaders;
  nsCOMPtr<nsIInputStream> mBody;
  RefPtr<BlobImpl> mBodyBlobImpl;
  nsString mBodyLocalPath;
  int64_t mBodySize;
  // It's used to passed to the CacheResponse to generate padding size. Once, we
  // generate the padding size for resposne, we don't need it anymore.
  Maybe<uint32_t> mPaddingInfo;
  int64_t mPaddingSize;
  nsresult mErrorCode;
  RequestCredentials mCredentialsMode;

  // For alternative data such as JS Bytecode cached in the HTTP cache.
  nsCString mAlternativeDataType;
  nsCOMPtr<nsIInputStream> mAlternativeBody;
  nsMainThreadPtrHandle<nsICacheInfoChannel> mCacheInfoChannel;
  bool mCloned;
  // boolean to indicate the body/alternativeBody will be serialized as a
  // RemoteLazyInputStream.
  bool mSerializeAsLazy{true};

 public:
  static constexpr int64_t UNKNOWN_BODY_SIZE = -1;
  static constexpr int64_t UNKNOWN_PADDING_SIZE = -1;

 private:
  ChannelInfo mChannelInfo;
  UniquePtr<mozilla::ipc::PrincipalInfo> mPrincipalInfo;

  // For filtered responses.
  // Cache, and SW interception should always serialize/access the underlying
  // unfiltered headers and when deserializing, create an InternalResponse
  // with the unfiltered headers followed by wrapping it.
  SafeRefPtr<InternalResponse> mWrappedResponse;
};

ParentToChildInternalResponse ToParentToChild(
    const ParentToParentInternalResponse& aResponse);

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_InternalResponse_h
