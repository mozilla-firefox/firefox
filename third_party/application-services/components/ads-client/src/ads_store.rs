/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod connection_initializer;
pub mod store;

use crate::{
    ads::PlacementId,
    ads_store::{connection_initializer::AdsStoreConnectionInitializer, store::AdsStoreHolder},
    bytesize::ByteSize,
    telemetry, IN_MEMORY_DB_PATH,
};
use rusqlite::OpenFlags;
use sql_support::{open_database, LazyDb};
use std::path::Path;

#[derive(Debug, thiserror::Error)]
pub enum AdsStoreError {
    #[error("Database error: {0}")]
    Database(#[from] open_database::Error),
    // TODO: Remove this once memory dbs confirmed to be OK.
    #[error("Database (memory) error: {0}")]
    DatabaseMemory(open_database::Error),
    #[error("Database path cannot be empty")]
    EmptyDbPath,
}

pub struct AdsStore {
    holder: AdsStoreHolder,
    #[expect(dead_code, reason = "nothing trims the store to it yet")]
    max_size: ByteSize,
}

impl AdsStore {
    /// Falls back to a memory db if `db_path` cannot be opened.
    ///
    /// # Telemetry
    ///
    /// Records the failure to open `db_path` in `ads_client.build_cache_error`.
    /// If the memory db fails to open too, that error is returned for the
    /// caller to record.
    pub fn new<P: AsRef<Path>>(db_path: P, max_size: ByteSize) -> Result<Self, AdsStoreError> {
        Ok(Self {
            holder: AdsStoreHolder::new(open_db(db_path.as_ref())?),
            max_size,
        })
    }

    pub fn clear(&self) -> Result<(), open_database::Error> {
        self.holder.clear_all()?;
        Ok(())
    }

    pub fn invalidate_by_id(&self, placement_id: &PlacementId) -> Result<(), open_database::Error> {
        self.holder.invalidate_ad_by_id(placement_id)?;
        Ok(())
    }

    pub fn is_memory(&self) -> Result<bool, open_database::Error> {
        self.holder.is_memory()
    }

    pub fn shutdown_db(&self) {
        self.holder.close();
    }
}

// Opens the db at `db_path`, or a memory db if it fails to open.
fn open_db(db_path: &Path) -> Result<LazyDb<AdsStoreConnectionInitializer>, AdsStoreError> {
    let memory = Path::new(IN_MEMORY_DB_PATH);
    if db_path != memory {
        match open_connection(db_path) {
            Ok(db) => return Ok(db),
            Err(e) => telemetry::record_ads_store_error(&AdsStoreError::from(e)),
        }
    }

    // TODO: This is cast to DatabaseMemory rather than Database. Remove this when DatabaseMemory is removed.
    open_connection(memory).map_err(AdsStoreError::DatabaseMemory)
}

// Opens the db at `path`, eagerly opening the initial connection so failures surface here.
fn open_connection(
    path: &Path,
) -> Result<LazyDb<AdsStoreConnectionInitializer>, open_database::Error> {
    let db = LazyDb::new(path, OpenFlags::default(), AdsStoreConnectionInitializer {});
    drop(db.lock()?);
    Ok(db)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::{get_example_happy_image_ads, memory_ads_store};

    #[test]
    fn test_ads_store_creation() {
        assert!(memory_ads_store().is_memory().unwrap());
    }

    #[test]
    fn test_unopenable_path_falls_back_to_memory_db() {
        // The fallback records telemetry, so don't race the telemetry tests.
        let _lock = crate::telemetry::test_lock();
        let store = AdsStore::new("this/is/a/test/bad/path", ByteSize::mib(1)).unwrap();
        assert!(store.is_memory().unwrap());
    }

    #[test]
    fn test_clear_store() {
        let store = memory_ads_store();

        let (placement_id, ads) = get_example_happy_image_ads("mock_billboard_1");
        store.holder.store_ads(&placement_id, &ads).unwrap();

        // Verify it's cached
        let retrieved = store.holder.lookup(&placement_id).unwrap();
        assert!(retrieved.is_some());

        // Clear the cache
        store.clear().unwrap();

        // Verify it's cleared
        let retrieved_after_clear = store.holder.lookup(&placement_id).unwrap();
        assert!(retrieved_after_clear.is_none());
    }

    #[test]
    fn test_invalidate_by_id() {
        let store = memory_ads_store();

        let (placement_1, ads_1) = get_example_happy_image_ads("mock_billboard_1");
        let (placement_2, ads_2) = get_example_happy_image_ads("mock_billboard_2");
        store.holder.store_ads(&placement_1, &ads_1).unwrap();
        store.holder.store_ads(&placement_2, &ads_2).unwrap();

        assert!(store.holder.lookup(&placement_1).unwrap().is_some());
        assert!(store.holder.lookup(&placement_2).unwrap().is_some());

        store.invalidate_by_id(&placement_1).unwrap();

        assert!(store.holder.lookup(&placement_1).unwrap().is_none());
        assert!(store.holder.lookup(&placement_2).unwrap().is_some());
    }
}
