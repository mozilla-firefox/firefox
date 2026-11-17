/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::warn;
use serde_derive::*;
use std::collections::HashMap;
use sync_guid::Guid;

// Known record formats.

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Eq)]
pub struct MetaGlobalEngine {
    pub version: usize,
    #[serde(rename = "syncID")]
    pub sync_id: Guid,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct MetaGlobalRecord {
    #[serde(rename = "syncID")]
    pub sync_id: Guid,
    #[serde(rename = "storageVersion")]
    pub storage_version: usize,
    #[serde(default)]
    pub engines: HashMap<String, MetaGlobalEngine>,
    #[serde(default)]
    pub declined: Vec<String>,
}

/// The only version of `meta/syncids` we understand.
pub const SYNC_IDS_RECORD_VERSION: usize = 1;

/// The `meta/syncids` record, a registry of per-engine sync IDs written by
/// clients which allow engines to be enabled or disabled per device. Such
/// clients don't add engines to `meta/global`, so this is where we look for an
/// engine's sync ID before minting a new one - minting would force all of them
/// to reset the engine.
/// When using per-account engine choices we only ever read it; when using
/// per-device choices, we maintain it too.
#[derive(Serialize, Deserialize, Debug, Clone, Default, PartialEq)]
pub struct SyncIdsRecord {
    #[serde(default)]
    pub version: usize,
    /// The `meta/global` sync ID this record was written under.
    #[serde(
        rename = "globalSyncID",
        default,
        skip_serializing_if = "Option::is_none"
    )]
    pub global_sync_id: Option<Guid>,
    #[serde(default)]
    pub engines: HashMap<String, MetaGlobalEngine>,
    /// Fields we don't know about, preserved when we rewrite the record, as
    /// Desktop does.
    #[serde(flatten)]
    pub extra: serde_json::Map<String, serde_json::Value>,
}

impl SyncIdsRecord {
    /// Parses a record, returning `None` if we can't. If we fail, attempt
    /// to at least get a `version` so we can succeed well enough so our
    /// callers know it isn't compatible and why.
    pub fn from_payload(payload: &str) -> Option<Self> {
        let err = match serde_json::from_str(payload) {
            Ok(r) => return Some(r),
            Err(e) => e,
        };
        #[derive(Deserialize)]
        struct Version {
            #[serde(default)]
            version: usize,
        }
        match serde_json::from_str::<Version>(payload) {
            Ok(Version { version }) if version > SYNC_IDS_RECORD_VERSION => Some(Self {
                version,
                ..Default::default()
            }),
            _ => {
                warn!("Ignoring meta/syncids which failed to parse: {}", err);
                None
            }
        }
    }

    /// A new, empty record for the given `meta/global`.
    pub fn new_for(global: &MetaGlobalRecord) -> Self {
        Self {
            version: SYNC_IDS_RECORD_VERSION,
            global_sync_id: Some(global.sync_id.clone()),
            ..Default::default()
        }
    }

    /// Whether this record was written by a newer client, in a format we don't
    /// understand. This engine will not sync until we're upgraded.
    pub fn is_newer_format(&self) -> bool {
        self.version > SYNC_IDS_RECORD_VERSION
    }

    /// Whether this record is one we understand and matches `meta/global`.
    /// Records which aren't, but aren't in a newer format either,
    /// were written for a previous `meta/global` (or are broken), so are safe
    /// to replace.
    pub fn is_usable_for(&self, global: &MetaGlobalRecord) -> bool {
        self.version == SYNC_IDS_RECORD_VERSION
            && self.global_sync_id.as_ref() == Some(&global.sync_id)
    }

    /// Returns the entry for an engine if it's safe to adopt it for the given
    /// `meta/global`, whatever its engine version. These rules must match
    /// Desktop's.
    pub fn usable_entry(&self, global: &MetaGlobalRecord, name: &str) -> Option<&MetaGlobalEngine> {
        if !self.is_usable_for(global) {
            return None;
        }
        self.engines.get(name).filter(|e| !e.sync_id.is_empty())
    }
}

#[derive(Deserialize, Serialize, Clone, Debug, Eq, PartialEq)]
pub struct CryptoKeysRecord {
    pub id: Guid,
    pub collection: String,
    pub default: [String; 2],
    pub collections: HashMap<String, [String; 2]>,
}

