/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Logins DB handling
///
/// The logins database works differently than other components because "mirror" and "local" mean
/// different things.  At some point we should probably refactor to make it match them, but here's
/// how it works for now:
///
///   - loginsM is the mirror table, which means it stores what we believe is on the server.  This
///     means either the last record we fetched from the server or the last record we uploaded.
///   - loginsL is the local table, which means it stores local changes that have not been sent to
///     the server.
///   - When we want to fetch a record, we need to look in both loginsL and loginsM for the data.
///     If a record is in both tables, then we prefer the loginsL data.  GET_BY_GUID_SQL contains a
///     clever UNION query to accomplish this.
///   - If a record is in both the local and mirror tables, we call the local record the "overlay"
///     and set the is_overridden flag on the mirror record.
///   - When we sync, the presence of a record in loginsL means that there was a local change that
///     we need to send to the the server and/or reconcile it with incoming changes from the
///     server.
///   - After we sync, we move all records from loginsL to loginsM, overwriting any previous data.
///     loginsL will be an empty table after this.  See mark_as_synchronized() for the details.
use crate::encryption::EncryptorDecryptor;
use crate::error::*;
use crate::login::*;
use crate::schema;
use crate::sync::SyncStatus;
use crate::util;
use interrupt_support::{SqlInterruptHandle, SqlInterruptScope};
use lazy_static::lazy_static;
use rusqlite::{
    named_params,
    types::{FromSql, ToSql},
    Connection,
};
use sql_support::ConnExt;
use std::ops::Deref;
use std::path::Path;
use std::sync::Arc;
use std::time::SystemTime;
use sync_guid::Guid;
use url::{Host, Url};

pub struct LoginDb {
    pub db: Connection,
    pub encdec: Arc<dyn EncryptorDecryptor>,
    interrupt_handle: Arc<SqlInterruptHandle>,
}

pub struct LoginsDeletionMetrics {
    pub local_deleted: u64,
    pub mirror_deleted: u64,
}

impl LoginDb {
    pub fn with_connection(db: Connection, encdec: Arc<dyn EncryptorDecryptor>) -> Result<Self> {
        #[cfg(test)]
        {
            util::init_test_logging();
        }

        // `temp_store = 2` is required on Android to force the DB to keep temp
        // files in memory, since on Android there's no tmp partition. See
        // https://github.com/mozilla/mentat/issues/505. Ideally we'd only
        // do this on Android, or allow caller to configure it.
        db.set_pragma("temp_store", 2)?;

        let mut logins = Self {
            interrupt_handle: Arc::new(SqlInterruptHandle::new(&db)),
            encdec,
            db,
        };
        let tx = logins.db.transaction()?;
        schema::init(&tx)?;
        tx.commit()?;
        Ok(logins)
    }

    pub fn open(path: impl AsRef<Path>, encdec: Arc<dyn EncryptorDecryptor>) -> Result<Self> {
        Self::with_connection(Connection::open(path)?, encdec)
    }

    #[cfg(test)]
    pub fn open_in_memory() -> Self {
        let encdec: Arc<dyn EncryptorDecryptor> =
            crate::encryption::test_utils::TEST_ENCDEC.clone();
        Self::with_connection(Connection::open_in_memory().unwrap(), encdec).unwrap()
    }

    pub fn new_interrupt_handle(&self) -> Arc<SqlInterruptHandle> {
        Arc::clone(&self.interrupt_handle)
    }

    #[inline]
    pub fn begin_interrupt_scope(&self) -> Result<SqlInterruptScope> {
        Ok(self.interrupt_handle.begin_interrupt_scope()?)
    }
}

impl ConnExt for LoginDb {
    #[inline]
    fn conn(&self) -> &Connection {
        &self.db
    }
}

impl Deref for LoginDb {
    type Target = Connection;
    #[inline]
    fn deref(&self) -> &Connection {
        &self.db
    }
}

// login specific stuff.

impl LoginDb {
    pub(crate) fn put_meta(&self, key: &str, value: &dyn ToSql) -> Result<()> {
        self.execute_cached(
            "REPLACE INTO loginsSyncMeta (key, value) VALUES (:key, :value)",
            named_params! { ":key": key, ":value": value },
        )?;
        Ok(())
    }

    pub(crate) fn get_meta<T: FromSql>(&self, key: &str) -> Result<Option<T>> {
        self.try_query_row(
            "SELECT value FROM loginsSyncMeta WHERE key = :key",
            named_params! { ":key": key },
            |row| Ok::<_, Error>(row.get(0)?),
            true,
        )
    }

    pub(crate) fn delete_meta(&self, key: &str) -> Result<()> {
        self.execute_cached(
            "DELETE FROM loginsSyncMeta WHERE key = :key",
            named_params! { ":key": key },
        )?;
        Ok(())
    }

    pub fn count_all(&self) -> Result<i64> {
        let mut stmt = self.db.prepare_cached(&COUNT_ALL_SQL)?;

        let count: i64 = stmt.query_row([], |row| row.get(0))?;
        Ok(count)
    }

    pub fn count_by_origin(&self, origin: &str) -> Result<i64> {
        match LoginEntry::validate_and_fixup_origin(origin) {
            Ok(result) => {
                let origin = result.unwrap_or(origin.to_string());
                let mut stmt = self.db.prepare_cached(&COUNT_BY_ORIGIN_SQL)?;
                let count: i64 =
                    stmt.query_row(named_params! { ":origin": origin }, |row| row.get(0))?;
                Ok(count)
            }
            Err(e) => {
                // don't log the input string as it's PII.
                warn!("count_by_origin was passed an invalid origin: {}", e);
                Ok(0)
            }
        }
    }

    pub fn count_by_form_action_origin(&self, form_action_origin: &str) -> Result<i64> {
        match LoginEntry::validate_and_normalize_form_action_origin(form_action_origin) {
            Ok(result) => {
                let form_action_origin = result.unwrap_or(form_action_origin.to_string());
                let mut stmt = self.db.prepare_cached(&COUNT_BY_FORM_ACTION_ORIGIN_SQL)?;
                let count: i64 = stmt.query_row(
                    named_params! { ":form_action_origin": form_action_origin },
                    |row| row.get(0),
                )?;
                Ok(count)
            }
            Err(e) => {
                // don't log the input string as it's PII.
                warn!(
                    "count_by_form_action_origin was passed an invalid origin: {}",
                    e
                );
                Ok(0)
            }
        }
    }

    pub fn get_all(&self) -> Result<Vec<EncryptedLogin>> {
        let mut stmt = self.db.prepare_cached(&GET_ALL_SQL)?;
        let rows = stmt.query_and_then([], EncryptedLogin::from_row)?;
        rows.collect::<Result<_>>()
    }

    pub fn get_by_base_domain(&self, base_domain: &str) -> Result<Vec<EncryptedLogin>> {
        // We first parse the input string as a host so it is normalized.
        let base_host = match Host::parse(base_domain) {
            Ok(d) => d,
            Err(e) => {
                // don't log the input string as it's PII.
                warn!("get_by_base_domain was passed an invalid domain: {}", e);
                return Ok(vec![]);
            }
        };
        // We just do a linear scan. Another option is to have an indexed
        // reverse-host column or similar, but current thinking is that it's
        // extra complexity for (probably) zero actual benefit given the record
        // counts are expected to be so low.
        // A regex would probably make this simpler, but we don't want to drag
        // in a regex lib just for this.
        let mut stmt = self.db.prepare_cached(&GET_ALL_SQL)?;
        let rows = stmt
            .query_and_then([], EncryptedLogin::from_row)?
            .filter(|r| {
                let login = r
                    .as_ref()
                    .ok()
                    .and_then(|login| Url::parse(&login.fields.origin).ok());
                let this_host = login.as_ref().and_then(|url| url.host());
                match (&base_host, this_host) {
                    (Host::Domain(base), Some(Host::Domain(look))) => {
                        // a fairly long-winded way of saying
                        // `login.fields.origin == base_domain ||
                        //  login.fields.origin.ends_with('.' + base_domain);`
                        let mut rev_input = base.chars().rev();
                        let mut rev_host = look.chars().rev();
                        loop {
                            match (rev_input.next(), rev_host.next()) {
                                (Some(ref a), Some(ref b)) if a == b => continue,
                                (None, None) => return true, // exactly equal
                                (None, Some(ref h)) => return *h == '.',
                                _ => return false,
                            }
                        }
                    }
                    // ip addresses must match exactly.
                    (Host::Ipv4(base), Some(Host::Ipv4(look))) => *base == look,
                    (Host::Ipv6(base), Some(Host::Ipv6(look))) => *base == look,
                    // all "mismatches" in domain types are false.
                    _ => false,
                }
            });
        rows.collect::<Result<_>>()
    }

    pub fn get_by_id(&self, id: &str) -> Result<Option<EncryptedLogin>> {
        self.try_query_row(
            &GET_BY_GUID_SQL,
            &[(":guid", &id as &dyn ToSql)],
            EncryptedLogin::from_row,
            true,
        )
    }

    // Match a `LoginEntry` being saved to existing logins in the DB
    //
    // When a user is saving new login, there are several cases for how we want to save the data:
    //
    //  - Adding a new login: `None` will be returned
    //  - Updating an existing login: `Some(login)` will be returned and the username will match
    //    the one for look.
    //  - Filling in a blank username for an existing login: `Some(login)` will be returned
    //    with a blank username.
    //
    //  Returns an Err if the new login is not valid and could not be fixed up
    pub fn find_login_to_update(&self, look: LoginEntry) -> Result<Option<Login>> {
        let look = look.fixup()?;
        let logins = self
            .get_by_entry_target(&look)?
            .into_iter()
            .map(|enc_login| enc_login.decrypt(self.encdec.as_ref()))
            .collect::<Result<Vec<Login>>>()?;
        Ok(logins
            // First, try to match the username
            .iter()
            .find(|login| login.username == look.username)
            // Fall back on a blank username
            .or_else(|| logins.iter().find(|login| login.username.is_empty()))
            // Clone the login to avoid ref issues when returning across the FFI
            .cloned())
    }

