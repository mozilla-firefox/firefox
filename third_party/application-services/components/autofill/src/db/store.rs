/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::db::models::address::{
    Address, AddressBulkResultEntry, AddressBulkTombstoneResultEntry, AddressTombstone,
    UpdatableAddressFields, UpdatableAddressFieldsWithMeta,
};
use crate::db::models::credit_card::{
    CreditCard, CreditCardBulkResultEntry, CreditCardBulkTombstoneResultEntry, CreditCardCandidate,
    CreditCardTombstone, UpdatableCreditCardFields, UpdatableCreditCardFieldsWithMeta,
};
use crate::db::models::passport::{Passport, UpdatablePassportFields};
use crate::db::{
    addresses, credit_cards, credit_cards::CreditCardsDeletionMetrics, passports, AutofillDb,
};
use crate::error::*;
use db_crypto::EncryptorDecryptor;
use error_support::handle_error;
use parking_lot::{MappedMutexGuard, Mutex, MutexGuard};
use rusqlite::{
    types::{FromSql, ToSql},
    Connection,
};
use sql_support::{self, run_maintenance, ConnExt};
use std::path::Path;
use std::sync::{Arc, Weak};
use sync15::engine::{SyncEngine, SyncEngineId};
use sync_guid::Guid;

// Our "sync manager" will use whatever is stashed here.
lazy_static::lazy_static! {
    // Mutex: just taken long enough to update the contents - needed to wrap
    //        the Weak as it isn't `Sync`
    // [Arc/Weak]<Store>: What the sync manager actually needs.
    static ref STORE_FOR_MANAGER: std::sync::Mutex<Weak<Store>> =
        std::sync::Mutex::new(Weak::new());
}

/// Called by the sync manager to get a sync engine via the store previously
/// registered with the sync manager.
pub fn get_registered_sync_engine(engine_id: &SyncEngineId) -> Option<Box<dyn SyncEngine>> {
    let weak = STORE_FOR_MANAGER.lock().unwrap();
    match weak.upgrade() {
        None => None,
        Some(store) => match engine_id {
            SyncEngineId::Addresses => Some(Box::new(crate::sync::address::create_engine(store))),
            SyncEngineId::CreditCards => {
                Some(Box::new(crate::sync::credit_card::create_engine(store)))
            }
            // panicking here seems reasonable - it's a static error if this
            // it hit, not something that runtime conditions can influence.
            _ => unreachable!("can't provide unknown engine: {}", engine_id),
        },
    }
}

// This is the type that uniffi exposes.
pub struct Store {
    pub(crate) db: Mutex<Option<AutofillDb>>,
}

impl Store {
    #[handle_error(Error)]
    pub fn new(db_path: impl AsRef<Path>, encdec: Arc<dyn EncryptorDecryptor>) -> ApiResult<Self> {
        Ok(Self {
            db: Mutex::new(Some(AutofillDb::new(db_path, encdec)?)),
        })
    }

    /// Creates a store backed by an in-memory database with its own memory API (required for unit tests).
    #[cfg(test)]
    pub fn new_memory() -> Self {
        Self {
            db: Mutex::new(Some(crate::db::test::new_mem_db())),
        }
    }

    /// Creates a store backed by an in-memory database that shares its memory API (required for autofill sync tests).
    #[handle_error(Error)]
    pub fn new_shared_memory(
        db_name: &str,
        encdec: Arc<dyn EncryptorDecryptor>,
    ) -> ApiResult<Self> {
        Ok(Self {
            db: Mutex::new(Some(AutofillDb::new_memory(db_name, encdec)?)),
        })
    }

