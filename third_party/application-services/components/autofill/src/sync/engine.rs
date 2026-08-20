/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::{plan_incoming, ProcessIncomingRecordImpl, ProcessOutgoingRecordImpl, SyncRecord};
use crate::error::*;
use crate::Store;
use error_support::warn;
use rusqlite::{
    types::{FromSql, ToSql},
    Connection, Transaction,
};
use std::sync::Arc;
use sync15::bso::{IncomingBso, OutgoingBso};
use sync15::engine::{CollSyncIds, CollectionRequest, EngineSyncAssociation, SyncEngine};
use sync15::{telemetry, CollectionName, ServerTimestamp};
use sync_guid::Guid;

// We have 2 engines in this crate and they are identical except for stuff
// abstracted here!
pub struct EngineConfig {
    pub(crate) namespace: String,          // prefix for meta keys, etc.
    pub(crate) collection: CollectionName, // collection name on the server.
}

// meta keys, will be prefixed by the "namespace"
pub const LAST_SYNC_META_KEY: &str = "last_sync_time";
pub const GLOBAL_SYNCID_META_KEY: &str = "global_sync_id";
pub const COLLECTION_SYNCID_META_KEY: &str = "sync_id";

// A trait to abstract the broader sync processes.
// Send + Sync is required to use a `ConfigSyncEngine` as a `BridgedEngine`.
pub trait SyncEngineStorageImpl<T>: Send + Sync {
    fn get_incoming_impl(
        &self,
        enc_key: &Option<String>,
    ) -> Result<Box<dyn ProcessIncomingRecordImpl<Record = T>>>;
    fn reset_storage(&self, conn: &Transaction<'_>) -> Result<()>;
    fn get_outgoing_impl(
        &self,
        enc_key: &Option<String>,
    ) -> Result<Box<dyn ProcessOutgoingRecordImpl<Record = T>>>;
}

// A sync engine that gets functionality from an EngineConfig.
pub struct ConfigSyncEngine<T> {
    pub(crate) config: EngineConfig,
    pub(crate) store: Arc<Store>,
    pub(crate) storage_impl: Box<dyn SyncEngineStorageImpl<T>>,
    local_enc_key: Option<String>,
}

impl<T> ConfigSyncEngine<T> {
    pub fn new(
        config: EngineConfig,
        store: Arc<Store>,
        storage_impl: Box<dyn SyncEngineStorageImpl<T>>,
    ) -> Self {
        Self {
            config,
            store,
            storage_impl,
            local_enc_key: None,
        }
    }
    fn put_meta(&self, conn: &Connection, tail: &str, value: &dyn ToSql) -> Result<()> {
        let key = format!("{}.{}", self.config.namespace, tail);
        crate::db::store::put_meta(conn, &key, value)
    }
    fn get_meta<V: FromSql>(&self, conn: &Connection, tail: &str) -> Result<Option<V>> {
        let key = format!("{}.{}", self.config.namespace, tail);
        crate::db::store::get_meta(conn, &key)
    }
    fn delete_meta(&self, conn: &Connection, tail: &str) -> Result<()> {
        let key = format!("{}.{}", self.config.namespace, tail);
        crate::db::store::delete_meta(conn, &key)
    }

    // Reset the local sync data so the next server request fetches all records.
    pub fn reset_local_sync_data(&self) -> Result<()> {
        let db = self.store.lock_db()?;
        let tx = db.unchecked_transaction()?;
        self.storage_impl.reset_storage(&tx)?;
        self.put_meta(&tx, LAST_SYNC_META_KEY, &0)?;
        tx.commit()?;
        Ok(())
    }

    // We cannot call `reset_local_sync_data` from  the store's `scrub_undecryptable_credit_card_data_for_remote_replacement`
    // function, so we've created this function specifically for it's use. It should not be used anywhere else.
    pub fn reset_local_sync_data_for_verification(&self, conn: &Connection) -> Result<()> {
        let tx = conn.unchecked_transaction()?;
        self.storage_impl.reset_storage(&tx)?;
        self.put_meta(&tx, LAST_SYNC_META_KEY, &0)?;
        tx.commit()?;
        Ok(())
    }
}

impl<T: SyncRecord + std::fmt::Debug> SyncEngine for ConfigSyncEngine<T> {
    fn collection_name(&self) -> CollectionName {
        self.config.collection.clone()
    }

    fn set_local_encryption_key(&mut self, key: &str) -> anyhow::Result<()> {
        self.local_enc_key = Some(key.to_string());
        Ok(())
    }