    pub fn touch(&self, id: &str) -> Result<()> {
        let tx = self.unchecked_transaction()?;
        self.ensure_local_overlay_exists(id)?;
        self.mark_mirror_overridden(id)?;
        let now_ms = util::system_time_ms_i64(SystemTime::now());
        // As on iOS, just using a record doesn't flip it's status to changed.
        // TODO: this might be wrong for lockbox!
        self.execute_cached(
            "UPDATE loginsL
             SET timeLastUsed = :now_millis,
                 timesUsed = timesUsed + 1,
                 local_modified = :now_millis
             WHERE guid = :guid
                 AND is_deleted = 0",
            named_params! {
                ":now_millis": now_ms,
                ":guid": id,
            },
        )?;
        tx.commit()?;
        Ok(())
    }

    /// Records passwords in the breachesL table for password reuse detection.
    ///
    /// Encrypts and stores passwords, automatically filtering out duplicates.
    /// Used by `add_many_with_meta()` to populate the breach database during import.
    pub fn record_potentially_vulnerable_passwords(&self, passwords: Vec<String>) -> Result<()> {
        let tx = self.unchecked_transaction()?;
        self.insert_potentially_vulnerable_passwords(passwords)?;
        tx.commit()?;
        Ok(())
    }

    fn insert_potentially_vulnerable_passwords(&self, passwords: Vec<String>) -> Result<()> {
        let encrypted_existing_potentially_vulnerable_passwords: Vec<String> = self
            .db
            .query_rows_and_then_cached("SELECT encryptedPassword FROM breachesL", [], |row| {
                row.get(0)
            })?;
        let existing_potentially_vulnerable_passwords: Result<Vec<String>> =
            encrypted_existing_potentially_vulnerable_passwords
                .iter()
                .map(|ciphertext| {
                    let decrypted_bytes = self
                        .encdec
                        .decrypt(ciphertext.as_bytes().into())
                        .map_err(|e| {
                            Error::DecryptionFailed(format!(
                                "Failed to decrypt password from breachesL: {}",
                                e
                            ))
                        })?;

                    let password = std::str::from_utf8(&decrypted_bytes).map_err(|e| {
                        Error::DecryptionFailed(format!(
                            "Decrypted password from breachesL is not valid UTF-8: {}",
                            e
                        ))
                    })?;

                    Ok(password.into())
                })
                .collect();

        let existing: std::collections::HashSet<String> =
            existing_potentially_vulnerable_passwords?
                .into_iter()
                .collect();
        let difference: Vec<_> = passwords
            .iter()
            .filter(|item| !existing.contains(item.as_str()))
            .collect();

        for password in difference {
            let encrypted_password_bytes = self
                .encdec
                .encrypt(password.as_bytes().into())
                .map_err(|e| Error::EncryptionFailed(format!("{e} (encrypting password)")))?;
            let encrypted_password =
                std::str::from_utf8(&encrypted_password_bytes).map_err(|e| {
                    Error::EncryptionFailed(format!("{e} (encrypting password: data not utf8)"))
                })?;

            self.execute_cached(
                "INSERT INTO breachesL (encryptedPassword) VALUES (:encrypted_password)",
                named_params! {
                    ":encrypted_password": encrypted_password,
                },
            )?;
        }

        Ok(())
    }

    /// Checks multiple logins for password reuse in a single batch operation.
    ///
    /// Returns the GUIDs of logins whose passwords match any password in the breach database.
    /// This is more efficient than calling `is_potentially_vulnerable_password()` repeatedly,
    /// as it decrypts the breach database only once.
    ///
    /// Performance: O(M + N) where M = breached passwords, N = logins to check
    /// - Single check: Use `is_potentially_vulnerable_password()` (simpler)
    /// - Multiple checks: Use this method (faster)
    pub fn are_potentially_vulnerable_passwords(&self, guids: &[&str]) -> Result<Vec<String>> {
        if guids.is_empty() {
            return Ok(Vec::new());
        }

        // Load and decrypt all breached passwords once
        let all_encrypted_passwords: Vec<String> = self.db.query_rows_and_then_cached(
            "SELECT encryptedPassword FROM breachesL",
            [],
            |row| row.get(0),
        )?;

        let mut breached_passwords = std::collections::HashSet::new();
        for ciphertext in &all_encrypted_passwords {
            let decrypted_bytes =
                self.encdec
                    .decrypt(ciphertext.as_bytes().into())
                    .map_err(|e| {
                        Error::DecryptionFailed(format!(
                            "Failed to decrypt password from breachesL: {}",
                            e
                        ))
                    })?;

            let decrypted_password = std::str::from_utf8(&decrypted_bytes).map_err(|e| {
                Error::DecryptionFailed(format!(
                    "Decrypted password from breachesL is not valid UTF-8: {}",
                    e
                ))
            })?;

            breached_passwords.insert(decrypted_password.to_string());
        }

        // Check each login against the breached passwords set
        let mut vulnerable_guids = Vec::new();
        for guid in guids {
            if let Some(login) = self.get_by_id(guid)? {
                let decrypted_login = login.decrypt(self.encdec.as_ref())?;
                if breached_passwords.contains(&decrypted_login.password) {
                    vulnerable_guids.push(guid.to_string());
                }
            }
        }

        Ok(vulnerable_guids)
    }

    pub fn is_potentially_vulnerable_password(&self, guid: &str) -> Result<bool> {
        // Delegate to batch method for code reuse
        let vulnerable = self.are_potentially_vulnerable_passwords(&[guid])?;
        Ok(!vulnerable.is_empty())
    }

    pub fn reset_all_breaches(&self) -> Result<()> {
        let tx = self.unchecked_transaction()?;
        self.execute_cached("DELETE FROM breachesL", [])?;
        tx.commit()?;
        Ok(())
    }

    /// Records that the user dismissed the breach alert for a login using the current time.
    ///
    /// For testing or when you need to specify a particular timestamp, use
    /// [`record_breach_alert_dismissal_time`](Self::record_breach_alert_dismissal_time) instead.
    pub fn record_breach_alert_dismissal(&self, id: &str) -> Result<()> {
        let timestamp = util::system_time_ms_i64(SystemTime::now());
        self.record_breach_alert_dismissal_time(id, timestamp)
    }

    /// Records that the user dismissed the breach alert for a login at a specific time.
    ///
    /// This is primarily useful for testing or when syncing dismissal times from other devices.
    /// For normal usage, prefer [`record_breach_alert_dismissal`](Self::record_breach_alert_dismissal)
    /// which automatically uses the current time.
    pub fn record_breach_alert_dismissal_time(&self, id: &str, timestamp: i64) -> Result<()> {
        let tx = self.unchecked_transaction()?;
        self.ensure_local_overlay_exists(id)?;
        self.mark_mirror_overridden(id)?;
        self.execute_cached(
            "UPDATE loginsL
             SET timeLastBreachAlertDismissed = :now_millis
             WHERE guid = :guid",
            named_params! {
                ":now_millis": timestamp,
                ":guid": id,
            },
        )?;
        tx.commit()?;
        Ok(())
    }

    // The single place we insert new rows or update existing local rows.
    // just the SQL - no validation or anything.
    fn insert_new_login(&self, login: &EncryptedLogin) -> Result<()> {
        let sql = format!(
            "INSERT OR REPLACE INTO loginsL (
                origin,
                httpRealm,
                formActionOrigin,
                usernameField,
                passwordField,
                timesUsed,
                secFields,
                guid,
                timeCreated,
                timeLastUsed,
                timePasswordChanged,
                timeLastBreachAlertDismissed,
                local_modified,
                is_deleted,
                sync_status
            ) VALUES (
                :origin,
                :http_realm,
                :form_action_origin,
                :username_field,
                :password_field,
                :times_used,
                :sec_fields,
                :guid,
                :time_created,
                :time_last_used,
                :time_password_changed,
                :time_last_breach_alert_dismissed,
                :local_modified,
                0, -- is_deleted
                {new} -- sync_status
            )",
            new = SyncStatus::New as u8
        );

