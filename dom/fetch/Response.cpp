/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Response.h"

#include <memory>

#include "BodyExtractor.h"
#include "FetchStreamReader.h"
#include "InternalResponse.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FetchBinding.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStreamDefaultReader.h"
#include "mozilla/dom/ResponseBinding.h"
#include "mozilla/dom/URL.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "nsDOMString.h"
#include "nsISupportsImpl.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"

namespace mozilla::dom {

NS_IMPL_ADDREF_INHERITED(Response, FetchBody<Response>)
NS_IMPL_RELEASE_INHERITED(Response, FetchBody<Response>)

NS_IMPL_CYCLE_COLLECTION_CLASS(Response)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(Response, FetchBody<Response>)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHeaders)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSignalImpl)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFetchStreamReader)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(Response, FetchBody<Response>)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHeaders)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSignalImpl)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFetchStreamReader)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(Response, FetchBody<Response>)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Response)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_END_INHERITING(FetchBody<Response>)

Response::Response(nsIGlobalObject* aGlobal,
                   SafeRefPtr<InternalResponse> aInternalResponse,
                   AbortSignalImpl* aSignalImpl)
    : FetchBody<Response>(aGlobal),
      mInternalResponse(std::move(aInternalResponse)),
      mSignalImpl(aSignalImpl) {
  MOZ_ASSERT(
      mInternalResponse->Headers()->Guard() == HeadersGuardEnum::Immutable ||
      mInternalResponse->Headers()->Guard() == HeadersGuardEnum::Response);

  mozilla::HoldJSObjects(this);
}

Response::~Response() { mozilla::DropJSObjects(this); }

/* static */
already_AddRefed<Response> Response::Error(const GlobalObject& aGlobal) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Response> r = new Response(
      global, InternalResponse::NetworkError(NS_ERROR_FAILURE), nullptr);
  return r.forget();
}