    pub(crate) fn lock_db(&self) -> Result<MappedMutexGuard<'_, AutofillDb>> {
        MutexGuard::try_map(self.db.lock(), |db| db.as_mut()).map_err(|_| Error::DatabaseClosed)
    }

    #[handle_error(Error)]
    pub fn add_credit_card(&self, fields: UpdatableCreditCardFields) -> ApiResult<CreditCard> {
        let db = self.lock_db()?;
        let credit_card = credit_cards::add_credit_card(&db.writer, db.encdec.as_ref(), fields)?;
        credit_card.into_external(db.encdec.as_ref())
    }

    /// Adds a credit card **including metadata**. Normally the metadata (guid,
    /// timestamps, sync change counter) is owned by this store and
    /// `add_credit_card` will take care of it. This is for the case where a
    /// record is being migrated from another store that already holds it.
    #[handle_error(Error)]
    pub fn add_credit_card_with_meta(
        &self,
        entry_with_meta: UpdatableCreditCardFieldsWithMeta,
    ) -> ApiResult<CreditCard> {
        let db = self.lock_db()?;
        credit_cards::add_credit_card_with_meta(
            &db.writer,
            db.encdec.as_ref(),
            entry_with_meta.fields,
            entry_with_meta.meta,
        )?
        .into_external(db.encdec.as_ref())
    }

    /// Adds multiple credit cards **including metadata**, with a result per
    /// record.
    #[handle_error(Error)]
    pub fn add_many_credit_cards_with_meta(
        &self,
        entries_with_meta: Vec<UpdatableCreditCardFieldsWithMeta>,
    ) -> ApiResult<Vec<CreditCardBulkResultEntry>> {
        let db = self.lock_db()?;
        let results = credit_cards::add_many_credit_cards_with_meta(
            &db.writer,
            db.encdec.as_ref(),
            entries_with_meta,
        )?;
        Ok(results
            .into_iter()
            .map(|result| match result {
                Ok(credit_card) => match credit_card.into_external(db.encdec.as_ref()) {
                    Ok(credit_card) => CreditCardBulkResultEntry::Success { credit_card },
                    Err(e) => CreditCardBulkResultEntry::Error {
                        message: e.to_string(),
                    },
                },
                Err(message) => CreditCardBulkResultEntry::Error { message },
            })
            .collect())
    }

    /// Adds tombstones for credit cards whose deletion has not yet been
    /// uploaded, with a result per record.
    #[handle_error(Error)]
    pub fn add_many_credit_card_tombstones(
        &self,
        tombstones: Vec<CreditCardTombstone>,
    ) -> ApiResult<Vec<CreditCardBulkTombstoneResultEntry>> {
        let results = credit_cards::add_many_credit_card_tombstones(
            &self.lock_db()?.writer,
            tombstones
                .into_iter()
                .map(|t| (t.guid, t.time_deleted))
                .collect(),
        )?;
        Ok(results
            .into_iter()
            .map(|result| match result {
                Ok(guid) => CreditCardBulkTombstoneResultEntry::Success { guid },
                Err(message) => CreditCardBulkTombstoneResultEntry::Error { message },
            })
            .collect())
    }

    /// Removes every credit card and every credit card tombstone.
    ///
    /// A migration primitive: it leaves the sync mirror intact and produces no
    /// tombstones, so the deletions are never uploaded and a synced profile gets
    /// the records back on the next sync. Use `delete_credit_card` to delete on the
    /// user's behalf.
    #[handle_error(Error)]
    pub fn delete_all_credit_cards(&self) -> ApiResult<()> {
        credit_cards::delete_all_credit_cards(&self.lock_db()?.writer)?;
        Ok(())
    }

    /// Updates a credit card **including metadata**, setting both its fields
    /// and its timestamps and `times_used` to the supplied values. Normally you
    /// will use `update_credit_card` instead, which leaves `time_last_modified`
    /// to this store; this is for keeping a record identical to one held
    /// elsewhere. Errors with `NoSuchRecord` if the guid is absent.
    #[handle_error(Error)]
    pub fn update_credit_card_with_meta(
        &self,
        entry_with_meta: UpdatableCreditCardFieldsWithMeta,
    ) -> ApiResult<()> {
        let db = self.lock_db()?;
        credit_cards::update_credit_card_with_meta(
            &db.writer,
            db.encdec.as_ref(),
            entry_with_meta.fields,
            entry_with_meta.meta,
        )
    }

    #[handle_error(Error)]
    pub fn get_credit_card(&self, guid: String) -> ApiResult<CreditCard> {
        let db = self.lock_db()?;
        let credit_card = credit_cards::get_credit_card(&db.writer, &Guid::new(&guid))?;
        credit_card.into_external(db.encdec.as_ref())
    }

    #[handle_error(Error)]
    pub fn get_all_credit_cards(&self) -> ApiResult<Vec<CreditCard>> {
        let db = self.lock_db()?;
        credit_cards::get_all_credit_cards(&db.writer)?
            .into_iter()
            .map(|x| x.into_external(db.encdec.as_ref()))
            .collect()
    }

    /// List every credit card without decrypting it.
    ///
    /// Unlike `get_all_credit_cards()` this never touches the encryption key,
    /// so listing cards cannot prompt for a primary password. Read the one
    /// card whose secrets are needed with `get_credit_card()`.
    #[handle_error(Error)]
    pub fn list_credit_card_candidates(&self) -> ApiResult<Vec<CreditCardCandidate>> {
        let db = self.lock_db()?;
        Ok(credit_cards::get_all_credit_cards(&db.writer)?
            .into_iter()
            .map(CreditCardCandidate::from)
            .collect())
    }

    /// Get a single credit card without decrypting it.
    ///
    /// Like `list_credit_card_candidates()` this never touches the encryption
    /// key, so it cannot prompt for a primary password. Use `get_credit_card()`
    /// when the card's number or CVV is needed.
    #[handle_error(Error)]
    pub fn get_credit_card_candidate(&self, guid: String) -> ApiResult<CreditCardCandidate> {
        let db = self.lock_db()?;
        let credit_card = credit_cards::get_credit_card(&db.writer, &Guid::new(&guid))?;
        Ok(CreditCardCandidate::from(credit_card))
    }

    #[handle_error(Error)]
    pub fn count_all_credit_cards(&self) -> ApiResult<i64> {
        let count = credit_cards::count_all_credit_cards(&self.lock_db()?.writer)?;
        Ok(count)
    }

    #[handle_error(Error)]
    pub fn update_credit_card(
        &self,
        guid: String,
        credit_card: UpdatableCreditCardFields,
    ) -> ApiResult<()> {
        let db = self.lock_db()?;
        credit_cards::update_credit_card(
            &db.writer,
            db.encdec.as_ref(),
            &Guid::new(&guid),
            &credit_card,
        )
    }

    #[handle_error(Error)]
    pub fn delete_credit_card(&self, guid: String) -> ApiResult<bool> {
        credit_cards::delete_credit_card(&self.lock_db()?.writer, &Guid::new(&guid))
    }

    #[handle_error(Error)]
    pub fn touch_credit_card(&self, guid: String) -> ApiResult<()> {
        credit_cards::touch(&self.lock_db()?.writer, &Guid::new(&guid))
    }

    #[handle_error(Error)]
    pub fn add_address(&self, new_address: UpdatableAddressFields) -> ApiResult<Address> {
        Ok(addresses::add_address(&self.lock_db()?.writer, new_address)?.into())
    }

    /// Adds an address **including metadata**. Normally you will use
    /// `add_address` instead, and the metadata (guid, timestamps, change counter)
    /// will be taken care of here. However, in some cases this method is
    /// necessary, for example when migrating data from another store that
    /// already contains the metadata.
    #[handle_error(Error)]
    pub fn add_address_with_meta(
        &self,
        entry_with_meta: UpdatableAddressFieldsWithMeta,
    ) -> ApiResult<Address> {
        Ok(addresses::add_address_with_meta(
            &self.lock_db()?.writer,
            entry_with_meta.fields,
            entry_with_meta.meta,
        )?
        .into())
    }

    /// Adds multiple addresses **including metadata**, with a result per record.
    #[handle_error(Error)]
    pub fn add_many_addresses_with_meta(
        &self,
        entries_with_meta: Vec<UpdatableAddressFieldsWithMeta>,
    ) -> ApiResult<Vec<AddressBulkResultEntry>> {
        let results =
            addresses::add_many_addresses_with_meta(&self.lock_db()?.writer, entries_with_meta)?;
        Ok(results
            .into_iter()
            .map(|result| match result {
                Ok(address) => AddressBulkResultEntry::Success {
                    address: address.into(),
                },
                Err(message) => AddressBulkResultEntry::Error { message },
            })
            .collect())
    }

    /// Adds tombstones for addresses whose deletion has not yet been uploaded,
    /// with a result per record.
    #[handle_error(Error)]
    pub fn add_many_address_tombstones(
        &self,
        tombstones: Vec<AddressTombstone>,
    ) -> ApiResult<Vec<AddressBulkTombstoneResultEntry>> {
        let results = addresses::add_many_address_tombstones(
            &self.lock_db()?.writer,
            tombstones
                .into_iter()
                .map(|t| (t.guid, t.time_deleted))
                .collect(),
        )?;
        Ok(results
            .into_iter()
            .map(|result| match result {
                Ok(guid) => AddressBulkTombstoneResultEntry::Success { guid },
                Err(message) => AddressBulkTombstoneResultEntry::Error { message },
            })
            .collect())
    }

    /// Removes every address and every address tombstone.
    ///
    /// A migration primitive: it leaves the sync mirror intact and produces no
    /// tombstones, so the deletions are never uploaded and a synced profile gets
    /// the records back on the next sync. Use `delete_address` to delete on the
    /// user's behalf.
    #[handle_error(Error)]
    pub fn delete_all_addresses(&self) -> ApiResult<()> {
        addresses::delete_all_addresses(&self.lock_db()?.writer)?;
        Ok(())
    }

    #[handle_error(Error)]
    pub fn get_address(&self, guid: String) -> ApiResult<Address> {
        Ok(addresses::get_address(&self.lock_db()?.writer, &Guid::new(&guid))?.into())
    }

    #[handle_error(Error)]
    pub fn get_all_addresses(&self) -> ApiResult<Vec<Address>> {
        let addresses = addresses::get_all_addresses(&self.lock_db()?.writer)?
            .into_iter()
            .map(|x| x.into())
            .collect();
        Ok(addresses)
    }

    #[handle_error(Error)]
    pub fn count_all_addresses(&self) -> ApiResult<i64> {
        let count = addresses::count_all_addresses(&self.lock_db()?.writer)?;
        Ok(count)
    }

    #[handle_error(Error)]
    pub fn update_address(&self, guid: String, address: UpdatableAddressFields) -> ApiResult<()> {
        addresses::update_address(&self.lock_db()?.writer, &Guid::new(&guid), &address)
    }

    /// Updates an address **including metadata**, setting both its fields and
    /// its timestamps and `times_used` to the supplied values. Normally you will
    /// use `update_address` instead, which leaves `time_last_modified` to this
    /// store; this is for keeping a record identical to one held elsewhere.
    /// Errors with `NoSuchRecord` if the guid is absent.
    #[handle_error(Error)]
    pub fn update_address_with_meta(
        &self,
        entry_with_meta: UpdatableAddressFieldsWithMeta,
    ) -> ApiResult<()> {
        addresses::update_address_with_meta(
            &self.lock_db()?.writer,
            entry_with_meta.fields,
            entry_with_meta.meta,
        )
    }

    #[handle_error(Error)]
    pub fn delete_address(&self, guid: String) -> ApiResult<bool> {
        addresses::delete_address(&self.lock_db()?.writer, &Guid::new(&guid))
    }

    #[handle_error(Error)]
    pub fn touch_address(&self, guid: String) -> ApiResult<()> {
        addresses::touch(&self.lock_db()?.writer, &Guid::new(&guid))
    }

    #[handle_error(Error)]
    pub fn add_passport(&self, fields: UpdatablePassportFields) -> ApiResult<Passport> {
        Ok(passports::add_passport(&self.lock_db()?.writer, fields)?.into())
    }

    #[handle_error(Error)]
    pub fn get_passport(&self, guid: String) -> ApiResult<Passport> {
        Ok(passports::get_passport(&self.lock_db()?.writer, &Guid::new(&guid))?.into())
    }

    #[handle_error(Error)]
    pub fn get_all_passports(&self) -> ApiResult<Vec<Passport>> {
        let passports = passports::get_all_passports(&self.lock_db()?.writer)?
            .into_iter()
            .map(|x| x.into())
            .collect();
        Ok(passports)
    }

    #[handle_error(Error)]
    pub fn count_all_passports(&self) -> ApiResult<i64> {
        passports::count_all_passports(&self.lock_db()?.writer)
    }

    #[handle_error(Error)]
    pub fn update_passport(
        &self,
        guid: String,
        passport: UpdatablePassportFields,
    ) -> ApiResult<()> {
        passports::update_passport(&self.lock_db()?.writer, &Guid::new(&guid), &passport)
    }

    #[handle_error(Error)]
    pub fn delete_passport(&self, guid: String) -> ApiResult<bool> {
        passports::delete_passport(&self.lock_db()?.writer, &Guid::new(&guid))
    }

    #[handle_error(Error)]
    pub fn touch_passport(&self, guid: String) -> ApiResult<()> {
        passports::touch(&self.lock_db()?.writer, &Guid::new(&guid))
    }

    #[handle_error(Error)]
    pub fn scrub_encrypted_data(self: Arc<Self>) -> ApiResult<()> {
        // scrub the data on disk
        // Currently only credit cards have encrypted data
        credit_cards::scrub_encrypted_credit_card_data(&self.lock_db()?.writer)?;
        // Force the sync engine to refetch data (only need to do this for the credit cards, since the
        // addresses engine doesn't store encrypted data).
        crate::sync::credit_card::create_engine(self).reset_local_sync_data()?;
        Ok(())
    }

    #[handle_error(Error)]
    pub fn scrub_undecryptable_credit_card_data_for_remote_replacement(
        self: Arc<Self>,
    ) -> ApiResult<CreditCardsDeletionMetrics> {
        let db = self.lock_db()?;
        let deletion_stats =
            credit_cards::scrub_undecryptable_credit_card_data_for_remote_replacement(&db)?;

        // Here we reset the local sync data so that the credit card engine syncs as if
        // it were the first sync. This will potentially allow a previous sync of the
        // record that exists on the sync server to overwrite the local record and restore
        // the scrubbed credit card number.
        crate::sync::credit_card::create_engine(self.clone())
            .reset_local_sync_data_for_verification(&db.writer)?;
        Ok(deletion_stats)
    }

    #[handle_error(Error)]
    pub fn run_maintenance(&self) -> ApiResult<()> {
        let conn = self.lock_db()?;
        run_maintenance(&conn)?;
        Ok(())
    }

    pub fn shutdown(&self) {
        if let Some(db) = self.db.lock().take() {
            db.close();
        }
    }

    // This allows the embedding app to say "make this instance available to
    // the sync manager". The implementation is more like "offer to sync mgr"
    // (thereby avoiding us needing to link with the sync manager) but
    // `register_with_sync_manager()` is logically what's happening so that's
    // the name it gets.
    pub fn register_with_sync_manager(self: Arc<Self>) {
        let mut state = STORE_FOR_MANAGER.lock().unwrap();
        *state = Arc::downgrade(&self);
    }

    // These 2 are a little odd - they aren't exposed by uniffi - currently the
    // only consumer of this is our "example" (and hence why they
    // are `pub` and not `pub(crate)`).
    // We could probably make the example work with the sync manager - but then
    // our example would link with places and logins etc, and it's not a big
    // deal really.
    pub fn create_credit_cards_sync_engine(self: Arc<Self>) -> Box<dyn SyncEngine> {
        Box::new(crate::sync::credit_card::create_engine(self))
    }

    pub fn create_addresses_sync_engine(self: Arc<Self>) -> Box<dyn SyncEngine> {
        Box::new(crate::sync::address::create_engine(self))
    }
}