        self.execute(
            &sql,
            named_params! {
                ":origin": login.fields.origin,
                ":http_realm": login.fields.http_realm,
                ":form_action_origin": login.fields.form_action_origin,
                ":username_field": login.fields.username_field,
                ":password_field": login.fields.password_field,
                ":time_created": login.meta.time_created,
                ":times_used": login.meta.times_used,
                ":time_last_used": login.meta.time_last_used,
                ":time_password_changed": login.meta.time_password_changed,
                ":local_modified": login.meta.time_created,
                ":time_last_breach_alert_dismissed": login.meta.time_last_breach_alert_dismissed,
                ":sec_fields": login.sec_fields,
                ":guid": login.guid(),
            },
        )?;
        Ok(())
    }

    fn update_existing_login(&self, login: &EncryptedLogin) -> Result<()> {
        // assumes the "local overlay" exists, so the guid must too.
        let now_ms = util::system_time_ms_i64(SystemTime::now());
        let sql = format!(
            "UPDATE loginsL
             SET local_modified                           = :now_millis,
                 timeLastUsed                             = :time_last_used,
                 timePasswordChanged                      = :time_password_changed,
                 httpRealm                                = :http_realm,
                 formActionOrigin                         = :form_action_origin,
                 usernameField                            = :username_field,
                 passwordField                            = :password_field,
                 timesUsed                                = :times_used,
                 secFields                                = :sec_fields,
                 origin                                   = :origin,
                 -- leave New records as they are, otherwise update them to `changed`
                 sync_status                              = max(sync_status, {changed})
             WHERE guid = :guid",
            changed = SyncStatus::Changed as u8
        );

        self.db.execute(
            &sql,
            named_params! {
                ":origin": login.fields.origin,
                ":http_realm": login.fields.http_realm,
                ":form_action_origin": login.fields.form_action_origin,
                ":username_field": login.fields.username_field,
                ":password_field": login.fields.password_field,
                ":time_last_used": login.meta.time_last_used,
                ":times_used": login.meta.times_used,
                ":time_password_changed": login.meta.time_password_changed,
                ":sec_fields": login.sec_fields,
                ":guid": &login.meta.id,
                ":now_millis": now_ms,
            },
        )?;
        Ok(())
    }

    /// Adds multiple logins within a single transaction and returns the successfully saved logins.
    pub fn add_many(&self, entries: Vec<LoginEntry>) -> Result<Vec<Result<EncryptedLogin>>> {
        let now_ms = util::system_time_ms_i64(SystemTime::now());

        let entries_with_meta = entries
            .into_iter()
            .map(|entry| {
                let guid = Guid::random();
                LoginEntryWithMeta {
                    entry,
                    meta: LoginMeta {
                        id: guid.to_string(),
                        time_created: now_ms,
                        time_password_changed: now_ms,
                        time_last_used: now_ms,
                        times_used: 1,
                        time_last_breach_alert_dismissed: None,
                    },
                }
            })
            .collect();

        self.add_many_with_meta(entries_with_meta)
    }

    /// Adds multiple logins **including metadata** within a single transaction and returns the successfully saved logins.
    /// Normally, you will use `add_many` instead, and AS Logins will take care of the metadata (setting timestamps, generating an ID) itself.
    /// However, in some cases, this method is necessary, for example when migrating data from another store that already contains the metadata.
    ///
    pub fn add_many_with_meta(
        &self,
        entries_with_meta: Vec<LoginEntryWithMeta>,
    ) -> Result<Vec<Result<EncryptedLogin>>> {
        let tx = self.unchecked_transaction()?;
        let mut results = vec![];
        for mut entry_with_meta in entries_with_meta {
            let guid = match Self::validate_or_fixup_guid(Guid::from_string(
                entry_with_meta.meta.id.clone(),
            )) {
                Ok(guid) => guid,
                Err(err) => {
                    results.push(Err(err));
                    continue;
                }
            };
            // Keep `meta.id` in sync with the (possibly regenerated) guid; it is used
            // as the stored/envelope id and when encrypting `sec_fields` below.
            entry_with_meta.meta.id = guid.to_string();
            match self.fixup_and_check_for_dupes(&guid, entry_with_meta.entry) {
                Ok(new_entry) => {
                    let sec_fields = SecureLoginFields {
                        username: new_entry.username,
                        password: new_entry.password,
                    }
                    .encrypt(self.encdec.as_ref(), &entry_with_meta.meta.id)?;
                    let encrypted_login = EncryptedLogin {
                        meta: entry_with_meta.meta,
                        fields: LoginFields {
                            origin: new_entry.origin,
                            form_action_origin: new_entry.form_action_origin,
                            http_realm: new_entry.http_realm,
                            username_field: new_entry.username_field,
                            password_field: new_entry.password_field,
                        },
                        sec_fields,
                    };
                    let result = self
                        .insert_new_login(&encrypted_login)
                        .map(|_| encrypted_login);
                    results.push(result);
                }

                Err(error) => results.push(Err(error)),
            }
        }

        tx.commit()?;

        Ok(results)
    }

    /// Validates a caller-supplied guid from the "with meta" import path against the
    /// sync server's rules (see `Guid::is_valid_for_sync_server`). A guid that is
    /// invalid for the sync server can never have existed on the server, so
    /// regenerating it loses no sync identity.
    ///
    /// With the `fixup_invalid_guids` feature (enabled on Desktop during migration),
    /// an invalid guid is silently replaced with a fresh random one. Without it, an
    /// invalid guid is rejected so the problem surfaces at write time instead of being
    /// persisted and later crashing the sync uploader (bug 2056116).
    fn validate_or_fixup_guid(guid: Guid) -> Result<Guid> {
        if guid.is_valid_for_sync_server() {
            return Ok(guid);
        }
        #[cfg(feature = "fixup_invalid_guids")]
        {
            warn!("regenerating a login guid that is invalid for the sync server");
            Ok(Guid::random())
        }
        #[cfg(not(feature = "fixup_invalid_guids"))]
        {
            Err(InvalidLogin::IllegalFieldValue {
                field_info: "guid is not valid for the sync server".into(),
            }
            .into())
        }
    }

    pub fn add(&self, entry: LoginEntry) -> Result<EncryptedLogin> {
        let guid = Guid::random();
        let now_ms = util::system_time_ms_i64(SystemTime::now());

        let entry_with_meta = LoginEntryWithMeta {
            entry,
            meta: LoginMeta {
                id: guid.to_string(),
                time_created: now_ms,
                time_password_changed: now_ms,
                time_last_used: now_ms,
                times_used: 1,
                time_last_breach_alert_dismissed: None,
            },
        };

        self.add_with_meta(entry_with_meta)
    }

    /// Adds a login **including metadata**.
    /// Normally, you will use `add` instead, and AS Logins will take care of the metadata (setting timestamps, generating an ID) itself.
    /// However, in some cases, this method is necessary, for example when migrating data from another store that already contains the metadata.
    pub fn add_with_meta(&self, entry_with_meta: LoginEntryWithMeta) -> Result<EncryptedLogin> {
        let mut results = self.add_many_with_meta(vec![entry_with_meta])?;
        results.pop().expect("there should be a single result")
    }

    pub fn update(&self, sguid: &str, entry: LoginEntry) -> Result<EncryptedLogin> {
        let guid = Guid::new(sguid);
        let now_ms = util::system_time_ms_i64(SystemTime::now());
        let tx = self.unchecked_transaction()?;

        let entry = entry.fixup()?;

        // Check if there's an existing login that's the dupe of this login.  That indicates that
        // something has gone wrong with our underlying logic.  However, if we do see a dupe login,
        // just log an error and continue.  This avoids a crash on android-components
        // (mozilla-mobile/android-components#11251).

        if self.check_for_dupes(&guid, &entry).is_err() {
            // Try to detect if sync is enabled by checking if there are any mirror logins
            let has_mirror_row: bool = self
                .db
                .conn_ext_query_one("SELECT EXISTS (SELECT 1 FROM loginsM)")?;
            let has_http_realm = entry.http_realm.is_some();
            let has_form_action_origin = entry.form_action_origin.is_some();
            report_error!(
                "logins-duplicate-in-update",
                "(mirror: {has_mirror_row}, realm: {has_http_realm}, form_origin: {has_form_action_origin})");
        }

        // Note: This fail with NoSuchRecord if the record doesn't exist.
        self.ensure_local_overlay_exists(&guid)?;
        self.mark_mirror_overridden(&guid)?;

        // We must read the existing record so we can correctly manage timePasswordChanged.
        let existing = match self.get_by_id(sguid)? {
            Some(e) => e.decrypt(self.encdec.as_ref())?,
            None => return Err(Error::NoSuchRecord(sguid.to_owned())),
        };
        let time_password_changed = if existing.password == entry.password {
            existing.time_password_changed
        } else {
            now_ms
        };

        // Make the final object here - every column will be updated.
        let sec_fields = SecureLoginFields {
            username: entry.username,
            password: entry.password,
        }
        .encrypt(self.encdec.as_ref(), &existing.id)?;
        let result = EncryptedLogin {
            meta: LoginMeta {
                id: existing.id,
                time_created: existing.time_created,
                time_password_changed,
                // An edit is not a use (see bug 2045032)
                time_last_used: existing.time_last_used,
                times_used: existing.times_used,
                time_last_breach_alert_dismissed: None,
            },
            fields: LoginFields {
                origin: entry.origin,
                form_action_origin: entry.form_action_origin,
                http_realm: entry.http_realm,
                username_field: entry.username_field,
                password_field: entry.password_field,
            },
            sec_fields,
        };

        self.update_existing_login(&result)?;
        tx.commit()?;
        Ok(result)
    }

    pub fn add_or_update(&self, entry: LoginEntry) -> Result<EncryptedLogin> {
        // Make sure to fixup the entry first, in case that changes the username
        let entry = entry.fixup()?;
        match self.find_login_to_update(entry.clone())? {
            Some(login) => self.update(&login.id, entry),
            None => self.add(entry),
        }
    }

    pub fn fixup_and_check_for_dupes(&self, guid: &Guid, entry: LoginEntry) -> Result<LoginEntry> {
        let entry = entry.fixup()?;
        self.check_for_dupes(guid, &entry)?;
        Ok(entry)
    }

    pub fn check_for_dupes(&self, guid: &Guid, entry: &LoginEntry) -> Result<()> {
        if self.dupe_exists(guid, entry)? {
            return Err(InvalidLogin::DuplicateLogin.into());
        }
        Ok(())
    }

    pub fn dupe_exists(&self, guid: &Guid, entry: &LoginEntry) -> Result<bool> {
        Ok(self.find_dupe(guid, entry)?.is_some())
    }

    pub fn find_dupe(&self, guid: &Guid, entry: &LoginEntry) -> Result<Option<Guid>> {
        for possible in self.get_by_entry_target(entry)? {
            if possible.guid() != *guid {
                let pos_sec_fields = possible.decrypt_fields(self.encdec.as_ref())?;
                if pos_sec_fields.username == entry.username {
                    return Ok(Some(possible.guid()));
                }
            }
        }
        Ok(None)
    }

    // Find saved logins that match the target for a `LoginEntry`
    //
    // This means that:
    //   - `origin` matches
    //   - Either `form_action_origin` or `http_realm` matches, depending on which one is non-null
    //
    // This is used for dupe-checking and `find_login_to_update()`
    //
    // Note that `entry` must be a normalized Login (via `fixup()`)
    fn get_by_entry_target(&self, entry: &LoginEntry) -> Result<Vec<EncryptedLogin>> {
        // Could be lazy_static-ed...
        lazy_static::lazy_static! {
            static ref GET_BY_FORM_ACTION_ORIGIN: String = format!(
                "SELECT {common_cols} FROM loginsL
                WHERE is_deleted = 0
                    AND origin = :origin
                    AND formActionOrigin = :form_action_origin

                UNION ALL

                SELECT {common_cols} FROM loginsM
                WHERE is_overridden = 0
                    AND origin = :origin
                    AND formActionOrigin = :form_action_origin
                ",
                common_cols = schema::COMMON_COLS
            );
            static ref GET_BY_HTTP_REALM: String = format!(
                "SELECT {common_cols} FROM loginsL
                WHERE is_deleted = 0
                    AND origin = :origin
                    AND httpRealm = :http_realm

                UNION ALL

                SELECT {common_cols} FROM loginsM
                WHERE is_overridden = 0
                    AND origin = :origin
                    AND httpRealm = :http_realm
                ",
                common_cols = schema::COMMON_COLS
            );
        }
        match (entry.form_action_origin.as_ref(), entry.http_realm.as_ref()) {
            (Some(form_action_origin), None) => {
                let params = named_params! {
                    ":origin": &entry.origin,
                    ":form_action_origin": form_action_origin,
                };
                self.db
                    .prepare_cached(&GET_BY_FORM_ACTION_ORIGIN)?
                    .query_and_then(params, EncryptedLogin::from_row)?
                    .collect()
            }
            (None, Some(http_realm)) => {
                let params = named_params! {
                    ":origin": &entry.origin,
                    ":http_realm": http_realm,
                };
                self.db
                    .prepare_cached(&GET_BY_HTTP_REALM)?
                    .query_and_then(params, EncryptedLogin::from_row)?
                    .collect()
            }
            (Some(_), Some(_)) => Err(InvalidLogin::BothTargets.into()),
            (None, None) => Err(InvalidLogin::NoTarget.into()),
        }
    }

    pub fn exists(&self, id: &str) -> Result<bool> {
        Ok(self.db.query_row(
            "SELECT EXISTS(
                 SELECT 1 FROM loginsL
                 WHERE guid = :guid AND is_deleted = 0
                 UNION ALL
                 SELECT 1 FROM loginsM
                 WHERE guid = :guid AND is_overridden IS NOT 1
             )",
            named_params! { ":guid": id },
            |row| row.get(0),
        )?)
    }

    /// Delete the record with the provided id. Returns true if the record
    /// existed already.
    pub fn delete(&self, id: &str) -> Result<bool> {
        let mut results = self.delete_many(vec![id])?;
        Ok(results.pop().expect("there should be a single result"))
    }

    // Delete all records. Return an array with the ids of the deleted logins
    pub fn delete_all(&self) -> Result<Vec<String>> {
        let ids: Vec<String> = self.db.query_rows_and_then_cached(
            "SELECT guid FROM loginsL WHERE is_deleted = 0
             UNION ALL
             SELECT guid FROM loginsM WHERE is_overridden = 0",
            [],
            |row| row.get(0),
        )?;
        self.delete_many(ids.iter().map(String::as_str).collect())?;
        Ok(ids)
    }

    // Delete all records, except the FxA login. Return an array with the ids of
    // the deleted logins
    pub fn delete_all_except_fxa(&self) -> Result<Vec<String>> {
        let ids: Vec<String> = self.db.query_rows_and_then_cached(
            "SELECT guid FROM loginsL WHERE is_deleted = 0 AND origin != :fxa_origin
             UNION ALL
             SELECT guid FROM loginsM WHERE is_overridden = 0 AND origin != :fxa_origin",
            named_params! { ":fxa_origin": FXA_CREDENTIALS_ORIGIN },
            |row| row.get(0),
        )?;
        self.delete_many(ids.iter().map(String::as_str).collect())?;
        Ok(ids)
    }

    /// Delete the records with the specified IDs. Returns a list of Boolean values
    /// indicating whether the respective records already existed.
    pub fn delete_many(&self, ids: Vec<&str>) -> Result<Vec<bool>> {
        let tx = self.unchecked_transaction_imm()?;
        let sql = format!(
            "
            UPDATE loginsL
            SET local_modified = :now_ms,
                sync_status = {status_changed},
                is_deleted = 1,
                secFields = '',
                origin = '',
                httpRealm = NULL,
                formActionOrigin = NULL
            WHERE guid = :guid AND is_deleted IS FALSE
            ",
            status_changed = SyncStatus::Changed as u8
        );
        let mut stmt = self.db.prepare_cached(&sql)?;

        let mut result = vec![];

        for id in ids {
            let now_ms = util::system_time_ms_i64(SystemTime::now());

            // For IDs that have, mark is_deleted and clear sensitive fields
            let update_result = stmt.execute(named_params! { ":now_ms": now_ms, ":guid": id })?;

            let exists = update_result == 1;

            // Mark the mirror as overridden
            self.execute(
                "UPDATE loginsM SET is_overridden = 1 WHERE guid = :guid",
                named_params! { ":guid": id },
            )?;

            // If we don't have a local record for this ID, but do have it in the mirror
            // insert a tombstone.
            self.execute(&format!("
                INSERT OR IGNORE INTO loginsL
                        (guid, local_modified, is_deleted, sync_status, origin, timeCreated, timePasswordChanged, secFields)
                SELECT   guid, :now_ms,        1,          {changed},   '',     timeCreated, :now_ms,             ''
                FROM loginsM
                WHERE guid = :guid",
                changed = SyncStatus::Changed as u8),
                named_params! { ":now_ms": now_ms, ":guid": id })?;

            result.push(exists);
        }

        tx.commit()?;

        Ok(result)
    }

    pub fn delete_undecryptable_records_for_remote_replacement(
        &self,
    ) -> Result<LoginsDeletionMetrics> {
        // Retrieve a list of guids for logins that cannot be decrypted
        let corrupted_logins = self
            .get_all()?
            .into_iter()
            .filter(|login| login.clone().decrypt(self.encdec.as_ref()).is_err())
            .collect::<Vec<_>>();
        let ids = corrupted_logins
            .iter()
            .map(|login| login.guid_str())
            .collect::<Vec<_>>();

        self.delete_local_records_for_remote_replacement(ids)
    }

    pub fn delete_local_records_for_remote_replacement(
        &self,
        ids: Vec<&str>,
    ) -> Result<LoginsDeletionMetrics> {
        let tx = self.unchecked_transaction_imm()?;
        let mut local_deleted = 0;
        let mut mirror_deleted = 0;

        sql_support::each_chunk(&ids, |chunk, _| -> Result<()> {
            let deleted = self.execute(
                &format!(
                    "DELETE FROM loginsL WHERE guid IN ({})",
                    sql_support::repeat_sql_values(chunk.len())
                ),
                rusqlite::params_from_iter(chunk),
            )?;
            local_deleted += deleted;
            Ok(())
        })?;

        sql_support::each_chunk(&ids, |chunk, _| -> Result<()> {
            let deleted = self.execute(
                &format!(
                    "DELETE FROM loginsM WHERE guid IN ({})",
                    sql_support::repeat_sql_values(chunk.len())
                ),
                rusqlite::params_from_iter(chunk),
            )?;
            mirror_deleted += deleted;
            Ok(())
        })?;

        tx.commit()?;
        Ok(LoginsDeletionMetrics {
            local_deleted: local_deleted as u64,
            mirror_deleted: mirror_deleted as u64,
        })
    }

    fn mark_mirror_overridden(&self, guid: &str) -> Result<()> {
        self.execute_cached(
            "UPDATE loginsM SET is_overridden = 1 WHERE guid = :guid",
            named_params! { ":guid": guid },
        )?;
        Ok(())
    }

    fn ensure_local_overlay_exists(&self, guid: &str) -> Result<()> {
        let already_have_local: bool = self.db.query_row(
            "SELECT EXISTS(SELECT 1 FROM loginsL WHERE guid = :guid)",
            named_params! { ":guid": guid },
            |row| row.get(0),
        )?;

        if already_have_local {
            return Ok(());
        }

        debug!("No overlay; cloning one for {:?}.", guid);
        let changed = self.clone_mirror_to_overlay(guid)?;
        if changed == 0 {
            report_error!(
                "logins-local-overlay-error",
                "Failed to create local overlay for GUID {guid:?}."
            );
            return Err(Error::NoSuchRecord(guid.to_owned()));
        }
        Ok(())
    }

    fn clone_mirror_to_overlay(&self, guid: &str) -> Result<usize> {
        Ok(self.execute_cached(&CLONE_SINGLE_MIRROR_SQL, &[(":guid", &guid as &dyn ToSql)])?)
    }

    /// Wipe all local data, returns the number of rows deleted
    pub fn wipe_local(&self) -> Result<usize> {
        info!("Executing wipe_local on password engine!");
        let tx = self.unchecked_transaction()?;
        let mut row_count = 0;
        row_count += self.execute("DELETE FROM loginsL", [])?;
        row_count += self.execute("DELETE FROM loginsM", [])?;
        row_count += self.execute("DELETE FROM loginsSyncMeta", [])?;
        row_count += self.execute("DELETE FROM breachesL", [])?;
        tx.commit()?;
        Ok(row_count)
    }

    /// Wipe all local data except the FxA login, returns the number of rows deleted
    pub fn wipe_local_except_fxa(&self) -> Result<usize> {
        info!("Executing wipe_local_except_fxa on password engine!");
        let tx = self.unchecked_transaction()?;
        let mut row_count = 0;
        row_count += self.execute(
            "DELETE FROM loginsL WHERE origin != :fxa_origin",
            named_params! { ":fxa_origin": FXA_CREDENTIALS_ORIGIN },
        )?;
        row_count += self.execute(
            "DELETE FROM loginsM WHERE origin != :fxa_origin",
            named_params! { ":fxa_origin": FXA_CREDENTIALS_ORIGIN },
        )?;
        row_count += self.execute("DELETE FROM loginsSyncMeta", [])?;
        row_count += self.execute("DELETE FROM breachesL", [])?;
        tx.commit()?;
        Ok(row_count)
    }

    pub fn shutdown(self) -> Result<()> {
        self.db.close().map_err(|(_, e)| Error::SqlError(e))
    }
}

