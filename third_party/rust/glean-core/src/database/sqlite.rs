// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::cell::Cell;
use std::collections::{btree_map::Entry, BTreeMap};
use std::fmt::{self, Display};
use std::fs;
use std::num::NonZeroU64;
use std::path::Path;
use std::str;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::RwLock;
use std::time::{Duration, Instant};

use chrono::{DateTime, Utc};
use connection::Connection;
use malloc_size_of::MallocSizeOf;
use rusqlite::fallible_iterator::FallibleIterator;
use rusqlite::types::{FromSql, FromSqlError, FromSqlResult, ToSqlOutput, ValueRef};
use rusqlite::OptionalExtension;
use rusqlite::Transaction;
use rusqlite::{params, ToSql};
use rusqlite::{Error as SqlError, ErrorCode};
use schema::Schema;
pub use schema::SchemaError;

use crate::common_metric_data::CommonMetricDataInternal;
use crate::database::migration::{self, MigrationState};
use crate::metrics::dual_labeled_counter::RECORD_SEPARATOR;
use crate::metrics::Metric;
use crate::Lifetime;
use crate::Result;
use crate::{Glean, JsonValue};

use super::{ConnExt, StoredSubmittedPingHandler, EMPTY_LABEL};

mod connection;
mod schema;

// Database-conformant label:
// * empty string if no label given
// * `DEL` character if an empty label is given
// * otherwise the (already validated) label
fn database_label(label: Option<&str>) -> &str {
    match label {
        None => "",
        Some("") => EMPTY_LABEL,
        Some(label) => label,
    }
}

#[derive(Debug)]
pub enum LoadState {
    Ok,
    Err(OpenError),
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum MigrationResult {
    /// Migration did not happen yet
    Unknown,
    /// Migration failed
    Error,
}

#[derive(Debug)]
pub struct Database {
    /// The database connection.
    pub(crate) conn: connection::Connection,

    /// Initial file size when opening the database.
    pub(crate) file_size: Option<NonZeroU64>,

    /// Load state
    load_state: LoadState,

    /// Migration state, counts migrated metrics and the time it took.
    pub(crate) migration_state: Option<MigrationState>,

    /// Set when a database migration attempt failed.
    pub(crate) migration_error: MigrationResult,

    /// If the `delay_ping_lifetime_io` Glean config option is `true`,
    /// we will save metrics with 'ping' lifetime data in memory only,
    /// and persist them to disk in bulk on demand.
    delay_ping_lifetime_io: bool,

    ping_lifetime_data: RwLock<BTreeMap<String, Metric>>,

    /// A count of how many database writes have been done since the last ping-lifetime flush.
    ///
    /// A ping-lifetime flush is automatically done after `ping_lifetime_threshold` writes.
    ///
    /// Only relevant if `delay_ping_lifetime_io` is set to `true`,
    ping_lifetime_count: AtomicUsize,

    /// Write-count threshold when to auto-flush. `0` disables it.
    ping_lifetime_threshold: usize,

    /// The last time the `lifetime=ping` data was flushed to disk.
    ///
    /// Data is flushed to disk automatically when the last flush was more than
    /// `ping_lifetime_max_time` ago.
    ///
    /// Only relevant if `delay_ping_lifetime_io` is set to `true`,
    ping_lifetime_store_ts: Cell<Instant>,

    /// After what delay to auto-flush. 0 disables it.
    ping_lifetime_max_time: Duration,
}

impl MallocSizeOf for Database {
    fn size_of(&self, _ops: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        // FIXME: Can we get the allocated size of the connection?
        0
    }
}

pub struct SubmittedPing {
    pub document_id: String,
    pub ping: String,
    pub submitted_date: SqliteDatetime,
    pub uploaded_date: Option<SqliteDatetime>,
    pub upload_failed: Option<SqliteDatetime>,
    pub payload: Option<String>,
}

impl SubmittedPing {
    pub fn payload(&self) -> Option<JsonValue> {
        self.payload
            .as_ref()
            .map(|p| match serde_json::from_str(p) {
                Ok(v) => Some(v),
                Err(e) => {
                    log::warn!("Unable to serialize JSON payload from string: {:?}", e);
                    None
                }
            })
            .unwrap_or(None)
    }

    pub fn submitted_date(&self) -> DateTime<Utc> {
        self.submitted_date.0
    }

    pub fn uploaded_date(&self) -> Option<DateTime<Utc>> {
        self.uploaded_date.clone().map(|d| d.0)
    }

    pub fn upload_failed(&self) -> Option<DateTime<Utc>> {
        self.upload_failed.clone().map(|d| d.0)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct SqliteDatetime(pub DateTime<Utc>);

impl ToSql for SqliteDatetime {
    fn to_sql(&self) -> rusqlite::Result<ToSqlOutput<'_>> {
        Ok(ToSqlOutput::from(self.0.timestamp_millis()))
    }
}

impl FromSql for SqliteDatetime {
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        i64::column_result(value).and_then(|as_i64| match DateTime::from_timestamp_millis(as_i64) {
            Some(d) => Ok(SqliteDatetime(d)),
            None => Err(FromSqlError::InvalidType),
        })
    }
}

const DEFAULT_DATABASE_FILE_NAME: &str = "glean.sqlite";

/// Calculate the database size from all the files in the directory.
///
///  # Arguments
///
///  *`path` - The path to the directory
///
///  # Returns
///
/// Returns the non-zero combined size in bytes of all files in a directory,
/// or `None` on error or if the size is `0`.
fn database_size(dir: &Path) -> Option<NonZeroU64> {
    let mut total_size = 0;
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            if let Ok(file_type) = entry.file_type() {
                if file_type.is_file() {
                    let path = entry.path();
                    if let Ok(metadata) = fs::metadata(path) {
                        total_size += metadata.len();
                    } else {
                        continue;
                    }
                }
            }
        }
    }

    NonZeroU64::new(total_size)
}

