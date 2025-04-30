/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use std::{cell::OnceCell, path::Path, sync::Arc};

use interrupt_support::{SqlInterruptHandle, SqlInterruptScope};
use parking_lot::{Mutex, MutexGuard};
use rusqlite::{
    named_params,
    types::{FromSql, ToSql},
    Connection,
};
use sql_support::{open_database, repeat_sql_vars, ConnExt};

use crate::{
    config::{SuggestGlobalConfig, SuggestProviderConfig},
    error::RusqliteResultExt,
    fakespot,
    geoname::GeonameCache,
    pocket::{split_keyword, KeywordConfidence},
    provider::{AmpMatchingStrategy, SuggestionProvider},
    query::{full_keywords_to_fts_content, FtsQuery},
    rs::{
        DownloadedAmoSuggestion, DownloadedAmpSuggestion, DownloadedDynamicRecord,
        DownloadedDynamicSuggestion, DownloadedFakespotSuggestion, DownloadedMdnSuggestion,
        DownloadedPocketSuggestion, DownloadedWikipediaSuggestion, Record, SuggestRecordId,
    },
    schema::{clear_database, SuggestConnectionInitializer},
    suggestion::{cook_raw_suggestion_url, FtsMatchInfo, Suggestion},
    util::full_keyword,
    weather::WeatherCache,
    Result, SuggestionQuery,
};

/// The metadata key whose value is a JSON string encoding a
/// `SuggestGlobalConfig`, which contains global Suggest configuration data.
pub const GLOBAL_CONFIG_META_KEY: &str = "global_config";
/// Prefix of metadata keys whose values are JSON strings encoding
/// `SuggestProviderConfig`, which contains per-provider configuration data. The
/// full key is this prefix plus the `SuggestionProvider` value as a u8.
pub const PROVIDER_CONFIG_META_KEY_PREFIX: &str = "provider_config_";

// Default value when Suggestion does not have a value for score
pub const DEFAULT_SUGGESTION_SCORE: f64 = 0.2;

/// The database connection type.
#[derive(Clone, Copy)]
pub(crate) enum ConnectionType {
    ReadOnly,
    ReadWrite,
}

#[derive(Default, Clone)]
pub struct Sqlite3Extension {
    pub library: String,
    pub entry_point: Option<String>,
}

/// A thread-safe wrapper around an SQLite connection to the Suggest database,
/// and its interrupt handle.
pub(crate) struct SuggestDb {
    pub conn: Mutex<Connection>,

    /// An object that's used to interrupt an ongoing database operation.
    ///
    /// When this handle is interrupted, the thread that's currently accessing
    /// the database will be told to stop and release the `conn` lock as soon
    /// as possible.
    pub interrupt_handle: Arc<SqlInterruptHandle>,
}

impl SuggestDb {
    /// Opens a read-only or read-write connection to a Suggest database at the
    /// given path.
    pub fn open(
        path: impl AsRef<Path>,
        extensions_to_load: &[Sqlite3Extension],
        type_: ConnectionType,
    ) -> Result<Self> {
        let conn = open_database::open_database_with_flags(
            path,
            match type_ {
                ConnectionType::ReadWrite => open_database::read_write_flags(),
                ConnectionType::ReadOnly => open_database::read_only_flags(),
            },
            &SuggestConnectionInitializer::new(extensions_to_load),
        )?;
        Ok(Self::with_connection(conn))
    }

    fn with_connection(conn: Connection) -> Self {
        let interrupt_handle = Arc::new(SqlInterruptHandle::new(&conn));
        Self {
            conn: Mutex::new(conn),
            interrupt_handle,
        }
    }

    /// Accesses the Suggest database for reading.
    pub fn read<T>(&self, op: impl FnOnce(&SuggestDao) -> Result<T>) -> Result<T> {
        let conn = self.conn.lock();
        let scope = self.interrupt_handle.begin_interrupt_scope()?;
        let dao = SuggestDao::new(&conn, &scope);
        op(&dao)
    }

    /// Accesses the Suggest database in a transaction for reading and writing.
    pub fn write<T>(&self, op: impl FnOnce(&mut SuggestDao) -> Result<T>) -> Result<T> {
        let mut conn = self.conn.lock();
        let scope = self.interrupt_handle.begin_interrupt_scope()?;
        let tx = conn.transaction()?;
        let mut dao = SuggestDao::new(&tx, &scope);
        let result = op(&mut dao)?;
        tx.commit()?;
        Ok(result)
    }

    /// Create a new write scope.
    ///
    /// This enables performing multiple `write()` calls with the same shared interrupt scope.
    /// This is important for things like ingestion, where you want the operation to be interrupted
    /// if [Self::interrupt_handle::interrupt] is called after the operation starts.  Calling
    /// [Self::write] multiple times during the operation risks missing a call that happens after
    /// between those calls.
    pub fn write_scope(&self) -> Result<WriteScope> {
        Ok(WriteScope {
            conn: self.conn.lock(),
            scope: self.interrupt_handle.begin_interrupt_scope()?,
        })
    }
}

pub(crate) struct WriteScope<'a> {
    pub conn: MutexGuard<'a, Connection>,
    pub scope: SqlInterruptScope,
}

impl WriteScope<'_> {
    /// Accesses the Suggest database in a transaction for reading and writing.
    pub fn write<T>(&mut self, op: impl FnOnce(&mut SuggestDao) -> Result<T>) -> Result<T> {
        let tx = self.conn.transaction()?;
        let mut dao = SuggestDao::new(&tx, &self.scope);
        let result = op(&mut dao)?;
        tx.commit()?;
        Ok(result)
    }

    /// Accesses the Suggest database in a transaction for reading only
    pub fn read<T>(&mut self, op: impl FnOnce(&SuggestDao) -> Result<T>) -> Result<T> {
        let tx = self.conn.transaction()?;
        let dao = SuggestDao::new(&tx, &self.scope);
        let result = op(&dao)?;
        tx.commit()?;
        Ok(result)
    }

    pub fn err_if_interrupted(&self) -> Result<()> {
        Ok(self.scope.err_if_interrupted()?)
    }
}

/// A data access object (DAO) that wraps a connection to the Suggest database
/// with methods for reading and writing suggestions, icons, and metadata.
///
/// Methods that only read from the database take an immutable reference to
/// `self` (`&self`), and methods that write to the database take a mutable
/// reference (`&mut self`).
pub(crate) struct SuggestDao<'a> {
    pub conn: &'a Connection,
    pub scope: &'a SqlInterruptScope,
    pub weather_cache: OnceCell<WeatherCache>,
    pub geoname_cache: OnceCell<GeonameCache>,
}

impl<'a> SuggestDao<'a> {
    fn new(conn: &'a Connection, scope: &'a SqlInterruptScope) -> Self {
        Self {
            conn,
            scope,
            weather_cache: std::cell::OnceCell::new(),
            geoname_cache: std::cell::OnceCell::new(),
        }
    }

    // =============== High level API ===============
    //
    //  These methods combine several low-level calls into one logical operation.

