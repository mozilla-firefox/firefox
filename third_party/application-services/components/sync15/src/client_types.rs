/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! This module has to be here because of some hard-to-avoid hacks done for the
//! tabs engine... See issue #2590

use crate::DeviceType;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Argument to `SyncEngine::set_clients` - a leaky abstraction of fxa/sync
/// device ids. These are "short term" IDs in that they don't survive reauth
/// etc, so used for "recent" things like open tabs.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct ClientData {
    pub local_client_id: String,
    /// A hashmap of records in the `clients` collection. Key is the id of the record in
    /// that collection, which may or may not be the device's fxa_device_id.
    pub recent_clients: HashMap<String, RemoteClient>,
}

/// Information about a remote client in the clients collection.
#[derive(Clone, Debug, Eq, Hash, PartialEq, Serialize, Deserialize)]
pub struct RemoteClient {
    pub fxa_device_id: Option<String>,
    pub device_name: String,
    #[serde(default)]
    pub device_type: DeviceType,
}

#[cfg(test)]
mod client_types_tests {
    use super::*;

    #[test]
    fn test_remote_client() {
        // Missing `device_type` gets DeviceType::Unknown.
        let dt = serde_json::from_str::<RemoteClient>("{\"device_name\": \"foo\"}").unwrap();
        assert_eq!(dt.device_type, DeviceType::Unknown);
        // But reserializes as null.
        assert_eq!(
            serde_json::to_string(&dt).unwrap(),
            "{\"fxa_device_id\":null,\"device_name\":\"foo\",\"device_type\":null}"
        );

        // explicit null is also unknown.
        assert_eq!(
            serde_json::from_str::<RemoteClient>(
                "{\"device_name\": \"foo\", \"device_type\": null}",
            )
            .unwrap()
            .device_type,
            DeviceType::Unknown
        );

        // Unknown device_type string deserializes as DeviceType::Unknown.
        let dt = serde_json::from_str::<RemoteClient>(
            "{\"device_name\": \"foo\", \"device_type\": \"foo\"}",
        )
        .unwrap();
        assert_eq!(dt.device_type, DeviceType::Unknown);
        // The None gets re-serialized as null.
        assert_eq!(
            serde_json::to_string(&dt).unwrap(),
            "{\"fxa_device_id\":null,\"device_name\":\"foo\",\"device_type\":null}"
        );

        // DeviceType::Unknown gets serialized as null.
        let dt = RemoteClient {
            device_name: "bar".to_string(),
            fxa_device_id: None,
            device_type: DeviceType::Unknown,
        };
        assert_eq!(
            serde_json::to_string(&dt).unwrap(),
            "{\"fxa_device_id\":null,\"device_name\":\"bar\",\"device_type\":null}"
        );

        // DeviceType::Desktop gets serialized as "desktop".
        let dt = RemoteClient {
            device_name: "bar".to_string(),
            fxa_device_id: Some("fxa".to_string()),
            device_type: DeviceType::Desktop,
        };
        assert_eq!(
            serde_json::to_string(&dt).unwrap(),
            "{\"fxa_device_id\":\"fxa\",\"device_name\":\"bar\",\"device_type\":\"desktop\"}"
        );
    }

    #[test]
    fn test_client_data() {
        let client_data = ClientData {
            local_client_id: "my-device".to_string(),
            recent_clients: HashMap::from([
                (
                    "my-device".to_string(),
                    RemoteClient {
                        fxa_device_id: None,
                        device_name: "my device".to_string(),
                        device_type: DeviceType::Unknown,
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
                        device_name: "device with a tab".to_string(),
                        device_type: DeviceType::Desktop,
                    },
                ),
            ]),
        };
        //serialize
        let client_data_ser = serde_json::to_string(&client_data).unwrap();
        println!("SER: {}", client_data_ser);
        // deserialize
        let client_data_des: ClientData = serde_json::from_str(&client_data_ser).unwrap();
        assert_eq!(client_data_des, client_data);
    }
}
