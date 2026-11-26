/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// How long we expect a remote command to live. After this time we assume it's
// either been delivered or will not be.
// Matches COMMAND_TTL in close_tabs.rs in fxa-client.
const REMOTE_COMMAND_TTL_MS: u64 = 2 * 24 * 60 * 60 * 1000; // 48 hours.

use crate::error::*;
use crate::schema;
use crate::sync::TabsRecord;
use crate::DeviceType;
use crate::{PendingCommand, RemoteCommand, Timestamp};
use error_support::{error, info, trace, warn};
use rusqlite::{
    types::{FromSql, FromSqlError, FromSqlResult, ToSql, ToSqlOutput, ValueRef},
    Connection, OpenFlags,
};
use sql_support::open_database::{self, open_database_with_flags};
use sql_support::ConnExt;
use std::cell::RefCell;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use sync15::{RemoteClient, ServerTimestamp};
pub type TabsDeviceType = crate::DeviceType;

pub(crate) const TABS_CLIENT_TTL: u32 = 15_552_000; // 180 days, same as CLIENTS_TTL
const FAR_FUTURE: i64 = 4_102_405_200_000; // 2100/01/01

#[derive(Clone, Debug, Default, uniffi::Record)]
#[cfg_attr(test, derive(PartialEq, Eq))] // only tests need this
pub struct RemoteTabRecord {
    pub title: String,
    pub url_history: Vec<String>,
    pub icon: Option<String>,
    pub last_used: i64, // In ms.
    #[uniffi(default = false)]
    pub inactive: bool,
    #[uniffi(default = false)]
    pub pinned: bool,
    /// The index within the window_id.
    #[uniffi(default = 0)]
    pub index: u32,
    #[uniffi(default = "")]
    pub window_id: String,
    #[uniffi(default = "")]
    pub tab_group_id: String,
}
pub type RemoteTab = RemoteTabRecord;

#[derive(Clone, Debug, uniffi::Record)]
pub struct ClientRemoteTabs {
    /// misnamed: this is the fxa_device_id of the client (which may or may not be the same as the corresponding ID in the `clients` collection)
    pub client_id: String,
    pub client_name: String,
    pub device_type: DeviceType,
    /// Number of ms since the unix epoch (as reported by the server's clock)
    pub last_modified: i64,
    pub remote_tabs: Vec<RemoteTab>,
    pub tab_groups: HashMap<String, TabGroup>,
    pub windows: HashMap<String, Window>,
    #[uniffi(default = false)]
    pub tabs_disabled: bool,
}

#[derive(uniffi::Enum, Clone, Debug, Default)]
#[repr(u8)]
pub enum WindowType {
    #[default]
    Normal = 0,
}

impl From<u8> for WindowType {
    fn from(value: u8) -> Self {
        match value {
            0 => WindowType::Normal,
            _ => {
                warn!("Unknown window type {}, defaulting to Normal", value);
                WindowType::Normal
            }
        }
    }
}

#[derive(uniffi::Record, Debug, Clone)]
#[cfg_attr(test, derive(Default))]
pub struct Window {
    pub id: String,
    pub last_used: Timestamp,
    pub index: u32,
    pub window_type: WindowType,
}

/// A tab-group, representing a session store `TabGroupStateData`.
#[derive(uniffi::Record, Debug, Clone)]
#[cfg_attr(test, derive(Default))]
pub struct TabGroup {
    pub id: String,
    pub name: String,
    pub color: String,
    pub collapsed: bool,
}

// This is what we expect clients to supply as their own tabs.
#[derive(uniffi::Record, Debug, Clone, Default)]
pub struct LocalTabsInfo {
    pub tabs: Vec<RemoteTab>,
    pub tab_groups: HashMap<String, TabGroup>,
    pub windows: HashMap<String, Window>,
    #[uniffi(default = false)]
    pub tabs_disabled: bool,
}

pub(crate) enum DbConnection {
    Created,
    Open(Connection),
    Closed,
}

// Tabs has unique requirements for storage:
// * The "local_tabs" exist only so we can sync them out. There's no facility to
//   query "local tabs", so there's no need to store these persistently - ie, they
//   are write-only.
// * The "remote_tabs" exist purely for incoming items via sync - there's no facility
//   to set them locally - they are read-only.
// Note that this means a database is only actually needed after Sync fetches remote tabs,
// and because sync users are in the minority, the use of a database here is purely
// optional and created on demand. The implication here is that asking for the "remote tabs"
// when no database exists is considered a normal situation and just implies no remote tabs exist.
// (Note however we don't attempt to remove the database when no remote tabs exist, so having
// no remote tabs in an existing DB is also a normal situation)
pub struct TabsStorage {
    pub(crate) local_tabs: RefCell<Option<LocalTabsInfo>>,
    db_path: PathBuf,
    db_connection: DbConnection,
}

impl TabsStorage {
    pub fn new(db_path: impl AsRef<Path>) -> Self {
        Self {
            local_tabs: RefCell::default(),
            db_path: db_path.as_ref().to_path_buf(),
            db_connection: DbConnection::Created,
        }
    }

    pub fn close(&mut self) {
        if let DbConnection::Open(conn) =
            std::mem::replace(&mut self.db_connection, DbConnection::Closed)
        {
            if let Err(err) = conn.close() {
                // Log the error, but continue with shutdown
                error!("Failed to close the connection: {:?}", err);
            }
        }
    }

    /// Arrange for a new memory-based TabsStorage. As per other DB semantics, creating
    /// this isn't enough to actually create the db!
    pub fn new_with_mem_path(db_path: &str) -> Self {
        let name = PathBuf::from(format!("file:{}?mode=memory&cache=shared", db_path));
        Self::new(name)
    }

    /// If a DB file exists, open and return it.
    pub fn open_if_exists(&mut self) -> Result<Option<&Connection>> {
        match self.db_connection {
            DbConnection::Open(ref conn) => return Ok(Some(conn)),
            DbConnection::Closed => return Ok(None),
            DbConnection::Created => {}
        }
        let flags = OpenFlags::SQLITE_OPEN_NO_MUTEX
            | OpenFlags::SQLITE_OPEN_URI
            | OpenFlags::SQLITE_OPEN_READ_WRITE;
        match open_database_with_flags(
            self.db_path.clone(),
            flags,
            &crate::schema::TabsMigrationLogic,
        ) {
            Ok(conn) => {
                info!("tabs storage is opening an existing database");
                self.db_connection = DbConnection::Open(conn);
                match self.db_connection {
                    DbConnection::Open(ref conn) => Ok(Some(conn)),
                    _ => unreachable!("impossible value"),
                }
            }
            Err(open_database::Error::SqlError(rusqlite::Error::SqliteFailure(code, _)))
                if code.code == rusqlite::ErrorCode::CannotOpen =>
            {
                info!("tabs storage could not open an existing database and hasn't been asked to create one");
                Ok(None)
            }
            Err(e) => Err(e.into()),
        }
    }