    pub fn delete_record_data(&mut self, record_id: &SuggestRecordId) -> Result<()> {
        // Drop either the icon or suggestions, records only contain one or the other
        match record_id.as_icon_id() {
            Some(icon_id) => self.drop_icon(icon_id)?,
            None => self.drop_suggestions(record_id)?,
        };
        Ok(())
    }

    // =============== Low level API ===============
    //
    //  These methods implement CRUD operations

    pub fn get_ingested_records(&self) -> Result<Vec<IngestedRecord>> {
        let mut stmt = self
            .conn
            .prepare_cached("SELECT id, collection, type, last_modified FROM ingested_records")?;
        let rows = stmt.query_and_then((), IngestedRecord::from_row)?;
        rows.collect()
    }

    pub fn update_ingested_records(
        &mut self,
        collection: &str,
        new_records: &[&Record],
        updated_records: &[&Record],
        deleted_records: &[&IngestedRecord],
    ) -> Result<()> {
        let mut delete_stmt = self
            .conn
            .prepare_cached("DELETE FROM ingested_records WHERE collection = ? AND id = ?")?;
        for deleted in deleted_records {
            delete_stmt.execute((collection, deleted.id.as_str()))?;
        }

        let mut insert_stmt = self.conn.prepare_cached(
            "INSERT OR REPLACE INTO ingested_records(id, collection, type, last_modified) VALUES(?, ?, ?, ?)",
        )?;
        for record in new_records.iter().chain(updated_records) {
            insert_stmt.execute((
                record.id.as_str(),
                collection,
                record.record_type().as_str(),
                record.last_modified,
            ))?;
        }
        Ok(())
    }

    /// Update the DB so that we re-ingest all records on the next ingestion.
    ///
    /// We hack this by setting the last_modified time to 1 so that the next time around we always
    /// re-ingest the record.
    pub fn force_reingest(&mut self) -> Result<()> {
        self.conn
            .prepare_cached("UPDATE ingested_records SET last_modified=1")?
            .execute(())?;
        Ok(())
    }

    pub fn suggestions_table_empty(&self) -> Result<bool> {
        Ok(self
            .conn
            .query_one::<bool>("SELECT NOT EXISTS (SELECT 1 FROM suggestions)")?)
    }

