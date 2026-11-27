/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InternalResponse.h"

#include "mozilla/Assertions.h"
#include "mozilla/RandomNum.h"
#include "mozilla/RefPtr.h"
#include "mozilla/RemoteLazyInputStreamStorage.h"
#include "mozilla/dom/FetchStreamUtils.h"
#include "mozilla/dom/FetchTypes.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/cache/CacheTypes.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsIRandomGenerator.h"
#include "nsStreamUtils.h"

namespace mozilla::dom {

namespace {

// Const variable for generate padding size
// XXX This will be tweaked to something more meaningful in Bug 1383656.
const uint32_t kMaxRandomNumber = 1048576;

}  // namespace

InternalResponse::InternalResponse(uint16_t aStatus,
                                   const nsACString& aStatusText,
                                   RequestCredentials aCredentialsMode)
    : mType(ResponseType::Default),
      mStatus(aStatus),
      mStatusText(aStatusText),
      mHeaders(new InternalHeaders(HeadersGuardEnum::Response)),
      mBodySize(UNKNOWN_BODY_SIZE),
      mPaddingSize(UNKNOWN_PADDING_SIZE),
      mErrorCode(NS_OK),
      mCredentialsMode(aCredentialsMode),
      mCloned(false) {}

/* static */ SafeRefPtr<InternalResponse> InternalResponse::FromIPC(
    const ParentToParentInternalResponse& aIPCResponse) {
  MOZ_ASSERT(XRE_IsParentProcess());
  return FromIPCTemplate(aIPCResponse);
}

/* static */ SafeRefPtr<InternalResponse> InternalResponse::FromIPC(
    const ParentToChildInternalResponse& aIPCResponse) {
  MOZ_ASSERT(XRE_IsContentProcess());
  return FromIPCTemplate(aIPCResponse);
}

template <typename T>
/* static */ SafeRefPtr<InternalResponse> InternalResponse::FromIPCTemplate(
    const T& aIPCResponse) {
  if (aIPCResponse.metadata().type() == ResponseType::Error) {
    return InternalResponse::NetworkError(aIPCResponse.metadata().errorCode());
  }

  SafeRefPtr<InternalResponse> response = MakeSafeRefPtr<InternalResponse>(
      aIPCResponse.metadata().status(), aIPCResponse.metadata().statusText());

  response->SetURLList(aIPCResponse.metadata().urlList());
  response->mHeaders =
      new InternalHeaders(aIPCResponse.metadata().headers(),
                          aIPCResponse.metadata().headersGuard());

  if (aIPCResponse.body()) {
    auto bodySize = aIPCResponse.bodySize();
    auto body = ToInputStream(*aIPCResponse.body());
    response->SetBody(body.get(), bodySize);
  }

  response->SetAlternativeDataType(
      aIPCResponse.metadata().alternativeDataType());

  if (aIPCResponse.alternativeBody()) {
    auto alternativeBody = ToInputStream(*aIPCResponse.alternativeBody());
    response->SetAlternativeBody(alternativeBody.get());
  }

  response->InitChannelInfo(aIPCResponse.metadata().securityInfo());

  if (aIPCResponse.metadata().principalInfo()) {
    response->SetPrincipalInfo(std::make_unique<mozilla::ipc::PrincipalInfo>(
        aIPCResponse.metadata().principalInfo().ref()));
  }

  response->SetBodyBlobImpl(aIPCResponse.metadata().bodyBlobImpl());
  nsAutoString bodyLocalPath(aIPCResponse.metadata().bodyLocalPath());
  response->SetBodyLocalPath(bodyLocalPath);

  response->mCredentialsMode = aIPCResponse.metadata().credentialsMode();

  switch (aIPCResponse.metadata().type()) {
    case ResponseType::Basic:
      response = response->BasicResponse();
      break;
    case ResponseType::Cors:
      response = response->CORSResponse();
      break;
    case ResponseType::Default:
      break;
    case ResponseType::Opaque:
      response = response->OpaqueResponse();
      break;
    case ResponseType::Opaqueredirect:
      response = response->OpaqueRedirectResponse();
      break;
    default:
      MOZ_CRASH("Unexpected ResponseType!");
  }

  MOZ_ASSERT(response);

  return response;
}

InternalResponse::~InternalResponse() = default;

InternalResponseMetadata InternalResponse::GetMetadata() {
  nsTArray<HeadersEntry> headers;
  HeadersGuardEnum headersGuard;
  UnfilteredHeaders()->ToIPC(headers, headersGuard);

  Maybe<mozilla::ipc::PrincipalInfo> principalInfo =
      mPrincipalInfo ? Some(*mPrincipalInfo) : Nothing();

  RefPtr<BlobImpl> bodyBlobImpl(BodyBlobImpl());
  nsAutoString bodyLocalPath(BodyLocalPath());

  // Note: all the arguments are copied rather than moved, which would be more
  // efficient, because there's no move-friendly constructor generated.
  nsCOMPtr<nsITransportSecurityInfo> securityInfo(mChannelInfo.SecurityInfo());
  return InternalResponseMetadata(
      mType, GetUnfilteredURLList().Clone(), GetUnfilteredStatus(),
      GetUnfilteredStatusText(), headersGuard, headers, mErrorCode,
      GetAlternativeDataType(), securityInfo, principalInfo, bodyBlobImpl,
      bodyLocalPath, GetCredentialsMode());
}

void InternalResponse::ToChildToParentInternalResponse(
    ChildToParentInternalResponse* aIPCResponse) {
  *aIPCResponse = ChildToParentInternalResponse(GetMetadata(), Nothing(),
                                                UNKNOWN_BODY_SIZE, Nothing());
}

ParentToParentInternalResponse
InternalResponse::ToParentToParentInternalResponse() {
  ParentToParentInternalResponse result(GetMetadata(), Nothing(),
                                        UNKNOWN_BODY_SIZE, Nothing());

  nsCOMPtr<nsIInputStream> body;
  int64_t bodySize;
  GetUnfilteredBody(getter_AddRefs(body), &bodySize);

  if (body) {
    result.body() = Some(ToParentToParentStream(WrapNotNull(body), bodySize));
    result.bodySize() = bodySize;
  }

  nsCOMPtr<nsIInputStream> alternativeBody = TakeAlternativeBody();
  if (alternativeBody) {
    result.alternativeBody() = Some(ToParentToParentStream(
        WrapNotNull(alternativeBody), UNKNOWN_BODY_SIZE));
  }

  return result;
}

ParentToChildInternalResponse
InternalResponse::ToParentToChildInternalResponse() {
  ParentToChildInternalResponse result(GetMetadata(), Nothing(),
                                       UNKNOWN_BODY_SIZE, Nothing());

  nsCOMPtr<nsIInputStream> body;
  int64_t bodySize;
  GetUnfilteredBody(getter_AddRefs(body), &bodySize);

  if (body) {
    ParentToChildStream bodyStream =
        ToParentToChildStream(WrapNotNull(body), bodySize, mSerializeAsLazy);
    // The body stream can fail to serialize as an IPCStream. In the case, the
    // IPCStream's type would be T__None. Don't set up IPCInternalResponse's
    // body with the failed IPCStream.
    if (mSerializeAsLazy || bodyStream.get_IPCStream().stream().type() !=
                                mozilla::ipc::InputStreamParams::T__None) {
      result.body() = Some(bodyStream);
      result.bodySize() = bodySize;
    }
  }

  nsCOMPtr<nsIInputStream> alternativeBody = TakeAlternativeBody();
  if (alternativeBody) {
    ParentToChildStream alterBodyStream = ToParentToChildStream(
        WrapNotNull(alternativeBody), UNKNOWN_BODY_SIZE, mSerializeAsLazy);
    // The body stream can fail to serialize as an IPCStream. In the case, the
    // IPCStream's type would be T__None. Don't set up IPCInternalResponse's
    // body with the failed IPCStream.
    if (mSerializeAsLazy || alterBodyStream.get_IPCStream().stream().type() !=
                                mozilla::ipc::InputStreamParams::T__None) {
      result.alternativeBody() = Some(alterBodyStream);
    }
  }

  return result;
}

SafeRefPtr<InternalResponse> InternalResponse::Clone(CloneType aCloneType) {
  SafeRefPtr<InternalResponse> clone = CreateIncompleteCopy();
  clone->mCloned = (mCloned = true);

  clone->mHeaders = new InternalHeaders(*mHeaders);

  // Make sure the clone response will have the same padding size.
  clone->mPaddingInfo = mPaddingInfo;
  clone->mPaddingSize = mPaddingSize;

  clone->mCacheInfoChannel = mCacheInfoChannel;
  clone->mCredentialsMode = mCredentialsMode;

  if (mWrappedResponse) {
    clone->mWrappedResponse = mWrappedResponse->Clone(aCloneType);
    MOZ_ASSERT(!mBody);
    return clone;
  }

  if (!mBody || aCloneType == eDontCloneInputStream) {
    return clone;
  }

  nsCOMPtr<nsIInputStream> clonedBody;
  nsCOMPtr<nsIInputStream> replacementBody;

  nsresult rv = NS_CloneInputStream(mBody, getter_AddRefs(clonedBody),
                                    getter_AddRefs(replacementBody));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  clone->mBody.swap(clonedBody);
  if (replacementBody) {
    mBody.swap(replacementBody);
  }

  return clone;
}

SafeRefPtr<InternalResponse> InternalResponse::BasicResponse() {
  MOZ_ASSERT(!mWrappedResponse,
             "Can't BasicResponse a already wrapped response");
  SafeRefPtr<InternalResponse> basic = CreateIncompleteCopy();
  basic->mType = ResponseType::Basic;
  basic->mHeaders = InternalHeaders::BasicHeaders(Headers());
  basic->mWrappedResponse = SafeRefPtrFromThis();
  return basic;
}

SafeRefPtr<InternalResponse> InternalResponse::CORSResponse() {
  MOZ_ASSERT(!mWrappedResponse,
             "Can't CORSResponse a already wrapped response");
  SafeRefPtr<InternalResponse> cors = CreateIncompleteCopy();
  cors->mType = ResponseType::Cors;
  cors->mHeaders = InternalHeaders::CORSHeaders(Headers(), mCredentialsMode);
  cors->mWrappedResponse = SafeRefPtrFromThis();
  return cors;
}

uint32_t InternalResponse::GetPaddingInfo() {
  // If it's an opaque response, the paddingInfo should be generated only when
  // paddingSize is unknown size.
  // If it's not, the paddingInfo should be nothing and the paddingSize should
  // be unknown size.
  MOZ_DIAGNOSTIC_ASSERT(
      (mType == ResponseType::Opaque && mPaddingSize == UNKNOWN_PADDING_SIZE &&
       mPaddingInfo.isSome()) ||
      (mType == ResponseType::Opaque && mPaddingSize != UNKNOWN_PADDING_SIZE &&
       mPaddingInfo.isNothing()) ||
      (mType != ResponseType::Opaque && mPaddingSize == UNKNOWN_PADDING_SIZE &&
       mPaddingInfo.isNothing()));
  return mPaddingInfo.isSome() ? mPaddingInfo.ref() : 0;
}

nsresult InternalResponse::GeneratePaddingInfo() {
  MOZ_DIAGNOSTIC_ASSERT(mType == ResponseType::Opaque);
  MOZ_DIAGNOSTIC_ASSERT(mPaddingSize == UNKNOWN_PADDING_SIZE);

  // Utilize random generator to generator a random number
  nsresult rv;
  uint32_t randomNumber = 0;
  nsCOMPtr<nsIRandomGenerator> randomGenerator =
      do_GetService("@mozilla.org/security/random-generator;1", &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Maybe<uint64_t> maybeRandomNum = RandomUint64();
    if (maybeRandomNum.isSome()) {
      mPaddingInfo.emplace(uint32_t(maybeRandomNum.value() % kMaxRandomNumber));
      return NS_OK;
    }
    return rv;
  }

  MOZ_DIAGNOSTIC_ASSERT(randomGenerator);

  rv = randomGenerator->GenerateRandomBytesInto(randomNumber);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Maybe<uint64_t> maybeRandomNum = RandomUint64();
    if (maybeRandomNum.isSome()) {
      mPaddingInfo.emplace(uint32_t(maybeRandomNum.value() % kMaxRandomNumber));
      return NS_OK;
    }
    return rv;
  }

