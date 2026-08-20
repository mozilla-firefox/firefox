/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::TabsRecord;
use crate::schema;
use crate::storage::{ClientRemoteTabs, TABS_CLIENT_TTL};
use crate::store::TabsStore;
use anyhow::Result;
use error_support::{debug, info, trace, warn};

use std::collections::HashMap;
use std::sync::{Arc, Mutex, RwLock, Weak};
use sync15::bso::{IncomingBso, OutgoingBso, OutgoingEnvelope};
use sync15::engine::{
    CollSyncIds, CollectionRequest, EngineSyncAssociation, SyncEngine, SyncEngineId,
};
use sync15::{telemetry, ClientData, CollectionName, RemoteClient, ServerTimestamp};
use sync_guid::Guid;

// Our "sync manager" will use whatever is stashed here.
lazy_static::lazy_static! {
    // Mutex: just taken long enough to update the inner stuff
    static ref STORE_FOR_MANAGER: Mutex<Weak<TabsStore>> = Mutex::new(Weak::new());
}

/// Called by the sync manager to get a sync engine via the store previously
/// registered with the sync manager.
pub fn get_registered_sync_engine(
    engine_id: &SyncEngineId,
) -> Option<Box<dyn sync15::engine::SyncEngine>> {
    let weak = STORE_FOR_MANAGER.lock().unwrap();
    match weak.upgrade() {
        None => None,
        Some(store) => match engine_id {
            SyncEngineId::Tabs => Some(Box::new(TabsEngine::new(Arc::clone(&store)))),
            // panicking here seems reasonable - it's a static error if this
            // it hit, not something that runtime conditions can influence.
            _ => unreachable!("can't provide unknown engine: {}", engine_id),
        },
    }
}

impl ClientRemoteTabs {
    pub(crate) fn from_record(
        client_id: String,
        last_modified: ServerTimestamp,
        remote_client: &RemoteClient,
        record: TabsRecord,
    ) -> Self {
        Self {
            client_id,
            client_name: remote_client.device_name.clone(),
            device_type: remote_client.device_type,
            last_modified: last_modified.as_millis(),
            remote_tabs: record.tabs.into_iter().map(Into::into).collect(),
            tab_groups: record
                .tab_groups
                .into_iter()
                .map(|(n, v)| (n, v.into()))
                .collect(),
            windows: record
                .windows
                .into_iter()
                .map(|(n, v)| (n, v.into()))
                .collect(),
        }
    }
}

// This is the implementation of syncing, which is used by the 2 different "sync engines"
// (We hope to get these 2 engines even closer in the future, but for now, we suck this up)
pub struct TabsEngine {
    pub(super) store: Arc<TabsStore>,
    // local_id is made public for use in examples/tabs-sync
    pub local_id: RwLock<String>,
}

impl TabsEngine {
    pub fn new(store: Arc<TabsStore>) -> Self {
        Self {
            store,
            local_id: Default::default(),
        }
    }

    // Internally owned last-sync - written internally from `apply`,
    // `set_uploaded` and `reset` but otherwise private.
    fn set_last_sync(&self, last_sync: ServerTimestamp) -> Result<()> {
        let mut storage = self.store.storage.lock().unwrap();
        debug!("Updating last sync to {}", last_sync);
        let last_sync_millis = last_sync.as_millis();
        Ok(storage.put_meta(schema::LAST_SYNC_META_KEY, &last_sync_millis)?)
    }
}

impl SyncEngine for TabsEngine {
    fn collection_name(&self) -> CollectionName {
        "tabs".into()
    }

    fn last_sync(&self) -> Result<Option<ServerTimestamp>> {
        let mut storage = self.store.storage.lock().unwrap();
        let millis = storage.get_meta::<i64>(schema::LAST_SYNC_META_KEY)?;
        Ok(millis.map(ServerTimestamp))
    }

    fn reset_last_sync(&self) -> Result<()> {
        self.set_last_sync(ServerTimestamp(0))
    }

    fn set_clients(&self, get_client_data: &dyn Fn() -> ClientData) -> Result<()> {
        let mut storage = self.store.storage.lock().unwrap();
        // We only know the client list at sync time, but need to return tabs potentially
        // at any time -- so we store the clients in the meta table to be able to properly
        // return a ClientRemoteTab struct
        let client_data = get_client_data();
        storage.put_meta(
            schema::REMOTE_CLIENTS_KEY,
            &serde_json::to_string(&client_data.recent_clients)?,
        )?;
        *self.local_id.write().unwrap() = client_data.local_client_id;
        Ok(())
    }