    /// Fetches Suggestions of type Amp provider that match the given query
    pub fn fetch_amp_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let strategy = query
            .provider_constraints
            .as_ref()
            .and_then(|c| c.amp_alternative_matching.as_ref());
        match strategy {
            None => self.fetch_amp_suggestions_using_keywords(query, true),
            Some(AmpMatchingStrategy::NoKeywordExpansion) => {
                self.fetch_amp_suggestions_using_keywords(query, false)
            }
            Some(AmpMatchingStrategy::FtsAgainstFullKeywords) => {
                self.fetch_amp_suggestions_using_fts(query, "full_keywords")
            }
            Some(AmpMatchingStrategy::FtsAgainstTitle) => {
                self.fetch_amp_suggestions_using_fts(query, "title")
            }
        }
    }

    pub fn fetch_amp_suggestions_using_keywords(
        &self,
        query: &SuggestionQuery,
        allow_keyword_expansion: bool,
    ) -> Result<Vec<Suggestion>> {
        let keyword_lowercased = &query.keyword.to_lowercase();
        let where_extra = if allow_keyword_expansion {
            ""
        } else {
            "AND INSTR(CONCAT(fk.full_keyword, ' '), k.keyword) != 0"
        };
        let suggestions = self.conn.query_rows_and_then_cached(
            &format!(
                r#"
                SELECT
                  s.id,
                  k.rank,
                  s.title,
                  s.url,
                  s.provider,
                  s.score,
                  fk.full_keyword
                FROM
                  suggestions s
                JOIN
                  keywords k
                  ON k.suggestion_id = s.id
                LEFT JOIN
                  full_keywords fk
                  ON k.full_keyword_id = fk.id
                WHERE
                  s.provider = :provider
                  AND k.keyword = :keyword
                  {where_extra}
                  AND NOT EXISTS (
                    -- For AMP suggestions dismissed with the deprecated URL-based dismissal API,
                    -- `dismissed_suggestions.url` will be the suggestion URL. With the new
                    -- `Suggestion`-based API, it will be the full keyword.
                    SELECT 1 FROM dismissed_suggestions WHERE url IN (fk.full_keyword, s.url)
                  )
                "#
            ),
            named_params! {
                ":keyword": keyword_lowercased,
                ":provider": SuggestionProvider::Amp,
            },
            |row| -> Result<Suggestion> {
                let suggestion_id: i64 = row.get("id")?;
                let title = row.get("title")?;
                let raw_url: String = row.get("url")?;
                let score: f64 = row.get("score")?;
                let full_keyword_from_db: Option<String> = row.get("full_keyword")?;

                self.conn.query_row_and_then(
                    r#"
                    SELECT
                      amp.advertiser,
                      amp.block_id,
                      amp.iab_category,
                      amp.impression_url,
                      amp.click_url,
                      i.data AS icon,
                      i.mimetype AS icon_mimetype
                    FROM
                      amp_custom_details amp
                    LEFT JOIN
                      icons i ON amp.icon_id = i.id
                    WHERE
                      amp.suggestion_id = :suggestion_id
                    "#,
                    named_params! {
                        ":suggestion_id": suggestion_id
                    },
                    |row| {
                        let cooked_url = cook_raw_suggestion_url(&raw_url);
                        let raw_click_url = row.get::<_, String>("click_url")?;
                        let cooked_click_url = cook_raw_suggestion_url(&raw_click_url);

                        Ok(Suggestion::Amp {
                            block_id: row.get("block_id")?,
                            advertiser: row.get("advertiser")?,
                            iab_category: row.get("iab_category")?,
                            title,
                            url: cooked_url,
                            raw_url,
                            full_keyword: full_keyword_from_db.unwrap_or_default(),
                            icon: row.get("icon")?,
                            icon_mimetype: row.get("icon_mimetype")?,
                            impression_url: row.get("impression_url")?,
                            click_url: cooked_click_url,
                            raw_click_url,
                            score,
                            fts_match_info: None,
                        })
                    },
                )
            },
        )?;
        Ok(suggestions)
    }

    pub fn fetch_amp_suggestions_using_fts(
        &self,
        query: &SuggestionQuery,
        fts_column: &str,
    ) -> Result<Vec<Suggestion>> {
        let fts_query = query.fts_query();
        let match_arg = &fts_query.match_arg;
        let suggestions = self.conn.query_rows_and_then_cached(
            &format!(
                r#"
                SELECT
                  s.id,
                  s.title,
                  s.url,
                  s.provider,
                  s.score
                FROM
                  suggestions s
                JOIN
                  amp_fts fts
                  ON fts.rowid = s.id
                WHERE
                  s.provider = :provider
                  AND amp_fts match '{fts_column}: {match_arg}'
                AND NOT EXISTS (SELECT 1 FROM dismissed_suggestions WHERE url=s.url)
                ORDER BY rank
                LIMIT 1
                "#
            ),
            named_params! {
                ":provider": SuggestionProvider::Amp,
            },
            |row| -> Result<Suggestion> {
                let suggestion_id: i64 = row.get("id")?;
                let title: String = row.get("title")?;
                let raw_url: String = row.get("url")?;
                let score: f64 = row.get("score")?;

                self.conn.query_row_and_then(
                    r#"
                    SELECT
                      amp.advertiser,
                      amp.block_id,
                      amp.iab_category,
                      amp.impression_url,
                      amp.click_url,
                      i.data AS icon,
                      i.mimetype AS icon_mimetype
                    FROM
                      amp_custom_details amp
                    LEFT JOIN
                      icons i ON amp.icon_id = i.id
                    WHERE
                      amp.suggestion_id = :suggestion_id
                    "#,
                    named_params! {
                        ":suggestion_id": suggestion_id
                    },
                    |row| {
                        let cooked_url = cook_raw_suggestion_url(&raw_url);
                        let raw_click_url = row.get::<_, String>("click_url")?;
                        let cooked_click_url = cook_raw_suggestion_url(&raw_click_url);
                        let match_info = self.fetch_amp_fts_match_info(
                            &fts_query,
                            suggestion_id,
                            fts_column,
                            &title,
                        )?;

                        Ok(Suggestion::Amp {
                            block_id: row.get("block_id")?,
                            advertiser: row.get("advertiser")?,
                            iab_category: row.get("iab_category")?,
                            title,
                            url: cooked_url,
                            raw_url,
                            full_keyword: query.keyword.clone(),
                            icon: row.get("icon")?,
                            icon_mimetype: row.get("icon_mimetype")?,
                            impression_url: row.get("impression_url")?,
                            click_url: cooked_click_url,
                            raw_click_url,
                            score,
                            fts_match_info: Some(match_info),
                        })
                    },
                )
            },
        )?;
        Ok(suggestions)
    }

    fn fetch_amp_fts_match_info(
        &self,
        fts_query: &FtsQuery<'_>,
        suggestion_id: i64,
        fts_column: &str,
        title: &str,
    ) -> Result<FtsMatchInfo> {
        let fts_content = match fts_column {
            "title" => title.to_lowercase(),
            "full_keywords" => {
                let full_keyword_list: Vec<String> = self.conn.query_rows_and_then(
                    "
                    SELECT fk.full_keyword
                    FROM full_keywords fk
                    JOIN keywords k on fk.id == k.full_keyword_id
                    WHERE k.suggestion_id = ?
                    ",
                    (suggestion_id,),
                    |row| row.get(0),
                )?;
                full_keywords_to_fts_content(full_keyword_list.iter().map(String::as_str))
            }
            // fts_column comes from the code above and we know there's only 2 possibilities
            _ => unreachable!(),
        };

        let prefix = if fts_query.is_prefix_query {
            // If the query was a prefix match query then test if the query without the prefix
            // match would have also matched.  If not, then this counts as a prefix match.
            let sql = "SELECT 1 FROM amp_fts WHERE rowid = ? AND amp_fts MATCH ?";
            let params = (&suggestion_id, &fts_query.match_arg_without_prefix_match);
            !self.conn.exists(sql, params)?
        } else {
            // If not, then it definitely wasn't a prefix match
            false
        };

        Ok(FtsMatchInfo {
            prefix,
            stemming: fts_query.match_required_stemming(&fts_content),
        })
    }

    /// Fetches Suggestions of type Wikipedia provider that match the given query
    pub fn fetch_wikipedia_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let keyword_lowercased = &query.keyword.to_lowercase();
        let suggestions = self.conn.query_rows_and_then_cached(
            r#"
            SELECT
              s.id,
              k.rank,
              s.title,
              s.url
            FROM
              suggestions s
            JOIN
              keywords k
              ON k.suggestion_id = s.id
            WHERE
              s.provider = :provider
              AND k.keyword = :keyword
              AND NOT EXISTS (SELECT 1 FROM dismissed_suggestions WHERE url=s.url)
            "#,
            named_params! {
                ":keyword": keyword_lowercased,
                ":provider": SuggestionProvider::Wikipedia
            },
            |row| -> Result<Suggestion> {
                let suggestion_id: i64 = row.get("id")?;
                let title = row.get("title")?;
                let raw_url = row.get::<_, String>("url")?;

                let keywords: Vec<String> = self.conn.query_rows_and_then_cached(
                    "SELECT keyword FROM keywords
                     WHERE suggestion_id = :suggestion_id AND rank >= :rank
                     ORDER BY rank ASC",
                    named_params! {
                        ":suggestion_id": suggestion_id,
                        ":rank": row.get::<_, i64>("rank")?,
                    },
                    |row| row.get(0),
                )?;
                let (icon, icon_mimetype) = self
                    .conn
                    .try_query_row(
                        "SELECT i.data, i.mimetype
                     FROM icons i
                     JOIN wikipedia_custom_details s ON s.icon_id = i.id
                     WHERE s.suggestion_id = :suggestion_id
                     LIMIT 1",
                        named_params! {
                            ":suggestion_id": suggestion_id
                        },
                        |row| -> Result<_> {
                            Ok((
                                row.get::<_, Option<Vec<u8>>>(0)?,
                                row.get::<_, Option<String>>(1)?,
                            ))
                        },
                        true,
                    )?
                    .unwrap_or((None, None));

                Ok(Suggestion::Wikipedia {
                    title,
                    url: raw_url,
                    full_keyword: full_keyword(keyword_lowercased, &keywords),
                    icon,
                    icon_mimetype,
                })
            },
        )?;
        Ok(suggestions)
    }

    /// Query for suggestions using the keyword prefix and provider
    fn map_prefix_keywords<T>(
        &self,
        query: &SuggestionQuery,
        provider: &SuggestionProvider,
        mut mapper: impl FnMut(&rusqlite::Row, &str) -> Result<T>,
    ) -> Result<Vec<T>> {
        let keyword_lowercased = &query.keyword.to_lowercase();
        let (keyword_prefix, keyword_suffix) = split_keyword(keyword_lowercased);
        let suggestions_limit = query.limit.unwrap_or(-1);
        self.conn.query_rows_and_then_cached(
            r#"
                SELECT
                  s.id,
                  MAX(k.rank) AS rank,
                  s.title,
                  s.url,
                  s.provider,
                  s.score,
                  k.keyword_suffix
                FROM
                  suggestions s
                JOIN
                  prefix_keywords k
                  ON k.suggestion_id = s.id
                WHERE
                  k.keyword_prefix = :keyword_prefix
                  AND (k.keyword_suffix BETWEEN :keyword_suffix AND :keyword_suffix || x'FFFF')
                  AND s.provider = :provider
                  AND NOT EXISTS (SELECT 1 FROM dismissed_suggestions WHERE url=s.url)
                GROUP BY
                  s.id
                ORDER BY
                  s.score DESC,
                  rank DESC
                LIMIT
                  :suggestions_limit
                "#,
            &[
                (":keyword_prefix", &keyword_prefix as &dyn ToSql),
                (":keyword_suffix", &keyword_suffix as &dyn ToSql),
                (":provider", provider as &dyn ToSql),
                (":suggestions_limit", &suggestions_limit as &dyn ToSql),
            ],
            |row| mapper(row, keyword_suffix),
        )
    }

    /// Fetches Suggestions of type Amo provider that match the given query
    pub fn fetch_amo_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let suggestions = self
            .map_prefix_keywords(
                query,
                &SuggestionProvider::Amo,
                |row, keyword_suffix| -> Result<Option<Suggestion>> {
                    let suggestion_id: i64 = row.get("id")?;
                    let title = row.get("title")?;
                    let raw_url = row.get::<_, String>("url")?;
                    let score = row.get::<_, f64>("score")?;

                    let full_suffix = row.get::<_, String>("keyword_suffix")?;
                    full_suffix
                        .starts_with(keyword_suffix)
                        .then(|| {
                            self.conn.query_row_and_then(
                                r#"
                                SELECT
                                  amo.description,
                                  amo.guid,
                                  amo.rating,
                                  amo.icon_url,
                                  amo.number_of_ratings
                                FROM
                                  amo_custom_details amo
                                WHERE
                                  amo.suggestion_id = :suggestion_id
                                "#,
                                named_params! {
                                    ":suggestion_id": suggestion_id
                                },
                                |row| {
                                    Ok(Suggestion::Amo {
                                        title,
                                        url: raw_url,
                                        icon_url: row.get("icon_url")?,
                                        description: row.get("description")?,
                                        rating: row.get("rating")?,
                                        number_of_ratings: row.get("number_of_ratings")?,
                                        guid: row.get("guid")?,
                                        score,
                                    })
                                },
                            )
                        })
                        .transpose()
                },
            )?
            .into_iter()
            .flatten()
            .collect();
        Ok(suggestions)
    }

    /// Fetches Suggestions of type pocket provider that match the given query
    pub fn fetch_pocket_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let keyword_lowercased = &query.keyword.to_lowercase();
        let (keyword_prefix, keyword_suffix) = split_keyword(keyword_lowercased);
        let suggestions = self
            .conn
            .query_rows_and_then_cached(
                r#"
            SELECT
              s.id,
              MAX(k.rank) AS rank,
              s.title,
              s.url,
              s.provider,
              s.score,
              k.confidence,
              k.keyword_suffix
            FROM
              suggestions s
            JOIN
              prefix_keywords k
              ON k.suggestion_id = s.id
            WHERE
              k.keyword_prefix = :keyword_prefix
              AND (k.keyword_suffix BETWEEN :keyword_suffix AND :keyword_suffix || x'FFFF')
              AND s.provider = :provider
              AND NOT EXISTS (SELECT 1 FROM dismissed_suggestions WHERE url=s.url)
            GROUP BY
              s.id,
              k.confidence
            ORDER BY
              s.score DESC,
              rank DESC
            "#,
                named_params! {
                    ":keyword_prefix": keyword_prefix,
                    ":keyword_suffix": keyword_suffix,
                    ":provider": SuggestionProvider::Pocket,
                },
                |row| -> Result<Option<Suggestion>> {
                    let title = row.get("title")?;
                    let raw_url = row.get::<_, String>("url")?;
                    let score = row.get::<_, f64>("score")?;
                    let confidence = row.get("confidence")?;
                    let full_suffix = row.get::<_, String>("keyword_suffix")?;
                    let suffixes_match = match confidence {
                        KeywordConfidence::Low => full_suffix.starts_with(keyword_suffix),
                        KeywordConfidence::High => full_suffix == keyword_suffix,
                    };
                    if suffixes_match {
                        Ok(Some(Suggestion::Pocket {
                            title,
                            url: raw_url,
                            score,
                            is_top_pick: matches!(confidence, KeywordConfidence::High),
                        }))
                    } else {
                        Ok(None)
                    }
                },
            )?
            .into_iter()
            .flatten()
            .take(
                query
                    .limit
                    .and_then(|limit| usize::try_from(limit).ok())
                    .unwrap_or(usize::MAX),
            )
            .collect();
        Ok(suggestions)
    }

    /// Fetches suggestions for MDN
    pub fn fetch_mdn_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let suggestions = self
            .map_prefix_keywords(
                query,
                &SuggestionProvider::Mdn,
                |row, keyword_suffix| -> Result<Option<Suggestion>> {
                    let suggestion_id: i64 = row.get("id")?;
                    let title = row.get("title")?;
                    let raw_url = row.get::<_, String>("url")?;
                    let score = row.get::<_, f64>("score")?;

                    let full_suffix = row.get::<_, String>("keyword_suffix")?;
                    full_suffix
                        .starts_with(keyword_suffix)
                        .then(|| {
                            self.conn.query_row_and_then(
                                r#"
                                SELECT
                                    description
                                FROM
                                    mdn_custom_details
                                WHERE
                                    suggestion_id = :suggestion_id
                                "#,
                                named_params! {
                                    ":suggestion_id": suggestion_id
                                },
                                |row| {
                                    Ok(Suggestion::Mdn {
                                        title,
                                        url: raw_url,
                                        description: row.get("description")?,
                                        score,
                                    })
                                },
                            )
                        })
                        .transpose()
                },
            )?
            .into_iter()
            .flatten()
            .collect();

        Ok(suggestions)
    }

    /// Fetches Fakespot suggestions
    pub fn fetch_fakespot_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let fts_query = query.fts_query();
        let sql = r#"
            SELECT
                s.id,
                s.title,
                s.url,
                s.score,
                f.fakespot_grade,
                f.product_id,
                f.rating,
                f.total_reviews,
                i.data,
                i.mimetype,
                f.keywords,
                f.product_type
            FROM
                suggestions s
            JOIN
                fakespot_fts fts
                ON fts.rowid = s.id
            JOIN
                fakespot_custom_details f
                ON f.suggestion_id = s.id
            LEFT JOIN
                icons i
                ON i.id = f.icon_id
            WHERE
                fakespot_fts MATCH ?
            ORDER BY
                s.score DESC
            "#
        .to_string();

        // Store the list of results plus the suggestion id for calculating the FTS match info
        let mut results =
            self.conn
                .query_rows_and_then_cached(&sql, (&fts_query.match_arg,), |row| {
                    let id: usize = row.get(0)?;
                    let score = fakespot::FakespotScore::new(
                        &query.keyword,
                        row.get(10)?,
                        row.get(11)?,
                        row.get(3)?,
                    )
                    .as_suggest_score();
                    Result::Ok((
                        Suggestion::Fakespot {
                            title: row.get(1)?,
                            url: row.get(2)?,
                            score,
                            fakespot_grade: row.get(4)?,
                            product_id: row.get(5)?,
                            rating: row.get(6)?,
                            total_reviews: row.get(7)?,
                            icon: row.get(8)?,
                            icon_mimetype: row.get(9)?,
                            match_info: None,
                        },
                        id,
                    ))
                })?;
        // Sort the results, then add the FTS match info to the first one
        // For performance reasons, this is only calculated for the result with the highest score.
        // We assume that only one that will be shown to the user and therefore the only one we'll
        // collect metrics for.
        results.sort();
        if let Some((suggestion, id)) = results.first_mut() {
            match suggestion {
                Suggestion::Fakespot {
                    match_info, title, ..
                } => {
                    *match_info = Some(self.fetch_fakespot_fts_match_info(&fts_query, *id, title)?);
                }
                _ => unreachable!(),
            }
        }
        Ok(results
            .into_iter()
            .map(|(suggestion, _)| suggestion)
            .collect())
    }

    fn fetch_fakespot_fts_match_info(
        &self,
        fts_query: &FtsQuery<'_>,
        suggestion_id: usize,
        title: &str,
    ) -> Result<FtsMatchInfo> {
        let prefix = if fts_query.is_prefix_query {
            // If the query was a prefix match query then test if the query without the prefix
            // match would have also matched.  If not, then this counts as a prefix match.
            let sql = "SELECT 1 FROM fakespot_fts WHERE rowid = ? AND fakespot_fts MATCH ?";
            let params = (&suggestion_id, &fts_query.match_arg_without_prefix_match);
            !self.conn.exists(sql, params)?
        } else {
            // If not, then it definitely wasn't a prefix match
            false
        };

        Ok(FtsMatchInfo {
            prefix,
            stemming: fts_query.match_required_stemming(title),
        })
    }

    /// Fetches dynamic suggestions
    pub fn fetch_dynamic_suggestions(&self, query: &SuggestionQuery) -> Result<Vec<Suggestion>> {
        let Some(suggestion_types) = query
            .provider_constraints
            .as_ref()
            .and_then(|c| c.dynamic_suggestion_types.as_ref())
        else {
            return Ok(vec![]);
        };

        let keyword = query.keyword.to_lowercase();
        let params = rusqlite::params_from_iter(
            std::iter::once(&SuggestionProvider::Dynamic as &dyn ToSql)
                .chain(std::iter::once(&keyword as &dyn ToSql))
                .chain(suggestion_types.iter().map(|t| t as &dyn ToSql)),
        );
        self.conn.query_rows_and_then_cached(
            &format!(
                r#"
                SELECT
                  s.url,
                  s.score,
                  d.suggestion_type,
                  d.json_data
                FROM
                  suggestions s
                JOIN
                  dynamic_custom_details d
                  ON d.suggestion_id = s.id
                JOIN
                  keywords k
                  ON k.suggestion_id = s.id
                WHERE
                  s.provider = ?
                  AND k.keyword = ?
                  AND d.suggestion_type IN ({})
                  AND NOT EXISTS (SELECT 1 FROM dismissed_suggestions WHERE url = s.url)
                ORDER BY
                  s.score ASC, d.suggestion_type ASC, s.id ASC
                "#,
                repeat_sql_vars(suggestion_types.len())
            ),
            params,
            |row| -> Result<Suggestion> {
                let dismissal_key: String = row.get("url")?;
                let json_data: Option<String> = row.get("json_data")?;
                Ok(Suggestion::Dynamic {
                    suggestion_type: row.get("suggestion_type")?,
                    data: match json_data {
                        None => None,
                        Some(j) => serde_json::from_str(&j)?,
                    },
                    score: row.get("score")?,
                    dismissal_key: (!dismissal_key.is_empty()).then_some(dismissal_key),
                })
            },
        )
    }

    pub fn are_suggestions_ingested_for_record(&self, record_id: &SuggestRecordId) -> Result<bool> {
        Ok(self.conn.exists(
            r#"
            SELECT
              id
            FROM
              suggestions
            WHERE
              record_id = :record_id
            "#,
            named_params! {
                ":record_id": record_id.as_str(),
            },
        )?)
    }

    pub fn is_amp_fts_data_ingested(&self, record_id: &SuggestRecordId) -> Result<bool> {
        Ok(self.conn.exists(
            r#"
            SELECT 1
            FROM suggestions s
            JOIN amp_fts fts
              ON fts.rowid = s.id
            WHERE s.record_id = :record_id
            "#,
            named_params! {
                ":record_id": record_id.as_str(),
            },
        )?)
    }

    /// Inserts all suggestions from a downloaded AMO attachment into
    /// the database.
    pub fn insert_amo_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestions: &[DownloadedAmoSuggestion],
    ) -> Result<()> {
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut amo_insert = AmoInsertStatement::new(self.conn)?;
        let mut prefix_keyword_insert = PrefixKeywordInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            self.scope.err_if_interrupted()?;
            let suggestion_id = suggestion_insert.execute(
                record_id,
                &suggestion.title,
                &suggestion.url,
                suggestion.score,
                SuggestionProvider::Amo,
            )?;
            amo_insert.execute(suggestion_id, suggestion)?;
            for (index, keyword) in suggestion.keywords.iter().enumerate() {
                let (keyword_prefix, keyword_suffix) = split_keyword(keyword);
                prefix_keyword_insert.execute(
                    suggestion_id,
                    None,
                    keyword_prefix,
                    keyword_suffix,
                    index,
                )?;
            }
        }
        Ok(())
    }

    /// Inserts suggestions from an AMP attachment into the database.
    pub fn insert_amp_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestions: &[DownloadedAmpSuggestion],
        enable_fts: bool,
    ) -> Result<()> {
        // Prepare statements outside of the loop.  This results in a large performance
        // improvement on a fresh ingest, since there are so many rows.
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut amp_insert = AmpInsertStatement::new(self.conn)?;
        let mut keyword_insert = KeywordInsertStatement::new(self.conn)?;
        let mut fts_insert = AmpFtsInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            self.scope.err_if_interrupted()?;
            let suggestion_id = suggestion_insert.execute(
                record_id,
                &suggestion.title,
                &suggestion.url,
                suggestion.score.unwrap_or(DEFAULT_SUGGESTION_SCORE),
                SuggestionProvider::Amp,
            )?;
            amp_insert.execute(suggestion_id, suggestion)?;
            if enable_fts {
                fts_insert.execute(
                    suggestion_id,
                    &suggestion.full_keywords_fts_column(),
                    &suggestion.title,
                )?;
            }
            let mut full_keyword_inserter = FullKeywordInserter::new(self.conn, suggestion_id);
            for keyword in suggestion.keywords() {
                let full_keyword_id = if let Some(full_keyword) = keyword.full_keyword {
                    Some(full_keyword_inserter.maybe_insert(full_keyword)?)
                } else {
                    None
                };
                keyword_insert.execute(
                    suggestion_id,
                    keyword.keyword,
                    full_keyword_id,
                    keyword.rank,
                )?;
            }
        }
        Ok(())
    }

    /// Inserts suggestions from a Wikipedia attachment into the database.
    pub fn insert_wikipedia_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestions: &[DownloadedWikipediaSuggestion],
    ) -> Result<()> {
        // Prepare statements outside of the loop.  This results in a large performance
        // improvement on a fresh ingest, since there are so many rows.
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut wiki_insert = WikipediaInsertStatement::new(self.conn)?;
        let mut keyword_insert = KeywordInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            self.scope.err_if_interrupted()?;
            let suggestion_id = suggestion_insert.execute(
                record_id,
                &suggestion.title,
                &suggestion.url,
                suggestion.score.unwrap_or(DEFAULT_SUGGESTION_SCORE),
                SuggestionProvider::Wikipedia,
            )?;
            wiki_insert.execute(suggestion_id, suggestion)?;
            for keyword in suggestion.keywords() {
                // Don't update `full_keywords`, see bug 1876217.
                keyword_insert.execute(suggestion_id, keyword.keyword, None, keyword.rank)?;
            }
        }
        Ok(())
    }

    /// Inserts all suggestions from a downloaded Pocket attachment into
    /// the database.
    pub fn insert_pocket_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestions: &[DownloadedPocketSuggestion],
    ) -> Result<()> {
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut prefix_keyword_insert = PrefixKeywordInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            self.scope.err_if_interrupted()?;
            let suggestion_id = suggestion_insert.execute(
                record_id,
                &suggestion.title,
                &suggestion.url,
                suggestion.score,
                SuggestionProvider::Pocket,
            )?;
            for ((rank, keyword), confidence) in suggestion
                .high_confidence_keywords
                .iter()
                .enumerate()
                .zip(std::iter::repeat(KeywordConfidence::High))
                .chain(
                    suggestion
                        .low_confidence_keywords
                        .iter()
                        .enumerate()
                        .zip(std::iter::repeat(KeywordConfidence::Low)),
                )
            {
                let (keyword_prefix, keyword_suffix) = split_keyword(keyword);
                prefix_keyword_insert.execute(
                    suggestion_id,
                    Some(confidence as u8),
                    keyword_prefix,
                    keyword_suffix,
                    rank,
                )?;
            }
        }
        Ok(())
    }

    /// Inserts all suggestions from a downloaded MDN attachment into
    /// the database.
    pub fn insert_mdn_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestions: &[DownloadedMdnSuggestion],
    ) -> Result<()> {
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut mdn_insert = MdnInsertStatement::new(self.conn)?;
        let mut prefix_keyword_insert = PrefixKeywordInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            self.scope.err_if_interrupted()?;
            let suggestion_id = suggestion_insert.execute(
                record_id,
                &suggestion.title,
                &suggestion.url,
                suggestion.score,
                SuggestionProvider::Mdn,
            )?;
            mdn_insert.execute(suggestion_id, suggestion)?;
            for (index, keyword) in suggestion.keywords.iter().enumerate() {
                let (keyword_prefix, keyword_suffix) = split_keyword(keyword);
                prefix_keyword_insert.execute(
                    suggestion_id,
                    None,
                    keyword_prefix,
                    keyword_suffix,
                    index,
                )?;
            }
        }
        Ok(())
    }

    /// Inserts all suggestions from a downloaded Fakespot attachment into the database.
    pub fn insert_fakespot_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        suggestions: &[DownloadedFakespotSuggestion],
    ) -> Result<()> {
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut fakespot_insert = FakespotInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            let suggestion_id = suggestion_insert.execute(
                record_id,
                &suggestion.title,
                &suggestion.url,
                suggestion.score,
                SuggestionProvider::Fakespot,
            )?;
            fakespot_insert.execute(suggestion_id, suggestion)?;
        }
        Ok(())
    }

    /// Inserts dynamic suggestion records data into the database.
    pub fn insert_dynamic_suggestions(
        &mut self,
        record_id: &SuggestRecordId,
        record: &DownloadedDynamicRecord,
        suggestions: &[DownloadedDynamicSuggestion],
    ) -> Result<()> {
        // `suggestion.keywords()` can yield duplicates for dynamic
        // suggestions, so ignore failures on insert in the uniqueness
        // constraint on `(suggestion_id, keyword)`.
        let mut keyword_insert = KeywordInsertStatement::new_with_or_ignore(self.conn)?;
        let mut suggestion_insert = SuggestionInsertStatement::new(self.conn)?;
        let mut dynamic_insert = DynamicInsertStatement::new(self.conn)?;
        for suggestion in suggestions {
            self.scope.err_if_interrupted()?;
            let suggestion_id = suggestion_insert.execute(
                record_id,
                // title - Not used by dynamic suggestions.
                "",
                // url - Dynamic suggestions store their dismissal key here
                // instead.
                suggestion.dismissal_key.as_deref().unwrap_or(""),
                record.score.unwrap_or(DEFAULT_SUGGESTION_SCORE),
                SuggestionProvider::Dynamic,
            )?;
            dynamic_insert.execute(suggestion_id, &record.suggestion_type, suggestion)?;

            // Dynamic suggestions don't use `rank` but `(suggestion_id, rank)`
            // must be unique since there's an index on that tuple.
            for (rank, keyword) in suggestion.keywords().enumerate() {
                keyword_insert.execute(suggestion_id, &keyword, None, rank)?;
            }
        }
        Ok(())
    }

    /// Inserts or replaces an icon for a suggestion into the database.
    pub fn put_icon(&mut self, icon_id: &str, data: &[u8], mimetype: &str) -> Result<()> {
        self.conn.execute(
            "INSERT OR REPLACE INTO icons(
                 id,
                 data,
                 mimetype
             )
             VALUES(
                 :id,
                 :data,
                 :mimetype
             )",
            named_params! {
                ":id": icon_id,
                ":data": data,
                ":mimetype": mimetype,
            },
        )?;
        Ok(())
    }

    pub fn insert_dismissal(&self, key: &str) -> Result<()> {
        self.conn.execute(
            "INSERT OR IGNORE INTO dismissed_suggestions(url)
             VALUES(:url)",
            named_params! {
                ":url": key,
            },
        )?;
        Ok(())
    }

    pub fn clear_dismissals(&self) -> Result<()> {
        self.conn.execute("DELETE FROM dismissed_suggestions", ())?;
        Ok(())
    }

    pub fn has_dismissal(&self, key: &str) -> Result<bool> {
        Ok(self.conn.exists(
            "SELECT 1 FROM dismissed_suggestions WHERE url = :url",
            named_params! {
                ":url": key,
            },
        )?)
    }

    pub fn any_dismissals(&self) -> Result<bool> {
        Ok(self
            .conn
            .exists("SELECT 1 FROM dismissed_suggestions LIMIT 1", ())?)
    }

    /// Deletes all suggestions associated with a Remote Settings record from
    /// the database.
    pub fn drop_suggestions(&mut self, record_id: &SuggestRecordId) -> Result<()> {
        // If you update this, you probably need to update
        // `schema::clear_database()` too!
        //
        // Call `err_if_interrupted` before each statement since these have historically taken a
        // long time and caused shutdown hangs.

        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM keywords WHERE suggestion_id IN (SELECT id from suggestions WHERE record_id = :record_id)",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM full_keywords WHERE suggestion_id IN (SELECT id from suggestions WHERE record_id = :record_id)",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM prefix_keywords WHERE suggestion_id IN (SELECT id from suggestions WHERE record_id = :record_id)",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM keywords_metrics WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "
            DELETE FROM fakespot_fts
            WHERE rowid IN (SELECT id from suggestions WHERE record_id = :record_id)
            ",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM suggestions WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM yelp_subjects WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM yelp_modifiers WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM yelp_custom_details WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM geonames WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;
        self.scope.err_if_interrupted()?;
        self.conn.execute_cached(
            "DELETE FROM geonames_metrics WHERE record_id = :record_id",
            named_params! { ":record_id": record_id.as_str() },
        )?;

        // Invalidate these caches since we might have deleted a record their
        // contents are based on.
        self.weather_cache.take();
        self.geoname_cache.take();

        Ok(())
    }

    /// Deletes an icon for a suggestion from the database.
    pub fn drop_icon(&mut self, icon_id: &str) -> Result<()> {
        self.conn.execute_cached(
            "DELETE FROM icons WHERE id = :id",
            named_params! { ":id": icon_id },
        )?;
        Ok(())
    }

    /// Clears the database, removing all suggestions, icons, and metadata.
    pub fn clear(&mut self) -> Result<()> {
        Ok(clear_database(self.conn)?)
    }

    /// Returns the value associated with a metadata key.
    pub fn get_meta<T: FromSql>(&self, key: &str) -> Result<Option<T>> {
        Ok(self.conn.try_query_one(
            "SELECT value FROM meta WHERE key = :key",
            named_params! { ":key": key },
            true,
        )?)
    }

    /// Sets the value for a metadata key.
    pub fn put_meta(&mut self, key: &str, value: impl ToSql) -> Result<()> {
        self.conn.execute_cached(
            "INSERT OR REPLACE INTO meta(key, value) VALUES(:key, :value)",
            named_params! { ":key": key, ":value": value },
        )?;
        Ok(())
    }

    /// Stores global Suggest configuration data.
    pub fn put_global_config(&mut self, config: &SuggestGlobalConfig) -> Result<()> {
        self.put_meta(GLOBAL_CONFIG_META_KEY, serde_json::to_string(config)?)
    }

    /// Gets the stored global Suggest configuration data or a default config if
    /// none is stored.
    pub fn get_global_config(&self) -> Result<SuggestGlobalConfig> {
        self.get_meta::<String>(GLOBAL_CONFIG_META_KEY)?
            .map_or_else(
                || Ok(SuggestGlobalConfig::default()),
                |json| Ok(serde_json::from_str(&json)?),
            )
    }

    /// Stores configuration data for a given provider.
    pub fn put_provider_config(
        &mut self,
        provider: SuggestionProvider,
        config: &SuggestProviderConfig,
    ) -> Result<()> {
        self.put_meta(
            &provider_config_meta_key(provider),
            serde_json::to_string(config)?,
        )
    }

    /// Gets the stored configuration data for a given provider or None if none
    /// is stored.
    pub fn get_provider_config(
        &self,
        provider: SuggestionProvider,
    ) -> Result<Option<SuggestProviderConfig>> {
        self.get_meta::<String>(&provider_config_meta_key(provider))?
            .map_or_else(|| Ok(None), |json| Ok(serde_json::from_str(&json)?))
    }
}