    fn sync_started(&self) -> anyhow::Result<()> {
        let db = self.store.lock_db()?;
        let signal = db.begin_interrupt_scope()?;
        crate::db::schema::create_empty_sync_temp_tables(&db.writer)?;
        signal.err_if_interrupted()?;
        Ok(())
    }

    fn stage_incoming(
        &self,
        inbound: Vec<IncomingBso>,
        telem: &mut telemetry::Engine,
    ) -> anyhow::Result<()> {
        let db = self.store.lock_db()?;
        let signal = db.begin_interrupt_scope()?;

        // Stage all incoming items.
        let mut incoming_telemetry = telemetry::EngineIncoming::new();
        incoming_telemetry.applied(inbound.len() as u32);
        telem.incoming(incoming_telemetry);
        let tx = db.writer.unchecked_transaction()?;
        let incoming_impl = self.storage_impl.get_incoming_impl(&self.local_enc_key)?;

        incoming_impl.stage_incoming(&tx, inbound, &signal)?;
        tx.commit()?;
        Ok(())
    }

    fn apply(
        &self,
        timestamp: ServerTimestamp,
        _telem: &mut telemetry::Engine,
    ) -> anyhow::Result<Vec<OutgoingBso>> {
        let db = self.store.lock_db()?;
        let signal = db.begin_interrupt_scope()?;
        let tx = db.writer.unchecked_transaction()?;
        let incoming_impl = self.storage_impl.get_incoming_impl(&self.local_enc_key)?;
        let outgoing_impl = self.storage_impl.get_outgoing_impl(&self.local_enc_key)?;

        // Get "states" for each record...
        for state in incoming_impl.fetch_incoming_states(&tx)? {
            signal.err_if_interrupted()?;
            // Finally get a "plan" and apply it.
            let action = plan_incoming(&*incoming_impl, &tx, state)?;
            super::apply_incoming_action(&*incoming_impl, &tx, action)?;
        }

        // The timestamp value is handled differently in desktop v mobile. Record a
        // timestamp if we are given one now, so if we are interrupted merging or
        // creating outgoing changesets we don't need to re-download the same
        // records.
        if timestamp != ServerTimestamp(0) {
            self.put_meta(&tx, LAST_SYNC_META_KEY, &timestamp.as_millis())?;
        }

        incoming_impl.finish_incoming(&tx)?;

        // Finally, stage outgoing items.
        let outgoing = outgoing_impl.fetch_outgoing_records(&tx)?;
        // we're committing now because it may take a long time to actually perform the upload
        // and we've already staged everything we need to complete the sync in a way that
        // doesn't require the transaction to stay alive, so we commit now and start a new
        // transaction once complete
        tx.commit()?;
        Ok(outgoing)
    }

    fn set_uploaded(&self, new_timestamp: ServerTimestamp, ids: Vec<Guid>) -> anyhow::Result<()> {
        let db = self.store.lock_db()?;
        self.put_meta(&db.writer, LAST_SYNC_META_KEY, &new_timestamp.as_millis())?;
        let tx = db.writer.unchecked_transaction()?;
        let outgoing_impl = self.storage_impl.get_outgoing_impl(&self.local_enc_key)?;
        outgoing_impl.finish_synced_items(&tx, ids)?;
        tx.commit()?;
        Ok(())
    }

    fn get_collection_request(
        &self,
        server_timestamp: ServerTimestamp,
    ) -> anyhow::Result<Option<CollectionRequest>> {
        let db = self.store.lock_db()?;
        let since = ServerTimestamp(
            self.get_meta::<i64>(&db.writer, LAST_SYNC_META_KEY)?
                .unwrap_or_default(),
        );
        Ok(if since == server_timestamp {
            None
        } else {
            Some(
                CollectionRequest::new(self.collection_name())
                    .full()
                    .newer_than(since),
            )
        })
    }

    fn get_sync_assoc(&self) -> anyhow::Result<EngineSyncAssociation> {
        let db = self.store.lock_db()?;
        let global = self.get_meta(&db.writer, GLOBAL_SYNCID_META_KEY)?;
        let coll = self.get_meta(&db.writer, COLLECTION_SYNCID_META_KEY)?;
        Ok(if let (Some(global), Some(coll)) = (global, coll) {
            EngineSyncAssociation::Connected(CollSyncIds { global, coll })
        } else {
            EngineSyncAssociation::Disconnected
        })
    }