    /// Open and return the DB, creating it if necessary.
    pub fn open_or_create(&mut self) -> Result<&Connection> {
        match self.db_connection {
            DbConnection::Open(ref conn) => return Ok(conn),
            DbConnection::Closed => return Err(Error::UnexpectedConnectionState),
            DbConnection::Created => {}
        }
        let flags = OpenFlags::SQLITE_OPEN_NO_MUTEX
            | OpenFlags::SQLITE_OPEN_URI
            | OpenFlags::SQLITE_OPEN_READ_WRITE
            | OpenFlags::SQLITE_OPEN_CREATE;
        let conn = open_database_with_flags(
            self.db_path.clone(),
            flags,
            &crate::schema::TabsMigrationLogic,
        )?;
        info!("tabs storage is creating a database connection");
        self.db_connection = DbConnection::Open(conn);
        match self.db_connection {
            DbConnection::Open(ref conn) => Ok(conn),
            _ => unreachable!("We just set to Open, this should be impossible."),
        }
    }

    pub fn update_local_state(&mut self, local_state: LocalTabsInfo) {
        let num_tabs = local_state.tabs.len();
        self.local_tabs.borrow_mut().replace(local_state);
        info!("update_local_state has {num_tabs} tab entries");
    }

    pub fn get_remote_tabs(&mut self) -> Option<Vec<ClientRemoteTabs>> {
        let conn = match self.open_if_exists() {
            Err(e) => {
                error_support::report_error!(
                    "tabs-read-remote",
                    "Failed to read remote tabs: {}",
                    e
                );
                return None;
            }
            Ok(None) => return None,
            Ok(Some(conn)) => conn,
        };

        let records: Vec<(TabsRecord, ServerTimestamp)> = match conn.query_rows_and_then_cached(
            "SELECT record, last_modified FROM tabs",
            [],
            |row| -> Result<_> {
                Ok((
                    serde_json::from_str(&row.get::<_, String>(0)?)?,
                    ServerTimestamp(row.get::<_, i64>(1)?),
                ))
            },
        ) {
            Ok(records) => records,
            Err(e) => {
                error_support::report_error!("tabs-read-remote", "Failed to read database: {}", e);
                return None;
            }
        };
        let mut crts: Vec<ClientRemoteTabs> = Vec::new();
        let remote_clients: HashMap<String, RemoteClient> =
            match self.get_meta::<String>(schema::REMOTE_CLIENTS_KEY) {
                Err(e) => {
                    error_support::report_error!(
                        "tabs-read-remote",
                        "Failed to get remote clients: {}",
                        e
                    );
                    return None;
                }
                // We don't return early here since we still store tabs even if we don't
                // "know" about the client it's associated with (incase it becomes available later)
                Ok(None) => HashMap::default(),
                Ok(Some(json)) => serde_json::from_str(&json).unwrap(),
            };
        for (record, last_modified) in records {
            let id = record.id.clone();
            if let Some(remote_client) = remote_clients.get(&id) {
                crts.push(ClientRemoteTabs::from_record(
                    remote_client
                        .fxa_device_id
                        .as_ref()
                        .unwrap_or(&id)
                        .to_owned(),
                    last_modified,
                    remote_client,
                    record,
                ));
            } else {
                // As per https://github.com/mozilla/application-services/issues/5199, we don't want to store these.
                warn!("Dropping tabs from an unknown client: {id}");
            };
        }
        // Filter out any tabs the user requested to be closed on other devices but those devices
        // have not yet actually closed the tab, so we hide them from the user until such time
        // Should we add a flag here to give the call an option of not doing this?
        let filtered_crts = self.filter_pending_remote_tabs(crts);
        Some(filtered_crts)
    }

    fn filter_pending_remote_tabs(&mut self, crts: Vec<ClientRemoteTabs>) -> Vec<ClientRemoteTabs> {
        let conn = match self.open_if_exists() {
            Err(e) => {
                error_support::report_error!(
                    "tabs-read-remote",
                    "Failed to read remote tabs: {}",
                    e
                );
                return crts;
            }
            Ok(None) => return crts,
            Ok(Some(conn)) => conn,
        };
        let pending_tabs_result: Result<Vec<(String, String)>> = conn.query_rows_and_then_cached(
            "SELECT device_id, url
             FROM remote_tab_commands
             WHERE command = :command_close_tab",
            rusqlite::named_params! { ":command_close_tab": CommandKind::CloseTab },
            |row| {
                Ok((
                    row.get::<_, String>(0)?, // device_id
                    row.get::<_, String>(1)?, // url
                ))
            },
        );
        // Make a hash map of all urls per client_id that we potentially want to filter
        let pending_closures = match pending_tabs_result {
            Ok(pending_closures) => pending_closures.into_iter().fold(
                HashMap::new(),
                |mut acc: HashMap<String, Vec<String>>, (device_id, url)| {
                    acc.entry(device_id).or_default().push(url);
                    acc
                },
            ),
            Err(e) => {
                error_support::report_error!("tabs-read-remote", "Failed to read database: {}", e);
                return crts;
            }
        };
        // Check if any of the client records that were passed in have urls that the user closed
        // This means that they requested to close those tabs but those devices have not yet got
        // actually closed the tabs
        let filtered_crts: Vec<ClientRemoteTabs> = crts
            .into_iter()
            .map(|mut crt| {
                crt.remote_tabs.retain(|tab| {
                    // The top level in the url_history is the "active" tab, which we should use
                    // TODO: probably not the best way to url check
                    !pending_closures
                        .get(&crt.client_id)
                        .is_some_and(|urls| urls.contains(&tab.url_history[0]))
                });
                crt
            })
            .collect();
        // Return the filtered crts
        filtered_crts
    }

