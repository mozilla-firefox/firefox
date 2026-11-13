/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Fixtures mirroring toolkit/components/contextualidentity/tests/unit/test_migratedFile.js,
//! so that both implementations are pinned to the same corpus.

use serde_json::{json, Value};

use super::{parse, serialize};
use crate::data::{ContainersData, Identity, LATEST_VERSION, MAX_USER_CONTEXT_ID};
use crate::defaults::{defaults, WEBEXT_STORAGE_LOCAL_IDENTITY_NAME};
use crate::error::ParseError;

fn bytes(document: Value) -> Vec<u8> {
    serde_json::to_vec(&document).unwrap()
}

fn load(document: Value) -> ContainersData {
    parse(&bytes(document)).expect("fixture should load").0
}

/// The four shipped identities as stored before version 5.
fn string_bundle_defaults() -> Vec<Value> {
    vec![
        json!({
            "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue",
            "l10nID": "userContextPersonal.label", "accessKey": "userContextPersonal.accesskey"
        }),
        json!({
            "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange",
            "l10nID": "userContextWork.label", "accessKey": "userContextWork.accesskey"
        }),
        json!({
            "userContextId": 3, "public": true, "icon": "dollar", "color": "green",
            "l10nID": "userContextBanking.label", "accessKey": "userContextBanking.accesskey"
        }),
        json!({
            "userContextId": 4, "public": true, "icon": "cart", "color": "pink",
            "l10nID": "userContextShopping.label", "accessKey": "userContextShopping.accesskey"
        }),
    ]
}

/// The same four identities from version 5 to version 6.
fn fluent_defaults() -> Vec<Value> {
    vec![
        json!({ "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue", "l10nId": "user-context-personal" }),
        json!({ "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange", "l10nId": "user-context-work" }),
        json!({ "userContextId": 3, "public": true, "icon": "dollar", "color": "green", "l10nId": "user-context-banking" }),
        json!({ "userContextId": 4, "public": true, "icon": "cart", "color": "pink", "l10nId": "user-context-shopping" }),
    ]
}

/// The same four identities at version 7, where the Fluent identifiers had been
/// renamed but were still stored.
fn renamed_fluent_defaults() -> Vec<Value> {
    vec![
        json!({ "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue", "l10nId": "user-context-personal2" }),
        json!({ "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange", "l10nId": "user-context-work2" }),
        json!({ "userContextId": 3, "public": true, "icon": "dollar", "color": "green", "l10nId": "user-context-banking2" }),
        json!({ "userContextId": 4, "public": true, "icon": "cart", "color": "pink", "l10nId": "user-context-shopping2" }),
    ]
}

/// The same four identities from version 8 on, where a label the user has not
/// replaced is not stored at all.
fn unlabelled_defaults() -> Vec<Value> {
    vec![
        json!({ "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue" }),
        json!({ "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange" }),
        json!({ "userContextId": 3, "public": true, "icon": "dollar", "color": "green" }),
        json!({ "userContextId": 4, "public": true, "icon": "cart", "color": "pink" }),
    ]
}

fn thumbnail(legacy: bool) -> Value {
    let mut identity = json!({
        "userContextId": 5, "public": false, "icon": "", "color": "",
        "name": "userContextIdInternal.thumbnail"
    });
    if legacy {
        identity["accessKey"] = json!("");
    }
    identity
}

fn webext_storage_local(legacy: bool) -> Value {
    let mut identity = json!({
        "userContextId": MAX_USER_CONTEXT_ID, "public": false, "icon": "", "color": "",
        "name": WEBEXT_STORAGE_LOCAL_IDENTITY_NAME
    });
    if legacy {
        identity["accessKey"] = json!("");
    }
    identity
}

fn custom_identity(user_context_id: u32, color: &str, name: &str) -> Value {
    json!({
        "userContextId": user_context_id, "public": true, "icon": "gift",
        "color": color, "name": name
    })
}

