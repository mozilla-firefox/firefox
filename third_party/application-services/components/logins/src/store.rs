/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use crate::db::{LoginDb, LoginsDeletionMetrics};
use crate::error::*;
use crate::login::{
    BulkResultEntry, EncryptedLogin, Login, LoginCandidate, LoginEntry, LoginEntryWithMeta,
};
use crate::LoginsSyncEngine;
use db_crypto::{DbCryptoApiError, EncryptorDecryptor};
use parking_lot::Mutex;
use sql_support::run_maintenance;
use std::path::Path;
use std::sync::{Arc, Weak};
use sync15::{
    engine::{EngineSyncAssociation, SyncEngine, SyncEngineId},
    ServerTimestamp,
};

/// What `LoginStore::check_decryptability()` found.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct DecryptabilityReport {
    /// Every stored login, local and mirror.
    pub total: u64,
    /// Logins that did not decrypt although the key was available.
    pub undecryptable: u64,
}

#[derive(uniffi::Enum)]
pub enum LoginOrErrorMessage {
    Login,
    String,
}

// Our "sync manager" will use whatever is stashed here.
lazy_static::lazy_static! {
    // Mutex: just taken long enough to update the inner stuff - needed
    //        to wrap the RefCell as they aren't `Sync`
    static ref STORE_FOR_MANAGER: Mutex<Weak<LoginStore>> = Mutex::new(Weak::new());
}

/// Called by the sync manager to get a sync engine via the store previously
/// registered with the sync manager.
pub fn get_registered_sync_engine(engine_id: &SyncEngineId) -> Option<Box<dyn SyncEngine>> {
    let weak = STORE_FOR_MANAGER.lock();
    match weak.upgrade() {
        None => None,
        Some(store) => match create_sync_engine(store, engine_id) {
            Ok(engine) => Some(engine),
            Err(e) => {
                report_error!("logins-sync-engine-create-error", "{e}");
                None
            }
        },
    }
}

fn create_sync_engine(
    store: Arc<LoginStore>,
    engine_id: &SyncEngineId,
) -> Result<Box<dyn SyncEngine>> {
    match engine_id {
        SyncEngineId::Passwords => Ok(Box::new(LoginsSyncEngine::new(Arc::clone(&store))?)),
        // panicking here seems reasonable - it's a static error if this
        // it hit, not something that runtime conditions can influence.
        _ => unreachable!("can't provide unknown engine: {}", engine_id),
    }
}

fn map_bulk_result_entry(
    enc_login: Result<EncryptedLogin>,
    encdec: &dyn EncryptorDecryptor,
) -> BulkResultEntry {
    match enc_login {
        Ok(enc_login) => match enc_login.decrypt(encdec) {
            Ok(login) => BulkResultEntry::Success { login },
            Err(error) => {
                warn!("Login could not be decrypted. This indicates a fundamental problem with the encryption key.");
                BulkResultEntry::Error {
                    message: error.to_string(),
                }
            }
        },
        Err(error) => BulkResultEntry::Error {
            message: error.to_string(),
        },
    }
}

pub struct LoginStore {
    pub db: Mutex<Option<LoginDb>>,
}

impl LoginStore {
    #[handle_error(Error)]
    pub fn new(path: impl AsRef<Path>, encdec: Arc<dyn EncryptorDecryptor>) -> ApiResult<Self> {
        let db = Mutex::new(Some(LoginDb::open(path, encdec)?));
        Ok(Self { db })
    }

    pub fn new_from_db(db: LoginDb) -> Self {
        let db = Mutex::new(Some(db));
        Self { db }
    }

    // Only used for tests, but it's `pub` the `sync-test` crate uses it.
    #[cfg(test)]
    pub fn new_in_memory() -> Self {
        let db = Mutex::new(Some(LoginDb::open_in_memory()));
        Self { db }
    }