#[derive(Debug)]
pub enum OpenError {
    IncompatibleVersion(u32),
    Corrupt,
    SqlError(rusqlite::Error),
    RecoveryError(std::io::Error),
}

impl std::error::Error for OpenError {}

impl Display for OpenError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use OpenError::*;
        match self {
            IncompatibleVersion(v) => write!(f, "Incompatible database version: {v}"),
            Corrupt => write!(f, "Database is corrupt"),
            SqlError(err) => write!(f, "Error executing SQL: {err}"),
            RecoveryError(err) => write!(
                f,
                "Failed to recover a corrupt database due to an error deleting the file: {err}"
            ),
        }
    }
}

impl From<rusqlite::Error> for OpenError {
    fn from(value: rusqlite::Error) -> Self {
        match value {
            rusqlite::Error::SqliteFailure(e, _)
                if matches!(e.code, ErrorCode::DatabaseCorrupt | ErrorCode::NotADatabase) =>
            {
                Self::Corrupt
            }
            _ => Self::SqlError(value),
        }
    }
}

impl From<SchemaError> for OpenError {
    fn from(value: SchemaError) -> Self {
        match value {
            SchemaError::Sqlite(err) => OpenError::SqlError(err),
            SchemaError::UnsupportedSchemaVersion(v) => OpenError::IncompatibleVersion(v),
        }
    }
}

pub fn sqlite_open(path: &Path) -> std::result::Result<(Connection, LoadState), OpenError> {
    // TODO(bug 2049292): Make this more robust, use the correct errors and see how we can test all the branches
    // properly.
    match Connection::new::<Schema>(path) {
        Err(e @ SchemaError::UnsupportedSchemaVersion(_)) => Err(e.into()),
        Err(e @ SchemaError::Sqlite(SqlError::SqliteFailure(err, _))) => {
            match err.code {
                ErrorCode::PermissionDenied => Err(e.into()),
                ErrorCode::NotADatabase => {
                    log::debug!("sqlite failed: not a database. starting from scratch.");
                    fs::remove_file(path).map_err(OpenError::RecoveryError)?;
                    // Now try again, we only handle that error once.
                    let conn = Connection::new::<Schema>(path)?;
                    Ok((conn, LoadState::Err(OpenError::Corrupt)))
                }
                ErrorCode::CannotOpen => {
                    log::debug!("sqlite failed: cannot open. starting from scratch.");
                    fs::remove_file(path).map_err(OpenError::RecoveryError)?;
                    // Now try again, we only handle that error once.
                    let conn = Connection::new::<Schema>(path)?;
                    Ok((conn, LoadState::Err(OpenError::Corrupt)))
                }
                _ => Err(e.into()),
            }
        }
        Err(err @ SchemaError::Sqlite(SqlError::SqlInputError { .. })) => {
            log::debug!("sqlite failed: schema migration failed. starting from scratch.");
            fs::remove_file(path).map_err(OpenError::RecoveryError)?;
            // Now try again, we only handle that error once.
            let conn = Connection::new::<Schema>(path)?;
            Ok((conn, LoadState::Err(err.into())))
        }
        other => {
            let conn = other?;
            Ok((conn, LoadState::Ok))
        }
    }
}

impl Database {
    /// Initializes the data store.
    ///
    /// This opens the underlying SQLite store and creates
    /// the underlying directory structure.
    pub fn new(
        data_path: &Path,
        delay_ping_lifetime_io: bool,
        ping_lifetime_threshold: usize,
        ping_lifetime_max_time: Duration,
    ) -> Result<Self> {
        let path = data_path.join("db");
        log::debug!("Database path: {:?}", path.display());
        let file_size = database_size(&path);

        fs::create_dir_all(&path)?;
        let store_path = path.join(DEFAULT_DATABASE_FILE_NAME);
        let (conn, load_state) = sqlite_open(&store_path)?;

        let ping_lifetime_data = RwLock::new(Default::default());

        let now = Instant::now();
        let mut db = Self {
            conn,
            file_size,
            load_state,
            migration_state: None,
            migration_error: MigrationResult::Unknown,
            delay_ping_lifetime_io,
            ping_lifetime_data,
            ping_lifetime_count: AtomicUsize::new(0),
            ping_lifetime_threshold,
            ping_lifetime_store_ts: Cell::new(now),
            ping_lifetime_max_time,
        };

        match migration::try_migrate(&path, &db) {
            Ok(Some(state)) => {
                log::debug!("Migration done. state={state:?}");
                db.migration_state = Some(state);
                db.run_maintenance(true)?;
            }
            Ok(None) => {
                log::debug!("No migration.");
                db.run_maintenance(false)?;
            }
            Err(e) => {
                db.migration_error = MigrationResult::Error;
                log::warn!(
                    "Migration failed! Continuing with SQLite backend without migrated data. Error: {e:?}"
                )
            }
        }

        db.conn.write(|tx| {
            tx.execute("INSERT INTO migration (id, state) VALUES (1, 'done') ON CONFLICT(id) DO UPDATE SET state = excluded.state", [])?;
            Ok::<(), rusqlite::Error>(())
        })?;

        db.load_ping_lifetime_data();

        Ok(db)
    }

