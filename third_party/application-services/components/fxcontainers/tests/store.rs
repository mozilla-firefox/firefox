/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::HashSet;
use std::sync::{Arc, Mutex};

use serde_json::json;

use fxcontainers::{
    max_user_context_id, ContainerColor, ContainerIcon, ContainerLabel, ContainersCallback,
    ContainersStore, DefaultIdentity, StoreError,
};

#[derive(Default)]
struct Recorded {
    persists: Mutex<usize>,
}

#[derive(Clone, Default)]
struct Recorder {
    inner: Arc<Recorded>,
}

impl Recorder {
    fn persist_count(&self) -> usize {
        *self.inner.persists.lock().unwrap()
    }
}

impl ContainersCallback for Recorder {
    fn persist(&self) {
        *self.inner.persists.lock().unwrap() += 1;
    }
}

fn seeded() -> (ContainersStore, Recorder) {
    let recorder = Recorder::default();
    let store = ContainersStore::new(None, None, Box::new(recorder.clone())).unwrap();
    (store, recorder)
}

fn current_document() -> Vec<u8> {
    serde_json::to_vec(&json!({
        "version": 8,
        "lastUserContextId": 5,
        "identities": [
            { "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue" },
            { "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange" },
            { "userContextId": 5, "public": false, "icon": "", "color": "", "name": "userContextIdInternal.thumbnail" },
        ],
        "siteAssociations": {},
    }))
    .unwrap()
}

#[test]
fn seeding_from_defaults_persists_once() {
    let (store, recorder) = seeded();

    assert_eq!(store.public_identities().len(), 4);
    assert_eq!(recorder.persist_count(), 1);
}

#[test]
fn loading_a_current_document_does_not_persist() {
    let recorder = Recorder::default();
    let store =
        ContainersStore::new(Some(current_document()), None, Box::new(recorder.clone())).unwrap();

    assert_eq!(store.public_identities().len(), 2);
    assert_eq!(recorder.persist_count(), 0);
}

#[test]
fn loading_a_migrated_document_persists() {
    let recorder = Recorder::default();
    let document = serde_json::to_vec(&json!({
        "version": 5,
        "lastUserContextId": 1,
        "identities": [
            { "userContextId": 1, "public": true, "icon": "gift", "color": "turquoise", "name": "Aliased" },
        ],
    }))
    .unwrap();

    let store = ContainersStore::new(Some(document), None, Box::new(recorder.clone())).unwrap();

    assert_eq!(
        store.public_identity_from_id(1).unwrap().color,
        Some(ContainerColor::Cyan)
    );
    assert_eq!(recorder.persist_count(), 1);
}

#[test]
fn create_assigns_the_next_id_and_persists() {
    let (store, recorder) = seeded();

    let container = store
        .create("Reading", ContainerIcon::Tree, ContainerColor::Purple)
        .unwrap();

    assert_eq!(container.user_context_id, 6);
    assert_eq!(
        container.label,
        ContainerLabel::Name {
            name: "Reading".into()
        }
    );
    assert!(container.is_public);
    assert_eq!(store.public_identities().len(), 5);
    assert_eq!(recorder.persist_count(), 2);
}

#[test]
fn create_rejects_a_blank_name() {
    let (store, recorder) = seeded();
    let before = recorder.persist_count();

    assert_eq!(
        store.create("   ", ContainerIcon::Tree, ContainerColor::Purple),
        Err(StoreError::EmptyName)
    );
    assert_eq!(store.public_identities().len(), 4);
    assert_eq!(recorder.persist_count(), before);
}

#[test]
fn update_replaces_the_label_of_a_default() {
    let (store, _) = seeded();

    let updated = store
        .update(1, "Mine", ContainerIcon::Fence, ContainerColor::Red)
        .unwrap()
        .unwrap();

    assert_eq!(
        updated.label,
        ContainerLabel::Name {
            name: "Mine".into()
        }
    );
    assert_eq!(updated.icon, Some(ContainerIcon::Fence));
    assert_eq!(updated.color, Some(ContainerColor::Red));

    // The name is what the store keeps, and it wins over the localized label
    // the default identity would otherwise give.
    let persisted: serde_json::Value = serde_json::from_slice(&store.serialize()).unwrap();
    let identity = &persisted["identities"][0];
    assert_eq!(identity["name"], json!("Mine"));
    assert!(identity.get("l10nId").is_none());
    assert_eq!(
        store.public_identity_from_id(1).unwrap().label,
        updated.label
    );
}