lazy_static! {
    static ref GET_ALL_SQL: String = format!(
        "SELECT {common_cols} FROM loginsL WHERE is_deleted = 0
         UNION ALL
         SELECT {common_cols} FROM loginsM WHERE is_overridden = 0",
        common_cols = schema::COMMON_COLS,
    );
    static ref COUNT_ALL_SQL: String = format!(
        "SELECT COUNT(*) FROM (
          SELECT guid FROM loginsL WHERE is_deleted = 0
          UNION ALL
          SELECT guid FROM loginsM WHERE is_overridden = 0
        )"
    );
    static ref COUNT_BY_ORIGIN_SQL: String = format!(
        "SELECT COUNT(*) FROM (
          SELECT guid FROM loginsL WHERE is_deleted = 0 AND origin = :origin
          UNION ALL
          SELECT guid FROM loginsM WHERE is_overridden = 0 AND origin = :origin
        )"
    );
    static ref COUNT_BY_FORM_ACTION_ORIGIN_SQL: String = format!(
        "SELECT COUNT(*) FROM (
          SELECT guid FROM loginsL WHERE is_deleted = 0 AND formActionOrigin = :form_action_origin
          UNION ALL
          SELECT guid FROM loginsM WHERE is_overridden = 0 AND formActionOrigin = :form_action_origin
        )"
    );
    static ref GET_BY_GUID_SQL: String = format!(
        "SELECT {common_cols}
         FROM loginsL
         WHERE is_deleted = 0
           AND guid = :guid

         UNION ALL

         SELECT {common_cols}
         FROM loginsM
         WHERE is_overridden IS NOT 1
           AND guid = :guid
         ORDER BY origin ASC

         LIMIT 1",
        common_cols = schema::COMMON_COLS,
    );
    pub static ref CLONE_ENTIRE_MIRROR_SQL: String = format!(
        "INSERT OR IGNORE INTO loginsL ({common_cols}, local_modified, is_deleted, sync_status)
         SELECT {common_cols}, NULL AS local_modified, 0 AS is_deleted, 0 AS sync_status
         FROM loginsM",
        common_cols = schema::COMMON_COLS,
    );
    static ref CLONE_SINGLE_MIRROR_SQL: String =
        format!("{} WHERE guid = :guid", &*CLONE_ENTIRE_MIRROR_SQL,);
}