    pub fn lock_db(&self) -> Result<parking_lot::MappedMutexGuard<'_, LoginDb>> {
        parking_lot::MutexGuard::try_map(self.db.lock(), |db| db.as_mut())
            .map_err(|_| Error::DatabaseClosed)
    }

    #[handle_error(Error)]
    pub fn is_empty(&self) -> ApiResult<bool> {
        Ok(self.lock_db()?.count_all()? == 0)
    }

    #[handle_error(Error)]
    pub fn list(&self) -> ApiResult<Vec<Login>> {
        let db = self.lock_db()?;
        db.get_all().and_then(|logins| {
            logins
                .into_iter()
                .map(|login| login.decrypt(db.encdec.as_ref()))
                .collect()
        })
    }

    /// List all logins without decrypting them.
    ///
    /// Unlike `list()` this never touches the encryption key, so consumers which only need the
    /// cleartext fields to decide which logins they care about can filter without forcing the
    /// user to authenticate.  Feed the ids of the matches to `get_many()`.
    #[handle_error(Error)]
    pub fn list_candidates(&self) -> ApiResult<Vec<LoginCandidate>> {
        Ok(self
            .lock_db()?
            .get_all()?
            .into_iter()
            .map(LoginCandidate::from)
            .collect())
    }

    /// Like `list_candidates()`, but only the logins whose origin is one of `origins`, or whose
    /// host is one of `domains` or a subdomain of one.
    ///
    /// This is meant as a pre-filter for consumers who have their own origin matching rules:
    /// pass every origin you would accept (eg, the `http://` variant too if you allow scheme
    /// upgrades) and the base domain of each host you would accept subdomains of, then run your
    /// own matching over the result.  Working out the base domain is up to the caller, since
    /// this component has no copy of the Public Suffix List.
    #[handle_error(Error)]
    pub fn list_candidates_by_origin(
        &self,
        origins: Vec<String>,
        domains: Vec<String>,
    ) -> ApiResult<Vec<LoginCandidate>> {
        Ok(self
            .lock_db()?
            .get_by_origins_or_domains(&origins, &domains)?
            .into_iter()
            .map(LoginCandidate::from)
            .collect())
    }

    #[handle_error(Error)]
    pub fn count(&self) -> ApiResult<i64> {
        self.lock_db()?.count_all()
    }

    #[handle_error(Error)]
    pub fn count_by_origin(&self, origin: &str) -> ApiResult<i64> {
        self.lock_db()?.count_by_origin(origin)
    }

    #[handle_error(Error)]
    pub fn count_by_form_action_origin(&self, form_action_origin: &str) -> ApiResult<i64> {
        self.lock_db()?
            .count_by_form_action_origin(form_action_origin)
    }

    #[handle_error(Error)]
    pub fn get(&self, id: &str) -> ApiResult<Option<Login>> {
        let db = self.lock_db()?;
        match db.get_by_id(id) {
            Ok(result) => match result {
                Some(enc_login) => enc_login.decrypt(db.encdec.as_ref()).map(Some),
                None => Ok(None),
            },
            Err(err) => Err(err),
        }
    }

    /// Get the logins with the given ids, decrypting them.
    ///
    /// This is the other half of `list_candidates()`: having filtered on the cleartext fields,
    /// only pay for decrypting the logins which actually matched.  Ids we don't have a login for
    /// are skipped; a login we can't decrypt fails the call, as it does for `list()`.
    #[handle_error(Error)]
    pub fn get_many(&self, ids: Vec<String>) -> ApiResult<Vec<Login>> {
        let db = self.lock_db()?;
        db.get_many(&ids)?
            .into_iter()
            .map(|login| login.decrypt(db.encdec.as_ref()))
            .collect()
    }

    /// Count the logins that do not decrypt with the current key, without returning or deleting
    /// anything, to tell a store with a few unreadable logins apart from one whose key no longer
    /// fits any of them.
    ///
    /// Stops at the first failure of the key itself (a cancelled primary password prompt, an NSS
    /// authentication error, a missing or invalid key) and reports it as that error: moving on
    /// would ask for the key, and so for the primary password, once per login.
    #[handle_error(Error)]
    pub fn check_decryptability(&self) -> ApiResult<DecryptabilityReport> {
        let db = self.lock_db()?;
        let mut report = DecryptabilityReport::default();
        for login in db.get_all()? {
            report.total += 1;
            match db.encdec.decrypt(login.sec_fields.as_bytes().into()) {
                Ok(_) => {}
                Err(DbCryptoApiError::DecryptionFailed { .. }) => report.undecryptable += 1,
                Err(e) => return Err(Error::CryptoError(e)),
            }
        }
        Ok(report)
    }

    #[handle_error(Error)]
    pub fn get_by_base_domain(&self, base_domain: &str) -> ApiResult<Vec<Login>> {
        let db = self.lock_db()?;
        db.get_by_base_domain(base_domain).and_then(|logins| {
            logins
                .into_iter()
                .map(|login| login.decrypt(db.encdec.as_ref()))
                .collect()
        })
    }

    #[handle_error(Error)]
    pub fn has_logins_by_base_domain(&self, base_domain: &str) -> ApiResult<bool> {
        self.lock_db()?
            .get_by_base_domain(base_domain)
            .map(|logins| !logins.is_empty())
    }

    #[handle_error(Error)]
    pub fn find_login_to_update(&self, entry: LoginEntry) -> ApiResult<Option<Login>> {
        let db = self.lock_db()?;
        db.find_login_to_update(entry)
    }

    #[handle_error(Error)]
    pub fn touch(&self, id: &str) -> ApiResult<()> {
        self.lock_db()?.touch(id)
    }

    #[handle_error(Error)]
    pub fn are_potentially_vulnerable_passwords(&self, ids: Vec<String>) -> ApiResult<Vec<String>> {
        // Note: Vec<&str> is not supported with UDL, so we receive Vec<String> and convert
        let db = self.lock_db()?;
        let ids: Vec<&str> = ids.iter().map(|id| &**id).collect();
        db.are_potentially_vulnerable_passwords(&ids)
    }

    #[handle_error(Error)]
    pub fn is_potentially_vulnerable_password(&self, id: &str) -> ApiResult<bool> {
        let db = self.lock_db()?;
        db.is_potentially_vulnerable_password(id)
    }

    #[handle_error(Error)]
    pub fn record_potentially_vulnerable_passwords(&self, passwords: Vec<String>) -> ApiResult<()> {
        let db = self.lock_db()?;
        db.record_potentially_vulnerable_passwords(passwords)
    }

    #[handle_error(Error)]
    pub fn reset_all_breaches(&self) -> ApiResult<()> {
        self.lock_db()?.reset_all_breaches()
    }

    #[handle_error(Error)]
    pub fn record_breach_alert_dismissal(&self, id: &str) -> ApiResult<()> {
        self.lock_db()?.record_breach_alert_dismissal(id)
    }

    #[handle_error(Error)]
    pub fn record_breach_alert_dismissal_time(&self, id: &str, timestamp: i64) -> ApiResult<()> {
        self.lock_db()?
            .record_breach_alert_dismissal_time(id, timestamp)
    }

    #[handle_error(Error)]
    pub fn delete(&self, id: &str) -> ApiResult<bool> {
        self.lock_db()?.delete(id)
    }

    #[handle_error(Error)]
    pub fn delete_many(&self, ids: Vec<String>) -> ApiResult<Vec<bool>> {
        // Note we need to receive a vector of String here because `Vec<&str>` is not supported
        // with UDL.
        let ids: Vec<&str> = ids.iter().map(|id| &**id).collect();
        self.lock_db()?.delete_many(ids)
    }

    #[handle_error(Error)]
    pub fn delete_all(&self) -> ApiResult<Vec<String>> {
        self.lock_db()?.delete_all()
    }

    #[handle_error(Error)]
    pub fn delete_all_except_fxa(&self) -> ApiResult<Vec<String>> {
        self.lock_db()?.delete_all_except_fxa()
    }

    #[handle_error(Error)]
    pub fn delete_undecryptable_records_for_remote_replacement(
        self: Arc<Self>,
    ) -> ApiResult<LoginsDeletionMetrics> {
        // This function was created for the iOS logins verification logic that will
        // remove records that prevent logins syncing. Once the verification logic is
        // removed from iOS, this function can be removed from the store.

        // Creating an engine requires locking the DB, so make sure to do this first
        let engine = LoginsSyncEngine::new(Arc::clone(&self))?;

        let db = self.lock_db()?;
        let deletion_stats = db.delete_undecryptable_records_for_remote_replacement()?;
        engine.set_last_sync(&db, ServerTimestamp(0))?;
        Ok(deletion_stats)
    }

    #[handle_error(Error)]
    pub fn wipe_local(&self) -> ApiResult<()> {
        self.lock_db()?.wipe_local()?;
        Ok(())
    }

    #[handle_error(Error)]
    pub fn wipe_local_except_fxa(&self) -> ApiResult<()> {
        self.lock_db()?.wipe_local_except_fxa()?;
        Ok(())
    }

    #[handle_error(Error)]
    pub fn reset(self: Arc<Self>) -> ApiResult<()> {
        // Reset should not exist here - all resets should be done via the
        // sync manager. It seems that actual consumers don't use this, but
        // some tests do, so it remains for now.
        let engine = LoginsSyncEngine::new(Arc::clone(&self))?;
        engine.do_reset(&EngineSyncAssociation::Disconnected)?;
        Ok(())
    }

    #[handle_error(Error)]
    pub fn update(&self, id: &str, entry: LoginEntry) -> ApiResult<Login> {
        let db = self.lock_db()?;
        db.update(id, entry)
            .and_then(|enc_login| enc_login.decrypt(db.encdec.as_ref()))
    }

    #[handle_error(Error)]
    pub fn add(&self, entry: LoginEntry) -> ApiResult<Login> {
        let db = self.lock_db()?;
        db.add(entry)
            .and_then(|enc_login| enc_login.decrypt(db.encdec.as_ref()))
    }

    #[handle_error(Error)]
    pub fn add_many(&self, entries: Vec<LoginEntry>) -> ApiResult<Vec<BulkResultEntry>> {
        let db = self.lock_db()?;
        let encdec = db.encdec.as_ref();
        db.add_many(entries).map(|enc_logins| {
            enc_logins
                .into_iter()
                .map(|enc_login| map_bulk_result_entry(enc_login, encdec))
                .collect()
        })
    }

    /// This method is intended to preserve metadata (LoginMeta) during a migration.
    /// In normal operation, this method should not be used; instead,
    /// use `add(entry)`, which manages the corresponding fields itself.
    #[handle_error(Error)]
    pub fn add_with_meta(&self, entry_with_meta: LoginEntryWithMeta) -> ApiResult<Login> {
        let db = self.lock_db()?;
        db.add_with_meta(entry_with_meta)
            .and_then(|enc_login| enc_login.decrypt(db.encdec.as_ref()))
    }

    #[handle_error(Error)]
    pub fn add_many_with_meta(
        &self,
        entries_with_meta: Vec<LoginEntryWithMeta>,
    ) -> ApiResult<Vec<BulkResultEntry>> {
        let db = self.lock_db()?;
        let encdec = db.encdec.as_ref();
        db.add_many_with_meta(entries_with_meta).map(|enc_logins| {
            enc_logins
                .into_iter()
                .map(|enc_login| map_bulk_result_entry(enc_login, encdec))
                .collect()
        })
    }

    #[handle_error(Error)]
    pub fn add_or_update(&self, entry: LoginEntry) -> ApiResult<Login> {
        let db = self.lock_db()?;
        db.add_or_update(entry)
            .and_then(|enc_login| enc_login.decrypt(db.encdec.as_ref()))
    }

    #[handle_error(Error)]
    pub fn run_maintenance(&self, options: Option<RunMaintenanceOptions>) -> ApiResult<()> {
        let conn = self.lock_db()?;
        let options = options.unwrap_or_default();
        run_maintenance(&conn)?;
        if options.delete_undecryptable_records_for_remote_replacement {
            conn.delete_undecryptable_records_for_remote_replacement()?;
        }
        Ok(())
    }

    pub fn shutdown(&self) {
        if let Some(db) = self.db.lock().take() {
            let _ = db.shutdown();
        }
    }

    // This allows the embedding app to say "make this instance available to
    // the sync manager". The implementation is more like "offer to sync mgr"
    // (thereby avoiding us needing to link with the sync manager) but
    // `register_with_sync_manager()` is logically what's happening so that's
    // the name it gets.
    pub fn register_with_sync_manager(self: Arc<Self>) {
        let mut state = STORE_FOR_MANAGER.lock();
        *state = Arc::downgrade(&self);
    }

    // this isn't exposed by uniffi - currently the
    // only consumer of this is our "example" (and hence why they
    // are `pub` and not `pub(crate)`).
    // We could probably make the example work with the sync manager - but then
    // our example would link with places and logins etc, and it's not a big
    // deal really.
    #[handle_error(Error)]
    pub fn create_logins_sync_engine(self: Arc<Self>) -> ApiResult<Box<dyn SyncEngine>> {
        Ok(Box::new(LoginsSyncEngine::new(self)?) as Box<dyn SyncEngine>)
    }
}