    // Keep DB from growing infinitely since we only ask for records since our last sync
    // and may or may not know about the client it's associated with -- but we could at some point
    // and should start returning those tabs immediately. If that client hasn't been seen in 3 weeks,
    // we remove it until it reconnects
    pub fn remove_stale_clients(&mut self) -> Result<()> {
        let last_sync = self.get_meta::<i64>(schema::LAST_SYNC_META_KEY)?;
        if let Some(conn) = self.open_if_exists()? {
            if let Some(last_sync) = last_sync {
                let client_ttl_ms = (TABS_CLIENT_TTL as i64) * 1000;
                // On desktop, a quick write temporarily sets the last_sync to FAR_FUTURE
                // but if it doesn't set it back to the original (crash, etc) it
                // means we'll most likely trash all our records (as it's more than any TTL we'd ever do)
                // so we need to detect this for now until we have native quick write support
                if last_sync - client_ttl_ms >= 0 && last_sync != (FAR_FUTURE * 1000) {
                    let tx = conn.unchecked_transaction()?;
                    let num_removed = tx.execute_cached(
                        "DELETE FROM tabs WHERE last_modified <= :last_sync - :ttl",
                        rusqlite::named_params! {
                            ":last_sync": last_sync,
                            ":ttl": client_ttl_ms,
                        },
                    )?;
                    info!(
                        "removed {} stale clients (threshold was {})",
                        num_removed,
                        last_sync - client_ttl_ms
                    );
                    tx.commit()?;
                }
            }
        }
        Ok(())
    }

    pub(crate) fn replace_remote_tabs(
        &mut self,
        // This is a tuple because we need to know what the server reports
        // as the last time a record was modified
        new_remote_tabs: &Vec<(TabsRecord, ServerTimestamp)>,
    ) -> Result<()> {
        let connection = self.open_or_create()?;
        let tx = connection.unchecked_transaction()?;

        // For tabs it's fine if we override the existing tabs for a remote
        // there can only ever be one record for each client
        for remote_tab in new_remote_tabs {
            let record = &remote_tab.0;
            let last_modified = remote_tab.1;
            info!(
                "inserting tab for device {}, last modified at {}",
                record.id,
                last_modified.as_millis()
            );
            tx.execute_cached(
                "INSERT OR REPLACE INTO tabs (guid, record, last_modified) VALUES (:guid, :record, :last_modified);",
                rusqlite::named_params! {
                    ":guid": &record.id,
                    ":record": serde_json::to_string(&record).expect("tabs don't fail to serialize"),
                    ":last_modified": last_modified.as_millis()
                },
            )?;
        }
        tx.commit()?;
        Ok(())
    }

    pub(crate) fn wipe_remote_tabs(&mut self) -> Result<()> {
        if let Some(db) = self.open_if_exists()? {
            db.execute_batch("DELETE FROM tabs")?;
        }
        Ok(())
    }

    pub(crate) fn wipe_local_tabs(&self) {
        self.local_tabs.replace(None);
    }

    pub(crate) fn put_meta(&mut self, key: &str, value: &dyn ToSql) -> Result<()> {
        let db = self.open_or_create()?;
        db.execute_cached(
            "REPLACE INTO moz_meta (key, value) VALUES (:key, :value)",
            &[(":key", &key as &dyn ToSql), (":value", value)],
        )?;
        Ok(())
    }

    pub(crate) fn get_meta<T: FromSql>(&mut self, key: &str) -> Result<Option<T>> {
        match self.open_if_exists() {
            Ok(Some(db)) => {
                let res = db.try_query_one(
                    "SELECT value FROM moz_meta WHERE key = :key",
                    &[(":key", &key)],
                    true,
                )?;
                Ok(res)
            }
            Err(e) => Err(e),
            Ok(None) => Ok(None),
        }
    }

    pub(crate) fn delete_meta(&mut self, key: &str) -> Result<()> {
        if let Some(db) = self.open_if_exists()? {
            db.execute_cached("DELETE FROM moz_meta WHERE key = :key", &[(":key", &key)])?;
        }
        Ok(())
    }
}

// Implementations related to storage of remotely closing remote tabs.
// We should probably split this module!
impl TabsStorage {
    /// Store tabs that we requested to close on other devices but
    /// not yet executed on target device, other calls like getAll()
    /// will check against this table to filter out any urls
    pub fn add_remote_tab_command(
        &mut self,
        device_id: &str,
        command: &RemoteCommand,
    ) -> Result<bool> {
        self.add_remote_tab_command_at(device_id, command, Timestamp::now())
    }

    pub fn add_remote_tab_command_at(
        &mut self,
        device_id: &str,
        command: &RemoteCommand,
        time_requested: Timestamp,
    ) -> Result<bool> {
        let connection = self.open_or_create()?;
        let RemoteCommand::CloseTab { url } = command;
        info!("Adding remote command for {device_id} at {time_requested}");
        trace!("command is {command:?}");
        // tx maybe not needed for single write?
        let tx = connection.unchecked_transaction()?;
        let changes = tx.execute_cached(
            "INSERT OR IGNORE INTO remote_tab_commands
                (device_id, command, url, time_requested, time_sent)
            VALUES (:device_id, :command, :url, :time_requested, null)",
            rusqlite::named_params! {
                ":device_id": &device_id,
                ":url": url,
                ":time_requested": time_requested,
                ":command": command.as_ref(),
            },
        )?;
        tx.commit()?;
        Ok(changes != 0)
    }

    pub fn remove_remote_tab_command(
        &mut self,
        device_id: &str,
        command: &RemoteCommand,
    ) -> Result<bool> {
        let connection = self.open_or_create()?;
        let RemoteCommand::CloseTab { url } = command;
        info!("removing remote tab close details: client={device_id}");
        let tx = connection.unchecked_transaction()?;
        let changes = tx.execute_cached(
            "DELETE FROM remote_tab_commands
             WHERE device_id = :device_id AND command = :command AND url = :url;",
            rusqlite::named_params! {
                ":device_id": &device_id,
                ":url": url,
                ":command": command.as_ref(),
            },
        )?;
        tx.commit()?;
        Ok(changes != 0)
    }

    pub fn get_unsent_commands(&mut self) -> Result<Vec<PendingCommand>> {
        self.do_get_pending_commands("WHERE time_sent IS NULL")
    }

    fn do_get_pending_commands(&mut self, where_clause: &str) -> Result<Vec<PendingCommand>> {
        let Some(conn) = self.open_if_exists()? else {
            return Ok(Vec::new());
        };
        let result = conn.query_rows_and_then_cached(
            &format!(
                "SELECT device_id, command, url, time_requested, time_sent
                    FROM remote_tab_commands
                    {where_clause}
                    ORDER BY time_requested
                    LIMIT 1000 -- sue me!"
            ),
            [],
            |row| -> Result<_> {
                // overly cautious I guess - ignore bad enum values rather than failing
                let command = match row.get::<_, CommandKind>(1) {
                    Ok(c) => c,
                    Err(e) => {
                        error!("do_get_pending_commands: ignoring error fetching command: {e:?}");
                        return Ok(None);
                    }
                };
                Ok(Some(match command {
                    CommandKind::CloseTab => PendingCommand {
                        device_id: row.get::<_, String>(0)?,
                        command: RemoteCommand::CloseTab {
                            url: row.get::<_, String>(2)?,
                        },
                        time_requested: row.get::<_, Timestamp>(3)?,
                        time_sent: row.get::<_, Option<Timestamp>>(4)?,
                    },
                }))
            },
        );
        Ok(match result {
            Ok(records) => records.into_iter().flatten().collect(),
            Err(e) => {
                error_support::report_error!("tabs-get_unsent", "Failed to read database: {}", e);
                Vec::new()
            }
        })
    }