#[cfg(not(feature = "keydb"))]
#[cfg(test)]
pub mod test_utils {
    use super::*;
    use crate::encryption::test_utils::decrypt_struct;
    use crate::login::test_utils::enc_login;
    use crate::SecureLoginFields;
    use sync15::ServerTimestamp;

    // Insert a login into the local and/or mirror tables.
    //
    // local_login and mirror_login are specified as Some(password_string)
    pub fn insert_login(
        db: &LoginDb,
        guid: &str,
        local_login: Option<&str>,
        mirror_login: Option<&str>,
    ) {
        if let Some(password) = mirror_login {
            add_mirror(
                db,
                &enc_login(guid, password),
                &ServerTimestamp(util::system_time_ms_i64(std::time::SystemTime::now())),
                local_login.is_some(),
            )
            .unwrap();
        }
        if let Some(password) = local_login {
            db.insert_new_login(&enc_login(guid, password)).unwrap();
        }
    }

    pub fn insert_encrypted_login(
        db: &LoginDb,
        local: &EncryptedLogin,
        mirror: &EncryptedLogin,
        server_modified: &ServerTimestamp,
    ) {
        db.insert_new_login(local).unwrap();
        add_mirror(db, mirror, server_modified, true).unwrap();
    }

    pub fn add_mirror(
        db: &LoginDb,
        login: &EncryptedLogin,
        server_modified: &ServerTimestamp,
        is_overridden: bool,
    ) -> Result<()> {
        let sql = "
            INSERT OR IGNORE INTO loginsM (
                is_overridden,
                server_modified,

                httpRealm,
                formActionOrigin,
                usernameField,
                passwordField,
                secFields,
                origin,

                timesUsed,
                timeLastUsed,
                timePasswordChanged,
                timeCreated,

                timeLastBreachAlertDismissed,

                guid
            ) VALUES (
                :is_overridden,
                :server_modified,

                :http_realm,
                :form_action_origin,
                :username_field,
                :password_field,
                :sec_fields,
                :origin,

                :times_used,
                :time_last_used,
                :time_password_changed,
                :time_created,

                :time_last_breach_alert_dismissed,

                :guid
            )";
        let mut stmt = db.prepare_cached(sql)?;

        stmt.execute(named_params! {
            ":is_overridden": is_overridden,
            ":server_modified": server_modified.as_millis(),
            ":http_realm": login.fields.http_realm,
            ":form_action_origin": login.fields.form_action_origin,
            ":username_field": login.fields.username_field,
            ":password_field": login.fields.password_field,
            ":origin": login.fields.origin,
            ":sec_fields": login.sec_fields,
            ":times_used": login.meta.times_used,
            ":time_last_used": login.meta.time_last_used,
            ":time_password_changed": login.meta.time_password_changed,
            ":time_created": login.meta.time_created,
            ":time_last_breach_alert_dismissed": login.meta.time_last_breach_alert_dismissed,
            ":guid": login.guid_str(),
        })?;
        Ok(())
    }

    pub fn get_local_guids(db: &LoginDb) -> Vec<String> {
        get_guids(db, "SELECT guid FROM loginsL")
    }

    pub fn get_mirror_guids(db: &LoginDb) -> Vec<String> {
        get_guids(db, "SELECT guid FROM loginsM")
    }

    fn get_guids(db: &LoginDb, sql: &str) -> Vec<String> {
        let mut stmt = db.prepare_cached(sql).unwrap();
        let mut res: Vec<String> = stmt
            .query_map([], |r| r.get(0))
            .unwrap()
            .map(|r| r.unwrap())
            .collect();
        res.sort();
        res
    }

    pub fn get_server_modified(db: &LoginDb, guid: &str) -> i64 {
        db.conn_ext_query_one(&format!(
            "SELECT server_modified FROM loginsM WHERE guid='{}'",
            guid
        ))
        .unwrap()
    }

