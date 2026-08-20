/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::Result;
use rusqlite::Transaction;
use std::sync::{Arc, Weak};
use sync15::bso::{IncomingBso, OutgoingBso};
use sync15::engine::{CollSyncIds, CollectionRequest, EngineSyncAssociation, SyncEngine};
use sync15::{telemetry, CollectionName, ServerTimestamp};
use sync_guid::Guid as SyncGuid;

// The collection name Desktop's Sync framework uses for `storage.sync`. Only
// used for telemetry labelling here (Desktop builds the collection URL itself).
const COLLECTION_NAME: &str = "extension-storage";

use crate::db::{delete_meta, get_meta, put_meta, ThreadSafeStorageDb};
use crate::schema;
use crate::sync::incoming::{apply_actions, get_incoming, plan_incoming, stage_incoming};
use crate::sync::outgoing::{get_outgoing, record_uploaded, stage_outgoing};
use crate::WebExtStorageStore;

const LAST_SYNC_META_KEY: &str = "last_sync_time";
const SYNC_ID_META_KEY: &str = "sync_id";

impl WebExtStorageStore {
    // Returns a bridged sync engine for this store.
    pub fn bridged_engine(self: Arc<Self>) -> Arc<WebExtStorageBridgedEngine> {
        let engine = Box::new(WebExtSyncEngine::new(&self.db));
        Arc::new(WebExtStorageBridgedEngine::new(engine))
    }
}

pub struct WebExtSyncEngine {
    db: Weak<ThreadSafeStorageDb>,
}

impl WebExtSyncEngine {
    /// Creates a bridged engine for syncing.
    pub fn new(db: &Arc<ThreadSafeStorageDb>) -> Self {
        WebExtSyncEngine {
            db: Arc::downgrade(db),
        }
    }

    fn do_reset(&self, tx: &Transaction<'_>) -> Result<()> {
        tx.execute_batch(
            "DELETE FROM storage_sync_mirror;
             UPDATE storage_sync_data SET sync_change_counter = 1;",
        )?;
        delete_meta(tx, LAST_SYNC_META_KEY)?;
        Ok(())
    }

    fn thread_safe_storage_db(&self) -> Result<Arc<ThreadSafeStorageDb>> {
        self.db
            .upgrade()
            .ok_or_else(|| crate::error::Error::DatabaseConnectionClosed.into())
    }
}

impl SyncEngine for WebExtSyncEngine {
    fn collection_name(&self) -> CollectionName {
        COLLECTION_NAME.into()
    }

