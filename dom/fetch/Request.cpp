/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Request.h"

#include <memory>

#include "js/Value.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/BodyExtractor.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/FetchUtil.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStreamBinding.h"
#include "mozilla/dom/ReadableStreamDefaultReader.h"
#include "mozilla/dom/TransformStream.h"
#include "mozilla/dom/TransformStreamBinding.h"
#include "mozilla/dom/URL.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WritableStream.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"

namespace mozilla::dom {

NS_IMPL_ADDREF_INHERITED(Request, FetchBody<Request>)
NS_IMPL_RELEASE_INHERITED(Request, FetchBody<Request>)

// Can't use _INHERITED macro here because FetchBody<Request> is abstract.
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Request)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(Request, FetchBody<Request>)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHeaders)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSignal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFetchStreamReader)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(Request, FetchBody<Request>)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHeaders)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSignal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFetchStreamReader)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Request)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_END_INHERITING(FetchBody<Request>)

Request::Request(nsIGlobalObject* aGlobal, SafeRefPtr<InternalRequest> aRequest,
                 AbortSignal* aSignal)
    : FetchBody<Request>(aGlobal), mRequest(std::move(aRequest)) {
  MOZ_ASSERT(mRequest->Headers()->Guard() == HeadersGuardEnum::Immutable ||
             mRequest->Headers()->Guard() == HeadersGuardEnum::Request ||
             mRequest->Headers()->Guard() == HeadersGuardEnum::Request_no_cors);
  if (aSignal) {
    // If we don't have a signal as argument, we will create it when required by
    // content, otherwise the Request's signal must follow what has been passed.
    AutoTArray<OwningNonNull<AbortSignal>, 1> array{OwningNonNull(*aSignal)};
    mSignal = AbortSignal::Any(aGlobal, array, [](nsIGlobalObject* aGlobal) {
      return AbortSignal::Create(aGlobal, SignalAborted::No,
                                 JS::UndefinedHandleValue);
    });
  }
}

Request::~Request() = default;

SafeRefPtr<InternalRequest> Request::GetInternalRequest() {
  return mRequest.clonePtr();
}

namespace {

already_AddRefed<nsIURI> ParseURL(nsIGlobalObject* aGlobal,
                                  const nsACString& aInput, ErrorResult& aRv) {
  nsCOMPtr<nsIURI> baseURI;
  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> inner(do_QueryInterface(aGlobal));
    Document* doc = inner ? inner->GetExtantDoc() : nullptr;
    baseURI = doc ? doc->GetBaseURI() : nullptr;
  } else {
    WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
    baseURI = worker->GetBaseURI();
  }

  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aInput, nullptr, baseURI))) {
    aRv.ThrowTypeError<MSG_INVALID_URL>(aInput);
    return nullptr;
  }
  return uri.forget();
}

void GetRequestURL(nsIGlobalObject* aGlobal, const nsACString& aInput,
                   nsIURI** aRequestURL, nsACString& aURLfragment,
                   ErrorResult& aRv) {
  nsCOMPtr<nsIURI> resolvedURI = ParseURL(aGlobal, aInput, aRv);
  if (aRv.Failed()) {
    return;
  }
  // This fails with URIs with weird protocols, even when they are valid,
  // so we ignore the failure
  nsAutoCString credentials;
  (void)resolvedURI->GetUserPass(credentials);
  if (!credentials.IsEmpty()) {
    aRv.ThrowTypeError<MSG_URL_HAS_CREDENTIALS>(aInput);
    return;
  }

  aRv = NS_GetURIWithoutRef(resolvedURI, aRequestURL);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  // Get the fragment from nsIURI.
  aRv = resolvedURI->GetRef(aURLfragment);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }
}
}  // namespace

/*static*/
SafeRefPtr<Request> Request::Constructor(const GlobalObject& aGlobal,
                                         const RequestOrUTF8String& aInput,
                                         const RequestInit& aInit,
                                         ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  return Constructor(global, aGlobal.Context(), aInput, aInit,
                     aGlobal.CallerType(), aRv);
}