pub struct RunMaintenanceOptions {
    pub delete_undecryptable_records_for_remote_replacement: bool,
}

impl Default for RunMaintenanceOptions {
    fn default() -> Self {
        Self {
            delete_undecryptable_records_for_remote_replacement: true,
        }
    }
}

#[cfg(not(feature = "keydb"))]
#[cfg(test)]
mod tests {
    use super::*;
    use crate::util;
    use db_crypto::{create_key, KeyManager, ManagedEncryptorDecryptor};
    use nss_as::ensure_initialized;
    use std::cmp::Reverse;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::SystemTime;

    fn assert_logins_equiv(a: &LoginEntry, b: &Login) {
        assert_eq!(a.origin, b.origin);
        assert_eq!(a.form_action_origin, b.form_action_origin);
        assert_eq!(a.http_realm, b.http_realm);
        assert_eq!(a.username_field, b.username_field);
        assert_eq!(a.password_field, b.password_field);
        assert_eq!(b.username, a.username);
        assert_eq!(b.password, a.password);
    }

    #[test]
    fn test_general() {
        ensure_initialized();

        let store = LoginStore::new_in_memory();
        let list = store.list().expect("Grabbing Empty list to work");
        assert_eq!(list.len(), 0);
        let start_us = util::system_time_ms_i64(SystemTime::now());

        let a = LoginEntry {
            origin: "https://www.example.com".into(),
            form_action_origin: Some("https://www.example.com".into()),
            username_field: "user_input".into(),
            password_field: "pass_input".into(),
            username: "user".into(),
            password: "password".into(),
            ..Default::default()
        };

        let b = LoginEntry {
            origin: "https://www.example2.com".into(),
            http_realm: Some("Some String Here".into()),
            username: "asdf".into(),
            password: "fdsa".into(),
            ..Default::default()
        };
        let a_id = store.add(a.clone()).expect("added a").id;
        let b_id = store.add(b.clone()).expect("added b").id;

        let a_from_db = store
            .get(&a_id)
            .expect("Not to error getting a")
            .expect("a to exist");

        assert_logins_equiv(&a, &a_from_db);
        assert!(a_from_db.time_created >= start_us);
        assert!(a_from_db.time_password_changed >= start_us);
        assert!(a_from_db.time_last_used >= start_us);
        assert_eq!(a_from_db.times_used, 1);

        let b_from_db = store
            .get(&b_id)
            .expect("Not to error getting b")
            .expect("b to exist");

        assert_logins_equiv(&LoginEntry { ..b.clone() }, &b_from_db);
        assert!(b_from_db.time_created >= start_us);
        assert!(b_from_db.time_password_changed >= start_us);
        assert!(b_from_db.time_last_used >= start_us);
        assert_eq!(b_from_db.times_used, 1);

        let mut list = store.list().expect("Grabbing list to work");
        assert_eq!(list.len(), 2);

        let mut expect = vec![a_from_db, b_from_db.clone()];

        list.sort_by_key(|b| Reverse(b.guid()));
        expect.sort_by_key(|b| Reverse(b.guid()));
        assert_eq!(list, expect);

        store.delete(&a_id).expect("Successful delete");
        assert!(store
            .get(&a_id)
            .expect("get after delete should still work")
            .is_none());

        let list = store.list().expect("Grabbing list to work");
        assert_eq!(list.len(), 1);
        assert_eq!(list[0], b_from_db);

        let has_logins = store
            .has_logins_by_base_domain("example2.com")
            .expect("Expect a result for this origin");
        assert!(has_logins);

        let list = store
            .get_by_base_domain("example2.com")
            .expect("Expect a list for this origin");
        assert_eq!(list.len(), 1);
        assert_eq!(list[0], b_from_db);

        let has_logins = store
            .has_logins_by_base_domain("www.example.com")
            .expect("Expect a result for this origin");
        assert!(!has_logins);

        let list = store
            .get_by_base_domain("www.example.com")
            .expect("Expect an empty list");
        assert_eq!(list.len(), 0);

        let now_us = util::system_time_ms_i64(SystemTime::now());
        let b2 = LoginEntry {
            username: b.username.to_owned(),
            password: "newpass".into(),
            ..b
        };

        store
            .update(&b_id, b2.clone())
            .expect("update b should work");

        let b_after_update = store
            .get(&b_id)
            .expect("Not to error getting b")
            .expect("b to exist");

        assert_logins_equiv(&b2, &b_after_update);
        assert!(b_after_update.time_created >= start_us);
        assert!(b_after_update.time_created <= now_us);
        assert!(b_after_update.time_password_changed >= now_us);
        // An edit is not a use: usage stats are unchanged by update().
        assert_eq!(b_after_update.time_last_used, b_from_db.time_last_used);
        assert_eq!(b_after_update.times_used, 1);
    }

