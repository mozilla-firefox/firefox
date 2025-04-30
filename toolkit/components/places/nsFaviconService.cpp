/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This is the favicon service, which stores favicons for web pages with your
 * history as you browse. It is also used to save the favicons for bookmarks.
 *
 * DANGER: The history query system makes assumptions about the favicon storage
 * so that icons can be quickly generated for history/bookmark result sets. If
 * you change the database layout at all, you will have to update both services.
 */

#include "nsFaviconService.h"

#include "nsNavHistory.h"
#include "nsPlacesMacros.h"
#include "Helpers.h"

#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsStreamUtils.h"
#include "plbase64.h"
#include "nsIClassInfoImpl.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/Promise.h"
#include "nsILoadInfo.h"
#include "nsIContentPolicy.h"
#include "nsIProtocolHandler.h"
#include "nsIScriptError.h"
#include "nsContentUtils.h"
#include "imgICache.h"

using namespace mozilla;
using namespace mozilla::places;

const uint16_t gFaviconSizes[7] = {192, 144, 96, 64, 48, 32, 16};

/**
 * Used to notify a topic to system observers on async execute completion.
 * Will throw on error.
 */
class ExpireFaviconsStatementCallbackNotifier : public AsyncStatementCallback {
 public:
  ExpireFaviconsStatementCallbackNotifier();
  NS_IMETHOD HandleCompletion(uint16_t aReason) override;
};

namespace {

/**
 * Extracts and filters native sizes from the given container, based on the
 * list of sizes we are supposed to retain.
 * All calculation is done considering square sizes and the largest side.
 * In case of multiple frames of the same size, only the first one is retained.
 */
nsresult GetFramesInfoForContainer(imgIContainer* aContainer,
                                   nsTArray<FrameData>& aFramesInfo) {
  // Don't extract frames from animated images.
  bool animated;
  nsresult rv = aContainer->GetAnimated(&animated);
  if (NS_FAILED(rv) || !animated) {
    nsTArray<nsIntSize> nativeSizes;
    rv = aContainer->GetNativeSizes(nativeSizes);
    if (NS_SUCCEEDED(rv) && nativeSizes.Length() > 1) {
      for (uint32_t i = 0; i < nativeSizes.Length(); ++i) {
        nsIntSize nativeSize = nativeSizes[i];
        // Only retain square frames.
        if (nativeSize.width != nativeSize.height) {
          continue;
        }
        // Check if it's one of the sizes we care about.
        const auto* end = std::end(gFaviconSizes);
        const uint16_t* matchingSize =
            std::find(std::begin(gFaviconSizes), end, nativeSize.width);
        if (matchingSize != end) {
          // We must avoid duped sizes, an image could contain multiple frames
          // of the same size, but we can only store one. We could use an
          // hashtable, but considered the average low number of frames, we'll
          // just do a linear search.
          bool dupe = false;
          for (const auto& frameInfo : aFramesInfo) {
            if (frameInfo.width == *matchingSize) {
              dupe = true;
              break;
            }
          }
          if (!dupe) {
            aFramesInfo.AppendElement(FrameData(i, *matchingSize));
          }
        }
      }
    }
  }

  if (aFramesInfo.Length() == 0) {
    // Always have at least the default size.
    int32_t width;
    rv = aContainer->GetWidth(&width);
    NS_ENSURE_SUCCESS(rv, rv);
    int32_t height;
    rv = aContainer->GetHeight(&height);
    NS_ENSURE_SUCCESS(rv, rv);
    // For non-square images, pick the largest side.
    aFramesInfo.AppendElement(FrameData(0, std::max(width, height)));
  }
  return NS_OK;
}

static constexpr nsLiteralCString supportedProtocols[] = {
    "http"_ns, "https"_ns, "file"_ns, "about"_ns};

bool canStoreIconForPage(nsIURI* aPageURI) {
  nsAutoCString pageURIScheme;
  return (NS_SUCCEEDED(aPageURI->GetScheme(pageURIScheme)) &&
          std::find(std::begin(supportedProtocols),
                    std::end(supportedProtocols),
                    pageURIScheme) != std::end(supportedProtocols));
}

}  // namespace