fn named<'a>(data: &'a ContainersData, name: &str) -> &'a Identity {
    data.identities
        .iter()
        .find(|identity| identity.name.as_deref() == Some(name))
        .expect("identity should exist")
}

fn with_id(data: &ContainersData, user_context_id: u32) -> &Identity {
    data.identities
        .iter()
        .find(|identity| identity.user_context_id == user_context_id)
        .expect("identity should exist")
}

/// From version 8 on no identity carries a Fluent identifier: the labels of the
/// defaults live in the code.
fn assert_no_stored_labels(data: &ContainersData) {
    assert!(data
        .identities
        .iter()
        .all(|identity| !identity.extra.contains_key("l10nId")));
}

#[test]
fn version_1_has_no_migration_path() {
    let error = parse(&bytes(json!({
        "version": 1,
        "lastUserContextId": 6,
        "identities": [custom_identity(6, "purple", "Custom user-created identity")],
    })))
    .expect_err("version 1 should be rejected");

    assert!(matches!(error, ParseError::UnsupportedVersion(1)));
}

#[test]
fn version_2_runs_the_whole_chain() {
    let mut identities = string_bundle_defaults();
    identities.push(thumbnail(true));
    identities.push(custom_identity(6, "pink", "Custom user-created identity"));

    let data = load(json!({
        "version": 2,
        "lastUserContextId": 6,
        "identities": identities,
    }));

    assert_eq!(data.version, LATEST_VERSION);
    assert_eq!(data.public_identities().count(), 5);
    assert!(data
        .find_private_by_name(WEBEXT_STORAGE_LOCAL_IDENTITY_NAME)
        .is_some());
    assert!(data.identities.iter().all(|identity| {
        !identity.extra.contains_key("l10nID") && !identity.extra.contains_key("accessKey")
    }));
    assert_no_stored_labels(&data);
}

#[test]
fn version_3_adds_the_reserved_identity_and_drops_the_labels() {
    let mut identities = string_bundle_defaults();
    identities.push(thumbnail(true));
    identities.push(custom_identity(6, "purple", "Custom user-created identity"));

    let data = load(json!({
        "version": 3,
        "lastUserContextId": 6,
        "identities": identities,
    }));

    let reserved = data
        .find_private_by_name(WEBEXT_STORAGE_LOCAL_IDENTITY_NAME)
        .expect("3 -> 4 adds the reserved extension storage identity");
    assert_eq!(reserved.user_context_id, MAX_USER_CONTEXT_ID);

    assert_no_stored_labels(&data);
    assert_eq!(data.public_identities().count(), 5);
    assert!(data.site_associations.is_empty());
}

#[test]
fn version_4_does_not_duplicate_the_reserved_identity() {
    let mut identities = string_bundle_defaults();
    identities.push(thumbnail(true));
    identities.push(webext_storage_local(true));
    identities.push(custom_identity(6, "purple", "Custom user-created identity"));

    let data = load(json!({
        "version": 4,
        "lastUserContextId": 6,
        "identities": identities,
    }));

    assert_eq!(
        data.identities
            .iter()
            .filter(|identity| identity.user_context_id == MAX_USER_CONTEXT_ID)
            .count(),
        1
    );
    assert_no_stored_labels(&data);
}

#[test]
fn version_5_resolves_color_aliases() {
    let mut identities = fluent_defaults();
    identities.push(thumbnail(false));
    identities.push(webext_storage_local(false));
    identities.push(custom_identity(6, "turquoise", "Aliased to cyan"));
    identities.push(custom_identity(7, "toolbar", "Aliased to gray"));

    let data = load(json!({
        "version": 5,
        "lastUserContextId": 7,
        "identities": identities,
    }));

    assert_eq!(named(&data, "Aliased to cyan").color, "cyan");
    assert_eq!(named(&data, "Aliased to gray").color, "gray");
    assert_eq!(with_id(&data, 1).color, "blue");
    // The system identities have no color to resolve.
    assert_eq!(named(&data, "userContextIdInternal.thumbnail").color, "");
}