    #[test]
    fn test_sync_manager_registration() {
        ensure_initialized();
        let store = Arc::new(LoginStore::new_in_memory());
        assert_eq!(Arc::strong_count(&store), 1);
        assert_eq!(Arc::weak_count(&store), 0);
        Arc::clone(&store).register_with_sync_manager();
        assert_eq!(Arc::strong_count(&store), 1);
        assert_eq!(Arc::weak_count(&store), 1);
        let registered = STORE_FOR_MANAGER.lock().upgrade().expect("should upgrade");
        assert!(Arc::ptr_eq(&store, &registered));
        drop(registered);
        // should be no new references
        assert_eq!(Arc::strong_count(&store), 1);
        assert_eq!(Arc::weak_count(&store), 1);
        // dropping the registered object should drop the registration.
        drop(store);
        assert!(STORE_FOR_MANAGER.lock().upgrade().is_none());
    }

    #[test]
    fn test_wipe_local_on_a_fresh_database_is_a_noop() {
        ensure_initialized();
        // If the database has data, then wipe_local() returns > 0 rows deleted
        let db = LoginDb::open_in_memory();
        db.add_or_update(LoginEntry {
            origin: "https://www.example.com".into(),
            form_action_origin: Some("https://www.example.com".into()),
            username_field: "user_input".into(),
            password_field: "pass_input".into(),
            username: "coolperson21".into(),
            password: "p4ssw0rd".into(),
            ..Default::default()
        })
        .unwrap();
        assert!(db.wipe_local().unwrap() > 0);

        // If the database is empty, then wipe_local() returns 0 rows deleted
        let db = LoginDb::open_in_memory();
        assert_eq!(db.wipe_local().unwrap(), 0);
    }