    pub fn set_pending_command_sent(&mut self, command: &PendingCommand) -> Result<bool> {
        let connection = self.open_or_create()?;
        let RemoteCommand::CloseTab { url } = &command.command;
        info!("setting remote tab sent: client={}", command.device_id);
        trace!("command: {command:?}");
        let tx = connection.unchecked_transaction()?;
        let ts = Timestamp::now();
        let changes = tx.execute_cached(
            "UPDATE remote_tab_commands
             SET time_sent = :ts
             WHERE device_id = :device_id AND command = :command AND url = :url;",
            rusqlite::named_params! {
                ":command": command.command.as_ref(),
                ":device_id": &command.device_id,
                ":url": url,
                ":ts": &ts,
            },
        )?;
        tx.commit()?;
        Ok(changes != 0)
    }

    // Remove any pending closures that were completed - the device's new record no
    // longer has the tab - or that have timed out, meaning the device has written a
    // record more than REMOTE_COMMAND_TTL_MS after the closure was requested.
    pub fn remove_old_pending_closures(
        &mut self,
        // This is a tuple because we need to know what the server reports
        // as the last time a record was modified
        new_remote_tabs: &[(TabsRecord, ServerTimestamp)],
    ) -> Result<()> {
        // Commands are keyed by fxa device ID, but records (and the `tabs` table)
        // by sync client ID, so we need the map of client-id -> RemoteClient.
        let remote_clients: HashMap<String, RemoteClient> = {
            match self.get_meta::<String>(schema::REMOTE_CLIENTS_KEY)? {
                None => HashMap::default(),
                Some(json) => serde_json::from_str(&json).unwrap(),
            }
        };
        let fxa_device_id = |client_id: &str| -> String {
            remote_clients
                .get(client_id)
                .and_then(|r| r.fxa_device_id.clone())
                .unwrap_or_else(|| client_id.to_string())
        };

        let conn = self.open_or_create()?;
        let tx = conn.unchecked_transaction()?;

        // Every device in the batch, and every tab each one still has open. The
        // devices are separate as a device with no tabs completed all its closures.
        tx.execute_batch(
            "CREATE TEMP TABLE IF NOT EXISTS new_remote_devices (device_id TEXT PRIMARY KEY);
             CREATE TEMP TABLE IF NOT EXISTS new_remote_tabs (device_id TEXT, url TEXT);
             DELETE FROM new_remote_devices;
             DELETE FROM new_remote_tabs;",
        )?;
        for (record, _) in new_remote_tabs.iter() {
            let device_id = fxa_device_id(&record.id);
            tx.execute_cached(
                "INSERT OR IGNORE INTO new_remote_devices (device_id) VALUES (?)",
                [&device_id],
            )?;
            for tab in &record.tabs {
                if let Some(url) = tab.url_history.first() {
                    tx.execute_cached(
                        "INSERT INTO new_remote_tabs (device_id, url) VALUES (?, ?)",
                        rusqlite::params![device_id, url],
                    )?;
                }
            }
        }

        let num_completed = tx.execute_cached(
            "DELETE FROM remote_tab_commands
             WHERE command = :command_close_tab
               AND device_id IN (SELECT device_id FROM new_remote_devices)
               AND url NOT IN (
                   SELECT url FROM new_remote_tabs
                   WHERE new_remote_tabs.device_id = remote_tab_commands.device_id
               )",
            rusqlite::named_params! {
                ":command_close_tab": CommandKind::CloseTab,
            },
        )?;
        info!("deleted {num_completed} pending tab closures because they were not in the new tabs");

        // Anything that couldn't be removed above, where the device has written a
        // record more than REMOTE_COMMAND_TTL_MS after the command was requested, is
        // assumed not closeable and we can remove it from the list.
        let devices: Vec<(String, i64)> = tx.query_rows_and_then_cached(
            "SELECT guid, last_modified FROM tabs",
            [],
            |row| -> rusqlite::Result<_> { Ok((row.get(0)?, row.get(1)?)) },
        )?;
        let mut num_timed_out = 0;
        for (client_id, last_modified) in devices {
            num_timed_out += tx.execute_cached(
                "DELETE FROM remote_tab_commands
                 WHERE device_id = :device_id
                   AND :last_modified - time_requested >= :ttl",
                rusqlite::named_params! {
                    ":device_id": fxa_device_id(&client_id),
                    ":last_modified": last_modified,
                    ":ttl": REMOTE_COMMAND_TTL_MS,
                },
            )?;
        }
        info!("deleted {num_timed_out} pending commands because they timed out");

        // Commit changes and clean up temp
        tx.commit()?;
        conn.execute_batch("DROP TABLE new_remote_devices; DROP TABLE new_remote_tabs;")?;
        Ok(())
    }
}

// Simple enum for the DB.
#[derive(Debug, Copy, Clone)]
#[repr(u8)]
enum CommandKind {
    CloseTab = 0,
}

impl AsRef<CommandKind> for RemoteCommand {
    // Required method
    fn as_ref(&self) -> &CommandKind {
        match self {
            RemoteCommand::CloseTab { .. } => &CommandKind::CloseTab,
        }
    }
}

impl FromSql for CommandKind {
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        Ok(match value.as_i64()? {
            0 => CommandKind::CloseTab,
            _ => return Err(FromSqlError::InvalidType),
        })
    }
}

impl ToSql for CommandKind {
    fn to_sql(&self) -> rusqlite::Result<ToSqlOutput<'_>> {
        Ok(ToSqlOutput::from(*self as u8))
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::*;
    use crate::{sync::record::TabsRecordTab, PendingCommand};

    impl RemoteCommand {
        fn close_tab(url: &str) -> Self {
            RemoteCommand::CloseTab {
                url: url.to_string(),
            }
        }
    }

    #[test]
    fn test_open_if_exists_no_file() {
        error_support::init_for_tests();
        let dir = tempfile::tempdir().unwrap();
        let db_name = dir.path().join("test_open_for_read_no_file.db");
        let mut storage = TabsStorage::new(db_name.clone());
        assert!(storage.open_if_exists().unwrap().is_none());
        storage.open_or_create().unwrap(); // will have created it.
                                           // make a new storage, but leave the file alone.
        let mut storage = TabsStorage::new(db_name);
        // db file exists, so opening for read should open it.
        assert!(storage.open_if_exists().unwrap().is_some());
    }