/// Version 7 renamed the Fluent identifiers and version 8 stopped storing them,
/// so a version 6 document loses them outright.
#[test]
fn version_6_drops_the_stored_labels() {
    let mut identities = fluent_defaults();
    identities.push(thumbnail(false));
    identities.push(webext_storage_local(false));
    identities.push(custom_identity(6, "purple", "Custom user-created identity"));

    let data = load(json!({
        "version": 6,
        "lastUserContextId": 6,
        "identities": identities,
    }));

    assert_eq!(data.version, LATEST_VERSION);
    assert_no_stored_labels(&data);
    // A name the user chose is not a label the code owns, so it stays.
    assert_eq!(
        named(&data, "Custom user-created identity").user_context_id,
        6
    );
}

/// A version 7 document carries the renamed identifiers; version 8 drops them
/// in favour of the code's default identities.
#[test]
fn version_7_drops_the_stored_labels() {
    let mut identities = renamed_fluent_defaults();
    identities.push(thumbnail(false));
    identities.push(webext_storage_local(false));
    // A default the user renamed keeps its name and never had an identifier.
    identities[3] = custom_identity(4, "pink", "Renamed default");

    let data = load(json!({
        "version": 7,
        "lastUserContextId": 5,
        "identities": identities,
    }));

    assert_eq!(data.version, LATEST_VERSION);
    assert_no_stored_labels(&data);
    assert_eq!(named(&data, "Renamed default").user_context_id, 4);
}

#[test]
fn version_8_is_loaded_verbatim() {
    let mut identities = unlabelled_defaults();
    identities.push(thumbnail(false));
    identities.push(webext_storage_local(false));
    identities.push(custom_identity(6, "purple", "Custom user-created identity"));

    let (data, migrated) = parse(&bytes(json!({
        "version": 8,
        "lastUserContextId": 6,
        "identities": identities,
        "siteAssociations": { "example.org": 1, "example.com": 6 },
    })))
    .expect("current version should load");

    assert!(!migrated);
    assert_eq!(data.public_identities().count(), 5);
    assert_eq!(named(&data, "Custom user-created identity").color, "purple");
    assert_eq!(data.site_associations.get("example.org"), Some(&1));
    assert_eq!(data.site_associations.get("example.com"), Some(&6));
    assert_eq!(data.site_associations.get("unassociated.example"), None);
}

#[test]
fn migrated_documents_report_that_they_need_a_write() {
    let (_, migrated) = parse(&bytes(json!({
        "version": 5,
        "lastUserContextId": 5,
        "identities": fluent_defaults(),
    })))
    .unwrap();

    assert!(migrated);
}

#[test]
fn a_version_from_the_future_is_rejected() {
    let error = parse(&bytes(json!({
        "version": LATEST_VERSION + 1,
        "lastUserContextId": 6,
        "identities": unlabelled_defaults(),
        "siteAssociations": { "example.org": 1 },
    })))
    .expect_err("an unknown version should be rejected");

    assert!(matches!(
        error,
        ParseError::UnsupportedVersion(version) if version == LATEST_VERSION + 1
    ));
}

#[test]
fn malformed_data_is_rejected() {
    let error = parse(b"{ vers").expect_err("malformed data should be rejected");
    assert!(matches!(error, ParseError::Malformed(_)));
}

#[test]
fn unknown_fields_survive_a_round_trip() {
    let mut identity = custom_identity(6, "purple", "Custom user-created identity");
    identity["guid"] = json!("a-stable-identifier");

    let mut identities = unlabelled_defaults();
    identities.push(identity);

    let data = load(json!({
        "version": 8,
        "lastUserContextId": 6,
        "identities": identities,
        "unknownTopLevelKey": 42,
    }));

    let round_tripped: Value = serde_json::from_slice(&serialize(&data)).unwrap();

    assert_eq!(round_tripped["unknownTopLevelKey"], json!(42));
    assert_eq!(
        round_tripped["identities"][4]["guid"],
        json!("a-stable-identifier")
    );
}