  mPaddingInfo.emplace(randomNumber % kMaxRandomNumber);

  return rv;
}

int64_t InternalResponse::GetPaddingSize() {
  // We initialize padding size to an unknown size (-1). After cached, we only
  // pad opaque response. Opaque response's padding size might be unknown before
  // cached.
  MOZ_DIAGNOSTIC_ASSERT(mType == ResponseType::Opaque ||
                        mPaddingSize == UNKNOWN_PADDING_SIZE);
  MOZ_DIAGNOSTIC_ASSERT(mPaddingSize == UNKNOWN_PADDING_SIZE ||
                        mPaddingSize >= 0);

  return mPaddingSize;
}

void InternalResponse::SetPaddingSize(int64_t aPaddingSize) {
  // We should only pad the opaque response.
  MOZ_DIAGNOSTIC_ASSERT(
      (mType == ResponseType::Opaque) !=
      (aPaddingSize == InternalResponse::UNKNOWN_PADDING_SIZE));
  MOZ_DIAGNOSTIC_ASSERT(aPaddingSize == UNKNOWN_PADDING_SIZE ||
                        aPaddingSize >= 0);

  mPaddingSize = aPaddingSize;
}

void InternalResponse::SetPrincipalInfo(
    UniquePtr<mozilla::ipc::PrincipalInfo> aPrincipalInfo) {
  mPrincipalInfo = std::move(aPrincipalInfo);
}