#[derive(Debug, PartialEq, Eq, Hash)]
pub struct IngestedRecord {
    pub id: SuggestRecordId,
    pub collection: String,
    pub record_type: String,
    pub last_modified: u64,
}

impl IngestedRecord {
    fn from_row(row: &rusqlite::Row) -> Result<Self> {
        Ok(Self {
            id: SuggestRecordId::new(row.get("id")?),
            collection: row.get("collection")?,
            record_type: row.get("type")?,
            last_modified: row.get("last_modified")?,
        })
    }
}

/// Helper struct to get full_keyword_ids for a suggestion
///
/// `FullKeywordInserter` handles repeated full keywords efficiently.  The first instance will
/// cause a row to be inserted into the database.  Subsequent instances will return the same
/// full_keyword_id.
struct FullKeywordInserter<'a> {
    conn: &'a Connection,
    suggestion_id: i64,
    last_inserted: Option<(&'a str, i64)>,
}

impl<'a> FullKeywordInserter<'a> {
    fn new(conn: &'a Connection, suggestion_id: i64) -> Self {
        Self {
            conn,
            suggestion_id,
            last_inserted: None,
        }
    }

    fn maybe_insert(&mut self, full_keyword: &'a str) -> rusqlite::Result<i64> {
        match self.last_inserted {
            Some((s, id)) if s == full_keyword => Ok(id),
            _ => {
                let full_keyword_id = self.conn.query_row_and_then(
                    "INSERT INTO full_keywords(
                        suggestion_id,
                        full_keyword
                     )
                     VALUES(
                        :suggestion_id,
                        :keyword
                     )
                     RETURNING id",
                    named_params! {
                        ":keyword": full_keyword,
                        ":suggestion_id": self.suggestion_id,
                    },
                    |row| row.get(0),
                )?;
                self.last_inserted = Some((full_keyword, full_keyword_id));
                Ok(full_keyword_id)
            }
        }
    }
}