/// Mirrors resetDefault: the shipped containers name themselves, and nothing
/// about their label reaches the document.
#[test]
fn the_shipped_labels_are_not_stored() {
    let (store, _) = seeded();

    assert_eq!(
        store
            .public_identities()
            .into_iter()
            .map(|container| container.label)
            .collect::<Vec<_>>(),
        vec![
            ContainerLabel::Personal,
            ContainerLabel::Work,
            ContainerLabel::Banking,
            ContainerLabel::Shopping,
        ]
    );

    let persisted: serde_json::Value = serde_json::from_slice(&store.serialize()).unwrap();
    assert_eq!(persisted["version"], json!(8));
    for identity in persisted["identities"].as_array().unwrap().iter().take(4) {
        assert!(identity.get("l10nId").is_none());
        assert!(identity.get("name").is_none());
    }
}

#[test]
fn update_ignores_unknown_and_private_containers() {
    let (store, _) = seeded();

    assert!(store
        .update(999, "Mine", ContainerIcon::Fence, ContainerColor::Red)
        .unwrap()
        .is_none());
    // The thumbnail identity is private.
    assert!(store
        .update(5, "Mine", ContainerIcon::Fence, ContainerColor::Red)
        .unwrap()
        .is_none());
}

#[test]
fn remove_drops_the_container_and_its_associations() {
    let (store, _) = seeded();
    store.set_site_association("example.org", 1).unwrap();
    store.set_site_association("example.com", 2).unwrap();

    let removed = store.remove(1).expect("container 1 should be removed");

    assert_eq!(removed.user_context_id, 1);
    assert_eq!(store.public_identities().len(), 3);
    assert_eq!(store.get_site_association("example.org"), 0);
    assert_eq!(store.get_site_association("example.com"), 2);
}

#[test]
fn remove_ignores_unknown_and_private_containers() {
    let (store, _) = seeded();

    assert!(store.remove(999).is_none());
    assert!(store.remove(5).is_none());
    assert!(store
        .private_identity("userContextIdInternal.webextStorageLocal")
        .is_some());
}

#[test]
fn move_reorders_public_containers() {
    let (store, _) = seeded();

    assert!(store.move_containers(vec![4], 0));

    assert_eq!(
        store.public_user_context_ids(),
        vec![4, 1, 2, 3],
        "the moved container lands at the front"
    );
}

#[test]
fn move_to_minus_one_appends() {
    let (store, _) = seeded();

    assert!(store.move_containers(vec![1], -1));

    assert_eq!(store.public_user_context_ids(), vec![2, 3, 4, 1]);
}

#[test]
fn move_rejects_positions_below_minus_one() {
    let (store, recorder) = seeded();
    let before = recorder.persist_count();

    assert!(!store.move_containers(vec![1], -2));
    assert_eq!(store.public_user_context_ids(), vec![1, 2, 3, 4]);
    assert_eq!(recorder.persist_count(), before);
}

#[test]
fn move_is_a_no_op_without_matching_containers() {
    let (store, recorder) = seeded();
    let before = recorder.persist_count();

    assert!(!store.move_containers(vec![999], 0));
    assert_eq!(recorder.persist_count(), before);
}

#[test]
fn site_associations_round_trip() {
    let (store, _) = seeded();

    store.set_site_association("Example.ORG", 2).unwrap();

    assert_eq!(store.get_site_association("example.org"), 2);
    assert_eq!(store.get_site_association("other.example"), 0);

    store.remove_site_association("example.org");
    assert_eq!(store.get_site_association("example.org"), 0);
}

#[test]
fn setting_the_same_association_twice_does_not_persist() {
    let (store, recorder) = seeded();
    store.set_site_association("example.org", 2).unwrap();
    let after_first = recorder.persist_count();

    store.set_site_association("example.org", 2).unwrap();

    assert_eq!(recorder.persist_count(), after_first);
}

#[test]
fn associations_require_a_known_public_container() {
    let (store, _) = seeded();

    assert_eq!(
        store.set_site_association("example.org", 999),
        Err(StoreError::NoSuchContainer {
            user_context_id: 999
        })
    );
    // The thumbnail identity is private.
    assert_eq!(
        store.set_site_association("example.org", 5),
        Err(StoreError::NoSuchContainer { user_context_id: 5 })
    );
}

#[test]
fn wildcards_are_not_valid_sites() {
    let (store, _) = seeded();

    assert_eq!(
        store.set_site_association("*.example.org", 1),
        Err(StoreError::InvalidSite)
    );
}

