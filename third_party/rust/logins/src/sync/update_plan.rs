/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::merge::{LocalLogin, MirrorLogin};
use super::{IncomingLogin, SyncStatus};
use crate::encryption::EncryptorDecryptor;
use crate::error::*;
use crate::util;
use interrupt_support::SqlInterruptScope;
use rusqlite::{named_params, Connection};
use std::time::SystemTime;
use sync15::ServerTimestamp;
use sync_guid::Guid;

#[derive(Default, Debug)]
pub(super) struct UpdatePlan {
    pub delete_mirror: Vec<Guid>,
    pub delete_local: Vec<Guid>,
    pub local_updates: Vec<MirrorLogin>,
    // the bool is the `is_overridden` flag, the i64 is ServerTimestamp in millis
    pub mirror_inserts: Vec<(IncomingLogin, i64, bool)>,
    pub mirror_updates: Vec<(IncomingLogin, i64)>,
}

impl UpdatePlan {
    pub fn plan_two_way_merge(
        &mut self,
        local: LocalLogin,
        upstream: (IncomingLogin, ServerTimestamp),
    ) {
        match &local {
            LocalLogin::Tombstone { .. } => {
                debug!("  ignoring local tombstone, inserting into mirror");
                self.delete_local.push(upstream.0.guid());
                self.plan_mirror_insert(upstream.0, upstream.1, false);
            }
            LocalLogin::Alive { login, .. } => {
                debug!("  Conflicting record without shared parent, using newer");
                let is_override =
                    login.meta.time_password_changed > upstream.0.login.meta.time_password_changed;
                self.plan_mirror_insert(upstream.0, upstream.1, is_override);
                if !is_override {
                    self.delete_local.push(login.guid());
                }
            }
        }
    }

    pub fn plan_three_way_merge(
        &mut self,
        local: LocalLogin,
        shared: MirrorLogin,
        upstream: IncomingLogin,
        upstream_time: ServerTimestamp,
        server_now: ServerTimestamp,
        encdec: &dyn EncryptorDecryptor,
    ) -> Result<()> {
        let local_age = SystemTime::now()
            .duration_since(local.local_modified())
            .unwrap_or_default();
        let remote_age = server_now.duration_since(upstream_time).unwrap_or_default();

        let delta = {
            let upstream_delta = upstream.login.delta(&shared.login, encdec)?;
            match local {
                LocalLogin::Tombstone { .. } => {
                    // If the login was deleted locally, the merged delta is the
                    // upstream delta. We do this because a user simultaneously deleting their
                    // login and updating it has two possible outcomes:
                    //   - A login that was intended to be deleted remains because another update was
                    //   there
                    //   - A login that was intended to be updated got deleted
                    //
                    //   The second case is arguably worse, where a user could lose their login
                    //   indefinitely
                    // So, just like desktop, this acts as though the local login doesn't exist at all.
                    upstream_delta
                }
                LocalLogin::Alive { login, .. } => {
                    let local_delta = login.delta(&shared.login, encdec)?;
                    local_delta.merge(upstream_delta, remote_age < local_age)
                }
            }
        };

        // Update mirror to upstream
        self.mirror_updates
            .push((upstream, upstream_time.as_millis()));
        let mut new = shared;

        new.login.apply_delta(delta, encdec)?;
        new.server_modified = upstream_time;
        self.local_updates.push(new);
        Ok(())
    }

    pub fn plan_delete(&mut self, id: Guid) {
        self.delete_local.push(id.clone());
        self.delete_mirror.push(id);
    }

    pub fn plan_mirror_update(&mut self, upstream: IncomingLogin, time: ServerTimestamp) {
        self.mirror_updates.push((upstream, time.as_millis()));
    }

    pub fn plan_mirror_insert(
        &mut self,
        upstream: IncomingLogin,
        time: ServerTimestamp,
        is_override: bool,
    ) {
        self.mirror_inserts
            .push((upstream, time.as_millis(), is_override));
    }

    fn perform_deletes(&self, conn: &Connection, scope: &SqlInterruptScope) -> Result<()> {
        sql_support::each_chunk(&self.delete_local, |chunk, _| -> Result<()> {
            conn.execute(
                &format!(
                    "DELETE FROM loginsL WHERE guid IN ({vars})",
                    vars = sql_support::repeat_sql_vars(chunk.len())
                ),
                rusqlite::params_from_iter(chunk),
            )?;
            scope.err_if_interrupted()?;
            Ok(())
        })?;

        sql_support::each_chunk(&self.delete_mirror, |chunk, _| {
            conn.execute(
                &format!(
                    "DELETE FROM loginsM WHERE guid IN ({vars})",
                    vars = sql_support::repeat_sql_vars(chunk.len())
                ),
                rusqlite::params_from_iter(chunk),
            )?;
            Ok(())
        })
    }

