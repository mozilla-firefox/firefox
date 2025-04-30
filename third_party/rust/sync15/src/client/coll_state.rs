/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::request::InfoConfiguration;
use super::{CollectionKeys, GlobalState};
use crate::engine::{CollSyncIds, EngineSyncAssociation, SyncEngine};
use crate::error;
use crate::KeyBundle;
use crate::ServerTimestamp;

/// Holds state for a collection necessary to perform a sync of it. Lives for the lifetime
/// of a single sync.
#[derive(Debug, Clone)]
pub struct CollState {
    // Info about the server configuration/capabilities
    pub config: InfoConfiguration,
    // from meta/global, used for XIUS when we POST outgoing record based on this state.
    pub last_modified: ServerTimestamp,
    pub key: KeyBundle,
}

/// This mini state-machine helps build a CollState
#[derive(Debug)]
pub enum LocalCollState {
    /// The state is unknown, with the EngineSyncAssociation the collection
    /// reports.
    Unknown { assoc: EngineSyncAssociation },

    /// The engine has been declined. This is a "terminal" state.
    Declined,

    /// There's no such collection in meta/global. We could possibly update
    /// meta/global, but currently all known collections are there by default,
    /// so this is, basically, an error condition.
    NoSuchCollection,

    /// Either the global or collection sync ID has changed - we will reset the engine.
    SyncIdChanged { ids: CollSyncIds },

    /// The collection is ready to sync.
    Ready { coll_state: CollState },
}

pub struct LocalCollStateMachine<'state> {
    global_state: &'state GlobalState,
    root_key: &'state KeyBundle,
}

impl<'state> LocalCollStateMachine<'state> {
    fn advance(
        &self,
        from: LocalCollState,
        engine: &dyn SyncEngine,
    ) -> error::Result<LocalCollState> {
        let name = &engine.collection_name().to_string();
        let meta_global = &self.global_state.global;
        match from {
            LocalCollState::Unknown { assoc } => {
                if meta_global.declined.contains(name) {
                    return Ok(LocalCollState::Declined);
                }
                match meta_global.engines.get(name) {
                    Some(engine_meta) => match assoc {
                        EngineSyncAssociation::Disconnected => Ok(LocalCollState::SyncIdChanged {
                            ids: CollSyncIds {
                                global: meta_global.sync_id.clone(),
                                coll: engine_meta.sync_id.clone(),
                            },
                        }),
                        EngineSyncAssociation::Connected(ref ids)
                            if ids.global == meta_global.sync_id
                                && ids.coll == engine_meta.sync_id =>
                        {
                            // We are done - build the CollState
                            let coll_keys = CollectionKeys::from_encrypted_payload(
                                self.global_state.keys.clone(),
                                self.global_state.keys_timestamp,
                                self.root_key,
                            )?;
                            let key = coll_keys.key_for_collection(name).clone();
                            let name = engine.collection_name();
                            let config = self.global_state.config.clone();
                            let last_modified = self
                                .global_state
                                .collections
                                .get(name.as_ref())
                                .cloned()
                                .unwrap_or_default();
                            let coll_state = CollState {
                                config,
                                last_modified,
                                key,
                            };
                            Ok(LocalCollState::Ready { coll_state })
                        }
                        _ => Ok(LocalCollState::SyncIdChanged {
                            ids: CollSyncIds {
                                global: meta_global.sync_id.clone(),
                                coll: engine_meta.sync_id.clone(),
                            },
                        }),
                    },
                    None => Ok(LocalCollState::NoSuchCollection),
                }
            }

            LocalCollState::Declined => unreachable!("can't advance from declined"),

            LocalCollState::NoSuchCollection => unreachable!("the collection is unknown"),

            LocalCollState::SyncIdChanged { ids } => {
                let assoc = EngineSyncAssociation::Connected(ids);
                log::info!("Resetting {} engine", engine.collection_name());
                engine.reset(&assoc)?;
                Ok(LocalCollState::Unknown { assoc })
            }

            LocalCollState::Ready { .. } => unreachable!("can't advance from ready"),
        }
    }