// ======================== Statement types ========================
//
// During ingestion we can insert hundreds of thousands of rows.  These types enable speedups by
// allowing us to prepare a statement outside a loop and use it many times inside the loop.
//
// Each type wraps [Connection::prepare] and [Statement] to provide a simplified interface,
// tailored to a specific query.
//
// This pattern is applicable for whenever we execute the same query repeatedly in a loop.
// The impact scales with the number of loop iterations, which is why we currently don't do this
// for providers like Mdn, Pocket, and Weather, which have relatively small number of records
// compared to Amp/Wikipedia.

pub(crate) struct SuggestionInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> SuggestionInsertStatement<'conn> {
    pub(crate) fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO suggestions(
                 record_id,
                 title,
                 url,
                 score,
                 provider
             )
             VALUES(?, ?, ?, ?, ?)
             RETURNING id",
        )?))
    }

    /// Execute the insert and return the `suggestion_id` for the new row
    pub(crate) fn execute(
        &mut self,
        record_id: &SuggestRecordId,
        title: &str,
        url: &str,
        score: f64,
        provider: SuggestionProvider,
    ) -> Result<i64> {
        self.0
            .query_row(
                (record_id.as_str(), title, url, score, provider as u8),
                |row| row.get(0),
            )
            .with_context("suggestion insert")
    }
}

