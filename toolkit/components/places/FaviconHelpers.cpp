/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FaviconHelpers.h"

#include "nsICacheEntry.h"
#include "nsICachingChannel.h"
#include "nsIClassOfService.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIHttpChannel.h"
#include "nsIPrincipal.h"

#include "nsComponentManagerUtils.h"
#include "nsNavHistory.h"
#include "nsFaviconService.h"

#include "mozilla/dom/PlacesFavicon.h"
#include "mozilla/dom/PlacesObservers.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/Base64.h"
#include "mozilla/storage.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsIPrivateBrowsingChannel.h"
#include "nsISupportsPriority.h"
#include <algorithm>
#include <deque>
#include "mozilla/gfx/2D.h"
#include "imgIContainer.h"
#include "ImageOps.h"
#include "imgIEncoder.h"

using namespace mozilla;
using namespace mozilla::places;
using namespace mozilla::storage;

namespace mozilla {
namespace places {

namespace {

// Number of most recent visits to a page used to find a bookmarked page
// redirecting to it. The relevant redirect chain is the one of the load setting
// the icon, so older visits are not worth reading.
// TODO: Bug 1999939 should allow to look up redirects directly, removing this
// limit.
constexpr int32_t MAX_VISITS_TO_FIND_BOOKMARKED_REDIRECT_SOURCE = 10;

/**
 * Fetches information about a page from the database.
 *
 * @param aDB
 *        Database connection to history tables.
 * @param _page
 *        Page that should be fetched.
 */
nsresult FetchPageInfo(const RefPtr<Database>& aDB, PageData& _page) {
  MOZ_ASSERT(_page.spec.Length(), "Must have a non-empty spec!");
  MOZ_ASSERT(!NS_IsMainThread());

  // The subquery finds the bookmarked uri we want to set the icon for,
  // walking up redirects.
  nsCString query = nsPrintfCString(
      "SELECT h.id, pi.id, h.guid, ( "
      "WITH RECURSIVE "
      "destinations(visit_type, from_visit, place_id, rev_host, bm) AS ( "
      "SELECT v.visit_type, v.from_visit, p.id, p.rev_host, b.id "
      "FROM moz_places p  "
      "LEFT JOIN ( "
      "  SELECT visit_type, from_visit, place_id FROM moz_historyvisits "
      "  WHERE place_id = h.id ORDER BY visit_date DESC LIMIT %d "
      ") v ON v.place_id = p.id  "
      "LEFT JOIN moz_bookmarks b ON b.fk = p.id "
      "WHERE p.id = h.id "
      "UNION "
      "SELECT src.visit_type, src.from_visit, src.place_id, p.rev_host, b.id "
      "FROM moz_places p "
      "JOIN moz_historyvisits src ON src.place_id = p.id "
      "JOIN destinations dest ON dest.from_visit = src.id AND dest.visit_type "
      "IN (%d, %d) "
      "LEFT JOIN moz_bookmarks b ON b.fk = src.place_id "
      "WHERE instr(p.rev_host, dest.rev_host) = 1 "
      "OR instr(dest.rev_host, p.rev_host) = 1 "
      ") "
      "SELECT url "
      "FROM moz_places p "
      "JOIN destinations r ON r.place_id = p.id "
      "WHERE bm NOTNULL "
      "LIMIT 1 "
      "), fixup_url(get_unreversed_host(h.rev_host)) AS host "
      "FROM moz_places h "
      "LEFT JOIN moz_pages_w_icons pi ON page_url_hash = hash(:page_url) AND "
      "page_url = :page_url "
      "WHERE h.url_hash = hash(:page_url) AND h.url = :page_url",
      MAX_VISITS_TO_FIND_BOOKMARKED_REDIRECT_SOURCE,
      nsINavHistoryService::TRANSITION_REDIRECT_PERMANENT,
      nsINavHistoryService::TRANSITION_REDIRECT_TEMPORARY);

  nsCOMPtr<mozIStorageStatement> stmt = aDB->GetStatement(query);
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = URIBinder::Bind(stmt, "page_url"_ns, _page.spec);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasResult) {
    // The page does not exist.
    return NS_ERROR_NOT_AVAILABLE;
  }