    /// Get the initial database file size.
    pub fn file_size(&self) -> Option<NonZeroU64> {
        self.file_size
    }

    /// Get the load state.
    pub fn load_state(&self) -> Option<String> {
        if let LoadState::Err(e) = &self.load_state {
            Some(match e {
                OpenError::IncompatibleVersion(v) => format!("incompatible version: {v}"),
                OpenError::Corrupt => "database file corrupt".to_string(),
                OpenError::SqlError(error) => format!("sql error: {error:?}"),
                OpenError::RecoveryError(error) => format!("recovery error: {error:?}"),
            })
        } else {
            None
        }
    }

    /// Run periodic database maintenance.
    ///
    /// If `force=true` always run the full maintenance taks
    pub fn run_maintenance(&self, force: bool) -> Result<()> {
        let conn = self.conn.lock();
        let conn = &*conn;

        self.run_maintenance_vacuum(conn, force)?;
        self.run_maintenance_optimize(conn)?;
        self.run_maintenance_checkpoint(conn)?;

        Ok(())
    }

    /// Run maintenance on the database (vacuum step)
    ///
    /// If `force_full: true` it _always_ runs a full `VACUUM`.
    fn run_maintenance_vacuum(&self, conn: &rusqlite::Connection, force_full: bool) -> Result<()> {
        let auto_vacuum_setting: u32 =
            conn.query_row_and_then("PRAGMA auto_vacuum", [], |row| row.get(0))?;
        if !force_full && auto_vacuum_setting == 2 {
            // Ideally, we run an incremental vacuum to delete 2 pages
            conn.execute_one("PRAGMA incremental_vacuum(2)")?;
        } else {
            // If auto_vacuum=incremental isn't set, configure it and run a full vacuum.
            log::debug!(
                "run_maintenance_vacuum: Need to run a full vacuum to set auto_vacuum=incremental"
            );
            conn.execute_one("PRAGMA auto_vacuum = INCREMENTAL")?;
            conn.execute_one("VACUUM")?;
        }
        Ok(())
    }

    /// Run maintenance on the database (optimize step)
    fn run_maintenance_optimize(&self, conn: &rusqlite::Connection) -> Result<()> {
        conn.execute("PRAGMA optimize", [])?;
        Ok(())
    }

    /// Run maintenance on the database (checkpoint step)
    fn run_maintenance_checkpoint(&self, conn: &rusqlite::Connection) -> Result<()> {
        conn.query_row("PRAGMA wal_checkpoint(PASSIVE)", [], |_| Ok(()))?;
        Ok(())
    }

    /// Loads `Lifetime::Ping` data from the database to memory,
    /// if `delay_ping_lifetime_io` is set to true.
    ///
    /// Does nothing if it isn't or if there is not data to load.
    fn load_ping_lifetime_data(&mut self) {
        if !self.delay_ping_lifetime_io {
            return;
        };

        let load_sql = "SELECT ping, id, labels, value FROM telemetry WHERE lifetime = 'ping'";
        let conn = self.conn.lock();
        let mut stmt = match conn.prepare_cached(load_sql) {
            Ok(stmt) => stmt,
            Err(err) => {
                log::error!("Could not load ping lifetime data into memory: {err:?}. Disabling ping lifetime IO delay.");
                self.delay_ping_lifetime_io = false;
                return;
            }
        };

        let rows = stmt.query_map(params![], |row| {
            let ping: String = row.get(0)?;
            let id: String = row.get(1)?;
            let labels: String = row.get(2)?;
            let blob: Vec<u8> = row.get(3)?;
            let blob: Metric =
                rmp_serde::from_slice(&blob).map_err(|_| FromSqlError::InvalidType)?;
            Ok((ping, id, labels, blob))
        });

        let rows = match rows {
            Err(err) => {
                log::error!("Could not load ping lifetime data into memory: {err:?}. Disabling ping lifetime IO delay.");
                self.delay_ping_lifetime_io = false;
                return;
            }
            Ok(rows) => rows,
        };

        let mut map = self.ping_lifetime_data.write().unwrap();
        for row in rows {
            let Ok((ping, metric_id, labels, metric)) = row else {
                continue;
            };

            let key = [ping, metric_id, labels].join("|");
            map.insert(key, metric);
        }
    }

