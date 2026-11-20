// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

//! The SQLite database schema.

use std::num::NonZeroU32;

use rusqlite::{config::DbConfig, OptionalExtension, Transaction};

use super::connection::ConnectionOpener;

/// The schema for a physical SQLite database that contains many
/// named logical databases.
#[derive(Debug)]
pub struct Schema;

fn table_schema(schema: Option<&str>) -> String {
    let separator = if schema.is_some() { "." } else { "" };
    let schema = schema.unwrap_or("");
    let table_name = "telemetry";
    format!(
        "
        CREATE TABLE {schema}{separator}{table_name}(
          id TEXT NOT NULL,
          ping TEXT NOT NULL,
          lifetime TEXT NOT NULL,
          labels TEXT NOT NULL, -- can't be null or ON CONFLICT won't work
          value BLOB,
          UNIQUE(id, ping, labels)
        );
        "
    )
}

impl ConnectionOpener for Schema {
    const MAX_SCHEMA_VERSION: u32 = 3;

    type Error = SchemaError;

    fn setup(conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
        conn.execute_batch(
            "
            -- we unconditionally want write-ahead-logging mode
            PRAGMA journal_mode = WAL;
            -- Sync at the most criticial moments, but not with every write
            PRAGMA synchronous = NORMAL;
            -- limit size of the journal. TODO(bug 2049290): value currently arbitrary.
            -- needs refinement.
            PRAGMA journal_size_limit = 512000; -- 512 KB.
            -- We don't care about temp tables being persisted to disk
            PRAGMA temp_store = MEMORY;
            -- allows adding incremental cleanup later
            PRAGMA auto_vacuum = INCREMENTAL;
            -- How long to wait for a lock before returning SQLITE_BUSY (in ms)
            PRAGMA busy_timeout = 5000;
            ",
        )?;

        // Set hardening flags.
        conn.set_db_config(DbConfig::SQLITE_DBCONFIG_DEFENSIVE, true)?;

        // Turn off misfeatures: double-quoted strings and untrusted schemas.
        conn.set_db_config(DbConfig::SQLITE_DBCONFIG_DQS_DML, false)?;
        conn.set_db_config(DbConfig::SQLITE_DBCONFIG_DQS_DDL, false)?;
        conn.set_db_config(DbConfig::SQLITE_DBCONFIG_TRUSTED_SCHEMA, true)?;

        Ok(())
    }

    fn create(tx: &mut Transaction<'_>) -> Result<(), Self::Error> {
        tx.execute_batch(&format!(
            "
            {}
            CREATE TABLE migration(id INTEGER PRIMARY KEY, state TEXT NOT NULL);
            CREATE TABLE submitted_pings(
              document_id TEXT PRIMARY KEY,
              ping TEXT NOT NULL,
              date_submitted INTEGER NOT NULL,
              date_uploaded INTEGER,
              upload_failed INTEGER,
              payload BLOB
            );
            CREATE INDEX submitted_pings_ping on submitted_pings(ping);
            ",
            table_schema(None)
        ))?;
        Ok(())
    }

    fn upgrade(tx: &mut Transaction<'_>, to_version: NonZeroU32) -> Result<(), Self::Error> {
        match to_version.get() {
            2 => {
                log::info!("Upgrading user_version to 2");
                // Clients upgrading to schema 2 don't have the table.
                // But they did run through the migration.
                tx.execute_batch(
                    "CREATE TABLE migration(id INTEGER PRIMARY KEY, state TEXT NOT NULL);",
                )?;

                let cid_exists: Option<i32> = tx
                    .query_row(
                        "SELECT 1 FROM telemetry WHERE id = 'client_id'",
                        [],
                        |row| row.get(0),
                    )
                    .optional()?;
                if cid_exists.is_some() {
                    log::info!("Client ID already exists. Marking migration as done.");
                    tx.execute("INSERT INTO migration (id, state) VALUES (1, 'done') ON CONFLICT(id) DO UPDATE SET state = excluded.state", [])?;
                }
                Ok(())
            }
            3 => {
                log::info!("Upgrading user_version to 3");
                // Clients upgrading to schema 3 don't have the table or index
                tx.execute_batch(
                    "
                    CREATE TABLE submitted_pings(
                        document_id TEXT PRIMARY KEY,
                        ping TEXT NOT NULL,
                        date_submitted INTEGER NOT NULL,
                        date_uploaded INTEGER,
                        upload_failed INTEGER,
                        payload TEXT
                    );
                    CREATE INDEX submitted_pings_ping on submitted_pings(ping);
                    ",
                )?;
                Ok(())
            }
            to_version => Err(SchemaError::UnsupportedSchemaVersion(to_version)),
        }
    }

    fn validate(tx: &mut Transaction<'_>) -> Result<(), Self::Error> {
        // A query selecting every field, it doesn't need to return anything.
        tx.execute_batch("SELECT id, ping, lifetime, labels, value FROM telemetry WHERE 1 = 0")?;
        Ok(())
    }
}

#[derive(thiserror::Error, Debug)]
pub enum SchemaError {
    #[error("unsupported schema version: {0}")]
    UnsupportedSchemaVersion(u32),
    #[error("sqlite: {0}")]
    Sqlite(#[from] rusqlite::Error),
}
