/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsPlacesIndexes_h_
#define nsPlacesIndexes_h_

#define CREATE_IDX_IN_SCHEMA(__schema, __name, __table, __columns, __type)   \
  nsLiteralCString("CREATE " __type " INDEX IF NOT EXISTS " __schema __table \
                   "_" __name " ON " __table " (" __columns ")")

#define CREATE_PLACES_IDX(__name, __table, __columns, __type) \
  CREATE_IDX_IN_SCHEMA("", __name, __table, __columns, __type)

/**
 * moz_places
 */
#define CREATE_IDX_MOZ_PLACES_URL_HASH \
  CREATE_PLACES_IDX("url_hashindex", "moz_places", "url_hash", "")

#define CREATE_IDX_MOZ_PLACES_REVHOST \
  CREATE_PLACES_IDX("hostindex", "moz_places", "rev_host", "")

#define CREATE_IDX_MOZ_PLACES_VISITCOUNT \
  CREATE_PLACES_IDX("visitcount", "moz_places", "visit_count", "")

#define CREATE_IDX_MOZ_PLACES_FRECENCY \
  CREATE_PLACES_IDX("frecencyindex", "moz_places", "frecency", "")

#define CREATE_IDX_MOZ_PLACES_LASTVISITDATE \
  CREATE_PLACES_IDX("lastvisitdateindex", "moz_places", "last_visit_date", "")

#define CREATE_IDX_MOZ_PLACES_GUID \
  CREATE_PLACES_IDX("guid_uniqueindex", "moz_places", "guid", "UNIQUE")

#define CREATE_IDX_MOZ_PLACES_ORIGIN_ID \
  CREATE_PLACES_IDX("originidindex", "moz_places", "origin_id", "")

#define CREATE_IDX_MOZ_PLACES_ALT_FRECENCY \
  CREATE_PLACES_IDX("altfrecencyindex", "moz_places", "alt_frecency", "")

/**
 * moz_historyvisits
 */

#define CREATE_IDX_MOZ_HISTORYVISITS_PLACEDATE             \
  CREATE_PLACES_IDX("placedateindex", "moz_historyvisits", \
                    "place_id, visit_date", "")

#define CREATE_IDX_MOZ_HISTORYVISITS_FROMVISIT \
  CREATE_PLACES_IDX("fromindex", "moz_historyvisits", "from_visit", "")

#define CREATE_IDX_MOZ_HISTORYVISITS_VISITDATE \
  CREATE_PLACES_IDX("dateindex", "moz_historyvisits", "visit_date", "")

/**
 * moz_bookmarks
 */

#define CREATE_IDX_MOZ_BOOKMARKS_PLACETYPE \
  CREATE_PLACES_IDX("itemindex", "moz_bookmarks", "fk, type", "")

#define CREATE_IDX_MOZ_BOOKMARKS_PARENTPOSITION \
  CREATE_PLACES_IDX("parentindex", "moz_bookmarks", "parent, position", "")

#define CREATE_IDX_MOZ_BOOKMARKS_PLACELASTMODIFIED            \
  CREATE_PLACES_IDX("itemlastmodifiedindex", "moz_bookmarks", \
                    "fk, lastModified", "")

#define CREATE_IDX_MOZ_BOOKMARKS_DATEADDED \
  CREATE_PLACES_IDX("dateaddedindex", "moz_bookmarks", "dateAdded", "")

#define CREATE_IDX_MOZ_BOOKMARKS_GUID \
  CREATE_PLACES_IDX("guid_uniqueindex", "moz_bookmarks", "guid", "UNIQUE")

/**
 * moz_annos
 */

#define CREATE_IDX_MOZ_ANNOS_PLACEATTRIBUTE             \
  CREATE_PLACES_IDX("placeattributeindex", "moz_annos", \
                    "place_id, anno_attribute_id", "UNIQUE")

/**
 * moz_items_annos
 */

#define CREATE_IDX_MOZ_ITEMSANNOS_PLACEATTRIBUTE             \
  CREATE_PLACES_IDX("itemattributeindex", "moz_items_annos", \
                    "item_id, anno_attribute_id", "UNIQUE")

/**
 * moz_keywords
 */

#define CREATE_IDX_MOZ_KEYWORDS_PLACEPOSTDATA                    \
  CREATE_PLACES_IDX("placepostdata_uniqueindex", "moz_keywords", \
                    "place_id, post_data", "UNIQUE")

// moz_pages_w_icons

#define CREATE_IDX_MOZ_PAGES_W_ICONS_ICONURLHASH \
  CREATE_PLACES_IDX("urlhashindex", "moz_pages_w_icons", "page_url_hash", "")

// moz_icons

#define CREATE_IDX_MOZ_ICONS_ICONURLHASH \
  CREATE_PLACES_IDX("iconurlhashindex", "moz_icons", "fixed_icon_url_hash", "")

// moz_icons_to_pages
// Indexes the referencing column of the FOREIGN KEY (icon_id), so cascading
// deletes from moz_icons and orphan-icon lookups don't scan the table.
#define CREATE_IDX_MOZ_ICONS_TO_PAGES_ICON(__schema)                           \
  CREATE_IDX_IN_SCHEMA(__schema, "iconindex", "moz_icons_to_pages", "icon_id", \
                       "")

// moz_places_metadata
#define CREATE_IDX_MOZ_PLACES_METADATA_PLACECREATED                    \
  CREATE_PLACES_IDX("placecreated_uniqueindex", "moz_places_metadata", \
                    "place_id, created_at", "UNIQUE")

#define CREATE_IDX_MOZ_PLACES_METADATA_REFERRER             \
  CREATE_PLACES_IDX("referrerindex", "moz_places_metadata", \
                    "referrer_place_id", "")

// moz_newtab_story
#define CREATE_IDX_MOZ_NEWTAB_STORY_CLICK_TIMESTAMP                          \
  CREATE_PLACES_IDX("newtab_click_timestampindex", "moz_newtab_story_click", \
                    "timestamp_s", "")

#define CREATE_IDX_MOZ_NEWTAB_IMPRESSION_TIMESTAMP      \
  CREATE_PLACES_IDX("newtab_impression_timestampindex", \
                    "moz_newtab_story_impression", "timestamp_s", "")

// moz_newtab_shortcuts
#define CREATE_IDX_MOZ_NEWTAB_SHORTCUTS_TIMESTAMP                         \
  CREATE_PLACES_IDX("timestampindex", "moz_newtab_shortcuts_interaction", \
                    "timestamp_s", "")

#define CREATE_IDX_MOZ_NEWTAB_SHORTCUTS_PLACEID                         \
  CREATE_PLACES_IDX("placeidindex", "moz_newtab_shortcuts_interaction", \
                    "place_id", "")

#endif  // nsPlacesIndexes_h_