    #[test]
    fn test_shutdown() {
        ensure_initialized();
        let store = LoginStore::new_in_memory();
        store.shutdown();
        assert!(matches!(
            store.list(),
            Err(LoginsApiError::UnexpectedLoginsApiError { reason: _ })
        ));
        assert!(store.db.lock().is_none());
    }

    /// A `KeyManager` which counts how often it was asked for the key, so we can prove
    /// `list_candidates()` never asks.
    struct CountingKeyManager {
        key: String,
        calls: AtomicUsize,
    }

    impl KeyManager for CountingKeyManager {
        fn get_key(&self) -> db_crypto::ApiResult<Vec<u8>> {
            self.calls.fetch_add(1, Ordering::SeqCst);
            Ok(self.key.as_bytes().into())
        }
    }

    fn store_with_encdec(encdec: Arc<dyn EncryptorDecryptor>) -> LoginStore {
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        LoginStore::new_from_db(LoginDb::with_connection(conn, encdec).unwrap())
    }

    fn test_entry(origin: &str, username: &str) -> LoginEntry {
        LoginEntry {
            origin: origin.into(),
            http_realm: Some("Some Realm".into()),
            username: username.into(),
            password: "p4ssw0rd".into(),
            ..Default::default()
        }
    }

    #[test]
    fn test_list_candidates_does_not_need_the_key() {
        ensure_initialized();

        let key_manager = Arc::new(CountingKeyManager {
            key: create_key().unwrap(),
            calls: AtomicUsize::new(0),
        });
        let store = store_with_encdec(Arc::new(ManagedEncryptorDecryptor::new(
            key_manager.clone(),
        )));

        let a = store
            .add(test_entry("https://www.a.com", "a-user"))
            .unwrap();
        let b = store
            .add(test_entry("https://www.b.com", "b-user"))
            .unwrap();

        // Adding needed the key; listing the candidates must not.
        assert!(key_manager.calls.load(Ordering::SeqCst) > 0);
        key_manager.calls.store(0, Ordering::SeqCst);

        let mut candidates = store.list_candidates().unwrap();
        assert_eq!(key_manager.calls.load(Ordering::SeqCst), 0);

        candidates.sort_by(|l, r| l.origin.cmp(&r.origin));
        assert_eq!(candidates.len(), 2);
        for (candidate, login) in candidates.iter().zip([&a, &b]) {
            assert_eq!(candidate.id, login.id);
            assert_eq!(candidate.origin, login.origin);
            assert_eq!(candidate.http_realm, login.http_realm);
            assert_eq!(candidate.form_action_origin, login.form_action_origin);
            assert_eq!(candidate.username_field, login.username_field);
            assert_eq!(candidate.password_field, login.password_field);
            assert_eq!(candidate.times_used, login.times_used);
            assert_eq!(candidate.time_created, login.time_created);
            assert_eq!(candidate.time_last_used, login.time_last_used);
            assert_eq!(candidate.time_password_changed, login.time_password_changed);
            assert_eq!(
                candidate.time_last_breach_alert_dismissed,
                login.time_last_breach_alert_dismissed
            );
        }
    }