    // Read-only view of the engine-owned last-sync time, for the Desktop bridge.
    // It's written only internally, in `apply`/`set_uploaded`.
    fn last_sync(&self) -> Result<Option<ServerTimestamp>> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        Ok(get_meta::<i64>(conn, LAST_SYNC_META_KEY)?.map(ServerTimestamp))
    }

    fn reset_last_sync(&self) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        let tx = conn.unchecked_transaction()?;
        delete_meta(&tx, LAST_SYNC_META_KEY)?;
        tx.commit()?;
        Ok(())
    }

    fn get_sync_assoc(&self) -> Result<EngineSyncAssociation> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        // Bridged engines never maintain the "global" guid - that's all managed
        // by the consumer (Desktop); they only care about the per-collection one.
        Ok(match get_meta::<String>(conn, SYNC_ID_META_KEY)? {
            Some(coll) => EngineSyncAssociation::Connected(CollSyncIds {
                global: SyncGuid::empty(),
                coll: coll.into(),
            }),
            None => EngineSyncAssociation::Disconnected,
        })
    }

    fn sync_started(&self) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        schema::create_empty_sync_temp_tables(conn)?;
        Ok(())
    }

    fn stage_incoming(
        &self,
        incoming_bsos: Vec<IncomingBso>,
        _telem: &mut telemetry::Engine,
    ) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let signal = db.begin_interrupt_scope()?;
        let conn = db.get_connection()?;
        let tx = conn.unchecked_transaction()?;
        let incoming_content: Vec<_> = incoming_bsos
            .into_iter()
            .map(IncomingBso::into_content::<super::WebextRecord>)
            .collect();
        stage_incoming(&tx, &incoming_content, &signal)?;
        tx.commit()?;
        Ok(())
    }

    fn apply(
        &self,
        timestamp: ServerTimestamp,
        _telem: &mut telemetry::Engine,
    ) -> Result<Vec<OutgoingBso>> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let signal = db.begin_interrupt_scope()?;
        let conn = db.get_connection()?;
        let tx = conn.unchecked_transaction()?;
        let incoming = get_incoming(&tx)?;
        let actions = incoming
            .into_iter()
            .map(|(item, state)| (item, plan_incoming(state)))
            .collect();
        apply_actions(&tx, actions, &signal)?;
        stage_outgoing(&tx)?;
        // The engine owns its last-sync time: record the collection timestamp we
        // just synced to, so it advances without any external `set_last_sync`.
        // (Timestamp is zero only in an upload-only path, which must not move it.)
        if timestamp != ServerTimestamp(0) {
            put_meta(&tx, LAST_SYNC_META_KEY, &timestamp.as_millis())?;
        }
        tx.commit()?;

        Ok(get_outgoing(conn, &signal)?)
    }

    fn set_uploaded(&self, new_timestamp: ServerTimestamp, ids: Vec<SyncGuid>) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        let signal = db.begin_interrupt_scope()?;
        let tx = conn.unchecked_transaction()?;
        record_uploaded(&tx, &ids, &signal)?;
        // Advance the engine-owned last-sync time to the post-upload timestamp.
        if new_timestamp != ServerTimestamp(0) {
            put_meta(&tx, LAST_SYNC_META_KEY, &new_timestamp.as_millis())?;
        }
        tx.commit()?;

        Ok(())
    }

    fn sync_finished(&self) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        schema::create_empty_sync_temp_tables(conn)?;
        Ok(())
    }

    fn get_collection_request(
        &self,
        server_timestamp: ServerTimestamp,
    ) -> Result<Option<CollectionRequest>> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        let since = ServerTimestamp(get_meta::<i64>(conn, LAST_SYNC_META_KEY)?.unwrap_or(0));
        Ok(if since == server_timestamp {
            None
        } else {
            Some(
                CollectionRequest::new(COLLECTION_NAME.into())
                    .full()
                    .newer_than(since),
            )
        })
    }

    fn reset(&self, assoc: &EngineSyncAssociation) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        let tx = conn.unchecked_transaction()?;
        self.do_reset(&tx)?;
        // A `Disconnected` reset clears the sync ID; a `Connected` one adopts the
        // (per-collection) ID. `do_reset` already cleared the last sync time.
        match assoc {
            EngineSyncAssociation::Disconnected => {
                delete_meta(&tx, SYNC_ID_META_KEY)?;
            }
            EngineSyncAssociation::Connected(ids) => {
                put_meta(&tx, SYNC_ID_META_KEY, &ids.coll.to_string())?;
            }
        }
        tx.commit()?;
        Ok(())
    }

    fn wipe(&self) -> Result<()> {
        let shared_db = self.thread_safe_storage_db()?;
        let db = shared_db.lock();
        let conn = db.get_connection()?;
        let tx = conn.unchecked_transaction()?;
        // We assume the meta table is only used by sync.
        tx.execute_batch(
            "DELETE FROM storage_sync_data; DELETE FROM storage_sync_mirror; DELETE FROM meta;",
        )?;
        tx.commit()?;
        Ok(())
    }
}

// The UniFFI-exposed `WebExtStorageBridgedEngine` (a thin newtype around
// `sync15::engine::BridgedEngineWrapper`) is generated by this macro, which
// removes the facade + BSO marshalling boilerplate. The wrapper drives the
// `SyncEngine` impl on the `BridgedEngine` defined above (webext-storage is
// Desktop-only, but implements the one unified `SyncEngine` trait like everyone
// else).
sync15::uniffi_bridged_engine!(WebExtStorageBridgedEngine);