PLACES_FACTORY_SINGLETON_IMPLEMENTATION(nsFaviconService, gFaviconService)

NS_IMPL_CLASSINFO(nsFaviconService, nullptr, 0, NS_FAVICONSERVICE_CID)
NS_IMPL_ISUPPORTS_CI(nsFaviconService, nsIFaviconService)

nsFaviconService::nsFaviconService()
    : mDefaultIconURIPreferredSize(UINT16_MAX) {
  NS_ASSERTION(!gFaviconService,
               "Attempting to create two instances of the service!");
  gFaviconService = this;
}

nsFaviconService::~nsFaviconService() {
  NS_ASSERTION(gFaviconService == this,
               "Deleting a non-singleton instance of the service");
  if (gFaviconService == this) gFaviconService = nullptr;
}

Atomic<int64_t> nsFaviconService::sLastInsertedIconId(0);

void  // static
nsFaviconService::StoreLastInsertedId(const nsACString& aTable,
                                      const int64_t aLastInsertedId) {
  MOZ_ASSERT(aTable.EqualsLiteral("moz_icons"));
  sLastInsertedIconId = aLastInsertedId;
}

nsresult nsFaviconService::Init() {
  mDB = Database::GetDatabase();
  NS_ENSURE_STATE(mDB);
  return NS_OK;
}

NS_IMETHODIMP
nsFaviconService::ExpireAllFavicons() {
  NS_ENSURE_STATE(mDB);

  nsCOMPtr<mozIStorageAsyncStatement> removePagesStmt =
      mDB->GetAsyncStatement("DELETE FROM moz_pages_w_icons");
  NS_ENSURE_STATE(removePagesStmt);
  nsCOMPtr<mozIStorageAsyncStatement> removeIconsStmt =
      mDB->GetAsyncStatement("DELETE FROM moz_icons");
  NS_ENSURE_STATE(removeIconsStmt);
  nsCOMPtr<mozIStorageAsyncStatement> unlinkIconsStmt =
      mDB->GetAsyncStatement("DELETE FROM moz_icons_to_pages");
  NS_ENSURE_STATE(unlinkIconsStmt);

  nsTArray<RefPtr<mozIStorageBaseStatement>> stmts = {
      ToRefPtr(std::move(removePagesStmt)),
      ToRefPtr(std::move(removeIconsStmt)),
      ToRefPtr(std::move(unlinkIconsStmt))};
  nsCOMPtr<mozIStorageConnection> conn = mDB->MainConn();
  if (!conn) {
    return NS_ERROR_UNEXPECTED;
  }
  nsCOMPtr<mozIStoragePendingStatement> ps;
  RefPtr<ExpireFaviconsStatementCallbackNotifier> callback =
      new ExpireFaviconsStatementCallbackNotifier();
  return conn->ExecuteAsync(stmts, callback, getter_AddRefs(ps));
}

////////////////////////////////////////////////////////////////////////////////
//// nsIFaviconService

