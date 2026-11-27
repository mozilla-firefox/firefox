/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Response_h
#define mozilla_dom_Response_h

#include "InternalHeaders.h"
#include "InternalResponse.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/ResponseBinding.h"
#include "nsISupportsImpl.h"
#include "nsWrapperCache.h"

namespace mozilla {
namespace ipc {
class PrincipalInfo;
}  // namespace ipc

namespace dom {

class Headers;

class Response final : public FetchBody<Response>, public nsWrapperCache {
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(Response,
                                                         FetchBody<Response>)

 public:
  Response(nsIGlobalObject* aGlobal,
           SafeRefPtr<InternalResponse> aInternalResponse,
           AbortSignalImpl* aSignalImpl);

  Response(const Response& aOther) = delete;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override {
    return Response_Binding::Wrap(aCx, this, aGivenProto);
  }

  ResponseType Type() const { return mInternalResponse->Type(); }
  void GetUrl(nsACString& aUrl) const {
    aUrl.Truncate();
    if (nsIURI* uri = mInternalResponse->GetURL()) {
      MOZ_ALWAYS_SUCCEEDS(uri->GetSpec(aUrl));
    }
  }
  bool Redirected() const { return mInternalResponse->IsRedirected(); }
  uint16_t Status() const { return mInternalResponse->GetStatus(); }

  bool Ok() const {
    return mInternalResponse->GetStatus() >= 200 &&
           mInternalResponse->GetStatus() <= 299;
  }

  void GetStatusText(nsCString& aStatusText) const {
    aStatusText = mInternalResponse->GetStatusText();
  }

  InternalHeaders* GetInternalHeaders() const {
    return mInternalResponse->Headers();
  }

  void InitChannelInfo(nsIChannel* aChannel) {
    mInternalResponse->InitChannelInfo(aChannel);
  }

  const ChannelInfo& GetChannelInfo() const {
    return mInternalResponse->GetChannelInfo();
  }

  const UniquePtr<mozilla::ipc::PrincipalInfo>& GetPrincipalInfo() const {
    return mInternalResponse->GetPrincipalInfo();
  }

  bool HasCacheInfoChannel() const {
    return mInternalResponse->HasCacheInfoChannel();
  }

  Headers* Headers_();

  void GetBody(nsIInputStream** aStream, int64_t* aBodyLength = nullptr) {
    mInternalResponse->GetBody(aStream, aBodyLength);
  }

  using FetchBody::GetBody;

  using FetchBody::BodyBlobImpl;

  BlobImpl* BodyBlobImpl() const { return mInternalResponse->BodyBlobImpl(); }

  using FetchBody::BodyLocalPath;

  const nsAString& BodyLocalPath() const {
    return mInternalResponse->BodyLocalPath();
  }

  static already_AddRefed<Response> Error(const GlobalObject& aGlobal);

  static already_AddRefed<Response> Redirect(const GlobalObject& aGlobal,
                                             const nsACString& aUrl,
                                             uint16_t aStatus,
                                             ErrorResult& aRv);

  static already_AddRefed<Response> CreateFromJson(const GlobalObject&,
                                                   JSContext*,
                                                   JS::Handle<JS::Value>,
                                                   const ResponseInit&,
                                                   ErrorResult&);

  static already_AddRefed<Response> Constructor(
      const GlobalObject& aGlobal,
      const Nullable<fetch::ResponseBodyInit>& aBody, const ResponseInit& aInit,
      ErrorResult& rv);

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  already_AddRefed<Response> Clone(JSContext* aCx, ErrorResult& aRv);

  already_AddRefed<Response> CloneUnfiltered(JSContext* aCx, ErrorResult& aRv);

  void SetBody(nsIInputStream* aBody, int64_t aBodySize);

  SafeRefPtr<InternalResponse> GetInternalResponse() const;

  AbortSignalImpl* GetSignalImpl() const override { return mSignalImpl; }
  AbortSignalImpl* GetSignalImplToConsumeBody() const final {
    // XXX: BodyConsumer is supposed to work in terms of ReadableStream and
    // should be affected by: https://fetch.spec.whatwg.org/#abort-fetch
    //
    // Step 6: If response’s body is not null and is readable, then error
    // response’s body with error.
    //
    // But since it's written before streams work, it's currently depending on
    // abort signal to be aborted.
    // Please fix this when DOM ReadableStream is ready. (Bug 1730584)
    return mSignalImpl;
  }

 private:
  static already_AddRefed<Response> CreateAndInitializeAResponse(
      const GlobalObject& aGlobal,
      const Nullable<fetch::ResponseBodyInit>& aBody,
      const nsACString& aDefaultContentType, const ResponseInit& aInit,
      ErrorResult& aRv);

  ~Response();

  SafeRefPtr<InternalResponse> mInternalResponse;
  // Lazily created
  RefPtr<Headers> mHeaders;
  RefPtr<AbortSignalImpl> mSignalImpl;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_Response_h