    /// Iterates with the provided transaction function
    /// over the requested data from the given storage.
    ///
    /// * If the storage is unavailable, the transaction function is never invoked.
    /// * If the read data cannot be deserialized it will be silently skipped.
    ///
    /// # Arguments
    ///
    /// * `lifetime` - The metric lifetime to iterate over.
    /// * `storage_name` - The storage name to iterate over.
    /// * `transaction_fn` - Called for each entry being iterated over. It is
    ///   passed two arguments: `(metric_id: &[u8], metric: &Metric)`.
    ///
    /// # Panics
    ///
    /// This function will **not** panic on database errors.
    pub fn iter_store<F>(
        &self,
        lifetime: Lifetime,
        storage_name: &str,
        mut transaction_fn: F,
    ) -> Result<()>
    where
        F: FnMut(&[u8], &[&str], &Metric),
    {
        if lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
            let map = self.ping_lifetime_data.read().unwrap();

            for (key, value) in map.iter() {
                let mut split = key.splitn(3, '|');
                let Some(ping) = split.next() else { continue };
                if ping != storage_name {
                    continue;
                }

                let Some(key) = split.next() else { continue };
                let Some(labels_str) = split.next() else {
                    continue;
                };

                let labels: &[&str] = if labels_str.is_empty() {
                    &[]
                } else if labels_str.contains(RECORD_SEPARATOR) {
                    let (key, category) = labels_str.split_once(RECORD_SEPARATOR).unwrap();
                    &[key, category]
                } else {
                    &[labels_str]
                };
                transaction_fn(key.as_bytes(), labels, value);
            }

            return Ok(());
        }

        let iter_sql = "
            SELECT
                id,
                value,
                labels
            FROM telemetry
            WHERE
                lifetime = ?1
                AND ping = ?2
        ";