NS_IMETHODIMP
nsFaviconService::GetDefaultFavicon(nsIURI** _retval) {
  NS_ENSURE_ARG_POINTER(_retval);

  // not found, use default
  if (!mDefaultIcon) {
    nsresult rv = NS_NewURI(getter_AddRefs(mDefaultIcon),
                            nsLiteralCString(FAVICON_DEFAULT_URL));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIURI> uri = mDefaultIcon;
  uri.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
nsFaviconService::GetDefaultFaviconMimeType(nsACString& _retval) {
  _retval = nsLiteralCString(FAVICON_DEFAULT_MIMETYPE);
  return NS_OK;
}

void nsFaviconService::ClearImageCache(nsIURI* aImageURI) {
  MOZ_ASSERT(aImageURI, "Must pass a non-null URI");
  nsCOMPtr<imgICache> imgCache;
  nsresult rv =
      GetImgTools()->GetImgCacheForDocument(nullptr, getter_AddRefs(imgCache));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_SUCCEEDED(rv)) {
    Unused << imgCache->RemoveEntry(aImageURI, nullptr);
  }
}

NS_IMETHODIMP
nsFaviconService::SetFaviconForPage(nsIURI* aPageURI, nsIURI* aFaviconURI,
                                    nsIURI* aDataURL, PRTime aExpiration = 0,
                                    bool isRichIcon = false,
                                    JSContext* aContext = nullptr,
                                    dom::Promise** aPromise = nullptr) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aPageURI);
  NS_ENSURE_ARG(aFaviconURI);
  NS_ENSURE_ARG(aDataURL);

  MOZ_DIAGNOSTIC_ASSERT(aDataURL->SchemeIs("data"));

  ErrorResult result;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aContext), result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  nsresult rv = NS_OK;
  auto guard = MakeScopeExit([&]() {
    if (NS_SUCCEEDED(rv)) {
      promise->MaybeResolveWithUndefined();
      promise.forget(aPromise);
    }
  });

  if (!aDataURL->SchemeIs("data")) {
    return (rv = NS_ERROR_INVALID_ARG);
  }

  NS_ENSURE_ARG(canStoreIconForPage(aPageURI));

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return (rv = NS_OK);
  }

  PRTime now = PR_Now();
  if (aExpiration < now + MIN_FAVICON_EXPIRATION) {
    // Invalid input, just use the default.
    aExpiration = now + MAX_FAVICON_EXPIRATION;
  }

  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIProtocolHandler> protocolHandler;
  rv = ioService->GetProtocolHandler("data", getter_AddRefs(protocolHandler));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> loadingPrincipal =
      NullPrincipal::CreateWithoutOriginAttributes();
  if (MOZ_UNLIKELY(!(loadingPrincipal))) {
    return (rv = NS_ERROR_NULL_POINTER);
  }

  nsCOMPtr<nsILoadInfo> loadInfo = new mozilla::net::LoadInfo(
      loadingPrincipal,
      nullptr,  // aTriggeringPrincipal
      nullptr,  // aLoadingNode
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT |
          nsILoadInfo::SEC_ALLOW_CHROME | nsILoadInfo::SEC_DISALLOW_SCRIPT,
      nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON);

  nsCOMPtr<nsIChannel> channel;
  rv = protocolHandler->NewChannel(aDataURL, loadInfo, getter_AddRefs(channel));
  NS_ENSURE_SUCCESS(rv, rv);

  // Blocking stream is OK for data URIs.
  nsCOMPtr<nsIInputStream> stream;
  rv = channel->Open(getter_AddRefs(stream));
  NS_ENSURE_SUCCESS(rv, rv);

  uint64_t available64;
  rv = stream->Available(&available64);
  NS_ENSURE_SUCCESS(rv, rv);
  if (available64 == 0 || available64 > UINT32_MAX / sizeof(uint8_t)) {
    return (rv = NS_ERROR_FILE_TOO_BIG);
  }
  uint32_t available = (uint32_t)available64;

  // Read all the decoded data.
  nsTArray<uint8_t> buffer;
  buffer.SetLength(available);
  uint32_t numRead;
  rv = stream->Read(TO_CHARBUFFER(buffer.Elements()), available, &numRead);
  NS_ENSURE_SUCCESS(rv, rv);
  if (numRead != available) {
    return (rv = NS_ERROR_UNEXPECTED);
  }

  nsAutoCString mimeType;
  rv = channel->GetContentType(mimeType);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!imgLoader::SupportImageWithMimeType(
          mimeType, AcceptedMimeTypes::IMAGES_AND_DOCUMENTS)) {
    return (rv = NS_ERROR_UNEXPECTED);
  }

  // If URI is a Data URI, mime type returned by channel may be incorrect, since
  // it relies on type text in the URI. So we override the initial type returned
  // by channel to ensure that the correct type is associated with the payload.
  if (aDataURL->SchemeIs("data")) {
    nsAutoCString sniffedMimeType;
    uint32_t bufferLength = buffer.Length();
    // Note that this function can detect png, ico, jpeg, among other file
    // types, but not SVG.
    rv = imgLoader::GetMimeTypeFromContent((const char*)buffer.Elements(),
                                           bufferLength, sniffedMimeType);
    if (NS_SUCCEEDED(rv)) {
      mimeType = sniffedMimeType;
    } else {
      // When the MIME type is not available, fall back to checking for SVG in
      // the initial part of the buffer.
      const char* content = reinterpret_cast<const char*>(buffer.Elements());
      uint32_t length = std::min(bufferLength, 255u);
      nsDependentCSubstring substring(content, length);
      if (substring.Find("<svg") != -1) {
        mimeType.AssignLiteral("image/svg+xml");
      }
    }
  }

  // Favicon should be handled without userpass.
  nsCOMPtr<nsIURI> faviconURI = GetExposableURI(aFaviconURI);
  nsCOMPtr<nsIURI> pageURI = GetExposableURI(aPageURI);

  IconData icon;
  icon.expiration = aExpiration;
  icon.status = ICON_STATUS_CACHED;
  if (isRichIcon) {
    icon.flags |= ICONDATA_FLAGS_RICH;
  }
  rv = faviconURI->GetSpec(icon.spec);
  NS_ENSURE_SUCCESS(rv, rv);
  // URIs can arguably lack a host.
  (void)faviconURI->GetHost(icon.host);
  if (StringBeginsWith(icon.host, "www."_ns)) {
    icon.host.Cut(0, 4);
  }

  IconPayload payload;
  payload.mimeType = mimeType;
  payload.data.Assign(TO_CHARBUFFER(buffer.Elements()), buffer.Length());
  if (payload.mimeType.EqualsLiteral(SVG_MIME_TYPE)) {
    payload.width = UINT16_MAX;
  }
  icon.payloads.AppendElement(payload);

  rv = OptimizeIconSizes(icon);
  NS_ENSURE_SUCCESS(rv, rv);

  PageData page;
  rv = pageURI->GetSpec(page.spec);
  NS_ENSURE_SUCCESS(rv, rv);
  // URIs can arguably lack a host.
  (void)pageURI->GetHost(page.host);
  if (StringBeginsWith(page.host, "www."_ns)) {
    page.host.Cut(0, 4);
  }

  // A root icon is when the icon and page have the same host and the path
  // is just /favicon.ico. These icons are considered valid for the whole
  // origin and expired with the origin through a trigger.
  nsAutoCString path;
  if (NS_SUCCEEDED(faviconURI->GetPathQueryRef(path)) && !icon.host.IsEmpty() &&
      icon.host.Equals(page.host) && path.EqualsLiteral("/favicon.ico")) {
    icon.rootIcon = 1;
  }

  // If the page url points to an image, the icon's url will be the same.
  // TODO (Bug 403651): store a resample of the image.  For now avoid that
  // for database size and UX concerns.
  // Don't store favicons for error pages either.
  if (icon.spec.Equals(page.spec) ||
      icon.spec.EqualsLiteral(FAVICON_CERTERRORPAGE_URL) ||
      icon.spec.EqualsLiteral(FAVICON_ERRORPAGE_URL)) {
    return NS_OK;
  }

  RefPtr<AsyncSetIconForPage> event =
      new AsyncSetIconForPage(icon, page, promise);
  RefPtr<Database> DB = Database::GetDatabase();
  if (MOZ_UNLIKELY(!DB)) {
    return (rv = NS_ERROR_UNEXPECTED);
  }

  DB->DispatchToAsyncThread(event);

  guard.release();
  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsFaviconService::GetFaviconForPage(nsIURI* aPageURI, uint16_t aPreferredWidth,
                                    JSContext* aContext = nullptr,
                                    dom::Promise** _retval = nullptr) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aPageURI);

  ErrorResult errorResult;
  RefPtr<dom::Promise> promise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aContext), errorResult);
  if (NS_WARN_IF(errorResult.Failed())) {
    return errorResult.StealNSResult();
  }

  RefPtr<FaviconPromise> result =
      AsyncGetFaviconForPage(aPageURI, aPreferredWidth);
  result->Then(GetMainThreadSerialEventTarget(), __func__,
               [promise](const FaviconPromise::ResolveOrRejectValue& aValue) {
                 if (aValue.IsResolve()) {
                   nsCOMPtr<nsIFavicon> favicon = aValue.ResolveValue();
                   if (favicon) {
                     promise->MaybeResolve(favicon);
                   } else {
                     promise->MaybeResolve(JS::NullHandleValue);
                   }
                 } else {
                   promise->MaybeReject(aValue.RejectValue());
                 }
               });

  promise.forget(_retval);
  return NS_OK;
}