#[cfg(test)]
#[test]
fn test_deserialize_meta_global() {
    let record = serde_json::json!({
        "syncID": "abcd1234abcd",
        "storageVersion": 1,
    })
    .to_string();
    let r = serde_json::from_str::<MetaGlobalRecord>(&record).unwrap();
    assert_eq!(r.sync_id, "abcd1234abcd");
    assert_eq!(r.storage_version, 1);
    assert_eq!(r.engines.len(), 0);
    assert_eq!(r.declined.len(), 0);
}

#[cfg(test)]
mod sync_ids_tests {
    use super::*;

    fn global() -> MetaGlobalRecord {
        MetaGlobalRecord {
            sync_id: "globalAAAAAA".into(),
            storage_version: 5,
            engines: HashMap::new(),
            declined: vec![],
        }
    }

    fn parse(v: serde_json::Value) -> SyncIdsRecord {
        serde_json::from_value(v).unwrap()
    }

    #[test]
    fn test_usable_entry() {
        let r = parse(serde_json::json!({
            "version": 1,
            "globalSyncID": "globalAAAAAA",
            "engines": {
                "bookmarks": {"version": 2, "syncID": "bookmarksAAA"},
                "empty": {"version": 1, "syncID": ""},
            },
            "someFutureField": true,
        }));
        let entry = r.usable_entry(&global(), "bookmarks").unwrap();
        assert_eq!(entry.sync_id, "bookmarksAAA");
        assert_eq!(entry.version, 2);
        // no such engine
        assert!(r.usable_entry(&global(), "history").is_none());
        // empty syncID
        assert!(r.usable_entry(&global(), "empty").is_none());
    }

    #[test]
    fn test_round_trip_keeps_unknown_fields() {
        let json = serde_json::json!({
            "version": 1,
            "globalSyncID": "globalAAAAAA",
            "engines": {"bookmarks": {"version": 2, "syncID": "bookmarksAAA"}},
            "someFutureField": {"a": 1},
        });
        let r = parse(json.clone());
        assert_eq!(serde_json::to_value(&r).unwrap(), json);
    }

    #[test]
    fn test_new_for() {
        let r = SyncIdsRecord::new_for(&global());
        assert!(r.is_usable_for(&global()));
        assert_eq!(
            serde_json::to_value(&r).unwrap(),
            serde_json::json!({"version": 1, "globalSyncID": "globalAAAAAA", "engines": {}})
        );
    }

    #[test]
    fn test_unusable_records() {
        let engines = serde_json::json!({"bookmarks": {"version": 2, "syncID": "bookmarksAAA"}});
        for v in [
            // another account (or another generation of this one)
            serde_json::json!({"version": 1, "globalSyncID": "globalBBBBBB", "engines": engines}),
            // missing globalSyncID
            serde_json::json!({"version": 1, "engines": engines}),
            // unknown record versions
            serde_json::json!({"version": 2, "globalSyncID": "globalAAAAAA", "engines": engines}),
            serde_json::json!({"globalSyncID": "globalAAAAAA", "engines": engines}),
        ] {
            let r = parse(v);
            assert!(!r.is_usable_for(&global()));
            assert!(r.usable_entry(&global(), "bookmarks").is_none());
        }
        // and no engines at all is fine.
        let r = parse(serde_json::json!({"version": 1, "globalSyncID": "globalAAAAAA"}));
        assert!(r.usable_entry(&global(), "bookmarks").is_none());
    }

    #[test]
    fn test_from_payload() {
        // A newer client changed the shape of the record, but we know it's newer.
        let r = SyncIdsRecord::from_payload(r#"{"version": 2, "engines": ["tabs"]}"#).unwrap();
        assert!(r.is_newer_format());
        // One in our format which we can't parse is ignored...
        assert!(SyncIdsRecord::from_payload(r#"{"version": 1, "engines": ["tabs"]}"#).is_none());
        // ...as is one without a version, which is broken rather than newer.
        let r = SyncIdsRecord::from_payload(r#"{"globalSyncID": "globalAAAAAA"}"#).unwrap();
        assert!(!r.is_newer_format());
    }
}
