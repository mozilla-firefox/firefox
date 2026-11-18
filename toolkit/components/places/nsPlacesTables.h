/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsPlacesTables_h_
#define _nsPlacesTables_h_

#define CREATE_MOZ_PLACES                                \
  nsLiteralCString(                                      \
      "CREATE TABLE moz_places ( "                       \
      "  id INTEGER PRIMARY KEY"                         \
      ", url LONGVARCHAR"                                \
      ", title LONGVARCHAR"                              \
      ", rev_host LONGVARCHAR"                           \
      ", visit_count INTEGER DEFAULT 0"                  \
      ", hidden INTEGER DEFAULT 0 NOT NULL"              \
      ", typed INTEGER DEFAULT 0 NOT NULL"               \
      ", frecency INTEGER DEFAULT -1 NOT NULL"           \
      ", last_visit_date INTEGER "                       \
      ", guid TEXT"                                      \
      ", foreign_count INTEGER DEFAULT 0 NOT NULL"       \
      ", url_hash INTEGER DEFAULT 0 NOT NULL "           \
      ", description TEXT"                               \
      ", preview_image_url TEXT"                         \
      ", site_name TEXT"                                 \
      ", origin_id INTEGER REFERENCES moz_origins(id)"   \
      ", recalc_frecency INTEGER NOT NULL DEFAULT 0"     \
      ", alt_frecency INTEGER"                           \
      ", recalc_alt_frecency INTEGER NOT NULL DEFAULT 0" \
      ")")

#define CREATE_MOZ_HISTORYVISITS                   \
  nsLiteralCString(                                \
      "CREATE TABLE moz_historyvisits ("           \
      "  id INTEGER PRIMARY KEY"                   \
      ", from_visit INTEGER"                       \
      ", place_id INTEGER"                         \
      ", visit_date INTEGER"                       \
      ", visit_type INTEGER"                       \
      ", session INTEGER"                          \
      ", source INTEGER DEFAULT 0 NOT NULL"        \
      ", triggeringPlaceId INTEGER"                \
      ", userContextId INTEGER NOT NULL DEFAULT 0" \
      ")")

// These two tables were designed to store data with json in mind
// ideally one column per "consumer" (sync, annotations, etc) to keep
// concerns separate. Using an UPSERT is the suggested way to update
// this table vs INSERT OR REPLACE to avoid clearing out any existing properties
// see PlacesSyncUtils.sys.mjs for an example of how sync does this
#define CREATE_MOZ_PLACES_EXTRA                                               \
  nsLiteralCString(                                                           \
      "CREATE TABLE moz_places_extra ("                                       \
      "  place_id INTEGER PRIMARY KEY NOT NULL"                               \
      ", sync_json TEXT"                                                      \
      ", FOREIGN KEY (place_id) REFERENCES moz_places(id) ON DELETE CASCADE " \
      ")")

#define CREATE_MOZ_HISTORYVISITS_EXTRA                                \
  nsLiteralCString(                                                   \
      "CREATE TABLE moz_historyvisits_extra ("                        \
      "  visit_id INTEGER PRIMARY KEY NOT NULL"                       \
      ", sync_json TEXT"                                              \
      ", FOREIGN KEY (visit_id) REFERENCES moz_historyvisits(id) ON " \
      "  DELETE CASCADE"                                              \
      ")")

#define CREATE_MOZ_INPUTHISTORY         \
  nsLiteralCString(                     \
      "CREATE TABLE moz_inputhistory (" \
      "  place_id INTEGER NOT NULL"     \
      ", input LONGVARCHAR NOT NULL"    \
      ", use_count INTEGER"             \
      ", PRIMARY KEY (place_id, input)" \
      ")")

// Note: flags, expiration, type, dateAdded and lastModified should be
// considered deprecated but are kept to ease backwards compatibility.
#define CREATE_MOZ_ANNOS                 \
  nsLiteralCString(                      \
      "CREATE TABLE moz_annos ("         \
      "  id INTEGER PRIMARY KEY"         \
      ", place_id INTEGER NOT NULL"      \
      ", anno_attribute_id INTEGER"      \
      ", content LONGVARCHAR"            \
      ", flags INTEGER DEFAULT 0"        \
      ", expiration INTEGER DEFAULT 0"   \
      ", type INTEGER DEFAULT 0"         \
      ", dateAdded INTEGER DEFAULT 0"    \
      ", lastModified INTEGER DEFAULT 0" \
      ")")

#define CREATE_MOZ_ANNO_ATTRIBUTES         \
  nsLiteralCString(                        \
      "CREATE TABLE moz_anno_attributes (" \
      "  id INTEGER PRIMARY KEY"           \
      ", name VARCHAR(32) UNIQUE NOT NULL" \
      ")")