  rv = stmt->GetInt64(0, &_page.placeId);
  NS_ENSURE_SUCCESS(rv, rv);
  // May be null, and in such a case this will be 0.
  _page.id = stmt->AsInt64(1);
  rv = stmt->GetUTF8String(2, _page.guid);
  NS_ENSURE_SUCCESS(rv, rv);
  // Bookmarked url can be nullptr.
  bool isNull;
  rv = stmt->GetIsNull(3, &isNull);
  NS_ENSURE_SUCCESS(rv, rv);
  // The page could not be bookmarked.
  if (!isNull) {
    rv = stmt->GetUTF8String(3, _page.bookmarkedSpec);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (_page.host.IsEmpty()) {
    rv = stmt->GetUTF8String(4, _page.host);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!_page.canAddToHistory) {
    // Either history is disabled or the scheme is not supported.  In such a
    // case we want to update the icon only if the page is bookmarked.

    if (_page.bookmarkedSpec.IsEmpty()) {
      // The page is not bookmarked.  Since updating the icon with a disabled
      // history would be a privacy leak, bail out as if the page did not exist.
      return NS_ERROR_NOT_AVAILABLE;
    }

    // The page, or a redirect to it, is bookmarked.  If the bookmarked spec
    // is different from the requested one, use it.
    if (!_page.bookmarkedSpec.Equals(_page.spec)) {
      _page.spec = _page.bookmarkedSpec;
      rv = FetchPageInfo(aDB, _page);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

/**
 * Stores information about an icon in the database.
 *
 * @param aDB
 *        Database connection to history tables.
 * @param aIcon
 *        Icon that should be stored.
 * @param aMustReplace
 *        If set to true, the function will bail out with NS_ERROR_NOT_AVAILABLE
 *        if it can't find a previous stored icon to replace.
 * @note Should be wrapped in a transaction.
 */
nsresult SetIconInfo(const RefPtr<Database>& aDB, IconData& aIcon,
                     bool aMustReplace = false) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aIcon.payloads.Length() > 0);
  MOZ_ASSERT(!aIcon.spec.IsEmpty());
  MOZ_ASSERT(aIcon.expiration > 0);

  // There are multiple cases possible at this point:
  //   1. We must insert some payloads and no payloads exist in the table. This
  //      would be a straight INSERT.
  //   2. The table contains the same number of payloads we are inserting. This
  //      would be a straight UPDATE.
  //   3. The table contains more payloads than we are inserting. This would be
  //      an UPDATE and a DELETE.
  //   4. The table contains less payloads than we are inserting. This would be
  //      an UPDATE and an INSERT.
  // We can't just remove all the old entries and insert the new ones, cause
  // we'd lose the referential integrity with pages.  For the same reason we
  // cannot use INSERT OR REPLACE, since it's implemented as DELETE AND INSERT.
  // Thus, we follow this strategy:
  //   * SELECT all existing icon ids
  //   * For each payload, either UPDATE OR INSERT reusing icon ids.
  //   * If any previous icon ids is leftover, DELETE it.

  nsCOMPtr<mozIStorageStatement> selectStmt = aDB->GetStatement(
      "SELECT id FROM moz_icons "
      "WHERE fixed_icon_url_hash = hash(fixup_url(:url)) "
      "AND icon_url = :url ");
  NS_ENSURE_STATE(selectStmt);
  mozStorageStatementScoper scoper(selectStmt);
  nsresult rv = URIBinder::Bind(selectStmt, "url"_ns, aIcon.spec);
  NS_ENSURE_SUCCESS(rv, rv);
  std::deque<int64_t> ids;
  bool hasResult = false;
  while (NS_SUCCEEDED(selectStmt->ExecuteStep(&hasResult)) && hasResult) {
    int64_t id = selectStmt->AsInt64(0);
    MOZ_ASSERT(id > 0);
    ids.push_back(id);
  }
  if (aMustReplace && ids.empty()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<mozIStorageStatement> insertStmt = aDB->GetStatement(
      "INSERT INTO moz_icons "
      "(icon_url, fixed_icon_url_hash, width, root, expire_ms, data, flags) "
      "VALUES (:url, hash(fixup_url(:url)), :width, :root, :expire, :data, "
      ":flags) ");
  NS_ENSURE_STATE(insertStmt);
  // ReplaceFaviconData may replace data for an already existing icon, and in
  // that case it won't have the page uri at hand, thus it can't tell if the
  // icon is a root icon or not. For that reason, never overwrite a root = 1.
  nsCOMPtr<mozIStorageStatement> updateStmt = aDB->GetStatement(
      "UPDATE moz_icons SET width = :width, "
      "expire_ms = :expire, "
      "data = :data, "
      "root = (root  OR :root), "
      "flags = :flags "
      "WHERE id = :id ");
  NS_ENSURE_STATE(updateStmt);

  for (auto& payload : aIcon.payloads) {
    // Sanity checks.
    MOZ_ASSERT(payload.mimeType.EqualsLiteral(PNG_MIME_TYPE) ||
                   payload.mimeType.EqualsLiteral(SVG_MIME_TYPE),
               "Only png and svg payloads are supported");
    MOZ_ASSERT(!payload.mimeType.EqualsLiteral(SVG_MIME_TYPE) ||
                   payload.width == UINT16_MAX,
               "SVG payloads should have max width");
    MOZ_ASSERT(payload.width > 0, "Payload should have a width");
#ifdef DEBUG
    // Done to ensure we fetch the id. See the MOZ_ASSERT below.
    payload.id = 0;
#endif
    if (!ids.empty()) {
      // Pop the first existing id for reuse.
      int64_t id = ids.front();
      ids.pop_front();
      mozStorageStatementScoper scoper(updateStmt);
      rv = updateStmt->BindInt64ByName("id"_ns, id);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = updateStmt->BindInt32ByName("width"_ns, payload.width);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = updateStmt->BindInt64ByName("expire"_ns, aIcon.expiration / 1000);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = updateStmt->BindInt32ByName("root"_ns, aIcon.rootIcon);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = updateStmt->BindBlobByName("data"_ns, TO_INTBUFFER(payload.data),
                                      payload.data.Length());
      NS_ENSURE_SUCCESS(rv, rv);
      rv = updateStmt->BindInt32ByName("flags"_ns, aIcon.flags);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = updateStmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
      // Set the new payload id.
      payload.id = id;
    } else {
      // Insert a new entry.
      mozStorageStatementScoper scoper(insertStmt);
      rv = URIBinder::Bind(insertStmt, "url"_ns, aIcon.spec);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = insertStmt->BindInt32ByName("width"_ns, payload.width);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = insertStmt->BindInt32ByName("root"_ns, aIcon.rootIcon);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = insertStmt->BindInt64ByName("expire"_ns, aIcon.expiration / 1000);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = insertStmt->BindBlobByName("data"_ns, TO_INTBUFFER(payload.data),
                                      payload.data.Length());
      NS_ENSURE_SUCCESS(rv, rv);
      rv = insertStmt->BindInt32ByName("flags"_ns, aIcon.flags);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = insertStmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
      // Set the new payload id.
      payload.id = nsFaviconService::sLastInsertedIconId;
    }
    MOZ_ASSERT(payload.id > 0, "Payload should have an id");
  }

  if (!ids.empty()) {
    // Remove any old leftover payload.
    nsAutoCString sql("DELETE FROM moz_icons WHERE id IN (");
    for (int64_t id : ids) {
      sql.AppendInt(id);
      sql.AppendLiteral(",");
    }
    sql.AppendLiteral(" 0)");  // Non-existing id to match the trailing comma.
    nsCOMPtr<mozIStorageStatement> stmt = aDB->GetStatement(sql);
    NS_ENSURE_STATE(stmt);
    mozStorageStatementScoper scoper(stmt);
    rv = stmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult FetchMostFrecentSubPageIcon(const UniquePtr<ConnectionAdapter>& aConn,
                                     const nsACString& aPageRoot,
                                     const nsACString& aPageHost,
                                     IconData& aIconData) {
  // width is a uint16, so packing it under frecency makes the single max()
  // rank rows exactly like ORDER BY frecency DESC, width DESC, without a sort.
  // The HAVING avoids the all-NULL row a bare aggregate returns on no match.
  nsCOMPtr<mozIStorageStatement> stmt = aConn->GetStatement(
      "SELECT i.icon_url, i.id, i.expire_ms, i.data, i.width, i.root, "
      "max(p.frecency * 65536 + i.width) "
      "FROM moz_pages_w_icons pwi "
      "JOIN moz_icons_to_pages itp ON itp.page_id = pwi.id "
      "JOIN moz_icons i ON i.id = itp.icon_id "
      "JOIN moz_places p ON p.url_hash = pwi.page_url_hash "
      "WHERE p.rev_host = get_unreversed_host(:pageHost || '.') || '.' "
      "AND p.url BETWEEN :pageRoot || '/' AND :pageRoot || '/'  || X'FFFF' "
      "HAVING count(*) > 0"_ns);
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoperFallback(stmt);

  nsresult rv = stmt->BindUTF8StringByName("pageRoot"_ns, aPageRoot);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindUTF8StringByName("pageHost"_ns, aPageHost);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;

  if (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    rv = stmt->GetUTF8String(0, aIconData.spec);
    NS_ENSURE_SUCCESS(rv, rv);

    // Expiration can be nullptr.
    bool isNull;
    rv = stmt->GetIsNull(2, &isNull);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    if (!isNull) {
      int64_t expire_ms;
      rv = stmt->GetInt64(2, &expire_ms);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      aIconData.expiration = expire_ms * 1000;
    }

    int32_t rootIcon;
    rv = stmt->GetInt32(5, &rootIcon);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    aIconData.rootIcon = rootIcon;

    IconPayload payload;
    rv = stmt->GetInt64(1, &payload.id);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    rv = stmt->GetBlobAsUTF8String(3, payload.data);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    int32_t width;
    rv = stmt->GetInt32(4, &width);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    payload.width = width;
    if (payload.width == UINT16_MAX) {
      payload.mimeType.AssignLiteral(SVG_MIME_TYPE);
    } else {
      payload.mimeType.AssignLiteral(PNG_MIME_TYPE);
    }

    aIconData.payloads.AppendElement(payload);
  }

  return NS_OK;
}

/**
 * Fetches icon information for the given page from the database.
 *
 * @param aDBConn
 *        Database connection to history tables.
 * @param aPageURI
 *        The page url that the icon is associated.
 * @param aPreferredWidth
 *        The preferred size to fetch.
 * @param _icon
 *        Icon that should be fetched.
 */
nsresult FetchIconInfo(const UniquePtr<ConnectionAdapter>& aConn,
                       const nsCOMPtr<nsIURI>& aPageURI,
                       uint16_t aPreferredWidth, IconData& _icon) {
  if (_icon.status & ICON_STATUS_CACHED) {
    // The icon data has already been set by ReplaceFaviconData.
    MOZ_ASSERT(_icon.spec.Length(), "Must have a non-empty spec!");
    return NS_OK;
  }

  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aPageURI, "URI must exist.");

  nsAutoCString pageSpec;
  nsresult rv = aPageURI->GetSpec(pageSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_ASSERT(!pageSpec.IsEmpty(), "Page spec must not be empty.");

  nsAutoCString pageHostAndPort;
  // It's expected that some URIs may not have a host/port.
  (void)aPageURI->GetHostPort(pageHostAndPort);

  const uint16_t THRESHOLD_WIDTH = 64;

  // This selects both associated and root domain icons, ordered by width,
  // where an associated icon has priority over a root domain icon, and the
  // most recently added icon has priority over others of the same width.
  // If the preferred width is less than or equal to THRESHOLD_WIDTH, non-rich
  // icons are prioritized over rich icons by ordering first by `isRich ASC`,
  // then by width. If the preferred width is greater than THRESHOLD_WIDTH, the
  // sorting prioritizes width, with no preference for rich or non-rich icons.
  // Regardless, note that while this way we are far more efficient, we lost
  // associations with root domain icons, so it's possible we'll return one for
  // a specific size when an associated icon for that size doesn't exist.

  nsCString query = nsPrintfCString(
      "/* do not warn (bug no: not worth having a compound index) */ "
      "SELECT i.id, i.expire_ms, i.data, width, icon_url, root, "
      "  (flags & %d) AS isRich "
      "FROM moz_icons i "
      "JOIN moz_icons_to_pages ON i.id = icon_id "
      "JOIN moz_pages_w_icons p ON p.id = page_id "
      "WHERE page_url_hash = hash(:url) AND page_url = :url "
      "OR (:hash_idx AND page_url_hash = hash(substr(:url, 0, :hash_idx)) "
      "AND page_url = substr(:url, 0, :hash_idx)) "
      "UNION ALL "
      "SELECT id, expire_ms, data, width, icon_url, root, "
      "  (flags & %d) AS isRich "
      "FROM moz_icons i "
      "WHERE fixed_icon_url_hash = "
      "  hash(fixup_url(:hostAndPort) || '/favicon.ico') "
      "ORDER BY %s width DESC, root ASC, id DESC",
      nsIFaviconService::ICONDATA_FLAGS_RICH,
      nsIFaviconService::ICONDATA_FLAGS_RICH,
      // Prefer non-rich icons for small sizes (<= 64px).
      aPreferredWidth <= THRESHOLD_WIDTH ? "isRich ASC, " : "");

  nsCOMPtr<mozIStorageStatement> stmt = aConn->GetStatement(query);

  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  rv = URIBinder::Bind(stmt, "url"_ns, pageSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindUTF8StringByName("hostAndPort"_ns, pageHostAndPort);
  NS_ENSURE_SUCCESS(rv, rv);
  int32_t hashIdx = PromiseFlatCString(pageSpec).RFind("#");
  rv = stmt->BindInt32ByName("hash_idx"_ns, hashIdx + 1);
  NS_ENSURE_SUCCESS(rv, rv);

  // Return the biggest icon close to the preferred width. It may be bigger
  // or smaller if the preferred width isn't found.
  // If the size difference between the bigger icon and preferred width is more
  // than 4 times greater than the difference between the preferred width and
  // the smaller icon, we prefer the smaller icon.
  // Non-rich icons are prioritized over rich ones for preferred widths <=
  // THRESHOLD_WIDTH. After the initial selection, we check if a suitable SVG
  // icon exists that could override the initial selection.
  // Finally, if the main selection chose a root domain icon but an associated
  // icon exists with an acceptable rescale factor, we prefer the associated
  // icon. Any downscale is acceptable; the risk of quality degradation from
  // downscaling a large image is limited because we already skip rich icons for
  // small sizes. For upscales we cap at 4x.

  bool hasResult;

  struct IconInfo {
    IconInfo(int64_t aIconId, const nsCString& aData, PRTime aExpiration,
             bool aIsRich, bool aRootIcon, uint16_t aWidth,
             const nsCString& aSvgSpec)
        : id(aIconId),
          data(aData),
          expiration(aExpiration),
          isRich(aIsRich),
          rootIcon(aRootIcon),
          width(aWidth),
          spec(aSvgSpec) {}

    int64_t id = -1;
    nsCString data;
    PRTime expiration = 0;
    bool isRich = false;
    bool rootIcon = false;
    uint16_t width = 0;
    nsAutoCString spec;
  };

  UniquePtr<IconInfo> svgIcon;
  UniquePtr<IconInfo> selectedIcon;
  UniquePtr<IconInfo> bestAssociatedIcon;
  uint16_t lastIconWidth = 0;

  bool preferNonRichIcons = aPreferredWidth <= THRESHOLD_WIDTH;

  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    int32_t width = stmt->AsInt32(3);
    if (lastIconWidth == width) {
      // If we already found an icon for this width, we always prefer the first
      // icon found, because it's a non-root icon, per the root ASC ordering.
      continue;
    }

    int64_t iconId = stmt->AsInt64(0);
    bool rootIcon = !!stmt->AsInt32(5);
    bool isRich = !!stmt->AsInt32(6);

    // Expiration can be NULL.
    PRTime expiration = 0;
    bool isNull;
    rv = stmt->GetIsNull(1, &isNull);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!isNull) {
      int64_t expire_ms = stmt->AsInt64(1);
      expiration = expire_ms * 1000;
    }

    nsCString data;
    rv = stmt->GetBlobAsUTF8String(2, data);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString iconURL;
    rv = stmt->GetUTF8String(4, iconURL);
    NS_ENSURE_SUCCESS(rv, rv);

    // If current icon is an SVG, and we haven't yet stored an SVG,
    // store the SVG when the preferred width is below
    // threshold, otherwise simply store the first SVG found regardless of
    // richness.
    int32_t isSVG = (width == UINT16_MAX);
    if (isSVG && !svgIcon) {
      if ((preferNonRichIcons && !isRich) || !preferNonRichIcons) {
        svgIcon = std::make_unique<IconInfo>(iconId, data, expiration, isRich,
                                             rootIcon, width, iconURL);
      }
    }

    if (preferNonRichIcons && isRich && selectedIcon && !selectedIcon->isRich) {
      // If we already found a non-rich icon, we prefer it to rich icons
      // for small sizes.
      break;
    }

    // Track the best-fitting non-SVG associated icon. Icons arrive in width
    // DESC order, so we update while width >= aPreferredWidth (giving the
    // smallest icon still at or above preferred = closest from above), and
    // set once on first encounter if all are below preferred (giving the
    // largest below = closest from below).
    if (!isSVG && !rootIcon &&
        (!bestAssociatedIcon || width >= aPreferredWidth)) {
      bestAssociatedIcon = std::make_unique<IconInfo>(
          iconId, data, expiration, isRich, false, width, iconURL);
    }

    if (!_icon.spec.IsEmpty() && width < aPreferredWidth) {
      // We found the best match, or we already found a match so we don't need
      // to fallback to the root domain icon.

      // If the difference between the preferred size and the previously found
      // larger icon is more than 4 times the difference between the preferred
      // size and the smaller icon, choose the smaller icon.
      if (aPreferredWidth - width < abs(lastIconWidth - aPreferredWidth) / 4) {
        selectedIcon = std::make_unique<IconInfo>(
            iconId, data, expiration, isRich, rootIcon, width, EmptyCString());
        rv = stmt->GetUTF8String(4, _icon.spec);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      break;
    }

    lastIconWidth = width;
    selectedIcon = std::make_unique<IconInfo>(iconId, data, expiration, isRich,
                                              rootIcon, width, EmptyCString());
    rv = stmt->GetUTF8String(4, _icon.spec);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (selectedIcon && selectedIcon->rootIcon && bestAssociatedIcon &&
      static_cast<uint32_t>(bestAssociatedIcon->width) * 4 >= aPreferredWidth) {
    _icon.spec = bestAssociatedIcon->spec;
    selectedIcon = std::move(bestAssociatedIcon);
  }

  // Check to see if we should overwrite the original icon selection with an
  // SVG. We prefer the SVG if the selected icon's width differs from the
  // preferred width. We also prefer the SVG if the selected icon is rich and
  // the preferred width is below threshold. Note that since we only store
  // non-rich SVGs for below-threshold requests, rich SVGs are not considered.
  // For above-threshold requests, any SVG would overwrite the selected icon if
  // its width differs from the requested size.
  if (svgIcon && selectedIcon) {
    if ((selectedIcon->width != aPreferredWidth) ||
        (preferNonRichIcons && selectedIcon->isRich)) {
      _icon.spec = svgIcon->spec;
      selectedIcon = std::move(svgIcon);
    }
  }

  if (selectedIcon) {
    _icon.expiration = selectedIcon->expiration;
    _icon.rootIcon = selectedIcon->rootIcon;

    IconPayload payload;
    payload.id = selectedIcon->id;
    payload.data = selectedIcon->data;
    payload.width = selectedIcon->width;
    if (payload.width == UINT16_MAX) {
      payload.mimeType.AssignLiteral(SVG_MIME_TYPE);
    } else {
      payload.mimeType.AssignLiteral(PNG_MIME_TYPE);
    }
    _icon.payloads.AppendElement(payload);

    return NS_OK;
  }

  // If we reached this stage without finding an icon, we can check if the
  // requested page spec is a host (no path) and if it contains any subpages
  // that have an icon associated with them. If they do, we fetch the icon
  // of the most frecent subpage.
  if (_icon.spec.IsEmpty()) {
    nsAutoCString pageFilePath;
    rv = aPageURI->GetFilePath(pageFilePath);
    NS_ENSURE_SUCCESS(rv, rv);
    if (pageFilePath == "/"_ns) {
      nsAutoCString pageHost;
      (void)aPageURI->GetHost(pageHost);

      nsAutoCString pagePrePath;
      (void)aPageURI->GetPrePath(pagePrePath);

      if (!pageHost.IsEmpty() && !pagePrePath.IsEmpty()) {
        rv = FetchMostFrecentSubPageIcon(aConn, pagePrePath, pageHost, _icon);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  return NS_OK;
}

class Favicon final : public nsIFavicon {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  Favicon(const nsACString& aURISpec, const nsACString& aRawData,
          const nsACString& aMimeType, const uint16_t& aWidth)
      : mURISpec(aURISpec),
        mRawData(aRawData),
        mMimeType(aMimeType),
        mWidth(aWidth) {}

  NS_IMETHODIMP GetUri(nsIURI** aURI) override {
    NS_ENSURE_ARG_POINTER(aURI);
    return NS_NewURI(aURI, mURISpec);
  }

  NS_IMETHODIMP GetDataURI(nsIURI** aDataURI) override {
    NS_ENSURE_ARG_POINTER(aDataURI);

    nsAutoCString spec;
    spec.AssignLiteral("data:");
    spec.Append(mMimeType);
    spec.AppendLiteral(";base64,");
    nsresult rv = Base64EncodeAppend(mRawData, spec);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_NewURI(aDataURI, spec);
  }

  NS_IMETHOD GetRawData(nsTArray<uint8_t>& aRawData) override {
    (void)aRawData.ReplaceElementsAt(0, aRawData.Length(),
                                     TO_INTBUFFER(mRawData), mRawData.Length(),
                                     fallible);
    return NS_OK;
  }

  NS_IMETHOD GetMimeType(nsACString& aMimeType) override {
    aMimeType.Assign(mMimeType);
    return NS_OK;
  }

  NS_IMETHOD GetWidth(uint16_t* aWidth) override {
    *aWidth = mWidth;
    return NS_OK;
  }

 private:
  ~Favicon() = default;

  nsCString mURISpec;
  nsCString mRawData;
  nsCString mMimeType;
  uint16_t mWidth;
};
NS_IMPL_ISUPPORTS(Favicon, nsIFavicon)

/**
 * Removes the given icons if they're orphans, see moz_orphan_icons.
 *
 * @param aDB
 *        Database connection.
 * @param aIconIds
 *        Ids of the icons that may have become orphans.
 */
nsresult RemoveOrphanIcons(const RefPtr<Database>& aDB,
                           const nsTArray<int64_t>& aIconIds) {
  if (aIconIds.IsEmpty()) {
    return NS_OK;
  }
  nsCOMPtr<mozIStorageStatement> stmt = aDB->GetStatement(
      "DELETE FROM moz_icons WHERE id IN ( "
      "  SELECT id FROM moz_orphan_icons WHERE id IN carray(:icon_ids) "
      ")");
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);
  nsresult rv = stmt->BindArrayOfIntegersByName("icon_ids"_ns, aIconIds);
  NS_ENSURE_SUCCESS(rv, rv);
  return stmt->Execute();
}

/**
 * Executes a statement returning icon ids, and appends them to aIconIds.
 */
nsresult CollectIconIds(mozIStorageStatement* aStmt,
                        nsTArray<int64_t>& aIconIds) {
  bool hasResult = false;
  nsresult rv;
  while (NS_SUCCEEDED(rv = aStmt->ExecuteStep(&hasResult)) && hasResult) {
    aIconIds.AppendElement(aStmt->AsInt64(0));
  }
  return rv;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
//// AsyncAssociateIconToPage

AsyncAssociateIconToPage::AsyncAssociateIconToPage(const IconData& aIcon,
                                                   const PageData& aPage)
    : Runnable("places::AsyncAssociateIconToPage"), mIcon(aIcon), mPage(aPage) {
  // May be created in both threads.
}

NS_IMETHODIMP
AsyncAssociateIconToPage::Run() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!mPage.guid.IsEmpty(),
             "Page info should have been fetched already");
  MOZ_ASSERT(mPage.canAddToHistory || !mPage.bookmarkedSpec.IsEmpty(),
             "The page should be addable to history or a bookmark");

  bool shouldUpdateIcon = false;
  for (const auto& payload : mIcon.payloads) {
    // If the entry is missing from the database, we should add it.
    if (payload.id == 0) {
      shouldUpdateIcon = true;
      break;
    }
  }

  RefPtr<Database> DB = Database::GetDatabase();
  NS_ENSURE_STATE(DB);

  mozStorageTransaction transaction(
      DB->MainConn(), false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

  // XXX Handle the error, bug 1696133.
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  nsresult rv;
  if (shouldUpdateIcon) {
    rv = SetIconInfo(DB, mIcon);
    if (NS_FAILED(rv)) {
      (void)transaction.Commit();
      return rv;
    }

    mIcon.status = (mIcon.status & ~(ICON_STATUS_CACHED)) | ICON_STATUS_SAVED;
  }

  // If the page does not have an id, don't try to insert a new one, cause we
  // don't know where the page comes from.  Not doing so we may end adding
  // a page that otherwise we'd explicitly ignore, like a POST or an error page.
  if (mPage.placeId == 0) {
    rv = transaction.Commit();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  // Expire old favicons to keep up with website changes. Associated icons must
  // be expired also when storing a root favicon, because a page may change to
  // only have a root favicon.
  // Note that here we could also be in the process of adding further payloads
  // to a page, and we don't want to expire just added payloads. For this
  // reason we only remove expired payloads.
  // Icons left orphan are removed once the new relations have been added.
  nsTArray<int64_t> expiredIconIds;
  if (mPage.id > 0) {
    nsCOMPtr<mozIStorageStatement> stmt;
    stmt = DB->GetStatement(
        "DELETE FROM moz_icons_to_pages "
        "WHERE page_id = :page_id "
        "AND expire_ms < strftime('%s','now','localtime','utc') * 1000 "
        "RETURNING icon_id");
    NS_ENSURE_STATE(stmt);
    mozStorageStatementScoper scoper(stmt);
    rv = stmt->BindInt64ByName("page_id"_ns, mPage.id);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = CollectIconIds(stmt, expiredIconIds);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Don't associate pages to root domain icons, since those will be returned
  // regardless.  This saves a lot of work and database space since we don't
  // need to store urls and relations.
  // Though, this is possible only if both the page and the icon have the same
  // host, otherwise we couldn't relate them.
  if (!mIcon.rootIcon || !mIcon.host.Equals(mPage.host)) {
    // The page may have associated payloads already, and those could have to be
    // expired. For example at a certain point a page could decide to stop
    // serving its usual 16px and 32px pngs, and use an svg instead. On the
    // other side, we could also be in the process of adding more payloads to
    // this page, and we should not expire the payloads we just added. For this,
    // we use the expiration field as an indicator and remove relations based on
    // it being elapsed.
    if (mPage.id == 0) {
      // We need to create the page entry.
      nsCOMPtr<mozIStorageStatement> stmt;
      stmt = DB->GetStatement(
          "INSERT OR IGNORE INTO moz_pages_w_icons (page_url, page_url_hash) "
          "VALUES (:page_url, hash(:page_url)) ");
      NS_ENSURE_STATE(stmt);
      mozStorageStatementScoper scoper(stmt);
      rv = URIBinder::Bind(stmt, "page_url"_ns, mPage.spec);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Then we can create the relations.
    nsCOMPtr<mozIStorageStatement> stmt;
    stmt = DB->GetStatement(
        "INSERT INTO moz_icons_to_pages (page_id, icon_id, expire_ms) "
        "VALUES ((SELECT id from moz_pages_w_icons WHERE page_url_hash = "
        "  hash(:page_url) AND page_url = :page_url), :icon_id, :expire) "
        "ON CONFLICT(page_id, icon_id) DO "
        "  UPDATE SET expire_ms = :expire ");
    NS_ENSURE_STATE(stmt);

    // For some reason using BindingParamsArray here fails execution, so we must
    // execute the statements one by one.
    // In the future we may want to investigate the reasons, sounds like related
    // to contraints.
    for (const auto& payload : mIcon.payloads) {
      mozStorageStatementScoper scoper(stmt);
      nsCOMPtr<mozIStorageBindingParams> params;
      rv = URIBinder::Bind(stmt, "page_url"_ns, mPage.spec);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->BindInt64ByName("icon_id"_ns, payload.id);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->BindInt64ByName("expire"_ns, mIcon.expiration / 1000);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = stmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // This must happen after adding the new relations, since the icon may have
  // been replaced by itself.
  rv = RemoveOrphanIcons(DB, expiredIconIds);
  NS_ENSURE_SUCCESS(rv, rv);

  mIcon.status |= ICON_STATUS_ASSOCIATED;

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  // Finally, dispatch an event to the main thread to notify observers.
  nsCOMPtr<nsIRunnable> event = new NotifyIconObservers(mIcon, mPage);
  rv = NS_DispatchToMainThread(event);
  NS_ENSURE_SUCCESS(rv, rv);

  // If there is a bookmarked page that redirects to this one, try to update its
  // icon as well.
  if (!mPage.bookmarkedSpec.IsEmpty() &&
      !mPage.bookmarkedSpec.Equals(mPage.spec)) {
    // Create a new page struct to avoid polluting it with old data.
    PageData bookmarkedPage;
    bookmarkedPage.spec = mPage.bookmarkedSpec;
    RefPtr<Database> DB = Database::GetDatabase();
    if (DB && NS_SUCCEEDED(FetchPageInfo(DB, bookmarkedPage))) {
      RefPtr<AsyncAssociateIconToPage> event =
          new AsyncAssociateIconToPage(mIcon, bookmarkedPage);
      (void)event->Run();
    }
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// AsyncSetIconForPage

AsyncSetIconForPage::AsyncSetIconForPage(const IconData& aIcon,
                                         const PageData& aPage,
                                         dom::Promise* aPromise)
    : Runnable("places::AsyncSetIconForPage"),
      mPromise(new nsMainThreadPtrHolder<dom::Promise>(
          "AsyncSetIconForPage::Promise", aPromise, false)),
      mIcon(aIcon),
      mPage(aPage) {}

NS_IMETHODIMP
AsyncSetIconForPage::Run() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(mIcon.payloads.Length(), "The icon should have valid data");
  MOZ_ASSERT(mPage.spec.Length(), "The page should have spec");
  MOZ_ASSERT(mPage.guid.IsEmpty(), "The page should not have guid");

  nsresult rv = NS_OK;
  auto guard = MakeScopeExit([&]() {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "AsyncSetIconForPage::Promise", [rv, promise = std::move(mPromise)]() {
          if (NS_SUCCEEDED(rv)) {
            promise->MaybeResolveWithUndefined();
          } else {
            promise->MaybeReject(rv);
          }
        }));
  });

  // Fetch the page data.
  RefPtr<Database> DB = Database::GetDatabase();
  if (MOZ_UNLIKELY(!DB)) {
    return (rv = NS_ERROR_UNEXPECTED);
  }
  rv = FetchPageInfo(DB, mPage);
  NS_ENSURE_SUCCESS(rv, rv);

  AsyncAssociateIconToPage event(mIcon, mPage);
  return (rv = event.Run());
}

////////////////////////////////////////////////////////////////////////////////
//// AsyncGetFaviconForPageRunnable

AsyncGetFaviconForPageRunnable::AsyncGetFaviconForPageRunnable(
    const nsCOMPtr<nsIURI>& aPageURI, uint16_t aPreferredWidth,
    const RefPtr<FaviconPromise::Private>& aPromise, bool aOnConcurrentConn)
    : Runnable("places::AsyncGetFaviconForPage"),
      mPageURI(aPageURI),
      mPreferredWidth(aPreferredWidth == 0 ? UINT16_MAX : aPreferredWidth),
      mPromise(new nsMainThreadPtrHolder<FaviconPromise::Private>(
          "AsyncGetFaviconForPageRunnable::Promise", aPromise, false)),
      mOnConcurrentConn(aOnConcurrentConn) {
  MOZ_ASSERT(NS_IsMainThread());
}

NS_IMETHODIMP
AsyncGetFaviconForPageRunnable::Run() {
  MOZ_ASSERT(!NS_IsMainThread());

  IconData iconData;
  nsresult rv = NS_OK;

  auto guard = MakeScopeExit([&]() {
    if (NS_FAILED(rv)) {
      mPromise->Reject(rv, __func__);
      return;
    }

    if (iconData.payloads.Length() == 0) {
      // Not found.
      mPromise->Resolve(nullptr, __func__);
      return;
    }

    IconPayload& payload = iconData.payloads[0];
    nsCOMPtr<nsIFavicon> favicon = new Favicon(iconData.spec, payload.data,
                                               payload.mimeType, payload.width);
    mPromise->Resolve(favicon.forget(), __func__);
  });

  UniquePtr<ConnectionAdapter> adapter;
  if (!mOnConcurrentConn) {
    RefPtr<Database> DB = Database::GetDatabase();
    if (DB) {
      adapter = std::make_unique<ConnectionAdapter>(DB);
    }
  } else {
    auto conn = ConcurrentConnection::GetInstance();
    MOZ_ASSERT(conn);
    if (conn.isSome()) {
      adapter = std::make_unique<ConnectionAdapter>(conn.value());
    }
  }
  // In certain circumstances, like when the database or ConcurrentConnection
  // is being shut down, we may not have a connection.
  if (!adapter) {
    return (rv = NS_ERROR_UNEXPECTED);
  }

  rv = FetchIconInfo(adapter, mPageURI, mPreferredWidth, iconData);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

AsyncGetFaviconForPageRunnable::~AsyncGetFaviconForPageRunnable() {
  // If the promise is not fulfilled yet we must reject it to avoid assertions,
  // otherwise this is a no-op.
  // This may happen for example when the runnable is enqueued in
  // ConcurrentConnection and it skips unnecessary work on shutdown.
  mPromise->Reject(NS_ERROR_ABORT, __func__);
}

////////////////////////////////////////////////////////////////////////////////
//// AsyncTryCopyFaviconsRunnable

AsyncTryCopyFaviconsRunnable::AsyncTryCopyFaviconsRunnable(
    const nsCOMPtr<nsIURI>& aFromPageURI, const nsCOMPtr<nsIURI>& aToPageURI,
    const bool aCanAddToHistoryForToPage,
    const RefPtr<BoolPromise::Private>& aPromise)
    : Runnable("places::AsyncTryCopyFaviconsRunnable"),
      mFromPageURI(aFromPageURI),
      mToPageURI(aToPageURI),
      mCanAddToHistoryForToPage(aCanAddToHistoryForToPage),
      mPromise(new nsMainThreadPtrHolder<BoolPromise::Private>(
          "AsyncTryCopyFaviconsRunnable::Promise", aPromise, false)) {
  MOZ_ASSERT(NS_IsMainThread());
}

NS_IMETHODIMP AsyncTryCopyFaviconsRunnable::Run() {
  MOZ_ASSERT(!NS_IsMainThread());

  IconData fromIconData;
  PageData toPageData;
  nsresult rv = NS_OK;

  auto guard = MakeScopeExit([&]() {
    if (NS_FAILED(rv)) {
      mPromise->Reject(rv, __func__);
      return;
    }

    bool copied = fromIconData.status & ICON_STATUS_ASSOCIATED;
    mPromise->Resolve(copied, __func__);

    if (!copied) {
      return;
    }

    nsCOMPtr<nsIRunnable> event =
        new NotifyIconObservers(fromIconData, toPageData);
    NS_DispatchToMainThread(event);
  });

  RefPtr<Database> DB = Database::GetDatabase();
  NS_ENSURE_STATE(DB);
  auto adapter = std::make_unique<ConnectionAdapter>(DB);
  rv = FetchIconInfo(adapter, mFromPageURI, UINT16_MAX, fromIconData);
  NS_ENSURE_SUCCESS(rv, rv);
  if (fromIconData.payloads.IsEmpty()) {
    // There's nothing to copy.
    return NS_OK;
  }

  mToPageURI->GetSpec(toPageData.spec);
  toPageData.canAddToHistory = mCanAddToHistoryForToPage;
  rv = FetchPageInfo(DB, toPageData);

  if (rv == NS_ERROR_NOT_AVAILABLE || !toPageData.placeId) {
    // We have never seen this page, or we can't add this page to history and
    // and it's not a bookmark. We won't add the page.
    return (rv = NS_OK);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Insert an entry in moz_pages_w_icons if needed.
  if (!toPageData.id) {
    // We need to create the page entry.
    nsCOMPtr<mozIStorageStatement> stmt;
    stmt = DB->GetStatement(
        "INSERT OR IGNORE INTO moz_pages_w_icons (page_url, page_url_hash) "
        "VALUES (:page_url, hash(:page_url)) ");
    NS_ENSURE_STATE(stmt);
    mozStorageStatementScoper scoper(stmt);
    rv = URIBinder::Bind(stmt, "page_url"_ns, toPageData.spec);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
    // Required to to fetch the id and the guid.
    rv = FetchPageInfo(DB, toPageData);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Create the relations.
  nsCOMPtr<mozIStorageStatement> stmt = DB->GetStatement(
      "INSERT INTO moz_icons_to_pages (page_id, icon_id, expire_ms) "
      "SELECT :id, icon_id, expire_ms "
      "FROM moz_icons_to_pages "
      "WHERE page_id = (SELECT id FROM moz_pages_w_icons "
      "                 WHERE page_url_hash = hash(:url) AND page_url = :url) "
      "ON CONFLICT (page_id, icon_id) DO "
      "  UPDATE SET expire_ms = max(excluded.expire_ms, :min_expiration_ms)");
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);
  rv = stmt->BindInt64ByName("id"_ns, toPageData.id);
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString fromPageSpec;
  mFromPageURI->GetSpec(fromPageSpec);
  rv = URIBinder::Bind(stmt, "url"_ns, fromPageSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindInt64ByName("min_expiration_ms"_ns,
                             (PR_Now() + MIN_FAVICON_EXPIRATION) / 1000);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // Setting this will make us send pageChanged notifications.
  // The scope exit will take care of the callback and notifications.
  fromIconData.status |= ICON_STATUS_ASSOCIATED;

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// AsyncExpireFaviconsForPage

AsyncExpireFaviconsForPage::AsyncExpireFaviconsForPage(
    const nsCOMPtr<nsIURI>& aPageURI, dom::Promise* aPromise)
    : Runnable("places::AsyncExpireFaviconsForPage"),
      mPageURI(aPageURI),
      mPromise(new nsMainThreadPtrHolder<dom::Promise>(
          "AsyncExpireFaviconsForPage::Promise", aPromise, false)) {}

NS_IMETHODIMP
AsyncExpireFaviconsForPage::Run() {
  MOZ_ASSERT(!NS_IsMainThread());

  nsresult rv = NS_OK;
  auto guard = MakeScopeExit([&]() {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("AsyncExpireFaviconsForPage::Promise",
                               [rv, promise = std::move(mPromise)]() {
                                 if (NS_SUCCEEDED(rv)) {
                                   promise->MaybeResolveWithUndefined();
                                 } else {
                                   promise->MaybeReject(rv);
                                 }
                               }));
  });

  RefPtr<Database> DB = Database::GetDatabase();
  if (MOZ_UNLIKELY(!DB)) {
    return (rv = NS_ERROR_UNEXPECTED);
  }

  mozStorageTransaction transaction(
      DB->MainConn(), false, mozIStorageConnection::TRANSACTION_IMMEDIATE);
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  // Delete all icon associations for the given page, using the same page
  // lookup logic as FetchIconInfo: match the exact URL and, if it has a
  // fragment, also match the fragment-stripped URL (since bookmarks typically
  // point to the URL without fragment).
  nsTArray<int64_t> iconIds;
  {
    nsCOMPtr<mozIStorageStatement> stmt = DB->GetStatement(
        "DELETE FROM moz_icons_to_pages WHERE page_id IN ( "
        "  SELECT id FROM moz_pages_w_icons "
        "  WHERE (page_url_hash = hash(:page_url) AND page_url = :page_url) "
        "  OR (:hash_idx AND page_url_hash = hash(substr(:page_url, 0, "
        ":hash_idx)) "
        "      AND page_url = substr(:page_url, 0, :hash_idx)) "
        ") RETURNING icon_id");
    if (MOZ_UNLIKELY(!stmt)) {
      return (rv = NS_ERROR_UNEXPECTED);
    }
    mozStorageStatementScoper scoper(stmt);
    nsAutoCString spec;
    rv = mPageURI->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);
    int32_t hashIdx = spec.RFind("#") + 1;
    rv = URIBinder::Bind(stmt, "page_url"_ns, spec);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = stmt->BindInt32ByName("hash_idx"_ns, hashIdx);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = CollectIconIds(stmt, iconIds);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = RemoveOrphanIcons(DB, iconIds);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// NotifyIconObservers

NotifyIconObservers::NotifyIconObservers(const IconData& aIcon,
                                         const PageData& aPage)
    : Runnable("places::NotifyIconObservers"), mIcon(aIcon), mPage(aPage) {}

// MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is marked
// MOZ_CAN_RUN_SCRIPT.  See bug 1535398.
MOZ_CAN_RUN_SCRIPT_BOUNDARY
NS_IMETHODIMP
NotifyIconObservers::Run() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIURI> iconURI;
  if (!mIcon.spec.IsEmpty()) {
    if (!NS_WARN_IF(
            NS_FAILED(NS_NewURI(getter_AddRefs(iconURI), mIcon.spec)))) {
      // Notify observers only if something changed.
      if (mIcon.status & ICON_STATUS_SAVED ||
          mIcon.status & ICON_STATUS_ASSOCIATED) {
        nsCOMPtr<nsIURI> pageURI;
        if (!NS_WARN_IF(
                NS_FAILED(NS_NewURI(getter_AddRefs(pageURI), mPage.spec)))) {
          // Invalide page-icon image cache, since the icon is about to change.
          nsFaviconService* favicons = nsFaviconService::GetFaviconService();
          MOZ_ASSERT(favicons);
          if (favicons) {
            nsCString pageIconSpec("page-icon:");
            pageIconSpec.Append(mPage.spec);
            nsCOMPtr<nsIURI> pageIconURI;
            if (NS_SUCCEEDED(
                    NS_NewURI(getter_AddRefs(pageIconURI), pageIconSpec))) {
              favicons->ClearImageCache(pageIconURI);
            }
          }

          // Notify about the favicon change.
          dom::Sequence<OwningNonNull<dom::PlacesEvent>> events;
          RefPtr<dom::PlacesFavicon> faviconEvent = new dom::PlacesFavicon();
          AppendUTF8toUTF16(mPage.spec, faviconEvent->mUrl);
          AppendUTF8toUTF16(mIcon.spec, faviconEvent->mFaviconUrl);
          faviconEvent->mPageGuid.Assign(mPage.guid);
          bool success =
              !!events.AppendElement(faviconEvent.forget(), fallible);
          MOZ_RELEASE_ASSERT(success);
          dom::PlacesObservers::NotifyListeners(events);
        }
      }
    }
  }

  return NS_OK;
}

}  // namespace places
}  // namespace mozilla