        self.conn.read(|conn| {
            let mut stmt = conn.prepare_cached(iter_sql)?;
            let rows = stmt.query_map(
                params![lifetime.as_str().to_string(), storage_name],
                |row| {
                    let id: String = row.get(0)?;
                    let blob: Vec<u8> = row.get(1)?;
                    let labels: String = row.get(2)?;
                    let blob: Metric =
                        rmp_serde::from_slice(&blob).map_err(|_| FromSqlError::InvalidType)?;
                    Ok((id, labels, blob))
                },
            )?;

            for row in rows {
                let Ok((metric_id, labels, metric)) = row else {
                    continue;
                };
                let label_vec;
                let labels = if labels.is_empty() {
                    &[]
                } else if labels == EMPTY_LABEL {
                    &[""]
                } else {
                    label_vec = labels.split(RECORD_SEPARATOR).collect::<Vec<_>>();
                    &*label_vec
                };
                transaction_fn(metric_id.as_bytes(), labels, &metric);
            }

            Ok(())
        })
    }

    /// Get a single metric by name from storage
    pub fn get_metric(
        &self,
        data: &CommonMetricDataInternal,
        storage_name: &str,
    ) -> Option<Metric> {
        if data.inner.lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
            let map = self.ping_lifetime_data.read().unwrap();

            let name = data.base_identifier();
            let labels = data.check_labels_in_memory(&map);
            let label = database_label(labels.label());
            let key = [storage_name, &name, label].join("|");
            return map.get(&key).cloned();
        }

        // TODO(bug 2048194): Remove the `LIMIT 1` and error out when more than 1 row is returned.
        let get_metric_sql = "
            SELECT
                value
            FROM telemetry
            WHERE
                id = ?1
                AND ping = ?2
                AND labels = ?3
            LIMIT 1
        ";

        let metric_identifier = &data.base_identifier();

        self.conn
            .read(|tx| {
                let labels = data.check_labels(tx);

                let mut stmt = tx.prepare_cached(get_metric_sql)?;
                let label = database_label(labels.label());
                stmt.query_one([metric_identifier, storage_name, label], |row| {
                    let blob: Vec<u8> = row.get(0)?;
                    let blob: Metric =
                        rmp_serde::from_slice(&blob).map_err(|_| FromSqlError::InvalidType)?;
                    Ok(blob)
                })
                .optional()
            })
            .unwrap_or(None) // TODO(bug 2047617): Should we handle the error here properly?
    }

    /// Records a metric in the underlying storage system.
    pub fn record(&self, glean: &Glean, data: &CommonMetricDataInternal, value: &Metric) {
        if data.inner.lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
            let map = self.ping_lifetime_data.read().unwrap();

            let name = data.base_identifier();
            let labels = data.check_labels_in_memory(&map);

            // Release the map so that error recording can work
            drop(map);
            labels.record_error_metric(glean, data);

            let mut map = self.ping_lifetime_data.write().unwrap();

            for ping_name in data.storage_names() {
                if glean.is_ping_enabled(ping_name) {
                    if let Err(e) = self.record_ping_lifetime_with(
                        &mut map,
                        ping_name,
                        &name,
                        database_label(labels.label()),
                        |_metric| value.clone(),
                    ) {
                        log::error!(
                            "Failed to record metric '{}' into {}: {:?}",
                            data.base_identifier(),
                            ping_name,
                            e
                        );
                    }
                }
            }

            // Ensure map is unlocked.
            drop(map);

            if let Err(err) = self.persist_ping_lifetime_data_if_full(None) {
                log::error!("Can't flush ping lifetime data: {err:?}");
            };

            return;
        }

        let name = data.base_identifier();

        _ = self.conn.write(|tx| {
            let labels = data.check_labels(tx);
            labels.record_error(glean, tx, &name, data.storage_names());

            for ping_name in data.storage_names() {
                if glean.is_ping_enabled(ping_name) {
                    if let Err(e) = self.record_per_lifetime(
                        tx,
                        data.inner.lifetime,
                        ping_name,
                        &name,
                        database_label(labels.label()),
                        value,
                    ) {
                        log::error!(
                            "Failed to record metric '{}' into {}: {:?}",
                            data.base_identifier(),
                            ping_name,
                            e
                        );
                    }
                }
            }

            Ok::<(), rusqlite::Error>(())
        });
    }

    /// Records a metric in the underlying storage system, for a single lifetime.
    ///
    /// # Returns
    ///
    /// If the storage is unavailable or the write fails, no data will be stored and an error will be returned.
    ///
    /// Otherwise `Ok(())` is returned.
    ///
    /// # Panics
    ///
    /// This function will **not** panic on database errors.
    pub(crate) fn record_per_lifetime(
        &self,
        tx: &mut Transaction,
        lifetime: Lifetime,
        storage_name: &str,
        key: &str,
        labels: &str,
        metric: &Metric,
    ) -> Result<()> {
        let insert_sql = "
            INSERT INTO
                telemetry (id, ping, lifetime, labels, value)
            VALUES
                (?1, ?2, ?3, ?4,  ?5)
            ON CONFLICT(id, ping, labels) DO UPDATE SET
                lifetime = excluded.lifetime,
                value = excluded.value
        ";

        {
            let mut stmt = tx.prepare_cached(insert_sql)?;
            let encoded =
                rmp_serde::to_vec(&metric).expect("IMPOSSIBLE: Serializing metric failed");
            stmt.execute(params![
                key,
                storage_name,
                lifetime.as_str(),
                labels,
                encoded
            ])?;
        }

        if lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
            if let Err(err) = self.persist_ping_lifetime_data_if_full(Some(tx)) {
                log::error!("Can't flush ping lifetime data: {err:?}");
            };
            return Ok(());
        }

        Ok(())
    }

    /// Record a metric to the in-memory storage if necessary.
    ///
    /// Returns `true` if it recorded, `false` otherwise.
    pub fn record_in_memory<F>(
        &self,
        glean: &Glean,
        data: &CommonMetricDataInternal,
        tx: Option<&mut Transaction>,
        mut transform: F,
    ) -> bool
    where
        F: FnMut(Option<Metric>) -> Metric,
    {
        if data.inner.lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
            let map = self.ping_lifetime_data.read().unwrap();

            let name = data.base_identifier();
            let labels = data.check_labels_in_memory(&map);

            // Release the map so that error recording can work
            drop(map);
            labels.record_error_metric(glean, data);

            let mut map = self.ping_lifetime_data.write().unwrap();

            for ping_name in data.storage_names() {
                if glean.is_ping_enabled(ping_name) {
                    if let Err(e) = self.record_ping_lifetime_with(
                        &mut map,
                        ping_name,
                        &name,
                        database_label(labels.label()),
                        &mut transform,
                    ) {
                        log::error!(
                            "Failed to record metric '{}' into {}: {:?}",
                            data.base_identifier(),
                            ping_name,
                            e
                        );
                    }
                }
            }

            // Ensure map is unlocked.
            drop(map);

            if let Err(err) = self.persist_ping_lifetime_data_if_full(tx) {
                log::error!("Can't flush ping lifetime data: {err:?}");
            };

            return true;
        }

        false
    }

    /// Records the provided value, with the given lifetime,
    /// after applying a transformation function.
    pub fn record_with<F>(&self, glean: &Glean, data: &CommonMetricDataInternal, mut transform: F)
    where
        F: FnMut(Option<Metric>) -> Metric,
    {
        if self.record_in_memory(glean, data, None, &mut transform) {
            return;
        }

        _ = self
            .conn
            .write(|tx| self.record_with_transaction(glean, tx, data, transform));
    }

    fn record_ping_lifetime_with<F>(
        &self,
        map: &mut BTreeMap<String, Metric>,
        storage_name: &str,
        key: &str,
        labels: &str,
        mut transform: F,
    ) -> Result<()>
    where
        F: FnMut(Option<Metric>) -> Metric,
    {
        let key = [storage_name, key, labels].join("|");

        match map.entry(key) {
            Entry::Vacant(entry) => {
                entry.insert(transform(None));
            }
            Entry::Occupied(mut entry) => {
                let old_value = entry.get().clone();
                entry.insert(transform(Some(old_value)));
            }
        }
        Ok(())
    }

    pub fn record_with_transaction<F>(
        &self,
        glean: &Glean,
        tx: &mut Transaction,
        data: &CommonMetricDataInternal,
        mut transform: F,
    ) -> Result<()>
    where
        F: FnMut(Option<Metric>) -> Metric,
    {
        // We still need to check if we need to record in-memory.
        // Usually caught higher up the chain by `record_with`,
        // but some parts might already have a transaction and will call this function directly.
        if self.record_in_memory(glean, data, Some(tx), &mut transform) {
            return Ok(());
        }

        let name = data.base_identifier();

        let labels = data.check_labels(tx);
        labels.record_error(glean, tx, &name, data.storage_names());

        for ping_name in data.storage_names() {
            if glean.is_ping_enabled(ping_name) {
                if let Err(e) = self.record_per_lifetime_with(
                    tx,
                    data.inner.lifetime,
                    ping_name,
                    &name,
                    database_label(labels.label()),
                    &mut transform,
                ) {
                    log::error!(
                        "Failed to record metric '{}' into {}: {:?}",
                        data.base_identifier(),
                        ping_name,
                        e
                    );
                }
            }
        }

        Ok(())
    }

    /// Records a metric in the underlying storage system,
    /// after applying the given transformation function, for a single lifetime.
    ///
    /// # Returns
    ///
    /// If the storage is unavailable or the write fails, no data will be stored and an error will be returned.
    ///
    /// Otherwise `Ok(())` is returned.
    ///
    /// # Panics
    ///
    /// This function will **not** panic on database errors.
    fn record_per_lifetime_with<F>(
        &self,
        tx: &mut Transaction,
        lifetime: Lifetime,
        storage_name: &str,
        key: &str,
        labels: &str,
        mut transform: F,
    ) -> Result<()>
    where
        F: FnMut(Option<Metric>) -> Metric,
    {
        // TODO(bug 2048194): Remove the `LIMIT 1` and error out when more than 1 row is returned.
        let value_sql = "
            SELECT value
            FROM telemetry
            WHERE
                id = ?1
                AND ping = ?2
                AND lifetime = ?3
                AND labels = ?4
            LIMIT 1
        ";

        let new_value = {
            let mut stmt = tx.prepare_cached(value_sql)?;
            let mut rows = stmt.query(params![
                key,
                storage_name,
                lifetime.as_str().to_string(),
                labels
            ])?;

            if let Ok(Some(row)) = rows.next() {
                let blob: Vec<u8> = row.get(0)?;
                let old_value = rmp_serde::from_slice(&blob).ok();
                transform(old_value)
            } else {
                transform(None)
            }
        };

        let insert_sql = "
            INSERT INTO
                telemetry (id, ping, lifetime, labels, value)
            VALUES
                (?1, ?2, ?3, ?4, ?5)
            ON CONFLICT(id, ping, labels) DO UPDATE SET
                lifetime = excluded.lifetime,
                value = excluded.value
        ";

        {
            let mut stmt = tx.prepare_cached(insert_sql)?;
            let encoded =
                rmp_serde::to_vec(&new_value).expect("IMPOSSIBLE: Serializing metric failed");
            stmt.execute(params![
                key,
                storage_name,
                lifetime.as_str(),
                labels,
                encoded
            ])?;
        }

        if lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
            if let Err(err) = self.persist_ping_lifetime_data_if_full(Some(tx)) {
                log::error!("Can't flush ping lifetime data: {err:?}");
            };
            return Ok(());
        }

        Ok(())
    }

    /// Clears a storage (only Ping Lifetime).
    ///
    /// # Returns
    ///
    /// * If the storage is unavailable an error is returned.
    /// * If any individual delete fails, an error is returned, but other deletions might have
    ///   happened.
    ///
    /// Otherwise `Ok(())` is returned.
    ///
    /// # Panics
    ///
    /// This function will **not** panic on database errors.
    pub fn clear_ping_lifetime_storage(&self, storage_name: &str) -> Result<()> {
        if self.delay_ping_lifetime_io {
            let ping = [storage_name, "|"].concat();
            self.ping_lifetime_data
                .write()
                .expect("Can't access ping lifetime data as writable")
                .retain(|metric_id, _| !metric_id.starts_with(&ping));
        }

        self.conn.write(|tx| {
            let clear_sql = "DELETE FROM telemetry WHERE lifetime = 'ping' AND ping = ?1";
            let mut stmt = tx.prepare_cached(clear_sql)?;
            stmt.execute([storage_name])?;

            Ok(())
        })
    }

    pub fn clear_lifetime_storage(&self, lifetime: Lifetime, storage_name: &str) -> Result<()> {
        let clear_sql = "DELETE FROM telemetry WHERE lifetime = ?1 AND ping = ?2";
        self.conn.write(|tx| {
            let mut stmt = tx.prepare_cached(clear_sql)?;
            stmt.execute([lifetime.as_str(), storage_name])?;
            Ok(())
        })
    }

    /// Removes a single metric from the storage.
    ///
    /// # Arguments
    ///
    /// * `lifetime` - the lifetime of the storage in which to look for the metric.
    /// * `storage_name` - the name of the storage to store/fetch data from.
    /// * `metric_id` - the metric category + name.
    ///
    /// # Returns
    ///
    /// * If the storage is unavailable an error is returned.
    /// * If the metric could not be deleted, an error is returned.
    ///
    /// Otherwise `Ok(())` is returned.
    ///
    /// # Panics
    ///
    /// This function will **not** panic on database errors.
    pub fn remove_single_metric(
        &self,
        lifetime: Lifetime,
        storage_name: &str,
        metric_id: &str,
    ) -> Result<()> {
        self.conn.write(|tx| {
            let clear_sql = "DELETE FROM telemetry WHERE lifetime = ?1 AND ping = ?2 AND id = ?3";
            let mut stmt = tx.prepare_cached(clear_sql)?;
            stmt.execute([lifetime.as_str(), storage_name, metric_id])?;

            if lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
                let mut map = self.ping_lifetime_data.write().unwrap();
                let mut key = String::with_capacity(storage_name.len() + metric_id.len());
                key.push_str(storage_name);
                key.push('|');
                key.push_str(metric_id);
                key.push('|');

                map.retain(|metric_id, _| !metric_id.starts_with(&key))
            }
            Ok(())
        })
    }

    /// Clears all the metrics in the database, for the provided lifetime.
    ///
    /// Errors are logged.
    ///
    /// # Panics
    ///
    /// * This function will **not** panic on database errors.
    pub fn clear_lifetime(&self, lifetime: Lifetime) {
        _ = self.conn.write(|tx| {
            let clear_sql = "DELETE FROM telemetry WHERE lifetime = ?1";
            let mut stmt = tx.prepare_cached(clear_sql)?;
            let res = stmt.execute([lifetime.as_str()]);

            if let Err(e) = res {
                log::warn!("Could not clear store for lifetime {:?}: {:?}", lifetime, e);
            }

            // Lifetime::Ping data is not persisted to disk if
            // Glean has `delay_ping_lifetime_io` set to true
            if lifetime == Lifetime::Ping && self.delay_ping_lifetime_io {
                self.ping_lifetime_data
                    .write()
                    .expect("Can't access ping lifetime data as writable")
                    .clear();
            }

            Ok::<(), rusqlite::Error>(())
        });
    }

    /// Clears all metrics in the database.
    ///
    /// Errors are logged.
    ///
    /// # Panics
    ///
    /// * This function will **not** panic on database errors.
    pub fn clear_all(&self) {
        let lifetimes = &[
            Lifetime::User.as_str(),
            Lifetime::Ping.as_str(),
            Lifetime::Application.as_str(),
        ];
        let clear_sql =
            "DELETE FROM telemetry WHERE lifetime = ?1 OR lifetime = ?2 OR lifetime = ?3";
        let res = self.conn.write(|tx| {
            let mut stmt = tx.prepare_cached(clear_sql)?;
            stmt.execute(lifetimes)?;

            Ok::<(), rusqlite::Error>(())
        });

        if let Err(err) = res {
            log::warn!("Could not clear store for all lifetimes: {err:?}");
        }

        if self.delay_ping_lifetime_io {
            self.ping_lifetime_data
                .write()
                .expect("Can't access ping lifetime data as writable")
                .clear();
        }
    }

    /// Persists `Lifetime::Ping` data to disk.
    ///
    /// Does nothing in case there is nothing to persist.
    ///
    /// # Panics
    ///
    /// * This function will **not** panic on database errors.
    pub fn persist_ping_lifetime_data(&self) -> Result<()> {
        self.conn
            .write(|tx| self.persist_ping_lifetime_data_inner(tx))
    }

    fn persist_ping_lifetime_data_inner(&self, tx: &mut Transaction) -> Result<()> {
        if self.delay_ping_lifetime_io {
            let insert_sql = r#"
                INSERT INTO
                    telemetry (id, ping, lifetime, labels, value)
                VALUES
                    (?1, ?2, ?3, ?4, ?5)
                ON CONFLICT(id, ping, labels) DO UPDATE SET
                    lifetime = excluded.lifetime,
                    value = excluded.value
            "#;

            let mut stmt = tx.prepare_cached(insert_sql)?;
            let map = self.ping_lifetime_data.read().unwrap();

            for (key, value) in map.iter() {
                let mut split = key.splitn(3, '|');
                let Some(storage_name) = split.next() else {
                    continue;
                };
                let Some(key) = split.next() else { continue };
                let Some(labels) = split.next() else { continue };

                let encoded =
                    rmp_serde::to_vec(&value).expect("IMPOSSIBLE: Serializing metric failed");
                stmt.execute(params![
                    key,
                    storage_name,
                    Lifetime::Ping.as_str(),
                    labels,
                    encoded
                ])?;
            }

            // We can reset the write-counter. Current data has been persisted.
            self.ping_lifetime_count.store(0, Ordering::Release);
            self.ping_lifetime_store_ts.replace(Instant::now());
        }
        Ok(())
    }

    fn persist_ping_lifetime_data_if_full(&self, tx: Option<&mut Transaction>) -> Result<()> {
        if self.ping_lifetime_threshold == 0 && self.ping_lifetime_max_time.is_zero() {
            return Ok(());
        }

        let write_count = self.ping_lifetime_count.fetch_add(1, Ordering::Release) + 1;
        let last_write = self.ping_lifetime_store_ts.get();
        let elapsed = last_write.elapsed();

        if (self.ping_lifetime_threshold == 0 || write_count < self.ping_lifetime_threshold)
            && (self.ping_lifetime_max_time.is_zero() || elapsed < self.ping_lifetime_max_time)
        {
            log::trace!(
                "Not flushing. write_count={} (threshold={}), elapsed={:?} (max={:?})",
                write_count,
                self.ping_lifetime_threshold,
                elapsed,
                self.ping_lifetime_max_time
            );
            return Ok(());
        }

        if self.ping_lifetime_threshold > 0 && write_count >= self.ping_lifetime_threshold {
            log::debug!(
                "Flushing database due to threshold of {} reached.",
                self.ping_lifetime_threshold
            )
        } else if !self.ping_lifetime_max_time.is_zero() && elapsed >= self.ping_lifetime_max_time {
            log::debug!(
                "Flushing database due to last write more than {:?} ago",
                self.ping_lifetime_max_time
            );
        }

        match tx {
            Some(tx) => self.persist_ping_lifetime_data_inner(tx)?,
            None => self
                .conn
                .write(|tx| self.persist_ping_lifetime_data_inner(tx))?,
        }

        self.ping_lifetime_count.store(0, Ordering::Release);
        self.ping_lifetime_store_ts.replace(Instant::now());

        Ok(())
    }
}

