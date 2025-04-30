/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// data implementation

#include "nsDataChannel.h"

#include "mozilla/Base64.h"
#include "mozilla/dom/MimeType.h"
#include "nsDataHandler.h"
#include "nsIInputStream.h"
#include "nsEscape.h"
#include "nsISupports.h"
#include "nsStringStream.h"
#include "nsIObserverService.h"
#include "mozilla/dom/ContentParent.h"
#include "../protocol/http/nsHttpHandler.h"

using namespace mozilla;
using namespace mozilla::net;

NS_IMPL_ISUPPORTS_INHERITED(nsDataChannel, nsBaseChannel, nsIDataChannel,
                            nsIIdentChannel)

/**
 * Helper for performing a fallible unescape.
 *
 * @param aStr The string to unescape.
 * @param aBuffer Buffer to unescape into if necessary.
 * @param rv Out: nsresult indicating success or failure of unescaping.
 * @return Reference to the string containing the unescaped data.
 */
const nsACString& Unescape(const nsACString& aStr, nsACString& aBuffer,
                           nsresult* rv) {
  MOZ_ASSERT(rv);

  bool appended = false;
  *rv = NS_UnescapeURL(aStr.Data(), aStr.Length(), /* aFlags = */ 0, aBuffer,
                       appended, mozilla::fallible);
  if (NS_FAILED(*rv) || !appended) {
    return aStr;
  }

  return aBuffer;
}

nsresult nsDataChannel::OpenContentStream(bool async, nsIInputStream** result,
                                          nsIChannel** channel) {
  NS_ENSURE_TRUE(URI(), NS_ERROR_NOT_INITIALIZED);

  nsresult rv;

  // In general, a `data:` URI is stored as a `nsSimpleURI`, which holds a
  // single mSpec string. This can be read most efficiently with `GetSpec`, as
  // the underlying string buffer will be shared.
  //
  // NOTE: In the case where the `data:` URI is parsed with `DefaultURI`, this
  // will be inefficient, as there is no way to share the string buffer with
  // MozURL.

  nsAutoCString spec;
  rv = URI()->GetSpec(spec);
  if (NS_FAILED(rv)) return rv;

  nsCString contentType, contentCharset;
  nsDependentCSubstring dataRange;
  RefPtr<CMimeType> fullMimeType;
  bool lBase64;
  rv = nsDataHandler::ParseURI(spec, contentType, &contentCharset, lBase64,
                               &dataRange, &fullMimeType);
  if (NS_FAILED(rv)) return rv;

  // This will avoid a copy if nothing needs to be unescaped.
  nsAutoCString unescapedBuffer;
  const nsACString& data = Unescape(dataRange, unescapedBuffer, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (lBase64 && &data == &unescapedBuffer) {
    // Don't allow spaces in base64-encoded content. This is only
    // relevant for escaped spaces; other spaces are stripped in
    // NewURI. We know there were no escaped spaces if the data buffer
    // wasn't used in |Unescape|.
    unescapedBuffer.StripWhitespace();
  }

  nsCOMPtr<nsIInputStream> bufInStream;
  uint32_t contentLen;
  if (lBase64) {
    nsAutoCString decodedData;
    rv = Base64Decode(data, decodedData);
    if (NS_FAILED(rv)) {
      // Returning this error code instead of what Base64Decode returns
      // (NS_ERROR_ILLEGAL_VALUE) will prevent rendering of redirect response
      // content by HTTP channels.  It's also more logical error to return.
      // Here we know the URL is actually corrupted.
      return NS_ERROR_MALFORMED_URI;
    }

    contentLen = decodedData.Length();
    rv = NS_NewCStringInputStream(getter_AddRefs(bufInStream), decodedData);
  } else {
    contentLen = data.Length();
    rv = NS_NewCStringInputStream(getter_AddRefs(bufInStream), data);
  }

  if (NS_FAILED(rv)) return rv;

  SetContentType(contentType);
  SetContentCharset(contentCharset);
  SetFullMimeType(std::move(fullMimeType));
  mContentLength = contentLen;

  // notify "data-channel-opened" observers
  MaybeSendDataChannelOpenNotification();

  bufInStream.forget(result);

  return NS_OK;
}

nsresult nsDataChannel::Init() {
  NS_ENSURE_STATE(mLoadInfo);

  RefPtr<nsHttpHandler> handler = nsHttpHandler::GetInstance();
  MOZ_ALWAYS_SUCCEEDS(handler->NewChannelId(mChannelId));

  return NS_OK;
}

nsresult nsDataChannel::MaybeSendDataChannelOpenNotification() {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (!obsService) {
    return NS_OK;
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = GetLoadInfo(getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    return rv;
  }

  bool isTopLevel;
  rv = loadInfo->GetIsTopLevelLoad(&isTopLevel);
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint64_t browsingContextID;
  rv = loadInfo->GetBrowsingContextID(&browsingContextID);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if ((browsingContextID != 0 && isTopLevel) ||
      !loadInfo->TriggeringPrincipal()->IsSystemPrincipal()) {
    obsService->NotifyObservers(static_cast<nsIIdentChannel*>(this),
                                "data-channel-opened", nullptr);
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsDataChannel::nsIIdentChannel

NS_IMETHODIMP
nsDataChannel::GetChannelId(uint64_t* aChannelId) {
  *aChannelId = mChannelId;
  return NS_OK;
}

NS_IMETHODIMP
nsDataChannel::SetChannelId(uint64_t aChannelId) {
  mChannelId = aChannelId;
  return NS_OK;
}