    #[test]
    fn test_list_candidates_by_origin() {
        ensure_initialized();

        let key_manager = Arc::new(CountingKeyManager {
            key: create_key().unwrap(),
            calls: AtomicUsize::new(0),
        });
        let store = store_with_encdec(Arc::new(ManagedEncryptorDecryptor::new(
            key_manager.clone(),
        )));

        let a = store
            .add(test_entry("https://www.a.com", "a-user"))
            .unwrap();
        let sub_a = store
            .add(test_entry("https://login.sub.a.com", "sub-a-user"))
            .unwrap();
        let b = store.add(test_entry("http://b.com", "b-user")).unwrap();
        store
            .add(test_entry("https://www.c.com", "c-user"))
            .unwrap();
        key_manager.calls.store(0, Ordering::SeqCst);

        let mut ids: Vec<String> = store
            .list_candidates_by_origin(vec!["http://b.com".into()], vec!["a.com".into()])
            .unwrap()
            .into_iter()
            .map(|c| c.id)
            .collect();
        ids.sort_unstable();
        let mut expected = vec![a.id, sub_a.id, b.id];
        expected.sort_unstable();
        assert_eq!(ids, expected);

        // Like `list_candidates()`, this must not need the key.
        assert_eq!(key_manager.calls.load(Ordering::SeqCst), 0);

        assert!(store
            .list_candidates_by_origin(vec![], vec![])
            .unwrap()
            .is_empty());
    }

