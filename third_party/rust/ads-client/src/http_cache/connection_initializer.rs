/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use rusqlite::Connection;
use sql_support::open_database;
use std::time::Duration;

pub struct HttpCacheConnectionInitializer {}

impl open_database::ConnectionInitializer for HttpCacheConnectionInitializer {
    const NAME: &'static str = "http_cache";
    const END_VERSION: u32 = 1;

    fn prepare(&self, conn: &Connection, _db_empty: bool) -> open_database::Result<()> {
        conn.execute_batch("PRAGMA journal_mode=wal;")?;
        conn.busy_timeout(Duration::from_secs(5))?;
        Ok(())
    }

    fn init(&self, tx: &rusqlite::Transaction<'_>) -> open_database::Result<()> {
        const SCHEMA: &str = "
            CREATE TABLE IF NOT EXISTS http_cache (
                cached_at INTEGER NOT NULL,
                expires_at INTEGER NOT NULL,
                request_hash TEXT NOT NULL,
                response_body BLOB NOT NULL,
                response_headers BLOB,
                response_status INTEGER NOT NULL,
                size_bytes INTEGER NOT NULL,
                ttl_seconds INTEGER NOT NULL,
                PRIMARY KEY (request_hash)
            );
            CREATE INDEX IF NOT EXISTS idx_http_cache_cached_at ON http_cache(cached_at);
            CREATE INDEX IF NOT EXISTS idx_http_cache_expires_at ON http_cache(expires_at);
            CREATE INDEX IF NOT EXISTS idx_http_cache_request_hash ON http_cache(request_hash);
        ";
        tx.execute_batch(SCHEMA)?;
        Ok(())
    }

    fn upgrade_from(
        &self,
        conn: &rusqlite::Transaction<'_>,
        version: u32,
    ) -> open_database::Result<()> {
        match version {
            0 => {
                // Version 0 means we need to create the initial schema
                self.init(conn)
            }
            _ => Err(open_database::Error::IncompatibleVersion(version)),
        }
    }
}