    pub fn check_local_login(db: &LoginDb, guid: &str, password: &str, local_modified_gte: i64) {
        let row: (String, i64, bool) = db
            .query_row(
                "SELECT secFields, local_modified, is_deleted FROM loginsL WHERE guid=?",
                [guid],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .unwrap();
        let enc: SecureLoginFields = decrypt_struct(row.0);
        assert_eq!(enc.password, password);
        assert!(row.1 >= local_modified_gte);
        assert!(!row.2);
    }

    pub fn check_mirror_login(
        db: &LoginDb,
        guid: &str,
        password: &str,
        server_modified: i64,
        is_overridden: bool,
    ) {
        let row: (String, i64, bool) = db
            .query_row(
                "SELECT secFields, server_modified, is_overridden FROM loginsM WHERE guid=?",
                [guid],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .unwrap();
        let enc: SecureLoginFields = decrypt_struct(row.0);
        assert_eq!(enc.password, password);
        assert_eq!(row.1, server_modified);
        assert_eq!(row.2, is_overridden);
    }
}

#[cfg(not(feature = "keydb"))]
#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::test_utils::{get_local_guids, get_mirror_guids};
    use crate::encryption::test_utils::TEST_ENCDEC;
    use crate::sync::merge::LocalLogin;
    use nss_as::ensure_initialized;
    use std::{thread, time};

    #[test]
    fn test_username_dupe_semantics() {
        ensure_initialized();
        let mut login = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let db = LoginDb::open_in_memory();
        db.add(login.clone())
            .expect("should be able to add first login");

        // We will reject new logins with the same username value...
        let exp_err = "Invalid login: Login already exists";
        assert_eq!(db.add(login.clone()).unwrap_err().to_string(), exp_err);

        // Add one with an empty username - not a dupe.
        login.username = "".to_string();
        db.add(login.clone()).expect("empty login isn't a dupe");

        assert_eq!(db.add(login).unwrap_err().to_string(), exp_err);

        // one with a username, 1 without.
        assert_eq!(db.get_all().unwrap().len(), 2);
    }

    #[test]
    fn test_add_many() {
        ensure_initialized();

        let login_a = LoginEntry {
            origin: "https://a.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let login_b = LoginEntry {
            origin: "https://b.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let db = LoginDb::open_in_memory();
        let added = db
            .add_many(vec![login_a.clone(), login_b.clone()])
            .expect("should be able to add logins");

        let [added_a, added_b] = added.as_slice() else {
            panic!("there should really be 2")
        };

        let fetched_a = db
            .get_by_id(&added_a.as_ref().unwrap().meta.id)
            .expect("should work")
            .expect("should get a record");

        assert_eq!(fetched_a.fields.origin, login_a.origin);

        let fetched_b = db
            .get_by_id(&added_b.as_ref().unwrap().meta.id)
            .expect("should work")
            .expect("should get a record");

        assert_eq!(fetched_b.fields.origin, login_b.origin);

        assert_eq!(db.count_all().unwrap(), 2);
    }

    #[test]
    fn test_count_by_origin() {
        ensure_initialized();

        let origin_a = "https://a.example.com";
        let login_a = LoginEntry {
            origin: origin_a.into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let login_b = LoginEntry {
            origin: "https://b.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let origin_umlaut = "https://bücher.example.com";
        let login_umlaut = LoginEntry {
            origin: origin_umlaut.into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let db = LoginDb::open_in_memory();
        db.add_many(vec![login_a.clone(), login_b.clone(), login_umlaut.clone()])
            .expect("should be able to add logins");

        assert_eq!(db.count_by_origin(origin_a).unwrap(), 1);
        assert_eq!(db.count_by_origin(origin_umlaut).unwrap(), 1);
    }

    #[test]
    fn test_count_by_form_action_origin() {
        ensure_initialized();

        let origin_a = "https://a.example.com";
        let login_a = LoginEntry {
            origin: origin_a.into(),
            form_action_origin: Some(origin_a.into()),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let login_b = LoginEntry {
            origin: "https://b.example.com".into(),
            form_action_origin: Some("https://b.example.com".into()),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let origin_umlaut = "https://bücher.example.com";
        let login_umlaut = LoginEntry {
            origin: origin_umlaut.into(),
            form_action_origin: Some(origin_umlaut.into()),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let db = LoginDb::open_in_memory();
        db.add_many(vec![login_a.clone(), login_b.clone(), login_umlaut.clone()])
            .expect("should be able to add logins");

        assert_eq!(db.count_by_form_action_origin(origin_a).unwrap(), 1);
        assert_eq!(db.count_by_form_action_origin(origin_umlaut).unwrap(), 1);
    }

    #[test]
    #[cfg(feature = "ignore_form_action_origin_validation_errors")]
    fn test_count_by_invalid_form_action_origin() {
        ensure_initialized();

        let login = LoginEntry {
            origin: "https://example.com".into(),
            form_action_origin: Some("email".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let db = LoginDb::open_in_memory();
        db.add(login)
            .expect("should be able to add login with invalid form_action_origin");
        assert_eq!(db.count_by_form_action_origin("email").unwrap(), 1);
    }

    #[test]
    fn test_add_many_with_failed_constraint() {
        ensure_initialized();

        let login_a = LoginEntry {
            origin: "https://example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let login_b = LoginEntry {
            // same origin will result in duplicate error
            origin: "https://example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };

        let db = LoginDb::open_in_memory();
        let added = db
            .add_many(vec![login_a.clone(), login_b.clone()])
            .expect("should be able to add logins");

        let [added_a, added_b] = added.as_slice() else {
            panic!("there should really be 2")
        };

        // first entry has been saved successfully
        let fetched_a = db
            .get_by_id(&added_a.as_ref().unwrap().meta.id)
            .expect("should work")
            .expect("should get a record");

        assert_eq!(fetched_a.fields.origin, login_a.origin);

        // second entry failed
        assert!(!added_b.is_ok());
    }

    #[test]
    fn test_add_with_meta() {
        ensure_initialized();

        let guid = Guid::random();
        let now_ms = util::system_time_ms_i64(SystemTime::now());
        let login = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };
        let meta = LoginMeta {
            id: guid.to_string(),
            time_created: now_ms,
            time_password_changed: now_ms + 100,
            time_last_used: now_ms + 10,
            times_used: 42,
            time_last_breach_alert_dismissed: None,
        };

        let db = LoginDb::open_in_memory();
        let entry_with_meta = LoginEntryWithMeta {
            entry: login.clone(),
            meta: meta.clone(),
        };

        db.add_with_meta(entry_with_meta)
            .expect("should be able to add login with record");

        let fetched = db
            .get_by_id(&guid)
            .expect("should work")
            .expect("should get a record");

        assert_eq!(fetched.meta, meta);
    }

    #[test]
    fn test_add_with_meta_invalid_guid() {
        ensure_initialized();

        let now_ms = util::system_time_ms_i64(SystemTime::now());
        // A guid containing a comma is invalid for the sync server.
        let meta = LoginMeta {
            id: "invalid,guid".to_string(),
            time_created: now_ms,
            time_password_changed: now_ms,
            time_last_used: now_ms,
            times_used: 1,
            time_last_breach_alert_dismissed: None,
        };
        let db = LoginDb::open_in_memory();
        let result = db.add_with_meta(LoginEntryWithMeta {
            entry: LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test".into(),
                password: "sekret".into(),
                ..LoginEntry::default()
            },
            meta,
        });

        // Without the fixup feature the invalid guid is rejected; with it, the guid
        // is regenerated to one that is valid for the sync server.
        #[cfg(not(feature = "fixup_invalid_guids"))]
        assert!(result.is_err());

        #[cfg(feature = "fixup_invalid_guids")]
        {
            let login = result.expect("invalid guid should be repaired");
            assert!(Guid::new(&login.meta.id).is_valid_for_sync_server());
        }
    }

    #[test]
    fn test_add_with_meta_duplicate_id() {
        ensure_initialized();

        let guid = Guid::random();
        let now_ms = util::system_time_ms_i64(SystemTime::now());
        let meta = LoginMeta {
            id: guid.to_string(),
            time_created: now_ms,
            time_password_changed: now_ms,
            time_last_used: now_ms,
            times_used: 1,
            time_last_breach_alert_dismissed: None,
        };

        let db = LoginDb::open_in_memory();
        db.add_with_meta(LoginEntryWithMeta {
            entry: LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test".into(),
                password: "sekret".into(),
                ..LoginEntry::default()
            },
            meta: meta.clone(),
        })
        .expect("should be able to add login with record");

        // Adding a second login that reuses the same id (different origin so the
        // dupe-check passes) succeeds and replaces the existing record.
        db.add_with_meta(LoginEntryWithMeta {
            entry: LoginEntry {
                origin: "https://www.other.com".into(),
                http_realm: Some("https://www.other.com".into()),
                username: "test".into(),
                password: "sekret".into(),
                ..LoginEntry::default()
            },
            meta,
        })
        .expect("should be able to re-add a login with the same id");

        let fetched = db
            .get_by_id(&guid)
            .expect("should work")
            .expect("should get a record");
        assert_eq!(fetched.fields.origin, "https://www.other.com");
    }

    #[test]
    fn test_record_potentially_vulnerable_passwords() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();

        // Initially breachesL should be empty
        let count: i64 = db
            .db
            .query_row("SELECT COUNT(*) FROM breachesL", [], |row| row.get(0))
            .unwrap();
        assert_eq!(count, 0);

        // Record some passwords
        db.record_potentially_vulnerable_passwords(vec![
            "password1".into(),
            "password2".into(),
            "password3".into(),
        ])
        .unwrap();

        // Verify they were inserted
        let count: i64 = db
            .db
            .query_row("SELECT COUNT(*) FROM breachesL", [], |row| row.get(0))
            .unwrap();
        assert_eq!(count, 3);

        // Try to insert duplicates - should be filtered out
        db.record_potentially_vulnerable_passwords(vec!["password1".into(), "password4".into()])
            .unwrap();

        // Only password4 should have been added
        let count: i64 = db
            .db
            .query_row("SELECT COUNT(*) FROM breachesL", [], |row| row.get(0))
            .unwrap();
        assert_eq!(count, 4);

        // Try to insert only duplicates - should be a no-op
        db.record_potentially_vulnerable_passwords(vec!["password1".into(), "password2".into()])
            .unwrap();

        let count: i64 = db
            .db
            .query_row("SELECT COUNT(*) FROM breachesL", [], |row| row.get(0))
            .unwrap();
        assert_eq!(count, 4);
    }

    #[test]
    fn test_add_with_meta_deleted() {
        ensure_initialized();

        let guid = Guid::random();
        let now_ms = util::system_time_ms_i64(SystemTime::now());
        let login = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "sekret".into(),
            ..LoginEntry::default()
        };
        let meta = LoginMeta {
            id: guid.to_string(),
            time_created: now_ms,
            time_password_changed: now_ms + 100,
            time_last_used: now_ms + 10,
            times_used: 42,
            time_last_breach_alert_dismissed: None,
        };

        let db = LoginDb::open_in_memory();
        let entry_with_meta = LoginEntryWithMeta {
            entry: login.clone(),
            meta: meta.clone(),
        };

        db.add_with_meta(entry_with_meta)
            .expect("should be able to add login with record");

        db.delete(&guid).expect("should be able to delete login");

        let entry_with_meta2 = LoginEntryWithMeta {
            entry: login.clone(),
            meta: meta.clone(),
        };

        db.add_with_meta(entry_with_meta2)
            .expect("should be able to re-add login with record");

        let fetched = db
            .get_by_id(&guid)
            .expect("should work")
            .expect("should get a record");

        assert_eq!(fetched.meta, meta);
    }

    #[test]
    fn test_unicode_submit() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let added = db
            .add(LoginEntry {
                form_action_origin: Some("http://😍.com".into()),
                origin: "http://😍.com".into(),
                http_realm: None,
                username_field: "😍".into(),
                password_field: "😍".into(),
                username: "😍".into(),
                password: "😍".into(),
            })
            .unwrap();
        let fetched = db
            .get_by_id(&added.meta.id)
            .expect("should work")
            .expect("should get a record");
        assert_eq!(added, fetched);
        assert_eq!(fetched.fields.origin, "http://xn--r28h.com");
        assert_eq!(
            fetched.fields.form_action_origin,
            Some("http://xn--r28h.com".to_string())
        );
        assert_eq!(fetched.fields.username_field, "😍");
        assert_eq!(fetched.fields.password_field, "😍");
        let sec_fields = fetched.decrypt_fields(db.encdec.as_ref()).unwrap();
        assert_eq!(sec_fields.username, "😍");
        assert_eq!(sec_fields.password, "😍");
    }

    #[test]
    fn test_unicode_realm() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let added = db
            .add(LoginEntry {
                form_action_origin: None,
                origin: "http://😍.com".into(),
                http_realm: Some("😍😍".into()),
                username: "😍".into(),
                password: "😍".into(),
                ..Default::default()
            })
            .unwrap();
        let fetched = db
            .get_by_id(&added.meta.id)
            .expect("should work")
            .expect("should get a record");
        assert_eq!(added, fetched);
        assert_eq!(fetched.fields.origin, "http://xn--r28h.com");
        assert_eq!(fetched.fields.http_realm.unwrap(), "😍😍");
    }

    fn check_matches(db: &LoginDb, query: &str, expected: &[&str]) {
        let mut results = db
            .get_by_base_domain(query)
            .unwrap()
            .into_iter()
            .map(|l| l.fields.origin)
            .collect::<Vec<String>>();
        results.sort_unstable();
        let mut sorted = expected.to_owned();
        sorted.sort_unstable();
        assert_eq!(sorted, results);
    }

    fn check_good_bad(
        good: Vec<&str>,
        bad: Vec<&str>,
        good_queries: Vec<&str>,
        zero_queries: Vec<&str>,
    ) {
        let db = LoginDb::open_in_memory();
        for h in good.iter().chain(bad.iter()) {
            db.add(LoginEntry {
                origin: (*h).into(),
                http_realm: Some((*h).into()),
                password: "test".into(),
                ..Default::default()
            })
            .unwrap();
        }
        for query in good_queries {
            check_matches(&db, query, &good);
        }
        for query in zero_queries {
            check_matches(&db, query, &[]);
        }
    }

    #[test]
    fn test_get_by_base_domain_invalid() {
        ensure_initialized();
        check_good_bad(
            vec!["https://example.com"],
            vec![],
            vec![],
            vec!["invalid query"],
        );
    }

    #[test]
    fn test_get_by_base_domain() {
        ensure_initialized();
        check_good_bad(
            vec![
                "https://example.com",
                "https://www.example.com",
                "http://www.example.com",
                "http://www.example.com:8080",
                "http://sub.example.com:8080",
                "https://sub.example.com:8080",
                "https://sub.sub.example.com",
                "ftp://sub.example.com",
            ],
            vec![
                "https://badexample.com",
                "https://example.co",
                "https://example.com.au",
            ],
            vec!["example.com"],
            vec!["foo.com"],
        );
    }

    #[test]
    fn test_get_by_base_domain_punicode() {
        ensure_initialized();
        // punycode! This is likely to need adjusting once we normalize
        // on insert.
        check_good_bad(
            vec![
                "http://xn--r28h.com", // punycoded version of "http://😍.com"
            ],
            vec!["http://💖.com"],
            vec!["😍.com", "xn--r28h.com"],
            vec![],
        );
    }

    #[test]
    fn test_get_by_base_domain_ipv4() {
        ensure_initialized();
        check_good_bad(
            vec!["http://127.0.0.1", "https://127.0.0.1:8000"],
            vec!["https://127.0.0.0", "https://example.com"],
            vec!["127.0.0.1"],
            vec!["127.0.0.2"],
        );
    }

    #[test]
    fn test_get_by_base_domain_ipv6() {
        ensure_initialized();
        check_good_bad(
            vec!["http://[::1]", "https://[::1]:8000"],
            vec!["https://[0:0:0:0:0:0:1:1]", "https://example.com"],
            vec!["[::1]", "[0:0:0:0:0:0:0:1]"],
            vec!["[0:0:0:0:0:0:1:2]"],
        );
    }

    #[test]
    fn test_add() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let to_add = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test_user".into(),
            password: "test_password".into(),
            ..Default::default()
        };
        let login = db.add(to_add).unwrap();
        let login2 = db.get_by_id(&login.meta.id).unwrap().unwrap();

        assert_eq!(login.fields.origin, login2.fields.origin);
        assert_eq!(login.fields.http_realm, login2.fields.http_realm);
        assert_eq!(login.sec_fields, login2.sec_fields);
    }

    #[test]
    fn test_update() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "user1".into(),
                password: "password1".into(),
                ..Default::default()
            })
            .unwrap();
        db.update(
            &login.meta.id,
            LoginEntry {
                origin: "https://www.example2.com".into(),
                http_realm: Some("https://www.example2.com".into()),
                username: "user2".into(),
                password: "password2".into(),
                ..Default::default() // TODO: check and fix if needed
            },
        )
        .unwrap();

