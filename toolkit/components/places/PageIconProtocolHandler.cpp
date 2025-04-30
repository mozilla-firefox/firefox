/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PageIconProtocolHandler.h"

#include "mozilla/NullPrincipal.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Components.h"
#include "mozilla/Try.h"
#include "nsFaviconService.h"
#include "nsStringStream.h"
#include "nsStreamUtils.h"
#include "nsIChannel.h"
#include "nsIFaviconService.h"
#include "nsIIOService.h"
#include "nsILoadInfo.h"
#include "nsIOutputStream.h"
#include "nsIPipe.h"
#include "nsIRequestObserver.h"
#include "nsIURIMutator.h"
#include "nsNetUtil.h"
#include "SimpleChannel.h"
#include "mozilla/glean/PlacesMetrics.h"

#define PAGE_ICON_SCHEME "page-icon"

using mozilla::net::IsNeckoChild;
using mozilla::net::NeckoChild;
using mozilla::net::RemoteStreamGetter;
using mozilla::net::RemoteStreamInfo;

namespace mozilla::places {

struct FaviconMetadata {
  nsCOMPtr<nsIInputStream> mStream;
  nsCString mContentType;
  int64_t mContentLength = 0;
  uint16_t mWidth = 0;
};

static nsresult GetFaviconMetadata(
    const FaviconPromise::ResolveOrRejectValue& aResult,
    FaviconMetadata& aMetadata) {
  if (aResult.IsReject()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIFavicon> favicon = aResult.ResolveValue();
  if (!favicon) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsTArray<uint8_t> rawData;
  favicon->GetRawData(rawData);

  if (rawData.IsEmpty()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = NS_NewByteInputStream(
      getter_AddRefs(stream),
      AsChars(Span{rawData.Elements(), rawData.Length()}), NS_ASSIGNMENT_COPY);
  NS_ENSURE_SUCCESS(rv, rv);

  favicon->GetWidth(&aMetadata.mWidth);
  favicon->GetMimeType(aMetadata.mContentType);
  aMetadata.mStream = stream;
  aMetadata.mContentLength = rawData.Length();

  return NS_OK;
}

void RecordIconSizeTelemetry(nsIURI* uri, const FaviconMetadata& metadata) {
  uint16_t preferredSize = INT16_MAX;
  auto* faviconService = nsFaviconService::GetFaviconService();

  if (!MOZ_UNLIKELY(!faviconService)) {
    faviconService->PreferredSizeFromURI(uri, &preferredSize);
    if (metadata.mWidth < preferredSize) {
      mozilla::glean::page_icon::small_icon_count.Add(1);
    } else {
      mozilla::glean::page_icon::fit_icon_count.Add(1);
    }
  }
}

StaticRefPtr<PageIconProtocolHandler> PageIconProtocolHandler::sSingleton;

namespace {

class DefaultFaviconObserver final : public nsIRequestObserver {
 public:
  explicit DefaultFaviconObserver(nsIOutputStream* aOutputStream)
      : mOutputStream(aOutputStream) {
    MOZ_ASSERT(aOutputStream);
  }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
 private:
  ~DefaultFaviconObserver() = default;

  nsCOMPtr<nsIOutputStream> mOutputStream;
};

NS_IMPL_ISUPPORTS(DefaultFaviconObserver, nsIRequestObserver);

NS_IMETHODIMP DefaultFaviconObserver::OnStartRequest(nsIRequest*) {
  return NS_OK;
}

NS_IMETHODIMP DefaultFaviconObserver::OnStopRequest(nsIRequest*, nsresult) {
  // We must close the outputStream regardless.
  mOutputStream->Close();
  return NS_OK;
}

}  // namespace

static nsresult MakeDefaultFaviconChannel(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                                          nsIChannel** aOutChannel) {
  nsCOMPtr<nsIIOService> ios = mozilla::components::IO::Service();
  nsCOMPtr<nsIChannel> chan;
  nsCOMPtr<nsIURI> defaultFaviconURI;

  auto* faviconService = nsFaviconService::GetFaviconService();
  if (MOZ_UNLIKELY(!faviconService)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsresult rv =
      faviconService->GetDefaultFavicon(getter_AddRefs(defaultFaviconURI));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = ios->NewChannelFromURIWithLoadInfo(defaultFaviconURI, aLoadInfo,
                                          getter_AddRefs(chan));
  NS_ENSURE_SUCCESS(rv, rv);
  chan->SetOriginalURI(aURI);
  chan->SetContentType(nsLiteralCString(FAVICON_DEFAULT_MIMETYPE));
  chan.forget(aOutChannel);
  return NS_OK;
}

static nsresult StreamDefaultFavicon(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                                     nsIOutputStream* aOutputStream) {
  auto closeStreamOnError =
      mozilla::MakeScopeExit([&] { aOutputStream->Close(); });

  auto observer = MakeRefPtr<DefaultFaviconObserver>(aOutputStream);

  nsCOMPtr<nsIStreamListener> listener;
  nsresult rv = NS_NewSimpleStreamListener(getter_AddRefs(listener),
                                           aOutputStream, observer);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> defaultIconChannel;
  rv = MakeDefaultFaviconChannel(aURI, aLoadInfo,
                                 getter_AddRefs(defaultIconChannel));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = defaultIconChannel->AsyncOpen(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  closeStreamOnError.release();
  return NS_OK;
}

NS_IMPL_ISUPPORTS(PageIconProtocolHandler, nsIProtocolHandler,
                  nsISupportsWeakReference);

NS_IMETHODIMP PageIconProtocolHandler::GetScheme(nsACString& aScheme) {
  aScheme.AssignLiteral(PAGE_ICON_SCHEME);
  return NS_OK;
}

NS_IMETHODIMP PageIconProtocolHandler::AllowPort(int32_t, const char*,
                                                 bool* aAllow) {
  *aAllow = false;
  return NS_OK;
}

NS_IMETHODIMP PageIconProtocolHandler::NewChannel(nsIURI* aURI,
                                                  nsILoadInfo* aLoadInfo,
                                                  nsIChannel** aOutChannel) {
  // Load the URI remotely if accessed from a child.
  if (IsNeckoChild()) {
    MOZ_TRY(SubstituteRemoteChannel(aURI, aLoadInfo, aOutChannel));
    return NS_OK;
  }

  nsresult rv = NewChannelInternal(aURI, aLoadInfo, aOutChannel);
  if (NS_SUCCEEDED(rv)) {
    return rv;
  }
  return MakeDefaultFaviconChannel(aURI, aLoadInfo, aOutChannel);
}

Result<Ok, nsresult> PageIconProtocolHandler::SubstituteRemoteChannel(
    nsIURI* aURI, nsILoadInfo* aLoadInfo, nsIChannel** aRetVal) {
  MOZ_ASSERT(IsNeckoChild());
  MOZ_TRY(aURI ? NS_OK : NS_ERROR_INVALID_ARG);
  MOZ_TRY(aLoadInfo ? NS_OK : NS_ERROR_INVALID_ARG);

  RefPtr<RemoteStreamGetter> streamGetter =
      new RemoteStreamGetter(aURI, aLoadInfo);

  NewSimpleChannel(aURI, aLoadInfo, streamGetter, aRetVal);
  return Ok();
}

nsresult PageIconProtocolHandler::NewChannelInternal(nsIURI* aURI,
                                                     nsILoadInfo* aLoadInfo,
                                                     nsIChannel** aOutChannel) {
  // Create a pipe that will give us an output stream that we can use once
  // we got all the favicon data.
  nsCOMPtr<nsIAsyncInputStream> pipeIn;
  nsCOMPtr<nsIAsyncOutputStream> pipeOut;
  GetStreams(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut));

  // Create our channel.
  nsCOMPtr<nsIChannel> channel;
  {
    // We override the channel's loadinfo below anyway, so using a null
    // principal here is alright.
    nsCOMPtr<nsIPrincipal> loadingPrincipal =
        NullPrincipal::CreateWithoutOriginAttributes();
    nsresult rv = NS_NewInputStreamChannel(
        getter_AddRefs(channel), aURI, pipeIn.forget(), loadingPrincipal,
        nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED,
        nsIContentPolicy::TYPE_INTERNAL_IMAGE);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsresult rv = channel->SetLoadInfo(aLoadInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  GetFaviconData(aURI)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [pipeOut, channel, uri = nsCOMPtr{aURI}, loadInfo = nsCOMPtr{aLoadInfo}](
          const FaviconPromise::ResolveOrRejectValue& aResult) {
        FaviconMetadata metadata;
        if (NS_SUCCEEDED(GetFaviconMetadata(aResult, metadata))) {
          channel->SetContentType(metadata.mContentType);
          channel->SetContentLength(metadata.mContentLength);

          nsresult rv;
          const nsCOMPtr<nsIEventTarget> target =
              do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            channel->CancelWithReason(NS_BINDING_ABORTED,
                                      "GetFaviconData failed"_ns);
            return;
          }

          rv = NS_AsyncCopy(metadata.mStream, pipeOut, target);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return;
          }
          RecordIconSizeTelemetry(uri, metadata);
        } else {
          // There are a few reasons why this might fail. For example, one
          // reason is that the URI might not actually be properly parsable.
          // In that case, we'll try one last time to stream the default
          // favicon before giving up.
          channel->SetContentType(nsLiteralCString(FAVICON_DEFAULT_MIMETYPE));
          channel->SetContentLength(-1);
          Unused << StreamDefaultFavicon(uri, loadInfo, pipeOut);
        }
      });

  channel.forget(aOutChannel);
  return NS_OK;
}

RefPtr<FaviconPromise> PageIconProtocolHandler::GetFaviconData(
    nsIURI* aPageIconURI) {
  auto* faviconService = nsFaviconService::GetFaviconService();
  if (MOZ_UNLIKELY(!faviconService)) {
    return FaviconPromise::CreateAndReject(NS_ERROR_UNEXPECTED, __func__);
  }

  uint16_t preferredSize = 0;
  faviconService->PreferredSizeFromURI(aPageIconURI, &preferredSize);

  nsCOMPtr<nsIURI> pageURI;
  nsresult rv;
  // NOTE: We don't need to strip #size= fragments because
  // GetFaviconDataForPage strips them when doing the database lookup.
  nsAutoCString pageQuery;
  aPageIconURI->GetPathQueryRef(pageQuery);
  rv = NS_NewURI(getter_AddRefs(pageURI), pageQuery);
  if (NS_FAILED(rv)) {
    return FaviconPromise::CreateAndReject(rv, __func__);
  }

  return faviconService->AsyncGetFaviconForPage(pageURI, preferredSize);
}

RefPtr<RemoteStreamPromise> PageIconProtocolHandler::NewStream(
    nsIURI* aChildURI, nsILoadInfo* aLoadInfo, bool* aTerminateSender) {
  MOZ_ASSERT(!IsNeckoChild());
  MOZ_ASSERT(NS_IsMainThread());

  if (!aChildURI || !aLoadInfo || !aTerminateSender) {
    return RemoteStreamPromise::CreateAndReject(NS_ERROR_INVALID_ARG, __func__);
  }

  *aTerminateSender = true;

  // We should never receive a URI that isn't for a page-icon because
  // these requests ordinarily come from the child's PageIconProtocolHandler.
  // Ensure this request is for a page-icon URI. A compromised child process
  // could send us any URI.
  bool isPageIconScheme = false;
  if (NS_FAILED(aChildURI->SchemeIs(PAGE_ICON_SCHEME, &isPageIconScheme)) ||
      !isPageIconScheme) {
    return RemoteStreamPromise::CreateAndReject(NS_ERROR_UNKNOWN_PROTOCOL,
                                                __func__);
  }

  // For errors after this point, we want to propagate the error to
  // the child, but we don't force the child process to be terminated.
  *aTerminateSender = false;

  RefPtr<RemoteStreamPromise::Private> outerPromise =
      new RemoteStreamPromise::Private(__func__);
  nsCOMPtr<nsIURI> uri(aChildURI);
  nsCOMPtr<nsILoadInfo> loadInfo(aLoadInfo);
  RefPtr<PageIconProtocolHandler> self = this;

  GetFaviconData(uri)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, uri, loadInfo, outerPromise, childURI = nsCOMPtr{aChildURI}](
          const FaviconPromise::ResolveOrRejectValue& aResult) {
        FaviconMetadata metadata;
        if (NS_SUCCEEDED(GetFaviconMetadata(aResult, metadata))) {
          RecordIconSizeTelemetry(childURI, metadata);
          RemoteStreamInfo info(metadata.mStream, metadata.mContentType,
                                metadata.mContentLength);
          outerPromise->Resolve(std::move(info), __func__);
        } else {
          nsCOMPtr<nsIAsyncInputStream> pipeIn;
          nsCOMPtr<nsIAsyncOutputStream> pipeOut;
          self->GetStreams(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut));

          RemoteStreamInfo info(pipeIn,
                                nsLiteralCString(FAVICON_DEFAULT_MIMETYPE), -1);
          Unused << StreamDefaultFavicon(uri, loadInfo, pipeOut);
          outerPromise->Resolve(std::move(info), __func__);
        }
      });