    // These aren't batched but probably should be.
    fn perform_mirror_updates(&self, conn: &Connection, scope: &SqlInterruptScope) -> Result<()> {
        let sql = "
            UPDATE loginsM
            SET server_modified  = :server_modified,
                enc_unknown_fields = :enc_unknown_fields,
                httpRealm        = :http_realm,
                formActionOrigin = :form_action_origin,
                usernameField    = :username_field,
                passwordField    = :password_field,
                origin           = :origin,
                secFields        = :sec_fields,
                -- Avoid zeroes if the remote has been overwritten by an older client.
                timesUsed           = coalesce(nullif(:times_used,            0), timesUsed),
                timeLastUsed        = coalesce(nullif(:time_last_used,        0), timeLastUsed),
                timePasswordChanged = coalesce(nullif(:time_password_changed, 0), timePasswordChanged),
                timeCreated         = coalesce(nullif(:time_created,          0), timeCreated)
            WHERE guid = :guid
        ";
        let mut stmt = conn.prepare_cached(sql)?;
        for (upstream, timestamp) in &self.mirror_updates {
            let login = &upstream.login;
            trace!("Updating mirror {:?}", login.guid_str());
            stmt.execute(named_params! {
                ":server_modified": *timestamp,
                ":enc_unknown_fields": upstream.unknown,
                ":http_realm": login.fields.http_realm,
                ":form_action_origin": login.fields.form_action_origin,
                ":username_field": login.fields.username_field,
                ":password_field": login.fields.password_field,
                ":origin": login.fields.origin,
                ":times_used": login.meta.times_used,
                ":time_last_used": login.meta.time_last_used,
                ":time_password_changed": login.meta.time_password_changed,
                ":time_created": login.meta.time_created,
                ":guid": login.guid_str(),
                ":sec_fields": login.sec_fields,
            })?;
            scope.err_if_interrupted()?;
        }
        Ok(())
    }

    fn perform_mirror_inserts(&self, conn: &Connection, scope: &SqlInterruptScope) -> Result<()> {
        let sql = "
            INSERT OR IGNORE INTO loginsM (
                is_overridden,
                server_modified,
                enc_unknown_fields,

                httpRealm,
                formActionOrigin,
                usernameField,
                passwordField,
                origin,
                secFields,

                timesUsed,
                timeLastUsed,
                timePasswordChanged,
                timeCreated,

                guid
            ) VALUES (
                :is_overridden,
                :server_modified,
                :enc_unknown_fields,

                :http_realm,
                :form_action_origin,
                :username_field,
                :password_field,
                :origin,
                :sec_fields,

                :times_used,
                :time_last_used,
                :time_password_changed,
                :time_created,

                :guid
            )";
        let mut stmt = conn.prepare_cached(sql)?;

        for (upstream, timestamp, is_overridden) in &self.mirror_inserts {
            let login = &upstream.login;
            trace!("Inserting mirror {:?}", login.guid_str());
            stmt.execute(named_params! {
                ":is_overridden": *is_overridden,
                ":server_modified": *timestamp,
                ":enc_unknown_fields": upstream.unknown,
                ":http_realm": login.fields.http_realm,
                ":form_action_origin": login.fields.form_action_origin,
                ":username_field": login.fields.username_field,
                ":password_field": login.fields.password_field,
                ":origin": login.fields.origin,
                ":times_used": login.meta.times_used,
                ":time_last_used": login.meta.time_last_used,
                ":time_password_changed": login.meta.time_password_changed,
                ":time_created": login.meta.time_created,
                ":guid": login.guid_str(),
                ":sec_fields": login.sec_fields,
            })?;
            scope.err_if_interrupted()?;
        }
        Ok(())
    }