    #[test]
    fn test_tabs_meta() {
        error_support::init_for_tests();
        let dir = tempfile::tempdir().unwrap();
        let db_name = dir.path().join("test_tabs_meta.db");
        let mut db = TabsStorage::new(db_name);
        let test_key = "TEST KEY A";
        let test_value = "TEST VALUE A";
        let test_key2 = "TEST KEY B";
        let test_value2 = "TEST VALUE B";

        // should automatically make the DB if one doesn't exist
        db.put_meta(test_key, &test_value).unwrap();
        db.put_meta(test_key2, &test_value2).unwrap();

        let retrieved_value: String = db.get_meta(test_key).unwrap().expect("test value");
        let retrieved_value2: String = db.get_meta(test_key2).unwrap().expect("test value 2");

        assert_eq!(retrieved_value, test_value);
        assert_eq!(retrieved_value2, test_value2);

        // check that the value of an existing key can be updated
        let test_value3 = "TEST VALUE C";
        db.put_meta(test_key, &test_value3).unwrap();

        let retrieved_value3: String = db.get_meta(test_key).unwrap().expect("test value 3");

        assert_eq!(retrieved_value3, test_value3);

        // check that a deleted key is not retrieved
        db.delete_meta(test_key).unwrap();
        let retrieved_value4: Option<String> = db.get_meta(test_key).unwrap();
        assert!(retrieved_value4.is_none());
    }

    // Helper struct to model what's stored in the DB
    struct TabsSQLRecord {
        guid: String,
        record: TabsRecord,
        last_modified: i64,
    }
    #[test]
    fn test_remove_stale_clients() {
        error_support::init_for_tests();
        let dir = tempfile::tempdir().unwrap();
        let db_name = dir.path().join("test_remove_stale_clients.db");
        let mut storage = TabsStorage::new(db_name);
        storage.open_or_create().unwrap();
        assert!(storage.open_if_exists().unwrap().is_some());

        // info about the remote clients.
        let recent_clients = HashMap::from([
            (
                "device-1".to_string(),
                RemoteClient {
                    fxa_device_id: None,
                    device_name: "my device".to_string(),
                    device_type: sync15::DeviceType::Unknown,
                },
            ),
            (
                "device-outdated".to_string(),
                RemoteClient {
                    fxa_device_id: None,
                    device_name: "device with no tabs".to_string(),
                    device_type: DeviceType::Unknown,
                },
            ),
        ]);
        storage
            .put_meta(
                schema::REMOTE_CLIENTS_KEY,
                &serde_json::to_string(&recent_clients).unwrap(),
            )
            .unwrap();

        let records = vec![
            TabsSQLRecord {
                guid: "device-1".to_string(),
                record: TabsRecord {
                    id: "device-1".to_string(),
                    ..Default::default()
                },
                last_modified: 1643764207000,
            },
            TabsSQLRecord {
                guid: "device-outdated".to_string(),
                record: TabsRecord {
                    id: "device-outdated".to_string(),
                    client_name: "Device outdated".to_string(),
                    ..Default::default()
                },
                last_modified: 1443764207000, // old
            },
        ];
        let db = storage.open_if_exists().unwrap().unwrap();
        for record in records {
            db.execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES (:guid, :record, :last_modified);",
                rusqlite::named_params! {
                    ":guid": &record.guid,
                    ":record": serde_json::to_string(&record.record).unwrap(),
                    ":last_modified": &record.last_modified,
                },
            ).unwrap();
        }
        // pretend we just synced
        let last_synced = 1643764207000_i64;
        storage
            .put_meta(schema::LAST_SYNC_META_KEY, &last_synced)
            .unwrap();
        storage.remove_stale_clients().unwrap();