    fn reset(&self, assoc: &EngineSyncAssociation) -> anyhow::Result<()> {
        let db = self.store.lock_db()?;
        let tx = db.unchecked_transaction()?;
        self.storage_impl.reset_storage(&tx)?;
        // Reset the last sync time, so that the next sync fetches fresh records
        // from the server.
        self.put_meta(&tx, LAST_SYNC_META_KEY, &0)?;

        // Clear the sync ID if we're signing out, or set it to whatever the
        // server gave us if we're signing in.
        match assoc {
            EngineSyncAssociation::Disconnected => {
                self.delete_meta(&tx, GLOBAL_SYNCID_META_KEY)?;
                self.delete_meta(&tx, COLLECTION_SYNCID_META_KEY)?;
            }
            EngineSyncAssociation::Connected(ids) => {
                self.put_meta(&tx, GLOBAL_SYNCID_META_KEY, &ids.global)?;
                self.put_meta(&tx, COLLECTION_SYNCID_META_KEY, &ids.coll)?;
            }
        }

        tx.commit()?;
        Ok(())
    }

    fn wipe(&self) -> anyhow::Result<()> {
        warn!("not implemented as there isn't a valid use case for it");
        Ok(())
    }

    fn last_sync(&self) -> anyhow::Result<Option<ServerTimestamp>> {
        let db = self.store.lock_db()?;
        Ok(self
            .get_meta::<i64>(&db.writer, LAST_SYNC_META_KEY)?
            .map(ServerTimestamp::from_millis))
    }