    fn stage_incoming(
        &self,
        inbound: Vec<IncomingBso>,
        telem: &mut telemetry::Engine,
    ) -> Result<()> {
        // We don't really "stage" records, we just apply them.
        let local_id = &*self.local_id.read().unwrap();
        let mut remote_tabs = Vec::with_capacity(inbound.len());

        let mut incoming_telemetry = telemetry::EngineIncoming::new();
        for incoming in inbound {
            if incoming.envelope.id == *local_id {
                // That's our own record, ignore it.
                continue;
            }
            let modified = incoming.envelope.modified;
            let record = match incoming.into_content::<TabsRecord>().content() {
                Some(record) => record,
                None => {
                    // Invalid record or a "tombstone" which tabs don't have.
                    warn!("Ignoring incoming invalid tab");
                    incoming_telemetry.failed(1);
                    continue;
                }
            };
            incoming_telemetry.applied(1);
            remote_tabs.push((record, modified));
        }
        telem.incoming(incoming_telemetry);
        let mut storage = self.store.storage.lock().unwrap();
        // In desktop we might end up here with zero records when doing a quick-write, in
        // which case we don't want to wipe the DB.
        if !remote_tabs.is_empty() {
            storage.replace_remote_tabs(&remote_tabs)?;
        }
        storage.remove_stale_clients()?;
        storage.remove_old_pending_closures(&remote_tabs)?;
        Ok(())
    }

    fn apply(
        &self,
        timestamp: ServerTimestamp,
        _telem: &mut telemetry::Engine,
    ) -> Result<Vec<OutgoingBso>> {
        // We've already applied them - we just need to fetch outgoing.
        let local_id = &*self.local_id.read().unwrap();
        // Timestamp will be zero when used as a "bridged" engine.
        if timestamp.0 != 0 {
            self.set_last_sync(timestamp)?;
        }

        let mut storage = self.store.storage.lock().unwrap();
        let remote_clients: HashMap<String, RemoteClient> = {
            match storage.get_meta::<String>(schema::REMOTE_CLIENTS_KEY)? {
                None => HashMap::default(),
                Some(json) => serde_json::from_str(&json).unwrap(),
            }
        };

        let Some(ref tabs_info) = *storage.local_tabs.borrow() else {
            // It's a less than ideal outcome if at startup (or any time) we are asked to
            // sync tabs before the app has told us what the tabs are, so make noise, but
            // don't actually write that we have no tabs.
            warn!("syncing without local tabs");
            return Ok(vec![]);
        };

        let client_name = remote_clients
            .get(local_id)
            .map(|client| client.device_name.clone())
            .unwrap_or_default();

        let mut record = TabsRecord {
            id: local_id.clone(),
            client_name,
            tabs: tabs_info
                .tabs
                .iter()
                .map(Clone::clone)
                .map(Into::into)
                .collect(),
            windows: tabs_info
                .windows
                .iter()
                .map(|(n, v)| (n.clone(), v.clone().into()))
                .collect(),
            tab_groups: tabs_info
                .tab_groups
                .iter()
                .map(|(n, v)| (n.clone(), v.clone().into()))
                .collect(),
        };
        super::prepare_for_upload(&mut record);

        trace!("outgoing {record}");
        let envelope = OutgoingEnvelope {
            id: local_id.as_str().into(),
            ttl: Some(TABS_CLIENT_TTL),
            ..Default::default()
        };
        // XXX - outgoing telem?
        Ok(vec![OutgoingBso::from_content(envelope, record)?])
    }

    fn set_uploaded(&self, new_timestamp: ServerTimestamp, ids: Vec<Guid>) -> Result<()> {
        info!("sync uploaded {} records", ids.len());
        self.set_last_sync(new_timestamp)?;
        Ok(())
    }

    fn get_collection_request(
        &self,
        server_timestamp: ServerTimestamp,
    ) -> Result<Option<CollectionRequest>> {
        let since = self.last_sync()?.unwrap_or_default();
        Ok(if since == server_timestamp {
            None
        } else {
            Some(
                CollectionRequest::new("tabs".into())
                    .full()
                    .newer_than(since),
            )
        })
    }