    #[test]
    fn test_get_many() {
        ensure_initialized();

        let store = LoginStore::new_in_memory();
        let a = store
            .add(test_entry("https://www.a.com", "a-user"))
            .unwrap();
        let b = store
            .add(test_entry("https://www.b.com", "b-user"))
            .unwrap();
        store
            .add(test_entry("https://www.c.com", "c-user"))
            .unwrap();

        assert_eq!(store.get_many(vec![]).unwrap(), vec![]);

        // Ids we don't know about are skipped rather than being an error.  Note the results come
        // back in the db's order, not the order of the ids we asked for.
        let mut got = store
            .get_many(vec![b.id.clone(), "no-such-guid".to_string(), a.id.clone()])
            .unwrap();
        got.sort_by(|l, r| l.origin.cmp(&r.origin));
        assert_eq!(got, vec![a, b]);
    }

    #[test]
    fn test_get_many_with_an_undecryptable_login() {
        ensure_initialized();

        let store = LoginStore::new_in_memory();
        let a = store
            .add(test_entry("https://www.a.com", "a-user"))
            .unwrap();
        let b = store
            .add(test_entry("https://www.b.com", "b-user"))
            .unwrap();

        store
            .lock_db()
            .unwrap()
            .db
            .execute(
                "UPDATE loginsL SET secFields = 'not-a-ciphertext' WHERE guid = ?",
                [&b.id],
            )
            .unwrap();

        // As with `list()`, one login we can't read fails the whole call.
        assert!(matches!(
            store.get_many(vec![a.id.clone(), b.id]),
            Err(LoginsApiError::UnexpectedLoginsApiError { .. })
        ));
    }

    #[test]
    fn test_check_decryptability() {
        ensure_initialized();

        let store = LoginStore::new_in_memory();
        assert_eq!(
            store.check_decryptability().unwrap(),
            DecryptabilityReport::default()
        );

        store
            .add(test_entry("https://www.a.com", "a-user"))
            .unwrap();
        let b = store
            .add(test_entry("https://www.b.com", "b-user"))
            .unwrap();
        store
            .add(test_entry("https://www.c.com", "c-user"))
            .unwrap();
        assert_eq!(
            store.check_decryptability().unwrap(),
            DecryptabilityReport {
                total: 3,
                ..Default::default()
            }
        );

        store
            .lock_db()
            .unwrap()
            .db
            .execute(
                "UPDATE loginsL SET secFields = 'not-a-ciphertext' WHERE guid = ?",
                [&b.id],
            )
            .unwrap();

        assert_eq!(
            store.check_decryptability().unwrap(),
            DecryptabilityReport {
                total: 3,
                undecryptable: 1,
            }
        );
    }

    /// A `KeyManager` that can be told to stop handing out the key with a given error, the way a
    /// cancelled primary password prompt does, and counts how often it was asked.
    struct FailingKeyManager {
        key: String,
        error: Mutex<Option<fn() -> db_crypto::DbCryptoApiError>>,
        calls: AtomicUsize,
    }

    impl KeyManager for FailingKeyManager {
        fn get_key(&self) -> db_crypto::ApiResult<Vec<u8>> {
            self.calls.fetch_add(1, Ordering::SeqCst);
            match *self.error.lock() {
                Some(error) => Err(error()),
                None => Ok(self.key.as_bytes().into()),
            }
        }
    }