        let remote_tabs = storage.get_remote_tabs().unwrap();
        // We should've removed the outdated device
        assert_eq!(remote_tabs.len(), 1);
        // Assert the correct record is still being returned
        assert_eq!(remote_tabs[0].client_id, "device-1");
    }

    fn pending_url_command(device_id: &str, url: &str, ts: Timestamp) -> PendingCommand {
        PendingCommand {
            device_id: device_id.to_string(),
            command: RemoteCommand::CloseTab {
                url: url.to_string(),
            },
            time_requested: ts,
            time_sent: None,
        }
    }

    #[test]
    fn test_add_pending_dupe_simple() {
        error_support::init_for_tests();
        let mut storage = TabsStorage::new_with_mem_path("test_add_pending_dupe_simple");
        let command = RemoteCommand::close_tab("https://example1.com");
        // returns a bool to say if it's new or not.
        assert!(storage
            .add_remote_tab_command("device-1", &command)
            .expect("should work"));
        assert!(!storage
            .add_remote_tab_command("device-1", &command)
            .expect("should work"));
        assert!(storage
            .remove_remote_tab_command("device-1", &command)
            .expect("should work"));
        assert!(storage
            .add_remote_tab_command("device-1", &command)
            .expect("should work"));
    }

    #[test]
    fn test_add_pending_remote_close() {
        error_support::init_for_tests();
        let mut storage = TabsStorage::new_with_mem_path("test_add_pending_remote_close");
        storage.open_or_create().unwrap();
        assert!(storage.open_if_exists().unwrap().is_some());

        let now = Timestamp::now();
        let earliest = now.checked_sub(Duration::from_millis(1)).unwrap();
        let later = now.checked_add(Duration::from_millis(1)).unwrap();
        let latest = now.checked_add(Duration::from_millis(2)).unwrap();
        // The tabs requested to to be closed. We'll insert them in the "wrong" order
        // relative to their time-stamp.
        storage
            .add_remote_tab_command_at(
                "device-1",
                &RemoteCommand::close_tab("https://example1.com"),
                latest,
            )
            .expect("should work");
        storage
            .add_remote_tab_command_at(
                "device-1",
                &RemoteCommand::close_tab("https://example2.com"),
                earliest,
            )
            .expect("should work");
        storage
            .add_remote_tab_command_at(
                "device-2",
                &RemoteCommand::close_tab("https://example2.com"),
                now,
            )
            .expect("should work");
        storage
            .add_remote_tab_command_at(
                "device-2",
                &RemoteCommand::close_tab("https://example3.com"),
                later,
            )
            .expect("should work");

        let got = storage.get_unsent_commands().unwrap();

        assert_eq!(got.len(), 4);
        assert_eq!(
            got,
            vec![
                pending_url_command("device-1", "https://example2.com", earliest),
                pending_url_command("device-2", "https://example2.com", now),
                pending_url_command("device-2", "https://example3.com", later),
                pending_url_command("device-1", "https://example1.com", latest),
            ]
        );
    }

    #[test]
    fn test_remote_tabs_filters_pending_closures() {
        error_support::init_for_tests();
        let mut storage =
            TabsStorage::new_with_mem_path("test_remote_tabs_filters_pending_closures");
        let records = vec![
            TabsSQLRecord {
                guid: "device-1".to_string(),
                record: TabsRecord {
                    id: "device-1".to_string(),
                    client_name: "Device #1".to_string(),
                    tabs: vec![TabsRecordTab {
                        title: "the title".to_string(),
                        url_history: vec!["https://mozilla.org/".to_string()],
                        icon: Some("https://mozilla.org/icon".to_string()),
                        last_used: 1711929600015, // 4/1/2024
                        ..Default::default()
                    }],
                    ..Default::default()
                },
                last_modified: 1711929600015, // 4/1/2024
            },
            TabsSQLRecord {
                guid: "device-2".to_string(),
                record: TabsRecord {
                    id: "device-2".to_string(),
                    client_name: "Another device".to_string(),
                    tabs: vec![
                        TabsRecordTab {
                            title: "the title".to_string(),
                            url_history: vec!["https://mozilla.org/".to_string()],
                            icon: Some("https://mozilla.org/icon".to_string()),
                            last_used: 1711929600015, // 4/1/2024
                            ..Default::default()
                        },
                        TabsRecordTab {
                            title: "the title".to_string(),
                            url_history: vec![
                                "https://example.com/".to_string(),
                                "https://example1.com/".to_string(),
                            ],
                            icon: None,
                            last_used: 1711929600015, // 4/1/2024
                            ..Default::default()
                        },
                        TabsRecordTab {
                            title: "the title".to_string(),
                            url_history: vec!["https://example1.com/".to_string()],
                            icon: None,
                            last_used: 1711929600015, // 4/1/2024
                            ..Default::default()
                        },
                    ],
                    ..Default::default()
                },
                last_modified: 1711929600015, // 4/1/2024
            },
        ];

        // info about the remote clients.
        let recent_clients = HashMap::from([
            (
                "device-1".to_string(),
                RemoteClient {
                    fxa_device_id: None,
                    device_name: "my device".to_string(),
                    device_type: sync15::DeviceType::Unknown,
                },
            ),
            (
                "device-2".to_string(),
                RemoteClient {
                    fxa_device_id: None,
                    device_name: "device with no tabs".to_string(),
                    device_type: DeviceType::Unknown,
                },
            ),
        ]);
        storage
            .put_meta(
                schema::REMOTE_CLIENTS_KEY,
                &serde_json::to_string(&recent_clients).unwrap(),
            )
            .unwrap();

        let db = storage.open_if_exists().unwrap().unwrap();
        for record in records {
            db.execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES (:guid, :record, :last_modified);",
                rusqlite::named_params! {
                    ":guid": &record.guid,
                    ":record": serde_json::to_string(&record.record).unwrap(),
                    ":last_modified": &record.last_modified,
                },
            ).unwrap();
        }

        // Some tabs were requested to be closed
        storage
            .add_remote_tab_command(
                "device-1",
                &RemoteCommand::close_tab("https://mozilla.org/"),
            )
            .unwrap();
        storage
            .add_remote_tab_command(
                "device-2",
                &RemoteCommand::close_tab("https://example.com/"),
            )
            .unwrap();
        storage
            .add_remote_tab_command(
                "device-2",
                &RemoteCommand::close_tab("https://example1.com/"),
            )
            .unwrap();

        let remote_tabs = storage.get_remote_tabs().unwrap();

        assert_eq!(remote_tabs.len(), 2);

        // Device 1 had only 1 tab synced, we remotely closed it, so we expect no tabs
        assert_eq!(remote_tabs[0].client_id, "device-1");
        assert_eq!(remote_tabs[0].remote_tabs.len(), 0);

        // Device 2 had 3 tabs open and we remotely closed 2, so we expect 1 tab returned
        assert_eq!(remote_tabs[1].client_id, "device-2");
        assert_eq!(remote_tabs[1].remote_tabs.len(), 1);
        assert_eq!(
            remote_tabs[1].remote_tabs[0],
            RemoteTab {
                title: "the title".to_string(),
                url_history: vec!["https://mozilla.org/".to_string()],
                icon: Some("https://mozilla.org/icon".to_string()),
                last_used: 1711929600015000, //server time is ns, so 1000 bigger than local.
                ..Default::default()
            }
        );
    }

    #[test]
    fn test_remove_old_pending_closures_timed_removal() {
        error_support::init_for_tests();
        let mut storage =
            TabsStorage::new_with_mem_path("test_remove_old_pending_closures_timed_removal");

        let now = Timestamp::now();
        let older = now
            .checked_sub(Duration::from_millis(REMOTE_COMMAND_TTL_MS))
            .unwrap();

        {
            let db = storage.open_if_exists().unwrap().unwrap();

            // We manually insert two devices, one that hasn't updated in awhile and one that's
            // updated recently
            db.execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES ('device-synced', '', :now);",
                rusqlite::named_params! {
                    ":now" : now,
                },
            )
            .unwrap();

            db.execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES ('device-not-synced', '', :old);",
                    rusqlite::named_params! {
                        ":old" : older,
                    },
            ).unwrap();
        }
        // We also manually insert some pending remote tab closures, we specifically add a recent one
        // and one that is 48hrs older since that device updated, which should get removed
        storage
            .add_remote_tab_command_at(
                "device-synced",
                &RemoteCommand::close_tab("https://example.com"),
                older,
            )
            .unwrap();

        storage
            .add_remote_tab_command_at(
                "device-not-synced",
                &RemoteCommand::close_tab("https://example2.com"),
                now,
            )
            .unwrap();

        {
            let db = storage.open_if_exists().unwrap().unwrap();

            // Verify we actually have 2 pending closures
            let before_count: i64 = db
                .conn_ext_query_one("SELECT COUNT(*) FROM remote_tab_commands")
                .unwrap();
            assert_eq!(before_count, 2);
        }
        // "incoming" records from other devices
        let new_records = vec![(
            TabsRecord {
                id: "device-not-synced".to_string(),
                tabs: vec![TabsRecordTab {
                    url_history: vec!["https://example2.com".to_string()],
                    ..Default::default()
                }],
                ..Default::default()
            },
            ServerTimestamp::from_millis(now.as_millis_i64()),
        )];
        // Cleanup old pending closures
        storage.remove_old_pending_closures(&new_records).unwrap();

        let reopen_db = storage.open_if_exists().unwrap().unwrap();
        let after_count: i64 = reopen_db
            .conn_ext_query_one("SELECT COUNT(*) FROM remote_tab_commands")
            .unwrap();
        assert_eq!(after_count, 1);

        let remaining_device_id: String = reopen_db
            .conn_ext_query_one("SELECT device_id FROM remote_tab_commands")
            .unwrap();

        // Only the device that still hasn't synced keeps
        assert_eq!(remaining_device_id, "device-not-synced");
    }
    #[test]
    fn test_remove_old_pending_closures_no_tab_removal() {
        error_support::init_for_tests();
        let mut storage =
            TabsStorage::new_with_mem_path("test_remove_old_pending_closures_no_tab_removal");
        let db = storage.open_if_exists().unwrap().unwrap();

        let now_ms: u64 = Timestamp::now().as_millis();

        // Set up the initial state with tabs that have been synced recently
        db.execute(
            "INSERT INTO tabs (guid, record, last_modified) VALUES ('device-recent', '', :now);",
            rusqlite::named_params! {
                ":now": now_ms,
            },
        )
        .unwrap();

        // Insert pending closures for a device
        db.execute(
        "INSERT INTO remote_tab_commands (device_id, command, url, time_requested) VALUES (:device_id, :command, :url, :time_requested)",
        rusqlite::named_params! {
            ":command": CommandKind::CloseTab,
            ":device_id": "device-recent",
            ":url": "https://example.com",
            ":time_requested": now_ms,
        },
    ).unwrap();

        db.execute(
        "INSERT INTO remote_tab_commands (device_id, command, url, time_requested) VALUES (:device_id, :command, :url, :time_requested)",
        rusqlite::named_params! {
            ":command": CommandKind::CloseTab,
            ":device_id": "device-recent",
            ":url": "https://old-url.com",
            ":time_requested": now_ms,
        },
    ).unwrap();

        // Verify initial state has 2 pending closures
        let before_count: i64 = db
            .query_row("SELECT COUNT(*) FROM remote_tab_commands", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert_eq!(before_count, 2);

        // Simulate incoming data that no longer includes one of the URLs
        let new_records = vec![(
            TabsRecord {
                id: "device-recent".to_string(),
                tabs: vec![
                    TabsRecordTab {
                        url_history: vec!["https://example99.com".to_string()],
                        ..Default::default()
                    },
                    TabsRecordTab {
                        url_history: vec!["https://example.com".to_string()],
                        ..Default::default()
                    },
                ],
                ..Default::default()
            },
            ServerTimestamp::default(),
        )];

        // Perform the cleanup
        storage.remove_old_pending_closures(&new_records).unwrap();

        // need to reopen db to avoid mutable errors
        let reopen_db = storage.open_if_exists().unwrap().unwrap();
        // Check results after cleanup
        let after_count: i64 = reopen_db
            .query_row("SELECT COUNT(*) FROM remote_tab_commands", [], |row| {
                row.get(0)
            })
            .unwrap();
        assert_eq!(after_count, 1); // Only one entry should remain

        let remaining_url: String = reopen_db
            .query_row("SELECT url FROM remote_tab_commands", [], |row| row.get(0))
            .unwrap();

        assert_eq!(remaining_url, "https://example.com"); // The URL still present in new_records should remain
    }

    #[test]
    fn test_remove_pending_command() {
        error_support::init_for_tests();
        let mut storage = TabsStorage::new_with_mem_path("test_remove_pending_command");
        storage.open_or_create().unwrap();
        assert!(storage.open_if_exists().unwrap().is_some());

        storage
            .add_remote_tab_command(
                "device-1",
                &RemoteCommand::close_tab("https://example1.com"),
            )
            .expect("should work");

        assert_eq!(storage.get_unsent_commands().unwrap().len(), 1);
        assert!(!storage
            .remove_remote_tab_command(
                "no-devce",
                &RemoteCommand::close_tab("https://example1.com"),
            )
            .unwrap());
        assert_eq!(storage.get_unsent_commands().unwrap().len(), 1);

        assert!(!storage
            .remove_remote_tab_command(
                "device-1",
                &RemoteCommand::close_tab("https://example9.com"),
            )
            .unwrap());
        assert_eq!(storage.get_unsent_commands().unwrap().len(), 1);

        assert!(storage
            .remove_remote_tab_command(
                "device-1",
                &RemoteCommand::close_tab("https://example1.com"),
            )
            .unwrap());
        assert_eq!(storage.get_unsent_commands().unwrap().len(), 0);
    }

    #[test]
    fn test_sent_command() {
        error_support::init_for_tests();
        let mut storage = TabsStorage::new_with_mem_path("test_sent_command");
        let command = RemoteCommand::close_tab("https://example1.com");
        storage
            .add_remote_tab_command("device-1", &command)
            .expect("should work");

        assert_eq!(storage.get_unsent_commands().unwrap().len(), 1);
        let pending_command = PendingCommand {
            device_id: "device-1".to_string(),
            command: command.clone(),
            time_requested: Timestamp::now(),
            time_sent: None,
        };
        assert!(storage.set_pending_command_sent(&pending_command).unwrap());
        assert_eq!(storage.get_unsent_commands().unwrap().len(), 0);
        // but can't re-add it because it's still alive.
        assert!(!storage
            .add_remote_tab_command("device-1", &command)
            .unwrap());
        // can remove it.
        assert!(storage
            .remove_remote_tab_command("device-1", &command)
            .unwrap());
        // now can re-add it.
        assert!(storage
            .add_remote_tab_command("device-1", &command)
            .unwrap());
        assert_eq!(storage.get_unsent_commands().unwrap().len(), 1);
    }

    #[test]
    fn test_remove_pending_closures_only_affects_target_device() {
        error_support::init_for_tests();
        let mut storage =
            TabsStorage::new_with_mem_path("test_remove_pending_closures_target_device");
        let now = Timestamp::now();

        let db = storage.open_if_exists().unwrap().unwrap();

        // Insert two devices into the tabs db
        db.execute(
            "INSERT INTO tabs (guid, record, last_modified) VALUES ('device-1', '', :now);",
            rusqlite::named_params! { ":now" : now },
        )
        .unwrap();

        db.execute(
            "INSERT INTO tabs (guid, record, last_modified) VALUES ('device-2', '', :now);",
            rusqlite::named_params! { ":now" : now },
        )
        .unwrap();

        // Add three commands, two for device-1 and one for device-2
        storage
            .add_remote_tab_command(
                "device-1",
                &RemoteCommand::close_tab("https://example1.com"),
            )
            .unwrap();

        storage
            .add_remote_tab_command(
                "device-1",
                &RemoteCommand::close_tab("https://example2.com"),
            )
            .unwrap();

        storage
            .add_remote_tab_command(
                "device-2",
                &RemoteCommand::close_tab("https://example3.com"),
            )
            .unwrap();

        // Pretend only device-1 "synced", example2.com tab was closed
        let new_records = vec![(
            TabsRecord {
                id: "device-1".to_string(),
                tabs: vec![TabsRecordTab {
                    url_history: vec!["https://example1.com".to_string()],
                    ..Default::default()
                }],
                ..Default::default()
            },
            ServerTimestamp::default(),
        )];

        storage.remove_old_pending_closures(&new_records).unwrap();

        let reopen_db = storage.open_if_exists().unwrap().unwrap();
        let remaining_commands: Vec<(String, String)> = reopen_db
            .prepare("SELECT device_id, url FROM remote_tab_commands")
            .unwrap()
            .query_map([], |row| {
                Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
            })
            .unwrap()
            .collect::<rusqlite::Result<Vec<_>, _>>()
            .unwrap();
        // We should only have removed 1 command from the list
        assert_eq!(remaining_commands.len(), 2);
        assert!(remaining_commands
            .contains(&("device-1".to_string(), "https://example1.com".to_string())));
        assert!(remaining_commands
            .contains(&("device-2".to_string(), "https://example3.com".to_string())));
    }

    // A pending closure is only resolved by the URL still being open on the
    // device it was sent to - the same URL open on a different device in the
    // same batch must not keep it alive.
    #[test]
    fn test_remove_pending_closures_matches_urls_per_device() {
        error_support::init_for_tests();
        let mut storage =
            TabsStorage::new_with_mem_path("test_remove_pending_closures_matches_urls_per_device");
        let now = Timestamp::now();

        let db = storage.open_if_exists().unwrap().unwrap();
        for guid in ["device-1", "device-2"] {
            db.execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES (:guid, '', :now);",
                rusqlite::named_params! { ":guid": guid, ":now": now },
            )
            .unwrap();
        }

        // Each device is asked to close a different tab...
        storage
            .add_remote_tab_command("device-1", &RemoteCommand::close_tab("https://a.com"))
            .unwrap();
        storage
            .add_remote_tab_command("device-2", &RemoteCommand::close_tab("https://b.com"))
            .unwrap();

        // ...and both close it, but each still has the tab the *other* device was
        // asked to close.
        let record = |id: &str, url: &str| {
            (
                TabsRecord {
                    id: id.to_string(),
                    tabs: vec![TabsRecordTab {
                        url_history: vec![url.to_string()],
                        ..Default::default()
                    }],
                    ..Default::default()
                },
                ServerTimestamp::default(),
            )
        };
        let new_records = vec![
            record("device-1", "https://b.com"),
            record("device-2", "https://a.com"),
        ];

        storage.remove_old_pending_closures(&new_records).unwrap();

        assert_eq!(
            storage.get_unsent_commands().unwrap(),
            vec![],
            "both closures were completed on the device they were sent to"
        );
    }

    // A device that closed every tab uploads a record with no tabs, which
    // completes every pending closure for it.
    #[test]
    fn test_remove_pending_closures_device_with_no_tabs() {
        error_support::init_for_tests();
        let mut storage =
            TabsStorage::new_with_mem_path("test_remove_pending_closures_device_with_no_tabs");
        storage
            .open_if_exists()
            .unwrap()
            .unwrap()
            .execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES ('device-1', '', :now);",
                rusqlite::named_params! { ":now": Timestamp::now() },
            )
            .unwrap();
        storage
            .add_remote_tab_command("device-1", &RemoteCommand::close_tab("https://a.com"))
            .unwrap();

        let new_records = vec![(
            TabsRecord {
                id: "device-1".to_string(),
                tabs: vec![],
                ..Default::default()
            },
            ServerTimestamp::default(),
        )];
        storage.remove_old_pending_closures(&new_records).unwrap();

        assert_eq!(storage.get_unsent_commands().unwrap(), vec![]);
    }

    // Commands are keyed by FxA device ID, but `tabs` rows by sync client ID, and
    // on desktop these differ. Timed-out closures must still be removed - and
    // they are even with no incoming records, so a database holding closures
    // that should have timed out already is cleaned up on the next sync.
    #[test]
    fn test_remove_old_pending_closures_timed_removal_uses_fxa_device_id() {
        error_support::init_for_tests();
        let mut storage = TabsStorage::new_with_mem_path(
            "test_remove_old_pending_closures_timed_removal_uses_fxa_device_id",
        );
        let now = Timestamp::now();
        let older = now
            .checked_sub(Duration::from_millis(REMOTE_COMMAND_TTL_MS))
            .unwrap();

        let remote_clients = HashMap::from([(
            "sync-client-id".to_string(),
            RemoteClient {
                fxa_device_id: Some("fxa-device-id".to_string()),
                device_name: "other device".to_string(),
                device_type: DeviceType::Desktop,
            },
        )]);
        storage
            .put_meta(
                schema::REMOTE_CLIENTS_KEY,
                &serde_json::to_string(&remote_clients).unwrap(),
            )
            .unwrap();
        storage
            .open_if_exists()
            .unwrap()
            .unwrap()
            .execute(
                "INSERT INTO tabs (guid, record, last_modified) VALUES ('sync-client-id', '', :now);",
                rusqlite::named_params! { ":now": now },
            )
            .unwrap();
        storage
            .add_remote_tab_command_at(
                "fxa-device-id",
                &RemoteCommand::close_tab("https://timed-out.com"),
                older,
            )
            .unwrap();
        storage
            .add_remote_tab_command_at(
                "fxa-device-id",
                &RemoteCommand::close_tab("https://recent.com"),
                now,
            )
            .unwrap();

        storage.remove_old_pending_closures(&[]).unwrap();

        let remaining: Vec<_> = storage
            .get_unsent_commands()
            .unwrap()
            .into_iter()
            .map(|c| c.command)
            .collect();
        assert_eq!(
            remaining,
            vec![RemoteCommand::close_tab("https://recent.com")]
        );
    }

    #[test]
    fn test_close_connection() {
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("test_close_connection.db");
        let mut storage = TabsStorage::new(db_path);

        // Open the connection
        storage.open_or_create().unwrap();

        // Verify that the connection is open
        assert!(matches!(storage.db_connection, DbConnection::Open(_)));

        // Close the connection
        storage.close();

        // Verify that the connection is closed
        assert!(matches!(storage.db_connection, DbConnection::Closed));

        // Attempt to reopen the connection should fail
        let result = storage.open_or_create();
        assert!(result.is_err());
        assert!(matches!(
            result.unwrap_err(),
            Error::UnexpectedConnectionState
        ));
    }
}