    // A little whimsy - a portmanteau of far and fast
    fn run_and_run_as_farst_as_you_can(
        &mut self,
        engine: &dyn SyncEngine,
    ) -> error::Result<Option<CollState>> {
        let mut s = LocalCollState::Unknown {
            assoc: engine.get_sync_assoc()?,
        };
        // This is a simple state machine and should never take more than
        // 10 goes around.
        let mut count = 0;
        loop {
            log::trace!("LocalCollState in {:?}", s);
            match s {
                LocalCollState::Ready { coll_state } => return Ok(Some(coll_state)),
                LocalCollState::Declined | LocalCollState::NoSuchCollection => return Ok(None),
                _ => {
                    count += 1;
                    if count > 10 {
                        log::warn!("LocalCollStateMachine appears to be looping");
                        return Ok(None);
                    }
                    // should we have better loop detection? Our limit of 10
                    // goes is probably OK for now, but not really ideal.
                    s = self.advance(s, engine)?;
                }
            };
        }
    }

    pub fn get_state(
        engine: &dyn SyncEngine,
        global_state: &'state GlobalState,
        root_key: &'state KeyBundle,
    ) -> error::Result<Option<CollState>> {
        let mut gingerbread_man = Self {
            global_state,
            root_key,
        };
        gingerbread_man.run_and_run_as_farst_as_you_can(engine)
    }
}

#[cfg(test)]
mod tests {
    use super::super::request::{InfoCollections, InfoConfiguration};
    use super::super::CollectionKeys;
    use super::*;
    use crate::bso::{IncomingBso, OutgoingBso};
    use crate::engine::CollectionRequest;
    use crate::record_types::{MetaGlobalEngine, MetaGlobalRecord};
    use crate::{telemetry, CollectionName};
    use anyhow::Result;
    use nss::ensure_initialized;
    use std::cell::{Cell, RefCell};
    use std::collections::HashMap;
    use sync_guid::Guid;

    fn get_global_state(root_key: &KeyBundle) -> GlobalState {
        let keys = CollectionKeys::new_random()
            .unwrap()
            .to_encrypted_payload(root_key)
            .unwrap();
        GlobalState {
            config: InfoConfiguration::default(),
            collections: InfoCollections::new(HashMap::new()),
            global: MetaGlobalRecord {
                sync_id: "syncIDAAAAAA".into(),
                storage_version: 5usize,
                engines: vec![(
                    "bookmarks",
                    MetaGlobalEngine {
                        version: 1usize,
                        sync_id: "syncIDBBBBBB".into(),
                    },
                )]
                .into_iter()
                .map(|(key, value)| (key.to_owned(), value))
                .collect(),
                declined: vec![],
            },
            global_timestamp: ServerTimestamp::default(),
            keys,
            keys_timestamp: ServerTimestamp::default(),
        }
    }

    struct TestSyncEngine {
        collection_name: &'static str,
        assoc: Cell<EngineSyncAssociation>,
        num_resets: RefCell<usize>,
    }

    impl TestSyncEngine {
        fn new(collection_name: &'static str, assoc: EngineSyncAssociation) -> Self {
            Self {
                collection_name,
                assoc: Cell::new(assoc),
                num_resets: RefCell::new(0),
            }
        }
        fn get_num_resets(&self) -> usize {
            *self.num_resets.borrow()
        }
    }

    impl SyncEngine for TestSyncEngine {
        fn collection_name(&self) -> CollectionName {
            self.collection_name.into()
        }

        fn stage_incoming(
            &self,
            _inbound: Vec<IncomingBso>,
            _telem: &mut telemetry::Engine,
        ) -> Result<()> {
            unreachable!("these tests shouldn't call these");
        }

        fn apply(
            &self,
            _timestamp: ServerTimestamp,
            _telem: &mut telemetry::Engine,
        ) -> Result<Vec<OutgoingBso>> {
            unreachable!("these tests shouldn't call these");
        }

        fn set_uploaded(&self, _new_timestamp: ServerTimestamp, _ids: Vec<Guid>) -> Result<()> {
            unreachable!("these tests shouldn't call these");
        }

        fn sync_finished(&self) -> Result<()> {
            unreachable!("these tests shouldn't call these");
        }