#[test]
fn get_site_associations_filters_by_container() {
    let (store, _) = seeded();
    store.set_site_association("one.example", 1).unwrap();
    store.set_site_association("two.example", 2).unwrap();

    assert_eq!(store.get_site_associations(None).len(), 2);
    assert_eq!(store.get_site_associations(Some(1)).len(), 1);
    assert_eq!(store.get_site_associations(Some(1))[0].site, "one.example");
}

#[test]
fn mutations_are_visible_in_the_serialized_document() {
    let (store, _) = seeded();

    store
        .create("Reading", ContainerIcon::Tree, ContainerColor::Purple)
        .unwrap();

    let persisted: serde_json::Value = serde_json::from_slice(&store.serialize()).unwrap();
    assert_eq!(persisted["lastUserContextId"], json!(6));

    // create() appends, so the new container sits after the system identities.
    let identities = persisted["identities"].as_array().unwrap();
    assert_eq!(identities.last().unwrap()["name"], json!("Reading"));
}

#[test]
fn unset_callback_stops_delivery() {
    let (store, recorder) = seeded();
    let before = recorder.persist_count();

    store.unset_callback();
    store
        .create("Reading", ContainerIcon::Tree, ContainerColor::Purple)
        .unwrap();

    assert_eq!(recorder.persist_count(), before);
    assert_eq!(store.public_identities().len(), 5);
}

fn store_with_last_id(last_user_context_id: u32) -> ContainersStore {
    let document = serde_json::to_vec(&json!({
        "version": 6,
        "lastUserContextId": last_user_context_id,
        "identities": [
            { "userContextId": max_user_context_id(), "public": false, "icon": "", "color": "",
              "name": "userContextIdInternal.webextStorageLocal" },
        ],
    }))
    .unwrap();

    ContainersStore::new(Some(document), None, Box::new(Recorder::default())).unwrap()
}

#[test]
fn the_last_assignable_id_is_the_one_below_the_reserved_one() {
    let store = store_with_last_id(max_user_context_id() - 2);

    let identity = store
        .create("Last one", ContainerIcon::Circle, ContainerColor::Gray)
        .unwrap();

    assert_eq!(identity.user_context_id, max_user_context_id() - 1);
}

#[test]
fn create_fails_once_the_id_space_is_exhausted() {
    let store = store_with_last_id(max_user_context_id() - 1);

    assert_eq!(
        store.create("One too many", ContainerIcon::Circle, ContainerColor::Gray),
        Err(StoreError::IdSpaceExhausted)
    );
    assert!(store.public_identities().is_empty());
}

/// Gecko increments before validating, so there a rejected name leaves a hole
/// in the sequence. This pins the deliberate difference.
#[test]
fn a_rejected_name_does_not_consume_an_id() {
    let (store, _) = seeded();

    assert!(store
        .create("   ", ContainerIcon::Tree, ContainerColor::Purple)
        .is_err());

    assert_eq!(
        store
            .create("Reading", ContainerIcon::Tree, ContainerColor::Purple)
            .unwrap()
            .user_context_id,
        6
    );
}

#[test]
fn an_unknown_container_is_reported_before_an_unusable_site() {
    let (store, _) = seeded();

    assert_eq!(
        store.set_site_association("*.example.org", 999),
        Err(StoreError::NoSuchContainer {
            user_context_id: 999
        })
    );
}

fn reloaded(store: &ContainersStore) -> ContainersStore {
    ContainersStore::new(Some(store.serialize()), None, Box::new(Recorder::default())).unwrap()
}

#[test]
fn every_mutation_leaves_a_document_that_reloads_identically() {
    let (store, _) = seeded();

    store
        .create("Reading", ContainerIcon::Tree, ContainerColor::Purple)
        .unwrap();
    assert_eq!(
        reloaded(&store).public_identities(),
        store.public_identities()
    );

    store
        .update(1, "Mine", ContainerIcon::Fence, ContainerColor::Red)
        .unwrap();
    assert_eq!(
        reloaded(&store).public_identities(),
        store.public_identities()
    );

    store.set_site_association("example.org", 2).unwrap();
    assert_eq!(
        reloaded(&store).get_site_associations(None),
        store.get_site_associations(None)
    );

    assert!(store.move_containers(vec![1], -1));
    assert_eq!(
        reloaded(&store).public_user_context_ids(),
        store.public_user_context_ids()
    );

    // Removing container 2 also drops the association bound to it.
    store.remove(2).unwrap();
    let reloaded = reloaded(&store);
    assert_eq!(reloaded.public_identities(), store.public_identities());
    assert_eq!(
        reloaded.get_site_associations(None),
        store.get_site_associations(None)
    );
}