struct AmpInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> AmpInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO amp_custom_details(
                 suggestion_id,
                 advertiser,
                 block_id,
                 iab_category,
                 impression_url,
                 click_url,
                 icon_id
             )
             VALUES(?, ?, ?, ?, ?, ?, ?)
             ",
        )?))
    }

    fn execute(&mut self, suggestion_id: i64, amp: &DownloadedAmpSuggestion) -> Result<()> {
        self.0
            .execute((
                suggestion_id,
                &amp.advertiser,
                amp.block_id,
                &amp.iab_category,
                &amp.impression_url,
                &amp.click_url,
                &amp.icon_id,
            ))
            .with_context("amp insert")?;
        Ok(())
    }
}

struct WikipediaInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> WikipediaInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO wikipedia_custom_details(
                 suggestion_id,
                 icon_id
             )
             VALUES(?, ?)
             ",
        )?))
    }

    fn execute(
        &mut self,
        suggestion_id: i64,
        wikipedia: &DownloadedWikipediaSuggestion,
    ) -> Result<()> {
        self.0
            .execute((suggestion_id, &wikipedia.icon_id))
            .with_context("wikipedia insert")?;
        Ok(())
    }
}

struct AmoInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> AmoInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO amo_custom_details(
                 suggestion_id,
                 description,
                 guid,
                 icon_url,
                 rating,
                 number_of_ratings
             )
             VALUES(?, ?, ?, ?, ?, ?)
             ",
        )?))
    }

    fn execute(&mut self, suggestion_id: i64, amo: &DownloadedAmoSuggestion) -> Result<()> {
        self.0
            .execute((
                suggestion_id,
                &amo.description,
                &amo.guid,
                &amo.icon_url,
                &amo.rating,
                amo.number_of_ratings,
            ))
            .with_context("amo insert")?;
        Ok(())
    }
}