/* static */
already_AddRefed<Response> Response::Redirect(const GlobalObject& aGlobal,
                                              const nsACString& aUrl,
                                              uint16_t aStatus,
                                              ErrorResult& aRv) {
  nsAutoCString parsedURL;

  if (NS_IsMainThread()) {
    nsIURI* baseURI = nullptr;
    nsCOMPtr<nsPIDOMWindowInner> inner(
        do_QueryInterface(aGlobal.GetAsSupports()));
    if (Document* doc = inner ? inner->GetExtantDoc() : nullptr) {
      baseURI = doc->GetBaseURI();
    }
    nsCOMPtr<nsIURI> resolvedURI;
    nsresult rv =
        NS_NewURI(getter_AddRefs(resolvedURI), aUrl, nullptr, baseURI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError<MSG_INVALID_URL>(aUrl);
      return nullptr;
    }

    rv = resolvedURI->GetSpec(parsedURL);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.ThrowTypeError<MSG_INVALID_URL>(aUrl);
      return nullptr;
    }
  } else {
    WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(worker);
    worker->AssertIsOnWorkerThread();

    RefPtr<URL> url = URL::Constructor(aGlobal.GetAsSupports(), aUrl,
                                       worker->GetBaseURI(), aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
    url->GetHref(parsedURL);
  }

  if (aStatus != 301 && aStatus != 302 && aStatus != 303 && aStatus != 307 &&
      aStatus != 308) {
    aRv.ThrowRangeError("Invalid redirect status code.");
    return nullptr;
  }

  // We can't just pass nullptr for our null-valued Nullable, because the
  // fetch::ResponseBodyInit is a non-temporary type due to the MOZ_RAII
  // annotations on some of its members.
  Nullable<fetch::ResponseBodyInit> body;
  ResponseInit init;
  init.mStatus = aStatus;
  init.mStatusText.AssignASCII("");
  RefPtr<Response> r = Response::Constructor(aGlobal, body, init, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  r->GetInternalHeaders()->Set("Location"_ns, parsedURL, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  r->GetInternalHeaders()->SetGuard(HeadersGuardEnum::Immutable, aRv);
  MOZ_ASSERT(!aRv.Failed());

  return r.forget();
}

/* static */ already_AddRefed<Response> Response::CreateAndInitializeAResponse(
    const GlobalObject& aGlobal, const Nullable<fetch::ResponseBodyInit>& aBody,
    const nsACString& aDefaultContentType, const ResponseInit& aInit,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());

  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  // Initialize a response, Step 1.
  if (aInit.mStatus < 200 || aInit.mStatus > 599) {
    aRv.ThrowRangeError("Invalid response status code.");
    return nullptr;
  }

  // Initialize a response, Step 2: Check if the status text contains illegal
  // characters
  nsACString::const_iterator start, end;
  aInit.mStatusText.BeginReading(start);
  aInit.mStatusText.EndReading(end);
  if (FindCharInReadable('\r', start, end)) {
    aRv.ThrowTypeError<MSG_RESPONSE_INVALID_STATUSTEXT_ERROR>();
    return nullptr;
  }
  // Reset iterator since FindCharInReadable advances it.
  aInit.mStatusText.BeginReading(start);
  if (FindCharInReadable('\n', start, end)) {
    aRv.ThrowTypeError<MSG_RESPONSE_INVALID_STATUSTEXT_ERROR>();
    return nullptr;
  }

  // Initialize a response, Step 3-4.
  SafeRefPtr<InternalResponse> internalResponse =
      MakeSafeRefPtr<InternalResponse>(aInit.mStatus, aInit.mStatusText);

  UniquePtr<mozilla::ipc::PrincipalInfo> principalInfo;

  // Grab a valid channel info from the global so this response is 'valid' for
  // interception.
  if (NS_IsMainThread()) {
    ChannelInfo info;
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(global);
    if (window) {
      Document* doc = window->GetExtantDoc();
      MOZ_ASSERT(doc);
      info.InitFromDocument(doc);

      principalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>();
      nsresult rv =
          PrincipalToPrincipalInfo(doc->NodePrincipal(), principalInfo.get());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
        return nullptr;
      }

      internalResponse->InitChannelInfo(info);
    } else if (global->PrincipalOrNull()->IsSystemPrincipal()) {
      info.InitFromChromeGlobal(global);

      internalResponse->InitChannelInfo(info);
    }

    /**
     * The channel info is left uninitialized if neither the above `if` nor
     * `else if` statements are executed; this could be because we're in a
     * WebExtensions content script, where the global (i.e. `global`) is a
     * wrapper, and the principal is an expanded principal. In this case,
     * as far as I can tell, there's no way to get the security info, but we'd
     * like the `Response` to be successfully constructed.
     */
  } else {
    WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(worker);
    internalResponse->InitChannelInfo(worker->GetChannelInfo());
    principalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>(
        worker->GetPrincipalInfo());
  }

  internalResponse->SetPrincipalInfo(std::move(principalInfo));

  RefPtr<Response> r =
      new Response(global, internalResponse.clonePtr(), nullptr);

  if (aInit.mHeaders.WasPassed()) {
    internalResponse->Headers()->Clear();

    // Instead of using Fill, create an object to allow the constructor to
    // unwrap the HeadersInit.
    RefPtr<Headers> headers =
        Headers::Create(global, aInit.mHeaders.Value(), aRv);
    if (aRv.Failed()) {
      return nullptr;
    }

    internalResponse->Headers()->Fill(*headers->GetInternalHeaders(), aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  if (!aBody.IsNull()) {
    if (aInit.mStatus == 204 || aInit.mStatus == 205 || aInit.mStatus == 304) {
      aRv.ThrowTypeError("Response body is given with a null body status.");
      return nullptr;
    }

    nsCString contentTypeWithCharset;
    contentTypeWithCharset.SetIsVoid(true);
    nsCOMPtr<nsIInputStream> bodyStream;
    int64_t bodySize = InternalResponse::UNKNOWN_BODY_SIZE;

    const fetch::ResponseBodyInit& body = aBody.Value();
    if (body.IsReadableStream()) {
      JSContext* cx = aGlobal.Context();
      aRv.MightThrowJSException();

      ReadableStream& readableStream = body.GetAsReadableStream();

      if (readableStream.Locked() || readableStream.Disturbed()) {
        aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
        return nullptr;
      }

      r->SetReadableStreamBody(cx, &readableStream);

      // If this is a DOM generated ReadableStream, we can extract the
      // inputStream directly.
      if (nsIInputStream* underlyingSource =
              readableStream.MaybeGetInputStreamIfUnread()) {
        bodyStream = underlyingSource;
      } else {
        // If this is a JS-created ReadableStream, let's create a
        // FetchStreamReader.
        aRv = FetchStreamReader::Create(aGlobal.Context(), global,
                                        getter_AddRefs(r->mFetchStreamReader),
                                        getter_AddRefs(bodyStream));
        if (NS_WARN_IF(aRv.Failed())) {
          return nullptr;
        }
      }
    } else {
      uint64_t size = 0;
      aRv = ExtractByteStreamFromBody(body, getter_AddRefs(bodyStream),
                                      contentTypeWithCharset, size);
      if (NS_WARN_IF(aRv.Failed())) {
        return nullptr;
      }

      if (!aDefaultContentType.IsVoid()) {
        contentTypeWithCharset = aDefaultContentType;
      }

      bodySize = size;
    }

    internalResponse->SetBody(bodyStream, bodySize);

    if (!contentTypeWithCharset.IsVoid() &&
        !internalResponse->Headers()->Has("Content-Type"_ns, aRv)) {
      // Ignore Append() failing here.
      ErrorResult error;
      internalResponse->Headers()->Append("Content-Type"_ns,
                                          contentTypeWithCharset, error);
      error.SuppressException();
    }

    if (aRv.Failed()) {
      return nullptr;
    }
  }

  return r.forget();
}

/* static */
already_AddRefed<Response> Response::CreateFromJson(const GlobalObject& aGlobal,
                                                    JSContext* aCx,
                                                    JS::Handle<JS::Value> aData,
                                                    const ResponseInit& aInit,
                                                    ErrorResult& aRv) {
  aRv.MightThrowJSException();
  Nullable<fetch::ResponseBodyInit> body;
  auto& result = body.SetValue().SetAsUSVString();
  if (!nsContentUtils::StringifyJSON(aCx, aData, result,
                                     UndefinedIsVoidString)) {
    aRv.StealExceptionFromJSContext(aCx);
    return nullptr;
  }
  if (result.IsVoid()) {
    aRv.ThrowTypeError<MSG_JSON_INVALID_VALUE>();
    return nullptr;
  }
  return CreateAndInitializeAResponse(aGlobal, body, "application/json"_ns,
                                      aInit, aRv);
}

/*static*/
already_AddRefed<Response> Response::Constructor(
    const GlobalObject& aGlobal, const Nullable<fetch::ResponseBodyInit>& aBody,
    const ResponseInit& aInit, ErrorResult& aRv) {
  return CreateAndInitializeAResponse(aGlobal, aBody, VoidCString(), aInit,
                                      aRv);
}

already_AddRefed<Response> Response::Clone(JSContext* aCx, ErrorResult& aRv) {
  bool bodyUsed = BodyUsed();

  if (!bodyUsed && mReadableStreamBody) {
    bool locked = mReadableStreamBody->Locked();
    bodyUsed = locked;
  }

  if (bodyUsed) {
    aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
    return nullptr;
  }

  RefPtr<FetchStreamReader> streamReader;
  nsCOMPtr<nsIInputStream> inputStream;

  RefPtr<ReadableStream> body;
  MaybeTeeReadableStreamBody(aCx, getter_AddRefs(body),
                             getter_AddRefs(streamReader),
                             getter_AddRefs(inputStream), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_ASSERT_IF(body, streamReader);
  MOZ_ASSERT_IF(body, inputStream);

  SafeRefPtr<InternalResponse> ir =
      mInternalResponse->Clone(body ? InternalResponse::eDontCloneInputStream
                                    : InternalResponse::eCloneInputStream);

  RefPtr<Response> response =
      new Response(mGlobal, ir.clonePtr(), GetSignalImpl());

  if (body) {
    // Maybe we have a body, but we receive null from MaybeTeeReadableStreamBody
    // if this body is a native stream.   In this case the InternalResponse will
    // have a clone of the native body and the ReadableStream will be created
    // lazily if needed.
    response->SetReadableStreamBody(aCx, body);
    response->mFetchStreamReader = streamReader;
    ir->SetBody(inputStream, InternalResponse::UNKNOWN_BODY_SIZE);
  } else {
    // We didn't tee: an existing, unread native ReadableStream keeps reflecting
    // this response's body. InternalResponse::Clone() may have replaced the
    // underlying input stream (a non-cloneable body is now consumed by the
    // cloning copy), so repoint the reflector at the current stream.
    MaybeRebindReadableStreamBody();
  }

  return response.forget();
}

already_AddRefed<Response> Response::CloneUnfiltered(JSContext* aCx,
                                                     ErrorResult& aRv) {
  if (BodyUsed()) {
    aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
    return nullptr;
  }

  RefPtr<FetchStreamReader> streamReader;
  nsCOMPtr<nsIInputStream> inputStream;

  RefPtr<ReadableStream> body;
  MaybeTeeReadableStreamBody(aCx, getter_AddRefs(body),
                             getter_AddRefs(streamReader),
                             getter_AddRefs(inputStream), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_ASSERT_IF(body, streamReader);
  MOZ_ASSERT_IF(body, inputStream);

  SafeRefPtr<InternalResponse> clone =
      mInternalResponse->Clone(body ? InternalResponse::eDontCloneInputStream
                                    : InternalResponse::eCloneInputStream);

  SafeRefPtr<InternalResponse> ir = clone->Unfiltered();
  RefPtr<Response> ref = new Response(mGlobal, ir.clonePtr(), GetSignalImpl());

  if (body) {
    // Maybe we have a body, but we receive null from MaybeTeeReadableStreamBody
    // if this body is a native stream.   In this case the InternalResponse will
    // have a clone of the native body and the ReadableStream will be created
    // lazily if needed.
    ref->SetReadableStreamBody(aCx, body);
    ref->mFetchStreamReader = streamReader;
    ir->SetBody(inputStream, InternalResponse::UNKNOWN_BODY_SIZE);
  }

  return ref.forget();
}

void Response::SetBody(nsIInputStream* aBody, int64_t aBodySize) {
  MOZ_ASSERT(!BodyUsed());
  mInternalResponse->SetBody(aBody, aBodySize);
}

SafeRefPtr<InternalResponse> Response::GetInternalResponse() const {
  return mInternalResponse.clonePtr();
}

Headers* Response::Headers_() {
  if (!mHeaders) {
    mHeaders = new Headers(mGlobal, mInternalResponse->Headers());
  }

  return mHeaders;
}

}  // namespace mozilla::dom
