/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

//! Tests for clients with different engine choice models sharing an account,
//! as will happen while per-device engine choices roll out. These check what
//! "old" (per-account) and "new" (per-device) clients leave on the server for
//! each other - particularly when one of them starts afresh, which is when the
//! account's engine choices and sync IDs get rewritten.
//!
//! Note that these can't simulate Desktop - only the states Desktop would see.

use crate::auth::TestClient;
use crate::testing::TestGroup;
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use sync15::EngineChoicesModel::{self, Device};

const ENGINES: [&str; 2] = ["tabs", "passwords"];

fn engines() -> Vec<String> {
    ENGINES.iter().map(ToString::to_string).collect()
}

// Syncs tabs and passwords with the given engine choices model, returning the
// engines which actually synced. `EngineChoicesModel::default()` is a per-account
// client with no changes to make.
fn sync(c: &mut TestClient, model: EngineChoicesModel) -> HashSet<String> {
    c.engine_choices_model = model;
    let result = c.sync(&engines(), HashMap::new()).expect("should sync");
    result.successful.into_iter().collect()
}

// Like `sync`, but some engines are expected to fail, which are returned.
fn sync_failing(c: &mut TestClient, model: EngineChoicesModel) -> HashSet<String> {
    c.engine_choices_model = model;
    let failures = c
        .sync_with_failure(&engines(), HashMap::new())
        .expect("should sync");
    failures.into_keys().collect()
}

fn meta_global(c: &TestClient) -> Value {
    c.server_meta_global().expect("should fetch meta/global")
}

fn meta_syncids(c: &TestClient) -> Value {
    c.server_sync_ids().expect("should fetch meta/syncids")
}

fn global_sync_id(record: &Value, field: &str) -> String {
    record[field]
        .as_str()
        .expect("should have a global sync ID")
        .to_string()
}

fn set_tabs(enabled: bool) -> EngineChoicesModel {
    EngineChoicesModel::Account {
        enabled_changes: HashMap::from([("tabs".to_string(), enabled)]),
    }
}

fn sync_id(record: &Value, engine: &str) -> Option<String> {
    record["engines"][engine]["syncID"]
        .as_str()
        .map(ToString::to_string)
}

fn set_tabs_version(c: &TestClient, version: usize) {
    c.update_server_meta_global(|g| g["engines"]["tabs"]["version"] = version.into())
        .expect("should update meta/global");
}

// A per-device client's fresh start puts the engines it syncs in meta/global,
// even one the account had disabled, as long as it has never seen a
// meta/global. A per-account client then syncs them too.
fn test_per_device_fresh_start(c0: &mut TestClient, c1: &mut TestClient) {
    // c1 is a per-account client on which the user disabled tabs.
    sync(c1, set_tabs(false));

    c0.fully_wipe_server().expect("should wipe");
    sync(c0, Device);
    let global = meta_global(c0);
    let tabs_id = sync_id(&global, "tabs");
    assert!(tabs_id.is_some());
    assert_eq!(sync_id(&meta_syncids(c0), "tabs"), tabs_id);

    assert!(sync(c1, EngineChoicesModel::default()).contains("tabs"));
    assert_eq!(meta_global(c1), global);
}

// But a per-device client which has seen the account's choices republishes
// them - eg, after finding the server wiped after a node reassignment.
fn test_per_device_fresh_start_keeps_account_choices(c0: &mut TestClient, c1: &mut TestClient) {
    // c1 is a per-account client on which the user disabled tabs.
    sync(c1, set_tabs(false));
    sync(c0, Device);

    c0.fully_wipe_server().expect("should wipe");
    sync(c0, Device);

    let global = meta_global(c0);
    assert!(sync_id(&global, "tabs").is_none());
    assert!(global["declined"]
        .as_array()
        .expect("should have declined")
        .contains(&Value::from("tabs")));
    // c0 still syncs tabs, using meta/syncids.
    assert!(sync_id(&meta_syncids(c0), "tabs").is_some());

    assert!(!sync(c1, EngineChoicesModel::default()).contains("tabs"));
}

// Otherwise a per-device client never changes the account's choices - it syncs
// an engine the account disabled using a sync ID from meta/syncids. Switching
// back to per-account choices respects the account's choices again, and if the
// account enables the engine, it adopts the per-device client's sync ID.
fn test_per_device_leaves_account_choices(c0: &mut TestClient, c1: &mut TestClient) {
    // c1 is a per-account client on which the user disabled tabs.
    sync(c1, set_tabs(false));
    let global = meta_global(c1);

    assert!(sync(c0, Device).contains("tabs"));
    assert_eq!(meta_global(c0), global);
    let tabs_id = sync_id(&meta_syncids(c0), "tabs").expect("should have tabs");
    assert!(!sync(c1, EngineChoicesModel::default()).contains("tabs"));

    // Writing meta/syncids changed `meta`, so another sync is needed for c0
    // to have a setup it could reuse.
    assert!(sync(c0, Device).contains("tabs"));
    // Nothing changed on the server, but c0 must not reuse its per-device setup.
    assert!(!sync(c0, EngineChoicesModel::default()).contains("tabs"));
    assert_eq!(meta_global(c0), global);

    assert!(sync(c1, set_tabs(true)).contains("tabs"));
    assert_eq!(sync_id(&meta_global(c1), "tabs"), Some(tabs_id));
}