#define CREATE_MOZ_ITEMS_ANNOS           \
  nsLiteralCString(                      \
      "CREATE TABLE moz_items_annos ("   \
      "  id INTEGER PRIMARY KEY"         \
      ", item_id INTEGER NOT NULL"       \
      ", anno_attribute_id INTEGER"      \
      ", content LONGVARCHAR"            \
      ", flags INTEGER DEFAULT 0"        \
      ", expiration INTEGER DEFAULT 0"   \
      ", type INTEGER DEFAULT 0"         \
      ", dateAdded INTEGER DEFAULT 0"    \
      ", lastModified INTEGER DEFAULT 0" \
      ")")

#define CREATE_MOZ_BOOKMARKS                                                   \
  nsLiteralCString(                                                            \
      "CREATE TABLE moz_bookmarks ("                                           \
      "  id INTEGER PRIMARY KEY"                                               \
      ", type INTEGER"                                                         \
      ", fk INTEGER DEFAULT NULL" /* place_id */                               \
      ", parent INTEGER"                                                       \
      ", position INTEGER"                                                     \
      ", title LONGVARCHAR"                                                    \
      ", keyword_id INTEGER"                                                   \
      ", folder_type TEXT"                                                     \
      ", dateAdded INTEGER"                                                    \
      ", lastModified INTEGER"                                                 \
      ", guid TEXT" /* The sync status is determined from the change source.   \
                       We set this to SYNC_STATUS_NEW = 1 for new local        \
                       bookmarks, and SYNC_STATUS_NORMAL = 2 for bookmarks     \
                       from other devices. Uploading a local bookmark for the  \
                       first time changes its status to SYNC_STATUS_NORMAL.    \
                       For bookmarks restored from a backup, we set            \
                       SYNC_STATUS_UNKNOWN = 0, indicating that Sync should    \
                       reconcile them with bookmarks on the server. If Sync is \
                       disconnected or never set up, all bookmarks will stay   \
                       in SYNC_STATUS_NEW.                                     \
                    */                                                         \
      ", syncStatus INTEGER NOT NULL DEFAULT 0" /* This field is incremented   \
                                                   for every bookmark change   \
                                                   that should trigger a sync. \
                                                   It's a counter instead of a \
                                                   Boolean so that we can      \
                                                   track changes made during a \
                                                   sync, and queue them for    \
                                                   the next sync. Changes made \
                                                   by Sync don't bump the      \
                                                   counter, to avoid sync      \
                                                   loops. If Sync is           \
                                                   disconnected, we'll reset   \
                                                   the counter to 1 for all    \
                                                   bookmarks.                  \
                                                */                             \
      ", syncChangeCounter INTEGER NOT NULL DEFAULT 1"                         \
      ")")

// This table stores tombstones for bookmarks with SYNC_STATUS_NORMAL. We
// upload tombstones during a sync, and delete them from this table on success.
// If Sync is disconnected, we'll delete all stored tombstones. If Sync is
// never set up, we'll never write new tombstones, since all bookmarks will stay
// in SYNC_STATUS_NEW.
#define CREATE_MOZ_BOOKMARKS_DELETED             \
  nsLiteralCString(                              \
      "CREATE TABLE moz_bookmarks_deleted ("     \
      "  guid TEXT PRIMARY KEY"                  \
      ", dateRemoved INTEGER NOT NULL DEFAULT 0" \
      ")")

#define CREATE_MOZ_KEYWORDS                    \
  nsLiteralCString(                            \
      "CREATE TABLE moz_keywords ("            \
      "  id INTEGER PRIMARY KEY AUTOINCREMENT" \
      ", keyword TEXT UNIQUE"                  \
      ", place_id INTEGER"                     \
      ", post_data TEXT"                       \
      ")")

#define CREATE_MOZ_ORIGINS                               \
  nsLiteralCString(                                      \
      "CREATE TABLE moz_origins ( "                      \
      "id INTEGER PRIMARY KEY, "                         \
      "prefix TEXT NOT NULL, "                           \
      "host TEXT NOT NULL, "                             \
      "frecency INTEGER NOT NULL, "                      \
      "recalc_frecency INTEGER NOT NULL DEFAULT 0, "     \
      "alt_frecency INTEGER, "                           \
      "recalc_alt_frecency INTEGER NOT NULL DEFAULT 0, " \
      "block_until_ms INTEGER, "                         \
      "block_pages_until_ms INTEGER, "                   \
      "UNIQUE (host, prefix) "                           \
      ")")