#[test]
fn concurrent_creates_get_distinct_ids() {
    let store = Arc::new(ContainersStore::new(None, None, Box::new(Recorder::default())).unwrap());

    let handles: Vec<_> = (0..8)
        .map(|i| {
            let store = Arc::clone(&store);
            std::thread::spawn(move || {
                store
                    .create(
                        &format!("Container {i}"),
                        ContainerIcon::Circle,
                        ContainerColor::Gray,
                    )
                    .unwrap()
                    .user_context_id
            })
        })
        .collect();

    let ids: HashSet<u32> = handles
        .into_iter()
        .map(|handle| handle.join().unwrap())
        .collect();

    assert_eq!(ids.len(), 8, "every concurrent create gets its own id");
    assert_eq!(store.public_identities().len(), 12);
}

fn seed(name: &str, icon: ContainerIcon, color: ContainerColor) -> DefaultIdentity {
    DefaultIdentity {
        icon,
        color,
        label: ContainerLabel::Name {
            name: name.to_string(),
        },
    }
}

fn seeded_with(default_identity: DefaultIdentity) -> ContainersStore {
    ContainersStore::new(
        None,
        Some(vec![default_identity]),
        Box::new(Recorder::default()),
    )
    .unwrap()
}

/// An embedder-supplied default names its icon and color through the enums, so
/// there is no longer an unrenderable one to reject.
#[test]
fn a_seed_identity_lands_in_the_store() {
    let store = seeded_with(seed("Work", ContainerIcon::Briefcase, ContainerColor::Cyan));

    let containers = store.public_identities();
    assert_eq!(containers.len(), 1);
    assert_eq!(containers[0].icon, Some(ContainerIcon::Briefcase));
    assert_eq!(containers[0].color, Some(ContainerColor::Cyan));
    assert_eq!(
        containers[0].label,
        ContainerLabel::Name {
            name: "Work".into()
        }
    );
}

/// Mirrors getUserContextLabel: a non-empty name wins, an empty one falls
/// through to the label of the default identity owning the id, and a container
/// no default owns is not left without a label.
#[test]
fn the_label_follows_geckos_precedence() {
    let document = serde_json::to_vec(&json!({
        "version": 8,
        "lastUserContextId": 9,
        "identities": [
            { "userContextId": 1, "public": true, "icon": "cart", "color": "blue",
              "name": "Renamed" },
            { "userContextId": 2, "public": true, "icon": "cart", "color": "blue",
              "name": "" },
            { "userContextId": 3, "public": true, "icon": "cart", "color": "blue" },
            { "userContextId": 9, "public": true, "icon": "cart", "color": "blue" },
        ],
    }))
    .unwrap();

    let store = ContainersStore::new(Some(document), None, Box::new(Recorder::default())).unwrap();
    let label = |id| store.public_identity_from_id(id).unwrap().label;

    assert_eq!(
        label(1),
        ContainerLabel::Name {
            name: "Renamed".into()
        }
    );
    assert_eq!(label(2), ContainerLabel::Work);
    assert_eq!(label(3), ContainerLabel::Banking);
    assert_eq!(
        label(9),
        ContainerLabel::Name {
            name: String::new()
        }
    );
}

/// A version 6 document still holds the Fluent ids from before Bug 2071753
/// renamed them, and a version 7 one holds the renamed ones. Either way the
/// crate never reads them: the labels come from the default identities.
/// Mirrors test_migratedFile.js's migratedFileV6.
#[test]
fn a_version_6_document_takes_the_labels_from_the_defaults() {
    let document = serde_json::to_vec(&json!({
        "version": 6,
        "lastUserContextId": 5,
        "identities": [
            { "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue", "l10nId": "user-context-personal" },
            { "userContextId": 2, "public": true, "icon": "briefcase", "color": "orange", "l10nId": "user-context-work" },
            { "userContextId": 3, "public": true, "icon": "dollar", "color": "green", "l10nId": "user-context-banking" },
            { "userContextId": 4, "public": true, "icon": "cart", "color": "pink", "l10nId": "user-context-shopping" },
            { "userContextId": 5, "public": true, "icon": "cart", "color": "pink", "name": "Custom user-created identity" },
        ],
    }))
    .unwrap();

    let recorder = Recorder::default();
    let store = ContainersStore::new(Some(document), None, Box::new(recorder.clone())).unwrap();

    assert_eq!(
        store
            .public_identities()
            .into_iter()
            .map(|container| container.label)
            .collect::<Vec<_>>(),
        vec![
            ContainerLabel::Personal,
            ContainerLabel::Work,
            ContainerLabel::Banking,
            ContainerLabel::Shopping,
            ContainerLabel::Name {
                name: "Custom user-created identity".into()
            },
        ]
    );

    // The migration is written back, without the Fluent ids.
    assert_eq!(recorder.persist_count(), 1);
    let persisted: serde_json::Value = serde_json::from_slice(&store.serialize()).unwrap();
    assert_eq!(persisted["version"], json!(8));
    assert!(persisted["identities"]
        .as_array()
        .unwrap()
        .iter()
        .all(|identity| identity.get("l10nId").is_none()));
}

