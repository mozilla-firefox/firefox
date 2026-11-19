/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoViewStreamListener.h"

#include "mozilla/fallible.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIChannelEventSink.h"
#include "nsIHttpChannel.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIInputStream.h"
#include "nsINSSErrorsService.h"
#include "nsITransportSecurityInfo.h"
#include "nsIWebProgressListener.h"
#include "nsIX509Cert.h"
#include "nsPrintfCString.h"
#include "nsContentSecurityUtils.h"
#include "nsITransfer.h"

#include "nsNetUtil.h"

#include "JavaBuiltins.h"

using namespace mozilla;

NS_IMPL_ISUPPORTS(GeckoViewStreamListener, nsIStreamListener,
                  nsIInterfaceRequestor, nsIChannelEventSink)

class HeaderVisitor final : public nsIHttpHeaderVisitor {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit HeaderVisitor(java::WebResponse::Builder::Param aBuilder)
      : mBuilder(aBuilder) {}

  NS_IMETHOD
  VisitHeader(const nsACString& aHeader, const nsACString& aValue) override {
    mBuilder->Header(aHeader, aValue);
    return NS_OK;
  }

 private:
  virtual ~HeaderVisitor() {}

  const java::WebResponse::Builder::GlobalRef mBuilder;
};

NS_IMPL_ISUPPORTS(HeaderVisitor, nsIHttpHeaderVisitor)

class StreamSupport final
    : public java::GeckoInputStream::Support::Natives<StreamSupport> {
 public:
  typedef java::GeckoInputStream::Support::Natives<StreamSupport> Base;
  using Base::AttachNative;
  using Base::GetNative;

  explicit StreamSupport(java::GeckoInputStream::Support::Param aInstance,
                         nsIRequest* aRequest)
      : mInstance(aInstance), mRequest(aRequest) {}

  void Close() {
    mRequest->Cancel(NS_ERROR_ABORT);
    mRequest->Resume();

    // This is basically `delete this`, so don't run anything else!
    Base::DisposeNative(mInstance);
  }

  void Resume() { mRequest->Resume(); }

 private:
  java::GeckoInputStream::Support::GlobalRef mInstance;
  nsCOMPtr<nsIRequest> mRequest;
};

NS_IMETHODIMP
GeckoViewStreamListener::OnStartRequest(nsIRequest* aRequest) {
  MOZ_ASSERT(!mStream);

  nsresult status;
  aRequest->GetStatus(&status);
  if (NS_FAILED(status)) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    CompleteWithError(status, channel);
    return NS_OK;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (channel) {
    int32_t classification = nsContentSecurityUtils::ClassifyDownload(channel);
    if (classification == nsITransfer::DOWNLOAD_FORBIDDEN) {
      channel->Cancel(NS_ERROR_ABORT);
      CompleteWithError(NS_ERROR_ABORT, channel);
      return NS_OK;
    }
  }

  // We're expecting data later via OnDataAvailable, so create the stream now.
  InitializeStreamSupport(aRequest);

  mStream = java::GeckoInputStream::New(mSupport);

  // Suspend the request immediately. It will be resumed when (if) someone
  // tries to read the Java stream.
  aRequest->Suspend();

  nsresult rv = HandleWebResponse(aRequest);
  if (NS_FAILED(rv)) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    CompleteWithError(rv, channel);
    return NS_OK;
  }

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewStreamListener::OnStopRequest(nsIRequest* aRequest,
                                       nsresult aStatusCode) {
  if (mStream) {
    if (NS_FAILED(aStatusCode)) {
      mStream->SendError();
    } else {
      mStream->SendEof();
    }
  }
  return NS_OK;
}

NS_IMETHODIMP GeckoViewStreamListener::OnDataAvailable(
    nsIRequest* aRequest, nsIInputStream* aInputStream, uint64_t aOffset,
    uint32_t aCount) {
  MOZ_ASSERT(mStream);

  // We only need this for the ReadSegments call, the value is unused.
  uint32_t countRead;
  nsresult rv =
      aInputStream->ReadSegments(WriteSegment, this, aCount, &countRead);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mStream->ShouldSuspend()) {
    aRequest->Suspend();
  }
  return rv;
}