pub(crate) fn put_meta(conn: &Connection, key: &str, value: &dyn ToSql) -> Result<()> {
    conn.execute_cached(
        "REPLACE INTO moz_meta (key, value) VALUES (:key, :value)",
        &[(":key", &key as &dyn ToSql), (":value", value)],
    )?;
    Ok(())
}

pub(crate) fn get_meta<T: FromSql>(conn: &Connection, key: &str) -> Result<Option<T>> {
    let res = conn.try_query_one(
        "SELECT value FROM moz_meta WHERE key = :key",
        &[(":key", &key)],
        true,
    )?;
    Ok(res)
}

pub(crate) fn delete_meta(conn: &Connection, key: &str) -> Result<()> {
    conn.execute_cached("DELETE FROM moz_meta WHERE key = :key", &[(":key", &key)])?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::test::{new_mem_db, test_encdec};
    use nss_as::ensure_initialized;

    #[test]
    fn test_autofill_meta() -> Result<()> {
        let db = new_mem_db();
        let test_key = "TEST KEY A";
        let test_value = "TEST VALUE A";
        let test_key2 = "TEST KEY B";
        let test_value2 = "TEST VALUE B";

        put_meta(&db, test_key, &test_value)?;
        put_meta(&db, test_key2, &test_value2)?;

        let retrieved_value: String = get_meta(&db, test_key)?.expect("test value");
        let retrieved_value2: String = get_meta(&db, test_key2)?.expect("test value 2");

        assert_eq!(retrieved_value, test_value);
        assert_eq!(retrieved_value2, test_value2);

        // check that the value of an existing key can be updated
        let test_value3 = "TEST VALUE C";
        put_meta(&db, test_key, &test_value3)?;

        let retrieved_value3: String = get_meta(&db, test_key)?.expect("test value 3");

        assert_eq!(retrieved_value3, test_value3);

        // check that a deleted key is not retrieved
        delete_meta(&db, test_key)?;
        let retrieved_value4: Option<String> = get_meta(&db, test_key)?;
        assert!(retrieved_value4.is_none());

        db.writer.execute("DELETE FROM moz_meta", [])?;

        Ok(())
    }

    #[test]
    fn test_static_key_factory_rejects_bad_key() {
        ensure_initialized();
        assert!(matches!(
            crate::create_autofill_store_with_static_key_manager(
                ":memory:".to_string(),
                "garbage".to_string(),
            ),
            Err(crate::error::AutofillApiError::CryptoError { .. })
        ));
    }

    #[test]
    fn test_sync_manager_registration() {
        let store = Arc::new(Store::new_shared_memory("sync-mgr-test", test_encdec()).unwrap());
        assert_eq!(Arc::strong_count(&store), 1);
        assert_eq!(Arc::weak_count(&store), 0);
        Arc::clone(&store).register_with_sync_manager();
        assert_eq!(Arc::strong_count(&store), 1);
        assert_eq!(Arc::weak_count(&store), 1);
        let registered = STORE_FOR_MANAGER
            .lock()
            .unwrap()
            .upgrade()
            .expect("should upgrade");
        assert!(Arc::ptr_eq(&store, &registered));
        drop(registered);
        // should be no new references
        assert_eq!(Arc::strong_count(&store), 1);
        assert_eq!(Arc::weak_count(&store), 1);
        // dropping the registered object should drop the registration.
        drop(store);
        assert!(STORE_FOR_MANAGER.lock().unwrap().upgrade().is_none());
    }

    #[test]
    fn test_shutdown_closes_the_store() {
        let store = Store::new_shared_memory("shutdown-test", test_encdec()).expect("create store");
        // Operations succeed before shutdown.
        assert_eq!(store.count_all_passports().expect("count"), 0);

        store.shutdown();

        // After shutdown, operations return an error rather than panicking or
        // operating on a half-closed store.
        assert!(store.count_all_passports().is_err());

        // shutdown is idempotent.
        store.shutdown();
    }

    #[test]
    fn test_cvv_roundtrip() {
        ensure_initialized();
        let store = Store::new_shared_memory("cvv-roundtrip", test_encdec()).unwrap();
        let fields = UpdatableCreditCardFields {
            cc_name: "jane doe".to_string(),
            cc_number: "4111111111117629".to_string(),
            cc_cvv: Some("123".to_string()),
            cc_exp_month: 1,
            cc_exp_year: 2030,
            cc_type: "visa".to_string(),
        };
        let card = store.add_credit_card(fields.clone()).unwrap();
        assert_eq!(card.cc_cvv.as_deref(), Some("123"));
        // Stored encrypted, not as the bare value.
        {
            let db = store.lock_db().unwrap();
            let stored: String = db
                .query_row(
                    "SELECT cc_cvv_enc FROM credit_cards_data WHERE guid = :guid",
                    rusqlite::named_params! { ":guid": card.guid },
                    |row| row.get(0),
                )
                .unwrap();
            assert!(stored.len() > 20);
            assert!(!stored.contains("123"));
        }

        // No CVV stores the empty marker and reads back as None; an empty
        // string is treated the same as None.
        let card2 = store
            .add_credit_card(UpdatableCreditCardFields {
                cc_number: "4111111111111111".to_string(),
                cc_cvv: Some(String::new()),
                ..fields.clone()
            })
            .unwrap();
        assert_eq!(card2.cc_cvv, None);

        // Updating replaces or clears it.
        store
            .update_credit_card(
                card.guid.clone(),
                UpdatableCreditCardFields {
                    cc_cvv: Some("999".to_string()),
                    ..fields.clone()
                },
            )
            .unwrap();
        assert_eq!(
            store
                .get_credit_card(card.guid.clone())
                .unwrap()
                .cc_cvv
                .as_deref(),
            Some("999")
        );
        store
            .update_credit_card(
                card.guid.clone(),
                UpdatableCreditCardFields {
                    cc_cvv: Some(String::new()),
                    ..fields
                },
            )
            .unwrap();
        assert_eq!(store.get_credit_card(card.guid).unwrap().cc_cvv, None);
    }

    #[test]
    fn test_update_without_cvv_keeps_it() {
        ensure_initialized();
        let store = Store::new_shared_memory("cvv-kept-on-update", test_encdec()).unwrap();
        let fields = UpdatableCreditCardFields {
            cc_name: "jane doe".to_string(),
            cc_number: "4111111111117629".to_string(),
            cc_cvv: Some("123".to_string()),
            cc_exp_month: 1,
            cc_exp_year: 2030,
            cc_type: "visa".to_string(),
        };
        let card = store.add_credit_card(fields.clone()).unwrap();

        // A caller that does not know about CVVs edits other fields.
        store
            .update_credit_card(
                card.guid.clone(),
                UpdatableCreditCardFields {
                    cc_name: "jane q doe".to_string(),
                    cc_exp_year: 2031,
                    cc_cvv: None,
                    ..fields
                },
            )
            .unwrap();
        let updated = store.get_credit_card(card.guid.clone()).unwrap();
        assert_eq!(updated.cc_name, "jane q doe");
        assert_eq!(updated.cc_exp_year, 2031);
        assert_eq!(updated.cc_cvv.as_deref(), Some("123"));
    }

    #[test]
    fn test_empty_number_stays_empty() {
        ensure_initialized();
        let store = Store::new_shared_memory("empty-number", test_encdec()).unwrap();
        let empty = UpdatableCreditCardFields {
            cc_name: "jane doe".to_string(),
            cc_number: String::new(),
            cc_cvv: None,
            cc_exp_month: 1,
            cc_exp_year: 2030,
            cc_type: "visa".to_string(),
        };
        let card = store.add_credit_card(empty.clone()).unwrap();
        assert_eq!(card.cc_number, "");
        assert_eq!(card.cc_number_last_4, "");

        let with_number = store
            .add_credit_card(UpdatableCreditCardFields {
                cc_number: "4111111111117629".to_string(),
                cc_cvv: None,
                ..empty.clone()
            })
            .unwrap();
        store
            .update_credit_card(with_number.guid.clone(), empty)
            .unwrap();
        assert_eq!(
            store
                .get_credit_card(with_number.guid.clone())
                .unwrap()
                .cc_number,
            ""
        );

        // Stored as the empty marker, not as a ciphertext of "".
        let db = store.lock_db().unwrap();
        for guid in [&card.guid, &with_number.guid] {
            let enc: String = db
                .query_row(
                    "SELECT cc_number_enc FROM credit_cards_data WHERE guid = :guid",
                    rusqlite::named_params! { ":guid": guid },
                    |row| row.get(0),
                )
                .unwrap();
            assert_eq!(enc, "");
        }
    }

    #[test]
    fn test_candidates_do_not_decrypt() {
        ensure_initialized();
        let store = Store::new_shared_memory("candidates", test_encdec()).unwrap();
        // A foreign key: any attempt to decrypt fails, so a successful read
        // proves none was made.
        let foreign = crate::db::test::random_key_encryptor().unwrap();
        let with_cvv = crate::db::models::credit_card::InternalCreditCard {
            guid: sync_guid::Guid::random(),
            cc_name: "jane doe".to_string(),
            cc_number_enc: crate::db::models::credit_card::encrypt_str(
                &foreign,
                "4111111111117629",
            )
            .unwrap(),
            cc_cvv_enc: crate::db::models::credit_card::encrypt_str(&foreign, "123").unwrap(),
            cc_number_last_4: "7629".to_string(),
            cc_exp_month: 1,
            cc_exp_year: 2030,
            cc_type: "visa".to_string(),
            ..Default::default()
        };
        let without_cvv = crate::db::models::credit_card::InternalCreditCard {
            guid: sync_guid::Guid::random(),
            cc_name: "john doe".to_string(),
            cc_number_enc: crate::db::models::credit_card::encrypt_str(
                &foreign,
                "4111111111111111",
            )
            .unwrap(),
            cc_number_last_4: "1111".to_string(),
            cc_exp_month: 2,
            cc_exp_year: 2031,
            cc_type: "visa".to_string(),
            ..Default::default()
        };
        {
            let db = store.lock_db().unwrap();
            let tx = db.unchecked_transaction().unwrap();
            crate::db::credit_cards::add_internal_credit_card(&tx, &with_cvv).unwrap();
            crate::db::credit_cards::add_internal_credit_card(&tx, &without_cvv).unwrap();
            tx.commit().unwrap();
        }

        let mut candidates = store.list_credit_card_candidates().unwrap();
        candidates.sort_by(|a, b| a.cc_name.cmp(&b.cc_name));
        assert_eq!(candidates.len(), 2);
        assert_eq!(candidates[0].guid, with_cvv.guid.to_string());
        assert_eq!(candidates[0].cc_name, "jane doe");
        assert_eq!(candidates[0].cc_number_last_4, "7629");
        assert_eq!(candidates[0].cc_exp_month, 1);
        assert_eq!(candidates[0].cc_type, "visa");
        assert!(candidates[0].has_cvv);
        assert_eq!(candidates[1].cc_name, "john doe");
        assert!(!candidates[1].has_cvv);

        let candidate = store
            .get_credit_card_candidate(with_cvv.guid.to_string())
            .unwrap();
        assert_eq!(candidate.guid, with_cvv.guid.to_string());
        assert_eq!(candidate.cc_name, "jane doe");
        assert_eq!(candidate.cc_number_last_4, "7629");
        assert!(candidate.has_cvv);
        let candidate = store
            .get_credit_card_candidate(without_cvv.guid.to_string())
            .unwrap();
        assert_eq!(candidate.cc_name, "john doe");
        assert!(!candidate.has_cvv);

        assert!(matches!(
            store.get_credit_card_candidate(sync_guid::Guid::random().to_string()),
            Err(crate::error::AutofillApiError::NoSuchRecord { .. })
        ));
    }

    #[test]
    fn test_unreadable_number_fails_the_read() {
        ensure_initialized();
        let store = Store::new_shared_memory("unreadable-number", test_encdec()).unwrap();
        let foreign = crate::db::test::random_key_encryptor().unwrap();
        let internal = crate::db::models::credit_card::InternalCreditCard {
            guid: sync_guid::Guid::random(),
            cc_name: "jane doe".to_string(),
            cc_number_enc: crate::db::models::credit_card::encrypt_str(
                &foreign,
                "4111111111117629",
            )
            .unwrap(),
            cc_number_last_4: "7629".to_string(),
            cc_exp_month: 1,
            cc_exp_year: 2030,
            cc_type: "visa".to_string(),
            ..Default::default()
        };
        {
            let db = store.lock_db().unwrap();
            let tx = db.unchecked_transaction().unwrap();
            crate::db::credit_cards::add_internal_credit_card(&tx, &internal).unwrap();
            tx.commit().unwrap();
        }

        assert!(store.get_credit_card(internal.guid.to_string()).is_err());
        assert!(store.get_all_credit_cards().is_err());
    }

    // Data written before the store owned its encryptor was encrypted the
    // same way - a bare jwcrypto JWE of the number under the consumer's key.
    // It has to stay readable through the transparent API, with no migration.
    #[test]
    fn test_reads_pre_transparency_ciphertext() {
        ensure_initialized();
        let key = db_crypto::create_key().unwrap();
        let store = Store::new_shared_memory(
            "old-format",
            Arc::new(crate::static_key_encryptor(&key).unwrap()),
        )
        .unwrap();

        let old_style = jwcrypto::EncryptorDecryptor::new(&key)
            .unwrap()
            .encrypt("4111111111117629")
            .unwrap();
        let internal = crate::db::models::credit_card::InternalCreditCard {
            guid: sync_guid::Guid::random(),
            cc_name: "jane doe".to_string(),
            cc_number_enc: old_style,
            cc_number_last_4: "7629".to_string(),
            cc_exp_month: 1,
            cc_exp_year: 2030,
            cc_type: "visa".to_string(),
            ..Default::default()
        };
        {
            let db = store.lock_db().unwrap();
            let tx = db.unchecked_transaction().unwrap();
            crate::db::credit_cards::add_internal_credit_card(&tx, &internal).unwrap();
            tx.commit().unwrap();
        }

        let card = store.get_credit_card(internal.guid.to_string()).unwrap();
        assert_eq!(card.cc_number, "4111111111117629");
    }

    #[test]
    fn test_scrub_undecryptable_credit_card_data_for_remote_replacement() {
        ensure_initialized();
        let store =
            Arc::new(Store::new_shared_memory("scrub-test", test_encdec()).expect("create store"));

        store
            .add_credit_card(UpdatableCreditCardFields {
                cc_name: "john deer".to_string(),
                cc_number: "567812345678123456781".to_string(),
                cc_cvv: None,
                cc_exp_month: 10,
                cc_exp_year: 2025,
                cc_type: "mastercard".to_string(),
            })
            .expect("add credit card to database");

        store
            .scrub_undecryptable_credit_card_data_for_remote_replacement()
            .expect("scrub credit card record");
    }
}
