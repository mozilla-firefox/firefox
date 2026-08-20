/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::Store;
use std::sync::Arc;

impl Store {
    /// Returns a bridged sync engine for addresses, for use by Desktop's Sync
    /// framework. Constructing a `ConfigSyncEngine` only assembles structs and
    /// never touches the DB, so this cannot fail.
    pub fn addresses_bridged_engine(self: Arc<Self>) -> Arc<AddressesBridgedEngine> {
        let engine = crate::sync::address::create_engine(self);
        Arc::new(AddressesBridgedEngine::new(Box::new(engine)))
    }
}

// Generates the UniFFI-exposed `AddressesBridgedEngine`, a newtype around
// `sync15::engine::BridgedEngineWrapper`.
sync15::uniffi_bridged_engine!(AddressesBridgedEngine);

#[cfg(test)]
mod tests {
    use super::*;
    use crate::db::models::address::UpdatableAddressFields;
    use std::collections::HashMap;

    // Exercises the sync metadata the bridge owns: last_sync, sync_id and reset.
    #[test]
    fn test_sync_meta() {
        error_support::init_for_tests();

        let store = Arc::new(Store::new_shared_memory("addresses-bridge").unwrap());
        let bridge = store.addresses_bridged_engine();

        bridge.sync_started().unwrap();
        // Fresh DB: never synced.
        assert_eq!(bridge.last_sync().unwrap(), 0);
        bridge.set_uploaded(3, vec![]).unwrap();
        assert_eq!(bridge.last_sync().unwrap(), 3);

        assert!(bridge.sync_id().unwrap().is_none());

        bridge.ensure_current_sync_id("some_guid").unwrap();
        assert_eq!(bridge.sync_id().unwrap(), Some("some_guid".to_string()));
        // changing the sync ID resets the timestamp
        assert_eq!(bridge.last_sync().unwrap(), 0);
        bridge.set_uploaded(3, vec![]).unwrap();

        bridge.reset_sync_id().unwrap();
        assert_ne!(bridge.sync_id().unwrap(), Some("some_guid".to_string()));
        assert_eq!(bridge.last_sync().unwrap(), 0);
        bridge.set_uploaded(3, vec![]).unwrap();

        // `reset` clears the guid and the timestamp.
        bridge.reset().unwrap();
        assert_eq!(bridge.last_sync().unwrap(), 0);
        assert!(bridge.sync_id().unwrap().is_none());
    }

    // A roundtrip through the bridge's data path: stage an incoming remote
    // address, apply it, and confirm the local-only address comes back out for
    // upload. Unlike `test_sync_meta` this exercises the JSON (de)serialization
    // of BSOs and the sync staging tables, mirroring the logins and tabs
    // `test_sync_via_bridge` tests.
    #[test]
    fn test_sync_via_bridge() {
        error_support::init_for_tests();

        let store = Arc::new(Store::new_shared_memory("addresses-bridge-roundtrip").unwrap());

        // A local-only address: nothing on the server knows about it yet, so it
        // should be uploaded.
        let local = store
            .add_address(UpdatableAddressFields {
                name: "Local Person".to_string(),
                street_address: "1 Local Lane".to_string(),
                address_level2: "Seattle, WA".to_string(),
                country: "US".to_string(),
                ..Default::default()
            })
            .expect("should add local address");

        let bridge = store.clone().addresses_bridged_engine();

        // `sync_started` is what creates the sync staging tables.
        bridge.sync_started().expect("should prepare for sync");

        // An incoming remote address that isn't known locally. We build the
        // envelope as raw JSON, exactly as the JS bridge hands it to us.
        let incoming = vec![serde_json::json!({
            "id": "remote-only-bbbb",
            "modified": 0,
            "payload": serde_json::json!({
                "id": "remote-only-bbbb",
                "entry": {
                    "name": "Remote Person",
                    "street-address": "99 Remote Road",
                    "address-level2": "Portland, OR",
                    "country": "US",
                    "version": 1,
                },
            })
            .to_string(),
        })
        .to_string()];
        bridge
            .store_incoming(incoming)
            .expect("should store incoming");

        // Applying stores the remote record locally and returns the local-only
        // address for upload.
        let outgoing = bridge.apply(1234).expect("should apply");
        let changes: HashMap<String, serde_json::Value> = outgoing
            .into_iter()
            .map(|s| {
                let bso: serde_json::Value = serde_json::from_str(&s).unwrap();
                let payload: serde_json::Value =
                    serde_json::from_str(bso["payload"].as_str().unwrap()).unwrap();
                (payload["id"].as_str().unwrap().to_string(), payload)
            })
            .collect();

        // Only the local address is outgoing; the just-applied remote one is not
        // re-uploaded.
        assert_eq!(changes.len(), 1);
        assert_eq!(
            changes[&local.guid]["entry"]["street-address"],
            "1 Local Lane"
        );

        // The incoming remote address was actually persisted.
        let stored = store
            .get_address("remote-only-bbbb".to_string())
            .expect("remote address should have been stored");
        assert_eq!(stored.street_address, "99 Remote Road");

        assert_eq!(bridge.last_sync().unwrap(), 1234);
        bridge.set_uploaded(5678, vec![local.guid.clone()]).unwrap();
        bridge.sync_finished().unwrap();
        assert_eq!(bridge.last_sync().unwrap(), 5678);

        // Acknowledging the upload cleared the record's change counter, so a
        // subsequent sync has nothing to send.
        assert!(bridge.apply(5678).expect("should apply again").is_empty());
    }
}