/// A version 7 document holds the renamed Fluent ids, and a default the user
/// renamed. Mirrors test_migratedFile.js's migratedFileV7.
#[test]
fn a_version_7_document_keeps_a_renamed_default() {
    let document = serde_json::to_vec(&json!({
        "version": 7,
        "lastUserContextId": 5,
        "identities": [
            { "userContextId": 1, "public": true, "icon": "fingerprint", "color": "blue", "l10nId": "user-context-personal2" },
            { "userContextId": 4, "public": true, "icon": "cart", "color": "pink", "name": "Renamed default" },
            { "userContextId": 5, "public": true, "icon": "cart", "color": "pink", "name": "Custom user-created identity" },
        ],
    }))
    .unwrap();

    let store = ContainersStore::new(Some(document), None, Box::new(Recorder::default())).unwrap();
    let label = |id| store.public_identity_from_id(id).unwrap().label;

    assert_eq!(label(1), ContainerLabel::Personal);
    assert_eq!(
        label(4),
        ContainerLabel::Name {
            name: "Renamed default".into()
        }
    );
    assert_eq!(
        label(5),
        ContainerLabel::Name {
            name: "Custom user-created identity".into()
        }
    );
}

/// The default identities label the containers whether or not the store was
/// seeded from them, as gecko's _defaultIdentities do.
#[test]
fn the_default_identities_label_a_stored_document() {
    let store = ContainersStore::new(
        Some(current_document()),
        Some(vec![
            seed("Corp", ContainerIcon::Briefcase, ContainerColor::Orange),
            seed(
                "Corp private",
                ContainerIcon::Fingerprint,
                ContainerColor::Blue,
            ),
        ]),
        Box::new(Recorder::default()),
    )
    .unwrap();

    assert_eq!(
        store.public_identity_from_id(1).unwrap().label,
        ContainerLabel::Name {
            name: "Corp".into()
        }
    );
    assert_eq!(
        store.public_identity_from_id(2).unwrap().label,
        ContainerLabel::Name {
            name: "Corp private".into()
        }
    );
    // The shipped labels no longer apply: they belong to the shipped set.
    assert!(store
        .public_identities()
        .iter()
        .all(|container| matches!(container.label, ContainerLabel::Name { .. })));
}

/// Mirrors ContextualIdentityService.createForPolicy: not public, named after
/// the policy id, and carrying it.
#[test]
fn create_for_policy_takes_the_next_id_and_is_not_public() {
    let (store, recorder) = seeded();

    let container = store.create_for_policy("corp").unwrap();

    assert_eq!(container.user_context_id, 6);
    assert!(!container.is_public);
    assert_eq!(container.policy_id.as_deref(), Some("corp"));
    assert_eq!(
        container.label,
        ContainerLabel::Name {
            name: "corp".into()
        }
    );
    assert_eq!(container.icon, None);
    assert_eq!(container.color, None);
    assert_eq!(recorder.persist_count(), 2);

    assert_eq!(store.policy_identities(), vec![container]);
    assert_eq!(store.public_identities().len(), 4);
    assert!(store.public_identity_from_id(6).is_none());

    // Kept alongside the system identities, so that a "clear all containers"
    // leaves the data of a policy container alone.
    assert!(store.private_user_context_ids().contains(&6));
}

#[test]
fn create_for_policy_rejects_a_blank_policy_id() {
    let (store, recorder) = seeded();
    let before = recorder.persist_count();

    assert_eq!(
        store.create_for_policy("   "),
        Err(StoreError::EmptyPolicyId)
    );
    assert!(store.policy_identities().is_empty());
    assert_eq!(recorder.persist_count(), before);
}