    fn store_failing_with(
        error: fn() -> db_crypto::DbCryptoApiError,
    ) -> (LoginStore, Arc<FailingKeyManager>) {
        let key_manager = Arc::new(FailingKeyManager {
            key: create_key().unwrap(),
            error: Mutex::new(None),
            calls: AtomicUsize::new(0),
        });
        let store = store_with_encdec(Arc::new(ManagedEncryptorDecryptor::new(
            key_manager.clone(),
        )));
        for origin in [
            "https://www.a.com",
            "https://www.b.com",
            "https://www.c.com",
        ] {
            store.add(test_entry(origin, "user")).unwrap();
        }
        *key_manager.error.lock() = Some(error);
        key_manager.calls.store(0, Ordering::SeqCst);
        (store, key_manager)
    }

    #[test]
    fn test_check_decryptability_stops_at_a_key_error() {
        ensure_initialized();

        let (store, key_manager) =
            store_failing_with(|| db_crypto::DbCryptoApiError::AuthenticationCanceled);
        assert!(matches!(
            store.check_decryptability(),
            Err(LoginsApiError::AuthenticationCanceled)
        ));
        assert_eq!(
            key_manager.calls.load(Ordering::SeqCst),
            1,
            "a key error must not be retried for every login"
        );
    }

    #[test]
    fn test_check_decryptability_keeps_the_kind_of_key_error() {
        ensure_initialized();

        let (store, _) =
            store_failing_with(|| db_crypto::DbCryptoApiError::NSSAuthenticationError {
                reason: "token locked".into(),
            });
        assert!(matches!(
            store.check_decryptability(),
            Err(LoginsApiError::NSSAuthenticationError { reason }) if reason == "token locked"
        ));

        // The encryptor reports any failure of the key manager other than authentication as a
        // missing key.
        let (store, _) =
            store_failing_with(|| db_crypto::DbCryptoApiError::UnexpectedDbCryptoApiError {
                reason: "callback failed".into(),
            });
        assert!(matches!(
            store.check_decryptability(),
            Err(LoginsApiError::MissingKey)
        ));
    }

    #[test]
    fn test_delete_undecryptable_records_for_remote_replacement() {
        ensure_initialized();
        let store = Arc::new(LoginStore::new_in_memory());
        // Not much of a test, but let's make sure this doesn't deadlock at least.
        store
            .delete_undecryptable_records_for_remote_replacement()
            .unwrap();
    }
}

#[test]
fn test_send() {
    fn ensure_send<T: Send>() {}
    ensure_send::<LoginStore>();
}

#[cfg(feature = "keydb")]
#[cfg(test)]
mod tests_keydb {
    use super::*;
    use crate::{ManagedEncryptorDecryptor, NSSKeyManager, PrimaryPasswordAuthenticator};
    use async_trait::async_trait;
    use nss_as::ensure_initialized_with_profile_dir;
    use std::path::PathBuf;

    struct MockPrimaryPasswordAuthenticator {
        password: String,
    }

    #[async_trait]
    impl PrimaryPasswordAuthenticator for MockPrimaryPasswordAuthenticator {
        async fn get_primary_password(&self) -> db_crypto::ApiResult<String> {
            Ok(self.password.clone())
        }
        async fn on_authentication_success(&self) -> db_crypto::ApiResult<()> {
            Ok(())
        }
        async fn on_authentication_failure(&self) -> db_crypto::ApiResult<()> {
            Ok(())
        }
    }

    fn profile_path() -> PathBuf {
        std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../support/rc_crypto/nss/fixtures/profile")
    }

    #[test]
    fn decrypting_logins_with_primary_password() {
        ensure_initialized_with_profile_dir(profile_path());

        // `password` is the primary password of the profile fixture
        let primary_password_authenticator = MockPrimaryPasswordAuthenticator {
            password: "password".to_string(),
        };
        let key_manager = NSSKeyManager::new(
            crate::KEY_NAME.to_string(),
            Arc::new(primary_password_authenticator),
        );
        let encdec = ManagedEncryptorDecryptor::new(Arc::new(key_manager));
        let store = LoginStore::new(profile_path().join("logins.db"), Arc::new(encdec))
            .expect("store from fixtures");
        let list = store.list().expect("Grabbing list to work");

        assert_eq!(list.len(), 1);

        assert_eq!(list[0].origin, "https://www.example.com");
        assert_eq!(list[0].username, "test");
        assert_eq!(list[0].password, "test");
    }
}