    fn reset(&self, assoc: &EngineSyncAssociation) -> Result<()> {
        self.set_last_sync(ServerTimestamp(0))?;
        let mut storage = self.store.storage.lock().unwrap();
        storage.delete_meta(schema::REMOTE_CLIENTS_KEY)?;
        storage.wipe_remote_tabs()?;
        match assoc {
            EngineSyncAssociation::Disconnected => {
                storage.delete_meta(schema::GLOBAL_SYNCID_META_KEY)?;
                storage.delete_meta(schema::COLLECTION_SYNCID_META_KEY)?;
            }
            EngineSyncAssociation::Connected(ids) => {
                storage.put_meta(schema::GLOBAL_SYNCID_META_KEY, &ids.global.to_string())?;
                storage.put_meta(schema::COLLECTION_SYNCID_META_KEY, &ids.coll.to_string())?;
            }
        };
        Ok(())
    }

    fn wipe(&self) -> Result<()> {
        self.reset(&EngineSyncAssociation::Disconnected)?;
        // not clear why we need to wipe the local tabs - the app is just going
        // to re-add them?
        self.store.storage.lock().unwrap().wipe_local_tabs();
        Ok(())
    }

    fn get_sync_assoc(&self) -> Result<EngineSyncAssociation> {
        let mut storage = self.store.storage.lock().unwrap();
        let global = storage.get_meta::<String>(schema::GLOBAL_SYNCID_META_KEY)?;
        let coll = storage.get_meta::<String>(schema::COLLECTION_SYNCID_META_KEY)?;
        Ok(if let (Some(global), Some(coll)) = (global, coll) {
            EngineSyncAssociation::Connected(CollSyncIds {
                global: Guid::from_string(global),
                coll: Guid::from_string(coll),
            })
        } else {
            EngineSyncAssociation::Disconnected
        })
    }
}

impl crate::TabsStore {
    // This allows the embedding app to say "make this instance available to
    // the sync manager". The implementation is more like "offer to sync mgr"
    // (thereby avoiding us needing to link with the sync manager) but
    // `register_with_sync_manager()` is logically what's happening so that's
    // the name it gets.
    pub fn register_with_sync_manager(self: Arc<Self>) {
        let mut state = STORE_FOR_MANAGER.lock().unwrap();
        *state = Arc::downgrade(&self);
    }
}

#[cfg(test)]
pub mod test {
    use super::*;
    use crate::DeviceType;
    use serde_json::json;
    use sync15::bso::IncomingBso;

    #[test]
    fn test_incoming_tabs() {
        error_support::init_for_tests();

        let engine = TabsEngine::new(Arc::new(TabsStore::new_with_mem_path("test-incoming")));

        let client_data = ClientData {
            local_client_id: "my-device".to_string(),
            recent_clients: HashMap::from([
                (
                    "my-device".to_string(),
                    RemoteClient {
                        fxa_device_id: None,
                        device_name: "my device".to_string(),
                        device_type: sync15::DeviceType::Unknown,
                    },
                ),
                (
                    "device-no-tabs".to_string(),
                    RemoteClient {
                        fxa_device_id: None,
                        device_name: "device with no tabs".to_string(),
                        device_type: DeviceType::Unknown,
                    },
                ),
                (
                    "device-with-a-tab".to_string(),
                    RemoteClient {
                        fxa_device_id: None,
                        device_name: "device with an updated tab".to_string(),
                        device_type: DeviceType::Unknown,
                    },
                ),
            ]),
        };
        engine
            .set_clients(&|| client_data.clone())
            .expect("should work");

        let records = vec![
            json!({
                "id": "device-no-tabs",
                "clientName": "device with no tabs",
                "tabs": [],
            }),
            json!({
                "id": "device-with-a-tab",
                "clientName": "device with a tab",
                "tabs": [{
                    "title": "the title",
                    "urlHistory": [
                        "https://mozilla.org/"
                    ],
                    "icon": "https://mozilla.org/icon",
                    "lastUsed": 1643764207
                }]
            }),
            json!({
                "id": "device-with-a-tab",
                "clientName": "device with an updated tab",
                "tabs": [{
                    "title": "the new title",
                    "urlHistory": [
                        "https://mozilla.org/"
                    ],
                    "icon": "https://mozilla.org/icon",
                    "lastUsed": 1643764208
                }]
            }),
            // This has the main payload as OK but the tabs part invalid.
            json!({
                "id": "device-with-invalid-tab",
                "clientName": "device with a tab",
                "tabs": [{
                    "foo": "bar",
                }]
            }),
            // We want this to be a valid payload but an invalid tab - so it needs an ID.
            json!({
                "id": "invalid-tab",
                "foo": "bar"
            }),
        ];

        let mut telem = telemetry::Engine::new("tabs");
        let incoming = records
            .into_iter()
            .map(IncomingBso::from_test_content)
            .collect();
        engine
            .stage_incoming(incoming, &mut telem)
            .expect("Should apply incoming and stage outgoing records");
        let outgoing = engine
            .apply(ServerTimestamp(0), &mut telem)
            .expect("should apply");

        assert!(outgoing.is_empty());

        // now check the store has what we think it has.
        let mut storage = engine.store.storage.lock().unwrap();
        let mut crts = storage.get_remote_tabs().expect("should work");
        crts.sort_by(|a, b| a.client_name.partial_cmp(&b.client_name).unwrap());
        assert_eq!(crts.len(), 2, "we currently include devices with no tabs");
        let crt = &crts[0];
        assert_eq!(crt.client_name, "device with an updated tab");
        assert_eq!(crt.device_type, DeviceType::Unknown);
        assert_eq!(crt.remote_tabs.len(), 1);
        assert_eq!(crt.remote_tabs[0].title, "the new title");

        let crt = &crts[1];
        assert_eq!(crt.client_name, "device with no tabs");
        assert_eq!(crt.device_type, DeviceType::Unknown);
        assert_eq!(crt.remote_tabs.len(), 0);
    }