LoadTainting InternalResponse::GetTainting() const {
  switch (mType) {
    case ResponseType::Cors:
      return LoadTainting::CORS;
    case ResponseType::Opaque:
      return LoadTainting::Opaque;
    default:
      return LoadTainting::Basic;
  }
}

SafeRefPtr<InternalResponse> InternalResponse::Unfiltered() {
  SafeRefPtr<InternalResponse> ref = mWrappedResponse.clonePtr();
  if (!ref) {
    ref = SafeRefPtrFromThis();
  }
  return ref;
}

SafeRefPtr<InternalResponse> InternalResponse::OpaqueResponse() {
  MOZ_ASSERT(!mWrappedResponse,
             "Can't OpaqueResponse a already wrapped response");
  SafeRefPtr<InternalResponse> response =
      MakeSafeRefPtr<InternalResponse>(0, ""_ns);
  response->mType = ResponseType::Opaque;
  response->mChannelInfo = mChannelInfo;
  if (mPrincipalInfo) {
    response->mPrincipalInfo =
        std::make_unique<mozilla::ipc::PrincipalInfo>(*mPrincipalInfo);
  }
  response->mWrappedResponse = SafeRefPtrFromThis();
  return response;
}

SafeRefPtr<InternalResponse> InternalResponse::OpaqueRedirectResponse() {
  MOZ_ASSERT(!mWrappedResponse,
             "Can't OpaqueRedirectResponse a already wrapped response");
  MOZ_ASSERT(!mURLList.IsEmpty(),
             "URLList should not be emtpy for internalResponse");
  SafeRefPtr<InternalResponse> response = OpaqueResponse();
  response->mType = ResponseType::Opaqueredirect;
  response->mURLList = mURLList.Clone();
  return response;
}

SafeRefPtr<InternalResponse> InternalResponse::CreateIncompleteCopy() {
  SafeRefPtr<InternalResponse> copy =
      MakeSafeRefPtr<InternalResponse>(mStatus, mStatusText);
  copy->mType = mType;
  copy->mURLList = mURLList.Clone();
  copy->mChannelInfo = mChannelInfo;
  if (mPrincipalInfo) {
    copy->mPrincipalInfo =
        std::make_unique<mozilla::ipc::PrincipalInfo>(*mPrincipalInfo);
  }
  return copy;
}

ParentToChildInternalResponse ToParentToChild(
    const ParentToParentInternalResponse& aResponse) {
  ParentToChildInternalResponse result(aResponse.metadata(), Nothing(),
                                       aResponse.bodySize(), Nothing());

  if (aResponse.body().isSome()) {
    result.body() = Some(
        ToParentToChildStream(aResponse.body().ref(), aResponse.bodySize()));
  }
  if (aResponse.alternativeBody().isSome()) {
    result.alternativeBody() =
        Some(ToParentToChildStream(aResponse.alternativeBody().ref(),
                                   InternalResponse::UNKNOWN_BODY_SIZE));
  }

  return result;
}

}  // namespace mozilla::dom