/// The shape ContextualIdentityService writes for a container an enterprise
/// policy owns.
fn policy_identity(user_context_id: u32, policy_id: &str) -> Value {
    json!({
        "userContextId": user_context_id, "public": false,
        "name": policy_id, "policy": true, "policyId": policy_id
    })
}

#[test]
fn a_policy_identity_is_loaded_into_its_own_fields() {
    let mut identities = unlabelled_defaults();
    identities.push(thumbnail(false));
    identities.push(policy_identity(6, "corp"));

    let data = load(json!({
        "version": 8,
        "lastUserContextId": 6,
        "identities": identities,
    }));

    let identity = named(&data, "corp");
    assert!(identity.policy);
    assert_eq!(identity.policy_id.as_deref(), Some("corp"));
    assert!(identity.extra.is_empty(), "not round-tripped as unknown");

    assert_eq!(data.policy_identities().count(), 1);
    assert_eq!(data.public_identities().count(), 4);
    // Private, so that "clear all containers" leaves its data alone.
    assert_eq!(data.private_identities().count(), 2);
}

/// Gecko writes a policy identity without an icon and a color, where every
/// other identity it writes has both. The crate always writes them, so a
/// rewritten policy identity gains two empty strings. This pins that: they read
/// back as the empty icon and color the identity already had.
#[test]
fn a_policy_identity_round_trips_with_an_empty_icon_and_color() {
    let mut identities = unlabelled_defaults();
    identities.push(policy_identity(6, "corp"));

    let data = load(json!({
        "version": 8,
        "lastUserContextId": 6,
        "identities": identities,
    }));

    let round_tripped: Value = serde_json::from_slice(&serialize(&data)).unwrap();

    let identity = &round_tripped["identities"][4];
    assert_eq!(
        *identity,
        json!({
            "userContextId": 6, "public": false, "icon": "", "color": "",
            "name": "corp", "policy": true, "policyId": "corp"
        })
    );

    // Nothing but a policy identity carries the flag.
    assert!(round_tripped["identities"][0].get("policy").is_none());
    assert!(round_tripped["identities"][0].get("policyId").is_none());
}

/// A profile that had policy containers before the migration keeps them, with
/// the flag intact and the color resolution applied to everything else.
#[test]
fn migrating_keeps_the_policy_identities() {
    let mut identities = fluent_defaults();
    identities.push(policy_identity(6, "corp"));
    identities.push(custom_identity(7, "turquoise", "Aliased to cyan"));

    let data = load(json!({
        "version": 5,
        "lastUserContextId": 7,
        "identities": identities,
    }));

    assert_eq!(data.version, LATEST_VERSION);
    assert_eq!(named(&data, "Aliased to cyan").color, "cyan");
    assert_eq!(
        data.find_policy_by_id("corp").map(|i| i.user_context_id),
        Some(6)
    );
}

#[test]
fn defaults_seed_a_usable_store() {
    let data = defaults();

    assert_eq!(data.version, LATEST_VERSION);
    assert_eq!(data.public_identities().count(), 4);
    assert_eq!(data.private_identities().count(), 2);
    // The reserved identity is excluded when computing the next available id.
    assert_eq!(data.last_user_context_id, 5);
    assert_eq!(
        data.find_private_by_name(WEBEXT_STORAGE_LOCAL_IDENTITY_NAME)
            .unwrap()
            .user_context_id,
        MAX_USER_CONTEXT_ID
    );
    assert!(data.site_associations.is_empty());
    // The shipped labels are localized, and a localized label is not stored.
    assert!(data
        .public_identities()
        .all(|identity| identity.name.is_none()));
    assert_no_stored_labels(&data);
}

#[test]
fn defaults_round_trip_through_the_format() {
    let data = defaults();
    let reloaded = parse(&serialize(&data)).expect("defaults should reload").0;

    assert_eq!(data, reloaded);
}