RefPtr<FaviconPromise> nsFaviconService::AsyncGetFaviconForPage(
    nsIURI* aPageURI, uint16_t aPreferredWidth) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPageURI);

  // Use the default value, may be UINT16_MAX if a default is not set.
  if (aPreferredWidth == 0) {
    aPreferredWidth = mDefaultIconURIPreferredSize;
  }

  nsCOMPtr<nsIURI> pageURI = GetExposableURI(aPageURI);

  RefPtr<FaviconPromise::Private> promise =
      new FaviconPromise::Private(__func__);

  RefPtr<AsyncGetFaviconForPageRunnable> runnable =
      new AsyncGetFaviconForPageRunnable(pageURI, aPreferredWidth, promise);
  RefPtr<Database> DB = Database::GetDatabase();
  if (MOZ_UNLIKELY(!DB)) {
    promise->Reject(NS_ERROR_UNEXPECTED, __func__);
  }
  DB->DispatchToAsyncThread(runnable);

  return promise;
}

NS_IMETHODIMP
nsFaviconService::CopyFavicons(nsIURI* aFromPageURI, nsIURI* aToPageURI,
                               uint32_t aFaviconLoadType,
                               nsIFaviconDataCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aFromPageURI);
  NS_ENSURE_ARG(aToPageURI);
  NS_ENSURE_TRUE(
      aFaviconLoadType >= nsIFaviconService::FAVICON_LOAD_PRIVATE &&
          aFaviconLoadType <= nsIFaviconService::FAVICON_LOAD_NON_PRIVATE,
      NS_ERROR_INVALID_ARG);

  nsCOMPtr<nsIURI> fromPageURI = GetExposableURI(aFromPageURI);
  nsCOMPtr<nsIURI> toPageURI = GetExposableURI(aToPageURI);

  PageData fromPage;
  nsresult rv = fromPageURI->GetSpec(fromPage.spec);
  NS_ENSURE_SUCCESS(rv, rv);
  PageData toPage;
  rv = toPageURI->GetSpec(toPage.spec);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_ARG(canStoreIconForPage(aToPageURI));

  bool canAddToHistory;
  nsNavHistory* navHistory = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(navHistory, NS_ERROR_OUT_OF_MEMORY);
  rv = navHistory->CanAddURI(toPageURI, &canAddToHistory);
  NS_ENSURE_SUCCESS(rv, rv);
  toPage.canAddToHistory =
      !!canAddToHistory &&
      aFaviconLoadType != nsIFaviconService::FAVICON_LOAD_PRIVATE;

  RefPtr<AsyncCopyFavicons> event =
      new AsyncCopyFavicons(fromPage, toPage, aCallback);

  // Get the target thread and start the work.
  // DB will be updated and observers notified when done.
  RefPtr<Database> DB = Database::GetDatabase();
  NS_ENSURE_STATE(DB);
  DB->DispatchToAsyncThread(event);

  return NS_OK;
}