NS_IMETHODIMP
GeckoViewStreamListener::GetInterface(const nsIID& aIID, void** aResultOut) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *aResultOut = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  return NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
GeckoViewStreamListener::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t flags,
    nsIAsyncVerifyRedirectCallback* callback) {
  callback->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

/* static */
nsresult GeckoViewStreamListener::WriteSegment(
    nsIInputStream* aInputStream, void* aClosure, const char* aFromSegment,
    uint32_t aToOffset, uint32_t aCount, uint32_t* aWriteCount) {
  GeckoViewStreamListener* self =
      static_cast<GeckoViewStreamListener*>(aClosure);
  MOZ_ASSERT(self);
  MOZ_ASSERT(self->mStream);

  *aWriteCount = aCount;

  jni::ByteArray::LocalRef buffer = jni::ByteArray::New(
      reinterpret_cast<signed char*>(const_cast<char*>(aFromSegment)),
      *aWriteCount, fallible);
  if (!buffer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (NS_FAILED(self->mStream->AppendBuffer(buffer))) {
    // The stream was closed or something, abort reading this channel.
    return NS_ERROR_ABORT;
  }

  return NS_OK;
}

nsresult GeckoViewStreamListener::HandleWebResponse(nsIRequest* aRequest) {
  nsresult rv;

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // URI
  nsCOMPtr<nsIURI> uri;
  rv = channel->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString uriSpec;
  rv = uri->GetSpec(uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  java::WebResponse::Builder::LocalRef builder =
      java::WebResponse::Builder::New(uriSpec);

  // Body stream
  if (mStream) {
    builder->Body(mStream);
  }

  // Suggested download filename
  nsString filename;
  if (NS_SUCCEEDED(channel->GetContentDispositionFilename(filename))) {
    builder->Header(jni::StringParam(u"content-disposition"_ns),
                    nsPrintfCString("attachment; filename=\"%s\"",
                                    NS_ConvertUTF16toUTF8(filename).get()));
  }

  // Redirected
  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  builder->Redirected(!loadInfo->RedirectChain().IsEmpty());

  // Secure status
  auto [certBytes, isSecure] = CertificateFromChannel(channel);
  builder->IsSecure(isSecure);
  if (certBytes) {
    rv = builder->CertificateBytes(certBytes);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // We might need some additional info for response to http/https request
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel, &rv));
  if (httpChannel) {
    // Status code
    uint32_t statusCode;
    rv = httpChannel->GetResponseStatus(&statusCode);
    NS_ENSURE_SUCCESS(rv, rv);
    builder->StatusCode(statusCode);

    // Headers
    RefPtr<HeaderVisitor> visitor = new HeaderVisitor(builder);
    rv = httpChannel->VisitResponseHeaders(visitor);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // Headers for other responses
    // try to provide some basic metadata about the response

    nsCString contentType;
    if (NS_SUCCEEDED(channel->GetContentType(contentType))) {
      builder->Header(jni::StringParam(u"content-type"_ns), contentType);
    }

    int64_t contentLength = 0;
    if (NS_SUCCEEDED(channel->GetContentLength(&contentLength))) {
      nsString contentLengthString;
      contentLengthString.AppendInt(contentLength);
      builder->Header(jni::StringParam(u"content-length"_ns),
                      contentLengthString);
    }
  }

  AmendWebResponse(builder);

  java::WebResponse::GlobalRef response = builder->Build();

  SendWebResponse(response);
  return NS_OK;
}

void GeckoViewStreamListener::InitializeStreamSupport(nsIRequest* aRequest) {
  StreamSupport::Init();

  mSupport = java::GeckoInputStream::Support::New();
  StreamSupport::AttachNative(
      mSupport, std::make_unique<StreamSupport>(mSupport, aRequest));
}

std::tuple<jni::ByteArray::LocalRef, bool>
GeckoViewStreamListener::CertificateFromChannel(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsITransportSecurityInfo> securityInfo;
  aChannel->GetSecurityInfo(getter_AddRefs(securityInfo));
  if (!securityInfo) {
    return std::make_tuple((jni::ByteArray::LocalRef) nullptr, false);
  }

  uint32_t securityState = 0;
  securityInfo->GetSecurityState(&securityState);
  bool isSecure = securityState == nsIWebProgressListener::STATE_IS_SECURE;

  nsCOMPtr<nsIX509Cert> cert;
  securityInfo->GetServerCert(getter_AddRefs(cert));
  if (!cert) {
    return std::make_tuple((jni::ByteArray::LocalRef) nullptr, false);
  }

  nsTArray<uint8_t> derBytes;
  nsresult rv = cert->GetRawDER(derBytes);
  NS_ENSURE_SUCCESS(rv,
                    std::make_tuple((jni::ByteArray::LocalRef) nullptr, false));

  auto certBytes = jni::ByteArray::New(
      reinterpret_cast<const int8_t*>(derBytes.Elements()), derBytes.Length());

  return std::make_tuple(certBytes, isSecure);
}