impl From<anyhow::Error> for crate::error::Error {
    fn from(value: anyhow::Error) -> Self {
        crate::error::Error::SyncError(value.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::test::new_mem_thread_safe_storage_db;
    use crate::db::StorageDb;
    use sync15::engine::BridgedEngineWrapper;

    // The sync-ID and reset semantics that used to live on the old
    // `BridgedEngine` trait now live on `BridgedEngineWrapper` (which drives our
    // `SyncEngine`), so we exercise them the same way Desktop does - through the
    // wrapper. Each engine holds a `Weak` to the shared db, so callers keep the
    // strong `Arc` alive and inspect DB state through it directly.
    fn wrapper(db: &Arc<ThreadSafeStorageDb>) -> BridgedEngineWrapper {
        BridgedEngineWrapper::new(Box::new(WebExtSyncEngine::new(db)))
    }

    fn query_count(db: &StorageDb, table: &str) -> u32 {
        let conn = db.get_connection().expect("should retrieve connection");
        conn.query_row_and_then(&format!("SELECT COUNT(*) FROM {};", table), [], |row| {
            row.get::<_, u32>(0)
        })
        .expect("should work")
    }

    // Sets up mock data for the tests here.
    fn setup_mock_data(db: &Arc<ThreadSafeStorageDb>) -> Result<()> {
        {
            let shared = db.lock();
            let conn = shared.get_connection().expect("should retrieve connection");
            conn.execute(
                "INSERT INTO storage_sync_data (ext_id, data, sync_change_counter)
                    VALUES ('ext-a', 'invalid-json', 2)",
                [],
            )?;
            conn.execute(
                "INSERT INTO storage_sync_mirror (guid, ext_id, data)
                    VALUES ('guid', 'ext-a', '3')",
                [],
            )?;
        }
        // Seed a last-sync time directly - there's no public setter for it.
        {
            let shared = db.lock();
            let conn = shared.get_connection().expect("should retrieve connection");
            put_meta(conn, LAST_SYNC_META_KEY, &1i64)?;
        }

        let shared = db.lock();
        // and assert we wrote what we think we did.
        assert_eq!(query_count(&shared, "storage_sync_data"), 1);
        assert_eq!(query_count(&shared, "storage_sync_mirror"), 1);
        assert_eq!(query_count(&shared, "meta"), 1);
        Ok(())
    }

    // Assuming a DB setup with setup_mock_data, assert it was correctly reset.
    fn assert_reset(db: &Arc<ThreadSafeStorageDb>) -> Result<()> {
        // A reset never wipes data...
        let shared = db.lock();
        let conn = shared.get_connection().expect("should retrieve connection");
        assert_eq!(query_count(&shared, "storage_sync_data"), 1);

        // But did reset the change counter.
        let cc = conn.query_row_and_then(
            "SELECT sync_change_counter FROM storage_sync_data WHERE ext_id = 'ext-a';",
            [],
            |row| row.get::<_, u32>(0),
        )?;
        assert_eq!(cc, 1);
        // But did wipe the mirror...
        assert_eq!(query_count(&shared, "storage_sync_mirror"), 0);
        // And the last_sync should have been wiped.
        assert!(get_meta::<i64>(conn, LAST_SYNC_META_KEY)?.is_none());
        Ok(())
    }

    // Assuming a DB setup with setup_mock_data, assert it has not been reset.
    fn assert_not_reset(db: &Arc<ThreadSafeStorageDb>) -> Result<()> {
        let shared = db.lock();
        let conn = shared.get_connection().expect("should retrieve connection");
        assert_eq!(query_count(&shared, "storage_sync_data"), 1);
        let cc = conn.query_row_and_then(
            "SELECT sync_change_counter FROM storage_sync_data WHERE ext_id = 'ext-a';",
            [],
            |row| row.get::<_, u32>(0),
        )?;
        assert_eq!(cc, 2);
        assert_eq!(query_count(&shared, "storage_sync_mirror"), 1);
        // And the last_sync should remain.
        assert!(get_meta::<i64>(conn, LAST_SYNC_META_KEY)?.is_some());
        Ok(())
    }

    #[test]
    fn test_wipe() -> Result<()> {
        let strong = new_mem_thread_safe_storage_db();
        setup_mock_data(&strong)?;

        wrapper(&strong).wipe()?;

        let db = strong.lock();
        assert_eq!(query_count(&db, "storage_sync_data"), 0);
        assert_eq!(query_count(&db, "storage_sync_mirror"), 0);
        assert_eq!(query_count(&db, "meta"), 0);
        Ok(())
    }

    #[test]
    fn test_reset() -> Result<()> {
        let strong = new_mem_thread_safe_storage_db();
        setup_mock_data(&strong)?;
        {
            let db = strong.lock();
            let conn = db.get_connection()?;
            put_meta(conn, SYNC_ID_META_KEY, &"sync-id".to_string())?;
        }

        wrapper(&strong).reset()?;
        assert_reset(&strong)?;

        {
            let db = strong.lock();
            let conn = db.get_connection()?;
            // Only an explicit reset kills the sync-id, so check that here.
            assert_eq!(get_meta::<String>(conn, SYNC_ID_META_KEY)?, None);
        }

        Ok(())
    }

    #[test]
    fn test_ensure_missing_sync_id() -> Result<()> {
        let strong = new_mem_thread_safe_storage_db();
        setup_mock_data(&strong)?;

        assert_eq!(wrapper(&strong).sync_id()?, None);
        // We don't have a sync ID - so setting one should reset.
        wrapper(&strong).ensure_current_sync_id("new-id")?;
        // should have cause a reset.
        assert_reset(&strong)?;
        Ok(())
    }

    #[test]
    fn test_ensure_new_sync_id() -> Result<()> {
        let strong = new_mem_thread_safe_storage_db();
        setup_mock_data(&strong)?;

        {
            let db = strong.lock();
            let conn = db.get_connection()?;
            put_meta(conn, SYNC_ID_META_KEY, &"old-id".to_string())?;
        }

        assert_not_reset(&strong)?;
        assert_eq!(wrapper(&strong).sync_id()?, Some("old-id".to_string()));

        wrapper(&strong).ensure_current_sync_id("new-id")?;
        // should have cause a reset.
        assert_reset(&strong)?;
        // should have the new id.
        assert_eq!(wrapper(&strong).sync_id()?, Some("new-id".to_string()));
        Ok(())
    }

    #[test]
    fn test_ensure_same_sync_id() -> Result<()> {
        let strong = new_mem_thread_safe_storage_db();
        setup_mock_data(&strong)?;
        assert_not_reset(&strong)?;

        {
            let db = strong.lock();
            let conn = db.get_connection()?;
            put_meta(conn, SYNC_ID_META_KEY, &"sync-id".to_string())?;
        }

        wrapper(&strong).ensure_current_sync_id("sync-id")?;
        // should not have reset.
        assert_not_reset(&strong)?;
        Ok(())
    }

    #[test]
    fn test_reset_sync_id() -> Result<()> {
        let strong = new_mem_thread_safe_storage_db();
        setup_mock_data(&strong)?;

        {
            let db = strong.lock();
            let conn = db.get_connection()?;
            put_meta(conn, SYNC_ID_META_KEY, &"sync-id".to_string())?;
        }

        assert_eq!(wrapper(&strong).sync_id()?, Some("sync-id".to_string()));
        let new_id = wrapper(&strong).reset_sync_id()?;
        // should have cause a reset.
        assert_reset(&strong)?;
        assert_eq!(wrapper(&strong).sync_id()?, Some(new_id));
        Ok(())
    }
}