impl StoredSubmittedPingHandler for Database {
    fn get_all_submitted_pings(&self) -> Vec<crate::SubmittedPing> {
        let get_all_submitted_pings_sql = "
            SELECT
                document_id,
                ping,
                date_submitted,
                date_uploaded,
                upload_failed,
                payload
            FROM submitted_pings
            ORDER BY date_submitted DESC
        ";
        self.conn
            .read(|conn| {
                let Ok(mut stmt) = conn.prepare_cached(get_all_submitted_pings_sql) else {
                    return Ok(Default::default());
                };
                let Ok(pings_iter) = stmt.query([]) else {
                    return Ok(Default::default());
                };

                pings_iter
                    .map(|r| {
                        Ok(SubmittedPing {
                            document_id: r.get(0).unwrap(),
                            ping: r.get(1).unwrap(),
                            submitted_date: r.get(2).unwrap(),
                            uploaded_date: r.get(3).unwrap(),
                            upload_failed: r.get(4).unwrap(),
                            payload: r.get(5).unwrap(),
                        }
                        .into())
                    })
                    .collect()
            })
            .unwrap_or_default()
    }

    fn get_submitted_pings_by_name(&self, ping: &str) -> Vec<crate::SubmittedPing> {
        let get_submitted_pings_sql = "
            SELECT
                document_id,
                ping,
                date_submitted,
                date_uploaded,
                upload_failed,
                payload
            FROM submitted_pings
            WHERE
                ping = ?1
            ORDER BY date_submitted DESC
        ";
        self.conn
            .read(|conn| {
                let Ok(mut stmt) = conn.prepare_cached(get_submitted_pings_sql) else {
                    return Ok(Default::default());
                };
                let Ok(pings_iter) = stmt.query([ping]) else {
                    return Ok(Default::default());
                };

                pings_iter
                    .map(|r| {
                        Ok(SubmittedPing {
                            document_id: r.get(0).unwrap(),
                            ping: r.get(1).unwrap(),
                            submitted_date: r.get(2).unwrap(),
                            uploaded_date: r.get(3).unwrap(),
                            upload_failed: r.get(4).unwrap(),
                            payload: r.get(5).unwrap(),
                        }
                        .into())
                    })
                    .collect()
            })
            .unwrap_or_default()
    }