// Note: this should be kept up-to-date with the definition in
//       PlacesUtils.sys.mjs.
#define CREATE_MOZ_OPENPAGES_TEMP                   \
  nsLiteralCString(                                 \
      "CREATE TEMP TABLE moz_openpages_temp ("      \
      "  url TEXT NOT NULL"                         \
      ", userContextId INTEGER NOT NULL"            \
      ", groupId TEXT NOT NULL"                     \
      ", open_count INTEGER"                        \
      ", PRIMARY KEY (url, userContextId, groupId)" \
      ")")

// This table is used to remove orphan origins after pages are removed from
// moz_places. Insertions are made by moz_places_afterdelete_trigger.
// This allows for more performant handling of batch removals, since we'll look
// for orphan origins only once, instead of doing it for each page removal.
// The downside of this approach is that after the removal is complete the
// consumer must remember to also delete from this table, and a trigger will
// take care of orphans.
#define CREATE_UPDATEORIGINSDELETE_TEMP                   \
  nsLiteralCString(                                       \
      "CREATE TEMP TABLE moz_updateoriginsdelete_temp ( " \
      "  prefix TEXT NOT NULL, "                          \
      "  host TEXT NOT NULL, "                            \
      "  PRIMARY KEY (prefix, host) "                     \
      ") WITHOUT ROWID")

// Icons not related to any page. Root icons are used by all the pages of their
// origin, so they are only orphans if no origin with pages is left for their
// host, with or without www.
#define CREATE_MOZ_ORPHAN_ICONS_VIEW                                    \
  nsLiteralCString(                                                     \
      "CREATE TEMP VIEW moz_orphan_icons AS "                           \
      "SELECT id FROM moz_icons i "                                     \
      "WHERE NOT EXISTS ( "                                             \
      "  SELECT 1 FROM moz_icons_to_pages WHERE icon_id = i.id "        \
      ") "                                                              \
      "AND (root = 0 OR NOT EXISTS ( "                                  \
      "  SELECT 1 FROM moz_origins o "                                  \
      "  WHERE o.host IN ( "                                            \
      "    fixup_url(get_host_and_port(i.icon_url)), "                  \
      "    'www.' || fixup_url(get_host_and_port(i.icon_url)) "         \
      "  ) "                                                            \
      "  AND EXISTS (SELECT 1 FROM moz_places WHERE origin_id = o.id) " \
      "))")

// This table would not be strictly needed for functionality since it's just
// mimicking moz_places, though it's great for database portability.
// With this we don't have to take care into account a bunch of database
// mismatch cases, where places.sqlite could be mixed up with a favicons.sqlite
// created with a different places.sqlite (not just in case of a user messing
// up with the profile, but also in case of corruption).
#define CREATE_MOZ_PAGES_W_ICONS          \
  nsLiteralCString(                       \
      "CREATE TABLE moz_pages_w_icons ( " \
      "id INTEGER PRIMARY KEY, "          \
      "page_url TEXT NOT NULL, "          \
      "page_url_hash INTEGER NOT NULL "   \
      ") ")

// This table retains the icons data. The hashes url is "fixed" (thus the scheme
// and www are trimmed in most cases) so we can quickly query for root icon urls
// like "domain/favicon.ico".
// We are considering squared icons for simplicity, so storing only one size.
// For svg payloads, width will be set to 65535 (UINT16_MAX).
#define CREATE_MOZ_ICONS                       \
  nsLiteralCString(                            \
      "CREATE TABLE moz_icons ( "              \
      "id INTEGER PRIMARY KEY, "               \
      "icon_url TEXT NOT NULL, "               \
      "fixed_icon_url_hash INTEGER NOT NULL, " \
      "width INTEGER NOT NULL DEFAULT 0, "     \
      "root INTEGER NOT NULL DEFAULT 0, "      \
      "color INTEGER, "                        \
      "expire_ms INTEGER NOT NULL DEFAULT 0, " \
      "flags INTEGER NOT NULL DEFAULT 0, "     \
      "data BLOB "                             \
      ") ")

// This table maintains relations between icons and pages.
// Each page can have multiple icons, and each icon can be used by multiple
// pages.
#define CREATE_MOZ_ICONS_TO_PAGES                                              \
  nsLiteralCString(                                                            \
      "CREATE TABLE moz_icons_to_pages ( "                                     \
      "page_id INTEGER NOT NULL, "                                             \
      "icon_id INTEGER NOT NULL, "                                             \
      "expire_ms INTEGER NOT NULL DEFAULT 0, "                                 \
      "PRIMARY KEY (page_id, icon_id), "                                       \
      "FOREIGN KEY (page_id) REFERENCES moz_pages_w_icons ON DELETE CASCADE, " \
      "FOREIGN KEY (icon_id) REFERENCES moz_icons ON DELETE CASCADE "          \
      ") WITHOUT ROWID ")