    fn reset_last_sync(&self) -> anyhow::Result<()> {
        let db = self.store.lock_db()?;
        self.delete_meta(&db.writer, LAST_SYNC_META_KEY)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::credit_cards::add_internal_credit_card;
    use crate::db::credit_cards::tests::{
        get_all, insert_tombstone_record, test_insert_mirror_record,
    };
    use crate::db::models::credit_card::InternalCreditCard;
    use crate::db::schema::create_empty_sync_temp_tables;
    use crate::encryption::EncryptorDecryptor;
    use crate::sync::{IncomingBso, UnknownFields};
    use nss_as::ensure_initialized;
    use sql_support::ConnExt;

    impl InternalCreditCard {
        pub fn into_test_incoming_bso(
            self,
            encdec: &EncryptorDecryptor,
            unknown_fields: UnknownFields,
        ) -> IncomingBso {
            let mut payload = self.into_payload(encdec).expect("is json");
            payload.entry.unknown_fields = unknown_fields;
            IncomingBso::from_test_content(payload)
        }
    }

    // We use the credit-card engine here.
    fn create_engine() -> ConfigSyncEngine<InternalCreditCard> {
        let store = crate::db::store::Store::new_memory();
        crate::sync::credit_card::create_engine(Arc::new(store))
    }

    pub fn clear_cc_tables(conn: &Connection) -> rusqlite::Result<(), rusqlite::Error> {
        conn.execute_all(&[
            "DELETE FROM credit_cards_data;",
            "DELETE FROM credit_cards_mirror;",
            "DELETE FROM credit_cards_tombstones;",
            "DELETE FROM moz_meta;",
        ])
    }

    #[test]
    fn test_credit_card_engine_apply_timestamp() -> Result<()> {
        ensure_initialized();
        let mut credit_card_engine = create_engine();
        let test_key = crate::encryption::create_autofill_key().unwrap();
        credit_card_engine
            .set_local_encryption_key(&test_key)
            .unwrap();
        {
            let db = credit_card_engine.store.lock_db()?;
            create_empty_sync_temp_tables(&db.writer)?;
        }

        let mut telem = telemetry::Engine::new("whatever");
        let last_sync = 24;
        let result = credit_card_engine.apply(ServerTimestamp::from_millis(last_sync), &mut telem);
        assert!(result.is_ok());

        // check that last sync metadata was set
        let db = credit_card_engine.store.lock_db()?;
        let conn = &db.writer;

        assert_eq!(
            credit_card_engine.get_meta::<i64>(conn, LAST_SYNC_META_KEY)?,
            Some(last_sync)
        );

        Ok(())
    }

    #[test]
    fn test_credit_card_engine_get_sync_assoc() -> Result<()> {
        ensure_initialized();
        let credit_card_engine = create_engine();

        let result = credit_card_engine.get_sync_assoc();
        assert!(result.is_ok());

        // check that we disconnect if sync IDs not found
        assert_eq!(result.unwrap(), EngineSyncAssociation::Disconnected);

        // create sync metadata
        let global_guid = Guid::new("AAAA");
        let coll_guid = Guid::new("AAAA");
        let ids = CollSyncIds {
            global: global_guid,
            coll: coll_guid,
        };
        {
            let db = credit_card_engine.store.lock_db()?;
            let conn = &db.writer;
            credit_card_engine.put_meta(conn, GLOBAL_SYNCID_META_KEY, &ids.global)?;
            credit_card_engine.put_meta(conn, COLLECTION_SYNCID_META_KEY, &ids.coll)?;
        }

        let result = credit_card_engine.get_sync_assoc();
        assert!(result.is_ok());

        // check that we return the metadata
        assert_eq!(result.unwrap(), EngineSyncAssociation::Connected(ids));
        Ok(())
    }

    #[test]
    fn test_engine_sync_reset() -> Result<()> {
        ensure_initialized();
        let engine = create_engine();
        let encdec = EncryptorDecryptor::new_with_random_key().unwrap();

        let cc = InternalCreditCard {
            guid: Guid::random(),
            cc_name: "Ms Jane Doe".to_string(),
            cc_number_enc: encdec.encrypt("12341232412341234")?,
            cc_number_last_4: "1234".to_string(),
            cc_exp_month: 12,
            cc_exp_year: 2021,
            cc_type: "visa".to_string(),
            ..Default::default()
        };

        {
            // temp scope for the mutex lock.
            let db = engine.store.lock_db()?;
            let tx = db.writer.unchecked_transaction()?;
            // create a normal record, a mirror record and a tombstone.
            add_internal_credit_card(&tx, &cc)?;
            test_insert_mirror_record(
                &tx,
                cc.clone()
                    .into_test_incoming_bso(&encdec, Default::default()),
            );
            insert_tombstone_record(&tx, Guid::random().to_string())?;
            tx.commit()?;
        }

        // create sync metadata
        let global_guid = Guid::new("AAAA");
        let coll_guid = Guid::new("AAAA");
        let ids = CollSyncIds {
            global: global_guid.clone(),
            coll: coll_guid.clone(),
        };
        {
            let db = engine.store.lock_db()?;
            let conn = &db.writer;
            engine.put_meta(conn, GLOBAL_SYNCID_META_KEY, &ids.global)?;
            engine.put_meta(conn, COLLECTION_SYNCID_META_KEY, &ids.coll)?;
        }

        // call reset for sign out
        engine
            .reset(&EngineSyncAssociation::Disconnected)
            .expect("should work");

        {
            let db = engine.store.lock_db()?;
            let conn = &db.writer;

            // check that the mirror and tombstone tables have no records
            assert!(get_all(conn, "credit_cards_mirror".to_string())?.is_empty());
            assert!(get_all(conn, "credit_cards_tombstones".to_string())?.is_empty());

            // check that the last sync time was reset to 0
            let expected_sync_time = 0;
            assert_eq!(
                engine
                    .get_meta::<i64>(conn, LAST_SYNC_META_KEY)?
                    .unwrap_or(1),
                expected_sync_time
            );

            // check that the meta records were deleted
            assert!(engine
                .get_meta::<String>(conn, GLOBAL_SYNCID_META_KEY)?
                .is_none());
            assert!(engine
                .get_meta::<String>(conn, COLLECTION_SYNCID_META_KEY)?
                .is_none());

            clear_cc_tables(conn)?;

            // re-populating the tables
            let tx = conn.unchecked_transaction()?;
            add_internal_credit_card(&tx, &cc)?;
            test_insert_mirror_record(&tx, cc.into_test_incoming_bso(&encdec, Default::default()));
            insert_tombstone_record(&tx, Guid::random().to_string())?;
            tx.commit()?;
        }

        // call reset for sign in
        engine
            .reset(&EngineSyncAssociation::Connected(ids))
            .expect("should work");

        let db = engine.store.lock_db()?;
        let conn = &db.writer;
        // check that the meta records were set
        let retrieved_global_sync_id = engine.get_meta::<String>(conn, GLOBAL_SYNCID_META_KEY)?;
        assert_eq!(
            retrieved_global_sync_id.unwrap_or_default(),
            global_guid.to_string()
        );

        let retrieved_coll_sync_id = engine.get_meta::<String>(conn, COLLECTION_SYNCID_META_KEY)?;
        assert_eq!(
            retrieved_coll_sync_id.unwrap_or_default(),
            coll_guid.to_string()
        );
        Ok(())
    }
}