    fn perform_local_updates(&self, conn: &Connection, scope: &SqlInterruptScope) -> Result<()> {
        let sql = format!(
            "UPDATE loginsL
             SET local_modified      = :local_modified,
                 httpRealm           = :http_realm,
                 formActionOrigin    = :form_action_origin,
                 usernameField       = :username_field,
                 passwordField       = :password_field,
                 timeLastUsed        = :time_last_used,
                 timePasswordChanged = :time_password_changed,
                 timesUsed           = :times_used,
                 origin              = :origin,
                 secFields           = :sec_fields,
                 sync_status         = {changed},
                 is_deleted          = 0
             WHERE guid = :guid",
            changed = SyncStatus::Changed as u8
        );
        let mut stmt = conn.prepare_cached(&sql)?;
        // XXX OutgoingChangeset should no longer have timestamp.
        let local_ms: i64 = util::system_time_ms_i64(SystemTime::now());
        for l in &self.local_updates {
            trace!("Updating local {:?}", l.guid_str());
            stmt.execute(named_params! {
                ":local_modified": local_ms,
                ":http_realm": l.login.fields.http_realm,
                ":form_action_origin": l.login.fields.form_action_origin,
                ":username_field": l.login.fields.username_field,
                ":password_field": l.login.fields.password_field,
                ":origin": l.login.fields.origin,
                ":time_last_used": l.login.meta.time_last_used,
                ":time_password_changed": l.login.meta.time_password_changed,
                ":times_used": l.login.meta.times_used,
                ":guid": l.guid_str(),
                ":sec_fields": l.login.sec_fields,
            })?;
            scope.err_if_interrupted()?;
        }
        Ok(())
    }

    pub fn execute(&self, conn: &Connection, scope: &SqlInterruptScope) -> Result<()> {
        debug!(
            "UpdatePlan: deleting {} records...",
            self.delete_local.len()
        );
        self.perform_deletes(conn, scope)?;
        debug!(
            "UpdatePlan: Updating {} existing mirror records...",
            self.mirror_updates.len()
        );
        self.perform_mirror_updates(conn, scope)?;
        debug!(
            "UpdatePlan: Inserting {} new mirror records...",
            self.mirror_inserts.len()
        );
        self.perform_mirror_inserts(conn, scope)?;
        debug!(
            "UpdatePlan: Updating {} reconciled local records...",
            self.local_updates.len()
        );
        self.perform_local_updates(conn, scope)?;
        Ok(())
    }
}

#[cfg(not(feature = "keydb"))]
#[cfg(test)]
mod tests {
    use nss::ensure_initialized;
    use std::time::Duration;

    use super::*;
    use crate::db::test_utils::{
        check_local_login, check_mirror_login, get_local_guids, get_mirror_guids,
        get_server_modified, insert_encrypted_login, insert_login,
    };
    use crate::db::LoginDb;
    use crate::encryption::test_utils::TEST_ENCDEC;
    use crate::login::test_utils::enc_login;

    fn inc_login(id: &str, password: &str) -> crate::sync::IncomingLogin {
        IncomingLogin {
            login: enc_login(id, password),
            unknown: Default::default(),
        }
    }

    #[test]
    fn test_deletes() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        insert_login(&db, "login1", Some("password"), Some("password"));
        insert_login(&db, "login2", Some("password"), Some("password"));
        insert_login(&db, "login3", Some("password"), Some("password"));
        insert_login(&db, "login4", Some("password"), Some("password"));

        UpdatePlan {
            delete_mirror: vec![Guid::new("login1"), Guid::new("login2")],
            delete_local: vec![Guid::new("login2"), Guid::new("login3")],
            ..UpdatePlan::default()
        }
        .execute(&db, &db.begin_interrupt_scope().unwrap())
        .unwrap();

