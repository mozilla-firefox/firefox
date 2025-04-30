/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "nsIFaviconService.h"
#include "nsIChannelEventSink.h"
#include "nsIInterfaceRequestor.h"
#include "nsIStreamListener.h"
#include "mozIPlacesPendingOperation.h"
#include "nsThreadUtils.h"
#include "nsProxyRelease.h"
#include "imgLoader.h"

class nsIPrincipal;

#include "Database.h"
#include "mozilla/storage.h"

#define ICON_STATUS_UNKNOWN 0
#define ICON_STATUS_CHANGED 1 << 0
#define ICON_STATUS_SAVED 1 << 1
#define ICON_STATUS_ASSOCIATED 1 << 2
#define ICON_STATUS_CACHED 1 << 3

#define TO_CHARBUFFER(_buffer) \
  reinterpret_cast<char*>(const_cast<uint8_t*>(_buffer))
#define TO_INTBUFFER(_string) \
  reinterpret_cast<uint8_t*>(const_cast<char*>(_string.get()))

#define PNG_MIME_TYPE "image/png"
#define SVG_MIME_TYPE "image/svg+xml"

// Always ensure a minimum expiration time, so icons are not already expired
// on addition.
#define MIN_FAVICON_EXPIRATION ((PRTime)1 * 24 * 60 * 60 * PR_USEC_PER_SEC)
// The maximum time we will keep a favicon around.  We always ask the cache
// first and default to this value if we can't get a time, or the time we get
// is far in the future.
#define MAX_FAVICON_EXPIRATION ((PRTime)7 * 24 * 60 * 60 * PR_USEC_PER_SEC)

namespace mozilla {
namespace places {

/**
 * Represents one of the payloads (frames) of an icon entry.
 */
struct IconPayload {
  IconPayload() : id(0), width(0) {
    data.SetIsVoid(true);
    mimeType.SetIsVoid(true);
  }

  int64_t id;
  uint16_t width;
  nsCString data;
  nsCString mimeType;
};

/**
 * Represents an icon entry.
 */
struct IconData {
  IconData()
      : expiration(0), status(ICON_STATUS_UNKNOWN), rootIcon(0), flags(0) {}

  nsCString spec;
  nsCString host;
  PRTime expiration;
  uint16_t status;  // This is a bitset, see ICON_STATUS_* defines above.
  uint8_t rootIcon;
  CopyableTArray<IconPayload> payloads;
  uint16_t flags;  // This is a bitset, see ICONDATA_FLAGS_* defines
                   // in toolkit/components/places/nsIFaviconService.idl.
};

/**
 * Data cache for a page entry.
 */
struct PageData {
  PageData() : id(0), placeId(0), canAddToHistory(true) {
    guid.SetIsVoid(true);
  }

  int64_t id;       // This is the moz_pages_w_icons id.
  int64_t placeId;  // This is the moz_places page id.
  nsCString spec;
  nsCString host;
  nsCString bookmarkedSpec;
  bool canAddToHistory;  // False for disabled history and unsupported schemas.
  nsCString guid;
};

/**
 * Info for a frame.
 */
struct FrameData {
  FrameData(uint16_t aIndex, uint16_t aWidth) : index(aIndex), width(aWidth) {}

  uint16_t index;
  uint16_t width;
};

/**
 * Associates the icon to the required page, finally dispatches an event to the
 * main thread to notify the change to observers.
 */
class AsyncAssociateIconToPage final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aIcon
   *        Icon to be associated.
   * @param aPage
   *        Page to which associate the icon.
   * @param aCallback
   *        Function to be called when the associate process finishes.
   */
  AsyncAssociateIconToPage(
      const IconData& aIcon, const PageData& aPage,
      const nsMainThreadPtrHandle<nsIFaviconDataCallback>& aCallback);

 private:
  nsMainThreadPtrHandle<nsIFaviconDataCallback> mCallback;
  IconData mIcon;
  PageData mPage;
};

/**
 * Set favicon for the page, finally dispatches an event to the
 * main thread to notify the change to observers.
 */
class AsyncSetIconForPage final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aIcon
   *        Icon to be associated.
   * @param aPage
   *        Page to which associate the icon.
   * @param aPromise
   *        Promise that returns the result.
   */
  AsyncSetIconForPage(const IconData& aIcon, const PageData& aPage,
                      dom::Promise* aPromise);

 private:
  nsMainThreadPtrHandle<dom::Promise> mPromise;
  IconData mIcon;
  PageData mPage;
};

using FaviconPromise =
    mozilla::MozPromise<nsCOMPtr<nsIFavicon>, nsresult, true>;

/**
 * Asynchronously tries to get the URL and data of a page's favicon, then
 * resolve given promise with the result.
 */
class AsyncGetFaviconForPageRunnable final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aPageURI
   *        URI of the page whose favicon's URL we're fetching
   * @param aPreferredWidth
   *        The preferred size of the icon.  We will try to return an icon close
   *        to this size.
   * @param aPromise
   *        Promise that returns the result.
   */
  AsyncGetFaviconForPageRunnable(const nsCOMPtr<nsIURI>& aPageURI,
                                 uint16_t aPreferredWidth,
                                 FaviconPromise::Private* aPromise);

 private:
  nsCOMPtr<nsIURI> mPageURI;
  uint16_t mPreferredWidth;
  nsMainThreadPtrHandle<FaviconPromise::Private> mPromise;
};

/**
 * Notifies the icon change to favicon observers.
 */
class NotifyIconObservers final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor for nsIFaviconDataCallback.
   *
   * @param aIcon
   *        Icon information. Can be empty if no icon is associated to the page.
   * @param aPage
   *        Page to which the icon information applies.
   * @param aCallback
   *        Function to be notified in all cases.
   */
  NotifyIconObservers(
      const IconData& aIcon, const PageData& aPage,
      const nsMainThreadPtrHandle<nsIFaviconDataCallback>& aCallback);

 private:
  nsMainThreadPtrHandle<nsIFaviconDataCallback> mCallback;
  IconData mIcon;
  PageData mPage;
};

/**
 * Copies Favicons from one page to another one.
 */
class AsyncCopyFavicons final : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE

  /**
   * Constructor.
   *
   * @param aFromPage
   *        The originating page.
   * @param aToPage
   *        The destination page.
   * @param aFaviconLoadPrivate
   *        Whether this favicon load is in private browsing.
   * @param aCallback
   *        An optional callback to invoke when done.
   */
  AsyncCopyFavicons(PageData& aFromPage, PageData& aToPage,
                    nsIFaviconDataCallback* aCallback);

 private:
  PageData mFromPage;
  PageData mToPage;
  nsMainThreadPtrHandle<nsIFaviconDataCallback> mCallback;
};

}  // namespace places
}  // namespace mozilla
