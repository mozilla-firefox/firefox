/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::db::{PlacesDb, PlacesTransaction};
use crate::error::*;
use crate::RowId;
use error_support::{breadcrumb, redact_url};
use rusqlite::types::{FromSql, FromSqlResult, ToSql, ToSqlOutput, ValueRef};
use sql_support::ConnExt;
use std::vec::Vec;
use sync_guid::Guid as SyncGuid;
use types::Timestamp;
use url::Url;

use lazy_static::lazy_static;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum DocumentType {
    Regular = 0,
    Media = 1,
}

impl FromSql for DocumentType {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        Ok(match value.as_i64()? {
            0 => DocumentType::Regular,
            1 => DocumentType::Media,
            other => {
                // seems safe to ignore?
                warn!("invalid DocumentType {}", other);
                DocumentType::Regular
            }
        })
    }
}

impl ToSql for DocumentType {
    #[inline]
    fn to_sql(&self) -> rusqlite::Result<ToSqlOutput<'_>> {
        Ok(ToSqlOutput::from(*self as u32))
    }
}

#[derive(Clone)]
pub struct HistoryHighlightWeights {
    pub view_time: f64,
    pub frequency: f64,
}

#[derive(Clone)]
pub struct HistoryHighlight {
    pub score: f64,
    pub place_id: i32,
    pub url: String,
    pub title: Option<String>,
    pub preview_image_url: Option<String>,
}

