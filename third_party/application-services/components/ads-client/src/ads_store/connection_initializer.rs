/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use rusqlite::Connection;
use sql_support::open_database;
use std::time::Duration;

/// How long a connection waits for another connection's lock before failing
/// with `SQLITE_BUSY`.
const BUSY_TIMEOUT: Duration = Duration::from_secs(5);

pub struct AdsStoreConnectionInitializer;

impl open_database::ConnectionInitializer for AdsStoreConnectionInitializer {
    const END_VERSION: u32 = 1;
    const NAME: &'static str = "ads_cache";

    fn init(&self, tx: &rusqlite::Transaction<'_>) -> open_database::Result<()> {
        const SCHEMA: &str = "
            CREATE TABLE IF NOT EXISTS ads (
                stored_at INTEGER NOT NULL,
                placement_id TEXT NOT NULL,
                ad_body BLOB NOT NULL,
                size_bytes INTEGER NOT NULL,
                PRIMARY KEY (placement_id)
            );
            CREATE INDEX IF NOT EXISTS idx_ads_stored_at ON ads(stored_at);
            CREATE INDEX IF NOT EXISTS idx_ads_placement_id ON ads(placement_id);
            ";
        // If the schema fails to initialize, it might be corrupted or outdated so we drop the table and try again
        if tx.execute_batch(SCHEMA).is_err() {
            tx.execute_batch("DROP TABLE IF EXISTS ads")?;
            tx.execute_batch(SCHEMA)?;
        }
        Ok(())
    }

    fn prepare(&self, conn: &Connection, _db_empty: bool) -> open_database::Result<()> {
        conn.execute_batch("PRAGMA journal_mode=wal;")?;
        conn.busy_timeout(BUSY_TIMEOUT)?;
        Ok(())
    }

    fn upgrade_from(
        &self,
        conn: &rusqlite::Transaction<'_>,
        version: u32,
    ) -> open_database::Result<()> {
        match version {
            0 => self.init(conn),
            _ => Err(open_database::Error::IncompatibleVersion(version)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rusqlite::Connection;
    use sql_support::open_database::ConnectionInitializer;

    #[test]
    fn test_corrupted_schema_is_recreated() {
        let mut conn = Connection::open_in_memory().unwrap();
        let initializer = AdsStoreConnectionInitializer {};

        // Create a corrupted table with only one column
        conn.execute_batch("CREATE TABLE ads (placement_id TEXT);")
            .unwrap();

        // Run init - should drop the corrupted table and recreate it properly
        let tx = conn.transaction().unwrap();
        initializer.init(&tx).unwrap();
        tx.commit().unwrap();

        // Verify the table was recreated with correct schema by checking column count
        let column_count: i64 = conn
            .query_row("SELECT COUNT(*) FROM pragma_table_info('ads')", [], |row| {
                row.get(0)
            })
            .unwrap();

        assert!(
            column_count > 1,
            "Table should have more than 1 column after recreation"
        );
    }
}