        assert_eq!(get_local_guids(&db), vec!["login1", "login4"]);
        assert_eq!(get_mirror_guids(&db), vec!["login3", "login4"]);
    }

    #[test]
    fn test_mirror_updates() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        insert_login(&db, "unchanged", None, Some("password"));
        insert_login(&db, "changed", None, Some("password"));
        insert_login(
            &db,
            "changed2",
            Some("new-local-password"),
            Some("password"),
        );
        let initial_modified = get_server_modified(&db, "unchanged");

        UpdatePlan {
            mirror_updates: vec![
                (inc_login("changed", "new-password"), 20000),
                (inc_login("changed2", "new-password2"), 21000),
            ],
            ..UpdatePlan::default()
        }
        .execute(&db, &db.begin_interrupt_scope().unwrap())
        .unwrap();
        check_mirror_login(&db, "unchanged", "password", initial_modified, false);
        check_mirror_login(&db, "changed", "new-password", 20000, false);
        check_mirror_login(&db, "changed2", "new-password2", 21000, true);
    }

    #[test]
    fn test_mirror_inserts() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        UpdatePlan {
            mirror_inserts: vec![
                (inc_login("login1", "new-password"), 20000, false),
                (inc_login("login2", "new-password2"), 21000, true),
            ],
            ..UpdatePlan::default()
        }
        .execute(&db, &db.begin_interrupt_scope().unwrap())
        .unwrap();
        check_mirror_login(&db, "login1", "new-password", 20000, false);
        check_mirror_login(&db, "login2", "new-password2", 21000, true);
    }

    #[test]
    fn test_local_updates() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        insert_login(&db, "login", Some("password"), Some("password"));
        let before_update = util::system_time_ms_i64(SystemTime::now());

        UpdatePlan {
            local_updates: vec![MirrorLogin {
                login: enc_login("login", "new-password"),
                server_modified: ServerTimestamp(10000),
            }],
            ..UpdatePlan::default()
        }
        .execute(&db, &db.begin_interrupt_scope().unwrap())
        .unwrap();
        check_local_login(&db, "login", "new-password", before_update);
    }

    #[test]
    fn test_plan_three_way_merge_server_wins() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        // First we create our expected logins
        let login = enc_login("login", "old local password");
        let mirror_login = enc_login("login", "mirror password");
        let server_login = enc_login("login", "new upstream password");

        // Then, we create a new, empty update plan
        let mut update_plan = UpdatePlan::default();
        // Here, we define all the timestamps, remember, if difference between the
        // upstream record timestamp and the server timestamp is less than the
        // difference between the local record timestamp and time **now** then the server wins.
        //
        // In other words, if server_time - upstream_time < now - local_record_time then the server
        // wins. This is because we determine which record to "prefer" based on the "age" of the
        // update
        let now = SystemTime::now();
        // local record's timestamps is now - 100 second, so the local age is 100
        let local_modified = now.checked_sub(Duration::from_secs(100)).unwrap();
        // mirror timestamp is not too relevant here, but we set it for completeness
        let mirror_timestamp = now.checked_sub(Duration::from_secs(1000)).unwrap();
        // Server's timestamp is now
        let server_timestamp = now;
        // Server's record timestamp is now - 1 second, so the server age is: 1
        // And since the local age is 100, then the server should win.
        let server_record_timestamp = now.checked_sub(Duration::from_secs(1)).unwrap();
        let local_login = LocalLogin::Alive {
            login: Box::new(login.clone()),
            local_modified,
        };

        let mirror_login = MirrorLogin {
            login: mirror_login,
            server_modified: mirror_timestamp.try_into().unwrap(),
        };

        // Lets make sure our local login is in the database, so that it can be updated later
        insert_encrypted_login(
            &db,
            &login,
            &mirror_login.login,
            &mirror_login.server_modified,
        );
        let upstream_login = IncomingLogin {
            login: server_login,
            unknown: None,
        };

        update_plan
            .plan_three_way_merge(
                local_login,
                mirror_login,
                upstream_login,
                server_record_timestamp.try_into().unwrap(),
                server_timestamp.try_into().unwrap(),
                &*TEST_ENCDEC,
            )
            .unwrap();
        update_plan
            .execute(&db, &db.begin_interrupt_scope().unwrap())
            .unwrap();

        check_local_login(&db, "login", "new upstream password", 0);
    }

    #[test]
    fn test_plan_three_way_merge_local_wins() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        // First we create our expected logins
        let login = enc_login("login", "new local password");
        let mirror_login = enc_login("login", "mirror password");
        let server_login = enc_login("login", "old upstream password");

        // Then, we create a new, empty update plan
        let mut update_plan = UpdatePlan::default();
        // Here, we define all the timestamps, remember, if difference between the
        // upstream record timestamp and the server timestamp is less than the
        // difference between the local record timestamp and time **now** then the server wins.
        //
        // In other words, if server_time - upstream_time < now - local_record_time then the server
        // wins. This is because we determine which record to "prefer" based on the "age" of the
        // update
        let now = SystemTime::now();
        // local record's timestamps is now - 1 second, so the local age is 1
        let local_modified = now.checked_sub(Duration::from_secs(1)).unwrap();
        // mirror timestamp is not too relevant here, but we set it for completeness
        let mirror_timestamp = now.checked_sub(Duration::from_secs(1000)).unwrap();
        // Server's timestamp is now
        let server_timestamp = now;
        // Server's record timestamp is now - 500 second, so the server age is: 500
        // And since the local age is 1, the local record should win!
        let server_record_timestamp = now.checked_sub(Duration::from_secs(500)).unwrap();
        let local_login = LocalLogin::Alive {
            login: Box::new(login.clone()),
            local_modified,
        };
        let mirror_login = MirrorLogin {
            login: mirror_login,
            server_modified: mirror_timestamp.try_into().unwrap(),
        };

        // Lets make sure our local login is in the database, so that it can be updated later
        insert_encrypted_login(
            &db,
            &login,
            &mirror_login.login,
            &mirror_login.server_modified,
        );

        let upstream_login = IncomingLogin {
            login: server_login,
            unknown: None,
        };

        update_plan
            .plan_three_way_merge(
                local_login,
                mirror_login,
                upstream_login,
                server_record_timestamp.try_into().unwrap(),
                server_timestamp.try_into().unwrap(),
                &*TEST_ENCDEC,
            )
            .unwrap();
        update_plan
            .execute(&db, &db.begin_interrupt_scope().unwrap())
            .unwrap();

        check_local_login(&db, "login", "new local password", 0);
    }

    #[test]
    fn test_plan_three_way_merge_local_tombstone_loses() {
        ensure_initialized();
        let db = LoginDb::open_in_memory();
        // First we create our expected logins
        let login = enc_login("login", "new local password");
        let mirror_login = enc_login("login", "mirror password");
        let server_login = enc_login("login", "old upstream password");

        // Then, we create a new, empty update plan
        let mut update_plan = UpdatePlan::default();
        // Here, we define all the timestamps, remember, if difference between the
        // upstream record timestamp and the server timestamp is less than the
        // difference between the local record timestamp and time **now** then the server wins.
        //
        // In other words, if server_time - upstream_time < now - local_record_time then the server
        // wins. This is because we determine which record to "prefer" based on the "age" of the
        // update
        let now = SystemTime::now();
        // local record's timestamps is now - 1 second, so the local age is 1
        let local_modified = now.checked_sub(Duration::from_secs(1)).unwrap();
        // mirror timestamp is not too relevant here, but we set it for completeness
        let mirror_timestamp = now.checked_sub(Duration::from_secs(1000)).unwrap();
        // Server's timestamp is now
        let server_timestamp = now;
        // Server's record timestamp is now - 500 second, so the server age is: 500
        // And since the local age is 1, the local record should win!
        let server_record_timestamp = now.checked_sub(Duration::from_secs(500)).unwrap();
        let mirror_login = MirrorLogin {
            login: mirror_login,
            server_modified: mirror_timestamp.try_into().unwrap(),
        };

        // Lets make sure our local login is in the database, so that it can be updated later
        insert_encrypted_login(
            &db,
            &login,
            &mirror_login.login,
            &mirror_login.server_modified,
        );

        // Now, lets delete our local login
        db.delete("login").unwrap();

        // Then, lets set our tombstone
        let local_login = LocalLogin::Tombstone {
            id: login.meta.id.clone(),
            local_modified,
        };

        let upstream_login = IncomingLogin {
            login: server_login,
            unknown: None,
        };

        update_plan
            .plan_three_way_merge(
                local_login,
                mirror_login,
                upstream_login,
                server_record_timestamp.try_into().unwrap(),
                server_timestamp.try_into().unwrap(),
                &*TEST_ENCDEC,
            )
            .unwrap();
        update_plan
            .execute(&db, &db.begin_interrupt_scope().unwrap())
            .unwrap();

        // Now we verify that even though our login deletion was "younger"
        // then the upstream modification, the upstream modification wins because
        // modifications always beat tombstones
        check_local_login(&db, "login", "old upstream password", 0);
    }

    #[test]
    fn test_plan_two_way_merge_local_tombstone_loses() {
        ensure_initialized();
        let mut update_plan = UpdatePlan::default();
        // Ensure the local tombstone is newer than the incoming - it still loses.
        let local = LocalLogin::Tombstone {
            id: "login-id".to_string(),
            local_modified: SystemTime::now(),
        };
        let incoming = IncomingLogin {
            login: enc_login("login-id", "new local password"),
            unknown: None,
        };

        update_plan.plan_two_way_merge(local, (incoming, ServerTimestamp::from_millis(1234)));

        // Plan should be to apply the incoming.
        assert_eq!(update_plan.mirror_inserts.len(), 1);
        assert_eq!(update_plan.delete_local.len(), 1);
        assert_eq!(update_plan.delete_mirror.len(), 0);
    }
}