        fn get_collection_request(
            &self,
            _server_timestamp: ServerTimestamp,
        ) -> Result<Option<CollectionRequest>> {
            unreachable!("these tests shouldn't call these");
        }

        fn get_sync_assoc(&self) -> Result<EngineSyncAssociation> {
            Ok(self.assoc.replace(EngineSyncAssociation::Disconnected))
        }

        fn reset(&self, new_assoc: &EngineSyncAssociation) -> Result<()> {
            self.assoc.replace(new_assoc.clone());
            *self.num_resets.borrow_mut() += 1;
            Ok(())
        }

        fn wipe(&self) -> Result<()> {
            unreachable!("these tests shouldn't call these");
        }
    }

    #[test]
    fn test_unknown() {
        ensure_initialized();
        let root_key = KeyBundle::new_random().expect("should work");
        let gs = get_global_state(&root_key);
        let engine = TestSyncEngine::new("unknown", EngineSyncAssociation::Disconnected);
        let cs = LocalCollStateMachine::get_state(&engine, &gs, &root_key).expect("should work");
        assert!(cs.is_none(), "unknown collection name can't sync");
        assert_eq!(engine.get_num_resets(), 0);
    }

    #[test]
    fn test_known_no_state() {
        ensure_initialized();
        let root_key = KeyBundle::new_random().expect("should work");
        let gs = get_global_state(&root_key);
        let engine = TestSyncEngine::new("bookmarks", EngineSyncAssociation::Disconnected);
        let cs = LocalCollStateMachine::get_state(&engine, &gs, &root_key).expect("should work");
        assert!(cs.is_some(), "collection can sync");
        assert_eq!(
            engine.assoc.replace(EngineSyncAssociation::Disconnected),
            EngineSyncAssociation::Connected(CollSyncIds {
                global: "syncIDAAAAAA".into(),
                coll: "syncIDBBBBBB".into(),
            })
        );
        assert_eq!(engine.get_num_resets(), 1);
    }

    #[test]
    fn test_known_wrong_state() {
        ensure_initialized();
        let root_key = KeyBundle::new_random().expect("should work");
        let gs = get_global_state(&root_key);
        let engine = TestSyncEngine::new(
            "bookmarks",
            EngineSyncAssociation::Connected(CollSyncIds {
                global: "syncIDXXXXXX".into(),
                coll: "syncIDYYYYYY".into(),
            }),
        );
        let cs = LocalCollStateMachine::get_state(&engine, &gs, &root_key).expect("should work");
        assert!(cs.is_some(), "collection can sync");
        assert_eq!(
            engine.assoc.replace(EngineSyncAssociation::Disconnected),
            EngineSyncAssociation::Connected(CollSyncIds {
                global: "syncIDAAAAAA".into(),
                coll: "syncIDBBBBBB".into(),
            })
        );
        assert_eq!(engine.get_num_resets(), 1);
    }

    #[test]
    fn test_known_good_state() {
        ensure_initialized();
        let root_key = KeyBundle::new_random().expect("should work");
        let gs = get_global_state(&root_key);
        let engine = TestSyncEngine::new(
            "bookmarks",
            EngineSyncAssociation::Connected(CollSyncIds {
                global: "syncIDAAAAAA".into(),
                coll: "syncIDBBBBBB".into(),
            }),
        );
        let cs = LocalCollStateMachine::get_state(&engine, &gs, &root_key).expect("should work");
        assert!(cs.is_some(), "collection can sync");
        assert_eq!(engine.get_num_resets(), 0);
    }

    #[test]
    fn test_declined() {
        ensure_initialized();
        let root_key = KeyBundle::new_random().expect("should work");
        let mut gs = get_global_state(&root_key);
        gs.global.declined.push("bookmarks".to_string());
        let engine = TestSyncEngine::new(
            "bookmarks",
            EngineSyncAssociation::Connected(CollSyncIds {
                global: "syncIDAAAAAA".into(),
                coll: "syncIDBBBBBB".into(),
            }),
        );
        let cs = LocalCollStateMachine::get_state(&engine, &gs, &root_key).expect("should work");
        assert!(cs.is_none(), "declined collection can sync");
        assert_eq!(engine.get_num_resets(), 0);
    }
}