// This table holds key-value metadata for Places and its consumers. Sync stores
// the sync IDs for the bookmarks and history collections in this table, and the
// last sync time for history.
#define CREATE_MOZ_META         \
  nsLiteralCString(             \
      "CREATE TABLE moz_meta (" \
      "key TEXT PRIMARY KEY, "  \
      "value NOT NULL"          \
      ") WITHOUT ROWID ")

// This table holds history interactions that will be used to achieve improved
// history recalls.
#define CREATE_MOZ_PLACES_METADATA                                           \
  nsLiteralCString(                                                          \
      "CREATE TABLE moz_places_metadata ("                                   \
      "id INTEGER PRIMARY KEY, "                                             \
      "place_id INTEGER NOT NULL, "                                          \
      "referrer_place_id INTEGER, "                                          \
      "created_at INTEGER NOT NULL DEFAULT 0, "                              \
      "updated_at INTEGER NOT NULL DEFAULT 0, "                              \
      "total_view_time INTEGER NOT NULL DEFAULT 0, "                         \
      "typing_time INTEGER NOT NULL DEFAULT 0, "                             \
      "key_presses INTEGER NOT NULL DEFAULT 0, "                             \
      "scrolling_time INTEGER NOT NULL DEFAULT 0, "                          \
      "scrolling_distance INTEGER NOT NULL DEFAULT 0, "                      \
      "document_type INTEGER NOT NULL DEFAULT 0, "                           \
      "search_query_id INTEGER, "                                            \
      "FOREIGN KEY (place_id) REFERENCES moz_places(id) ON DELETE CASCADE, " \
      "FOREIGN KEY (referrer_place_id) REFERENCES moz_places(id) ON DELETE " \
      "CASCADE, "                                                            \
      "FOREIGN KEY(search_query_id) REFERENCES "                             \
      "moz_places_metadata_search_queries(id) ON DELETE CASCADE "            \
      "CHECK(place_id != referrer_place_id) "                                \
      ")")

#define CREATE_MOZ_PLACES_METADATA_SEARCH_QUERIES                        \
  nsLiteralCString(                                                      \
      "CREATE TABLE IF NOT EXISTS moz_places_metadata_search_queries ( " \
      "id INTEGER PRIMARY KEY, "                                         \
      "terms TEXT NOT NULL UNIQUE "                                      \
      ")")

#define CREATE_MOZ_PREVIEWS_TOMBSTONES                        \
  nsLiteralCString(                                           \
      "CREATE TABLE IF NOT EXISTS moz_previews_tombstones ( " \
      "  hash TEXT PRIMARY KEY "                              \
      ") WITHOUT ROWID")

#define CREATE_MOZ_NEWTAB_STORY_CLICK            \
  nsLiteralCString(                              \
      "CREATE TABLE moz_newtab_story_click ( "   \
      "  feature TEXT NOT NULL, "                \
      "  timestamp_s INTEGER NOT NULL, "         \
      "  card_format_enum INTEGER NOT NULL, "    \
      "  position INTEGER NOT NULL, "            \
      "  section_position INTEGER NOT NULL, "    \
      "  feature_value REAL NOT NULL DEFAULT 1 " \
      ")")

#define CREATE_MOZ_NEWTAB_STORY_IMPRESSION          \
  nsLiteralCString(                                 \
      "CREATE TABLE moz_newtab_story_impression ( " \
      "  feature TEXT NOT NULL, "                   \
      "  timestamp_s INTEGER NOT NULL, "            \
      "  card_format_enum INTEGER NOT NULL, "       \
      "  position INTEGER NOT NULL, "               \
      "  section_position INTEGER NOT NULL, "       \
      "  feature_value REAL NOT NULL DEFAULT 1 "    \
      ")")

#define CREATE_MOZ_NEWTAB_SHORTCUTS_INTERACTION                          \
  nsLiteralCString(                                                      \
      "CREATE TABLE moz_newtab_shortcuts_interaction ( "                 \
      "  id INTEGER PRIMARY KEY, "                                       \
      "  place_id INTEGER NOT NULL REFERENCES moz_places(id) ON DELETE " \
      "CASCADE, "                                                        \
      "  event_type INTEGER NOT NULL, "                                  \
      "  timestamp_s INTEGER NOT NULL, "                                 \
      "  pinned INTEGER NOT NULL CHECK (pinned IN (0, 1)), "             \
      "  tile_position INTEGER NOT NULL"                                 \
      ")")

#endif  // _nsPlacesTables_h_
