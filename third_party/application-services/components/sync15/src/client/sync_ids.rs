/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Maintaining `meta/syncids` when using per-device engine choices.
//!
//! Clients using per-device engine choices never add or remove engines in
//! `meta/global` - adding one would enable it on every client using per-account
//! choices. So the sync IDs for engines missing from `meta/global` live in
//! `meta/syncids` instead. An engine in `meta/global` always uses that entry,
//! which we copy into `meta/syncids` so it survives the engine being removed
//! from `meta/global` by a client using per-account choices.
//!
//! As on Desktop, an entry for an older engine version gets a new sync ID for
//! ours, even in `meta/global`, and one for a newer version is left alone.

use super::state::GlobalState;
use super::storage_client::SetupStorageClient;
use crate::Guid;
use crate::error::{self, Error, ErrorResponse, info, warn};
use crate::record_types::{MetaGlobalEngine, MetaGlobalRecord, SyncIdsRecord};
use std::cmp::Ordering;

/// The records we need to write so every engine has a sync ID for its version.
#[derive(Debug, Default)]
pub(crate) struct SyncIdsPlan {
    /// The new `meta/global`, if any of its entries were for older versions.
    pub global: Option<MetaGlobalRecord>,
    /// The new `meta/syncids`, if it changed.
    pub sync_ids: Option<SyncIdsRecord>,
}

impl SyncIdsPlan {
    pub(crate) fn has_changes(&self) -> bool {
        self.global.is_some() || self.sync_ids.is_some()
    }
}

fn new_engine_meta(name: &str, version: usize) -> MetaGlobalEngine {
    info!("Minting a new sync ID for the {} engine", name);
    MetaGlobalEngine {
        version,
        sync_id: Guid::random(),
    }
}

/// Works out how `meta/global` and `meta/syncids` need to change so every
/// engine in `engines` has a sync ID for its version. Engines we can't give one
/// fail when they ask for it (see `GlobalState::engine_meta`).
pub(crate) fn plan_sync_ids(
    global: &MetaGlobalRecord,
    current: Option<&SyncIdsRecord>,
    engines: &[(String, usize)],
) -> SyncIdsPlan {
    // A record in a newer format belongs to a newer client, so we neither use nor
    // change it. One written for a different meta/global (i.e. before the server
    // was last set up afresh), or that's otherwise unusable, is replaced.
    let mut record = match current {
        Some(r) if r.is_newer_format() => {
            warn!("Not updating meta/syncids, as it's in a newer format");
            None
        }
        Some(r) if r.is_usable_for(global) => Some(r.clone()),
        _ => Some(SyncIdsRecord::new_for(global)),
    };
    let mut new_global: Option<MetaGlobalRecord> = None;
    let mut record_changed = false;
    for (name, version) in engines {
        let entry = match global.engines.get(name) {
            Some(mg) => match mg.version.cmp(version) {
                Ordering::Equal => mg.clone(),
                Ordering::Greater => continue,
                Ordering::Less => {
                    let entry = new_engine_meta(name, *version);
                    new_global
                        .get_or_insert_with(|| global.clone())
                        .engines
                        .insert(name.clone(), entry.clone());
                    entry
                }
            },
            None => {
                let Some(record) = &record else {
                    continue;
                };
                match record.engines.get(name) {
                    Some(e) if !e.sync_id.is_empty() && e.version >= *version => continue,
                    _ => new_engine_meta(name, *version),
                }
            }
        };
        if let Some(record) = &mut record
            && record.engines.get(name) != Some(&entry)
        {
            record.engines.insert(name.clone(), entry);
            record_changed = true;
        }
    }
    SyncIdsPlan {
        global: new_global,
        sync_ids: record.filter(|_| record_changed),
    }
}

/// The result of trying to update `meta/global` and `meta/syncids`.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum SyncIdsOutcome {
    /// Every engine has a sync ID.
    Ready,
    /// Someone else changed `meta/global` or `meta/syncids` since we fetched
    /// them.
    Conflict,
}

fn is_conflict(e: &Error) -> bool {
    matches!(
        e,
        Error::StorageHttpError(ErrorResponse::PreconditionFailed { .. })
    )
}