    #[test]
    fn test_no_incoming_doesnt_write() {
        error_support::init_for_tests();

        let engine = TabsEngine::new(Arc::new(TabsStore::new_with_mem_path(
            "test_no_incoming_doesnt_write",
        )));

        let client_data = ClientData {
            local_client_id: "my-device".to_string(),
            recent_clients: HashMap::from([(
                "device-with-a-tab".to_string(),
                RemoteClient {
                    fxa_device_id: None,
                    device_name: "device-with-a-tab".to_string(),
                    device_type: DeviceType::Unknown,
                },
            )]),
        };
        engine
            .set_clients(&|| client_data.clone())
            .expect("should work");

        let records = vec![json!({
            "id": "device-with-a-tab",
            "clientName": "device with a tab",
            "tabs": [{
                "title": "the title",
                "urlHistory": [
                    "https://mozilla.org/"
                ],
                "icon": "https://mozilla.org/icon",
                "lastUsed": 1643764207
            }]
        })];

        let mut telem = telemetry::Engine::new("tabs");
        let incoming = records
            .into_iter()
            .map(IncomingBso::from_test_content)
            .collect();
        engine
            .stage_incoming(incoming, &mut telem)
            .expect("Should apply incoming and stage outgoing records");
        engine
            .apply(ServerTimestamp(0), &mut telem)
            .expect("should apply");

        // now check the store has what we think it has.
        {
            let mut storage = engine.store.storage.lock().unwrap();
            assert_eq!(storage.get_remote_tabs().expect("should work").len(), 1);
        }

        // Now another sync with zero incoming records, should still be able to get back
        // our one client.
        engine
            .stage_incoming(vec![], &mut telemetry::Engine::new("tabs"))
            .expect("Should succeed applying zero records");

        {
            let mut storage = engine.store.storage.lock().unwrap();
            assert_eq!(storage.get_remote_tabs().expect("should work").len(), 1);
        }
    }

    #[test]
    fn test_sync_manager_registration() {
        let store = Arc::new(TabsStore::new_with_mem_path("test-registration"));
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
    fn test_apply_timestamp() {
        error_support::init_for_tests();

        let engine = TabsEngine::new(Arc::new(TabsStore::new_with_mem_path(
            "test-apply-timestamp",
        )));

        let records = vec![json!({
            "id": "device-no-tabs",
            "clientName": "device with no tabs",
            "tabs": [],
        })];

        let mut telem = telemetry::Engine::new("tabs");
        engine
            .set_last_sync(ServerTimestamp::from_millis(123))
            .unwrap();
        let incoming = records
            .into_iter()
            .map(IncomingBso::from_test_content)
            .collect();
        engine
            .stage_incoming(incoming, &mut telem)
            .expect("Should apply incoming and stage outgoing records");
        engine
            .apply(ServerTimestamp(0), &mut telem)
            .expect("should apply");

        assert_eq!(
            engine
                .last_sync()
                .expect("should work")
                .expect("should have a value"),
            ServerTimestamp::from_millis(123),
            "didn't set a zero timestamp"
        )
    }
}
