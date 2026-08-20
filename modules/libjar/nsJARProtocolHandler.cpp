/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ClearOnShutdown.h"
#include "nsJARProtocolHandler.h"
#include "nsComponentManagerUtils.h"
#include "nsCRT.h"
#include "nsJARURI.h"
#include "nsJARChannel.h"
#include "nsString.h"
#include "nsNetCID.h"
#include "nsIMIMEService.h"
#include "nsMimeTypes.h"
#include "nsThreadUtils.h"

static NS_DEFINE_CID(kZipReaderCacheCID, NS_ZIPREADERCACHE_CID);

#define NS_JAR_CACHE_SIZE 32

//-----------------------------------------------------------------------------

mozilla::StaticRefPtr<nsJARProtocolHandler> gJarHandler;

nsJARProtocolHandler::nsJARProtocolHandler() { MOZ_ASSERT(NS_IsMainThread()); }

nsJARProtocolHandler::~nsJARProtocolHandler() = default;

nsresult nsJARProtocolHandler::Init() {
  nsresult rv;

  mJARCache = do_CreateInstance(kZipReaderCacheCID, &rv);
  if (NS_FAILED(rv)) return rv;

  rv = mJARCache->Init(NS_JAR_CACHE_SIZE);
  return rv;
}

nsIMIMEService* nsJARProtocolHandler::MimeService() {
  if (!mMimeService) mMimeService = do_GetService("@mozilla.org/mime;1");

  return mMimeService.get();
}

already_AddRefed<nsIZipReaderCache> nsJARProtocolHandler::GetJarCache() {
  if (!gJarHandler) {
    return nullptr;
  }
  return do_AddRef(gJarHandler->JarCache());
}

already_AddRefed<nsIMIMEService> nsJARProtocolHandler::GetMimeService() {
  if (!gJarHandler) {
    return nullptr;
  }
  return do_AddRef(gJarHandler->MimeService());
}

NS_IMPL_ISUPPORTS(nsJARProtocolHandler, nsIProtocolHandler,
                  nsISupportsWeakReference)

already_AddRefed<nsJARProtocolHandler> nsJARProtocolHandler::GetSingleton() {
  if (!gJarHandler) {
    gJarHandler = new nsJARProtocolHandler();
    if (NS_SUCCEEDED(gJarHandler->Init())) {
      // Chrome JS module loading reads from omni.ja, so the handler has to
      // outlive mozJSModuleLoader::UnloadLoaders(), which runs after
      // XPCOMShutdownFinal.
      ClearOnShutdown(&gJarHandler,
                      mozilla::ShutdownPhase::CCPostLastCycleCollection);
    } else {
      gJarHandler = nullptr;
    }
  }
  return do_AddRef(gJarHandler);
}

////////////////////////////////////////////////////////////////////////////////
// nsIProtocolHandler methods:

NS_IMETHODIMP
nsJARProtocolHandler::GetScheme(nsACString& result) {
  result.AssignLiteral("jar");
  return NS_OK;
}

NS_IMETHODIMP
nsJARProtocolHandler::NewChannel(nsIURI* uri, nsILoadInfo* aLoadInfo,
                                 nsIChannel** result) {
  RefPtr<nsJARChannel> chan = new nsJARChannel();
  if (!chan) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsresult rv = chan->Init(uri);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // set the loadInfo on the new channel
  rv = chan->SetLoadInfo(aLoadInfo);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *result = chan.forget().take();
  return NS_OK;
}

NS_IMETHODIMP
nsJARProtocolHandler::AllowPort(int32_t port, const char* scheme,
                                bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