/// Makes a single attempt to ensure every engine in `engines` has a sync ID,
/// updating `meta/global` and `meta/syncids` if necessary. The setup state
/// machine starts over after a conflict.
pub(crate) fn try_ensure_sync_ids(
    client: &dyn SetupStorageClient,
    state: &mut GlobalState,
    engines: &[(String, usize)],
) -> error::Result<SyncIdsOutcome> {
    let plan = plan_sync_ids(&state.global, state.sync_ids.as_ref(), engines);
    // We deliberately leave `meta_timestamp` alone after either write -
    // the other record might have changed since we read it, and this way our
    // next sync will check.
    if let Some(global) = plan.global {
        info!("Uploading meta/global with new engine versions");
        match client.put_meta_global(state.global_timestamp, &global) {
            Ok(timestamp) => {
                state.global = global;
                state.global_timestamp = timestamp;
            }
            Err(e) if is_conflict(&e) => return Ok(SyncIdsOutcome::Conflict),
            Err(e) => return Err(e),
        }
    }
    if let Some(record) = plan.sync_ids {
        match client.put_sync_ids(state.sync_ids_timestamp, &record) {
            Ok(timestamp) => {
                state.sync_ids = Some(record);
                state.sync_ids_timestamp = timestamp;
            }
            Err(e) if is_conflict(&e) => return Ok(SyncIdsOutcome::Conflict),
            Err(e) => return Err(e),
        }
    }
    Ok(SyncIdsOutcome::Ready)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ServerTimestamp;
    use crate::bso::{IncomingEncryptedBso, OutgoingEncryptedBso};
    use crate::client::request::{InfoCollections, InfoConfiguration};
    use crate::client::storage_client::Sync15ClientResponse;
    use std::cell::RefCell;
    use std::collections::HashMap;

    const GLOBAL_SYNC_ID: &str = "globalAAAAAA";

    fn engine(version: usize, sync_id: &str) -> MetaGlobalEngine {
        MetaGlobalEngine {
            version,
            sync_id: sync_id.into(),
        }
    }

    fn global(engines: &[(&str, usize, &str)]) -> MetaGlobalRecord {
        MetaGlobalRecord {
            sync_id: GLOBAL_SYNC_ID.into(),
            storage_version: 5,
            engines: engines
                .iter()
                .map(|(n, v, id)| (n.to_string(), engine(*v, id)))
                .collect(),
            declined: vec!["bookmarks".into()],
        }
    }

    fn record(engines: &[(&str, usize, &str)]) -> SyncIdsRecord {
        SyncIdsRecord {
            version: 1,
            global_sync_id: Some(GLOBAL_SYNC_ID.into()),
            engines: engines
                .iter()
                .map(|(n, v, id)| (n.to_string(), engine(*v, id)))
                .collect(),
            ..Default::default()
        }
    }

    fn engines(names: &[(&str, usize)]) -> Vec<(String, usize)> {
        names.iter().map(|(n, v)| (n.to_string(), *v)).collect()
    }

    /// Plans when meta/global needs no changes, returning the new meta/syncids.
    fn plan(
        global: &MetaGlobalRecord,
        current: Option<&SyncIdsRecord>,
        engines: &[(String, usize)],
    ) -> Option<SyncIdsRecord> {
        let plan = plan_sync_ids(global, current, engines);
        assert!(plan.global.is_none());
        plan.sync_ids
    }

    #[test]
    fn test_meta_global_wins_and_is_mirrored() {
        let g = global(&[("history", 1, "historyMGMG")]);
        // Not in the registry yet.
        let updated = plan(&g, None, &engines(&[("history", 1)])).unwrap();
        assert_eq!(updated.engines["history"], engine(1, "historyMGMG"));
        assert!(updated.is_usable_for(&g));
        // In the registry with a different ID: meta/global wins.
        let current = record(&[("history", 1, "historyREGI")]);
        let updated = plan(&g, Some(&current), &engines(&[("history", 1)])).unwrap();
        assert_eq!(updated.engines["history"], engine(1, "historyMGMG"));
        // Already mirrored: nothing to do.
        let current = record(&[("history", 1, "historyMGMG")]);
        assert_eq!(plan(&g, Some(&current), &engines(&[("history", 1)])), None);
    }

    #[test]
    fn test_uses_registry_then_mints() {
        // Note `bookmarks` is declined in meta/global, which we ignore.
        let g = global(&[]);
        let current = record(&[
            ("bookmarks", 2, "bookmarksREG"),
            ("other", 1, "otherREGIDAA"),
        ]);
        assert_eq!(
            plan(&g, Some(&current), &engines(&[("bookmarks", 2)])),
            None
        );

        let updated = plan(
            &g,
            Some(&current),
            &engines(&[("bookmarks", 2), ("tabs", 1)]),
        )
        .unwrap();
        assert_eq!(updated.engines["bookmarks"], engine(2, "bookmarksREG"));
        // Entries for other engines are kept.
        assert_eq!(updated.engines["other"], engine(1, "otherREGIDAA"));
        assert_eq!(updated.engines["tabs"].version, 1);
        assert!(!updated.engines["tabs"].sync_id.is_empty());
    }

    #[test]
    fn test_newer_records_are_left_alone() {
        let g = global(&[("history", 1, "historyMGMG")]);
        let mut current = record(&[("tabs", 1, "")]);
        current.version = 2;
        // Even when it's for a different meta/global - a newer client might
        // tie the record to meta/global differently.
        let mut stale = current.clone();
        stale.global_sync_id = Some("globalOLDOLD".into());
        for current in [current, stale] {
            assert_eq!(
                plan(
                    &g,
                    Some(&current),
                    &engines(&[("history", 1), ("tabs", 1), ("bookmarks", 2)])
                ),
                None
            );
        }
    }

    #[test]
    fn test_engine_versions() {
        // history is newer in meta/global, and forms is older.
        let g = global(&[("history", 2, "historyMGMG"), ("forms", 0, "formsMGMGMGM")]);
        // tabs is newer in meta/syncids, and passwords is older.
        let current = record(&[
            ("history", 1, "historyREGI"),
            ("tabs", 2, "tabsREGIDAAA"),
            ("passwords", 0, "passwordsREG"),
        ]);
        let ours = engines(&[("history", 1), ("forms", 1), ("tabs", 1), ("passwords", 1)]);
        let plan = plan_sync_ids(&g, Some(&current), &ours);
        let new_global = plan.global.unwrap();
        let updated = plan.sync_ids.unwrap();
        // Newer entries are left alone, wherever they are.
        assert_eq!(new_global.engines["history"], engine(2, "historyMGMG"));
        assert_eq!(updated.engines["history"], engine(1, "historyREGI"));
        assert_eq!(updated.engines["tabs"], engine(2, "tabsREGIDAAA"));
        // Older ones are replaced, even in meta/global, and mirrored as usual.
        let forms = &new_global.engines["forms"];
        assert_eq!(forms.version, 1);
        assert_ne!(forms.sync_id, "formsMGMGMGM");
        assert_eq!(&updated.engines["forms"], forms);
        assert_eq!(updated.engines["passwords"].version, 1);
        assert_ne!(updated.engines["passwords"].sync_id, "passwordsREG");
        // But we never add engines to meta/global.
        assert_eq!(new_global.engines.len(), 2);
        assert_eq!(new_global.declined, g.declined);

        // And older entries in meta/global are replaced even when meta/syncids
        // is in a newer format.
        let mut newer = current.clone();
        newer.version = 2;
        let plan = plan_sync_ids(&g, Some(&newer), &ours);
        assert_eq!(plan.global.unwrap().engines["forms"].version, 1);
        assert!(plan.sync_ids.is_none());

        // Nothing to do when everything's current.
        assert!(
            plan_sync_ids(&g, Some(&current), &engines(&[("history", 2)]))
                .global
                .is_none()
        );
    }

    #[test]
    fn test_replaces_unusable_entries_and_records() {
        let g = global(&[]);
        let current = record(&[("tabs", 1, "")]);
        let updated = plan(&g, Some(&current), &engines(&[("tabs", 1)])).unwrap();
        assert!(!updated.engines["tabs"].sync_id.is_empty());

        // A record for a different meta/global (i.e. from before the server
        // was last set up afresh), or without a version, is replaced.
        let mut stale = record(&[("tabs", 1, "tabsREGIDAAA"), ("other", 1, "otherREGIDAA")]);
        stale.global_sync_id = Some("globalOLDOLD".into());
        let mut unversioned = record(&[("tabs", 1, "tabsREGIDAAA"), ("other", 1, "otherREGIDAA")]);
        unversioned.version = 0;
        for current in [stale, unversioned] {
            let updated = plan(&g, Some(&current), &engines(&[("tabs", 1)])).unwrap();
            assert!(updated.is_usable_for(&g));
            assert_ne!(updated.engines["tabs"].sync_id, "tabsREGIDAAA");
            assert!(!updated.engines.contains_key("other"));
        }
    }

    // A client for `try_ensure_sync_ids`, which only writes meta/global and
    // meta/syncids.
    struct SyncIdsClient {
        // The responses to successive PUTs of either record.
        put_results: RefCell<Vec<error::Result<ServerTimestamp>>>,
        // What we PUT, and the X-If-Unmodified-Since we used.
        puts: RefCell<Vec<(ServerTimestamp, SyncIdsRecord)>>,
        global_puts: RefCell<Vec<(ServerTimestamp, MetaGlobalRecord)>>,
    }

    impl SyncIdsClient {
        fn new(put_results: Vec<error::Result<ServerTimestamp>>) -> Self {
            Self {
                put_results: RefCell::new(put_results),
                puts: RefCell::new(vec![]),
                global_puts: RefCell::new(vec![]),
            }
        }
    }

    impl SetupStorageClient for SyncIdsClient {
        fn fetch_info_configuration(
            &self,
        ) -> error::Result<Sync15ClientResponse<InfoConfiguration>> {
            unimplemented!()
        }
        fn fetch_info_collections(&self) -> error::Result<Sync15ClientResponse<InfoCollections>> {
            unimplemented!()
        }
        fn fetch_meta_global(&self) -> error::Result<Sync15ClientResponse<MetaGlobalRecord>> {
            unimplemented!()
        }
        fn fetch_crypto_keys(&self) -> error::Result<Sync15ClientResponse<IncomingEncryptedBso>> {
            unimplemented!()
        }
        fn fetch_sync_ids(&self) -> error::Result<Sync15ClientResponse<Option<SyncIdsRecord>>> {
            unimplemented!()
        }
        fn put_meta_global(
            &self,
            xius: ServerTimestamp,
            global: &MetaGlobalRecord,
        ) -> error::Result<ServerTimestamp> {
            self.global_puts.borrow_mut().push((xius, global.clone()));
            self.put_results.borrow_mut().remove(0)
        }
        fn put_sync_ids(
            &self,
            xius: ServerTimestamp,
            sync_ids: &SyncIdsRecord,
        ) -> error::Result<ServerTimestamp> {
            self.puts.borrow_mut().push((xius, sync_ids.clone()));
            self.put_results.borrow_mut().remove(0)
        }
        fn put_crypto_keys(
            &self,
            _xius: ServerTimestamp,
            _keys: &OutgoingEncryptedBso,
        ) -> error::Result<()> {
            unimplemented!()
        }
        fn wipe_all_remote(&self) -> error::Result<()> {
            unimplemented!()
        }
    }

    fn precondition_failed() -> error::Result<ServerTimestamp> {
        Err(Error::StorageHttpError(ErrorResponse::PreconditionFailed {
            route: "storage/meta/syncids".into(),
        }))
    }

    fn state(sync_ids: Option<SyncIdsRecord>) -> GlobalState {
        GlobalState {
            config: InfoConfiguration::default(),
            collections: InfoCollections::new(HashMap::new()),
            global: global(&[]),
            global_timestamp: ServerTimestamp(500),
            meta_timestamp: ServerTimestamp(1000),
            keys: crate::EncryptedPayload {
                iv: String::new(),
                hmac: String::new(),
                ciphertext: String::new(),
            },
            keys_timestamp: ServerTimestamp(500),
            per_device: true,
            sync_ids_timestamp: if sync_ids.is_some() {
                ServerTimestamp(1000)
            } else {
                ServerTimestamp(0)
            },
            sync_ids,
        }
    }

    #[test]
    fn test_try_ensure_writes_and_updates_state() {
        let client = SyncIdsClient::new(vec![Ok(ServerTimestamp(3000))]);
        let mut s = state(None);
        let outcome = try_ensure_sync_ids(&client, &mut s, &engines(&[("tabs", 1)])).unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Ready);
        let puts = client.puts.borrow();
        assert_eq!(puts.len(), 1);
        // No record yet, so we insist there still isn't one.
        assert_eq!(puts[0].0, ServerTimestamp(0));
        assert_eq!(s.sync_ids.as_ref(), Some(&puts[0].1));
        assert_eq!(s.sync_ids_timestamp, ServerTimestamp(3000));
        assert_eq!(
            s.engine_meta("tabs").unwrap(),
            Some(&puts[0].1.engines["tabs"])
        );
        assert_eq!(s.meta_timestamp, ServerTimestamp(1000));
        drop(puts);

        // And nothing more to do next time.
        let outcome = try_ensure_sync_ids(&client, &mut s, &engines(&[("tabs", 1)])).unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Ready);
        assert_eq!(client.puts.borrow().len(), 1);
    }

    #[test]
    fn test_try_ensure_reports_conflict() {
        let client = SyncIdsClient::new(vec![precondition_failed()]);
        let mut s = state(None);
        let outcome = try_ensure_sync_ids(&client, &mut s, &engines(&[("tabs", 1)])).unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Conflict);
        assert_eq!(client.puts.borrow().len(), 1);
        assert!(s.sync_ids.is_none());
        assert!(s.engine_meta("tabs").unwrap().is_none());

        // Even after we've written meta/global.
        let client = SyncIdsClient::new(vec![Ok(ServerTimestamp(2000)), precondition_failed()]);
        let mut s = state(None);
        s.global = global(&[("forms", 0, "formsMGMGMGM")]);
        let outcome = try_ensure_sync_ids(&client, &mut s, &engines(&[("forms", 1)])).unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Conflict);
        assert_eq!(client.global_puts.borrow().len(), 1);
        assert_eq!(client.puts.borrow().len(), 1);
    }

    #[test]
    fn test_try_ensure_leaves_newer_records_alone() {
        let client = SyncIdsClient::new(vec![]);
        let mut current = record(&[("tabs", 1, "tabsREGIDAAA")]);
        current.version = 2;
        let mut s = state(Some(current));
        s.global = global(&[("history", 1, "historyMGMG")]);
        let outcome =
            try_ensure_sync_ids(&client, &mut s, &engines(&[("tabs", 1), ("history", 1)])).unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Ready);
        assert!(client.puts.borrow().is_empty());
        // Engines in meta/global can still sync, but the others can't.
        assert_eq!(
            s.engine_meta("history").unwrap(),
            Some(&engine(1, "historyMGMG"))
        );
        assert!(matches!(
            s.engine_meta("tabs"),
            Err(Error::ClientUpgradeRequired)
        ));
    }

    #[test]
    fn test_try_ensure_replaces_older_meta_global_entries() {
        let client = SyncIdsClient::new(vec![Ok(ServerTimestamp(2000)), Ok(ServerTimestamp(3000))]);
        let mut s = state(None);
        s.global = global(&[("forms", 0, "formsMGMGMGM"), ("history", 1, "historyMGMG")]);
        let outcome =
            try_ensure_sync_ids(&client, &mut s, &engines(&[("forms", 1), ("history", 1)]))
                .unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Ready);
        let global_puts = client.global_puts.borrow();
        assert_eq!(global_puts.len(), 1);
        assert_eq!(global_puts[0].0, ServerTimestamp(500));
        assert_eq!(s.global.engines, global_puts[0].1.engines);
        assert_eq!(s.global_timestamp, ServerTimestamp(2000));
        let forms = s.engine_meta("forms").unwrap().unwrap();
        assert_eq!(forms.version, 1);
        assert_ne!(forms.sync_id, "formsMGMGMGM");
        // Mirrored to meta/syncids as usual.
        assert_eq!(&client.puts.borrow()[0].1.engines["forms"], forms);
        assert_eq!(s.sync_ids_timestamp, ServerTimestamp(3000));
        assert_eq!(s.meta_timestamp, ServerTimestamp(1000));
    }

    #[test]
    fn test_try_ensure_reports_meta_global_conflict() {
        let client = SyncIdsClient::new(vec![precondition_failed()]);
        let mut s = state(None);
        s.global = global(&[("forms", 0, "formsMGMGMGM")]);
        let outcome = try_ensure_sync_ids(&client, &mut s, &engines(&[("forms", 1)])).unwrap();
        assert_eq!(outcome, SyncIdsOutcome::Conflict);
        assert_eq!(client.global_puts.borrow().len(), 1);
        assert!(client.puts.borrow().is_empty());
        assert_eq!(s.global.engines["forms"], engine(0, "formsMGMGMGM"));
        assert_eq!(s.global_timestamp, ServerTimestamp(500));
    }
}