impl HistoryHighlight {
    pub(crate) fn from_row(row: &rusqlite::Row<'_>) -> Result<Self> {
        Ok(Self {
            score: row.get("score")?,
            place_id: row.get("place_id")?,
            url: row.get("url")?,
            title: row.get("title")?,
            preview_image_url: row.get("preview_image_url")?,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HistoryMetadataObservation {
    pub url: String,
    pub view_time: Option<i32>,
    pub search_term: Option<String>,
    pub document_type: Option<DocumentType>,
    pub referrer_url: Option<String>,
    pub title: Option<String>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HistoryMetadataPageMissingBehavior {
    InsertPage,
    IgnoreObservation,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NoteHistoryMetadataObservationOptions {
    pub if_page_missing: HistoryMetadataPageMissingBehavior,
}

impl Default for NoteHistoryMetadataObservationOptions {
    fn default() -> Self {
        Self::new()
    }
}

impl NoteHistoryMetadataObservationOptions {
    pub fn new() -> Self {
        Self {
            if_page_missing: HistoryMetadataPageMissingBehavior::IgnoreObservation,
        }
    }

    pub fn if_page_missing(self, if_page_missing: HistoryMetadataPageMissingBehavior) -> Self {
        Self { if_page_missing }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HistoryMetadata {
    pub url: String,
    pub title: Option<String>,
    pub preview_image_url: Option<String>,
    pub created_at: i64,
    pub updated_at: i64,
    pub total_view_time: i32,
    pub search_term: Option<String>,
    pub document_type: DocumentType,
    pub referrer_url: Option<String>,
}

impl HistoryMetadata {
    pub(crate) fn from_row(row: &rusqlite::Row<'_>) -> Result<Self> {
        let created_at: Timestamp = row.get("created_at")?;
        let updated_at: Timestamp = row.get("updated_at")?;

        // Guard against invalid data in the db.
        // Certain client bugs allowed accumulating values that are too large to fit into i32,
        // leading to overflow failures. While this data will expire and will be deleted
        // by clients via `delete_older_than`, we still want to ensure we won't crash in case of
        // encountering it.
        // See `apply_metadata_observation` for where we guard against observing invalid view times.
        let total_view_time: i64 = row.get("total_view_time")?;
        let total_view_time = i32::try_from(total_view_time).unwrap_or(i32::MAX);

        Ok(Self {
            url: row.get("url")?,
            title: row.get("title")?,
            preview_image_url: row.get("preview_image_url")?,
            created_at: created_at.0 as i64,
            updated_at: updated_at.0 as i64,
            total_view_time,
            search_term: row.get("search_term")?,
            document_type: row.get("document_type")?,
            referrer_url: row.get("referrer_url")?,
        })
    }
}

enum PlaceEntry {
    Existing(i64),
    CreateFor(Url, Option<String>),
}

trait WhereArg {
    fn to_where_arg(&self, db_field: &str) -> String;
}

impl PlaceEntry {
    fn fetch(url: &str, tx: &PlacesTransaction<'_>, title: Option<String>) -> Result<Self> {
        let url = Url::parse(url).inspect_err(|_e| {
            breadcrumb!(
                "PlaceEntry::fetch -- Error parsing url: {}",
                redact_url(url)
            );
        })?;
        let place_id = tx.try_query_one(
            "SELECT id FROM moz_places WHERE url_hash = hash(:url) AND url = :url",
            &[(":url", &url.as_str())],
            true,
        )?;

        Ok(match place_id {
            Some(id) => PlaceEntry::Existing(id),
            None => PlaceEntry::CreateFor(url, title),
        })
    }
}

impl WhereArg for PlaceEntry {
    fn to_where_arg(&self, db_field: &str) -> String {
        match self {
            PlaceEntry::Existing(id) => format!("{} = {}", db_field, id),
            PlaceEntry::CreateFor(_, _) => panic!("WhereArg: place entry must exist"),
        }
    }
}

impl WhereArg for Option<PlaceEntry> {
    fn to_where_arg(&self, db_field: &str) -> String {
        match self {
            Some(entry) => entry.to_where_arg(db_field),
            None => format!("{} IS NULL", db_field),
        }
    }
}

trait DatabaseId {
    fn get_or_insert(&self, tx: &PlacesTransaction<'_>) -> Result<i64>;
}

impl DatabaseId for PlaceEntry {
    fn get_or_insert(&self, tx: &PlacesTransaction<'_>) -> Result<i64> {
        Ok(match self {
            PlaceEntry::Existing(id) => *id,
            PlaceEntry::CreateFor(url, title) => {
                let sql = "INSERT INTO moz_places (guid, url, title, url_hash)
                VALUES (:guid, :url, :title, hash(:url))";

                let guid = SyncGuid::random();

                tx.execute_cached(
                    sql,
                    &[
                        (":guid", &guid as &dyn rusqlite::ToSql),
                        (":title", &title),
                        (":url", &url.as_str()),
                    ],
                )?;
                tx.conn().last_insert_rowid()
            }
        })
    }
}

enum SearchQueryEntry {
    Existing(i64),
    CreateFor(String),
}

impl DatabaseId for SearchQueryEntry {
    fn get_or_insert(&self, tx: &PlacesTransaction<'_>) -> Result<i64> {
        Ok(match self {
            SearchQueryEntry::Existing(id) => *id,
            SearchQueryEntry::CreateFor(term) => {
                tx.execute_cached(
                    "INSERT INTO moz_places_metadata_search_queries(term) VALUES (:term)",
                    &[(":term", &term)],
                )?;
                tx.conn().last_insert_rowid()
            }
        })
    }
}

impl SearchQueryEntry {
    fn from(search_term: &str, tx: &PlacesTransaction<'_>) -> Result<Self> {
        let lowercase_term = search_term.to_lowercase();
        Ok(
            match tx.try_query_one(
                "SELECT id FROM moz_places_metadata_search_queries WHERE term = :term",
                &[(":term", &lowercase_term)],
                true,
            )? {
                Some(id) => SearchQueryEntry::Existing(id),
                None => SearchQueryEntry::CreateFor(lowercase_term),
            },
        )
    }
}

impl WhereArg for SearchQueryEntry {
    fn to_where_arg(&self, db_field: &str) -> String {
        match self {
            SearchQueryEntry::Existing(id) => format!("{} = {}", db_field, id),
            SearchQueryEntry::CreateFor(_) => panic!("WhereArg: search query entry must exist"),
        }
    }
}

impl WhereArg for Option<SearchQueryEntry> {
    fn to_where_arg(&self, db_field: &str) -> String {
        match self {
            Some(entry) => entry.to_where_arg(db_field),
            None => format!("{} IS NULL", db_field),
        }
    }
}

struct HistoryMetadataCompoundKey {
    place_entry: PlaceEntry,
    referrer_entry: Option<PlaceEntry>,
    search_query_entry: Option<SearchQueryEntry>,
}

struct MetadataObservation {
    document_type: Option<DocumentType>,
    view_time: Option<i32>,
}

impl HistoryMetadataCompoundKey {
    fn can_debounce(&self) -> Option<i64> {
        match self.place_entry {
            PlaceEntry::Existing(id) => {
                if (match self.search_query_entry {
                    None | Some(SearchQueryEntry::Existing(_)) => true,
                    Some(SearchQueryEntry::CreateFor(_)) => false,
                } && match self.referrer_entry {
                    None | Some(PlaceEntry::Existing(_)) => true,
                    Some(PlaceEntry::CreateFor(_, _)) => false,
                }) {
                    Some(id)
                } else {
                    None
                }
            }
            _ => None,
        }
    }

    // Looks up matching metadata records, by the compound key and time window.
    fn lookup(&self, tx: &PlacesTransaction<'_>, newer_than: i64) -> Result<Option<i64>> {
        Ok(match self.can_debounce() {
            Some(id) => {
                let search_query_id = match self.search_query_entry {
                    None | Some(SearchQueryEntry::CreateFor(_)) => None,
                    Some(SearchQueryEntry::Existing(id)) => Some(id),
                };

                let referrer_place_id = match self.referrer_entry {
                    None | Some(PlaceEntry::CreateFor(_, _)) => None,
                    Some(PlaceEntry::Existing(id)) => Some(id),
                };

                tx.try_query_one::<i64, _>(
                    "SELECT id FROM moz_places_metadata
                        WHERE
                            place_id IS :place_id AND
                            referrer_place_id IS :referrer_place_id AND
                            search_query_id IS :search_query_id AND
                            updated_at >= :newer_than
                        ORDER BY updated_at DESC LIMIT 1",
                    rusqlite::named_params! {
                        ":place_id": id,
                        ":search_query_id": search_query_id,
                        ":referrer_place_id": referrer_place_id,
                        ":newer_than": newer_than
                    },
                    true,
                )?
            }
            None => None,
        })
    }
}

const DEBOUNCE_WINDOW_MS: i64 = 2 * 60 * 1000; // 2 minutes
const MAX_QUERY_RESULTS: i32 = 1000;

const COMMON_METADATA_SELECT: &str = "
SELECT
    m.id as metadata_id, p.url as url, p.title as title, p.preview_image_url as preview_image_url,
    m.created_at as created_at, m.updated_at as updated_at, m.total_view_time as total_view_time,
    m.document_type as document_type, o.url as referrer_url, s.term as search_term
FROM moz_places_metadata m
LEFT JOIN moz_places p ON m.place_id = p.id
LEFT JOIN moz_places_metadata_search_queries s ON m.search_query_id = s.id
LEFT JOIN moz_places o ON o.id = m.referrer_place_id";

// Highlight query returns moz_places entries ranked by a "highlight score".
// This score takes into account two factors:
// 1) frequency of visits to a page,
// 2) cumulative view time of a page.
//
// Eventually, we could consider combining this with `moz_places.frecency` as a basis for (1), that assumes we have a populated moz_historyvisits table.
// Currently, iOS doesn't use 'places' library to track visits, so iOS clients won't have meaningful frecency scores.
//
// Instead, we use moz_places_metadata entries to compute both (1) and (2).
// This has several nice properties:
// - it works on clients that only use 'metadata' APIs, not 'places'
// - since metadata is capped by clients to a certain time window (via `delete_older_than`), the scores will be computed for the same time window
// - we debounce metadata observations to the same "key" if they're close in time.
// -- this is an equivalent of saying that if a page was visited multiple times in quick succession, treat that as a single visit while accumulating the view time
// -- the assumption we're making is that this better matches user perception of their browsing activity
//
// The score is computed as a weighted sum of two probabilities:
// - at any given moment in my browsing sessions for the past X days, how likely am I to be looking at a page?
// - for any given visit during my browsing sessions for the past X days, how likely am I to visit a page?
//
// This kind of scoring is fairly intuitive and simple to reason about at the product level.
//
// An alternative way to arrive at the same ranking would be to normalize the values to compare data of different dimensions, time vs frequency.
// We can normalize view time and frequency into a 0-1 scale before computing weighted scores.
// (select place_id, (normal_frequency * 1.0 + normal_view_time * 1.0) as score from
//     (select place_id, cast(count(*) - min_f as REAL) / cast(range_f as REAL) as normal_frequency, cast(sum(total_view_time) - min_v as REAL) / cast(max_v as REAL) as normal_view_time from moz_places_metadata,
//     (select min(frequency) as min_f, max(frequency) as max_f, max(frequency) - min(frequency) as range_f
//         from (select count(*) as frequency from moz_places_metadata group by place_id)
//     ),
//     (select min(view_time) as min_v, max(view_time) as max_v, max(view_time) - min(view_time) as range_v
//         from (select sum(total_view_time) as view_time from moz_places_metadata where total_view_time > 0 group by place_id)
//     ) where total_view_time > 0 group by place_id)) ranked
//
// Note that while it's tempting to use built-in window functions such percent_rank, they're not sufficient.
// The built-in functions concern themselves with absolute ranking, not taking into account magnitudes of differences between values.
// For example, given two entries we'll know that one is larger than another, but not by how much.
const HIGHLIGHTS_QUERY: &str = "
SELECT
    IFNULL(ranked.score, 0.0) AS score, p.id AS place_id, p.url AS url, p.title AS title, p.preview_image_url AS preview_image_url
FROM moz_places p
INNER JOIN
    (
        SELECT place_id, :view_time_weight * view_time_prob + :frequency_weight * frequency_prob AS score FROM (
            SELECT
                place_id,
                CAST(count(*) AS REAL) / total_count AS frequency_prob,
                CAST(sum(total_view_time) AS REAL) / all_view_time AS view_time_prob
                FROM (
                    SELECT place_id, count(*) OVER () AS total_count, total_view_time, sum(total_view_time) OVER () AS all_view_time FROM moz_places_metadata
                )
            GROUP BY place_id
        )
    ) ranked
ON p.id = ranked.place_id
ORDER BY ranked.score DESC
LIMIT :limit";

lazy_static! {
    static ref GET_LATEST_SQL: String = format!(
        "{common_select_sql}
        WHERE p.url_hash = hash(:url) AND p.url = :url
        ORDER BY updated_at DESC, metadata_id DESC
        LIMIT 1",
        common_select_sql = COMMON_METADATA_SELECT
    );
    static ref GET_BETWEEN_SQL: String = format!(
        "{common_select_sql}
        WHERE updated_at BETWEEN :start AND :end
        ORDER BY updated_at DESC
        LIMIT {max_limit}",
        common_select_sql = COMMON_METADATA_SELECT,
        max_limit = MAX_QUERY_RESULTS
    );
    static ref GET_SINCE_SQL: String = format!(
        "{common_select_sql}
        WHERE updated_at >= :start
        ORDER BY updated_at DESC
        LIMIT :limit",
        common_select_sql = COMMON_METADATA_SELECT
    );
    static ref SEARCH_QUERY_SQL: String = format!(
        "{common_select_sql}
        WHERE search_term NOT NULL
        ORDER BY updated_at DESC
        LIMIT :limit",
        common_select_sql = COMMON_METADATA_SELECT
    );
    static ref QUERY_SQL: String = format!(
        "{common_select_sql}
        WHERE
            p.url LIKE :query OR
            p.title LIKE :query OR
            search_term LIKE :query
        ORDER BY total_view_time DESC
        LIMIT :limit",
        common_select_sql = COMMON_METADATA_SELECT
    );
}

pub fn get_latest_for_url(db: &PlacesDb, url: &Url) -> Result<Option<HistoryMetadata>> {
    let metadata = db.try_query_row(
        GET_LATEST_SQL.as_str(),
        &[(":url", &url.as_str())],
        HistoryMetadata::from_row,
        true,
    )?;
    Ok(metadata)
}

pub fn get_between(db: &PlacesDb, start: i64, end: i64) -> Result<Vec<HistoryMetadata>> {
    db.query_rows_and_then_cached(
        GET_BETWEEN_SQL.as_str(),
        rusqlite::named_params! {
            ":start": start,
            ":end": end,
        },
        HistoryMetadata::from_row,
    )
}

// Returns all history metadata updated on or after `start`, ordered by most recent first,
// capped at the default `MAX_QUERY_RESULTS`.
// TODO(Bug 1993213): Once both iOS and Android consumers use `get_most_recent` instead of `get_since`,
// we should remove `get_since`.
pub fn get_since(db: &PlacesDb, start: i64) -> Result<Vec<HistoryMetadata>> {
    db.query_rows_and_then_cached(
        GET_SINCE_SQL.as_str(),
        rusqlite::named_params! {
            ":start": start,
            ":limit": MAX_QUERY_RESULTS,
        },
        HistoryMetadata::from_row,
    )
}

// Returns the most recent history metadata entries (newest first),
// limited by `limit`.
//
// Internally this uses [`GET_SINCE_SQL`] with `start = i64::MIN`
// to include all entries, ordered by descending `updated_at`.
pub fn get_most_recent(db: &PlacesDb, limit: i32) -> Result<Vec<HistoryMetadata>> {
    db.query_rows_and_then_cached(
        GET_SINCE_SQL.as_str(),
        rusqlite::named_params! {
            ":start": i64::MIN,
            ":limit": limit,
        },
        HistoryMetadata::from_row,
    )
}

// Returns the most recent history metadata entries where search term is not null (newest first),
// limited by `limit`.
//
// Internally this uses [`SEARCH_QUERY_SQL`], ordered by descending `updated_at`.
pub fn get_most_recent_search_entries(db: &PlacesDb, limit: i32) -> Result<Vec<HistoryMetadata>> {
    db.query_rows_and_then_cached(
        SEARCH_QUERY_SQL.as_str(),
        rusqlite::named_params! {
            ":limit": limit,
        },
        HistoryMetadata::from_row,
    )
}

pub fn get_highlights(
    db: &PlacesDb,
    weights: HistoryHighlightWeights,
    limit: i32,
) -> Result<Vec<HistoryHighlight>> {
    db.query_rows_and_then_cached(
        HIGHLIGHTS_QUERY,
        rusqlite::named_params! {
            ":view_time_weight": weights.view_time,
            ":frequency_weight": weights.frequency,
            ":limit": limit
        },
        HistoryHighlight::from_row,
    )
}

pub fn query(db: &PlacesDb, query: &str, limit: i32) -> Result<Vec<HistoryMetadata>> {
    db.query_rows_and_then_cached(
        QUERY_SQL.as_str(),
        rusqlite::named_params! {
            ":query": format!("%{}%", query),
            ":limit": limit
        },
        HistoryMetadata::from_row,
    )
}

pub fn delete_older_than(db: &PlacesDb, older_than: i64) -> Result<()> {
    db.execute_cached(
        "DELETE FROM moz_places_metadata
         WHERE updated_at < :older_than",
        &[(":older_than", &older_than)],
    )?;
    Ok(())
}

pub fn delete_between(db: &PlacesDb, start: i64, end: i64) -> Result<()> {
    db.execute_cached(
        "DELETE FROM moz_places_metadata
        WHERE updated_at > :start and updated_at < :end",
        &[(":start", &start), (":end", &end)],
    )?;
    Ok(())
}

/// Delete all metadata for the specified place id.
pub fn delete_all_metadata_for_page(db: &PlacesDb, place_id: RowId) -> Result<()> {
    db.execute_cached(
        "DELETE FROM moz_places_metadata
         WHERE place_id = :place_id",
        &[(":place_id", &place_id)],
    )?;
    Ok(())
}

/// Delete all metadata for search queries table.
pub fn delete_all_metadata_for_search(db: &PlacesDb) -> Result<()> {
    db.execute_cached("DELETE FROM moz_places_metadata_search_queries", [])?;
    Ok(())
}

pub fn delete_metadata(
    db: &PlacesDb,
    url: &Url,
    referrer_url: Option<&Url>,
    search_term: Option<&str>,
) -> Result<()> {
    let tx = db.begin_transaction()?;

    // Only delete entries that exactly match the key (url+referrer+search_term) we were passed-in.
    // Do nothing if we were asked to delete a key which doesn't match what's in the database.
    // e.g. referrer_url.is_some(), but a correspodning moz_places entry doesn't exist.
    // In practice this shouldn't happen, or it may imply API misuse, but in either case we shouldn't
    // delete things we were not asked to delete.
    let place_entry = PlaceEntry::fetch(url.as_str(), &tx, None)?;
    let place_entry = match place_entry {
        PlaceEntry::Existing(_) => place_entry,
        PlaceEntry::CreateFor(_, _) => {
            tx.rollback()?;
            return Ok(());
        }
    };
    let referrer_entry = match referrer_url {
        Some(referrer_url) if !referrer_url.as_str().is_empty() => {
            Some(PlaceEntry::fetch(referrer_url.as_str(), &tx, None)?)
        }
        _ => None,
    };
    let referrer_entry = match referrer_entry {
        Some(PlaceEntry::Existing(_)) | None => referrer_entry,
        Some(PlaceEntry::CreateFor(_, _)) => {
            tx.rollback()?;
            return Ok(());
        }
    };
    let search_query_entry = match search_term {
        Some(search_term) if !search_term.is_empty() => {
            Some(SearchQueryEntry::from(search_term, &tx)?)
        }
        _ => None,
    };
    let search_query_entry = match search_query_entry {
        Some(SearchQueryEntry::Existing(_)) | None => search_query_entry,
        Some(SearchQueryEntry::CreateFor(_)) => {
            tx.rollback()?;
            return Ok(());
        }
    };

    let sql = format!(
        "DELETE FROM moz_places_metadata WHERE {} AND {} AND {}",
        place_entry.to_where_arg("place_id"),
        referrer_entry.to_where_arg("referrer_place_id"),
        search_query_entry.to_where_arg("search_query_id")
    );

    tx.execute_cached(&sql, [])?;
    tx.commit()?;

    Ok(())
}

pub fn apply_metadata_observation(
    db: &PlacesDb,
    observation: HistoryMetadataObservation,
    options: NoteHistoryMetadataObservationOptions,
) -> Result<()> {
    if let Some(view_time) = observation.view_time {
        // Consider any view_time observations that are higher than 24hrs to be invalid.
        // This guards against clients passing us wildly inaccurate view_time observations,
        // likely resulting from some measurement bug. If we detect such cases, we fail so
        // that the client has a chance to discover its mistake.
        // When recording a view time, we increment the stored value directly in SQL, which
        // doesn't allow for error detection unless we run an additional SELECT statement to
        // query current cumulative view time and see if incrementing it will result in an
        // overflow. This check is a simpler way to achieve the same goal (detect invalid inputs).
        if view_time > 1000 * 60 * 60 * 24 {
            return Err(InvalidMetadataObservation::ViewTimeTooLong.into());
        }
    }

    // Begin a write transaction. We do this before any other work (e.g. SELECTs) to avoid racing against
    // other writers. Even though we expect to only have a single application writer, a sync writer
    // can come in at any time and change data we depend on, such as moz_places
    // and moz_origins, leaving us in a potentially inconsistent state.
    let tx = db.begin_transaction()?;

    let place_entry = PlaceEntry::fetch(&observation.url, &tx, observation.title.clone())?;
    let result = apply_metadata_observation_impl(&tx, place_entry, observation, options);

    // Inserting into moz_places has side-effects (temp tables are populated via triggers and need to be flushed).
    // This call "finalizes" these side-effects.
    super::delete_pending_temp_tables(db)?;
    match result {
        Ok(_) => tx.commit()?,
        Err(_) => tx.rollback()?,
    };

    result
}

fn apply_metadata_observation_impl(
    tx: &PlacesTransaction<'_>,
    place_entry: PlaceEntry,
    observation: HistoryMetadataObservation,
    options: NoteHistoryMetadataObservationOptions,
) -> Result<()> {
    let referrer_entry = match observation.referrer_url {
        Some(referrer_url) if !referrer_url.is_empty() => {
            Some(PlaceEntry::fetch(&referrer_url, tx, None)?)
        }
        Some(_) | None => None,
    };
    let search_query_entry = match observation.search_term {
        Some(search_term) if !search_term.is_empty() => {
            Some(SearchQueryEntry::from(&search_term, tx)?)
        }
        Some(_) | None => None,
    };

    let compound_key = HistoryMetadataCompoundKey {
        place_entry,
        referrer_entry,
        search_query_entry,
    };

    let observation = MetadataObservation {
        document_type: observation.document_type,
        view_time: observation.view_time,
    };

    let now = Timestamp::now().as_millis() as i64;
    let newer_than = now - DEBOUNCE_WINDOW_MS;
    let matching_metadata = compound_key.lookup(tx, newer_than)?;

    // If a matching record exists, update it; otherwise, insert a new one.
    match matching_metadata {
        Some(metadata_id) => {
            // If document_type isn't part of the observation, make sure we don't accidentally erase what's currently set.
            match observation {
                MetadataObservation {
                    document_type: Some(dt),
                    view_time,
                } => {
                    tx.execute_cached(
                        "UPDATE
                            moz_places_metadata
                        SET
                            document_type = :document_type,
                            total_view_time = total_view_time + :view_time_delta,
                            updated_at = :updated_at
                        WHERE id = :id",
                        rusqlite::named_params! {
                            ":id": metadata_id,
                            ":document_type": dt,
                            ":view_time_delta": view_time.unwrap_or(0),
                            ":updated_at": now
                        },
                    )?;
                }
                MetadataObservation {
                    document_type: None,
                    view_time,
                } => {
                    tx.execute_cached(
                        "UPDATE
                            moz_places_metadata
                        SET
                            total_view_time = total_view_time + :view_time_delta,
                            updated_at = :updated_at
                        WHERE id = :id",
                        rusqlite::named_params! {
                            ":id": metadata_id,
                            ":view_time_delta": view_time.unwrap_or(0),
                            ":updated_at": now
                        },
                    )?;
                }
            }
            Ok(())
        }
        None => insert_metadata_in_tx(tx, compound_key, observation, options),
    }
}

fn insert_metadata_in_tx(
    tx: &PlacesTransaction<'_>,
    key: HistoryMetadataCompoundKey,
    observation: MetadataObservation,
    options: NoteHistoryMetadataObservationOptions,
) -> Result<()> {
    let now = Timestamp::now();

    let referrer_place_id = match key.referrer_entry {
        None => None,
        Some(entry) => Some(entry.get_or_insert(tx)?),
    };

    let search_query_id = match key.search_query_entry {
        None => None,
        Some(entry) => Some(entry.get_or_insert(tx)?),
    };

    // Heavy lifting around moz_places inserting (e.g. updating moz_origins, frecency, etc) is performed via triggers.
    // This lets us simply INSERT here without worrying about the rest.
    let place_id = match (key.place_entry, options.if_page_missing) {
        (PlaceEntry::Existing(id), _) => id,
        (PlaceEntry::CreateFor(_, _), HistoryMetadataPageMissingBehavior::IgnoreObservation) => {
            return Ok(())
        }
        (
            ref entry @ PlaceEntry::CreateFor(_, _),
            HistoryMetadataPageMissingBehavior::InsertPage,
        ) => entry.get_or_insert(tx)?,
    };

    let sql = "INSERT INTO moz_places_metadata
        (place_id, created_at, updated_at, total_view_time, search_query_id, document_type, referrer_place_id)
    VALUES
        (:place_id, :created_at, :updated_at, :total_view_time, :search_query_id, :document_type, :referrer_place_id)";

    tx.execute_cached(
        sql,
        &[
            (":place_id", &place_id as &dyn rusqlite::ToSql),
            (":created_at", &now),
            (":updated_at", &now),
            (":search_query_id", &search_query_id),
            (":referrer_place_id", &referrer_place_id),
            (
                ":document_type",
                &observation.document_type.unwrap_or(DocumentType::Regular),
            ),
            (":total_view_time", &observation.view_time.unwrap_or(0)),
        ],
    )?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::api::places_api::ConnectionType;
    use crate::observation::VisitObservation;
    use crate::storage::bookmarks::{
        get_raw_bookmark, insert_bookmark, BookmarkPosition, BookmarkRootGuid, InsertableBookmark,
        InsertableItem,
    };
    use crate::storage::fetch_page_info;
    use crate::storage::history::{
        apply_observation, delete_everything, delete_visits_between, delete_visits_for,
        get_visit_count, url_to_guid,
    };
    use crate::types::VisitType;
    use crate::VisitTransitionSet;
    use std::{thread, time};

    // NOTE: `updated_at` timestamps have millisecond precision, so multiple observations
    // written in the same millisecond can in theory share the same value. To avoid flaky
    // ordering in tests ( since we have `ORDER BY updated_at` in a few queries )
    // this helper sleeps briefly to avoid having overlapping timestamps.
    fn bump_clock() {
        thread::sleep(time::Duration::from_millis(10));
    }

    macro_rules! assert_table_size {
        ($conn:expr, $table:expr, $count:expr) => {
            assert_eq!(
                $count,
                $conn
                    .try_query_one::<i64, _>(
                        format!("SELECT count(*) FROM {table}", table = $table).as_str(),
                        [],
                        true
                    )
                    .expect("select works")
                    .expect("got count")
            );
        };
    }

    macro_rules! assert_history_metadata_record {
        ($record:expr, url $url:expr, total_time $tvt:expr, search_term $search_term:expr, document_type $document_type:expr, referrer_url $referrer_url:expr, title $title:expr, preview_image_url $preview_image_url:expr) => {
            assert_eq!(String::from($url), $record.url, "url must match");
            assert_eq!($tvt, $record.total_view_time, "total_view_time must match");
            assert_eq!($document_type, $record.document_type, "is_media must match");

            let meta = $record.clone(); // ugh... not sure why this `clone` is necessary.

            match $search_term as Option<&str> {
                Some(t) => assert_eq!(
                    String::from(t),
                    meta.search_term.expect("search_term must be Some"),
                    "search_term must match"
                ),
                None => assert_eq!(
                    true,
                    meta.search_term.is_none(),
                    "search_term expected to be None"
                ),
            };
            match $referrer_url as Option<&str> {
                Some(t) => assert_eq!(
                    String::from(t),
                    meta.referrer_url.expect("referrer_url must be Some"),
                    "referrer_url must match"
                ),
                None => assert_eq!(
                    true,
                    meta.referrer_url.is_none(),
                    "referrer_url expected to be None"
                ),
            };
            match $title as Option<&str> {
                Some(t) => assert_eq!(
                    String::from(t),
                    meta.title.expect("title must be Some"),
                    "title must match"
                ),
                None => assert_eq!(true, meta.title.is_none(), "title expected to be None"),
            };
            match $preview_image_url as Option<&str> {
                Some(t) => assert_eq!(
                    String::from(t),
                    meta.preview_image_url
                        .expect("preview_image_url must be Some"),
                    "preview_image_url must match"
                ),
                None => assert_eq!(
                    true,
                    meta.preview_image_url.is_none(),
                    "preview_image_url expected to be None"
                ),
            };
        };
    }

    macro_rules! assert_total_after_observation {
        ($conn:expr, total_records_after $total_records:expr, total_view_time_after $total_view_time:expr, url $url:expr, view_time $view_time:expr, search_term $search_term:expr, document_type $document_type:expr, referrer_url $referrer_url:expr, title $title:expr) => {
            note_observation!($conn,
                url $url,
                view_time $view_time,
                search_term $search_term,
                document_type $document_type,
                referrer_url $referrer_url,
                title $title
            );

            assert_table_size!($conn, "moz_places_metadata", $total_records);
            let updated = get_latest_for_url($conn, &Url::parse($url).unwrap()).unwrap().unwrap();
            assert_eq!($total_view_time, updated.total_view_time, "total view time must match");
        }
    }

    macro_rules! note_observation {
        ($conn:expr, url $url:expr, view_time $view_time:expr, search_term $search_term:expr, document_type $document_type:expr, referrer_url $referrer_url:expr, title $title:expr) => {
            note_observation!(
                $conn,
                NoteHistoryMetadataObservationOptions::new()
                    .if_page_missing(HistoryMetadataPageMissingBehavior::InsertPage),
                url $url,
                view_time $view_time,
                search_term $search_term,
                document_type $document_type,
                referrer_url $referrer_url,
                title $title
            )
        };
        ($conn:expr, $options:expr, url $url:expr, view_time $view_time:expr, search_term $search_term:expr, document_type $document_type:expr, referrer_url $referrer_url:expr, title $title:expr) => {
            apply_metadata_observation(
                $conn,
                HistoryMetadataObservation {
                    url: String::from($url),
                    view_time: $view_time,
                    search_term: $search_term.map(|s: &str| s.to_string()),
                    document_type: $document_type,
                    referrer_url: $referrer_url.map(|s: &str| s.to_string()),
                    title: $title.map(|s: &str| s.to_string()),
                },
                $options,
            )
            .unwrap();
        };
    }

    macro_rules! assert_after_observation {
        ($conn:expr, total_records_after $total_records:expr, total_view_time_after $total_view_time:expr, url $url:expr, view_time $view_time:expr, search_term $search_term:expr, document_type $document_type:expr, referrer_url $referrer_url:expr, title $title:expr, assertion $assertion:expr) => {
            // can set title on creating a new record
            assert_total_after_observation!($conn,
                total_records_after $total_records,
                total_view_time_after $total_view_time,
                url $url,
                view_time $view_time,
                search_term $search_term,
                document_type $document_type,
                referrer_url $referrer_url,
                title $title
            );

            let m = get_latest_for_url(
                $conn,
                &Url::parse(&String::from($url)).unwrap(),
            )
            .unwrap()
            .unwrap();
            #[allow(clippy::redundant_closure_call)]
            $assertion(m);
        }
    }

    #[test]
    fn test_note_observation() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).unwrap();

        assert_table_size!(&conn, "moz_places_metadata", 0);

        assert_total_after_observation!(&conn,
            total_records_after 1,
            total_view_time_after 1500,
            url "http://mozilla.com/",
            view_time Some(1500),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        // debounced! total time was updated
        assert_total_after_observation!(&conn,
            total_records_after 1,
            total_view_time_after 2500,
            url "http://mozilla.com/",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        // different document type, record updated
        assert_total_after_observation!(&conn,
            total_records_after 1,
            total_view_time_after 3500,
            url "http://mozilla.com/",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url None,
            title None
        );

        // referrer set
        assert_total_after_observation!(&conn,
            total_records_after 2,
            total_view_time_after 2000,
            url "http://mozilla.com/",
            view_time Some(2000),
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url Some("https://news.website"),
            title None
        );

        // search term and referrer are set
        assert_total_after_observation!(&conn,
            total_records_after 3,
            total_view_time_after 1100,
            url "http://mozilla.com/",
            view_time Some(1100),
            search_term Some("firefox"),
            document_type Some(DocumentType::Media),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=firefox"),
            title None
        );

        // debounce!
        assert_total_after_observation!(&conn,
            total_records_after 3,
            total_view_time_after 6100,
            url "http://mozilla.com/",
            view_time Some(5000),
            search_term Some("firefox"),
            document_type Some(DocumentType::Media),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=firefox"),
            title None
        );

        // different url now
        assert_total_after_observation!(&conn,
            total_records_after 4,
            total_view_time_after 3000,
            url "http://mozilla.com/another",
            view_time Some(3000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );

        // shared origin for both url and referrer
        assert_total_after_observation!(&conn,
            total_records_after 5,
            total_view_time_after 100000,
            url "https://www.youtube.com/watch?v=tpiyEe_CqB4",
            view_time Some(100000),
            search_term Some("cute cat"),
            document_type Some(DocumentType::Media),
            referrer_url Some("https://www.youtube.com/results?search_query=cute+cat"),
            title None
        );

        // empty search term/referrer url are treated the same as None
        assert_total_after_observation!(&conn,
            total_records_after 6,
            total_view_time_after 80000,
            url "https://www.youtube.com/watch?v=daff43jif3",
            view_time Some(80000),
            search_term Some(""),
            document_type Some(DocumentType::Media),
            referrer_url Some(""),
            title None
        );

        assert_total_after_observation!(&conn,
            total_records_after 6,
            total_view_time_after 90000,
            url "https://www.youtube.com/watch?v=daff43jif3",
            view_time Some(10000),
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url None,
            title None
        );

        // document type recording
        assert_total_after_observation!(&conn,
            total_records_after 7,
            total_view_time_after 0,
            url "https://www.youtube.com/watch?v=fds32fds",
            view_time None,
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url None,
            title None
        );

        // now, update the view time as a separate call
        assert_total_after_observation!(&conn,
            total_records_after 7,
            total_view_time_after 1338,
            url "https://www.youtube.com/watch?v=fds32fds",
            view_time Some(1338),
            search_term None,
            document_type None,
            referrer_url None,
            title None
        );

        // and again, bump the view time
        assert_total_after_observation!(&conn,
            total_records_after 7,
            total_view_time_after 2000,
            url "https://www.youtube.com/watch?v=fds32fds",
            view_time Some(662),
            search_term None,
            document_type None,
            referrer_url None,
            title None
        );

        // now try the other way - record view time first, document type after.
        // and again, bump the view time
        assert_after_observation!(&conn,
            total_records_after 8,
            total_view_time_after 662,
            url "https://www.youtube.com/watch?v=dasdg34d",
            view_time Some(662),
            search_term None,
            document_type None,
            referrer_url None,
            title None,
            assertion |m: HistoryMetadata| { assert_eq!(DocumentType::Regular, m.document_type) }
        );

        assert_after_observation!(&conn,
            total_records_after 8,
            total_view_time_after 662,
            url "https://www.youtube.com/watch?v=dasdg34d",
            view_time None,
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url None,
            title None,
            assertion |m: HistoryMetadata| { assert_eq!(DocumentType::Media, m.document_type) }
        );

        // document type not overwritten (e.g. remains 1, not default 0).
        assert_after_observation!(&conn,
            total_records_after 8,
            total_view_time_after 675,
            url "https://www.youtube.com/watch?v=dasdg34d",
            view_time Some(13),
            search_term None,
            document_type None,
            referrer_url None,
            title None,
            assertion |m: HistoryMetadata| { assert_eq!(DocumentType::Media, m.document_type) }
        );

        // can set title on creating a new record
        assert_after_observation!(&conn,
            total_records_after 9,
            total_view_time_after 13,
            url "https://www.youtube.com/watch?v=dasdsada",
            view_time Some(13),
            search_term None,
            document_type None,
            referrer_url None,
            title Some("hello!"),
            assertion |m: HistoryMetadata| { assert_eq!(Some(String::from("hello!")), m.title) }
        );

        // can not update title after
        assert_after_observation!(&conn,
            total_records_after 9,
            total_view_time_after 26,
            url "https://www.youtube.com/watch?v=dasdsada",
            view_time Some(13),
            search_term None,
            document_type None,
            referrer_url None,
            title Some("world!"),
            assertion |m: HistoryMetadata| { assert_eq!(Some(String::from("hello!")), m.title) }
        );
    }

    #[test]
    fn test_note_observation_invalid_view_time() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "https://www.mozilla.org/",
            view_time None,
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        // 48 hrs is clearly a bad view to observe.
        assert!(apply_metadata_observation(
            &conn,
            HistoryMetadataObservation {
                url: String::from("https://www.mozilla.org"),
                view_time: Some(1000 * 60 * 60 * 24 * 2),
                search_term: None,
                document_type: None,
                referrer_url: None,
                title: None
            },
            NoteHistoryMetadataObservationOptions::new(),
        )
        .is_err());

        // 12 hrs is assumed to be "plausible".
        assert!(apply_metadata_observation(
            &conn,
            HistoryMetadataObservation {
                url: String::from("https://www.mozilla.org"),
                view_time: Some(1000 * 60 * 60 * 12),
                search_term: None,
                document_type: None,
                referrer_url: None,
                title: None
            },
            NoteHistoryMetadataObservationOptions::new(),
        )
        .is_ok());
    }

    #[test]
    fn test_get_between() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        assert_eq!(0, get_between(&conn, 0, 0).unwrap().len());

        let beginning = Timestamp::now().as_millis() as i64;
        note_observation!(&conn,
            url "http://mozilla.com/another",
            view_time Some(3000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );
        let after_meta1 = Timestamp::now().as_millis() as i64;

        assert_eq!(0, get_between(&conn, 0, beginning - 1).unwrap().len());
        assert_eq!(1, get_between(&conn, 0, after_meta1).unwrap().len());

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.com/video/",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url None,
            title None
        );
        let after_meta2 = Timestamp::now().as_millis() as i64;

        assert_eq!(1, get_between(&conn, beginning, after_meta1).unwrap().len());
        assert_eq!(2, get_between(&conn, beginning, after_meta2).unwrap().len());
        assert_eq!(
            1,
            get_between(&conn, after_meta1, after_meta2).unwrap().len()
        );
        assert_eq!(
            0,
            get_between(&conn, after_meta2, after_meta2 + 1)
                .unwrap()
                .len()
        );
    }

    #[test]
    fn test_get_since() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        assert_eq!(0, get_since(&conn, 0).unwrap().len());

        let beginning = Timestamp::now().as_millis() as i64;
        note_observation!(&conn,
            url "http://mozilla.com/another",
            view_time Some(3000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );
        let after_meta1 = Timestamp::now().as_millis() as i64;

        assert_eq!(1, get_since(&conn, 0).unwrap().len());
        assert_eq!(1, get_since(&conn, beginning).unwrap().len());
        assert_eq!(0, get_since(&conn, after_meta1).unwrap().len());

        // thread::sleep(time::Duration::from_millis(50));

        note_observation!(&conn,
            url "http://mozilla.com/video/",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url None,
            title None
        );
        let after_meta2 = Timestamp::now().as_millis() as i64;
        assert_eq!(2, get_since(&conn, beginning).unwrap().len());
        assert_eq!(1, get_since(&conn, after_meta1).unwrap().len());
        assert_eq!(0, get_since(&conn, after_meta2).unwrap().len());
    }

    #[test]
    fn test_get_most_recent_empty() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");
        let rows = get_most_recent(&conn, 5).expect("query ok");
        assert!(rows.is_empty());
    }

    #[test]
    fn test_get_most_recent_orders_and_limits_same_observation() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "https://example.com/1",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "https://example.com/1",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "https://example.com/1",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        // Limiting to 1 should return the most recent entry only.
        let most_recents1 = get_most_recent(&conn, 1).expect("query ok");
        assert_eq!(most_recents1.len(), 1);
        assert_eq!(most_recents1[0].url, "https://example.com/1");

        // Limiting to 3 should also return one entry, since we only have one unique URL.
        let most_recents2 = get_most_recent(&conn, 3).expect("query ok");
        assert_eq!(most_recents2.len(), 1);
        assert_eq!(most_recents2[0].url, "https://example.com/1");

        // Limiting to 10 should also return one entry, since we only have one unique URL.
        let most_recents3 = get_most_recent(&conn, 10).expect("query ok");
        assert_eq!(most_recents3.len(), 1);
        assert_eq!(most_recents3[0].url, "https://example.com/1");
    }

    #[test]
    fn test_get_most_recent_orders_and_limits_different_observations() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "https://example.com/1",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "https://example.com/2",
            view_time Some(20),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "https://example.com/3",
            view_time Some(30),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        // Limiting to 1 should return the most recent entry only.
        let most_recents1 = get_most_recent(&conn, 1).expect("query ok");
        assert_eq!(most_recents1.len(), 1);
        assert_eq!(most_recents1[0].url, "https://example.com/3");

        // Limiting to 2 should return the two most recent entries.
        let most_recents2 = get_most_recent(&conn, 2).expect("query ok");
        assert_eq!(most_recents2.len(), 2);
        assert_eq!(most_recents2[0].url, "https://example.com/3");
        assert_eq!(most_recents2[1].url, "https://example.com/2");

        // Limiting to 10 should return all three entries, in the correct order.
        let most_recents3 = get_most_recent(&conn, 10).expect("query ok");
        assert_eq!(most_recents3.len(), 3);
        assert_eq!(most_recents3[0].url, "https://example.com/3");
        assert_eq!(most_recents3[1].url, "https://example.com/2");
        assert_eq!(most_recents3[2].url, "https://example.com/1");
    }

    #[test]
    fn test_get_most_recent_negative_limit() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "https://example.com/1",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "https://example.com/2",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "https://example.com/3",
            view_time Some(10),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        // Limiting to -1 should return all entries properly ordered.
        let most_recents = get_most_recent(&conn, -1).expect("query ok");
        assert_eq!(most_recents.len(), 3);
        assert_eq!(most_recents[0].url, "https://example.com/3");
        assert_eq!(most_recents[1].url, "https://example.com/2");
        assert_eq!(most_recents[2].url, "https://example.com/1");
    }

    #[test]
    fn test_get_most_recent_search_entries_empty() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");
        let rows = get_most_recent_search_entries(&conn, 5).expect("query ok");
        assert!(rows.is_empty());
    }

    #[test]
    fn test_get_most_recent_search_entries_with_limits_and_same_observation() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        // Limiting to 1 should return the most recent entry where search is not null.
        let most_recents1 = get_most_recent_search_entries(&conn, 1).expect("query ok");
        assert_eq!(most_recents1.len(), 1);
        assert_eq!(most_recents1[0].url, "http://mozilla.org/1/");

        // Limiting to 3 should also return one entry, since we only have one unique URL.
        let most_recents2 = get_most_recent_search_entries(&conn, 3).expect("query ok");
        assert_eq!(most_recents2.len(), 1);
        assert_eq!(most_recents2[0].url, "http://mozilla.org/1/");

        // Limiting to 10 should also return one entry, since we only have one unique URL.
        let most_recents3 = get_most_recent_search_entries(&conn, 10).expect("query ok");
        assert_eq!(most_recents3.len(), 1);
        assert_eq!(most_recents3[0].url, "http://mozilla.org/1/");
    }

    #[test]
    fn test_get_most_recent_search_entries_with_limits_and_different_observations() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/2/",
            view_time Some(20),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/3/",
            view_time None,
            search_term Some("search_term_2"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/4/",
            view_time None,
            search_term Some("search_term_3"),
            document_type None,
            referrer_url None,
            title None
        );

        // Limiting to 1 should return the most recent entry where search is not null.
        let most_recents1 = get_most_recent_search_entries(&conn, 1).expect("query ok");
        assert_eq!(most_recents1.len(), 1);
        assert_eq!(most_recents1[0].url, "http://mozilla.org/4/");

        // Limiting to 2 should return the two most recent entries.
        let most_recents2 = get_most_recent_search_entries(&conn, 2).expect("query ok");
        assert_eq!(most_recents2.len(), 2);
        assert_eq!(most_recents2[0].url, "http://mozilla.org/4/");
        assert_eq!(most_recents2[1].url, "http://mozilla.org/3/");

        // Limiting to 10 should return all three entries, in the correct order.
        let most_recents3 = get_most_recent_search_entries(&conn, 10).expect("query ok");
        assert_eq!(most_recents3.len(), 3);
        assert_eq!(most_recents3[0].url, "http://mozilla.org/4/");
        assert_eq!(most_recents3[1].url, "http://mozilla.org/3/");
        assert_eq!(most_recents3[2].url, "http://mozilla.org/1/");
    }

    #[test]
    fn test_get_most_recent_search_entries_with_negative_limit_with_same_observation() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        // Limiting to -1 should return all entries properly ordered.
        let most_recents = get_most_recent_search_entries(&conn, -1).expect("query ok");
        assert_eq!(most_recents.len(), 1);
        assert_eq!(most_recents[0].url, "http://mozilla.org/1/");
    }