// When a per-account client starts afresh under a per-device one, the
// per-device client's sync IDs must follow the new meta/global, not the
// meta/syncids it had before.
fn test_per_account_fresh_start(c0: &mut TestClient, c1: &mut TestClient) {
    // c1 is a per-account client on which the user disabled tabs, so c0 syncs
    // them using meta/syncids.
    sync(c1, set_tabs(false));
    sync(c0, Device);
    let old_global = meta_global(c0);
    let old_tabs_id = sync_id(&meta_syncids(c0), "tabs").expect("should have tabs");

    // c1 starts afresh (as it would after a node reassignment).
    c1.fully_wipe_server().expect("should wipe");
    sync(c1, EngineChoicesModel::default());
    let global = meta_global(c1);
    assert_ne!(
        global_sync_id(&global, "syncID"),
        global_sync_id(&old_global, "syncID")
    );
    assert!(sync_id(&global, "tabs").is_none());

    // c0 still remembers the old setup, but notices it changed.
    assert!(sync(c0, Device).contains("tabs"));
    assert_eq!(meta_global(c0), global);
    let sync_ids = meta_syncids(c0);
    assert_eq!(
        global_sync_id(&sync_ids, "globalSyncID"),
        global_sync_id(&global, "syncID")
    );
    let tabs_id = sync_id(&sync_ids, "tabs").expect("should have tabs");
    assert_ne!(tabs_id, old_tabs_id);

    assert!(!sync(c1, EngineChoicesModel::default()).contains("tabs"));
}

// A per-account client switching to per-device engine choices starts syncing
// an engine the account disabled, and other per-device clients share its sync
// ID rather than minting their own.
fn test_switch_to_per_device(c0: &mut TestClient, c1: &mut TestClient) {
    sync(c1, set_tabs(false));
    // A first sync changes `meta`, so another is needed for c1 to have
    // a setup it could reuse.
    sync(c1, EngineChoicesModel::default());
    let global = meta_global(c1);

    // Nothing changed on the server, but c1 must not reuse its per-account setup.
    assert!(sync(c1, Device).contains("tabs"));
    assert_eq!(meta_global(c1), global);
    let tabs_id = sync_id(&meta_syncids(c1), "tabs").expect("should have tabs");

    assert!(sync(c0, Device).contains("tabs"));
    assert_eq!(sync_id(&meta_syncids(c0), "tabs"), Some(tabs_id));
    assert_eq!(meta_global(c0), global);
}

// An engine whose meta/global entry is for a newer version than ours fails
// with either model, without stopping the other engines.
fn test_newer_engine_version_fails(c0: &mut TestClient, c1: &mut TestClient) {
    sync(c1, EngineChoicesModel::default());
    set_tabs_version(c1, 2);
    let global = meta_global(c1);

    for (c, model) in [
        (&mut *c1, EngineChoicesModel::default()),
        (&mut *c0, Device),
    ] {
        assert_eq!(sync_failing(c, model), HashSet::from(["tabs".to_string()]));
    }
    assert_eq!(meta_global(c0), global);
}

// A per-device client replaces an entry in meta/global for an older version
// than ours, which per-account clients then use.
fn test_per_device_replaces_older_engine_version(c0: &mut TestClient, c1: &mut TestClient) {
    sync(c1, EngineChoicesModel::default());
    set_tabs_version(c1, 0);
    let old_tabs_id = sync_id(&meta_global(c1), "tabs");

    assert!(sync(c0, Device).contains("tabs"));
    let global = meta_global(c0);
    assert_eq!(global["engines"]["tabs"]["version"], 1);
    let tabs_id = sync_id(&global, "tabs");
    assert_ne!(tabs_id, old_tabs_id);
    assert_eq!(sync_id(&meta_syncids(c0), "tabs"), tabs_id);

    assert!(sync(c1, EngineChoicesModel::default()).contains("tabs"));
    assert_eq!(meta_global(c1), global);
}

// A per-device client neither uses nor changes meta/syncids in a newer format,
// so engines which need it fail, but those in meta/global still sync.
fn test_per_device_leaves_newer_sync_ids(c0: &mut TestClient, c1: &mut TestClient) {
    // c1 is a per-account client on which the user disabled tabs, so c0 syncs
    // them using meta/syncids.
    sync(c1, set_tabs(false));
    sync(c0, Device);
    c0.update_server_sync_ids(|r| {
        r["version"] = 2.into();
        r["someFutureField"] = true.into();
    })
    .expect("should update meta/syncids");
    let sync_ids = meta_syncids(c0);

    assert_eq!(
        sync_failing(c0, Device),
        HashSet::from(["tabs".to_string()])
    );
    assert_eq!(meta_syncids(c0), sync_ids);
}

pub fn get_test_group() -> TestGroup {
    TestGroup::new(
        "engine_choices",
        vec![
            ("test_per_device_fresh_start", test_per_device_fresh_start),
            (
                "test_per_device_fresh_start_keeps_account_choices",
                test_per_device_fresh_start_keeps_account_choices,
            ),
            (
                "test_per_device_leaves_account_choices",
                test_per_device_leaves_account_choices,
            ),
            ("test_per_account_fresh_start", test_per_account_fresh_start),
            ("test_switch_to_per_device", test_switch_to_per_device),
            (
                "test_newer_engine_version_fails",
                test_newer_engine_version_fails,
            ),
            (
                "test_per_device_replaces_older_engine_version",
                test_per_device_replaces_older_engine_version,
            ),
            (
                "test_per_device_leaves_newer_sync_ids",
                test_per_device_leaves_newer_sync_ids,
            ),
        ],
    )
}