  return outerPromise;
}

void PageIconProtocolHandler::GetStreams(nsIAsyncInputStream** inStream,
                                         nsIAsyncOutputStream** outStream) {
  static constexpr size_t kSegmentSize = 4096;
  nsCOMPtr<nsIAsyncInputStream> pipeIn;
  nsCOMPtr<nsIAsyncOutputStream> pipeOut;
  NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut), true, true,
              kSegmentSize,
              nsIFaviconService::MAX_FAVICON_BUFFER_SIZE / kSegmentSize);

  pipeIn.forget(inStream);
  pipeOut.forget(outStream);
}

// static
void PageIconProtocolHandler::NewSimpleChannel(
    nsIURI* aURI, nsILoadInfo* aLoadInfo, RemoteStreamGetter* aStreamGetter,
    nsIChannel** aRetVal) {
  nsCOMPtr<nsIChannel> channel = NS_NewSimpleChannel(
      aURI, aLoadInfo, aStreamGetter,
      [](nsIStreamListener* listener, nsIChannel* simpleChannel,
         RemoteStreamGetter* getter) -> RequestOrReason {
        return getter->GetAsync(listener, simpleChannel,
                                &NeckoChild::SendGetPageIconStream);
      });

  channel.swap(*aRetVal);
}

}  // namespace mozilla::places