        let login2 = db.get_by_id(&login.meta.id).unwrap().unwrap();

        assert_eq!(login2.fields.origin, "https://www.example2.com");
        assert_eq!(
            login2.fields.http_realm,
            Some("https://www.example2.com".into())
        );
        let sec_fields = login2.decrypt_fields(db.encdec.as_ref()).unwrap();
        assert_eq!(sec_fields.username, "user2");
        assert_eq!(sec_fields.password, "password2");
    }

    #[test]
    fn test_touch() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "user1".into(),
                password: "password1".into(),
                ..Default::default()
            })
            .unwrap();
        // Simulate touch happening at another "time"
        thread::sleep(time::Duration::from_millis(50));
        db.touch(&login.meta.id).unwrap();
        let login2 = db.get_by_id(&login.meta.id).unwrap().unwrap();
        assert!(login2.meta.time_last_used > login.meta.time_last_used);
        assert_eq!(login2.meta.times_used, login.meta.times_used + 1);
    }

    #[test]
    fn test_update_does_not_count_as_use() {
        // A plain update is not a password use.
        // It must not bump `times_used` or `time_last_used`. Only `touch()` is
        // allowed to do that.
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "user1".into(),
                password: "password1".into(),
                ..Default::default()
            })
            .unwrap();
        // Make sure the "now" an update would use differs from the add time.
        thread::sleep(time::Duration::from_millis(50));
        db.update(
            &login.meta.id,
            LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "user1".into(),
                password: "password2".into(),
                ..Default::default()
            },
        )
        .unwrap();
        let updated = db.get_by_id(&login.meta.id).unwrap().unwrap();
        // An edit is not a use: times_used must stay unchanged.
        assert_eq!(updated.meta.times_used, login.meta.times_used);
        // An edit is not a use: time_last_used must stay unchanged.
        assert_eq!(updated.meta.time_last_used, login.meta.time_last_used);
    }

    #[test]
    fn test_breach_alert_dismissal() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "user1".into(),
                password: "password1".into(),
                ..Default::default()
            })
            .unwrap();
        // initial state
        assert!(login.meta.time_last_breach_alert_dismissed.is_none());

        // dismiss
        db.record_breach_alert_dismissal(&login.meta.id).unwrap();
        let login1 = db.get_by_id(&login.meta.id).unwrap().unwrap();
        assert!(login1.meta.time_last_breach_alert_dismissed.is_some());
    }

    #[test]
    fn test_breach_alert_dismissal_with_specific_timestamp() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "user1".into(),
                password: "password1".into(),
                ..Default::default()
            })
            .unwrap();

        let dismiss_time = login.meta.time_password_changed + 1000;
        db.record_breach_alert_dismissal_time(&login.meta.id, dismiss_time)
            .unwrap();

        let retrieved = db
            .get_by_id(&login.meta.id)
            .unwrap()
            .unwrap()
            .decrypt(db.encdec.as_ref())
            .unwrap();
        assert_eq!(
            retrieved.time_last_breach_alert_dismissed,
            Some(dismiss_time)
        );
    }

    #[test]
    fn test_delete() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        assert!(db.delete(login.guid_str()).unwrap());

        let local_login = db
            .query_row(
                "SELECT * FROM loginsL WHERE guid = :guid",
                named_params! { ":guid": login.guid_str() },
                |row| Ok(LocalLogin::test_raw_from_row(row).unwrap()),
            )
            .unwrap();
        assert_eq!(local_login.fields.http_realm, None);
        assert_eq!(local_login.fields.form_action_origin, None);

        assert!(!db.exists(login.guid_str()).unwrap());
    }

    #[test]
    fn test_delete_many() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();

        let login_a = db
            .add(LoginEntry {
                origin: "https://a.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        let login_b = db
            .add(LoginEntry {
                origin: "https://b.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        let result = db
            .delete_many(vec![login_a.guid_str(), login_b.guid_str()])
            .unwrap();
        assert!(result[0]);
        assert!(result[1]);
        assert!(!db.exists(login_a.guid_str()).unwrap());
        assert!(!db.exists(login_b.guid_str()).unwrap());
    }

    #[test]
    fn test_subsequent_delete_many() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();

        let login = db
            .add(LoginEntry {
                origin: "https://a.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        let result = db.delete_many(vec![login.guid_str()]).unwrap();
        assert!(result[0]);
        assert!(!db.exists(login.guid_str()).unwrap());

        let result = db.delete_many(vec![login.guid_str()]).unwrap();
        assert!(!result[0]);
    }

    #[test]
    fn test_delete_many_with_non_existent_id() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();

        let result = db.delete_many(vec![&Guid::random()]).unwrap();
        assert!(!result[0]);
    }

    #[test]
    fn test_delete_all() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login_a = db
            .add(LoginEntry {
                origin: "https://a.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();
        let login_b = db
            .add(LoginEntry {
                origin: "https://b.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        let mut deleted = db.delete_all().unwrap();
        deleted.sort();
        let mut expected = vec![login_a.meta.id.clone(), login_b.meta.id.clone()];
        expected.sort();
        assert_eq!(deleted, expected);
        assert!(!db.exists(login_a.guid_str()).unwrap());
        assert!(!db.exists(login_b.guid_str()).unwrap());

        // On an empty database it's a no-op returning no ids.
        assert_eq!(db.delete_all().unwrap(), Vec::<String>::new());
    }

    #[test]
    fn test_delete_all_except_fxa() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://a.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();
        let fxa_login = db
            .add(LoginEntry {
                origin: FXA_CREDENTIALS_ORIGIN.into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        let deleted = db.delete_all_except_fxa().unwrap();
        assert_eq!(deleted, vec![login.meta.id.clone()]);

        // Only the FxA login remains.
        assert!(!db.exists(login.guid_str()).unwrap());
        assert!(db.exists(fxa_login.guid_str()).unwrap());
    }

    #[test]
    fn test_wipe_local_except_fxa() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://a.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();
        let fxa_login = db
            .add(LoginEntry {
                origin: FXA_CREDENTIALS_ORIGIN.into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        db.wipe_local_except_fxa().unwrap();

        // Only the FxA login remains.
        assert!(!db.exists(login.guid_str()).unwrap());
        assert!(db.exists(fxa_login.guid_str()).unwrap());
    }

    #[test]
    fn test_delete_local_for_remote_replacement() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        let login = db
            .add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("https://www.example.com".into()),
                username: "test_user".into(),
                password: "test_password".into(),
                ..Default::default()
            })
            .unwrap();

        let result = db
            .delete_local_records_for_remote_replacement(vec![login.guid_str()])
            .unwrap();

        let local_guids = get_local_guids(&db);
        assert_eq!(local_guids.len(), 0);

        let mirror_guids = get_mirror_guids(&db);
        assert_eq!(mirror_guids.len(), 0);

        assert_eq!(result.local_deleted, 1);
    }

    mod test_find_login_to_update {
        use super::*;

        fn make_entry(username: &str, password: &str) -> LoginEntry {
            LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("the website".into()),
                username: username.into(),
                password: password.into(),
                ..Default::default()
            }
        }

        fn make_saved_login(db: &LoginDb, username: &str, password: &str) -> Login {
            db.add(make_entry(username, password))
                .unwrap()
                .decrypt(db.encdec.as_ref())
                .unwrap()
        }

        #[test]
        fn test_match() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();
            let login = make_saved_login(&db, "user", "pass");
            assert_eq!(
                Some(login),
                db.find_login_to_update(make_entry("user", "pass")).unwrap(),
            );
        }

        #[test]
        fn test_non_matches() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();
            // Non-match because the username is different
            make_saved_login(&db, "other-user", "pass");
            // Non-match because the http_realm is different
            db.add(LoginEntry {
                origin: "https://www.example.com".into(),
                http_realm: Some("the other website".into()),
                username: "user".into(),
                password: "pass".into(),
                ..Default::default()
            })
            .unwrap();
            // Non-match because it uses form_action_origin instead of http_realm
            db.add(LoginEntry {
                origin: "https://www.example.com".into(),
                form_action_origin: Some("https://www.example.com/".into()),
                username: "user".into(),
                password: "pass".into(),
                ..Default::default()
            })
            .unwrap();
            assert_eq!(
                None,
                db.find_login_to_update(make_entry("user", "pass")).unwrap(),
            );
        }

        #[test]
        fn test_match_blank_password() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();
            let login = make_saved_login(&db, "", "pass");
            assert_eq!(
                Some(login),
                db.find_login_to_update(make_entry("user", "pass")).unwrap(),
            );
        }

        #[test]
        fn test_username_match_takes_precedence_over_blank_username() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();
            make_saved_login(&db, "", "pass");
            let username_match = make_saved_login(&db, "user", "pass");
            assert_eq!(
                Some(username_match),
                db.find_login_to_update(make_entry("user", "pass")).unwrap(),
            );
        }

        #[test]
        fn test_invalid_login() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();
            assert!(db
                .find_login_to_update(LoginEntry {
                    http_realm: None,
                    form_action_origin: None,
                    ..LoginEntry::default()
                })
                .is_err());
        }

        #[test]
        fn test_update_with_duplicate_login() {
            ensure_initialized();
            // If we have duplicate logins in the database, it should be possible to update them
            // without triggering a DuplicateLogin error
            let db = LoginDb::open_in_memory();
            let login = make_saved_login(&db, "user", "pass");
            let mut dupe = login.clone().encrypt(&*TEST_ENCDEC).unwrap();
            dupe.meta.id = "different-guid".to_string();
            db.insert_new_login(&dupe).unwrap();

            let mut entry = login.entry();
            entry.password = "pass2".to_string();
            db.update(&login.id, entry).unwrap();

            let mut entry = login.entry();
            entry.password = "pass3".to_string();
            db.add_or_update(entry).unwrap();
        }

        #[test]
        fn test_password_reuse_detection() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();

            // Create two logins with the same password
            let login1 = db
                .add(LoginEntry {
                    origin: "https://site1.com".into(),
                    http_realm: Some("realm".into()),
                    username: "user1".into(),
                    password: "shared_password".into(),
                    ..Default::default()
                })
                .unwrap();

            let login2 = db
                .add(LoginEntry {
                    origin: "https://site2.com".into(),
                    http_realm: Some("realm".into()),
                    username: "user2".into(),
                    password: "shared_password".into(),
                    ..Default::default()
                })
                .unwrap();

            // Initially, neither login is vulnerable
            assert!(!db
                .is_potentially_vulnerable_password(&login1.meta.id)
                .unwrap());
            assert!(!db
                .is_potentially_vulnerable_password(&login2.meta.id)
                .unwrap());
            // And checking both logins should return empty (none are vulnerable yet)
            let vulnerable = db
                .are_potentially_vulnerable_passwords(&[&login1.meta.id, &login2.meta.id])
                .unwrap();
            assert_eq!(vulnerable.len(), 0);

            // Record "shared_password" as a vulnerable password
            db.record_potentially_vulnerable_passwords(vec!["shared_password".into()])
                .unwrap();

            // login2 should be recognized as vulnerable (same password as breached login1)
            assert!(db
                .is_potentially_vulnerable_password(&login2.meta.id)
                .unwrap());
            // Batch check: both logins should be vulnerable (they share the same password)
            let vulnerable = db
                .are_potentially_vulnerable_passwords(&[&login1.meta.id, &login2.meta.id])
                .unwrap();
            assert_eq!(vulnerable.len(), 2);
            assert!(vulnerable.contains(&login1.meta.id));
            assert!(vulnerable.contains(&login2.meta.id));

            // Change password of login2 → should no longer be vulnerable
            db.update(
                &login2.meta.id,
                LoginEntry {
                    origin: "https://site2.com".into(),
                    http_realm: Some("realm".into()),
                    username: "user2".into(),
                    password: "different_password".into(),
                    ..Default::default()
                },
            )
            .unwrap();

            assert!(!db
                .is_potentially_vulnerable_password(&login2.meta.id)
                .unwrap());
        }

        #[test]
        fn test_reset_all_breaches_clears_breach_table() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();

            let login = db
                .add(LoginEntry {
                    origin: "https://example.com".into(),
                    http_realm: Some("realm".into()),
                    username: "user".into(),
                    password: "password123".into(),
                    ..Default::default()
                })
                .unwrap();

            db.record_potentially_vulnerable_passwords(vec!["password123".into()])
                .unwrap();

            // Verify that breachesL has an entry
            let count: i64 = db
                .db
                .query_row("SELECT COUNT(*) FROM breachesL", [], |row| row.get(0))
                .unwrap();
            assert_eq!(count, 1);
            // And verify via the API that this login is vulnerable
            let vulnerable = db
                .are_potentially_vulnerable_passwords(&[&login.meta.id])
                .unwrap();
            assert_eq!(vulnerable.len(), 1);
            assert_eq!(vulnerable[0], login.meta.id);

            // Reset all breaches
            db.reset_all_breaches().unwrap();

            // After reset, breachesL should be empty
            let count: i64 = db
                .db
                .query_row("SELECT COUNT(*) FROM breachesL", [], |row| row.get(0))
                .unwrap();
            assert_eq!(count, 0);
            // And verify via the API that no logins are vulnerable anymore
            let vulnerable = db
                .are_potentially_vulnerable_passwords(&[&login.meta.id])
                .unwrap();
            assert_eq!(vulnerable.len(), 0);
        }

        #[test]
        fn test_different_passwords_not_vulnerable() {
            ensure_initialized();
            let db = LoginDb::open_in_memory();

            let login1 = db
                .add(LoginEntry {
                    origin: "https://site1.com".into(),
                    http_realm: Some("realm".into()),
                    username: "user".into(),
                    password: "password_A".into(),
                    ..Default::default()
                })
                .unwrap();

            let login2 = db
                .add(LoginEntry {
                    origin: "https://site2.com".into(),
                    http_realm: Some("realm".into()),
                    username: "user".into(),
                    password: "password_B".into(),
                    ..Default::default()
                })
                .unwrap();

            db.record_potentially_vulnerable_passwords(vec!["password_A".into()])
                .unwrap();

            // login2 has a different password → not vulnerable
            assert!(!db
                .is_potentially_vulnerable_password(&login2.meta.id)
                .unwrap());
            // Batch check: login1 should be vulnerable (its password is in breachesL)
            // login2 has a different password, so it's not vulnerable
            let vulnerable = db
                .are_potentially_vulnerable_passwords(&[&login1.meta.id, &login2.meta.id])
                .unwrap();
            assert_eq!(vulnerable.len(), 1);
            assert!(vulnerable.contains(&login1.meta.id));
        }
    }
}