/*static*/
SafeRefPtr<Request> Request::Constructor(
    nsIGlobalObject* aGlobal, JSContext* aCx, const RequestOrUTF8String& aInput,
    const RequestInit& aInit, CallerType aCallerType, ErrorResult& aRv) {
  bool hasCopiedBody = false;
  SafeRefPtr<InternalRequest> request;

  RefPtr<AbortSignal> signal;
  bool bodyFromInit = false;
  RefPtr<FetchStreamReader> temporaryStreamReader;
  // The spec keeps the exact ReadableStream object passed as init["body"], so
  // that request.body is reference-equal to it. Remember it here and attach it
  // to the Request once that has been constructed.
  RefPtr<ReadableStream> temporaryStreamBody;

  if (aInput.IsRequest()) {
    RefPtr<Request> inputReq = &aInput.GetAsRequest();
    nsCOMPtr<nsIInputStream> body;

    if (aInit.mBody.WasPassed() && !aInit.mBody.Value().IsNull()) {
      bodyFromInit = true;
      hasCopiedBody = true;
    } else {
      inputReq->GetBody(getter_AddRefs(body));
      if (inputReq->IsBodyUnusable()) {
        aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
        return nullptr;
      }

      // The body will be copied when GetRequestConstructorCopy() is executed.
      if (body) {
        hasCopiedBody = true;
      }
    }

    request = inputReq->GetInternalRequest();
    signal = inputReq->GetOrCreateSignal();
  } else {
    // aInput is UTF8String.
    // We need to get url before we create a InternalRequest.
    const nsACString& input = aInput.GetAsUTF8String();
    nsCOMPtr<nsIURI> requestURL;
    nsCString fragment;
    GetRequestURL(aGlobal, input, getter_AddRefs(requestURL), fragment, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
    request = MakeSafeRefPtr<InternalRequest>(WrapNotNull(requestURL.get()),
                                              fragment);
  }
  request = request->GetRequestConstructorCopy(aGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  Maybe<RequestMode> mode;
  if (aInit.mMode.WasPassed()) {
    if (aInit.mMode.Value() == RequestMode::Navigate) {
      aRv.ThrowTypeError<MSG_INVALID_REQUEST_MODE>("navigate");
      return nullptr;
    }

    mode.emplace(aInit.mMode.Value());
  }
  Maybe<RequestCredentials> credentials;
  if (aInit.mCredentials.WasPassed()) {
    credentials.emplace(aInit.mCredentials.Value());
  }
  Maybe<RequestCache> cache;
  if (aInit.mCache.WasPassed()) {
    cache.emplace(aInit.mCache.Value());
  }
  if (aInput.IsUTF8String()) {
    if (mode.isNothing()) {
      mode.emplace(RequestMode::Cors);
    }
    if (credentials.isNothing()) {
      if (aCallerType == CallerType::System &&
          StaticPrefs::network_fetch_systemDefaultsToOmittingCredentials()) {
        credentials.emplace(RequestCredentials::Omit);
      } else {
        credentials.emplace(RequestCredentials::Same_origin);
      }
    }
    if (cache.isNothing()) {
      cache.emplace(RequestCache::Default);
    }
  }

  if (aInit.IsAnyMemberPresent() && request->Mode() == RequestMode::Navigate) {
    mode = Some(RequestMode::Same_origin);
  }

  if (aInit.IsAnyMemberPresent()) {
    request->SetReferrer(nsLiteralCString(kFETCH_CLIENT_REFERRER_STR));
    request->SetReferrerPolicy(ReferrerPolicy::_empty);
  }
  if (aInit.mReferrer.WasPassed()) {
    const nsCString& referrer = aInit.mReferrer.Value();
    if (referrer.IsEmpty()) {
      request->SetReferrer(""_ns);
    } else {
      nsCOMPtr<nsIURI> referrerURI = ParseURL(aGlobal, referrer, aRv);
      if (NS_WARN_IF(aRv.Failed())) {
        aRv.ThrowTypeError<MSG_INVALID_REFERRER_URL>(referrer);
        return nullptr;
      }

      nsAutoCString spec;
      referrerURI->GetSpec(spec);
      if (!spec.EqualsLiteral(kFETCH_CLIENT_REFERRER_STR)) {
        nsCOMPtr<nsIPrincipal> principal = aGlobal->PrincipalOrNull();
        if (principal) {
          nsresult rv =
              principal->CheckMayLoad(referrerURI,
                                      /* allowIfInheritsPrincipal */ false);
          if (NS_FAILED(rv)) {
            spec.AssignLiteral(kFETCH_CLIENT_REFERRER_STR);
          }
        }
      }

      request->SetReferrer(spec);
    }
  }

  if (aInit.mReferrerPolicy.WasPassed()) {
    request->SetReferrerPolicy(aInit.mReferrerPolicy.Value());
  }

  if (aInit.mSignal.WasPassed()) {
    signal = aInit.mSignal.Value();
  }

  // https://fetch.spec.whatwg.org/#dom-global-fetch
  // https://fetch.spec.whatwg.org/#dom-request
  // The priority of init overrides input's priority.
  if (aInit.mPriority.WasPassed()) {
    request->SetPriorityMode(aInit.mPriority.Value());
  }

  UniquePtr<mozilla::ipc::PrincipalInfo> principalInfo;
  nsILoadInfo::CrossOriginEmbedderPolicy coep =
      nsILoadInfo::EMBEDDER_POLICY_NULL;

  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal);
    if (window) {
      nsCOMPtr<Document> doc;
      doc = window->GetExtantDoc();
      if (doc) {
        request->SetEnvironmentReferrerPolicy(doc->GetReferrerPolicy());

        principalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>();
        nsresult rv =
            PrincipalToPrincipalInfo(doc->NodePrincipal(), principalInfo.get());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
          return nullptr;
        }
      }
      if (window->GetWindowContext()) {
        coep = window->GetWindowContext()->GetEmbedderPolicy();
      }
    }
  } else {
    WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
    if (worker) {
      worker->AssertIsOnWorkerThread();
      request->SetEnvironmentReferrerPolicy(worker->GetReferrerPolicy());
      principalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>(
          worker->GetPrincipalInfo());
      coep = worker->GetEmbedderPolicy();
      // For dedicated worker, the response must respect the owner's COEP.
      if (coep == nsILoadInfo::EMBEDDER_POLICY_NULL &&
          worker->IsDedicatedWorker()) {
        coep = worker->GetOwnerEmbedderPolicy();
      }
    }
  }

  request->SetPrincipalInfo(std::move(principalInfo));
  request->SetEmbedderPolicy(coep);

  if (mode.isSome()) {
    request->SetMode(mode.value());
  }

  if (credentials.isSome()) {
    request->SetCredentialsMode(credentials.value());
  }

  if (cache.isSome()) {
    if (cache.value() == RequestCache::Only_if_cached &&
        request->Mode() != RequestMode::Same_origin) {
      aRv.ThrowTypeError<MSG_ONLY_IF_CACHED_WITHOUT_SAME_ORIGIN>(
          GetEnumString(request->Mode()));
      return nullptr;
    }
    request->SetCacheMode(cache.value());
  }

  if (aInit.mRedirect.WasPassed()) {
    request->SetRedirectMode(aInit.mRedirect.Value());
  }

  if (aInit.mIntegrity.WasPassed()) {
    request->SetIntegrity(aInit.mIntegrity.Value());
  }

  if (aInit.mKeepalive.WasPassed()) {
    request->SetKeepalive(aInit.mKeepalive.Value());
  }

  if (aInit.mMozErrors.WasPassed() && aInit.mMozErrors.Value()) {
    request->SetMozErrors();
  }

  if (aInit.mTriggeringPrincipal.WasPassed() &&
      aInit.mTriggeringPrincipal.Value()) {
    request->SetTriggeringPrincipal(aInit.mTriggeringPrincipal.Value());
  }

  if (aInit.mNeverTaint.WasPassed()) {
    if (!XRE_IsParentProcess()) {
      aRv.ThrowNotAllowedError(
          "Taint has to happen outside of the parent process.");
      return nullptr;
    }
    request->SetNeverTaint(aInit.mNeverTaint.Value());
  }

  if (aInit.mCookieJarSettings.WasPassed() &&
      aInit.mCookieJarSettings.Value()) {
    request->SetCookieJarSettings(aInit.mCookieJarSettings.Value());
  }

  // Request constructor step 14.
  if (aInit.mMethod.WasPassed()) {
    nsAutoCString method(aInit.mMethod.Value());

    // Step 14.1. Disallow forbidden methods, and anything that is not a HTTP
    // token, since HTTP states that Method may be any of the defined values or
    // a token (extension method).
    nsAutoCString outMethod;
    nsresult rv = FetchUtil::GetValidRequestMethod(method, outMethod);
    if (NS_FAILED(rv)) {
      aRv.ThrowTypeError<MSG_INVALID_REQUEST_METHOD>(method);
      return nullptr;
    }

    // Step 14.2
    request->SetMethod(outMethod);
  }

  RefPtr<InternalHeaders> requestHeaders = request->Headers();

  // From "Let r be a new Request object associated with request and a new
  // Headers object whose guard is "request"."
  requestHeaders->SetGuard(HeadersGuardEnum::Request, aRv);
  MOZ_ASSERT(!aRv.Failed());

  if (request->Mode() == RequestMode::No_cors) {
    if (!request->HasSimpleMethod()) {
      nsAutoCString method;
      request->GetMethod(method);
      aRv.ThrowTypeError<MSG_INVALID_REQUEST_METHOD>(method);
      return nullptr;
    }

    requestHeaders->SetGuard(HeadersGuardEnum::Request_no_cors, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  // Step 33. Remove privileged no-CORS request-headers, if any member presented
  // in aInit.
  if (aInit.IsAnyMemberPresent()) {
    RefPtr<InternalHeaders> headers;
    if (aInit.mHeaders.WasPassed()) {
      RefPtr<Headers> h = Headers::Create(aGlobal, aInit.mHeaders.Value(), aRv);
      if (aRv.Failed()) {
        return nullptr;
      }
      headers = h->GetInternalHeaders();
    } else {
      headers = new InternalHeaders(*requestHeaders);
    }
    requestHeaders->Clear();
    requestHeaders->Fill(*headers, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  if ((aInit.mBody.WasPassed() && !aInit.mBody.Value().IsNull()) ||
      hasCopiedBody) {
    // HEAD and GET are not allowed to have a body.
    nsAutoCString method;
    request->GetMethod(method);
    // method is guaranteed to be uppercase due to step 14.2 above.
    if (method.EqualsLiteral("HEAD") || method.EqualsLiteral("GET")) {
      aRv.ThrowTypeError("HEAD or GET Request cannot have a body.");
      return nullptr;
    }
  }

  // Step 39: validate a body whose source is null. With the pref off, stream
  // bodies are stringified instead.
  const bool hasInitBody =
      aInit.mBody.WasPassed() && !aInit.mBody.Value().IsNull();
  const bool hasStreamBody =
      hasInitBody ? StaticPrefs::dom_fetch_streaming_upload() &&
                        aInit.mBody.Value().Value().IsReadableStream()
                  : request->HasStreamBody();
  if (hasStreamBody) {
    if (hasInitBody && !aInit.mDuplex.WasPassed()) {
      aRv.ThrowTypeError(
          "duplex parameter is required when body is a ReadableStream");
      return nullptr;
    }
    if (request->Mode() != RequestMode::Same_origin &&
        request->Mode() != RequestMode::Cors) {
      aRv.ThrowTypeError(
          "ReadableStream bodies require same-origin or cors mode");
      return nullptr;
    }
  }

  if (aInit.mBody.WasPassed()) {
    const Nullable<fetch::OwningBodyInit>& bodyInitNullable =
        aInit.mBody.Value();
    if (!bodyInitNullable.IsNull()) {
      const fetch::OwningBodyInit& bodyInit = bodyInitNullable.Value();
      nsCOMPtr<nsIInputStream> stream;
      nsAutoCString contentTypeWithCharset;
      uint64_t extractedLength = 0;
      int64_t contentLength = 0;

      // ReadableStream is in the BodyInit union unconditionally, because WebIDL
      // typedefs cannot be pref-gated. Until the feature ships, keep converting
      // it to a USVString, which is what the union used to do.
      const bool streamBodyDisabled =
          bodyInit.IsReadableStream() &&
          !StaticPrefs::dom_fetch_streaming_upload();

      // Handle ReadableStream bodies separately
      if (streamBodyDisabled) {
        nsAutoString stringified(u"[object ReadableStream]"_ns);
        nsAutoCString charset;
        BodyExtractor<const nsAString> body(&stringified);
        aRv = body.GetAsStream(getter_AddRefs(stream), &extractedLength,
                               contentTypeWithCharset, charset);
        if (NS_WARN_IF(aRv.Failed())) {
          return nullptr;
        }
        contentLength = static_cast<int64_t>(extractedLength);
      } else if (bodyInit.IsReadableStream()) {
        aRv.MightThrowJSException();

        ReadableStream& readableStream = bodyInit.GetAsReadableStream();

        // https://fetch.spec.whatwg.org/#concept-bodyinit-extract step 10,
        // ReadableStream case: extract is invoked with the request's keepalive,
        // and a streaming body cannot be combined with it.
        if (request->GetKeepalive()) {
          aRv.ThrowTypeError(
              "keepalive cannot be used with a ReadableStream body");
          return nullptr;
        }

        if (readableStream.Locked() || readableStream.Disturbed()) {
          aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
          return nullptr;
        }

        temporaryStreamBody = &readableStream;

        // If this is a DOM generated ReadableStream, extract the inputStream
        if (nsIInputStream* underlyingSource =
                readableStream.MaybeGetInputStreamIfUnread()) {
          stream = underlyingSource;
        } else {
          // For JS-created streams, use FetchStreamReader
          RefPtr<FetchStreamReader> streamReader;
          nsCOMPtr<nsIInputStream> pipeInputStream;
          aRv = FetchStreamReader::Create(aCx, aGlobal,
                                          getter_AddRefs(streamReader),
                                          getter_AddRefs(pipeInputStream));
          if (NS_WARN_IF(aRv.Failed())) {
            return nullptr;
          }

          stream = pipeInputStream.forget();

          // Store stream reader to keep it alive
          temporaryStreamReader = streamReader.forget();
        }

        // The length is only known once the stream ends.
        contentLength = -1;
        contentTypeWithCharset.SetIsVoid(true);
      } else {
        aRv =
            ExtractByteStreamFromBody(bodyInit, getter_AddRefs(stream),
                                      contentTypeWithCharset, extractedLength);
        if (NS_WARN_IF(aRv.Failed())) {
          return nullptr;
        }
        contentLength = static_cast<int64_t>(extractedLength);
      }

      nsCOMPtr<nsIInputStream> temporaryBody = stream;

      if (!contentTypeWithCharset.IsVoid() &&
          !requestHeaders->Has("Content-Type"_ns, aRv)) {
        requestHeaders->Append("Content-Type"_ns, contentTypeWithCharset, aRv);
      }

      if (NS_WARN_IF(aRv.Failed())) {
        return nullptr;
      }

      if (hasCopiedBody) {
        request->SetBody(nullptr, 0);
      }

      request->SetBody(temporaryBody, contentLength);
      request->SetHasStreamBody(hasStreamBody);
    }
  }

  auto domRequest =
      MakeSafeRefPtr<Request>(aGlobal, std::move(request), signal);

  if (temporaryStreamReader) {
    domRequest->mFetchStreamReader = temporaryStreamReader.forget();
  }

  if (temporaryStreamBody) {
    domRequest->SetReadableStreamBody(aCx, temporaryStreamBody);
  }

  if (aInput.IsRequest() && !bodyFromInit) {
    RefPtr<Request> inputReq = &aInput.GetAsRequest();
    nsCOMPtr<nsIInputStream> body;
    inputReq->GetBody(getter_AddRefs(body));
    if (body) {
      if (inputReq->mFetchStreamReader) {
        // Step 41: proxy the JS stream with a single consumer and preserve
        // backpressure until the new Request is consumed.
        JS::Rooted<JSObject*> globalObject(aCx, aGlobal->GetGlobalJSObject());
        GlobalObject global(aCx, globalObject);
        RefPtr<TransformStream> transform = TransformStream::Constructor(
            global, Optional<JS::Handle<JSObject*>>(), QueuingStrategy(),
            QueuingStrategy(), aRv);
        if (aRv.Failed()) {
          return nullptr;
        }
        ReadableWritablePair pair;
        pair.mReadable = transform->Readable();
        pair.mWritable = transform->Writable();
        RefPtr<ReadableStream> source = inputReq->mReadableStreamBody;
        // PipeThrough() locks the source, which is what leaves inputReq
        // unusable. SetBodyUsed() must not be called on it: that would start
        // its reader consuming a stream the pipe already holds.
        RefPtr<ReadableStream> proxy =
            source->PipeThrough(pair, StreamPipeOptions(), aRv);
        if (aRv.Failed()) {
          return nullptr;
        }
        nsCOMPtr<nsIInputStream> proxyInput;
        aRv = FetchStreamReader::Create(
            aCx, aGlobal, getter_AddRefs(domRequest->mFetchStreamReader),
            getter_AddRefs(proxyInput));
        if (aRv.Failed()) {
          return nullptr;
        }
        domRequest->SetBody(nullptr, 0);
        domRequest->SetBody(proxyInput, -1);
        domRequest->SetReadableStreamBody(aCx, proxy);
        return domRequest;
      }
      inputReq->SetBody(nullptr, 0);
      inputReq->SetBodyUsed(aCx, aRv);
      if (NS_WARN_IF(aRv.Failed())) {
        return nullptr;
      }
    }
  }
  return domRequest;
}

SafeRefPtr<Request> Request::Clone(JSContext* aCx, ErrorResult& aRv) {
  if (IsBodyUnusable()) {
    aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
    return nullptr;
  }

  RefPtr<ReadableStream> body;
  RefPtr<FetchStreamReader> streamReader;
  nsCOMPtr<nsIInputStream> inputStream;
  MaybeTeeReadableStreamBody(aCx, getter_AddRefs(body),
                             getter_AddRefs(streamReader),
                             getter_AddRefs(inputStream), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  SafeRefPtr<InternalRequest> ir = mRequest->Clone();
  if (!ir) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  auto clone =
      MakeSafeRefPtr<Request>(mGlobal, std::move(ir), GetOrCreateSignal());
  if (body) {
    clone->SetBody(nullptr, 0);
    clone->SetBody(inputStream, -1);
    clone->mFetchStreamReader = streamReader.forget();
    clone->SetReadableStreamBody(aCx, body);
  } else {
    // Rebind a native stream if cloning replaced its underlying input stream.
    MaybeRebindReadableStreamBody();
  }
  return clone;
}

already_AddRefed<nsIInputStream> Request::TakeBodyForServiceWorker(
    JSContext* aCx, ErrorResult& aRv) {
  MOZ_ASSERT(HasStreamBody());
  MOZ_ASSERT(!IsBodyUnusable());
  RefPtr<ReadableStream> body = FetchBody<Request>::GetBody(aCx, aRv);
  if (aRv.Failed() || NS_WARN_IF(!body)) {
    return nullptr;
  }
  nsCOMPtr<nsIInputStream> stream;
  GetBody(getter_AddRefs(stream));
  // Close the worker's JS body without cancelling the stream returned to Necko:
  // NonAsyncInputToReadableStreamAlgorithms::ReleaseObjects() only closes its
  // mInput, so detaching the input stream first leaves ours open. The
  // !mAsyncAlgorithms assertion in SetInputStreamIfUnread() holds because a
  // pull would have disturbed the stream and failed IsBodyUnusable() above.
  body->SetInputStreamIfUnread(nullptr);
  SetBody(nullptr, 0);
  CancelBody(aCx, aRv);
  return stream.forget();
}

void Request::FollowBodySignal() {
  if (!mSignal) {
    return;
  }
  if (mFetchStreamReader) {
    mFetchStreamReader->FollowSignal(mSignal);
  } else if (mReadableStreamBody) {
    Follow(mSignal);
  }
}

Headers* Request::Headers_() {
  if (!mHeaders) {
    mHeaders = new Headers(mGlobal, mRequest->Headers());
  }

  return mHeaders;
}

AbortSignal* Request::GetOrCreateSignal() {
  if (!mSignal) {
    mSignal = AbortSignal::Create(mGlobal, SignalAborted::No,
                                  JS::UndefinedHandleValue);
  }

  return mSignal;
}

AbortSignalImpl* Request::GetSignalImpl() const { return mSignal; }

AbortSignalImpl* Request::GetSignalImplToConsumeBody() const {
  // The signal controls fetch(), not consumption of an unused Request's body.
  return nullptr;
}

}  // namespace mozilla::dom
