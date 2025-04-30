/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_places_PageIconProtocolHandler_h
#define mozilla_places_PageIconProtocolHandler_h

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/MozPromise.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/net/RemoteStreamGetter.h"
#include "mozilla/places/nsFaviconService.h"
#include "nsIProtocolHandler.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"

namespace mozilla::places {

struct FaviconMetadata;

using net::RemoteStreamPromise;

class PageIconProtocolHandler final : public nsIProtocolHandler,
                                      public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROTOCOLHANDLER

  static already_AddRefed<PageIconProtocolHandler> GetSingleton() {
    MOZ_ASSERT(NS_IsMainThread());
    if (MOZ_UNLIKELY(!sSingleton)) {
      sSingleton = new PageIconProtocolHandler();
      ClearOnShutdown(&sSingleton);
    }
    return do_AddRef(sSingleton);
  }

  /**
   * To be called in the parent process to obtain an input stream for the
   * given icon.
   *
   * @param aChildURI a page-icon URI sent from the child.
   * @param aLoadInfo the nsILoadInfo for the load attempt from the child.
   * @param aTerminateSender out param set to true when the params are invalid
   *        and indicate the child should be terminated. If |aChildURI| is
   *        not a page-icon URI, the child is in an invalid state and
   *        should be terminated. This outparam will be set synchronously.
   *
   * @return RemoteStreamPromise
   *        The RemoteStreamPromise will resolve with an RemoteStreamInfo on
   *        success, and reject with an nsresult on failure.
   */
  RefPtr<RemoteStreamPromise> NewStream(nsIURI* aChildURI,
                                        nsILoadInfo* aLoadInfo,
                                        bool* aTerminateSender);

 private:
  ~PageIconProtocolHandler() = default;

  /**
   * This replaces the provided channel with a channel that will proxy the load
   * to the parent process.
   *
   * @param aURI the page-icon: URI.
   * @param aLoadInfo the loadinfo for the request.
   * @param aRetVal in/out channel param referring to the channel that
   *        might need to be substituted with a remote channel.
   * @return NS_OK if the replacement channel was created successfully.
   *         Otherwise, returns an error.
   */
  Result<Ok, nsresult> SubstituteRemoteChannel(nsIURI* aURI,
                                               nsILoadInfo* aLoadInfo,
                                               nsIChannel** aRetVal);

  RefPtr<FaviconPromise> GetFaviconData(nsIURI* aPageIconURI);

  nsresult NewChannelInternal(nsIURI*, nsILoadInfo*, nsIChannel**);

  void GetStreams(nsIAsyncInputStream** inStream,
                  nsIAsyncOutputStream** outStream);

  // Gets a SimpleChannel that wraps the provided channel.
  static void NewSimpleChannel(nsIURI* aURI, nsILoadInfo* aLoadinfo,
                               mozilla::net::RemoteStreamGetter* aStreamGetter,
                               nsIChannel** aRetVal);
  static StaticRefPtr<PageIconProtocolHandler> sSingleton;
};

}  // namespace mozilla::places

#endif
