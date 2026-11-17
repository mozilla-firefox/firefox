/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::ads::Ad;
use crate::ads::PlacementId;
use crate::ads_store::connection_initializer::AdsStoreConnectionInitializer;
use crate::bytesize::ByteSize;
use crate::clock::Clock;
use crate::mars::error::FetchAdsError;
use rusqlite::{params, OptionalExtension};
use sql_support::{open_database, LazyDb};

#[cfg(test)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaultKind {
    Lookup,
    None,
    Store,
    Trim,
}

pub struct AdsStoreHolder {
    clock: Clock,
    db: LazyDb<AdsStoreConnectionInitializer>,
    #[cfg(test)]
    fault: parking_lot::Mutex<FaultKind>,
}

impl AdsStoreHolder {
    pub fn new(db: LazyDb<AdsStoreConnectionInitializer>) -> Self {
        Self {
            clock: Clock::default(),
            db,
            #[cfg(test)]
            fault: parking_lot::Mutex::new(FaultKind::None),
        }
    }

    /// Removes all entries from cache.
    pub fn clear_all(&self) -> Result<usize, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        let mut total = 0;
        total += conn.execute("DELETE FROM ads", [])?;
        Ok(total)
    }

    pub fn close(&self) {
        self.db.close(true);
    }

    /// Returns whether the db is in memory rather than backed by a file.
    pub fn is_memory(&self) -> Result<bool, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        // SQLite reports an empty path for in-memory dbs.
        Ok(conn.path() == Some(""))
    }

    /// Returns total size of the cache in bytes.
    pub fn current_total_size_bytes(&self) -> Result<ByteSize, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        let size_bytes_ads: u64 =
            conn.query_row("SELECT COALESCE(SUM(size_bytes),0) FROM ads", [], |row| {
                row.get(0)
            })?;
        Ok(ByteSize::b(size_bytes_ads))
    }

    pub fn invalidate_ad_by_id(
        &self,
        placement_id: &PlacementId,
    ) -> Result<usize, open_database::Error> {
        let (conn, _) = self.db.lock()?;
        Ok(conn.execute(
            "DELETE FROM ads WHERE placement_id = ?1",
            params![&placement_id.as_ref()],
        )?)
    }

    pub fn lookup(&self, placement_id: &PlacementId) -> Result<Option<Vec<Ad>>, FetchAdsError> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Lookup {
            return Err(open_database::Error::from(Self::forced_fault_error(
                "forced lookup failure",
            )))?;
        }
        let (conn, _) = self.db.lock()?;
        let res = conn
            .query_row(
                "SELECT placement_id, ad_body
                 FROM ads
                 WHERE placement_id = ?1",
                params![placement_id.as_ref()],
                |row| {
                    let ad_body: Vec<u8> = row.get(1)?;
                    Ok(ad_body)
                },
            )
            .optional()
            .map_err(open_database::Error::from)?;
        Ok(res.map(|x| serde_json::from_slice(&x)).transpose()?)
    }

    #[cfg(test)]
    pub fn set_fault(&self, kind: FaultKind) {
        *self.fault.lock() = kind;
    }

    // Upsert multiple ads for a singular placement, replacing any ads already stored for it.
    pub fn store_ads(&self, placement_id: &PlacementId, ads: &[Ad]) -> Result<(), FetchAdsError> {
        self.store_batch_ads([(placement_id, ads)])
    }

    /// Upsert multiple ads for many placements, replacing any ads already stored for it.
    pub fn store_batch_ads<'a, K: AsRef<str>, V: AsRef<[Ad]>>(
        &self,
        ads_map: impl IntoIterator<Item = (K, V)>,
    ) -> Result<(), FetchAdsError> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Store {
            return Err(open_database::Error::from(Self::forced_fault_error(
                "forced store failure",
            )))?;
        }

        let (mut conn, _) = self.db.lock()?;
        let tx = conn.transaction().map_err(open_database::Error::from)?;
        let mut stmt = tx
            .prepare(
                "INSERT INTO ads (
                stored_at,
                placement_id,
                ad_body,
                size_bytes
            )
            VALUES (?1, ?2, ?3, ?4)
            ON CONFLICT(placement_id) DO UPDATE SET
                stored_at=excluded.stored_at,
                ad_body=excluded.ad_body,
                size_bytes=excluded.size_bytes",
            )
            .map_err(open_database::Error::from)?;

        let now = self.clock.now();
        for (placement_id, ads) in ads_map {
            let placement_id_str: &str = placement_id.as_ref();
            let ads: &[Ad] = ads.as_ref();
            let ad_body = serde_json::to_vec(&ads)?;
            let size_bytes = ad_body.len();
            stmt.execute(params![
                now.timestamp(),
                placement_id_str,
                ad_body,
                size_bytes,
            ])
            .map_err(open_database::Error::from)?;
        }
        stmt.finalize().map_err(open_database::Error::from)?;
        tx.commit().map_err(open_database::Error::from)?;

        Ok(())
    }

    pub fn trim_to_max_size(&self, max_size: &ByteSize) -> Result<(), open_database::Error> {
        #[cfg(test)]
        if *self.fault.lock() == FaultKind::Trim {
            return Err(Self::forced_fault_error("forced trim failure"))?;
        }
        // Keeps the newest rows that fit in `max_size` and deletes the rest,
        // in one statement.
        let (conn, _) = self.db.lock()?;
        conn.execute(
            "DELETE FROM ads
             WHERE rowid IN (
                 SELECT rowid
                 FROM (
                     SELECT rowid,
                            SUM(size_bytes) OVER (ORDER BY stored_at DESC, rowid DESC) AS kept_bytes
                     FROM ads
                 )
                 WHERE kept_bytes > ?1
             )",
            params![max_size.as_u64()],
        )?;
        Ok(())
    }

    #[cfg(test)]
    fn forced_fault_error(msg: &str) -> rusqlite::Error {
        rusqlite::Error::SqliteFailure(
            rusqlite::ffi::Error {
                code: rusqlite::ErrorCode::InternalMalfunction,
                extended_code: 0,
            },
            Some(msg.to_string()),
        )
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use super::*;
    use crate::{
        ads_store::connection_initializer::AdsStoreConnectionInitializer,
        test_utils::{get_example_happy_image_ads, get_example_happy_spoc_response},
    };
    use rusqlite::OpenFlags;
    use sql_support::open_database;

    fn create_test_store() -> AdsStoreHolder {
        let initializer = AdsStoreConnectionInitializer {};
        let db = LazyDb::new(
            Path::new(crate::IN_MEMORY_DB_PATH),
            OpenFlags::default(),
            initializer,
        );
        AdsStoreHolder::new(db)
    }

    #[test]
    fn test_lookup_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Lookup);

        let (placement, _) = get_example_happy_image_ads("mock_billboard_1");
        let err = store.lookup(&placement).unwrap_err();

        match err {
            FetchAdsError::Sqlite(open_database::Error::SqlError(
                rusqlite::Error::SqliteFailure(_, Some(msg)),
            )) => {
                assert!(msg.contains("forced lookup failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_store_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Store);

        let (placement, ads) = get_example_happy_image_ads("mock_billboard_1");

        let err = store.store_ads(&placement, &ads).unwrap_err();
        match err {
            FetchAdsError::Sqlite(open_database::Error::SqlError(
                rusqlite::Error::SqliteFailure(_, Some(msg)),
            )) => {
                assert!(msg.contains("forced store failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_trim_fault_injection() {
        let store = create_test_store();
        store.set_fault(FaultKind::Trim);

        let (placement, ads) = get_example_happy_image_ads("mock_billboard_1");
        store.store_ads(&placement, &ads).unwrap();

        let err = store.trim_to_max_size(&ByteSize::b(1)).unwrap_err();
        match err {
            open_database::Error::SqlError(rusqlite::Error::SqliteFailure(_, Some(msg))) => {
                assert!(msg.contains("forced trim failure"));
            }
            other => panic!("unexpected error: {other:?}"),
        }
    }

    #[test]
    fn test_store_and_retrieve_ads() {
        let store = create_test_store();
        let (placement, ads) = get_example_happy_image_ads("mock_billboard_1");

        store.store_ads(&placement, &ads).unwrap();

        let retrieved = store.lookup(&placement).unwrap().unwrap();
        assert_eq!(retrieved, ads);
    }

    #[test]
    fn test_store_keeps_every_ad_for_a_placement() {
        let store = create_test_store();
        let placement = PlacementId::new("mock_spoc_1");
        let ads: Vec<Ad> = get_example_happy_spoc_response()
            .data
            .into_values()
            .flatten()
            .collect();
        assert!(ads.len() > 1);

        store.store_ads(&placement, &ads).unwrap();

        assert_eq!(store.lookup(&placement).unwrap(), Some(ads));
    }

    #[test]
    fn test_store_replaces_ads_for_a_placement() {
        let store = create_test_store();
        let placement = PlacementId::new("mock_spoc_1");
        let ads: Vec<Ad> = get_example_happy_spoc_response()
            .data
            .into_values()
            .flatten()
            .collect();
        store.store_ads(&placement, &ads).unwrap();

        let refreshed = ads[..1].to_vec();
        store.store_ads(&placement, &refreshed).unwrap();

        assert_eq!(store.lookup(&placement).unwrap(), Some(refreshed));
    }

    #[test]
    fn test_max_size_eviction_ads() {
        let store = create_test_store();

        for i in 0..10 {
            let (placement_id, ads) = get_example_happy_image_ads(&format!("mock_billboard_{i}"));
            store.store_ads(&placement_id, &ads).unwrap();
        }

        let total_size = store.current_total_size_bytes().unwrap();
        assert!(total_size.as_u64() >= 1024);

        store.trim_to_max_size(&ByteSize::kib(1)).unwrap();

        let total_size = store.current_total_size_bytes().unwrap();
        assert!(total_size.as_u64() <= 1024);

        let first_placement_id = PlacementId::new("mock_billboard_0");
        let first_cached = store.lookup(&first_placement_id).unwrap();
        assert!(first_cached.is_none());
    }

    #[test]
    fn test_clear_all_ads() {
        let store = create_test_store();
        let (placement_1, ads_1) = get_example_happy_image_ads("mock_billboard_1");

        store.store_ads(&placement_1, &ads_1).unwrap();

        let (placement_2, ads_2) = get_example_happy_image_ads("mock_billboard_2");
        store.store_ads(&placement_2, &ads_2).unwrap();

        assert!(store.lookup(&placement_1).unwrap().is_some());
        assert!(store.lookup(&placement_2).unwrap().is_some());

        let deleted_count = store.clear_all().unwrap();
        assert_eq!(deleted_count, 2);

        assert!(store.lookup(&placement_1).unwrap().is_none());
        assert!(store.lookup(&placement_2).unwrap().is_none());
    }

    #[test]
    fn test_invalidate_ad_by_placement_id() {
        let store = create_test_store();

        let (placement_1, ads_1) = get_example_happy_image_ads("mock_billboard_1");
        let (placement_2, ads_2) = get_example_happy_image_ads("mock_billboard_2");

        store.store_ads(&placement_1, &ads_1).unwrap();
        store.store_ads(&placement_2, &ads_2).unwrap();

        assert!(store.lookup(&placement_1).unwrap().is_some());
        assert!(store.lookup(&placement_2).unwrap().is_some());

        let deleted = store.invalidate_ad_by_id(&placement_1).unwrap();
        assert_eq!(deleted, 1);

        assert!(store.lookup(&placement_1).unwrap().is_none());
        assert!(store.lookup(&placement_2).unwrap().is_some());
    }

    #[test]
    fn batch_add_adds_all_ads() {
        let store = create_test_store();
        let (placement_1, ads_1) = get_example_happy_image_ads("mock_billboard_1");
        let (placement_2, ads_2) = get_example_happy_image_ads("mock_billboard_2");

        let mut map = std::collections::HashMap::new();
        map.insert(&placement_1, ads_1);
        map.insert(&placement_2, ads_2);

        store.store_batch_ads(map).unwrap();
        assert!(store.lookup(&placement_1).unwrap().is_some());
        assert!(store.lookup(&placement_2).unwrap().is_some());
    }
}