struct MdnInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> MdnInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO mdn_custom_details(
                 suggestion_id,
                 description
             )
             VALUES(?, ?)
             ",
        )?))
    }

    fn execute(&mut self, suggestion_id: i64, mdn: &DownloadedMdnSuggestion) -> Result<()> {
        self.0
            .execute((suggestion_id, &mdn.description))
            .with_context("mdn insert")?;
        Ok(())
    }
}

struct FakespotInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> FakespotInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO fakespot_custom_details(
                 suggestion_id,
                 fakespot_grade,
                 product_id,
                 keywords,
                 product_type,
                 rating,
                 total_reviews,
                 icon_id
             )
             VALUES(?, ?, ?, ?, ?, ?, ?, ?)
             ",
        )?))
    }

    fn execute(
        &mut self,
        suggestion_id: i64,
        fakespot: &DownloadedFakespotSuggestion,
    ) -> Result<()> {
        let icon_id = fakespot
            .product_id
            .split_once('-')
            .map(|(vendor, _)| format!("fakespot-{vendor}"));
        self.0
            .execute((
                suggestion_id,
                &fakespot.fakespot_grade,
                &fakespot.product_id,
                &fakespot.keywords.to_lowercase(),
                &fakespot.product_type.to_lowercase(),
                fakespot.rating,
                fakespot.total_reviews,
                icon_id,
            ))
            .with_context("fakespot insert")?;
        Ok(())
    }
}