nsresult nsFaviconService::GetFaviconLinkForIcon(nsIURI* aFaviconURI,
                                                 nsIURI** _retval) {
  NS_ENSURE_ARG(aFaviconURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsAutoCString spec;
  if (aFaviconURI) {
    nsCOMPtr<nsIURI> faviconURI = GetExposableURI(aFaviconURI);

    // List of protocols for which it doesn't make sense to generate a favicon
    // uri since they can be directly loaded from disk or memory.
    static constexpr nsLiteralCString sDirectRequestProtocols[] = {
        // clang-format off
        "about"_ns,
        "cached-favicon"_ns,
        "chrome"_ns,
        "data"_ns,
        "file"_ns,
        "moz-page-thumb"_ns,
        "page-icon"_ns,
        "resource"_ns,
        // clang-format on
    };
    nsAutoCString iconURIScheme;
    if (NS_SUCCEEDED(faviconURI->GetScheme(iconURIScheme)) &&
        std::find(std::begin(sDirectRequestProtocols),
                  std::end(sDirectRequestProtocols),
                  iconURIScheme) != std::end(sDirectRequestProtocols)) {
      // Just return the input URL.
      *_retval = do_AddRef(faviconURI).take();
      return NS_OK;
    }
    nsresult rv = faviconURI->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return GetFaviconLinkForIconString(spec, _retval);
}

// nsFaviconService::GetFaviconLinkForIconString
//
//    This computes a favicon URL with string input and using the cached
//    default one to minimize parsing.

nsresult nsFaviconService::GetFaviconLinkForIconString(const nsCString& aSpec,
                                                       nsIURI** aOutput) {
  if (aSpec.IsEmpty()) {
    return GetDefaultFavicon(aOutput);
  }

  if (StringBeginsWith(aSpec, "chrome:"_ns)) {
    // pass through for chrome URLs, since they can be referenced without
    // this service
    return NS_NewURI(aOutput, aSpec);
  }

  nsAutoCString annoUri;
  annoUri.AssignLiteral("cached-favicon:");
  annoUri += aSpec;
  return NS_NewURI(aOutput, annoUri);
}

/**
 * Checks the icon and evaluates if it needs to be optimized.
 *
 * @param aIcon
 *        The icon to be evaluated.
 */
nsresult nsFaviconService::OptimizeIconSizes(IconData& aIcon) {
  // TODO (bug 1346139): move optimization to the async thread.
  MOZ_ASSERT(NS_IsMainThread());
  // There should only be a single payload at this point, it may have to be
  // split though, if it's an ico file.
  MOZ_ASSERT(aIcon.payloads.Length() == 1);

  // Even if the page provides a large image for the favicon (eg, a highres
  // image or a multiresolution .ico file), don't try to store more data than
  // needed.
  IconPayload payload = aIcon.payloads[0];
  if (payload.mimeType.EqualsLiteral(SVG_MIME_TYPE)) {
    // Nothing to optimize, but check the payload size.
    if (payload.data.Length() >= nsIFaviconService::MAX_FAVICON_BUFFER_SIZE) {
      return NS_ERROR_FILE_TOO_BIG;
    }
    return NS_OK;
  }

  // Make space for the optimized payloads.
  aIcon.payloads.Clear();

  // decode image
  nsCOMPtr<imgIContainer> container;
  nsresult rv = GetImgTools()->DecodeImageFromBuffer(
      payload.data.get(), payload.data.Length(), payload.mimeType,
      getter_AddRefs(container));
  NS_ENSURE_SUCCESS(rv, rv);

  // For ICO files, we must evaluate each of the frames we care about.
  nsTArray<FrameData> framesInfo;
  rv = GetFramesInfoForContainer(container, framesInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  for (const auto& frameInfo : framesInfo) {
    IconPayload newPayload;
    newPayload.mimeType = nsLiteralCString(PNG_MIME_TYPE);
    newPayload.width = frameInfo.width;
    for (uint16_t size : gFaviconSizes) {
      // The icon could be smaller than 16, that is our minimum.
      // Icons smaller than 16px are kept as-is.
      if (frameInfo.width >= 16) {
        if (size > frameInfo.width) {
          continue;
        }
        newPayload.width = size;
      }

      // If the original payload is png, the size is the same and not animated,
      // rescale the image only if it's larger than the maximum allowed.
      bool animated;
      if (newPayload.mimeType.Equals(payload.mimeType) &&
          newPayload.width == frameInfo.width &&
          payload.data.Length() < nsIFaviconService::MAX_FAVICON_BUFFER_SIZE &&
          (NS_FAILED(container->GetAnimated(&animated)) || !animated)) {
        newPayload.data = payload.data;
        break;
      }

      // Otherwise, scale and recompress. Rescaling will also take care of
      // extracting a static image from an animated one.
      // Since EncodeScaledImage uses SYNC_DECODE, it will pick the best
      // frame.
      nsCOMPtr<nsIInputStream> iconStream;
      rv = GetImgTools()->EncodeScaledImage(container, newPayload.mimeType,
                                            newPayload.width, newPayload.width,
                                            u""_ns, getter_AddRefs(iconStream));
      NS_ENSURE_SUCCESS(rv, rv);
      // Read the stream into the new buffer.
      rv = NS_ConsumeStream(iconStream, UINT32_MAX, newPayload.data);
      NS_ENSURE_SUCCESS(rv, rv);

      // If the icon size is good, we are done, otherwise try the next size.
      if (newPayload.data.Length() <
          nsIFaviconService::MAX_FAVICON_BUFFER_SIZE) {
        break;
      }
    }

    MOZ_ASSERT(newPayload.data.Length() <
               nsIFaviconService::MAX_FAVICON_BUFFER_SIZE);
    if (newPayload.data.Length() < nsIFaviconService::MAX_FAVICON_BUFFER_SIZE) {
      aIcon.payloads.AppendElement(newPayload);
    }
  }

  return aIcon.payloads.IsEmpty() ? NS_ERROR_FILE_TOO_BIG : NS_OK;
}

nsresult nsFaviconService::GetFaviconDataAsync(
    const nsCString& aFaviconSpec, mozIStorageStatementCallback* aCallback) {
  MOZ_ASSERT(aCallback, "Doesn't make sense to call this without a callback");

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aFaviconSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  uri = GetExposableURI(uri);

  nsCOMPtr<mozIStorageAsyncStatement> stmt = mDB->GetAsyncStatement(
      "/*Do not warn (bug no: not worth adding an index */ "
      "SELECT data, width FROM moz_icons "
      "WHERE fixed_icon_url_hash = hash(fixup_url(:url)) AND icon_url = :url "
      "ORDER BY width DESC");
  NS_ENSURE_STATE(stmt);

  rv = URIBinder::Bind(stmt, "url"_ns, uri);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIStoragePendingStatement> pendingStatement;
  return stmt->ExecuteAsync(aCallback, getter_AddRefs(pendingStatement));
}

NS_IMETHODIMP
nsFaviconService::SetDefaultIconURIPreferredSize(uint16_t aDefaultSize) {
  mDefaultIconURIPreferredSize = aDefaultSize > 0 ? aDefaultSize : UINT16_MAX;
  return NS_OK;
}

NS_IMETHODIMP
nsFaviconService::PreferredSizeFromURI(nsIURI* aURI, uint16_t* _size) {
  NS_ENSURE_ARG(aURI);
  *_size = mDefaultIconURIPreferredSize;
  nsAutoCString ref;
  // Check for a ref first.
  if (NS_FAILED(aURI->GetRef(ref)) || ref.Length() == 0) return NS_OK;

  // Look for a "size=" fragment.
  int32_t start = ref.RFind("size=");
  if (start >= 0 && ref.Length() > static_cast<uint32_t>(start) + 5) {
    nsDependentCSubstring size;
    // This is safe regardless, since Rebind checks start is not over
    // Length().
    size.Rebind(ref, start + 5);
    // Check if the string contains any non-digit.
    auto begin = size.BeginReading(), end = size.EndReading();
    for (const auto* ch = begin; ch < end; ++ch) {
      if (*ch < '0' || *ch > '9') {
        // Not a digit.
        return NS_OK;
      }
    }
    // Convert the string to an integer value.
    nsresult rv;
    uint16_t val = PromiseFlatCString(size).ToInteger(&rv);
    if (NS_SUCCEEDED(rv)) {
      *_size = val;
    }
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// ExpireFaviconsStatementCallbackNotifier

ExpireFaviconsStatementCallbackNotifier::
    ExpireFaviconsStatementCallbackNotifier() = default;

NS_IMETHODIMP
ExpireFaviconsStatementCallbackNotifier::HandleCompletion(uint16_t aReason) {
  // We should dispatch only if expiration has been successful.
  if (aReason != mozIStorageStatementCallback::REASON_FINISHED) return NS_OK;

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    (void)observerService->NotifyObservers(
        nullptr, NS_PLACES_FAVICONS_EXPIRED_TOPIC_ID, nullptr);
  }

  return NS_OK;
}