    #[test]
    fn test_get_most_recent_search_entries_with_negative_limit_with_different_observations() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "http://mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/2/",
            view_time None,
            search_term Some("search_term_2"),
            document_type None,
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.org/3/",
            view_time None,
            search_term Some("search_term_3"),
            document_type None,
            referrer_url None,
            title None
        );

        // Limiting to -1 should return all entries properly ordered.
        let most_recents = get_most_recent_search_entries(&conn, -1).expect("query ok");
        assert_eq!(most_recents.len(), 3);
        assert_eq!(most_recents[0].url, "http://mozilla.org/3/");
        assert_eq!(most_recents[1].url, "http://mozilla.org/2/");
        assert_eq!(most_recents[2].url, "http://mozilla.org/1/");
    }

    #[test]
    fn test_get_highlights() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        // Empty database is fine.
        assert_eq!(
            0,
            get_highlights(
                &conn,
                HistoryHighlightWeights {
                    view_time: 1.0,
                    frequency: 1.0
                },
                10
            )
            .unwrap()
            .len()
        );

        // Database with "normal" history but no metadata observations is fine.
        apply_observation(
            &conn,
            VisitObservation::new(
                Url::parse("https://www.reddit.com/r/climbing").expect("Should parse URL"),
            )
            .with_visit_type(VisitType::Link)
            .with_at(Timestamp::now()),
        )
        .expect("Should apply observation");
        assert_eq!(
            0,
            get_highlights(
                &conn,
                HistoryHighlightWeights {
                    view_time: 1.0,
                    frequency: 1.0
                },
                10
            )
            .unwrap()
            .len()
        );

        // three observation to url1, each recording a second of view time.
        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(1000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );

        // one observation to url2 for 3.5s of view time.
        note_observation!(&conn,
            url "http://mozilla.com/2",
            view_time Some(3500),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );

        // The three visits to /2 got "debounced" into a single metadata entry (since they were made in quick succession).
        // We'll calculate the scoring as follows:
        // - for /1: 1.0 * 1/2 + 1.0 * 3000/6500 = 0.9615...
        // - for /2: 1.0 * 1/2 + 1.0 * 3500/6500 = 1.0384...
        // (above, 1/2 means 1 entry out of 2 entries total).

        let even_weights = HistoryHighlightWeights {
            view_time: 1.0,
            frequency: 1.0,
        };
        let highlights1 = get_highlights(&conn, even_weights.clone(), 10).unwrap();
        assert_eq!(2, highlights1.len());
        assert_eq!("http://mozilla.com/2", highlights1[0].url);

        // Since we have an equal amount of metadata entries, providing a very high view_time weight won't change the ranking.
        let frequency_heavy_weights = HistoryHighlightWeights {
            view_time: 1.0,
            frequency: 100.0,
        };
        let highlights2 = get_highlights(&conn, frequency_heavy_weights, 10).unwrap();
        assert_eq!(2, highlights2.len());
        assert_eq!("http://mozilla.com/2", highlights2[0].url);

        // Now, make an observation for url /1, but with a different metadata key.
        // It won't debounce, producing an additional entry for /1.
        // Total view time for /1 is now 3100 (vs 3500 for /2).
        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(100),
            search_term Some("test search"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );

        // Since we now have 2 metadata entries for /1, it ranks higher with even weights.
        let highlights3 = get_highlights(&conn, even_weights, 10).unwrap();
        assert_eq!(2, highlights3.len());
        assert_eq!("http://mozilla.com/1", highlights3[0].url);

        // With a high-enough weight for view_time, we can flip this order.
        // Even though we had 2x entries for /1, it now ranks second due to its lower total view time (3100 vs 3500).
        let view_time_heavy_weights = HistoryHighlightWeights {
            view_time: 6.0,
            frequency: 1.0,
        };
        let highlights4 = get_highlights(&conn, view_time_heavy_weights, 10).unwrap();
        assert_eq!(2, highlights4.len());
        assert_eq!("http://mozilla.com/2", highlights4[0].url);
    }

    #[test]
    fn test_get_highlights_no_viewtime() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        // Make sure we work if the only observations for a URL have a view time of zero.
        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(0),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://news.website/tech"),
            title None
        );
        let highlights = get_highlights(
            &conn,
            HistoryHighlightWeights {
                view_time: 1.0,
                frequency: 1.0,
            },
            2,
        )
        .unwrap();
        assert_eq!(highlights.len(), 1);
        assert_eq!(highlights[0].score, 0.0);
    }

    #[test]
    fn test_query() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");
        let now = Timestamp::now();

        // need a history observation to get a title query working.
        let observation1 = VisitObservation::new(Url::parse("https://www.cbc.ca/news/politics/federal-budget-2021-freeland-zimonjic-1.5991021").unwrap())
                .with_at(now)
                .with_title(Some(String::from("Budget vows to build &#x27;for the long term&#x27; as it promises child care cash, projects massive deficits | CBC News")))
                .with_preview_image_url(Some(Url::parse("https://i.cbc.ca/1.5993583.1618861792!/cpImage/httpImage/image.jpg_gen/derivatives/16x9_620/fedbudget-20210419.jpg").unwrap()))
                .with_is_remote(false)
                .with_visit_type(VisitType::Link);
        apply_observation(&conn, observation1).unwrap();

        note_observation!(
            &conn,
            url "https://www.cbc.ca/news/politics/federal-budget-2021-freeland-zimonjic-1.5991021",
            view_time Some(20000),
            search_term Some("cbc federal budget 2021"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://yandex.ru/search/?text=cbc%20federal%20budget%202021&lr=21512"),
            title None
        );

        note_observation!(
            &conn,
            url "https://stackoverflow.com/questions/37777675/how-to-create-a-formatted-string-out-of-a-literal-in-rust",
            view_time Some(20000),
            search_term Some("rust string format"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://yandex.ru/search/?lr=21512&text=rust%20string%20format"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.sqlite.org/lang_corefunc.html#instr",
            view_time Some(20000),
            search_term Some("sqlite like"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=sqlite+like"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.youtube.com/watch?v=tpiyEe_CqB4",
            view_time Some(100000),
            search_term Some("cute cat"),
            document_type Some(DocumentType::Media),
            referrer_url Some("https://www.youtube.com/results?search_query=cute+cat"),
            title None
        );

        // query by title
        let meta = query(&conn, "child care", 10).expect("query should work");
        assert_eq!(1, meta.len(), "expected exactly one result");
        assert_history_metadata_record!(meta[0],
            url "https://www.cbc.ca/news/politics/federal-budget-2021-freeland-zimonjic-1.5991021",
            total_time 20000,
            search_term Some("cbc federal budget 2021"),
            document_type DocumentType::Regular,
            referrer_url Some("https://yandex.ru/search/?text=cbc%20federal%20budget%202021&lr=21512"),
            title Some("Budget vows to build &#x27;for the long term&#x27; as it promises child care cash, projects massive deficits | CBC News"),
            preview_image_url Some("https://i.cbc.ca/1.5993583.1618861792!/cpImage/httpImage/image.jpg_gen/derivatives/16x9_620/fedbudget-20210419.jpg")
        );

        // query by search term
        let meta = query(&conn, "string format", 10).expect("query should work");
        assert_eq!(1, meta.len(), "expected exactly one result");
        assert_history_metadata_record!(meta[0],
            url "https://stackoverflow.com/questions/37777675/how-to-create-a-formatted-string-out-of-a-literal-in-rust",
            total_time 20000,
            search_term Some("rust string format"),
            document_type DocumentType::Regular,
            referrer_url Some("https://yandex.ru/search/?lr=21512&text=rust%20string%20format"),
            title None,
            preview_image_url None
        );

        // query by url
        let meta = query(&conn, "instr", 10).expect("query should work");
        assert_history_metadata_record!(meta[0],
            url "https://www.sqlite.org/lang_corefunc.html#instr",
            total_time 20000,
            search_term Some("sqlite like"),
            document_type DocumentType::Regular,
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=sqlite+like"),
            title None,
            preview_image_url None
        );

        // by url, referrer domain is different
        let meta = query(&conn, "youtube", 10).expect("query should work");
        assert_history_metadata_record!(meta[0],
            url "https://www.youtube.com/watch?v=tpiyEe_CqB4",
            total_time 100000,
            search_term Some("cute cat"),
            document_type DocumentType::Media,
            referrer_url Some("https://www.youtube.com/results?search_query=cute+cat"),
            title None,
            preview_image_url None
        );
    }

    #[test]
    fn test_delete_metadata() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        // url  |   search_term |   referrer
        // 1    |    1          |   1
        // 1    |    1          |   0
        // 1    |    0          |   1
        // 1    |    0          |   0

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(20000),
            search_term Some("1 with search"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("http://mozilla.com/"),
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(20000),
            search_term Some("1 with search"),
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("http://mozilla.com/"),
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/2",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        note_observation!(&conn,
            url "http://mozilla.com/2",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("http://mozilla.com/"),
            title None
        );

        bump_clock();
        // same observation a bit later:
        note_observation!(&conn,
            url "http://mozilla.com/2",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("http://mozilla.com/"),
            title None
        );

        assert_eq!(6, get_since(&conn, 0).expect("get worked").len());
        delete_metadata(
            &conn,
            &Url::parse("http://mozilla.com/1").unwrap(),
            None,
            None,
        )
        .expect("delete metadata");
        assert_eq!(5, get_since(&conn, 0).expect("get worked").len());

        delete_metadata(
            &conn,
            &Url::parse("http://mozilla.com/1").unwrap(),
            Some(&Url::parse("http://mozilla.com/").unwrap()),
            None,
        )
        .expect("delete metadata");
        assert_eq!(4, get_since(&conn, 0).expect("get worked").len());

        delete_metadata(
            &conn,
            &Url::parse("http://mozilla.com/1").unwrap(),
            Some(&Url::parse("http://mozilla.com/").unwrap()),
            Some("1 with search"),
        )
        .expect("delete metadata");
        assert_eq!(3, get_since(&conn, 0).expect("get worked").len());

        delete_metadata(
            &conn,
            &Url::parse("http://mozilla.com/1").unwrap(),
            None,
            Some("1 with search"),
        )
        .expect("delete metadata");
        assert_eq!(2, get_since(&conn, 0).expect("get worked").len());

        // key doesn't match, do nothing
        delete_metadata(
            &conn,
            &Url::parse("http://mozilla.com/2").unwrap(),
            Some(&Url::parse("http://wrong-referrer.com").unwrap()),
            Some("2 with search"),
        )
        .expect("delete metadata");
        assert_eq!(2, get_since(&conn, 0).expect("get worked").len());
    }

    #[test]
    fn test_delete_older_than() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        let beginning = Timestamp::now().as_millis() as i64;

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );
        let after_meta1 = Timestamp::now().as_millis() as i64;

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.com/2",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.com/3",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );
        let after_meta3 = Timestamp::now().as_millis() as i64;

        // deleting nothing.
        delete_older_than(&conn, beginning).expect("delete worked");
        assert_eq!(3, get_since(&conn, beginning).expect("get worked").len());

        // boundary condition, should only delete the first one.
        delete_older_than(&conn, after_meta1).expect("delete worked");
        assert_eq!(2, get_since(&conn, beginning).expect("get worked").len());
        assert_eq!(
            None,
            get_latest_for_url(&conn, &Url::parse("http://mozilla.com/1").expect("url"))
                .expect("get")
        );

        // delete everything now.
        delete_older_than(&conn, after_meta3).expect("delete worked");
        assert_eq!(0, get_since(&conn, beginning).expect("get worked").len());
    }

    #[test]
    fn test_delete_between() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        let beginning = Timestamp::now().as_millis() as i64;
        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.com/1",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.com/2",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );
        let after_meta2 = Timestamp::now().as_millis() as i64;

        bump_clock();

        note_observation!(&conn,
            url "http://mozilla.com/3",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );
        let after_meta3 = Timestamp::now().as_millis() as i64;

        // deleting meta 3
        delete_between(&conn, after_meta2, after_meta3).expect("delete worked");
        assert_eq!(2, get_since(&conn, beginning).expect("get worked").len());
        assert_eq!(
            None,
            get_latest_for_url(&conn, &Url::parse("http://mozilla.com/3").expect("url"))
                .expect("get")
        );
    }

    #[test]
    fn test_metadata_deletes_do_not_affect_places() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(
            &conn,
            url "https://www.mozilla.org/first/",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );
        let after_meta_added = Timestamp::now().as_millis() as i64;

        // Delete all metadata.
        delete_older_than(&conn, after_meta_added).expect("delete older than worked");

        // Query places. Records there should not have been affected by the delete above.
        // 2 for metadata entries + 1 for referrer url.
        assert_table_size!(&conn, "moz_places", 3);
    }

    #[test]
    fn test_delete_history_also_deletes_metadata_bookmarked() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");
        // Item 1 - bookmarked with regular visits and history metadata
        let url = Url::parse("https://www.mozilla.org/bookmarked").unwrap();
        let bm_guid: SyncGuid = "bookmarkAAAA".into();
        let bm = InsertableBookmark {
            parent_guid: BookmarkRootGuid::Unfiled.into(),
            position: BookmarkPosition::Append,
            date_added: None,
            last_modified: None,
            guid: Some(bm_guid.clone()),
            url: url.clone(),
            title: Some("bookmarked page".to_string()),
        };
        insert_bookmark(&conn, InsertableItem::Bookmark { b: bm }).expect("bookmark should insert");
        let obs = VisitObservation::new(url.clone()).with_visit_type(VisitType::Link);
        apply_observation(&conn, obs).expect("Should apply visit");
        note_observation!(
            &conn,
            url url.to_string(),
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        // Check the DB is what we expect before deleting.
        assert_eq!(
            get_visit_count(&conn, VisitTransitionSet::empty()).unwrap(),
            1
        );
        let place_guid = url_to_guid(&conn, &url)
            .expect("is valid")
            .expect("should exist");

        delete_visits_for(&conn, &place_guid).expect("should work");
        // bookmark must still exist.
        assert!(get_raw_bookmark(&conn, &bm_guid).unwrap().is_some());
        // place exists but has no visits.
        let pi = fetch_page_info(&conn, &url)
            .expect("should work")
            .expect("should exist");
        assert!(pi.last_visit_id.is_none());
        // and no metadata observations.
        assert!(get_latest_for_url(&conn, &url)
            .expect("should work")
            .is_none());
    }

    #[test]
    fn test_delete_history_also_deletes_metadata_not_bookmarked() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");
        // Item is not bookmarked, but has regular visit and a metadata observation.
        let url = Url::parse("https://www.mozilla.org/not-bookmarked").unwrap();
        let obs = VisitObservation::new(url.clone()).with_visit_type(VisitType::Link);
        apply_observation(&conn, obs).expect("Should apply visit");
        note_observation!(
            &conn,
            url url.to_string(),
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        // Check the DB is what we expect before deleting.
        assert_eq!(
            get_visit_count(&conn, VisitTransitionSet::empty()).unwrap(),
            1
        );
        let place_guid = url_to_guid(&conn, &url)
            .expect("is valid")
            .expect("should exist");

        delete_visits_for(&conn, &place_guid).expect("should work");
        // place no longer exists.
        assert!(fetch_page_info(&conn, &url).expect("should work").is_none());
        assert!(get_latest_for_url(&conn, &url)
            .expect("should work")
            .is_none());
    }

    #[test]
    fn test_delete_history_also_deletes_metadata_no_visits() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");
        // Item is not bookmarked, no regular visits but a metadata observation.
        let url = Url::parse("https://www.mozilla.org/no-visits").unwrap();
        note_observation!(
            &conn,
            url url.to_string(),
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        // Check the DB is what we expect before deleting.
        assert_eq!(
            get_visit_count(&conn, VisitTransitionSet::empty()).unwrap(),
            0
        );
        let place_guid = url_to_guid(&conn, &url)
            .expect("is valid")
            .expect("should exist");

        delete_visits_for(&conn, &place_guid).expect("should work");
        // place no longer exists.
        assert!(fetch_page_info(&conn, &url).expect("should work").is_none());
        assert!(get_latest_for_url(&conn, &url)
            .expect("should work")
            .is_none());
    }

    #[test]
    fn test_delete_between_also_deletes_metadata() -> Result<()> {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        let now = Timestamp::now();
        let url = Url::parse("https://www.mozilla.org/").unwrap();
        let other_url =
            Url::parse("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox")
                .unwrap();
        let start_timestamp = Timestamp(now.as_millis() - 1000_u64);
        let end_timestamp = Timestamp(now.as_millis() + 1000_u64);
        let observation1 = VisitObservation::new(url.clone())
            .with_at(start_timestamp)
            .with_title(Some(String::from("Test page 0")))
            .with_is_remote(false)
            .with_visit_type(VisitType::Link);

        let observation2 = VisitObservation::new(other_url)
            .with_at(end_timestamp)
            .with_title(Some(String::from("Test page 1")))
            .with_is_remote(false)
            .with_visit_type(VisitType::Link);

        apply_observation(&conn, observation1).expect("Should apply visit");
        apply_observation(&conn, observation2).expect("Should apply visit");

        note_observation!(
            &conn,
            url "https://www.mozilla.org/",
            view_time Some(20000),
            search_term Some("mozilla firefox"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );
        assert_eq!(
            "https://www.mozilla.org/",
            get_latest_for_url(&conn, &url)?.unwrap().url
        );
        delete_visits_between(&conn, start_timestamp, end_timestamp)?;
        assert_eq!(None, get_latest_for_url(&conn, &url)?);
        Ok(())
    }

    #[test]
    fn test_places_delete_triggers_with_bookmarks() {
        // The cleanup functionality lives as a TRIGGER in `create_shared_triggers`.
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        let now = Timestamp::now();
        let url = Url::parse("https://www.mozilla.org/").unwrap();
        let parent_url =
            Url::parse("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox")
                .unwrap();

        let observation1 = VisitObservation::new(url.clone())
            .with_at(now)
            .with_title(Some(String::from("Test page 0")))
            .with_is_remote(false)
            .with_visit_type(VisitType::Link);

        let observation2 = VisitObservation::new(parent_url.clone())
            .with_at(now)
            .with_title(Some(String::from("Test page 1")))
            .with_is_remote(false)
            .with_visit_type(VisitType::Link);

        apply_observation(&conn, observation1).expect("Should apply visit");
        apply_observation(&conn, observation2).expect("Should apply visit");

        assert_table_size!(&conn, "moz_bookmarks", 5);

        // add bookmark for the page we have a metadata entry
        insert_bookmark(
            &conn,
            InsertableItem::Bookmark {
                b: InsertableBookmark {
                    parent_guid: BookmarkRootGuid::Unfiled.into(),
                    position: BookmarkPosition::Append,
                    date_added: None,
                    last_modified: None,
                    guid: Some(SyncGuid::from("cccccccccccc")),
                    url,
                    title: None,
                },
            },
        )
        .expect("bookmark insert worked");

        // add another bookmark to the "parent" of our metadata entry
        insert_bookmark(
            &conn,
            InsertableItem::Bookmark {
                b: InsertableBookmark {
                    parent_guid: BookmarkRootGuid::Unfiled.into(),
                    position: BookmarkPosition::Append,
                    date_added: None,
                    last_modified: None,
                    guid: Some(SyncGuid::from("ccccccccccca")),
                    url: parent_url,
                    title: None,
                },
            },
        )
        .expect("bookmark insert worked");

        assert_table_size!(&conn, "moz_bookmarks", 7);
        assert_table_size!(&conn, "moz_origins", 2);

        note_observation!(
            &conn,
            url "https://www.mozilla.org/",
            view_time Some(20000),
            search_term Some("mozilla firefox"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        assert_table_size!(&conn, "moz_origins", 2);

        // this somehow deletes 1 origin record, and our metadata
        delete_everything(&conn).expect("places wipe succeeds");

        assert_table_size!(&conn, "moz_places_metadata", 0);
        assert_table_size!(&conn, "moz_places_metadata_search_queries", 0);
    }

    #[test]
    fn test_places_delete_triggers() {
        // The cleanup functionality lives as a TRIGGER in `create_shared_triggers`.
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        let now = Timestamp::now();
        let observation1 = VisitObservation::new(Url::parse("https://www.mozilla.org/").unwrap())
            .with_at(now)
            .with_title(Some(String::from("Test page 1")))
            .with_is_remote(false)
            .with_visit_type(VisitType::Link);
        let observation2 =
            VisitObservation::new(Url::parse("https://www.mozilla.org/another/").unwrap())
                .with_at(Timestamp(now.as_millis() + 10000))
                .with_title(Some(String::from("Test page 3")))
                .with_is_remote(false)
                .with_visit_type(VisitType::Link);
        let observation3 =
            VisitObservation::new(Url::parse("https://www.mozilla.org/first/").unwrap())
                .with_at(Timestamp(now.as_millis() - 10000))
                .with_title(Some(String::from("Test page 0")))
                .with_is_remote(true)
                .with_visit_type(VisitType::Link);
        apply_observation(&conn, observation1).expect("Should apply visit");
        apply_observation(&conn, observation2).expect("Should apply visit");
        apply_observation(&conn, observation3).expect("Should apply visit");

        note_observation!(
            &conn,
            url "https://www.mozilla.org/first/",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/",
            view_time Some(20000),
            search_term Some("mozilla"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/",
            view_time Some(25000),
            search_term Some("firefox"),
            document_type Some(DocumentType::Media),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/another/",
            view_time Some(20000),
            search_term Some("mozilla"),
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        // double-check that we have the 'firefox' search query entry.
        assert!(conn
            .try_query_one::<i64, _>(
                "SELECT id FROM moz_places_metadata_search_queries WHERE term = :term",
                rusqlite::named_params! { ":term": "firefox" },
                true
            )
            .expect("select works")
            .is_some());

        // Delete our first page & its visits. Note that /another/ page will remain in place.
        delete_visits_between(
            &conn,
            Timestamp(now.as_millis() - 1000),
            Timestamp(now.as_millis() + 1000),
        )
        .expect("delete worked");

        let meta1 =
            get_latest_for_url(&conn, &Url::parse("https://www.mozilla.org/").expect("url"))
                .expect("get worked");
        let meta2 = get_latest_for_url(
            &conn,
            &Url::parse("https://www.mozilla.org/another/").expect("url"),
        )
        .expect("get worked");

        assert!(meta1.is_none(), "expected metadata to have been deleted");
        // Verify that if a history metadata entry was entered **after** the visit
        // then we delete the range of the metadata, and not the visit. The metadata
        // is still deleted
        assert!(meta2.is_none(), "expected metadata to been deleted");

        // The 'mozilla' search query entry is deleted since the delete cascades.
        assert!(
            conn.try_query_one::<i64, _>(
                "SELECT id FROM moz_places_metadata_search_queries WHERE term = :term",
                rusqlite::named_params! { ":term": "mozilla" },
                true
            )
            .expect("select works")
            .is_none(),
            "search_query records with related metadata should have been deleted"
        );

        // don't have the 'firefox' search query entry either, nothing points to it.
        assert!(
            conn.try_query_one::<i64, _>(
                "SELECT id FROM moz_places_metadata_search_queries WHERE term = :term",
                rusqlite::named_params! { ":term": "firefox" },
                true
            )
            .expect("select works")
            .is_none(),
            "search_query records without related metadata should have been deleted"
        );

        // now, let's wipe places, and make sure none of the metadata stuff remains.
        delete_everything(&conn).expect("places wipe succeeds");

        assert_table_size!(&conn, "moz_places_metadata", 0);
        assert_table_size!(&conn, "moz_places_metadata_search_queries", 0);
    }

    #[test]
    fn test_delete_all_metadata_for_search() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(&conn,
            url "https://www.mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        note_observation!(&conn,
            url "https://www.mozilla.org/2/",
            view_time None,
            search_term Some("search_term_2"),
            document_type None,
            referrer_url None,
            title None
        );

        assert_table_size!(&conn, "moz_places_metadata", 2);
        assert_table_size!(&conn, "moz_places_metadata_search_queries", 2);

        delete_all_metadata_for_search(&conn).expect("query ok");

        assert_table_size!(&conn, "moz_places_metadata", 0);
        assert_table_size!(&conn, "moz_places_metadata_search_queries", 0);
    }

    #[test]
    fn test_delete_all_metadata_for_search_only_deletes_search_metadata() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        // url  |   search_term |   referrer
        // 1    |    1          |   0
        // 1    |    0          |   1
        // 1    |    1          |   0
        // 1    |    0          |   1

        note_observation!(&conn,
            url "https://www.mozilla.org/1/",
            view_time None,
            search_term Some("search_term_1"),
            document_type None,
            referrer_url None,
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/2/",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Media),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        note_observation!(&conn,
            url "https://www.mozilla.org/3/",
            view_time None,
            search_term Some("search_term_2"),
            document_type None,
            referrer_url None,
            title None
        );

        note_observation!(
            &conn,
            url "https://www.mozilla.org/4/",
            view_time Some(20000),
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url Some("https://www.google.com/search?client=firefox-b-d&q=mozilla+firefox"),
            title None
        );

        assert_eq!(4, get_since(&conn, 0).expect("get worked").len());

        assert_table_size!(&conn, "moz_places_metadata", 4);
        assert_table_size!(&conn, "moz_places_metadata_search_queries", 2);

        delete_all_metadata_for_search(&conn).expect("query ok");

        assert_table_size!(&conn, "moz_places_metadata", 2);
        assert_table_size!(&conn, "moz_places_metadata_search_queries", 0);
    }

    #[test]
    fn test_if_page_missing_behavior() {
        let conn = PlacesDb::open_in_memory(ConnectionType::ReadWrite).expect("memory db");

        note_observation!(
            &conn,
            NoteHistoryMetadataObservationOptions::new()
                .if_page_missing(HistoryMetadataPageMissingBehavior::IgnoreObservation),
            url "https://www.example.com/",
            view_time None,
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        let observations = get_since(&conn, 0).expect("should get all metadata observations");
        assert_eq!(observations, &[]);

        let visit_observation =
            VisitObservation::new(Url::parse("https://www.example.com/").unwrap())
                .with_at(Timestamp::now());
        apply_observation(&conn, visit_observation).expect("should apply visit observation");

        note_observation!(
            &conn,
            NoteHistoryMetadataObservationOptions::new()
                .if_page_missing(HistoryMetadataPageMissingBehavior::IgnoreObservation),
            url "https://www.example.com/",
            view_time None,
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        let observations = get_since(&conn, 0).expect("should get all metadata observations");
        assert_eq!(
            observations
                .into_iter()
                .map(|m| m.url)
                .collect::<Vec<String>>(),
            &["https://www.example.com/"]
        );

        note_observation!(
            &conn,
            NoteHistoryMetadataObservationOptions::new()
                .if_page_missing(HistoryMetadataPageMissingBehavior::InsertPage),
            url "https://www.example.org/",
            view_time None,
            search_term None,
            document_type Some(DocumentType::Regular),
            referrer_url None,
            title None
        );

        let observations = get_since(&conn, 0).expect("should get all metadata observations");
        assert_eq!(
            observations
                .into_iter()
                .map(|m| m.url)
                .collect::<Vec<String>>(),
            &[
                "https://www.example.org/", // Newest first.
                "https://www.example.com/",
            ],
        );
    }
}