struct DynamicInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> DynamicInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO dynamic_custom_details(
                 suggestion_id,
                 suggestion_type,
                 json_data
             )
             VALUES(?, ?, ?)
             ",
        )?))
    }

    fn execute(
        &mut self,
        suggestion_id: i64,
        suggestion_type: &str,
        suggestion: &DownloadedDynamicSuggestion,
    ) -> Result<()> {
        self.0
            .execute((
                suggestion_id,
                suggestion_type,
                match &suggestion.data {
                    None => None,
                    Some(d) => Some(serde_json::to_string(&d)?),
                },
            ))
            .with_context("dynamic insert")?;
        Ok(())
    }
}

pub(crate) struct KeywordInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> KeywordInsertStatement<'conn> {
    pub(crate) fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO keywords(
                 suggestion_id,
                 keyword,
                 full_keyword_id,
                 rank
             )
             VALUES(?, ?, ?, ?)
             ",
        )?))
    }

    pub(crate) fn new_with_or_ignore(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT OR IGNORE INTO keywords(
                 suggestion_id,
                 keyword,
                 full_keyword_id,
                 rank
             )
             VALUES(?, ?, ?, ?)
             ",
        )?))
    }

    pub(crate) fn execute(
        &mut self,
        suggestion_id: i64,
        keyword: &str,
        full_keyword_id: Option<i64>,
        rank: usize,
    ) -> Result<()> {
        self.0
            .execute((suggestion_id, keyword, full_keyword_id, rank))
            .with_context("keyword insert")?;
        Ok(())
    }
}

struct PrefixKeywordInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> PrefixKeywordInsertStatement<'conn> {
    fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO prefix_keywords(
                 suggestion_id,
                 confidence,
                 keyword_prefix,
                 keyword_suffix,
                 rank
             )
             VALUES(?, ?, ?, ?, ?)
             ",
        )?))
    }

    fn execute(
        &mut self,
        suggestion_id: i64,
        confidence: Option<u8>,
        keyword_prefix: &str,
        keyword_suffix: &str,
        rank: usize,
    ) -> Result<()> {
        self.0
            .execute((
                suggestion_id,
                confidence.unwrap_or(0),
                keyword_prefix,
                keyword_suffix,
                rank,
            ))
            .with_context("prefix keyword insert")?;
        Ok(())
    }
}

pub(crate) struct KeywordMetricsInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> KeywordMetricsInsertStatement<'conn> {
    pub(crate) fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO keywords_metrics(
                 record_id,
                 provider,
                 max_length,
                 max_word_count
             )
             VALUES(?, ?, ?, ?)
             ",
        )?))
    }

    pub(crate) fn execute(
        &mut self,
        record_id: &SuggestRecordId,
        provider: SuggestionProvider,
        max_len: usize,
        max_word_count: usize,
    ) -> Result<()> {
        self.0
            .execute((record_id.as_str(), provider, max_len, max_word_count))
            .with_context("keyword metrics insert")?;
        Ok(())
    }
}

pub(crate) struct AmpFtsInsertStatement<'conn>(rusqlite::Statement<'conn>);

impl<'conn> AmpFtsInsertStatement<'conn> {
    pub(crate) fn new(conn: &'conn Connection) -> Result<Self> {
        Ok(Self(conn.prepare(
            "INSERT INTO amp_fts(rowid, full_keywords, title)
             VALUES(?, ?, ?)
             ",
        )?))
    }

    pub(crate) fn execute(
        &mut self,
        suggestion_id: i64,
        full_keywords: &str,
        title: &str,
    ) -> Result<()> {
        self.0
            .execute((suggestion_id, full_keywords, title))
            .with_context("amp fts insert")?;
        Ok(())
    }
}

fn provider_config_meta_key(provider: SuggestionProvider) -> String {
    format!("{}{}", PROVIDER_CONFIG_META_KEY_PREFIX, provider as u8)
}