    fn mark_ping_as_uploaded(&self, document_id: &str, date_uploaded: DateTime<Utc>) -> usize {
        let update_submitted_pings_sql =
            "UPDATE submitted_pings SET date_uploaded = ?1 WHERE document_id = ?2";
        self.conn
            .write(|tx| {
                let Ok(mut stmt) = tx.prepare_cached(update_submitted_pings_sql) else {
                    return Ok(Default::default());
                };
                stmt.execute(params![SqliteDatetime(date_uploaded), document_id])
            })
            .unwrap_or_default()
    }

    fn mark_ping_as_upload_failed(&self, document_id: &str) -> usize {
        let update_submitted_pings_sql =
            "UPDATE submitted_pings SET upload_failed = ?1 WHERE document_id = ?2";
        self.conn
            .write(|tx| {
                let Ok(mut stmt) = tx.prepare_cached(update_submitted_pings_sql) else {
                    return Ok(Default::default());
                };
                stmt.execute(params![SqliteDatetime(Utc::now()), document_id])
            })
            .unwrap_or_default()
    }

    fn store_submitted_ping(
        &self,
        document_id: &str,
        ping: &str,
        date_submitted: DateTime<Utc>,
        date_uploaded: Option<DateTime<Utc>>,
        upload_failed: Option<DateTime<Utc>>,
        payload: JsonValue,
    ) -> Result<()> {
        self.conn.write(|tx| {
            let insert_sql = "
                INSERT INTO
                    submitted_pings (document_id, ping, date_submitted, date_uploaded, upload_failed, payload)
                VALUES
                    (?1, ?2, ?3, ?4, ?5, ?6)
                ON CONFLICT(document_id) DO UPDATE SET
                    ping = excluded.ping,
                    date_submitted = excluded.date_submitted,
                    date_uploaded = excluded.date_uploaded,
                    upload_failed = excluded.upload_failed,
                    payload = excluded.payload
            ";
            let mut stmt = tx.prepare_cached(insert_sql)?;
            stmt.execute(params![
                document_id,
                ping,
                SqliteDatetime(date_submitted),
                date_uploaded.map(SqliteDatetime),
                upload_failed.map(SqliteDatetime),
                serde_json::to_string(&payload).expect("Unable to convert JSON payload to string.")
            ])?;
            Ok(())
        })
    }

    fn cleanup_submitted_pings(&self, before_time: Option<DateTime<Utc>>) -> Result<()> {
        let days_30 = Duration::from_secs(30 * 24 * 60 * 60);
        let time = before_time.unwrap_or_else(|| Utc::now() - days_30);
        let delete_sql = "DELETE FROM submitted_pings WHERE date_submitted <= ?1";
        self.conn.write(|tx| {
            let mut stmt = tx.prepare_cached(delete_sql)?;
            stmt.execute(params![SqliteDatetime(time)])
        })?;
        Ok(())
    }
}