#[test]
fn policy_and_user_containers_share_the_id_counter() {
    let (store, _) = seeded();

    assert_eq!(store.create_for_policy("corp").unwrap().user_context_id, 6);
    assert_eq!(
        store
            .create("Reading", ContainerIcon::Tree, ContainerColor::Purple)
            .unwrap()
            .user_context_id,
        7
    );
    assert_eq!(store.create_for_policy("other").unwrap().user_context_id, 8);
}

#[test]
fn create_for_policy_fails_once_the_id_space_is_exhausted() {
    let store = store_with_last_id(max_user_context_id() - 1);

    assert_eq!(
        store.create_for_policy("corp"),
        Err(StoreError::IdSpaceExhausted)
    );
    assert!(store.policy_identities().is_empty());
}

#[test]
fn a_policy_container_is_out_of_reach_of_the_public_api() {
    let (store, _) = seeded();
    let container = store.create_for_policy("corp").unwrap();
    let id = container.user_context_id;

    assert!(store
        .update(id, "Mine", ContainerIcon::Fence, ContainerColor::Red)
        .unwrap()
        .is_none());
    assert!(store.remove(id).is_none());
    assert!(!store.move_containers(vec![id], 0));
    assert_eq!(
        store.set_site_association("example.org", id),
        Err(StoreError::NoSuchContainer {
            user_context_id: id
        })
    );

    assert_eq!(store.policy_identities(), vec![container]);
}

#[test]
fn remove_policy_identity_only_takes_policy_containers() {
    let (store, _) = seeded();
    let id = store.create_for_policy("corp").unwrap().user_context_id;

    // A public container, and the thumbnail identity: neither is a policy one.
    assert!(store.remove_policy_identity(1).is_none());
    assert!(store.remove_policy_identity(5).is_none());
    assert!(store.remove_policy_identity(999).is_none());

    let removed = store
        .remove_policy_identity(id)
        .expect("the policy container should be removed");

    assert_eq!(removed.policy_id.as_deref(), Some("corp"));
    assert!(store.policy_identities().is_empty());
    assert_eq!(store.public_identities().len(), 4);
    assert!(store.public_identity_from_id(1).is_some());
}

/// How Policies.sys.mjs reconciles the policy against what is stored.
#[test]
fn policy_identity_looks_up_by_policy_id() {
    let (store, _) = seeded();
    let id = store.create_for_policy("corp").unwrap().user_context_id;

    assert_eq!(store.policy_identity("corp").unwrap().user_context_id, id);
    assert!(store.policy_identity("unknown").is_none());
    // The name matches, but a public container is not a policy one.
    store
        .create("corp", ContainerIcon::Tree, ContainerColor::Purple)
        .unwrap();
    assert_eq!(store.policy_identity("corp").unwrap().user_context_id, id);
}

/// A policy names its container whatever it likes, and that must not let it
/// stand in for one of the identities Gecko looks up by name.
#[test]
fn a_policy_container_does_not_shadow_a_system_identity() {
    let (store, _) = seeded();

    store
        .create_for_policy("userContextIdInternal.thumbnail")
        .unwrap();

    let thumbnail = store
        .private_identity("userContextIdInternal.thumbnail")
        .expect("the system identity should still be found");

    assert_eq!(thumbnail.user_context_id, 5);
    assert_eq!(thumbnail.policy_id, None);
}

#[test]
fn a_policy_container_reloads_identically() {
    let (store, _) = seeded();
    store.create_for_policy("corp").unwrap();

    let persisted: serde_json::Value = serde_json::from_slice(&store.serialize()).unwrap();
    let identity = persisted["identities"].as_array().unwrap().last().unwrap();
    assert_eq!(identity["policy"], json!(true));
    assert_eq!(identity["policyId"], json!("corp"));
    assert_eq!(identity["public"], json!(false));
    assert_eq!(identity["name"], json!("corp"));

    // Nothing but a policy container carries the flag.
    assert!(persisted["identities"][0].get("policy").is_none());
    assert!(persisted["identities"][0].get("policyId").is_none());

    let reloaded = reloaded(&store);
    assert_eq!(reloaded.policy_identities(), store.policy_identities());
    assert_eq!(reloaded.public_identities(), store.public_identities());
    assert_eq!(
        reloaded.private_user_context_ids(),
        store.private_user_context_ids()
    );
}
